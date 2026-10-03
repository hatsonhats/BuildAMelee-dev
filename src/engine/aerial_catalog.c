#include <engine/aerial_catalog.h>
const RogueAerialDef* RogueAerial_Find(unsigned id)
{
    unsigned i;
    if (!id) return 0;
    /* Keep the dense fast path, but persistent identity is the authored ID. */
    if (id <= ROGUE_AERIALS && rogue_aerials[id-1].id == id)
        return &rogue_aerials[id-1];
    for (i = 0; i < ROGUE_AERIALS; ++i)
        if (rogue_aerials[i].id == id) return &rogue_aerials[i];
    return 0;
}
