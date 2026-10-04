/* Move-select panel on the character select screen (VS and Slippi Online
 * both use the retail CSS, mnCharSel).
 *
 *   Z (character chosen) -> the panel opens for that controller's port.
 *   Tabs: specials, aerials, ground attacks, smash attacks, throws, and
 *   Saved (three saved builds and the build's share code; build_store.c,
 *   build_code.c). On Saved: A loads a slot (saves into an empty one), X
 *   saves, Y deletes; A on the code types a code in.
 *   Up/Down     -> row (past the last row of a page: the buttons; past
 *                  the buttons: the next page).
 *   L/R         -> previous / next page.
 *   Left/Right  -> cycle the move for that row.
 *   A           -> next row (on RANDOMIZE: randomize; on LOCK IN: close).
 *   X           -> randomize everything.   Y -> reset the row (all, on the buttons).
 *   Start / B / Z -> lock in (close).
 *
 * While the panel is open, that controller's input is taken away from the
 * CSS (cleared in HSD_PadCopyStatus before the CSS reads it), so the cursor
 * and Slippi's own CSS features do not react.
 *
 * Hooks: BAM_CssFrame at mnCharSel_Scene_OnFrame entry (after the pads are
 * read, before the CSS and its GObj procs run), BAM_CssExit at
 * mnCharSel_Scene_OnExit entry.
 */
/* Menu code: smaller beats faster (the overlay has a fixed size). */
#pragma optimize_for_size on
#pragma auto_inline off
#include <bam/bam.h>
#include <engine/special_internal.h>
#include <engine/special_catalog.h>
#include <engine/aerial_catalog.h>
#include "ui_text.h"
#include "build_store.h"
#include <melee/mn/types.h>
#include <melee/mn/mnmain.h>
#include <melee/mn/mnnamenew.h>
#include <melee/mn/inlines.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/jobj.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/types.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/sislib.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <dolphin/os.h>
#include <dolphin/gx.h>
#include <sysdolphin/baselib/memory.h>
#include <string.h>
#include <stdio.h>

extern CSSData* mnCharSel_804D6CB0;

/* SIS font slot: the CSS uses 0; Slippi's online CSS may take others, so the
 * highest free one is chosen when the screen opens. */
static int FONT = 4;
extern SIS* HSD_SisLib_804D1124[5];
/* Rows: specials, aerials, ground attacks and throws (BamNormalSlot
 * order), the saved-build slots and the share code, then the two buttons. */
#define FIRST_AERIAL BAM_SPECIAL_SLOTS
#define FIRST_NORMAL (BAM_SPECIAL_SLOTS + BAM_AERIAL_SLOTS)
#define MOVE_ROWS (FIRST_NORMAL + BAM_NORMAL_SLOTS)
#define ROW_SLOT0 MOVE_ROWS
#define ROW_CODE (ROW_SLOT0 + BAM_SAVE_SLOTS)
#define ROW_RANDOM (ROW_CODE + 1)
#define ROW_LOCK (ROW_CODE + 2)
#define ROWS (ROW_LOCK + 1)
/* Tabs down the left of the panel, one group of rows each. */
#define PAGES 6
#define PAGE_SAVED 5
typedef struct PanelTab { const char* tab; const char* sub; const char* title; unsigned char first, count; } PanelTab;
static const PanelTab tabs[PAGES] = {
    { "SPECIALS", 0, "Special Moves", 0, BAM_SPECIAL_SLOTS },
    { "AERIALS", 0, "Aerials", FIRST_AERIAL, BAM_AERIAL_SLOTS },
    { "GROUND", "JAB / DASH / TILTS", "Ground Attacks", FIRST_NORMAL + BAM_NORMAL_JAB, 5 },
    { "SMASH", "ATTACKS", "Smash Attacks", FIRST_NORMAL + BAM_NORMAL_FSMASH, 3 },
    { "THROWS", 0, "Throws", FIRST_NORMAL + BAM_NORMAL_FTHROW, 4 },
    { "SAVED", 0, "Saved Builds", ROW_SLOT0, BAM_SAVE_SLOTS + 1 },
};
/* Layout, in the CSS's 640 x 480 screen space: a header, tabs down the
 * left, the tab's moves on the right, two buttons and a key strip. */
#define PANEL_X 64.0f
#define PANEL_Y 50.0f
#define PANEL_W 512.0f
#define PANEL_H 380.0f
#define HEAD_H 46.0f
#define SIDE_X 74.0f
#define SIDE_W 112.0f
#define TAB_Y 110.0f
#define TAB_H 32.0f
#define TAB_STEP 36.0f
#define CONT_X 200.0f
#define CONT_W 364.0f
#define TITLE_Y 108.0f
#define ROW_Y 140.0f
#define ROW_H 32.0f
#define ROW_STEP 36.0f
#define BTN_Y 334.0f
#define BTN_H 36.0f
#define FOOT_Y 386.0f
#define GOLD 0xF5C842
#define INK 0x111522
#define WHITE 0xF2F4FA
#define DIM 0x7A8399
#define SOFT 0xB4BBCE
#define BLUE 0x86B6FF
#define LINE 0x262D40
#define CARD 0x171C2A
#define WARN 0xFF9A4D
#define CARD_ON 0x222A3E

static const char* const ckind_names[26] = {
    "Captain Falcon", "Donkey Kong", "Fox", "Mr Game and Watch", "Kirby", "Bowser",
    "Link", "Luigi", "Mario", "Marth", "Mewtwo", "Ness", "Peach", "Pikachu",
    "Ice Climbers", "Jigglypuff", "Samus", "Yoshi", "Zelda", "Sheik", "Falco",
    "Young Link", "Dr Mario", "Roy", "Pichu", "Ganondorf"
};
static const char* const ckind_short[26] = {
    "Falcon", "DK", "Fox", "G&W", "Kirby", "Bowser", "Link", "Luigi", "Mario", "Marth", "Mewtwo",
    "Ness", "Peach", "Pikachu", "ICs", "Puff", "Samus", "Yoshi", "Zelda", "Sheik", "Falco",
    "YLink", "Doc", "Roy", "Pichu", "Ganon"
};
static const char* const row_labels[MOVE_ROWS] = {
    "Neutral B", "Side B", "Up B", "Down B",
    "Neutral Air", "Forward Air", "Back Air", "Up Air", "Down Air",
    "Jab", "Dash Attack", "Forward Tilt", "Up Tilt", "Down Tilt",
    "Forward Smash", "Up Smash", "Down Smash",
    "Forward Throw", "Back Throw", "Up Throw", "Down Throw"
};

static int ui_ready, ui_tried, canvas, first_frame;
static int open_port = -1, row, page, ckind, just_opened;
/* Share-code entry on Melee's own keyboard (the name entry screen the CSS
 * opens for new name tags): the port typing, the symbols typed so far, and
 * the outcome shown when the panel comes back. */
static int kb_state, kb_port, kb_len, kb_frames, kb_loaded;
enum { KB_OFF, KB_OPENING, KB_OPEN, KB_BACK };
static unsigned char kb_code[BAM_CODE_LEN];
static const char* kb_msg;
static unsigned kb_msg_rgb;
static BamText kb_text, kb_name;
/* A short message in the panel's title line ("Saved to slot 2"). */
static const char* toast;
static unsigned toast_rgb, toast_frames;
/* Port whose build was locked in last: the local player's build online. */
int bam_css_port = 0;
static s8 last_ckind[4] = { -1, -1, -1, -1 };
static unsigned stick_hold[4];
static int stick_dir[4];
static BamText hint, shapes, body;
/* SIS text streams (see ui_text.c). */
/* SIS text streams (see ui_text.c) and the panel's shapes live in one
 * scene-heap block taken when the CSS opens (static buffers would sit in the
 * overlay, which has no room to spare). */
typedef struct PanelQuad { s16 x, y, w, h; u8 kind, pad[3]; u32 rgba; } PanelQuad;
#define MAX_QUADS 200
typedef struct MenuMem {
    u32 hint[1024 / 4], body[4096 / 4], shapes[128 / 4];
    PanelQuad quads[MAX_QUADS];
} MenuMem;
static MenuMem* mem;
#define hint_buf (mem->hint)
#define body_buf (mem->body)
#define shapes_buf (mem->shapes)
#define quads (mem->quads)

static int same_family(int a, int b)
{
    if (a == b) return 1;
    return (a == CKind_Zelda || a == CKind_Seak) && (b == CKind_Zelda || b == CKind_Seak);
}

static int special_ok(const RogueSpecialDef* d, unsigned slot)
{
    return d && d->slot == slot && RogueSpecial_Offerable(d) && Rogue_GetAbility(d->id) &&
           !same_family((int) d->character, ckind);
}

static int aerial_ok(const RogueAerialDef* d, unsigned slot)
{
    return d && d->slot == slot && !same_family((int) d->character, ckind);
}

/* Step through native (0) and every move that fits the slot. */
static unsigned cycle_special(unsigned slot, unsigned cur, int dir)
{
    unsigned ids[ROGUE_SPECIALS + 1], n = 0, at = 0, i;
    ids[n++] = 0;
    for (i = 0; i < ROGUE_SPECIALS; ++i)
        if (special_ok(&rogue_specials[i], slot)) {
            if (rogue_specials[i].id == cur) at = n;
            ids[n++] = rogue_specials[i].id;
        }
    return ids[(at + n + dir) % n];
}

static unsigned cycle_aerial(unsigned slot, unsigned cur, int dir)
{
    unsigned ids[ROGUE_AERIALS + 1], n = 0, at = 0, i;
    ids[n++] = 0;
    for (i = 0; i < ROGUE_AERIALS; ++i)
        if (aerial_ok(&rogue_aerials[i], slot)) {
            if (rogue_aerials[i].id == cur) at = n;
            ids[n++] = rogue_aerials[i].id;
        }
    return ids[(at + n + dir) % n];
}

/* Ground attacks and throws: any other character (CharacterKind + 1). */
static int normal_ok(unsigned v)
{
    return v >= 1 && v <= 26 && !same_family((int) v - 1, ckind);
}

static unsigned cycle_normal(unsigned cur, int dir)
{
    unsigned i, v = cur > 26 ? 0 : cur;
    for (i = 0; i < 27; ++i) {
        v = (v + 27 + dir) % 27;
        if (!v || normal_ok(v)) return v;
    }
    return 0;
}

static void loadout_fix(BamLoadout* l)
{
    unsigned i, any = 0;
    for (i = 0; i < BAM_NORMAL_SLOTS; ++i) {
        if (l->normals[i] && !normal_ok(l->normals[i])) l->normals[i] = 0;
        any |= l->normals[i];
    }
    for (i = 0; i < BAM_SPECIAL_SLOTS; ++i) {
        const RogueSpecialDef* d = RogueSpecial_Find(l->specials[i]);
        if (l->specials[i] && !special_ok(d, i)) l->specials[i] = 0;
        any |= l->specials[i];
    }
    for (i = 0; i < BAM_AERIAL_SLOTS; ++i) {
        const RogueAerialDef* d = RogueAerial_Find(l->aerials[i]);
        if (l->aerials[i] && !aerial_ok(d, i)) l->aerials[i] = 0;
        any |= l->aerials[i];
    }
    l->enabled = any != 0;
}

/* ---- drawing ---------------------------------------------------------- */

/* Bytes left in the scene's SIS pool (the CSS gets 9 KB, Slippi Online 18 KB,
 * and both are mostly used by the CSS's own texts). Each of our texts takes
 * ~0x100 bytes of it; their streams use our own buffers. */
static unsigned sis_free(void)
{
    unsigned n = 0;
    SisBlock* b;
    for (b = free_head; b; b = b->next) n += (unsigned) b->size;
    return n;
}
#define SIS_NEEDED 0x800

static void ui_create(void)
{
    if (ui_ready || ui_tried) return;
    ui_tried = 1;
    BAM_LOG("css sis free %u bytes\n", sis_free());
    if (sis_free() < SIS_NEEDED) return;
    /* Use the CSS's own SIS font (slot 0, loaded by the CSS) instead of
     * loading one into a free slot: Slippi's direct-code entry loads its
     * own text data while the CSS is up, and a slot we held (and later
     * freed) froze the game there. */
    FONT = 0;
    if (!HSD_SisLib_804D1124[FONT]) { BAM_LOG("css: CSS font not loaded\n"); return; }
    /* The block lives in this CSS visit's scene heap: reused only when the
     * panel comes back from Melee's keyboard in the same visit. Any other
     * old pointer is stale (the match rebuilt the heap since). */
    if (!mem || kb_state != KB_BACK) mem = HSD_MemAlloc(sizeof(MenuMem));
    if (!mem) { BAM_NOTE("css: no memory for the panel\n"); return; }
    canvas = HSD_SisLib_803A611C(FONT, NULL, 9, 0x14, 0, 0xF, 0, 0x13);
    BamText_Create(&hint, FONT, canvas, hint_buf, sizeof(hint_buf));
    ui_ready = 1;
    Bam_StoreInit(); /* saved builds from the memory card, once per boot */
}

/* ---- shapes ----------------------------------------------------------
 * Every box, bar and outline of the panel is drawn by one SIS text's render
 * callback (the CSS's SIS pool has room for only a dozen texts): a list of
 * quads built each frame with the panel's text, in the same screen space. */
/* Quad kind: 0 box, 1 triangle pointing left, 2 triangle pointing right. */
static unsigned nquads;

static void shape(float x, float y, float w, float h, unsigned rgb, unsigned alpha, unsigned kind)
{
    PanelQuad* q;
    if (nquads >= MAX_QUADS || w <= 0 || h <= 0) return;
    q = &quads[nquads++];
    q->x = (s16) x; q->y = (s16) y; q->w = (s16) w; q->h = (s16) h; q->kind = (u8) kind;
    q->rgba = (rgb << 8) | (alpha & 0xFF);
}
#define quad(x, y, w, h, rgb, alpha) shape(x, y, w, h, rgb, alpha, 0)

/* A box with its corners cut by r pixels (reads as slightly rounded). */
static void rbox(float x, float y, float w, float h, unsigned rgb, unsigned alpha, float r)
{
    quad(x + r, y, w - 2 * r, h, rgb, alpha);
    quad(x, y + r, r, h - 2 * r, rgb, alpha);
    quad(x + w - r, y + r, r, h - 2 * r, rgb, alpha);
}

/* A rounded box with a border t pixels wide. */
static void rframe(float x, float y, float w, float h, unsigned fill, unsigned border, float t)
{
    rbox(x, y, w, h, border, 255, 2);
    rbox(x + t, y + t, w - 2 * t, h - 2 * t, fill, 255, 2);
}

static void draw_quads(void* gobj)
{
    unsigned i;
    float z = shapes.native ? shapes.native->pos_z : 0.0f;
    (void) gobj;
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    for (i = 0; i < nquads; ++i) {
        const PanelQuad* q = &quads[i];
        GXColor c;
        float x0 = q->x, x1 = (float) q->x + q->w, y0 = -(float) q->y, y1 = -((float) q->y + q->h);
        float ym = (y0 + y1) * 0.5f;
        float ax = x0, ay = y0, bx = x1, by = y0, cx = x1, cy = y1, dx = x0, dy = y1;
        if (q->kind == 1) { ax = dx = x0; ay = dy = ym; }      /* tip on the left */
        else if (q->kind == 2) { bx = cx = x1; by = cy = ym; } /* tip on the right */
        c.r = (u8) (q->rgba >> 24); c.g = (u8) (q->rgba >> 16); c.b = (u8) (q->rgba >> 8); c.a = (u8) q->rgba;
        GXSetTevColor(GX_TEVREG0, c);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(ax, ay, z); GXTexCoord2f32(0.0f, 0.0f);
        GXPosition3f32(bx, by, z); GXTexCoord2f32(1.0f, 0.0f);
        GXPosition3f32(cx, cy, z); GXTexCoord2f32(1.0f, 1.0f);
        GXPosition3f32(dx, dy, z); GXTexCoord2f32(0.0f, 1.0f);
        GXEnd();
    }
}

static void panel_destroy(void)
{
    if (!body.native) return;
    BamText_Destroy(&body);
    BamText_Destroy(&shapes);
    nquads = 0;
}

static int panel_create(void)
{
    if (body.native) return 1;
    if (!ui_ready || sis_free() < SIS_NEEDED) {
        BAM_LOG("css panel: no SIS memory (%u free)\n", ui_ready ? sis_free() : 0);
        return 0;
    }
    /* An empty box with no background of its own: only its callback draws. */
    nquads = 0;
    BamText_Box(&shapes, FONT, canvas, shapes_buf, PANEL_X, PANEL_Y, 1, 1, 0, 0);
    shapes.native->render_callback = draw_quads;
    BamText_Create(&body, FONT, canvas, body_buf, sizeof(body_buf));
    return 1;
}

static void ui_destroy(void)
{
    if (ui_ready) {
        panel_destroy();
        BamText_Destroy(&hint);
        ui_ready = 0;
    }
    /* Also after Slippi's code entry hid the panel (ui_ready 0): the block
     * must not outlive this CSS visit's heap. */
    if (mem) HSD_Free(mem);
    mem = NULL;
}

/* The move a row holds (0 = the fighter's own). */
static unsigned row_value(const BamLoadout* l, unsigned r)
{
    if (r < FIRST_AERIAL) return l->specials[r];
    if (r < FIRST_NORMAL) return l->aerials[r - FIRST_AERIAL];
    if (r < MOVE_ROWS) return l->normals[r - FIRST_NORMAL];
    return 0;
}

/* How many of rows first..first+count-1 hold a borrowed move. */
static unsigned custom_count(const BamLoadout* l, unsigned first, unsigned count)
{
    unsigned n = 0;
    while (count--) n += row_value(l, first++) != 0;
    return n;
}

static int page_of(int r)
{
    int p;
    for (p = 0; p < PAGES; ++p)
        if (r >= tabs[p].first && r < tabs[p].first + tabs[p].count) return p;
    return 0;
}
static int page_first(int p) { return tabs[p].first; }
static int page_last(int p) { return tabs[p].first + tabs[p].count - 1; }

/* Why the last online match played without builds (online_sync.c). SIS
 * cuts a line after about 50 bytes, so keep these short. */
static const char* notice_text(void)
{
    switch (bam_online_notice) {
    case BAM_NOTICE_OTHER_VERSION: return "Opponent has a different BuildAMelee version";
    case BAM_NOTICE_NO_ANSWER: return "Opponent has no BuildAMelee or quick chat off";
    case BAM_NOTICE_CHAT_OFF: return "Opponent has quick chat off (it sends builds)";
    }
    return NULL;
}

static void draw_hint(void)
{
    unsigned p, lines = 0;
    const char* notice;
    if (!hint.native) return;
    BamText_Begin(&hint);
    if (open_port < 0) {
        BamText_Line(&hint, 24, 452 - 16 * lines++, "BuildAMelee v%s", BAM_VERSION);
        BamText_Style(&hint, 0.6f, DIM);
        notice = notice_text();
        if (notice) {
            BamText_Line(&hint, 24, 452 - 16 * lines++, "%s", notice);
            BamText_Style(&hint, 0.7f, WARN);
            BamText_Line(&hint, 24, 452 - 16 * lines++, "%s", "Last online match: builds were off");
            BamText_Style(&hint, 0.7f, WARN);
        }
    }
    for (p = 0; p < 4 && open_port < 0; ++p) {
        const BamLoadout* l = &bam_loadouts[p];
        PlayerInitData* pl;
        unsigned n;
        if (!mnCharSel_804D6CB0) break;
        pl = &mnCharSel_804D6CB0->vs.start.players[p];
        if (pl->slot_type == 1 || (u8) pl->ckind >= 26) continue;
        n = l->enabled ? custom_count(l, 0, MOVE_ROWS) : 0;
        if (n)
            BamText_Line(&hint, 24, 452 - 16 * lines++, "P%u  Build A Fighter: %u borrowed moves   (Z to edit)", p + 1, n);
        else
            BamText_Line(&hint, 24, 452 - 16 * lines++, "P%u  Press Z to Build A Fighter", p + 1);
        BamText_Style(&hint, 0.8f, n ? GOLD : SOFT);
    }
    BamText_End(&hint);
}

/* The fighter's own special for a slot ("Rest"), from the catalog. */
static const RogueSpecialDef* own_special(unsigned slot)
{
    unsigned i;
    for (i = 0; i < ROGUE_SPECIALS; ++i)
        if (rogue_specials[i].slot == slot && (int) rogue_specials[i].character == ckind)
            return &rogue_specials[i];
    return NULL;
}

/* A special's name without its character ("Fox Illusion" -> "Illusion"),
 * which the row shows next to it. */
static const char* move_name(const RogueSpecialDef* d)
{
    static const char* const prefixes[] = { "Fox ", "Falco ", "Luigi ", "Young Link ", "Dr. Mario ", "Pichu ",
                                            "Marth ", "Roy " };
    unsigned i;
    if (!d) return "";
    for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        unsigned n = (unsigned) strlen(prefixes[i]);
        if (!strncmp(d->name, prefixes[i], n) && d->name[n]) return d->name + n;
    }
    return d->name;
}

/* Whose move row i holds (a CharacterKind) and, for specials, its name. */
static int row_owner(unsigned i, const char** move)
{
    const BamLoadout* l = &bam_loadouts[open_port];
    unsigned id = row_value(l, i);
    *move = NULL;
    if (i < FIRST_AERIAL) {
        const RogueSpecialDef* d = RogueSpecial_Find(id);
        if (!id || !d) d = own_special(i);
        *move = move_name(d);
        return id && d ? (int) d->character : ckind;
    }
    if (i < FIRST_NORMAL) {
        const RogueAerialDef* d = RogueAerial_Find(id);
        return id && d ? (int) d->character : ckind;
    }
    return id >= 1 && id <= 26 ? (int) id - 1 : ckind;
}

/* ---- text metrics ----------------------------------------------------
 * Measured with the font's own tables, as SIS lays the line out: each glyph
 * advances 32 - (left + right - 2) font units (its kerning pair), a space
 * 16; BamText's font size is 0.6 screen pixels per unit. */
extern u8 HSD_SisLib_8040C680[0x240]; /* ASCII (from 0x20) -> glyph code */
extern u8 HSD_SisLib_8040CB00[0x240]; /* glyph -> kerning pair */

static float text_w(const char* s, float scale)
{
    float units = 0;
    for (; *s; ++s) {
        unsigned c = (u8) *s, code;
        if (c == ' ' || c < 0x20 || c > 0x7E) { units += 16; continue; }
        code = (HSD_SisLib_8040C680[(c - 0x20) * 2] << 8) | HSD_SisLib_8040C680[(c - 0x20) * 2 + 1];
        if (code >= 0x2000 && code < 0x2120) {
            const u8* k = &HSD_SisLib_8040CB00[(code - 0x2000) * 2];
            units += 34.0f - (float) k[0] - (float) k[1];
        } else {
            units += 32;
        }
    }
    return units * 0.6f * scale;
}

/* SIS puts a line's glyph cells (19.2 px tall at scale 1) on the bottom of
 * a full-size cell: at scale s the glyphs span y + 19.2(1 - s) .. y + 19.2.
 * put() takes the vertical centre of the text instead. */
static float put(float x, float cy, float scale, unsigned rgb, const char* s)
{
    BamText_Line(&body, x, cy - 19.2f + 9.6f * scale, "%s", s);
    BamText_Style(&body, scale, rgb);
    return text_w(s, scale);
}

/* Largest scale <= `scale` at which s fits in `room` pixels. */
static float fit(const char* s, float scale, float room)
{
    float w = text_w(s, scale);
    return w > room ? scale * room / w : scale;
}

static float put_fit(float x, float cy, float scale, float room, unsigned rgb, const char* s)
{
    return put(x, cy, fit(s, scale, room), rgb, s);
}

static void put_right(float right, float cy, float scale, unsigned rgb, const char* s)
{
    put(right - text_w(s, scale), cy, scale, rgb, s);
}

static void put_center(float x, float w, float cy, float scale, unsigned rgb, const char* s)
{
    put(x + (w - text_w(s, scale)) * 0.5f, cy, scale, rgb, s);
}

/* A label in a filled pill, right-aligned at `right`; returns its left. */
static float pill(float right, float cy, const char* s, unsigned fill, unsigned rgb)
{
    float scale = 0.5f, w = text_w(s, scale) + 12;
    rbox(right - w, cy - 8, w, 16, fill, 255, 2);
    put(right - w + 6, cy, scale, rgb, s);
    return right - w;
}

/* ---- panel -------------------------------------------------------------- */

#define ROW_TEXT 0.62f
#define LABEL_W 104.0f

/* One move row:  Label   < Character  Move   >   BORROWED */
static void draw_row(unsigned i, float y)
{
    const BamLoadout* l = &bam_loadouts[open_port];
    int selected = row == (int) i, borrowed = row_value(l, i) != 0;
    float cy = y + ROW_H * 0.5f, x = CONT_X + 14, right = CONT_X + CONT_W - 12;
    float tag = text_w("BORROWED", 0.5f) + 12, vx, room, scale, w;
    const char* move;
    const char* who;
    int owner = row_owner(i, &move);
    rbox(CONT_X, y, CONT_W, ROW_H, selected ? CARD_ON : CARD, 255, 2);
    if (selected) quad(CONT_X, y + 6, 3, ROW_H - 12, GOLD, 255);
    put_fit(x, cy, ROW_TEXT, LABEL_W - 8, selected ? WHITE : DIM, row_labels[i]);

    /* The move: the character, then (specials) the move's name. */
    vx = x + LABEL_W + 16;
    room = right - tag - 26 - vx;
    who = move ? ckind_short[owner] : ckind_names[owner];
    scale = ROW_TEXT;
    w = text_w(who, scale) + (move ? 8 + text_w(move, scale) : 0);
    if (w > room) scale *= room / w;
    w = put(vx, cy, scale, borrowed ? BLUE : SOFT, who);
    if (move) put(vx + w + 8 * scale / ROW_TEXT, cy, scale, borrowed ? WHITE : DIM, move);
    if (selected) {
        shape(vx - 14, cy - 5, 6, 10, GOLD, 255, 1);
        shape(right - tag - 18, cy - 5, 6, 10, GOLD, 255, 2);
    }
    if (borrowed) pill(right, cy, "BORROWED", GOLD, INK);
    else put_center(right - tag, tag, cy, 0.5f, 0x566078, "OWN");
}

static unsigned saved_count(void)
{
    unsigned i, n = 0;
    for (i = 0; i < BAM_SAVE_SLOTS; ++i) n += bam_saved[i].used;
    return n;
}

/* The character a borrowed move in row r comes from, or -1. */
static int row_donor(const BamLoadout* l, unsigned r)
{
    unsigned id = row_value(l, r);
    if (!id) return -1;
    if (r < FIRST_AERIAL) { const RogueSpecialDef* d = RogueSpecial_Find(id); return d ? (int) d->character : -1; }
    if (r < FIRST_NORMAL) { const RogueAerialDef* d = RogueAerial_Find(id); return d ? (int) d->character : -1; }
    return (int) id - 1;
}

/* A slot's summary: "12 moves  Marth Falcon Fox" (first donors, row order). */
static void slot_summary(const BamLoadout* l, char* out)
{
    unsigned r, n = 0, shown = 0;
    u8 seen[26];
    char* o = out;
    memset(seen, 0, sizeof(seen));
    n = custom_count(l, 0, MOVE_ROWS);
    o += sprintf(o, "%u move%s ", n, n == 1 ? "" : "s");
    for (r = 0; r < MOVE_ROWS; ++r) {
        int who = row_donor(l, r);
        if (who < 0 || who >= 26 || seen[who]) continue;
        seen[who] = 1;
        if (shown < 3) { o += sprintf(o, " %s", ckind_short[who]); ++shown; }
    }
}

/* Saved tab: three slots, then the share code of the build being edited. */
static void draw_saved_row(unsigned i, float y)
{
    int selected = row == (int) i;
    float cy = y + ROW_H * 0.5f, x = CONT_X + 14, right = CONT_X + CONT_W - 12, vx = x + 70;
    char buf[48];
    rbox(CONT_X, y, CONT_W, ROW_H, selected ? CARD_ON : CARD, 255, 2);
    if (selected) quad(CONT_X, y + 6, 3, ROW_H - 12, GOLD, 255);
    if (i == ROW_CODE) {
        unsigned char code[BAM_CODE_LEN];
        put(x, cy, ROW_TEXT, selected ? WHITE : DIM, "Code");
        Bam_CodeFromLoadout(&bam_loadouts[open_port], code);
        Bam_CodeText(code, buf);
        put_fit(vx, cy, ROW_TEXT, right - vx - 60, BLUE, buf);
        if (selected) pill(right, cy, "A ENTER", GOLD, INK);
        return;
    }
    {
        const BamSavedSlot* sv = &bam_saved[i - ROW_SLOT0];
        BamLoadout l;
        buf[0] = 'S'; buf[1] = 'l'; buf[2] = 'o'; buf[3] = 't'; buf[4] = ' ';
        buf[5] = (char) ('1' + i - ROW_SLOT0); buf[6] = 0;
        put(x, cy, ROW_TEXT, selected ? WHITE : DIM, buf);
        if (sv->used && Bam_LoadoutFromCode(sv->code, &l)) {
            slot_summary(&l, buf);
            put_fit(vx, cy, ROW_TEXT, right - vx - text_w("A LOAD", 0.5f) - 24, WHITE, buf);
            if (selected) pill(right, cy, "A LOAD", GOLD, INK);
        } else {
            put(vx, cy, ROW_TEXT, 0x566078, "Empty");
            if (selected) pill(right, cy, "A SAVE", GOLD, INK);
        }
    }
}

static void draw_tab(int t, float y)
{
    const PanelTab* tb = &tabs[t];
    int on = page == t;
    unsigned n = t == PAGE_SAVED ? saved_count() : custom_count(&bam_loadouts[open_port], tb->first, tb->count);
    float cy = y + TAB_H * 0.5f;
    if (on) {
        rbox(SIDE_X, y, SIDE_W, TAB_H, CARD_ON, 255, 2);
        quad(SIDE_X, y + 7, 3, TAB_H - 14, GOLD, 255);
    }
    put(SIDE_X + 14, cy, 0.62f, on ? WHITE : DIM, tb->tab);
    if (n) {
        char num[4];
        num[0] = (char) ('0' + n); num[1] = 0;
        put_right(SIDE_X + SIDE_W - 10, cy, 0.56f, GOLD, num);
    }
}

static void draw_button(float x, float w, const char* label, int on, int primary)
{
    float cy = BTN_Y + BTN_H * 0.5f;
    if (primary) {
        if (on) rbox(x - 2, BTN_Y - 2, w + 4, BTN_H + 4, WHITE, 255, 3);
        rbox(x, BTN_Y, w, BTN_H, on ? 0xFFD95E : GOLD, 255, 2);
    } else {
        rframe(x, BTN_Y, w, BTN_H, on ? CARD_ON : CARD, on ? GOLD : 0x3A4258, on ? 2 : 1);
    }
    put_center(x, w, cy, 0.7f, primary ? INK : on ? GOLD : SOFT, label);
}

/* Key strip: "key action" pairs, centred under the panel's contents. */
static void draw_keys(void)
{
    static const char* const move_keys[] = { "L/R", "Tab", "Left/Right", "Change", "X", "Random", "Y", "Reset",
                                             "Start", "Done" };
    static const char* const saved_keys[] = { "L/R", "Tab", "A", "Load", "X", "Save", "Y", "Delete",
                                              "Start", "Done" };
    const char* const* keys = page == PAGE_SAVED ? saved_keys : move_keys;
    const float s = 0.54f, gap = 6, spread = 18;
    float total = 0, x, cy = FOOT_Y + 22;
    unsigned i;
    for (i = 0; i < 10; i += 2)
        total += text_w(keys[i], s) + gap + text_w(keys[i + 1], s) + (i < 8 ? spread : 0);
    x = PANEL_X + (PANEL_W - total) * 0.5f;
    for (i = 0; i < 10; i += 2) {
        x += put(x, cy, s, SOFT, keys[i]) + gap;
        x += put(x, cy, s, DIM, keys[i + 1]) + spread;
    }
}

static void draw_panel(void)
{
    const BamLoadout* l = &bam_loadouts[open_port];
    const PanelTab* tb = &tabs[page];
    unsigned i, n = custom_count(l, tb->first, tb->count);
    float hy = PANEL_Y + HEAD_H * 0.5f, x;
    char buf[32];
    int t;
    nquads = 0;
    BamText_Begin(&body);

    /* Panel, with a thin border and a gold line along its top. */
    rbox(PANEL_X - 1, PANEL_Y - 1, PANEL_W + 2, PANEL_H + 2, 0x2C3448, 255, 3);
    rbox(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 0x0E121D, 245, 3);
    quad(PANEL_X + 3, PANEL_Y, PANEL_W - 6, 2, GOLD, 255);

    /* Header: title on the left, player and character on the right. */
    quad(PANEL_X + 18, hy - 9, 3, 18, GOLD, 255);
    put(PANEL_X + 30, hy, 0.82f, WHITE, "BUILD A FIGHTER");
    x = PANEL_X + PANEL_W - 18 - text_w(ckind_names[ckind], 0.66f);
    put(x, hy, 0.66f, SOFT, ckind_names[ckind]);
    buf[0] = 'P'; buf[1] = (char) ('1' + open_port); buf[2] = 0;
    pill(x - 8, hy, buf, GOLD, INK);
    quad(PANEL_X + 12, PANEL_Y + HEAD_H, PANEL_W - 24, 1, LINE, 255);

    /* Tabs. */
    for (t = 0; t < PAGES; ++t) draw_tab(t, TAB_Y + TAB_STEP * t);
    quad(CONT_X - 8, TITLE_Y, 1, BTN_Y + BTN_H - TITLE_Y, LINE, 255);

    /* The tab's moves (or slots, or the code being typed). */
    put(CONT_X + 2, TITLE_Y + 12, 0.76f, WHITE, tb->title);
    if (toast_frames) --toast_frames;
    if (toast_frames) {
        put_right(CONT_X + CONT_W - 2, TITLE_Y + 12, 0.54f, toast_rgb, toast);
    } else if (page == PAGE_SAVED) {
        put_right(CONT_X + CONT_W - 2, TITLE_Y + 12, 0.54f, DIM,
                  bam_store_on_card ? "On memory card" : "Kept until you quit");
    } else {
        memcpy(buf, "0 of 0 borrowed", 16);
        buf[0] = (char) ('0' + n); buf[5] = (char) ('0' + tb->count);
        put_right(CONT_X + CONT_W - 2, TITLE_Y + 12, 0.54f, n ? GOLD : DIM, buf);
    }
    for (i = 0; i < tb->count; ++i) {
        if (page == PAGE_SAVED) draw_saved_row(tb->first + i, ROW_Y + ROW_STEP * i);
        else draw_row(tb->first + i, ROW_Y + ROW_STEP * i);
    }
    draw_button(CONT_X, (CONT_W - 10) * 0.5f, "RANDOMIZE", row == ROW_RANDOM, 0);
    draw_button(CONT_X + (CONT_W + 10) * 0.5f, (CONT_W - 10) * 0.5f, "LOCK IN", row == ROW_LOCK, 1);

    quad(PANEL_X + 12, FOOT_Y, PANEL_W - 24, 1, LINE, 255);
    draw_keys();
    BamText_End(&body);
}

/* ---- input ------------------------------------------------------------ */

#define DIR_UP 1
#define DIR_DOWN 2
#define DIR_LEFT 3
#define DIR_RIGHT 4

/* D-pad (with the pad's own repeat) or control stick (own repeat). */
static int read_dir(int port, const HSD_PadStatus* pad)
{
    u32 b = pad->trigger | pad->repeat;
    int dir = 0;
    if (b & HSD_PAD_DPADUP) return DIR_UP;
    if (b & HSD_PAD_DPADDOWN) return DIR_DOWN;
    if (b & HSD_PAD_DPADLEFT) return DIR_LEFT;
    if (b & HSD_PAD_DPADRIGHT) return DIR_RIGHT;
    if (pad->stickY > 50) dir = DIR_UP;
    else if (pad->stickY < -50) dir = DIR_DOWN;
    else if (pad->stickX < -50) dir = DIR_LEFT;
    else if (pad->stickX > 50) dir = DIR_RIGHT;
    if (dir != stick_dir[port]) {
        stick_dir[port] = dir;
        stick_hold[port] = 0;
        return dir;
    }
    if (!dir) return 0;
    ++stick_hold[port];
    return stick_hold[port] >= 14 && (stick_hold[port] - 14) % 5 == 0 ? dir : 0;
}

/* gm_EvaluateAllControllerInputs copies the pads into this per-port table
 * before the scene's frame runs; Slippi's online CSS (quick chat on the
 * D-pad) and menu code read it. Rebuilt from the pads every frame. */
struct GmPadState { u64 button, trigger, repeat, release, repeat2; s32 timer, x2C; };
#define gm_pad_states ((struct GmPadState*) 0x80479C30) /* controller_map */

static void swallow(HSD_PadStatus* pad)
{
    int port = (int) (pad - HSD_PadCopyStatus);
    if (port >= 0 && port < 4) {
        struct GmPadState* g = &gm_pad_states[port];
        g->button = g->trigger = g->repeat = g->release = g->repeat2 = 0;
        g = &gm_pad_states[4]; /* "any controller" */
        g->trigger = g->repeat = g->release = g->repeat2 = 0;
    }
    /* Keep button/last_button: the pad library derives next frame's
     * trigger from them. Clearing them made a held button "press" again
     * every frame (panel flickering open/closed) and leaked the lock-in A
     * into the CSS after the panel closed. */
    pad->trigger = pad->repeat = pad->release = 0;
    pad->stickX = pad->stickY = pad->subStickX = pad->subStickY = 0;
    pad->nml_stickX = pad->nml_stickY = pad->nml_subStickX = pad->nml_subStickY = 0;
    pad->analogL = pad->analogR = 0;
    pad->nml_analogL = pad->nml_analogR = 0;
}

static void open_panel(int port, int kind)
{
    open_port = port;
    ckind = kind;
    row = 0;
    page = 0;
    toast_frames = 0;
    loadout_fix(&bam_loadouts[port]);
    if (!panel_create()) { open_port = -1; return; }
    just_opened = 1; /* ignore the input of the frame that opened it */
    BAM_LOG("css panel open port=%d ckind=%d\n", port, kind);
}

static void close_panel(void)
{
    BamLoadout* l = &bam_loadouts[open_port];
    loadout_fix(l);
    BAM_LOG("css lock in port=%d enabled=%d specials=%u,%u,%u,%u aerials=%u,%u,%u,%u,%u\n",
            open_port, l->enabled, l->specials[0], l->specials[1], l->specials[2], l->specials[3],
            l->aerials[0], l->aerials[1], l->aerials[2], l->aerials[3], l->aerials[4]);
    BAM_LOG("css lock in port=%d normals=%u,%u,%u,%u,%u %u,%u,%u %u,%u,%u,%u\n", open_port,
            l->normals[0], l->normals[1], l->normals[2], l->normals[3], l->normals[4], l->normals[5],
            l->normals[6], l->normals[7], l->normals[8], l->normals[9], l->normals[10], l->normals[11]);
    bam_css_port = open_port;
    open_port = -1;
    panel_destroy();
}

/* Menu-only RNG (never touches gameplay state). */
static unsigned rng;
static unsigned rand_below(unsigned n)
{
    if (!rng) rng = (unsigned) OSGetTime() | 1U;
    rng = rng * 1664525U + 1013904223U;
    return n ? (rng >> 8) % n : 0;
}

/* Every slot gets a random borrowed move (never the character's own). */
static void randomize(BamLoadout* l)
{
    unsigned slot, i, n;
    unsigned ids[ROGUE_SPECIALS > ROGUE_AERIALS ? ROGUE_SPECIALS : ROGUE_AERIALS];
    rng ^= (unsigned) OSGetTime();
    for (slot = 0; slot < BAM_SPECIAL_SLOTS; ++slot) {
        for (n = i = 0; i < ROGUE_SPECIALS; ++i)
            if (special_ok(&rogue_specials[i], slot)) ids[n++] = rogue_specials[i].id;
        l->specials[slot] = (unsigned char) (n ? ids[rand_below(n)] : 0);
    }
    for (slot = 0; slot < BAM_AERIAL_SLOTS; ++slot) {
        for (n = i = 0; i < ROGUE_AERIALS; ++i)
            if (aerial_ok(&rogue_aerials[i], slot)) ids[n++] = rogue_aerials[i].id;
        l->aerials[slot] = (unsigned char) (n ? ids[rand_below(n)] : 0);
    }
    for (slot = 0; slot < BAM_NORMAL_SLOTS; ++slot) {
        for (n = 0, i = 1; i <= 26; ++i)
            if (normal_ok(i)) ids[n++] = i;
        l->normals[slot] = (unsigned char) (n ? ids[rand_below(n)] : 0);
    }
    loadout_fix(l);
}

static void set_row_value(BamLoadout* l, unsigned r, int dir)
{
    if (r < FIRST_AERIAL)
        l->specials[r] = (unsigned char) cycle_special(r, l->specials[r], dir);
    else if (r < FIRST_NORMAL)
        l->aerials[r - FIRST_AERIAL] = (unsigned char) cycle_aerial(r - FIRST_AERIAL, l->aerials[r - FIRST_AERIAL], dir);
    else if (r < MOVE_ROWS)
        l->normals[r - FIRST_NORMAL] = (unsigned char) cycle_normal(l->normals[r - FIRST_NORMAL], dir);
}

static void clear_row(BamLoadout* l, unsigned r)
{
    if (r < FIRST_AERIAL) l->specials[r] = 0;
    else if (r < FIRST_NORMAL) l->aerials[r - FIRST_AERIAL] = 0;
    else if (r < MOVE_ROWS) l->normals[r - FIRST_NORMAL] = 0;
}

static void say(const char* msg, unsigned rgb)
{
    BAM_LOG("css: %s\n", msg);
    toast = msg;
    toast_rgb = rgb;
    toast_frames = 150;
}

static const char* const slot_saved_msg[BAM_SAVE_SLOTS] = { "Saved to slot 1", "Saved to slot 2", "Saved to slot 3" };
static const char* const slot_loaded_msg[BAM_SAVE_SLOTS] = { "Loaded slot 1", "Loaded slot 2", "Loaded slot 3" };

/* After the slots changed: write them to the card, if there is one. */
static void store(const char* done)
{
    switch (Bam_StoreSave()) {
    case BAM_STORE_NOMEM: say("Card busy: kept until you quit", WARN); break;
    case BAM_STORE_ERROR: say("Memory card error: kept until you quit", WARN); break;
    default: say(done, GOLD); break;
    }
}

/* ---- share codes on Melee's keyboard --------------------------------------
 * A on the Code row asks the CSS to open its name-entry keyboard, as it does
 * for a new name tag (mnCharSel_804D6CF6 = 4, the port in 804D6CF9). The
 * keyboard runs as usual (its keys, cursor and sounds); only its input
 * routine is swapped for kb_input (BAM_NameEntryProc), which collects the 22
 * letters of a code instead of a 4-letter name and leaves without creating a
 * name tag. Slippi's keyboard codes only act in its connect-code mode, which
 * stays off here. */
#define CSS_SUBSCREEN (*(volatile u8*) 0x804D6CF6)
#define CSS_SUBSCREEN_PORT (*(volatile s8*) 0x804D6CF9)
extern HSD_GObj* mnNameNew_804D6C08;
extern void lbAudioAx_80024030(int);
#define SFX_BACK 0
#define SFX_OK 1
#define SFX_MOVE 2
#define SFX_ERROR 3

static void kb_open(void)
{
    kb_port = open_port;
    close_panel();
    kb_len = 0;
    kb_loaded = 0;
    kb_msg = NULL;
    kb_frames = 0;
    kb_text.native = kb_name.native = NULL;
    kb_state = KB_OPENING;
    CSS_SUBSCREEN_PORT = (s8) kb_port;
    CSS_SUBSCREEN = 4;
    BAM_LOG("css: code keyboard for port %d\n", kb_port);
}

/* inject: mnNameNew_EnterFromMnCharSel, after the keyboard's input proc is
 * set up (r3 = the proc). */
static void kb_input(HSD_GObj* gobj);
void BAM_NameEntryProc(HSD_GObjProc* proc)
{
    if (kb_state != KB_OPENING || !proc) return;
    proc->on_invoke = kb_input;
    kb_state = KB_OPEN;
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
        if (!mem || !HSD_SisLib_804D1124[0]) return;
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
    kb_loaded = loaded;
    kb_state = KB_BACK;
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
    bam_loadouts[kb_port] = got;
    lbAudioAx_80024030(SFX_OK);
    BAM_LOG("css: code typed for port %d\n", kb_port);
    kb_close(1);
}

static void kb_input(HSD_GObj* gobj)
{
    NameNewEntry* data = mnNameNew_804D6C08->user_data;
    u16* hov = &mn_804A04F0.hovered_selection;
    u32 b;
    (void) gobj;
    b = mn_804A04F0.buttons = mn_80229624((u32) kb_port);
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
            if (kb_state != KB_OPEN) return;
        } else if (sel == 0x32) {
            lbAudioAx_80024030(SFX_BACK);
            kb_close(0);
            return;
        } else {
            lbAudioAx_80024030(SFX_ERROR);
        }
    } else if (b & MenuInput_StartButton) {
        kb_confirm();
        if (kb_state != KB_OPEN) return;
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
static void slot_input(BamLoadout* l, unsigned slot, u32 t)
{
    BamSavedSlot* sv = &bam_saved[slot];
    BamLoadout saved;
    int save = (t & HSD_PAD_X) || ((t & HSD_PAD_A) && !sv->used);
    if (save) {
        loadout_fix(l);
        if (!custom_count(l, 0, MOVE_ROWS)) { say("No borrowed moves to save", WARN); return; }
        Bam_CodeFromLoadout(l, sv->code);
        sv->used = 1;
        store(slot_saved_msg[slot]);
    } else if (t & HSD_PAD_A) {
        if (!Bam_LoadoutFromCode(sv->code, &saved)) { say("This slot is damaged", WARN); return; }
        *l = saved;
        loadout_fix(l);
        say(slot_loaded_msg[slot], GOLD);
    } else if ((t & HSD_PAD_Y) && sv->used) {
        sv->used = 0;
        store("Slot deleted");
    }
}

static void panel_input(HSD_PadStatus* pad)
{
    BamLoadout* l = &bam_loadouts[open_port];
    int dir = read_dir(open_port, pad);
    u32 t = pad->trigger;
    /* L/R: turn the page. */
    if (t & (HSD_PAD_L | HSD_PAD_R)) {
        page = (page + ((t & HSD_PAD_R) ? 1 : PAGES - 1)) % PAGES;
        row = page_first(page);
        dir = 0;
    }
    /* The two buttons share the bottom line: Left/Right moves between them,
     * Up goes back to the page's last row, Down to the next page. */
    if (row >= ROW_RANDOM) {
        if (dir == DIR_LEFT) row = ROW_RANDOM;
        else if (dir == DIR_RIGHT) row = ROW_LOCK;
        else if (dir == DIR_UP) row = page_last(page);
        else if (dir == DIR_DOWN) {
            page = (page + 1) % PAGES;
            row = page_first(page);
        }
        dir = 0;
    } else {
        if (dir == DIR_UP) row = row == page_first(page) ? ROW_LOCK : row - 1;
        if (dir == DIR_DOWN) row = row == page_last(page) ? ROW_RANDOM : row + 1;
    }
    if (row < ROW_RANDOM) page = page_of(row);
    if (row >= ROW_SLOT0 && row < ROW_CODE) {
        slot_input(l, (unsigned) (row - ROW_SLOT0), t);
        t &= ~(HSD_PAD_A | HSD_PAD_X | HSD_PAD_Y);
    } else if (row == ROW_CODE) {
        if (t & HSD_PAD_A) {
            kb_open();
            return;
        }
        t &= ~(HSD_PAD_X | HSD_PAD_Y);
    }
    if (t & HSD_PAD_X) randomize(l);
    if ((dir == DIR_LEFT || dir == DIR_RIGHT) && row < MOVE_ROWS) {
        set_row_value(l, (unsigned) row, dir == DIR_LEFT ? -1 : 1);
        loadout_fix(l);
    }
    if (t & HSD_PAD_Y) {
        if (row < MOVE_ROWS) clear_row(l, (unsigned) row);
        else memset(l, 0, sizeof(*l)); /* on the buttons: reset everything */
        loadout_fix(l);
    }
    if ((t & (HSD_PAD_START | HSD_PAD_B | HSD_PAD_Z)) || ((t & HSD_PAD_A) && row == ROW_LOCK)) {
        close_panel();
        return;
    }
    if ((t & HSD_PAD_A) && row == ROW_RANDOM) randomize(l);
    else if (t & HSD_PAD_A) row = row == page_last(page) ? ROW_LOCK : row + 1; /* A: next row */
}

/* Preload the borrowed characters' files while the players are still on the
 * CSS. The CSS fills preload slots 0-3 with the players' characters each
 * frame and Melee's preloader streams them into the preload caches (main RAM
 * and ARAM) in the background; slots 4-7 are unused in VS. Putting donors
 * there means the match finds their data, model and effects already cached
 * instead of loading them into the match heap (which only fits a few).
 * Most-used donors first; Kirby is left out (preloading him pulls in every
 * copy-ability hat). Only the local loadouts are known here; the online
 * opponent's donors still load into the match heap. */
/* Preload "weight": fighters loaded per character (Zelda brings Sheik, Ice
 * Climbers bring Nana; Kirby brings every copy-ability hat). Melee sizes its
 * preload caches for 4 fighters' files plus a stage; going over makes the
 * preloader assert ("memp_kouho"), so players + donors stay within 4. */
#define PRELOAD_BUDGET 4
static int preload_weight(int ck)
{
    if (ck == CKind_Kirby) return PRELOAD_BUDGET + 1;
    if (ck == CKind_Zelda || ck == CKind_Seak || ck == CKind_PopoNana) return 2;
    return 1;
}

static void preload_donors(void)
{
    struct GameCache* cache = &lbDvd_GetPreloadCacheScene()->game_cache;
    u8 uses[26];
    int p, i, k, budget = PRELOAD_BUDGET;
    int pick[4] = { ChKind_None, ChKind_None, ChKind_None, ChKind_None };
    /* Only while nobody is editing: every change restarts the preloader. */
    if (open_port >= 0) return;
    memset(uses, 0, sizeof(uses));
    for (p = 0; p < 4; ++p) {
        PlayerInitData* pl = &mnCharSel_804D6CB0->vs.start.players[p];
        const BamLoadout* l = &bam_loadouts[p];
        if (pl->slot_type > 1 || (u8) pl->ckind >= 26) continue;
        budget -= preload_weight((u8) pl->ckind);
        if (pl->slot_type == 1 || !l->enabled) continue;
        for (i = 0; i < BAM_SPECIAL_SLOTS; ++i) {
            const RogueSpecialDef* d = RogueSpecial_Find(l->specials[i]);
            if (l->specials[i] && d && d->character < 26) ++uses[d->character];
        }
        for (i = 0; i < BAM_AERIAL_SLOTS; ++i) {
            const RogueAerialDef* d = RogueAerial_Find(l->aerials[i]);
            if (l->aerials[i] && d && d->character < 26) ++uses[d->character];
        }
        for (i = 0; i < BAM_NORMAL_SLOTS; ++i)
            if (l->normals[i] >= 1 && l->normals[i] <= 26) ++uses[l->normals[i] - 1];
    }
    for (p = 0; p < 4; ++p) {
        PlayerInitData* pl = &mnCharSel_804D6CB0->vs.start.players[p];
        if ((u8) pl->ckind < 26) uses[(u8) pl->ckind] = 0; /* preloaded anyway */
    }
    for (i = 0; i < 4 && budget > 0; ++i) {
        int best = -1;
        for (k = 0; k < 26; ++k)
            if (uses[k] && preload_weight(k) <= budget && (best < 0 || uses[k] > uses[best])) best = k;
        if (best < 0) break;
        uses[best] = 0;
        budget -= preload_weight(best);
        pick[i] = best;
    }
    for (i = 0; i < 4; ++i) {
        if (cache->entries[4 + i].char_id != pick[i])
            BAM_LOG("css preload slot %d: ckind %d\n", 4 + i, pick[i]);
        cache->entries[4 + i].char_id = pick[i];
        cache->entries[4 + i].color = 0;
        cache->entries[4 + i].x5 = 0;
    }
}

/* inject: mnCharSel_Scene_OnFrame entry */
void BAM_CssFrame(void)
{
    int p;
#if defined(BAM_QA) && BAM_QA
    { extern int QA_CssAuto(void); if (QA_CssAuto()) return; }
#endif
    if (!mnCharSel_804D6CB0) return;
    /* Melee's keyboard is up for a share code: leave the CSS alone. */
    if (kb_state == KB_OPENING || kb_state == KB_OPEN) {
        if (kb_state == KB_OPENING && ++kb_frames > 60) {
            BAM_NOTE("css: the keyboard did not take the code entry\n");
            kb_state = KB_OFF;
        }
        return;
    }
    /* Back from the keyboard: the CSS rebuilt itself and our texts went with
     * it. Make them again and reopen the panel on the code. */
    if (kb_state == KB_BACK) {
        hint.native = shapes.native = body.native = NULL;
        nquads = 0;
        open_port = -1;
        ui_ready = 0;
        ui_tried = 0;
    }
    if (!ui_tried) { ui_create(); first_frame = 1; }
    if (kb_state == KB_BACK && ui_ready) {
        PlayerInitData* pl = &mnCharSel_804D6CB0->vs.start.players[kb_port];
        kb_state = KB_OFF;
        if (pl->slot_type != 1 && (u8) pl->ckind < 26) {
            open_panel(kb_port, pl->ckind);
            if (open_port >= 0) {
                page = PAGE_SAVED;
                row = ROW_CODE;
                if (kb_loaded) say("Code loaded", GOLD);
            }
        }
    }
    /* Slippi's direct-code entry resets all SIS texts while the CSS is still
     * running. Ours are gone then: forget them (never touch freed texts) and
     * stay hidden until the CSS is entered again. */
    if (ui_ready && !BamText_Alive(&hint)) {
        BAM_LOG("css: SIS was reset (code entry); panel hidden\n");
        hint.native = shapes.native = body.native = NULL;
        nquads = 0;
        open_port = -1;
        ui_ready = 0;
        return;
    }
    if (!ui_ready) return;
    for (p = 0; p < 4; ++p) {
        PlayerInitData* pl = &mnCharSel_804D6CB0->vs.start.players[p];
        HSD_PadStatus* pad = &HSD_PadCopyStatus[p];
        s8 k = pl->ckind;
        int valid = pl->slot_type != 1 && (u8) k < 26; /* not a CPU slot */
        if (k != last_ckind[p]) {
            BAM_LOG("css port=%d ckind %d -> %d slot_type=%d\n", p, last_ckind[p], k, pl->slot_type);
            last_ckind[p] = k;
            /* Entering the CSS with a character already chosen (back from a
             * match) does not pop the panel; picking one does. */
            if (first_frame) continue;
            /* The panel opens with Z only; a new character re-filters an
             * open panel's moves (its own moves are not offered). */
            if (p == open_port && valid) { ckind = k; loadout_fix(&bam_loadouts[p]); }
        }
        if (open_port == p) {
            if (!just_opened) panel_input(pad);
            swallow(pad);
        } else if (open_port < 0 && valid && (pad->trigger & HSD_PAD_Z)) {
            open_panel(p, k);
            swallow(pad);
        }
    }
    first_frame = 0;
    just_opened = 0;
    preload_donors();
    if (open_port >= 0) draw_panel();
    draw_hint();
}

/* inject: mnCharSel_Scene_OnExit entry */
void BAM_CssExit(void)
{
    BAM_LOG("css exit\n");
    if (open_port >= 0) close_panel();
    ui_destroy();
    ui_tried = 0;
    {
        int p;
        for (p = 0; p < 4; ++p) last_ckind[p] = -1;
    }
}
