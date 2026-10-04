/* QA move sweep (QA builds only, never shipped).
 *
 * Plays every move slot of every character with every donor, back to back,
 * with no menus: VS matches on Final Destination chained from the end of one
 * match straight into the next (onExitVs/onExitCss overridden in QA builds).
 *
 * Match list (deterministic, see match_pair):
 *   0..25            native: character k with its own moves (hitbox baseline)
 *   26..             recipient R, donor D (every R, every other D): every
 *                    slot of the loadout borrowed from D at once (one donor's
 *                    files per match, like the most common real loadout).
 * Steps per match: 12 ground attacks / throws, the donor's 5 aerials, its
 * specials (up to 4; last, as a down-B can leave an item in hand).
 *
 * Port 1 is driven by inputs (qa_bot.c reads qa_drive_*), port 2 is an idle
 * human dummy. Each step: both fighters made idle and placed, the move's
 * inputs played, then wait for port 1 to stand idle again.
 *
 * Log lines (OSReport, parsed by tools/qa/sweep.py):
 *   [qa] M m R D steps            match start
 *   [qa] S m s                    step start (heartbeat for the watchdog)
 *   [qa] H m s f ms i r x y dmg   a hitbox appeared (rel. to fighter, facing right)
 *   [qa] B m s scale              port 1 borrowing during the step
 *   [qa] C m s gap                closest approach of its hitboxes to the dummy
 *   [qa] I m s f kind x y r       its projectile's first hitbox (rel. to it)
 *   [qa] R m s res frames bor dmg ms0 ms1 ms2 ms3
 *   [qa] E m                      match done
 * The sweep starts at qa_resume (match * 32 + step), patched into the ISO by
 * the runner after a crash or freeze.
 */
#include "../bam/bam.h"
#include <engine/special_internal.h>
#include <engine/special_catalog.h>
#include <engine/aerial_catalog.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftcommon.h>
#include <melee/gm/types.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvsmode.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/types.h>
#include <melee/mn/types.h>
#include <melee/pl/player.h>
#include <dolphin/os.h>
#include <string.h>
#include <math.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/gobj.h>
#include <melee/it/types.h>
#include <melee/it/inlines.h>
#include <melee/it/forward.h>
#include <melee/ft/fighter.h>
#include <melee/lb/lb_00B0.h>
#include <melee/ft/ftdata.h>

/* Patched by tools/qa/sweep.py (the word right after the marker). */
volatile u32 qa_resume_block[3] = { 0x51415253 /* 'QARS' */, 0, 0xFFFF };
#define QA_RESUME (qa_resume_block[1])
#define QA_END (qa_resume_block[2])

/* Inputs for port 1's next poll (qa_bot.c). */
int qa_drive_on;
u32 qa_drive_buttons;
s8 qa_drive_x, qa_drive_y, qa_drive_cx, qa_drive_cy;

#define NCHARS 26
#define NATIVE_MATCHES NCHARS
#define DUMMY CKind_Mario
#define STAGE 0x20 /* St_Kind_Last: Final Destination */

enum { K_NORMAL, K_SPECIAL, K_AERIAL };
typedef struct Step { u8 kind, slot, id; } Step;

static int same_family(int a, int b)
{
    if (a == b) return 1;
    return (a == CKind_Zelda || a == CKind_Seak) && (b == CKind_Zelda || b == CKind_Seak);
}

/* Match m -> recipient R and donor D (D < 0: native). 0 past the end. */
static int match_pair(unsigned m, int* R, int* D)
{
    unsigned n = 0;
    int r, d;
    if (m < NATIVE_MATCHES) { *R = (int) m; *D = -1; return 1; }
    m -= NATIVE_MATCHES;
    for (r = 0; r < NCHARS; ++r)
        for (d = 0; d < NCHARS; ++d) {
            if (same_family(r, d)) continue;
            if (n++ == m) { *R = r; *D = d; return 1; }
        }
    return 0;
}

static const RogueSpecialDef* special_of(int ck, unsigned slot)
{
    unsigned i;
    for (i = 0; i < ROGUE_SPECIALS; ++i) {
        const RogueSpecialDef* s = &rogue_specials[i];
        if (s->character == ck && s->slot == slot && RogueSpecial_Offerable(s) && Rogue_GetAbility(s->id))
            return s;
    }
    return NULL;
}

static const RogueAerialDef* aerial_of(int ck, unsigned slot)
{
    unsigned i;
    for (i = 0; i < ROGUE_AERIALS; ++i)
        if (rogue_aerials[i].character == ck && rogue_aerials[i].slot == slot) return &rogue_aerials[i];
    return NULL;
}

/* The steps of a match whose moves come from character `from`. */
static unsigned build_steps(int from, Step* out)
{
    unsigned n = 0, s;
    for (s = 0; s < BAM_NORMAL_SLOTS; ++s) { out[n].kind = K_NORMAL; out[n].slot = (u8) s; out[n].id = 0; ++n; }
    for (s = 0; s < BAM_AERIAL_SLOTS; ++s) {
        const RogueAerialDef* d = aerial_of(from, s);
        if (!d) continue;
        out[n].kind = K_AERIAL; out[n].slot = (u8) s; out[n].id = d->id; ++n;
    }
    for (s = 0; s < BAM_SPECIAL_SLOTS; ++s) {
        const RogueSpecialDef* d = special_of(from, s);
        if (!d) continue;
        out[n].kind = K_SPECIAL; out[n].slot = (u8) s; out[n].id = d->id; ++n;
    }
    return n;
}

/* ---- sweep state ---------------------------------------------------------- */

static unsigned cur_match, cur_step, nsteps;
static int cur_R, cur_D, started, finished;
static Step steps[32];

enum { PH_PREP, PH_PLACE, PH_RUN, PH_DONE };
static int phase;
static unsigned pf;              /* frames in the phase */
static unsigned rf;              /* frames since the move's input */
static int z_at, grab_tries;
static int grab_at, air_at, idle_frames, saw_borrow, logged_scale;
static float p2_start;
static s16 ms_seen[4];
static unsigned ms_count;
static int stocks_at_start;
static u32 hit_sig[4];

static void set_loadout(int D)
{
    BamLoadout* l = &bam_loadouts[0];
    unsigned i;
    memset(bam_loadouts, 0, sizeof(bam_loadouts));
    if (D < 0) return;
    for (i = 0; i < BAM_NORMAL_SLOTS; ++i) l->normals[i] = (u8) (D + 1);
    for (i = 0; i < BAM_SPECIAL_SLOTS; ++i) {
        const RogueSpecialDef* d = special_of(D, i);
        l->specials[i] = d ? d->id : 0;
    }
    for (i = 0; i < BAM_AERIAL_SLOTS; ++i) {
        const RogueAerialDef* d = aerial_of(D, i);
        l->aerials[i] = d ? d->id : 0;
    }
    l->enabled = 1;
}

static void qa_frame(void);
void QA_DumpRest(Fighter* fp);

/* Fill the VS data for match m (and the loadout); 0 when the sweep is over. */
static int setup_match(unsigned m)
{
    VsModeData* vs = &gmMainLib_804D3EE0->modes.vs_melee;
    StartMeleeData* st = &vs->start;
    int R, D, p;
    if (m >= QA_END || !match_pair(m, &R, &D)) return 0;
    cur_match = m; cur_R = R; cur_D = D;
    nsteps = build_steps(D < 0 ? R : D, steps);
    set_loadout(D);
    /* gmVsMelee_EnterVs copies the game's VS rules and item settings over
     * these (gm_80167BC8): set those too. Stock, 99 stocks, no timer, no
     * items (they spawned, exploded under port 1 and got picked up). */
    {
        GameRules* gr = gmMainLib_GetGameRules();
        struct GamePrefs* pr = gmMainLib_GetGamePrefs();
        gr->mode = 1;
        gr->stock_count = 99;
        gr->stock_time_limit = 0;
        gr->time_limit = 0;
        gr->handicap = 0;
        gr->damage_ratio = 10;
        gr->friendly_fire = 0;
        pr->item_freq = 0xFF;
        pr->item_mask = 0;
    }
    st->rules.match_kind = 1;           /* stock */
    st->rules.timer_enabled = 0;
    st->rules.is_teams = 0;
    st->rules.item_freq = -1;
    st->rules.x20 = 0; /* no item kinds either */
    st->rules.stkind = STAGE;
    st->rules.x30 = 1.0f;
    st->rules.game_speed = 1.0f;
    st->rules.on_frame_end = qa_frame;
    for (p = 0; p < 4; ++p) {
        PlayerInitData* pl = &st->players[p];
        pl->slot_type = 3; /* none */
    }
    for (p = 0; p < 2; ++p) {
        PlayerInitData* pl = &st->players[p];
        pl->ckind = (s8) (p ? DUMMY : R);
        pl->slot_type = 0; /* human */
        pl->stocks = 99;
        pl->color = 0;
        pl->team = (u8) p;
        pl->handicap = 9;
        pl->cpu_level = 1;
        pl->damage = 0;
        pl->attack_ratio = pl->defense_ratio = pl->model_scale = 1.0f;
    }
    OSReport("[qa] M %u %d %d %u\n", m, R, D, nsteps);
    cur_step = 0;
    if (m == QA_RESUME >> 5) cur_step = QA_RESUME & 31;
    phase = PH_PREP; pf = 0; finished = 0;
    return 1;
}

static int sweep_over;

/* The next match into the VS data; 0 when the sweep is over. */
static int next_match(void)
{
    unsigned m = started ? cur_match + 1 : QA_RESUME >> 5;
    started = 1;
    if (!setup_match(m)) {
        if (!sweep_over) OSReport("[qa] DONE\n");
        sweep_over = 1;
        return 0;
    }
    return 1;
}

/* Each match goes through the CSS for a few frames, as in play: the CSS
 * points the preload cache at the players (and starts loading them), which
 * changing the cache between scenes does not do safely. */
static int css_frames;

/* override: onExitCss (QA). */
void QA_OnExitCss(GameModeState* state)
{
    (void) state;
    gm_SetNextGameModeStateId(sweep_over ? gmVsMode_State_Css : gmVsMode_State_Vs);
}

/* override: onExitVs (QA). No results screen: the next match's CSS. */
void QA_OnExitVs(GameModeState* state)
{
    (void) state;
    OSReport("[qa] E %u\n", cur_match);
    next_match();
    css_frames = 0;
    gm_SetNextGameModeStateId(gmVsMode_State_Css);
}

/* ---- per frame ------------------------------------------------------------ */

static Fighter* fighter(int slot)
{
    HSD_GObj* g = Player_GetEntity(slot);
    return g ? GET_FIGHTER(g) : NULL;
}

static void place(Fighter* fp, float x)
{
    fp->cur_pos.x = x;
    fp->coll_data.cur_pos.x = fp->coll_data.prev_pos.x = fp->coll_data.last_pos.x = x;
    fp->self_vel.x = fp->self_vel.y = 0;
    fp->gr_vel = 0;
}

/* Port 1 keeps the way it faces (turning it by hand leaves its model and
 * grab box the other way); the dummy goes in front of it. */
static float face = 1.0f;

static int idle(Fighter* fp)
{
    return fp->motion_id == ftCo_MS_Wait && fp->ground_or_air == GA_Ground;
}

static float gap_native(const Step* s)
{
    if (s->kind == K_NORMAL) {
        if (s->slot == BAM_NORMAL_DASH) return 38;
        if (s->slot >= BAM_NORMAL_FTHROW) return 9;
        return 12;
    }
    return 14;
}

/* The dummy's distance. A borrowed move is drawn at Rogue_ShownScale of the
 * donor's own size, so its reach is too: the gap to the dummy's near side
 * (its half width, about DUMMY_HALF) scales with it, and a move that reaches
 * the dummy for the donor reaches it here. Only closer, never further: a
 * bigger move does not reach further up or down, and a hit that only just
 * landed for the donor then missed. Not for throws (a grab range) nor dash
 * attacks (a run-up). */
#define DUMMY_HALF 3.0f
static Fighter* gap_fp;
static float gap_for(const Step* s)
{
    float g = gap_native(s), k;
    if (cur_D < 0 || !gap_fp || (s->kind == K_NORMAL && (s->slot >= BAM_NORMAL_FTHROW || s->slot == BAM_NORMAL_DASH)))
        return g;
    k = Rogue_ShownScale(gap_fp->kind, Rogue_InternalKindForCharacter((CharacterKind) cur_D));
    if (k <= 0.0f || k >= 1.0f) return g;
    return DUMMY_HALF + (g - DUMMY_HALF) * k;
}

static void input_clear(void)
{
    qa_drive_buttons = 0;
    qa_drive_x = qa_drive_y = qa_drive_cx = qa_drive_cy = 0;
}

/* Port 1 is in the move being tested (or any move of its own). */
static int busy(Fighter* fp)
{
    RogueFighterState* S = Rogue_FighterCtx(fp);
    return Rogue_IsAbilityState(fp) || (S->fighter == fp && (S->active || S->aerial || S->normal_on)) ||
           fp->motion_id >= 341;
}

static void drive_raw(const Step* s, Fighter* fp);
/* Every step starts from the same random seed, so a move that rolls (Peach's
 * forward smash item, Judgment's number, Green Missile's misfire) rolls the
 * same for the donor's own run and every borrowed one. */
#define QA_SEED 0x5EED1234u
static void drive(const Step* s, Fighter* fp)
{
    *HSD_RandSeedPtr = QA_SEED;
    drive_raw(s, fp);
    qa_drive_x = (s8) (qa_drive_x * face);
    qa_drive_cx = (s8) (qa_drive_cx * face);
}

static void drive_raw(const Step* s, Fighter* fp)
{
    unsigned f = rf;
    input_clear();
    switch (s->kind) {
    case K_NORMAL:
        switch (s->slot) {
        case BAM_NORMAL_JAB:
            if (f == 0 || f == 8 || f == 16 || (f >= 24 && f < 70)) qa_drive_buttons = HSD_PAD_A;
            break;
        case BAM_NORMAL_DASH:
            if (f < 11) qa_drive_x = 80;
            if (f == 10) qa_drive_buttons = HSD_PAD_A;
            break;
        case BAM_NORMAL_FTILT:
            if (f < 3) qa_drive_x = 40;
            if (f == 0) qa_drive_buttons = HSD_PAD_A;
            break;
        case BAM_NORMAL_UTILT:
            if (f < 3) qa_drive_y = 40;
            if (f == 0) qa_drive_buttons = HSD_PAD_A;
            break;
        case BAM_NORMAL_DTILT:
            if (f < 3) qa_drive_y = -48;
            if (f == 0) qa_drive_buttons = HSD_PAD_A;
            break;
        case BAM_NORMAL_FSMASH: if (f < 2) qa_drive_cx = 80; break;
        case BAM_NORMAL_USMASH: if (f < 2) qa_drive_cy = 80; break;
        case BAM_NORMAL_DSMASH: if (f < 2) qa_drive_cy = -80; break;
        default: /* throws: grab, then the direction once holding */
            if ((int) f == z_at) qa_drive_buttons = HSD_PAD_Z;
            if (grab_at < 0 && fp->victim_gobj && f > 0) grab_at = (int) f + 4;
            /* Donkey Kong's cargo hold: throw with A and the direction. */
            if (grab_at >= 0 && f >= (unsigned) grab_at + 30 && (f - grab_at) % 25 < 2 && fp->motion_id >= 341)
                qa_drive_buttons = HSD_PAD_A;
            if (grab_at >= 0 && ((f >= (unsigned) grab_at && f < (unsigned) grab_at + 4) ||
                                 (f >= (unsigned) grab_at + 30 && (f - grab_at) % 25 < 2 && fp->motion_id >= 341))) {
                switch (s->slot) {
                case BAM_NORMAL_FTHROW: qa_drive_x = 80; break;
                case BAM_NORMAL_BTHROW: qa_drive_x = -80; break;
                case BAM_NORMAL_UTHROW: qa_drive_y = 80; break;
                default: qa_drive_y = -80; break;
                }
            }
            break;
        }
        break;
    case K_SPECIAL:
        if (f < 2) {
            if (s->slot == 1) qa_drive_x = 80;
            else if (s->slot == 2) qa_drive_y = 80;
            else if (s->slot == 3) qa_drive_y = -80;
        }
        if (f == 0) qa_drive_buttons = HSD_PAD_B;
        /* A side special that keeps travelling (Yoshi's Egg Roll) is steered
         * back toward the middle before it leaves Final Destination. */
        if (s->slot == 1 && f >= 2 && busy(fp) && (fp->cur_pos.x > 45.0f || fp->cur_pos.x < -45.0f))
            qa_drive_x = (s8) ((fp->cur_pos.x > 0.0f ? -80 : 80) * face); /* drive() turns it back */
        /* Charged / held moves: release, then cancel. */
        if (busy(fp)) {
            if (f == 90 || f == 180 || f == 300) qa_drive_buttons = HSD_PAD_B;
            if (f >= 400 && f < 404) qa_drive_buttons = HSD_PAD_R;
            if (f == 500) qa_drive_buttons = HSD_PAD_X;
        }
        break;
    case K_AERIAL:
        if (f < 10) qa_drive_buttons = HSD_PAD_X; /* full hop */
        if (air_at < 0 && fp->ground_or_air == GA_Air && f >= 2) air_at = (int) f + 2;
        if (air_at >= 0 && f >= (unsigned) air_at && f < (unsigned) air_at + 2) {
            switch (s->slot) {
            case 0: qa_drive_buttons = HSD_PAD_A; break;
            case 1: qa_drive_cx = 80; break;
            case 2: qa_drive_cx = -80; break;
            case 3: qa_drive_cy = 80; break;
            default: qa_drive_cy = -80; break;
            }
        }
        break;
    }
}

static void log_hitboxes(Fighter* fp)
{
    int i;
    for (i = 0; i < 4; ++i) {
        HitCapsule* h = &fp->x914[i];
        u32 sig;
        (void) sig;
        /* Every frame it is active (paired with the donor's own move by
         * motion and animation frame). */
        if (h->state == HitCapsule_Disabled) continue;
        {
            /* The body part the hitbox rides on (-1: none), and the nearest
             * body part at or above its joint ("anchor") with that joint's
             * world position, so a prop's hitbox (tail, sword, yoyo) can be
             * compared relative to what it hangs from. */
            int part = -1, anc = -1, j;
            const FighterPartsTable* t = ftPartsTable[fp->kind];
            HSD_JObj* walk;
            Vec3 ap = { 0, 0, 0 };
            for (j = 0; j < (int) t->parts_num; ++j)
                if (fp->parts[j].joint == h->jobj) { part = t->joint_to_part[j]; break; }
            {
                /* A rebuilt donor bone: from the donor part it hangs from. */
                extern int Rogue_QAHitAnchor(Fighter* fp, const HitCapsule* hit, Vec3* pos);
                anc = Rogue_QAHitAnchor(fp, h, &ap);
            }
            for (walk = h->jobj; walk && anc < 0; walk = walk->parent)
                for (j = 0; j < (int) t->parts_num; ++j)
                    if (fp->parts[j].joint == walk && t->joint_to_part[j] != 0xFF) {
                        anc = t->joint_to_part[j];
                        lb_8000B1CC(walk, NULL, &ap);
                        break;
                    }
            OSReport("[qa] H %u %u %u %d %d %.3f %.3f %.3f %.1f %d %.3f %d %.3f %.3f %.1f\n", cur_match, cur_step, rf,
                     (int) fp->motion_id, i, h->scale, (h->x4C.x - fp->cur_pos.x) * face, h->x4C.y - fp->cur_pos.y,
                     h->damage, part,
                     sqrtf(h->b_offset.x * h->b_offset.x + h->b_offset.y * h->b_offset.y +
                           h->b_offset.z * h->b_offset.z),
                     anc, (ap.x - fp->cur_pos.x) * face, ap.y - fp->cur_pos.y, fp->cur_anim_frame);
        }
    }
}

static const char* result_name(int r)
{
    static const char* const n[] = { "OK", "STUCK", "DIED", "NOGRAB", "NOMOVE", "FELL" };
    return n[r];
}


/* Closest approach of port 1's hitboxes to the dummy's hurtboxes this step:
 * the gap between the capsules (negative: they overlapped). Tells a move
 * that never reached the dummy (REACH) from one that touched it and dealt
 * no damage (NOHIT). Hitboxes sweep from last frame's place (x58) to this
 * frame's (x4C), as the game tests them. */
#define NO_REACH 1e9f
static float reach_gap;

static float seg_seg(const Vec3* p1, const Vec3* q1, const Vec3* p2, const Vec3* q2)
{
    Vec3 d1, d2, r, a, b;
    float aa, ee, ff, cc, bb, den, t, u;
    d1.x = q1->x - p1->x; d1.y = q1->y - p1->y; d1.z = q1->z - p1->z;
    d2.x = q2->x - p2->x; d2.y = q2->y - p2->y; d2.z = q2->z - p2->z;
    r.x = p1->x - p2->x; r.y = p1->y - p2->y; r.z = p1->z - p2->z;
    aa = d1.x * d1.x + d1.y * d1.y + d1.z * d1.z;
    ee = d2.x * d2.x + d2.y * d2.y + d2.z * d2.z;
    ff = d2.x * r.x + d2.y * r.y + d2.z * r.z;
    if (aa < 1e-6f && ee < 1e-6f) {
        t = u = 0.0f;
    } else if (aa < 1e-6f) {
        t = 0.0f; u = ff / ee;
    } else {
        cc = d1.x * r.x + d1.y * r.y + d1.z * r.z;
        if (ee < 1e-6f) {
            u = 0.0f; t = -cc / aa;
        } else {
            bb = d1.x * d2.x + d1.y * d2.y + d1.z * d2.z;
            den = aa * ee - bb * bb;
            t = den > 1e-6f ? (bb * ff - cc * ee) / den : 0.0f;
            if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
            u = (bb * t + ff) / ee;
            if (u < 0.0f) { u = 0.0f; t = -cc / aa; }
            else if (u > 1.0f) { u = 1.0f; t = (bb - cc) / aa; }
        }
    }
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
    a.x = p1->x + d1.x * t; a.y = p1->y + d1.y * t; a.z = p1->z + d1.z * t;
    b.x = p2->x + d2.x * u; b.y = p2->y + d2.y * u; b.z = p2->z + d2.z * u;
    a.x -= b.x; a.y -= b.y; a.z -= b.z;
    return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
}

static void reach_capsule(Fighter* p2, const HitCapsule* h, float rh)
{
    int j;
    for (j = 0; j < (int) p2->hurt_capsules_len && j < 15; ++j) {
        HurtCapsule* c = &p2->hurt_capsules[j].capsule;
        float g = seg_seg(&h->x58, &h->x4C, &c->a_pos, &c->b_pos) - rh - c->scale * p2->x34_scale.y;
        if (g < reach_gap) reach_gap = g;
    }
}

static void track_reach(Fighter* p1, Fighter* p2)
{
    int i, j;
    HSD_GObj* cur;
    /* Its projectiles and items too (fireballs, arrows, eggs). */
    for (cur = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_ITEM]; cur != NULL; cur = cur->next) {
        Item* ip = GET_ITEM(cur);
        if (!ip || ip->owner != p1->gobj) continue;
        for (i = 0; i < 4; ++i) {
            HitCapsule* h = &ip->x5D4_hitboxes[i].hit;
            if (h->state == HitCapsule_Disabled) continue;
            reach_capsule(p2, h, h->scale * ip->scl);
            if (i == 0)
                OSReport("[qa] I %u %u %u %d %.2f %.2f %.2f\n", cur_match, cur_step, rf, (int) ip->kind,
                         (h->x4C.x - p1->cur_pos.x) * face, h->x4C.y - p1->cur_pos.y, h->scale * ip->scl);
        }
    }
    for (i = 0; i < 4; ++i) {
        HitCapsule* h = &p1->x914[i];
        float rh;
        if (h->state == HitCapsule_Disabled) continue;
        rh = h->scale * p1->x34_scale.y;
        for (j = 0; j < (int) p2->hurt_capsules_len && j < 15; ++j) {
            HurtCapsule* c = &p2->hurt_capsules[j].capsule;
            float g = seg_seg(&h->x58, &h->x4C, &c->a_pos, &c->b_pos) - rh - c->scale * p2->x34_scale.y;
            if (g < reach_gap) reach_gap = g;
        }
    }
}

static void finish_step(Fighter* p1, Fighter* p2, int res)
{
    float dmg = p2 ? p2->dmg.x1830_percent - p2_start : 0;
    if (res == 0 && steps[cur_step].kind == K_NORMAL && steps[cur_step].slot >= BAM_NORMAL_FTHROW && grab_at < 0)
        res = 3;
    if (res == 0 && cur_D >= 0 && !saw_borrow) res = 4;
    if (reach_gap < NO_REACH) OSReport("[qa] C %u %u %.2f\n", cur_match, cur_step, reach_gap);
    OSReport("[qa] R %u %u %s %u %d %.1f %d %d %d %d %d %.1f %.1f %d\n", cur_match, cur_step, result_name(res), rf,
             saw_borrow, dmg, ms_count > 0 ? ms_seen[0] : -1, ms_count > 1 ? ms_seen[1] : -1,
             ms_count > 2 ? ms_seen[2] : -1, ms_count > 3 ? ms_seen[3] : -1, p1 ? (int) p1->motion_id : -1,
             p1 ? p1->cur_pos.x : 0, p1 ? p1->cur_pos.y : 0, p1 ? (int) p1->ground_or_air : -1);
    input_clear();
    ++cur_step;
    phase = PH_PREP; pf = 0;
}

static void qa_frame(void)
{
    Fighter* p1 = fighter(0);
    Fighter* p2 = fighter(1);
    const Step* s;
    qa_drive_on = 1;
    if (!p1 || !p2 || finished) return;
    if (cur_step >= nsteps) {
        if (!finished) { finished = 1; input_clear(); gm_801A4B60(); }
        return;
    }
    s = &steps[cur_step];
    ++pf;
    switch (phase) {
    case PH_PREP:
        input_clear();
        if (pf == 1) idle_frames = 0;
        /* Both idle on the ground (the dummy forced after a while). */
        if (pf > 240 && !idle(p2) && p2->ground_or_air == GA_Ground && p2->motion_id != ftCo_MS_RebirthWait)
            ft_8008A2BC(p2->gobj);
        if (pf > 900 && !idle(p1) && p1->ground_or_air == GA_Ground) ft_8008A2BC(p1->gobj);
        /* The dummy can end up hanging on a ledge (it never lets go):
         * drop it back over the stage. */
        if ((pf == 300 || pf == 700) && !idle(p2) && p2->motion_id != ftCo_MS_RebirthWait) {
            p2->cur_pos.y = 15.0f;
            place(p2, 0.0f);
            p2->coll_data.cur_pos.y = p2->coll_data.prev_pos.y = p2->coll_data.last_pos.y = 15.0f;
            if (p2->ground_or_air == GA_Air) ftCommon_8007D92C(p2->gobj);
        }
        if ((pf == 300 || pf == 700) && p1->motion_id >= 252 && p1->motion_id <= 263) {
            p1->cur_pos.y = 15.0f;
            place(p1, -30.0f * face);
            p1->coll_data.cur_pos.y = p1->coll_data.prev_pos.y = p1->coll_data.last_pos.y = 15.0f;
            ftCommon_8007D92C(p1->gobj);
        }
        /* Still held, or stuck in the air: make both idle (or falling). */
        if (pf == 600 || pf == 1000) {
            if (!idle(p2) && p2->motion_id != ftCo_MS_RebirthWait) ftCommon_8007D92C(p2->gobj);
            if (!idle(p1) && p1->motion_id != ftCo_MS_RebirthWait) ftCommon_8007D92C(p1->gobj);
        }
        if (idle(p1) && idle(p2)) {
            /* Body size (native matches): standing ECB top and the top of
             * the hurtboxes, over the ground, in world units. */
            if (idle_frames == 8 && cur_step == 0 && cur_D < 0) {
                unsigned i;
                float top = -1e9f, bot = 1e9f;
                for (i = 0; i < p1->hurt_capsules_len && i < 15; ++i) {
                    HurtCapsule* c = &p1->hurt_capsules[i].capsule;
                    float hi = (c->a_pos.y > c->b_pos.y ? c->a_pos.y : c->b_pos.y) + c->scale;
                    float lo = (c->a_pos.y < c->b_pos.y ? c->a_pos.y : c->b_pos.y) - c->scale;
                    if (hi > top) top = hi;
                    if (lo < bot) bot = lo;
                }
                {
                    float ctop = -1e9f, rmax = 0;
                    for (i = 0; i < p1->hurt_capsules_len && i < 15; ++i) {
                        HurtCapsule* c = &p1->hurt_capsules[i].capsule;
                        float hi = c->a_pos.y > c->b_pos.y ? c->a_pos.y : c->b_pos.y;
                        if (hi > ctop) ctop = hi;
                        if (c->scale > rmax) rmax = c->scale;
                    }
                    OSReport("[qa] SIZE %d %.3f %.3f %.3f %.3f %.3f %.3f\n", cur_R, p1->co_attrs.model_scaling,
                             p1->coll_data.ecb.top.y, top - p1->cur_pos.y, bot - p1->cur_pos.y,
                             ctop - p1->cur_pos.y, rmax);
                }
                { extern int qa_ui_mode; if (qa_ui_mode == 4) { cur_step = nsteps; return; } }
            }
            if (++idle_frames >= 10) { phase = PH_PLACE; pf = 0; }
        } else {
            idle_frames = 0;
        }
        if (pf > 1500) {
            OSReport("[qa] R %u %u PREPSTUCK 0 0 0 %d %d -1 -1\n", cur_match, cur_step, (int) p1->motion_id,
                     (int) p2->motion_id);
            ++cur_step; pf = 0;
        }
        break;
    case PH_PLACE:
        if (pf == 1) face = p1->facing_dir < 0 ? -1.0f : 1.0f;
        /* Draw hitboxes (only seen in video runs: tools/qa/sweep.py --video). */
        p1->x21FC_flag.b6 = 1;
        if (pf == 1 && cur_D < 0 && cur_step == 0) QA_DumpRest(p1);
        /* Not across the middle of Final Destination: its floor is two
         * lines there, and moves that measure along the floor (Donkey
         * Kong's hand slap) stop at a line's end. */
        place(p1, -30.0f * face);
        gap_fp = p1;
        place(p2, -30.0f * face + gap_for(s) * face);
        p1->dmg.x1830_percent = 0;
        p2->dmg.x1830_percent = 0;
        if (pf >= 3) {
            phase = PH_RUN; rf = 0;
            grab_at = air_at = -1; idle_frames = 0; z_at = 0; grab_tries = 0; saw_borrow = 0; logged_scale = 0;
            ms_count = 0; p2_start = p2->dmg.x1830_percent;
            memset(hit_sig, 0, sizeof(hit_sig));
            reach_gap = NO_REACH;
            stocks_at_start = Player_GetStocks(0);
            OSReport("[qa] S %u %u %d %.1f %.1f %.4f\n", cur_match, cur_step, (int) p2->motion_id,
                     p2->cur_pos.x - p1->cur_pos.x, p2->cur_pos.y - p1->cur_pos.y, p1->co_attrs.model_scaling);
            drive(s, p1);
        }
        break;
    case PH_RUN:
        log_hitboxes(p1);
        track_reach(p1, p2);
        if (Rogue_IsAbilityState(p1)) {
            saw_borrow = 1;
            if (!logged_scale) { logged_scale = 1; OSReport("[qa] B %u %u %.4f\n", cur_match, cur_step, Rogue_BorrowScale(p1)); }
        }
        if (ms_count < 4 && (ms_count == 0 || ms_seen[ms_count - 1] != (s16) p1->motion_id) &&
            p1->motion_id != ftCo_MS_Wait)
            ms_seen[ms_count++] = (s16) p1->motion_id;
        ++rf;
        if (Player_GetStocks(0) < stocks_at_start) { finish_step(p1, p2, 2); break; }
        if (idle(p1) && rf > 12) {
            /* A missed grab (the dummy can be briefly ungrabbable after a
             * throw): try again a few times. */
            if (s->kind == K_NORMAL && s->slot >= BAM_NORMAL_FTHROW && grab_at < 0 && grab_tries < 6 &&
                (int) rf > z_at + 12) {
                ++grab_tries;
                z_at = (int) rf + 15;
                place(p2, p1->cur_pos.x + gap_for(s) * face);
            }
            if (++idle_frames >= 3 && (int) rf > z_at + 12) { finish_step(p1, p2, 0); break; }
        } else {
            idle_frames = 0;
        }
        if (rf > 1200) {
            finish_step(p1, p2, 1);
            ftCommon_8007D92C(p1->gobj);
            if (!idle(p2)) ftCommon_8007D92C(p2->gobj);
            break;
        }
        drive(s, p1);
        break;
    }
}

extern CSSData* mnCharSel_804D6CB0;

/* Every CSS frame (QA): show the match's players to the CSS, which preloads
 * them, then leave (onExitCss goes to the match). */
int QA_CssAuto(void)
{
    extern int qa_ui_mode;
    extern void HSD_SisLib_803A5E70(void);
    if (qa_ui_mode >= 2 && qa_ui_mode <= 3) {
        /* First CSS: the panel's texts are wiped (as Slippi's code entry
         * does), then a one-step match; the CSS after it stays up. */
        static unsigned visit_frames;
        if (started) return 0;
        if (mnCharSel_804D6CB0) {
            mnCharSel_804D6CB0->vs.start.players[0].ckind = 9;
            mnCharSel_804D6CB0->vs.start.players[0].slot_type = 0;
        }
        ++visit_frames;
        if (visit_frames == 120 && qa_ui_mode == 2) { OSReport("[qa] wipe SIS\n"); HSD_SisLib_803A5E70(); }
        if (visit_frames == 180) {
            OSReport("[qa] to the match\n");
            next_match();
            nsteps = 1;
            {
                int q;
                for (q = 0; q < 4; ++q)
                    mnCharSel_804D6CB0->vs.start.players[q] = gmMainLib_804D3EE0->modes.vs_melee.start.players[q];
            }
            gm_801A4B60();
        }
        return 0;
    }
    if (qa_ui_mode == 1) {
        /* Menu tests: port 1 plays Marth; the script drives the panel. */
        if (mnCharSel_804D6CB0) {
            mnCharSel_804D6CB0->vs.start.players[0].ckind = 9;
            mnCharSel_804D6CB0->vs.start.players[0].slot_type = 0;
        }
        return 0;
    }
    if (!started) next_match();
    if (sweep_over) return 1;
    if (mnCharSel_804D6CB0) {
        int p;
        for (p = 0; p < 4; ++p)
            mnCharSel_804D6CB0->vs.start.players[p] = gmMainLib_804D3EE0->modes.vs_melee.start.players[p];
    }
    if (++css_frames == 20) gm_801A4B60();
    return 1;
}

/* override: bootOnLeave (QA). */
void QA_BootOnLeave(GameModeState* state)
{
    (void) state;
    gm_ChangeGameModeAfterCurrentScene(2 /* GM_VS */);
}

/* inject: efAsync_Spawn entry (QA): which effect, on which joint. */
void QA_GfxLog(HSD_GObj* gobj, int gfx, HSD_JObj* jobj, int type)
{
    Fighter* fp;
    int j, n;
    if (!gobj || gobj->classifier != HSD_GOBJ_CLASS_FIGHTER) return;
    fp = GET_FIGHTER(gobj);
    if (!fp || fp->player_id != 0 || (!Rogue_IsAbilityState(fp) && cur_D >= 0)) return;
    n = (int) ftPartsTable[fp->kind]->parts_num;
    for (j = 0; j < n && fp->parts[j].joint != jobj; ++j) {}
    OSReport("[qa] G %u %u %d gfx=%d type=%d jobj=%p joint=%d/%d\n", cur_match, cur_step, (int) fp->motion_id, gfx,
             type, jobj, j < n ? j : -1, n);
}

/* ---- rest pose dump (QA): world rest rotation of every body part ---------- */
typedef struct QQuat { float x, y, z, w; } QQuat;
static QQuat qq_mul(QQuat a, QQuat b)
{
    QQuat q;
    q.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    q.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    q.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    q.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    return q;
}
static QQuat qq_euler(const Vec3* e)
{
    float sx = sinf(e->x * 0.5f), cx = cosf(e->x * 0.5f);
    float sy = sinf(e->y * 0.5f), cy = cosf(e->y * 0.5f);
    float sz = sinf(e->z * 0.5f), cz = cosf(e->z * 0.5f);
    QQuat q;
    q.x = cz * cy * sx - sz * sy * cx;
    q.y = cz * sy * cx + sz * cy * sx;
    q.z = sz * cy * cx - cz * sy * sx;
    q.w = cz * cy * cx + sz * sy * sx;
    return q;
}
static void rest_walk(Fighter* fp, HSD_Joint* j, HSD_JObj* live, QQuat parent)
{
    const FighterPartsTable* t = ftPartsTable[fp->kind];
    for (; j && live; j = j->next, live = live->next) {
        QQuat w = qq_mul(parent, qq_euler(&j->rotation));
        unsigned k;
        for (k = 0; k < t->parts_num; ++k)
            if (fp->parts[k].joint == live) break;
        if (k < t->parts_num && t->joint_to_part[k] != 0xFF)
            OSReport("[qa] REST %d %u %d %d %d %d %d\n", (int) fp->kind, k, (int) t->joint_to_part[k],
                     (int) (w.x * 32767), (int) (w.y * 32767), (int) (w.z * 32767), (int) (w.w * 32767));
        if (j->child) rest_walk(fp, j->child, live->child, w);
    }
}
void QA_DumpRest(Fighter* fp)
{
    QQuat id = { 0, 0, 0, 1 };
    HSD_Joint* root = CostumeListsForeachCharacter[fp->kind].costume_list[0].joint;
    if (!root || !fp->parts[0].joint) return;
    rest_walk(fp, root, fp->parts[0].joint, id);
}
