# Automated move sweep (QA)

A QA build plays every move slot of every character with every donor in
Dolphin, as fast as the machine allows, and checks each borrowed move against
the donor's own. It never ships.

## Running it

One-time setup:

1. Install a **stock** Dolphin (a dev build from dolphin-emu.org; not Slippi
   Dolphin, whose disc boot differs). The Windows download includes
   `DolphinNoGUI.exe`, which runs without windows and is what the sweep uses.
2. Tell the sweep where it is, in `config/local.toml`:
   ```toml
   [paths]
   qa_dolphin = "C:/Dolphin-x64/DolphinNoGUI.exe"
   ```
   (or pass `--dolphin PATH`, or set the `DOLPHIN` environment variable).
   `paths.melee_iso` must point at your clean NTSC 1.02 ISO, as for normal
   builds.

Then, from the repository folder:

```
python tools/qa/sweep.py
```

This builds the QA executable, makes a QA copy of the ISO the first time
(`build-qa/sweep.iso`, 1.4 GB, reused afterwards), runs all 674 matches on
parallel Dolphin instances and writes:

- `build-qa/sweep/<tag>/report.txt`: the summary and every move that did not
  pass, with the reason;
- `build-qa/sweep/<tag>/results.csv`: one row per move tested;
- `build-qa/sweep/<tag>/logs/`: the raw logs, one per batch of 4 matches.

Useful options:

| Option | What it does |
|---|---|
| `--workers N` | Dolphin instances at once (default: half the CPU threads). Each uses about 1 GB of RAM and two threads. |
| `--pair Jigglypuff:Marth` | One recipient with one donor (about 30 seconds). |
| `--matches 26-100` | A range of matches (0-25 are each character's own moves). |
| `--tag NAME` | Output folder name (default: date and time). |
| `--no-build` | Reuse the last QA build. |
| `python tools/qa/sweep.py report TAG` | Re-run the analysis of an earlier sweep. |

Hitbox checks compare a borrowed move with the donor's own, so a sweep that
includes the native matches (0-25) checks more than `--pair` alone.

## Results

| Result | Meaning |
|---|---|
| OK | The move ran and its hitboxes match the donor's own, scaled |
| HITBOX | Hitboxes differ in size, place or damage from the donor's own |
| NOHIT | The dummy took no damage although the donor's own move hits it (often harmless: a move that whiffs from that spacing) |
| VARIANT | The move ran in another of its states than the donor's own, e.g. Ice Climbers' Squall Hammer without Nana (informational) |
| NOMOVE | The move never started |
| STUCK | The move never ended |
| CRASH / FREEZE | The emulator crashed or hung on this move; the sweep skipped it and went on |

## How it works

- `src/qa/qa_moves.c` boots straight into VS mode and chains matches on
  Final Destination. Port 1 is driven by inputs (`src/qa/qa_bot.c`), port 2
  is an idle Mario.
  - Matches 0-25: each character with its own moves (the reference).
  - Matches 26-673: every recipient with every other donor, every slot from
    that donor (12 ground attacks and throws, 5 aerials, then specials).
  - Per step: both fighters made idle and placed, the move's inputs played
    (tilts at half stick, smashes on the C-stick, throws after a grab,
    aerials from a full hop), then wait for port 1 to stand again.
  - Log lines: `M` match, `S` step start, `H` every active hitbox every frame
    (position relative to the fighter and to its body part, size, damage),
    `B` borrow scale, `G` effects spawned, `R` result, `E` match end.
- `qa.toml`: QA-only hooks, the full debug log, and a larger overlay.
- `tools/qa/sweep.py`: the runner. Each worker has its own Dolphin user folder
  (`build-qa/sweep/<tag>/users/`) with fast settings (unthrottled, no video, no
  audio, fast disc), and a game patch in its game settings that tells the QA
  executable which matches to play. It reads Dolphin's log file; after a
  crash or freeze it records the step and relaunches from the next one.
- `tools/qa/analyze.py`: pairs each borrowed hitbox with the donor's own by
  motion, animation frame and slot, and checks size, position relative to
  the body part it rides on, and damage.

Menu tests (`qa/ui_*.txt`, `qa/css.txt`) are QA scripts for the same build
(`bam.py build --qa NAME`), run by hand in Dolphin.
