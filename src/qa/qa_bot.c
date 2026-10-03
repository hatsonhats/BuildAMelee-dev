/* QA input bot (QA builds only, never shipped).
 *
 * Replaces the per-frame call to HSD_PadRenewMasterStatus (lb_800198E0) with
 * one that runs the retail function and then overwrites port 1's processed
 * pad state from a scripted timeline (qa_script.inc, generated from
 * qa/*.txt by tools/qa/script.py). This drives menus and matches
 * deterministically on any emulator, without emulator input plumbing.
 *
 * It also logs every scene change, so a headless run's log shows where the
 * game is ("[bam-qa] scene major=2 minor=2 frame=...").
 */
#include "../bam/bam.h"
#include <dolphin/os.h>
#include <dolphin/pad.h>
#include <sysdolphin/baselib/controller.h>

typedef struct QaStep { unsigned start, end, buttons; signed char x, y, cx, cy; } QaStep;
#include "qa_script.inc"     /* static const QaStep qa_script[]; QA_SCRIPT_LEN */

static unsigned qa_frame;
/* qa_moves.c: inputs computed by the move sweep for port 1. */
extern int qa_drive_on;
extern u32 qa_drive_buttons;
extern s8 qa_drive_x, qa_drive_y, qa_drive_cx, qa_drive_cy;
extern void gm_ChangeGameModeAfterCurrentScene(int pending_mode);
extern void gm_801A4B60(void);
static unsigned char qa_major = 0xFF, qa_minor = 0xFF;

void BAM_QaPadMaster(void)
{
    HSD_PadStatus* mp = &HSD_PadMasterStatus[0];
    const volatile unsigned char* scene = (const volatile unsigned char*) 0x80479D30;
    unsigned i, buttons = 0;
    int x = 0, y = 0, cx = 0, cy = 0, any = 0;
    HSD_PadRenewMasterStatus();
    ++qa_frame;
    if (scene[0] != qa_major || scene[3] != qa_minor) {
        qa_major = scene[0]; qa_minor = scene[3];
        OSReport("[bam-qa] scene major=%u minor=%u frame=%u\n", qa_major, qa_minor, qa_frame);
    }
    /* Boot straight into VS mode once the boot scene is over (its CSS
     * hands over to the sweep, qa_moves.c). */
    {
        static int switched;
        /* The boot scene waits on the memory card prompt: leave it. */
        if (qa_major == 40 && qa_frame == 30) gm_801A4B60();
        if (!switched && (qa_major == 0 || qa_major == 1)) {
            switched = 1;
            gm_ChangeGameModeAfterCurrentScene(2 /* GM_VS */);
            gm_801A4B60();
        }
    }
    if (qa_drive_on) {
        x = qa_drive_x; y = qa_drive_y; cx = qa_drive_cx; cy = qa_drive_cy;
        mp->err = 0;
        mp->button = qa_drive_buttons;
        mp->stickX = (s8) x; mp->stickY = (s8) y;
        mp->subStickX = (s8) cx; mp->subStickY = (s8) cy;
        mp->nml_stickX = x / 80.0f; mp->nml_stickY = y / 80.0f;
        mp->nml_subStickX = cx / 80.0f; mp->nml_subStickY = cy / 80.0f;
        mp->analogL = mp->analogR = 0;
        mp->nml_analogL = mp->nml_analogR = 0.0f;
        if (qa_drive_buttons & 0x20) { mp->analogR = 255; mp->nml_analogR = 1.0f; }
        return;
    }
    for (i = 0; i < QA_SCRIPT_LEN; ++i) {
        const QaStep* s = &qa_script[i];
        if (qa_frame >= s->start && qa_frame < s->end) {
            buttons |= s->buttons;
            if (s->x || s->y) { x = s->x; y = s->y; }
            if (s->cx || s->cy) { cx = s->cx; cy = s->cy; }
            any = 1;
        }
    }
    if (!any && qa_frame > QA_SCRIPT_END) return;
    mp->err = 0;
    mp->button = buttons;
    mp->stickX = (s8) x; mp->stickY = (s8) y;
    mp->subStickX = (s8) cx; mp->subStickY = (s8) cy;
    mp->nml_stickX = x / 80.0f; mp->nml_stickY = y / 80.0f;
    mp->nml_subStickX = cx / 80.0f; mp->nml_subStickY = cy / 80.0f;
    mp->analogL = mp->analogR = 0;
    mp->nml_analogL = mp->nml_analogR = 0.0f;
}
