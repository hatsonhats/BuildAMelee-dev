#include <engine/anim/internal.h>

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
 * Bam_ShownScale: that size on screen, against the donor's own (0 when
 * own and source are the same kind). */
#pragma push
#pragma dont_inline on
float Bam_ShownScale(unsigned own, unsigned source)
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
    if (!fp || !Bam_InBorrowedMove(fp)) return 0.0f;
    *source = Bam_DonorKind(fp);
    return Bam_ShownScale(fp->kind, *source);
}

/* In the fighter's own skeleton units: the shown size over the model scale
 * difference, as the skeleton applies this fighter's model scale. */
float Bam_BorrowScale(Fighter* fp)
{
    unsigned source;
    float shown = shown_scale(fp, &source);
    if (shown == 0.0f) return 1.0f;
    return shown * model_scale[source] / model_scale[fp->kind];
}

/* Effects a fighter spawns sized by its own scale (eflib.c): a borrowed
 * move's are sized as its hitbox offsets are, so they line up. */
float Bam_EffectScale(HSD_GObj* gobj)
{
    if (!gobj || gobj->classifier != HSD_GOBJ_CLASS_FIGHTER) return 1.0f;
    return Bam_BorrowScale(GET_FIGHTER(gobj));
}

/* In world units: hitbox radii, which no model scale applies to. */
float Bam_HitboxScale(Fighter* fp)
{
    unsigned source;
    float shown = shown_scale(fp, &source);
    return shown == 0.0f ? 1.0f : shown;
}
#pragma pop
/* Where a borrowed projectile leaves from: not lower than about the middle
 * of the borrower's body, so a shot from a short fighter's low bone does not
 * start in the floor (Samus's Charge Shot from Jigglypuff). */
void Bam_ProjectileOrigin(Fighter* fp, Vec3* pos)
{
    float low;
    if (!fp || !pos || !Bam_InBorrowedMove(fp) || (unsigned) fp->kind >= BODY_SIZE_KINDS) return;
    low = fp->cur_pos.y + 0.45f * body_size[fp->kind] * model_scale[fp->kind];
    if (pos->y < low) pos->y = low;
}
float Bam_OwnerScale(HSD_GObj* owner)
{
    Fighter* fp = owner ? GET_FIGHTER(owner) : NULL;
    if (!fp) return 1.0f;
    return fp->x34_scale.y * Bam_BorrowScale(fp);
}
