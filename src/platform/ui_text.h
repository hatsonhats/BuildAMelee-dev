/* Text lines and filled boxes on a SIS (HSD text) canvas. */
#ifndef BAM_UI_TEXT_H
#define BAM_UI_TEXT_H
#include <sysdolphin/baselib/sislib.h>
typedef struct BamText { HSD_Text* native; unsigned count, used; } BamText;
/* storage: capacity bytes, 4-aligned, alive as long as the text. */
void BamText_Create(BamText* text, int font, int canvas, void* storage, unsigned capacity);
void BamText_Begin(BamText* text);
void BamText_Line(BamText* text, float x, float y, const char* fmt, ...);
void BamText_Style(BamText* text, float scale, unsigned rgb);
void BamText_End(BamText* text);
void BamText_Destroy(BamText* text);
/* Still in SIS's live text list (scenes can reset SIS under us). */
int BamText_Alive(const BamText* text);
/* A solid rectangle (an empty text with a background colour). */
void BamText_Box(BamText* t, int font, int canvas, void* storage, float x, float y, float w, float h, unsigned rgb, unsigned alpha);
#endif
