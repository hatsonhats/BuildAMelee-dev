# BuildAMelee v1.3.7

A maintenance release: the code behind BuildAMelee was reorganized so it is easier to work on. Moves, builds and menus play exactly as in v1.3.6.

## Changes
- The messages BuildAMelee writes to Dolphin's log are clearer (each starts with what it is about: `memory:`, `online:`, `replay:`, ...), which makes bug reports easier to read. Please include the whole log when reporting a problem.

## Known issues
- Ice Climbers borrowing Ganondorf's or Bowser's side special do not grab.
- Sheik's chain is the wrong length on some characters.

## Install
Unzip, drag your clean Melee 1.02 ISO onto `DRAG VANILLA MELEE HERE.bat` (Linux/Mac: `patch_linux_mac.sh`), and play the new `BuildAMelee.iso`. Online play and replays need the same BuildAMelee version on both sides: v1.3.7 cannot exchange builds with v1.3.6.
