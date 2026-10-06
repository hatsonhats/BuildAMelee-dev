#include <engine/special_internal.h>
#include <sysdolphin/baselib/jobj.h>
#include <melee/ft/fighter.h>
#include <math.h>
#include <engine/anim_rest.inc>

/* ---- per-match state ----
 * Allocated on the match heap at match start (Bam_MatchBegin) so Slippi rollback
 * restores it; the pointer is NULL outside matches. */
typedef struct Scaled {
    const Fighter* fighter;
    HSD_JObj* jobj;
    float own[3], donor[3], ratio;
    float own_hip, donor_hip, size; /* rest hip heights; Rogue_BorrowScale */
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
#define ROTFIX_PER_FIGHTER (ROGUE_REST_PARTS * 2)
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
     * joint it fell back to + 1 (0: none): Rogue_NoteHeldPart. */
    signed char held_part[BAM_FIGHTERS], held_joint[BAM_FIGHTERS];
} AnimScaleState;
static AnimScaleState* bam_anim_scale;
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


/* Borrowed-move body scaling.
 *
 * A borrowed special or aerial plays the donor's animation through Melee's
 * cross-fighter retarget (ftPartsRemap): every bone value is copied as is.
 * Bone angles transfer correctly (all fighters share the same bone axes),
 * but the translation of the body bones does not: a crouch authored for
 * Captain Falcon (hips 12.6 high) lowers the hips 7-11 units, which puts a
 * shorter fighter's hips below the floor (Falcon Punch on Mario: -2.1).
 *
 * While a fighter plays a borrowed animation, the translation of its body
 * bones (XRotN, YRotN, HipN) is mapped from the donor's body to its own:
 *     value = own rest + (donor value - donor rest) * own hip / donor hip
 * (the hip ratio kept within 0.4..2.5, as the borrow scale is)
 * so a move lowers the hips by the same proportion of the body on everyone.
 * TransN (whole-body travel that drives movement) and every rotation are
 * untouched. Native animations and fighters without a borrowed move are
 * never changed.
 *
 * Rest values: hip height (TopN..HipN) and resting XRotN/YRotN/HipN
 * position per fighter kind, measured from the retail skeletons. */
typedef struct BodyRest {
    float hip;
    float pos[3][3]; /* XRotN, YRotN, HipN: x, y, z */
} BodyRest;
static const BodyRest body_rest[] = {
    { 4.992f, { { 0.000f, 5.667f, 0.000f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -0.675f, 0.000f } } }, /* Mario */
    { 8.300f, { { 0.000f, 8.300f, 0.000f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, 0.000f, 0.000f } } }, /* Fox */
    { 12.647f, { { 0.000f, 13.847f, -0.488f }, { 0.000f, -0.012f, -0.003f }, { 0.000f, -1.188f, 0.014f } } }, /* Captain Falcon */
    { 11.851f, { { 0.000f, 13.431f, 0.000f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -1.580f, -1.019f } } }, /* Donkey Kong */
    { 2.400f, { { 0.000f, 5.000f, 0.000f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -2.600f, 0.000f } } }, /* Kirby */
    { 17.175f, { { 0.000f, 15.525f, 0.000f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, 1.650f, 1.049f } } }, /* Bowser */
    { 8.551f, { { 0.000f, 8.552f, 0.226f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -0.001f, -0.021f } } }, /* Link */
    { 8.241f, { { 0.000f, 8.241f, 0.347f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, 0.000f, 0.000f } } }, /* Sheik */
    { 4.369f, { { 0.000f, 5.004f, 0.000f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -0.636f, 0.000f } } }, /* Ness */
    { 8.200f, { { 0.000f, 9.400f, 0.000f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -1.200f, 0.400f } } }, /* Peach */
    { 3.462f, { { 0.000f, 5.004f, 0.000f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -1.542f, -0.282f } } }, /* Popo */
    { 3.462f, { { 0.000f, 5.004f, 0.000f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -1.542f, -0.282f } } }, /* Nana */
    { 4.007f, { { 0.000f, 5.800f, 0.000f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -1.792f, -0.976f } } }, /* Pikachu */
    { 13.181f, { { 0.000f, 13.847f, -0.488f }, { 0.000f, -0.012f, -0.003f }, { 0.000f, -0.654f, -0.201f } } }, /* Samus */
    { 5.600f, { { 0.000f, 5.600f, 0.000f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, 0.000f, 0.000f } } }, /* Yoshi */
    { 2.400f, { { 0.000f, 5.000f, 0.000f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -2.600f, 0.000f } } }, /* Jigglypuff */
    { 10.307f, { { 0.000f, 10.308f, 0.226f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -0.001f, -0.021f } } }, /* Mewtwo */
    { 4.992f, { { 0.000f, 5.667f, 0.000f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -0.675f, 0.000f } } }, /* Luigi */
    { 8.700f, { { 0.000f, 9.848f, 0.426f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -1.148f, -0.026f } } }, /* Marth */
    { 8.200f, { { 0.000f, 9.400f, 0.000f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -1.200f, 0.400f } } }, /* Zelda */
    { 8.551f, { { 0.000f, 8.553f, 0.233f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -0.002f, 0.217f } } }, /* Young Link */
    { 4.992f, { { 0.000f, 5.667f, 0.000f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -0.675f, 0.000f } } }, /* Dr. Mario */
    { 8.300f, { { 0.000f, 8.300f, 0.000f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, 0.000f, 0.000f } } }, /* Falco */
    { 3.339f, { { 0.000f, 4.600f, 0.000f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -1.261f, 0.000f } } }, /* Pichu */
    { 5.800f, { { 0.000f, 6.194f, 0.000f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -0.394f, 0.000f } } }, /* Mr. Game & Watch */
    { 12.647f, { { 0.000f, 13.847f, -0.488f }, { 0.000f, -0.012f, -0.003f }, { 0.000f, -1.188f, 0.014f } } }, /* Ganondorf */
    { 8.700f, { { 0.000f, 9.848f, 0.426f }, { 0.000f, 0.000f, 0.000f }, { 0.000f, -1.148f, -0.026f } } }, /* Roy */
};
#define BODY_KINDS (sizeof(body_rest) / sizeof(body_rest[0]))

/* Three body bones, each on the live and the blend skeleton, per build fighter. */



static void forget(const Fighter* fp)
{
    unsigned i = 0;
    while (i < scaled_count)
        if (scaled[i].fighter == fp) scaled[i] = scaled[--scaled_count];
        else ++i;
}

/* ---- Rotation retargeting (world orientation per bone) ----
 *
 * Melee copies each borrowed bone rotation as is, but fighters' bones rest
 * in different orientations, so the same values bend limbs and tilt bodies
 * differently. Amalgam Fox avoids this by re-making every borrowed animation
 * on Fox's skeleton ahead of time; here the same correction is applied live.
 *
 * With D the donor's and R the recipient's rest world rotation of a body
 * part, the recipient bone is turned so its world orientation is the donor
 * bone's animated world orientation carried over by C = D^-1 * R:
 *     recipient local = C(parent)^-1 * donor local * C(bone)
 * At rest this gives exactly the recipient's own rest pose. Rest rotations
 * come from the user's disc at build time (anim_rest.inc). Bones whose
 * correction is the identity, and finger bones, keep the raw copy. */




static Quat q_mul(Quat a, Quat b)
{
    Quat q;
    q.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    q.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    q.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    q.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    return q;
}
static Quat q_conj(Quat q) { q.x = -q.x; q.y = -q.y; q.z = -q.z; return q; }
static int q_identity(Quat q) { return q.w > 0.99999f || q.w < -0.99999f; }
/* HSD Euler order: R = Rz * Ry * Rx. */
static Quat q_euler(const float e[3])
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
static void q_to_euler(Quat q, float e[3])
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
#pragma push
#pragma dont_inline on
static int rest_world(unsigned kind, unsigned i, Quat* q)
{
    const short* v;
    if (kind >= ROGUE_REST_KINDS || i >= ROGUE_REST_PARTS + ROGUE_REST_EXTRA) return 0;
    v = i < ROGUE_REST_PARTS ? rogue_rest_world[kind][i] : rogue_rest_extra[kind][i - ROGUE_REST_PARTS];
    if (!v[0] && !v[1] && !v[2] && !v[3]) return 0;
    q->x = v[0] * (1.0f / 32767.0f); q->y = v[1] * (1.0f / 32767.0f);
    q->z = v[2] * (1.0f / 32767.0f); q->w = v[3] * (1.0f / 32767.0f);
    return 1;
}
#pragma pop
/* Rest rotation of the donor's body-less joints between a part and its
 * parent part (Kirby's arm hangs from two of them); none for most. */
static Quat mid_quat(unsigned kind, int part)
{
    Quat q = { 0.0f, 0.0f, 0.0f, 1.0f };
    unsigned i;
    for (i = 0; i < ROGUE_PART_MID_COUNT; ++i)
        if (rogue_part_mid[i].kind == kind && rogue_part_mid[i].part == part) {
            q.x = rogue_part_mid[i].q[0] * (1.0f / 32767.0f); q.y = rogue_part_mid[i].q[1] * (1.0f / 32767.0f);
            q.z = rogue_part_mid[i].q[2] * (1.0f / 32767.0f); q.w = rogue_part_mid[i].q[3] * (1.0f / 32767.0f);
            break;
        }
    return q;
}
static unsigned rotfix_slot(HSD_JObj* jobj) { return ((unsigned) jobj >> 4) & (ROTFIX_HASH - 1); }
static void rotfix_rehash(void)
{
    unsigned i;
    memset(rotfix_hash, 0, sizeof(rotfix_hash));
    for (i = 0; i < sizeof(rotfix) / sizeof(rotfix[0]); ++i) {
        unsigned h;
        if (!rotfix[i].jobj) continue;
        for (h = rotfix_slot(rotfix[i].jobj); rotfix_hash[h]; h = (h + 1) & (ROTFIX_HASH - 1)) {}
        rotfix_hash[h] = (unsigned short) (i + 1);
    }
}
static RotFix* rotfix_find(HSD_JObj* jobj)
{
    unsigned h;
    for (h = rotfix_slot(jobj); rotfix_hash[h]; h = (h + 1) & (ROTFIX_HASH - 1))
        if (rotfix[rotfix_hash[h] - 1].jobj == jobj) return &rotfix[rotfix_hash[h] - 1];
    return NULL;
}
/* The recipient's own joint for a body part, or -1. Not ftParts_GetBoneIndex:
 * during a borrowed move that falls back to the nearest part the fighter
 * has, which would put one part's correction on another part's bone. */
static int part_joint_raw(Fighter* fp, int part)
{
    const FighterPartsTable* table = ftPartsTable[fp->kind];
    int joint;
    if (part < 0 || part >= ROGUE_PART_COUNT) return -1;
    joint = table->part_to_joint[part];
    if (joint == FTPART_INVALID || (unsigned) joint >= table->parts_num || !fp->parts[joint].joint) return -1;
    return joint;
}
/* The retargeted-part index (into rogue_rest_part) of a recipient joint. */
static int rest_index_of(Fighter* fp, HSD_JObj* jobj)
{
    unsigned i;
    if (!jobj) return -1;
    for (i = 0; i < ROGUE_REST_PARTS; ++i) {
        int joint = part_joint_raw(fp, rogue_rest_part[i]);
        if (joint >= 0 && fp->parts[joint].joint == jobj) return (int) i;
    }
    return -1;
}
/* Correction of part i: C = D^-1 * R, identity when either rest is unknown. */
static Quat correction(unsigned donor, unsigned own, int i)
{
    Quat d, r, c = { 0.0f, 0.0f, 0.0f, 1.0f };
    if (i >= 0 && rest_world(donor, (unsigned) i, &d) && rest_world(own, (unsigned) i, &r))
        c = q_mul(q_conj(d), r);
    return c;
}
/* Recipient joint index of each joint's parent (0xFF: root), per fighter. */


static void build_parents(Fighter* fp, unsigned slot)
{
    unsigned n = ftPartsTable[fp->kind]->parts_num, i, k;
    if (n > POSE_JOINTS) n = POSE_JOINTS;
    for (i = 0; i < n; ++i) {
        HSD_JObj* parent = fp->parts[i].joint ? HSD_JObjGetParent(fp->parts[i].joint) : NULL;
        joint_parent[slot][i] = 0xFF;
        for (k = 0; parent && k < i; ++k)
            if (fp->parts[k].joint == parent) { joint_parent[slot][i] = (unsigned char) k; break; }
    }
}
static void rotate_retarget(Fighter* fp, unsigned slot, unsigned source_kind, int first_part, int borrowed)
{
    unsigned i, k;
    RotFix* block = &rotfix[slot * ROTFIX_PER_FIGHTER];
    unsigned char* parts = &rotfix_part[slot * ROTFIX_PER_FIGHTER];
    /* Entries from the part this animation starts at are replaced. */
    for (i = 0; i < ROTFIX_PER_FIGHTER; ++i)
        if (block[i].jobj && parts[i] >= (unsigned) first_part) block[i].jobj = NULL;
    if (first_part == 0) pose_on[slot] = 0;
    if (borrowed) {
        if (first_part == 0) {
            build_parents(fp, slot);
            pose_on[slot] = 1;
        }
        for (i = 0; i < ROGUE_REST_PARTS; ++i) {
            int part = rogue_rest_part[i], joint, parent;
            unsigned char fold[ROTFIX_FOLDS], nfold;
            Quat cb, cp;
            Quat dp, db, rest;
            HSD_JObj* jobjs[2];
            if (part < first_part) continue;
            joint = part_joint_raw(fp, part);
            if (joint < 0) continue;
            parent = rest_index_of(fp, HSD_JObjGetParent(fp->parts[joint].joint));
            cb = correction(source_kind, fp->kind, (int) i);
            cp = correction(source_kind, fp->kind, parent);
            /* Donor ancestors (by body part) the recipient does not have. */
            {
                int up = rogue_part_parent[source_kind][part], chain[ROTFIX_FOLDS], n = 0, ok = 1;
                while (up != 0xFF && n < ROTFIX_FOLDS) {
                    if (part_joint_raw(fp, up) >= 0) break;
                    chain[n++] = up;
                    up = rogue_part_parent[source_kind][up];
                }
                if (up != 0xFF && n == ROTFIX_FOLDS) ok = 0;
                nfold = 0;
                while (ok && n > 0) fold[nfold++] = (unsigned char) chain[--n];
            }
            /* Every body bone is registered: the per-frame pass needs the
             * donor's own values for it (Rogue_AnimPostStep). */
            /* Until a track writes it, the bone holds the donor's rest pose. */
            if (!rest_world(source_kind, i, &db)) continue;
            rest = parent >= 0 && rest_world(source_kind, (unsigned) parent, &dp) ? q_mul(q_conj(dp), db) : db;
            jobjs[0] = fp->parts[joint].joint;
            jobjs[1] = fp->parts[joint].x4_jobj2;
            for (k = 0; k < 2; ++k) {
                RotFix* e = &block[i * 2 + k];
                if (!jobjs[k] || (k && jobjs[1] == jobjs[0]) || (jobjs[k]->flags & JOBJ_USE_QUATERNION)) continue;
                e->jobj = jobjs[k];
                e->a = q_conj(cp);
                e->b = cb;
                q_to_euler(q_mul(q_conj(mid_quat(source_kind, part)), rest), e->donor);
                e->slot = (unsigned char) slot;
                e->kind = (unsigned char) source_kind;
                e->nfold = nfold;
                memcpy(e->fold, fold, sizeof(e->fold));
                parts[i * 2 + k] = (unsigned char) part;
            }
        }
    }
    rotfix_rehash();
}
static Quat fold_quat(const RotFix* e);
static void rotfix_apply(RotFix* e)
{
    float out[3];
    Quat local = q_mul(mid_quat(e->kind, rotfix_part[e - rotfix]), q_euler(e->donor));
    if (e->nfold) local = q_mul(fold_quat(e), local);
    q_to_euler(q_mul(q_mul(e->a, local), e->b), out);
    HSD_JObjSetRotationX(e->jobj, out[0]);
    HSD_JObjSetRotationY(e->jobj, out[1]);
    HSD_JObjSetRotationZ(e->jobj, out[2]);
}
/* Animated rotation (jobj.c ROTX/ROTY/ROTZ). */
void Rogue_AnimRotate(HSD_JObj* jobj, int axis, float value)
{
    /* Every animated joint goes through here, menus included, and the
     * per-match state only exists during a match. */
    RotFix* e = bam_anim_scale ? rotfix_find(jobj) : NULL;
    if (!e) {
        if (axis == 0) HSD_JObjSetRotationX(jobj, value);
        else if (axis == 1) HSD_JObjSetRotationY(jobj, value);
        else HSD_JObjSetRotationZ(jobj, value);
        return;
    }
    e->donor[axis] = value;
    rotfix_apply(e);
}

static void prop_reset(const Fighter* fp);
void Rogue_AnimRetarget(Fighter* fp, unsigned source_kind, int first_part)
{
    static const int parts[3] = { FtPart_XRotN, FtPart_YRotN, FtPart_HipN };
    RogueFighterState* S;
    const BodyRest *own, *donor;
    float ratio;
    unsigned i, k;
    if (!fp || !bam_anim_scale) return;
    if (first_part == 0) prop_reset(fp);
    S = Rogue_FighterCtx(fp);
    if (S && S->fighter == fp) {
        unsigned slot = (unsigned) (S - bam_match->fighters);
        int borrowed = source_kind != fp->kind && (S->active || S->aerial || S->normal_on) &&
            source_kind < ROGUE_REST_KINDS && fp->kind < ROGUE_REST_KINDS;
        if (slot < BAM_FIGHTERS) rotate_retarget(fp, slot, source_kind, first_part, borrowed);
    }
    /* A partial animation that starts below the body bones leaves them alone. */
    if (first_part > FtPart_HipN) return;
    forget(fp);
    S = Rogue_FighterCtx(fp);
    if (source_kind == fp->kind || S->fighter != fp || (!S->active && !S->aerial && !S->normal_on) ||
        source_kind >= BODY_KINDS || fp->kind >= BODY_KINDS) return;
    own = &body_rest[fp->kind];
    donor = &body_rest[source_kind];
    if (own->hip <= 0.0f || donor->hip <= 0.0f) return;
    ratio = own->hip / donor->hip;
    /* Within the borrow scale's range (Rogue_BorrowScale): Jigglypuff's and
     * Kirby's hips sit 2.4 high, so their hop in a dash attack threw Captain
     * Falcon five times as high (his whole body above the dummy's head). */
    if (ratio < 0.4f) ratio = 0.4f;
    if (ratio > 2.5f) ratio = 2.5f;
    for (i = 0; i < 3; ++i) {
        int joint = part_joint_raw(fp, parts[i]);
        HSD_JObj* jobjs[2];
        if (joint == FTPART_INVALID || joint < 0) continue;
        jobjs[0] = fp->parts[joint].joint;
        jobjs[1] = fp->parts[joint].x4_jobj2;
        for (k = 0; k < 2; ++k) {
            Scaled* s;
            if (!jobjs[k] || (k && jobjs[1] == jobjs[0]) || scaled_count >= sizeof(scaled) / sizeof(scaled[0])) continue;
            s = &scaled[scaled_count++];
            s->fighter = fp;
            s->jobj = jobjs[k];
            memcpy(s->own, own->pos[i], sizeof(s->own));
            memcpy(s->donor, donor->pos[i], sizeof(s->donor));
            s->ratio = ratio;
            s->own_hip = own->hip;
            s->donor_hip = donor->hip;
            s->size = Rogue_BorrowScale(fp);
            s->lift = 1;
        }
    }
    {
        /* TransN2 carries what a move throws out from the body (Ness's
         * yo-yo in his up and down smash): the donor's positions, at the
         * move's size. */
        int joint = part_joint_raw(fp, FtPart_TransN2);
        if (joint != FTPART_INVALID && joint >= 0 && fp->parts[joint].joint &&
            scaled_count < sizeof(scaled) / sizeof(scaled[0])) {
            Scaled* s = &scaled[scaled_count++];
            memset(s, 0, sizeof(*s));
            s->fighter = fp;
            s->jobj = fp->parts[joint].joint;
            s->ratio = Rogue_BorrowScale(fp);
        }
    }
}

float Rogue_AnimTranslate(HSD_JObj* jobj, int axis, float value)
{
    unsigned i;
    if (!bam_anim_scale) return value; /* outside a match (menus) */
    for (i = 0; i < scaled_count; ++i)
        if (scaled[i].jobj == jobj) {
            const Scaled* e = &scaled[i];
            float d = value - e->donor[axis], move = d * e->ratio;
            if (e->lift && axis == 1 && d > 0.0f) {
                /* A lift (Jigglypuff's dash attack and up smash hop half
                 * her height): no higher off the ground than the donor's
                 * body goes, at the move's size, so the attack stays at the
                 * height it hits at. A taller fighter, whose hip is already
                 * that high, does not rise beyond a small bob; it never sinks for it. */
                float allowed = (e->donor_hip + d) * e->size - e->own_hip, bob = d * e->size;
                /* Small bobs keep their motion (at the move's size). */
                if (bob > 1.5f) bob = 1.5f;
                if (allowed < bob) allowed = bob;
                if (move > allowed) move = allowed;
            }
            return e->own[axis] + move;
        }
    return value;
}

void Rogue_AnimScaleReset(void)
{
    if (!bam_anim_scale) return;
    scaled_count = 0;
    memset(rotfix, 0, sizeof(rotfix));
    rotfix_rehash();
    prop_reset(NULL);
}

/* ---- Body-less bones (Marth's sword, Pikachu's tail...) ----
 *
 * Many moves put their hitboxes on a bone the recipient does not have:
 * Marth's sword hitboxes ride on his sword bone, offset along the blade.
 * Melee would put them on the recipient's root at the feet; a nearest-hand
 * fallback kept the sword-bone offsets in the hand's frame, so they pointed
 * the wrong way and bunched around the body. Instead the donor bone is
 * rebuilt on the recipient: it hangs from the same body part, with the
 * donor's rest transform and, every frame, the donor animation's own tracks
 * for it (captured while Melee retargets the animation, evaluated with
 * HSD's FObj interpreter). Hitboxes on it are placed on that body part with
 * their offset carried through the rebuilt bone each frame, and a borrowed
 * sword is drawn along it (sword_visual.c). Rest data: anim_rest.inc. */
#include <melee/lb/lbanim.h>
#include <sysdolphin/baselib/fobj.h>






static int prop_find(unsigned kind, int joint)
{
    int i;
    for (i = 0; i < ROGUE_PROP_COUNT; ++i)
        if (rogue_prop[i].kind == kind && rogue_prop[i].joint == joint) return i;
    return -1;
}
static int chain_find(unsigned kind, int joint)
{
    int i;
    for (i = 0; i < ROGUE_CHAIN_COUNT; ++i)
        if (rogue_chain[i].kind == kind && rogue_chain[i].joint == joint) return i;
    return -1;
}
static int slot_of_fighter(const Fighter* fp)
{
    RogueFighterState* S = Rogue_FighterCtx(fp);
    unsigned slot;
    if (!fp || !S || S->fighter != fp) return -1;
    slot = (unsigned) (S - bam_match->fighters);
    return slot < BAM_FIGHTERS ? (int) slot : -1;
}
static int own_joint(Fighter* fp, int part)
{
    return part_joint_raw(fp, part);
}
/* The recipient body part a prop entry hangs from: the donor's, or when the
 * recipient lacks it (Kirby has no finger bones) the donor's next part up
 * that it has. -1 if none. */
static int rest_slot(int part);
/* The body part something hanging from donor part `part` is carried by:
 * the part itself, or the nearest one above it the borrower has. Not a
 * finger even when the borrower has one: fingers are not retargeted, so the
 * borrower's finger keeps its own frame (Yoshi's is turned 120 degrees from
 * Marth's). From the hand, which is, with the donor's finger rotations
 * folded in (part_fold). -1 if none. */
static int fold_base(Fighter* fp, int part)
{
    unsigned kind = fp->x597_bits;
    int n = 0;
    while (part != 0xFF && (own_joint(fp, part) < 0 || rest_slot(part) >= ROGUE_REST_PARTS) && n++ < 8)
        part = kind < ROGUE_REST_KINDS ? rogue_part_parent[kind][part] : 0xFF;
    return part == 0xFF || n > 8 ? -1 : part;
}
static int prop_part(Fighter* fp, int prop)
{
    return fold_base(fp, rogue_prop[prop].part);
}
/* The recipient joint a prop entry hangs from, or -1. */
static int prop_root(Fighter* fp, int prop)
{
    int part = prop_part(fp, prop), joint;
    if (part >= 0) return own_joint(fp, part);
    joint = Rogue_AbilityFallbackJoint(fp, rogue_prop[prop].part);
    if (joint < 0 || (unsigned) joint >= ftPartsTable[fp->kind]->parts_num || !fp->parts[joint].joint) return -1;
    return joint;
}
/* Slot of a body part's rest rotation: the retargeted parts, then the
 * fingers (rest_world only; rotate_retarget walks the first ones). */
#pragma push
#pragma dont_inline on
static int rest_slot(int part)
{
    int i;
    for (i = 0; i < ROGUE_REST_PARTS; ++i)
        if (rogue_rest_part[i] == part) return i;
    for (i = 0; i < ROGUE_REST_EXTRA; ++i)
        if (rogue_rest_extra_part[i] == part) return ROGUE_REST_PARTS + i;
    return -1;
}
#pragma pop
/* A body part the same fighter-to-fighter correction applies to. The
 * fingers are not retargeted (each fighter keeps its own finger pose), so a
 * prop or hitbox on a finger both fighters have keeps the donor's offset as
 * is; their rest data only carries offsets onto another bone. */
static int body_slot(int part)
{
    int i = rest_slot(part);
    return i < ROGUE_REST_PARTS ? i : -1;
}

/* Melee is retargeting donor joint `joint` of the current animation
 * (ftanim.c): keep its tracks if it is a prop (including a weapon bone the
 * recipient has a namesake of, like Kirby's hammer bone) or a body part the
 * recipient lacks (folded into the next bone). */
void Rogue_PropTrack(Fighter* fp, int joint, FigaTrack* track, int count)
{
    int slot = slot_of_fighter(fp);
    unsigned i, kind;
    const FighterPartsTable* from;
    if (!bam_anim_scale || slot < 0 || count <= 0 || !track || joint < 0 || joint > 255) return;
    kind = fp->x597_bits;
    if (kind >= ROGUE_REST_KINDS) return;
    from = ftPartsTable[kind];
    if (prop_find(kind, joint) < 0 && chain_find(kind, joint) < 0) {
        /* Body parts the recipient lacks: those folded into a bone (no
         * finger bones) and those a prop hangs from. */
        int part = (unsigned) joint < from->parts_num ? from->joint_to_part[joint] : FTPART_INVALID, k, used = 0;
        if (part == FTPART_INVALID) return;
        for (k = 0; !used && k < ROGUE_PROP_COUNT; ++k)
            used = rogue_prop[k].kind == kind && rogue_prop[k].part == part;
        /* Fingers between a prop and the hand are folded even when the
         * borrower has them (prop_part); other bones it has are its own. */
        /* Fingers with rest data are folded even when the borrower has
         * them (fold_base: props and hitboxes hanging from them). */
        if (rest_slot(part) >= ROGUE_REST_PARTS) used = 1;
        else if (ftPartsRemap(fp->kind, kind, joint) != FTPART_INVALID) return;
        if (!used) used = body_slot(part) >= 0;
        if (!used) return;
    }
    for (i = 0; i < prop_track_count[slot]; ++i)
        if (prop_tracks[slot][i].joint == joint) break;
    if (i == prop_track_count[slot]) {
        if (i >= PROP_TRACKS) return;
        ++prop_track_count[slot];
    }
    prop_tracks[slot][i].track = track;
    prop_tracks[slot][i].count = (unsigned char) count;
    prop_tracks[slot][i].joint = (unsigned char) joint;
    prop_tracks[slot][i].kind = (unsigned char) kind;
}

typedef struct PropEval { float v[11]; } PropEval;
static void prop_store(void* obj, enum_t type, HSD_ObjData* value)
{
    if (type >= 1 && type <= 10) ((PropEval*) obj)->v[type] = value->fv;
}
/* Overwrite e with the captured donor tracks of `joint` at frame. */
static void eval_tracks(int slot, unsigned kind, int joint, float frame, PropEval* e)
{
    unsigned i, k;
    for (i = 0; slot >= 0 && i < prop_track_count[slot]; ++i) {
        const PropTrack* pt = &prop_tracks[slot][i];
        if (pt->joint != joint || pt->kind != kind) continue;
        for (k = 0; k < pt->count; ++k) {
            HSD_FObj f;
            const FigaTrack* tr = &pt->track[k];
            if (tr->obj_type < 1 || tr->obj_type > 10 || tr->obj_type == 4) continue;
            memset(&f, 0, sizeof(f));
            f.startframe = (s16) tr->startframe;
            f.obj_type = tr->obj_type;
            f.frac_value = tr->frac_value;
            f.frac_slope = tr->frac_slope;
            f.ad_head = tr->ad_head;
            f.length = tr->length;
            HSD_FObjReqAnimAll(&f, frame);
            HSD_FObjInterpretAnim(&f, e, prop_store, 0.0f);
        }
        return;
    }
}
/* The donor's animated local rotation of one of its body parts. */
/* finger_rest: a finger's own rest values under its tracks (a prop folded
 * onto another bone); else a finger starts from zero, as the per-frame
 * retarget has always folded them. */
static Quat part_local(Fighter* fp, int slot, unsigned kind, int part, int finger_rest)
{
    Quat d, dp, rest, none = { 0.0f, 0.0f, 0.0f, 1.0f };
    PropEval v;
    int up = rogue_part_parent[kind][part], joint = ftPartsTable[kind]->part_to_joint[part];
    int si = rest_slot(part), sp = up != 0xFF ? rest_slot(up) : -1;
    if (finger_rest && si >= ROGUE_REST_PARTS && kind < ROGUE_REST_KINDS) {
        /* A finger: its own rest values as the file has them, which the
         * tracks override channel by channel (a rotation rebuilt from the
         * world rest can split into other angles and mix badly). */
        const short* r = rogue_rest_extra_rot[kind][si - ROGUE_REST_PARTS];
        v.v[1] = r[0] * (1.0f / 4096.0f); v.v[2] = r[1] * (1.0f / 4096.0f); v.v[3] = r[2] * (1.0f / 4096.0f);
        if (joint == FTPART_INVALID) return none;
    } else if (si < 0 || si >= ROGUE_REST_PARTS || !rest_world(kind, (unsigned) si, &d)) {
        /* No rest data: the tracks alone, from zero. */
        v.v[1] = v.v[2] = v.v[3] = 0.0f;
        if (joint == FTPART_INVALID) return none;
    } else {
        rest = sp >= 0 && rest_world(kind, (unsigned) sp, &dp) ? q_mul(q_conj(dp), d) : d;
        q_to_euler(q_mul(q_conj(mid_quat(kind, part)), rest), &v.v[1]);
    }
    if (joint != FTPART_INVALID) eval_tracks(slot, kind, joint, fp->cur_anim_frame, &v);
    return q_mul(mid_quat(kind, part), q_euler(&v.v[1]));
}
/* The animated local rotation of the donor's body parts a RotFix folds in. */
static Quat fold_quat(const RotFix* e)
{
    Quat q = { 0.0f, 0.0f, 0.0f, 1.0f };
    Fighter* fp = bam_match->fighters[e->slot].fighter;
    unsigned i;
    if (!fp) return q;
    for (i = 0; i < e->nfold; ++i) q = q_mul(q, part_local(fp, e->slot, e->kind, e->fold[i], 0));
    return q;
}
/* Local transform of a prop entry at the fighter's current animation frame. */
static void prop_local(Fighter* fp, int slot, int prop, Mtx out)
{
    const RogueProp* p = &rogue_prop[prop];
    PropEval e;
    Vec3 s, r, t;
    e.v[1] = p->rot[0] * (1.0f / 4096.0f); e.v[2] = p->rot[1] * (1.0f / 4096.0f);
    e.v[3] = p->rot[2] * (1.0f / 4096.0f);
    e.v[5] = p->pos[0] * (1.0f / 256.0f); e.v[6] = p->pos[1] * (1.0f / 256.0f);
    e.v[7] = p->pos[2] * (1.0f / 256.0f);
    e.v[8] = p->scale[0] * (1.0f / 4096.0f); e.v[9] = p->scale[1] * (1.0f / 4096.0f);
    e.v[10] = p->scale[2] * (1.0f / 4096.0f);
    eval_tracks(slot, fp->x597_bits, p->joint, fp->cur_anim_frame, &e);
    s.x = e.v[8]; s.y = e.v[9]; s.z = e.v[10];
    r.x = e.v[1]; r.y = e.v[2]; r.z = e.v[3];
    t.x = e.v[5]; t.y = e.v[6]; t.z = e.v[7];
    HSD_MtxSRT(out, &s, &r, &t, NULL);
}
/* ---- Joints posed through the donor's own skeleton (rogue_chain) ----
 *
 * Mr. Game & Watch's aerial hitboxes all hang from his root, and what he
 * holds is where his own long arm puts it. Those joints are posed from the
 * borrower's root through the donor's whole animated skeleton (at the
 * donor's size), so the box, key and pan, and the articles he hangs from
 * them, land on the hitboxes whatever the borrower's limbs look like. */
static void chain_local(Fighter* fp, int slot, int row, Mtx out)
{
    const RogueChain* c = &rogue_chain[row];
    PropEval e;
    Vec3 s, r, t;
    e.v[1] = c->rot[0] * (1.0f / 4096.0f); e.v[2] = c->rot[1] * (1.0f / 4096.0f);
    e.v[3] = c->rot[2] * (1.0f / 4096.0f);
    e.v[5] = c->pos[0] * (1.0f / 256.0f); e.v[6] = c->pos[1] * (1.0f / 256.0f);
    e.v[7] = c->pos[2] * (1.0f / 256.0f);
    e.v[8] = c->scale[0] * (1.0f / 4096.0f); e.v[9] = c->scale[1] * (1.0f / 4096.0f);
    e.v[10] = c->scale[2] * (1.0f / 4096.0f);
    eval_tracks(slot, c->kind, c->joint, fp->cur_anim_frame, &e);
    s.x = e.v[8]; s.y = e.v[9]; s.z = e.v[10];
    r.x = e.v[1]; r.y = e.v[2]; r.z = e.v[3];
    t.x = e.v[5]; t.y = e.v[6]; t.z = e.v[7];
    HSD_MtxSRT(out, &s, &r, &t, NULL);
}
/* The borrower's root at the donor's size: where a chain starts. */
static void chain_base(Fighter* fp, Mtx out)
{
    float bs = Rogue_BorrowScale(fp);
    Mtx grow;
    PSMTXScale(grow, bs, bs, bs);
    PSMTXConcat(HSD_JObjGetMtxPtr(fp->parts[0].joint), grow, out);
}
/* World matrix of chain entry `row`. */
static void chain_world(Fighter* fp, int row, Mtx out)
{
    int path[24], n = 0, i, slot = slot_of_fighter(fp);
    Mtx local;
    for (i = row; i != 255 && i < ROGUE_CHAIN_COUNT && n < 24; i = rogue_chain[i].parent) path[n++] = i;
    chain_base(fp, out);
    /* The root entry is the borrower's root itself. */
    for (i = n - 1; i >= 0; --i) {
        if (rogue_chain[path[i]].parent == 255) continue;
        chain_local(fp, slot, path[i], local);
        PSMTXConcat(out, local, out);
    }
}

/* Transform from the body part a prop hangs from to the prop, including the
 * rest correction between the donor's and the recipient's body part. */
/* From the body part fold_base carries donor part `from` by to that donor
 * part (the rest correction, and the donor bones between them folded in). */
static void part_fold(Fighter* fp, int from, Mtx out)
{
    int slot = slot_of_fighter(fp);
    unsigned source = fp->x597_bits;
    Mtx local;
    Quat c;
    Quaternion cq;
    int base = fold_base(fp, from), part;
    /* C(base)^-1, then the donor parts between base and the prop's own part
     * that it hangs from instead: their rotations, and for fingers their
     * offsets too (Ice Climbers' hammer hangs three bones out from the
     * hand). */
    c = correction(source, fp->kind, base >= 0 ? body_slot(base) : -1);
    c = q_conj(c);
    cq.x = c.x; cq.y = c.y; cq.z = c.z; cq.w = c.w;
    PSMTXQuat(out, &cq);
    if (base >= 0) {
        int path[8], k = 0;
        for (part = from; part != 0xFF && part != base && k < 8; part = rogue_part_parent[source][part])
            path[k++] = part;
        while (k--) {
            Quat q = part_local(fp, slot, source, path[k], 1);
            int si = rest_slot(path[k]);
            cq.x = q.x; cq.y = q.y; cq.z = q.z; cq.w = q.w;
            PSMTXQuat(local, &cq);
            if (si >= ROGUE_REST_PARTS && source < ROGUE_REST_KINDS) {
                const short* t = rogue_rest_extra_pos[source][si - ROGUE_REST_PARTS];
                local[0][3] = t[0] * (1.0f / 256.0f);
                local[1][3] = t[1] * (1.0f / 256.0f);
                local[2][3] = t[2] * (1.0f / 256.0f);
            }
            PSMTXConcat(out, local, out);
        }
    }
}
static int prop_relative(Fighter* fp, int prop, Mtx out)
{
    int slot = slot_of_fighter(fp), chain[8], n = 0, i;
    Mtx local;
    for (i = prop; i != 255 && n < 8; i = rogue_prop[i].parent) chain[n++] = i;
    if (i != 255) return 0;
    part_fold(fp, rogue_prop[prop].part, out);
    while (n--) {
        prop_local(fp, slot, chain[n], local);
        PSMTXConcat(out, local, out);
    }
    return 1;
}

static bool prop_active(Fighter* fp, unsigned kind)
{
    return Rogue_IsAbilityState(fp) && Rogue_AbilitySourceKind(fp) == kind && fp->x597_bits == kind &&
        fp->kind != kind;
}

/* The last rebuilt bone a borrowed move resolved, per borrower: articles the
 * move then attaches to that joint ride on the rebuilt bone (Rogue_ItemAnchor). */

/* Recipient joint for donor joint `bone` when it is a prop, else -1. */
int Rogue_PropJoint(Fighter* fp, int bone)
{
    int prop, root, slot, row;
    if (!prop_active(fp, Rogue_AbilitySourceKind(fp))) return -1;
    prop = prop_find(Rogue_AbilitySourceKind(fp), bone);
    root = prop >= 0 ? prop_root(fp, prop) : -1;
    slot = slot_of_fighter(fp);
    if (root >= 0 && slot >= 0) {
        last_prop[slot] = (short) (prop + 1);
        last_root[slot] = (short) root;
    } else if (prop < 0 && (row = chain_find(Rogue_AbilitySourceKind(fp), bone)) >= 0 && fp->parts[0].joint) {
        /* Posed from the root (rogue_chain): articles ride on that pose. */
        root = 0;
        if (slot >= 0) {
            last_prop[slot] = (short) -(row + 1);
            last_root[slot] = 0;
        }
    }
    return root;
}

/* ---- Articles a borrowed move attaches ----
 *
 * An article a borrowed move attaches to a fighter joint (Peach's Toad, Mr.
 * Game & Watch's parachute) is constrained to that joint's position and
 * orientation, but this fighter's joint is not turned the way the donor's
 * is. The article is given an anchor instead, carrying the donor's frame:
 * the rebuilt bone's world matrix when the joint is the base of a rebuilt
 * donor bone, otherwise the joint turned by the rest correction (as the
 * donor's own meshes are, Rogue_DonorPose). It follows every frame and is
 * the plain joint again once the move ends. */


static void anchor_place(Fighter* fp, PropAnchor* a)
{
    Mtx rel, world, grow;
    unsigned source = Rogue_AbilitySourceKind(fp);
    float bs = Rogue_BorrowScale(fp);
    HSD_JObj* base;
    if (a->root < 0 || (unsigned) a->root >= ftPartsTable[fp->kind]->parts_num || !fp->parts[a->root].joint) return;
    base = fp->parts[a->root].joint;
    PSMTXScale(grow, bs, bs, bs);
    if (prop_active(fp, source) && a->prop <= -2) {
        chain_world(fp, -2 - a->prop, world);
    } else if (prop_active(fp, source) && a->prop >= 0 && prop_relative(fp, a->prop, rel)) {
        PSMTXConcat(grow, rel, rel);
        PSMTXConcat(HSD_JObjGetMtxPtr(base), rel, world);
    } else if (prop_active(fp, source) && a->prop < 0) {
        int part = ftPartsTable[fp->kind]->joint_to_part[a->root];
        int from = part != FTPART_INVALID ? fold_base(fp, part) : -1, own = from >= 0 ? own_joint(fp, from) : -1;
        if (from >= 0 && from != part && own >= 0) {
            /* Held on a finger (Mr. Game & Watch's Judge sign on his right
             * thumb): fingers are not retargeted, so the borrower's thumb
             * points its own way (the sign was edge-on). Carried from the
             * hand with the donor's finger rotations folded in, as props
             * hanging from fingers are. */
            part_fold(fp, part, rel);
            PSMTXConcat(grow, rel, rel);
            PSMTXConcat(HSD_JObjGetMtxPtr(fp->parts[own].joint), rel, world);
        } else {
            Quat c = correction(source, fp->kind, part != FTPART_INVALID ? body_slot(part) : -1);
            Quaternion q;
            q.x = -c.x; q.y = -c.y; q.z = -c.z; q.w = c.w;
            PSMTXQuat(rel, &q);
            PSMTXConcat(rel, grow, rel);
            PSMTXConcat(HSD_JObjGetMtxPtr(base), rel, world);
        }
    } else {
        PSMTXCopy(HSD_JObjGetMtxPtr(base), world);
    }
    HSD_JObjCopyMtx(a->jobj, world);
    a->jobj->flags |= JOBJ_USER_DEF_MTX | JOBJ_MTX_INDEP_PARENT | JOBJ_MTX_INDEP_SRT | JOBJ_USE_QUATERNION;
    {
        /* The anchor has no parent: lb_8000B1CC (where an absorb, reflect or
         * shield bubble is, and articles' positions) then reads its SRT, not
         * its matrix, unless it has a rotation or scale. Keep both: the
         * bubble of a borrowed Oil Panic sat at the stage's origin. */
        float sx = sqrtf(world[0][0] * world[0][0] + world[1][0] * world[1][0] + world[2][0] * world[2][0]);
        float sy = sqrtf(world[0][1] * world[0][1] + world[1][1] * world[1][1] + world[2][1] * world[2][1]);
        float sz = sqrtf(world[0][2] * world[0][2] + world[1][2] * world[1][2] + world[2][2] * world[2][2]);
        float m00 = world[0][0] / sx, m11 = world[1][1] / sy, m22 = world[2][2] / sz, t = m00 + m11 + m22, r;
        Quaternion q;
        if (t > 0.0f) {
            r = sqrtf(1.0f + t) * 2.0f;
            q.w = 0.25f * r;
            q.x = (world[2][1] / sy - world[1][2] / sz) / r;
            q.y = (world[0][2] / sz - world[2][0] / sx) / r;
            q.z = (world[1][0] / sx - world[0][1] / sy) / r;
        } else if (m00 > m11 && m00 > m22) {
            r = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
            q.w = (world[2][1] / sy - world[1][2] / sz) / r;
            q.x = 0.25f * r;
            q.y = (world[0][1] / sy + world[1][0] / sx) / r;
            q.z = (world[0][2] / sz + world[2][0] / sx) / r;
        } else if (m11 > m22) {
            r = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
            q.w = (world[0][2] / sz - world[2][0] / sx) / r;
            q.x = (world[0][1] / sy + world[1][0] / sx) / r;
            q.y = 0.25f * r;
            q.z = (world[1][2] / sz + world[2][1] / sy) / r;
        } else {
            r = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
            q.w = (world[1][0] / sx - world[0][1] / sy) / r;
            q.x = (world[0][2] / sz + world[2][0] / sx) / r;
            q.y = (world[1][2] / sz + world[2][1] / sy) / r;
            q.z = 0.25f * r;
        }
        a->jobj->rotate = q;
        a->jobj->scale.x = sx; a->jobj->scale.y = sy; a->jobj->scale.z = sz;
        a->jobj->translate.x = world[0][3]; a->jobj->translate.y = world[1][3]; a->jobj->translate.z = world[2][3];
    }
}
/* The joint an article attaches to (it_80274F48): `part` is this fighter's
 * joint index. */
/* A joint of this fighter for `part`, never NULL: a donor's part the
 * borrower lacks (or has no joint for) falls back to the nearest equivalent,
 * then the root. Constraining an article to NULL asserts in robj.c (Peach's
 * parasol froze the game). */
static HSD_JObj* part_joint(Fighter* fp, int part)
{
    unsigned n = ftPartsTable[fp->kind]->parts_num;
    int j;
    if (part >= 0 && (unsigned) part < n && fp->parts[part].joint) return fp->parts[part].joint;
    j = Rogue_AbilityFallbackJoint(fp, part);
    if (j >= 0 && (unsigned) j < n && fp->parts[j].joint) return fp->parts[j].joint;
    return fp->parts[0].joint;
}

/* The donor body part last asked for that this fighter lacks, and the joint
 * it fell back to (ftParts_GetBoneIndex): an article attached to that joint
 * next is held in the frame of the part asked for. */
void Rogue_NoteHeldPart(Fighter* fp, int part, int joint)
{
    int slot = bam_anim_scale ? slot_of_fighter(fp) : -1;
    if (slot < 0) return;
    held_part[slot] = (signed char) part;
    held_joint[slot] = (signed char) (joint + 1);
}

HSD_JObj* Rogue_ItemAnchor(HSD_GObj* gobj, int part)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int slot = slot_of_fighter(fp), prop = -1;
    unsigned i;
    if (slot < 0 || part < 0 || (unsigned) part >= ftPartsTable[fp->kind]->parts_num ||
        !prop_active(fp, Rogue_AbilitySourceKind(fp)))
        return part_joint(fp, part);
    if (last_root[slot] == part && last_prop[slot] > 0 && last_prop[slot] <= ROGUE_PROP_COUNT)
        prop = last_prop[slot] - 1;
    else if (last_root[slot] == part && last_prop[slot] < 0 && -last_prop[slot] <= ROGUE_CHAIN_COUNT)
        prop = -2 - (-last_prop[slot] - 1);
    else {
        /* An article held by a donor body part posed through the donor's
         * own skeleton (Mr. Game & Watch's hand: his Judge sign, turtle,
         * torch): it rides on that pose, as his props do. On the
         * borrower's own hand it faced along the wrong axis (seen edge-on). */
        unsigned source = Rogue_AbilitySourceKind(fp);
        int ftpart = ftPartsTable[fp->kind]->joint_to_part[part], row;
        if (held_joint[slot] == part + 1) {
            ftpart = held_part[slot];
            held_joint[slot] = 0;
        }
        if (source < Ft_Kind_Max && ftpart != FTPART_INVALID && (unsigned) ftpart < ftPartsTable[source]->parts_num) {
            int joint = ftPartsTable[source]->part_to_joint[ftpart];
            if (joint != FTPART_INVALID && (row = chain_find(source, joint)) >= 0) prop = -2 - row;
        }
    }
    for (i = 0; i < ANCHORS; ++i)
        if (anchors[slot][i].jobj && anchors[slot][i].prop == prop && anchors[slot][i].root == part) break;
    if (i == ANCHORS) {
        i = anchor_next[slot]++ % ANCHORS;
        if (!anchors[slot][i].jobj) anchors[slot][i].jobj = HSD_JObjAlloc();
        if (!anchors[slot][i].jobj) return part_joint(fp, part);
        anchors[slot][i].prop = (short) prop;
        anchors[slot][i].root = (short) part;
        /* Diagnostics for articles held edge-on (Mr. Game & Watch's). */
        BAM_NOTE("item_anchor donor=%u joint=%d part=%d prop=%d\n", (unsigned) Rogue_AbilitySourceKind(fp), part,
                 (int) ftPartsTable[fp->kind]->joint_to_part[part], prop);
    }
    anchor_place(fp, &anchors[slot][i]);
#if BAM_DEBUG
    BAM_LOG("item_anchor donor=%u joint=%d prop=%d\n", Rogue_AbilitySourceKind(fp), part, prop);
#endif
    return anchors[slot][i].jobj;
}

/* The joint an absorb, reflect or shield bubble rides on (ftcoll.c platform
 * fixes): the move's bone id is the donor's, so on a borrower it is mapped
 * like a hitbox's (Mr. Game & Watch's Oil Panic bucket is on his own thumb
 * bone: the borrower's joint of that number put the absorb bubble at the
 * legs, and nothing landed in the bucket), and held in the donor's frame
 * (a rebuilt or root-posed bone follows its pose) as articles are. */
HSD_JObj* Rogue_DonorBoneJObj(Fighter* fp, int bone)
{
    int joint;
    if (!Rogue_IsAbilityState(fp) || Rogue_AbilitySourceKind(fp) == fp->kind)
        return fp->parts[bone].joint;
    joint = Rogue_AbilityMapBone(fp, bone);
    if (joint < 0 || (unsigned) joint >= ftPartsTable[fp->kind]->parts_num || !fp->parts[joint].joint) joint = 0;
    return Rogue_ItemAnchor(fp->gobj, joint);
}

static PropHit* hit_record(const HitCapsule* hit)
{
    unsigned i;
    if (!bam_anim_scale) return NULL;
    for (i = 0; i < sizeof(prop_hits) / sizeof(prop_hits[0]); ++i)
        if (prop_hits[i].hit == hit) return &prop_hits[i];
    return NULL;
}
/* Body size per fighter kind: the standing height to the top of the
 * hurtboxes, measured in game (QA "SIZE" lines, tools/qa: bam.py build --qa
 * size), divided by the fighter's model scale so it is in skeleton units.
 * The old table (head bone height) made Bowser 2.1x Marth and Jigglypuff
 * 0.43x; by the body they are 1.17x and 0.65x. Pichu's head hurtbox is
 * oversized (radius 5.1), which made it measure taller than Pikachu; it is
 * Pikachu's height scaled by their standing ECB tops (5.71 / 6.98). */
static const float body_size[] = {
    13.23f, /* Mario */ 16.46f, /* Fox */ 19.19f, /* Captain Falcon */
    17.09f, /* Donkey Kong */ 10.57f, /* Kirby */ 32.59f, /* Bowser */
    14.86f, /* Link */ 12.71f, /* Sheik */ 13.26f, /* Ness */
    15.84f, /* Peach */ 11.46f, /* Popo */ 11.46f, /* Nana */
    13.22f, /* Pikachu */ 21.17f, /* Samus */ 16.02f, /* Yoshi */
    13.20f, /* Jigglypuff */ 18.75f, /* Mewtwo */ 12.96f, /* Luigi */
    16.67f, /* Marth */ 14.89f, /* Zelda */ 15.64f, /* Young Link */
    13.27f, /* Dr. Mario */ 16.89f, /* Falco */ 19.50f, /* Pichu */
    13.06f, /* Mr. Game & Watch */ 20.27f, /* Ganondorf */ 17.36f, /* Roy */
};
#define BODY_SIZE_KINDS (sizeof(body_size) / sizeof(body_size[0]))
/* Model scale per fighter kind (co_attrs.model_scaling), to compare sizes
 * as drawn: body_size x this is the height on screen. */
static const float model_scale[] = {
    1.10f, /* Mario */ 0.96f, /* Fox */ 0.97f, /* Captain Falcon */
    1.00f, /* Donkey Kong */ 0.92f, /* Kirby */ 0.69f, /* Bowser */
    1.22f, /* Link */ 1.40f, /* Sheik */ 1.00f, /* Ness */
    1.15f, /* Peach */ 1.15f, /* Popo */ 1.15f, /* Nana */
    0.90f, /* Pikachu */ 0.88f, /* Samus */ 1.05f, /* Yoshi */
    0.94f, /* Jigglypuff */ 1.00f, /* Mewtwo */ 1.25f, /* Luigi */
    1.15f, /* Marth */ 1.26f, /* Zelda */ 0.96f, /* Young Link */
    1.10f, /* Dr. Mario */ 1.10f, /* Falco */ 0.50f, /* Pichu */
    1.02f, /* Mr. Game & Watch */ 1.08f, /* Ganondorf */ 1.08f, /* Roy */
};

/* How a borrowed move's own parts are sized on this fighter: its weapons,
 * tails and props (rebuilt bones and the donor's meshes), the articles it
 * holds, and its hitboxes. They follow 70% of the body ratio (ratio^0.7),
 * kept within 0.8x..1.55x of the donor's own size (Marth's sword on Jigglypuff stays
 * near Marth's; Pichu's tail on Bowser grows, not to Bowser size), and the hitboxes stay on
 * the parts that are drawn.
 * Rogue_ShownScale: that size on screen, against the donor's own (0 when
 * own and source are the same kind). */
#pragma push
#pragma dont_inline on
float Rogue_ShownScale(unsigned own, unsigned source)
{
    float shown;
    if (source == own || source >= BODY_SIZE_KINDS || own >= BODY_SIZE_KINDS) return 0.0f;
    shown = (body_size[own] * model_scale[own]) / (body_size[source] * model_scale[source]);
    /* About 70% of the size difference (in log space), then clamped. */
    shown = powf(shown, 0.70f);
    if (shown < 0.80f) shown = 0.80f;
    else if (shown > 1.55f) shown = 1.55f;
    return shown;
}

static float shown_scale(Fighter* fp, unsigned* source)
{
    if (!fp || !Rogue_IsAbilityState(fp)) return 0.0f;
    *source = Rogue_AbilitySourceKind(fp);
    return Rogue_ShownScale(fp->kind, *source);
}

/* In the fighter's own skeleton units: the shown size over the model scale
 * difference, as the skeleton applies this fighter's model scale. */
float Rogue_BorrowScale(Fighter* fp)
{
    unsigned source;
    float shown = shown_scale(fp, &source);
    if (shown == 0.0f) return 1.0f;
    return shown * model_scale[source] / model_scale[fp->kind];
}

/* Effects a fighter spawns sized by its own scale (eflib.c): a borrowed
 * move's are sized as its hitbox offsets are, so they line up. */
float Rogue_EffectScale(HSD_GObj* gobj)
{
    if (!gobj || gobj->classifier != HSD_GOBJ_CLASS_FIGHTER) return 1.0f;
    return Rogue_BorrowScale(GET_FIGHTER(gobj));
}

/* In world units: hitbox radii, which no model scale applies to. */
float Rogue_HitboxScale(Fighter* fp)
{
    unsigned source;
    float shown = shown_scale(fp, &source);
    return shown == 0.0f ? 1.0f : shown;
}
#pragma pop
/* Where a borrowed projectile leaves from: not lower than about the middle
 * of the borrower's body, so a shot from a short fighter's low bone does not
 * start in the floor (Samus's Charge Shot from Jigglypuff). */
void Rogue_ProjectileOrigin(Fighter* fp, Vec3* pos)
{
    float low;
    if (!fp || !pos || !Rogue_IsAbilityState(fp) || (unsigned) fp->kind >= BODY_SIZE_KINDS) return;
    low = fp->cur_pos.y + 0.45f * body_size[fp->kind] * model_scale[fp->kind];
    if (pos->y < low) pos->y = low;
}
float Rogue_OwnerScale(HSD_GObj* owner)
{
    Fighter* fp = owner ? GET_FIGHTER(owner) : NULL;
    if (!fp) return 1.0f;
    return fp->x34_scale.y * Rogue_BorrowScale(fp);
}
/* Hitboxes on a joint posed through the donor's own skeleton (rogue_chain,
 * Mr. Game & Watch's arm and what it holds) hang from the borrower's root:
 * prop >= PROP_CHAIN marks chain row prop - PROP_CHAIN. */
#define PROP_CHAIN 128
/* prop >= PROP_FINGER: a hitbox on donor finger prop - PROP_FINGER, carried
 * by the hand with the donor's finger pose (Ness's bat, Game & Watch's jab). */
#define PROP_FINGER 192
static void hit_place(Fighter* fp, PropHit* h)
{
    Mtx rel;
    Vec3 out;
    float s = Rogue_BorrowScale(fp);
    if (h->prop >= PROP_FINGER) {
        part_fold(fp, h->prop - PROP_FINGER, rel);
        PSMTXMultVec(rel, &h->offset, &out);
        out.x *= s; out.y *= s; out.z *= s;
        h->hit->b_offset = out;
        return;
    }
    if (h->prop >= PROP_CHAIN) {
        Mtx w, inv;
        Vec3 world;
        if (!fp->parts[0].joint) return;
        chain_world(fp, h->prop - PROP_CHAIN, w);
        PSMTXMultVec(w, &h->offset, &world);
        if (!PSMTXInverse(HSD_JObjGetMtxPtr(fp->parts[0].joint), inv)) return;
        PSMTXMultVec(inv, &world, &h->hit->b_offset);
        return;
    }
    if (!prop_relative(fp, h->prop, rel)) return;
    PSMTXMultVec(rel, &h->offset, &out);
    out.x *= s; out.y *= s; out.z *= s;
    h->hit->b_offset = out;
}
#if BAM_QA
/* QA: for a hitbox on a rebuilt donor bone, the world position of the donor
 * body part that bone hangs from (rebuilt on the borrower), and that part;
 * -1 if it is not one. Hitbox positions are compared from it. */
int Rogue_QAHitAnchor(Fighter* fp, const HitCapsule* hit, Vec3* pos)
{
    PropHit* h = hit_record(hit);
    Mtx rel, grow, w;
    int root;
    float bs;
    int from;
    if (!fp || !h || h->fighter != fp || (h->prop >= PROP_CHAIN && h->prop < PROP_FINGER)) return -1;
    from = h->prop >= PROP_FINGER ? h->prop - PROP_FINGER : rogue_prop[h->prop].part;
    root = fold_base(fp, from);
    root = root >= 0 ? own_joint(fp, root) : -1;
    if (root < 0 || !fp->parts[root].joint) return -1;
    part_fold(fp, from, rel);
    bs = Rogue_BorrowScale(fp);
    PSMTXScale(grow, bs, bs, bs);
    PSMTXConcat(grow, rel, rel);
    PSMTXConcat(HSD_JObjGetMtxPtr(fp->parts[root].joint), rel, w);
    pos->x = w[0][3]; pos->y = w[1][3]; pos->z = w[2][3];
    return from;
}
#endif
/* A move script just made a hitbox (ftAction_8007121C); `bone` is the
 * script's joint, or -1 for a common body part. Offsets of a borrowed move
 * are carried into the recipient's frame. */
void Rogue_HitboxCreated(Fighter* fp, HitCapsule* hit, int bone)
{
    PropHit* h = hit_record(hit);
    unsigned source, i;
    int prop, root;
    if (h) h->hit = NULL;
    if (!fp || !hit || !Rogue_IsAbilityState(fp)) return;
    source = Rogue_AbilitySourceKind(fp);
    /* Offsets and radii fit this fighter's body (Rogue_BorrowScale), as
     * the move's weapons and props are drawn: a hitbox stays on the part it
     * belongs to, and keeps its size relative to it (the hitbox size
     * command scales the same way, overrides/fixes/30-normals.toml). */
    {
        float s = Rogue_BorrowScale(fp);
        hit->b_offset.x *= s; hit->b_offset.y *= s; hit->b_offset.z *= s;
        /* The radius is in world units (the model scale sizes the skeleton,
         * not hitboxes): the shown size, else a move from a fighter with a
         * small model scale (Bowser, Pichu) has hitboxes too small for it.
         * A throw's hitboxes are aimed at the fighter being thrown, who
         * keeps their own size: never smaller than the donor's (Bowser's
         * down throw on Mario-sized fighters missed the victim entirely). */
        s = Rogue_HitboxScale(fp);
        if (fp->motion_id >= ftCo_MS_ThrowF && fp->motion_id <= ftCo_MS_ThrowLw && s < 1.0f) s = 1.0f;
        hit->scale *= s;
    }
    if (!prop_active(fp, source)) return;
    if (bone < 0) return;
    prop = prop_find(source, bone);
    if (prop < 0 && bone > 0 && (root = chain_find(source, bone)) >= 0 && fp->parts[0].joint &&
        hit->jobj == fp->parts[0].joint) {
        /* A joint of the donor's own skeleton posed from the root: follow
         * that pose every frame (Rogue_HitboxRefresh). */
        for (i = 0; i < sizeof(prop_hits) / sizeof(prop_hits[0]); ++i)
            if (!prop_hits[i].hit) break;
        if (i == sizeof(prop_hits) / sizeof(prop_hits[0])) return;
        h = &prop_hits[i];
        h->fighter = fp;
        h->hit = hit;
        h->offset = hit->b_offset;
        {
            float s = Rogue_BorrowScale(fp);
            h->offset.x /= s; h->offset.y /= s; h->offset.z /= s;
        }
        h->prop = (unsigned char) (PROP_CHAIN + root);
        h->kind = (unsigned char) source;
        hit_place(fp, h);
        return;
    }
    if (prop < 0) {
        /* A body bone: its retargeted frame is the donor's turned by C. */
        const FighterPartsTable* from = ftPartsTable[source];
        int part = (unsigned) bone < from->parts_num ? from->joint_to_part[bone] : FTPART_INVALID;
        Quat c;
        Mtx m;
        Quaternion cq;
        int own_part = part;
        if (part == FTPART_INVALID) return;
        if (rest_slot(part) >= ROGUE_REST_PARTS) {
            /* A donor finger (what Ness's bat and Game & Watch's jab hang
             * from): carried by the hand with the donor's finger pose, every
             * frame, as a prop is (hit_place). */
            int base = fold_base(fp, part), joint = base >= 0 ? own_joint(fp, base) : -1;
            if (joint >= 0 && fp->parts[joint].joint) {
                for (i = 0; i < sizeof(prop_hits) / sizeof(prop_hits[0]); ++i)
                    if (!prop_hits[i].hit) break;
                if (i < sizeof(prop_hits) / sizeof(prop_hits[0])) {
                    float bs = Rogue_BorrowScale(fp);
                    h = &prop_hits[i];
                    h->fighter = fp;
                    h->hit = hit;
                    h->offset = hit->b_offset;
                    h->offset.x /= bs; h->offset.y /= bs; h->offset.z /= bs;
                    h->prop = (unsigned char) (PROP_FINGER + part);
                    h->kind = (unsigned char) source;
                    hit->jobj = fp->parts[joint].joint;
                    hit_place(fp, h);
                    return;
                }
            }
        }
        if (own_joint(fp, part) < 0) {
            /* The recipient lacks the bone (most have no waist): the hitbox
             * rides the bone Rogue_AbilityMapBone chose instead, so carry
             * the offset from the donor's bone frame into that one's (an
             * offset in Jigglypuff's waist frame pointed up a tall
             * fighter's hip, over its head). */
            const FighterPartsTable* to = ftPartsTable[fp->kind];
            unsigned j;
            own_part = FTPART_INVALID;
            for (j = 0; j < to->parts_num; ++j)
                if (fp->parts[j].joint == hit->jobj) { own_part = to->joint_to_part[j]; break; }
            if (own_part == FTPART_INVALID) return;
        }
        {
            Quat d, r;
            int ds = rest_slot(part), rs = rest_slot(own_part);
            if (own_part == part) {
                /* A finger both have is not retargeted: it plays the donor's
                 * own rotation on the borrower's hand, so the offset needs
                 * the hand's correction (about right: fingers turn little
                 * against the hand). Other bones: their own. */
                int hand = rest_slot(part) >= ROGUE_REST_PARTS && source < ROGUE_REST_KINDS ?
                    rogue_part_parent[source][part] : part;
                ds = rs = hand != 0xFF ? body_slot(hand) : -1;
            }
            if (ds < 0 || rs < 0 || !rest_world(source, (unsigned) ds, &d) || !rest_world(fp->kind, (unsigned) rs, &r))
                return;
            c = q_mul(q_conj(d), r);
        }
        if (q_identity(c)) return;
        cq.x = -c.x; cq.y = -c.y; cq.z = -c.z; cq.w = c.w;
        PSMTXQuat(m, &cq);
        {
            Vec3 out;
            PSMTXMultVec(m, &hit->b_offset, &out);
            hit->b_offset = out;
        }
        return;
    }
    root = prop_root(fp, prop);
    if (root < 0 || hit->jobj != fp->parts[root].joint) return;
    for (i = 0; i < sizeof(prop_hits) / sizeof(prop_hits[0]); ++i)
        if (!prop_hits[i].hit) break;
    if (i == sizeof(prop_hits) / sizeof(prop_hits[0])) return;
    h = &prop_hits[i];
    h->fighter = fp;
    h->hit = hit;
    h->offset = hit->b_offset;
    {
        /* hit_place applies the borrow scale itself. */
        float s = Rogue_BorrowScale(fp);
        h->offset.x /= s; h->offset.y /= s; h->offset.z /= s;
    }
    h->prop = (unsigned char) prop;
    h->kind = (unsigned char) source;
    hit_place(fp, h);
#if BAM_DEBUG
    BAM_LOG("prop_hit donor=%u bone=%d root=%d off=(%.2f,%.2f,%.2f)\n", source, bone, root,
        hit->b_offset.x, hit->b_offset.y, hit->b_offset.z);
#endif
}
/* Every frame before a hitbox's position is taken (ftColl_8007AD18). */
void Rogue_HitboxRefresh(Fighter* fp, HitCapsule* hit)
{
    PropHit* h = hit_record(hit);
    if (!h) return;
    if (h->fighter != fp || hit->state == HitCapsule_Disabled || !prop_active(fp, h->kind)) {
        h->hit = NULL;
        return;
    }
    hit_place(fp, h);
}

/* World matrix for a borrowed weapon: the item model (Beam Sword or Hammer)
 * laid along the donor's weapon bone, gripped where the donor holds it, the
 * hammer sized so its head lands on the donor's farthest hitbox. Returns the
 * item (0 sword, 1 hammer), or -1 when the current move has no weapon. */
int Rogue_PropWeaponMtx(Fighter* fp, Mtx out)
{
    /* Item models: where the hand goes on the long (+Y) axis, and the
     * distance from there to the tip (sword) or head centre (hammer). */
    static const float hold[3] = { -3.5f, -7.0f, 0.0f }, length[3] = { 11.3f, 12.0f, 1.0f };
    unsigned source, i;
    int root, item;
    float scale;
    Mtx rel, align, model;
    Quaternion q;
    if (!fp || !Rogue_IsAbilityState(fp)) return -1;
    source = Rogue_AbilitySourceKind(fp);
    if (!prop_active(fp, source)) return -1;
    for (i = 0; i < ROGUE_WEAPON_COUNT; ++i)
        if (rogue_weapon[i].kind == source && (unsigned) fp->anim_id >= rogue_weapon[i].first &&
            (unsigned) fp->anim_id <= rogue_weapon[i].last) break;
    if (i == ROGUE_WEAPON_COUNT) return -1;
    item = rogue_weapon[i].item > 2 ? 0 : rogue_weapon[i].item;
    root = prop_root(fp, rogue_weapon[i].prop);
    if (root < 0 || !prop_relative(fp, rogue_weapon[i].prop, rel)) return -1;
    /* At the donor's size (Rogue_BorrowScale), about the hand it hangs from. */
    {
        float bs = Rogue_BorrowScale(fp);
        Mtx grow;
        PSMTXScale(grow, bs, bs, bs);
        PSMTXConcat(grow, rel, rel);
    }
    if (item == 2) {
        /* An article the donor holds (Peach's parasol): its own model sits
         * exactly on the donor's bone, as the game's attachment does. */
        int c, r;
        PSMTXConcat(HSD_JObjGetMtxPtr(fp->parts[root].joint), rel, out);
        /* Articles keep their own size: only the bone's position and turn. */
        for (c = 0; c < 3; ++c) {
            float n = sqrtf(out[0][c] * out[0][c] + out[1][c] * out[1][c] + out[2][c] * out[2][c]);
            if (n > 1e-6f)
                for (r = 0; r < 3; ++r) out[r][c] /= n;
        }
        return item;
    }
    q.x = rogue_weapon[i].align[0] * (1.0f / 32767.0f); q.y = rogue_weapon[i].align[1] * (1.0f / 32767.0f);
    q.z = rogue_weapon[i].align[2] * (1.0f / 32767.0f); q.w = rogue_weapon[i].align[3] * (1.0f / 32767.0f);
    PSMTXQuat(align, &q);
    align[0][3] = rogue_weapon[i].grip[0] * (1.0f / 256.0f);
    align[1][3] = rogue_weapon[i].grip[1] * (1.0f / 256.0f);
    align[2][3] = rogue_weapon[i].grip[2] * (1.0f / 256.0f);
    /* The Beam Sword keeps its own size; the Hammer item is a giant mallet,
     * so it is scaled to the donor's reach. */
    scale = item == 1 ? rogue_weapon[i].reach * (1.0f / 256.0f) / length[1] : 1.0f;
    if (scale < 0.1f) scale = 0.1f;
    PSMTXScale(model, scale, scale, scale);
    model[1][3] = -hold[item] * scale;
    PSMTXConcat(align, model, align);
    PSMTXConcat(rel, align, rel);
    PSMTXConcat(HSD_JObjGetMtxPtr(fp->parts[root].joint), rel, out);
    return item;
}

/* A whole-body animation starts (full) or the match resets (fp NULL). */
static void prop_reset(const Fighter* fp)
{
    int slot;
    if (!fp) {
        memset(prop_track_count, 0, sizeof(prop_track_count));
        memset(prop_hits, 0, sizeof(prop_hits));
        memset(anchors, 0, sizeof(anchors));
        memset(anchor_next, 0, sizeof(anchor_next));
        memset(last_prop, 0, sizeof(last_prop));
        return;
    }
    slot = slot_of_fighter(fp);
    if (slot >= 0) prop_track_count[slot] = 0;
}

/* ---- Per-frame world-space pass ----
 *
 * The per-bone correction above assumes both skeletons have the same chain
 * of bones. They often do not: Mario has no BustN, Kirby's arm hangs from a
 * bone that is a finger on Marth. After every animation step each body bone
 * the donor also has is turned so its world orientation is the donor's
 * animated one carried over by C (Wr = Wd * C), whatever the recipient's own
 * parents are doing; the recipient's extra bones keep their pose. The
 * donor's world comes from its local values (as Melee wrote them, or from
 * its captured tracks for parts the recipient lacks) up its own hierarchy
 * (rogue_part_parent). TopN and TransN (facing, travel) are left alone. */
static Quat jobj_local(HSD_JObj* jobj)
{
    float e[3];
    if (jobj->flags & JOBJ_USE_QUATERNION) {
        Quat q;
        q.x = jobj->rotate.x; q.y = jobj->rotate.y; q.z = jobj->rotate.z; q.w = jobj->rotate.w;
        return q;
    }
    e[0] = jobj->rotate.x; e[1] = jobj->rotate.y; e[2] = jobj->rotate.z;
    return q_euler(e);
}
typedef struct DonorPose {
    Quat world[ROGUE_PART_COUNT];
    unsigned char done[ROGUE_PART_COUNT];
} DonorPose;
static Quat donor_world(Fighter* fp, int slot, unsigned source, int part, DonorPose* d, int depth)
{
    Quat identity = { 0.0f, 0.0f, 0.0f, 1.0f }, local, up_world;
    int up, joint;
    if (part <= 1 || part >= ROGUE_PART_COUNT || depth > 16) return identity;
    if (d->done[part]) return d->world[part];
    up = rogue_part_parent[source][part];
    up_world = up == 0xFF ? identity : donor_world(fp, slot, source, up, d, depth + 1);
    joint = own_joint(fp, part);
    if (joint >= 0) {
        RotFix* e = rotfix_find(fp->parts[joint].joint);
        local = q_mul(mid_quat(source, part), e ? q_euler(e->donor) : jobj_local(fp->parts[joint].joint));
    } else {
        local = part_local(fp, slot, source, part, 0);
    }
    d->world[part] = q_mul(up_world, local);
    d->done[part] = 1;
    return d->world[part];
}
static void anchors_follow(Fighter* fp);
static void pose_pass(Fighter* fp);
/* After the fighter's animation step (ftAnim_8006EBA4). */
void Rogue_AnimPostStep(Fighter* fp)
{
    if (!bam_anim_scale) return;
    pose_pass(fp);
    anchors_follow(fp);
}
static void pose_pass(Fighter* fp)
{
    /* On the stack (about 4 KB): static storage is short. */
    Quat act[POSE_JOINTS];
    DonorPose d;
    Quat identity = { 0.0f, 0.0f, 0.0f, 1.0f };
    const FighterPartsTable* own;
    int slot = slot_of_fighter(fp);
    unsigned n, j, source;
    if (slot < 0 || !pose_on[slot] || fp->x8A4_animBlendFrames != 0.0f) return;
    source = fp->x597_bits;
    if (!prop_active(fp, source) || source >= ROGUE_REST_KINDS || fp->kind >= ROGUE_REST_KINDS) return;
    if (rogue_part_parent[source][4] == 0xFF) return; /* No hierarchy data. */
    own = ftPartsTable[fp->kind];
    n = own->parts_num < POSE_JOINTS ? own->parts_num : POSE_JOINTS;
    memset(d.done, 0, sizeof(d.done));
    for (j = 0; j < n; ++j) {
        HSD_JObj* jobj = fp->parts[j].joint;
        unsigned pj = joint_parent[slot][j];
        int part = own->joint_to_part[j];
        Quat parent = pj < j ? act[pj] : identity;
        RotFix* e;
        if (!jobj) { act[j] = parent; continue; }
        if (part <= 1 || part == FTPART_INVALID) {
            act[j] = part <= 1 ? identity : q_mul(parent, jobj_local(jobj));
            continue;
        }
        e = rotfix_find(jobj);
        /* Joints the game drives itself (not animated) keep their value. */
        if (e && e->kind == source && !fp->parts[j].flags_b0 && !fp->parts[j].flags_b5) {
            float out[3];
            Quat want = q_mul(donor_world(fp, slot, source, part, &d, 0),
                correction(source, fp->kind, body_slot(part)));
            q_to_euler(q_mul(q_conj(parent), want), out);
            HSD_JObjSetRotationX(jobj, out[0]);
            HSD_JObjSetRotationY(jobj, out[1]);
            HSD_JObjSetRotationZ(jobj, out[2]);
            act[j] = want;
            continue;
        }
        act[j] = q_mul(parent, jobj_local(jobj));
    }
}
static void anchors_follow(Fighter* fp)
{
    int slot = slot_of_fighter(fp);
    unsigned i;
    if (slot < 0) return;
    for (i = 0; i < ANCHORS; ++i)
        if (anchors[slot][i].jobj) anchor_place(fp, &anchors[slot][i]);
}

/* ---- The donor's own meshes (sword_visual.c draws them) ----
 *
 * Tables from anim_rest.inc: which of the donor's meshes belong to a rebuilt
 * bone chain (Marth's sword, Mewtwo's tail), and in which donor motions each
 * chain shows. The donor's model is posed on the recipient: a rebuilt bone
 * where the rebuilt bone is, a body bone the recipient also has on the
 * recipient's bone (turned back by C so the donor's skin fits it), anything
 * else at its rest pose from its parent. */
unsigned Rogue_DonorMeshShow(Fighter* fp, unsigned char item_of_group[8])
{
    unsigned source, mask = 0, i;
    if (!fp || !Rogue_IsAbilityState(fp)) return 0;
    source = Rogue_AbilitySourceKind(fp);
    if (!prop_active(fp, source)) return 0;
    for (i = 0; i < ROGUE_MESH_SHOW_COUNT; ++i) {
        const RogueMeshShow* r = &rogue_mesh_show[i];
        if (r->kind != source || r->group >= 8 || (unsigned) fp->anim_id < r->first ||
            (unsigned) fp->anim_id > r->last) continue;
        mask |= 1U << r->group;
        item_of_group[r->group] = r->item;
    }
    return mask;
}
/* The donor's mesh rows: up to `max` (group, DObj index) pairs. */
unsigned Rogue_DonorMeshes(unsigned kind, unsigned char* groups, unsigned short* dobjs, unsigned short* pobjs,
                           unsigned max)
{
    unsigned i, n = 0;
    for (i = 0; i < ROGUE_MESH_COUNT && n < max; ++i)
        if (rogue_mesh[i].kind == kind) {
            groups[n] = rogue_mesh[i].group;
            if (pobjs) pobjs[n] = rogue_mesh[i].pobjs;
            dobjs[n++] = rogue_mesh[i].dobj;
        }
    return n;
}
/* World matrices for the donor model's joints (parents before children). */
/* Whether donor joint `joint` is collapsed while mesh groups `shown` show. */
static bool mesh_hidden(unsigned kind, unsigned joint, unsigned shown)
{
    unsigned i;
    for (i = 0; i < ROGUE_MESH_HIDE_COUNT; ++i)
        if (rogue_mesh_hide[i].kind == kind && rogue_mesh_hide[i].joint == joint && rogue_mesh_hide[i].group < 32 &&
            (shown & (1U << rogue_mesh_hide[i].group)))
            return true;
    return false;
}
/* Clothing the borrower wears instead of carries: Peach's dress (her skirt
 * bones under joint 17, mesh group 2) on her down smash. A carried part
 * keeps the move's size and hangs where the donor's body would be; a dress
 * has to fit the borrower, so it is stretched in the fighter's own axes:
 *   - its waist at the borrower's waist (their hip, or for round fighters
 *     whose hip is at the bottom, 45% of their torso's height) and its hem
 *     on the floor: the height scale;
 *   - wide enough for their torso (hurtbox girth against Peach's) with some
 *     room: the width scale;
 *   - centred between their legs (Donkey Kong's hip joint is at his back).
 * Girth and torso top: the torso hurtboxes (TransN..WaistN, BustN) in the
 * rest pose, distance from the body's vertical axis plus radius, and their
 * highest point; skeleton units, from each fighter's data (0: none listed). */
static const float wear_girth[] = {
    2.23f, 3.04f, 2.92f, 5.46f, 5.60f, 11.03f, 2.25f, 1.81f, 3.10f, 2.74f, 4.00f, 4.00f, 4.75f, 2.77f,
    4.60f, 5.60f, 0.00f, 2.43f, 2.37f, 1.70f, 2.40f, 2.63f, 2.14f, 4.50f, 0.00f, 3.16f, 2.37f,
};
static const float wear_top[] = {
    9.17f, 10.34f, 15.05f, 16.25f, 10.75f, 28.80f, 9.85f, 8.94f, 8.50f, 9.80f, 7.57f, 7.57f, 11.34f, 15.10f,
    8.60f, 10.85f, 0.00f, 9.37f, 10.00f, 9.40f, 9.85f, 9.57f, 10.34f, 8.99f, 0.00f, 15.29f, 10.00f,
};
#define WEAR_KINDS (sizeof(wear_girth) / sizeof(wear_girth[0]))
typedef struct Wear { unsigned char kind, group, root, hip; } Wear;
static const Wear wears[] = { { Ft_Kind_Peach, 2, 17, 4 } };
static const Wear* wear_of(unsigned source, unsigned shown)
{
    unsigned i;
    for (i = 0; i < sizeof(wears) / sizeof(wears[0]); ++i)
        if (wears[i].kind == source && (shown & (1U << wears[i].group))) return &wears[i];
    return NULL;
}
/* The wearer's frame for the donor's hip: the borrower's hip turned onto
 * the donor's (as body parts are), stretched to fit, its waist placed
 * between the legs at the wearer's waist height. 0 if no hip. */
static int wear_base(Fighter* fp, unsigned source, Mtx out)
{
    int hip = own_joint(fp, FtPart_HipN), l = own_joint(fp, FtPart_LLegJA), r = own_joint(fp, FtPart_RLegJA);
    float own_hip, waist, sy, sw, k, lift;
    Quat c;
    Quaternion q;
    Mtx turn, fit;
    if (hip < 0 || fp->kind >= BODY_KINDS || source >= BODY_KINDS || fp->kind >= WEAR_KINDS || source >= WEAR_KINDS)
        return 0;
    own_hip = body_rest[fp->kind].hip;
    waist = 0.45f * wear_top[fp->kind];
    if (waist < own_hip) waist = own_hip;
    sy = waist / body_rest[source].hip;
    if (sy < 0.35f) sy = 0.35f;
    if (sy > 2.5f) sy = 2.5f;
    sw = sy;
    if (wear_girth[fp->kind] > 0.0f && wear_girth[source] > 0.0f) {
        k = wear_girth[fp->kind] / wear_girth[source];
        if (sw < k) sw = k;
    }
    sw *= 1.2f; /* room to wear it */
    if (sw > 3.0f) sw = 3.0f;
    lift = waist - own_hip;
    c = correction(source, fp->kind, body_slot(FtPart_HipN));
    q.x = -c.x; q.y = -c.y; q.z = -c.z; q.w = c.w;
    PSMTXQuat(turn, &q);
    PSMTXConcat(HSD_JObjGetMtxPtr(fp->parts[hip].joint), turn, out);
    {
        /* Stretch in the fighter's axes (up, and both horizontals: facing
         * turns about up, so the dress stays round whichever way). */
        float tx = out[0][3], ty = out[1][3], tz = out[2][3];
        out[0][3] = out[1][3] = out[2][3] = 0.0f;
        PSMTXScale(fit, sw, sy, sw);
        PSMTXConcat(fit, out, out);
        out[0][3] = tx; out[1][3] = ty; out[2][3] = tz;
    }
    if (l >= 0 && r >= 0) {
        MtxPtr a = HSD_JObjGetMtxPtr(fp->parts[l].joint), b = HSD_JObjGetMtxPtr(fp->parts[r].joint);
        out[0][3] = (a[0][3] + b[0][3]) * 0.5f;
        out[2][3] = (a[2][3] + b[2][3]) * 0.5f;
    }
    /* Skeleton units to world: the fighter's model scale. */
    out[1][3] += lift * fp->x34_scale.y;
    return 1;
}

void Rogue_DonorPose(Fighter* fp, HSD_JObj* const* jobjs, const unsigned char* parents, unsigned n, int free_joint,
                     unsigned shown)
{
    unsigned source = fp->x597_bits, d;
    const FighterPartsTable* from;
    float bs = Rogue_BorrowScale(fp);
    Mtx grow, worn;
    const Wear* wear = wear_of(source, shown);
    if (source >= ROGUE_REST_KINDS) return;
    from = ftPartsTable[source];
    PSMTXScale(grow, bs, bs, bs);
    if (wear && !wear_base(fp, source, worn)) wear = NULL;
    for (d = 0; d < n; ++d) {
        HSD_JObj* j = jobjs[d];
        int prop = prop_find(source, (int) d), part, own;
        /* Each joint's world matrix is kept in the joint itself (no table:
         * static storage is short); parents are placed first. */
        HSD_JObj* up = parents[d] < d ? jobjs[parents[d]] : NULL;
        Mtx rel, w;
        int row = chain_find(source, (int) d);
        if (!j) continue;
        if (wear && (d == wear->hip || (prop >= 0 && rogue_prop[prop].joint == wear->root))) {
            /* Worn: the donor's hip and the root of the clothing's bones
             * on the wearer's frame (the hip's correction is in it). */
            if (d == wear->hip || !prop_relative(fp, prop, rel)) PSMTXCopy(worn, w);
            else {
                Quat c = correction(source, fp->kind, body_slot(FtPart_HipN));
                Quaternion q;
                Mtx undo;
                /* prop_relative starts with the hip's correction too. */
                q.x = c.x; q.y = c.y; q.z = c.z; q.w = c.w;
                PSMTXQuat(undo, &q);
                PSMTXConcat(undo, rel, rel);
                PSMTXConcat(worn, rel, w);
            }
        } else if (row >= 0 && (rogue_chain[row].parent == 255 || up)) {
            /* Through the donor's own skeleton from the root. */
            if (rogue_chain[row].parent == 255) chain_base(fp, w);
            else {
                chain_local(fp, slot_of_fighter(fp), row, rel);
                PSMTXConcat(up->mtx, rel, w);
            }
        } else if (prop >= 0 && rogue_prop[prop].parent != 255 && parents[d] < d &&
            rogue_prop[rogue_prop[prop].parent].joint == parents[d] && up) {
            /* Further down a rebuilt chain: from its parent, already placed. */
            prop_local(fp, slot_of_fighter(fp), prop, rel);
            PSMTXConcat(up->mtx, rel, w);
        } else if (prop >= 0 && (own = prop_root(fp, prop)) >= 0 && prop_relative(fp, prop, rel)) {
            PSMTXConcat(grow, rel, rel);
            PSMTXConcat(HSD_JObjGetMtxPtr(fp->parts[own].joint), rel, w);
        } else if ((int) d != free_joint && d < from->parts_num && (part = from->joint_to_part[d]) != FTPART_INVALID &&
                   (own = own_joint(fp, part)) >= 0) {
            Quat c = correction(source, fp->kind, body_slot(part));
            Quaternion q;
            Mtx turn;
            q.x = -c.x; q.y = -c.y; q.z = -c.z; q.w = c.w;
            PSMTXQuat(turn, &q);
            PSMTXConcat(turn, grow, turn);
            PSMTXConcat(HSD_JObjGetMtxPtr(fp->parts[own].joint), turn, w);
        } else {
            Mtx local;
            if (j->flags & JOBJ_USE_QUATERNION) HSD_MtxSRTQuat(local, &j->scale, &j->rotate, &j->translate, NULL);
            else HSD_MtxSRT(local, &j->scale, (Vec3*) &j->rotate, &j->translate, NULL);
            if (up) PSMTXConcat(up->mtx, local, w);
            else PSMTXCopy(local, w);
        }
        if (shown && mesh_hidden(source, d, shown)) {
            /* Bits of a shown mesh that belong elsewhere (Yoshi's mouth
             * around his tongue): drawn at no size. */
            int r, c;
            for (r = 0; r < 3; ++r)
                for (c = 0; c < 3; ++c) w[r][c] *= 0.001f;
        }
        HSD_JObjCopyMtx(j, w);
        j->flags |= JOBJ_USER_DEF_MTX | JOBJ_MTX_INDEP_PARENT | JOBJ_MTX_INDEP_SRT;
    }
}

#include <sysdolphin/baselib/memory.h>
/* Called from Bam_MatchBegin / Bam_MatchEnd (bam_fighter.c). */
void Rogue_AnimScaleMatchBegin(void)
{
    bam_anim_scale = HSD_MemAlloc(sizeof(*bam_anim_scale));
    memset(bam_anim_scale, 0, sizeof(*bam_anim_scale));
}
void Rogue_AnimScaleMatchEnd(void)
{
    bam_anim_scale = NULL;
}
