#include <engine/anim/internal.h>

static PropHit* hit_record(const HitCapsule* hit)
{
    unsigned i;
    if (!bam_anim_scale) return NULL;
    for (i = 0; i < sizeof(prop_hits) / sizeof(prop_hits[0]); ++i)
        if (prop_hits[i].hit == hit) return &prop_hits[i];
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
/* A move script just made a hitbox (ftAction_8007121C); `bone` is the
 * script's joint, or -1 for a common body part. Offsets of a borrowed move
 * are carried into the recipient's frame. */
void Bam_HitboxCreated(Fighter* fp, HitCapsule* hit, int bone)
{
    PropHit* h = hit_record(hit);
    unsigned source, i;
    int prop, root;
    if (h) h->hit = NULL;
    if (!fp || !hit || !Bam_IsAbilityState(fp)) return;
    source = Bam_AbilitySourceKind(fp);
    /* Offsets and radii fit this fighter's body (Bam_BorrowScale), as
     * the move's weapons and props are drawn: a hitbox stays on the part it
     * belongs to, and keeps its size relative to it (the hitbox size
     * command scales the same way, overrides/fixes/30-normals.toml). */
    {
        float s = Bam_BorrowScale(fp);
        hit->b_offset.x *= s; hit->b_offset.y *= s; hit->b_offset.z *= s;
        /* The radius is in world units (the model scale sizes the skeleton,
         * not hitboxes): the shown size, else a move from a fighter with a
         * small model scale (Bowser, Pichu) has hitboxes too small for it.
         * A throw's hitboxes are aimed at the fighter being thrown, who
         * keeps their own size: never smaller than the donor's (Bowser's
         * down throw on Mario-sized fighters missed the victim entirely). */
        s = Bam_HitboxScale(fp);
        if (fp->motion_id >= ftCo_MS_ThrowF && fp->motion_id <= ftCo_MS_ThrowLw && s < 1.0f) s = 1.0f;
        hit->scale *= s;
    }
    if (!prop_active(fp, source)) return;
    if (bone < 0) return;
    prop = prop_find(source, bone);
    if (prop < 0 && bone > 0 && (root = chain_find(source, bone)) >= 0 && fp->parts[0].joint &&
        hit->jobj == fp->parts[0].joint) {
        /* A joint of the donor's own skeleton posed from the root: follow
         * that pose every frame (Bam_HitboxRefresh). */
        for (i = 0; i < sizeof(prop_hits) / sizeof(prop_hits[0]); ++i)
            if (!prop_hits[i].hit) break;
        if (i == sizeof(prop_hits) / sizeof(prop_hits[0])) return;
        h = &prop_hits[i];
        h->fighter = fp;
        h->hit = hit;
        h->offset = hit->b_offset;
        {
            float s = Bam_BorrowScale(fp);
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
        if (rest_slot(part) >= BAM_REST_PARTS) {
            /* A donor finger (what Ness's bat and Game & Watch's jab hang
             * from): carried by the hand with the donor's finger pose, every
             * frame, as a prop is (hit_place). */
            int base = fold_base(fp, part), joint = base >= 0 ? own_joint(fp, base) : -1;
            if (joint >= 0 && fp->parts[joint].joint) {
                for (i = 0; i < sizeof(prop_hits) / sizeof(prop_hits[0]); ++i)
                    if (!prop_hits[i].hit) break;
                if (i < sizeof(prop_hits) / sizeof(prop_hits[0])) {
                    float bs = Bam_BorrowScale(fp);
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
             * rides the bone Bam_AbilityMapBone chose instead, so carry
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
                int hand = rest_slot(part) >= BAM_REST_PARTS && source < BAM_REST_KINDS ?
                    bam_part_parent[source][part] : part;
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
        float s = Bam_BorrowScale(fp);
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

