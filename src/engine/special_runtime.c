#include <engine/special_internal.h>
#include <stddef.h>
#include <melee/ft/fighter.h>
#include <melee/lb/lb_00B0.h>
#include <melee/ft/kinds/ftKoopa/types.h>
/* Returned for fighters that own no state. Hot path: native fighter code
 * reaches this through Rogue_AbilityVars many times per frame, so it must
 * stay two compares. It is never written; every write is behind an
 * ownership check (S->fighter == fp), which this state never passes. */
/* Only the fields before "owned fighters only" are ever read through it. */
static union { u32 words[16]; void* align; } no_state_storage;
#define no_state (*(RogueFighterState*) &no_state_storage)
typedef char no_state_fits[(sizeof(no_state_storage) >= offsetof(RogueFighterState, normal_fresh) + 1) ? 1 : -1];
RogueFighterState* Rogue_FighterCtx(const Fighter* fp)
{
    if (fp && bam_match) {
        RogueFighterState* S = *Bam_FighterExtSlot(fp);
        if (S && S->fighter == fp) return S;
    }
#if BAM_DEBUG
    if (no_state.fighter || no_state.active || no_state.aerial || no_state.normal_on)
        OSPanic(__FILE__, __LINE__, "unowned special state was written");
#endif
    return &no_state;
}
bool Rogue_IsAbilityState(const Fighter* fp)
{
    RogueFighterState* const S = Rogue_FighterCtx(fp);
#if BAM_DEBUG
    if (fp && S->fighter == fp && S->match_generation != bam_match->generation)
        OSPanic(__FILE__, __LINE__, "stale borrowed-special match generation");
#endif
    return fp && S->fighter == fp &&
           (S->active != NULL || S->aerial != NULL || S->normal_on);
}

void Rogue_AbilityCleanup(Fighter* fp)
{
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    FighterKind source;
    RogueAbilitySlot slot;
    if (!Rogue_IsAbilityState(fp)) return;


    source = Rogue_AbilitySourceKind(fp);
    slot = S->active ? S->active->native_slot : ROGUE_ABILITY_SLOTS;
#if BAM_DEBUG || 0
    if (S->normal_on)
        BAM_LOG("normal_restore slot=%d donor=%u match=%u\n", S->normal_slot, S->normal_donor, S->match_generation);
    else
        BAM_LOG("%s_restore id=%u match=%u\n", S->active ? "special" : "aerial", S->active ? S->active->id : S->aerial->id, S->match_generation);
#endif

    /*
     * Tear down source-owned attached state while source attrs/vars are still
     * installed. Free projectiles/items may outlive the animation; their owner
     * callbacks use Rogue_AbilityVars() to reach persistent source state.
     */
    switch (source) {
    case Ft_Kind_Donkey:
        if (slot == ROGUE_ABILITY_UP)
            ftDk_SpecialHi_DestroyAllEffects(fp->gobj);
        break;
    case Ft_Kind_GameWatch:
        ftGw_Init_OnDamage(fp->gobj);
        break;
    case Ft_Kind_Samus:
        if (slot == ROGUE_ABILITY_NEUTRAL)
            ftSamus_UnkAndDestroyAllEF(fp->gobj);
        break;
    case Ft_Kind_Ness:
        /* Smash attacks: the yo-yo and the bat go with the move. */
        if (S->normal_on) {
            ftNs_AttackHi4_YoyoItemDespawn(fp->gobj);
            ftNs_AttackS4_ItemNessBatRemove(fp->gobj);
        }
        break;
    case Ft_Kind_Mewtwo:
        if (slot == ROGUE_ABILITY_NEUTRAL) {
            int charge = fp->u.mt.x2234_shadowBallCharge;
            ftMt_SpecialN_OnDeath(fp->gobj);
            fp->u.mt.x2234_shadowBallCharge = charge;
        }
        break;
    case Ft_Kind_Peach:
        if (slot == ROGUE_ABILITY_NEUTRAL)
            ftPe_SpecialN_OnDeath2(fp->gobj);
        else if (slot == ROGUE_ABILITY_UP)
            ftPe_8011D598(fp->gobj);
        break;
    case Ft_Kind_Seak:
        if (slot == ROGUE_ABILITY_SIDE)
            ftSk_SpecialS_CheckAndDestroyChain(fp->gobj);
        break;
    case Ft_Kind_Captain:
    case Ft_Kind_Ganon:
        if (slot == ROGUE_ABILITY_SIDE)
            ftCa_SpecialS_RemoveGFX(fp->gobj);
        break;
    default:
        break;
    }

    if ((source == Ft_Kind_Mario || source == Ft_Kind_DrMario) &&
        slot == ROGUE_ABILITY_SIDE)
        ftMr_SpecialS_RemoveCape(fp->gobj);

    if ((source == Ft_Kind_Fox || source == Ft_Kind_Falco) &&
        (slot == ROGUE_ABILITY_NEUTRAL || S->normal_on))
        ftFx_SpecialN_RemoveBlaster(fp->gobj);
    /* Donkey Kong's cargo throw reads his attributes through x2CC. */
    if (S->normal_on) fp->x2CC = S->native_cargo;

    S->source_vars[source] = fp->u;
    fp->u = S->native_vars;
    memcpy(&fp->grab_cb, S->native_callbacks,
           sizeof(S->native_callbacks));
    fp->dat_attrs = S->native_attrs;
    fp->x24 = S->native_anims;
    fp->x28 = S->native_anim_flags;
    fp->x58C = S->native_anim_count;
    fp->reflecting = false;
    S->active = NULL;
    S->aerial = NULL;
    S->normal_on = false;
}

FighterKind Rogue_AbilitySourceKind(const Fighter* fp)
{
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    if (!fp) return Ft_Kind_Max;
    if (!Rogue_IsAbilityState(fp))
        return fp->kind;
    if (S->active != NULL)
        return S->active->internal_kind;
    if (S->normal_on) return (FighterKind) S->normal_donor;
    return S->aerial ? S->aerial->donor : fp->kind;
}

ftData* Rogue_AbilityData(Fighter* fp)
{
    FighterKind source;
    if (!Rogue_IsAbilityState(fp))
        return fp->ft_data;
    source = Rogue_AbilitySourceKind(fp);
    return source >= 0 && source < Ft_Kind_Max ? gFtDataList[source] : fp->ft_data;
}

static FighterKind abilityFamily(FighterKind kind)
{
    switch (kind) {
    case Ft_Kind_Falco: return Ft_Kind_Fox;
    case Ft_Kind_DrMario: return Ft_Kind_Mario;
    case Ft_Kind_CLink: return Ft_Kind_Link;
    case Ft_Kind_Pichu: return Ft_Kind_Pikachu;
    case Ft_Kind_Ganon: return Ft_Kind_Captain;
    case Ft_Kind_Emblem: return Ft_Kind_Mars;
    case Ft_Kind_Nana: return Ft_Kind_Popo;
    default: return kind;
    }
}

union Fighter_FighterVars* Rogue_AbilityVars(Fighter* fp, FighterKind family)
{
    int source;
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    if (S->fighter != fp) return &fp->u;
    family = abilityFamily(family);
    if (Rogue_IsAbilityState(fp) &&
        abilityFamily(Rogue_AbilitySourceKind(fp)) == family)
        return &fp->u;
    if (abilityFamily(fp->kind) == family)
        return Rogue_IsAbilityState(fp) ? &S->native_vars : &fp->u;
    /* Projectiles can outlive the animation which created them. Their owner
     * callbacks must update the source's persistent state, never the unrelated
     * base fighter's overlapping union fields. Prefer the equipped clone. */
    for (source = 0; source < ROGUE_ABILITY_SLOTS; ++source) {
        const RogueAbilityDefinition* def = Rogue_GetAbility(Rogue_EquippedSpecial(fp, source));
        if (def && abilityFamily(def->internal_kind) == family &&
            S->loaded_sources[def->internal_kind])
            return &S->source_vars[def->internal_kind];
    }
    for (source = 0; source < Ft_Kind_Max; ++source)
        if (abilityFamily(source) == family && S->loaded_sources[source])
            return &S->source_vars[source];
    return &fp->u;
}

void Rogue_AbilityFighterDestroyed(Fighter* fp)
{
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    if (S->fighter != fp || !fp) return;
    Rogue_AnimRetarget(fp, fp->kind, 0); /* Forget its body-bone mapping. */
    Rogue_SwordRelease(fp);
    Rogue_AbilityCleanup(fp);
    Rogue_AerialRelease(S);
#if BAM_DEBUG
    BAM_LOG("fighter_context_destroy kind=%u match=%u\n", fp->kind, S->match_generation);
#endif
    memset(S, 0, sizeof(*S));
}

/* Per frame: borrowed Fire Breath refills like Bowser's own (Bowser runs
 * ftKp_SpecialLw_80134D78 from his per-frame callback, which a borrower
 * does not have). The flame shrinks while breathing and regrows between
 * uses. */
void Rogue_AbilityFighterFrame(Fighter* fp)
{
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    const ftKoopaAttributes* da = (const ftKoopaAttributes*) bam_match->donor_attrs[Ft_Kind_Koopa].bytes;
    struct ftKoopa_FighterVars* fuel;
    if (S->fighter != fp || fp->kind == Ft_Kind_Koopa || !S->loaded_sources[Ft_Kind_Koopa]) return;
    if (Rogue_IsAbilityState(fp) && Rogue_AbilitySourceKind(fp) == Ft_Kind_Koopa) {
        /* Borrowed Bowser move in progress: its vars are installed. */
        if (fp->motion_id >= 0x155 && fp->motion_id < 0x15B) return;
        fuel = &fp->u.kp;
    } else fuel = &S->source_vars[Ft_Kind_Koopa].kp;
    fuel->x222C += da->x8;
    if (fuel->x222C > da->x10) fuel->x222C = da->x10;
    fuel->x2230 += da->xC;
    if (fuel->x2230 > da->x18) fuel->x2230 = da->x18;
}

void Rogue_AbilityMatchEnd(void)
{
    unsigned i;
    /* Retail defers fighter destruction until the next heap reset. Release our
     * donor context while its fighter and articles are still valid. */
    for (i = 0; i < BAM_FIGHTERS; ++i)
        if (bam_match->fighters[i].fighter) Rogue_AbilityFighterDestroyed(bam_match->fighters[i].fighter);
    Rogue_AnimScaleReset();
}

void Rogue_AbilityTransformed(Fighter* src, Fighter* dst)
{
    RogueFighterState* const S = Rogue_FighterCtx(src);
    if (!src || S->fighter != src || !Rogue_IsBuildFighter(dst)) return;
    Rogue_AbilityCleanup(src);
    /* Both native forms already exist; all borrowed assets and persistent
     * charge data belong to the borrower and survive the entity swap. */
    S->fighter = dst;
#if BAM_DEBUG
    BAM_LOG("fighter_transform from=%u to=%u match=%u\n",src->kind,dst->kind,bam_match->generation);
#endif
}

Fighter_GObj* Rogue_AbilityClimberPartner(Fighter* fp)
{
    Fighter_GObj* partner = Player_GetEntityAtIndex(fp->player_id, 1);
    if (partner && GET_FIGHTER(partner)->kind != Ft_Kind_Nana) return NULL;
    return partner;
}

MotionState* Rogue_AbilityMotionState(Fighter* fp, int motion)
{
    const RogueAbilityDefinition* def;
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    if (!Rogue_IsAbilityState(fp)) return NULL;


    if (S->normal_on) return Rogue_NormalMotionState(fp, motion);
    if (S->aerial) return Rogue_AerialMotionState(fp,motion);
    def = S->active;
    if (!def) return NULL;
    if (!Rogue_IsBuildFighter(fp) || motion < def->first_state || motion > def->last_state) {
        Rogue_AbilityCleanup(fp);
        return NULL;
    }
#if BAM_DEBUG || 0
    BAM_LOG("donor_motion id=%u motion=%d anim=%p\n", def->id, motion, def->states[motion-ftCo_MS_Count].anim_cb);
#endif
    return &def->states[motion - ftCo_MS_Count];
}

/* Raw table lookup: ftParts_GetBoneIndex itself falls back through here. */
static int part_joint(Fighter* fp, int part)
{
    const FighterPartsTable* table = ftPartsTable[fp->kind];
    int joint;
    if (part < 0 || part > FtPart_TransN2) return -1;
    joint = table->part_to_joint[part];
    if (joint == FTPART_INVALID || joint < 0 || (unsigned) joint >= table->parts_num || !fp->parts[joint].joint)
        return -1;
    return joint;
}
static float reach2(Fighter* fp, int joint, const Vec3* center)
{
    Vec3 p;
    lb_8000B1CC(fp->parts[joint].joint, NULL, &p);
    p.x -= center->x; p.y -= center->y; p.z -= center->z;
    return p.x * p.x + p.y * p.y + p.z * p.z;
}
int Rogue_AbilityFallbackJoint(Fighter* fp, int part)
{
    static const int tail[] = { FtPart_BustN, FtPart_HipN, FtPart_TransN };
    int joint = -1, left, right, chest;
    unsigned i;
    if (part >= FtPart_LShoulderN && part <= FtPart_LHandNb) {
        joint = part_joint(fp, FtPart_LHandN);
        if (joint < 0) joint = part_joint(fp, FtPart_LArmJ);
    } else if (part >= FtPart_RShoulderN && part <= FtPart_RHandNb) {
        joint = part_joint(fp, FtPart_RHandN);
        if (joint < 0) joint = part_joint(fp, FtPart_RArmJ);
    } else if (part == FtPart_NeckN || part == FtPart_HeadN) {
        joint = part_joint(fp, FtPart_HeadN);
    } else if (part >= FtPart_LLegJA && part <= FtPart_LFootJ) {
        joint = part_joint(fp, FtPart_LFootJ);
    } else if (part >= FtPart_RLegJA && part <= FtPart_RFootJ) {
        joint = part_joint(fp, FtPart_RFootJ);
    } else if (part < 0 || part > FtPart_TransN2) {
        /* A donor-only bone (sword, cannon, gun, prop): it rides on whichever
         * hand is extended furthest from the chest when the move needs it. */
        left = part_joint(fp, FtPart_LHandN);
        right = part_joint(fp, FtPart_RHandN);
        chest = part_joint(fp, FtPart_BustN);
        if (chest < 0) chest = part_joint(fp, FtPart_HipN);
        if (left >= 0 && right >= 0 && chest >= 0) {
            Vec3 center;
            lb_8000B1CC(fp->parts[chest].joint, NULL, &center);
            joint = reach2(fp, left, &center) > reach2(fp, right, &center) ? left : right;
        } else joint = left >= 0 ? left : right;
    }
    for (i = 0; joint < 0 && i < sizeof(tail) / sizeof(tail[0]); ++i) joint = part_joint(fp, tail[i]);
    return joint >= 0 ? joint : 0;
}
int Rogue_AbilityMapBone(Fighter* fp, int bone)
{
    int mapped, part = -1;
    unsigned i;
    FighterKind source;
    RogueFighterState* S;
    const FighterPartsTable* from;
    if (!Rogue_IsAbilityState(fp)) return bone;
    source = Rogue_AbilitySourceKind(fp);
    /* A donor bone rebuilt on the recipient (Marth's sword, Kirby's hammer
     * bone): the body part it hangs from; hitboxes on it follow the rebuilt
     * bone (anim_scale.c). Checked first: Kirby's hammer bone shares its
     * body-part slot with other fighters' thumbs. */
    mapped = Rogue_PropJoint(fp, bone);
    if (mapped >= 0) return mapped;
    mapped = ftPartsRemap(fp->kind, source, bone);
    if (mapped >= 0 && (unsigned) mapped < ftPartsTable[fp->kind]->parts_num && fp->parts[mapped].joint)
        return mapped;
    /* The recipient lacks this donor bone. Resolve it once per borrowed move
     * to the nearest body part it does have (never the root at the feet). */
    S = Rogue_FighterCtx(fp);
    for (i = 0; i < S->fallback_count; ++i)
        if (S->fallback_bone[i] == bone) return S->fallback_joint[i];
    from = ftPartsTable[source];
    if (bone >= 0 && (unsigned) bone < from->parts_num && from->joint_to_part[bone] != FTPART_INVALID)
        part = from->joint_to_part[bone];
    mapped = Rogue_AbilityFallbackJoint(fp, part);
#if BAM_DEBUG
    BAM_LOG("bone_fallback donor=%u bone=%d part=%d recipient=%u joint=%d\n",
        source, bone, part, fp->kind, mapped);
#endif
    if (S->fallback_count < sizeof(S->fallback_bone) / sizeof(S->fallback_bone[0])) {
        S->fallback_bone[S->fallback_count] = (short) bone;
        S->fallback_joint[S->fallback_count++] = (short) mapped;
    }
    return mapped;
}

void Rogue_BorrowBegin(Fighter* fp, FighterKind donor)
{
    ftData* source;
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    Rogue_AbilityCleanup(fp);
    S->native_attrs = fp->dat_attrs;
    S->native_anims = fp->x24;
    S->native_anim_flags = fp->x28;
    S->native_anim_count = fp->x58C;
    S->native_vars = fp->u;
    memcpy(S->native_callbacks, &fp->grab_cb,
           sizeof(S->native_callbacks));
    fp->u = S->source_vars[donor];
    source = gFtDataList[donor];
    S->fallback_count = 0; /* A new borrowed move resolves its bones afresh. */
    {
        extern void Rogue_VisReset(const Fighter* fp);
        Rogue_VisReset(fp);
    }
    fp->dat_attrs = bam_match->donor_attrs[donor].bytes;
    /* The private table holds the sliced animations when the donor's full
     * archive is not resident (special_preload.c). */
    fp->x24 = S->aerial_anims[donor] ? S->aerial_anims[donor] : source->xC;
    fp->x28 = source->x10;
    fp->x58C = ftData_Table_Unk0[donor].count;
}

static bool install_ability(Fighter* fp, RogueAbilitySlot slot)
{
    const RogueAbilityDefinition* def;
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    if (!Rogue_IsBuildFighter(fp) || slot < 0 || slot >= ROGUE_ABILITY_SLOTS) return false;
    def = Rogue_GetAbility(Rogue_EquippedSpecial(fp, slot));
    if (!def) { Rogue_AbilityCleanup(fp); return false; }
    if (!def->ground_enter || !def->air_enter) return false;
    if (def->native_slot != slot || S->fighter != fp ||
        !S->loaded[def->id]) return false;
    Rogue_BorrowBegin(fp, def->internal_kind);
    S->active = def;
    /* Source animation flags already identify the source skeleton. Melee's
     * ftPartsRemap path retargets its FigaTree to the unchanged base fighter. */
    return true;
}

bool Rogue_CanTrySpecial(Fighter* fp, RogueAbilitySlot slot)
{
    const RogueAbilityDefinition* def;
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    if (!Rogue_IsBuildFighter(fp) || slot < 0 || slot >= ROGUE_ABILITY_SLOTS) return false;
    def = Rogue_GetAbility(Rogue_EquippedSpecial(fp, slot));
    return def && def->ground_enter && def->air_enter && def->native_slot == slot &&
        S->fighter == fp && S->loaded[def->id];
}
bool Rogue_TrySpecial(Fighter_GObj* gobj, RogueAbilitySlot slot, bool airborne)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!install_ability(fp, slot)) return false;
    {
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    (airborne ? S->active->air_enter : S->active->ground_enter)(gobj);
    }
    return true;
}

bool Rogue_AbilityResumeFamily(Fighter* fp, FighterKind family, RogueAbilitySlot slot)
{
    const RogueAbilityDefinition* def;
    if (!Rogue_IsBuildFighter(fp)) return false;
    def = Rogue_GetAbility(Rogue_EquippedSpecial(fp, slot));
    if (def && abilityFamily(def->internal_kind) == abilityFamily(family))
        return install_ability(fp, slot);
    if (abilityFamily(fp->kind) == abilityFamily(family)) {
        Rogue_AbilityCleanup(fp);
        return true;
    }
    return false;
}

bool Rogue_AbilityOwnerResume(Fighter* fp, FighterKind family, RogueAbilitySlot slot)
{
    if (!fp) return false;
    if (!Rogue_IsBuildFighter(fp)) return abilityFamily(fp->kind) == abilityFamily(family);
    return Rogue_AbilityResumeFamily(fp, family, slot);
}

int Rogue_AbilityPartIndex(Fighter* fp, int part)
{
    int index = ftParts_GetBoneIndex(fp, part);
    if (Rogue_IsBuildFighter(fp) && ((unsigned) index >= ftPartsTable[fp->kind]->parts_num || !fp->parts[index].joint))
        return Rogue_AbilityFallbackJoint(fp, part);
    return index;
}

/* A joint two effects name by the donor's joint number (efsync.c 0x501,
 * Zelda's up smash: joint 85; efalt.c 0x494: joint 44): the borrower's
 * equivalent while it borrows, never past its joint table (on Pikachu,
 * joint 85 read past the table and the spark spawned there crashed).
 * inject: after the retail load, returns the joint to use. */
HSD_JObj* Rogue_EfJointFp(Fighter* fp, HSD_JObj* loaded, int joint)
{
    unsigned n;
    if (!fp) return loaded;
    n = ftPartsTable[fp->kind]->parts_num;
    if (!Rogue_IsAbilityState(fp) && (unsigned) joint < n) return loaded;
    if (Rogue_IsAbilityState(fp)) joint = Rogue_AbilityMapBone(fp, joint);
    if (joint < 0 || (unsigned) joint >= n || !fp->parts[joint].joint) joint = 0;
    return fp->parts[joint].joint;
}
HSD_JObj* Rogue_EfJoint85(Fighter* fp, HSD_JObj* loaded) { return Rogue_EfJointFp(fp, loaded, 85); }
HSD_JObj* Rogue_EfJoint44(Fighter* fp, HSD_JObj* loaded) { return Rogue_EfJointFp(fp, loaded, 44); }
