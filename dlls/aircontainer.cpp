/*
 * item_aircontainer
 *
 * Portable extra-air container for the BURN Featureful SDK.
 *
 * Put the model at:
 *   models/w_aircontainer.mdl
 *
 * Change AIRCONTAINER_PICKUP_SOUND below to your actual sound path.
 */

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "items.h"

#define AIRCONTAINER_MODEL "models/w_aircontainer.mdl"

 // Change this path to your own pickup sound.
#define AIRCONTAINER_PICKUP_SOUND "items/airtank1.wav"

class CItemAirContainer : public CItem
{
public:
	void Spawn() override
	{
		Precache();

		SetMyModel(AIRCONTAINER_MODEL);
		CItem::Spawn();
	}

	void Precache() override
	{
		PrecacheMyModel(AIRCONTAINER_MODEL);
		PRECACHE_SOUND(AIRCONTAINER_PICKUP_SOUND);
	}

	bool MyTouch(CBasePlayer* pPlayer) override
	{
		if (!pPlayer || pPlayer->pev->deadflag != DEAD_NO)
			return false;

		// Do not pick up another container while the extra-air value
		// is already at its maximum.
		if (pPlayer->m_flAirContainerTime >= AIRTANKTIME)
			return false;

		pPlayer->m_flAirContainerTime = AIRTANKTIME;

		// A new container gives the player a fresh sequence of
		// extra-air breaths when they are underwater.
		if (pPlayer->pev->waterlevel == WL_Eyes)
			pPlayer->m_flNextAirContainerBreathTime =
			gpGlobals->time + RANDOM_FLOAT(5.0f, 6.0f);
		else
			pPlayer->m_flNextAirContainerBreathTime = 0.0f;

		pPlayer->m_flAirContainerBubbleTime = 0.0f;

		EMIT_SOUND(pPlayer->edict(), CHAN_ITEM,
			AIRCONTAINER_PICKUP_SOUND, 1.0f, ATTN_NORM);

		pPlayer->NotifyPickup(STRING(pev->classname));

		return true;
	}
};

LINK_ENTITY_TO_CLASS(item_aircontainer, CItemAirContainer)
