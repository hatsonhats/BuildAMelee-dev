/* Text drawn by the menus: one HSD text object per panel, lines and
 * boxes in screen units.
 *
 * Called from: css_codes.c, css_draw.c, training.c.
 * State: none.
 */
#include "ui_text.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <printf.h>

void BamText_Create(BamText* text, int font, int canvas, void* storage, unsigned capacity)
{
    text->native = HSD_SisLib_803A6754(font, canvas);
    /* Reserve the text's SIS stream once; repeated growth fragments the
     * scene's small SIS pool. */
    {
        SisBlock* buffer = text->native->alloc_data;
        HSD_SisLib_Free(buffer->data);
        /* The stream lives in our own memory, not the scene's SIS pool (the
         * CSS pool is 9 KB and nearly full). HSD_SisLib_Free ignores
         * pointers it did not hand out, so the text's destroy is safe. */
        buffer->data = storage;
        buffer->next = (SisBlock*) buffer->data;
        buffer->size = capacity;
        text->native->sis_buffer = (SIS*) buffer->data;
        *(unsigned char*) buffer->data = 0;
    }
    text->native->font_size.x = text->native->font_size.y = 0.6f;
    text->native->default_kerning = 1;
    text->count = text->used = 0;
}

void BamText_Begin(BamText* text)
{
    HSD_SisLib_803A7664(text->native);
    text->used = text->count = 0;
}

void BamText_Line(BamText* text, float x, float y, const char* fmt, ...)
{
    char ascii[96], encoded[128];
    unsigned i, out = 0;
    va_list args;
    va_start(args, fmt);
    vsnprintf(ascii, sizeof(ascii), fmt, args);
    va_end(args);
    ascii[sizeof(ascii) - 1] = 0;
    /* SIS expects Shift-JIS for punctuation outside its ASCII subset. */
    for (i = 0; ascii[i] && out < sizeof(encoded) - 3; ++i) {
        unsigned code = 0;
        switch (ascii[i]) {
        case '%': code = 0x93; break;
        case '/': code = 0x5e; break;
        case '(': code = 0x69; break;
        case ')': code = 0x6a; break;
        case '>': code = 0x84; break;
        case '<': code = 0x83; break;
        case '+': code = 0x7b; break;
        case '!': code = 0x49; break;
        case ':': code = 0x46; break;
        case '-': code = 0x7c; break;
        case '.': code = 0x44; break;
        case '_': code = 0x51; break;
        case ',': code = 0x43; break;
        case '\'': code = 0x66; break;
        }
        if (code) { encoded[out++] = (char) 0x81; encoded[out++] = (char) code; }
        else encoded[out++] = ascii[i];
    }
    encoded[out] = 0;
    if (text->used == text->count) {
        HSD_SisLib_803A6B98(text->native, x / 0.6f, y / 0.6f, "%s", encoded);
        text->count++;
    } else {
        HSD_SisLib_803A70A0(text->native, text->used, "%s", encoded);
        HSD_SisLib_803A746C(text->native, text->used, x / 0.6f, y / 0.6f);
    }
    HSD_SisLib_803A7548(text->native, text->used, 1.0f, 1.0f);
    { GXColor white = { 235, 238, 249, 255 }; HSD_SisLib_803A74F0(text->native, text->used, &white); }
    text->used++;
}

void BamText_End(BamText* text)
{
    unsigned i;
    for (i = text->used; i < text->count; ++i)
        HSD_SisLib_803A70A0(text->native, i, "%s", "");
}

extern HSD_Text* HSD_SisLib_804D7978; /* every live SIS text */

int BamText_Alive(const BamText* text)
{
    HSD_Text* t;
    if (!text->native) return 0;
    for (t = HSD_SisLib_804D7978; t; t = t->next)
        if (t == text->native) return 1;
    return 0;
}

void BamText_Destroy(BamText* text)
{
    /* The scene may already have freed every text (Slippi's direct-code
     * entry resets SIS on the CSS); destroying ours again hung the game. */
    if (BamText_Alive(text)) HSD_SisLib_803A5CC4(text->native);
    text->native = NULL;
    text->count = text->used = 0;
}

void BamText_Style(BamText* text, float scale, unsigned rgb)
{
    GXColor color;
    color.r = (u8) (rgb >> 16); color.g = (u8) (rgb >> 8); color.b = (u8) rgb; color.a = 255;
    HSD_SisLib_803A7548(text->native, text->used - 1, scale, scale);
    HSD_SisLib_803A74F0(text->native, text->used - 1, &color);
}

void BamText_Box(BamText* t, int font, int canvas, void* storage, float x, float y, float w, float h, unsigned rgb, unsigned alpha)
{
    BamText_Create(t, font, canvas, storage, 128);
    BamText_Begin(t); BamText_End(t);
    t->native->font_size.x = t->native->font_size.y = 1;
    t->native->pos_x = x; t->native->pos_y = y;
    t->native->box_size_x = w; t->native->box_size_y = h;
    t->native->bg_color.r = (u8) (rgb >> 16); t->native->bg_color.g = (u8) (rgb >> 8);
    t->native->bg_color.b = (u8) rgb; t->native->bg_color.a = (u8) alpha;
}
