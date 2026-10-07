/* Names for the decomp fields BuildAMelee uses that the decomp still
 * numbers by offset (x24, x58C, ...). Each expands to the field itself, so it
 * reads and assigns like one; when the decomp names a field, point the macro
 * at the new name (or replace it) and nothing else changes. */
#ifndef BAM_FIELDS_H
#define BAM_FIELDS_H

/* ---- Fighter ---- */
/* The animation table the fighter's motions play from (ftData.xC of its own
 * kind; a borrowed move points it at the donor's), its per-motion flags
 * (ftData.x10) and how many entries it has. */
#define FT_ANIMS(fp) ((fp)->x24)
#define FT_ANIM_FLAGS(fp) ((fp)->x28)
#define FT_ANIM_COUNT(fp) ((fp)->x58C)
/* Donkey Kong's attributes, read by the cargo-carry states. */
#define FT_CARGO_ATTRS(fp) ((fp)->x2CC)
/* The fighter's hitboxes (four). */
#define FT_HITBOXES(fp) ((fp)->x914)
/* The overlay that tints the fighter (flashes, charge glow). */
#define FT_COLOR_OVERLAY(fp) ((fp)->x488)
/* Frames since L/R was pressed (L-cancel window). */
#define FT_SHIELD_PRESS_FRAMES(fp) ((fp)->x67F)

/* ---- ftData (a character's fighter data file) ---- */
#define FTDATA_COMMON_ATTRS(d) ((d)->x0)    /* ftCo_DatAttrs: walk, jump, weight, ... */
#define FTDATA_PARTS(d) ((d)->x8)           /* model parts, hand joint, visibility table */
#define FTDATA_ANIMS(d) ((d)->xC)           /* Fighter_WaitAnimData per motion */
#define FTDATA_ANIM_FLAGS(d) ((d)->x10)

/* ---- Fighter_WaitAnimData (one motion's animation in PlXxAJ.dat) ---- */
#define ANIM_NAME(a) ((a)->x0)              /* "PlyMars5K_Share_ACTION_SpecialN_figatree" */
#define ANIM_FILE_OFFSET(a) ((a)->x4)       /* where it starts in PlXxAJ.dat */
#define ANIM_SIZE(a) ((a)->x8)
#define ANIM_SCRIPT(a) ((a)->xC)            /* its move script (subactions) */
#define ANIM_LOADED(a) ((a)->x14)           /* where it was read to (0: not loaded) */

/* ---- ftCommonData (p_ftCommonData) ---- */
#define FTCOMMON_FASTFALL_STICK(c) ((c)->x88)    /* stick down needed to fast fall */
#define FTCOMMON_LCANCEL_FRAMES(c) ((c)->xE4)    /* L-cancel window */
#define FTCOMMON_SLEEP_TICK(c) ((c)->x63C)       /* sleep timer drop per frame (DamageSong) */

#endif
