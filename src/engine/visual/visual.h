/* Drawing a borrowed move's parts: the donor's own model, held weapons,
 * Peach's parasol (src/engine/visual/). */
#ifndef BAM_VISUAL_H
#define BAM_VISUAL_H
#include <melee/ft/forward.h>
#include <dolphin/mtx.h>
struct HSD_GObj;

/* Beam Sword model in the hand during borrowed sword moves (visual/weapons.c). */
void Bam_SwordDisplay(struct HSD_GObj* gobj, int pass, MtxPtr vmtx);
void Bam_SwordRelease(const Fighter* fp);
/* Donor model-part group switches kept for the donor's model (true: kept),
 * and whether the borrower's own body is hidden for one (Kirby's stone). */
bool Bam_VisSet(struct HSD_GObj* gobj, int group, int val);
bool Bam_BodyHidden(struct HSD_GObj* gobj);
bool Bam_ParasolFloat(HSD_GObj* gobj);
/* Every frame: a borrowed Peach up special's parasol float (visual/parasol.c). */
void Bam_ParasolTrack(Fighter* fp);
bool Bam_ParasolOpen(struct HSD_GObj* gobj);
/* Mr. Game & Watch's color for his articles on a borrower (ftlib.c fix). */
int Bam_DonorItemColor(struct HSD_GObj* gobj, void* dst, int outline);
/* The match-heap block (Bam_MatchBegin / Bam_MatchEnd). */
void Bam_SwordVisualMatchBegin(void);
void Bam_SwordVisualMatchEnd(void);
#endif
