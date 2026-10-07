/* Typing a share code on the CSS (the retail name-entry keyboard).
 *
 * Called from: css_menu.c; BAM_NameEntryProc hook (project.toml).
 * State: the keyboard's entry (CSS only).
 */
#include "css_panel.h"
#include <bam/retail.h>
/* Menu code: smaller beats faster (the overlay has a fixed size). */
#pragma optimize_for_size on
#pragma auto_inline off

/* The symbols typed so far, and the outcome shown when the panel comes back. */
static int kb_len;
static unsigned char kb_code[BAM_CODE_LEN];
static const char* kb_msg;
static unsigned kb_msg_rgb;
static BamText kb_text, kb_name;

/* ---- share codes on Melee's keyboard --------------------------------------
 * A on the Code row asks the CSS to open its name-entry keyboard, as it does
 * for a new name tag (mnCharSel_804D6CF6 = 4, the port in 804D6CF9). The
 * keyboard runs as usual (its keys, cursor and sounds); only its input
 * routine is swapped for kb_input (BAM_NameEntryProc), which collects the 22
 * letters of a code instead of a 4-letter name and leaves without creating a
 * name tag. Slippi's keyboard codes only act in its connect-code mode, which
 * stays off here. */
extern HSD_GObj* mnNameNew_804D6C08;
extern void lbAudioAx_80024030(int);
#define SFX_BACK 0
#define SFX_OK 1
#define SFX_MOVE 2
#define SFX_ERROR 3

void kb_open(void)
{
    css.kb_port = css.open_port;
    close_panel();
    kb_len = 0;
    css.kb_loaded = 0;
    kb_msg = NULL;
    css.kb_frames = 0;
    kb_text.native = kb_name.native = NULL;
    css.kb_state = KB_OPENING;
    BAM_CSS_SUBSCREEN_PORT = (s8) css.kb_port;
    BAM_CSS_SUBSCREEN = 4;
    BAM_LOG("css: code keyboard for port %d\n", css.kb_port);
}

/* inject: mnNameNew_EnterFromMnCharSel, after the keyboard's input proc is
 * set up (r3 = the proc). */
static void kb_input(HSD_GObj* gobj);
void BAM_NameEntryProc(HSD_GObjProc* proc)
{
    if (css.kb_state != KB_OPENING || !proc) return;
    proc->on_invoke = kb_input;
    css.kb_state = KB_OPEN;
}

/* A key's letter as a code symbol, or -1. Melee's keyboard types full-width
 * Shift-JIS letters and digits; I and L read as 1, O as 0. */
static int kb_symbol(unsigned sel, unsigned mode)
{
    char buf[8];
    const u8* b = (const u8*) buf;
    unsigned c, i;
    memset(buf, 0, sizeof(buf));
    AddCharacterToName(buf, (u8) sel, 0, (u8) mode);
    if (b[0] == 0x82 && b[1] >= 0x60 && b[1] <= 0x79) c = 'A' + b[1] - 0x60;
    else if (b[0] == 0x82 && b[1] >= 0x81 && b[1] <= 0x9A) c = 'A' + b[1] - 0x81;
    else if (b[0] == 0x82 && b[1] >= 0x4F && b[1] <= 0x58) c = '0' + b[1] - 0x4F;
    else if (b[0] >= 'a' && b[0] <= 'z') c = b[0] - 32;
    else if ((b[0] >= 'A' && b[0] <= 'Z') || (b[0] >= '0' && b[0] <= '9')) c = b[0];
    else return -1;
    if (c == 'O') c = '0';
    if (c == 'I' || c == 'L') c = '1';
    for (i = 0; i < 32; ++i)
        if (bam_code_alphabet[i] == (char) c) return (int) i;
    return -1;
}

/* The code is drawn like Melee draws a name: a text on the keyboard's 3D
 * layer, placed from the name field's model (first letter: jobjs[14], next:
 * jobjs[15]), smaller so 22 letters fit; the field itself (jobjs[12], 13 its
 * cursor) is hidden, the code runs across where it was. Help or an error
 * goes under the keyboard's description bar. */
#define KB_HELP_CY 454.0f
extern Vec3 mnNameNew_803EE330;

/* The code so far over Melee's keyboard. */
static void kb_draw(void)
{
    static const unsigned char dash_after[] = { 4, 9, 14, 17 };
    char text[BAM_CODE_TEXT + 2], *o = text;
    unsigned k, d = 0;
    if (!kb_text.native) {
        int cv;
        if (!css.mem || !HSD_SisLib_804D1124[0]) return;
        cv = HSD_SisLib_803A611C(0, NULL, 9, 0x14, 0, 0xF, 0, 0x13);
        BamText_Create(&kb_text, 0, cv, hint_buf, sizeof(hint_buf));
        BamText_Create(&kb_name, 0, mn_804D6BB5, body_buf, 1024);
    }
    for (k = 0; k < BAM_CODE_LEN; ++k) {
        if (d < sizeof(dash_after) && k == dash_after[d]) { *o++ = '-'; ++d; }
        *o++ = (int) k < kb_len ? bam_code_alphabet[kb_code[k] & 31] : '_';
    }
    *o = 0;
    {
        NameNewEntry* data = mnNameNew_804D6C08->user_data;
        HSD_JObj* first = data->jobjs[14];
        float sp = HSD_JObjGetTranslationX(data->jobjs[15]) - HSD_JObjGetTranslationX(first);
        float units = text_w(text, 1.0f) / 0.6f, fs;
        Vec3 pos;
        lb_8000B1CC(first, &mnNameNew_803EE330, &pos);
        /* Melee's name field and its cursor (4 letters wide) would cover
         * the code: hide them while it is typed. */
        HSD_JObjSetFlagsAll(data->jobjs[12], JOBJ_HIDDEN);
        HSD_JObjSetFlagsAll(data->jobjs[13], JOBJ_HIDDEN);
        if (sp < 0) sp = -sp;
        fs = 9.5f * sp / units;
        if (fs > 0.035f) fs = 0.035f;
        kb_name.native->font_size.x = fs;
        kb_name.native->font_size.y = fs * 1.25f;
        kb_name.native->pos_x = pos.x + 1.5f * sp - units * fs * 0.5f;
        kb_name.native->pos_y = -pos.y;
        kb_name.native->pos_z = pos.z;
        BamText_Begin(&kb_name);
        BamText_Line(&kb_name, 0, 0, "%s", text);
        BamText_Style(&kb_name, 1.0f, kb_len ? WHITE : DIM);
        BamText_End(&kb_name);
    }
    {
        const char* help = kb_msg ? kb_msg : "Start loads it.  B erases.  X clears.";
        float s2 = 0.6f, w2 = text_w(help, s2);
        BamText_Begin(&kb_text);
        BamText_Line(&kb_text, (640.0f - w2) * 0.5f, KB_HELP_CY - 19.2f + 9.6f * s2, "%s", help);
        BamText_Style(&kb_text, s2, kb_msg ? kb_msg_rgb : WHITE);
        BamText_End(&kb_text);
    }
}

static void kb_close(int loaded)
{
    css.kb_loaded = loaded;
    css.kb_state = KB_BACK;
    BamText_Destroy(&kb_text);
    BamText_Destroy(&kb_name);
    mnNameNew_8023B224(0); /* back to the CSS; no name tag is made */
}

static void kb_confirm(void)
{
    BamLoadout got;
    if (kb_len < BAM_CODE_LEN) {
        lbAudioAx_80024030(SFX_ERROR);
        kb_msg = "A code has 22 letters.";
        kb_msg_rgb = WARN;
        return;
    }
    if (!Bam_LoadoutFromCode(kb_code, &got)) {
        lbAudioAx_80024030(SFX_ERROR);
        kb_msg = "Not a valid code: check each letter.";
        kb_msg_rgb = WARN;
        return;
    }
    bam_loadouts[css.kb_port] = got;
    lbAudioAx_80024030(SFX_OK);
    BAM_LOG("css: code typed for port %d\n", css.kb_port);
    kb_close(1);
}

static void kb_input(HSD_GObj* gobj)
{
    NameNewEntry* data = mnNameNew_804D6C08->user_data;
    u16* hov = &mn_804A04F0.hovered_selection;
    u32 b;
    (void) gobj;
    b = mn_804A04F0.buttons = mn_80229624((u32) css.kb_port);
    if (b & MenuInput_AButton) {
        u16 sel = *hov;
        if (sel < 0x32) {
            int sym = kb_symbol(sel, data->mode);
            if (sym < 0 || kb_len >= BAM_CODE_LEN) {
                lbAudioAx_80024030(SFX_ERROR);
            } else {
                kb_code[kb_len++] = (unsigned char) sym;
                kb_msg = NULL;
                lbAudioAx_80024030(SFX_OK);
                if (kb_len == BAM_CODE_LEN) *hov = 0x39; /* to OK */
            }
        } else if (sel == 0x36) {
            if (kb_len) { --kb_len; kb_msg = NULL; lbAudioAx_80024030(SFX_BACK); }
            else lbAudioAx_80024030(SFX_ERROR);
        } else if (sel == 0x38 || sel == 0x39) {
            kb_confirm();
            if (css.kb_state != KB_OPEN) return;
        } else if (sel == 0x32) {
            lbAudioAx_80024030(SFX_BACK);
            kb_close(0);
            return;
        } else {
            lbAudioAx_80024030(SFX_ERROR);
        }
    } else if (b & MenuInput_StartButton) {
        kb_confirm();
        if (css.kb_state != KB_OPEN) return;
    } else if (b & MenuInput_Back) {
        lbAudioAx_80024030(SFX_BACK);
        if (!kb_len) { kb_close(0); return; }
        --kb_len;
        kb_msg = NULL;
    } else if (b & MenuInput_XButton) {
        kb_len = 0;
        kb_msg = NULL;
        lbAudioAx_80024030(SFX_BACK);
    } else if (b & (MenuInput_Up | MenuInput_Down | MenuInput_Left | MenuInput_Right)) {
        u8 next = (u8) mnNameNew_8023BAA8(data, (s32) b, (u8) *hov);
        if (next != *hov) {
            lbAudioAx_80024030(SFX_MOVE);
            *hov = next;
            if (next < 0x32) data->last_key_sel = next;
        }
    }
    kb_draw();
}

/* A / X / Y on a slot row. */