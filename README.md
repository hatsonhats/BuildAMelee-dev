# BuildAMelee (development)

Source for BuildAMelee: a Melee mod for Slippi where any character can use
any other character's specials, aerials, ground attacks and throws. Players
get it from the public repo's releases as a zip with an xdelta patch; this
repo is where it is built.

## Setup (once)

Python 3.11+ and Git. The Melee decompilation supplies headers, the retail
symbol map and the Metrowerks compilers.

```powershell
copy config\local.example.toml config\local.toml   # point paths.melee_iso at a clean NTSC 1.02 ISO
py -3 tools\bam.py fetch                           # .cache\melee + compilers
py -3 tools\bam.py doctor                          # checks everything
```

## Day to day

```powershell
py -3 tools\bam.py build --auto-iso   # build\output\main.dol and build\output\BuildAMelee.iso
py -3 tools\bam.py build --debug      # the same with the detailed [bam] log and internal checks
py -3 tools\qa\sweep.py              # automated move sweep (docs\QA.md)
python -m unittest discover -s tests
```

Open `build\output\BuildAMelee.iso` in Slippi Dolphin. The mod logs to
Dolphin's log (`[bam]` lines); the first line shows the version and build id.
Release builds log only what matters for bug reports (crashes and freezes,
moves dropped for memory, the online build exchange, replays); a `--debug`
build logs everything ([docs/LOGS.md](docs/LOGS.md) explains each line).
Every build prints how much of the overlay is free.

## Releasing

Write the player notes in `releases/RELEASE-NOTES-v1.2.md` first, then:

```powershell
py -3 tools\bam.py release 1.2
```

This sets the version in `project.toml`, builds, writes the ISO, makes
`patch.xdelta` from your clean ISO, checks that the patch reproduces the ISO
exactly, and packages `build\release\BuildAMelee-v1.2.zip` (the
drag-and-drop .bat, the Linux/Mac script, README.txt and xdelta3.exe, from
`packaging\`). Then:

1. Unzip it, drag your clean ISO onto the .bat, play the result.
2. Commit, and tag: `git tag v1.2 && git push --tags`.
3. On the public repo, create release v1.2 and attach the zip.

Versions: 1.0 was the first release, 1.1 the next. Bump the second number
for each release (1.1 -> 1.2); add a third for a quick hotfix (1.2 -> 1.2.1).

## Online versions

Builds are exchanged between players in Slippi Direct (`src/platform/online_sync.c`).
Each build carries an id: the version plus a hash of the source
(`1.1-` plus six hex digits), so two players exchange builds only when their DOLs come
from the same source. Otherwise both play their characters' own moves, and
the character select screen says why after the match (different version, no
answer, or quick chat off). The version is shown in the bottom-left corner of
the character select screen.

## Layout

| Path | What |
| --- | --- |
| `src/engine/` | Borrowed-move engine: catalog, loading donors, running borrowed moves |
| `src/engine/anim/` | Posing borrowed animations; hitboxes, rebuilt bones and articles on the borrower |
| `src/engine/visual/` | Drawing a borrowed move's parts (donor model, weapons, parasol) |
| `src/platform/` | Game features: build panel, saved builds, training mode, online exchange, replays, watchdog |
| `src/boot/`, `src/bam/` | Overlay installation and memory; shared definitions and named retail addresses |
| `src/qa/` | Automated move sweep (QA builds only, `docs/QA.md`) |
| `overrides/` | Edits to retail functions that are recompiled (`overrides/README.md`) |
| `project.toml` | Version, overlay address, sources, hooks, allowed engine globals |
| `tools/bam.py`, `tools/bam/` | Build pipeline, bone tables, trimmed models, release packaging |
| `tools/qa/` | Sweep runner and analyzer |
| `packaging/` | Files that go into the player zip |
| `releases/` | Player release notes |
| `slippi/` | Slippi Dolphin's code list, used to refuse conflicting patches |

More: [CONTRIBUTING.md](CONTRIBUTING.md) (common jobs and rules),
[docs/BUILDING.md](docs/BUILDING.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md),
[docs/DONOR_NOTES.md](docs/DONOR_NOTES.md), [docs/QA.md](docs/QA.md),
[docs/STATUS.md](docs/STATUS.md), [docs/CHANGELOG.md](docs/CHANGELOG.md).

Never commit game files: no ISO, `main.dol`, assets or anything read from
them (the bone tables and trimmed models are generated into `build/` from
your ISO; `.gitignore` keeps `build/`, `dist/` and `.iso/` out).
