/* BuildAMelee: shared definitions for overlay code.
 *
 * Overlay code is compiled against the Melee decomp headers and linked at a
 * fixed address inside what retail used as the first bytes of the main HSD
 * heap. Every call into retail resolves to the retail address, so the retail
 * DOL keeps its layout and Slippi's gecko codes keep working.
 *
 * Rules for overlay code (see docs/ARCHITECTURE.md):
 *  - Nothing here is restored by Slippi rollback. Per-match mutable state
 *    must live in the Fighter struct extension or in match-heap allocations.
 *  - No disc reads, OSGetTime or other non-determinism once a match runs.
 */
#ifndef BAM_H
#define BAM_H

#include <dolphin/types.h>
#include <dolphin/os.h>

#ifndef BAM_OVERLAY_BASE
#define BAM_OVERLAY_BASE 0x80BD5C40
#endif
#ifndef BAM_OVERLAY_RESERVE
#define BAM_OVERLAY_RESERVE 0x80000
#endif
#define BAM_OVERLAY_END (BAM_OVERLAY_BASE + BAM_OVERLAY_RESERVE)

#ifndef BAM_VERSION
#define BAM_VERSION "dev"
#endif
/* bam_online_notice (platform/online_sync.c): why the last online match
 * played without builds. */
enum { BAM_NOTICE_NONE, BAM_NOTICE_OTHER_VERSION, BAM_NOTICE_NO_ANSWER, BAM_NOTICE_CHAT_OFF };
extern int bam_online_notice;
#ifndef BAM_BUILD_ID
#define BAM_BUILD_ID "local"
#endif

/* Release builds (BAM_DEBUG 0) log only BAM_NOTE: crashes and freezes,
 * moves dropped for memory, the online build exchange, replays and the
 * version. `bam.py build --debug` (and QA builds) add the BAM_LOG detail
 * and the internal consistency checks. */
#ifndef BAM_DEBUG
#define BAM_DEBUG 0
#endif

#define BAM_NOTE(...) OSReport("[bam] " __VA_ARGS__)
#if BAM_DEBUG
#define BAM_LOG(...) OSReport("[bam] " __VA_ARGS__)
#else
#define BAM_LOG(...) ((void) 0)
#endif

/* Linker-provided end of the overlay image (before trampolines). */
extern char _bam_ovl_end[];

#endif
