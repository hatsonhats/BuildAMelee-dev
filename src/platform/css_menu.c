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
#include "css_panel.h"
#include <bam/retail.h>

/* Port whose build was locked in last: the local player's build online. */
int bam_css_port = 0;
CssPanelState css = { 4, -1, { -1, -1, -1, -1 } };
const PanelTab tabs[PAGES] = {
    { "SPECIALS", 0, "Special Moves", 0, BAM_SPECIAL_SLOTS },
    { "AERIALS", 0, "Aerials", FIRST_AERIAL, BAM_AERIAL_SLOTS },
    { "GROUND", "JAB / DASH / TILTS", "Ground Attacks", FIRST_NORMAL + BAM_NORMAL_JAB, 5 },
    { "SMASH", "ATTACKS", "Smash Attacks", FIRST_NORMAL + BAM_NORMAL_FSMASH, 3 },
    { "THROWS", 0, "Throws", FIRST_NORMAL + BAM_NORMAL_FTHROW, 4 },
    { "SAVED", 0, "Saved Builds", ROW_SLOT0, BAM_SAVE_SLOTS + 1 },
    { "OPTIONS", 0, "Training Options", ROW_OPT0, BAM_OPT_COUNT },
};

const char* const ckind_names[26] = {
    "Captain Falcon", "Donkey Kong", "Fox", "Mr Game and Watch", "Kirby", "Bowser",
    "Link", "Luigi", "Mario", "Marth", "Mewtwo", "Ness", "Peach", "Pikachu",
    "Ice Climbers", "Jigglypuff", "Samus", "Yoshi", "Zelda", "Sheik", "Falco",
    "Young Link", "Dr Mario", "Roy", "Pichu", "Ganondorf"
};

const char* const ckind_short[26] = {
    "Falcon", "DK", "Fox", "G&W", "Kirby", "Bowser", "Link", "Luigi", "Mario", "Marth", "Mewtwo",
    "Ness", "Peach", "Pikachu", "ICs", "Puff", "Samus", "Yoshi", "Zelda", "Sheik", "Falco",
    "YLink", "Doc", "Roy", "Pichu", "Ganon"
};

const char* const row_labels[MOVE_ROWS] = {
    "Neutral B", "Side B", "Up B", "Down B",
    "Neutral Air", "Forward Air", "Back Air", "Up Air", "Down Air",
    "Jab", "Dash Attack", "Forward Tilt", "Up Tilt", "Down Tilt",
    "Forward Smash", "Up Smash", "Down Smash",
    "Forward Throw", "Back Throw", "Up Throw", "Down Throw"
};

static int same_family(int a, int b)
{
    if (a == b) return 1;
    return (a == CKind_Zelda || a == CKind_Seak) && (b == CKind_Zelda || b == CKind_Seak);
}

static int special_ok(const BamSpecialDef* d, unsigned slot)
{
    return d && d->slot == slot && BamSpecial_Offerable(d) && Bam_GetAbility(d->id) &&
           !same_family((int) d->character, css.ckind);
}

static int aerial_ok(const BamAerialDef* d, unsigned slot)
{
    return d && d->slot == slot && !same_family((int) d->character, css.ckind);
}

/* Step through native (0) and every move that fits the slot. */
static unsigned cycle_special(unsigned slot, unsigned cur, int dir)
{
    unsigned ids[BAM_SPECIALS + 1], n = 0, at = 0, i;
    ids[n++] = 0;
    for (i = 0; i < BAM_SPECIALS; ++i)
        if (special_ok(&bam_specials[i], slot)) {
            if (bam_specials[i].id == cur) at = n;
            ids[n++] = bam_specials[i].id;
        }
    return ids[(at + n + dir) % n];
}

static unsigned cycle_aerial(unsigned slot, unsigned cur, int dir)
{
    unsigned ids[BAM_AERIALS + 1], n = 0, at = 0, i;
    ids[n++] = 0;
    for (i = 0; i < BAM_AERIALS; ++i)
        if (aerial_ok(&bam_aerials[i], slot)) {
            if (bam_aerials[i].id == cur) at = n;
            ids[n++] = bam_aerials[i].id;
        }
    return ids[(at + n + dir) % n];
}

/* Ground attacks and throws: any other character (CharacterKind + 1). */
static int normal_ok(unsigned v)
{
    return v >= 1 && v <= 26 && !same_family((int) v - 1, css.ckind);
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
        const BamSpecialDef* d = BamSpecial_Find(l->specials[i]);
        if (l->specials[i] && !special_ok(d, i)) l->specials[i] = 0;
        any |= l->specials[i];
    }
    for (i = 0; i < BAM_AERIAL_SLOTS; ++i) {
        const BamAerialDef* d = BamAerial_Find(l->aerials[i]);
        if (l->aerials[i] && !aerial_ok(d, i)) l->aerials[i] = 0;
        any |= l->aerials[i];
    }
    l->enabled = any != 0;
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
    if (dir != css.stick_dir[port]) {
        css.stick_dir[port] = dir;
        css.stick_hold[port] = 0;
        return dir;
    }
    if (!dir) return 0;
    ++css.stick_hold[port];
    return css.stick_hold[port] >= 14 && (css.stick_hold[port] - 14) % 5 == 0 ? dir : 0;
}


static void swallow(HSD_PadStatus* pad)
{
    int port = (int) (pad - HSD_PadCopyStatus);
    if (port >= 0 && port < 4) {
        struct BamGmPadState* g = &BAM_GM_PADS[port];
        g->button = g->trigger = g->repeat = g->release = g->repeat2 = 0;
        g = &BAM_GM_PADS[4]; /* "any controller" */
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
    css.open_port = port;
    css.ckind = kind;
    css.row = 0;
    css.page = 0;
    css.toast_frames = 0;
    loadout_fix(&bam_loadouts[port]);
    if (!panel_create()) { css.open_port = -1; return; }
    css.just_opened = 1; /* ignore the input of the frame that opened it */
    BAM_LOG("css panel open port=%d ckind=%d\n", port, kind);
}

void close_panel(void)
{
    BamLoadout* l = &bam_loadouts[css.open_port];
    loadout_fix(l);
    BAM_LOG("css lock in port=%d enabled=%d specials=%u,%u,%u,%u aerials=%u,%u,%u,%u,%u\n",
            css.open_port, l->enabled, l->specials[0], l->specials[1], l->specials[2], l->specials[3],
            l->aerials[0], l->aerials[1], l->aerials[2], l->aerials[3], l->aerials[4]);
    BAM_LOG("css lock in port=%d normals=%u,%u,%u,%u,%u %u,%u,%u %u,%u,%u,%u\n", css.open_port,
            l->normals[0], l->normals[1], l->normals[2], l->normals[3], l->normals[4], l->normals[5],
            l->normals[6], l->normals[7], l->normals[8], l->normals[9], l->normals[10], l->normals[11]);
    bam_css_port = css.open_port;
    css.open_port = -1;
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
    unsigned ids[BAM_SPECIALS > BAM_AERIALS ? BAM_SPECIALS : BAM_AERIALS];
    rng ^= (unsigned) OSGetTime();
    for (slot = 0; slot < BAM_SPECIAL_SLOTS; ++slot) {
        for (n = i = 0; i < BAM_SPECIALS; ++i)
            if (special_ok(&bam_specials[i], slot)) ids[n++] = bam_specials[i].id;
        l->specials[slot] = (unsigned char) (n ? ids[rand_below(n)] : 0);
    }
    for (slot = 0; slot < BAM_AERIAL_SLOTS; ++slot) {
        for (n = i = 0; i < BAM_AERIALS; ++i)
            if (aerial_ok(&bam_aerials[i], slot)) ids[n++] = bam_aerials[i].id;
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
    css.toast = msg;
    css.toast_rgb = rgb;
    css.toast_frames = 150;
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

/* 0: the panel stays open; 1: close (lock in / resume); 2: restart the
 * training match (in a match only). */
static int panel_input(HSD_PadStatus* pad)
{
    BamLoadout* l = &bam_loadouts[css.open_port];
    int dir = read_dir(css.open_port, pad);
    u32 t = pad->trigger;
    /* L/R: turn the page. */
    if (t & (HSD_PAD_L | HSD_PAD_R)) {
        css.page = (css.page + ((t & HSD_PAD_R) ? 1 : npages() - 1)) % npages();
        css.row = page_first(css.page);
        dir = 0;
    }
    /* The two buttons share the bottom line: Left/Right moves between them,
     * Up goes back to the page's last row, Down to the next page. */
    if (css.row >= ROW_RANDOM) {
        if (dir == DIR_LEFT) css.row = ROW_RANDOM;
        else if (dir == DIR_RIGHT) css.row = ROW_LOCK;
        else if (dir == DIR_UP) css.row = page_last(css.page);
        else if (dir == DIR_DOWN) {
            css.page = (css.page + 1) % npages();
            css.row = page_first(css.page);
        }
        dir = 0;
    } else {
        if (dir == DIR_UP) css.row = css.row == page_first(css.page) ? ROW_LOCK : css.row - 1;
        if (dir == DIR_DOWN) css.row = css.row == page_last(css.page) ? ROW_RANDOM : css.row + 1;
    }
    if (css.row < ROW_RANDOM) css.page = page_of(css.row);
    if (css.row >= ROW_SLOT0 && css.row < ROW_CODE) {
        slot_input(l, (unsigned) (css.row - ROW_SLOT0), t);
        t &= ~(HSD_PAD_A | HSD_PAD_X | HSD_PAD_Y);
    } else if (css.row == ROW_CODE) {
        if (t & HSD_PAD_A) {
            if (css.in_match) { say("Type codes on the character screen", WARN); return 0; }
            kb_open();
            return 0;
        }
        t &= ~(HSD_PAD_X | HSD_PAD_Y);
    } else if (css.row >= ROW_OPT0 && css.row < ROW_RANDOM) {
        if ((t & HSD_PAD_A) || dir == DIR_LEFT || dir == DIR_RIGHT)
            BamTraining_OptionToggle((unsigned) (css.row - ROW_OPT0));
        t &= ~(HSD_PAD_A | HSD_PAD_X | HSD_PAD_Y);
    }
    if (t & HSD_PAD_X) randomize(l);
    if ((dir == DIR_LEFT || dir == DIR_RIGHT) && css.row < MOVE_ROWS) {
        set_row_value(l, (unsigned) css.row, dir == DIR_LEFT ? -1 : 1);
        loadout_fix(l);
    }
    if (t & HSD_PAD_Y) {
        if (css.row < MOVE_ROWS) clear_row(l, (unsigned) css.row);
        else if (css.row >= ROW_RANDOM) memset(l, 0, sizeof(*l)); /* on the buttons: reset everything */
        loadout_fix(l);
    }
    if (css.in_match) {
        /* B / Start / RESUME close; a changed build restarts the match. */
        if ((t & HSD_PAD_A) && css.row == ROW_LOCK) return 2;
        if ((t & (HSD_PAD_START | HSD_PAD_B)) || ((t & HSD_PAD_A) && css.row == ROW_RANDOM)) return 1;
        if (t & HSD_PAD_A) css.row = css.row == page_last(css.page) ? ROW_RANDOM : css.row + 1;
        return 0;
    }
    if ((t & (HSD_PAD_START | HSD_PAD_B | HSD_PAD_Z)) || ((t & HSD_PAD_A) && css.row == ROW_LOCK)) return 1;
    if ((t & HSD_PAD_A) && css.row == ROW_RANDOM) randomize(l);
    else if (t & HSD_PAD_A) css.row = css.row == page_last(css.page) ? ROW_LOCK : css.row + 1; /* A: next row */
    return 0;
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
    if (css.open_port >= 0) return;
    memset(uses, 0, sizeof(uses));
    for (p = 0; p < 4; ++p) {
        PlayerInitData* pl = &mnCharSel_804D6CB0->vs.start.players[p];
        const BamLoadout* l = &bam_loadouts[p];
        if (pl->slot_type > 1 || (u8) pl->ckind >= 26) continue;
        budget -= preload_weight((u8) pl->ckind);
        if (pl->slot_type == 1 || !l->enabled) continue;
        for (i = 0; i < BAM_SPECIAL_SLOTS; ++i) {
            const BamSpecialDef* d = BamSpecial_Find(l->specials[i]);
            if (l->specials[i] && d && d->character < 26) ++uses[d->character];
        }
        for (i = 0; i < BAM_AERIAL_SLOTS; ++i) {
            const BamAerialDef* d = BamAerial_Find(l->aerials[i]);
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
    if (css.kb_state == KB_OPENING || css.kb_state == KB_OPEN) {
        if (css.kb_state == KB_OPENING && ++css.kb_frames > 60) {
            BAM_NOTE("css: the keyboard did not take the code entry\n");
            css.kb_state = KB_OFF;
        }
        return;
    }
    /* Back from the keyboard: the CSS rebuilt itself and our texts went with
     * it. Make them again and reopen the panel on the code. */
    if (css.kb_state == KB_BACK) {
        css.hint.native = css.shapes.native = css.body.native = NULL;
        css.nquads = 0;
        css.open_port = -1;
        css.ui_ready = 0;
        css.ui_tried = 0;
    }
    if (!css.ui_tried) { ui_create(); css.first_frame = 1; }
    if (css.kb_state == KB_BACK && css.ui_ready) {
        PlayerInitData* pl = &mnCharSel_804D6CB0->vs.start.players[css.kb_port];
        css.kb_state = KB_OFF;
        if (pl->slot_type != 1 && (u8) pl->ckind < 26) {
            open_panel(css.kb_port, pl->ckind);
            if (css.open_port >= 0) {
                css.page = PAGE_SAVED;
                css.row = ROW_CODE;
                if (css.kb_loaded) say("Code loaded", GOLD);
            }
        }
    }
    /* Slippi's direct-code entry resets all SIS texts while the CSS is still
     * running. Ours are gone then: forget them (never touch freed texts) and
     * stay hidden until the CSS is entered again. */
    if (css.ui_ready && !BamText_Alive(&css.hint)) {
        BAM_LOG("css: SIS was reset (code entry); panel hidden\n");
        css.hint.native = css.shapes.native = css.body.native = NULL;
        css.nquads = 0;
        css.open_port = -1;
        css.ui_ready = 0;
        return;
    }
    if (!css.ui_ready) return;
    for (p = 0; p < 4; ++p) {
        PlayerInitData* pl = &mnCharSel_804D6CB0->vs.start.players[p];
        HSD_PadStatus* pad = &HSD_PadCopyStatus[p];
        s8 k = pl->ckind;
        int valid = pl->slot_type != 1 && (u8) k < 26; /* not a CPU slot */
        if (k != css.last_ckind[p]) {
            BAM_LOG("css port=%d ckind %d -> %d slot_type=%d\n", p, css.last_ckind[p], k, pl->slot_type);
            css.last_ckind[p] = k;
            /* Entering the CSS with a character already chosen (back from a
             * match) does not pop the panel; picking one does. */
            if (css.first_frame) continue;
            /* The panel opens with Z only; a new character re-filters an
             * open panel's moves (its own moves are not offered). */
            if (p == css.open_port && valid) { css.ckind = k; loadout_fix(&bam_loadouts[p]); }
        }
        if (css.open_port == p) {
            if (!css.just_opened && panel_input(pad)) close_panel();
            swallow(pad);
        } else if (css.open_port < 0 && valid && (pad->trigger & HSD_PAD_Z)) {
            open_panel(p, k);
            swallow(pad);
        }
    }
    css.first_frame = 0;
    css.just_opened = 0;
    preload_donors();
    if (css.open_port >= 0) draw_panel();
    draw_hint();
}

/* inject: mnCharSel_Scene_OnExit entry */
void BAM_CssExit(void)
{
    BAM_LOG("css exit\n");
    if (css.open_port >= 0) close_panel();
    ui_destroy();
    css.ui_tried = 0;
    {
        int p;
        for (p = 0; p < 4; ++p) css.last_ckind[p] = -1;
    }
}

/* ---- the panel in a training match (training.c) ----------------------------
 * The same panel on the training match's own SIS canvas, for the player's
 * build (loadout 0 in the match). The match is frozen while it is open. */
int BamPanel_MatchOpen(int font, int canv, int kind)
{
    if (css.open_port >= 0) return 1;
    if ((unsigned) kind >= 26) return 0;
    css.in_match = 1;
    css.font = font;
    css.canvas = canv;
    if (!css.mem) css.mem = HSD_MemAlloc(sizeof(MenuMem));
    if (!css.mem) { css.in_match = 0; BAM_NOTE("training: no memory for the build menu\n"); return 0; }
    css.ui_ready = 1;
    open_panel(0, kind);
    if (css.open_port < 0) { BamPanel_MatchClose(); return 0; }
    /* The build the match runs with: a different one on resume restarts. */
    css.match_before = bam_loadouts[0];
    return 1;
}

int BamPanel_MatchFrame(HSD_PadStatus* pad)
{
    int act = 0;
    if (css.open_port < 0) return 1;
    if (css.just_opened) css.just_opened = 0;
    else act = panel_input(pad);
    if (act == 1 && memcmp(&bam_loadouts[0], &css.match_before, sizeof(css.match_before))) act = 2;
    if (!act) draw_panel();
    return act;
}

void BamPanel_MatchClose(void)
{
    if (css.open_port >= 0) {
        loadout_fix(&bam_loadouts[css.open_port]);
        css.open_port = -1;
    }
    panel_destroy();
    if (css.mem) HSD_Free(css.mem);
    css.mem = NULL;
    css.ui_ready = 0;
    css.in_match = 0;
}
