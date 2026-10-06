#include <engine/special_internal.h>
#include <melee/lb/lbfile.h>
#include <dolphin/dvd.h>
#include <sysdolphin/baselib/memory.h>
extern char* ftData_803C23E4[Ft_Kind_Max];
#include <melee/ft/kinds/ftCommon/ftCo_AttackAir.h>
#include <melee/ft/kinds/ftGameWatch/forward.h>
void Bam_LinkAerialDownEnter(Fighter_GObj* gobj);
#include <melee/ft/kinds/ftGameWatch/ftgamewatchattackair.h>

static int aerial_motion(const BamAerialDef* def, bool landing)
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

void Bam_AerialPrepare(Fighter* fp)
{
    int slot;
    BamFighterState* const S = Bam_FighterCtx(fp);
    if (!BAM_ENABLE_AERIALS || S->fighter != fp) return;
    for(slot=0;slot<BAM_AERIAL_SLOTS;++slot)
        S->aerial_equipped[slot]=Bam_EquippedAerial(fp, slot);
    for (slot = 0; slot < BAM_AERIAL_SLOTS; ++slot) {
        const BamAerialDef* def = BamAerial_Find(S->aerial_equipped[slot]);
        const BamAbilityDefinition* donor;
        short anims[2];
        int source, landing;
        if (!def) continue;
        source = def->donor;
        /* Out of memory: this slot keeps the native aerial. */
        if (!Bam_DonorEnsure(S, source)) {
            BAM_NOTE("aerial_skipped kind=%u slot=%u (out of memory)\n", source, slot);
            S->aerial_equipped[slot] = 0;
            continue;
        }
        donor = Bam_GetAbility(1 + source * 4);
        /* Read only this equipped attack and its landing. */
        for (landing = 0; landing < 2; ++landing) {
            int motion = aerial_motion(def, landing != 0);
            const MotionState* state = motion < ftCo_MS_Count ? &fp->x1C_actionStateList[motion]
                                                              : &donor->states[motion - ftCo_MS_Count];
            anims[landing] = (short) state->anim_id;
        }
        if (!Bam_DonorReadAnims(S, source, anims, 2, "aerial_slices")) {
            BAM_NOTE("aerial_skipped kind=%u slot=%u (no memory for animations)\n", source, slot);
            S->aerial_equipped[slot] = 0;
        }
    }
}

void Bam_AerialRelease(BamFighterState* S)
{
    int source,slot,landing;
    if(S->shares_partner) return; /* Nana's slices belong to Popo. */
    for(slot=0;slot<BAM_AERIAL_SLOTS;++slot)
        for(landing=0;landing<2;++landing)
            if(S->aerial_blobs[slot][landing]) {
                Bam_SliceFree(S->aerial_blobs[slot][landing]);
            }
    for(slot=0;slot<(int)S->special_blob_count;++slot)
        if(S->special_blobs[slot]) Bam_SliceFree(S->special_blobs[slot]);
    S->special_blob_count=0;
    for(source=0;source<Ft_Kind_Max;++source)
        if(S->aerial_anims[source] && !BamCache_Owns(S->aerial_anims[source]))
            HSD_Free(S->aerial_anims[source]);
}

bool Bam_AerialTryEnter(Fighter_GObj* gobj, int motion)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const BamAerialDef* def;
    int slot = motion - ftCo_MS_AttackAirN;
    BamFighterState* const S = Bam_FighterCtx(fp);
    if (!BAM_ENABLE_AERIALS || !Bam_IsBuildFighter(fp) || S->fighter != fp || slot < 0 || slot >= BAM_AERIAL_SLOTS) return false;
    def = BamAerial_Find(S->aerial_equipped[slot]);
    if (!def) return false;
    if (!S->loaded_sources[def->donor])
        OSPanic(__FILE__, __LINE__, "equipped aerial has no match-owned donor");
    Bam_BorrowBegin(fp, def->donor);
    S->aerial = def;
    if(S->aerial_anims[def->donor]) fp->x24=S->aerial_anims[def->donor];
    if ((def->donor == Ft_Kind_Link || def->donor == Ft_Kind_CLink) && slot == 4)
        Bam_LinkAerialDownEnter(gobj);
    else ftCo_AttackAir_EnterFromMsid(gobj, aerial_motion(def, false));
    if (def->donor == Ft_Kind_GameWatch) {
        if (slot == 0) fp->accessory4_cb = ftGw_AttackAirN_ItemParachuteSetup;
        if (slot == 2) fp->accessory4_cb = ftGw_AttackAirN_ItemTurtleSetup;
        if (slot == 3) fp->accessory4_cb = ftGw_AttackAirN_ItemSparkySetup;
    }
#if BAM_DEBUG
    BAM_LOG("aerial_enter id=%u recipient=%u match=%u\n", def->id, fp->kind, S->match_generation);
#endif
    return true;
}

MotionState* Bam_AerialMotionState(Fighter* fp, int motion)
{
    BamFighterState* const S = Bam_FighterCtx(fp);
    const BamAerialDef* def = S->aerial;
    if (!def || (motion != aerial_motion(def, false) && motion != aerial_motion(def, true))) {
        Bam_AbilityCleanup(fp);
        return NULL;
    }
    if (motion < ftCo_MS_Count) return &fp->x1C_actionStateList[motion];
    return &Bam_GetAbility(1 + def->donor * 4)->states[motion - ftCo_MS_Count];
}

float Bam_AerialLandingLag(Fighter* fp, int motion, float native_lag)
{
    ftCo_DatAttrs* attrs;
    float lag, scale;
    BamFighterState* const S = Bam_FighterCtx(fp);
    const BamAerialDef* def = S->aerial;
    if (!Bam_IsAbilityState(fp) || !def || motion != aerial_motion(def, false)) return native_lag;
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
