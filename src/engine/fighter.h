/* BuildAMelee fighter identity, loadouts and per-match state.
 *
 * Glue between the borrowed-move engine (engine.h) and the game. A
 * loadout is assigned per player slot
 * before a match; every fighter in that slot (Popo and Nana both) borrows.
 *
 * Memory rules (docs/ARCHITECTURE.md): the overlay is outside Slippi's
 * rollback range, so everything that changes during a match lives in heap
 * blocks allocated at match start, reached through pointers that stay
 * constant for the whole match (bam_match, and the Fighter-struct extension
 * slot that points at a fighter's BamFighterState).
 */
#ifndef BAM_FIGHTER_H
#define BAM_FIGHTER_H
#include <bam/bam.h>
#include <melee/ft/forward.h>
#include <melee/ft/types.h>
#include <engine/catalog.h>

#define BAM_SPECIAL_SLOTS 4
#ifndef BAM_AERIAL_SLOTS
#define BAM_AERIAL_SLOTS 5 /* also in catalog.h */
#endif
/* Ground attacks and throws, each borrowed whole from one character. */
#define BAM_NORMAL_SLOTS 12
enum BamNormalSlot {
    BAM_NORMAL_JAB, BAM_NORMAL_DASH, BAM_NORMAL_FTILT, BAM_NORMAL_UTILT, BAM_NORMAL_DTILT,
    BAM_NORMAL_FSMASH, BAM_NORMAL_USMASH, BAM_NORMAL_DSMASH,
    BAM_NORMAL_FTHROW, BAM_NORMAL_BTHROW, BAM_NORMAL_UTHROW, BAM_NORMAL_DTHROW
};
/* Player slots 0..3, leader and partner each: index = player_id * 2 + sub. */
#define BAM_FIGHTERS 8
#define BAM_PLAYER_SLOTS 4

#ifndef BAM_ENABLE_AERIALS
#define BAM_ENABLE_AERIALS 1
#endif

typedef struct BamLoadout {
    unsigned char enabled;                      /* 0: this slot plays retail */
    unsigned char specials[BAM_SPECIAL_SLOTS];  /* BamSpecialID, 0 = native */
    unsigned char aerials[BAM_AERIAL_SLOTS];    /* bam_aerials id, 0 = native */
    unsigned char normals[BAM_NORMAL_SLOTS];    /* donor CharacterKind + 1, 0 = native */
} BamLoadout;

/* Set before the match loads (menu, debug hotkeys, online exchange). Read
 * only once a match runs: Fighter_Create copies it into match-heap state. */
extern BamLoadout bam_loadouts[BAM_PLAYER_SLOTS];
void Bam_LoadoutClear(BamLoadout* l);

/* Identity. */
bool Bam_IsBuildFighter(const Fighter* fp);   /* slot has an enabled loadout */
int Bam_FighterIndex(const Fighter* fp);        /* 0..BAM_FIGHTERS-1 or -1 */

/* The fighter's own kit (match-heap copy of the loadout; Zelda/Sheik's
 * transform rewrites it, so it is per fighter and rolled back). */
unsigned Bam_EquippedSpecial(const Fighter* fp, unsigned slot);
unsigned Bam_EquippedAerial(const Fighter* fp, unsigned slot);
void Bam_SetEquippedSpecial(Fighter* fp, unsigned slot, unsigned id);
/* The donor (FighterKind + 1) of a ground attack or throw slot whose data
 * loaded for this match, 0 when the fighter uses its own move. */

/* Fighter-struct extension: one pointer appended past whatever size the
 * allocator was given (retail 0x23EC, Slippi 0x2600). */
#define BAM_FIGHTER_EXT_SIZE 0x20
struct BamFighterState;
struct BamFighterState** Bam_FighterExtSlot(const Fighter* fp);
unsigned Bam_FighterAllocSize(unsigned retail_size);   /* inject: Fighter_800679B0 */

/* Match lifetime. */
struct BamMatchState;
extern struct BamMatchState* bam_match;
void Bam_MatchBegin(void);     /* first build fighter of a scene */
void Bam_MatchEnd(void);       /* scene exit */

/* Hooks (project.toml). */
void BamFighter_Created(Fighter* fp);
void Bam_OnSceneEnter(void* info);
void Bam_OnSceneExit(void);

/* Whether a donor can be loaded at all (fighter.c). */
int Bam_DonorFits(int kind);
unsigned Bam_HeapRoom(void);
void Bam_LogHeapRoom(const char* where);
void Bam_LbHeapRoom(int heap, unsigned* total, unsigned* largest);
void Bam_LogHeapTable(void);
#endif
