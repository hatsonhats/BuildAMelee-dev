/* Share codes: a build as 22 base-32 symbols, one per move slot plus a
 * check symbol.
 *
 * Specials and aerials are their position among the catalog's moves for
 * that slot (the same numbers the online exchange sends); ground attacks and
 * throws are the donor's CharacterKind + 1. The check symbol hashes the
 * moves together with a fingerprint of the move catalogs, so a typo or a
 * code from a version whose catalogs differ is refused instead of loading
 * different moves. */
/* Menu code: smaller beats faster (the overlay has a fixed size). */
#pragma optimize_for_size on
#pragma auto_inline off
#include "build_code.h"
#include <engine/internal.h>
#include <engine/catalog.h>

const char bam_code_alphabet[33] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

unsigned Bam_SpecialIndex(unsigned slot, unsigned id)
{
    unsigned i, n = 0;
    if (!id) return 0;
    for (i = 0; i < BAM_SPECIALS; ++i)
        if (bam_specials[i].slot == slot) {
            ++n;
            if (bam_specials[i].id == id) return n < 32 ? n : 0;
        }
    return 0;
}

unsigned Bam_SpecialFromIndex(unsigned slot, unsigned idx)
{
    unsigned i, n = 0;
    if (!idx) return 0;
    for (i = 0; i < BAM_SPECIALS; ++i)
        if (bam_specials[i].slot == slot && ++n == idx) return bam_specials[i].id;
    return 0;
}

unsigned Bam_AerialIndex(unsigned slot, unsigned id)
{
    unsigned i, n = 0;
    if (!id) return 0;
    for (i = 0; i < BAM_AERIALS; ++i)
        if (bam_aerials[i].slot == slot) {
            ++n;
            if (bam_aerials[i].id == id) return n < 32 ? n : 0;
        }
    return 0;
}

unsigned Bam_AerialFromIndex(unsigned slot, unsigned idx)
{
    unsigned i, n = 0;
    if (!idx) return 0;
    for (i = 0; i < BAM_AERIALS; ++i)
        if (bam_aerials[i].slot == slot && ++n == idx) return bam_aerials[i].id;
    return 0;
}

#define FNV(h, v) ((h) = ((h) ^ (unsigned) (v)) * 16777619U)

static unsigned check_symbol(const unsigned char* code)
{
    static unsigned catalog;
    unsigned h, i;
    if (!catalog) {
        h = 2166136261U;
        for (i = 0; i < BAM_SPECIALS; ++i) {
            FNV(h, bam_specials[i].id); FNV(h, bam_specials[i].character); FNV(h, bam_specials[i].slot);
        }
        for (i = 0; i < BAM_AERIALS; ++i) {
            FNV(h, bam_aerials[i].id); FNV(h, bam_aerials[i].character); FNV(h, bam_aerials[i].slot);
        }
        catalog = h | 1;
    }
    h = catalog;
    for (i = 0; i < BAM_CODE_MOVES; ++i) FNV(h, code[i]);
    h ^= h >> 15;
    h *= 0x2C1B3C6DU;
    h ^= h >> 12;
    return (h ^ (h >> 5) ^ (h >> 10) ^ (h >> 20)) & 31;
}

void Bam_CodeFromLoadout(const BamLoadout* l, unsigned char* code)
{
    unsigned i, k = 0;
    for (i = 0; i < BAM_SPECIAL_SLOTS; ++i) code[k++] = (unsigned char) (l->enabled ? Bam_SpecialIndex(i, l->specials[i]) : 0);
    for (i = 0; i < BAM_AERIAL_SLOTS; ++i) code[k++] = (unsigned char) (l->enabled ? Bam_AerialIndex(i, l->aerials[i]) : 0);
    for (i = 0; i < BAM_NORMAL_SLOTS; ++i)
        code[k++] = (unsigned char) (l->enabled && l->normals[i] <= 26 ? l->normals[i] : 0);
    code[k] = (unsigned char) check_symbol(code);
}

int Bam_LoadoutFromCode(const unsigned char* code, BamLoadout* l)
{
    BamLoadout out;
    unsigned i, k = 0, any = 0;
    for (i = 0; i < BAM_CODE_LEN; ++i)
        if (code[i] > 31) return 0;
    if (code[BAM_CODE_MOVES] != check_symbol(code)) return 0;
    for (i = 0; i < BAM_SPECIAL_SLOTS; ++i, ++k) {
        out.specials[i] = (unsigned char) Bam_SpecialFromIndex(i, code[k]);
        if (code[k] && !out.specials[i]) return 0;
        any |= out.specials[i];
    }
    for (i = 0; i < BAM_AERIAL_SLOTS; ++i, ++k) {
        out.aerials[i] = (unsigned char) Bam_AerialFromIndex(i, code[k]);
        if (code[k] && !out.aerials[i]) return 0;
        any |= out.aerials[i];
    }
    for (i = 0; i < BAM_NORMAL_SLOTS; ++i, ++k) {
        if (code[k] > 26) return 0;
        out.normals[i] = code[k];
        any |= out.normals[i];
    }
    out.enabled = any != 0;
    *l = out;
    return 1;
}

void Bam_CodeText(const unsigned char* code, char* out)
{
    static const unsigned char groups[] = { 4, 5, 5, 3, 5 };
    unsigned g, i, k = 0;
    for (g = 0; g < sizeof(groups); ++g) {
        if (g) *out++ = '-';
        for (i = 0; i < groups[g]; ++i) *out++ = bam_code_alphabet[code[k++] & 31];
    }
    *out = 0;
}
