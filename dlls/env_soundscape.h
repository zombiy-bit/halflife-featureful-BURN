//=========================================================
// env_soundscape.h
//=========================================================

#ifndef ENV_SOUNDSCAPE_H
#define ENV_SOUNDSCAPE_H

#ifdef _WIN32
#pragma once
#endif

#include "cbase.h"

#include <map>
#include <string>
#include <vector>

struct SoundscapeCue
{
	std::string type;
	std::vector<std::string> waves;

	float volume;
	float pitch;

	float minTime;
	float maxTime;

	float nextTime;

	std::string lastWave;

	SoundscapeCue()
	{
		volume = 10.0f;
		pitch = 100.0f;

		minTime = 0.0f;
		maxTime = 0.0f;

		nextTime = 0.0f;
	}
};

struct SoundscapeDef
{
	std::vector<SoundscapeCue> cues;

	int dsp;
	int dspExit;

	SoundscapeDef()
	{
		dsp = -1;
		dspExit = 0;
	}
};

extern std::map<std::string, SoundscapeDef> g_Soundscapes;

class CEnvSoundscape : public CBaseEntity
{
public:
	void Spawn() override;
	void Precache() override;
	void KeyValue(KeyValueData* pkvd) override;

	int Save(CSave& save) override;
	int Restore(CRestore& restore) override;

	void EXPORT SoundscapeUse(
		CBaseEntity* pActivator,
		CBaseEntity* pCaller,
		USE_TYPE useType,
		float value);

	void EXPORT SoundscapeThink();

	int ObjectCaps() override
	{
		return CBaseEntity::ObjectCaps() & ~FCAP_ACROSS_TRANSITION;
	}

private:
	bool IsPlayerInsideBox(CBaseEntity* pPlayer) const;

	float ComputeFadeFactor(
		const Vector& delta,
		bool& insideInner) const;

	void UpdateLoopVolumes(float fadeFactor);

	void StartLoops(float fadeFactor);
	void StopLoops();

	void ScheduleRandoms();
	void PlayRandoms(CBaseEntity* pPlayer);

	void SendDSPToPlayer(
		CBaseEntity* pPlayer,
		int dsp);

	static std::string PickRandomWave(
		SoundscapeCue& cue);

	static std::string PickLoopWave(
		const SoundscapeCue& cue);

private:
	string_t m_iszSoundscape;

	Vector m_vecHalfSize;

	float m_flFadeMultiplier;
	float m_flCurrentLoopFade;

	int m_iDSP;
	int m_iDSPExit;

	BOOL m_bActive;
	BOOL m_bLoopsStarted;
	BOOL m_bWasInsideInner;

	int m_iClientDSP[33];
};

#endif
