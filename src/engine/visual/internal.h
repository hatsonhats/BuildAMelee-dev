/* Shared by the files that draw a borrowed move's parts, in src/engine/visual/:
 *   donor_model.c  the donor's own model (trimmed or full costume), its
 *                  model-part groups, Mr. Game & Watch's colors
 *   weapons.c      held items for a donor's weapon (Beam Sword, hammer)
 *   parasol.c      Peach's parasol float after a borrowed up special
 * Their per-match state is allocated on the match heap (Bam_MatchBegin) so
 * Slippi rollback restores it; NULL outside matches. */
#ifndef BAM_VISUAL_INTERNAL_H
#define BAM_VISUAL_INTERNAL_H
#include <engine/internal.h>
#include <melee/lb/lbfile.h>
#include <melee/lb/lbarchive.h>
#include <sysdolphin/baselib/archive.h>
#include <melee/ft/fighter.h>
#include <melee/it/forward.h>
#include <melee/it/types.h>
#include <melee/it/it_3F14.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/dobjlist.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/initialize.h>
#include <dolphin/os/OSAlloc.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mtx.h>
#include <melee/ft/kinds/ftPeach/ftpeach.h>
#include <melee/ft/ft_0877.h>
#include <melee/ft/kinds/ftPeach/types.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <string.h>

/* ---- per-match state ----
 * Allocated on the match heap at match start (Bam_MatchBegin) so Slippi rollback
 * restores it; the pointer is NULL outside matches. */
#define WEAPON_ITEMS 3
#define DONOR_JOINTS 192
#define DONOR_MESHES 16
typedef struct DonorModel {
    unsigned kind;          /* Ft_Kind_Max: none loaded. */
    HSD_JObj* root;
    unsigned joints, meshes;
    HSD_JObj* jobj[DONOR_JOINTS];
    unsigned char parent[DONOR_JOINTS];
    HSD_DObj* dobj[DONOR_MESHES];
    unsigned char group[DONOR_MESHES];
    /* A DObj only partly the donor's part (Yoshi's tail is in a body DObj):
     * its PObj chain is cut to the part's PObjs, the rest kept here and put
     * back before the model is freed. */
    HSD_PObj* dropped[DONOR_MESHES];
} DonorModel;
#define VIS_GROUPS 12
typedef struct VisualState {
    HSD_JObj* weapons[BAM_FIGHTERS][WEAPON_ITEMS];
    Fighter* weapon_owner[BAM_FIGHTERS];
    DonorModel* donor_models[BAM_FIGHTERS];
    signed char donor_vis[BAM_FIGHTERS][VIS_GROUPS];
    unsigned char donor_vis_kind[BAM_FIGHTERS];
    unsigned char parasol_float[BAM_FIGHTERS];
    unsigned char parasol_hit[BAM_FIGHTERS];     /* hitbox id + 1 while it is on */
    Vec3 parasol_hit_off[BAM_FIGHTERS];          /* canopy, in the root's frame */
    Mtx weapon_attach[BAM_FIGHTERS][WEAPON_ITEMS];
    Mtx parasol_rel[BAM_FIGHTERS];
    /* Trimmed donor models loaded this match (Pl<code>Bm.dat). */
    HSD_Joint* part_joint[Ft_Kind_Max];
} VisualState;
extern VisualState* bam_visual; /* weapons.c */

/* Index of a build fighter in bam_match->fighters, or -1. */
static inline int slot_of(const Fighter* fp)
{
    unsigned i;
    for (i = 0; i < BAM_FIGHTERS; ++i)
        if (bam_match->fighters[i].fighter == fp) return (int) i;
    return -1;
}

unsigned donor_display(Fighter* fp, unsigned slot, int pass, MtxPtr vmtx);
void donor_free(unsigned slot);
bool hammer_kind(unsigned kind);
int parasol_base(Fighter* fp);
void parasol_display(Fighter* fp, int slot, int pass, MtxPtr vmtx);
bool sword_kind(unsigned kind);
bool keeps_own_weapon(unsigned own, unsigned donor, int item);
HSD_JObj* weapon_model(unsigned slot, int item);
#endif
