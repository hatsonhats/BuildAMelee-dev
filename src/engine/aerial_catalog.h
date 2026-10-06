#ifndef BAM_AERIAL_CATALOG_H
#define BAM_AERIAL_CATALOG_H
#ifndef BAM_AERIAL_SLOTS
#define BAM_AERIAL_SLOTS 5 /* also in bam_fighter.h */
#endif
#include <engine/catalog_counts.h>
typedef struct BamAerialDef {
    unsigned char id, character, donor, slot;
} BamAerialDef;
extern const BamAerialDef bam_aerials[BAM_AERIALS];
const BamAerialDef* BamAerial_Find(unsigned id);
#endif
