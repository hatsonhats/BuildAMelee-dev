# Building

## Requirements
- Python 3.11+ (`py -3` on Windows), Git, Ninja (only for `fetch`).
- A clean NTSC-U 1.02 ISO. It is read, never written.
- The Melee decomp checkout with its compilers. Either run `bam.py fetch`
  (clones `.cache/melee`, downloads dtk/sjiswrap/compilers/binutils through
  the decomp's own `tools/download_tool.py`, builds and verifies the retail
  DOL) or point `paths.decomp` in `config/local.toml` at an existing checkout
  of the same revision.
- On Linux/macOS the Windows compilers run through `wibo` (fetched too).

## Commands
```
py -3 tools\bam.py doctor               # paths and toolchain check
py -3 tools\bam.py build --auto-iso     # build/output/main.dol + BuildAMelee.iso
py -3 tools\bam.py iso --quick          # ISO from dist/main.dol only (no compiler)
py -3 tools\bam.py check-slippi         # Slippi injections inside functions we touch
py -3 tools\bam.py info                 # retail DOL layout
py -3 tools\bam.py edits [FILTER]       # every edit to retail code, as a diff
py -3 tools\bam.py gen-tables           # the bone tables alone (the build makes them)
py -3 tools\bam.py fingerprint save F   # before a refactor ...
py -3 tools\bam.py fingerprint diff F   # ... and after: which functions changed
python3 -m unittest discover -s tests   # host tests
```
`scripts\build.bat` and `scripts\make_iso.bat` wrap the two common ones.

## Outputs
- `build/output/main.dol` – patched executable
- `build/output/overlay.map` – link map of the overlay
- `build/output/build-report.json` – every patch (address, old/new word, hook),
  every recompiled function with its retail/overlay address and whether it
  differed, the DOL section table
- `build/overlay/gen/` – generated (edited, externized) copies of retail units
- `build/generated/engine/bone_tables.{h,inc}` – the bone tables, from your ISO

## Adding a hook
Add a `[[hook]]` to `project.toml`. `at` is `Symbol+0xOFF` or a hex address.
`inject` hooks pass registers to a C function (`args = ["r31", "r3"]`,
optional `ret = "r3"`). The build fails if the site is a conditional or
indirect branch, if two hooks share a site, or if Slippi's code list injects
there.

## Changing a retail function
See [overrides/README.md](../overrides/README.md): hooks, fixes (anchor
edits in `overrides/fixes/`) and the engine's line edits, and when to use
which. A recompiled unit is compiled with its retail flags plus `-sdata 0
-sdata2 0`; functions whose bytes differ from retail get entry patches
automatically, and mutable retail data in the unit is externized (shared
with retail) automatically.
