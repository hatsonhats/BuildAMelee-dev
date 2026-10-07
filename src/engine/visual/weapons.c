/* Borrowed weapon moves show the weapon.
 *
 * Marth's, Roy's, Link's and Young Link's moves swing a sword, Kirby's Hammer
 * and Final Cutter swing a hammer and a sword, and the Ice Climbers swing
 * hammers; all of them are part of the donor's own body model, so a borrower
 * swung an empty hand. While such a move plays, the matching common item
 * model (Beam Sword or Hammer) is drawn along the donor's weapon bone,
 * rebuilt on the borrower's hand (anim/props.c), where the move's hitboxes
 * are. It is decoration only: not an item, cannot be picked up or dropped.
 * The models come from the common item file that every match already loads.
 * Fighters whose own model already shows that weapon are left alone.
 *
 * Better still, the donor's own model is drawn when it has the part: the
 * Falchion, Master Sword and Sword of Seals, the Ice Climbers' mallet, and
 * tails (Mewtwo's back air gives you Mewtwo's tail). Borrowing a move already
 * loads the donor's default-costume model; a second copy of it is made per
 * borrower, every mesh hidden except the ones riding on rebuilt bones, and
 * posed on the borrower each frame (anim/pose.c). The item models stay as
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
 * kept per borrower here (Bam_VisSet) and the donor's meshes drawn. While
 * Kirby's stone shows, the borrower's own body is hidden (Bam_BodyHidden).
 *
 * Called from: 10-core.toml (sword display), borrow.c, donor_model.c; match begin/end from fighter.c.
 * State: allocates and frees bam_visual (match block, visual/internal.h).
 */
#include <engine/visual/internal.h>

/* 0 Beam Sword, 1 Hammer (common items), 2 Peach's parasol (her article). */



bool sword_kind(unsigned kind);
bool hammer_kind(unsigned kind);


bool sword_kind(unsigned kind)
{
    return kind == Ft_Kind_Mars || kind == Ft_Kind_Emblem || kind == Ft_Kind_Link || kind == Ft_Kind_CLink;
}
bool hammer_kind(unsigned kind)
{
    return kind == Ft_Kind_Popo || kind == Ft_Kind_Nana;
}


static void release_slot(unsigned slot)
{
    unsigned k;
    for (k = 0; k < WEAPON_ITEMS; ++k) {
        if (bam_visual->weapons[slot][k]) HSD_JObjRemoveAll(bam_visual->weapons[slot][k]);
        bam_visual->weapons[slot][k] = NULL;
    }
    donor_free(slot);
    bam_visual->donor_vis_kind[slot] = Ft_Kind_Max;
    bam_visual->parasol_float[slot] = 0;
    bam_visual->weapon_owner[slot] = NULL;
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
HSD_JObj* weapon_model(unsigned slot, int item)
{
    static const ItemKind kinds[WEAPON_ITEMS] = { It_Kind_Sword, It_Kind_Hammer, It_Kind_Peach_Parasol };
    if (!bam_visual->weapons[slot][item]) {
        ItemKind kind = kinds[item];
        Article* article;
        Mtx rest;
        int id;
        if (kind >= It_Kind_Kuriboh) article = it_804D6D38 ? it_804D6D38[kind - It_Kind_Kuriboh] : NULL;
        else article = it_804D6D24 ? it_804D6D24[kind] : NULL;
        if (!article || !article->x10_modelDesc || !article->x10_modelDesc->x0_joint) return NULL;
        bam_visual->weapons[slot][item] = HSD_JObjLoadJoint(article->x10_modelDesc->x0_joint);
        PSMTXIdentity(bam_visual->weapon_attach[slot][item]);
        id = kind >= It_Kind_Kuriboh ? article->x10_modelDesc->x8_bone_attach_id : 0;
        if (bam_visual->weapons[slot][item] && id > 0 && attach_rest(bam_visual->weapons[slot][item], &id, NULL, rest))
            PSMTXInverse(rest, bam_visual->weapon_attach[slot][item]);
    }
    return bam_visual->weapons[slot][item];
}


void Bam_SwordDisplay(HSD_GObj* gobj, int pass, MtxPtr vmtx)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int slot, hand, item;
    unsigned source, replaced;
    HSD_JObj* model;
    Mtx place;
    slot = slot_of(fp);
    if (slot < 0) return;
    if (!Bam_InBorrowedMove(fp)) {
        bam_visual->donor_vis_kind[slot] = Ft_Kind_Max;
        if (bam_visual->parasol_float[slot]) parasol_display(fp, slot, pass, vmtx);
        return;
    }
    if (bam_visual->weapon_owner[slot] != fp) {
        release_slot((unsigned) slot);
        bam_visual->weapon_owner[slot] = fp;
    }
    source = Bam_DonorKind(fp);
    replaced = donor_display(fp, (unsigned) slot, pass, vmtx);
    item = Bam_PropWeaponMtx(fp, place);
    if (item == 2 && (hand = parasol_base(fp)) >= 0 && weapon_model((unsigned) slot, 2)) {
        /* Where the parasol sits on the hand, for the float that follows,
         * even while the donor's own model draws it: unset, the float drew
         * it at no size (invisible). */
        Mtx inv, at;
        PSMTXConcat(place, bam_visual->weapon_attach[slot][2], at);
        if (PSMTXInverse(HSD_JObjGetMtxPtr(fp->parts[hand].joint), inv)) PSMTXConcat(inv, at, bam_visual->parasol_rel[slot]);
    }
    if (item >= 0 && (replaced & (1U << item))) return;
    if (item < 0 && (replaced & 1U)) return;
    if (item < 0) {
        /* No weapon bone for this move: a sword donor's sword still goes in
         * the item hand. */
        if (!sword_kind(source) || !fp->ft_data || !FTDATA_PARTS(fp->ft_data)) return;
        hand = FTDATA_PARTS(fp->ft_data)->x10;
        if (hand < 0 || (unsigned) hand >= ftPartsTable[fp->kind]->parts_num || !fp->parts[hand].joint) return;
        item = 0;
        PSMTXCopy(HSD_JObjGetMtxPtr(fp->parts[hand].joint), place);
    }
    if (item >= WEAPON_ITEMS || (item == 0 && sword_kind(fp->kind)) || (item == 1 && hammer_kind(fp->kind))) return;
    model = weapon_model((unsigned) slot, item);
    if (!model) return;
    PSMTXConcat(place, bam_visual->weapon_attach[slot][item], place);
    HSD_JObjCopyMtx(model, place);
    model->flags |= JOBJ_USER_DEF_MTX | JOBJ_MTX_INDEP_PARENT | JOBJ_MTX_INDEP_SRT;
    HSD_JObjSetMtxDirty(model);
    HSD_JObjDispAll(model, vmtx, HSD_GObj_80390EB8(pass), 0);
}

/* The fighter is going away (match end, destroyed): free its weapons. */
void Bam_SwordRelease(const Fighter* fp)
{
    unsigned i;
    for (i = 0; i < BAM_FIGHTERS; ++i)
        if (bam_visual->weapon_owner[i] == fp || !fp) release_slot(i);
}

#include <sysdolphin/baselib/memory.h>
VisualState* bam_visual;
/* Called from Bam_MatchBegin / Bam_MatchEnd (fighter.c). */
void Bam_SwordVisualMatchBegin(void)
{
    bam_visual = HSD_MemAlloc(sizeof(*bam_visual));
    memset(bam_visual, 0, sizeof(*bam_visual));
}
void Bam_SwordVisualMatchEnd(void)
{
    bam_visual = NULL;
}
