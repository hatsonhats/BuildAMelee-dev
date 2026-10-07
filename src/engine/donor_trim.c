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
 *
 * Called from: donor_load.c.
 * State: temps (scratch while trimming, scene setup; project.toml [state]).
 */
#include <engine/internal.h>
#include <bam/retail.h>
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

/* The complete file in the preload-cache block (donor_cache.c), parsed in
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

/* Temporary space: the top of the preload-cache block (donor_cache.c) when
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

/* One ftData file being trimmed: its data block, sorted pointer locations
 * and object starts (with what reaches each), and the 32-byte blocks kept. */
typedef struct Trim {
    u8* data;
    u32 dsize, nrel, nstarts, nblk;
    u32 *rel, *starts, *stack;
    u8 *mark, *cover;
    u16* newblk;
    u32 cut[2];
    int ncut;
} Trim;

/* Pointer locations (sorted) and object starts (pointer targets + root). */
static void find_objects(Trim* t, const HSD_Archive* arc, u32 root_off)
{
    u32 i, w = 0;
    for (i = 0; i < t->nrel; ++i) t->rel[i] = arc->reloc_info[i].offset;
    sort_u32(t->rel, (int) t->nrel);
    t->nstarts = 0;
    for (i = 0; i < t->nrel; ++i) {
        u32 to = *(u32*) (t->data + t->rel[i]) - (u32) t->data;
        if (to < t->dsize) t->starts[t->nstarts++] = to;
    }
    t->starts[t->nstarts++] = root_off;
    sort_u32(t->starts, (int) t->nstarts);
    for (i = 0; i < t->nstarts; ++i)
        if (w == 0 || t->starts[i] != t->starts[w - 1]) t->starts[w++] = t->starts[i];
    t->nstarts = w;
}

/* Marks what the cut parts reach (REACH_CUT) and what everything else
 * reaches (REACH_KEPT). */
static void mark_reach(Trim* t, u32 root_off)
{
    u32 i, cut_to[2], lo = t->dsize, hi = 0;
    int nto = 0;
    memset(t->mark, 0, t->nstarts);
    for (i = 0; i < (u32) t->ncut; ++i) {
        u32 to = *(u32*) (t->data + t->cut[i]);
        if (to >= (u32) t->data && to - (u32) t->data < t->dsize) cut_to[nto++] = to - (u32) t->data;
    }
    reach(t->data, t->dsize, t->rel, t->nrel, t->starts, t->nstarts, t->stack, t->mark, REACH_CUT, cut_to, nto,
          NULL, 0);
    /* Kept: the root, and every unreferenced object outside the cut parts'
     * address range (it may be the tail of a script split at a loop
     * target). Unreferenced objects inside that range are fragments of the
     * cut parts themselves (checked on the disc: none holds a fighter
     * script). */
    reach(t->data, t->dsize, t->rel, t->nrel, t->starts, t->nstarts, t->stack, t->mark, REACH_KEPT, &root_off, 1,
          t->cut, t->ncut);
    for (i = 0; i < t->nstarts; ++i)
        if (t->mark[i] & REACH_CUT) {
            u32 e = i + 1 < t->nstarts ? t->starts[i + 1] : t->dsize;
            if (t->starts[i] < lo) lo = t->starts[i];
            if (e > hi) hi = e;
        }
    for (i = 0; i < t->nstarts; ++i)
        if (!t->mark[i] && (t->starts[i] < lo || t->starts[i] >= hi))
            reach(t->data, t->dsize, t->rel, t->nrel, t->starts, t->nstarts, t->stack, t->mark, REACH_KEPT,
                  &t->starts[i], 1, t->cut, t->ncut);
}

/* A block goes only when every byte of it is in dropped objects: sets
 * cover[] (1: dropped) and newblk[] (where a kept block lands) and returns
 * how many blocks are kept. */
static u32 drop_blocks(Trim* t)
{
    u32 i, kept;
    memset(t->cover, 0, t->nblk);
    for (i = 0; i < t->nstarts; ++i) {
        u32 a, e;
        if (t->mark[i] != REACH_CUT) continue;
        a = t->starts[i];
        e = i + 1 < t->nstarts ? t->starts[i + 1] : t->dsize;
        while (a < e) {
            u32 b = a / 32, stop = (b + 1) * 32 < e ? (b + 1) * 32 : e;
            t->cover[b] = (u8) (t->cover[b] + (stop - a));
            a = stop;
        }
    }
    for (i = 0, kept = 0; i < t->nblk; ++i) {
        u32 len = (i + 1) * 32 <= t->dsize ? 32 : t->dsize - i * 32;
        t->cover[i] = t->cover[i] >= len;
        t->newblk[i] = (u16) kept;
        if (!t->cover[i]) ++kept;
    }
    return kept;
}

/* Copies the kept blocks to `out` and moves every kept pointer; pointers to
 * the cut parts or to dropped blocks become NULL. */
static void copy_kept(const Trim* t, u8* out)
{
    u32 i;
    for (i = 0; i < t->nblk; ++i)
        if (!t->cover[i]) {
            u32 n = (i + 1) * 32 <= t->dsize ? 32 : t->dsize - i * 32;
            memset(out + t->newblk[i] * 32, 0, 32);
            memcpy(out + t->newblk[i] * 32, t->data + i * 32, n);
        }
    for (i = 0; i < t->nrel; ++i) {
        u32 at = t->rel[i], to;
        u32* dst;
        if (t->cover[at / 32]) continue;
        dst = (u32*) (out + t->newblk[at / 32] * 32 + at % 32);
        to = *(u32*) (t->data + at) - (u32) t->data;
        if (at == t->cut[0] || (t->ncut > 1 && at == t->cut[1]) || to >= t->dsize || t->cover[to / 32]) *dst = 0;
        else *dst = (u32) out + t->newblk[to / 32] * 32 + to % 32;
    }
}

/* Returns the trimmed ftData, or NULL (then the caller loads it whole). The
 * Metal Box model is always cut, the articles when `articles` is 0. */
static ftData* load_trimmed(int kind, int articles)
{
    const char* name = ftData_803C1F40[kind].a;
    size_t length = 0, file_size;
    u8* file;
    HSD_Archive* arc;
    u8* out;
    ftData* result = NULL;
    u32 kept, root_off;
    Trim t;

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
        t.data = arc->data;
        root_off = (u32) root - (u32) t.data;
    }
    t.dsize = arc->header.data_size;
    t.nrel = arc->header.nb_reloc;
    t.nblk = (t.dsize + 31) / 32;
    if (t.nblk >= 0x10000 || root_off + 0x60 > t.dsize) goto done;
    t.ncut = 0;
    t.cut[t.ncut++] = root_off + FTDATA_METAL_MODEL;
    if (!articles) t.cut[t.ncut++] = root_off + FTDATA_ARTICLES;

    t.rel = temp_alloc(t.nrel * 4);
    t.starts = temp_alloc((t.nrel + 1) * 4);
    t.stack = temp_alloc((t.nrel + 1) * 4);
    t.mark = temp_alloc(t.nrel + 1);
    t.cover = temp_alloc(t.nblk);
    t.newblk = temp_alloc(t.nblk * 2);
    if (!t.rel || !t.starts || !t.stack || !t.mark || !t.cover || !t.newblk) goto done;

    find_objects(&t, arc, root_off);
    mark_reach(&t, root_off);
    kept = drop_blocks(&t);
    out = keep_alloc(kept * 32);
    if (!out) goto done;
    copy_kept(&t, out);
    DCStoreRange(out, kept * 32);
    result = (ftData*) (out + t.newblk[root_off / 32] * 32 + root_off % 32);
    BAM_LOG("donor_data kind=%d %u KB -> %u KB%s in %s\n", kind, (unsigned) (t.dsize / 1024),
             (unsigned) (kept * 32 / 1024), articles ? "" : " (no articles)", BamCache_Owns(out) ? "cache" : "heap");

done:
    temp_release();
    return result;
}

/* Whether a character playing in this match is `kind` (its own fighter
 * then needs the complete file, Metal Box model included). */
static int kind_is_playing(int kind)
{
    int slot;
    for (slot = 0; slot < 6; ++slot) {
        int ck;
        if ((int) Player_GetPlayerSlotType(slot) == Gm_PKind_NA) continue;
        ck = (int) Player_GetPlayerCharacter(slot);
        if (ck < 0 || ck >= 33) continue;
        if (BAM_FT_MAPPING[ck * 3] == kind || BAM_FT_MAPPING[ck * 3 + 1] == kind) return 1;
    }
    return 0;
}

/* 1 when the donor's data is loaded. 0 when memory ran out: the caller
 * then leaves the slot on the fighter's own move. */
int Bam_LoadDonorData(int kind)
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
        ftData* d = load_trimmed(kind, Bam_DonorNeedsArticles(kind));
        if (d) { gFtDataList[kind] = d; return 1; }
    }
    {
        ftData* d = load_into_cache(kind);
        if (d) { gFtDataList[kind] = d; return 1; }
    }
    /* The complete file, into the match heap, only with room to spare. */
    if (Bam_HeapRoom() < OSRoundUp32B(lbFileGetSize(name)) + 0x100 + HEAP_FLOOR) {
        BAM_NOTE("memory: donor data kind=%d does not fit\n", kind);
        return 0;
    }
    ftData_8008572C(kind);
    return gFtDataList[kind] != NULL;
}

/* A donor's effects file (EfXxData.dat), as efAsync_LoadSync loads it but
 * into the preload-cache block when it has room. */
void psInitDataBank(int bank, int* cmdBank, int* texBank, u32* ref, int* formBank);

static BamEfDatEntry* donor_effects(int kind)
{
    int idx;
    if (kind < 0 || kind >= Ft_Kind_Max) return NULL;
    idx = ftData_UnkBytePerCharacter[kind];
    if (idx == 0xFF || idx >= 50 || !BAM_EF_ENTRIES[idx].file) return NULL;
    return &BAM_EF_ENTRIES[idx];
}

int Bam_LoadDonorEffects(int kind)
{
    BamEfDatEntry* e = donor_effects(kind);
    int idx;
    size_t size, length = 0;
    u8* buf;
    HSD_Archive* arc;
    BamEfDatEntry* table;
    if (!e || e->data) return 1;
    idx = (int) (e - BAM_EF_ENTRIES);
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
        BAM_NOTE("memory: donor effects kind=%d do not fit\n", kind);
        return 0;
    }
    efAsync_LoadSync(idx);
    return 1;
}
