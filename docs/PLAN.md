# Plan

Goal: build a fighter (any character's four specials and five aerials on any
base character) and play it against a friend over Slippi Online. Decisions
taken so far:

* **Slippi first.** Everything has to work in Slippi Direct with rollback.
  Game ID stays `GALE01`; the retail executable keeps its layout (see
  ARCHITECTURE.md).
* **Fully automatic loadout exchange.** No build codes typed by hand.
* **As easy as possible for players.** Patch the ISO once, set it in the
  Slippi launcher, pick a build in game, Direct-connect as usual.
* **Fresh repo.** The moveset engine is vendored from rogueMelee-v2; the
  roguelike mode is not carried over.

## Player flow (target)
1. Patched ISO boots with Slippi's required codes and menus intact (GALE01).
2. Slippi's versioned menus, including the Online section.
3. 1-P > Online Play > Versus > Direct (Slippi's own).
4. Select character (Slippi's online CSS).
5. **Move-select screen**: choose special moves and aerials from other
   characters (the rogueMelee Training-mode build menu, ported).
6. Lock in the moveset.
7. Start -> Slippi's built-in direct-connect code entry.
8. Both players ready -> match starts through Slippi matchmaking; loadouts
   are exchanged automatically (see phase 2).

## Phases

### 0. Boot spike — done in-repo, awaiting first boot on real Slippi Dolphin
* Pristine decomp reproduces retail DOL (verified).
* Overlay link against absolute retail symbols (verified by disassembly).
* `ClearArena`/`HSD_OSInit` overrides, `Fighter_Create` inject hook, DOL and
  ISO assembly, Slippi conflict check. Smoke-test marker: 1.5x fighters.
* **Open:** confirm the heap really starts at `0x80BD5C40` on Slippi Dolphin
  (the override panics with the actual value if not).

### 1. Engine port (local play)
* Vendor `specials/` (special_runtime, registry, preload, aerial_runtime,
  anim_scale, sword_visual, rest_sleep, transform) and the parts of
  `melee_fighter.c` the engine needs.
* Replace `RogueDirector_Run()->specials/aerials` and `Rogue_IsBuildFighter`
  with a **Loadout per player slot** (`BAM_Loadout` × 6 slots).
* Per-fighter engine state (`RogueFighterState`) moves to a match-heap block
  pointed to from a Fighter struct extension slot; `rogue_donor_attrs` too.
  `source_vars` shrinks to the ≤9 equipped donors.
* Apply the 1,026 pinned `special_adapters` edits through the override
  pipeline (recompiled units, byte-compared, entry patches).
* Debug hotkeys to assign loadouts in VS mode; Dolphin movie determinism test.
* Drop everything rogue reimplemented from Slippi's code list (UCF, wobbling,
  costume bound check, nametags, unlock-all): Slippi provides them.

### 2. Automatic loadout exchange (the hard part)
Slippi's selection message is a fixed 9-byte struct with no spare field, so
the loadout rides on channels Slippi already synchronises:

* **Primary: match-start input channel.** Slippi transmits the raw 8-byte
  pad every frame and rolls it back correctly. For the first N frames of a
  match (entry animations, inputs are not actionable) each side replaces its
  local pad with an encoded loadout + CRC; both sides decode the opponent's
  loadout from the received pad stream. Because remote inputs arrive late,
  the decode result is only *used* once Slippi has finalised those frames,
  which happens long before the first special can be thrown.
* **Prerequisite: preload independence.** Donor assets must be resident before
  the opponent's loadout is known, so match setup loads every donor's core
  `PlXx.dat` (≈26 × ~100 KB) and defers only the per-move animation slices
  and prop models. Phase 1 measures whether a 1v1 match has the room; if it
  does not, the fallback is the Slippi chat-message byte channel during the
  CSS (requires both players' chat enabled) with a tiny ack protocol.
* **Verification.** A loadout CRC exchanged the same way makes any mismatch a
  deterministic abort to the CSS rather than a desync minutes later.

### 3. Build-A-Fighter UI
* Builder screen: base character, 4 specials, 5 aerials; Training-mode
  preview (rogue_practice.c does this already); saved builds on the memory
  card; current build shown in the Slippi CSS.

### 4. Online hardening
* Two-instance Slippi netplay harness (rollback with induced latency),
  per-frame cost of animation retargeting under rollback re-simulation,
  memory budget per donor count, replay playback (embed loadout in the game
  info block so `.slp` files show the right moves).

### 5. Later
* Doubles, upgrades/passives online, balance rules, porting the roguelike
  mode onto the same framework.
