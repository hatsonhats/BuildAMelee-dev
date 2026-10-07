/* For the engine's own files (src/engine): the public headers, the match
 * state, the decomp headers they use, and functions only the engine calls. */
#ifndef BAM_ENGINE_INTERNAL_H
#define BAM_ENGINE_INTERNAL_H
#include <engine/engine.h>
#include <engine/state.h>
#include <bam/fields.h>
#include <melee/ef/efasync.h>
#include <melee/ef/eflib.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftFox/types.h>
#include <melee/ft/kinds/ftFox/ftfoxspecialn.h>
#include <melee/ft/kinds/ftCaptain/ftcaptainspecials.h>
#include <melee/ft/kinds/ftDonkey/ftdonkeyspecialhi.h>
#include <melee/ft/kinds/ftGameWatch/ftgamewatch.h>
#include <melee/ft/kinds/ftLink/ftlinkspecialn.h>
#include <melee/ft/kinds/ftLink/ftlinkspecials.h>
#include <melee/ft/kinds/ftPeach/ftpeachspecialhi.h>
#include <melee/ft/kinds/ftMario/ftmariospecials.h>
#include <melee/ft/kinds/ftSamus/inlines.h>
#include <melee/ft/kinds/ftMewtwo/ftmewtwospecialn.h>
#include <melee/ft/kinds/ftPeach/ftpeachspecialn.h>
#include <melee/ft/kinds/ftSeak/ftseakspecials.h>
#include <melee/ft/kinds/ftNess/ftnessattackhi4.h>
#include <melee/ft/kinds/ftNess/ftnessattacks4.h>
#include <melee/pl/player.h>
#include <melee/it/it_26B1.h>
#include <stdio.h>
#include <string.h>
#include <dolphin/os.h>

/* Both borrowers capture exactly once after restoring the previous owner. */
void Bam_BorrowBegin(Fighter* fp, FighterKind source);
void Bam_AerialRelease(BamFighterState* S);
/* A donor's data, effects and articles for this fighter (donor_load.c);
 * 0 when memory ran out. */
int Bam_DonorEnsure(BamFighterState* S, int source);
/* The private animation table of a donor whose archive is not resident,
 * created on first use; NULL when it is resident or out of memory. */
Fighter_WaitAnimData* Bam_DonorAnimTable(BamFighterState* S, int source);
/* Reads the listed animations of a donor into one block and points its
 * private table at them. 0 when out of memory. */
int Bam_DonorReadAnims(BamFighterState* S, int source, const short* anims, unsigned count, const char* what);
MotionState* Bam_NormalMotionState(Fighter* fp, int motion);
void Bam_NormalPrepare(Fighter* fp);
#endif
