# Reading a BuildAMelee log

The mod writes to Dolphin's log (in Slippi Launcher's Dolphin: View → Show
Log, or `dolphin.log` in the Dolphin user folder). Every line it writes
starts with `[bam]`, then an area and a colon. Release builds write only the
lines below; a `bam.py build --debug` build writes many more (each borrowed
move, hitbox and article), which are for developers.

When someone reports a bug, ask for the whole log from starting the game to
the problem. The first `[bam]` line says which build they run.

## boot

| Line | Meaning |
|---|---|
| `boot: overlay 817c9800-81829400, arena top was …, version 1.3.7 build 1.3.7-xxxxxx` | The mod loaded. Two players need the same `build` to exchange builds online. |
| `boot: FATAL: arena … cannot hold the overlay …` | This Dolphin set up memory differently from Slippi Dolphin; the mod cannot run. Use Slippi Dolphin. |

## FREEZE

| Line | Meaning |
|---|---|
| `FREEZE: no frame for N s. pc=… lr=…` and `FREEZE: caller …` | The game stopped producing frames. The addresses show where; send them with the report (src/platform/watchdog.c). |

## memory

The game has a fixed amount of memory per match. When a borrowed move's
files do not fit, that slot plays the fighter's own move for the match.

| Line | Meaning |
|---|---|
| `memory: donor kind=K does not fit (…)` | Character K's files did not fit; every move borrowed from K is skipped this match. |
| `memory: donor kind=K ran out while loading` | The same, found partway through loading. |
| `memory: donor kind=K move M skipped` | One borrowed special from K is skipped. |
| `memory: special M skipped, no room for its animations` | The special's animations did not fit. |
| `memory: aerial kind=K slot=S skipped, …` / `memory: normal slot=S kind=K skipped, …` | One borrowed aerial or ground attack/throw skipped. |
| `memory: <what> kind=K skipped, no room for N KB of animations` | A donor's animations for that kind of move did not fit. |
| `memory: donor data/effects kind=K …` | The donor's data or effect files did not fit. |
| `memory: donor model kind=K skipped (…)` / `memory: donor parts kind=K skipped (…)` | The donor's model (a sword, tail, dress) is not drawn; the move still works. |

Fewer characters or fewer different donors in one match leaves more room.
Kinds are the game's internal fighter numbers (`enum FighterKind`: 0 Mario,
1 Fox, 2 Captain Falcon, 3 Donkey Kong, 4 Kirby, 5 Bowser, 6 Link, 7 Sheik,
8 Ness, 9 Peach, 10 Popo, 11 Nana, 12 Pikachu, 13 Samus, 14 Yoshi,
15 Jigglypuff, 16 Mewtwo, 17 Luigi, 18 Marth, 19 Zelda, 20 Young Link,
21 Dr. Mario, 22 Falco, 23 Pichu, 24 Mr. Game & Watch, 25 Ganondorf, 26 Roy).

## parts

| Line | Meaning |
|---|---|
| `parts: kind=K: PlXxBm.dat is malformed …` / `… has no …` | A trimmed donor model on the patched disc is damaged. Re-patch the ISO from a clean one. |

## online

| Line | Meaning |
|---|---|
| `online: exchanging builds (first/second, build …)` | A Slippi Direct match is starting; builds are being exchanged. |
| `online: builds exchanged in N ms` | Both players' builds will be used. |
| `online: opponent runs a different BuildAMelee build; …` | The two players' `build` differs; both play their own moves. |
| `online: opponent has quick chat off` | The exchange runs over quick chat; with it off, builds cannot be exchanged. |
| `online: mode N is not Direct; …` | Builds are only exchanged in Direct mode. |
| `online: build exchange failed; …` / `online: timeout at message N` | The exchange did not finish (connection trouble). |
| `online: loaded masks here … there …` / `online: loaded-move exchange failed; …` | After loading, both sides compare which borrowed moves fit in memory; a move one side could not load is turned off on both. |
| `online: out of memory for the exchange; …`, `online: unexpected player slots …` | The exchange could not run; borrowed moves are off for the match. |

## replay

| Line | Meaning |
|---|---|
| `replay: P1 build …` | A replay is playing back with the builds recorded in it. |
| `replay: P1 build … is from another version; own moves` | The replay was recorded with another BuildAMelee version. |
| `replay: no builds recorded; …` | The replay has no builds (recorded without BuildAMelee, or before 1.3.3). |

## training, css, store, joint

| Line | Meaning |
|---|---|
| `training: restarting with the new build` | The build was changed in the training pause menu. |
| `training: no memory for …` / `css: no memory for the panel` | A menu could not be shown this time; reopening the screen usually helps. |
| `css: the keyboard did not take the code entry` | Typing a share code could not start. |
| `store: card file unreadable, ignored` | The saved-builds file on the memory card is damaged and was skipped. |
| `joint: bad joint … passed to lb_8000B1CC from …, ignored` | A borrowed move asked for a bone that does not exist; it was ignored instead of crashing. Report it with the move. |
