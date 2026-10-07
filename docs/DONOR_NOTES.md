# Character special cases

Most of a borrowed move works the same for every character: the donor's
animation is retargeted onto the borrower's skeleton, its hitboxes follow the
donor's bones, its articles and effects attach to the matching body part.
Some moves need something only one character has. This page lists every such
case and where it lives, so a fix for one character goes in the right place.

## Data (tools/bam/donor_notes.py)

Choices the build tools need, read when the bone tables and trimmed models
are made from your ISO:

| Table | Characters | What |
|---|---|---|
| `WEAPONS` | Marth, Roy, Link, Young Link, Ice Climbers, Peach | Held item drawn for the donor's weapon bone (sword, hammer, parasol) |
| `EXTRA_PARTS` | Yoshi, Peach | Donor-only bones shown without hitboxes (tongue, dress) |
| `ROOT_CHAINS` | Mr. Game & Watch | Joints posed through his own skeleton from the root (all his props) |
| `AS_PROPS` | Kirby | His hammer/Final Cutter finger joint rebuilt as a prop |
| `CORE_PARTS` | all | Which rebuilt bones may draw their mesh (tails, not extra limbs) |
| `VIS_DONORS` | Mr. Game & Watch, Peach, Kirby, Yoshi, Samus | Model-part groups kept in the trimmed models |

## Engine (src/engine)

| Where | Characters | What |
|---|---|---|
| `donor_specials.c` | all | The catalog: which special each character's slot borrows |
| `donor_load.c` `Bam_DonorEnsure` | Bowser, Samus, Mewtwo, Ness, Peach, Yoshi, Zelda, Sheik, Mr. Game & Watch, Kirby, Ice Climbers, Link, Young Link, Mario, Dr. Mario, Luigi, Pikachu, Pichu, Fox, Falco | Articles and per-donor variables set up before a match |
| `borrow.c` `Bam_BorrowEnd` | Donkey Kong, Mr. Game & Watch, Samus, Ness, Mewtwo, Peach, Sheik, Captain Falcon, Ganondorf, Mario, Dr. Mario, Fox, Falco | What a donor's own code tears down when the move ends early |
| `borrow.c` `Bam_ChargeFlash` | Donkey Kong, Sheik, Samus, Mewtwo, Mr. Game & Watch | Full-charge flash kept after the motion changes |
| `transform.c` | Zelda, Sheik | Borrowed Transform switches the borrowed form |
| `rest_sleep.c` | Jigglypuff | Rest's sleep on a borrower |
| `normals.c` | Mr. Game & Watch, Kirby, Ness, Peach, Link, Young Link, Donkey Kong, Samus, Fox, Falco, Mewtwo, Zelda, Sheik | Borrowed normals with their own states or articles (bat, yo-yo, clubs, cargo throws) |
| `aerials.c` | Mr. Game & Watch, Link, Young Link | Aerials with their own code (G&W's props, Link's down air) |
| `anim/wear.c` | Peach | Her dress worn by the borrower (fitted to waist and height) |
| `visual/donor_model.c` | Mr. Game & Watch | His model and articles take his color, not white |
| `visual/weapons.c` `sword_kind`, `hammer_kind` | Marth, Roy, Link, Young Link, Ice Climbers | Fighters whose own model already shows the weapon |
| `visual/parasol.c` | Peach | Up special's parasol float on a borrower |

## Edits to retail code (overrides/fixes)

`20-characters.toml` holds the edits for one character's moves; each fix's
`owner` names the character (`platform/specials/link`, `.../samus`, ...).
Mr. Game & Watch's and Link's aerials are in `40-aerials.toml`, character
normals in `30-normals.toml`. `bam.py edits <file>` shows any of them as a
diff.
