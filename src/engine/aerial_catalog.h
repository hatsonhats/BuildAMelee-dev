#ifndef ROGUE_AERIAL_CATALOG_H
#define ROGUE_AERIAL_CATALOG_H
#define ROGUE_AERIAL_SLOTS 5
#include <engine/catalog_counts.h>
typedef struct RogueAerialDef {
    unsigned char id, character, donor, slot;
} RogueAerialDef;
extern const RogueAerialDef rogue_aerials[ROGUE_AERIALS];
const RogueAerialDef* RogueAerial_Find(unsigned id);
#endif
