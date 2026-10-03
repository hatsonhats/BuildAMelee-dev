# Automated move sweep (QA)

A QA build plays every move slot of every character with every donor in a
headless Dolphin and logs what happened. It never ships.

## Pieces

- `src/qa/qa_moves.c`: the sweep. It boots straight into VS mode and chains
  matches on Final Destination (CSS for a few frames, no stage select, no
  results). Port 1 is driven by inputs, port 2 is an idle Mario.
  - Matches 0-25: each character with its own moves (the reference).
  - Matches 26-673: every recipient with every other donor, every slot from
    that donor (12 ground attacks and throws, 5 aerials, then specials).
  - Per step: both fighters made idle and placed, the move's inputs played
    (tilts at half stick, smashes on the C-stick, throws after a grab,
    aerials from a full hop), then wait for port 1 to stand again.
  - Log lines: `M` match, `S` step start, `H` every active hitbox every frame
    (position relative to the fighter and to its body part, size, damage),
    `B` borrow scale, `G` effects spawned, `R` result, `E` match end.
- `src/qa/qa_bot.c`: feeds the sweep's inputs into port 1.
- `qa.toml`: QA-only hooks and a larger overlay (built into `build-qa/`).
- `tools/qa/sweep.py`: runs it.
  - `sweep.py iso` / `sweep.py update` write the QA DOL into the ISO copies.
  - `sweep.py run --workers 2 --tag NAME` runs the sweep in parallel
    emulators, restarting after a crash or freeze and skipping the step that
    broke (`X` lines, with the crash's PC/LR/back chain).
  - `sweep.py run --slot 9 --start M --end M+1 --step S --video` replays one
    step with the software renderer and dumps every frame as PNG (hitboxes
    drawn), for a visual check.
- `tools/qa/analyze.py NAME`: per-step verdicts. It pairs each borrowed
  hitbox with the donor's own by motion, animation frame and slot, then checks:
  - its size is the donor's times the borrow scale;
  - its position relative to the body part it rides on is the donor's
    scaled (or relative to the fighter when the parts differ);
  - its damage matches the donor's.

  It also flags moves that never started (`NOMOVE`), never ended (`STUCK`),
  never hit when the donor's did (`NOHIT`, informational), and crashes.

## Emulator

Set `DOLPHIN` to the headless Dolphin binary (`dolphin-emu-nogui`) and, if
the decomp is not in `.cache/melee`, `BAM_SYMBOLS` to its
`config/GALE01/symbols.txt`.

Stock Dolphin 2506, built headless (`-DENABLE_QT=OFF -DENABLE_HEADLESS=ON`)
with three local changes (`tools/qa/dolphin-2506-qa.patch`):
- alerts never block when `DOLPHIN_NO_ALERTS` is set;
- every ISI/DSI exception logs PC, LR and the back chain;
- the software renderer runs without a GL window, for frame dumps.
