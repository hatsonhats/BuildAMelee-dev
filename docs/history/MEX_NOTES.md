> Historical design notes from the start of the project, kept for context.
> Current state: [../STATUS.md](../STATUS.md).

# What BuildAMelee takes from m-ex

m-ex ([akaneia/m-ex](https://github.com/akaneia/m-ex)) is the framework behind
Akaneia and Beyond Melee, both of which play on Slippi Online. Findings from
applying its DOL patch (`dol patcher/patch.xdelta`) to the retail DOL and
diffing (tools: xdelta3, our dol.py):

## The m-ex "template" is three pieces
1. **A 4-region DOL patch, no new sections:**
   | Address | Change |
   |---|---|
   | `HIOEnumDevices` 0x8032BBC4, 2991 words (~12 KB) | m-ex boot loader, written over dead retail dev-kit code (HIO host I/O never runs on retail) |
   | `HSD_OSInit+0x80` | `bl` into the loader, one instruction after Slippi's `bootloader.gct` hook at `+0x7C` |
   | `main+0xAC` | `HSD_INIT_HEAP_MAX_NUM` 4 -> 5, an extra OS heap for m-ex |
   | `lbFileGetSize+0x38` | file size fix |
2. **`codes.gct` on the disc.** The loader creates its heap after Slippi's
   code list (`OSCreateHeap`), reads `codes.gct` with Melee's own `lbFile`
   loader, applies the codes with its own gecko engine, then invalidates the
   instruction cache. All of m-ex's ~100 asm hooks ship this way.
3. **`MxDt.dat`**: data tables plus C compiled by MexTK into HSD archive
   functions. HSD's archive relocation table makes that code position
   independent, which is how m-ex runs C from a heap at an address that
   changes with Slippi's code list size.

Slippi recognises an m-ex disc by `MxDt.dat` in the filesystem
(`GAMETYPE_MELEE_MEX`) and then allows characters >= 26 in unranked/teams.
No Slippi code (GALE01r2.ini or bootloader.gct) touches m-ex's dead-code
region, which is what lets the two coexist.

## How that maps onto BuildAMelee
| m-ex | BuildAMelee (today) | Notes |
|---|---|---|
| loader in dead HIO code | 36-instruction stub in a staging section at `__ArenaLo` | Ours avoids colliding with m-ex's region, so an m-ex disc could still be layered later. |
| payload in a heap after Slippi's code list, relocated by HSD archive loader | payload linked at a fixed address at the top of the arena (0x81760000), copied once at boot | Fixed address = no relocation step; verified booting in Slippi 3.6.4. Revisit (relocatable `bam.dat` loaded from disc) if the arena top ever proves unstable on some emulator/console. |
| `HSD_INIT_HEAP_MAX_NUM` 4 -> 5 | arena top lowered at `HSD_OSInit+0x0` | Same effect: memory taken before HSD sizes its heaps; Slippi's `+0x7C` hook untouched. |
| Fighter struct extension at 0x800679BC (`Adjust Size.asm`) | planned: inject *after* the size is chosen and add to it | Slippi's `ExtendPlayerBlock` (04 code, size 0x2600) writes the same instruction as m-ex; whichever applies last wins. We never overwrite it. |
| Slippi detection: read instruction at 0x801A5014 (Slippi Online C2 patched in) | to adopt | Tells us at runtime that Slippi Online codes are active. |
| `Slippi Compatibility/*` CSS hooks (`GetFighterNum`, `CSPUpdate`, `SceneCheck`, `SkipSlippiSSS`) | to study for the move-select screen | Shows where Slippi's online CSS calls into the game and what it expects. |
| `Special Move Tables/*` (per-kind special tables via rtoc) | not used | Per character kind; our engine needs per player, so it hooks entry functions instead. |
