/* Builds in Slippi replays.
 *
 * A Slippi replay records the match setup block StartMelee is given (0x138
 * bytes: rules, then six 0x24-byte player entries) and plays a replay back
 * by re-running the match from the recorded inputs. Playback restores that
 * block (Slippi's RestoreGameInfo, fn_8016E730+0x18) but never visits the
 * character select screen, so without help every fighter would play its own
 * moves.
 *
 * Versus matches use player entries 0..3 only; entries 4 and 5 are empty
 * (slot type NA) and nothing reads past their first two bytes. So the
 * builds ride in those spare bytes (2 x 34):
 *
 *   fn_8016E730+0x14  BAM_ReplayBuildsWrite: before Slippi records the
 *                     block (+0x1C) or restores it (+0x18), put every
 *                     player's build there as share codes ("BAMR", a
 *                     version, then four 22-symbol codes, 5 bits each).
 *   fn_8016E730+0x20  BAM_ReplayBuildsRead: after both, put the spare bytes
 *                     back as they were. If they no longer hold what was
 *                     written, playback restored a recorded block: load the
 *                     builds it carries for this match.
 *
 * Slippi Online copies the match block both players agreed on over r31
 * (InitOnlinePlay, at +0x18) after the write above, wiping the builds before
 * recording. Its code then calls GObj_Create, so an inject there writes them
 * again while a write is pending.
 *
 * The share code's check symbol covers the move catalogs, so a replay from a
 * build with different catalogs plays native moves rather than wrong ones.
 */
#pragma optimize_for_size on
#pragma auto_inline off
#include "build_code.h"
#include <string.h>

#define PLAYERS 0x60    /* first player entry in the block */
#define ENTRY 0x24      /* bytes per player entry */
#define SLOT_TYPE 1     /* entry byte: Gm_PKind */
#define PKIND_NA 3
#define SPARE_ENTRY 4   /* entries 4 and 5 carry the builds */
#define SPARE_SKIP 2    /* their character and slot type stay */
#define SPARE_PER (ENTRY - SPARE_SKIP)
#define STASH (2 * SPARE_PER)
#define HEADER 5        /* "BAMR" + version */
#define VERSION 1
#define PACKED ((BAM_CODE_LEN * 5 + 7) / 8)

static const u8 magic[4] = { 'B', 'A', 'M', 'R' };
static u8 original[STASH], written[STASH];
static int have_original;
static u8* pending; /* block between the write and read hooks */
/* The players' own builds while a replay's are loaded. */
static BamLoadout before[BAM_PLAYER_SLOTS];
static int replay_loaded;

static u8* spare(u8* data, unsigned i)
{
    return data + PLAYERS + (SPARE_ENTRY + i / SPARE_PER) * ENTRY + SPARE_SKIP + i % SPARE_PER;
}

static void spare_get(u8* data, u8* buf)
{
    unsigned i;
    for (i = 0; i < STASH; ++i) buf[i] = *spare(data, i);
}

static void spare_put(u8* data, const u8* buf)
{
    unsigned i;
    for (i = 0; i < STASH; ++i) *spare(data, i) = buf[i];
}

static void encode(u8* buf)
{
    unsigned char code[BAM_CODE_LEN];
    unsigned p, i, bit;
    memset(buf, 0, STASH);
    memcpy(buf, magic, sizeof(magic));
    buf[4] = VERSION;
    for (p = 0; p < BAM_PLAYER_SLOTS; ++p) {
        u8* out = buf + HEADER + p * PACKED;
        Bam_CodeFromLoadout(&bam_loadouts[p], code);
        for (i = 0, bit = 0; i < BAM_CODE_LEN; ++i, bit += 5) {
            unsigned v = (code[i] & 31) << (11 - bit % 8);
            out[bit / 8] |= (u8) (v >> 8);
            out[bit / 8 + 1] |= (u8) v;
        }
    }
}

static void decode(const u8* in, unsigned char* code)
{
    unsigned i, bit;
    for (i = 0, bit = 0; i < BAM_CODE_LEN; ++i, bit += 5) {
        unsigned v = ((unsigned) in[bit / 8] << 8) | (bit / 8 + 1 < PACKED ? in[bit / 8 + 1] : 0);
        code[i] = (unsigned char) ((v >> (11 - bit % 8)) & 31);
    }
}

/* inject fn_8016E730+0x14 (r31 = StartMeleeData). */
void BAM_ReplayBuildsWrite(u8* data)
{
    have_original = 0;
    if (!data || data[PLAYERS + SPARE_ENTRY * ENTRY + SLOT_TYPE] != PKIND_NA ||
        data[PLAYERS + (SPARE_ENTRY + 1) * ENTRY + SLOT_TYPE] != PKIND_NA)
        return;
    spare_get(data, original);
    encode(written);
    spare_put(data, written);
    have_original = 1;
    pending = data;
}

/* inject GObj_Create: Slippi Online's match setup calls it after copying the
 * agreed block over ours. Write the builds again if they are gone. */
void BAM_ReplayBuildsRewrite(void)
{
    u8 got[STASH];
    u8* data = pending;
    /* Online only (scene 8); a replay's restored builds are never replaced. */
    if (!data || *(volatile u8*) 0x80479D30 != 8) return;
    spare_get(data, got);
    if (!memcmp(got, written, STASH) || !memcmp(got, magic, sizeof(magic))) return;
    if (data[PLAYERS + SPARE_ENTRY * ENTRY + SLOT_TYPE] != PKIND_NA ||
        data[PLAYERS + (SPARE_ENTRY + 1) * ENTRY + SLOT_TYPE] != PKIND_NA)
        return;
    memcpy(original, got, STASH);
    encode(written);
    spare_put(data, written);
}

/* inject fn_8016E730+0x20 (r31 = StartMeleeData), after Slippi's playback
 * restore and recording, before the match reads the block. */
void BAM_ReplayBuildsRead(u8* data)
{
    u8 got[STASH];
    unsigned p;
    pending = 0;
    if (!data || !have_original) return;
    have_original = 0;
    spare_get(data, got);
    spare_put(data, original);
    if (!memcmp(got, written, STASH)) return; /* a live match: as written */
    if (memcmp(got, magic, sizeof(magic)) || got[4] != VERSION) {
        BAM_NOTE("replay: no builds recorded; everyone plays their own moves\n");
        return;
    }
    if (!replay_loaded) memcpy(before, bam_loadouts, sizeof(before));
    replay_loaded = 1;
    for (p = 0; p < BAM_PLAYER_SLOTS; ++p) {
        unsigned char code[BAM_CODE_LEN];
        char text[BAM_CODE_TEXT];
        decode(got + HEADER + p * PACKED, code);
        Bam_CodeText(code, text);
        if (Bam_LoadoutFromCode(code, &bam_loadouts[p])) {
            BAM_NOTE("replay: P%u build %s\n", p + 1, text);
        } else {
            Bam_LoadoutClear(&bam_loadouts[p]);
            BAM_NOTE("replay: P%u build %s is from another version; own moves\n", p + 1, text);
        }
    }
}

/* Scene exit: the players get their own builds back. */
void Bam_ReplayBuildsSceneExit(void)
{
    if (!replay_loaded) return;
    replay_loaded = 0;
    memcpy(bam_loadouts, before, sizeof(before));
}
