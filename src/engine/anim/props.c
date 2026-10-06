#include <engine/anim/internal.h>

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
 * sword is drawn along it (visual/weapons.c). Rest data: bone_tables.h. */






int prop_find(unsigned kind, int joint)
{
    int i;
    for (i = 0; i < ROGUE_PROP_COUNT; ++i)
        if (rogue_prop[i].kind == kind && rogue_prop[i].joint == joint) return i;
    return -1;
}
int chain_find(unsigned kind, int joint)
{
    int i;
    for (i = 0; i < ROGUE_CHAIN_COUNT; ++i)
        if (rogue_chain[i].kind == kind && rogue_chain[i].joint == joint) return i;
    return -1;
}
int slot_of_fighter(const Fighter* fp)
{
    RogueFighterState* S = Rogue_FighterCtx(fp);
    unsigned slot;
    if (!fp || !S || S->fighter != fp) return -1;
    slot = (unsigned) (S - bam_match->fighters);
    return slot < BAM_FIGHTERS ? (int) slot : -1;
}
int own_joint(Fighter* fp, int part)
{
    return part_joint_raw(fp, part);
}
/* The recipient body part a prop entry hangs from: the donor's, or when the
 * recipient lacks it (Kirby has no finger bones) the donor's next part up
 * that it has. -1 if none. */
int rest_slot(int part);
/* The body part something hanging from donor part `part` is carried by:
 * the part itself, or the nearest one above it the borrower has. Not a
 * finger even when the borrower has one: fingers are not retargeted, so the
 * borrower's finger keeps its own frame (Yoshi's is turned 120 degrees from
 * Marth's). From the hand, which is, with the donor's finger rotations
 * folded in (part_fold). -1 if none. */
int fold_base(Fighter* fp, int part)
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
int prop_root(Fighter* fp, int prop)
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
int rest_slot(int part)
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
int body_slot(int part)
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
Quat part_local(Fighter* fp, int slot, unsigned kind, int part, int finger_rest)
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
Quat fold_quat(const RotFix* e)
{
    Quat q = { 0.0f, 0.0f, 0.0f, 1.0f };
    Fighter* fp = bam_match->fighters[e->slot].fighter;
    unsigned i;
    if (!fp) return q;
    for (i = 0; i < e->nfold; ++i) q = q_mul(q, part_local(fp, e->slot, e->kind, e->fold[i], 0));
    return q;
}
/* Local transform of a prop entry at the fighter's current animation frame. */
void prop_local(Fighter* fp, int slot, int prop, Mtx out)
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
void chain_local(Fighter* fp, int slot, int row, Mtx out)
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
void chain_base(Fighter* fp, Mtx out)
{
    float bs = Rogue_BorrowScale(fp);
    Mtx grow;
    PSMTXScale(grow, bs, bs, bs);
    PSMTXConcat(HSD_JObjGetMtxPtr(fp->parts[0].joint), grow, out);
}
/* World matrix of chain entry `row`. */
void chain_world(Fighter* fp, int row, Mtx out)
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
void part_fold(Fighter* fp, int from, Mtx out)
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
int prop_relative(Fighter* fp, int prop, Mtx out)
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

bool prop_active(Fighter* fp, unsigned kind)
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
void prop_reset(const Fighter* fp)
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

