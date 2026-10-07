#ifndef BAM_SPECIAL_CATALOG_H
#define BAM_SPECIAL_CATALOG_H
/* The borrowable specials (hand-maintained, special_catalog_data.c): id =
 * 1 + donor kind * 4 + slot (saved builds and share codes store it),
 * the character it belongs to (CharacterKind), the donor (FighterKind), the
 * slot (0 neutral, 1 side, 2 up, 3 down) and the name shown. The engine's
 * per-donor data for each is in special_registry.c, by the same id. */
#define BAM_SPECIALS 104U
typedef struct BamSpecialDef {
    unsigned char id, character, donor, slot;
    const char* name;
} BamSpecialDef;
extern const BamSpecialDef bam_specials[BAM_SPECIALS];
const BamSpecialDef* BamSpecial_Find(unsigned id);
/* False for specials that are kept for old saves but never offered. */
int BamSpecial_Offerable(const BamSpecialDef* def);
#endif
