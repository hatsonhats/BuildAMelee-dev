/* Shared by the move-select panel files (src/platform):
 *   css_menu.c   state, rows and moves, input, the CSS and training hooks
 *   css_draw.c   drawing: shapes, text, rows, tabs, buttons
 *   css_codes.c  typing a share code on Melee's keyboard */
#ifndef BAM_CSS_PANEL_H
#define BAM_CSS_PANEL_H
#include <bam/bam.h>
#include <engine/internal.h>
#include <engine/catalog.h>
#include "ui_text.h"
#include "build_store.h"
#include "training.h"
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
extern SIS* HSD_SisLib_804D1124[5];
/* Rows: specials, aerials, ground attacks and throws (BamNormalSlot
 * order), the saved-build slots and the share code, then the two buttons. */
#define FIRST_AERIAL BAM_SPECIAL_SLOTS
#define FIRST_NORMAL (BAM_SPECIAL_SLOTS + BAM_AERIAL_SLOTS)
#define MOVE_ROWS (FIRST_NORMAL + BAM_NORMAL_SLOTS)
#define ROW_SLOT0 MOVE_ROWS
#define ROW_CODE (ROW_SLOT0 + BAM_SAVE_SLOTS)
#define ROW_OPT0 (ROW_CODE + 1) /* training match only */
#define ROW_RANDOM (ROW_OPT0 + BAM_OPT_COUNT)
#define ROW_LOCK (ROW_RANDOM + 1)
#define ROWS (ROW_LOCK + 1)
/* Tabs down the left of the panel, one group of rows each. */
#define PAGES 7     /* in a training match; the CSS shows the first 6 */
#define CSS_PAGES 6
#define PAGE_SAVED 5
#define PAGE_OPTIONS 6
typedef struct PanelTab { const char* tab; const char* sub; const char* title; unsigned char first, count; } PanelTab;
/* The panel inside a training match (BamPanel_Match*): an Options tab, and
 * RESUME / RESTART for the two buttons. */
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

/* Share-code entry on Melee's own keyboard (css_codes.c). */
enum { KB_OFF, KB_OPENING, KB_OPEN, KB_BACK };
/* Port whose build was locked in last: the local player's build online. */
extern int bam_css_port; /* css_menu.c */
/* SIS text streams (see ui_text.c) and the panel's shapes live in one
 * scene-heap block taken when the CSS opens (static buffers would sit in the
 * overlay, which has no room to spare). */
typedef struct PanelQuad { s16 x, y, w, h; u8 kind, pad[3]; u32 rgba; } PanelQuad;
#define MAX_QUADS 200
typedef struct MenuMem {
    u32 hint_stream[1024 / 4], body_stream[4096 / 4], shapes_stream[128 / 4];
    PanelQuad quads[MAX_QUADS];
} MenuMem;
#define hint_buf (css.mem->hint_stream)
#define body_buf (css.mem->body_stream)
#define shapes_buf (css.mem->shapes_stream)
#define quads (css.mem->quads)
/* css_menu.c */
extern const PanelTab tabs[PAGES];
extern const char* const ckind_names[26];
extern const char* const ckind_short[26];
extern const char* const row_labels[MOVE_ROWS];

/* The panel's state (one panel open at a time). */
typedef struct CssPanelState {
    int font;       /* SIS font slot: the CSS uses 0; Slippi's online CSS may take others */
    int open_port;  /* controller the panel is open for, -1 none */
    s8 last_ckind[4];
    int in_match;   /* the panel inside a training match (BamPanel_Match*) */
    BamLoadout match_before;
    int ui_ready, ui_tried, canvas, first_frame;
    int row, page, ckind, just_opened;
    /* Share-code entry on Melee's keyboard (css_codes.c). */
    int kb_state, kb_port, kb_frames, kb_loaded;
    /* A short message in the panel's title line ("Saved to slot 2"). */
    const char* toast;
    unsigned toast_rgb, toast_frames;
    unsigned stick_hold[4];
    int stick_dir[4];
    BamText hint, shapes, body;
    MenuMem* mem;
    unsigned nquads;
} CssPanelState;
extern CssPanelState css;
static inline int npages(void) { return css.in_match ? PAGES : CSS_PAGES; }

void close_panel(void);
unsigned custom_count(const BamLoadout* l, unsigned first, unsigned count);
void draw_hint(void);
void draw_panel(void);
void kb_open(void);
int page_first(int p);
int page_last(int p);
int page_of(int r);
int panel_create(void);
void panel_destroy(void);
float text_w(const char* s, float scale);
void ui_create(void);
void ui_destroy(void);
#endif
