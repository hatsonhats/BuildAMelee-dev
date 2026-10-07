# Changing retail code

BuildAMelee never relinks Melee. Its own code (`src/`) goes into an overlay,
and retail code is changed in one of three ways. Pick the first that works.

| Need | Mechanism | Where |
|---|---|---|
| Run our code at a point in a retail function, or replace a whole function | **Hook** | `[[hook]]` in `project.toml` |
| Change a few lines inside a retail function | **Fix** (anchor edit) | `overrides/fixes/*.toml` |
| The borrowed-move engine's large mechanical rewrite of fighter code | **Line edits** | `overrides/line_edits.json` |

## Hooks (`project.toml`)

- `override`: the retail function's first instruction branches to ours.
- `call`: one `bl retail_fn` is pointed at our function instead.
- `inject`: a trampoline at an address saves the registers, calls our function
  with the registers listed in `args`, optionally writes its return value to
  the register named in `ret`, runs the displaced instruction and returns.
- `word`: a single instruction or data word is replaced (`value`).

The build refuses a hook on an instruction Slippi's own codes patch
(`slippi/GALE01r2.ini`, `slippi/bootloader.gct`), so online play, replays and
rollback keep working. `bam.py check-slippi` lists the overlaps.

## Fixes (`overrides/fixes/`)

A fix finds an exact piece of text (`anchor`) in one decomp source file and
replaces it. The decomp unit is then recompiled into the overlay, and every
function in it that now differs from retail gets an `override` hook.

```toml
[[fix]]
id = "borrowed-charge-flash"          # unique, used in error messages
owner = "platform/specials"           # which build includes it (override_set owners)
file = "src/melee/ft/ftcolanim.c"
anchor = "if (ftData_UnkMotionStates4[fp->kind] != NULL) {"
occurrences = 4                       # default 1; the build checks the count
replacement = "..."
reason = "What it fixes, in a sentence or two."
```

Files load in name order and fixes in file order:

| File | Contents |
|---|---|
| `10-core.toml` | The engine: retargeting, hitboxes on rebuilt bones, articles, effects, bubbles, model parts |
| `20-characters.toml` | One character's moves (the owner names the character) |
| `30-normals.toml` | Borrowed jabs, tilts, smashes and throws |
| `40-aerials.toml` | Borrowed aerials and landings |
| `50-slippi-codes.toml` | Compatibility with Slippi's optional codes |
| `60-training.toml` | Training mode |

A fix whose anchor is not in the retail source as is matches text that a line
edit or an earlier fix wrote, so it breaks if that edit changes. The build
lists these under `fix_dependencies` in `build/output/build-report.json` and
counts them in its output. Keep the anchor on the edited text when there is
no other choice, and say so in the fix's `reason`.

## Line edits (`overrides/line_edits.json`)

839 edits across 96 fighter, item and effect files, made when the
borrowed-move engine was written. Each file entry pins the decomp source's
SHA-256, so a decomp update that touches one of these files stops the build
instead of producing a wrong edit.

Most of them are one mechanical rewrite, written as a **rule** and the lines
it applies to (`[line, count]`, 0-based):

```json
{ "owner": "donor-variable-bank", "rule": "donor-access", "lines": [[30, 2], [75, 1]] }
```

`donor-access` (`tools/bam/transform.py` `RULES`) turns `fp->u.ca.x` into
`Bam_DonorVars(fp, Ft_Kind_Captain)->ca.x` (the donor's variables, not the
borrower's) and `fp->parts[FtPart_X]` into
`fp->parts[Bam_DonorBoneJoint(fp, FtPart_X)]` (the borrower's bone for the
donor's body part). Every other edit is written out as `line`, `before[]`
and `after[]`, with the function it is in (`symbol`). Owners:
`donor-variable-bank` (donor variables), `bone-animation-data` (bones and
animation data), `donor-compatibility`, `transform-subfighter` (Zelda/Sheik,
Ice Climbers), `special-input`, `state-table-redirect`, `lifecycle-cleanup`,
`donor-preload`.

Prefer a fix for new changes.

## Edited helpers retail inlined

A fix to a small `static` function (ftCo_AttackLw3.c's `decideFighter`)
does not reach the retail functions that carry their own inlined copy of it
(`ftCo_AttackLw3_IASA`): they never call it. The build finds those callers
(no `bl` to the helper in their retail code) and recompiles them too, so an
edit to a helper applies everywhere it runs.

## Seeing what changed

`python3 tools/bam.py edits [FILTER]` prints every changed retail file as a
unified diff (line edits and fixes applied), e.g. `bam.py edits ftcoll`.
