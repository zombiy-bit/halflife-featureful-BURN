// env_model_shadow.cpp
#include "extdll.h"
#include "util.h"
#include "cbase.h"

class CEnvModelShadow : public CBaseAnimating
{
public:
    void Spawn() override;
    void Precache() override;
    void KeyValue(KeyValueData* pkvd) override;
};

LINK_ENTITY_TO_CLASS(env_model_shadow, CEnvModelShadow);

void CEnvModelShadow::Precache()
{
    if (!FStringNull(pev->model))
        PRECACHE_MODEL(STRING(pev->model));
}

void CEnvModelShadow::KeyValue(KeyValueData* pkvd)
{
    if (FStrEq(pkvd->szKeyName, "model"))
    {
        pev->model = ALLOC_STRING(pkvd->szValue);
        pkvd->fHandled = TRUE;
        return;
    }

    if (FStrEq(pkvd->szKeyName, "body"))
    {
        pev->body = atoi(pkvd->szValue);
        pkvd->fHandled = TRUE;
        return;
    }

    if (FStrEq(pkvd->szKeyName, "skin"))
    {
        pev->skin = atoi(pkvd->szValue);
        pkvd->fHandled = TRUE;
        return;
    }

    if (FStrEq(pkvd->szKeyName, "sequence"))
    {
        pev->sequence = atoi(pkvd->szValue);
        pkvd->fHandled = TRUE;
        return;
    }

    CBaseAnimating::KeyValue(pkvd);
}

void CEnvModelShadow::Spawn()
{
    if (FStringNull(pev->model))
    {
        ALERT(at_console, "env_model_shadow: missing model\n");
        UTIL_Remove(this);
        return;
    }

    Precache();

    SET_MODEL(ENT(pev), STRING(pev->model));

    pev->movetype = MOVETYPE_NONE;
    pev->solid = SOLID_NOT;      // чтобы не мешал геймплею
    pev->takedamage = DAMAGE_NO;
    pev->effects &= ~EF_NODRAW;
    pev->rendermode = kRenderNormal;
    pev->renderamt = 255.0f;

    if (pev->scale <= 0.0f)
        pev->scale = 1.0f;

    // Даёт движку bbox модели, если понадобится для отладки/триггеров.
    SetObjectCollisionBox();

    ResetSequenceInfo();
}