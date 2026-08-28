/***
*
*  Half-Life Featureful - radar server support
*
*  Adds:
*    - game_radar_hint
*    - server -> client radar snapshots
*    - NPC hostile/neutral classification
*    - NPC damage reveal
*
***/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "player.h"
#include "game_radar_hint.h"

#include "radar_protocol.h"

static int gmsgRadarData = 0;
static bool g_radarSilentSync[65] = {};

// Spawnflag 1: Enabled on spawn.
#define SF_GAME_RADAR_HINT_START_ON 1

enum GameRadarHintType
{
	GAME_RADAR_HINT_MAIN = 0,
	GAME_RADAR_HINT_SECONDARY = 1
};

class CGameRadarHint : public CPointEntity
{
public:
	void Spawn() override;
	void Precache() override;
	void KeyValue(KeyValueData* pkvd) override;
	void Use(CBaseEntity* pActivator, CBaseEntity* pCaller, USE_TYPE useType, float value) override;

	int Save(CSave& save) override;
	int Restore(CRestore& restore) override;
	static TYPEDESCRIPTION m_SaveData[];

	bool IsEnabled() const { return m_bEnabled; }
	int MarkerKind() const
	{
		return m_iHintType == GAME_RADAR_HINT_SECONDARY
			? RADAR_MARKER_HINT_SECONDARY
			: RADAR_MARKER_HINT_MAIN;
	}

	void SendStateToAll(bool playSound);

private:
	int m_iHintType = GAME_RADAR_HINT_MAIN;
	bool m_bEnabled = false;
};

LINK_ENTITY_TO_CLASS(game_radar_hint, CGameRadarHint)

TYPEDESCRIPTION CGameRadarHint::m_SaveData[] =
{
	DEFINE_FIELD(CGameRadarHint, m_iHintType, FIELD_INTEGER),
	DEFINE_FIELD(CGameRadarHint, m_bEnabled, FIELD_BOOLEAN),
};

IMPLEMENT_SAVERESTORE(CGameRadarHint, CPointEntity)

void Radar_PrecacheSounds()
{
	PRECACHE_SOUND("hints/main.wav");
	PRECACHE_SOUND("hints/second.wav");
	PRECACHE_SOUND("hints/hostile.wav");
	PRECACHE_SOUND("hints/neutral.wav");
}

void CGameRadarHint::Precache()
{
	Radar_PrecacheSounds();
}

void CGameRadarHint::KeyValue(KeyValueData* pkvd)
{
	if (FStrEq(pkvd->szKeyName, "hint_type") || FStrEq(pkvd->szKeyName, "type"))
	{
		const char* value = pkvd->szValue;
		if (FStrEq(value, "secondary") || FStrEq(value, "second") || atoi(value) == 1)
			m_iHintType = GAME_RADAR_HINT_SECONDARY;
		else
			m_iHintType = GAME_RADAR_HINT_MAIN;

		pkvd->fHandled = true;
		return;
	}

	CPointEntity::KeyValue(pkvd);
}

void CGameRadarHint::Spawn()
{
	Precache();

	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	pev->effects |= EF_NODRAW;

	m_bEnabled = FBitSet(pev->spawnflags, SF_GAME_RADAR_HINT_START_ON);
	SetUse(&CGameRadarHint::Use);
}

//int CGameRadarHint::Save(CSave& save)
//{
//	if (!CPointEntity::Save(save))
//		return 0;
//
//	return save.WriteFields("CGameRadarHint", this, m_SaveData, ARRAYSIZE(m_SaveData));
//}

//int CGameRadarHint::Restore(CRestore& restore)
//{
//	if (!CPointEntity::Restore(restore))
//		return 0;
//
//	if (!restore.ReadFields("CGameRadarHint", this, m_SaveData, ARRAYSIZE(m_SaveData)))
//		return 0;
//
//	pev->solid = SOLID_NOT;
//	pev->movetype = MOVETYPE_NONE;
//	pev->effects |= EF_NODRAW;
//	SetUse(&CGameRadarHint::Use);
//	return 1;
//}

void CGameRadarHint::Use(CBaseEntity* pActivator, CBaseEntity* pCaller, USE_TYPE useType, float value)
{
	bool newState = m_bEnabled;

	if (useType == USE_ON)
		newState = true;
	else if (useType == USE_OFF)
		newState = false;
	else if (useType == USE_TOGGLE)
		newState = !m_bEnabled;
	else
		newState = value != 0.0f;

	if (newState == m_bEnabled)
		return;

	m_bEnabled = newState;
	SendStateToAll(true);
}

void CGameRadarHint::SendStateToAll(bool playSound)
{
	if (!gmsgRadarData)
		return;

	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		CBaseEntity* pPlayerEntity = UTIL_PlayerByIndex(i);
		CBasePlayer* pPlayer = static_cast<CBasePlayer*>(pPlayerEntity);
		if (!pPlayer)
			continue;

		MESSAGE_BEGIN(MSG_ONE, gmsgRadarData, nullptr, ENT(pPlayer->pev));
			WRITE_BYTE(RADAR_MSG_HINT_EVENT);
			WRITE_SHORT(entindex());
			WRITE_BYTE(MarkerKind());
			WRITE_BYTE(m_bEnabled ? 1 : 0);
			WRITE_COORD(pev->origin.x);
			WRITE_COORD(pev->origin.y);
			WRITE_COORD(pev->origin.z);
			WRITE_BYTE(playSound ? 1 : 0);
		MESSAGE_END();
	}
}

static bool RadarIsAliveMonster(CBaseEntity* pEntity, CBaseMonster*& pMonster)
{
	pMonster = nullptr;
	if (!pEntity || pEntity->IsPlayer())
		return false;

	if (!FBitSet(pEntity->pev->flags, FL_MONSTER))
		return false;

	pMonster = pEntity->MyMonsterPointer();
	if (!pMonster)
		return false;

	if (!pMonster->IsFullyAlive())
		return false;

	if (pEntity->pev->effects & EF_NODRAW)
		return false;

	return true;
}

static int RadarMonsterKind(CBaseMonster* pMonster, CBasePlayer* pPlayer)
{
	if (!pMonster || !pPlayer)
		return RADAR_MARKER_NEUTRAL;

	const int relationship = pMonster->IRelationship(pPlayer);

	// Featureful relationship values: dislike/hate/nemesis are hostile.
	if (relationship == R_DL || relationship == R_HT || relationship == R_NM)
		return RADAR_MARKER_HOSTILE;

	return RADAR_MARKER_NEUTRAL;
}

static void RadarWriteSnapshotForPlayer(CBasePlayer* pPlayer, bool silent)
{
	if (!gmsgRadarData || !pPlayer)
		return;

	static unsigned short generation = 0;
	++generation;
	if (!generation)
		++generation;

	MESSAGE_BEGIN(MSG_ONE, gmsgRadarData, nullptr, ENT(pPlayer->pev));
		WRITE_BYTE(RADAR_MSG_SNAPSHOT_BEGIN);
		WRITE_SHORT((short)generation);
		WRITE_BYTE(silent ? RADAR_SNAPSHOT_SILENT : 0);
	MESSAGE_END();

	// NPC position is deliberately NOT sent here. The client only updates the
	// remembered NPC position when the NPC is actually visible to that player.
	for (int i = gpGlobals->maxClients + 1; i < gpGlobals->maxEntities; ++i)
	{
		edict_t* pent = g_engfuncs.pfnPEntityOfEntIndex(i);
		if (!pent || pent->free)
			continue;

		CBaseEntity* pEntity = CBaseEntity::Instance(pent);
		CBaseMonster* pMonster = nullptr;
		if (!RadarIsAliveMonster(pEntity, pMonster))
			continue;

		MESSAGE_BEGIN(MSG_ONE, gmsgRadarData, nullptr, ENT(pPlayer->pev));
			WRITE_BYTE(RADAR_MSG_NPC_INFO);
			WRITE_SHORT(i);
			WRITE_BYTE(RadarMonsterKind(pMonster, pPlayer));
		MESSAGE_END();
	}

	// Enabled radar hints are global, so every player receives every enabled hint.
	for (int i = gpGlobals->maxClients + 1; i < gpGlobals->maxEntities; ++i)
	{
		edict_t* pent = g_engfuncs.pfnPEntityOfEntIndex(i);
		if (!pent || pent->free)
			continue;

		CBaseEntity* pEntity = CBaseEntity::Instance(pent);
		if (!pEntity || !FClassnameIs(pEntity->pev, "game_radar_hint"))
			continue;

		CGameRadarHint* pHint = static_cast<CGameRadarHint*>(pEntity);
		if (!pHint->IsEnabled())
			continue;

		MESSAGE_BEGIN(MSG_ONE, gmsgRadarData, nullptr, ENT(pPlayer->pev));
			WRITE_BYTE(RADAR_MSG_HINT_SYNC);
			WRITE_SHORT(i);
			WRITE_BYTE(pHint->MarkerKind());
			WRITE_COORD(pHint->pev->origin.x);
			WRITE_COORD(pHint->pev->origin.y);
			WRITE_COORD(pHint->pev->origin.z);
		MESSAGE_END();
	}

	MESSAGE_BEGIN(MSG_ONE, gmsgRadarData, nullptr, ENT(pPlayer->pev));
		WRITE_BYTE(RADAR_MSG_SNAPSHOT_END);
		WRITE_SHORT((short)generation);
	MESSAGE_END();
}

void Radar_RegisterUserMessages()
{
	if (!gmsgRadarData)
		gmsgRadarData = REG_USER_MSG("RadarData", -1);
}

void Radar_MarkPlayerForSilentSync(CBasePlayer* pPlayer)
{
	if (!pPlayer)
		return;

	const int index = pPlayer->entindex();
	if (index >= 1 && index < (int)ARRAYSIZE(g_radarSilentSync))
		g_radarSilentSync[index] = true;
}

void Radar_MarkAllPlayersForSilentSync()
{
	for (int i = 1; i <= gpGlobals->maxClients && i < (int)ARRAYSIZE(g_radarSilentSync); ++i)
	{
		if (UTIL_PlayerByIndex(i))
			g_radarSilentSync[i] = true;
	}
}

void Radar_PlayerDamagedByMonster(CBasePlayer* pPlayer, CBaseMonster* pMonster)
{
	if (!pPlayer || !pMonster || !gmsgRadarData)
		return;

	if (!pMonster->IsFullyAlive())
		return;

	const int playerIndex = pPlayer->entindex();
	if (playerIndex < 1 || playerIndex > gpGlobals->maxClients)
		return;

	const int kind = RadarMonsterKind(pMonster, pPlayer);

	MESSAGE_BEGIN(MSG_ONE, gmsgRadarData, nullptr, ENT(pPlayer->pev));
		WRITE_BYTE(RADAR_MSG_NPC_DAMAGE_REVEAL);
		WRITE_SHORT(pMonster->entindex());
		WRITE_BYTE(kind);
		WRITE_COORD(pMonster->pev->origin.x);
		WRITE_COORD(pMonster->pev->origin.y);
		WRITE_COORD(pMonster->pev->origin.z);
	MESSAGE_END();
}

void Radar_StartFrame()
{
	if (!gmsgRadarData)
		return;

	// Radar updates are intentionally much slower than the game frame rate.
	static float nextUpdate = 0.0f;
	if (gpGlobals->time < nextUpdate)
		return;

	nextUpdate = gpGlobals->time + 0.20f;

	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		CBaseEntity* pPlayerEntity = UTIL_PlayerByIndex(i);
		CBasePlayer* pPlayer = static_cast<CBasePlayer*>(pPlayerEntity);
		if (!pPlayer || !pPlayer->IsNetClient())
			continue;

		const bool silent = (i < (int)ARRAYSIZE(g_radarSilentSync)) ? g_radarSilentSync[i] : false;
		RadarWriteSnapshotForPlayer(pPlayer, silent);
		if (i < (int)ARRAYSIZE(g_radarSilentSync))
			g_radarSilentSync[i] = false;
	}
}
