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

#define ABILITY(kind, character, slot, key, label, table, count, attrs) \
    { 1 + kind * 4 + slot, label, character, kind, slot, NULL, NULL, \
      ROGUE_COMPAT_ADAPTED, ROGUE_ABILITY_NEEDS_ATTRS | ROGUE_ABILITY_NEEDS_ANIMATION | \
      ROGUE_ABILITY_NEEDS_BONE_MAP | ROGUE_ABILITY_NEEDS_STATE_TABLE, \
      ftCo_MS_Count, ftCo_MS_Count + count - 1, table, (((1U << 27) - 1) & ~(1U << Ft_Kind_Nana)), sizeof(attrs) }
#define FOUR(kind, character, prefix, table, count, attrs, n, s, u, d) \
    ABILITY(kind, character, ROGUE_ABILITY_NEUTRAL, prefix "_neutral", n, table, count, attrs), \
    ABILITY(kind, character, ROGUE_ABILITY_SIDE, prefix "_side", s, table, count, attrs), \
    ABILITY(kind, character, ROGUE_ABILITY_UP, prefix "_up", u, table, count, attrs), \
    ABILITY(kind, character, ROGUE_ABILITY_DOWN, prefix "_down", d, table, count, attrs)

static RogueAbilityDefinition abilities[] = {
    FOUR(Ft_Kind_Koopa, CKind_Koopa, "koopa", ftKp_Init_MotionStateTable,
         ftKp_MS_SelfCount, ftKoopaAttributes,
         "Fire Breath", "Koopa Klaw", "Whirling Fortress", "Bowser Bomb"),
    FOUR(Ft_Kind_Link, CKind_Link, "link", ftLk_Init_MotionStateTable,
         ftLk_MS_SelfCount, ftLk_DatAttrs,
         "Bow", "Boomerang", "Spin Attack", "Bomb"),
    FOUR(Ft_Kind_CLink, CKind_CLink, "clink", ftCl_Init_MotionStateTable,
         ftLk_MS_SelfCount, ftLk_DatAttrs,
         "Fire Bow", "Young Link Boomerang", "Young Link Spin Attack", "Young Link Bomb"),
    FOUR(Ft_Kind_Samus, CKind_Samus, "samus", ftSs_Init_MotionStateTable,
         ftSs_MS_SelfCount, ftSs_DatAttrs,
         "Charge Shot", "Missile", "Screw Attack", "Morph Ball Bomb"),
    FOUR(Ft_Kind_Mewtwo, CKind_Mewtwo, "mewtwo", ftMt_Init_MotionStateTable,
         ftMt_MS_SelfCount, ftMewtwoAttributes,
         "Shadow Ball", "Confusion", "Teleport", "Disable"),
    FOUR(Ft_Kind_Ness, CKind_Ness, "ness", ftNs_Init_MotionStateTable,
         ftNs_MS_SelfCount, ftNessAttributes,
         "PK Flash", "PK Fire", "PK Thunder", "PSI Magnet"),
    FOUR(Ft_Kind_Peach, CKind_Peach, "peach", ftPe_Init_MotionStateTable,
         ftPe_MS_SelfCount, ftPe_DatAttrs,
         "Toad", "Peach Bomber", "Peach Parasol", "Vegetable"),
    FOUR(Ft_Kind_Yoshi, CKind_Yoshi, "yoshi", ftYs_Init_MotionStateTable,
         ftYs_MS_SelfCount, ftYoshiAttributes,
         "Egg Lay", "Egg Roll", "Egg Throw", "Yoshi Bomb"),
    FOUR(Ft_Kind_Zelda, CKind_Zelda, "zelda", ftZd_Init_MotionStateTable,
         ftZd_MS_SelfCount, ftZelda_DatAttrs,
         "Nayru's Love", "Din's Fire", "Farore's Wind", "Transform to Sheik"),
    FOUR(Ft_Kind_Seak, CKind_Seak, "seak", ftSk_Init_MotionStateTable,
         ftSk_MS_SelfCount, ftSeakAttributes,
         "Needle Storm", "Chain", "Vanish", "Transform to Zelda"),
    FOUR(Ft_Kind_GameWatch, CKind_GameWatch, "gamewatch", ftGw_Init_MotionStateTable,
         ftGw_MS_SelfCount, ftGameWatchAttributes,
         "Chef", "Judgment", "Fire", "Oil Panic"),
    FOUR(Ft_Kind_Kirby, CKind_Kirby, "kirby", ftKb_Init_MotionStateTable,
         ftKb_MS_SelfCount, ftKb_DatAttrs,
         "Inhale", "Hammer", "Final Cutter", "Stone"),
    FOUR(Ft_Kind_Popo, CKind_PopoNana, "popo", ftPp_Init_MotionStateTable,
         ftPp_MS_SelfCount, ftIceClimberAttributes,
         "Ice Shot", "Squall Hammer", "Belay", "Blizzard"),
    FOUR(Ft_Kind_Mario, CKind_Mario, "mario", ftMr_Init_MotionStateTable,
         ftMr_MS_SelfCount, ftMario_DatAttrs,
         "Fireball", "Cape", "Super Jump Punch", "Mario Tornado"),
    FOUR(Ft_Kind_DrMario, CKind_DrMario, "drmario", ftDr_Init_MotionStateTable,
         ftMr_MS_SelfCount, ftMario_DatAttrs,
         "Megavitamins", "Super Sheet", "Dr. Mario Jump Punch", "Dr. Tornado"),
    FOUR(Ft_Kind_Luigi, CKind_Luigi, "luigi", ftLg_Init_MotionStateTable,
         ftLg_MS_SelfCount, ftLuigiAttributes,
         "Luigi Fireball", "Green Missile", "Luigi Jump Punch", "Luigi Cyclone"),
    FOUR(Ft_Kind_Donkey, CKind_Donkey, "donkey", ftDk_Init_MotionStateTable,
         ftDk_MS_SelfCount, ftDonkeyAttributes,
         "Giant Punch", "Headbutt", "Spinning Kong", "Hand Slap"),
    FOUR(Ft_Kind_Ganon, CKind_Ganon, "ganon", ftGn_Init_MotionStateTable,
         ftCa_MS_SelfCount, ftCaptain_DatAttrs,
         "Warlock Punch", "Gerudo Dragon", "Dark Dive", "Wizard's Foot"),
    FOUR(Ft_Kind_Emblem, CKind_Emblem, "emblem", ftFe_Init_MotionStateTable,
         ftMs_MS_SelfCount, MarsAttributes,
         "Flare Blade", "Double-Edge Dance", "Blazer", "Roy Counter"),
    FOUR(Ft_Kind_Pikachu, CKind_Pikachu, "pikachu", ftPk_Init_MotionStateTable,
         ftPk_MS_SelfCount, ftPikachuAttributes,
         "Thunder Jolt", "Skull Bash", "Quick Attack", "Thunder"),
    FOUR(Ft_Kind_Pichu, CKind_Pichu, "pichu", ftPc_Init_MotionStateTable,
         ftPk_MS_SelfCount, ftPikachuAttributes,
         "Pichu Thunder Jolt", "Pichu Skull Bash", "Agility", "Pichu Thunder"),
    FOUR(Ft_Kind_Fox, CKind_Fox, "fox", ftFx_Init_MotionStateTable,
         ftFx_MS_SelfCount, ftFox_DatAttrs,
         "Fox Blaster", "Fox Illusion", "Fire Fox", "Fox Reflector"),
    FOUR(Ft_Kind_Falco, CKind_Falco, "falco", ftFc_Init_MotionStateTable,
         ftFx_MS_SelfCount, ftFox_DatAttrs,
         "Falco Blaster", "Falco Phantasm", "Fire Bird", "Falco Reflector"),
    FOUR(Ft_Kind_Captain, CKind_Captain, "falcon", ftCa_Init_MotionStateTable,
         ftCa_MS_SelfCount, ftCaptain_DatAttrs,
         "Falcon Punch", "Raptor Boost", "Falcon Dive", "Falcon Kick"),
    FOUR(Ft_Kind_Mars, CKind_Mars, "marth", ftMs_Init_MotionStateTable,
         ftMs_MS_SelfCount, MarsAttributes,
         "Shield Breaker", "Dancing Blade", "Dolphin Slash", "Marth Counter"),
    FOUR(Ft_Kind_Purin, CKind_Purin, "puff", ftPr_Init_MotionStateTable,
         ftPr_MS_SelfCount, ftPurinAttributes,
         "Rollout", "Pound", "Sing", "Rest"),
};

const RogueAbilityDefinition* Rogue_GetAbility(RogueAbilityID id)
{
    unsigned i;
    for (i = 0; i < sizeof(abilities) / sizeof(*abilities); ++i) {
        RogueAbilityDefinition* def = &abilities[i];
        if (def->id != id) continue;
        switch (def->native_slot) {
        case ROGUE_ABILITY_NEUTRAL:
            def->ground_enter = ftData_SpecialN[def->internal_kind];
            def->air_enter = ftData_SpecialAirN[def->internal_kind]; break;
        case ROGUE_ABILITY_SIDE:
            def->ground_enter = ftData_SpecialS[def->internal_kind];
            def->air_enter = ftData_SpecialAirS[def->internal_kind]; break;
        case ROGUE_ABILITY_UP:
            def->ground_enter = ftData_SpecialHi[def->internal_kind];
            def->air_enter = ftData_SpecialAirHi[def->internal_kind]; break;
        case ROGUE_ABILITY_DOWN:
            def->ground_enter = ftData_SpecialLw[def->internal_kind];
            def->air_enter = ftData_SpecialAirLw[def->internal_kind]; break;
        default: return NULL;
        }
        /* Native Hand Slap is ground-only. Consume the borrowed air input and
         * transition to native fall instead of rejecting both ground and air. */
        if (def->internal_kind == Ft_Kind_Donkey && def->native_slot == ROGUE_ABILITY_DOWN) {
            def->flags |= ROGUE_ABILITY_GROUND_ONLY;
            def->air_enter = ftCo_Fall_Enter;
        }
        return def;
    }
    return NULL;
}

RogueAbilityID Rogue_AbilityForOpponent(CharacterKind opponent, RogueAbilitySlot slot)
{
    unsigned i;
    for (i = 0; i < sizeof(abilities) / sizeof(*abilities); ++i)
        if (abilities[i].source_kind == opponent && abilities[i].native_slot == slot)
            return abilities[i].id;
    return ROGUE_ABILITY_NATIVE;
}

FighterKind Rogue_InternalKindForCharacter(CharacterKind character)
{
    unsigned i;
    for (i = 0; i < sizeof(abilities) / sizeof(*abilities); ++i) {
        if (abilities[i].source_kind == character)
            return abilities[i].internal_kind;
    }
    return Ft_Kind_Max;
}
