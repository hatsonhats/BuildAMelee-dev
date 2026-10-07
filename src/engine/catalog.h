/* The moves a build can borrow, as players pick them and saves store them
 * (catalog_specials*.c, catalog_aerials*.c). Hand-maintained. */
#ifndef BAM_CATALOG_H
#define BAM_CATALOG_H
#ifndef BAM_AERIAL_SLOTS
#define BAM_AERIAL_SLOTS 5 /* also in fighter.h */
#endif
/* The borrowable specials (hand-maintained, catalog_specials_data.c): id =
 * 1 + donor kind * 4 + slot (saved builds and share codes store it),
 * the character it belongs to (CharacterKind), the donor (FighterKind), the
 * slot (0 neutral, 1 side, 2 up, 3 down) and the name shown. The engine's
 * per-donor data for each is in donor_specials.c, by the same id. */
#define BAM_SPECIALS 104U
typedef struct BamSpecialDef {
    unsigned char id, character, donor, slot;
    const char* name;
} BamSpecialDef;
extern const BamSpecialDef bam_specials[BAM_SPECIALS];
const BamSpecialDef* BamSpecial_Find(unsigned id);
/* False for specials that are kept for old saves but never offered. */
int BamSpecial_Offerable(const BamSpecialDef* def);
/* The borrowable aerials (hand-maintained, catalog_aerials_data.c): id,
 * character (CharacterKind), donor (FighterKind), slot (0 neutral, 1 forward,
 * 2 back, 3 up, 4 down). */
#define BAM_AERIALS 130U
typedef struct BamAerialDef {
    unsigned char id, character, donor, slot;
} BamAerialDef;
extern const BamAerialDef bam_aerials[BAM_AERIALS];
const BamAerialDef* BamAerial_Find(unsigned id);
#endif
