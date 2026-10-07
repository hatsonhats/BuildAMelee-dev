#ifndef BAM_AERIAL_CATALOG_H
#define BAM_AERIAL_CATALOG_H
#ifndef BAM_AERIAL_SLOTS
#define BAM_AERIAL_SLOTS 5 /* also in bam_fighter.h */
#endif
/* The borrowable aerials (hand-maintained, aerial_catalog_data.c): id,
 * character (CharacterKind), donor (FighterKind), slot (0 neutral, 1 forward,
 * 2 back, 3 up, 4 down). */
#define BAM_AERIALS 130U
typedef struct BamAerialDef {
    unsigned char id, character, donor, slot;
} BamAerialDef;
extern const BamAerialDef bam_aerials[BAM_AERIALS];
const BamAerialDef* BamAerial_Find(unsigned id);
#endif
