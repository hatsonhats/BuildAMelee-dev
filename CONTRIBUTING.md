# Working on BuildAMelee

Read [README.md](README.md) for setup and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
for how the mod sits inside the game. This page is the common jobs.

## Rules that keep online play working

- **Do not move retail code.** Changes go in through hooks and fixes
  ([overrides/README.md](overrides/README.md)); the build refuses a patch on an
  instruction Slippi's own codes patch.
- **Match state lives on the match heap.** Slippi rollback restores the scene
  heap, not the overlay. Engine state the match reads goes in a block
  allocated at match start (see `AnimScaleState` in
  `src/engine/anim/internal.h`); the build refuses other writable globals in
  `src/engine` unless `project.toml` `[state]` lists them with a reason.
- **Nothing random or timed during a match** (no `OSGetTime`, no disc reads).
- **Space is fixed.** The overlay is ~380 KB; every build prints how much is
  free. Prefer small code; menu code is compiled for size.
- **No game files in the repo.** Anything derived from the ISO is generated at
  build time into `build/`.

## Fixing a borrowed move

1. Reproduce it and get the log: `bam.py build --debug --auto-iso` logs every
   borrowed move, hitbox and article (`[bam]` lines in Dolphin's log).
2. Find where it is handled:
   - posing, rebuilt bones (swords, tails, props), hitbox placement, articles
     held by the fighter, sizes: `src/engine/anim/` (`internal.h` lists the files)
   - the donor's model, held weapons, the parasol: `src/engine/visual/`
   - starting and ending a borrowed move, the donor's variables:
     `src/engine/special_runtime.c`, `normal_runtime.c`, `aerial_runtime.c`
   - loading a donor's files and articles: `src/engine/special_preload.c`
   - something in the retail code of the move itself: a fix in
     `overrides/fixes/` (`bam.py edits <file>` shows what is already changed)
3. If only one character needs it, see [docs/DONOR_NOTES.md](docs/DONOR_NOTES.md)
   for where character cases go, and add yours to its table.
4. `bam.py build`, test in Dolphin, and run the move sweep for the
   characters involved ([docs/QA.md](docs/QA.md)).

## Adding a character case to the bone tables

The bone tables (`build/generated/engine/bone_tables.h`) are read from the
ISO by `tools/bam/bonetables.py`. Choices that cannot be read from the files
(which weapon a character holds, extra meshes, joints posed from the root)
are in `tools/bam/donor_notes.py`; edit them there and rebuild. `bam.py
gen-tables OUT` writes the tables on their own to compare before and after.

## Refactoring

Save a fingerprint before and compare after: the overlay's layout changes,
but each function's instructions should not.

```
py -3 tools\bam.py fingerprint save before.json
... change, build ...
py -3 tools\bam.py fingerprint diff before.json [--rename Old_=New_]
```

Run the host tests too: `python -m unittest discover -s tests`.

## Cutting a release

1. Write `releases/RELEASE-NOTES-vX.Y.md` for players (what changed, known
   issues, install steps; nothing about QA tooling).
2. `bam.py release X.Y` sets the version, builds, and packages
   `build/release/BuildAMelee-vX.Y.zip` (only what a player needs).
3. Unzip it, patch a clean ISO with it, and play the result.
4. Commit, tag `vX.Y`, and attach the zip to a release on the public repo.
5. Add the developer notes to [docs/CHANGELOG.md](docs/CHANGELOG.md).

## Style

- C99 for the Wii 1.7 compiler; four-space indents; names start `Bam_`
  (functions), `bam_` (globals and tables) or `BAM_` (macros).
- Comments say what the code does and why, including what would go wrong
  without it. The history of a fix belongs in the commit message.
- Logging: `BAM_NOTE` reaches every player's log, so keep it for crashes,
  freezes, dropped moves, the online exchange and replays; everything else is
  `BAM_LOG` (debug builds only).
