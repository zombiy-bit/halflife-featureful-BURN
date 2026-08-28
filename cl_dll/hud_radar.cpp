/***
*
*  Half-Life Featureful - CS 1.6 style radar
*
*  Radar contents:
*    - local player
*    - enemy players
*    - NPCs that are actually seen, or NPCs that recently damaged the player
*    - game_radar_hint objective markers
*
***/

#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "radar_protocol.h"
#include "pm_defs.h"
#include "event_api.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace
{
constexpr float RADAR_NPC_MEMORY_TIME = 5.0f;
constexpr float RADAR_DEFAULT_RANGE = 1600.0f;
constexpr float RADAR_DEFAULT_SIZE = 1.0f;
constexpr float RADAR_DEFAULT_OUTLINE = 2.0f;
constexpr float RADAR_MIN_SIZE = 0.35f;
constexpr float RADAR_MAX_SIZE = 3.0f;
constexpr float RADAR_MIN_OUTLINE = 1.0f;
constexpr float RADAR_MAX_OUTLINE = 12.0f;
constexpr float RADAR_PI = 3.14159265358979323846f;
}

static CHudRadar* g_pRadar = nullptr;

static void RadarCommandOn()
{
	gEngfuncs.Cvar_SetValue("cl_radar", 1.0f);
}

static void RadarCommandOff()
{
	gEngfuncs.Cvar_SetValue("cl_radar", 0.0f);
}

static void RadarCommandToggle()
{
	const float enabled = CVAR_GET_FLOAT("cl_radar");
	gEngfuncs.Cvar_SetValue("cl_radar", enabled != 0.0f ? 0.0f : 1.0f);
}

int __MsgFunc_RadarData(const char* pszName, int iSize, void* pbuf)
{
	if (!g_pRadar)
		return 0;
	return g_pRadar->MsgFunc_RadarData(pszName, iSize, pbuf);
}

int CHudRadar::Init()
{
	m_pCvarEnabled = CVAR_CREATE("cl_radar", "1", FCVAR_ARCHIVE);
	m_pCvarRotate = CVAR_CREATE("cl_radar_rotate", "1", FCVAR_ARCHIVE);
	m_pCvarRange = CVAR_CREATE("cl_radar_range", "1600", FCVAR_ARCHIVE);
	m_pCvarSize = CVAR_CREATE("cl_radar_size", "1", FCVAR_ARCHIVE);
	m_pCvarOutline = CVAR_CREATE("cl_radar_outline", "2", FCVAR_ARCHIVE);

	HOOK_MESSAGE(RadarData);

	gEngfuncs.pfnAddCommand("radar_on", RadarCommandOn);
	gEngfuncs.pfnAddCommand("radar_off", RadarCommandOff);
	gEngfuncs.pfnAddCommand("radar_toggle", RadarCommandToggle);

	m_iFlags |= HUD_ACTIVE;
	g_pRadar = this;
	gHUD.AddHudElem(this);
	return 1;
}

int CHudRadar::VidInit()
{
	ResetMarkers();
	m_initialSyncPending = true;
	m_suppressVisibilitySounds = false;
	return 1;
}

void CHudRadar::Reset()
{
	ResetMarkers();
	m_initialSyncPending = true;
}

void CHudRadar::ResetMarkers()
{
	m_npcs.clear();
	m_hints.clear();
	m_snapshotOpen = false;
	m_snapshotSilent = false;
	m_currentNpcSnapshot = 0;
	m_currentHintSnapshot = 0;
}

void CHudRadar::PlayRadarSound(int kind)
{
	const char* sound = "hints/neutral.wav";

	switch (kind)
	{
	case RADAR_MARKER_HINT_MAIN:
		sound = "hints/main.wav";
		break;
	case RADAR_MARKER_HINT_SECONDARY:
		sound = "hints/second.wav";
		break;
	case RADAR_MARKER_HOSTILE:
		sound = "hints/hostile.wav";
		break;
	case RADAR_MARKER_NEUTRAL:
	default:
		sound = "hints/neutral.wav";
		break;
	}

	gEngfuncs.pfnPlaySoundByName(const_cast<char*>(sound), 1.0f);
}

void CHudRadar::HandleSnapshotBegin(int generation, int flags)
{
	m_snapshotOpen = true;
	m_snapshotGeneration = generation;
	m_snapshotSilent = (flags & RADAR_SNAPSHOT_SILENT) != 0;
	if (m_snapshotSilent)
		m_suppressVisibilitySounds = true;
	m_currentNpcSnapshot = generation;
	m_currentHintSnapshot = generation;

	for (auto& pair : m_npcs)
		pair.second.snapshotSeen = 0;

	for (auto& pair : m_hints)
		pair.second.snapshotSeen = 0;
}

void CHudRadar::HandleNpcInfo(int entindex, int kind)
{
	if (entindex <= 0)
		return;

	CHudRadar::NpcState& state = m_npcs[entindex];
	state.kind = kind;
	state.snapshotSeen = m_currentNpcSnapshot;
}

void CHudRadar::HandleNpcDamageReveal(int entindex, int kind, const Vector& origin)
{
	if (entindex <= 0)
		return;

	CHudRadar::NpcState& state = m_npcs[entindex];
	const bool wasActive = state.active;

	state.kind = kind;
	state.lastKnown = origin;
	state.lastRefresh = gHUD.m_flTime;
	state.active = true;
	state.snapshotSeen = m_currentNpcSnapshot;

	if (!wasActive && !m_suppressVisibilitySounds)
		PlayRadarSound(kind);
}

void CHudRadar::HandleNpcRemove(int entindex)
{
	m_npcs.erase(entindex);
}

void CHudRadar::HandleHintSync(int entindex, int kind, const Vector& origin)
{
	if (entindex <= 0)
		return;

	CHudRadar::HintState& state = m_hints[entindex];
	state.kind = kind;
	state.origin = origin;
	state.active = true;
	state.snapshotSeen = m_currentHintSnapshot;
}

void CHudRadar::HandleHintEvent(int entindex, int kind, bool enabled, const Vector& origin, bool playSound)
{
	if (entindex <= 0)
		return;

	CHudRadar::HintState& state = m_hints[entindex];
	const bool wasActive = state.active;

	state.kind = kind;
	state.origin = origin;
	state.active = enabled;

	if (enabled && !wasActive && playSound)
		PlayRadarSound(kind);
}

void CHudRadar::HandleSnapshotEnd(int generation)
{
	if (!m_snapshotOpen)
		return;

	if (generation != m_snapshotGeneration)
		return;

	for (auto it = m_npcs.begin(); it != m_npcs.end(); )
	{
		if (it->second.snapshotSeen != generation)
			it = m_npcs.erase(it);
		else
			++it;
	}

	for (auto it = m_hints.begin(); it != m_hints.end(); )
	{
		if (it->second.snapshotSeen != generation)
			it = m_hints.erase(it);
		else
			++it;
	}

	if (m_snapshotSilent)
	{
		m_snapshotSilent = false;
		m_snapshotOpen = false;
		return;
	}

	m_snapshotOpen = false;
}

int CHudRadar::MsgFunc_RadarData(const char* pszName, int iSize, void* pbuf)
{
	BEGIN_READ(pbuf, iSize);
	const int opcode = READ_BYTE();

	switch (opcode)
	{
	case RADAR_MSG_SNAPSHOT_BEGIN:
		HandleSnapshotBegin(READ_SHORT(), READ_BYTE());
		break;

	case RADAR_MSG_NPC_INFO:
		HandleNpcInfo(READ_SHORT(), READ_BYTE());
		break;

	case RADAR_MSG_NPC_DAMAGE_REVEAL:
	{
		const int entindex = READ_SHORT();
		const int kind = READ_BYTE();
		const Vector origin(READ_COORD(), READ_COORD(), READ_COORD());
		HandleNpcDamageReveal(entindex, kind, origin);
		break;
	}

	case RADAR_MSG_NPC_REMOVE:
		HandleNpcRemove(READ_SHORT());
		break;

	case RADAR_MSG_HINT_SYNC:
	{
		const int entindex = READ_SHORT();
		const int kind = READ_BYTE();
		const Vector origin(READ_COORD(), READ_COORD(), READ_COORD());
		HandleHintSync(entindex, kind, origin);
		break;
	}

	case RADAR_MSG_HINT_EVENT:
	{
		const int entindex = READ_SHORT();
		const int kind = READ_BYTE();
		const bool enabled = READ_BYTE() != 0;
		const Vector origin(READ_COORD(), READ_COORD(), READ_COORD());
		const bool playSound = READ_BYTE() != 0;
		HandleHintEvent(entindex, kind, enabled, origin, playSound);
		break;
	}

	case RADAR_MSG_SNAPSHOT_END:
		HandleSnapshotEnd(READ_SHORT());
		break;

	default:
		break;
	}

	return 1;
}

bool CHudRadar::IsPointInView(const Vector& eyePosition, const Vector& targetPosition, float fovDegrees) const
{
	Vector viewAngles = gHUD.m_vecAngles;
	const float pitch = viewAngles.x * RADAR_PI / 180.0f;
	const float yaw = viewAngles.y * RADAR_PI / 180.0f;

	Vector forward(
		std::cos(pitch) * std::cos(yaw),
		std::cos(pitch) * std::sin(yaw),
		-std::sin(pitch));

	Vector toTarget = targetPosition - eyePosition;
	const float length = std::sqrt(DotProduct(toTarget, toTarget));
	if (length <= 0.01f)
		return true;

	toTarget = toTarget * (1.0f / length);
	const float dot = DotProduct(forward, toTarget);
	const float halfFov = std::max(1.0f, fovDegrees) * 0.5f * RADAR_PI / 180.0f;
	return dot >= std::cos(halfFov);
}

bool CHudRadar::HasLineOfSight(const Vector& eyePosition, const Vector& targetPosition) const
{
	pmtrace_t trace;
	Vector start = eyePosition;
	Vector end = targetPosition;

	gEngfuncs.pEventAPI->EV_PushPMStates();
	gEngfuncs.pEventAPI->EV_SetTraceHull(2);
	gEngfuncs.pEventAPI->EV_PlayerTrace((float*)&start, (float*)&end,
		PM_STUDIO_BOX | PM_WORLD_ONLY, -1, &trace);
	gEngfuncs.pEventAPI->EV_PopPMStates();

	return trace.fraction >= 0.999f;
}

bool CHudRadar::IsNpcVisible(cl_entity_t* localPlayer, cl_entity_t* npc) const
{
	if (!localPlayer || !npc)
		return false;

	if (npc->curstate.effects & EF_NODRAW)
		return false;

	const float distance2D = (npc->origin - localPlayer->origin).Length2D();
	const float radarRange = std::max(1.0f, m_pCvarRange ? m_pCvarRange->value : RADAR_DEFAULT_RANGE);
	if (distance2D > radarRange)
		return false;

	const Vector eyePosition = localPlayer->origin + Vector(0, 0, 28);
	const Vector targetPosition = npc->origin + Vector(0, 0, 32);
	const float fov = gHUD.m_iFOV > 0 ? static_cast<float>(gHUD.m_iFOV) : 90.0f;

	if (!IsPointInView(eyePosition, targetPosition, fov))
		return false;

	return HasLineOfSight(eyePosition, targetPosition);
}

void CHudRadar::UpdateNpcVisibility(cl_entity_t* localPlayer, float flTime)
{
	for (auto& pair : m_npcs)
	{
		const int entindex = pair.first;
		CHudRadar::NpcState& state = pair.second;
		cl_entity_t* npc = gEngfuncs.GetEntityByIndex(entindex);

		if (!npc)
			continue;

		if (IsNpcVisible(localPlayer, npc))
		{
			const bool wasActive = state.active;
			state.lastKnown = npc->origin;
			state.lastRefresh = flTime;
			state.active = true;

			if (!wasActive && !m_suppressVisibilitySounds && !m_initialSyncPending)
				PlayRadarSound(state.kind);
		}
	}
}

void CHudRadar::SeedVisibleNpcsSilently(cl_entity_t* localPlayer, float flTime)
{
	for (auto& pair : m_npcs)
	{
		CHudRadar::NpcState& state = pair.second;
		cl_entity_t* npc = gEngfuncs.GetEntityByIndex(pair.first);
		if (!npc)
			continue;

		if (IsNpcVisible(localPlayer, npc))
		{
			state.lastKnown = npc->origin;
			state.lastRefresh = flTime;
			state.active = true;
		}
	}

	m_initialSyncPending = false;
}

void CHudRadar::RemoveStaleMarkers(float flTime)
{
	for (auto& pair : m_npcs)
	{
		CHudRadar::NpcState& state = pair.second;
		if (state.active && flTime - state.lastRefresh > RADAR_NPC_MEMORY_TIME)
			state.active = false;
	}
}

void CHudRadar::DrawPlayerMarker(int x, int y, int size) const
{
	int r, g, b;
	UnpackRGB(r, g, b, gHUD.HUDColor());

	const int radius = std::max(2, 3 * size);
	FillRGBA(x - radius, y - radius, radius * 2 + 1, radius * 2 + 1, r, g, b, 255);
}

void CHudRadar::DrawDiamond(int x, int y, int r, int g, int b, int alpha, int size) const
{
	const int s = std::max(1, size);
	FillRGBA(x, y - 3 * s, 1 * s, 7 * s, r, g, b, alpha);
	FillRGBA(x - 1 * s, y - 2 * s, 3 * s, 5 * s, r, g, b, alpha);
	FillRGBA(x - 2 * s, y - 1 * s, 5 * s, 3 * s, r, g, b, alpha);
}

void CHudRadar::DrawFrame(int x, int y, int size, int thickness, int r, int g, int b, int alpha) const
{
	const int t = std::max(1, thickness);
	FillRGBA(x, y, size, t, r, g, b, alpha);
	FillRGBA(x, y + size - t, size, t, r, g, b, alpha);
	FillRGBA(x, y, t, size, r, g, b, alpha);
	FillRGBA(x + size - t, y, t, size, r, g, b, alpha);
}

void CHudRadar::WorldToRadar(const Vector& worldPosition, const Vector& playerPosition,
	float yaw, bool rotateRadar, float range, int radarX, int radarY, int radarSize,
	int& outX, int& outY) const
{
	const float dx = worldPosition.x - playerPosition.x;
	const float dy = worldPosition.y - playerPosition.y;

	float side = dx;
	float forward = dy;
	if (rotateRadar)
	{
		forward = dx * std::cos(yaw) + dy * std::sin(yaw);
		side = -dx * std::sin(yaw) + dy * std::cos(yaw);
	}

	const int centerX = radarX + radarSize / 2;
	const int centerY = radarY + radarSize / 2;
	const int radius = radarSize / 2 - 5;

	outX = centerX + static_cast<int>((side / range) * radius);
	outY = centerY - static_cast<int>((forward / range) * radius);

	outX = std::max(radarX + 4, std::min(radarX + radarSize - 5, outX));
	outY = std::max(radarY + 4, std::min(radarY + radarSize - 5, outY));
}

int CHudRadar::Draw(float flTime)
{
	if (!m_pCvarEnabled || m_pCvarEnabled->value == 0.0f)
		return 1;

	if (gHUD.m_iIntermission || (gHUD.m_iHideHUDDisplay & HIDEHUD_ALL))
		return 1;

	cl_entity_t* localPlayer = gEngfuncs.GetLocalPlayer();
	if (!localPlayer)
		return 1;

	const int screenWidth = ScreenWidth;
	const int screenHeight = ScreenHeight;
	if (screenWidth <= 0 || screenHeight <= 0)
		return 1;

	const int shortSide = std::min(screenWidth, screenHeight);
	const float sizeMultiplier = std::max(RADAR_MIN_SIZE,
		std::min(RADAR_MAX_SIZE, m_pCvarSize ? m_pCvarSize->value : RADAR_DEFAULT_SIZE));
	const int baseSize = std::max(120, std::min(260, shortSide * 23 / 100));
	const int radarSize = std::max(80, std::min(static_cast<int>(shortSide * 0.65f),
		static_cast<int>(baseSize * sizeMultiplier)));
	const int radarX = std::max(8, screenWidth * 2 / 100);
	const int radarY = std::max(8, screenHeight * 2 / 100);
	const int centerX = radarX + radarSize / 2;
	const int centerY = radarY + radarSize / 2;

	int hudR, hudG, hudB;
	UnpackRGB(hudR, hudG, hudB, gHUD.HUDColor());

	FillRGBA(radarX, radarY, radarSize, radarSize, 0, 0, 0, 115);
	const int outline = static_cast<int>(std::max(RADAR_MIN_OUTLINE,
		std::min(RADAR_MAX_OUTLINE, m_pCvarOutline ? m_pCvarOutline->value : RADAR_DEFAULT_OUTLINE)) * sizeMultiplier);
	DrawFrame(radarX, radarY, radarSize, outline, hudR, hudG, hudB, 210);

	DrawPlayerMarker(centerX, centerY, std::max(1, static_cast<int>(sizeMultiplier)));

	const float range = std::max(1.0f, m_pCvarRange ? m_pCvarRange->value : RADAR_DEFAULT_RANGE);
	const float yaw = localPlayer->angles.y * RADAR_PI / 180.0f;
	const bool rotateRadar = m_pCvarRotate && m_pCvarRotate->value != 0.0f;

	if (m_initialSyncPending && !m_snapshotOpen)
		SeedVisibleNpcsSilently(localPlayer, flTime);

	UpdateNpcVisibility(localPlayer, flTime);

	RemoveStaleMarkers(flTime);
	m_suppressVisibilitySounds = false;

	int localIndex = 0;
	for (int i = 1; i <= MAX_PLAYERS; ++i)
	{
		if (g_PlayerInfoList[i].thisplayer)
		{
			localIndex = i;
			break;
		}
	}

	int localTeam = 0;
	if (localIndex > 0)
		localTeam = g_PlayerExtraInfo[localIndex].teamnumber;
	if (localTeam <= 0)
		localTeam = g_iTeamNumber;

	// Player blips: exactly the old CS-style red enemy marker behaviour.
	for (int i = 1; i <= MAX_PLAYERS; ++i)
	{
		if (i == localIndex || g_IsSpectator[i])
			continue;
		if (!g_PlayerInfoList[i].name || !g_PlayerInfoList[i].name[0])
			continue;

		cl_entity_t* player = gEngfuncs.GetEntityByIndex(i);
		if (!player || (player->curstate.effects & EF_NODRAW))
			continue;

		const int playerTeam = g_PlayerExtraInfo[i].teamnumber;
		if (gHUD.m_Teamplay && localTeam > 0 && playerTeam > 0 && playerTeam == localTeam)
			continue;

		int markerX, markerY;
		WorldToRadar(player->origin, localPlayer->origin, yaw, rotateRadar, range,
			radarX, radarY, radarSize, markerX, markerY);
		DrawDiamond(markerX, markerY, 255, 35, 35, 255, std::max(1, static_cast<int>(sizeMultiplier)));
	}

	// NPC markers: use only the last known position.
	for (const auto& pair : m_npcs)
	{
		const CHudRadar::NpcState& state = pair.second;
		if (!state.active)
			continue;

		if (flTime - state.lastRefresh > RADAR_NPC_MEMORY_TIME)
			continue;

		int markerX, markerY;
		WorldToRadar(state.lastKnown, localPlayer->origin, yaw, rotateRadar, range,
			radarX, radarY, radarSize, markerX, markerY);

		if (state.kind == RADAR_MARKER_HOSTILE)
			DrawDiamond(markerX, markerY, 255, 35, 35, 255, std::max(1, static_cast<int>(sizeMultiplier)));
		else
			DrawDiamond(markerX, markerY, 165, 165, 165, 255, std::max(1, static_cast<int>(sizeMultiplier)));
	}

	// Objective hints are global and are always shown while enabled.
	for (const auto& pair : m_hints)
	{
		const CHudRadar::HintState& state = pair.second;
		if (!state.active)
			continue;

		int markerX, markerY;
		WorldToRadar(state.origin, localPlayer->origin, yaw, rotateRadar, range,
			radarX, radarY, radarSize, markerX, markerY);

		if (state.kind == RADAR_MARKER_HINT_SECONDARY)
			DrawDiamond(markerX, markerY, 0, 120, 0, 255, std::max(1, static_cast<int>(sizeMultiplier)));
		else
			DrawDiamond(markerX, markerY, 0, 255, 0, 255, std::max(1, static_cast<int>(sizeMultiplier)));
	}

	return 1;
}

// Static object is owned by CHud; this is kept only for the user-message bridge.
// The actual CHud member is initialized from hud.cpp as gHUD.m_Radar.
