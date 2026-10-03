/* Saved builds: three slots, kept on the memory card in Slot A when there
 * is one, otherwise until the game is closed. */
#ifndef BAM_BUILD_STORE_H
#define BAM_BUILD_STORE_H
#include "build_code.h"

#define BAM_SAVE_SLOTS 3
typedef struct BamSavedSlot {
    unsigned char used;
    unsigned char code[BAM_CODE_LEN];
} BamSavedSlot;
extern BamSavedSlot bam_saved[BAM_SAVE_SLOTS];
/* The slots are on a memory card (else kept until the game is closed). */
extern int bam_store_on_card;

enum {
    BAM_STORE_CARD,    /* written to the memory card */
    BAM_STORE_SESSION, /* no memory card: kept until the game is closed */
    BAM_STORE_NOMEM,   /* not enough memory right now to use the card */
    BAM_STORE_ERROR,   /* the card refused (full, broken, ...) */
};

/* Reads the slots from the card once per boot (cheap without a card). */
void Bam_StoreInit(void);
/* Writes the slots to the card when there is one. */
int Bam_StoreSave(void);
#endif
