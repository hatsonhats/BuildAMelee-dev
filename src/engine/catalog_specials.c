/* Looking up a special in the catalog (catalog.h).
 *
 * Called from: the CSS panel, training.c, qa_moves.c.
 * State: none.
 */
#include <engine/catalog.h>
#include <string.h>
const BamSpecialDef* BamSpecial_Find(unsigned id)
{
    unsigned i;
    for (i = 0; i < BAM_SPECIALS; ++i)
        if (bam_specials[i].id == id) return &bam_specials[i];
    return 0;
}
/* Specials that stay in the catalog (old saves keep them) but are never
 * offered: form changes and Kirby's copy ability don't work as borrowed
 * moves (Kirby's Inhale, Sheik's and Zelda's Transform). */
int BamSpecial_Offerable(const BamSpecialDef* def)
{
    return def && def->id != 17 && def->id != 32 && def->id != 80;
}
