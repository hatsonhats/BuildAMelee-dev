/* Storage for borrowed moves outside the match heap.
 *
 * A match's own heap (lbHeap 0, "Hsd") has 2-5 MB left once the stage and
 * fighters are in, depending on the stage and on Slippi Online. Melee's
 * preload caches have room to spare during a 1v1 (the game's own heap
 * report, lbHeap_80015DF8, on Battlefield and on a large stage):
 *   AllM (lbHeap 4, main RAM, characters and stage): 1.5-3.9 MB free
 *   AllA (lbHeap 5, ARAM, the fighters' animations):  3.6-4.3 MB free
 *
 * At the start of a match one block is taken from each (leaving a margin),
 * borrowed-move data is bump-allocated from it, and both blocks go back at
 * scene exit, before the next scene's preloads run. Main-RAM blocks hold the
 * donors' trimmed fighter data and effect files; the ARAM block holds the
 * borrowed animations, which Melee streams into a fighter's buffer on every
 * action change.
 *
 * The caches are managed by Melee's preloader, which compacts a heap (moving
 * every block in it) before it loads a file into it. It loads files only
 * between scenes and while a scene starts; in a VS match nothing is queued.
 * The blocks are taken only when the preloader is idle, and the main-RAM
 * block also keeps temporary space at its top for loading (reset after
 * each file). When anything here is unavailable, callers use the match heap
 * as before.
 *
 * Called from: donor_load.c, donor_trim.c, visual/donor_model.c; scene enter/exit from fighter.c.
 * State: arenas and their bookkeeping (loading cache, outside the match; project.toml [state]).
 */
#include <engine/internal.h>
#include <bam/retail.h>
#include <dolphin/os.h>
#include <string.h>

/* lbHeap state: retail keeps it at 0x80431FA0; Slippi moves it and points
 * r2+0x184 at it (its 04 patch of lbHeap_80015BB8+0x8). */
#define SDA2_BASE 0x804DF9E0
static u8* lbheap_state(void)
{
    u32 insn = *(u32*) 0x80015BC0;
    if (insn == 0x38631FA0) return (u8*) 0x80431FA0;     /* addi r3,r3,0x1FA0 */
    if (insn == 0x80620184) {                             /* lwz r3,0x184(r2) */
        u32 p = *(u32*) (SDA2_BASE + 0x184);
        if (p >= 0x80000000U && p < 0x81800000U && !(p & 3)) return (u8*) p;
    }
    return NULL;
}

typedef struct LbHeapRec { s32 id; u32 handle; u32 start; u32 size; s32 type; s32 transient; s32 status; } LbHeapRec;
typedef struct MemEntry { struct MemEntry* next; u32 lo; u32 size; } MemEntry;
typedef struct HeapHandle { MemEntry* next; u32 lo; u32 hi; MemEntry* first; } HeapHandle;

static LbHeapRec* heap_rec(int heap)
{
    u8* st = lbheap_state();
    if (!st || heap < 0 || heap > 5) return NULL;
    return (LbHeapRec*) (st + 0x10 + heap * 0x1C);
}

void Bam_LbHeapRoom(int heap, unsigned* total, unsigned* largest)
{
    LbHeapRec* r = heap_rec(heap);
    HeapHandle* h;
    MemEntry* it;
    u32 start, end;
    *total = *largest = 0;
    if (!r || r->status != 0 /* LbHeapStatus_Create */ || r->type == 0) return;
    h = (HeapHandle*) r->handle;
    if (!h || (u32) h == 0xFFFFFFFFU || (u32) h < 0x80000000U) return;
    start = h->lo;
    for (it = h->first;; it = it->next) {
        end = it ? it->lo : h->hi;
        if (end > start) {
            *total += end - start;
            if (end - start > *largest) *largest = end - start;
        }
        if (!it) break;
        start = it->lo + it->size;
    }
}

/* lbMemory's pool of block records, shared by all its heaps (0x83). */
#define LBMEM_FREE_LIST (*(MemEntry**) 0x80431EDC)
int BamCache_FreeRecords(void);
static int free_records(void) { return BamCache_FreeRecords(); }
int BamCache_FreeRecords(void)
{
    MemEntry* e;
    int n = 0;
    for (e = LBMEM_FREE_LIST; e && n < 0x100; e = e->next) ++n;
    return n;
}

/* Preloader idle: no file queued or loading, no compaction running. */
#define PRELOAD_CACHE ((u8*) 0x80432078)
static int preloader_idle(void)
{
    int i;
    if (*(s32*) (PRELOAD_CACHE + 0x96C) != 6) return 0;
    for (i = 0; i < 80; ++i) {
        s8 state = (s8) PRELOAD_CACHE[0xAC + i * 0x1C];
        if (state == 1 || state == 2) return 0;
        if (state < 0 || state > 4) return 0; /* not what the decomp describes: do not touch */
    }
    return 1;
}

void* lbHeap_80015BD0(int heap_id, size_t size);
void lbHeap_80015CA8(int heap_id, void* addr);

typedef struct Arena {
    int heap;          /* lbHeap id, 0 = not open */
    MemEntry* rec;     /* the block's record (its address can move only by compaction) */
    u32 base, size;
    u32 lo;            /* permanent allocations grow up from base */
    u32 hi;            /* temporary ones grow down from base + size */
} Arena;

static Arena arenas[2]; /* 0: AllM (main RAM), 1: AllA (ARAM) */
static unsigned arena_scene;
static unsigned scene_counter;

#define MARGIN 0x80000
static void arena_open(Arena* a, int heap, u32 cap)
{
    unsigned total, largest;
    u32 size;
    MemEntry* rec;
    memset(a, 0, sizeof(*a));
    Bam_LbHeapRoom(heap, &total, &largest);
    if (largest < MARGIN + 0x40000) return;
    size = (largest - MARGIN) & ~0x1FU;
    if (size > cap) size = cap;
    rec = lbHeap_80015BD0(heap, size);
    if (!rec || (u32) rec < 0x80000000U) return;
    a->heap = heap;
    a->rec = rec;
    a->base = rec->lo;
    a->size = size;
    a->lo = a->base;
    a->hi = a->base + size;
    BAM_LOG("cache arena heap=%d %u KB at %08x (free was %u KB, largest %u KB)\n", heap, size / 1024,
             (unsigned) a->base, total / 1024, largest / 1024);
}

static void release_all(void)
{
    int i;
    for (i = 0; i < 2; ++i) {
        Arena* a = &arenas[i];
        if (!a->heap) continue;
        if (a->rec->lo != a->base)
            BAM_LOG("cache arena heap=%d moved %08x -> %08x\n", a->heap, (unsigned) a->base,
                     (unsigned) a->rec->lo);
        lbHeap_80015CA8(a->heap, (void*) a->rec->lo);
        BAM_LOG("cache arena heap=%d used %u of %u KB, released\n", a->heap,
                 (a->lo - a->base) / 1024, a->size / 1024);
        memset(a, 0, sizeof(*a));
    }
}

/* Opens both blocks once per scene, on first use. */
static void ensure_open(void)
{
    if (arena_scene == scene_counter && scene_counter) return;
    arena_scene = scene_counter;
    release_all(); /* never two scenes' blocks at once */
    /* VS (2) and Slippi Online (8) only: those queue no preloads during a
     * match. Other modes keep using the match heap. */
    {
        u8 major = BAM_SCENE_MAJOR;
        if (major != BAM_SCENE_VS && major != BAM_SCENE_ONLINE) return;
    }
    /* Files the scene queued may still be streaming in (DVD and alarm
     * callbacks keep it going while this waits, on the loading screen). */
    {
        u32 ticks_ms = BAM_TICKS_PER_MS;
        OSTime start = OSGetTime();
        while (!preloader_idle() && (u32) ((OSGetTime() - start) / ticks_ms) < 1500) {}
    }
    if (!preloader_idle()) {
        BAM_LOG("cache arena: preloader busy, using the match heap\n");
        return;
    }
    if (free_records() < 16) {
        BAM_LOG("cache arena: lbMemory has %d records left, using the match heap\n", free_records());
        return;
    }
    arena_open(&arenas[0], 4, 0x400000);
    arena_open(&arenas[1], 5, 0x600000);
}

void BamCache_SceneEnter(void)
{
    ++scene_counter;
    if (!scene_counter) scene_counter = 1;
}

/* Scene exit: the blocks go back before the next scene's preloads. Uses
 * later in this scene (none expected) get the match heap. */
void BamCache_SceneExit(void)
{
    release_all();
    arena_scene = scene_counter;
}

/* ram: 1 = main RAM (AllM), 0 = ARAM (AllA). NULL when it does not fit. */
void* BamCache_Alloc(int ram, u32 size)
{
    Arena* a;
    u32 p;
    ensure_open();
    a = &arenas[ram ? 0 : 1];
    if (!a->heap) return NULL;
    size = (size + 31) & ~31U;
    if (a->hi - a->lo < size) return NULL;
    p = a->lo;
    a->lo += size;
    return (void*) p;
}

/* Temporary main-RAM space (top of the AllM block), dropped by TempReset. */
void* BamCache_TempAlloc(u32 size)
{
    Arena* a;
    ensure_open();
    a = &arenas[0];
    if (!a->heap) return NULL;
    size = (size + 31) & ~31U;
    if (a->hi - a->lo < size) return NULL;
    a->hi -= size;
    return (void*) a->hi;
}

void BamCache_TempReset(void)
{
    Arena* a = &arenas[0];
    if (a->heap) a->hi = a->base + a->size;
}

u32 BamCache_Room(int ram)
{
    Arena* a;
    ensure_open();
    a = &arenas[ram ? 0 : 1];
    return a->heap ? a->hi - a->lo : 0;
}

int BamCache_Owns(const void* p)
{
    int i;
    for (i = 0; i < 2; ++i)
        if (arenas[i].heap && (u32) p >= arenas[i].base && (u32) p < arenas[i].base + arenas[i].size) return 1;
    return 0;
}
