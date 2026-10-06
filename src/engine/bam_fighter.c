#include <engine/special_internal.h>
#include <melee/ft/fighter.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/initialize.h>
#include <dolphin/os/OSAlloc.h>
#include <sysdolphin/baselib/archive.h>
#include <melee/lb/lbfile.h>

BamLoadout bam_loadouts[BAM_PLAYER_SLOTS];
BamMatchState* bam_match;
static unsigned scene_generation;
/* Size the fighter allocator was asked for before our extension; the
 * extension pointer lives at this offset. Learned at Fighter_800679B0. */
static unsigned fighter_ext_offset;

void Bam_LoadoutClear(BamLoadout* l) { memset(l, 0, sizeof(*l)); }

static int climber_or_main(const Fighter* fp)
{
    return !fp->is_sub_fighter || fp->kind == Ft_Kind_Nana;
}

bool Bam_IsBuildFighter(const Fighter* fp)
{
    return fp && fp->player_id < BAM_PLAYER_SLOTS && climber_or_main(fp) &&
           bam_loadouts[fp->player_id].enabled;
}

int Bam_FighterIndex(const Fighter* fp)
{
    if (!fp || fp->player_id >= BAM_PLAYER_SLOTS) return -1;
    return (int) fp->player_id * 2 + (fp->is_sub_fighter ? 1 : 0);
}

unsigned Bam_EquippedSpecial(const Fighter* fp, unsigned slot)
{
    BamFighterState* S = Bam_FighterCtx(fp);
    if (S->fighter != fp || slot >= BAM_SPECIAL_SLOTS) return 0;
    return S->specials[slot];
}

unsigned Bam_EquippedAerial(const Fighter* fp, unsigned slot)
{
    BamFighterState* S = Bam_FighterCtx(fp);
    if (S->fighter != fp || slot >= BAM_AERIAL_SLOTS) return 0;
    return S->aerials[slot];
}

void Bam_SetEquippedSpecial(Fighter* fp, unsigned slot, unsigned id)
{
    BamFighterState* S = Bam_FighterCtx(fp);
    if (S->fighter == fp && slot < BAM_SPECIAL_SLOTS) S->specials[slot] = (unsigned char) id;
}

/* ---- Fighter struct extension ---------------------------------------------- */

/* inject at Fighter_800679B0+0x10: r4 holds the size HSD_ObjAllocInit is
 * about to receive (retail 0x23EC; Slippi's 04 code makes it 0x2600). We add
 * our slot after it and remember where it is. Deterministic per boot. */
unsigned Bam_FighterAllocSize(unsigned retail_size)
{
    fighter_ext_offset = (retail_size + 7U) & ~7U;
    BAM_LOG("fighter alloc %u -> ext at 0x%x\n", retail_size, fighter_ext_offset);
    return fighter_ext_offset + BAM_FIGHTER_EXT_SIZE;
}

struct BamFighterState** Bam_FighterExtSlot(const Fighter* fp)
{
    return (struct BamFighterState**) ((u8*) fp + fighter_ext_offset);
}

/* ---- match lifetime ----------------------------------------------------------- */

void Bam_MatchBegin(void)
{
    if (bam_match) return;
    bam_match = HSD_MemAlloc(sizeof(*bam_match));
    memset(bam_match, 0, sizeof(*bam_match));
    bam_match->generation = ++scene_generation;
    Bam_AnimScaleMatchBegin();
    Bam_SwordVisualMatchBegin();
    Bam_RestSleepMatchBegin();
    BAM_LOG("match_begin generation=%u state=%u bytes\n", bam_match->generation, (unsigned) sizeof(*bam_match));
}

void Bam_MatchEnd(void)
{
    if (!bam_match) return;
    Bam_AbilityMatchEnd();
    Bam_AnimScaleMatchEnd();
    Bam_SwordVisualMatchEnd();
    Bam_RestSleepMatchEnd();
    BAM_LOG("match_end generation=%u\n", bam_match->generation);
    bam_match = NULL;   /* the scene heap that held it is being torn down */
}

/* ---- hooks ---------------------------------------------------------------------- */

/* inject at Fighter_Create+0x4F0 (after ftLib_800867E8(gobj)). */
void BamFighter_Created(Fighter* fp)
{
    if (fighter_ext_offset == 0) return;   /* allocator hook did not run: never touch fp */
    *Bam_FighterExtSlot(fp) = NULL;
    if (!Bam_IsBuildFighter(fp)) return;
    Bam_MatchBegin();
    Bam_AbilityFighterCreated(fp);
}

void Bam_OnSceneEnter(void* info)
{
    (void) info;
    BamCache_SceneEnter();
    /* A new scene has a fresh heap; any match block from the previous scene
     * is gone with it. */
    bam_match = NULL;
}

void Bam_OnSceneExit(void)
{
    Bam_MatchEnd();
    BamCache_SceneExit();
}

/* Largest block the current (match) heap can still give, in 16 KB steps. */
unsigned Bam_HeapRoom(void)
{
    unsigned lo = 0, hi = 0x1000000;
    while (hi - lo > 0x4000) {
        unsigned mid = (lo + hi) / 2;
        void* p = OSAllocFromHeap(HSD_GetHeap(), mid);
        if (p) { OSFreeToHeap(HSD_GetHeap(), p); lo = mid; } else hi = mid;
    }
    return lo;
}

/* Melee's own heap table (Hsd/ARAM/Seq/Stay/AllM/AllA: used + free). */
void lbHeap_80015DF8(void);
void Bam_LogHeapTable(void)
{
#if BAM_DEBUG
    lbHeap_80015DF8();
#endif
}

void Bam_LogHeapRoom(const char* where)
{
#if BAM_DEBUG
    unsigned t1, l1;
    Bam_LbHeapRoom(1, &t1, &l1);
    BAM_LOG("heap room %s: %u KB (cache main %u KB, cache ARAM %u KB, ARAM heap %u KB)\n",
             where, Bam_HeapRoom() / 1024, BamCache_Room(1) / 1024, BamCache_Room(0) / 1024, l1 / 1024);
#else
    (void) where;
#endif
}

/* Whether a donor can be loaded at all. What a borrowed move's donor costs:
 *   its fighter data, without the Metal Box model and, when no equipped move
 *   needs them, its articles (donor_trim.c): 50-160 KB; loading it needs the
 *   whole file and some bookkeeping as temporary space
 *   its effects file (up to ~160 KB)
 * Both go to the preload-cache block (bam_cache.c) when it has room, else
 * to the match heap, which must keep BAM_HEAP_FLOOR free; every load checks
 * its own room and fails cleanly (the slot then keeps its own move).
 * Animations go to ARAM; models (drawn only) load after the fighters
 * (Bam_DonorModelsLoad). */
struct StringPair { char* a; char* b; };
extern struct StringPair ftData_803C1F40[Ft_Kind_Max];

int Bam_DonorFits(int kind)
{
    unsigned room;
    if (kind < 0 || kind >= Ft_Kind_Max || !ftData_803C1F40[kind].a) return 0;
    if (gFtDataList[kind] || BamCache_Room(1) >= 0x20000) return 1;
    room = Bam_HeapRoom();
    if (room >= BAM_HEAP_FLOOR + 0x20000) return 1;
    BAM_NOTE("donor kind=%d does not fit (%u KB free, cache %u KB)\n", kind, room / 1024,
             BamCache_Room(1) / 1024);
    return 0;
}
