#include <engine/visual/internal.h>

/* Peach's up special ends with her parasol open: holding it, she floats
 * down (the common parasol fall). A borrower holds no parasol item, so the
 * float is kept here: set while the borrowed up special shows the parasol,
 * it lasts through the special fall that follows until the fighter lands,
 * leaves that state or closes it (stick down). The parasol stays drawn in
 * the hand meanwhile. */

/* The float keeps the parasol where the borrowed up special left it on the
 * body (the root), not in the hand: the special fall's own pose has the
 * hand down, and the parasol hung sideways with its hitbox in the body. */
int parasol_base(Fighter* fp)
{
    return fp->parts[0].joint ? 0 : -1;
}
void ftAction_8007121C(Fighter_GObj* gobj, CommandInfo* cmd);

/* Peach's open parasol hits (a weak hit made by her ItemParasolOpen script
 * that lasts through the fall). The borrower's float gets the same hitbox,
 * on the canopy of the parasol drawn in its hand. */
static union CmdUnion* parasol_hit_cmd(void)
{
    /* Words per script command: 0x00-0x09 (common), 0x0A on (fighter: the
     * game's own table, ftaction.c). */
    static const u8 common[10] = { 1, 1, 1, 1, 1, 2, 1, 2, 1, 1 };
    extern u8 ftAction_803C0870[0x31];
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
        if (op == 0 || (op >= 10 && op - 10 >= sizeof(ftAction_803C0870))) return NULL;
        p += op < 10 ? common[op] : ftAction_803C0870[op - 10];
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
    PSMTXMultVec(rel, &off, &bam_visual->parasol_hit_off[slot]);
}

/* The float ends. A state change has already removed the hitbox; closing
 * the parasol (still in the special fall) removes it here. */
static void parasol_hit_off_clear(Fighter* fp, int slot, int disable)
{
    if (bam_visual->parasol_hit[slot] && disable) {
        HitCapsule* h = &fp->x914[bam_visual->parasol_hit[slot] - 1];
        int hand = parasol_base(fp);
        if (hand >= 0 && h->jobj == fp->parts[hand].joint) h->state = HitCapsule_Disabled;
    }
    bam_visual->parasol_hit[slot] = 0;
}

/* Every frame (BAM_OnFrame), part of the simulation: whether the next
 * special fall is a parasol float, and where its hitbox goes. */
void Bam_ParasolTrack(Fighter* fp)
{
    int slot = slot_of(fp), hand;
    Mtx w;
    if (slot < 0) return;
    if (Bam_InBorrowedMove(fp)) {
        if (Bam_PropWeaponMtx(fp, w) != 2 || (hand = parasol_base(fp)) < 0) return;
        bam_visual->parasol_float[slot] = 1;
        parasol_hit_off_set(fp, slot, hand, w);
        return;
    }
    if (bam_visual->parasol_float[slot] && (fp->motion_id != ftCo_MS_FallSpecial || fp->ground_or_air != GA_Air)) {
        parasol_hit_off_clear(fp, slot, 0);
        bam_visual->parasol_float[slot] = 0;
    }
}

/* ftCo_800CEFE0, where Peach's up special opens her parasol: a borrower
 * goes into the special fall instead, floating on the parasol kept here (the
 * common held-parasol states it went to need the parasol item, which goes
 * away with the borrowed move: the parasol vanished and could not be
 * closed). True when it did. */
bool Bam_ParasolOpen(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int slot = slot_of(fp);
    ftPe_DatAttrs* da;
    if (slot < 0 || fp->kind == Ft_Kind_Peach || !Bam_InBorrowedMove(fp) ||
        Bam_DonorKind(fp) != Ft_Kind_Peach)
        return false;
    da = fp->dat_attrs;
    bam_visual->parasol_float[slot] = 1;
    ftCo_80096900(gobj, 0, 1, false, da->x70, da->x74);
    return true;
}

/* ftCo_FallSpecial_Phys: true while the borrower floats on Peach's parasol. */
bool Bam_ParasolFloat(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int slot = slot_of(fp), hand;
    if (slot < 0 || !bam_visual->parasol_float[slot]) return false;
    if (fp->motion_id != ftCo_MS_FallSpecial || fp->ground_or_air != GA_Air ||
        fp->input.lstick[0].y <= p_ftCommonData->close_parasol_threshold) {
        parasol_hit_off_clear(fp, slot, fp->motion_id == ftCo_MS_FallSpecial);
        bam_visual->parasol_float[slot] = 0;
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
    if (!bam_visual->parasol_hit[slot] && (hand = parasol_base(fp)) >= 0) {
        union CmdUnion* cmd = parasol_hit_cmd();
        if (cmd) {
            CommandInfo ci;
            HitCapsule* h;
            memset(&ci, 0, sizeof(ci));
            ci.u = cmd;
            ftAction_8007121C(gobj, &ci);
            h = &fp->x914[cmd->create_hitbox_0.id];
            h->jobj = fp->parts[hand].joint;
            h->b_offset = bam_visual->parasol_hit_off[slot];
            bam_visual->parasol_hit[slot] = (unsigned char) (cmd->create_hitbox_0.id + 1);
            BAM_LOG("parasol hit id=%d off=(%.2f,%.2f,%.2f)\n", (int) cmd->create_hitbox_0.id,
                    h->b_offset.x, h->b_offset.y, h->b_offset.z);
        }
    }
    return true;
}
void parasol_display(Fighter* fp, int slot, int pass, MtxPtr vmtx)
{
    HSD_JObj* model;
    Mtx place;
    int hand = parasol_base(fp);
    if (fp->motion_id != ftCo_MS_FallSpecial || fp->ground_or_air != GA_Air || hand < 0) return;
    model = weapon_model((unsigned) slot, 2);
    if (!model) return;
    PSMTXConcat(HSD_JObjGetMtxPtr(fp->parts[hand].joint), bam_visual->parasol_rel[slot], place);
    HSD_JObjCopyMtx(model, place);
    model->flags |= JOBJ_USER_DEF_MTX | JOBJ_MTX_INDEP_PARENT | JOBJ_MTX_INDEP_SRT;
    HSD_JObjSetMtxDirty(model);
    HSD_JObjDispAll(model, vmtx, HSD_GObj_80390EB8(pass), 0);
}
