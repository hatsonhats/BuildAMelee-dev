# Changelog (developer notes)

What changed in each version, for developers. Players get the release notes in
`releases/`.

- 1.3.8: borrowed parts, hitboxes and effects are never smaller than the
  donor's (Bam_ShownScale floor 0.8 -> 1.0 in src/engine/anim/scale.c).
  The build recompiles retail callers that inlined an edited static helper
  (ftCo_AttackLw3's decideFighter: Mr. Game & Watch alternated a borrowed
  and his own down tilt); a sword is skipped only when the borrower holds
  its own in the same hand (keeps_own_weapon); CSS texts come back after
  Slippi's SIS reset; the CSS always shows the version and build hash.
- 1.3.7 (second cleanup, no gameplay change): engine files and headers by
  job (borrow.c, donor_load.c, catalog.h, borrow.h, ...; one public
  engine/engine.h) and one set of words (donor, borrower, borrowed move;
  glossary in CONTRIBUTING.md); match blocks read through their pointers;
  release log lines as `area: what happened`, explained in docs/LOGS.md;
  long functions split (Bam_HitboxCreated, Bam_DonorEnsure, load_trimmed);
  prop/chain lookups scan only the donor's rows; line edits are the
  `donor-access` rule plus written-out exceptions (overrides/line_edits.json,
  was special_adapters.json); numbered decomp fields named in
  src/bam/fields.h; build.py split (units.py); `bam.py check`; CI runs the
  host tests; every .c opens with what it does, who calls it and its state.
  Overlay 16.5 -> 19.3 KB free.
- 1.3.6 (code cleanup, no gameplay change except the two table fixes below):
  the bone tables are generated at build time from the ISO
  (tools/bam/bonetables.py, now in the repo; joint-skip table applied for
  every fighter, which rebuilds Kirby's ledge-attack bone and corrects
  Link's and Young Link's part 52 parent); retail edits split by area in
  overrides/fixes/ with dependencies reported; anim_scale.c, sword_visual.c
  and css_menu.c split into src/engine/anim/, src/engine/visual/ and
  css_draw.c/css_codes.c; Rogue_ names are Bam_; raw addresses named in
  src/bam/retail.h; the build refuses unlisted writable engine globals;
  bam.py edits, gen-tables and fingerprint; host tests for the asset tools.
- 1.3.6: trimmed donor models. The patched disc gets Pl<code>Bm.dat
  per donor whose parts moves draw (tools/bam/parts.py: whole skeleton,
  only the drawn meshes keep geometry and textures; 30-115 KB instead of
  130-790 KB). src/engine/visual/donor_model.c loads them before the full costume, down to a
  1 MB heap floor. Overlay reserve 0x5FC00 (the larger filesystem table
  lowers the arena top; the build checks it).
- 1.3.6: a borrowed Peach down smash draws her dress on the borrower
  (her skirt bones rebuilt like a tail; prop tracks per fighter 80).
- 1.3.5: Training Mode with builds (src/platform/training.c): the Z
  panel on the Training CSS; in the match D-Up opens the panel (plus an
  Options tab: hitboxes, hurtboxes, screen shake, missed L-cancel flash,
  move info), D-Down freezes, D-Right steps a frame, D-Left move info.
  A build change restarts the match (gmtrainingmode.c platform fixes).
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
  Second sweep: hitbox radii are in world units (no model scale applies to
  them) but were sized like skeleton offsets, so moves from fighters with a
  small model scale (Bowser, Pichu, Pikachu, Samus) had hitboxes too small
  for what was drawn and often missed; a body part the borrower lacks now
  falls back along the limb (a shoulder hitbox stays at the shoulder on
  Kirby, not the hand) and its hitbox offset is turned into that bone's
  frame (Jigglypuff's dash attack on fighters without a waist bone); finger
  bones have rest-pose data (the files' own rotations), so props and
  hitboxes hanging from them (Ice Climbers' hammer, Ness's bat) keep their
  orientation on fighters without those fingers, and props hanging from
  fingers hang from the retargeted hand (Yoshi's, Pichu's and Pikachu's
  hands are turned far from the humanoids', so Marth's sword pointed the
  wrong way on them). A borrowed move that lifts the body (Jigglypuff's dash
  attack and up smash) no longer lifts a taller fighter above the height the
  donor's body reaches, so it hits at the same height. Hitboxes on donor
  fingers (Ness's bat, Game & Watch's jab, Dr. Mario's Super Sheet) ride the
  hand with the donor's finger pose every frame; head hitboxes on Kirby and
  Jigglypuff ride the middle of the body; Ness's yo-yo reaches as far as
  drawn; Samus's Charge Shot leaves from mid-body (it was released into the
  floor on Jigglypuff and the Ice Climbers). Slippi Online replays now
  record the builds too (Slippi Online replaced the match block after they
  were written). The sweep: projectiles and items are measured, a move that
  misses is played again with the dummy moved to it (REACH is what still
  never touches it), each step starts from the same random seed, and
  rebuilt weapons are compared from the donor bone they hang from. Last
  sweep: 13654 OK of 14078; open: Ice Climbers' borrowed command grabs
  (Gerudo Dragon, Koopa Klaw) do not catch, Sheik's chain length.
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
