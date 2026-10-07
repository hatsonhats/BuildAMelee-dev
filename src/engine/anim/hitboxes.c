/* Hitboxes of a borrowed move: sized for the borrower and kept on the donor
 * bone they belong to.
 *
 * Called from: fixes in overrides/fixes/10-core.toml and 30-normals.toml (hitbox created,
 * every frame before its position is taken).
 * State: bam_anim->prop_hits (match block, anim/internal.h).
 */
#include <engine/anim/internal.h>

static PropHit* hit_record(const HitCapsule* hit)
{
    unsigned i;
    if (!bam_anim) return NULL;
    for (i = 0; i < sizeof(bam_anim->prop_hits) / sizeof(bam_anim->prop_hits[0]); ++i)
        if (bam_anim->prop_hits[i].hit == hit) return &bam_anim->prop_hits[i];
    return NULL;
}

/* Hitboxes on a joint posed through the donor's own skeleton (bam_chain,
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
    float s = Bam_BorrowScale(fp);
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
int Bam_QAHitAnchor(Fighter* fp, const HitCapsule* hit, Vec3* pos)
{
    PropHit* h = hit_record(hit);
    Mtx rel, grow, w;
    int root;
    float bs;
    int from;
    if (!fp || !h || h->fighter != fp || (h->prop >= PROP_CHAIN && h->prop < PROP_FINGER)) return -1;
    from = h->prop >= PROP_FINGER ? h->prop - PROP_FINGER : bam_prop[h->prop].part;
    root = fold_base(fp, from);
    root = root >= 0 ? own_joint(fp, root) : -1;
    if (root < 0 || !fp->parts[root].joint) return -1;
    part_fold(fp, from, rel);
    bs = Bam_BorrowScale(fp);
    PSMTXScale(grow, bs, bs, bs);
    PSMTXConcat(grow, rel, rel);
    PSMTXConcat(HSD_JObjGetMtxPtr(fp->parts[root].joint), rel, w);
    pos->x = w[0][3]; pos->y = w[1][3]; pos->z = w[2][3];
    return from;
}
#endif

/* Offsets and radii fit this fighter's body (Bam_BorrowScale), as the
 * move's weapons and props are drawn: a hitbox stays on the part it belongs
 * to, and keeps its size relative to it (the hitbox size command scales the
 * same way, overrides/fixes/30-normals.toml). */
static void scale_hitbox(Fighter* fp, HitCapsule* hit)
{
    float s = Bam_BorrowScale(fp);
    hit->b_offset.x *= s; hit->b_offset.y *= s; hit->b_offset.z *= s;
    /* The radius is in world units (the model scale sizes the skeleton,
     * not hitboxes): the shown size, else a move from a fighter with a
     * small model scale (Bowser, Pichu) has hitboxes too small for it.
     * A throw's hitboxes are aimed at the fighter being thrown, who keeps
     * their own size: never smaller than the donor's, or Bowser's down
     * throw misses a Mario-sized victim. */
    s = Bam_HitboxScale(fp);
    if (fp->motion_id >= ftCo_MS_ThrowF && fp->motion_id <= ftCo_MS_ThrowLw && s < 1.0f) s = 1.0f;
    hit->scale *= s;
}

/* Follows `hit` every frame (Bam_HitboxRefresh) on `prop` (a prop entry,
 * PROP_CHAIN + chain row or PROP_FINGER + finger part), moved onto `jobj`
 * when not NULL. False when all records are in use. */
static bool track_hit(Fighter* fp, HitCapsule* hit, unsigned source, int prop, HSD_JObj* jobj)
{
    unsigned i;
    PropHit* h;
    float s;
    for (i = 0; i < sizeof(bam_anim->prop_hits) / sizeof(bam_anim->prop_hits[0]); ++i)
        if (!bam_anim->prop_hits[i].hit) break;
    if (i == sizeof(bam_anim->prop_hits) / sizeof(bam_anim->prop_hits[0])) return false;
    h = &bam_anim->prop_hits[i];
    h->fighter = fp;
    h->hit = hit;
    /* hit_place applies the borrow scale itself. */
    s = Bam_BorrowScale(fp);
    h->offset = hit->b_offset;
    h->offset.x /= s; h->offset.y /= s; h->offset.z /= s;
    h->prop = (unsigned char) prop;
    h->kind = (unsigned char) source;
    if (jobj) hit->jobj = jobj;
    hit_place(fp, h);
    return true;
}

/* A hitbox on a donor body bone: the borrower's retargeted bone is the
 * donor's turned by C, so the offset is turned the same way. */
static void body_bone_hit(Fighter* fp, HitCapsule* hit, int bone, unsigned source)
{
    const FighterPartsTable* from = ftPartsTable[source];
    int part = (unsigned) bone < from->parts_num ? from->joint_to_part[bone] : FTPART_INVALID;
    int own_part = part, ds, rs;
    Quat c, d, r;
    Quaternion cq;
    Mtx m;
    Vec3 out;
    if (part == FTPART_INVALID) return;
    if (rest_slot(part) >= BAM_REST_PARTS) {
        /* A donor finger (what Ness's bat and Game & Watch's jab hang
         * from): carried by the hand with the donor's finger pose, every
         * frame, as a prop is (hit_place). */
        int base = fold_base(fp, part), joint = base >= 0 ? own_joint(fp, base) : -1;
        if (joint >= 0 && fp->parts[joint].joint &&
            track_hit(fp, hit, source, PROP_FINGER + part, fp->parts[joint].joint))
            return;
    }
    if (own_joint(fp, part) < 0) {
        /* The borrower lacks the bone (most have no waist): the hitbox rides
         * the bone Bam_DonorBoneJoint chose instead, so carry the offset from
         * the donor's bone frame into that one's (an offset in Jigglypuff's
         * waist frame would point up a tall fighter's hip, over its head). */
        const FighterPartsTable* to = ftPartsTable[fp->kind];
        unsigned j;
        own_part = FTPART_INVALID;
        for (j = 0; j < to->parts_num; ++j)
            if (fp->parts[j].joint == hit->jobj) { own_part = to->joint_to_part[j]; break; }
        if (own_part == FTPART_INVALID) return;
    }
    ds = rest_slot(part);
    rs = rest_slot(own_part);
    if (own_part == part) {
        /* A finger both have is not retargeted: it plays the donor's own
         * rotation on the borrower's hand, so the offset needs the hand's
         * correction (about right: fingers turn little against the hand).
         * Other bones: their own. */
        int hand = rest_slot(part) >= BAM_REST_PARTS && source < BAM_REST_KINDS ?
            bam_part_parent[source][part] : part;
        ds = rs = hand != 0xFF ? body_slot(hand) : -1;
    }
    if (ds < 0 || rs < 0 || !rest_world(source, (unsigned) ds, &d) || !rest_world(fp->kind, (unsigned) rs, &r))
        return;
    c = q_mul(q_conj(d), r);
    if (q_identity(c)) return;
    cq.x = -c.x; cq.y = -c.y; cq.z = -c.z; cq.w = c.w;
    PSMTXQuat(m, &cq);
    PSMTXMultVec(m, &hit->b_offset, &out);
    hit->b_offset = out;
}

/* A move script just made a hitbox (ftAction_8007121C fix); `bone` is the
 * script's joint, or -1 for a common body part. A borrowed move's hitbox is
 * sized for this fighter, then placed by what the bone is on the donor. */
void Bam_HitboxCreated(Fighter* fp, HitCapsule* hit, int bone)
{
    PropHit* h = hit_record(hit);
    unsigned source;
    int prop, root;
    if (h) h->hit = NULL;
    if (!fp || !hit || !Bam_InBorrowedMove(fp)) return;
    source = Bam_DonorKind(fp);
    scale_hitbox(fp, hit);
    if (!prop_active(fp, source) || bone < 0) return;
    prop = prop_find(source, bone);
    if (prop < 0 && bone > 0 && (root = chain_find(source, bone)) >= 0 && fp->parts[0].joint &&
        hit->jobj == fp->parts[0].joint) {
        /* A joint of the donor's own skeleton posed from the root. */
        track_hit(fp, hit, source, PROP_CHAIN + root, NULL);
        return;
    }
    if (prop < 0) {
        body_bone_hit(fp, hit, bone, source);
        return;
    }
    /* A rebuilt donor bone (a sword, a tail). */
    root = prop_root(fp, prop);
    if (root < 0 || hit->jobj != fp->parts[root].joint) return;
    if (!track_hit(fp, hit, source, prop, NULL)) return;
    BAM_LOG("prop_hit donor=%u bone=%d root=%d off=(%.2f,%.2f,%.2f)\n", source, bone, root,
            hit->b_offset.x, hit->b_offset.y, hit->b_offset.z);
}
/* Every frame before a hitbox's position is taken (ftColl_8007AD18). */
void Bam_HitboxRefresh(Fighter* fp, HitCapsule* hit)
{
    PropHit* h = hit_record(hit);
    if (!h) return;
    if (h->fighter != fp || hit->state == HitCapsule_Disabled || !prop_active(fp, h->kind)) {
        h->hit = NULL;
        return;
    }
    hit_place(fp, h);
}

