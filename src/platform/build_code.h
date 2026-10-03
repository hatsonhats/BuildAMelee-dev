/* Builds as short codes (share codes, saved slots) and the catalog indexes
 * the online exchange also uses. */
#ifndef BAM_BUILD_CODE_H
#define BAM_BUILD_CODE_H
#include <bam/bam.h>
#include <engine/bam_fighter.h>

/* One symbol (0..31) per move slot: specials, aerials, ground attacks and
 * throws, then a check symbol. */
#define BAM_CODE_MOVES (BAM_SPECIAL_SLOTS + BAM_AERIAL_SLOTS + BAM_NORMAL_SLOTS)
#define BAM_CODE_LEN (BAM_CODE_MOVES + 1)
/* As text: groups of 4-5-5-3-5 (the panel's tabs; the check ends the last). */
#define BAM_CODE_TEXT (BAM_CODE_LEN + 5)

/* Crockford's base 32: no I, L, O or U. */
extern const char bam_code_alphabet[33];

/* Position (1-based) of a move among the catalog's moves for its slot; 0 for
 * the fighter's own move. Fits 5 bits. */
unsigned Bam_SpecialIndex(unsigned slot, unsigned id);
unsigned Bam_SpecialFromIndex(unsigned slot, unsigned idx);
unsigned Bam_AerialIndex(unsigned slot, unsigned id);
unsigned Bam_AerialFromIndex(unsigned slot, unsigned idx);

/* code: BAM_CODE_LEN symbols. */
void Bam_CodeFromLoadout(const BamLoadout* l, unsigned char* code);
/* 1 and the loadout if the code is whole (check symbol and every move). */
int Bam_LoadoutFromCode(const unsigned char* code, BamLoadout* l);
/* "XXXX-XXXXX-XXXXX-XXX-XXXXX"; out holds BAM_CODE_TEXT bytes. */
void Bam_CodeText(const unsigned char* code, char* out);
#endif
