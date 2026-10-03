#include <engine/special_catalog.h>
#include <string.h>
const RogueSpecialDef* RogueSpecial_Find(unsigned id)
{
    unsigned i;
    for (i = 0; i < ROGUE_SPECIALS; ++i)
        if (rogue_specials[i].id == id) return &rogue_specials[i];
    return 0;
}
/* Specials that stay in the catalog (old saves keep them) but are never
 * offered: form changes and Kirby's copy ability don't work as borrowed
 * moves (Kirby's Inhale, Sheik's and Zelda's Transform). */
int RogueSpecial_Offerable(const RogueSpecialDef* def)
{
    return def && def->id != 17 && def->id != 32 && def->id != 80;
}
