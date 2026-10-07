/* Boot-time memory carve-out for the overlay.
 *
 * The overlay is loaded by the DOL loader at BAM_OVERLAY_BASE, which retail
 * Melee would otherwise hand to its main HSD heap. Two retail functions are
 * replaced so the bytes survive and the heap starts above them:
 *
 *  ClearArena  (dolphin/os/OS.c)  zeroes the whole arena during OSInit. The
 *              replacement zeroes everything except [BAM_OVERLAY_BASE,
 *              BAM_OVERLAY_END).
 *  HSD_OSInit  gets a one-instruction injection at its entry that lowers the
 *              arena top by the reserve (BAM_ReserveOverlay). Melee's lbHeap
 *              sizes its persistent heaps from the top with fixed tables, so
 *              only the scene heap shrinks, by exactly the reserve.
 *
 * Slippi's rollback restores the scene heap and retail .data/.bss; the
 * overlay is outside that range by design (it holds code and match-invariant
 * data; per-match state lives in heap blocks, see engine/fighter.h).
 */
#include "../bam/bam.h"
#include <dolphin/os.h>
#include <dolphin/os/OSAlloc.h>
#include <dolphin/os/OSReset.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/objalloc.h>
#include <sysdolphin/baselib/synth.h>
#include <string.h>

#define BOOT_REGION_START (*(u32*) 0x812FDFF0)
#define BOOT_REGION_END (*(u32*) 0x812FDFEC)

/* memset(lo, 0, hi - lo) skipping the overlay. */
static void clear_range(u32 lo, u32 hi)
{
    u32 ovl_lo = BAM_OVERLAY_BASE, ovl_hi = BAM_OVERLAY_END;
    if (hi <= lo) return;
    if (hi <= ovl_lo || lo >= ovl_hi) {
        memset((void*) lo, 0, hi - lo);
        return;
    }
    if (lo < ovl_lo) memset((void*) lo, 0, ovl_lo - lo);
    if (hi > ovl_hi) memset((void*) ovl_hi, 0, hi - ovl_hi);
}

/* override: ClearArena */
void BAM_ClearArena(void)
{
    u32 lo = (u32) OSGetArenaLo();
    u32 hi = (u32) OSGetArenaHi();
    if (OSGetResetCode() != 0x80000000) {
        clear_range(lo, hi);
    } else {
        u32 boot_region_start = BOOT_REGION_START;
        u32 boot_region_end = BOOT_REGION_END;
        if (boot_region_start == 0) {
            clear_range(lo, hi);
        } else if (lo < boot_region_start) {
            if (hi <= boot_region_start) {
                clear_range(lo, hi);
            } else {
                clear_range(lo, boot_region_start);
                if (hi > boot_region_end) clear_range(boot_region_end, hi);
            }
        }
    }
}

void BAM_ShrinkPreloadCache(u32 taken);

/* inject: HSD_OSInit entry (Slippi's gecko loader hooks HSD_OSInit+0x7C and
 * allocates its code list at the arena *bottom*; we must not touch that).
 *
 * Takes the overlay out of the top of the arena before HSD sizes its heaps.
 * The arena top is where the apploader placed the FST, which depends only on
 * the disc header, so BAM_OVERLAY_BASE is a build-time constant checked here:
 *   top >= BASE + RESERVE : fine (any extra above the reserve is left unused)
 *   top <  BASE + RESERVE : the DOL loader already wrote over the FST; stop. */
void BAM_ReserveOverlay(void)
{
    u32 hi = (u32) OSGetArenaHi() & ~31U;
    u32 lo = (u32) OSGetArenaLo();
    if (hi < BAM_OVERLAY_END || lo > BAM_OVERLAY_BASE) {
        BAM_NOTE("boot: FATAL: arena %08x-%08x cannot hold the overlay at %08x-%08x\n",
                 (unsigned) lo, (unsigned) hi, (unsigned) BAM_OVERLAY_BASE, (unsigned) BAM_OVERLAY_END);
        OSPanic(__FILE__, __LINE__, "BuildAMelee overlay does not fit the arena");
    }
    OSSetArenaHi((void*) BAM_OVERLAY_BASE);
    BAM_NOTE("boot: overlay %08x-%08x, arena top was %08x, version %s build %s\n",
            (unsigned) BAM_OVERLAY_BASE, (unsigned) BAM_OVERLAY_END, (unsigned) hi, BAM_VERSION, BAM_BUILD_ID);
    BAM_ShrinkPreloadCache(hi - BAM_OVERLAY_BASE);
}

/* Build stamp, compared between online players (platform/online_sync.c). */
const char bam_build_id[] = BAM_BUILD_ID;

/* Melee carves fixed-size persistent heaps out of the main arena
 * (lbHeap_803BA380): a 0x4F8800 preload cache from the bottom and a
 * 0x64B400 character/stage preload cache from the top. The scene heap,
 * which a VS match fills almost completely, gets whatever is left between.
 * Lowering the arena top without compensating takes that memory straight out
 * of the match and Melee hangs loading a VS match.
 *
 * Like m-ex (ShrinkCharacterCache.asm / Relocate Heap Def.asm), take it from
 * the top cache instead, by exactly the amount removed, so the scene heap
 * keeps its retail size. The table is read once, by lbHeap_80015F3C in main()
 * after HSD_InitComponent, so this runs early enough. */
struct lbHeap_HeapDesc { u32 idx, type, prev_idx, size; };
extern struct lbHeap_HeapDesc lbHeap_803BA380[5];

void BAM_ShrinkPreloadCache(u32 taken)
{
    struct lbHeap_HeapDesc* d;
    for (d = lbHeap_803BA380; d->idx != 6; ++d) {
        if (d->idx == 4 && d->type == 2) {
            u32 before = d->size;
            if (taken >= before / 2)
                OSPanic(__FILE__, __LINE__, "BuildAMelee overlay reserve too large for the preload cache");
            d->size = (before - taken) & ~31U;
            BAM_LOG("preload cache %x -> %x (gave %x to the overlay)\n",
                    (unsigned) before, (unsigned) d->size, (unsigned) taken);
            return;
        }
    }
    OSPanic(__FILE__, __LINE__, "lbHeap descriptor table not found");
}
