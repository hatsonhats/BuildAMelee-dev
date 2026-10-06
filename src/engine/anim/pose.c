#include <engine/anim/internal.h>

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
 * (bam_part_parent). TopN and TransN (facing, travel) are left alone. */
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
    Quat world[BAM_PART_COUNT];
    unsigned char done[BAM_PART_COUNT];
} DonorPose;
static Quat donor_world(Fighter* fp, int slot, unsigned source, int part, DonorPose* d, int depth)
{
    Quat identity = { 0.0f, 0.0f, 0.0f, 1.0f }, local, up_world;
    int up, joint;
    if (part <= 1 || part >= BAM_PART_COUNT || depth > 16) return identity;
    if (d->done[part]) return d->world[part];
    up = bam_part_parent[source][part];
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
static void pose_pass(Fighter* fp);
/* After the fighter's animation step (ftAnim_8006EBA4). */
void Bam_AnimPostStep(Fighter* fp)
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
    if (!prop_active(fp, source) || source >= BAM_REST_KINDS || fp->kind >= BAM_REST_KINDS) return;
    if (bam_part_parent[source][4] == 0xFF) return; /* No hierarchy data. */
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

/* ---- The donor's own meshes (visual/donor_model.c draws them) ----
 *
 * Tables from bone_tables.h: which of the donor's meshes belong to a rebuilt
 * bone chain (Marth's sword, Mewtwo's tail), and in which donor motions each
 * chain shows. The donor's model is posed on the recipient: a rebuilt bone
 * where the rebuilt bone is, a body bone the recipient also has on the
 * recipient's bone (turned back by C so the donor's skin fits it), anything
 * else at its rest pose from its parent. */
unsigned Bam_DonorMeshShow(Fighter* fp, unsigned char item_of_group[8])
{
    unsigned source, mask = 0, i;
    if (!fp || !Bam_IsAbilityState(fp)) return 0;
    source = Bam_AbilitySourceKind(fp);
    if (!prop_active(fp, source)) return 0;
    for (i = 0; i < BAM_MESH_SHOW_COUNT; ++i) {
        const BamMeshShow* r = &bam_mesh_show[i];
        if (r->kind != source || r->group >= 8 || (unsigned) fp->anim_id < r->first ||
            (unsigned) fp->anim_id > r->last) continue;
        mask |= 1U << r->group;
        item_of_group[r->group] = r->item;
    }
    return mask;
}
/* The donor's mesh rows: up to `max` (group, DObj index) pairs. */
unsigned Bam_DonorMeshes(unsigned kind, unsigned char* groups, unsigned short* dobjs, unsigned short* pobjs,
                           unsigned max)
{
    unsigned i, n = 0;
    for (i = 0; i < BAM_MESH_COUNT && n < max; ++i)
        if (bam_mesh[i].kind == kind) {
            groups[n] = bam_mesh[i].group;
            if (pobjs) pobjs[n] = bam_mesh[i].pobjs;
            dobjs[n++] = bam_mesh[i].dobj;
        }
    return n;
}
/* World matrices for the donor model's joints (parents before children). */
/* Whether donor joint `joint` is collapsed while mesh groups `shown` show. */
static bool mesh_hidden(unsigned kind, unsigned joint, unsigned shown)
{
    unsigned i;
    for (i = 0; i < BAM_MESH_HIDE_COUNT; ++i)
        if (bam_mesh_hide[i].kind == kind && bam_mesh_hide[i].joint == joint && bam_mesh_hide[i].group < 32 &&
            (shown & (1U << bam_mesh_hide[i].group)))
            return true;
    return false;
}
void Bam_DonorPose(Fighter* fp, HSD_JObj* const* jobjs, const unsigned char* parents, unsigned n, int free_joint,
                     unsigned shown)
{
    unsigned source = fp->x597_bits, d;
    const FighterPartsTable* from;
    float bs = Bam_BorrowScale(fp);
    Mtx grow, worn;
    const Wear* wear = wear_of(source, shown);
    if (source >= BAM_REST_KINDS) return;
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
        if (wear && (d == wear->hip || (prop >= 0 && bam_prop[prop].joint == wear->root))) {
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
        } else if (row >= 0 && (bam_chain[row].parent == 255 || up)) {
            /* Through the donor's own skeleton from the root. */
            if (bam_chain[row].parent == 255) chain_base(fp, w);
            else {
                chain_local(fp, slot_of_fighter(fp), row, rel);
                PSMTXConcat(up->mtx, rel, w);
            }
        } else if (prop >= 0 && bam_prop[prop].parent != 255 && parents[d] < d &&
            bam_prop[bam_prop[prop].parent].joint == parents[d] && up) {
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

AnimScaleState* bam_anim_scale;
/* Called from Bam_MatchBegin / Bam_MatchEnd (bam_fighter.c). */
void Bam_AnimScaleMatchBegin(void)
{
    bam_anim_scale = HSD_MemAlloc(sizeof(*bam_anim_scale));
    memset(bam_anim_scale, 0, sizeof(*bam_anim_scale));
}
void Bam_AnimScaleMatchEnd(void)
{
    bam_anim_scale = NULL;
}

