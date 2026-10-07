/* Retail hook entry points (see project.toml [[hook]]).
 *
 * Called from: retail, through the hooks in project.toml.
 * State: none.
 */
#include <bam/bam.h>
#include <bam/retail.h>
#include <engine/internal.h>
#include <dolphin/os.h>
#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include "training.h"

void Bam_OnlineSceneEnter(void);
void Bam_OnlineSceneReady(void);
void Bam_OnlineSceneExit(void);
void Bam_ReplayBuildsSceneExit(void);

/* inject Fighter_800679B0+0x10: r4 = size HSD_ObjAllocInit will use for
 * Fighter structs (retail 0x23EC, Slippi's ExtendPlayerBlock makes 0x2600).
 * Returns the size including our extension slot. */
unsigned BAM_FighterAllocSize(unsigned size)
{
    return Bam_FighterAllocSize(size);
}

/* inject Fighter_Create+0x4F0 (after ftLib_800867E8(gobj); r31 = gobj). */
void BAM_OnFighterCreated(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    BAM_LOG("fighter_create kind=%d player=%d sub=%d build=%d\n", (int) fp->kind,
            (int) fp->player_id, (int) fp->is_sub_fighter, (int) Bam_IsBuildFighter(fp));
    BamFighter_Created(fp);
}

/* inject gm_801A4014 after gm_801A4B88(info): a new scene's heaps exist. */
void BAM_OnSceneEnter(void)
{
    Bam_OnSceneEnter(NULL);
    Bam_OnlineSceneEnter();
}

/* inject gm_801A4014 before the scene's state on_exit. */
void BAM_OnSceneExit(void)
{
    Bam_OnSceneExit();
    Bam_OnlineSceneExit();
    Bam_ReplayBuildsSceneExit();
    Bam_TrainingSceneExit();
}

/* inject gm_801A4014 after scene->on_enter(): the scene created its stage
 * and fighters; the first frame has not run. */
void BAM_OnSceneReady(void)
{
    Bam_OnlineSceneReady();
    if (bam_match) {
        Bam_DonorModelsLoad();
        Bam_LogHeapRoom("match ready");
        Bam_LogHeapTable();
    }
}

/* inject gm_801A4D34 after on_frame(): once per game frame (also during
 * Slippi rollback re-simulation, which is what we want). */
void Bam_WatchdogFrame(void);
void BAM_OnFrame(void)
{
    unsigned i;
    Bam_WatchdogFrame();
    Bam_TrainingFrame();
    if (!bam_match || Bam_TrainingFrozen()) return;
    for (i = 0; i < BAM_FIGHTERS; ++i)
        if (bam_match->fighters[i].fighter) {
            Bam_BorrowFighterFrame(bam_match->fighters[i].fighter);
            Bam_ParasolTrack(bam_match->fighters[i].fighter);
        }
}

/* inject ftCo_LandingAir_EnterWithLag+0x88 (`cmpwi r4,-1`, before the
 * L-cancel halving). The function is not recompiled because Slippi Recording
 * hooks +0x9C. Here r4 = landing msid (-1 = none), r5 = fp, f1 = lag.
 * `regs` points at the trampoline's saved r0; r3..r31 follow it, the saved
 * f0..f13 (doubles) start 0x90 bytes after it. */
void BAM_OnLandingAirLag(u32* regs)
{
    s32 msid = (s32) regs[2];        /* r4 */
    Fighter* fp = (Fighter*) regs[3]; /* r5 */
    f64* fpr = (f64*) ((u8*) regs + 0x90);
    if (msid != -1 && fp != NULL)
        fpr[1] = Bam_AerialLandingLag(fp, fp->motion_id, (float) fpr[1]);
    Bam_TrainingLanding(fp, msid);
}

/* inject at the entry of every native aerial-jump enter (multi-jump
 * ftCo_800D74A4 and the Ness/Yoshi/Peach/Mewtwo variants).
 *
 * Shine (and other borrowed moves) can be jump-cancelled. The borrower's
 * jump code then runs while the donor's attributes and motion table are
 * still installed: a multi-jumper (Jigglypuff, Kirby) asks for its own
 * kind-specific jump state 341+, which the borrowed move maps onto the
 * donor's state of the same number (Fox Blaster) and the fighter freezes.
 * End the borrowed move first so the jump is fully native. */
void BAM_BeforeNativeJump(Fighter_GObj* gobj)
{
    Bam_BorrowEnd(GET_FIGHTER(gobj));
}

/* override: ftAnim_ApplyPartAnim (same body, plus the guard below).
 *
 * A move's command script can animate one of the fighter's "parts" (hands,
 * eyes, Yoshi's tongue...) with an index into the fighter's own part-anim
 * table (ft_data->x1C). A borrowed move's script carries the DONOR's
 * indices; looked up in this fighter's table they read garbage (Yoshi's
 * down air on Jigglypuff crashed: invalid read in ApplyPartAnim). Those
 * part animations belong to the donor's model, so skip them while a
 * borrowed move runs. */
#include <melee/ft/ftanim.h>
#include <melee/lb/lb_00B0.h>
void BAM_ApplyPartAnim(Fighter_GObj* gobj, s32 arg1, s32 arg2, f32 arg3)
{
    Fighter* fp = GET_FIGHTER(gobj);
    struct Fighter_x8B0_t* st;
    struct ftData_x1C* data;
    if (Bam_InBorrowedMove(fp)) return;
    st = &fp->x8B0[arg1];
    data = fp->ft_data->x1C[arg1];
    st->x11 = arg2;
    st->x4 = arg3;
    st->x8 = 0.0F;
    if (arg3) st->xC = lb_8000BFF0(data->x8[arg2]) / arg3;
    else st->xC = 0.0F;
    ftAnim_80070904(fp, data->x0, data->x8[arg2]);
}

/* inject: fn_8016CFE0 entry (the VS match scene's frame function, before it
 * checks the pause menu's quit combo).
 *
 * Some controllers (8BitDo) reserve L+R+A+Start for their own functions, so
 * the match can't be quit. While paused, Z+A on any controller counts as
 * L+R+A+Start for that port. */
extern int gm_GetDbPauseFlag(int);
void BAM_VsFrame(void)
{
    int p;
    if (!gm_GetDbPauseFlag(1)) return;
    for (p = 0; p < 4; ++p) {
        struct BamGmPadState* g = &BAM_GM_PADS[p];
        const u64 z = 0x10, a = 0x100, lras = 0x40 | 0x20 | 0x100 | 0x1000;
        if ((g->button & (z | a)) == (z | a) && (g->trigger & (z | a))) {
            g->button |= lras;
            g->trigger |= lras;
            BAM_LOG("pause quit (Z+A) port=%d\n", p);
        }
    }
}

/* inject: lb_8000B1CC entry (world position of a joint; r3 = jobj).
 * Borrowed moves have handed it garbage joints (Dr. Mario's down air and
 * Ganondorf's forward air on Jigglypuff). A pointer outside main RAM is
 * logged with its caller and replaced by NULL, which this function treats
 * as "no joint" (the position is the offset itself) instead of crashing. */
void* BAM_CheckJointPos(void* jobj, u32* regs)
{
    u32 p = (u32) jobj;
    if (p == 0 || (p >= 0x80000000 && p < 0x81800000 && !(p & 3))) return jobj;
    BAM_NOTE("joint: bad joint %08x passed to lb_8000B1CC from %08x, ignored\n", (unsigned) p, (unsigned) regs[0x7C / 4]);
    return NULL;
}
