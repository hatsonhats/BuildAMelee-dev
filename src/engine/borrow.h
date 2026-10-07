/* Borrowing a move: which donor's special fills a slot, starting and ending
 * a borrowed move, and the donor's data while it plays (borrow.c,
 * donor_specials.c, normals.c, aerials.c, transform.c, rest_sleep.c). */
#ifndef BAM_BORROW_H
#define BAM_BORROW_H
#include <melee/ft/forward.h>
#include <melee/gm/forward.h>
#include <engine/fighter.h>

typedef enum BamSpecialSlot {
    BAM_SPECIAL_NEUTRAL, BAM_SPECIAL_SIDE,
    BAM_SPECIAL_UP, BAM_SPECIAL_DOWN, BAM_SPECIAL_SLOT_COUNT
} BamSpecialSlot;

typedef enum BamSpecialID {
    BAM_SPECIAL_NATIVE,
    BAM_SPECIAL_FOX_REFLECTOR = 1 + Ft_Kind_Fox * 4 + BAM_SPECIAL_DOWN,
    BAM_SPECIAL_ID_COUNT = 1 + Ft_Kind_Max * 4
} BamSpecialID;
typedef struct BamDonorSpecial {
    BamSpecialID id;  /* = 1 + donor kind * 4 + slot, as bam_specials[].id */
    CharacterKind character;
    FighterKind internal_kind;
    BamSpecialSlot native_slot;
    HSD_GObjEvent ground_enter, air_enter;
    int first_state, last_state;
    MotionState* states;
    unsigned attrs_size;
} BamDonorSpecial;
const BamDonorSpecial* Bam_DonorSpecial(BamSpecialID id);
void Bam_BorrowFighterCreated(Fighter* fp);
void Bam_BorrowFighterDestroyed(Fighter* fp);
void Bam_BorrowMatchEnd(void);
void Bam_BorrowTransformed(Fighter* src, Fighter* dst);
Fighter_GObj* Bam_ClimberPartner(Fighter* fp);
void Bam_BorrowEnd(Fighter* fp);
MotionState* Bam_DonorMotionState(Fighter* fp, int motion);
bool Bam_TrySpecial(Fighter_GObj* gobj, BamSpecialSlot slot, bool airborne);
/* Whether Bam_TrySpecial would succeed for this slot. Lets a fighter with
 * no native move in a slot (Nana's side and up special) use a borrowed one. */
bool Bam_CanTrySpecial(Fighter* fp, BamSpecialSlot slot);
bool Bam_BorrowResumeFamily(Fighter* fp, FighterKind family, BamSpecialSlot slot);
/* A lingering article (Samus's bomb) re-enters its owner's move after the move
 * ended. True when fp may run `family`'s code for `slot` now: natively, or with
 * the borrowed move re-installed. False leaves the fighter untouched. */
bool Bam_BorrowOwnerResume(Fighter* fp, FighterKind family, BamSpecialSlot slot);
int Bam_PartJoint(Fighter* fp, int part);
int Bam_DonorBoneJoint(Fighter* fp, int bone);
/* Borrower joint for a body part it lacks: the same-side hand for hand and
 * weapon parts, head for head parts, otherwise the hand extended furthest
 * from the chest (swords, cannons, props), then chest, hip, root. */
int Bam_FallbackJoint(Fighter* fp, int part);
bool Bam_InBorrowedMove(const Fighter* fp);
FighterKind Bam_DonorKind(const Fighter* fp);
ftData* Bam_DonorData(Fighter* fp);
union Fighter_FighterVars* Bam_DonorVars(Fighter* fp, FighterKind family);
bool Bam_BorrowedTransform(Fighter_GObj* gobj, HSD_GObjEvent finish);
FighterKind Bam_InternalKindForCharacter(CharacterKind character);
/* Per-frame upkeep for borrowed moves (Fire Breath refill). */
void Bam_BorrowFighterFrame(Fighter* fp);

/* ---- aerials, ground attacks and throws, Rest ---- */
void Bam_AerialPrepare(Fighter* fp);
/* Ground attacks and throws (normals.c). Called where the game picks
 * the move for a slot (enum BamNormalSlot): installs the equipped donor's
 * move, or ends any borrowed move when the slot is the fighter's own.
 * Returns whose move it is (the donor, else the fighter's own kind). */
FighterKind Bam_NormalBegin(Fighter_GObj* gobj, int slot);
struct ftCo_DatAttrs* Bam_NormalCoAttrs(Fighter* fp);
bool Bam_AerialTryEnter(Fighter_GObj* gobj, int motion);
float Bam_AerialLandingLag(Fighter* fp, int motion, float native_lag);
MotionState* Bam_AerialMotionState(Fighter* fp, int motion);
void Bam_LinkAerialDownEnter(Fighter_GObj* gobj);
bool Bam_RestSleep(Fighter_GObj* gobj);
bool Bam_RestSleeping(Fighter* fp);
void Bam_RestSleepClear(Fighter* fp);
#endif
