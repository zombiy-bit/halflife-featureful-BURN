/**
*
* env_randomxenmaker
*
* 16 portal points: portal1..portal16
* 16 monster classes: monster1..monster16
* random delay between mindelay and maxdelay
* Start On spawnflag
*
* Features:
* - can be named / triggered via targetname (FGD uses base(Targetname))
* - save/load safe
* - slot stays busy until monster death
* - corpse does not fade (owner is cleared on death)
* - random yaw on spawn
* - optional exact xen/warp effect via pre-placed env_xenmaker/env_warpball
*   referenced by the optional key "xenmaker" or "warpball"
*   (safe fallback is precached sounds)
*
**/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "saverestore.h"

#define MAX_RANDOM_XENMAKER_SLOTS 16
#define SF_RANDOM_XENMAKER_START_ON 1

class CRandomXenMaker : public CBaseMonster
{
public:
	CRandomXenMaker();

	void Spawn(void) override;
	void Precache(void) override;
	void KeyValue(KeyValueData* pkvd) override;

	void EXPORT ToggleUse(CBaseEntity* pActivator, CBaseEntity* pCaller, USE_TYPE useType, float value);
	void EXPORT MakerThink(void);

	void DeathNotice(entvars_t* pevChild) override;

	int Save(CSave& save) override;
	int Restore(CRestore& restore) override;

	static TYPEDESCRIPTION m_SaveData[];

private:
	CBaseEntity* FindPortalEntity(int iSlot);
	int PickFreePortalSlot(void);
	int PickRandomMonsterSlot(void);
	float GetRandomDelay(void) const;

	void TriggerPortalEffect(CBaseEntity* pPortal, const Vector& origin);

	CBaseEntity* SpawnMonsterDirect(const Vector& origin, const Vector& angles, string_t iszMonsterClass, int iSlot);

private:
	string_t m_iszPortalTarget[MAX_RANDOM_XENMAKER_SLOTS];
	string_t m_iszMonsterClass[MAX_RANDOM_XENMAKER_SLOTS];
	EHANDLE  m_hSpawnedMonster[MAX_RANDOM_XENMAKER_SLOTS];
	string_t m_iszEffectTarget;

	float m_flMinDelay;
	float m_flMaxDelay;
	bool  m_fActive;
};

LINK_ENTITY_TO_CLASS(env_randomxenmaker, CRandomXenMaker)

TYPEDESCRIPTION CRandomXenMaker::m_SaveData[] =
{
	DEFINE_ARRAY(CRandomXenMaker, m_iszPortalTarget, FIELD_STRING, MAX_RANDOM_XENMAKER_SLOTS),
	DEFINE_ARRAY(CRandomXenMaker, m_iszMonsterClass, FIELD_STRING, MAX_RANDOM_XENMAKER_SLOTS),
	DEFINE_ARRAY(CRandomXenMaker, m_hSpawnedMonster, FIELD_EHANDLE, MAX_RANDOM_XENMAKER_SLOTS),
	DEFINE_FIELD(CRandomXenMaker, m_iszEffectTarget, FIELD_STRING),
	DEFINE_FIELD(CRandomXenMaker, m_flMinDelay, FIELD_FLOAT),
	DEFINE_FIELD(CRandomXenMaker, m_flMaxDelay, FIELD_FLOAT),
	DEFINE_FIELD(CRandomXenMaker, m_fActive, FIELD_BOOLEAN),
};

IMPLEMENT_SAVERESTORE(CRandomXenMaker, CBaseMonster)

CRandomXenMaker::CRandomXenMaker()
{
	for (int i = 0; i < MAX_RANDOM_XENMAKER_SLOTS; ++i)
	{
		m_iszPortalTarget[i] = iStringNull;
		m_iszMonsterClass[i] = iStringNull;
		m_hSpawnedMonster[i] = NULL;
	}

	m_iszEffectTarget = iStringNull;
	m_flMinDelay = 1.0f;
	m_flMaxDelay = 2.0f;
	m_fActive = false;
}

void CRandomXenMaker::KeyValue(KeyValueData* pkvd)
{
	for (int i = 0; i < MAX_RANDOM_XENMAKER_SLOTS; ++i)
	{
		char szPortalKey[32];
		char szMonsterKey[32];

		sprintf(szPortalKey, "portal%d", i + 1);
		sprintf(szMonsterKey, "monster%d", i + 1);

		if (FStrEq(pkvd->szKeyName, szPortalKey))
		{
			if (pkvd->szValue && pkvd->szValue[0])
				m_iszPortalTarget[i] = ALLOC_STRING(pkvd->szValue);

			pkvd->fHandled = TRUE;
			return;
		}

		if (FStrEq(pkvd->szKeyName, szMonsterKey))
		{
			if (pkvd->szValue && pkvd->szValue[0])
				m_iszMonsterClass[i] = ALLOC_STRING(pkvd->szValue);

			pkvd->fHandled = TRUE;
			return;
		}
	}

	if (FStrEq(pkvd->szKeyName, "mindelay"))
	{
		m_flMinDelay = (float)atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
		return;
	}

	if (FStrEq(pkvd->szKeyName, "maxdelay"))
	{
		m_flMaxDelay = (float)atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
		return;
	}

	if (FStrEq(pkvd->szKeyName, "xenmaker") || FStrEq(pkvd->szKeyName, "warpball"))
	{
		if (pkvd->szValue && pkvd->szValue[0])
			m_iszEffectTarget = ALLOC_STRING(pkvd->szValue);

		pkvd->fHandled = TRUE;
		return;
	}

	CBaseMonster::KeyValue(pkvd);
}

void CRandomXenMaker::Precache(void)
{
	CBaseMonster::Precache();

	PRECACHE_SOUND("debris/beamstart7.wav");
	PRECACHE_SOUND("debris/beamstart2.wav");

	for (int i = 0; i < MAX_RANDOM_XENMAKER_SLOTS; ++i)
	{
		if (!FStringNull(m_iszMonsterClass[i]))
			UTIL_PrecacheOther(STRING(m_iszMonsterClass[i]));
	}
}

float CRandomXenMaker::GetRandomDelay(void) const
{
	float flMin = m_flMinDelay;
	float flMax = m_flMaxDelay;

	if (flMin < 0.0f) flMin = 0.0f;
	if (flMax < 0.0f) flMax = 0.0f;

	if (flMax < flMin)
	{
		float flTmp = flMin;
		flMin = flMax;
		flMax = flTmp;
	}

	return RANDOM_FLOAT(flMin, flMax);
}

CBaseEntity* CRandomXenMaker::FindPortalEntity(int iSlot)
{
	if (iSlot < 0 || iSlot >= MAX_RANDOM_XENMAKER_SLOTS)
		return NULL;

	if (FStringNull(m_iszPortalTarget[iSlot]))
		return NULL;

	return UTIL_FindEntityByTargetname(NULL, STRING(m_iszPortalTarget[iSlot]));
}

int CRandomXenMaker::PickFreePortalSlot(void)
{
	int iChoices[MAX_RANDOM_XENMAKER_SLOTS];
	int iCount = 0;

	for (int i = 0; i < MAX_RANDOM_XENMAKER_SLOTS; ++i)
	{
		if (FStringNull(m_iszPortalTarget[i]))
			continue;

		if (m_hSpawnedMonster[i])
			continue;

		if (FindPortalEntity(i) == NULL)
			continue;

		iChoices[iCount++] = i;
	}

	if (iCount <= 0)
		return -1;

	return iChoices[RANDOM_LONG(0, iCount - 1)];
}

int CRandomXenMaker::PickRandomMonsterSlot(void)
{
	int iChoices[MAX_RANDOM_XENMAKER_SLOTS];
	int iCount = 0;

	for (int i = 0; i < MAX_RANDOM_XENMAKER_SLOTS; ++i)
	{
		if (!FStringNull(m_iszMonsterClass[i]))
			iChoices[iCount++] = i;
	}

	if (iCount <= 0)
		return -1;

	return iChoices[RANDOM_LONG(0, iCount - 1)];
}

void CRandomXenMaker::TriggerPortalEffect(CBaseEntity* pPortal, const Vector& origin)
{
	if (pPortal && !FStringNull(m_iszEffectTarget))
	{
		CBaseEntity* pFx = UTIL_FindEntityByTargetname(NULL, STRING(m_iszEffectTarget));
		if (pFx && (FClassnameIs(pFx->pev, "env_xenmaker") || FClassnameIs(pFx->pev, "env_warpball")))
		{
			edict_t* prevInflictor = pFx->pev->dmg_inflictor;
			Vector prevPos = pFx->pev->vuser1;

			pFx->pev->vuser1 = origin;
			pFx->pev->dmg_inflictor = edict();
			pFx->Use(this, this, USE_SET, 0.0f);

			pFx->pev->vuser1 = prevPos;
			pFx->pev->dmg_inflictor = prevInflictor;
			return;
		}
	}

	EMIT_SOUND(ENT(pPortal->pev), CHAN_ITEM, "debris/beamstart7.wav", 1.0f, ATTN_NORM);
	EMIT_SOUND(ENT(pPortal->pev), CHAN_ITEM, "debris/beamstart2.wav", 1.0f, ATTN_NORM);
}

CBaseEntity* CRandomXenMaker::SpawnMonsterDirect(const Vector& origin, const Vector& angles, string_t iszMonsterClass, int iSlot)
{
	if (FStringNull(iszMonsterClass))
		return NULL;

	edict_t* pent = CREATE_NAMED_ENTITY(iszMonsterClass);
	if (FNullEnt(pent))
		return NULL;

	entvars_t* pevMonster = VARS(pent);
	pevMonster->origin = origin;
	pevMonster->angles = angles;

	DispatchSpawn(ENT(pevMonster));
	pevMonster->owner = edict();

	CBaseEntity* pMonster = CBaseEntity::Instance(pent);
	if (pMonster)
		m_hSpawnedMonster[iSlot] = pMonster;

	return pMonster;
}

void CRandomXenMaker::Spawn(void)
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;

	Precache();

	SetUse(&CRandomXenMaker::ToggleUse);

	if (FBitSet(pev->spawnflags, SF_RANDOM_XENMAKER_START_ON))
	{
		m_fActive = true;
		SetThink(&CRandomXenMaker::MakerThink);
		pev->nextthink = gpGlobals->time + GetRandomDelay();
	}
	else
	{
		m_fActive = false;
		SetThink(NULL);
		pev->nextthink = 0;
	}
}



void CRandomXenMaker::ToggleUse(CBaseEntity* pActivator, CBaseEntity* pCaller, USE_TYPE useType, float value)
{
	switch (useType)
	{
	case USE_ON:
		m_fActive = true;
		break;
	case USE_OFF:
		m_fActive = false;
		break;
	default:
		m_fActive = !m_fActive;
		break;
	}

	if (m_fActive)
	{
		SetThink(&CRandomXenMaker::MakerThink);
		pev->nextthink = gpGlobals->time + GetRandomDelay();
	}
	else
	{
		SetThink(NULL);
		pev->nextthink = 0;
	}
}

void CRandomXenMaker::MakerThink(void)
{
	if (!m_fActive)
	{
		SetThink(NULL);
		pev->nextthink = 0;
		return;
	}

	int iPortalSlot = PickFreePortalSlot();
	int iMonsterSlot = PickRandomMonsterSlot();

	if (iPortalSlot >= 0 && iMonsterSlot >= 0)
	{
		CBaseEntity* pPortal = FindPortalEntity(iPortalSlot);
		if (pPortal)
		{
			Vector spawnAngles = pPortal->pev->angles;
			spawnAngles.x = 0.0f;
			spawnAngles.z = 0.0f;
			spawnAngles.y = RANDOM_FLOAT(0.0f, 360.0f);

			TriggerPortalEffect(pPortal, pPortal->pev->origin);

			CBaseEntity* pMonster = SpawnMonsterDirect(
				pPortal->pev->origin,
				spawnAngles,
				m_iszMonsterClass[iMonsterSlot],
				iPortalSlot
			);

			if (pMonster)
			{
				// optional second pulse, helps the spawn feel closer to xen portals
				TriggerPortalEffect(pPortal, pPortal->pev->origin);
			}
		}
	}

	pev->nextthink = gpGlobals->time + GetRandomDelay();
}

void CRandomXenMaker::DeathNotice(entvars_t* pevChild)
{
	for (int i = 0; i < MAX_RANDOM_XENMAKER_SLOTS; ++i)
	{
		if (m_hSpawnedMonster[i] && m_hSpawnedMonster[i]->pev == pevChild)
		{
			// make corpse stay
			pevChild->owner = NULL;

			m_hSpawnedMonster[i] = NULL;
			return;
		}
	}
}