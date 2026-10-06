#include <engine/special_internal.h>
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
typedef struct SwordVisualState {
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
} SwordVisualState;
static SwordVisualState* bam_sword_visual;
#define donor_models (bam_sword_visual->donor_models)
#define part_joint (bam_sword_visual->part_joint)
#define donor_vis (bam_sword_visual->donor_vis)
#define donor_vis_kind (bam_sword_visual->donor_vis_kind)
#define parasol_float (bam_sword_visual->parasol_float)
#define parasol_rel (bam_sword_visual->parasol_rel)
#define parasol_hit (bam_sword_visual->parasol_hit)
#define parasol_hit_off (bam_sword_visual->parasol_hit_off)
#define weapon_attach (bam_sword_visual->weapon_attach)
#define weapon_owner (bam_sword_visual->weapon_owner)
#define weapons (bam_sword_visual->weapons)


/* Borrowed weapon moves show the weapon.
 *
 * Marth's, Roy's, Link's and Young Link's moves swing a sword, Kirby's Hammer
 * and Final Cutter swing a hammer and a sword, and the Ice Climbers swing
 * hammers; all of them are part of the donor's own body model, so a borrower
 * swung an empty hand. While such a move plays, the matching common item
 * model (Beam Sword or Hammer) is drawn along the donor's weapon bone,
 * rebuilt on the borrower's hand (anim_scale.c), where the move's hitboxes
 * are. It is decoration only: not an item, cannot be picked up or dropped.
 * The models come from the common item file that every match already loads.
 * Fighters whose own model already shows that weapon are left alone.
 *
 * Better still, the donor's own model is drawn when it has the part: the
 * Falchion, Master Sword and Sword of Seals, the Ice Climbers' mallet, and
 * tails (Mewtwo's back air gives you Mewtwo's tail). Borrowing a move already
 * loads the donor's default-costume model; a second copy of it is made per
 * borrower, every mesh hidden except the ones riding on rebuilt bones, and
 * posed on the borrower each frame (anim_scale.c). The item models stay as
 * the fallback (Kirby's hammer is not part of his model).
 *
 * Peach's parasol is an article her up special spawns, which other fighters
 * do not spawn (it stalled them); its model is drawn on her hand bone
 * instead, the way the game attaches it.
 *
 * Some donors switch meshes of their own model on for a move instead (the
 * game's model-part groups): Mr. Game & Watch's pan, box, key and bucket,
 * Peach's crown in her hand, Kirby's stone, Yoshi's Egg Roll egg. Those switches are aimed at the
 * donor's groups, so a borrower's own meshes were toggled instead; they are
 * kept per borrower here (Rogue_VisSet) and the donor's meshes drawn. While
 * Kirby's stone shows, the borrower's own body is hidden (Rogue_BodyHidden). */

/* 0 Beam Sword, 1 Hammer (common items), 2 Peach's parasol (her article). */



static bool sword_kind(unsigned kind);
static bool hammer_kind(unsigned kind);

/* One posed copy of the current donor's model per borrower, allocated from
 * the match heap only once a borrower needs one (static storage comes out
 * of every scene's heap: the game is at its memory ceiling). */


/* Model-part groups a donor switches for its moves that are drawn on the
 * borrower: donor, group, lowest variant drawn (lower ones, such as Peach's
 * crown on her head, are not), and whether the borrower's body is hidden. */
typedef struct VisDonor { unsigned char kind, group, from, hide_body; } VisDonor;
static const VisDonor vis_donors[] = {
    { Ft_Kind_GameWatch, 5, 0, 0 }, { Ft_Kind_GameWatch, 6, 0, 0 },
    { Ft_Kind_GameWatch, 7, 0, 0 }, { Ft_Kind_GameWatch, 8, 0, 0 },
    { Ft_Kind_Peach, 4, 1, 0 },
    { Ft_Kind_Peach, 3, 0, 0 }, /* Her forward smash: golf club, frying pan, racket. */
    { Ft_Kind_Kirby, 0, 2, 1 },
    { Ft_Kind_Yoshi, 0, 1, 1 }, /* His Egg Roll egg. */
    /* Not Mr. Game & Watch's groups 2/3 (Judge): routing them to his model
     * hid the number; dropped, his Judge item draws it correctly. */
    /* Samus's Morph Ball (variant 2 of her body group): draw the ball and
     * hide the borrower's body, as Kirby's stone does. */
    { Ft_Kind_Samus, 0, 2, 1 },
};



static int slot_of(const Fighter* fp);
static const VisDonor* vis_donor(unsigned kind, int group)
{
    unsigned i;
    for (i = 0; i < sizeof(vis_donors) / sizeof(vis_donors[0]); ++i)
        if (vis_donors[i].kind == kind && (group < 0 || vis_donors[i].group == group)) return &vis_donors[i];
    return NULL;
}
/* The donor's current variant of `group` for this borrower (-1 none). */
static int vis_get(int slot, unsigned kind, int group)
{
    if (donor_vis_kind[slot] != kind || group < 0 || group >= VIS_GROUPS) return -1;
    return donor_vis[slot][group];
}
/* ftParts_80074B0C / ftParts_80074A4C: a borrowed move switching one of the
 * donor's model-part groups. True when it was kept here. */
bool Rogue_VisSet(HSD_GObj* gobj, int group, int val)
{
    Fighter* fp = GET_FIGHTER(gobj);
    unsigned source;
    int slot;
    if (!fp || !Rogue_IsAbilityState(fp)) return false;
    source = Rogue_AbilitySourceKind(fp);
    if (source == fp->kind) return false;
    /* A donor's model-part switch that is not drawn on the borrower (Samus
     * curling into her Morph Ball) names one of the DONOR's groups; applied
     * to this fighter it would hide parts of its own body (the borrower
     * went invisible). Drop it: the borrower keeps its own look. */
    if (!vis_donor(source, group) || group >= VIS_GROUPS) {
        BAM_LOG("vis_drop kind=%u group=%d variant=%d\n", source, group, val);
        return true;
    }
    slot = slot_of(fp);
    if (slot < 0) return false;
    if (donor_vis_kind[slot] != source) {
        memset(donor_vis[slot], 0xFF, sizeof(donor_vis[slot]));
        donor_vis_kind[slot] = (unsigned char) source;
    }
    donor_vis[slot][group] = (signed char) val;
    /* Diagnostics: Mr. Game & Watch's box, key and horn not showing. */
    BAM_LOG("vis_set kind=%u group=%d variant=%d frame=%d\n", source, group, val, (int) fp->cur_anim_frame);
    return true;
}
/* The variant DObjs (full-detail set) of one of the donor's groups. */
static const FtPartsVisLookup* vis_lookup(unsigned kind, int group)
{
    const FtPartsVisLookup* lookup;
    if (kind >= Ft_Kind_Max || !gFtDataList[kind] || !gFtDataList[kind]->x8) return NULL;
    if ((unsigned) group >= gFtDataList[kind]->x8->x0.model_num || !gFtDataList[kind]->x8->x0.vis_table) return NULL;
    lookup = (const FtPartsVisLookup*) gFtDataList[kind]->x8->x0.vis_table[0][1];
    return lookup ? &lookup[group] : NULL;
}

static void donor_release(DonorModel* m)
{
    unsigned i;
    if (!m) return;
    for (i = 0; i < m->meshes; ++i) {
        HSD_PObj* p;
        if (!m->dropped[i]) continue;
        if (!m->dobj[i]->pobj) {
            m->dobj[i]->pobj = m->dropped[i];
            continue;
        }
        for (p = m->dobj[i]->pobj; p->next; p = p->next) {}
        p->next = m->dropped[i];
    }
    if (m->root) HSD_JObjRemoveAll(m->root);
    memset(m, 0, sizeof(*m));
    m->kind = Ft_Kind_Max;
}
static void donor_free(unsigned slot)
{
    if (!donor_models[slot]) return;
    donor_release(donor_models[slot]);
    OSFreeToHeap(HSD_GetHeap(), donor_models[slot]);
    donor_models[slot] = NULL;
}
/* The `index`th DObj as the fighter numbers them, or NULL. */
static HSD_DObj* dobj_at(DonorModel* m, unsigned index)
{
    unsigned i, d = 0;
    for (i = 0; i < m->joints; ++i) {
        HSD_DObj* dobj;
        if (m->jobj[i]->flags & (JOBJ_SPLINE | JOBJ_PTCL)) continue;
        for (dobj = HSD_JObjGetDObj(m->jobj[i]); dobj; dobj = dobj->next, ++d)
            if (d == index) return dobj;
    }
    return NULL;
}
static void donor_index(DonorModel* m, HSD_JObj* j, unsigned parent)
{
    while (j && m->joints < DONOR_JOINTS) {
        unsigned self = m->joints++;
        m->jobj[self] = j;
        m->parent[self] = (unsigned char) (parent < DONOR_JOINTS ? parent : 0xFF);
        if (HSD_JObjGetChild(j)) donor_index(m, HSD_JObjGetChild(j), self);
        j = HSD_JObjGetNext(j);
    }
}
static void cut_pobjs(DonorModel* m, unsigned i, HSD_DObj* dobj, unsigned mask)
{
    HSD_PObj *p = dobj->pobj, *next, *keep = NULL, *keep_tail = NULL, *drop = NULL, *drop_tail = NULL;
    unsigned k;
    for (k = 0; p; p = next, ++k) {
        next = p->next;
        p->next = NULL;
        if (k < 16 && (mask & (1U << k))) {
            if (keep_tail) keep_tail->next = p;
            else keep = p;
            keep_tail = p;
        } else {
            if (drop_tail) drop_tail->next = p;
            else drop = p;
            drop_tail = p;
        }
    }
    dobj->pobj = keep;
    m->dropped[i] = drop;
}
/* The donor's model for this borrower, loaded on first use: every mesh hidden
 * except the listed ones (shown per frame). NULL if the donor has none. */
/* Mr. Game & Watch's model is white; his color is set on its materials at
 * load (ftGw_Init_OnLoad: GAMEWATCH_COLOR of his costume, the first here),
 * and his articles ask their owner for it (ftLib_8008770C). */
static void gw_color(HSD_JObj* j)
{
    const GXColor* c = (const GXColor*) (bam_match->donor_attrs[Ft_Kind_GameWatch].bytes + 4);
    for (; j; j = j->next) {
        HSD_DObj* d;
        if (!(j->flags & (JOBJ_SPLINE | JOBJ_PTCL)))
            for (d = HSD_JObjGetDObj(j); d; d = d->next)
                if (d->mobj && d->mobj->mat) d->mobj->mat->diffuse = *c;
        if (!(j->flags & JOBJ_INSTANCE)) gw_color(j->child);
    }
}
/* ftLib_8008770C / ftLib_80087744 (platform fix): the color an article of
 * Mr. Game & Watch's takes from its owner. A borrower holding his moves
 * gives his (first costume's) color and outline, not Kirby's copy colors
 * the game falls back to for anyone else (they came out white). */
int Rogue_DonorItemColor(HSD_GObj* gobj, void* dst, int outline)
{
    Fighter* fp = GET_FIGHTER(gobj);
    RogueFighterState* S;
    if (!fp || fp->kind == Ft_Kind_GameWatch || !bam_match) return 0;
    S = Rogue_FighterCtx(fp);
    if (S->fighter != fp || !S->loaded_sources[Ft_Kind_GameWatch]) return 0;
    if (fp->kind == Ft_Kind_Kirby && !Rogue_IsAbilityState(fp)) return 0; /* his own copy ability */
    *(GXColor*) dst = *(const GXColor*) (bam_match->donor_attrs[Ft_Kind_GameWatch].bytes + (outline ? 0x14 : 4));
    return 1;
}

static DonorModel* donor_model(unsigned slot, unsigned kind)
{
    DonorModel* m = donor_models[slot];
    unsigned char groups[DONOR_MESHES];
    unsigned short wanted[DONOR_MESHES], pobjs[DONOR_MESHES];
    unsigned count, i, d, k;
    HSD_Joint* desc;
    if (m && m->kind == kind) return m->root ? m : NULL;
    count = Rogue_DonorMeshes(kind, groups, wanted, pobjs, DONOR_MESHES);
    if (!m) {
        if ((!count && !vis_donor(kind, -1)) || kind >= Ft_Kind_Max) return NULL;
        m = OSAllocFromHeap(HSD_GetHeap(), sizeof(DonorModel));
        if (!m) return NULL;
        memset(m, 0, sizeof(*m));
        donor_models[slot] = m;
    }
    donor_release(m);
    m->kind = kind;
    if ((!count && !vis_donor(kind, -1)) || kind >= Ft_Kind_Max) return NULL;
    desc = CostumeListsForeachCharacter[kind].costume_list[0].joint;
    if (!desc) desc = part_joint[kind];
    if (!desc) {
        BAM_LOG("donor_model kind=%u not loaded\n", kind);
        return NULL;
    }
    ftPartsPObjSetDefaultClass();
    m->root = HSD_JObjLoadJoint(desc);
    ftPartsPObjClearDefaultClass();
    if (!m->root) return NULL;
    if (kind == Ft_Kind_GameWatch && bam_match) gw_color(m->root);
    donor_index(m, m->root, 0xFF);
    /* DObjs are numbered as the fighter numbers them: joint order, then each
     * joint's DObj chain (spline and particle joints carry none). */
    for (i = 0, d = 0; i < m->joints; ++i) {
        HSD_DObj* dobj;
        if (m->jobj[i]->flags & (JOBJ_SPLINE | JOBJ_PTCL)) continue;
        for (dobj = HSD_JObjGetDObj(m->jobj[i]); dobj; dobj = dobj->next, ++d) {
            HSD_DObjSetFlags(dobj, DOBJ_HIDDEN);
            for (k = 0; k < count; ++k)
                if (wanted[k] == d && m->meshes < DONOR_MESHES) {
                    m->dobj[m->meshes] = dobj;
                    if (pobjs[k]) cut_pobjs(m, m->meshes, dobj, pobjs[k]);
                    m->group[m->meshes++] = groups[k];
                }
        }
    }
    BAM_LOG("donor_model kind=%u joints=%u meshes=%u of %u\n", kind, m->joints, m->meshes, count);
    return m;
}
/* A donor whose moves draw part of its model (a tail, a sword, Mr. Game &
 * Watch's props) needs its default costume. It is only drawn, so it never
 * decides what a move does: it is loaded once the scene created every
 * fighter and their borrowed moves (Rogue_DonorModelsLoad), from what the
 * match heap can spare, and right away when the CSS preloaded it. */
static u8 model_wanted[Ft_Kind_Max];
void Rogue_DonorModelPreload(unsigned kind)
{
    unsigned char groups[DONOR_MESHES];
    unsigned short dobjs[DONOR_MESHES];
    extern Fighter_CostumeStrings* ftData_803C2360[Ft_Kind_Max];
    extern HSD_Archive* lbDvd_8001819C(const char* basename);
    if (kind >= Ft_Kind_Max || CostumeListsForeachCharacter[kind].costume_list[0].joint) return;
    if (!Rogue_DonorMeshes(kind, groups, dobjs, NULL, DONOR_MESHES) && !vis_donor(kind, -1)) return;
    if (ftData_803C2360[kind] && ftData_803C2360[kind][0].dat_filename &&
        lbDvd_8001819C(ftData_803C2360[kind][0].dat_filename)) {
        ftData_80085820(kind, 0);
        BAM_LOG("donor_model_load kind=%u (cached)\n", kind);
        return;
    }
    model_wanted[kind] = 1;
}

/* The donor's default costume into the preload-cache block (bam_cache.c),
 * parsed as ftData_80085820 would; 0 when the block has no room. */
static int model_into_cache(unsigned kind, const Fighter_CostumeStrings* cs)
{
    UnkCostumeStruct* c = &CostumeListsForeachCharacter[kind].costume_list[0];
    unsigned size = (unsigned) lbFileGetSize(cs->dat_filename);
    size_t length = 0;
    u8* buf;
    HSD_Archive* arc;
    if (!size || BamCache_Room(1) < OSRoundUp32B(size) + 0x60) return 0;
    buf = BamCache_Alloc(1, OSRoundUp32B(size));
    arc = BamCache_Alloc(1, sizeof(HSD_Archive));
    if (!buf || !arc) return 0;
    memset(arc, 0, sizeof(HSD_Archive));
    lbFile_8001668C(cs->dat_filename, buf, &length);
    lbArchive_InitializeDAT(arc, buf, length);
    c->joint = HSD_ArchiveGetPublicAddress(arc, cs->joint_name);
    c->x4 = cs->matanim_joint_name ? HSD_ArchiveGetPublicAddress(arc, cs->matanim_joint_name) : NULL;
    c->x14_archive = arc;
    if (!c->joint) {
        c->x4 = NULL;
        c->x14_archive = NULL;
        return 0;
    }
    BAM_LOG("donor_model_load kind=%u %u KB in cache\n", kind, size / 1024);
    return 1;
}

/* The donor's trimmed model (tools/bam/parts.py writes Pl<code>Bm.dat into
 * the patched disc): its whole skeleton and every mesh in order, but only the
 * meshes borrowed moves draw keep their geometry and textures, 30-110 KB
 * instead of the 130-790 KB costume. It goes in the preload-cache block when
 * there is room, else the match heap down to a lower floor than the full
 * costume (it is that much smaller). 0 when the disc has none (an image
 * patched by an older tool) or there is no room. */
#define PARTS_FLOOR 0x100000
#include <dolphin/dvd.h>
#include <sysdolphin/baselib/memory.h>
static int parts_load(unsigned kind, const Fighter_CostumeStrings* cs)
{
    char name[16];
    unsigned size, i;
    size_t length = 0;
    u8* buf = NULL;
    HSD_Archive* arc = NULL;
    HSD_Joint* joint;
    for (i = 0; cs->dat_filename[i] && i < sizeof(name) - 1; ++i) name[i] = cs->dat_filename[i];
    name[i] = 0;
    if (i != 10 || name[4] != 'N' || name[5] != 'r') return 0; /* Pl<code>Nr.dat */
    name[4] = 'B';
    name[5] = 'm';
    if (DVDConvertPathToEntrynum(lbFileGetFullName(name)) < 0) return 0;
    size = (unsigned) lbFileGetSize(name);
    if (!size) return 0;
    if (BamCache_Room(1) >= OSRoundUp32B(size) + 0x60) {
        buf = BamCache_Alloc(1, OSRoundUp32B(size));
        arc = BamCache_Alloc(1, sizeof(HSD_Archive));
    } else if (Bam_HeapRoom() >= size + PARTS_FLOOR) {
        buf = HSD_MemAlloc(OSRoundUp32B(size));
        arc = HSD_MemAlloc(sizeof(HSD_Archive));
    }
    if (!buf || !arc) {
        BAM_NOTE("donor parts kind=%u skipped (%u KB free, parts %u KB)\n", kind, Bam_HeapRoom() / 1024, size / 1024);
        return 0;
    }
    memset(arc, 0, sizeof(HSD_Archive));
    lbFile_8001668C(name, buf, &length);
    /* HSD_ArchiveParse halts the game on a malformed archive: check its
     * header first and fall back to the full costume. */
    if (length < 0x20 || *(u32*) buf != length || ((u32*) buf)[1] + 0x20 > length) {
        BAM_NOTE("donor parts kind=%u: %s is malformed (%u bytes, header %u)\n", kind, name, (unsigned) length,
                 *(u32*) buf);
        return 0;
    }
    lbArchive_InitializeDAT(arc, buf, length);
    joint = HSD_ArchiveGetPublicAddress(arc, cs->joint_name);
    if (!joint) {
        BAM_NOTE("donor parts kind=%u: %s has no %s\n", kind, name, cs->joint_name);
        return 0;
    }
    part_joint[kind] = joint;
    BAM_LOG("donor_parts_load kind=%u %u KB\n", kind, size / 1024);
    return 1;
}

void Rogue_DonorModelsLoad(void)
{
    extern Fighter_CostumeStrings* ftData_803C2360[Ft_Kind_Max];
    unsigned kind;
    for (kind = 0; kind < Ft_Kind_Max; ++kind) {
        unsigned size, room;
        if (!model_wanted[kind]) continue;
        model_wanted[kind] = 0;
        if (CostumeListsForeachCharacter[kind].costume_list[0].joint) continue;
        if (!ftData_803C2360[kind] || !ftData_803C2360[kind][0].dat_filename) continue;
        if (bam_sword_visual && parts_load(kind, &ftData_803C2360[kind][0])) continue;
        if (model_into_cache(kind, &ftData_803C2360[kind][0])) continue;
        size = (unsigned) lbFileGetSize(ftData_803C2360[kind][0].dat_filename);
        room = Bam_HeapRoom();
        if (room < size + BAM_HEAP_FLOOR) {
            BAM_NOTE("donor_model_load kind=%u skipped (%u KB free, model %u KB)\n", kind, room / 1024, size / 1024);
            continue;
        }
        ftData_80085820(kind, 0);
        BAM_LOG("donor_model_load kind=%u %u KB\n", kind, size / 1024);
    }
}

/* Whether the donor has switched on one of its drawn group variants. */
static bool vis_active(int slot, unsigned kind)
{
    unsigned i;
    for (i = 0; i < sizeof(vis_donors) / sizeof(vis_donors[0]); ++i)
        if (vis_donors[i].kind == kind && vis_get(slot, kind, vis_donors[i].group) >= vis_donors[i].from) return true;
    return false;
}
/* Shows the donor's switched group variants on its model; true if any. */
static bool vis_show(DonorModel* m, int slot, unsigned kind)
{
    unsigned i;
    bool any = false;
    for (i = 0; i < sizeof(vis_donors) / sizeof(vis_donors[0]); ++i) {
        const VisDonor* v = &vis_donors[i];
        const FtPartsVisLookup* lookup;
        int cur, j, k;
        if (v->kind != kind) continue;
        if (!(lookup = vis_lookup(kind, v->group))) {
            static unsigned char told;
            if (!told) BAM_LOG("vis_show kind=%u group=%d no lookup\n", kind, v->group);
            told = 1;
            continue;
        }
        cur = vis_get(slot, kind, v->group);
        /* Only the variants drawn for moves: lower ones (Yoshi's body, whose
         * tail DObj is also a mesh row) stay as the mesh rows set them. */
        for (j = v->from; j < lookup->x0; ++j) {
            const TempS* variant = &lookup->x4[j];
            bool show = j == cur && cur >= v->from;
            for (k = 0; k < variant->x0; ++k) {
                HSD_DObj* dobj = dobj_at(m, variant->x4[k]);
                if (!dobj) continue;
                if (show) HSD_DObjClearFlags(dobj, DOBJ_HIDDEN);
                else HSD_DObjSetFlags(dobj, DOBJ_HIDDEN);
            }
            any |= show && variant->x0 > 0;
        }
    }
    return any;
}
/* Draws the donor's parts for the current move; returns the weapon items
 * (bit per item) the donor's own model replaced. */
static unsigned donor_display(Fighter* fp, unsigned slot, int pass, MtxPtr vmtx)
{
    unsigned char item_of[8];
    unsigned mask, i, replaced = 0, source = Rogue_AbilitySourceKind(fp);
    DonorModel* m;
    memset(item_of, 0xFF, sizeof(item_of));
    mask = Rogue_DonorMeshShow(fp, item_of);
    for (i = 0; i < 8; ++i) {
        if (!(mask & (1U << i))) continue;
        /* A fighter whose own model shows that weapon keeps its own. */
        if ((item_of[i] == 0 && sword_kind(fp->kind)) || (item_of[i] == 1 && hammer_kind(fp->kind)))
            mask &= ~(1U << i);
    }
    /* The model copy is only made once something of it shows. */
    if (!mask && !vis_active((int) slot, source)) return 0;
    m = donor_model(slot, source);
    if (!m) return 0;
    for (i = 0; i < m->meshes; ++i) {
        if (mask & (1U << m->group[i])) {
            HSD_DObjClearFlags(m->dobj[i], DOBJ_HIDDEN);
            if (item_of[m->group[i]] < WEAPON_ITEMS) replaced |= 1U << item_of[m->group[i]];
        } else {
            HSD_DObjSetFlags(m->dobj[i], DOBJ_HIDDEN);
        }
    }
    if (!vis_show(m, (int) slot, source) && !mask) return 0;
    Rogue_DonorPose(fp, m->jobj, m->parent, m->joints, -1, mask);
    HSD_JObjDispAll(m->root, vmtx, HSD_GObj_80390EB8(pass), 0);
    return replaced;
}

/* The fighter's own body is not drawn: a borrowed move turned it into the
 * donor's (Kirby's stone), drawn in its place. */
bool Rogue_BodyHidden(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    unsigned i, source;
    int slot;
    if (!fp || !Rogue_IsAbilityState(fp)) return false;
    source = Rogue_AbilitySourceKind(fp);
    slot = slot_of(fp);
    if (slot < 0 || source == fp->kind || !donor_models[slot] || donor_models[slot]->kind != source ||
        !donor_models[slot]->root)
        return false;
    for (i = 0; i < sizeof(vis_donors) / sizeof(vis_donors[0]); ++i)
        if (vis_donors[i].kind == source && vis_donors[i].hide_body &&
            vis_get(slot, source, vis_donors[i].group) >= vis_donors[i].from)
            return true;
    return false;
}

static bool sword_kind(unsigned kind)
{
    return kind == Ft_Kind_Mars || kind == Ft_Kind_Emblem || kind == Ft_Kind_Link || kind == Ft_Kind_CLink;
}
static bool hammer_kind(unsigned kind)
{
    return kind == Ft_Kind_Popo || kind == Ft_Kind_Nana;
}

static int slot_of(const Fighter* fp)
{
    unsigned i;
    for (i = 0; i < BAM_FIGHTERS; ++i)
        if (bam_match->fighters[i].fighter == fp) return (int) i;
    return -1;
}

/* A new borrowed move starts with none of the donor's groups switched:
 * each move's script switches the ones it shows (Peach's club stayed in
 * her hand after an interrupted forward smash). */
void Rogue_VisReset(const Fighter* fp)
{
    int slot;
    if (!bam_sword_visual || !bam_match) return;
    slot = slot_of(fp);
    if (slot >= 0) donor_vis_kind[slot] = Ft_Kind_Max;
}

static void release_slot(unsigned slot)
{
    unsigned k;
    for (k = 0; k < WEAPON_ITEMS; ++k) {
        if (weapons[slot][k]) HSD_JObjRemoveAll(weapons[slot][k]);
        weapons[slot][k] = NULL;
    }
    donor_free(slot);
    donor_vis_kind[slot] = Ft_Kind_Max;
    parasol_float[slot] = 0;
    weapon_owner[slot] = NULL;
}


/* Rest transform from an item model's root to its `id`th joint (depth
 * first), the joint the game attaches to the fighter. */
static bool attach_rest(HSD_JObj* j, int* id, Mtx parent, Mtx out)
{
    for (; j; j = HSD_JObjGetNext(j)) {
        Mtx local, world;
        if (j->flags & JOBJ_USE_QUATERNION) HSD_MtxSRTQuat(local, &j->scale, &j->rotate, &j->translate, NULL);
        else HSD_MtxSRT(local, &j->scale, (Vec3*) &j->rotate, &j->translate, NULL);
        if (parent) PSMTXConcat(parent, local, world);
        else PSMTXIdentity(world); /* The root itself is placed directly. */
        if ((*id)-- == 0) {
            PSMTXCopy(world, out);
            return true;
        }
        if (HSD_JObjGetChild(j) && attach_rest(HSD_JObjGetChild(j), id, world, out)) return true;
    }
    return false;
}
static HSD_JObj* weapon_model(unsigned slot, int item)
{
    static const ItemKind kinds[WEAPON_ITEMS] = { It_Kind_Sword, It_Kind_Hammer, It_Kind_Peach_Parasol };
    if (!weapons[slot][item]) {
        ItemKind kind = kinds[item];
        Article* article;
        Mtx rest;
        int id;
        if (kind >= It_Kind_Kuriboh) article = it_804D6D38 ? it_804D6D38[kind - It_Kind_Kuriboh] : NULL;
        else article = it_804D6D24 ? it_804D6D24[kind] : NULL;
        if (!article || !article->x10_modelDesc || !article->x10_modelDesc->x0_joint) return NULL;
        weapons[slot][item] = HSD_JObjLoadJoint(article->x10_modelDesc->x0_joint);
        PSMTXIdentity(weapon_attach[slot][item]);
        id = kind >= It_Kind_Kuriboh ? article->x10_modelDesc->x8_bone_attach_id : 0;
        if (weapons[slot][item] && id > 0 && attach_rest(weapons[slot][item], &id, NULL, rest))
            PSMTXInverse(rest, weapon_attach[slot][item]);
    }
    return weapons[slot][item];
}

/* Peach's up special ends with her parasol open: holding it, she floats
 * down (the common parasol fall). A borrower holds no parasol item, so the
 * float is kept here: set while the borrowed up special shows the parasol,
 * it lasts through the special fall that follows until the fighter lands,
 * leaves that state or closes it (stick down). The parasol stays drawn in
 * the hand meanwhile. */

/* The float keeps the parasol where the borrowed up special left it on the
 * body (the root), not in the hand: the special fall's own pose has the
 * hand down, and the parasol hung sideways with its hitbox in the body. */
static int parasol_base(Fighter* fp)
{
    return fp->parts[0].joint ? 0 : -1;
}
void ftAction_8007121C(Fighter_GObj* gobj, CommandInfo* cmd);

/* Peach's open parasol hits (a weak hit made by her ItemParasolOpen script
 * that lasts through the fall). The borrower's float gets the same hitbox,
 * on the canopy of the parasol drawn in its hand. */
static union CmdUnion* parasol_hit_cmd(void)
{
    /* Words per script command: 0x00-0x09 (common), 0x0A on (fighter). */
    static const u8 common[10] = { 1, 1, 1, 1, 1, 2, 1, 2, 1, 1 };
    static const u8 fighter[] = { 5, 5, 1, 1, 1, 1, 1, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3,
                                  1, 1, 1, 7, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 2, 1, 4 };
    ftData* d = gFtDataList[Ft_Kind_Peach];
    int anim;
    u32* p;
    unsigned i;
    if (!d || !d->xC) return NULL;
    anim = ftPe_Init_MotionStateTable[ftPe_MS_ItemParasolOpen - ftCo_MS_Count].anim_id;
    if (anim < 0) return NULL;
    p = (u32*) ((Fighter_WaitAnimData*) d->xC)[anim].xC;
    for (i = 0; p && i < 32; ++i) {
        unsigned op = *p >> 26;
        if (op == 0x0B) return (union CmdUnion*) p;
        if (op == 0 || (op >= 10 && op - 10 >= sizeof(fighter))) return NULL;
        p += op < 10 ? common[op] : fighter[op - 10];
    }
    return NULL;
}

static void parasol_hit_off_set(Fighter* fp, int slot, int hand, Mtx parasol)
{
    union CmdUnion* cmd = parasol_hit_cmd();
    Mtx inv, rel;
    Vec3 off;
    if (!cmd || !PSMTXInverse(HSD_JObjGetMtxPtr(fp->parts[hand].joint), inv)) return;
    off.x = 0.003906f * cmd[1].create_hitbox_1.z_offset;
    off.y = 0.003906f * cmd[2].create_hitbox_2.y_offset;
    off.z = 0.003906f * cmd[2].create_hitbox_2.x_offset;
    PSMTXConcat(inv, parasol, rel);
    PSMTXMultVec(rel, &off, &parasol_hit_off[slot]);
}

/* The float ends. A state change has already removed the hitbox; closing
 * the parasol (still in the special fall) removes it here. */
static void parasol_hit_off_clear(Fighter* fp, int slot, int disable)
{
    if (parasol_hit[slot] && disable) {
        HitCapsule* h = &fp->x914[parasol_hit[slot] - 1];
        int hand = parasol_base(fp);
        if (hand >= 0 && h->jobj == fp->parts[hand].joint) h->state = HitCapsule_Disabled;
    }
    parasol_hit[slot] = 0;
}

/* Every frame (BAM_OnFrame), part of the simulation: whether the next
 * special fall is a parasol float, and where its hitbox goes. */
void Rogue_ParasolTrack(Fighter* fp)
{
    int slot = slot_of(fp), hand;
    Mtx w;
    if (slot < 0) return;
    if (Rogue_IsAbilityState(fp)) {
        if (Rogue_PropWeaponMtx(fp, w) != 2 || (hand = parasol_base(fp)) < 0) return;
        parasol_float[slot] = 1;
        parasol_hit_off_set(fp, slot, hand, w);
        return;
    }
    if (parasol_float[slot] && (fp->motion_id != ftCo_MS_FallSpecial || fp->ground_or_air != GA_Air)) {
        parasol_hit_off_clear(fp, slot, 0);
        parasol_float[slot] = 0;
    }
}

/* ftCo_800CEFE0, where Peach's up special opens her parasol: a borrower
 * goes into the special fall instead, floating on the parasol kept here (the
 * common held-parasol states it went to need the parasol item, which goes
 * away with the borrowed move: the parasol vanished and could not be
 * closed). True when it did. */
bool Rogue_ParasolOpen(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int slot = slot_of(fp);
    ftPe_DatAttrs* da;
    if (slot < 0 || fp->kind == Ft_Kind_Peach || !Rogue_IsAbilityState(fp) ||
        Rogue_AbilitySourceKind(fp) != Ft_Kind_Peach)
        return false;
    da = fp->dat_attrs;
    parasol_float[slot] = 1;
    ftCo_80096900(gobj, 0, 1, false, da->x70, da->x74);
    return true;
}

/* ftCo_FallSpecial_Phys: true while the borrower floats on Peach's parasol. */
bool Rogue_ParasolFloat(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int slot = slot_of(fp), hand;
    if (slot < 0 || !parasol_float[slot]) return false;
    if (fp->motion_id != ftCo_MS_FallSpecial || fp->ground_or_air != GA_Air ||
        fp->input.lstick[0].y <= p_ftCommonData->close_parasol_threshold) {
        parasol_hit_off_clear(fp, slot, fp->motion_id == ftCo_MS_FallSpecial);
        parasol_float[slot] = 0;
        if (fp->motion_id == ftCo_MS_FallSpecial && fp->ground_or_air == GA_Air &&
            fp->input.lstick[0].y <= -p_ftCommonData->x88 && !fp->fall_fast) {
            /* Closing it with a tap down drops into a fast fall at once
             * (the float's slow fall, or its rise, kept the special fall's
             * fast fall check from ever passing). */
            fp->fall_fast = true;
            fp->mv.co.fallspecial.xC = 1;
            ft_PlaySFX(fp, 0x96, 0x7F, 0x40);
        }
        return false;
    }
    if (!parasol_hit[slot] && (hand = parasol_base(fp)) >= 0) {
        union CmdUnion* cmd = parasol_hit_cmd();
        if (cmd) {
            CommandInfo ci;
            HitCapsule* h;
            memset(&ci, 0, sizeof(ci));
            ci.u = cmd;
            ftAction_8007121C(gobj, &ci);
            h = &fp->x914[cmd->create_hitbox_0.id];
            h->jobj = fp->parts[hand].joint;
            h->b_offset = parasol_hit_off[slot];
            parasol_hit[slot] = (unsigned char) (cmd->create_hitbox_0.id + 1);
            BAM_LOG("parasol hit id=%d off=(%.2f,%.2f,%.2f)\n", (int) cmd->create_hitbox_0.id,
                    h->b_offset.x, h->b_offset.y, h->b_offset.z);
        }
    }
    return true;
}
static void parasol_display(Fighter* fp, int slot, int pass, MtxPtr vmtx)
{
    HSD_JObj* model;
    Mtx place;
    int hand = parasol_base(fp);
    if (fp->motion_id != ftCo_MS_FallSpecial || fp->ground_or_air != GA_Air || hand < 0) return;
    model = weapon_model((unsigned) slot, 2);
    if (!model) return;
    PSMTXConcat(HSD_JObjGetMtxPtr(fp->parts[hand].joint), parasol_rel[slot], place);
    HSD_JObjCopyMtx(model, place);
    model->flags |= JOBJ_USER_DEF_MTX | JOBJ_MTX_INDEP_PARENT | JOBJ_MTX_INDEP_SRT;
    HSD_JObjSetMtxDirty(model);
    HSD_JObjDispAll(model, vmtx, HSD_GObj_80390EB8(pass), 0);
}

void Rogue_SwordDisplay(HSD_GObj* gobj, int pass, MtxPtr vmtx)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int slot, hand, item;
    unsigned source, replaced;
    HSD_JObj* model;
    Mtx place;
    slot = slot_of(fp);
    if (slot < 0) return;
    if (!Rogue_IsAbilityState(fp)) {
        donor_vis_kind[slot] = Ft_Kind_Max;
        if (parasol_float[slot]) parasol_display(fp, slot, pass, vmtx);
        return;
    }
    if (weapon_owner[slot] != fp) {
        release_slot((unsigned) slot);
        weapon_owner[slot] = fp;
    }
    source = Rogue_AbilitySourceKind(fp);
    replaced = donor_display(fp, (unsigned) slot, pass, vmtx);
    item = Rogue_PropWeaponMtx(fp, place);
    if (item == 2 && (hand = parasol_base(fp)) >= 0 && weapon_model((unsigned) slot, 2)) {
        /* Where the parasol sits on the hand, for the float that follows,
         * even while the donor's own model draws it: unset, the float drew
         * it at no size (invisible). */
        Mtx inv, at;
        PSMTXConcat(place, weapon_attach[slot][2], at);
        if (PSMTXInverse(HSD_JObjGetMtxPtr(fp->parts[hand].joint), inv)) PSMTXConcat(inv, at, parasol_rel[slot]);
    }
    if (item >= 0 && (replaced & (1U << item))) return;
    if (item < 0 && (replaced & 1U)) return;
    if (item < 0) {
        /* No weapon bone for this move: a sword donor's sword still goes in
         * the item hand. */
        if (!sword_kind(source) || !fp->ft_data || !fp->ft_data->x8) return;
        hand = fp->ft_data->x8->x10;
        if (hand < 0 || (unsigned) hand >= ftPartsTable[fp->kind]->parts_num || !fp->parts[hand].joint) return;
        item = 0;
        PSMTXCopy(HSD_JObjGetMtxPtr(fp->parts[hand].joint), place);
    }
    if (item >= WEAPON_ITEMS || (item == 0 && sword_kind(fp->kind)) || (item == 1 && hammer_kind(fp->kind))) return;
    model = weapon_model((unsigned) slot, item);
    if (!model) return;
    PSMTXConcat(place, weapon_attach[slot][item], place);
    HSD_JObjCopyMtx(model, place);
    model->flags |= JOBJ_USER_DEF_MTX | JOBJ_MTX_INDEP_PARENT | JOBJ_MTX_INDEP_SRT;
    HSD_JObjSetMtxDirty(model);
    HSD_JObjDispAll(model, vmtx, HSD_GObj_80390EB8(pass), 0);
}

/* The fighter is going away (match end, destroyed): free its weapons. */
void Rogue_SwordRelease(const Fighter* fp)
{
    unsigned i;
    for (i = 0; i < BAM_FIGHTERS; ++i)
        if (weapon_owner[i] == fp || !fp) release_slot(i);
}

#include <sysdolphin/baselib/memory.h>
/* Called from Bam_MatchBegin / Bam_MatchEnd (bam_fighter.c). */
void Rogue_SwordVisualMatchBegin(void)
{
    bam_sword_visual = HSD_MemAlloc(sizeof(*bam_sword_visual));
    memset(bam_sword_visual, 0, sizeof(*bam_sword_visual));
}
void Rogue_SwordVisualMatchEnd(void)
{
    bam_sword_visual = NULL;
}
