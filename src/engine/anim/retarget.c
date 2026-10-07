/* Retargeting: a donor animation played on the borrower's skeleton, each
 * body part turned by the difference of the two rest poses.
 *
 * Called from: the animation fixes in overrides/fixes/10-core.toml (rotation, translation),
 * borrow.c (starting a borrowed move).
 * State: bam_anim (match block, anim/internal.h).
 */
#include <engine/anim/internal.h>

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
const BodyRest body_rest[BODY_KINDS] = {
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

/* Three body bones, each on the live and the blend skeleton, per build fighter. */



static void forget(const Fighter* fp)
{
    unsigned i = 0;
    while (i < bam_anim->scaled_count)
        if (bam_anim->scaled[i].fighter == fp) bam_anim->scaled[i] = bam_anim->scaled[--bam_anim->scaled_count];
        else ++i;
}

/* ---- Rotation retargeting (world orientation per bone) ----
 *
 * Melee copies each borrowed bone rotation as is, but fighters' bones rest
 * in different orientations, so the same values bend limbs and tilt bodies
 * differently. Amalgam Fox avoids this by re-making every borrowed animation
 * on Fox's skeleton ahead of time; here the same correction is applied live.
 *
 * With D the donor's and R the borrower's rest world rotation of a body
 * part, the borrower bone is turned so its world orientation is the donor
 * bone's animated world orientation carried over by C = D^-1 * R:
 *     borrower local = C(parent)^-1 * donor local * C(bone)
 * At rest this gives exactly the borrower's own rest pose. Rest rotations
 * come from the user's disc at build time (bone_tables.h). Bones whose
 * correction is the identity, and finger bones, keep the raw copy. */




#pragma push
#pragma dont_inline on
int rest_world(unsigned kind, unsigned i, Quat* q)
{
    const short* v;
    if (kind >= BAM_REST_KINDS || i >= BAM_REST_PARTS + BAM_REST_EXTRA) return 0;
    v = i < BAM_REST_PARTS ? bam_rest_world[kind][i] : bam_rest_extra[kind][i - BAM_REST_PARTS];
    if (!v[0] && !v[1] && !v[2] && !v[3]) return 0;
    q->x = v[0] * (1.0f / 32767.0f); q->y = v[1] * (1.0f / 32767.0f);
    q->z = v[2] * (1.0f / 32767.0f); q->w = v[3] * (1.0f / 32767.0f);
    return 1;
}
#pragma pop
/* Rest rotation of the donor's body-less joints between a part and its
 * parent part (Kirby's arm hangs from two of them); none for most. */
Quat mid_quat(unsigned kind, int part)
{
    Quat q = { 0.0f, 0.0f, 0.0f, 1.0f };
    unsigned i;
    for (i = 0; i < BAM_PART_MID_COUNT; ++i)
        if (bam_part_mid[i].kind == kind && bam_part_mid[i].part == part) {
            q.x = bam_part_mid[i].q[0] * (1.0f / 32767.0f); q.y = bam_part_mid[i].q[1] * (1.0f / 32767.0f);
            q.z = bam_part_mid[i].q[2] * (1.0f / 32767.0f); q.w = bam_part_mid[i].q[3] * (1.0f / 32767.0f);
            break;
        }
    return q;
}
static unsigned rotfix_slot(HSD_JObj* jobj) { return ((unsigned) jobj >> 4) & (ROTFIX_HASH - 1); }
static void rotfix_rehash(void)
{
    unsigned i;
    memset(bam_anim->rotfix_hash, 0, sizeof(bam_anim->rotfix_hash));
    for (i = 0; i < sizeof(bam_anim->rotfix) / sizeof(bam_anim->rotfix[0]); ++i) {
        unsigned h;
        if (!bam_anim->rotfix[i].jobj) continue;
        for (h = rotfix_slot(bam_anim->rotfix[i].jobj); bam_anim->rotfix_hash[h]; h = (h + 1) & (ROTFIX_HASH - 1)) {}
        bam_anim->rotfix_hash[h] = (unsigned short) (i + 1);
    }
}
RotFix* rotfix_find(HSD_JObj* jobj)
{
    unsigned h;
    for (h = rotfix_slot(jobj); bam_anim->rotfix_hash[h]; h = (h + 1) & (ROTFIX_HASH - 1))
        if (bam_anim->rotfix[bam_anim->rotfix_hash[h] - 1].jobj == jobj) return &bam_anim->rotfix[bam_anim->rotfix_hash[h] - 1];
    return NULL;
}
/* The borrower's own joint for a body part, or -1. Not ftParts_GetBoneIndex:
 * during a borrowed move that falls back to the nearest part the fighter
 * has, which would put one part's correction on another part's bone. */
int part_joint_raw(Fighter* fp, int part)
{
    const FighterPartsTable* table = ftPartsTable[fp->kind];
    int joint;
    if (part < 0 || part >= BAM_PART_COUNT) return -1;
    joint = table->part_to_joint[part];
    if (joint == FTPART_INVALID || (unsigned) joint >= table->parts_num || !fp->parts[joint].joint) return -1;
    return joint;
}
/* The retargeted-part index (into bam_rest_part) of a borrower joint. */
static int rest_index_of(Fighter* fp, HSD_JObj* jobj)
{
    unsigned i;
    if (!jobj) return -1;
    for (i = 0; i < BAM_REST_PARTS; ++i) {
        int joint = part_joint_raw(fp, bam_rest_part[i]);
        if (joint >= 0 && fp->parts[joint].joint == jobj) return (int) i;
    }
    return -1;
}
/* Correction of part i: C = D^-1 * R, identity when either rest is unknown. */
Quat correction(unsigned donor, unsigned own, int i)
{
    Quat d, r, c = { 0.0f, 0.0f, 0.0f, 1.0f };
    if (i >= 0 && rest_world(donor, (unsigned) i, &d) && rest_world(own, (unsigned) i, &r))
        c = q_mul(q_conj(d), r);
    return c;
}
/* Borrower joint index of each joint's parent (0xFF: root), per fighter. */


static void build_parents(Fighter* fp, unsigned slot)
{
    unsigned n = ftPartsTable[fp->kind]->parts_num, i, k;
    if (n > POSE_JOINTS) n = POSE_JOINTS;
    for (i = 0; i < n; ++i) {
        HSD_JObj* parent = fp->parts[i].joint ? HSD_JObjGetParent(fp->parts[i].joint) : NULL;
        bam_anim->joint_parent[slot][i] = 0xFF;
        for (k = 0; parent && k < i; ++k)
            if (fp->parts[k].joint == parent) { bam_anim->joint_parent[slot][i] = (unsigned char) k; break; }
    }
}
static void rotate_retarget(Fighter* fp, unsigned slot, unsigned donor_kind, int first_part, int borrowed)
{
    unsigned i, k;
    RotFix* block = &bam_anim->rotfix[slot * ROTFIX_PER_FIGHTER];
    unsigned char* parts = &bam_anim->rotfix_part[slot * ROTFIX_PER_FIGHTER];
    /* Entries from the part this animation starts at are replaced. */
    for (i = 0; i < ROTFIX_PER_FIGHTER; ++i)
        if (block[i].jobj && parts[i] >= (unsigned) first_part) block[i].jobj = NULL;
    if (first_part == 0) bam_anim->pose_on[slot] = 0;
    if (borrowed) {
        if (first_part == 0) {
            build_parents(fp, slot);
            bam_anim->pose_on[slot] = 1;
        }
        for (i = 0; i < BAM_REST_PARTS; ++i) {
            int part = bam_rest_part[i], joint, parent;
            unsigned char fold[ROTFIX_FOLDS], nfold;
            Quat cb, cp;
            Quat dp, db, rest;
            HSD_JObj* jobjs[2];
            if (part < first_part) continue;
            joint = part_joint_raw(fp, part);
            if (joint < 0) continue;
            parent = rest_index_of(fp, HSD_JObjGetParent(fp->parts[joint].joint));
            cb = correction(donor_kind, fp->kind, (int) i);
            cp = correction(donor_kind, fp->kind, parent);
            /* Donor ancestors (by body part) the borrower does not have. */
            {
                int up = bam_part_parent[donor_kind][part], chain[ROTFIX_FOLDS], n = 0, ok = 1;
                while (up != 0xFF && n < ROTFIX_FOLDS) {
                    if (part_joint_raw(fp, up) >= 0) break;
                    chain[n++] = up;
                    up = bam_part_parent[donor_kind][up];
                }
                if (up != 0xFF && n == ROTFIX_FOLDS) ok = 0;
                nfold = 0;
                while (ok && n > 0) fold[nfold++] = (unsigned char) chain[--n];
            }
            /* Every body bone is registered: the per-frame pass needs the
             * donor's own values for it (Bam_AnimPostStep). */
            /* Until a track writes it, the bone holds the donor's rest pose. */
            if (!rest_world(donor_kind, i, &db)) continue;
            rest = parent >= 0 && rest_world(donor_kind, (unsigned) parent, &dp) ? q_mul(q_conj(dp), db) : db;
            jobjs[0] = fp->parts[joint].joint;
            jobjs[1] = fp->parts[joint].x4_jobj2;
            for (k = 0; k < 2; ++k) {
                RotFix* e = &block[i * 2 + k];
                if (!jobjs[k] || (k && jobjs[1] == jobjs[0]) || (jobjs[k]->flags & JOBJ_USE_QUATERNION)) continue;
                e->jobj = jobjs[k];
                e->a = q_conj(cp);
                e->b = cb;
                q_to_euler(q_mul(q_conj(mid_quat(donor_kind, part)), rest), e->donor);
                e->slot = (unsigned char) slot;
                e->kind = (unsigned char) donor_kind;
                e->nfold = nfold;
                memcpy(e->fold, fold, sizeof(e->fold));
                parts[i * 2 + k] = (unsigned char) part;
            }
        }
    }
    rotfix_rehash();
}
static void rotfix_apply(RotFix* e)
{
    float out[3];
    Quat local = q_mul(mid_quat(e->kind, bam_anim->rotfix_part[e - bam_anim->rotfix]), q_euler(e->donor));
    if (e->nfold) local = q_mul(fold_quat(e), local);
    q_to_euler(q_mul(q_mul(e->a, local), e->b), out);
    HSD_JObjSetRotationX(e->jobj, out[0]);
    HSD_JObjSetRotationY(e->jobj, out[1]);
    HSD_JObjSetRotationZ(e->jobj, out[2]);
}
/* Animated rotation (jobj.c ROTX/ROTY/ROTZ). */
void Bam_AnimRotate(HSD_JObj* jobj, int axis, float value)
{
    /* Every animated joint goes through here, menus included, and the
     * per-match state only exists during a match. */
    RotFix* e = bam_anim ? rotfix_find(jobj) : NULL;
    if (!e) {
        if (axis == 0) HSD_JObjSetRotationX(jobj, value);
        else if (axis == 1) HSD_JObjSetRotationY(jobj, value);
        else HSD_JObjSetRotationZ(jobj, value);
        return;
    }
    e->donor[axis] = value;
    rotfix_apply(e);
}

void Bam_AnimRetarget(Fighter* fp, unsigned donor_kind, int first_part)
{
    static const int parts[3] = { FtPart_XRotN, FtPart_YRotN, FtPart_HipN };
    BamFighterState* S;
    const BodyRest *own, *donor;
    float ratio;
    unsigned i, k;
    if (!fp || !bam_anim) return;
    if (first_part == 0) prop_reset(fp);
    S = Bam_FighterCtx(fp);
    if (S && S->fighter == fp) {
        unsigned slot = (unsigned) (S - bam_match->fighters);
        int borrowed = donor_kind != fp->kind && (S->active || S->aerial || S->normal_on) &&
            donor_kind < BAM_REST_KINDS && fp->kind < BAM_REST_KINDS;
        if (slot < BAM_FIGHTERS) rotate_retarget(fp, slot, donor_kind, first_part, borrowed);
    }
    /* A partial animation that starts below the body bones leaves them alone. */
    if (first_part > FtPart_HipN) return;
    forget(fp);
    S = Bam_FighterCtx(fp);
    if (donor_kind == fp->kind || S->fighter != fp || (!S->active && !S->aerial && !S->normal_on) ||
        donor_kind >= BODY_KINDS || fp->kind >= BODY_KINDS) return;
    own = &body_rest[fp->kind];
    donor = &body_rest[donor_kind];
    if (own->hip <= 0.0f || donor->hip <= 0.0f) return;
    ratio = own->hip / donor->hip;
    /* Within the borrow scale's range (Bam_BorrowScale): Jigglypuff's and
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
            if (!jobjs[k] || (k && jobjs[1] == jobjs[0]) || bam_anim->scaled_count >= sizeof(bam_anim->scaled) / sizeof(bam_anim->scaled[0])) continue;
            s = &bam_anim->scaled[bam_anim->scaled_count++];
            s->fighter = fp;
            s->jobj = jobjs[k];
            memcpy(s->own, own->pos[i], sizeof(s->own));
            memcpy(s->donor, donor->pos[i], sizeof(s->donor));
            s->ratio = ratio;
            s->own_hip = own->hip;
            s->donor_hip = donor->hip;
            s->size = Bam_BorrowScale(fp);
            s->lift = 1;
        }
    }
    {
        /* TransN2 carries what a move throws out from the body (Ness's
         * yo-yo in his up and down smash): the donor's positions, at the
         * move's size. */
        int joint = part_joint_raw(fp, FtPart_TransN2);
        if (joint != FTPART_INVALID && joint >= 0 && fp->parts[joint].joint &&
            bam_anim->scaled_count < sizeof(bam_anim->scaled) / sizeof(bam_anim->scaled[0])) {
            Scaled* s = &bam_anim->scaled[bam_anim->scaled_count++];
            memset(s, 0, sizeof(*s));
            s->fighter = fp;
            s->jobj = fp->parts[joint].joint;
            s->ratio = Bam_BorrowScale(fp);
        }
    }
}

float Bam_AnimTranslate(HSD_JObj* jobj, int axis, float value)
{
    unsigned i;
    if (!bam_anim) return value; /* outside a match (menus) */
    for (i = 0; i < bam_anim->scaled_count; ++i)
        if (bam_anim->scaled[i].jobj == jobj) {
            const Scaled* e = &bam_anim->scaled[i];
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

void Bam_AnimScaleReset(void)
{
    if (!bam_anim) return;
    bam_anim->scaled_count = 0;
    memset(bam_anim->rotfix, 0, sizeof(bam_anim->rotfix));
    rotfix_rehash();
    prop_reset(NULL);
}

