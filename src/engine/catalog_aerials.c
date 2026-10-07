/* Looking up an aerial in the catalog (catalog.h).
 *
 * Called from: aerials.c, normals.c and the CSS panel.
 * State: none.
 */
#include <engine/catalog.h>
const BamAerialDef* BamAerial_Find(unsigned id)
{
    unsigned i;
    if (!id) return 0;
    /* Keep the dense fast path, but persistent identity is the authored ID. */
    if (id <= BAM_AERIALS && bam_aerials[id-1].id == id)
        return &bam_aerials[id-1];
    for (i = 0; i < BAM_AERIALS; ++i)
        if (bam_aerials[i].id == id) return &bam_aerials[i];
    return 0;
}
