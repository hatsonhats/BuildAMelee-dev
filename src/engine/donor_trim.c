/* Loads a donor's fighter data (PlXx.dat) without what borrowed moves never
 * use, to fit many donors in one match.
 *
 * A PlXx.dat is ~100-330 KB. Two parts of it are only ever used by the
 * character's own fighter or by some of its moves:
 *   ftData x5C, the Metal Box model (40-100 KB), drawn only on the fighter
 *   itself;
 *   ftData x48, the articles (bat, yo-yo, blaster, Shadow Ball...; 30-200
 *   KB), used only by the moves that spawn them.
 * Every PlXx.dat on the disc is ~5.2 MB together; without those parts 1.9 MB.
 *
 * The file has no type information, so objects are delimited by pointer
 * targets (an object runs from one pointer target to the next). That is not
 * exact: a move script that loops back into itself is split at the loop
 * target, and nothing else points at its tail. So nothing is dropped for
 * being unreferenced. Only objects that the cut parts reach, and that
 * nothing else reaches (from the root or from any unreferenced object
 * outside the cut parts), are dropped (whole 32-byte blocks, so textures and
 * display lists keep their alignment); everything else stays, referenced or
 * not. Checked offline on every PlXx.dat on the disc: every action script
 * (walked command by command) stays whole and no kept object points into a
 * dropped one.
 *
 * Articles are kept when any equipped move of any player needs them (the
 * donor's data is shared by every borrower), and the complete file is loaded
 * when the character is also playing (its own fighter uses this data) or is
 * already in Melee's preload cache.
 */
#include <engine/special_internal.h>
#include <melee/lb/lbfile.h>
#include <melee/ef/efasync.h>
#include <melee/ft/ftdata.h>
#include <melee/pl/player.h>
#include <melee/lb/lbarchive.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/memory.h>
#include <dolphin/os.h>
#include <string.h>

struct StringPair { char* a; char* b; };
extern struct StringPair ftData_803C1F40[Ft_Kind_Max];
HSD_Archive* lbDvd_8001819C(const char* basename);

#define FTDATA_METAL_MODEL 0x5C

#define HEAP_FLOOR BAM_HEAP_FLOOR

/* The complete file in the preload-cache block (bam_cache.c), parsed in
 * place, as the game loads it into the match heap. */
static ftData* load_into_cache(int kind)
{
    const char* name = ftData_803C1F40[kind].a;
    size_t size = lbFileGetSize(name), length = 0;
    u8* buf;
    HSD_Archive* arc;
    ftData* root;
    if (!size || BamCache_Room(1) < OSRoundUp32B(size) + 0x60) return NULL;
    buf = BamCache_Alloc(1, OSRoundUp32B(size));
    arc = BamCache_Alloc(1, sizeof(HSD_Archive));
    if (!buf || !arc) return NULL;
    memset(arc, 0, sizeof(HSD_Archive));
    lbFile_8001668C(name, buf, &length);
    lbArchive_InitializeDAT(arc, buf, length);
    root = HSD_ArchiveGetPublicAddress(arc, ftData_803C1F40[kind].b);
    if (root) BAM_LOG("donor_data kind=%d %u KB in cache\n", kind, (unsigned) (size / 1024));
    return root;
}

static void sift(u32* a, int root, int n)
{
    int child;
    u32 t;
    while ((child = root * 2 + 1) < n) {
        if (child + 1 < n && a[child] < a[child + 1]) ++child;
        if (a[root] >= a[child]) return;
        t = a[root]; a[root] = a[child]; a[child] = t;
        root = child;
    }
}

static void sort_u32(u32* a, int n)
{
    int i;
    u32 t;
    for (i = n / 2 - 1; i >= 0; --i) sift(a, i, n);
    for (i = n - 1; i > 0; --i) {
        t = a[0]; a[0] = a[i]; a[i] = t;
        sift(a, 0, i);
    }
}

/* Index of the last element <= v (or -1). */
static int floor_index(const u32* a, int n, u32 v)
{
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (a[mid] <= v) lo = mid + 1; else hi = mid;
    }
    return lo - 1;
}

/* Index of the first element >= v. */
static int lower_index(const u32* a, int n, u32 v)
{
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (a[mid] < v) lo = mid + 1; else hi = mid;
    }
    return lo;
}

/* Temporary space: the top of the preload-cache block (bam_cache.c) when
 * it has room, else the match heap (freed when the load finishes). */
#define MAX_TEMPS 8
static void* temps[MAX_TEMPS];
static int ntemps;

/* HSD_MemAlloc asserts instead of failing, so the match heap is only
 * used when it keeps HEAP_FLOOR free afterwards. */
static void* heap_alloc(u32 size)
{
    size = OSRoundUp32B(size ? size : 4);
    if (Bam_HeapRoom() < size + HEAP_FLOOR) return NULL;
    return HSD_MemAlloc(size);
}

static void* temp_alloc(u32 size)
{
    void* p;
    size = OSRoundUp32B(size ? size : 4);
    p = BamCache_TempAlloc(size);
    if (p) return p;
    if (ntemps >= MAX_TEMPS) return NULL;
    p = heap_alloc(size);
    if (p) temps[ntemps++] = p;
    return p;
}

static void temp_release(void)
{
    while (ntemps > 0) HSD_Free(temps[--ntemps]);
    BamCache_TempReset();
}

/* Permanent space: the preload-cache block, else the match heap. */
static void* keep_alloc(u32 size)
{
    void* p = BamCache_Alloc(1, size);
    return p ? p : heap_alloc(size);
}

/* Reach over objects from `from` (object indices, marked in `mark` with
 * `bit`), not following pointers stored at the `ncut` locations in `cut`. */
static void reach(const u8* data, u32 dsize, const u32* rel, u32 nrel, const u32* starts, u32 nstarts, u32* stack,
                  u8* mark, u8 bit, const u32* from, int nfrom, const u32* cut, int ncut)
{
    int sp = 0, i;
    for (i = 0; i < nfrom; ++i) {
        int k = floor_index(starts, (int) nstarts, from[i]);
        if (k >= 0 && !(mark[k] & bit)) { mark[k] |= bit; stack[sp++] = (u32) k; }
    }
    while (sp > 0) {
        u32 k = stack[--sp], start = starts[k], end = k + 1 < nstarts ? starts[k + 1] : dsize;
        int r;
        for (r = lower_index(rel, (int) nrel, start); r < (int) nrel && rel[r] < end; ++r) {
            u32 t;
            int idx, c, skip = 0;
            for (c = 0; c < ncut; ++c) if (rel[r] == cut[c]) skip = 1;
            if (skip) continue;
            t = *(u32*) (data + rel[r]) - (u32) data;
            if (t >= dsize) continue;
            idx = floor_index(starts, (int) nstarts, t);
            if (idx >= 0 && !(mark[idx] & bit)) { mark[idx] |= bit; stack[sp++] = (u32) idx; }
        }
    }
}

#define FTDATA_ARTICLES 0x48
#define REACH_KEPT 1
#define REACH_CUT 2

/* Returns the trimmed ftData, or NULL (then the caller loads it whole). */
static ftData* load_trimmed(int kind, int articles)
{
    const char* name = ftData_803C1F40[kind].a;
    size_t length = 0, file_size;
    u8* file = NULL;
    HSD_Archive* arc = NULL;
    u32 *rel = NULL, *starts = NULL, *stack = NULL;
    u8 *mark = NULL, *cover = NULL;
    u16* newblk = NULL;
    u8* data;
    u8* out = NULL;
    ftData* result = NULL;
    u32 dsize, nrel, nstarts, nblk, i, kept, root_off, cut[2], cut_to[2];
    int ncut = 0;

    file_size = lbFileGetSize(name);
    if (!file_size) return NULL;
    ntemps = 0;
    file = temp_alloc(file_size);
    arc = temp_alloc(sizeof(HSD_Archive));
    if (!file || !arc) goto done;
    memset(arc, 0, sizeof(HSD_Archive));
    lbFile_8001668C(name, file, &length);
    lbArchive_InitializeDAT(arc, file, length);
    {
        void* root = HSD_ArchiveGetPublicAddress(arc, ftData_803C1F40[kind].b);
        if (!root) goto done;
        data = arc->data;
        root_off = (u32) root - (u32) data;
    }
    dsize = arc->header.data_size;
    nrel = arc->header.nb_reloc;
    nblk = (dsize + 31) / 32;
    if (nblk >= 0x10000 || root_off + 0x60 > dsize) goto done;
    cut[ncut++] = root_off + FTDATA_METAL_MODEL;
    if (!articles) cut[ncut++] = root_off + FTDATA_ARTICLES;

    rel = temp_alloc(nrel * 4);
    starts = temp_alloc((nrel + 1) * 4);
    stack = temp_alloc((nrel + 1) * 4);
    mark = temp_alloc(nrel + 1);
    cover = temp_alloc(nblk);
    newblk = temp_alloc(nblk * 2);
    if (!rel || !starts || !stack || !mark || !cover || !newblk) goto done;

    /* Pointer locations (sorted) and object starts (pointer targets + root). */
    for (i = 0; i < nrel; ++i) rel[i] = arc->reloc_info[i].offset;
    sort_u32(rel, (int) nrel);
    nstarts = 0;
    for (i = 0; i < nrel; ++i) {
        u32 t = *(u32*) (data + rel[i]) - (u32) data;
        if (t < dsize) starts[nstarts++] = t;
    }
    starts[nstarts++] = root_off;
    sort_u32(starts, (int) nstarts);
    {
        u32 w = 0;
        for (i = 0; i < nstarts; ++i)
            if (w == 0 || starts[i] != starts[w - 1]) starts[w++] = starts[i];
        nstarts = w;
    }
    memset(mark, 0, nstarts);
    /* What the cut parts reach... */
    {
        int nto = 0;
        for (i = 0; i < (u32) ncut; ++i) {
            u32 t = *(u32*) (data + cut[i]);
            if (t >= (u32) data && t - (u32) data < dsize) cut_to[nto++] = t - (u32) data;
        }
        reach(data, dsize, rel, nrel, starts, nstarts, stack, mark, REACH_CUT, cut_to, nto, NULL, 0);
    }
    /* ...and what everything else reaches: the root, and every unreferenced
     * object outside the cut parts' address range (it may be the tail of a
     * script split at a loop target). Unreferenced objects inside that
     * range are fragments of the cut parts themselves (checked on the
     * disc: none holds a fighter script). */
    reach(data, dsize, rel, nrel, starts, nstarts, stack, mark, REACH_KEPT, &root_off, 1, cut, ncut);
    {
        u32 lo = dsize, hi = 0;
        for (i = 0; i < nstarts; ++i)
            if (mark[i] & REACH_CUT) {
                u32 e = i + 1 < nstarts ? starts[i + 1] : dsize;
                if (starts[i] < lo) lo = starts[i];
                if (e > hi) hi = e;
            }
        for (i = 0; i < nstarts; ++i)
            if (!mark[i] && (starts[i] < lo || starts[i] >= hi))
                reach(data, dsize, rel, nrel, starts, nstarts, stack, mark, REACH_KEPT, &starts[i], 1, cut, ncut);
    }

    /* A block goes only when every byte of it is in dropped objects. */
    memset(cover, 0, nblk);
    for (i = 0; i < nstarts; ++i) {
        u32 a, e;
        if (mark[i] != REACH_CUT) continue;
        a = starts[i];
        e = i + 1 < nstarts ? starts[i + 1] : dsize;
        while (a < e) {
            u32 b = a / 32, stop = (b + 1) * 32 < e ? (b + 1) * 32 : e;
            cover[b] = (u8) (cover[b] + (stop - a));
            a = stop;
        }
    }
    for (i = 0, kept = 0; i < nblk; ++i) {
        u32 len = (i + 1) * 32 <= dsize ? 32 : dsize - i * 32;
        cover[i] = cover[i] >= len; /* 1: dropped */
        newblk[i] = (u16) kept;
        if (!cover[i]) ++kept;
    }
    out = keep_alloc(kept * 32);
    if (!out) goto done;
    for (i = 0; i < nblk; ++i)
        if (!cover[i]) {
            u32 n = (i + 1) * 32 <= dsize ? 32 : dsize - i * 32;
            memset(out + newblk[i] * 32, 0, 32);
            memcpy(out + newblk[i] * 32, data + i * 32, n);
        }
    for (i = 0; i < nrel; ++i) {
        u32 at = rel[i], t;
        u32* dst;
        if (cover[at / 32]) continue;
        dst = (u32*) (out + newblk[at / 32] * 32 + at % 32);
        t = *(u32*) (data + at) - (u32) data;
        if (at == cut[0] || (ncut > 1 && at == cut[1]) || t >= dsize || cover[t / 32]) *dst = 0; /* the cut parts */
        else *dst = (u32) out + newblk[t / 32] * 32 + t % 32;
    }
    DCStoreRange(out, kept * 32);
    result = (ftData*) (out + newblk[root_off / 32] * 32 + root_off % 32);
    BAM_LOG("donor_data kind=%d %u KB -> %u KB%s in %s\n", kind, (unsigned) (dsize / 1024),
             (unsigned) (kept * 32 / 1024), articles ? "" : " (no articles)", BamCache_Owns(out) ? "cache" : "heap");

done:
    temp_release();
    return result;
}

/* Whether a character playing in this match is `kind` (its own fighter
 * then needs the complete file, Metal Box model included). */
#define FT_MAPPING ((const s8*) 0x803BCDE0) /* ftMapping_list: internal id, extra id, ... */
static int kind_is_playing(int kind)
{
    int slot;
    for (slot = 0; slot < 6; ++slot) {
        int ck;
        if ((int) Player_GetPlayerSlotType(slot) == Gm_PKind_NA) continue;
        ck = (int) Player_GetPlayerCharacter(slot);
        if (ck < 0 || ck >= 33) continue;
        if (FT_MAPPING[ck * 3] == kind || FT_MAPPING[ck * 3 + 1] == kind) return 1;
    }
    return 0;
}

/* 1 when the donor's data is loaded. 0 when memory ran out: the caller
 * then leaves the slot on the fighter's own move. */
int Rogue_LoadDonorData(int kind)
{
    const char* name;
    if (kind < 0 || kind >= Ft_Kind_Max) return 0;
    if (gFtDataList[kind]) return 1;
    name = ftData_803C1F40[kind].a;
    if (!name) return 0;
    if (lbDvd_8001819C(name)) {           /* preloaded: costs no heap */
        ftData_8008572C(kind);
        return gFtDataList[kind] != NULL;
    }
    /* A character that is also playing needs the complete file: its own
     * fighter uses this data. */
    if (!kind_is_playing(kind)) {
        ftData* d = load_trimmed(kind, Rogue_DonorNeedsArticles(kind));
        if (d) { gFtDataList[kind] = d; return 1; }
    }
    {
        ftData* d = load_into_cache(kind);
        if (d) { gFtDataList[kind] = d; return 1; }
    }
    /* The complete file, into the match heap, only with room to spare. */
    if (Bam_HeapRoom() < OSRoundUp32B(lbFileGetSize(name)) + 0x100 + HEAP_FLOOR) {
        BAM_NOTE("donor_data kind=%d: out of memory\n", kind);
        return 0;
    }
    ftData_8008572C(kind);
    return gFtDataList[kind] != NULL;
}

/* A donor's effects file (EfXxData.dat), as efAsync_LoadSync loads it but
 * into the preload-cache block when it has room. */
typedef struct EfDatEntry { char* file; char* table; void* data; } EfDatEntry;
#define EF_ENTRIES ((EfDatEntry*) 0x803C025C) /* efAsync_DatEntries[51] */
void psInitDataBank(int bank, int* cmdBank, int* texBank, u32* ref, int* formBank);

static EfDatEntry* donor_effects(int kind)
{
    int idx;
    if (kind < 0 || kind >= Ft_Kind_Max) return NULL;
    idx = ftData_UnkBytePerCharacter[kind];
    if (idx == 0xFF || idx >= 50 || !EF_ENTRIES[idx].file) return NULL;
    return &EF_ENTRIES[idx];
}

int Rogue_LoadDonorEffects(int kind)
{
    EfDatEntry* e = donor_effects(kind);
    int idx;
    size_t size, length = 0;
    u8* buf;
    HSD_Archive* arc;
    EfDatEntry* table;
    if (!e || e->data) return 1;
    idx = (int) (e - EF_ENTRIES);
    if (!lbDvd_8001819C(e->file) && !kind_is_playing(kind)) {
        size = lbFileGetSize(e->file);
        if (size && BamCache_Room(1) >= OSRoundUp32B(size) + 0x60) {
            buf = BamCache_Alloc(1, OSRoundUp32B(size));
            arc = BamCache_Alloc(1, sizeof(HSD_Archive));
            if (buf && arc) {
                memset(arc, 0, sizeof(HSD_Archive));
                lbFile_8001668C(e->file, buf, &length);
                lbArchive_InitializeDAT(arc, buf, length);
                table = HSD_ArchiveGetPublicAddress(arc, e->table);
                if (table) {
                    if ((u32) table->file | (u32) table->table)
                        psInitDataBank(idx, (int*) table->file, (int*) table->table, NULL, NULL);
                    e->data = &table->data;
                    BAM_LOG("donor_effects kind=%d %u KB in cache\n", kind, (unsigned) (size / 1024));
                    return 1;
                }
            }
        }
    }
    /* As the game loads it: preloaded, or into the match heap. */
    if (!lbDvd_8001819C(e->file) &&
        Bam_HeapRoom() < OSRoundUp32B(lbFileGetSize(e->file)) + 0x100 + HEAP_FLOOR) {
        BAM_NOTE("donor_effects kind=%d: out of memory\n", kind);
        return 0;
    }
    efAsync_LoadSync(idx);
    return 1;
}
