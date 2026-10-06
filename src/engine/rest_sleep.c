#include <engine/special_internal.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>

/* ---- per-match state ----
 * Allocated on the match heap at match start (Bam_MatchBegin) so Slippi rollback
 * restores it; the pointer is NULL outside matches. */
typedef struct RestSleepState {
    Fighter* sleepers[BAM_FIGHTERS];
} RestSleepState;
static RestSleepState* bam_rest_sleep;
#define sleepers (bam_rest_sleep->sleepers)


/* Borrowed Rest: once Jigglypuff's animation has her lying asleep, the
 * borrower switches to its own sleep animations (the ones Melee plays when
 * Sing puts a fighter to sleep: DamageSong, DamageSongWait, DamageSongRv).
 *
 * Rest's timing is kept: the sleep ends when Jigglypuff's Rest would, and
 * mashing does not wake you sooner (it can with Sing, not with Rest). Rest
 * started in the air keeps Jigglypuff's pose until you land.
 * Jigglypuff herself is unchanged. */
#define REST_ASLEEP_FRAME 34.0f /* Jigglypuff has settled asleep */
#define REST_WAKE_FRAMES 30.0f  /* left for the borrower's own wake-up */



bool Rogue_RestSleep(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float remaining;
    unsigned i;
    if (fp->kind == Ft_Kind_Purin || !Rogue_IsBuildFighter(fp) || fp->ground_or_air != GA_Ground ||
        fp->cur_anim_frame < REST_ASLEEP_FRAME) return false;
    remaining = ftAnim_8006F484(gobj) - fp->cur_anim_frame - REST_WAKE_FRAMES;
    if (remaining < 1.0f) return false;
    /* Leaving Rest's own states ends the borrow, so the sleep plays the
     * borrower's animations. */
    Fighter_ChangeMotionState(gobj, ftCo_MS_DamageSong, Ft_MF_None, 0, 1, 0, NULL);
    ftCommon_InitGrab(fp, 0, remaining * p_ftCommonData->x63C);
    for (i = 0; i < BAM_FIGHTERS; ++i) if (sleepers[i] == fp) return true;
    for (i = 0; i < BAM_FIGHTERS; ++i) if (!sleepers[i]) { sleepers[i] = fp; break; }
    return true;
}

/* True while a Rest sleep runs (no mashing out). */
bool Rogue_RestSleeping(Fighter* fp)
{
    unsigned i;
    for (i = 0; i < BAM_FIGHTERS; ++i) if (fp && sleepers[i] == fp) return true;
    return false;
}

/* A sleep that Sing (or anything else) starts is a normal one. */
void Rogue_RestSleepClear(Fighter* fp)
{
    unsigned i;
    for (i = 0; i < BAM_FIGHTERS; ++i) if (sleepers[i] == fp) sleepers[i] = NULL;
}

#include <sysdolphin/baselib/memory.h>
/* Called from Bam_MatchBegin / Bam_MatchEnd (bam_fighter.c). */
void Rogue_RestSleepMatchBegin(void)
{
    bam_rest_sleep = HSD_MemAlloc(sizeof(*bam_rest_sleep));
    memset(bam_rest_sleep, 0, sizeof(*bam_rest_sleep));
}
void Rogue_RestSleepMatchEnd(void)
{
    bam_rest_sleep = NULL;
}
