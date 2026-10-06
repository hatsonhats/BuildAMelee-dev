/* Shared by the borrowed-move animation files in src/engine/anim/:
 *   retarget.c   body bone translation and rotation retargeting
 *   props.c      body-less donor bones rebuilt on the borrower (swords,
 *                tails, props) and donor joints posed from the root
 *   anchors.c    articles a borrowed move attaches to the fighter
 *   hitboxes.c   hitboxes on rebuilt bones and donor fingers
 *   scale.c      how big a borrowed move is on this fighter
 *   pose.c       per-frame world-space pass, the donor model's pose
 *   wear.c       clothing worn rather than held (Peach's dress)
 * Tables: build/generated/engine/bone_tables.h (tools/bam/bonetables.py). */
#ifndef BAM_ANIM_INTERNAL_H
#define BAM_ANIM_INTERNAL_H
#include <engine/special_internal.h>
#include <sysdolphin/baselib/jobj.h>
#include <melee/ft/fighter.h>
#include <math.h>
#include <engine/bone_tables.h>
#include <melee/lb/lbanim.h>
#include <sysdolphin/baselib/fobj.h>
#include <sysdolphin/baselib/memory.h>
#include <string.h>

/* ---- per-match state ----
 * Allocated on the match heap at match start (Bam_MatchBegin) so Slippi rollback
 * restores it; the pointer is NULL outside matches. */
typedef struct Scaled {
    const Fighter* fighter;
    HSD_JObj* jobj;
    float own[3], donor[3], ratio;
    float own_hip, donor_hip, size; /* rest hip heights; Bam_BorrowScale */
    int lift;                       /* a body bone: lifts are capped */
} Scaled;
typedef struct Quat { float x, y, z, w; } Quat;
typedef struct RotFix {
    HSD_JObj* jobj;
    Quat a, b;        /* C(parent)^-1 and C(bone). */
    float donor[3];   /* The donor's latest local Euler angles for this bone. */
    /* Donor body parts between this bone and its parent that the recipient
     * lacks (Marth's BustN on Mario), top first: their animated rotation is
     * folded in, so the limb still points where the donor's does. */
    unsigned char slot, kind, nfold, fold[4];
} RotFix;
#define ROTFIX_FOLDS 4
#define ROTFIX_PER_FIGHTER (BAM_REST_PARTS * 2)
#define ROTFIX_HASH 1024U
#define POSE_JOINTS 192
#include <sysdolphin/baselib/mtx.h>
typedef struct PropTrack {
    const FigaTrack* track;
    unsigned char joint, count, kind;
} PropTrack;
#define PROP_TRACKS 80 /* Peach's dress alone is 49 bones */
typedef struct PropHit {
    const Fighter* fighter;
    HitCapsule* hit;
    Vec3 offset;
    unsigned char prop, kind;
} PropHit;
typedef struct PropAnchor { HSD_JObj* jobj; short prop, root; } PropAnchor;
#define ANCHORS 4
typedef struct AnimScaleState {
    Scaled scaled[BAM_FIGHTERS * 8];
    unsigned scaled_count;
    RotFix rotfix[BAM_FIGHTERS * ROTFIX_PER_FIGHTER];
    unsigned char rotfix_part[BAM_FIGHTERS * ROTFIX_PER_FIGHTER];
    unsigned short rotfix_hash[ROTFIX_HASH];
    unsigned char joint_parent[BAM_FIGHTERS][POSE_JOINTS];
    unsigned char pose_on[BAM_FIGHTERS];
    PropTrack prop_tracks[BAM_FIGHTERS][PROP_TRACKS];
    unsigned char prop_track_count[BAM_FIGHTERS];
    PropHit prop_hits[BAM_FIGHTERS * 4];
    short last_prop[BAM_FIGHTERS], last_root[BAM_FIGHTERS];
    PropAnchor anchors[BAM_FIGHTERS][ANCHORS];
    unsigned char anchor_next[BAM_FIGHTERS];
    /* The donor body part last asked for that the borrower lacks, and the
     * joint it fell back to + 1 (0: none): Bam_NoteHeldPart. */
    signed char held_part[BAM_FIGHTERS], held_joint[BAM_FIGHTERS];
} AnimScaleState;
extern AnimScaleState* bam_anim_scale; /* pose.c */
#define anchor_next (bam_anim_scale->anchor_next)
#define held_part (bam_anim_scale->held_part)
#define held_joint (bam_anim_scale->held_joint)
#define anchors (bam_anim_scale->anchors)
#define joint_parent (bam_anim_scale->joint_parent)
#define last_prop (bam_anim_scale->last_prop)
#define last_root (bam_anim_scale->last_root)
#define pose_on (bam_anim_scale->pose_on)
#define prop_hits (bam_anim_scale->prop_hits)
#define prop_track_count (bam_anim_scale->prop_track_count)
#define prop_tracks (bam_anim_scale->prop_tracks)
#define rotfix (bam_anim_scale->rotfix)
#define rotfix_hash (bam_anim_scale->rotfix_hash)
#define rotfix_part (bam_anim_scale->rotfix_part)
#define scaled (bam_anim_scale->scaled)
#define scaled_count (bam_anim_scale->scaled_count)

/* Rest hip heights and body bone positions per fighter kind (retarget.c). */
typedef struct BodyRest {
    float hip;
    float pos[3][3]; /* XRotN, YRotN, HipN: x, y, z */
} BodyRest;
#define BODY_KINDS BAM_REST_KINDS
extern const BodyRest body_rest[BODY_KINDS];
/* Clothing worn instead of carried (wear.c). */
typedef struct Wear { unsigned char kind, group, root, hip; } Wear;

/* ---- quaternions ---- */
static inline Quat q_mul(Quat a, Quat b)
{
    Quat q;
    q.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    q.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    q.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    q.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    return q;
}
static inline Quat q_conj(Quat q) { q.x = -q.x; q.y = -q.y; q.z = -q.z; return q; }
static inline int q_identity(Quat q) { return q.w > 0.99999f || q.w < -0.99999f; }
/* HSD Euler order: R = Rz * Ry * Rx. */
static inline Quat q_euler(const float e[3])
{
    float sx = sinf(e[0] * 0.5f), cx = cosf(e[0] * 0.5f);
    float sy = sinf(e[1] * 0.5f), cy = cosf(e[1] * 0.5f);
    float sz = sinf(e[2] * 0.5f), cz = cosf(e[2] * 0.5f);
    Quat q;
    q.x = cz * cy * sx - sz * sy * cx;
    q.y = cz * sy * cx + sz * cy * sx;
    q.z = sz * cy * cx - cz * sy * sx;
    q.w = cz * cy * cx + sz * sy * sx;
    return q;
}
static inline void q_to_euler(Quat q, float e[3])
{
    float m20 = 2.0f * (q.x * q.z - q.y * q.w);
    float m21 = 2.0f * (q.y * q.z + q.x * q.w), m22 = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    float m10 = 2.0f * (q.x * q.y + q.z * q.w), m00 = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    float sy = -m20;
    if (sy > 1.0f) sy = 1.0f;
    if (sy < -1.0f) sy = -1.0f;
    e[1] = asinf(sy);
    if (sy < 0.99999f && sy > -0.99999f) {
        e[0] = atan2f(m21, m22);
        e[2] = atan2f(m10, m00);
    } else {
        float m12 = 2.0f * (q.y * q.z - q.x * q.w), m11 = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
        e[0] = atan2f(-m12, m11);
        e[2] = 0.0f;
    }
}

/* ---- shared helpers ---- */
void anchors_follow(Fighter* fp);
int body_slot(int part);
void chain_base(Fighter* fp, Mtx out);
int chain_find(unsigned kind, int joint);
void chain_local(Fighter* fp, int slot, int row, Mtx out);
void chain_world(Fighter* fp, int row, Mtx out);
Quat correction(unsigned donor, unsigned own, int i);
int fold_base(Fighter* fp, int part);
Quat fold_quat(const RotFix* e);
Quat mid_quat(unsigned kind, int part);
int own_joint(Fighter* fp, int part);
void part_fold(Fighter* fp, int from, Mtx out);
int part_joint_raw(Fighter* fp, int part);
Quat part_local(Fighter* fp, int slot, unsigned kind, int part, int finger_rest);
bool prop_active(Fighter* fp, unsigned kind);
int prop_find(unsigned kind, int joint);
void prop_local(Fighter* fp, int slot, int prop, Mtx out);
int prop_relative(Fighter* fp, int prop, Mtx out);
void prop_reset(const Fighter* fp);
int prop_root(Fighter* fp, int prop);
int rest_slot(int part);
int rest_world(unsigned kind, unsigned i, Quat* q);
RotFix* rotfix_find(HSD_JObj* jobj);
int slot_of_fighter(const Fighter* fp);
int wear_base(Fighter* fp, unsigned source, Mtx out);
const Wear* wear_of(unsigned source, unsigned shown);
#endif
