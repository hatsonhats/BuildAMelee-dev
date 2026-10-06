#include <engine/anim/internal.h>

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


void anchors_follow(Fighter* fp)
{
    int slot = slot_of_fighter(fp);
    unsigned i;
    if (slot < 0) return;
    for (i = 0; i < ANCHORS; ++i)
        if (anchors[slot][i].jobj) anchor_place(fp, &anchors[slot][i]);
}
