# Status

**v1.2** - all 21 move slots borrowable on every character; local and
Slippi Online (Direct) play.

## Changes
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
