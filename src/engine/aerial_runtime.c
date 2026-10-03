#include <engine/special_internal.h>
#include <melee/lb/lbfile.h>
#include <dolphin/dvd.h>
#include <sysdolphin/baselib/memory.h>
extern char* ftData_803C23E4[Ft_Kind_Max];
#include <melee/ft/kinds/ftCommon/ftCo_AttackAir.h>
#include <melee/ft/kinds/ftGameWatch/forward.h>
void Rogue_LinkAerialDownEnter(Fighter_GObj* gobj);
#include <melee/ft/kinds/ftGameWatch/ftgamewatchattackair.h>

static int aerial_motion(const RogueAerialDef* def, bool landing)
{
    if (def->donor == Ft_Kind_GameWatch) {
        switch (def->slot) {
        case 0: return landing ? ftGw_MS_LandingAirN : ftGw_MS_AttackAirN;
        case 2: return landing ? ftGw_MS_LandingAirB : ftGw_MS_AttackAirB;
        case 3: return landing ? ftGw_MS_LandingAirHi : ftGw_MS_AttackAirHi;
        }
    }
    return (landing ? ftCo_MS_LandingAirN : ftCo_MS_AttackAirN) + def->slot;
}

void Rogue_AerialPrepare(Fighter* fp)
{
    int slot;
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    if (!BAM_ENABLE_AERIALS || S->fighter != fp) return;
    for(slot=0;slot<ROGUE_AERIAL_SLOTS;++slot)
        S->aerial_equipped[slot]=Rogue_EquippedAerial(fp, slot);
    for (slot = 0; slot < ROGUE_AERIAL_SLOTS; ++slot) {
        const RogueAerialDef* def = RogueAerial_Find(S->aerial_equipped[slot]);
        const RogueAbilityDefinition* donor;
        short anims[2];
        int source, landing;
        if (!def) continue;
        source = def->donor;
        /* Out of memory: this slot keeps the native aerial. */
        if (!Rogue_DonorEnsure(S, source)) {
            OSReport("[bam] aerial_skipped kind=%u slot=%u (out of memory)\n", source, slot);
            S->aerial_equipped[slot] = 0;
            continue;
        }
        donor = Rogue_GetAbility(1 + source * 4);
        /* Read only this equipped attack and its landing. */
        for (landing = 0; landing < 2; ++landing) {
            int motion = aerial_motion(def, landing != 0);
            const MotionState* state = motion < ftCo_MS_Count ? &fp->x1C_actionStateList[motion]
                                                              : &donor->states[motion - ftCo_MS_Count];
            anims[landing] = (short) state->anim_id;
        }
        if (!Rogue_DonorReadAnims(S, source, anims, 2, "aerial_slices")) {
            OSReport("[bam] aerial_skipped kind=%u slot=%u (no memory for animations)\n", source, slot);
            S->aerial_equipped[slot] = 0;
        }
    }
}

void Rogue_AerialRelease(RogueFighterState* S)
{
    int source,slot,landing;
    if(S->shares_partner) return; /* Nana's slices belong to Popo. */
    for(slot=0;slot<ROGUE_AERIAL_SLOTS;++slot)
        for(landing=0;landing<2;++landing)
            if(S->aerial_blobs[slot][landing]) {
                Rogue_SliceFree(S->aerial_blobs[slot][landing]);
                /* match accounting removed */
            }
    for(slot=0;slot<(int)S->special_blob_count;++slot)
        if(S->special_blobs[slot]) Rogue_SliceFree(S->special_blobs[slot]);
    S->special_blob_count=0;
    for(source=0;source<Ft_Kind_Max;++source)
        if(S->aerial_anims[source] && !BamCache_Owns(S->aerial_anims[source]))
            HSD_Free(S->aerial_anims[source]);
}

bool Rogue_AerialTryEnter(Fighter_GObj* gobj, int motion)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const RogueAerialDef* def;
    int slot = motion - ftCo_MS_AttackAirN;
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    if (!BAM_ENABLE_AERIALS || !Rogue_IsBuildFighter(fp) || S->fighter != fp || slot < 0 || slot >= ROGUE_AERIAL_SLOTS) return false;
    def = RogueAerial_Find(S->aerial_equipped[slot]);
    if (!def) return false;
    if (!S->loaded_sources[def->donor])
        OSPanic(__FILE__, __LINE__, "equipped aerial has no match-owned donor");
    Rogue_BorrowBegin(fp, def->donor);
    S->aerial = def;
    if(S->aerial_anims[def->donor]) fp->x24=S->aerial_anims[def->donor];
    if ((def->donor == Ft_Kind_Link || def->donor == Ft_Kind_CLink) && slot == 4)
        Rogue_LinkAerialDownEnter(gobj);
    else ftCo_AttackAir_EnterFromMsid(gobj, aerial_motion(def, false));
    if (def->donor == Ft_Kind_GameWatch) {
        if (slot == 0) fp->accessory4_cb = ftGw_AttackAirN_ItemParachuteSetup;
        if (slot == 2) fp->accessory4_cb = ftGw_AttackAirN_ItemTurtleSetup;
        if (slot == 3) fp->accessory4_cb = ftGw_AttackAirN_ItemSparkySetup;
    }
#if BAM_DEBUG || 0 || 0
    OSReport("[bam] aerial_enter id=%u recipient=%u match=%u\n", def->id, fp->kind, S->match_generation);
#endif
    return true;
}

MotionState* Rogue_AerialMotionState(Fighter* fp, int motion)
{
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    const RogueAerialDef* def = S->aerial;
    if (!def || (motion != aerial_motion(def, false) && motion != aerial_motion(def, true))) {
        Rogue_AbilityCleanup(fp);
        return NULL;
    }
    if (motion < ftCo_MS_Count) return &fp->x1C_actionStateList[motion];
    return &Rogue_GetAbility(1 + def->donor * 4)->states[motion - ftCo_MS_Count];
}

float Rogue_AerialLandingLag(Fighter* fp, int motion, float native_lag)
{
    ftCo_DatAttrs* attrs;
    float lag, scale;
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    const RogueAerialDef* def = S->aerial;
    if (!Rogue_IsAbilityState(fp) || !def || motion != aerial_motion(def, false)) return native_lag;
    attrs = gFtDataList[def->donor]->x0;
    switch (def->slot) {
    case 0: lag = attrs->landingairn_lag; break;
    case 1: lag = attrs->landingairf_lag; break;
    case 2: lag = attrs->landingairb_lag; break;
    case 3: lag = def->donor == Ft_Kind_GameWatch ? attrs->landingairb_lag : attrs->landingairhi_lag; break;
    default: lag = attrs->landingairlw_lag; break;
    }
    scale = 1.0f;
    return lag * scale;
}
