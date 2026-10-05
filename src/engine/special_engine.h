#ifndef ROGUE_MELEE_SPECIAL_ENGINE_H
#define ROGUE_MELEE_SPECIAL_ENGINE_H
#include <melee/ft/forward.h>
#include <melee/gm/forward.h>
#include <dolphin/mtx.h>
#include <engine/bam_fighter.h>
/* Training options (platform/training.c): bubble filters for the native
 * develop-mode display, 1 outside training. */
int Bam_DrawHitboxes(void);
int Bam_DrawHurtboxes(void);

typedef enum RogueAbilitySlot {
    ROGUE_ABILITY_NEUTRAL, ROGUE_ABILITY_SIDE,
    ROGUE_ABILITY_UP, ROGUE_ABILITY_DOWN, ROGUE_ABILITY_SLOTS
} RogueAbilitySlot;

typedef enum RogueAbilityID {
    ROGUE_ABILITY_NATIVE,
    ROGUE_ABILITY_FOX_REFLECTOR = 1 + Ft_Kind_Fox * 4 + ROGUE_ABILITY_DOWN,
    ROGUE_ABILITY_COUNT = 1 + Ft_Kind_Max * 4
} RogueAbilityID;
typedef enum RogueAbilityCompatibility {
    ROGUE_COMPAT_NATIVE, ROGUE_COMPAT_SIMPLE,
    ROGUE_COMPAT_ADAPTED, ROGUE_COMPAT_UNSUPPORTED
} RogueAbilityCompatibility;
enum RogueAbilityFlags {
    ROGUE_ABILITY_NEEDS_ARTICLE = 1 << 0,
    ROGUE_ABILITY_NEEDS_ATTRS = 1 << 1,
    ROGUE_ABILITY_NEEDS_ANIMATION = 1 << 2,
    ROGUE_ABILITY_NEEDS_BONE_MAP = 1 << 3,
    ROGUE_ABILITY_NEEDS_STATE_TABLE = 1 << 4,
    ROGUE_ABILITY_GROUND_ONLY = 1 << 5
};
typedef struct RogueAbilityDefinition {
    RogueAbilityID id;
    const char* name;
    CharacterKind source_kind;
    FighterKind internal_kind;
    RogueAbilitySlot native_slot;
    HSD_GObjEvent ground_enter, air_enter;
    RogueAbilityCompatibility compatibility;
    unsigned flags;
    int first_state, last_state;
    MotionState* states;
    /* Runtime lifecycle coverage by internal FighterKind bit; this does not
     * certify every collision, visual effect or opponent matchup. */
    u32 tested_recipients;
    unsigned attrs_size;
} RogueAbilityDefinition;
const RogueAbilityDefinition* Rogue_GetAbility(RogueAbilityID id);
void Rogue_AbilityFighterCreated(Fighter* fp);
void Rogue_AbilityFighterDestroyed(Fighter* fp);
void Rogue_AbilityMatchEnd(void);
void Rogue_AbilityTransformed(Fighter* src, Fighter* dst);
Fighter_GObj* Rogue_AbilityClimberPartner(Fighter* fp);
void Rogue_AbilityCleanup(Fighter* fp);
MotionState* Rogue_AbilityMotionState(Fighter* fp, int motion);
bool Rogue_TrySpecial(Fighter_GObj* gobj, RogueAbilitySlot slot, bool airborne);
/* Whether Rogue_TrySpecial would succeed for this slot. Lets a fighter with
 * no native move in a slot (Nana's side and up special) use a borrowed one. */
bool Rogue_CanTrySpecial(Fighter* fp, RogueAbilitySlot slot);
bool Rogue_AbilityResumeFamily(Fighter* fp, FighterKind family, RogueAbilitySlot slot);
/* A lingering article (Samus's bomb) re-enters its owner's move after the move
 * ended. True when fp may run `family`'s code for `slot` now: natively, or with
 * the borrowed move re-installed. False leaves the fighter untouched. */
bool Rogue_AbilityOwnerResume(Fighter* fp, FighterKind family, RogueAbilitySlot slot);
int Rogue_AbilityPartIndex(Fighter* fp, int part);
int Rogue_AbilityMapBone(Fighter* fp, int bone);
/* Recipient joint for a body part it lacks: the same-side hand for hand and
 * weapon parts, head for head parts, otherwise the hand extended furthest
 * from the chest (swords, cannons, props), then chest, hip, root. */
int Rogue_AbilityFallbackJoint(Fighter* fp, int part);
/* Recipient joint a rebuilt donor-only bone hangs from, or -1 (anim_scale.c). */
int Rogue_PropJoint(Fighter* fp, int bone);
/* Joint an absorb/reflect/shield bubble with donor bone id `bone` rides on. */
struct HSD_JObj* Rogue_DonorBoneJObj(Fighter* fp, int bone);
/* World matrix of a borrowed weapon along the donor's weapon bone; returns
 * the item (0 Beam Sword, 1 Hammer) or -1 if the current move has none. */
int Rogue_PropWeaponMtx(Fighter* fp, Mtx out);
/* The donor's own meshes for rebuilt bones (anim_scale.c, anim_rest.inc):
 * groups to show for the current move (item_of_group: weapon item a group
 * replaces, 255 none), the donor's mesh rows, and posing its model. */
unsigned Rogue_DonorMeshShow(Fighter* fp, unsigned char item_of_group[8]);
void Rogue_DonorModelPreload(unsigned kind);
/* PlXx.dat for a borrowed move, trimmed of what borrowed moves never use. */
int Rogue_LoadDonorData(int kind);
/* What the match heap keeps free after borrowed-move data is loaded into it:
 * the scene allocates ~300 KB more once the fighters exist, and matches that
 * started with ~700 KB free ran out mid-match (effects, items). */
#define BAM_HEAP_FLOOR 0x160000
/* Whether a donor's articles are needed by any equipped move of any player
 * (normal_runtime.c); without, its data is loaded without them. */
int Rogue_DonorNeedsArticles(int kind);
/* Borrowed animation slices: ARAM when it has room, else main RAM. */
void* Rogue_SliceAlloc(unsigned bytes);
void Rogue_SliceRead(int file, unsigned offset, void* dst, unsigned bytes);
void Rogue_SliceFree(void* p);
/* Effects file for a borrowed move (donor_trim.c). */
int Rogue_LoadDonorEffects(int kind);
/* Preload-cache blocks for borrowed-move data (bam_cache.c).
 * ram: 1 main RAM, 0 ARAM. */
void* BamCache_Alloc(int ram, u32 size);
void* BamCache_TempAlloc(u32 size);
void BamCache_TempReset(void);
u32 BamCache_Room(int ram);
int BamCache_Owns(const void* p);
int BamCache_FreeRecords(void);
void BamCache_SceneEnter(void);
void BamCache_SceneExit(void);
/* Donor models (drawn only) are loaded after the scene created its fighters. */
void Rogue_DonorModelsLoad(void);
/* pobjs (may be NULL): the PObjs of each DObj to draw, bit each, 0 all. */
unsigned Rogue_DonorMeshes(unsigned kind, unsigned char* groups, unsigned short* dobjs, unsigned short* pobjs,
                           unsigned max);
struct HSD_JObj;
/* free_joint: a donor joint posed from its own rest pose, not the matching
 * fighter joint (-1 none); shown: the mesh groups drawn (their collapsed
 * joints, rogue_mesh_hide, are drawn at no size). */
void Rogue_DonorPose(Fighter* fp, struct HSD_JObj* const* jobjs, const unsigned char* parents, unsigned n,
                     int free_joint, unsigned shown);
/* Size of a borrowed move's own parts (weapons, props, held articles,
 * hitboxes) on this fighter: its body over the donor's, 1 when not
 * borrowing (anim_scale.c). */
float Rogue_BorrowScale(Fighter* fp);
/* The same in world units, for hitbox radii. */
float Rogue_HitboxScale(Fighter* fp);
/* A move of FighterKind source on FighterKind own: its size on screen against
 * the source's own (0 when they are the same). */
float Rogue_ShownScale(unsigned own, unsigned source);
/* The scale an article sizes itself by from its owner: the owner's model
 * scale, times Rogue_BorrowScale while the owner borrows. */
float Rogue_OwnerScale(HSD_GObj* owner);
/* The joint an article a borrowed move attaches to part `part` rides on. */
struct HSD_JObj* Rogue_ItemAnchor(HSD_GObj* gobj, int part);
bool Rogue_IsAbilityState(const Fighter* fp);
FighterKind Rogue_AbilitySourceKind(const Fighter* fp);
ftData* Rogue_AbilityData(Fighter* fp);
union Fighter_FighterVars;
union Fighter_FighterVars* Rogue_AbilityVars(Fighter* fp, FighterKind family);
bool Rogue_BorrowedTransform(Fighter_GObj* gobj, HSD_GObjEvent finish);
FighterKind Rogue_InternalKindForCharacter(CharacterKind character);

void Rogue_AerialPrepare(Fighter* fp);
/* Ground attacks and throws (normal_runtime.c). Called where the game picks
 * the move for a slot (enum BamNormalSlot): installs the equipped donor's
 * move, or ends any borrowed move when the slot is the fighter's own.
 * Returns whose move it is (the donor, else the fighter's own kind). */
FighterKind Rogue_NormalBegin(Fighter_GObj* gobj, int slot);
/* Common attributes of the move in progress: the donor's during a borrowed
 * ground attack (jab windows, throw weights), else the fighter's own. */
struct ftCo_DatAttrs;
struct ftCo_DatAttrs* Rogue_NormalCoAttrs(Fighter* fp);
bool Rogue_AerialTryEnter(Fighter_GObj* gobj, int motion);
float Rogue_AerialLandingLag(Fighter* fp, int motion, float native_lag);
MotionState* Rogue_AerialMotionState(Fighter* fp, int motion);
/* Borrowed-move body scaling (anim_scale.c): called when an animation is
 * applied; animated translations of registered body bones are mapped from
 * the donor's body to the recipient's. */
void Rogue_AnimRetarget(Fighter* fp, unsigned source_kind, int first_part);
float Rogue_AnimTranslate(struct HSD_JObj* jobj, int axis, float value);
void Rogue_AnimScaleReset(void);
/* Animated joint rotation; retargets a borrowed bone by world orientation. */
void Rogue_AnimRotate(HSD_JObj* jobj, int axis, float value);
/* Per-frame upkeep for borrowed moves (Fire Breath refill). */
void Rogue_AbilityFighterFrame(Fighter* fp);
/* Beam Sword model in the hand during borrowed sword moves (sword_visual.c). */
void Rogue_SwordDisplay(struct HSD_GObj* gobj, int pass, MtxPtr vmtx);
void Rogue_SwordRelease(const Fighter* fp);
/* Donor model-part group switches kept for the donor's model (true: kept),
 * and whether the borrower's own body is hidden for one (Kirby's stone). */
bool Rogue_VisSet(struct HSD_GObj* gobj, int group, int val);
bool Rogue_BodyHidden(struct HSD_GObj* gobj);
/* Entry points called from pinned edits in retail units (overrides/). */
struct FigaTrack;
struct HitCapsule;
void Rogue_LinkAerialDownEnter(Fighter_GObj* gobj);
void Rogue_PropTrack(Fighter* fp, int joint, struct FigaTrack* track, int count);
void Rogue_HitboxCreated(Fighter* fp, struct HitCapsule* hit, int bone);
void Rogue_HitboxRefresh(Fighter* fp, struct HitCapsule* hit);
void Rogue_AnimPostStep(Fighter* fp);
bool Rogue_RestSleep(Fighter_GObj* gobj);
bool Rogue_RestSleeping(Fighter* fp);
void Rogue_RestSleepClear(Fighter* fp);
bool Rogue_ParasolFloat(HSD_GObj* gobj);
/* Every frame: a borrowed Peach up special's parasol float (sword_visual.c). */
void Rogue_ParasolTrack(Fighter* fp);
#endif
