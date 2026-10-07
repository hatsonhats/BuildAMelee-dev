#include <engine/visual/internal.h>
#include <melee/ft/kinds/ftGameWatch/types.h>

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
    if (bam_visual->donor_vis_kind[slot] != kind || group < 0 || group >= VIS_GROUPS) return -1;
    return bam_visual->donor_vis[slot][group];
}
/* ftParts_80074B0C / ftParts_80074A4C: a borrowed move switching one of the
 * donor's model-part groups. True when it was kept here. */
bool Bam_VisSet(HSD_GObj* gobj, int group, int val)
{
    Fighter* fp = GET_FIGHTER(gobj);
    unsigned source;
    int slot;
    if (!fp || !Bam_IsAbilityState(fp)) return false;
    source = Bam_AbilitySourceKind(fp);
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
    if (bam_visual->donor_vis_kind[slot] != source) {
        memset(bam_visual->donor_vis[slot], 0xFF, sizeof(bam_visual->donor_vis[slot]));
        bam_visual->donor_vis_kind[slot] = (unsigned char) source;
    }
    bam_visual->donor_vis[slot][group] = (signed char) val;
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
void donor_free(unsigned slot)
{
    if (!bam_visual->donor_models[slot]) return;
    donor_release(bam_visual->donor_models[slot]);
    OSFreeToHeap(HSD_GetHeap(), bam_visual->donor_models[slot]);
    bam_visual->donor_models[slot] = NULL;
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
    const GXColor* c = &((const ftGameWatchAttributes*) bam_match->donor_attrs[Ft_Kind_GameWatch].bytes)->x4_GAMEWATCH_COLOR[0];
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
 * the game falls back to for anyone else (white). */
int Bam_DonorItemColor(HSD_GObj* gobj, void* dst, int outline)
{
    Fighter* fp = GET_FIGHTER(gobj);
    BamFighterState* S;
    if (!fp || fp->kind == Ft_Kind_GameWatch || !bam_match) return 0;
    S = Bam_FighterCtx(fp);
    if (S->fighter != fp || !S->loaded_sources[Ft_Kind_GameWatch]) return 0;
    if (fp->kind == Ft_Kind_Kirby && !Bam_IsAbilityState(fp)) return 0; /* his own copy ability */
    {
        const ftGameWatchAttributes* a = (const ftGameWatchAttributes*) bam_match->donor_attrs[Ft_Kind_GameWatch].bytes;
        *(GXColor*) dst = outline ? a->x14_GAMEWATCH_OUTLINE : a->x4_GAMEWATCH_COLOR[0];
    }
    return 1;
}

static DonorModel* donor_model(unsigned slot, unsigned kind)
{
    DonorModel* m = bam_visual->donor_models[slot];
    unsigned char groups[DONOR_MESHES];
    unsigned short wanted[DONOR_MESHES], pobjs[DONOR_MESHES];
    unsigned count, i, d, k;
    HSD_Joint* desc;
    if (m && m->kind == kind) return m->root ? m : NULL;
    count = Bam_DonorMeshes(kind, groups, wanted, pobjs, DONOR_MESHES);
    if (!m) {
        if ((!count && !vis_donor(kind, -1)) || kind >= Ft_Kind_Max) return NULL;
        m = OSAllocFromHeap(HSD_GetHeap(), sizeof(DonorModel));
        if (!m) return NULL;
        memset(m, 0, sizeof(*m));
        bam_visual->donor_models[slot] = m;
    }
    donor_release(m);
    m->kind = kind;
    if ((!count && !vis_donor(kind, -1)) || kind >= Ft_Kind_Max) return NULL;
    desc = CostumeListsForeachCharacter[kind].costume_list[0].joint;
    if (!desc) desc = bam_visual->part_joint[kind];
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
 * fighter and their borrowed moves (Bam_DonorModelsLoad), from what the
 * match heap can spare, and right away when the CSS preloaded it. */
static u8 model_wanted[Ft_Kind_Max];
void Bam_DonorModelPreload(unsigned kind)
{
    unsigned char groups[DONOR_MESHES];
    unsigned short dobjs[DONOR_MESHES];
    extern Fighter_CostumeStrings* ftData_803C2360[Ft_Kind_Max];
    extern HSD_Archive* lbDvd_8001819C(const char* basename);
    if (kind >= Ft_Kind_Max || CostumeListsForeachCharacter[kind].costume_list[0].joint) return;
    if (!Bam_DonorMeshes(kind, groups, dobjs, NULL, DONOR_MESHES) && !vis_donor(kind, -1)) return;
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
        BAM_NOTE("memory: donor parts kind=%u skipped (%u KB free, parts %u KB)\n", kind, Bam_HeapRoom() / 1024, size / 1024);
        return 0;
    }
    memset(arc, 0, sizeof(HSD_Archive));
    lbFile_8001668C(name, buf, &length);
    /* HSD_ArchiveParse halts the game on a malformed archive: check its
     * header first and fall back to the full costume. */
    if (length < 0x20 || *(u32*) buf != length || ((u32*) buf)[1] + 0x20 > length) {
        BAM_NOTE("parts: kind=%u: %s is malformed (%u bytes, header %u)\n", kind, name, (unsigned) length,
                 *(u32*) buf);
        return 0;
    }
    lbArchive_InitializeDAT(arc, buf, length);
    joint = HSD_ArchiveGetPublicAddress(arc, cs->joint_name);
    if (!joint) {
        BAM_NOTE("parts: kind=%u: %s has no %s\n", kind, name, cs->joint_name);
        return 0;
    }
    bam_visual->part_joint[kind] = joint;
    BAM_LOG("donor_parts_load kind=%u %u KB\n", kind, size / 1024);
    return 1;
}

void Bam_DonorModelsLoad(void)
{
    extern Fighter_CostumeStrings* ftData_803C2360[Ft_Kind_Max];
    unsigned kind;
    for (kind = 0; kind < Ft_Kind_Max; ++kind) {
        unsigned size, room;
        if (!model_wanted[kind]) continue;
        model_wanted[kind] = 0;
        if (CostumeListsForeachCharacter[kind].costume_list[0].joint) continue;
        if (!ftData_803C2360[kind] || !ftData_803C2360[kind][0].dat_filename) continue;
        if (bam_visual && parts_load(kind, &ftData_803C2360[kind][0])) continue;
        if (model_into_cache(kind, &ftData_803C2360[kind][0])) continue;
        size = (unsigned) lbFileGetSize(ftData_803C2360[kind][0].dat_filename);
        room = Bam_HeapRoom();
        if (room < size + BAM_HEAP_FLOOR) {
            BAM_NOTE("memory: donor model kind=%u skipped (%u KB free, model %u KB)\n", kind, room / 1024, size / 1024);
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
unsigned donor_display(Fighter* fp, unsigned slot, int pass, MtxPtr vmtx)
{
    unsigned char item_of[8];
    unsigned mask, i, replaced = 0, source = Bam_AbilitySourceKind(fp);
    DonorModel* m;
    memset(item_of, 0xFF, sizeof(item_of));
    mask = Bam_DonorMeshShow(fp, item_of);
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
    Bam_DonorPose(fp, m->jobj, m->parent, m->joints, -1, mask);
    HSD_JObjDispAll(m->root, vmtx, HSD_GObj_80390EB8(pass), 0);
    return replaced;
}

/* The fighter's own body is not drawn: a borrowed move turned it into the
 * donor's (Kirby's stone), drawn in its place. */
bool Bam_BodyHidden(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    unsigned i, source;
    int slot;
    if (!fp || !Bam_IsAbilityState(fp)) return false;
    source = Bam_AbilitySourceKind(fp);
    slot = slot_of(fp);
    if (slot < 0 || source == fp->kind || !bam_visual->donor_models[slot] || bam_visual->donor_models[slot]->kind != source ||
        !bam_visual->donor_models[slot]->root)
        return false;
    for (i = 0; i < sizeof(vis_donors) / sizeof(vis_donors[0]); ++i)
        if (vis_donors[i].kind == source && vis_donors[i].hide_body &&
            vis_get(slot, source, vis_donors[i].group) >= vis_donors[i].from)
            return true;
    return false;
}


/* A new borrowed move starts with none of the donor's groups switched:
 * each move's script switches the ones it shows (Peach's club stayed in
 * her hand after an interrupted forward smash). */
void Bam_VisReset(const Fighter* fp)
{
    int slot;
    if (!bam_visual || !bam_match) return;
    slot = slot_of(fp);
    if (slot >= 0) bam_visual->donor_vis_kind[slot] = Ft_Kind_Max;
}
