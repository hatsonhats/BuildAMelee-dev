# Status

**v1.3.4** - all 21 move slots borrowable on every character; local and
Slippi Online (Direct) play.

## Changes
- 1.3.4: a borrowed Kirby inhale swallows without copying (Kirby himself
  still copies), so the copy-ability moves run with retail code; release
  builds keep only the essential log lines (`bam.py build --debug` for all);
  hook trampolines share one register save/restore; dead code removed. The
  overlay went from 1.5 KB to ~40 KB free. The move sweep runs locally on
  Windows with parallel Dolphin instances (docs/QA.md). Fixes from the first
  full local sweep: Kirby's rest-pose data was read without the game's
  joint-skip table, so Kirby's moves (Hammer, Final Cutter, down smash, jab,
  forward tilt) put hitboxes in the wrong place on other fighters and other
  fighters' moves posed Kirby's arms wrongly; a borrowed Peach up special's
  parasol float now has Peach's open-parasol hitbox, and the float is decided
  in the game simulation instead of the renderer (rollback-safe); Pichu's
  height (an oversized head hurtbox had made it taller than Pikachu).
- 1.3.3: Slippi replays play back the builds that were used (recorded in the
  spare player entries of the match setup block; src/platform/replay_builds.c);
  borrowed moves follow 70% of the body ratio (ratio^0.7), within 0.8x-1.55x
  of the donor's own size.
- 1.3.2: fixed a crash after a borrowed Peach up B; borrowed moves are never
  drawn smaller than on their own character (they still grow on bigger ones).
- 1.3.1: borrowed moves sized by each character's measured body height
  (Marth's sword on Jigglypuff was far too small, Pichu's moves on Bowser
  too big).
- 1.3: share codes typed on Melee's own keyboard; fixed a crash after online
  matches (after typing a connect code).
- 1.2: three saved builds (on the memory card in Slot A when there is one,
  otherwise until the game is closed) and share codes, on the build panel's
  Saved tab. Codes are typed on Melee's own name-entry keyboard.
- 1.1: version shown on the character select screen; after an online
  match that played without builds, the screen says why (different version,
  no answer, quick chat off). The online build id is now the version plus a
  source hash, so the same source always matches. `bam.py release`.
- 1.0: first release.

## Verified
- Automated sweep (docs/QA.md): every move slot of all 26 characters with
  every other character as donor, about 14,000 moves, in a headless Dolphin.
  Every move starts, ends and returns the fighter to idle; borrowed hitboxes
  are checked against the donor's own move (size, body part, position,
  damage).
- Played by hand, offline and online between two Slippi accounts.

## Known limitations
- Ness's yo-yo and Sheik's chain keep the donor's absolute length on bigger
  or smaller fighters (their hitboxes stay on the yo-yo and chain).
- Some of Kirby's kicks and hammer swings point at an odd angle on other
  bodies (they still connect).
- A borrowed move whose donor files do not fit in memory falls back to the
  fighter's own move for that match (logged in dolphin.log).
