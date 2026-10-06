#ifndef BAM_SPECIAL_INTERNAL_H
#define BAM_SPECIAL_INTERNAL_H
#include <engine/special_engine.h>
#include <engine/bam_fighter.h>
#include <melee/ef/efasync.h>
#include <melee/ef/eflib.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftFox/types.h>
#include <melee/ft/kinds/ftFox/ftfoxspecialn.h>
#include <melee/ft/kinds/ftCaptain/ftcaptainspecials.h>
#include <melee/ft/kinds/ftDonkey/ftdonkeyspecialhi.h>
#include <melee/ft/kinds/ftGameWatch/ftgamewatch.h>
#include <melee/ft/kinds/ftLink/ftlinkspecialn.h>
#include <melee/ft/kinds/ftLink/ftlinkspecials.h>
#include <melee/ft/kinds/ftPeach/ftpeachspecialhi.h>
#include <melee/ft/kinds/ftMario/ftmariospecials.h>
#include <melee/ft/kinds/ftSamus/inlines.h>
#include <melee/ft/kinds/ftMewtwo/ftmewtwospecialn.h>
#include <melee/ft/kinds/ftPeach/ftpeachspecialn.h>
#include <melee/ft/kinds/ftSeak/ftseakspecials.h>
#include <melee/ft/kinds/ftNess/ftnessattackhi4.h>
#include <melee/ft/kinds/ftNess/ftnessattacks4.h>
#include <melee/pl/player.h>
#include <melee/it/it_26B1.h>
#include <stdio.h>
#include <string.h>
#include <dolphin/os.h>

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
    const BamAbilityDefinition* active;
    const BamAerialDef* aerial;
    /* The borrowed ground attack in progress (normal_runtime.c). */
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
    bool loaded[BAM_ABILITY_COUNT];
    bool loaded_sources[Ft_Kind_Max];
    void* native_attrs;
    struct Fighter_WaitAnimData* native_anims;
    u8 (*native_anim_flags)[2];
    u32 native_anim_count;
    union Fighter_FighterVars native_vars;
    union Fighter_FighterVars source_vars[BAM_DONOR_KINDS];
    /* Fighter callbacks fp+0x2190..0x21F8 are contiguous (0x6C bytes). */
    u8 native_callbacks[0x6C];
    /* Donor bones with no recipient equivalent, resolved once per borrowed
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
     * its data and animations loaded (normal_runtime.c), 0 = own move. */
    unsigned char normals[BAM_NORMAL_SLOTS];
    /* Fighter.x2CC (Donkey Kong's cargo attributes) outside a borrowed
     * Donkey Kong forward throw. */
    void* native_cargo;
} BamFighterState;
/* Donor attribute copies are identical for every borrower, so one table is
 * shared (the per-borrower state above stays small). */
typedef union { double align; unsigned char bytes[0x424]; } BamDonorAttrs;
#include <engine/bam_match.h>
/* The state owned by fp, or an empty, never-owned state (fighter == NULL). */
BamFighterState* Bam_FighterCtx(const Fighter* fp);
/* Both borrowers capture exactly once after restoring the previous owner. */
void Bam_BorrowBegin(Fighter* fp, FighterKind source);
void Bam_AerialRelease(BamFighterState* S);
/* A donor's data, effects and articles for this fighter (special_preload.c);
 * 0 when memory ran out. */
int Bam_DonorEnsure(BamFighterState* S, int source);
/* The private animation table of a donor whose archive is not resident,
 * created on first use; NULL when it is resident or out of memory. */
Fighter_WaitAnimData* Bam_DonorAnimTable(BamFighterState* S, int source);
/* Reads the listed animations of a donor into one block and points its
 * private table at them. 0 when out of memory. */
int Bam_DonorReadAnims(BamFighterState* S, int source, const short* anims, unsigned count, const char* what);
MotionState* Bam_NormalMotionState(Fighter* fp, int motion);
void Bam_NormalPrepare(Fighter* fp);

#endif
