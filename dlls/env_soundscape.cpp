//=========================================================
// env_soundscape.cpp
//=========================================================

#include "extdll.h"
#include "util.h"
#include "cbase.h"

#include "file_utils.h"
#include "parsetext.h"

#include "env_soundscape.h"

#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>

std::map<std::string, SoundscapeDef> g_Soundscapes;

static bool g_SoundscapesLoaded = false;

LINK_ENTITY_TO_CLASS(env_soundscape, CEnvSoundscape);

#ifndef SND_LOOP
#define SND_LOOP 0
#endif

#ifndef SND_CHANGE_VOL
#define SND_CHANGE_VOL 0
#endif

static float Clamp01(float v)
{
	if (v < 0.0f)
		return 0.0f;

	if (v > 1.0f)
		return 1.0f;

	return v;
}

static std::string TrimCopy(const std::string& s)
{
	size_t a = 0;
	while (a < s.size() && std::isspace((unsigned char)s[a]))
		++a;

	size_t b = s.size();
	while (b > a && std::isspace((unsigned char)s[b - 1]))
		--b;

	return s.substr(a, b - a);
}

static void StripCommentFromLine(std::string& s)
{
	size_t p1 = s.find("//");
	size_t p2 = s.find("--");

	size_t p = std::string::npos;

	if (p1 != std::string::npos)
		p = p1;

	if (p2 != std::string::npos && (p == std::string::npos || p2 < p))
		p = p2;

	if (p != std::string::npos)
		s.erase(p);
}

static std::string NormalizeWavePath(std::string s)
{
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '\\')
			s[i] = '/';
	}

	return s;
}

static void SkipWhitespaceAndComments(const char*& p)
{
	while (*p)
	{
		if (std::isspace((unsigned char)*p))
		{
			++p;
			continue;
		}

		if (p[0] == '/' && p[1] == '/')
		{
			while (*p && *p != '\n')
				++p;
			continue;
		}

		if (p[0] == '-' && p[1] == '-')
		{
			while (*p && *p != '\n')
				++p;
			continue;
		}

		break;
	}
}

static bool ReadToken(const char*& p, std::string& out)
{
	SkipWhitespaceAndComments(p);

	if (!*p)
		return false;

	if (*p == '{' || *p == '}')
	{
		out.assign(1, *p);
		++p;
		return true;
	}

	if (*p == '"')
	{
		++p;
		const char* start = p;
		while (*p && *p != '"')
			++p;

		out.assign(start, p - start);

		if (*p == '"')
			++p;

		return true;
	}

	const char* start = p;
	while (*p && !std::isspace((unsigned char)*p) && *p != '{' && *p != '}')
		++p;

	out.assign(start, p - start);
	return true;
}

static bool ExpectToken(const char*& p, const char* expected)
{
	std::string token;
	if (!ReadToken(p, token))
		return false;
	return token == expected;
}

static bool ParseFloatSafe(const std::string& s, float& out)
{
	return std::sscanf(s.c_str(), "%f", &out) == 1;
}

static bool ParseIntSafe(const std::string& s, int& out)
{
	return std::sscanf(s.c_str(), "%d", &out) == 1;
}

static bool ParseVectorSafe(const char* value, Vector& out)
{
	float x, y, z;
	if (std::sscanf(value, "%f %f %f", &x, &y, &z) == 3)
	{
		out.x = std::fabs(x);
		out.y = std::fabs(y);
		out.z = std::fabs(z);
		return true;
	}

	if (std::sscanf(value, "%f", &x) == 1)
	{
		out.x = std::fabs(x);
		out.y = std::fabs(x);
		out.z = std::fabs(x);
		return true;
	}

	return false;
}

static void ReadRestOfLine(const char*& p, std::string& out)
{
	const char* start = p;
	while (*p && *p != '\n' && *p != '\r')
		++p;

	out.assign(start, p - start);

	while (*p == '\n' || *p == '\r')
		++p;
}

static void SplitWaveList(const std::string& raw, std::vector<std::string>& out)
{
	std::string line = TrimCopy(raw);
	StripCommentFromLine(line);
	line = TrimCopy(line);

	if (line.empty())
		return;

	size_t start = 0;
	while (start < line.size())
	{
		size_t comma = line.find(',', start);

		std::string part = (comma == std::string::npos)
			? line.substr(start)
			: line.substr(start, comma - start);

		part = TrimCopy(part);

		if (!part.empty())
		{
			if (part.size() >= 2 && part.front() == '"' && part.back() == '"')
				part = part.substr(1, part.size() - 2);

			part = NormalizeWavePath(part);
			out.push_back(part);
		}

		if (comma == std::string::npos)
			break;

		start = comma + 1;
	}
}

static void LoadSoundscapes()
{
	if (g_SoundscapesLoaded)
		return;

	g_SoundscapesLoaded = true;

	int fileSize = 0;
	char* fileData = ReadFileContents("sound/soundscapes.txt", fileSize);

	if (!fileData || fileSize <= 0)
	{
		if (fileData)
			FreeFileContents(fileData);

		ALERT(at_console, "env_soundscape: failed to load sound/soundscapes.txt\n");
		return;
	}

	const char* p = fileData;

	while (true)
	{
		std::string soundscapeName;
		if (!ReadToken(p, soundscapeName))
			break;

		if (soundscapeName == "}")
			break;

		if (!ExpectToken(p, "{"))
			break;

		SoundscapeDef def;

		while (true)
		{
			std::string token;
			if (!ReadToken(p, token))
				break;

			if (token == "}")
				break;

			SkipWhitespaceAndComments(p);

			if (*p == '{')
			{
				ExpectToken(p, "{");

				SoundscapeCue cue;
				cue.type = token;

				while (true)
				{
					std::string key;
					if (!ReadToken(p, key))
						break;

					if (key == "}")
						break;

					if (key == "wave")
					{
						std::string rawLine;
						ReadRestOfLine(p, rawLine);
						SplitWaveList(rawLine, cue.waves);
						continue;
					}

					std::string value;
					if (!ReadToken(p, value))
						break;

					if (key == "volume")
					{
						ParseFloatSafe(value, cue.volume);
					}
					else if (key == "pitch")
					{
						ParseFloatSafe(value, cue.pitch);
					}
					else if (key == "min")
					{
						ParseFloatSafe(value, cue.minTime);
					}
					else if (key == "max")
					{
						ParseFloatSafe(value, cue.maxTime);
					}
				}

				if (cue.maxTime <= 0.0f)
					cue.maxTime = cue.minTime;

				if (!cue.waves.empty())
					def.cues.push_back(cue);
			}
			else
			{
				std::string value;
				if (!ReadToken(p, value))
					break;

				if (token == "DSP")
				{
					ParseIntSafe(value, def.dsp);
				}
				else if (token == "DSPexit")
				{
					ParseIntSafe(value, def.dspExit);
				}
			}
		}

		if (!soundscapeName.empty())
			g_Soundscapes[soundscapeName] = def;
	}

	FreeFileContents(fileData);

	ALERT(at_console, "env_soundscape: loaded %d soundscapes\n", (int)g_Soundscapes.size());
}

int CEnvSoundscape::Save(CSave& save)
{
	return CBaseEntity::Save(save);
}

int CEnvSoundscape::Restore(CRestore& restore)
{
	if (!CBaseEntity::Restore(restore))
		return 0;

	SetUse(&CEnvSoundscape::SoundscapeUse);
	SetThink(&CEnvSoundscape::SoundscapeThink);

	m_bLoopsStarted = FALSE;
	m_bWasInsideInner = FALSE;
	m_flCurrentLoopFade = -1.0f;

	for (int i = 0; i < 33; ++i)
		m_iClientDSP[i] = -1;

	pev->nextthink = gpGlobals->time + 0.1f;
	return 1;
}

std::string CEnvSoundscape::PickRandomWave(SoundscapeCue& cue)
{
	if (cue.waves.empty())
		return std::string();

	if (cue.waves.size() == 1)
	{
		cue.lastWave = cue.waves[0];
		return cue.waves[0];
	}

	for (int i = 0; i < 32; ++i)
	{
		int idx = RANDOM_LONG(0, (int)cue.waves.size() - 1);
		if (cue.waves[idx] != cue.lastWave)
		{
			cue.lastWave = cue.waves[idx];
			return cue.waves[idx];
		}
	}

	cue.lastWave = cue.waves[0];
	return cue.waves[0];
}

std::string CEnvSoundscape::PickLoopWave(const SoundscapeCue& cue)
{
	if (cue.waves.empty())
		return std::string();

	return cue.waves[0];
}

void CEnvSoundscape::SendDSPToPlayer(CBaseEntity* pPlayer, int dsp)
{
	if (!pPlayer)
		return;

	MESSAGE_BEGIN(MSG_ONE, SVC_ROOMTYPE, NULL, ENT(pPlayer->pev));
	WRITE_SHORT((short)dsp);
	MESSAGE_END();
}

void CEnvSoundscape::KeyValue(KeyValueData* pkvd)
{
	if (FStrEq(pkvd->szKeyName, "Sound") ||
		FStrEq(pkvd->szKeyName, "sound"))
	{
		m_iszSoundscape = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
		return;
	}

	if (FStrEq(pkvd->szKeyName, "boxsize"))
	{
		Vector v;
		if (ParseVectorSafe(pkvd->szValue, v))
			m_vecHalfSize = v;

		pkvd->fHandled = TRUE;
		return;
	}

	if (FStrEq(pkvd->szKeyName, "fadefactor"))
	{
		float mult = 2.0f;
		if (ParseFloatSafe(pkvd->szValue, mult))
		{
			if (mult < 1.0f)
				mult = 1.0f;

			m_flFadeMultiplier = mult;
		}

		pkvd->fHandled = TRUE;
		return;
	}

	CBaseEntity::KeyValue(pkvd);
}

void CEnvSoundscape::Precache()
{
	LoadSoundscapes();

	const char* name = STRING(m_iszSoundscape);
	auto it = g_Soundscapes.find(name ? name : "");

	if (it == g_Soundscapes.end())
		return;

	m_iDSP = it->second.dsp;
	m_iDSPExit = it->second.dspExit;

	for (size_t i = 0; i < it->second.cues.size(); ++i)
	{
		const SoundscapeCue& cue = it->second.cues[i];

		for (size_t j = 0; j < cue.waves.size(); ++j)
		{
			const std::string& wave = cue.waves[j];
			if (!wave.empty() && wave[0] != '!')
				PRECACHE_SOUND(wave.c_str());
		}
	}
}

void CEnvSoundscape::Spawn()
{
	Precache();

	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;

	SetUse(&CEnvSoundscape::SoundscapeUse);
	SetThink(&CEnvSoundscape::SoundscapeThink);

	m_bActive = TRUE;
	m_bLoopsStarted = FALSE;
	m_bWasInsideInner = FALSE;
	m_flCurrentLoopFade = -1.0f;

	for (int i = 0; i < 33; ++i)
		m_iClientDSP[i] = -1;

	pev->nextthink = gpGlobals->time + 0.1f;
}

float CEnvSoundscape::ComputeFadeFactor(const Vector& delta, bool& insideInner) const
{
	insideInner =
		(std::fabs(delta.x) <= m_vecHalfSize.x) &&
		(std::fabs(delta.y) <= m_vecHalfSize.y) &&
		(std::fabs(delta.z) <= m_vecHalfSize.z);

	Vector outer(
		m_vecHalfSize.x * m_flFadeMultiplier,
		m_vecHalfSize.y * m_flFadeMultiplier,
		m_vecHalfSize.z * m_flFadeMultiplier);

	float ax = std::fabs(delta.x);
	float ay = std::fabs(delta.y);
	float az = std::fabs(delta.z);

	if (ax > outer.x || ay > outer.y || az > outer.z)
		return 0.0f;

	if (m_flFadeMultiplier <= 1.0f)
		return insideInner ? 1.0f : 0.0f;

	auto axisFactor = [](float d, float inner, float outerEdge) -> float
		{
			if (d <= inner)
				return 1.0f;

			float range = outerEdge - inner;
			if (range <= 0.0f)
				return 0.0f;

			return Clamp01((outerEdge - d) / range);
		};

	float fx = axisFactor(ax, m_vecHalfSize.x, outer.x);
	float fy = axisFactor(ay, m_vecHalfSize.y, outer.y);
	float fz = axisFactor(az, m_vecHalfSize.z, outer.z);

	float fade = fx;
	if (fy < fade) fade = fy;
	if (fz < fade) fade = fz;

	return Clamp01(fade);
}

void CEnvSoundscape::UpdateLoopVolumes(float fadeFactor)
{
	if (!m_bLoopsStarted)
	{
		StartLoops(fadeFactor);
		return;
	}

	const char* name = STRING(m_iszSoundscape);
	auto it = g_Soundscapes.find(name ? name : "");

	if (it == g_Soundscapes.end())
		return;

	for (size_t i = 0; i < it->second.cues.size(); ++i)
	{
		const SoundscapeCue& cue = it->second.cues[i];

		if (cue.type != "Loop")
			continue;

		std::string wave = PickLoopWave(cue);
		if (wave.empty())
			continue;

		EMIT_SOUND_DYN(
			ENT(pev),
			CHAN_STATIC,
			wave.c_str(),
			(cue.volume * fadeFactor) / 10.0f,
			ATTN_NONE,
			SND_CHANGE_VOL,
			(int)cue.pitch);
	}

	m_flCurrentLoopFade = fadeFactor;
}

void CEnvSoundscape::StartLoops(float fadeFactor)
{
	const char* name = STRING(m_iszSoundscape);
	auto it = g_Soundscapes.find(name ? name : "");

	if (it == g_Soundscapes.end())
		return;

	for (size_t i = 0; i < it->second.cues.size(); ++i)
	{
		const SoundscapeCue& cue = it->second.cues[i];

		if (cue.type != "Loop")
			continue;

		std::string wave = PickLoopWave(cue);
		if (wave.empty())
			continue;

		EMIT_SOUND_DYN(
			ENT(pev),
			CHAN_STATIC,
			wave.c_str(),
			(cue.volume * fadeFactor) / 10.0f,
			ATTN_NONE,
			0,
			(int)cue.pitch);
	}

	m_bLoopsStarted = TRUE;
	m_flCurrentLoopFade = fadeFactor;
}

void CEnvSoundscape::StopLoops()
{
	const char* name = STRING(m_iszSoundscape);
	auto it = g_Soundscapes.find(name ? name : "");

	if (it == g_Soundscapes.end())
		return;

	for (size_t i = 0; i < it->second.cues.size(); ++i)
	{
		const SoundscapeCue& cue = it->second.cues[i];

		if (cue.type != "Loop")
			continue;

		std::string wave = PickLoopWave(cue);
		if (wave.empty())
			continue;

		STOP_SOUND(ENT(pev), CHAN_STATIC, wave.c_str());
	}

	m_bLoopsStarted = FALSE;
	m_flCurrentLoopFade = -1.0f;
}

void CEnvSoundscape::ScheduleRandoms()
{
	const char* name = STRING(m_iszSoundscape);
	auto it = g_Soundscapes.find(name ? name : "");

	if (it == g_Soundscapes.end())
		return;

	for (size_t i = 0; i < it->second.cues.size(); ++i)
	{
		SoundscapeCue& cue = it->second.cues[i];

		if (cue.type != "Random")
			continue;

		float minT = (cue.minTime > 0.0f) ? cue.minTime : 1.0f;
		float maxT = (cue.maxTime > 0.0f) ? cue.maxTime : minT;

		cue.nextTime = gpGlobals->time + RANDOM_FLOAT(minT, maxT);
	}
}

void CEnvSoundscape::PlayRandoms(CBaseEntity* pPlayer)
{
	if (!pPlayer)
		return;

	const char* name = STRING(m_iszSoundscape);
	auto it = g_Soundscapes.find(name ? name : "");

	if (it == g_Soundscapes.end())
		return;

	for (size_t i = 0; i < it->second.cues.size(); ++i)
	{
		SoundscapeCue& cue = it->second.cues[i];

		if (cue.type != "Random")
			continue;

		if (gpGlobals->time < cue.nextTime)
			continue;

		std::string wave = PickRandomWave(cue);
		if (wave.empty())
			continue;

		Vector pos = pPlayer->pev->origin;
		pos.x += RANDOM_FLOAT(-10, 10);
		pos.y += RANDOM_FLOAT(-10, 10);
		pos.z += RANDOM_FLOAT(-10, 10);

		UTIL_EmitAmbientSound(
			ENT(pPlayer->pev),
			pos,
			wave.c_str(),
			cue.volume / 10.0f,
			ATTN_NONE,
			0,
			(int)cue.pitch);

		float minT = (cue.minTime > 0.0f) ? cue.minTime : 1.0f;
		float maxT = (cue.maxTime > 0.0f) ? cue.maxTime : minT;

		cue.nextTime = gpGlobals->time + RANDOM_FLOAT(minT, maxT);
	}
}

void CEnvSoundscape::SoundscapeUse(
	CBaseEntity* pActivator,
	CBaseEntity* pCaller,
	USE_TYPE useType,
	float value)
{
	if (useType == USE_ON)
		m_bActive = TRUE;
	else if (useType == USE_OFF)
		m_bActive = FALSE;
	else
		m_bActive = !m_bActive;

	if (!m_bActive)
	{
		StopLoops();

		for (int i = 1; i <= gpGlobals->maxClients; ++i)
		{
			CBaseEntity* pPlayer = UTIL_PlayerByIndex(i);
			if (!pPlayer)
				continue;

			if (m_iClientDSP[i] != m_iDSPExit)
			{
				SendDSPToPlayer(pPlayer, m_iDSPExit);
				m_iClientDSP[i] = m_iDSPExit;
			}
		}

		m_bWasInsideInner = FALSE;
	}
	else
	{
		m_bWasInsideInner = FALSE;
	}

	pev->nextthink = gpGlobals->time + 0.1f;
}

void CEnvSoundscape::SoundscapeThink()
{
	SetUse(&CEnvSoundscape::SoundscapeUse);
	SetThink(&CEnvSoundscape::SoundscapeThink);

	if (!m_bActive)
	{
		pev->nextthink = gpGlobals->time + 0.2f;
		return;
	}

	bool anyOuter = false;
	bool anyInner = false;
	float bestFade = 0.0f;
	CBaseEntity* bestPlayer = NULL;

	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		CBaseEntity* pPlayer = UTIL_PlayerByIndex(i);
		if (!pPlayer)
			continue;

		Vector delta = pPlayer->pev->origin - pev->origin;

		bool insideInner = false;
		float fade = ComputeFadeFactor(delta, insideInner);

		if (fade > 0.0f)
		{
			anyOuter = true;

			if (fade > bestFade)
			{
				bestFade = fade;
				bestPlayer = pPlayer;
			}

			if (insideInner)
				anyInner = true;

			if (m_iDSP >= 0 && m_iClientDSP[i] != m_iDSP)
			{
				SendDSPToPlayer(pPlayer, m_iDSP);
				m_iClientDSP[i] = m_iDSP;
			}
		}
		else
		{
			if (m_iClientDSP[i] != m_iDSPExit)
			{
				SendDSPToPlayer(pPlayer, m_iDSPExit);
				m_iClientDSP[i] = m_iDSPExit;
			}
		}
	}

	if (anyOuter)
	{
		UpdateLoopVolumes(bestFade);

		if (anyInner)
		{
			if (!m_bWasInsideInner)
				ScheduleRandoms();

			if (bestPlayer)
				PlayRandoms(bestPlayer);
		}
	}
	else
	{
		if (m_bLoopsStarted)
			StopLoops();
	}

	m_bWasInsideInner = anyInner;

	pev->nextthink = gpGlobals->time + 0.1f;
}
