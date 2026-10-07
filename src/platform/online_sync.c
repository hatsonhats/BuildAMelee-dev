/* Slippi Online: exchange build loadouts so both clients run the same match.
 *
 * Slippi only carries pad inputs and a few selections between the players,
 * so a build (4 specials, 5 aerials, 12 ground attacks and throws) is sent
 * through Slippi's quick-chat
 * channel, which Dolphin forwards as one byte per message but only accepts
 * for its 16 chat IDs. Each message carries 3 bits of data plus a 1-bit
 * sequence number. The player in match slot 0 sends, the other answers each
 * message with one of its own (strict ping-pong): only one message is ever in
 * flight, so Dolphin's single "last chat received" slot never overwrites one.
 * A message lost anyway (Slippi's own CSS code polling it away) is resent
 * after a timeout; the sequence bit makes duplicates harmless.
 *
 *   Phase 1 (scene enter, before fighters exist): build hash + loadout.
 *   Phase 2 (after the scene created the fighters): which borrowed moves each
 *            client actually loaded (memory can run out on one client
 *            only). A move is kept only if it loaded on both.
 *   FIN: the slot-0 player confirms it got the last answer.
 *
 * Both clients block in a loop while exchanging (on the loading screen, a
 * second or two). Any failure (opponent without the mod, quick chat turned
 * off, different BuildAMelee build, timeout) makes both players play their
 * characters' own moves; the cases are symmetric so both agree.
 *
 * Only Slippi Direct 1v1 exchanges. Other online modes play retail moves.
 *
 * Called from: hooks.c (scene enter, ready, exit).
 * State: the exchange in progress (scene setup, before the match).
 */
#include <bam/bam.h>
#include <bam/retail.h>
#include <engine/internal.h>
#include <engine/catalog.h>
#include <dolphin/os.h>
#include <string.h>
#include <sysdolphin/baselib/memory.h>
#include "build_code.h"

#define CMD_SEND_CHAT 0xBB
#define EXI_WRITE 1
/* MSRB offsets (Online.s). */
#define MSRB_LOCAL_INDEX 3
#define MSRB_REMOTE_INDEX 4
#define MSRB_SENT_CHAT 10
#define MSRB_OPP_CHAT 11
/* Scene controller and Slippi's selected online mode (r13 - 0x5060). */
#define SCENE_ONLINE_IN_GAME 2
#define ONLINE_MODE_DIRECT 2
#define CHAT_DISABLED 0x10

extern const char bam_build_id[];
extern int bam_css_port;

/* What happened to the last online match's build exchange, shown on the
 * character select screen afterwards (css_menu.c). */
int bam_online_notice;
static int chat_off;

/* Slippi match-state buffer, from the scene heap only while exchanging. */
static u8* msrb;
#define MSRB_ALLOC 0x800
static u8 cmd[32] __attribute__((aligned(32)));
static const u8 chat_ids[16] = {
    0x81, 0x82, 0x84, 0x88, 0x11, 0x12, 0x14, 0x18,
    0x21, 0x22, 0x24, 0x28, 0x41, 0x42, 0x44, 0x48,
};

/* Payloads in 3-bit symbols. */
#define SLOT_BITS 5
#define ALL_SLOTS (BAM_SPECIAL_SLOTS + BAM_AERIAL_SLOTS + BAM_NORMAL_SLOTS)
#define P1_BITS (8 + ALL_SLOTS * SLOT_BITS)
#define P1_SYMS ((P1_BITS + 2) / 3)
#define P2_BITS (2 * ALL_SLOTS)
#define P2_SYMS ((P2_BITS + 2) / 3)

static struct {
    int active;            /* this scene is an online match we manage */
    int ok;                /* phase 1 succeeded */
    int initiator;
    int local, remote;     /* match slots */
    unsigned k;            /* next message number */
    u8 last_reply;         /* responder: last answer, resent on duplicates */
    int have_reply;
    BamLoadout saved[BAM_PLAYER_SLOTS];
} sync;

static u32 ticks_per_ms(void)
{
    return BAM_TICKS_PER_MS;
}

static u32 now_ms(void)
{
    return (u32) (OSGetTime() / ticks_per_ms());
}

static void wait_ms(u32 ms)
{
    u32 t = now_ms();
    while (now_ms() - t < ms) {}
}

static void send_msg(u8 id)
{
    cmd[0] = CMD_SEND_CHAT;
    cmd[1] = id;
    cmd[2] = 0;
    BAM_SLIPPI_EXI_TRANSFER(cmd, 3, EXI_WRITE);
}

/* Returns a message index 0..15, -1 none, -2 opponent has chat disabled. */
static int poll_msg(void)
{
    int i;
    u8 id;
    BAM_SLIPPI_LOAD_MATCH_STATE(msrb);
    id = msrb[MSRB_OPP_CHAT];
    if (!id) return -1;
    if (id == CHAT_DISABLED) return -2;
    for (i = 0; i < 16; ++i)
        if (chat_ids[i] == id) return i;
    return -1;
}

static u8 make_msg(unsigned k, unsigned data) { return chat_ids[((k & 1) << 3) | (data & 7)]; }
#define MSG_SEQ(m) (((m) >> 3) & 1)
#define MSG_DATA(m) ((m) & 7)

#define RESEND_MS 300
#define CONTACT_MS 10000   /* first message from the opponent */
#define PROGRESS_MS 8000   /* any later message */

/* Exchanges n symbols each way. Returns 1 on success. */
static int run_phase(const u8* out, u8* in, int n, int first)
{
    int i;
    u32 limit = first ? CONTACT_MS : PROGRESS_MS;
    for (i = 0; i < n; ++i) {
        unsigned k = sync.k;
        u32 start = now_ms(), sent = 0;
        int got = 0;
        if (sync.initiator) { send_msg(make_msg(k, out[i])); sent = now_ms(); }
        while (!got) {
            int m;
            wait_ms(2);
            m = poll_msg();
            if (m == -2) { BAM_NOTE("online: opponent has quick chat off\n"); chat_off = 1; return 0; }
            if (m >= 0) {
                if ((unsigned) MSG_SEQ(m) == (k & 1)) {
                    in[i] = (u8) MSG_DATA(m);
                    if (!sync.initiator) {
                        sync.last_reply = make_msg(k, out[i]);
                        sync.have_reply = 1;
                        send_msg(sync.last_reply);
                    }
                    got = 1;
                    break;
                }
                /* The other parity: a resent previous message (our answer
                 * got lost). Answer it again. */
                if (!sync.initiator && sync.have_reply) send_msg(sync.last_reply);
            }
            if (sync.initiator && now_ms() - sent >= RESEND_MS) {
                send_msg(make_msg(k, out[i]));
                sent = now_ms();
            }
            if (now_ms() - start >= limit) {
                BAM_NOTE("online: timeout at message %u\n", k);
                return 0;
            }
        }
        sync.k = k + 1;
        limit = PROGRESS_MS;
    }
    return 1;
}

/* After the last phase: the initiator tells the responder it is done; the
 * responder keeps answering resends until then. */
static void finish(void)
{
    if (sync.initiator) {
        int i;
        for (i = 0; i < 3; ++i) { send_msg(make_msg(sync.k, 0)); wait_ms(30); }
    } else {
        u32 start = now_ms();
        while (now_ms() - start < 2000) {
            int m;
            wait_ms(2);
            m = poll_msg();
            if (m < 0) continue;
            if ((unsigned) MSG_SEQ(m) == (sync.k & 1)) break;
            if (sync.have_reply) send_msg(sync.last_reply);
        }
    }
    ++sync.k;
}

/* ---- bit packing -------------------------------------------------------- */

static void put_bits(u8* syms, unsigned* pos, unsigned value, unsigned bits)
{
    while (bits--) {
        unsigned b = (value >> bits) & 1;
        syms[*pos / 3] |= (u8) (b << (2 - *pos % 3));
        ++*pos;
    }
}

static unsigned get_bits(const u8* syms, unsigned* pos, unsigned bits)
{
    unsigned v = 0;
    while (bits--) {
        v = (v << 1) | ((syms[*pos / 3] >> (2 - *pos % 3)) & 1);
        ++*pos;
    }
    return v;
}

/* Moves travel as their position among the catalog's moves for that slot
 * (1-based; 0 = own move), which fits 5 bits (build_code.c). */
#define special_index Bam_SpecialIndex
#define special_from_index Bam_SpecialFromIndex
#define aerial_index Bam_AerialIndex
#define aerial_from_index Bam_AerialFromIndex

static unsigned build_hash(void)
{
    unsigned h = 2166136261U;
    const char* s;
    for (s = bam_build_id; *s; ++s) h = (h ^ (u8) *s) * 16777619U;
    return (h ^ (h >> 8) ^ (h >> 16) ^ (h >> 24)) & 0xFF;
}

static void encode_loadout(const BamLoadout* l, u8* syms)
{
    unsigned pos = 0, i;
    memset(syms, 0, P1_SYMS);
    put_bits(syms, &pos, build_hash(), 8);
    for (i = 0; i < BAM_SPECIAL_SLOTS; ++i)
        put_bits(syms, &pos, l->enabled ? special_index(i, l->specials[i]) : 0, SLOT_BITS);
    for (i = 0; i < BAM_AERIAL_SLOTS; ++i)
        put_bits(syms, &pos, l->enabled ? aerial_index(i, l->aerials[i]) : 0, SLOT_BITS);
    /* Ground attacks and throws: the donor's CharacterKind + 1 (0..26). */
    for (i = 0; i < BAM_NORMAL_SLOTS; ++i)
        put_bits(syms, &pos, l->enabled && l->normals[i] <= 26 ? l->normals[i] : 0, SLOT_BITS);
}

/* Returns 0 if the opponent runs a different build. */
static int decode_loadout(const u8* syms, BamLoadout* l)
{
    unsigned pos = 0, i, any = 0;
    if (get_bits(syms, &pos, 8) != build_hash()) return 0;
    memset(l, 0, sizeof(*l));
    for (i = 0; i < BAM_SPECIAL_SLOTS; ++i) {
        l->specials[i] = (u8) special_from_index(i, get_bits(syms, &pos, SLOT_BITS));
        any |= l->specials[i];
    }
    for (i = 0; i < BAM_AERIAL_SLOTS; ++i) {
        l->aerials[i] = (u8) aerial_from_index(i, get_bits(syms, &pos, SLOT_BITS));
        any |= l->aerials[i];
    }
    for (i = 0; i < BAM_NORMAL_SLOTS; ++i) {
        unsigned v = get_bits(syms, &pos, SLOT_BITS);
        l->normals[i] = (u8) (v <= 26 ? v : 0);
        any |= l->normals[i];
    }
    l->enabled = any != 0;
    return 1;
}

static void log_loadout(const char* who, int slot, const BamLoadout* l)
{
    BAM_LOG("online %s slot=%d enabled=%d specials=%u,%u,%u,%u aerials=%u,%u,%u,%u,%u\n", who, slot,
            l->enabled, l->specials[0], l->specials[1], l->specials[2], l->specials[3],
            l->aerials[0], l->aerials[1], l->aerials[2], l->aerials[3], l->aerials[4]);
    BAM_LOG("online %s slot=%d normals=%u,%u,%u,%u,%u %u,%u,%u %u,%u,%u,%u\n", who, slot,
            l->normals[0], l->normals[1], l->normals[2], l->normals[3], l->normals[4], l->normals[5],
            l->normals[6], l->normals[7], l->normals[8], l->normals[9], l->normals[10], l->normals[11]);
}

static void clear_all(void)
{
    int p;
    for (p = 0; p < BAM_PLAYER_SLOTS; ++p) Bam_LoadoutClear(&bam_loadouts[p]);
}

/* ---- scene hooks ---------------------------------------------------------- */

static int is_online_match(void)
{
    return BAM_SCENE_MAJOR == BAM_SCENE_ONLINE && BAM_SCENE_MINOR == SCENE_ONLINE_IN_GAME;
}

/* Scene enter (before the scene creates the stage and fighters). */
static void scene_enter(void)
{
    BamLoadout mine, theirs;
    u8 out[P1_SYMS], in[P1_SYMS];
    int port;
    sync.active = sync.ok = 0;
    if (!is_online_match()) return;
    sync.active = 1;
    bam_online_notice = BAM_NOTICE_NONE;
    chat_off = 0;
    memcpy(sync.saved, bam_loadouts, sizeof(sync.saved));
    port = bam_css_port >= 0 && bam_css_port < BAM_PLAYER_SLOTS ? bam_css_port : 0;
    mine = bam_loadouts[port];
    clear_all();
    if (BAM_SLIPPI_ONLINE_MODE != ONLINE_MODE_DIRECT) {
        BAM_NOTE("online: mode %d is not Direct; everyone plays their own moves\n", BAM_SLIPPI_ONLINE_MODE);
        return;
    }
    BAM_SLIPPI_LOAD_MATCH_STATE(msrb);
    sync.local = msrb[MSRB_LOCAL_INDEX];
    sync.remote = msrb[MSRB_REMOTE_INDEX];
    if (sync.local > 1 || sync.remote > 1 || sync.local == sync.remote) {
        BAM_NOTE("online: unexpected player slots %d/%d\n", sync.local, sync.remote);
        return;
    }
    sync.initiator = sync.local == 0;
    sync.k = 0;
    sync.have_reply = 0;
    /* Drop whatever chat is still pending from the CSS. */
    { int i; for (i = 0; i < 3; ++i) { poll_msg(); wait_ms(5); } }
    encode_loadout(&mine, out);
    log_loadout("mine", sync.local, &mine);
    BAM_NOTE("online: exchanging builds (%s, build %s)\n", sync.initiator ? "first" : "second", bam_build_id);
    {
        u32 t = now_ms();
        if (!run_phase(out, in, P1_SYMS, 1)) {
            BAM_NOTE("online: build exchange failed; everyone plays their own moves\n");
            bam_online_notice = chat_off ? BAM_NOTICE_CHAT_OFF : BAM_NOTICE_NO_ANSWER;
            return;
        }
        BAM_NOTE("online: builds exchanged in %u ms\n", now_ms() - t);
    }
    if (!decode_loadout(in, &theirs)) {
        BAM_NOTE("online: opponent runs a different BuildAMelee build; everyone plays their own moves\n");
        bam_online_notice = BAM_NOTICE_OTHER_VERSION;
        /* They see the same mismatch. Still finish the handshake. */
        finish();
        return;
    }
    log_loadout("theirs", sync.remote, &theirs);
    bam_loadouts[sync.local] = mine;
    bam_loadouts[sync.remote] = theirs;
    sync.ok = 1;
    if (!mine.enabled && !theirs.enabled) { finish(); sync.ok = 0; }
}

/* Which equipped moves of player slot p actually loaded here. */
static unsigned loaded_mask(int p)
{
    unsigned mask = 0, s;
    BamFighterState* S;
    if (!bam_match) return 0;
    S = &bam_match->fighters[p * 2];
    if (!S->fighter) return 0;
    for (s = 0; s < BAM_SPECIAL_SLOTS; ++s) {
        unsigned id = S->specials[s];
        if (id && id < BAM_SPECIAL_ID_COUNT && S->loaded[id]) mask |= 1U << s;
    }
    for (s = 0; s < BAM_AERIAL_SLOTS; ++s)
        if (S->aerial_equipped[s]) mask |= 1U << (BAM_SPECIAL_SLOTS + s);
    for (s = 0; s < BAM_NORMAL_SLOTS; ++s)
        if (S->normals[s]) mask |= 1U << (BAM_SPECIAL_SLOTS + BAM_AERIAL_SLOTS + s);
    return mask;
}

static void apply_mask(int p, unsigned mask)
{
    unsigned s;
    int f;
    BamLoadout* l = &bam_loadouts[p];
    for (s = 0; s < BAM_SPECIAL_SLOTS; ++s)
        if (!(mask & (1U << s))) l->specials[s] = 0;
    for (s = 0; s < BAM_AERIAL_SLOTS; ++s)
        if (!(mask & (1U << (BAM_SPECIAL_SLOTS + s)))) l->aerials[s] = 0;
    for (s = 0; s < BAM_NORMAL_SLOTS; ++s)
        if (!(mask & (1U << (BAM_SPECIAL_SLOTS + BAM_AERIAL_SLOTS + s)))) l->normals[s] = 0;
    if (!bam_match) return;
    for (f = p * 2; f < p * 2 + 2; ++f) {
        BamFighterState* S = &bam_match->fighters[f];
        if (!S->fighter) continue;
        for (s = 0; s < BAM_SPECIAL_SLOTS; ++s)
            if (!(mask & (1U << s))) S->specials[s] = 0;
        for (s = 0; s < BAM_AERIAL_SLOTS; ++s)
            if (!(mask & (1U << (BAM_SPECIAL_SLOTS + s)))) {
                S->aerials[s] = 0;
                S->aerial_equipped[s] = 0;
            }
        for (s = 0; s < BAM_NORMAL_SLOTS; ++s)
            if (!(mask & (1U << (BAM_SPECIAL_SLOTS + BAM_AERIAL_SLOTS + s)))) S->normals[s] = 0;
    }
}

/* After the scene's on_enter created the fighters, before the first frame. */
static void scene_ready(void)
{
    u8 out[P2_SYMS], in[P2_SYMS];
    unsigned pos = 0, m0, m1, t0, t1;
    if (!sync.active || !sync.ok) return;
    sync.ok = 0;
    m0 = loaded_mask(0);
    m1 = loaded_mask(1);
    memset(out, 0, sizeof(out));
    put_bits(out, &pos, m0, P2_BITS / 2);
    put_bits(out, &pos, m1, P2_BITS / 2);
    if (!run_phase(out, in, P2_SYMS, 0)) {
        BAM_NOTE("online: loaded-move exchange failed; turning borrowed moves off\n");
        bam_online_notice = BAM_NOTICE_NO_ANSWER;
        apply_mask(0, 0);
        apply_mask(1, 0);
        return;
    }
    finish();
    pos = 0;
    t0 = get_bits(in, &pos, P2_BITS / 2);
    t1 = get_bits(in, &pos, P2_BITS / 2);
    BAM_NOTE("online: loaded masks here %06x/%06x there %06x/%06x\n", m0, m1, t0, t1);
    apply_mask(0, m0 & t0);
    apply_mask(1, m1 & t1);
}

void Bam_OnlineSceneEnter(void)
{
    sync.active = 0;
    if (!is_online_match()) return;
    msrb = HSD_MemAlloc(MSRB_ALLOC);
    if (!msrb) { clear_all(); return; }
    scene_enter();
    HSD_Free(msrb);
    msrb = NULL;
}

void Bam_OnlineSceneReady(void)
{
    if (!sync.active || !sync.ok) return;
    msrb = HSD_MemAlloc(MSRB_ALLOC);
    if (!msrb) {
        BAM_NOTE("online: out of memory for the exchange; turning borrowed moves off\n");
        sync.ok = 0;
        apply_mask(0, 0);
        apply_mask(1, 0);
        return;
    }
    scene_ready();
    HSD_Free(msrb);
    msrb = NULL;
}

/* Scene exit: the CSS gets the player's own builds back. */
void Bam_OnlineSceneExit(void)
{
    if (!sync.active) return;
    sync.active = 0;
    memcpy(bam_loadouts, sync.saved, sizeof(sync.saved));
}
