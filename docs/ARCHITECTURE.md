# Architecture

## Why the game is not relinked

A mod built by rebuilding the whole game from the decomp with its edits, and
letting the linker lay everything out, moves every retail address: every
modified function grows and everything after it shifts. Slippi's code list
is gecko codes at fixed retail addresses, and Slippi Online, replays and
rollback all read fixed addresses too, so such a build needs its own game ID
and its own copies of UCF and friends.

The pinned decomp revision is fully matching: building it unmodified
reproduces `main.dol` byte for byte (SHA-1 `08e0bf20…`). So instead of
relinking the game we treat the decomp as an SDK:

1. The retail DOL is never relinked. Game ID stays `GALE01` rev 2.
2. Our code (`src/`) and recompiled copies of retail translation units are
   linked into an **overlay** at a fixed address, against an absolute symbol
   table generated from `config/GALE01/symbols.txt` (21k globals + unique
   locals). A call to `Fighter_ChangeMotionState` from overlay code is a plain
   `bl` to `0x8006xxxx`.
3. The overlay is added to the DOL as a new text section (retail uses 2 of 7
   text slots). Hooks are word patches in the retail text:
   * `override`: `b overlay_fn` at a retail function entry.
   * `call`: an existing `bl retail_fn` retargeted to an overlay function.
   * `inject`: Gecko-C2 style trampoline that saves r0, r3–r12, LR, CR, CTR,
     XER and f0–f13, calls a C function with chosen registers as arguments,
     restores, runs the displaced instruction and jumps back.
4. For recompiled retail units, only functions whose bytes differ from retail
   (after normalising relative branch targets) get an `override` patch.
   Mutable retail data defined in those units (`.bss`, `.sbss`, `.sdata`,
   named `.data` state) is turned into `extern` so retail code and overlay
   code share one variable; the build refuses a unit that redefines retail
   state.
5. The build refuses any patch whose address Slippi's own code list injects
   into (`slippi/GALE01r2.ini`). Today Slippi's defaults touch 164 retail
   functions; of the 593 functions the moveset engine edits, only
   `Fighter_Create` overlaps, at `+0x54`, so our fighter hook sits at `+0x4F0`.

## Memory map

Retail boot, as the OS and HSD sources show it (and as Slippi's
`bootloader.gct` changes it):

```
0x80003100  .init/.text/.data/... retail sections                 (never moved)
0x804DEC00  end of .sdata2; main stack; db stack; __ArenaLo
            crash-handler buffer, 2 XFBs, GX FIFO                  (fixed)
0x8065CC00  OS heap descriptors
            Slippi's gecko code list  <- bootloader.gct hooks HSD_OSInit+0x7C
            audio heap (512 KB)       <- so everything from here shifts with
            lbHeap persistent heaps      the size of Slippi's code list
            scene heap (Slippi's rollback window = this region)
0x811AD5A0  lbHeap persistent heaps from the top (fixed sizes)
0x81760000  ┌─ BuildAMelee overlay (reserve 0x80000)
0x817E0000  └─ slack
0x817F8AC0  FST (apploader) = arena top; 0x81800000 end of MEM1
```

Nothing near the bottom of the arena has a stable address once Slippi has
loaded its code list, so the overlay lives at the top: `BAM_ReserveOverlay`
(injected at the first instruction of `HSD_OSInit`, before HSD sizes anything)
lowers the arena top to the overlay base. lbHeap sizes its persistent heaps
from fixed tables, so only the scene heap shrinks, by exactly the reserve.
The arena top is where the apploader put the FST, a function of the disc
header alone, which we never change; the boot code tolerates a higher top
(leaves the extra unused) and stops with the real numbers in the log if it
is lower.

`ClearArena` (OS.c) zeroes the whole arena during `OSInit`; it is replaced
by a copy that skips the overlay range.

Slippi's rollback savestate restores exactly `[hsd_heap_next_arena_lo,
hsd_heap_next_arena_hi)` (the scene heap), retail `.data/.bss`, and a fixed
window `0x8065c000–0x8071b000` (heap descriptors, code list, audio heap).
The overlay is therefore **not** rolled back, by design. Rules that follow:

* Everything mutable per match lives in the Fighter struct extension or in
  match-heap allocations made during match setup. Overlay globals are for
  configuration decided before the match (loadouts) and constants.
* Slippi itself extends the Fighter struct (`040679BC 38802600`, size 0x2600).
  Our extension is added *on top of whatever value is there* by hooking after
  that instruction, never by overwriting it.
* No disc reads, `OSGetTime`, or other non-determinism once a match runs.
* Never replace a retail function that Slippi injects into: an `override`
  swallows every injection in the body. The build checks both
  `GALE01r2.ini` and `bootloader.gct` against patch sites and override bodies.

## Slippi compatibility checklist

* Game ID `GALE01`, revision 2, retail apploader, retail FST: Slippi treats
  the disc as NTSC 1.02 and applies `GALE01r2.ini`.
* Direct mode has no ISO hash gate; ranked/unranked are not a target (opponents
  would not have the mod).
* Any Slippi update that moves an injection address is caught by
  `bam.py check-slippi` / the build-time conflict check.

## Build pipeline (`tools/bam/`)

| Module | Role |
| --- | --- |
| `symbols.py` | Parse `symbols.txt`/`splits.txt`; function/unit lookup |
| `toolchain.py` | Find compilers; per-unit retail flags from `build.ninja`; compile/link; map parsing |
| `transform.py` | Pinned line/anchor edits; `externize` for shared retail state |
| `compare.py` | Retail-vs-overlay function diff with branch normalisation |
| `hooks.py` | Hook manifest → word patches and trampolines |
| `dol.py`, `elf.py` | DOL/ELF readers and the DOL writer |
| `iso.py` | Retail image verification; place the grown DOL and add the trimmed donor models to the FST |
| `slippi.py` | Injection addresses from a Slippi code list |
| `build.py` | Orchestration; writes `build/output/main.dol` and `build-report.json` |
| `project.py` | `project.toml`, `config/local.toml`, the fixes in `overrides/fixes/` |
| `hsd.py` | Reads HSD archives (the game's `.dat` files) |
| `bonetables.py` | Bone tables from the ISO into `build/generated/engine/bone_tables.{h,inc}` |
| `donor_notes.py` | Per-character choices the tables and trimmed models use (docs/DONOR_NOTES.md) |
| `parts.py` | Trimmed donor models (`Pl<code>Bm.dat`) added to the patched disc |
| `fingerprint.py` | Layout-independent per-function hashes, to check refactors |
| `release.py` | The player zip (xdelta patch, scripts, README) |

## Source layout (`src/`)

| Path | What |
| --- | --- |
| `boot/` | Overlay entry, arena carve-out (`BAM_ReserveOverlay`, `ClearArena`) |
| `bam/bam.h`, `bam/retail.h` | Shared definitions and logging; named retail and Slippi addresses |
| `engine/` | The borrowed-move engine. `engine.h` is its public header and lists the others: `fighter.h` (loadouts, build fighters, the match block), `catalog.h` (the moves players pick: `catalog_*.c`), `borrow.h` (borrowing a move: `borrow.c`, `donor_specials.c`, `normals.c`, `aerials.c`, `transform.c`, `rest_sleep.c`), `donor_load.h` (a donor's files: `donor_load.c`, `donor_trim.c`, `donor_cache.c`). `state.h` is the per-match state, `internal.h` what only engine files use |
| `engine/anim/` | Posing borrowed animations and placing hitboxes and articles (`anim.h` public, `internal.h` lists the files) |
| `engine/visual/` | Drawing a borrowed move's parts: the donor's model, held weapons, the parasol (`visual.h`) |
| `platform/` | Game-facing features: the build panel (`css_*`), saved builds and share codes, training mode, the online build exchange, replays, hooks and the watchdog |
| `qa/` | The automated move sweep (QA builds only) |

Per-match engine state is allocated on the match heap at match start
(`Bam_MatchBegin`); the build refuses writable globals in `src/engine` that
are not listed with a reason in `project.toml` `[state]`.
