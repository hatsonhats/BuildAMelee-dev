#ifndef BAM_MELEE_SPECIAL_ENGINE_H
#define BAM_MELEE_SPECIAL_ENGINE_H
#include <melee/ft/forward.h>
#include <melee/gm/forward.h>
#include <dolphin/mtx.h>
#include <engine/bam_fighter.h>
/* Training options (platform/training.c): bubble filters for the native
 * develop-mode display, 1 outside training. */
int Bam_DrawHitboxes(void);
int Bam_DrawHurtboxes(void);

typedef enum BamAbilitySlot {
    BAM_ABILITY_NEUTRAL, BAM_ABILITY_SIDE,
    BAM_ABILITY_UP, BAM_ABILITY_DOWN, BAM_ABILITY_SLOTS
} BamAbilitySlot;

typedef enum BamAbilityID {
    BAM_ABILITY_NATIVE,
    BAM_ABILITY_FOX_REFLECTOR = 1 + Ft_Kind_Fox * 4 + BAM_ABILITY_DOWN,
    BAM_ABILITY_COUNT = 1 + Ft_Kind_Max * 4
} BamAbilityID;
typedef enum BamAbilityCompatibility {
    BAM_COMPAT_NATIVE, BAM_COMPAT_SIMPLE,
    BAM_COMPAT_ADAPTED, BAM_COMPAT_UNSUPPORTED
} BamAbilityCompatibility;
enum BamAbilityFlags {
    BAM_ABILITY_NEEDS_ARTICLE = 1 << 0,
    BAM_ABILITY_NEEDS_ATTRS = 1 << 1,
    BAM_ABILITY_NEEDS_ANIMATION = 1 << 2,
    BAM_ABILITY_NEEDS_BONE_MAP = 1 << 3,
    BAM_ABILITY_NEEDS_STATE_TABLE = 1 << 4,
    BAM_ABILITY_GROUND_ONLY = 1 << 5
};
typedef struct BamAbilityDefinition {
    BamAbilityID id;
    const char* name;
    CharacterKind source_kind;
    FighterKind internal_kind;
    BamAbilitySlot native_slot;
    HSD_GObjEvent ground_enter, air_enter;
    BamAbilityCompatibility compatibility;
    unsigned flags;
    int first_state, last_state;
    MotionState* states;
    /* Runtime lifecycle coverage by internal FighterKind bit; this does not
     * certify every collision, visual effect or opponent matchup. */
    u32 tested_recipients;
    unsigned attrs_size;
} BamAbilityDefinition;
const BamAbilityDefinition* Bam_GetAbility(BamAbilityID id);
void Bam_AbilityFighterCreated(Fighter* fp);
void Bam_AbilityFighterDestroyed(Fighter* fp);
void Bam_AbilityMatchEnd(void);
void Bam_AbilityTransformed(Fighter* src, Fighter* dst);
Fighter_GObj* Bam_AbilityClimberPartner(Fighter* fp);
void Bam_AbilityCleanup(Fighter* fp);
MotionState* Bam_AbilityMotionState(Fighter* fp, int motion);
bool Bam_TrySpecial(Fighter_GObj* gobj, BamAbilitySlot slot, bool airborne);
/* Whether Bam_TrySpecial would succeed for this slot. Lets a fighter with
 * no native move in a slot (Nana's side and up special) use a borrowed one. */
bool Bam_CanTrySpecial(Fighter* fp, BamAbilitySlot slot);
bool Bam_AbilityResumeFamily(Fighter* fp, FighterKind family, BamAbilitySlot slot);
/* A lingering article (Samus's bomb) re-enters its owner's move after the move
 * ended. True when fp may run `family`'s code for `slot` now: natively, or with
 * the borrowed move re-installed. False leaves the fighter untouched. */
bool Bam_AbilityOwnerResume(Fighter* fp, FighterKind family, BamAbilitySlot slot);
int Bam_AbilityPartIndex(Fighter* fp, int part);
int Bam_AbilityMapBone(Fighter* fp, int bone);
/* Recipient joint for a body part it lacks: the same-side hand for hand and
 * weapon parts, head for head parts, otherwise the hand extended furthest
 * from the chest (swords, cannons, props), then chest, hip, root. */
int Bam_AbilityFallbackJoint(Fighter* fp, int part);
/* Recipient joint a rebuilt donor-only bone hangs from, or -1 (anim/props.c). */
int Bam_PropJoint(Fighter* fp, int bone);
/* Joint an absorb/reflect/shield bubble with donor bone id `bone` rides on. */
struct HSD_JObj* Bam_DonorBoneJObj(Fighter* fp, int bone);
/* World matrix of a borrowed weapon along the donor's weapon bone; returns
 * the item (0 Beam Sword, 1 Hammer) or -1 if the current move has none. */
int Bam_PropWeaponMtx(Fighter* fp, Mtx out);
/* The donor's own meshes for rebuilt bones (anim/pose.c, bone_tables.h):
 * groups to show for the current move (item_of_group: weapon item a group
 * replaces, 255 none), the donor's mesh rows, and posing its model. */
unsigned Bam_DonorMeshShow(Fighter* fp, unsigned char item_of_group[8]);
void Bam_DonorModelPreload(unsigned kind);
/* PlXx.dat for a borrowed move, trimmed of what borrowed moves never use. */
int Bam_LoadDonorData(int kind);
/* What the match heap keeps free after borrowed-move data is loaded into it:
 * the scene allocates ~300 KB more once the fighters exist, and matches that
 * started with ~700 KB free ran out mid-match (effects, items). */
#define BAM_HEAP_FLOOR 0x160000
/* Whether a donor's articles are needed by any equipped move of any player
 * (normal_runtime.c); without, its data is loaded without them. */
int Bam_DonorNeedsArticles(int kind);
/* Borrowed animation slices: ARAM when it has room, else main RAM. */
void* Bam_SliceAlloc(unsigned bytes);
void Bam_SliceRead(int file, unsigned offset, void* dst, unsigned bytes);
void Bam_SliceFree(void* p);
/* Effects file for a borrowed move (donor_trim.c). */
int Bam_LoadDonorEffects(int kind);
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
void Bam_DonorModelsLoad(void);
/* pobjs (may be NULL): the PObjs of each DObj to draw, bit each, 0 all. */
unsigned Bam_DonorMeshes(unsigned kind, unsigned char* groups, unsigned short* dobjs, unsigned short* pobjs,
                           unsigned max);
struct HSD_JObj;
/* free_joint: a donor joint posed from its own rest pose, not the matching
 * fighter joint (-1 none); shown: the mesh groups drawn (their collapsed
 * joints, bam_mesh_hide, are drawn at no size). */
void Bam_DonorPose(Fighter* fp, struct HSD_JObj* const* jobjs, const unsigned char* parents, unsigned n,
                     int free_joint, unsigned shown);
/* Size of a borrowed move's own parts (weapons, props, held articles,
 * hitboxes) on this fighter: its body over the donor's, 1 when not
 * borrowing (anim/scale.c). */
float Bam_BorrowScale(Fighter* fp);
/* The same in world units, for hitbox radii. */
float Bam_HitboxScale(Fighter* fp);
/* A move of FighterKind source on FighterKind own: its size on screen against
 * the source's own (0 when they are the same). */
float Bam_ShownScale(unsigned own, unsigned source);
/* The scale an article sizes itself by from its owner: the owner's model
 * scale, times Bam_BorrowScale while the owner borrows. */
float Bam_OwnerScale(HSD_GObj* owner);
/* The joint an article a borrowed move attaches to part `part` rides on. */
struct HSD_JObj* Bam_ItemAnchor(HSD_GObj* gobj, int part);
bool Bam_IsAbilityState(const Fighter* fp);
FighterKind Bam_AbilitySourceKind(const Fighter* fp);
ftData* Bam_AbilityData(Fighter* fp);
union Fighter_FighterVars;
union Fighter_FighterVars* Bam_AbilityVars(Fighter* fp, FighterKind family);
bool Bam_BorrowedTransform(Fighter_GObj* gobj, HSD_GObjEvent finish);
FighterKind Bam_InternalKindForCharacter(CharacterKind character);

void Bam_AerialPrepare(Fighter* fp);
/* Ground attacks and throws (normal_runtime.c). Called where the game picks
 * the move for a slot (enum BamNormalSlot): installs the equipped donor's
 * move, or ends any borrowed move when the slot is the fighter's own.
 * Returns whose move it is (the donor, else the fighter's own kind). */
FighterKind Bam_NormalBegin(Fighter_GObj* gobj, int slot);
/* Common attributes of the move in progress: the donor's during a borrowed
 * ground attack (jab windows, throw weights), else the fighter's own. */
struct ftCo_DatAttrs;
struct ftCo_DatAttrs* Bam_NormalCoAttrs(Fighter* fp);
bool Bam_AerialTryEnter(Fighter_GObj* gobj, int motion);
float Bam_AerialLandingLag(Fighter* fp, int motion, float native_lag);
MotionState* Bam_AerialMotionState(Fighter* fp, int motion);
/* Borrowed-move body scaling (anim/retarget.c): called when an animation is
 * applied; animated translations of registered body bones are mapped from
 * the donor's body to the recipient's. */
void Bam_AnimRetarget(Fighter* fp, unsigned source_kind, int first_part);
float Bam_AnimTranslate(struct HSD_JObj* jobj, int axis, float value);
void Bam_AnimScaleReset(void);
/* Animated joint rotation; retargets a borrowed bone by world orientation. */
void Bam_AnimRotate(HSD_JObj* jobj, int axis, float value);
/* Per-frame upkeep for borrowed moves (Fire Breath refill). */
void Bam_AbilityFighterFrame(Fighter* fp);
/* Beam Sword model in the hand during borrowed sword moves (visual/weapons.c). */
void Bam_SwordDisplay(struct HSD_GObj* gobj, int pass, MtxPtr vmtx);
void Bam_SwordRelease(const Fighter* fp);
/* Donor model-part group switches kept for the donor's model (true: kept),
 * and whether the borrower's own body is hidden for one (Kirby's stone). */
bool Bam_VisSet(struct HSD_GObj* gobj, int group, int val);
bool Bam_BodyHidden(struct HSD_GObj* gobj);
/* Entry points called from pinned edits in retail units (overrides/). */
struct FigaTrack;
struct HitCapsule;
void Bam_LinkAerialDownEnter(Fighter_GObj* gobj);
void Bam_PropTrack(Fighter* fp, int joint, struct FigaTrack* track, int count);
void Bam_HitboxCreated(Fighter* fp, struct HitCapsule* hit, int bone);
void Bam_HitboxRefresh(Fighter* fp, struct HitCapsule* hit);
void Bam_AnimPostStep(Fighter* fp);
bool Bam_RestSleep(Fighter_GObj* gobj);
bool Bam_RestSleeping(Fighter* fp);
void Bam_RestSleepClear(Fighter* fp);
bool Bam_ParasolFloat(HSD_GObj* gobj);
/* Every frame: a borrowed Peach up special's parasol float (visual/parasol.c). */
void Bam_ParasolTrack(Fighter* fp);
#endif
