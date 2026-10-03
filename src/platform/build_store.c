/* Saved builds on the memory card.
 *
 * Slippi Dolphin has no memory card in Slot A by default; players who turn
 * one on get their three slots back after a restart. The slots live in one
 * file of our own ("BuildAMelee builds", one 8 KB block), never in Melee's
 * save. Card I/O runs synchronously when the character select screen opens
 * (read, once per boot) and when a slot is saved or deleted (write), with a
 * 48 KB work area borrowed from the scene heap for the operation only; if
 * the heap cannot spare it, the slots stay in memory and the player is told.
 */
/* Menu code: smaller beats faster (the overlay has a fixed size). */
#pragma optimize_for_size on
#pragma auto_inline off
#include "build_store.h"
#include <dolphin/card.h>
#include <dolphin/os.h>
#include <sysdolphin/baselib/memory.h>
#include <string.h>

/* SDK calls in the retail executable without prototypes in card.h. */
s32 CARDProbeEx(s32 chan, s32* mem_size, s32* sector_size);
s32 CARDMountAsync(s32 chan, void* work_area, CARDCallback detach, CARDCallback attach);
s32 CARDUnmount(s32 chan);
s32 CARDOpen(s32 chan, const char* name, CARDFileInfo* info);
s32 CARDCreateAsync(s32 chan, const char* name, u32 size, CARDFileInfo* info, CARDCallback cb);
s32 CARDRead(CARDFileInfo* info, void* buf, s32 length, s32 offset);
s32 CARDWrite(CARDFileInfo* info, void* buf, s32 length, s32 offset);
s32 CARDClose(CARDFileInfo* info);
s32 __CARDSync(s32 chan);
void __CARDSyncCallback(s32 chan, s32 result);
OSHeapHandle HSD_GetHeap(void);

#define CHAN 0 /* Slot A; Slot B is Slippi's device */
#define FILE_NAME "BuildAMelee builds"
#define MAGIC 0x42414D53 /* "BAMS" */
#define SECTOR 0x2000

BamSavedSlot bam_saved[BAM_SAVE_SLOTS];

typedef struct CardImage {
    u32 magic;
    u8 version, count, pad[2];
    BamSavedSlot slots[BAM_SAVE_SLOTS];
    u32 sum;
} CardImage;

static int inited;
int bam_store_on_card;

static u32 image_sum(const CardImage* im)
{
    const u8* p = (const u8*) im;
    unsigned n = (unsigned) ((const u8*) &im->sum - p);
    u32 s = 0x9E3779B9U;
    while (n--) s = ((s << 5) | (s >> 27)) ^ *p++;
    return s;
}

/* Work area (CARD_WORKAREA_SIZE) followed by one sector, 32-byte aligned.
 * OSAllocFromHeap returns NULL when the heap is short (HSD_MemAlloc would
 * stop the game instead). */
static void* take(u8** work, u8** sector)
{
    void* raw = OSAllocFromHeap(HSD_GetHeap(), CARD_WORKAREA_SIZE + SECTOR + 32);
    u8* p;
    if (!raw) return NULL;
    p = (u8*) (((u32) raw + 31) & ~31U);
    *work = p;
    *sector = p + CARD_WORKAREA_SIZE;
    return raw;
}

static s32 sync(s32 r) { return r < 0 ? r : __CARDSync(CHAN); }

static s32 mount(u8* work)
{
    s32 mem, sector, r = CARDProbeEx(CHAN, &mem, &sector);
    if (r < 0) return r;
    if (sector != SECTOR) return CARD_RESULT_WRONGDEVICE;
    r = sync(CARDMountAsync(CHAN, work, NULL, __CARDSyncCallback));
    if (r == CARD_RESULT_READY || r == CARD_RESULT_BROKEN) r = sync(CARDCheckAsync(CHAN, __CARDSyncCallback));
    if (r < 0) CARDUnmount(CHAN);
    return r;
}

void Bam_StoreInit(void)
{
    s32 mem, sector, r;
    u8 *work, *buf;
    void* raw;
    CARDFileInfo fi;
    if (inited) return;
    inited = 1;
    r = CARDProbeEx(CHAN, &mem, &sector);
    BAM_LOG("store: slot A probe %d\n", (int) r);
    if (r < 0) return; /* no card: slots live in memory only */
    raw = take(&work, &buf);
    if (!raw) { BAM_LOG("store: no memory to read the card\n"); inited = 0; return; }
    r = mount(work);
    if (r == CARD_RESULT_READY) {
        r = CARDOpen(CHAN, FILE_NAME, &fi);
        if (r == CARD_RESULT_READY) {
            r = CARDRead(&fi, buf, CARD_READ_SIZE, 0);
            CARDClose(&fi);
            if (r == CARD_RESULT_READY) {
                const CardImage* im = (const CardImage*) buf;
                if (im->magic == MAGIC && im->version == 1 && im->count == BAM_SAVE_SLOTS && im->sum == image_sum(im)) {
                    memcpy(bam_saved, im->slots, sizeof(bam_saved));
                    BAM_LOG("store: loaded %u-slot file from the card\n", (unsigned) im->count);
                } else {
                    BAM_LOG("store: card file unreadable, ignored\n");
                }
            }
        }
        CARDUnmount(CHAN);
    }
    BAM_LOG("store: read result %d\n", (int) r);
    bam_store_on_card = r == CARD_RESULT_READY || r == CARD_RESULT_NOFILE;
    HSD_Free(raw);
}

int Bam_StoreSave(void)
{
    s32 r;
    u8 *work, *buf;
    void* raw;
    CARDFileInfo fi;
    CardImage* im;
    {
        s32 mem, sector;
        if (CARDProbeEx(CHAN, &mem, &sector) < 0) { bam_store_on_card = 0; return BAM_STORE_SESSION; }
    }
    raw = take(&work, &buf);
    if (!raw) return BAM_STORE_NOMEM;
    r = mount(work);
    if (r == CARD_RESULT_READY) {
        r = CARDOpen(CHAN, FILE_NAME, &fi);
        if (r == CARD_RESULT_NOFILE) r = sync(CARDCreateAsync(CHAN, FILE_NAME, SECTOR, &fi, __CARDSyncCallback));
        if (r == CARD_RESULT_READY) {
            memset(buf, 0, SECTOR);
            im = (CardImage*) buf;
            im->magic = MAGIC;
            im->version = 1;
            im->count = BAM_SAVE_SLOTS;
            memcpy(im->slots, bam_saved, sizeof(bam_saved));
            im->sum = image_sum(im);
            r = CARDWrite(&fi, buf, SECTOR, 0);
            CARDClose(&fi);
        }
        CARDUnmount(CHAN);
    }
    HSD_Free(raw);
    BAM_LOG("store: write result %d\n", (int) r);
    bam_store_on_card = r == CARD_RESULT_READY;
    if (r == CARD_RESULT_READY) return BAM_STORE_CARD;
    return r == CARD_RESULT_NOCARD ? BAM_STORE_SESSION : BAM_STORE_ERROR;
}
