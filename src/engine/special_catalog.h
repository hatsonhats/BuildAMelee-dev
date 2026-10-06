#ifndef BAM_SPECIAL_CATALOG_H
#define BAM_SPECIAL_CATALOG_H
#include <engine/catalog_counts.h>
/* Portable IDs and metadata. Engine pointers remain in the platform registry. */
typedef struct BamSpecialDef {
    unsigned char id, character, donor, slot;
    const char* name;
} BamSpecialDef;
extern const BamSpecialDef bam_specials[BAM_SPECIALS];
const BamSpecialDef* BamSpecial_Find(unsigned id);
/* False for specials that are kept for old saves but never offered. */
int BamSpecial_Offerable(const BamSpecialDef* def);
#endif
