/* Freeze reporter. When the game stops advancing frames for 3 seconds (an
 * endless loop, or a wait that never ends), an OS alarm, which interrupts
 * whatever the game thread is doing, writes where it is to dolphin.log:
 * the interrupted PC, LR and the return addresses up the stack. Those map
 * to retail functions (symbols.txt) or overlay ones (build/overlay map).
 * One report per freeze. */
#include <bam/bam.h>
#include <dolphin/os.h>
#include <dolphin/os/OSAlarm.h>
#include <dolphin/os/OSContext.h>

static OSAlarm alarm;
static int started;
static volatile u32 frames;
static u32 seen_frames, still, reported;

static int ram_ptr(u32 p) { return p >= 0x80000000U && p < 0x81800000U && !(p & 3); }

static void check(OSAlarm* a, OSContext* ctx)
{
    (void) a;
    if (frames != seen_frames) {
        seen_frames = frames;
        still = 0;
        reported = 0;
        return;
    }
    if (++still < 3 || reported) return;
    reported = 1;
    OSReport("[bam] FREEZE: no frame for %u s. pc=%08x lr=%08x sp=%08x r3=%08x r4=%08x r5=%08x\n",
             (unsigned) still, (unsigned) ctx->srr0, (unsigned) ctx->lr, (unsigned) ctx->gpr[1],
             (unsigned) ctx->gpr[3], (unsigned) ctx->gpr[4], (unsigned) ctx->gpr[5]);
    {
        u32 sp = ctx->gpr[1];
        int i;
        for (i = 0; i < 16 && ram_ptr(sp); ++i) {
            u32 back = *(u32*) sp;
            if (!ram_ptr(back) || back <= sp) break;
            OSReport("[bam] FREEZE: caller %d %08x\n", i, (unsigned) *(u32*) (back + 4));
            sp = back;
        }
    }
}

/* Every game frame (the frame hook). Starts the alarm on the first one. */
void Bam_WatchdogFrame(void)
{
    ++frames;
    if (!started) {
        started = 1;
        OSCreateAlarm(&alarm);
        OSSetPeriodicAlarm(&alarm, OSGetTime(), OSSecondsToTicks(1), check);
    }
}
