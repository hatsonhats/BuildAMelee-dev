/* Posing a borrowed animation on the borrower and placing what hangs off
 * it: rebuilt donor bones, hitboxes, articles, sizes (src/engine/anim/). */
#ifndef BAM_ANIM_H
#define BAM_ANIM_H
#include <melee/ft/forward.h>
#include <dolphin/mtx.h>
struct HSD_JObj;
struct HSD_GObj;
struct FigaTrack;
struct HitCapsule;

/* Borrower joint a rebuilt donor-only bone hangs from, or -1 (anim/props.c). */
int Bam_PropJoint(Fighter* fp, int bone);
/* Joint an absorb/reflect/shield bubble with donor bone id `bone` rides on. */
struct HSD_JObj* Bam_DonorBoneJObj(Fighter* fp, int bone);
/* World matrix of a borrowed weapon along the donor's weapon bone; returns
 * the item (0 Beam Sword, 1 Hammer) or -1 if the current move has none. */
int Bam_PropWeaponMtx(Fighter* fp, Mtx out);
/* The donor's own meshes for rebuilt bones (anim/pose.c, bone_tables.h):
 * groups to show for the current move (item_of_group: weapon item a group
 * replaces, 255 none), the donor's mesh rows, and posing its model. */
unsigned Bam_DonorMeshShow(Fighter* fp, unsigned char item_of_group[8]);
/* pobjs (may be NULL): the PObjs of each DObj to draw, bit each, 0 all. */
unsigned Bam_DonorMeshes(unsigned kind, unsigned char* groups, unsigned short* dobjs, unsigned short* pobjs,
                           unsigned max);
/* free_joint: a donor joint posed from its own rest pose, not the matching
 * fighter joint (-1 none); shown: the mesh groups drawn (their collapsed
 * joints, bam_mesh_hide, are drawn at no size). */
void Bam_DonorPose(Fighter* fp, struct HSD_JObj* const* jobjs, const unsigned char* parents, unsigned n,
                     int free_joint, unsigned shown);
/* Size of a borrowed move's own parts (weapons, props, held articles,
 * hitboxes) on this fighter: its body over the donor's, 1 when not
 * borrowing (anim/scale.c). */
float Bam_BorrowScale(Fighter* fp);
/* The same in world units, for hitbox radii. */
float Bam_HitboxScale(Fighter* fp);
/* A move of FighterKind source on FighterKind own: its size on screen against
 * the source's own (0 when they are the same). */
float Bam_ShownScale(unsigned own, unsigned source);
/* The scale an article sizes itself by from its owner: the owner's model
 * scale, times Bam_BorrowScale while the owner borrows. */
float Bam_OwnerScale(HSD_GObj* owner);
/* The joint an article a borrowed move attaches to part `part` rides on. */
struct HSD_JObj* Bam_ItemAnchor(HSD_GObj* gobj, int part);
/* Borrowed-move body scaling (anim/retarget.c): called when an animation is
 * applied; animated translations of registered body bones are mapped from
 * the donor's body to the borrower's. */
void Bam_AnimRetarget(Fighter* fp, unsigned donor_kind, int first_part);
float Bam_AnimTranslate(struct HSD_JObj* jobj, int axis, float value);
void Bam_AnimScaleReset(void);
/* Animated joint rotation; retargets a borrowed bone by world orientation. */
void Bam_AnimRotate(HSD_JObj* jobj, int axis, float value);
void Bam_PropTrack(Fighter* fp, int joint, struct FigaTrack* track, int count);
void Bam_HitboxCreated(Fighter* fp, struct HitCapsule* hit, int bone);
void Bam_HitboxRefresh(Fighter* fp, struct HitCapsule* hit);
void Bam_AnimPostStep(Fighter* fp);
/* Effects a fighter spawns sized by its own scale (eflib.c fix). */
float Bam_EffectScale(struct HSD_GObj* gobj);
/* A donor body part the borrower lacks, asked for by a move (ftparts.c fix). */
void Bam_NoteHeldPart(Fighter* fp, int part, int joint);
/* The match-heap block (Bam_MatchBegin / Bam_MatchEnd). */
void Bam_AnimScaleMatchBegin(void);
void Bam_AnimScaleMatchEnd(void);
#endif
