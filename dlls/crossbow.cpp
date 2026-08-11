/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*
*	This product contains software technology licensed from Id
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc.
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "skill.h"
#include "weapons.h"
#include "player.h"

#if !CLIENT_DLL
#include "combat.h"
#include "global_models.h"
#include "gamerules.h"

#define BOLT_AIR_VELOCITY	2000
#define BOLT_WATER_VELOCITY	1000

#define SF_CROSSBOW_BOLT_EXPLOSIVE 512

// UNDONE: Save/restore this?  Don't forget to set classname and LINK_ENTITY_TO_CLASS()
// 
// OVERLOADS SOME ENTVARS:
//
// speed - the ideal magnitude of my velocity
class CCrossbowBolt : public CBaseEntity
{
public:
	void Spawn() override;
	void Precache() override;
	int Classify() override;
	void EXPORT BubbleThink();
	void EXPORT BoltTouch(CBaseEntity* pOther);
	void EXPORT ExplodeThink();
	void SetProjectileParamsBeforeSpawn(const ProjectileParameters& params) {
		SetProjectileParamsBeforeSpawnImpl(params);
		if (params.variant)
			pev->spawnflags |= SF_CROSSBOW_BOLT_EXPLOSIVE;
	}
	void LaunchAsProjectile(const ProjectileParameters& params) override;

	static const NamedSoundScript boltHitBody;
	static const NamedSoundScript boltHitWorld;
};

LINK_ENTITY_TO_CLASS(crossbow_bolt, CCrossbowBolt)

const NamedSoundScript CCrossbowBolt::boltHitBody = {
	CHAN_BODY,
	{"weapons/xbow_hitbod1.wav", "weapons/xbow_hitbod2.wav"},
	"Crossbow.BoltHitBody"
};

const NamedSoundScript CCrossbowBolt::boltHitWorld = {
	CHAN_BODY,
	{"weapons/xbow_hit1.wav", "weapons/xbow_hit2.wav"},
	FloatRange(0.95f, 1.0f),
	ATTN_NORM,
	IntRange(98, 105),
	"Crossbow.BoltHitWorld"
};

void CCrossbowBolt::LaunchAsProjectile(const ProjectileParameters& params)
{
	bool inWater = false;
	if (params.pLauncher && params.pOwner && params.pLauncher->MyWeaponPointer() && params.pOwner->IsPlayer())
	{
		inWater = params.pOwner->pev->waterlevel == WL_Eyes;
	}
	else
	{
		inWater = UTIL_PointContents(pev->origin) == CONTENTS_WATER;
	}

	const float defaultSpeed = inWater ? BOLT_WATER_VELOCITY : BOLT_AIR_VELOCITY;

	LaunchAsProjectileImpl(defaultSpeed, params);
	SetMyProjectileEffectFlags();
	pev->speed = pev->velocity.Length();
	pev->avelocity.z = 10.0f;
}

void CCrossbowBolt::Spawn()
{
	Precache();
	pev->movetype = MOVETYPE_FLY;
	pev->solid = SOLID_BBOX;

	pev->gravity = 0.5f;

	SetMyModel("models/crossbow_bolt.mdl");

	UTIL_SetOrigin(pev, pev->origin);
	UTIL_SetSize(pev, Vector(0, 0, 0), Vector(0, 0, 0));

	SetTouch(&CCrossbowBolt::BoltTouch);
	SetThink(&CCrossbowBolt::BubbleThink);
	pev->nextthink = gpGlobals->time + 0.2f;
}

void CCrossbowBolt::Precache()
{
	PrecacheMyModel("models/crossbow_bolt.mdl");
	RegisterAndPrecacheSoundScript(boltHitBody);
	RegisterAndPrecacheSoundScript(boltHitWorld);
}

int CCrossbowBolt::Classify()
{
	return CLASS_NONE;
}

void CCrossbowBolt::BoltTouch(CBaseEntity* pOther)
{
	SetTouch(NULL);
	SetThink(NULL);

	const bool explosiveBolt = FBitSet(pev->spawnflags, SF_CROSSBOW_BOLT_EXPLOSIVE);

	if (pOther->pev->takedamage)
	{
		TraceResult tr = UTIL_GetGlobalTrace();
		entvars_t* pevOwner = VARS(pev->owner);

		float damage = GetProjectileDamage();
		int dmgType = DMG_GENERIC;

		if (damage == 0)
		{
			if (pOther->IsPlayer())
			{
				damage = GetSkillValue("plr_xbow_bolt_client");
			}
			else
			{
				damage = GetSkillValue("plr_xbow_bolt_monster");
			}
		}

		if (!pOther->IsPlayer())
		{
			dmgType = DMG_BULLET;
		}

		DamageInfo damageInfo(damage, dmgType);
		damageInfo.SetGibPolicy(GIB_NEVER);
		pOther->ApplyTraceAttack(pev, pevOwner, damageInfo, pev->velocity.Normalize(), &tr);

		pev->velocity = Vector(0, 0, 0);
		// play body "thwack" sound
		EmitSoundScript(boltHitBody);

		if (!explosiveBolt)
		{
			Killed(pev, pev, GIB_NEVER);
		}
	}
	else
	{
		EmitSoundScript(boltHitWorld);

		SetThink(&CBaseEntity::SUB_Remove);
		pev->nextthink = gpGlobals->time;// this will get changed below if the bolt is allowed to stick in what it hit.

		if (FClassnameIs(pOther->pev, "worldspawn"))
		{
			// if what we hit is static architecture, can stay around for a while.
			Vector vecDir = pev->velocity.Normalize();
			UTIL_SetOrigin(pev, pev->origin - vecDir * 12.0f);
			pev->angles = UTIL_VecToAngles(vecDir);
			pev->solid = SOLID_NOT;
			pev->movetype = MOVETYPE_FLY;
			pev->velocity = Vector(0, 0, 0);
			pev->avelocity.z = 0;
			pev->angles.z = RANDOM_LONG(0, 360);
			pev->nextthink = gpGlobals->time + 10.0f;
		}
		// TODO: make configurable?
		/*else if( g_fIsXash3D && (pOther->pev->movetype == MOVETYPE_PUSH || pOther->pev->movetype == MOVETYPE_PUSHSTEP) )
		{
			Vector vecDir = pev->velocity.Normalize();
			UTIL_SetOrigin( pev, pev->origin - vecDir * 12.0f );
			pev->angles = UTIL_VecToAngles( vecDir );
			pev->solid = SOLID_NOT;
			pev->velocity = Vector( 0, 0, 0 );
			pev->avelocity.z = 0;
			pev->angles.z = RANDOM_LONG( 0, 360 );
			pev->nextthink = gpGlobals->time + 10.0f;

			// g-cont. Setup movewith feature
			pev->movetype = MOVETYPE_COMPOUND;	// set movewith type
			pev->aiment = ENT( pOther->pev );	// set parent
		}*/

		if (UTIL_PointContents(pev->origin) != CONTENTS_WATER)
		{
			UTIL_Sparks(pev->origin);
		}

		ClearBits(pev->effects, EF_LIGHT);
	}

	if (explosiveBolt)
	{
		SetThink(&CCrossbowBolt::ExplodeThink);
		pev->nextthink = gpGlobals->time + 0.1f;
	}
}

void CCrossbowBolt::BubbleThink()
{
	pev->nextthink = gpGlobals->time + 0.1f;

	if (pev->waterlevel == WL_NotInWater)
		return;

	UTIL_BubbleTrail(pev->origin - pev->velocity * 0.1f, pev->origin, 1);
}

void CCrossbowBolt::ExplodeThink()
{
	int iContents = UTIL_PointContents(pev->origin);
	int iScale;

	pev->dmg = GetSkillValue("plr_xbow_bolt_explo");
	iScale = 10;

	MESSAGE_BEGIN(MSG_PVS, SVC_TEMPENTITY, pev->origin);
	WRITE_BYTE(TE_EXPLOSION);
	WRITE_VECTOR(pev->origin);
	if (iContents != CONTENTS_WATER)
	{
		WRITE_SHORT(g_sModelIndexFireball);
	}
	else
	{
		WRITE_SHORT(g_sModelIndexWExplosion);
	}
	WRITE_BYTE(iScale); // scale * 10
	WRITE_BYTE(15); // framerate
	WRITE_BYTE(TE_EXPLFLAG_NONE);
	MESSAGE_END();

	entvars_t* pevOwner;

	if (pev->owner)
		pevOwner = VARS(pev->owner);
	else
		pevOwner = NULL;

	pev->owner = NULL; // can't traceline attack owner if this is set

	::RadiusDamage(pev->origin, pev, pevOwner, DamageInfo(pev->dmg, DMG_BLAST).SetGibPolicy(GIB_ALWAYS), 128, CLASS_NONE);

	UTIL_Remove(this);
}
#endif

enum crossbow_e
{
	CROSSBOW_IDLE1 = 0,	// full
	CROSSBOW_IDLE2,		// empty
	CROSSBOW_FIDGET1,	// full
	CROSSBOW_FIDGET2,	// empty
	CROSSBOW_FIRE1,		// full
	CROSSBOW_FIRE2,		// reload
	CROSSBOW_FIRE3,		// empty
	CROSSBOW_RELOAD,	// from empty
	CROSSBOW_DRAW1,		// full
	CROSSBOW_DRAW2,		// empty
	CROSSBOW_HOLSTER1,	// full
	CROSSBOW_HOLSTER2	// empty
};

#ifdef CLIENT_DLL
bool g_bCrossbowScopeVisible = false;
#endif

enum crossbow_scope_phase_e
{
	CROSSBOW_SCOPE_NONE = 0,
	CROSSBOW_SCOPE_ZOOMING_IN,
	CROSSBOW_SCOPE_FADE_TO_SCOPE,
	CROSSBOW_SCOPE_SCOPED,
	CROSSBOW_SCOPE_ZOOMING_OUT,
	CROSSBOW_SCOPE_FADE_TO_DEFAULT
};

class CCrossbow : public CConfigurableWeapon
{
public:
	void Precache() override;
	int WeaponId() const override { return WEAPON_CROSSBOW; }
	bool GetItemInfo(ItemInfo* p) override;
	WeaponParameters GetDefaultParameters() const override;
	int GetPlaybackEvent(bool altModeFire) const override;

	bool Deploy() override;
	void Holster() override;
	void ItemPostFrame() override;

	void NativeAttack(bool altMode) override;

private:
	void StartScopeIn();
	void StartScopeOut();
	void UpdateScope();
	void SetPlayerFOV(float flFov);
	float GetPlayerFOV() const;
	void PushScreenFade(bool toBlack, float fadeTime, float holdTime);

	unsigned short m_usCrossbow2;
	int   m_iScopePhase = CROSSBOW_SCOPE_NONE;
	float m_flScopePhaseStartTime = 0.0f;
	float m_flScopeStartFOV = 0.0f;
	float m_flScopeTargetFOV = 25.0f;
	float m_flScopeRestoreFOV = 0.0f;
	float m_flScopeAnimTime = 0.22f;
	float m_flScopeBlackHoldTime = 0.02f;
};

LINK_WEAPON_TO_CLASS(weapon_crossbow, CCrossbow)

void CCrossbow::Precache()
{
	CConfigurableWeapon::Precache();
	m_usCrossbow2 = PRECACHE_EVENT(1, "events/crossbow2.sc");
}

bool CCrossbow::GetItemInfo(ItemInfo* p)
{
	p->iSlot = 2;
	p->iPosition = 2;
	return true;
}


float CCrossbow::GetPlayerFOV() const
{
	float flFov = m_pPlayer->pev->fov;

	if (flFov <= 0.0f)
	{
		flFov = 90.0f;
	}

	return flFov;
}

void CCrossbow::SetPlayerFOV(float flFov)
{
	if (flFov <= 0.0f)
	{
		m_pPlayer->pev->fov = 0.0f;
		m_pPlayer->m_iFOV = 0;
		return;
	}

	if (flFov < 1.0f)
		flFov = 1.0f;
	else if (flFov > 179.0f)
		flFov = 179.0f;

	m_pPlayer->pev->fov = flFov;
	m_pPlayer->m_iFOV = (int)flFov;
}

void CCrossbow::PushScreenFade(bool toBlack, float fadeTime, float holdTime)
{
#if !CLIENT_DLL
	UTIL_ScreenFade(m_pPlayer, Vector(0, 0, 0), fadeTime, holdTime, 255, toBlack ? (FFADE_OUT | FFADE_STAYOUT) : FFADE_IN);
#endif
}

void CCrossbow::StartScopeIn()
{
	if (m_iScopePhase == CROSSBOW_SCOPE_SCOPED || m_iScopePhase == CROSSBOW_SCOPE_FADE_TO_SCOPE || m_iScopePhase == CROSSBOW_SCOPE_ZOOMING_IN)
		return;
	ALERT(at_console, "SCOPE", STRING(pev->classname));
	m_flScopeRestoreFOV = GetPlayerFOV();
	m_flScopeStartFOV = m_flScopeRestoreFOV;
	m_flScopeTargetFOV = 25.0f;

	m_iScopePhase = CROSSBOW_SCOPE_ZOOMING_IN;
	m_flScopePhaseStartTime = gpGlobals->time;

	SendWeaponAnim(12); // scope
}

void CCrossbow::StartScopeOut()
{
	if (m_iScopePhase == CROSSBOW_SCOPE_NONE || m_iScopePhase == CROSSBOW_SCOPE_FADE_TO_DEFAULT || m_iScopePhase == CROSSBOW_SCOPE_ZOOMING_OUT)
		return;
	ALERT(at_console, "UNSCOPE", STRING(pev->classname));
	m_flScopeStartFOV = 25.0f;
	m_flScopeTargetFOV = m_flScopeRestoreFOV > 0.0f ? m_flScopeRestoreFOV : 90.0f;

	PushScreenFade(true, 0.1f, m_flScopeBlackHoldTime);

	m_iScopePhase = CROSSBOW_SCOPE_FADE_TO_DEFAULT;
	m_flScopePhaseStartTime = gpGlobals->time;
	
	SendWeaponAnim(13); // unscope
}

void CCrossbow::UpdateScope()
{
	switch (m_iScopePhase)
	{
	case CROSSBOW_SCOPE_ZOOMING_IN: // меньше фов
	{
		float t = (gpGlobals->time - m_flScopePhaseStartTime) / m_flScopeAnimTime;
		t = (t < 0.0f) ? 0.0f : ((t > 0.85f) ? 0.85f : t);
		SetPlayerFOV(m_flScopeStartFOV + (m_flScopeTargetFOV - m_flScopeStartFOV) * t);
		ALERT(at_console, "ZOOMING IN", STRING(pev->classname));

		std::string s = std::to_string(GetPlayerFOV());
		const char* c_str = s.c_str(); // Points to "3.140000"

		ALERT(at_console, c_str, STRING(pev->classname));

		if (t >= 0.85f)
		{
			ALERT(at_console, "ZOOMED IN", STRING(pev->classname));
			m_iScopePhase = CROSSBOW_SCOPE_FADE_TO_SCOPE;
			m_flScopePhaseStartTime = gpGlobals->time;
			PushScreenFade(true, 0.1f, m_flScopeBlackHoldTime);
		}
		break;
	}

	case CROSSBOW_SCOPE_ZOOMING_OUT: // больше фов
	{
		float t = (gpGlobals->time - m_flScopePhaseStartTime) / m_flScopeAnimTime;
		t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
		SetPlayerFOV(m_flScopeStartFOV + (m_flScopeTargetFOV - m_flScopeStartFOV) * t);
		ALERT(at_console, "ZOOMING OUT", STRING(pev->classname));

		std::string s = std::to_string(GetPlayerFOV());
		const char* c_str = s.c_str(); // Points to "3.140000"

		ALERT(at_console, c_str, STRING(pev->classname));
		if (t >= 1.0f)
		{
			ALERT(at_console, "ZOOMED OUT", STRING(pev->classname));
			SetPlayerFOV(m_flScopeTargetFOV);
			m_iScopePhase = CROSSBOW_SCOPE_NONE;
		}
		break;
	}

	case CROSSBOW_SCOPE_SCOPED:
		//SetPlayerFOV(25.0f);
		break;

	case CROSSBOW_SCOPE_FADE_TO_SCOPE: // сделай фейд на прицел
		if (gpGlobals->time - m_flScopePhaseStartTime >= m_flScopeBlackHoldTime)
		{
			ALERT(at_console, "FADE TO SCOPE", STRING(pev->classname));
			SetPlayerFOV(25.0f);
#ifdef CLIENT_DLL
			g_bCrossbowScopeVisible = true; // включить спрайт прицела
#endif
			m_iScopePhase = CROSSBOW_SCOPE_SCOPED;
			m_flScopePhaseStartTime = gpGlobals->time;
			PushScreenFade(false, 0.1f, 0.0f); // убрать ф
		}
		break;

	case CROSSBOW_SCOPE_FADE_TO_DEFAULT: // сделай фейд из прицела
		if (gpGlobals->time - m_flScopePhaseStartTime >= m_flScopeBlackHoldTime)
		{
			ALERT(at_console, "FADE FROM SCOPE", STRING(pev->classname));
#ifdef CLIENT_DLL
			g_bCrossbowScopeVisible = false; // убрать спрайт прицела
#endif
			m_flScopeStartFOV = 25.0f;
			PushScreenFade(false, 0.1f, 0.0f); // убрать ф
			m_iScopePhase = CROSSBOW_SCOPE_ZOOMING_OUT;
			m_flScopePhaseStartTime = gpGlobals->time;
	
		}
		break;

	default:
		break;
	}
}

bool CCrossbow::Deploy()
{
	m_iScopePhase = CROSSBOW_SCOPE_NONE;
	m_flScopePhaseStartTime = 0.0f;
	m_flScopeRestoreFOV = 0.0f;
	m_flScopeStartFOV = 0.0f;
	m_flScopeTargetFOV = 25.0f;
#ifdef CLIENT_DLL
	g_bCrossbowScopeVisible = false;
#endif
	return CConfigurableWeapon::Deploy();
}

void CCrossbow::Holster()
{
	m_iScopePhase = CROSSBOW_SCOPE_NONE;
	m_flScopePhaseStartTime = 0.0f;
	m_flScopeRestoreFOV = 0.0f;
	m_flScopeStartFOV = 0.0f;
	m_flScopeTargetFOV = 25.0f;
	SetPlayerFOV(0.0f);
#ifdef CLIENT_DLL
	g_bCrossbowScopeVisible = false;
#endif
	CConfigurableWeapon::Holster();
}

void CCrossbow::ItemPostFrame()
{
	const bool attack2Pressed = (m_pPlayer->m_afButtonPressed & IN_ATTACK2) != 0;
	const bool attack2Released = (m_pPlayer->m_afButtonReleased & IN_ATTACK2) != 0;

	if (attack2Pressed)
	{
		StartScopeIn();
	}
	else if (attack2Released)
	{
		StartScopeOut();
	}

	UpdateScope();

	CConfigurableWeapon::ItemPostFrame();
}

WeaponParameters CCrossbow::GetDefaultParameters() const
{
	WeaponParameters params;

	params.initialAmmoAmount = 5;
	params.maxClip = 5;
	params.ammoName = "bolts";

	params.worldModel = "models/w_crossbow.mdl";
	params.viewModel = "models/v_crossbow.mdl";
	params.playerModel = "models/p_crossbow.mdl";
	params.playerAnimExt = "bow";
	params.priority = 10;

	params.deploy.animIndex = CROSSBOW_DRAW1;
	params.deploy.animIndex.mainEmptied = CROSSBOW_DRAW2;

	params.idleAnims.main = WeaponParameters::IdleAnimArray{
		WeaponParameters::IdleAnim{CROSSBOW_IDLE1, 0.75f, 91.0f / 30.0f },
		WeaponParameters::IdleAnim{CROSSBOW_FIDGET1, 0.25f, 81.0f / 30.0f},
	};

	params.idleAnims.mainEmptied = WeaponParameters::IdleAnimArray{
		WeaponParameters::IdleAnim{CROSSBOW_IDLE2, 0.75f, 91.0f / 30.0f },
		WeaponParameters::IdleAnim{CROSSBOW_FIDGET2, 0.25f, 81.0f / 30.0f},
	};

	params.fire.fireType = WeaponParameters::Fire::PROJECTILE;
	params.fire.anims = { CROSSBOW_FIRE1 };
	params.fire.anims.mainEmptied = { CROSSBOW_FIRE3 };
	params.fire.sound = {
		CHAN_WEAPON,
		{"weapons/xbow_fire1.wav"},
		1.0f,
		ATTN_NORM,
		IntRange(93, 108)
	};
	params.fire.soundAdditional = {
		CHAN_ITEM,
		{"ambience/_comma.wav"},
		FloatRange(0.9f, 1.0f),
		ATTN_NORM,
		IntRange(93, 108)
	};
	params.fire.cycleTime = 0.75f;
	params.fire.idleDelay = 5.0f;
	params.fire.idleDelay.mainEmptied = 0.75f;
	params.fire.allowUnderwater = true;
	params.fire.autoAimDegree = AUTOAIM_2DEGREES;
	params.fire.weaponVolume = QUIET_GUN_VOLUME;
	params.fire.clientPunchPitch = -2.0f;
	if (bIsMultiplayer())
		params.fire.clientPunchPitch.alt = 0.0f;

	params.fire.projectileName = "crossbow_bolt";
	params.fire.projectileOffsetUp = -2.0f;
	params.fire.projectileRespectPunchangle = true;
	params.fire.projectileAdjustToCross = false;

	if (bIsMultiplayer())
	{
		params.fire.projectileName = "crossbow_bolt explosive";
		params.fire.fireType.alt = WeaponParameters::Fire::NATIVE;
	}

	params.secondaryFireType = SecondaryFireType::DISABLED;

	params.reload.animIndex = CROSSBOW_RELOAD;
	params.reload.duration = 4.5f;
	params.reload.sound = {
		CHAN_ITEM,
		{"ambience/_comma.wav"},
		FloatRange(0.95f, 1.0f),
		ATTN_NORM,
		IntRange(93, 108)
	};

	params.holster.animIndex = CROSSBOW_HOLSTER1;
	params.holster.attackDelay = 0.5f;

	params.holster.animIndex.mainEmptied = CROSSBOW_HOLSTER2;

	params.dropAmmo.classname = "ammo_crossbow";

	return params;
}

int CCrossbow::GetPlaybackEvent(bool altModeFire) const
{
	return (altModeFire && bIsMultiplayer()) ? m_usCrossbow2 : CConfigurableWeapon::GetPlaybackEvent(altModeFire);
}

void CCrossbow::NativeAttack(bool altMode)
{
	TraceResult tr;

	Vector anglesAim = m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle;
	UTIL_MakeVectors(anglesAim);
	Vector vecSrc = m_pPlayer->GetGunPosition() - gpGlobals->v_up * 2.0f;
	Vector vecDir = gpGlobals->v_forward;

	UTIL_TraceLine(vecSrc, vecSrc + vecDir * 8192, dont_ignore_monsters, m_pPlayer->edict(), &tr);

#if !CLIENT_DLL
	if (tr.pHit->v.takedamage)
	{
		CBaseEntity::Instance(tr.pHit)->ApplyTraceAttack(m_pPlayer->pev, m_pPlayer->pev, DamageInfo(GetSkillValue("plr_xbow_bolt_hitscan"), DMG_BULLET).SetGibPolicy(GIB_NEVER), vecDir, &tr);
	}
#endif
}
