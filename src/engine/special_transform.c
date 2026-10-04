#include <engine/special_internal.h>
#include <dolphin/os.h>
bool Rogue_BorrowedTransform(Fighter_GObj* gobj, HSD_GObjEvent finish)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const RogueAbilityDefinition* next;
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    FighterKind old_kind, next_kind;
    int slot;
    if (!Rogue_IsAbilityState(fp) || !S->active) return false;
    old_kind = S->active->internal_kind;
    if (old_kind != Ft_Kind_Zelda && old_kind != Ft_Kind_Seak) return false;
    next_kind = old_kind == Ft_Kind_Zelda ? Ft_Kind_Seak : Ft_Kind_Zelda;
    next = Rogue_GetAbility(1 + next_kind * 4 + ROGUE_ABILITY_DOWN);
    if (!next || !S->loaded_sources[next_kind]) return false;
    /* Transform the borrowed kit while retaining the player's base fighter. */
    for (slot = 0; slot < 4; ++slot) {
        const RogueAbilityDefinition* equipped = Rogue_GetAbility(Rogue_EquippedSpecial(fp, slot));
        if (equipped && equipped->internal_kind == old_kind)
            Rogue_SetEquippedSpecial(fp, slot, 1 + next_kind * 4 + slot);
    }
    S->source_vars[old_kind] = fp->u;
    fp->u = S->source_vars[next_kind];
    S->active = next;
    fp->dat_attrs = bam_match->donor_attrs[next_kind].bytes;
    fp->x24 = gFtDataList[next_kind]->xC;
    fp->x28 = gFtDataList[next_kind]->x10;
    fp->x58C = ftData_Table_Unk0[next_kind].count;
    finish(gobj);
#if BAM_DEBUG
    BAM_LOG("borrowed_transform from=%u to=%u recipient=%u match=%u\n",old_kind,next_kind,fp->kind,S->match_generation);
#endif
    return true;
}

