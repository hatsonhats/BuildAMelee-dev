/* Borrowed ground attacks and throws.
 *
 * Each of the twelve slots (jab, dash attack, the three tilts, the three
 * smash attacks, the four throws) can hold another character's move. It is
 * borrowed the way the aerials are: where the game picks the move for the
 * slot (overrides/platform_fixes.toml, the "normal-*" fixes), the donor's
 * attributes, animation table, move variables and callbacks are installed
 * (Rogue_BorrowBegin) and the game then runs the move as that character
 * would. A slot's "family" is every motion the move can pass through: the
 * common ones (Attack11..Attack100End for the jab, AttackS4Hi..AttackS4Lw
 * for the forward smash) and the donor's own (Mr. Game & Watch's jab and
 * down tilt, Ness's yo-yo and bat, Peach's club, pan and racket, Link's
 * second forward smash swing, Kirby's dash attack, Donkey Kong's cargo
 * carry). Leaving the family ends the borrowed move (Rogue_AbilityMotionState).
 *
 * The game's checks of the fighter's kind at those points read the move's
 * owner instead (Rogue_AbilitySourceKind), so a jab from Pikachu chains into
 * his rapid jab and a forward throw from Mewtwo shoots his Shadow Balls.
 *
 * Throws: the grabbed fighter plays its "thrown" animations from the
 * thrower's animation table (Fighter_ChangeMotionState's arg3), so the
 * donor's are read too, as are the victim's shouldered animations for
 * Donkey Kong's cargo carry.
 *
 * Memory: a slot loads its donor's data (shared with the donor's specials
 * and aerials) and the animations of its family only. A slot that does not
 * fit keeps the fighter's own move. */
#include <engine/special_internal.h>
#include <melee/ft/kinds/ftGameWatch/forward.h>
#include <melee/ft/kinds/ftNess/forward.h>
#include <melee/ft/kinds/ftPeach/forward.h>
#include <melee/ft/kinds/ftLink/forward.h>
#include <melee/ft/kinds/ftKirby/forward.h>
#include <melee/ft/kinds/ftDonkey/forward.h>
#include <melee/ft/kinds/ftDonkey/types.h>

#define ANY_DONOR 0xFF
typedef struct NormalRange {
    unsigned char slot, donor;
    short first, last;
} NormalRange;

static const NormalRange ranges[] = {
    { BAM_NORMAL_JAB, ANY_DONOR, ftCo_MS_Attack11, ftCo_MS_Attack100End },
    { BAM_NORMAL_JAB, Ft_Kind_GameWatch, ftGw_MS_Attack11, ftGw_MS_Attack100End },
    { BAM_NORMAL_DASH, ANY_DONOR, ftCo_MS_AttackDash, ftCo_MS_AttackDash },
    { BAM_NORMAL_DASH, Ft_Kind_Kirby, ftKb_MS_AttackDash, ftKb_MS_AttackDashAir },
    { BAM_NORMAL_FTILT, ANY_DONOR, ftCo_MS_AttackS3Hi, ftCo_MS_AttackS3Lw },
    { BAM_NORMAL_UTILT, ANY_DONOR, ftCo_MS_AttackHi3, ftCo_MS_AttackHi3 },
    { BAM_NORMAL_DTILT, ANY_DONOR, ftCo_MS_AttackLw3, ftCo_MS_AttackLw3 },
    { BAM_NORMAL_DTILT, Ft_Kind_GameWatch, ftGw_MS_AttackLw3, ftGw_MS_AttackLw3 },
    { BAM_NORMAL_FSMASH, ANY_DONOR, ftCo_MS_AttackS4Hi, ftCo_MS_AttackS4Lw },
    { BAM_NORMAL_FSMASH, Ft_Kind_Ness, ftNs_MS_AttackS4, ftNs_MS_AttackS4 },
    { BAM_NORMAL_FSMASH, Ft_Kind_Peach, ftPe_MS_AttackS4Club, ftPe_MS_AttackS4Racket },
    { BAM_NORMAL_FSMASH, Ft_Kind_GameWatch, ftGw_MS_AttackS4, ftGw_MS_AttackS4 },
    { BAM_NORMAL_FSMASH, Ft_Kind_Link, ftLk_MS_AttackS42, ftLk_MS_AttackS42 },
    { BAM_NORMAL_FSMASH, Ft_Kind_CLink, ftLk_MS_AttackS42, ftLk_MS_AttackS42 },
    { BAM_NORMAL_USMASH, ANY_DONOR, ftCo_MS_AttackHi4, ftCo_MS_AttackHi4 },
    { BAM_NORMAL_USMASH, Ft_Kind_Ness, ftNs_MS_AttackHi4, ftNs_MS_AttackHi4Release },
    { BAM_NORMAL_DSMASH, ANY_DONOR, ftCo_MS_AttackLw4, ftCo_MS_AttackLw4 },
    { BAM_NORMAL_DSMASH, Ft_Kind_Ness, ftNs_MS_AttackLw4, ftNs_MS_AttackLw4Release },
    { BAM_NORMAL_FTHROW, ANY_DONOR, ftCo_MS_ThrowF, ftCo_MS_ThrowF },
    { BAM_NORMAL_FTHROW, Ft_Kind_Donkey, ftDk_MS_ThrowFWait0, ftDk_MS_ThrowAirFLw },
    { BAM_NORMAL_BTHROW, ANY_DONOR, ftCo_MS_ThrowB, ftCo_MS_ThrowB },
    { BAM_NORMAL_UTHROW, ANY_DONOR, ftCo_MS_ThrowHi, ftCo_MS_ThrowHi },
    { BAM_NORMAL_DTHROW, ANY_DONOR, ftCo_MS_ThrowLw, ftCo_MS_ThrowLw },
};
#define RANGE_COUNT (sizeof(ranges) / sizeof(ranges[0]))

/* The thrown fighter's motions a throw plays from the thrower's table. */
static const NormalRange victim_ranges[] = {
    { BAM_NORMAL_FTHROW, ANY_DONOR, ftCo_MS_ThrownF, ftCo_MS_ThrownF },
    { BAM_NORMAL_FTHROW, Ft_Kind_Donkey, ftCo_MS_ShoulderedWait, ftCo_MS_ThrownFLw },
    { BAM_NORMAL_BTHROW, ANY_DONOR, ftCo_MS_ThrownB, ftCo_MS_ThrownB },
    { BAM_NORMAL_UTHROW, ANY_DONOR, ftCo_MS_ThrownHi, ftCo_MS_ThrownHi },
    { BAM_NORMAL_DTHROW, ANY_DONOR, ftCo_MS_ThrownLw, ftCo_MS_ThrownlwWomen },
};
#define VICTIM_COUNT (sizeof(victim_ranges) / sizeof(victim_ranges[0]))

static bool range_has(const NormalRange* r, unsigned n, int slot, unsigned donor, int motion)
{
    unsigned i;
    for (i = 0; i < n; ++i)
        if (r[i].slot == slot && (r[i].donor == ANY_DONOR || r[i].donor == donor) && motion >= r[i].first &&
            motion <= r[i].last)
            return true;
    return false;
}

static bool in_family(int slot, unsigned donor, int motion)
{
    return range_has(ranges, RANGE_COUNT, slot, donor, motion);
}

static const MotionState* family_state(Fighter* fp, unsigned donor, int motion)
{
    const RogueAbilityDefinition* def;
    if (motion < 0) return NULL;
    if (motion < ftCo_MS_Count) return &fp->x1C_actionStateList[motion];
    def = Rogue_GetAbility(1 + donor * 4);
    if (!def || motion > def->last_state) return NULL;
    return &def->states[motion - ftCo_MS_Count];
}

FighterKind Rogue_NormalBegin(Fighter_GObj* gobj, int slot)
{
    Fighter* fp = GET_FIGHTER(gobj);
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    unsigned donor;
    if (S->fighter != fp || slot < 0 || slot >= BAM_NORMAL_SLOTS || !S->normals[slot] || !Rogue_IsBuildFighter(fp)) {
        /* The fighter's own move: whatever was borrowed ends here. */
        Rogue_AbilityCleanup(fp);
        return fp->kind;
    }
    donor = S->normals[slot] - 1U;
    if (S->normal_on && S->normal_slot == slot && S->normal_donor == donor) {
        /* The move starting over (Mr. Game & Watch's down tilt): keep it. */
        S->normal_fresh = true;
        return (FighterKind) donor;
    }
    Rogue_BorrowBegin(fp, (FighterKind) donor);
    S->native_cargo = fp->x2CC;
    if (donor == Ft_Kind_Donkey) fp->x2CC = (ftDonkeyAttributes*) bam_match->donor_attrs[Ft_Kind_Donkey].bytes;
    S->normal_on = true;
    S->normal_slot = (signed char) slot;
    S->normal_donor = (unsigned char) donor;
    S->normal_fresh = true;
#if BAM_DEBUG
    BAM_LOG("normal_enter slot=%d donor=%u recipient=%u match=%u\n", slot, donor, fp->kind,
             S->match_generation);
#endif
    return (FighterKind) donor;
}

/* Fighter_ChangeMotionState during a borrowed ground attack. A donor's own
 * motion (341+) is accepted only as the move's first motion or from another
 * motion of the move: the fighter's own motions share those numbers
 * (Yoshi's shield is 341, as is Ness's forward smash). */
MotionState* Rogue_NormalMotionState(Fighter* fp, int motion)
{
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    const MotionState* state = NULL;
    unsigned donor = S->normal_donor;
    int slot = S->normal_slot;
    bool fresh = S->normal_fresh;
    S->normal_fresh = false;
    if (Rogue_IsBuildFighter(fp) && in_family(slot, donor, motion) &&
        (motion < ftCo_MS_Count || fresh || in_family(slot, donor, fp->motion_id)))
        state = family_state(fp, donor, motion);
    if (!state) {
        Rogue_AbilityCleanup(fp);
        return NULL;
    }
    return (MotionState*) state;
}

struct ftCo_DatAttrs* Rogue_NormalCoAttrs(Fighter* fp)
{
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    if (S->fighter == fp && S->normal_on && gFtDataList[S->normal_donor] && gFtDataList[S->normal_donor]->x0)
        return gFtDataList[S->normal_donor]->x0;
    return &fp->co_attrs;
}

/* The animations of a slot's family for one donor (and its throw victims). */
#define MAX_ANIMS 48
static unsigned family_anims(Fighter* fp, int slot, unsigned donor, short* out)
{
    unsigned n = 0, i;
    int m;
    for (i = 0; i < RANGE_COUNT + VICTIM_COUNT; ++i) {
        const NormalRange* r = i < RANGE_COUNT ? &ranges[i] : &victim_ranges[i - RANGE_COUNT];
        if (r->slot != slot || (r->donor != ANY_DONOR && r->donor != donor)) continue;
        for (m = r->first; m <= r->last && n < MAX_ANIMS; ++m) {
            const MotionState* st = family_state(fp, donor, m);
            if (st && st->anim_id >= 0) out[n++] = (short) st->anim_id;
        }
    }
    return n;
}

void Rogue_NormalPrepare(Fighter* fp)
{
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    const BamLoadout* l;
    int slot;
    if (S->fighter != fp || fp->player_id >= BAM_PLAYER_SLOTS) return;
    l = &bam_loadouts[fp->player_id];
    for (slot = 0; slot < BAM_NORMAL_SLOTS; ++slot) {
        unsigned ck = l->normals[slot], n;
        FighterKind donor;
        short anims[MAX_ANIMS];
        S->normals[slot] = 0;
        if (!ck || ck > CKind_Playable_Count) continue;
        donor = Rogue_InternalKindForCharacter((CharacterKind) (ck - 1));
        if (donor >= Ft_Kind_Max || donor == fp->kind) continue;
        if (!Rogue_DonorEnsure(S, donor)) {
            BAM_NOTE("normal_skipped slot=%d kind=%u (out of memory)\n", slot, donor);
            continue;
        }
        n = family_anims(fp, slot, donor, anims);
        if (!Rogue_DonorReadAnims(S, donor, anims, n, "normal_slices")) {
            BAM_NOTE("normal_skipped slot=%d kind=%u (no memory for animations)\n", slot, donor);
            continue;
        }
        S->normals[slot] = (unsigned char) (donor + 1);
#if BAM_DEBUG
        BAM_LOG("normal_ready slot=%d donor=%u recipient=%u\n", slot, donor, fp->kind);
#endif
    }
}

/* Ground attacks and throws that spawn the donor's articles. */
static bool normal_needs_articles(FighterKind kind, int slot)
{
    bool is_throw = slot >= BAM_NORMAL_FTHROW && slot <= BAM_NORMAL_DTHROW;
    switch (kind) {
    case Ft_Kind_GameWatch: /* bug spray, manhole, torch */
        return slot == BAM_NORMAL_JAB || slot == BAM_NORMAL_DTILT || slot == BAM_NORMAL_FSMASH;
    case Ft_Kind_Ness: /* bat, yo-yo */
        return slot == BAM_NORMAL_FSMASH || slot == BAM_NORMAL_USMASH || slot == BAM_NORMAL_DSMASH;
    case Ft_Kind_Samus:   /* grapple beam */
    case Ft_Kind_Fox:     /* blaster */
    case Ft_Kind_Falco:
    case Ft_Kind_Mewtwo:  /* Shadow Balls */
        return is_throw;
    default:
        return false;
    }
}

int Rogue_DonorNeedsArticles(int kind)
{
    int p;
    unsigned s;
    for (p = 0; p < BAM_PLAYER_SLOTS; ++p) {
        const BamLoadout* l = &bam_loadouts[p];
        if (!l->enabled) continue;
        /* Specials: always (most spawn articles). Zelda's and Sheik's are
         * loaded together for the transformation. */
        for (s = 0; s < BAM_SPECIAL_SLOTS; ++s) {
            const RogueAbilityDefinition* d = Rogue_GetAbility(l->specials[s]);
            if (!d) continue;
            if (d->internal_kind == kind ||
                ((kind == Ft_Kind_Zelda || kind == Ft_Kind_Seak) &&
                 (d->internal_kind == Ft_Kind_Zelda || d->internal_kind == Ft_Kind_Seak)))
                return 1;
        }
        /* Aerials: Mr. Game & Watch's parachute, turtle and sausages. */
        for (s = 0; s < BAM_AERIAL_SLOTS; ++s) {
            const RogueAerialDef* d = RogueAerial_Find(l->aerials[s]);
            if (d && d->donor == (unsigned) kind && kind == Ft_Kind_GameWatch) return 1;
        }
        for (s = 0; s < BAM_NORMAL_SLOTS; ++s) {
            unsigned v = l->normals[s];
            if (v && v <= CKind_Playable_Count && Rogue_InternalKindForCharacter((CharacterKind) (v - 1)) == kind &&
                normal_needs_articles((FighterKind) kind, (int) s))
                return 1;
        }
    }
    return 0;
}
