/* Training Mode with builds (training.c).
 *
 * The build made on the Training character select screen (the same Z panel
 * as VS) plays in the training match. In the match, on the player's
 * controller:
 *   D-Up     build menu (the CSS panel, plus an Options tab); the game
 *            freezes while it is open. Closing it after a move change
 *            restarts the match with the new build.
 *   D-Down   freeze / resume.
 *   D-Right  while frozen: advance one frame (hold to repeat).
 *   D-Left   move info (motion, frame, percent, borrowed move).
 * The D-pad does not reach the fighter in training (no D-pad taunt). */
#ifndef BAM_TRAINING_H
#define BAM_TRAINING_H
#include <sysdolphin/baselib/controller.h>
#include <melee/ft/forward.h>

/* Options tab rows. */
enum { BAM_OPT_HITBOXES, BAM_OPT_HURTBOXES, BAM_OPT_SHAKE, BAM_OPT_LCANCEL, BAM_OPT_INFO, BAM_OPT_COUNT };
const char* BamTraining_OptionName(unsigned i);
const char* BamTraining_OptionHelp(unsigned i);
int BamTraining_OptionOn(unsigned i);
void BamTraining_OptionToggle(unsigned i);

/* The build panel inside a training match (css_menu.c). */
int BamPanel_MatchOpen(int font, int canvas, int ckind);
/* 0 still open, 1 resume, 2 restart the match. */
int BamPanel_MatchFrame(HSD_PadStatus* pad);
void BamPanel_MatchClose(void);

/* Hooks (hooks.c, platform fixes). */
void Bam_TrainingSceneExit(void);
/* Every frame after the scene's own (BAM_OnFrame); skipped while frozen. */
void Bam_TrainingFrame(void);
/* While frozen between steps, per-frame engine work must not run either. */
int Bam_TrainingFrozen(void);
void Bam_TrainingLanding(Fighter* fp, int msid);
/* Hitbox / hurtbox bubbles (ftdrawcommon.c, itdraw.c): 1 outside training. */
int Bam_DrawHitboxes(void);
int Bam_DrawHurtboxes(void);
#endif
