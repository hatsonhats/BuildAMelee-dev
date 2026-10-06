/* Training Mode with builds: freeze, frame advance, the build menu and
 * options in the match (controls in training.h).
 *
 * The training match is native Training Mode. Its player is match slot 0
 * whatever port they use, so the loadouts are remapped for the session
 * (slot 0 = that port's build, the CPU none) and put back when the match
 * scene ends, keeping changes made in the build menu.
 *
 * Freeze and frame advance use Melee's debug pause: with flag 0 set the
 * scene loop skips the frame (the game stands still, still drawn), and bit 0
 * of gm_80479D58.x2 lets exactly one frame through, as the debug ROM's frame
 * advance does. BAM_TrainingLoop runs every loop iteration before that
 * check (gm_801A4D34+0xCC), so a press acts on the same iteration. */
#pragma optimize_for_size on
#pragma auto_inline off
#include <bam/bam.h>
#include <bam/retail.h>
#include <engine/special_internal.h>
#include <engine/special_catalog.h>
#include <engine/aerial_catalog.h>
#include "training.h"
#include "ui_text.h"
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/types.h>
#include <melee/ft/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/inlines.h>
#include <melee/it/types.h>
#include <melee/it/inlines.h>
#include <melee/pl/player.h>
#include <melee/cm/camera.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/sislib.h>
#include <string.h>

#define FREEZE 0     /* debug pause flag that stops the frame */
#define NATIVE_MENU 2 /* Training Mode's own menu (Start) is up */
#define FONT_SLOT 4
#define DPAD (HSD_PAD_DPADUP | HSD_PAD_DPADDOWN | HSD_PAD_DPADLEFT | HSD_PAD_DPADRIGHT)
#define MENU_BUTTONS (DPAD | HSD_PAD_A | HSD_PAD_B | HSD_PAD_START | HSD_PAD_L | HSD_PAD_R)

extern SIS* HSD_SisLib_804D1124[5];

/* ---- options (kept for the session) ---- */
static u8 opt[BAM_OPT_COUNT] = { 0, 0, 1, 0, 0 };
static u8 bubbles_applied, shake_applied = 1;
static Fighter* lcancel_pending;

const char* BamTraining_OptionName(unsigned i)
{
    static const char* const names[BAM_OPT_COUNT] = { "Show hitboxes", "Show hurtboxes", "Screen shake",
                                                      "Flash on missed L-cancel", "Move info (also D-Left)" };
    return i < BAM_OPT_COUNT ? names[i] : "";
}
const char* BamTraining_OptionHelp(unsigned i)
{
    static const char* const help[BAM_OPT_COUNT] = {
        "Attack hitboxes (red bubbles)", "Hurtboxes and shields (yellow bubbles)", "Camera shake on big hits",
        "Your fighter flashes red when an L-cancel is missed", "Motion, frame, percent and borrowed move" };
    return i < BAM_OPT_COUNT ? help[i] : "";
}
int BamTraining_OptionOn(unsigned i) { return i < BAM_OPT_COUNT && opt[i]; }
void BamTraining_OptionToggle(unsigned i)
{
    if (i < BAM_OPT_COUNT) opt[i] = !opt[i];
}

/* ---- session ---- */
static u8 session, mapped, port, ckind, frozen, panel, release_wait, restart, hint_frames;
static BamLoadout saved[BAM_PLAYER_SLOTS];

int Bam_DrawHitboxes(void) { return !session || opt[BAM_OPT_HITBOXES]; }
int Bam_DrawHurtboxes(void) { return !session || opt[BAM_OPT_HURTBOXES]; }
int Bam_TrainingFrozen(void) { return session && gm_GetDbPauseFlag(FREEZE) && !(BAM_FRAME_STEP & 1); }

/* Platform fix in gm_801B1F70 (the training match's setup, players filled). */
void BAM_TrainingPrepare(StartMeleeData* data)
{
    unsigned i;
    port = gm_801677F0();
    if (port >= 4) port = 0;
    ckind = (u8) data->players[0].ckind;
    if (!mapped) {
        memcpy(saved, bam_loadouts, sizeof(saved));
        mapped = 1;
    }
    bam_loadouts[0] = saved[port];
    for (i = 1; i < BAM_PLAYER_SLOTS; ++i) Bam_LoadoutClear(&bam_loadouts[i]);
    session = 1;
    frozen = panel = release_wait = restart = 0;
    hint_frames = 240;
    lcancel_pending = NULL;
    bubbles_applied = 0;
    shake_applied = 1;
    BAM_LOG("training: port %u ckind %u build %d\n", (unsigned) port, (unsigned) ckind, bam_loadouts[0].enabled);
}

/* Platform fix in gm_801B2204 (the training match's exit). */
int BAM_TrainingTakeRestart(void)
{
    int r = restart;
    restart = 0;
    return r;
}

/* ---- text ---- */
static int canvas = -1, loaded_font;
typedef struct TrainMem { u32 info[1536 / 4], back[128 / 4]; } TrainMem;
static TrainMem* mem;
static BamText info, info_back;

static int ui_create(void)
{
    if (mem) return 1;
    if (canvas == -2) return 0; /* failed once this match */
    if (!HSD_SisLib_804D1124[FONT_SLOT]) {
        HSD_SisLib_803A62A0(FONT_SLOT, "SdMenu.usd", "SIS_MenuData");
        loaded_font = 1;
    }
    mem = HSD_MemAlloc(sizeof(TrainMem));
    if (!HSD_SisLib_804D1124[FONT_SLOT] || !mem) {
        BAM_NOTE("training: no memory for the training text\n");
        canvas = -2;
        return 0;
    }
    canvas = HSD_SisLib_803A611C(FONT_SLOT, NULL, 9, 0x14, 0, 0xF, 0, 0x13);
    BamText_Box(&info_back, FONT_SLOT, canvas, mem->back, 12, 52, 470, 40, 0x05070C, 170);
    info_back.native->hidden = 1;
    BamText_Create(&info, FONT_SLOT, canvas, mem->info, sizeof(mem->info));
    return 1;
}

static void ui_destroy(void)
{
    if (panel) BamPanel_MatchClose();
    panel = 0;
    if (mem) {
        BamText_Destroy(&info);
        BamText_Destroy(&info_back);
        HSD_Free(mem);
        mem = NULL;
    }
    if (loaded_font) HSD_SisLib_803A5F50(FONT_SLOT);
    loaded_font = 0;
    canvas = -1;
}

static const char* const fighter_names[26] = {
    "Falcon", "DK", "Fox", "G&W", "Kirby", "Bowser", "Link", "Luigi", "Mario", "Marth", "Mewtwo",
    "Ness", "Peach", "Pikachu", "ICs", "Puff", "Samus", "Yoshi", "Zelda", "Sheik", "Falco",
    "YLink", "Doc", "Roy", "Pichu", "Ganon"
};
static const char* who(unsigned k) { return k < 26 ? fighter_names[k] : "?"; }

/* Two lines per fighter: motion and frame, then the borrowed move. */
static unsigned fighter_lines(unsigned slot, float y)
{
    static const char* const aerial_slots[5] = { "Neutral air", "Forward air", "Back air", "Up air", "Down air" };
    HSD_GObj* gobj = Player_GetEntity((s32) slot);
    Fighter* fp = gobj ? GET_FIGHTER(gobj) : NULL;
    BamFighterState* S;
    if (!fp) return 0;
    BamText_Line(&info, 22, y, "%s  motion %d (0x%X)  frame %d  %d%%", slot ? "CPU" : "YOU", (int) fp->motion_id,
                 (unsigned) fp->motion_id, (int) fp->cur_anim_frame, (int) Player_GetDamage((s32) slot));
    BamText_Style(&info, 0.5f, slot ? 0xFF9A6B : 0xF5C842);
    S = Bam_FighterCtx(fp);
    if (!bam_match || S->fighter != fp) return 1;
    if (S->active)
        BamText_Line(&info, 40, y + 15, "borrowed %s (%s)", S->active->name, who((unsigned) S->active->source_kind));
    else if (S->aerial && S->aerial->slot < 5)
        BamText_Line(&info, 40, y + 15, "borrowed %s (%s)", aerial_slots[S->aerial->slot], who(S->aerial->character));
    else if (S->normal_on)
        BamText_Line(&info, 40, y + 15, "borrowed ground attack (%s)", who((unsigned) S->normal_donor - 1));
    else
        return 1;
    BamText_Style(&info, 0.46f, 0x86B6FF);
    return 2;
}

static void draw_info(void)
{
    unsigned lines = 0;
    float y = 58;
    BamText_Begin(&info);
    if (!panel && (frozen || release_wait)) {
        BamText_Line(&info, 120, 14, "%s", release_wait ? "Release to resume" :
                     "FROZEN   D-Right: step   D-Down: resume   D-Up: build");
        BamText_Style(&info, 0.55f, 0xF5C842);
    } else if (!panel && hint_frames) {
        BamText_Line(&info, 120, 14, "%s", "D-Up: build   D-Down: freeze   D-Left: move info");
        BamText_Style(&info, 0.55f, 0xDCE6FF);
    }
    if (opt[BAM_OPT_INFO] && !panel) {
        unsigned n = fighter_lines(0, y);
        lines += n;
        lines += fighter_lines(1, y + 15.0f * n + 4);
    }
    BamText_End(&info);
    info_back.native->hidden = !lines;
    if (lines) info_back.native->box_size_y = 15.0f * lines + 14;
}

/* End the match as a native quit does (no contest), then the scene; the
 * training exit (BAM_TrainingTakeRestart) loads the match again. The
 * freeze stays on so no further frame runs. */
static void request_restart(void)
{
    restart = 1;
    frozen = panel = release_wait = 0;
    gm_SetDbPauseFlag(FREEZE);
    gmVs_GetSceneController()->state.match_result = OUTCOME_NO_CONTEST;
    fn_8016C7F0();
    gm_801A4B60();
    BAM_NOTE("training: restarting with the new build\n");
}

/* inject gm_801A4D34+0xCC: every scene loop iteration, after the pads are
 * read and before the frame-or-freeze check. */
void BAM_TrainingLoop(void)
{
    HSD_PadStatus* pad;
    u32 press;
    if (!session || restart || BAM_SCENE_MAJOR != BAM_SCENE_TRAINING) return;
    if (!ui_create()) return;
    pad = &HSD_PadMasterStatus[port];
    press = pad->trigger;
    if (hint_frames && !gm_GetDbPauseFlag(FREEZE)) --hint_frames;
    if (panel) {
        int act = BamPanel_MatchFrame(pad);
        if (act) {
            BamPanel_MatchClose();
            panel = 0;
            if (act == 2) { request_restart(); return; }
            if (!frozen) release_wait = 1;
        }
    } else if (release_wait) {
        if (!(pad->button & MENU_BUTTONS)) {
            release_wait = 0;
            gm_ClearDbPauseFlag(FREEZE);
        }
    } else if (!gm_GetDbPauseFlag(NATIVE_MENU)) {
        if (press & HSD_PAD_DPADUP) {
            gm_SetDbPauseFlag(FREEZE);
            panel = (u8) BamPanel_MatchOpen(FONT_SLOT, canvas, ckind);
            if (!panel) {
                BAM_NOTE("training: the build menu could not open (no text memory)\n");
                if (!frozen) release_wait = 1;
            }
        } else if (press & HSD_PAD_DPADDOWN) {
            if (frozen) { frozen = 0; release_wait = 1; }
            else { frozen = 1; gm_SetDbPauseFlag(FREEZE); }
        } else if (press & HSD_PAD_DPADLEFT) {
            opt[BAM_OPT_INFO] = !opt[BAM_OPT_INFO];
        } else if (frozen && ((press | pad->repeat) & HSD_PAD_DPADRIGHT)) {
            /* x3 = 0 so the loop always sees the bit change and clears it
             * after this one frame (a step right after a step left it set). */
            BAM_FRAME_STEP |= 1;
            BAM_FRAME_STEP_SEEN = 0;
        }
    }
    /* The D-pad is ours in training; the whole pad while the menu is open
     * (the fighter reads the copy). Training's own menu keeps its D-pad. */
    if (panel || release_wait || !gm_GetDbPauseFlag(NATIVE_MENU)) {
        HSD_PadStatus* c = &HSD_PadCopyStatus[port];
        u32 mask = panel || release_wait ? ~0U : DPAD;
        c->button &= ~mask; c->trigger &= ~mask; c->repeat &= ~mask; c->release &= ~mask;
        if (panel || release_wait) {
            c->stickX = c->stickY = c->subStickX = c->subStickY = 0;
            c->nml_stickX = c->nml_stickY = c->nml_subStickX = c->nml_subStickY = 0;
            c->analogL = c->analogR = 0;
            c->nml_analogL = c->nml_analogR = 0;
        }
    }
    draw_info();
}

/* Melee's develop-mode bubbles: the low two bits of the fighters' and
 * items' draw flags are 1 = model, 2 = collision bubbles; which bubbles
 * draw is filtered in ftdrawcommon.c / itdraw.c (Bam_DrawHitboxes). */
static void apply_bubbles(void)
{
    HSD_GObj* gobj;
    unsigned on = opt[BAM_OPT_HITBOXES] || opt[BAM_OPT_HURTBOXES];
    u8 bits = on ? 3 : 1;
    if (!on && !bubbles_applied) return;
    for (gobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; gobj != NULL; gobj = gobj->next) {
        Fighter* fp = GET_FIGHTER(gobj);
        fp->x21FC_flag.byte = (u8) ((fp->x21FC_flag.byte & ~3) | bits);
    }
    for (gobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_ITEM]; gobj != NULL; gobj = gobj->next) {
        Item* ip = GET_ITEM(gobj);
        ip->xDAA_byte = (u8) ((ip->xDAA_byte & ~3) | bits);
    }
    bubbles_applied = (u8) on;
}

/* Red flash for a missed L-cancel (as Slippi's code): the landing's motion
 * change has reset the colour animation by now, so this one stays. */
static void flash(Fighter* fp)
{
    ColorOverlay* co = &fp->x488;
    co->x28_colanim.ptr = NULL;
    co->x0_timer = 0;
    co->x4_pri = 20;
    co->x30_color_red = 255.0f; co->x34_color_green = 0.0f; co->x38_color_blue = 0.0f;
    co->x3C_color_alpha = 200.0f;
    co->x40_colorblend_red = co->x44_colorblend_green = co->x48_colorblend_blue = 0.0f;
    co->x4C_colorblend_alpha = -10.0f;
    co->x2C_hex.r = 255; co->x2C_hex.g = 0; co->x2C_hex.b = 0; co->x2C_hex.a = 200;
    co->x7C_color_enable = 1;
    co->x7C_flag2 = 0;
}

/* BAM_OnLandingAirLag: an aerial landing with landing lag (msid != -1). */
void Bam_TrainingLanding(Fighter* fp, int msid)
{
    if (!session || !opt[BAM_OPT_LCANCEL] || msid == -1 || !fp || fp->player_id != 0 || fp->is_sub_fighter)
        return;
    if (fp->x67F >= p_ftCommonData->xE4) lcancel_pending = fp; /* L/R pressed too early (or not at all) */
}

void Bam_TrainingFrame(void)
{
    if (!session || restart) return;
    apply_bubbles();
    if (opt[BAM_OPT_SHAKE] != shake_applied) {
        Camera_SetQuakeScale(opt[BAM_OPT_SHAKE] ? 1.0f : 0.0f);
        shake_applied = opt[BAM_OPT_SHAKE];
    }
    if (lcancel_pending) {
        flash(lcancel_pending);
        lcancel_pending = NULL;
    }
}

/* Scene exit (hooks.c): the players' own loadouts come back, with the
 * training player's menu changes kept on their port. */
void Bam_TrainingSceneExit(void)
{
    if (mapped) {
        BamLoadout mine = bam_loadouts[0];
        memcpy(bam_loadouts, saved, sizeof(saved));
        bam_loadouts[port] = mine;
        mapped = 0;
    }
    if (!session) return;
    ui_destroy();
    gm_ClearDbPauseFlag(FREEZE);
    session = 0;
    frozen = release_wait = 0;
}
