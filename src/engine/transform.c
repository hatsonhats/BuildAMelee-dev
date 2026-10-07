#include <engine/internal.h>
#include <dolphin/os.h>
bool Bam_BorrowedTransform(Fighter_GObj* gobj, HSD_GObjEvent finish)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const BamDonorSpecial* next;
    BamFighterState* const S = Bam_FighterCtx(fp);
    FighterKind old_kind, next_kind;
    int slot;
    if (!Bam_InBorrowedMove(fp) || !S->active) return false;
    old_kind = S->active->internal_kind;
    if (old_kind != Ft_Kind_Zelda && old_kind != Ft_Kind_Seak) return false;
    next_kind = old_kind == Ft_Kind_Zelda ? Ft_Kind_Seak : Ft_Kind_Zelda;
    next = Bam_DonorSpecial(1 + next_kind * 4 + BAM_SPECIAL_DOWN);
    if (!next || !S->loaded_sources[next_kind]) return false;
    /* Transform the borrowed kit while retaining the player's base fighter. */
    for (slot = 0; slot < 4; ++slot) {
        const BamDonorSpecial* equipped = Bam_DonorSpecial(Bam_EquippedSpecial(fp, slot));
        if (equipped && equipped->internal_kind == old_kind)
            Bam_SetEquippedSpecial(fp, slot, 1 + next_kind * 4 + slot);
    }
    S->source_vars[old_kind] = fp->u;
    fp->u = S->source_vars[next_kind];
    S->active = next;
    fp->dat_attrs = bam_match->donor_attrs[next_kind].bytes;
    FT_ANIMS(fp) = FTDATA_ANIMS(gFtDataList[next_kind]);
    FT_ANIM_FLAGS(fp) = FTDATA_ANIM_FLAGS(gFtDataList[next_kind]);
    FT_ANIM_COUNT(fp) = ftData_Table_Unk0[next_kind].count;
    finish(gobj);
#if BAM_DEBUG
    BAM_LOG("borrowed_transform from=%u to=%u borrower=%u match=%u\n",old_kind,next_kind,fp->kind,S->match_generation);
#endif
    return true;
}

