/* Drawing the move-select panel (css_panel.h). */
#include "css_panel.h"
/* Menu code: smaller beats faster (the overlay has a fixed size). */
#pragma optimize_for_size on
#pragma auto_inline off

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

void ui_create(void)
{
    if (css.ui_ready || css.ui_tried) return;
    css.ui_tried = 1;
    BAM_LOG("css sis free %u bytes\n", sis_free());
    if (sis_free() < SIS_NEEDED) return;
    /* Use the CSS's own SIS font (slot 0, loaded by the CSS) instead of
     * loading one into a free slot: Slippi's direct-code entry loads its
     * own text data while the CSS is up, and a slot we held (and later
     * freed) froze the game there. */
    css.font = 0;
    if (!HSD_SisLib_804D1124[css.font]) { BAM_LOG("css: CSS font not loaded\n"); return; }
    /* The block lives in this CSS visit's scene heap: reused only when the
     * panel comes back from Melee's keyboard in the same visit. Any other
     * old pointer is stale (the match rebuilt the heap since). */
    if (!css.mem || css.kb_state != KB_BACK) css.mem = HSD_MemAlloc(sizeof(MenuMem));
    if (!css.mem) { BAM_NOTE("css: no memory for the panel\n"); return; }
    css.canvas = HSD_SisLib_803A611C(css.font, NULL, 9, 0x14, 0, 0xF, 0, 0x13);
    BamText_Create(&css.hint, css.font, css.canvas, hint_buf, sizeof(hint_buf));
    css.ui_ready = 1;
    Bam_StoreInit(); /* saved builds from the memory card, once per boot */
}

/* ---- shapes ----------------------------------------------------------
 * Every box, bar and outline of the panel is drawn by one SIS text's render
 * callback (the CSS's SIS pool has room for only a dozen texts): a list of
 * quads built each frame with the panel's text, in the same screen space. */
/* Quad kind: 0 box, 1 triangle pointing left, 2 triangle pointing right. */

static void shape(float x, float y, float w, float h, unsigned rgb, unsigned alpha, unsigned kind)
{
    PanelQuad* q;
    if (css.nquads >= MAX_QUADS || w <= 0 || h <= 0) return;
    q = &quads[css.nquads++];
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
    float z = css.shapes.native ? css.shapes.native->pos_z : 0.0f;
    (void) gobj;
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    for (i = 0; i < css.nquads; ++i) {
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

void panel_destroy(void)
{
    if (!css.body.native) return;
    BamText_Destroy(&css.body);
    BamText_Destroy(&css.shapes);
    css.nquads = 0;
}

int panel_create(void)
{
    if (css.body.native) return 1;
    if (!css.ui_ready || sis_free() < SIS_NEEDED) {
        BAM_LOG("css panel: no SIS memory (%u free)\n", css.ui_ready ? sis_free() : 0);
        return 0;
    }
    /* An empty box with no background of its own: only its callback draws. */
    css.nquads = 0;
    BamText_Box(&css.shapes, css.font, css.canvas, shapes_buf, PANEL_X, PANEL_Y, 1, 1, 0, 0);
    css.shapes.native->render_callback = draw_quads;
    BamText_Create(&css.body, css.font, css.canvas, body_buf, sizeof(body_buf));
    return 1;
}

void ui_destroy(void)
{
    if (css.ui_ready) {
        panel_destroy();
        BamText_Destroy(&css.hint);
        css.ui_ready = 0;
    }
    /* Also after Slippi's code entry hid the panel (ui_ready 0): the block
     * must not outlive this CSS visit's heap. */
    if (css.mem) HSD_Free(css.mem);
    css.mem = NULL;
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
unsigned custom_count(const BamLoadout* l, unsigned first, unsigned count)
{
    unsigned n = 0;
    while (count--) n += row_value(l, first++) != 0;
    return n;
}

int page_of(int r)
{
    int p;
    for (p = 0; p < npages(); ++p)
        if (r >= tabs[p].first && r < tabs[p].first + tabs[p].count) return p;
    return 0;
}
int page_first(int p) { return tabs[p].first; }
int page_last(int p) { return tabs[p].first + tabs[p].count - 1; }

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

void draw_hint(void)
{
    unsigned p, lines = 0;
    const char* notice;
    if (!css.hint.native) return;
    BamText_Begin(&css.hint);
    if (css.open_port < 0) {
        BamText_Line(&css.hint, 24, 452 - 16 * lines++, "BuildAMelee v%s", BAM_VERSION);
        BamText_Style(&css.hint, 0.6f, DIM);
        notice = notice_text();
        if (notice) {
            BamText_Line(&css.hint, 24, 452 - 16 * lines++, "%s", notice);
            BamText_Style(&css.hint, 0.7f, WARN);
            BamText_Line(&css.hint, 24, 452 - 16 * lines++, "%s", "Last online match: builds were off");
            BamText_Style(&css.hint, 0.7f, WARN);
        }
    }
    for (p = 0; p < 4 && css.open_port < 0; ++p) {
        const BamLoadout* l = &bam_loadouts[p];
        PlayerInitData* pl;
        unsigned n;
        if (!mnCharSel_804D6CB0) break;
        pl = &mnCharSel_804D6CB0->vs.start.players[p];
        if (pl->slot_type == 1 || (u8) pl->ckind >= 26) continue;
        n = l->enabled ? custom_count(l, 0, MOVE_ROWS) : 0;
        if (n)
            BamText_Line(&css.hint, 24, 452 - 16 * lines++, "P%u  Build A Fighter: %u borrowed moves   (Z to edit)", p + 1, n);
        else
            BamText_Line(&css.hint, 24, 452 - 16 * lines++, "P%u  Press Z to Build A Fighter", p + 1);
        BamText_Style(&css.hint, 0.8f, n ? GOLD : SOFT);
    }
    BamText_End(&css.hint);
}

/* The fighter's own special for a slot ("Rest"), from the catalog. */
static const BamSpecialDef* own_special(unsigned slot)
{
    unsigned i;
    for (i = 0; i < BAM_SPECIALS; ++i)
        if (bam_specials[i].slot == slot && (int) bam_specials[i].character == css.ckind)
            return &bam_specials[i];
    return NULL;
}

/* A special's name without its character ("Fox Illusion" -> "Illusion"),
 * which the row shows next to it. */
static const char* move_name(const BamSpecialDef* d)
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
    const BamLoadout* l = &bam_loadouts[css.open_port];
    unsigned id = row_value(l, i);
    *move = NULL;
    if (i < FIRST_AERIAL) {
        const BamSpecialDef* d = BamSpecial_Find(id);
        if (!id || !d) d = own_special(i);
        *move = move_name(d);
        return id && d ? (int) d->character : css.ckind;
    }
    if (i < FIRST_NORMAL) {
        const BamAerialDef* d = BamAerial_Find(id);
        return id && d ? (int) d->character : css.ckind;
    }
    return id >= 1 && id <= 26 ? (int) id - 1 : css.ckind;
}

/* ---- text metrics ----------------------------------------------------
 * Measured with the font's own tables, as SIS lays the line out: each glyph
 * advances 32 - (left + right - 2) font units (its kerning pair), a space
 * 16; BamText's font size is 0.6 screen pixels per unit. */
extern u8 HSD_SisLib_8040C680[0x240]; /* ASCII (from 0x20) -> glyph code */
extern u8 HSD_SisLib_8040CB00[0x240]; /* glyph -> kerning pair */

float text_w(const char* s, float scale)
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
    BamText_Line(&css.body, x, cy - 19.2f + 9.6f * scale, "%s", s);
    BamText_Style(&css.body, scale, rgb);
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
    const BamLoadout* l = &bam_loadouts[css.open_port];
    int selected = css.row == (int) i, borrowed = row_value(l, i) != 0;
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
    if (r < FIRST_AERIAL) { const BamSpecialDef* d = BamSpecial_Find(id); return d ? (int) d->character : -1; }
    if (r < FIRST_NORMAL) { const BamAerialDef* d = BamAerial_Find(id); return d ? (int) d->character : -1; }
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
    int selected = css.row == (int) i;
    float cy = y + ROW_H * 0.5f, x = CONT_X + 14, right = CONT_X + CONT_W - 12, vx = x + 70;
    char buf[48];
    rbox(CONT_X, y, CONT_W, ROW_H, selected ? CARD_ON : CARD, 255, 2);
    if (selected) quad(CONT_X, y + 6, 3, ROW_H - 12, GOLD, 255);
    if (i == ROW_CODE) {
        unsigned char code[BAM_CODE_LEN];
        put(x, cy, ROW_TEXT, selected ? WHITE : DIM, "Code");
        Bam_CodeFromLoadout(&bam_loadouts[css.open_port], code);
        Bam_CodeText(code, buf);
        put_fit(vx, cy, ROW_TEXT, right - vx - 60, BLUE, buf);
        if (selected && !css.in_match) pill(right, cy, "A ENTER", GOLD, INK);
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

/* Options tab (training): the option, its state, and one line of help
 * under the last row. */
static void draw_option_row(unsigned i, float y)
{
    unsigned o = i - ROW_OPT0;
    int selected = css.row == (int) i, on = BamTraining_OptionOn(o);
    float cy = y + ROW_H * 0.5f, x = CONT_X + 14, right = CONT_X + CONT_W - 12;
    rbox(CONT_X, y, CONT_W, ROW_H, selected ? CARD_ON : CARD, 255, 2);
    if (selected) quad(CONT_X, y + 6, 3, ROW_H - 12, GOLD, 255);
    put_fit(x, cy, ROW_TEXT, right - x - 60, selected ? WHITE : DIM, BamTraining_OptionName(o));
    if (on) pill(right, cy, "ON", GOLD, INK);
    else put_center(right - 40, 40, cy, 0.5f, 0x566078, "OFF");
    if (selected)
        put_fit(CONT_X + 2, ROW_Y + ROW_STEP * BAM_OPT_COUNT + 6, 0.5f, CONT_W - 4, SOFT, BamTraining_OptionHelp(o));
}

static void draw_tab(int t, float y, float h)
{
    const PanelTab* tb = &tabs[t];
    int on = css.page == t;
    unsigned n = t == PAGE_SAVED ? saved_count() : t == PAGE_OPTIONS ? 0 :
                 custom_count(&bam_loadouts[css.open_port], tb->first, tb->count);
    float cy = y + h * 0.5f;
    if (on) {
        rbox(SIDE_X, y, SIDE_W, h, CARD_ON, 255, 2);
        quad(SIDE_X, y + 7, 3, h - 14, GOLD, 255);
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
    static const char* const match_keys[] = { "L/R", "Tab", "Left/Right", "Change", "X", "Random", "Y", "Reset",
                                              "B", "Resume" };
    static const char* const option_keys[] = { "L/R", "Tab", "Up/Down", "Option", "A", "Toggle", "Left/Right",
                                               "Toggle", "B", "Resume" };
    const char* const* keys = css.page == PAGE_SAVED ? saved_keys : css.page == PAGE_OPTIONS ? option_keys :
                              css.in_match ? match_keys : move_keys;
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

void draw_panel(void)
{
    const BamLoadout* l = &bam_loadouts[css.open_port];
    const PanelTab* tb = &tabs[css.page];
    unsigned i, n = custom_count(l, tb->first, tb->count);
    float hy = PANEL_Y + HEAD_H * 0.5f, x;
    char buf[32];
    int t;
    css.nquads = 0;
    BamText_Begin(&css.body);

    /* Panel, with a thin border and a gold line along its top. */
    rbox(PANEL_X - 1, PANEL_Y - 1, PANEL_W + 2, PANEL_H + 2, 0x2C3448, 255, 3);
    rbox(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 0x0E121D, 245, 3);
    quad(PANEL_X + 3, PANEL_Y, PANEL_W - 6, 2, GOLD, 255);

    /* Header: title on the left, player and character on the right. */
    quad(PANEL_X + 18, hy - 9, 3, 18, GOLD, 255);
    put(PANEL_X + 30, hy, 0.82f, WHITE, "BUILD A FIGHTER");
    x = PANEL_X + PANEL_W - 18 - text_w(ckind_names[css.ckind], 0.66f);
    put(x, hy, 0.66f, SOFT, ckind_names[css.ckind]);
    buf[0] = 'P'; buf[1] = (char) ('1' + css.open_port); buf[2] = 0;
    pill(x - 8, hy, css.in_match ? "TRAINING" : buf, GOLD, INK);
    quad(PANEL_X + 12, PANEL_Y + HEAD_H, PANEL_W - 24, 1, LINE, 255);

    /* Tabs. */
    /* Seven tabs (training) fit the same height a little tighter. */
    for (t = 0; t < npages(); ++t)
        draw_tab(t, TAB_Y + (css.in_match ? 31.0f : TAB_STEP) * t, css.in_match ? 28.0f : TAB_H);
    quad(CONT_X - 8, TITLE_Y, 1, BTN_Y + BTN_H - TITLE_Y, LINE, 255);

    /* The tab's moves (or slots, or the code being typed). */
    put(CONT_X + 2, TITLE_Y + 12, 0.76f, WHITE, tb->title);
    if (css.toast_frames) --css.toast_frames;
    if (css.toast_frames) {
        put_right(CONT_X + CONT_W - 2, TITLE_Y + 12, 0.54f, css.toast_rgb, css.toast);
    } else if (css.page == PAGE_SAVED) {
        put_right(CONT_X + CONT_W - 2, TITLE_Y + 12, 0.54f, DIM,
                  bam_store_on_card ? "On memory card" : "Kept until you quit");
    } else if (css.page == PAGE_OPTIONS) {
        put_right(CONT_X + CONT_W - 2, TITLE_Y + 12, 0.54f, DIM, "Applied right away");
    } else if (css.in_match && memcmp(l, &css.match_before, sizeof(css.match_before))) {
        put_right(CONT_X + CONT_W - 2, TITLE_Y + 12, 0.54f, GOLD, "Restarts when you resume");
    } else {
        memcpy(buf, "0 of 0 borrowed", 16);
        buf[0] = (char) ('0' + n); buf[5] = (char) ('0' + tb->count);
        put_right(CONT_X + CONT_W - 2, TITLE_Y + 12, 0.54f, n ? GOLD : DIM, buf);
    }
    for (i = 0; i < tb->count; ++i) {
        if (css.page == PAGE_SAVED) draw_saved_row(tb->first + i, ROW_Y + ROW_STEP * i);
        else if (css.page == PAGE_OPTIONS) draw_option_row(tb->first + i, ROW_Y + ROW_STEP * i);
        else draw_row(tb->first + i, ROW_Y + ROW_STEP * i);
    }
    draw_button(CONT_X, (CONT_W - 10) * 0.5f, css.in_match ? "RESUME" : "RANDOMIZE", css.row == ROW_RANDOM, 0);
    draw_button(CONT_X + (CONT_W + 10) * 0.5f, (CONT_W - 10) * 0.5f, css.in_match ? "RESTART" : "LOCK IN",
                css.row == ROW_LOCK, 1);

    quad(PANEL_X + 12, FOOT_Y, PANEL_W - 24, 1, LINE, 255);
    draw_keys();
    BamText_End(&css.body);
}
