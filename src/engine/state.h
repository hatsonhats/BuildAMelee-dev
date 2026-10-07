/* The engine's state during a match. All of it is on the match heap,
 * allocated at match start (Bam_MatchBegin), so Slippi rollback restores it:
 * one BamFighterState per build fighter, in one BamMatchState (bam_match). */
#ifndef BAM_STATE_H
#define BAM_STATE_H
#include <engine/engine.h>

#define BAM_SPECIAL_BLOBS 32
/* Host-owned lifetime, never appended to a disc-layout Fighter or shared data. */
/* Kinds that can lend moves: every playable character (Nana's moves come
 * from Popo). */
#define BAM_DONOR_KINDS (Ft_Kind_Emblem + 1)
typedef struct BamFighterState {
    /* Read for every fighter (Bam_FighterCtx returns a shared empty state
     * for fighters without one, which holds only these first fields: every
     * other field is read only after S->fighter == fp). */
    Fighter* fighter;
    unsigned match_generation;
    const BamDonorSpecial* active;
    const BamAerialDef* aerial;
    /* The borrowed ground attack in progress (normals.c). */
    bool normal_on;
    signed char normal_slot;
    unsigned char normal_donor;
    bool normal_fresh;       /* Bam_NormalBegin ran; its first motion is next */
    /* ---- owned fighters only below ---- */
    unsigned aerial_equipped[BAM_AERIAL_SLOTS];
    Fighter_WaitAnimData* aerial_anims[Ft_Kind_Max];
    void* aerial_blobs[BAM_AERIAL_SLOTS][2];
    /* Animation slices of the equipped specials' and ground attacks' donor
     * states (one block per move). */
    void* special_blobs[BAM_SPECIAL_BLOBS];
    unsigned special_blob_count;
    bool loaded[BAM_SPECIAL_ID_COUNT];
    bool loaded_sources[Ft_Kind_Max];
    void* native_attrs;
    struct Fighter_WaitAnimData* native_anims;
    u8 (*native_anim_flags)[2];
    u32 native_anim_count;
    union Fighter_FighterVars native_vars;
    union Fighter_FighterVars source_vars[BAM_DONOR_KINDS];
    /* Fighter callbacks fp+0x2190..0x21F8 are contiguous (0x6C bytes). */
    u8 native_callbacks[0x6C];
    /* Donor bones with no borrower equivalent, resolved once per borrowed
     * move so a hitbox or held item never jumps between hands mid-move. */
    unsigned fallback_count;
    short fallback_bone[8], fallback_joint[8];
    /* Nana borrows through Popo's loaded donors and aerial slices; she owns
     * none of them, so her state never frees them. */
    bool shares_partner;
    /* This fighter's kit (copied from its slot's loadout at creation). */
    unsigned char specials[BAM_SPECIAL_SLOTS];
    unsigned char aerials[BAM_AERIAL_SLOTS];
    /* Ground attacks and throws: the donor (FighterKind + 1) per slot once
     * its data and animations loaded (normals.c), 0 = own move. */
    unsigned char normals[BAM_NORMAL_SLOTS];
    /* Fighter.x2CC (Donkey Kong's cargo attributes) outside a borrowed
     * Donkey Kong forward throw. */
    void* native_cargo;
} BamFighterState;
/* Donor attribute copies are identical for every borrower, so one table is
 * shared (the per-borrower state above stays small). */
typedef union { double align; unsigned char bytes[0x424]; } BamDonorAttrs;
typedef struct BamMatchState {
    unsigned generation;                 /* increments per scene with build fighters */
    unsigned fighter_count;
    BamFighterState fighters[BAM_FIGHTERS];
    /* Donor attribute copies are identical for every borrower, so one table
     * is shared. */
    BamDonorAttrs donor_attrs[Ft_Kind_Max];
} BamMatchState;
/* The state owned by fp, or an empty, never-owned state (fighter == NULL). */
BamFighterState* Bam_FighterCtx(const Fighter* fp);
/* Rest's sleep block (rest_sleep.c). */
void Bam_RestSleepMatchBegin(void);
void Bam_RestSleepMatchEnd(void);
#endif
