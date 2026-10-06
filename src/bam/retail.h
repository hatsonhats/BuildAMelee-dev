/* Retail (and Slippi) addresses overlay code reads directly: state the
 * decomp keeps file-local or has not named, and Slippi's own helpers. One
 * place for every raw address, each with what it is. */
#ifndef BAM_RETAIL_H
#define BAM_RETAIL_H
#include <dolphin/types.h>

/* ---- the scene controller (gm, `state_machine` 0x80479D30) ---- */
#define BAM_SCENE_MAJOR (*(volatile u8*) 0x80479D30)
#define BAM_SCENE_MINOR (*(volatile u8*) 0x80479D33)
#define BAM_SCENE_VS 2
#define BAM_SCENE_ONLINE 8       /* Slippi Online */
#define BAM_SCENE_TRAINING 0x1C
/* gm_80479D58.x2: the scene loop's "run one frame" bit (frame advance), and
 * .x3: x2 as of the last frame. */
#define BAM_FRAME_STEP (*(volatile u8*) 0x80479D6A)
#define BAM_FRAME_STEP_SEEN (*(volatile u8*) 0x80479D6B)

/* ---- controllers ---- */
/* gm_EvaluateAllControllerInputs copies the pads into this per-port table
 * (controller_map) before the scene's frame runs; Slippi's online CSS
 * (quick chat on the D-pad) and menu code read it. */
struct BamGmPadState { u64 button, trigger, repeat, release, repeat2; s32 timer, x2C; };
#define BAM_GM_PADS ((struct BamGmPadState*) 0x80479C30)

/* ---- character select ---- */
/* mnCharSel's sub-screen (4: the name-entry keyboard) and its port. */
#define BAM_CSS_SUBSCREEN (*(volatile u8*) 0x804D6CF6)
#define BAM_CSS_SUBSCREEN_PORT (*(volatile s8*) 0x804D6CF9)

/* ---- fighters and effects ---- */
/* ftMapping_list: per character, internal kind, extra kind, ... */
#define BAM_FT_MAPPING ((const s8*) 0x803BCDE0)
/* efAsync_DatEntries[51]: effect files. */
typedef struct BamEfDatEntry { char* file; char* table; void* data; } BamEfDatEntry;
#define BAM_EF_ENTRIES ((BamEfDatEntry*) 0x803C025C)

/* ---- hardware ---- */
/* Bus clock (OS globals): the timer runs at a quarter of it. */
#define BAM_BUS_CLOCK (*(u32*) 0x800000F8)
#define BAM_TICKS_PER_MS (BAM_BUS_CLOCK / 4000)

/* ---- Slippi (slippi-ssbm-asm) ---- */
/* Common.s static helpers. */
#define BAM_SLIPPI_EXI_TRANSFER ((void (*)(void*, u32, u32)) 0x800055F0)
#define BAM_SLIPPI_LOAD_MATCH_STATE ((void* (*)(void*)) 0x80005610)
/* The selected online mode (r13 - 0x5060). */
#define BAM_SLIPPI_ONLINE_MODE (*(volatile u8*) 0x804D6640)
#endif
