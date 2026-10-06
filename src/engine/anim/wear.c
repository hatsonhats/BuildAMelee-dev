#include <engine/anim/internal.h>

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
static const Wear wears[] = { { Ft_Kind_Peach, 2, 17, 4 } };
const Wear* wear_of(unsigned source, unsigned shown)
{
    unsigned i;
    for (i = 0; i < sizeof(wears) / sizeof(wears[0]); ++i)
        if (wears[i].kind == source && (shown & (1U << wears[i].group))) return &wears[i];
    return NULL;
}
/* The wearer's frame for the donor's hip: the borrower's hip turned onto
 * the donor's (as body parts are), stretched to fit, its waist placed
 * between the legs at the wearer's waist height. 0 if no hip. */
int wear_base(Fighter* fp, unsigned source, Mtx out)
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

