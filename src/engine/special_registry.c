#include <engine/special_engine.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftKoopa/ftkoopa.h>
#include <melee/ft/kinds/ftLink/ftlink.h>
#include <melee/ft/kinds/ftCLink/ftclink.h>
#include <melee/ft/kinds/ftSamus/ftsamus.h>
#include <melee/ft/kinds/ftMewtwo/ftmewtwo.h>
#include <melee/ft/kinds/ftNess/ftness.h>
#include <melee/ft/kinds/ftPeach/ftpeach.h>
#include <melee/ft/kinds/ftYoshi/ftyoshi.h>
#include <melee/ft/kinds/ftZelda/ftzelda.h>
#include <melee/ft/kinds/ftSeak/ftseak.h>
#include <melee/ft/kinds/ftGameWatch/ftgamewatch.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/ft/kinds/ftPopo/ftpopo.h>
#include <melee/ft/kinds/ftFox/ftfox.h>
#include <melee/ft/kinds/ftFalco/ftfalco.h>
#include <melee/ft/kinds/ftCaptain/ftcaptain.h>
#include <melee/ft/kinds/ftMars/ftmars.h>
#include <melee/ft/kinds/ftPurin/ftpurin.h>
#include <melee/ft/kinds/ftMario/ftmario.h>
#include <melee/ft/kinds/ftDrMario/ftdrmario.h>
#include <melee/ft/kinds/ftLuigi/ftluigi.h>
#include <melee/ft/kinds/ftDonkey/ftdonkey.h>
#include <melee/ft/kinds/ftGanon/ftganon.h>
#include <melee/ft/kinds/ftEmblem/ftemblem.h>
#include <melee/ft/kinds/ftPikachu/ftpikachu.h>
#include <melee/ft/kinds/ftPichu/ftpichu.h>

/* Each donor's four specials: the donor's own motion-state table and
 * attributes. The moves' names are in the catalog (special_catalog_data.c),
 * by the same id. */
#define ABILITY(kind, character, slot, table, count, attrs) \
    { 1 + kind * 4 + slot, character, kind, slot, NULL, NULL, \
      ftCo_MS_Count, ftCo_MS_Count + count - 1, table, sizeof(attrs) }
#define FOUR(kind, character, table, count, attrs) \
    ABILITY(kind, character, BAM_ABILITY_NEUTRAL, table, count, attrs), \
    ABILITY(kind, character, BAM_ABILITY_SIDE, table, count, attrs), \
    ABILITY(kind, character, BAM_ABILITY_UP, table, count, attrs), \
    ABILITY(kind, character, BAM_ABILITY_DOWN, table, count, attrs)

static BamAbilityDefinition abilities[] = {
    FOUR(Ft_Kind_Koopa, CKind_Koopa, ftKp_Init_MotionStateTable, ftKp_MS_SelfCount, ftKoopaAttributes),
    FOUR(Ft_Kind_Link, CKind_Link, ftLk_Init_MotionStateTable, ftLk_MS_SelfCount, ftLk_DatAttrs),
    FOUR(Ft_Kind_CLink, CKind_CLink, ftCl_Init_MotionStateTable, ftLk_MS_SelfCount, ftLk_DatAttrs),
    FOUR(Ft_Kind_Samus, CKind_Samus, ftSs_Init_MotionStateTable, ftSs_MS_SelfCount, ftSs_DatAttrs),
    FOUR(Ft_Kind_Mewtwo, CKind_Mewtwo, ftMt_Init_MotionStateTable, ftMt_MS_SelfCount, ftMewtwoAttributes),
    FOUR(Ft_Kind_Ness, CKind_Ness, ftNs_Init_MotionStateTable, ftNs_MS_SelfCount, ftNessAttributes),
    FOUR(Ft_Kind_Peach, CKind_Peach, ftPe_Init_MotionStateTable, ftPe_MS_SelfCount, ftPe_DatAttrs),
    FOUR(Ft_Kind_Yoshi, CKind_Yoshi, ftYs_Init_MotionStateTable, ftYs_MS_SelfCount, ftYoshiAttributes),
    FOUR(Ft_Kind_Zelda, CKind_Zelda, ftZd_Init_MotionStateTable, ftZd_MS_SelfCount, ftZelda_DatAttrs),
    FOUR(Ft_Kind_Seak, CKind_Seak, ftSk_Init_MotionStateTable, ftSk_MS_SelfCount, ftSeakAttributes),
    FOUR(Ft_Kind_GameWatch, CKind_GameWatch, ftGw_Init_MotionStateTable, ftGw_MS_SelfCount, ftGameWatchAttributes),
    FOUR(Ft_Kind_Kirby, CKind_Kirby, ftKb_Init_MotionStateTable, ftKb_MS_SelfCount, ftKb_DatAttrs),
    FOUR(Ft_Kind_Popo, CKind_PopoNana, ftPp_Init_MotionStateTable, ftPp_MS_SelfCount, ftIceClimberAttributes),
    FOUR(Ft_Kind_Mario, CKind_Mario, ftMr_Init_MotionStateTable, ftMr_MS_SelfCount, ftMario_DatAttrs),
    FOUR(Ft_Kind_DrMario, CKind_DrMario, ftDr_Init_MotionStateTable, ftMr_MS_SelfCount, ftMario_DatAttrs),
    FOUR(Ft_Kind_Luigi, CKind_Luigi, ftLg_Init_MotionStateTable, ftLg_MS_SelfCount, ftLuigiAttributes),
    FOUR(Ft_Kind_Donkey, CKind_Donkey, ftDk_Init_MotionStateTable, ftDk_MS_SelfCount, ftDonkeyAttributes),
    FOUR(Ft_Kind_Ganon, CKind_Ganon, ftGn_Init_MotionStateTable, ftCa_MS_SelfCount, ftCaptain_DatAttrs),
    FOUR(Ft_Kind_Emblem, CKind_Emblem, ftFe_Init_MotionStateTable, ftMs_MS_SelfCount, MarsAttributes),
    FOUR(Ft_Kind_Pikachu, CKind_Pikachu, ftPk_Init_MotionStateTable, ftPk_MS_SelfCount, ftPikachuAttributes),
    FOUR(Ft_Kind_Pichu, CKind_Pichu, ftPc_Init_MotionStateTable, ftPk_MS_SelfCount, ftPikachuAttributes),
    FOUR(Ft_Kind_Fox, CKind_Fox, ftFx_Init_MotionStateTable, ftFx_MS_SelfCount, ftFox_DatAttrs),
    FOUR(Ft_Kind_Falco, CKind_Falco, ftFc_Init_MotionStateTable, ftFx_MS_SelfCount, ftFox_DatAttrs),
    FOUR(Ft_Kind_Captain, CKind_Captain, ftCa_Init_MotionStateTable, ftCa_MS_SelfCount, ftCaptain_DatAttrs),
    FOUR(Ft_Kind_Mars, CKind_Mars, ftMs_Init_MotionStateTable, ftMs_MS_SelfCount, MarsAttributes),
    FOUR(Ft_Kind_Purin, CKind_Purin, ftPr_Init_MotionStateTable, ftPr_MS_SelfCount, ftPurinAttributes),
};

const BamAbilityDefinition* Bam_GetAbility(BamAbilityID id)
{
    unsigned i;
    for (i = 0; i < sizeof(abilities) / sizeof(*abilities); ++i) {
        BamAbilityDefinition* def = &abilities[i];
        if (def->id != id) continue;
        switch (def->native_slot) {
        case BAM_ABILITY_NEUTRAL:
            def->ground_enter = ftData_SpecialN[def->internal_kind];
            def->air_enter = ftData_SpecialAirN[def->internal_kind]; break;
        case BAM_ABILITY_SIDE:
            def->ground_enter = ftData_SpecialS[def->internal_kind];
            def->air_enter = ftData_SpecialAirS[def->internal_kind]; break;
        case BAM_ABILITY_UP:
            def->ground_enter = ftData_SpecialHi[def->internal_kind];
            def->air_enter = ftData_SpecialAirHi[def->internal_kind]; break;
        case BAM_ABILITY_DOWN:
            def->ground_enter = ftData_SpecialLw[def->internal_kind];
            def->air_enter = ftData_SpecialAirLw[def->internal_kind]; break;
        default: return NULL;
        }
        /* Hand Slap is ground-only: in the air the borrowed input is
         * consumed and the fighter falls instead. */
        if (def->internal_kind == Ft_Kind_Donkey && def->native_slot == BAM_ABILITY_DOWN)
            def->air_enter = ftCo_Fall_Enter;
        return def;
    }
    return NULL;
}

FighterKind Bam_InternalKindForCharacter(CharacterKind character)
{
    unsigned i;
    for (i = 0; i < sizeof(abilities) / sizeof(*abilities); ++i) {
        if (abilities[i].source_kind == character)
            return abilities[i].internal_kind;
    }
    return Ft_Kind_Max;
}
