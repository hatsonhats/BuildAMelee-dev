/* Loading a donor's files for borrowed moves: its data trimmed to what
 * borrowed moves use, effects, animations, and model (donor_load.c,
 * donor_trim.c, donor_cache.c, visual/donor_model.c). */
#ifndef BAM_DONOR_LOAD_H
#define BAM_DONOR_LOAD_H
#include <dolphin/types.h>

/* What the match heap keeps free after borrowed-move data is loaded into it:
 * the scene allocates ~300 KB more once the fighters exist, and matches that
 * started with ~700 KB free ran out mid-match (effects, items). */
#define BAM_HEAP_FLOOR 0x160000
void Bam_DonorModelPreload(unsigned kind);
/* PlXx.dat for a borrowed move, trimmed of what borrowed moves never use. */
int Bam_LoadDonorData(int kind);
/* Whether a donor's articles are needed by any equipped move of any player
 * (normals.c); without, its data is loaded without them. */
int Bam_DonorNeedsArticles(int kind);
/* Borrowed animation slices: ARAM when it has room, else main RAM. */
void* Bam_SliceAlloc(unsigned bytes);
void Bam_SliceRead(int file, unsigned offset, void* dst, unsigned bytes);
void Bam_SliceFree(void* p);
/* Effects file for a borrowed move (donor_trim.c). */
int Bam_LoadDonorEffects(int kind);
/* Preload-cache blocks for borrowed-move data (donor_cache.c).
 * ram: 1 main RAM, 0 ARAM. */
void* BamCache_Alloc(int ram, u32 size);
void* BamCache_TempAlloc(u32 size);
void BamCache_TempReset(void);
u32 BamCache_Room(int ram);
int BamCache_Owns(const void* p);
int BamCache_FreeRecords(void);
void BamCache_SceneEnter(void);
void BamCache_SceneExit(void);
/* Donor models (drawn only) are loaded after the scene created its fighters. */
void Bam_DonorModelsLoad(void);
#endif
