
/*
 * prop_door_rotating.cpp
 *
 * Prototype of prop_door_rotating for Half-Life Featureful-BURN.
 *
 * ------------------------------------------------------------
 * PURPOSE
 * ------------------------------------------------------------
 *
 * This is a studio-model point entity intended to become a
 * rotating animated door later.
 *
 * The important part of this prototype is COLLISION.
 *
 * Player movement:
 *
 *     normal entity bbox
 *
 * Generic traces / bullets / hitbox traces:
 *
 *     studio model hitboxes
 *
 * The MDL must contain hitboxes and must be compiled with:
 *
 *     $flags 512
 *
 * which enables STUDIO_TRACE_HITBOX.
 *
 *
 * ------------------------------------------------------------
 * IMPORTANT GOLDsrc DETAIL
 * ------------------------------------------------------------
 *
 * SOLID_BBOX is intentional.
 *
 * The GoldSrc engine's physent construction recognizes a studio
 * model for complex hitbox collision when:
 *
 *     pev->solid == SOLID_BBOX
 *
 * and the model has:
 *
 *     STUDIO_TRACE_HITBOX
 *
 * In that case the engine puts the model into physent_t's
 * studiomodel field.
 *
 * SOLID_SLIDEBOX is NOT used here because that path is what made
 * the previous prototype behave as a normal bounding-box entity.
 *
 *
 * ------------------------------------------------------------
 * CURRENT FEATURES
 * ------------------------------------------------------------
 *
 *   - point entity
 *   - model key from Hammer/J.A.C.K.
 *   - targetname through normal entity keyvalue handling
 *   - studio model
 *   - bbox collision for player movement
 *   - studio hitbox collision for studio traces
 *   - animated studio state is preserved
 *
 *
 * ------------------------------------------------------------
 * NOT IMPLEMENTED YET
 * ------------------------------------------------------------
 *
 *   - opening
 *   - closing
 *   - door animation control
 *   - Use()
 *   - automatic opening
 *   - automatic closing
 *   - sounds
 *   - locking
 *   - damage/crush behavior
 *
 */

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "animation.h"


 //=========================================================
 // CPropDoorRotating
 //=========================================================

class CPropDoorRotating : public CBaseAnimating
{
public:
	void Spawn() override;
	void Precache() override;

	/*
	 * The bbox used by the normal engine/entity collision path
	 * must be kept valid.
	 *
	 * We intentionally do not replace this with a custom
	 * SetObjectCollisionBox implementation.
	 */
};


//=========================================================
// Entity registration
//=========================================================

LINK_ENTITY_TO_CLASS(prop_door_rotating, CPropDoorRotating);


//=========================================================
// Precache
//=========================================================

void CPropDoorRotating::Precache()
{
	/*
	 * The model comes from the entity's "model" key.
	 *
	 * Do not use SetMyModel() here.
	 *
	 * We need the normal SET_MODEL() path because modelindex is
	 * what allows the engine to obtain the studio model later.
	 */
	if (pev->model != iStringNull)
	{
		const char* pszModel = STRING(pev->model);

		if (pszModel && pszModel[0])
			PRECACHE_MODEL(pszModel);
	}
}


//=========================================================
// Spawn
//=========================================================

void CPropDoorRotating::Spawn()
{
	Precache();


	//=========================================================
	// Validate model
	//=========================================================

	if (pev->model == iStringNull || !STRING(pev->model)[0])
	{
		ALERT(
			at_error,
			"prop_door_rotating at %.0f %.0f %.0f missing modelname\n",
			(double)pev->origin.x,
			(double)pev->origin.y,
			(double)pev->origin.z
		);

		pev->solid = SOLID_NOT;
		pev->movetype = MOVETYPE_NONE;

		return;
	}


	//=========================================================
	// Set model
	//=========================================================
	//
	// This is critical.
	//
	// We use SET_MODEL(), not SetMyModel().
	//
	// This gives the entity a real modelindex. The engine can then
	// inspect that model's studio flags and, for STUDIO_TRACE_HITBOX,
	// attach the studio model to physent_t.
	//=========================================================

	PRECACHE_MODEL(STRING(pev->model));

	SET_MODEL(
		ENT(pev),
		STRING(pev->model)
	);


	//=========================================================
	// Initial studio state
	//=========================================================

	InitBoneControllers();

	pev->sequence = 0;
	pev->frame = 0;
	pev->framerate = 0.0f;
	pev->animtime = gpGlobals->time;

	ResetSequenceInfo();


	//=========================================================
	// COLLISION TYPE
	//=========================================================
	//
	// This MUST be SOLID_BBOX.
	//
	// For studio models GoldSrc uses this solid type to put the
	// studio model into physent_t::studiomodel when the model
	// contains STUDIO_TRACE_HITBOX.
	//
	// The entity still has mins/maxs for the normal player
	// movement broad-phase.
	//=========================================================

	pev->solid = SOLID_BBOX;


	//=========================================================
	// Movement
	//=========================================================
	//
	// The prototype itself does not move yet.
	//
	// Later, when the door is animated/rotated, the same entity
	// will remain in the world and its:
	//
	//     origin
	//     angles
	//     sequence
	//     frame
	//     controller[]
	//     blending[]
	//
	// will describe the current collision pose.
	//=========================================================

	pev->movetype = MOVETYPE_NONE;


	//=========================================================
	// DAMAGE
	//=========================================================
	//
	// Keep DAMAGE_YES.
	//
	// This follows the working cycler architecture in BURN and
	// ensures the entity remains a normal damage-capable studio
	// collision object.
	//
	// The prototype does not actually lose health.
	//=========================================================

	pev->takedamage = DAMAGE_YES;
	pev->health = 80000;


	//=========================================================
	// Entity flags
	//=========================================================
	//
	// This is a prop, not an AI monster.
	//
	// In particular, do not manually add FL_MONSTER.
	//=========================================================

	pev->flags &= ~FL_MONSTER;


	//=========================================================
	// Player collision bbox
	//=========================================================
	//
	// The player movement code needs an entity bbox.
	//
	// Unlike the previous versions, we deliberately do NOT try to
	// make this bbox equal to the model's full bounds.
	//
	// This is the same principle as cycler:
	//
	//     player -> bbox
	//
	// while:
	//
	//     trace -> studio hitbox
	//
	//=========================================================

	UTIL_SetSize(
		pev,
		Vector(-16, -16, 0),
		Vector(16, 16, 72)
	);


	//=========================================================
	// Relink
	//=========================================================

	UTIL_SetOrigin(
		pev,
		pev->origin
	);
}
