/* Per-match engine state block (match heap, rolled back by Slippi).
 * Included from special_internal.h after BamFighterState is defined. */
#ifndef BAM_MATCH_H
#define BAM_MATCH_H
typedef struct BamMatchState {
    unsigned generation;                 /* increments per scene with build fighters */
    unsigned fighter_count;
    BamFighterState fighters[BAM_FIGHTERS];
    /* Donor attribute copies are identical for every borrower, so one table
     * is shared. */
    BamDonorAttrs donor_attrs[Ft_Kind_Max];
} BamMatchState;
/* Per-file blocks (anim/pose.c, visual/weapons.c, rest_sleep.c). */
void Bam_AnimScaleMatchBegin(void);
void Bam_AnimScaleMatchEnd(void);
void Bam_SwordVisualMatchBegin(void);
void Bam_SwordVisualMatchEnd(void);
void Bam_RestSleepMatchBegin(void);
void Bam_RestSleepMatchEnd(void);
#endif
