#include <engine/special_internal.h>
#include <melee/ft/kinds/ftKoopa/types.h>
#include <melee/lb/lbfile.h>
#include <dolphin/dvd.h>
#include <sysdolphin/baselib/memory.h>
extern char* ftData_803C23E4[Ft_Kind_Max];
static unsigned skipped_donors;

/* Borrowed animations go to ARAM, as a fighter's own do: on every action
 * change Melee copies the action's animation from there into the fighter's
 * 32 KB buffer (ftData_80085CD8 reads Fighter_WaitAnimData.x14 as an ARAM
 * address when it is below 0x80000000, a main-RAM one otherwise). ARAM's
 * lbHeap is rebuilt every scene, so ARAM slices need no freeing. Main RAM
 * only when ARAM is short. */
#define ARAM_KEEP 0x100000
void* Rogue_SliceAlloc(unsigned bytes)
{
    unsigned total, largest;
    void* p = BamCache_Alloc(0, bytes);
    if (p) return p;
    Bam_LbHeapRoom(1, &total, &largest);
    /* lbHeap 1 (ARAM): each block takes one of lbMemory's shared records. */
    if (largest >= bytes + ARAM_KEEP && total >= bytes + ARAM_KEEP && BamCache_FreeRecords() >= 24) {
        extern void* lbHeap_80015BD0(int heap_id, size_t size);
        p = lbHeap_80015BD0(1, bytes);
        if (p) return p;
    }
    /* The match heap (HSD_MemAlloc asserts when it is full). */
    if (Bam_HeapRoom() < OSRoundUp32B(bytes) + BAM_HEAP_FLOOR) return NULL;
    return HSD_MemAlloc(bytes);
}

void Rogue_SliceRead(int file, unsigned offset, void* dst, unsigned bytes)
{
    lbFile_800161C4(file, offset, (uintptr_t) dst, bytes, (u32) dst < 0x80000000U ? 0x23 : 0x21, 1);
}

/* Only match-heap slices are freed; ARAM ones go with their heap or
 * block at scene exit. */
void Rogue_SliceFree(void* p)
{
    if ((u32) p >= 0x80000000U && !BamCache_Owns(p)) HSD_Free(p);
}

/* A donor's core data (PlXx.dat), effects and model. Unlike ftLib_80087508 this does NOT load the donor's whole
 * animation archive (PlXxAJ.dat, 1-2 MB, into ARAM): a VS match has no ARAM
 * or RAM for several of those. Only the equipped moves' animations are read,
 * see load_special_slices. */
/* 0 when memory ran out (the slot keeps the fighter's own move). */
static int load_donor_core(int source)
{
    if (!Rogue_LoadDonorData(source) || !Rogue_LoadDonorEffects(source)) return 0;
    /* The donor's model (costume 0, ~0.1-0.8 MB) only when its moves draw
     * part of it (tails, swords, Mr. Game & Watch's props): the bone
     * retargeting in anim_scale.c uses precomputed rest poses, not the
     * model. Loading it for every donor ran the online match heap out. */
    Rogue_DonorModelPreload((unsigned) source);
    /* Not ftData_800857E0: its only entry is Kirby's, which loads a copy
     * hat for every character in the match (his inhale, which is not
     * offered as a borrowed move), unchecked, into the match heap. */
    return 1;
}

/* Read the animations of one equipped special's donor states into main RAM
 * and point a private copy of the donor's animation table at them (the same
 * scheme the aerials use; Rogue_BorrowBegin installs the private table).
 * Nothing to do when the donor's full archive is resident (the donor is also
 * a fighter in this match). */
/* Which special slot an animation belongs to, from its figatree name in the
 * donor's PlXx.dat ("PlyCaptain5K_Share_ACTION_SpecialAirHi_figatree" ->
 * up). -1 when the name does not say; such animations are always loaded. */
static int anim_slot(const char* name)
{
    const char* p;
    if (!name) return -1;
    for (p = name; *p; ++p) {
        const char* q;
        if (strncmp(p, "Special", 7) != 0) continue;
        q = p + 7;
        if (strncmp(q, "Air", 3) == 0) q += 3;
        if (q[0] == 'H' && q[1] == 'i') return ROGUE_ABILITY_UP;
        if (q[0] == 'L' && q[1] == 'w') return ROGUE_ABILITY_DOWN;
        if (q[0] == 'N') return ROGUE_ABILITY_NEUTRAL;
        if (q[0] == 'S') return ROGUE_ABILITY_SIDE;
    }
    return -1;
}

/* 0 when there was no memory for the animations. */
static int load_special_slices(RogueFighterState* S, const RogueAbilityDefinition* def)
{
    int source = def->internal_kind, m, pass, file, count = ftData_Table_Unk0[source].count;
    unsigned loaded = 0, total = 0;
    u8* buf = NULL;
    Fighter_WaitAnimData* table;
    if (ftData_Table_Unk0[source].data) return 1;
    table = Rogue_DonorAnimTable(S, source);
    if (!table) return 0;
    file = DVDConvertPathToEntrynum(lbFileGetFullName(ftData_803C23E4[source]));
    if (file < 0) OSPanic(__FILE__, __LINE__, "missing donor animation file");
    /* Pass 0 sizes every missing animation, pass 1 reads them all into one
     * block (one heap entry per special instead of one per animation). */
    for (pass = 0; pass < 2; ++pass) {
        unsigned at = 0;
        if (pass == 1) {
            if (!total) break;
            if (S->special_blob_count >= BAM_SPECIAL_BLOBS) return 0;
            buf = Rogue_SliceAlloc(total);
            if (!buf) {
                OSReport("[bam] special_slices id=%u: out of memory\n", def->id);
                return 0;
            }
            S->special_blobs[S->special_blob_count++] = buf;
        }
        for (m = def->first_state; m <= def->last_state; ++m) {
            MotionState* st;
            Fighter_WaitAnimData* anim;
            unsigned offset, skip, bytes, k;
            bool dup = false;
            if (m < ftCo_MS_Count) continue;
            st = &def->states[m - ftCo_MS_Count];
            if (st->anim_id < 0 || st->anim_id >= count) continue;
            anim = &table[st->anim_id];
            if (!anim->x8 || anim->x14) continue;
            if (anim->x8 < 0 || anim->x8 > 0x8000 || anim->x4 < 0) continue;
            /* The donor's state range spans all four of its specials; read
             * only this move's animations (a different slot's are skipped). */
            { int a = anim_slot(anim->x0); if (a >= 0 && a != (int) def->native_slot) continue; }
            /* States sharing one animation: size it once. */
            for (k = (unsigned) def->first_state; k < (unsigned) m; ++k)
                if (k >= ftCo_MS_Count && def->states[k - ftCo_MS_Count].anim_id == st->anim_id) dup = true;
            if (dup) continue;
            offset = (unsigned) anim->x4 & ~31U;
            skip = (unsigned) anim->x4 - offset;
            bytes = ((unsigned) anim->x8 + skip + 31U) & ~31U;
            if (pass == 0) { total += bytes; continue; }
            /* ARAM reads need 32-byte alignment (every PlXxAJ.dat
             * animation starts on one; checked on the disc). */
            if (skip && (u32) buf < 0x80000000U) OSPanic(__FILE__, __LINE__, "unaligned ARAM animation");
            Rogue_SliceRead(file, offset, buf + at, bytes);
            anim->x14 = (u32) (buf + at) + skip;
            at += bytes;
            ++loaded;
        }
    }
    OSReport("[bam] special_slices id=%u kind=%d anims=%u bytes=%u in %s\n", def->id, source, loaded, total,
             (u32) buf < 0x80000000U ? "ARAM" : "RAM");
    return 1;
}
/* Everything a donor's moves need, once per fighter: its fighter data,
 * effects and model (load_donor_core), its attribute block, its articles
 * registered with the item system, and its persistent move variables. The
 * articles of every move are registered, whichever slot needs the donor:
 * registering only records where the article data is. */
int Rogue_DonorEnsure(RogueFighterState* S, int source)
{
    const RogueAbilityDefinition* donor;
    if (source < 0 || source >= ROGUE_DONOR_KINDS) return 0;
    if (S->loaded_sources[source]) return 1;
    donor = Rogue_GetAbility(1 + source * 4);
    if (!donor || donor->attrs_size > sizeof(bam_match->donor_attrs[source])) return 0;
    /* Out of memory for this source: the slot keeps its native move. */
    if (!Rogue_DonorFits(source)) {
        OSReport("[bam] donor kind=%u: out of memory\n", source);
        return 0;
    }
    {
        unsigned before = Bam_HeapRoom();
        if (!load_donor_core(source)) {
            OSReport("[bam] donor kind=%u: out of memory while loading\n", source);
            return 0;
        }
        OSReport("[bam] donor_cost kind=%u core=%u KB\n", source, (before - Bam_HeapRoom()) / 1024);
    }
    if (!gFtDataList[source] || !gFtDataList[source]->ext_attr) return 0;
    memcpy(bam_match->donor_attrs[source].bytes, gFtDataList[source]->ext_attr, donor->attrs_size);
    /* Articles: none when no equipped move needs them (donor_trim.c). */
    if (gFtDataList[source]->x48_items) {
        void** items = gFtDataList[source]->x48_items;
        void* attrs = bam_match->donor_attrs[source].bytes;
        switch (source) {
        case Ft_Kind_Koopa:
            it_8026B3F8(items[0], It_Kind_Koopa_Flame);
            /* Fire Breath starts with a full flame, as Bowser does on
             * every stock (ftKp_Init_OnDeath). Zeroed, it was tiny. */
            S->source_vars[Ft_Kind_Koopa].kp.x222C = ((ftKoopaAttributes*) attrs)->x10;
            S->source_vars[Ft_Kind_Koopa].kp.x2230 = ((ftKoopaAttributes*) attrs)->x18;
            break;
        case Ft_Kind_Samus:
            it_8026B3F8(items[0], It_Kind_Samus_Bomb);
            it_8026B3F8(items[1], It_Kind_Samus_Charge);
            it_8026B3F8(items[2], It_Kind_Samus_Missile);
            it_8026B3F8(items[3], It_Kind_Samus_GBeam);
            break;
        case Ft_Kind_Mewtwo:
            it_8026B3F8(items[0], It_Kind_Mewtwo_Disable);
            it_8026B3F8(items[1], It_Kind_Mewtwo_ShadowBall);
            break;
        case Ft_Kind_Ness:
            it_8026B3F8(items[0], It_Kind_Ness_PKFire);
            it_8026B3F8(items[1], It_Kind_Ness_PKFire_Flame);
            it_8026B3F8(items[2], It_Kind_Ness_PKFlush);
            it_8026B3F8(items[3], It_Kind_Ness_PKThunder);
            it_8026B3F8(items[4], It_Kind_Ness_PKThunder1);
            it_8026B3F8(items[5], It_Kind_Ness_PKThunder2);
            it_8026B3F8(items[6], It_Kind_Ness_PKThunder3);
            it_8026B3F8(items[7], It_Kind_Ness_PKThunder4);
            it_8026B3F8(items[8], It_Kind_Ness_PKFlush_Explode);
            it_8026B3F8(items[9], It_Kind_Ness_Bat);
            it_8026B3F8(items[10], It_Kind_Ness_Yoyo);
            break;
        case Ft_Kind_Peach:
            it_8026B3F8(items[0], It_Kind_Peach_Explode);
            it_8026B3F8(items[1], It_Kind_Peach_Turnip);
            it_8026B3F8(items[2], It_Kind_Peach_Parasol);
            it_8026B3F8(items[3], It_Kind_Peach_Toad);
            it_8026B3F8(items[4], It_Kind_Peach_ToadSpore);
            break;
        case Ft_Kind_Yoshi:
            it_8026B3F8(items[0], It_Kind_Yoshi_EggThrow);
            it_8026B3F8(items[1], It_Kind_Yoshi_Star);
            it_8026B3F8(items[2], It_Kind_Yoshi_EggLay);
            break;
        case Ft_Kind_Zelda:
            it_8026B3F8(items[0], It_Kind_Zelda_DinFire);
            it_8026B3F8(items[1], It_Kind_Zelda_DinFire_Explode);
            break;
        case Ft_Kind_Seak:
            it_8026B3F8(items[0], It_Kind_Seak_NeedleThrow);
            it_8026B3F8(items[1], It_Kind_Seak_NeedleHeld);
            it_8026B3F8(items[2], It_Kind_Seak_Vanish);
            it_8026B3F8(items[3], It_Kind_Seak_Chain);
            break;
        case Ft_Kind_GameWatch:
            it_8026B3F8(items[0], It_Kind_GameWatch_Greenhouse);
            it_8026B3F8(items[1], It_Kind_GameWatch_Manhole);
            it_8026B3F8(items[2], It_Kind_GameWatch_Fire);
            it_8026B3F8(items[3], It_Kind_GameWatch_Parachute);
            it_8026B3F8(items[4], It_Kind_GameWatch_Turtle);
            it_8026B3F8(items[5], It_Kind_GameWatch_Breath);
            it_8026B3F8(items[6], It_Kind_GameWatch_Judge);
            it_8026B3F8(items[7], It_Kind_GameWatch_Panic);
            it_8026B3F8(items[8], It_Kind_GameWatch_Chef);
            it_8026B3F8(items[9], It_Kind_GameWatch_Rescue);
            break;
        case Ft_Kind_Kirby:
            it_8026B3F8(items[0], It_Kind_Kirby_CBeam);
            it_8026B3F8(items[1], It_Kind_Kirby_Hammer);
            it_8026B3F8(items[2], It_Kind_Unk1);
            it_8026B3F8(items[3], It_Kind_Unk2);
            break;
        case Ft_Kind_Popo:
            it_8026B3F8(items[0], It_Kind_IceClimber_Ice);
            it_8026B3F8(items[1], It_Kind_IceClimber_Blizzard);
            it_8026B3F8(items[2], It_Kind_IceClimber_GumStrings);
            break;
        case Ft_Kind_Link: case Ft_Kind_CLink: {
            ftLk_DatAttrs* lk = attrs;
            it_8026B3F8(items[0], lk->x48);
            it_8026B3F8(items[1], lk->x2C);
            it_8026B3F8(items[2], lk->xBC);
            it_8026B3F8(items[3], lk->xC);
            it_8026B3F8(items[4], lk->x10);
            break;
        }
        case Ft_Kind_Mario:
            it_8026B3F8(items[0], It_Kind_Mario_Fire);
            it_8026B3F8(items[2], ((ftMario_DatAttrs*)attrs)->specials.cape_kind);
            break;
        case Ft_Kind_DrMario:
            it_8026B3F8(items[1], It_Kind_DrMario_Vitamin);
            it_8026B3F8(items[3], ((ftMario_DatAttrs*)attrs)->specials.cape_kind);
            break;
        case Ft_Kind_Luigi:
            it_8026B3F8(items[0], It_Kind_Luigi_Fire);
            break;
        case Ft_Kind_Pikachu: case Ft_Kind_Pichu: {
            ftPikachuAttributes* pk = attrs;
            it_8026B3F8(items[0], pk->xDC);
            it_8026B3F8(items[1], pk->specialn_itkind);
            it_8026B3F8(items[2], pk->specialairn_itkind);
            break;
        }
        default: break;
        }
    }
    if ((source == Ft_Kind_Fox || source == Ft_Kind_Falco) && gFtDataList[source]->x48_items) {
        ftFox_DatAttrs* attrs = (ftFox_DatAttrs*) bam_match->donor_attrs[source].bytes;
        void** items = gFtDataList[source]->x48_items;
        it_8026B3F8(items[0], attrs->x1C_FOX_BLASTER_SHOT_ITKIND);
        it_8026B3F8(items[1], attrs->x20_FOX_BLASTER_GUN_ITKIND);
        it_8026B3F8(items[source == Ft_Kind_Fox ? 2 : 3],
                    source == Ft_Kind_Fox ? It_Kind_Fox_Illusion : It_Kind_Falco_Phantasm);
    }
    if (source == Ft_Kind_Kirby) S->source_vars[source].kb.hat.kind = Ft_Kind_Kirby;
    if (source == Ft_Kind_GameWatch) {
        S->source_vars[source].gw.x222C_judgeVar1 = 1;
        S->source_vars[source].gw.x2230_judgeVar2 = 0;
        S->source_vars[source].gw.x2234 = 0;
        S->source_vars[source].gw.x2238_panicCharge = 0;
        S->source_vars[source].gw.x223C_panicDamage = 0;
        S->source_vars[source].gw.x2240_chefVar1 = 1;
        S->source_vars[source].gw.x2244_chefVar2 = 3;
    }
    S->loaded_sources[source] = true;
#if BAM_DEBUG
    OSReport("[bam] donor_preload kind=%u match=%u\n", source, S->match_generation);
#endif
    return 1;
}

/* A private copy of a donor's animation table, for a donor whose animation
 * archive is not resident (it is not playing): borrowed animations are read
 * one by one and the copy points at them (Rogue_BorrowBegin installs it).
 * Every ARAM address is cleared first: a preloaded PlXx.dat keeps the ones
 * of the match it was last played in, which are gone. */
Fighter_WaitAnimData* Rogue_DonorAnimTable(RogueFighterState* S, int source)
{
    unsigned bytes, i;
    Fighter_WaitAnimData* t;
    if (source < 0 || source >= Ft_Kind_Max || ftData_Table_Unk0[source].data || !gFtDataList[source]) return NULL;
    if (S->aerial_anims[source]) return S->aerial_anims[source];
    bytes = ftData_Table_Unk0[source].count * sizeof(Fighter_WaitAnimData);
    t = BamCache_Alloc(1, bytes);
    if (!t) {
        if (Bam_HeapRoom() < OSRoundUp32B(bytes) + BAM_HEAP_FLOOR) return NULL;
        t = HSD_MemAlloc(bytes);
        if (!t) return NULL;
    }
    memcpy(t, gFtDataList[source]->xC, bytes);
    for (i = 0; i < ftData_Table_Unk0[source].count; ++i) t[i].x14 = 0;
    S->aerial_anims[source] = t;
    return t;
}

/* Reads the listed animations of a donor that are not read yet into one
 * block (ARAM when it has room) and points the private table at them. */
int Rogue_DonorReadAnims(RogueFighterState* S, int source, const short* anims, unsigned n, const char* what)
{
    Fighter_WaitAnimData* table;
    int file, count, pass;
    unsigned total = 0, loaded = 0, i, k;
    u8* buf = NULL;
    if (ftData_Table_Unk0[source].data) return 1; /* resident: its own table */
    table = Rogue_DonorAnimTable(S, source);
    if (!table) return 0;
    count = ftData_Table_Unk0[source].count;
    file = DVDConvertPathToEntrynum(lbFileGetFullName(ftData_803C23E4[source]));
    if (file < 0) return 0;
    for (pass = 0; pass < 2; ++pass) {
        unsigned at = 0;
        if (pass == 1) {
            if (!total) break;
            if (S->special_blob_count >= BAM_SPECIAL_BLOBS) return 0;
            buf = Rogue_SliceAlloc(total);
            if (!buf) {
                OSReport("[bam] %s kind=%d: no memory for %u KB of animations\n", what, source, total / 1024);
                return 0;
            }
            S->special_blobs[S->special_blob_count++] = buf;
        }
        for (i = 0; i < n; ++i) {
            Fighter_WaitAnimData* anim;
            unsigned offset, skip, bytes;
            bool dup = false;
            if (anims[i] < 0 || anims[i] >= count) continue;
            for (k = 0; k < i; ++k) if (anims[k] == anims[i]) dup = true;
            if (dup) continue;
            anim = &table[anims[i]];
            if (!anim->x8 || anim->x14) continue;
            if (anim->x8 < 0 || anim->x8 > 0x8000 || anim->x4 < 0) continue;
            offset = (unsigned) anim->x4 & ~31U;
            skip = (unsigned) anim->x4 - offset;
            bytes = ((unsigned) anim->x8 + skip + 31U) & ~31U;
            if (pass == 0) { total += bytes; continue; }
            if (skip && (u32) buf < 0x80000000U) return 0; /* ARAM reads are 32-byte aligned */
            Rogue_SliceRead(file, offset, buf + at, bytes);
            anim->x14 = (u32) (buf + at) + skip;
            at += bytes;
            ++loaded;
        }
    }
    OSReport("[bam] %s kind=%d anims=%u bytes=%u in %s\n", what, source, loaded, total,
             (u32) buf < 0x80000000U ? "ARAM" : "RAM");
    return 1;
}

unsigned Rogue_AbilitySkippedDonors(void) { return skipped_donors; }
void Rogue_AbilityFighterCreated(Fighter* fp)
{
    int i;
    unsigned index;
    RogueFighterState* S;
#if BAM_DEBUG
    if (fp->kind == Ft_Kind_Nana || fp->kind == Ft_Kind_Popo)
        OSReport("[bam] climber_create kind=%u sub=%u player=%u build=%d\n",
            (unsigned) fp->kind, (unsigned) fp->is_sub_fighter, (unsigned) fp->player_id, (int) Rogue_IsBuildFighter(fp));
#endif
    if (!Rogue_IsBuildFighter(fp)) return;
    index = (unsigned) Bam_FighterIndex(fp);
    S = &bam_match->fighters[index];
    *Bam_FighterExtSlot(fp) = S;
    if (index == 0) skipped_donors = 0;
    if (S->fighter) Rogue_AbilityFighterDestroyed(S->fighter);
    memset(S, 0, sizeof(*S));
    S->fighter = fp;
    S->match_generation = bam_match->generation;
    memcpy(S->specials, bam_loadouts[fp->player_id].specials, sizeof(S->specials));
    memcpy(S->aerials, bam_loadouts[fp->player_id].aerials, sizeof(S->aerials));
    /* match accounting removed */
    /* trace removed */
#if BAM_DEBUG
    OSReport("[bam] fighter_create kind=%u match=%u\n", fp->kind, S->match_generation);
#endif
    /* Nana: Popo (created just before her) already loaded every donor and
     * aerial slice. Share them instead of loading or allocating again, which
     * Training Mode has no memory for. */
    if ((index & 1) && bam_match->fighters[index - 1].fighter) {
        const RogueFighterState* P = &bam_match->fighters[index - 1];
        memcpy(S->loaded, P->loaded, sizeof(S->loaded));
        memcpy(S->loaded_sources, P->loaded_sources, sizeof(S->loaded_sources));
        memcpy(S->source_vars, P->source_vars, sizeof(S->source_vars));
        memcpy(S->aerial_equipped, P->aerial_equipped, sizeof(S->aerial_equipped));
        memcpy(S->aerial_anims, P->aerial_anims, sizeof(S->aerial_anims));
        memcpy(S->normals, P->normals, sizeof(S->normals));
        S->shares_partner = true;
        return;
    }

    Bam_LogHeapRoom("before donors");
    for (i = 1; i < ROGUE_ABILITY_COUNT; ++i) {
        const RogueAbilityDefinition* def = Rogue_GetAbility(i);
        int slot, source;
        bool needed = false;
        if (!def) continue;
        source = def->internal_kind;
        for (slot = 0; slot < ROGUE_ABILITY_SLOTS; ++slot) {
            const RogueAbilityDefinition* equipped = Rogue_GetAbility(Rogue_EquippedSpecial(fp, slot));
            if (!equipped) continue;
            if (equipped->internal_kind == source ||
                ((source == Ft_Kind_Zelda || source == Ft_Kind_Seak) &&
                 (equipped->internal_kind == Ft_Kind_Zelda || equipped->internal_kind == Ft_Kind_Seak)))
                needed = true;
        }
        if (!needed) continue;
        if (S->loaded_sources[source]) { S->loaded[i] = true; continue; }
        if (!Rogue_DonorEnsure(S, source)) {
            ++skipped_donors;
            OSReport("[bam] donor_skipped kind=%u ability=%u\n", source, i);
            continue;
        }
        S->loaded[def->id] = true;
    }
    {
        int slot;
        for (slot = 0; slot < ROGUE_ABILITY_SLOTS; ++slot) {
            const RogueAbilityDefinition* d = Rogue_GetAbility(Rogue_EquippedSpecial(fp, slot));
            if (d && S->loaded[d->id] && !load_special_slices(S, d)) S->loaded[d->id] = false;
        }
    }
    Rogue_AerialPrepare(fp);
    Rogue_NormalPrepare(fp);
    Bam_LogHeapRoom("after donors");
}

