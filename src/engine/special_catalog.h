#ifndef ROGUE_SPECIAL_CATALOG_H
#define ROGUE_SPECIAL_CATALOG_H
#include <engine/catalog_counts.h>
/* Portable IDs and metadata. Engine pointers remain in the platform registry. */
typedef struct RogueSpecialDef {
    unsigned char id, character, donor, slot;
    const char* name;
} RogueSpecialDef;
extern const RogueSpecialDef rogue_specials[ROGUE_SPECIALS];
const RogueSpecialDef* RogueSpecial_Find(unsigned id);
/* False for specials that are kept for old saves but never offered. */
int RogueSpecial_Offerable(const RogueSpecialDef* def);
#endif
