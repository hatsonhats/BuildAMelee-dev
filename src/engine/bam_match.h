/* Per-match engine state block (match heap, rolled back by Slippi).
 * Included from special_internal.h after RogueFighterState is defined. */
#ifndef BAM_MATCH_H
#define BAM_MATCH_H
typedef struct BamMatchState {
    unsigned generation;                 /* increments per scene with build fighters */
    unsigned fighter_count;
    RogueFighterState fighters[BAM_FIGHTERS];
    /* Donor attribute copies are identical for every borrower, so one table
     * is shared. */
    RogueDonorAttrs donor_attrs[Ft_Kind_Max];
} BamMatchState;
/* Per-file blocks (anim/pose.c, visual/weapons.c, rest_sleep.c). */
void Rogue_AnimScaleMatchBegin(void);
void Rogue_AnimScaleMatchEnd(void);
void Rogue_SwordVisualMatchBegin(void);
void Rogue_SwordVisualMatchEnd(void);
void Rogue_RestSleepMatchBegin(void);
void Rogue_RestSleepMatchEnd(void);
#endif
