# Status

**v1.3.6** - all 21 move slots borrowable on every character; local play,
Training Mode with builds, and Slippi Online (Direct) play with replays.
What changed in each version: [CHANGELOG.md](CHANGELOG.md).

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
