#include "font.h"

#include "../assets/exe.h"

#define FONT_SEG   0x26e6
#define FONT_FIRST 0x20
#define FONT_COUNT 0x5b         /* ' ' .. 'z'; anything else draws as a space */

static const uint8_t *glyphs;   /* FONT_COUNT * 9 bytes: width, 8 row bitmaps */

int font_init(void)
{
    glyphs = exe_ptr(FONT_SEG, 6, FONT_COUNT * 9);
    return glyphs ? 0 : -1;
}

static const uint8_t *glyph(char c)
{
    int i = (unsigned char)c - FONT_FIRST;
    if (i < 0 || i >= FONT_COUNT) i = 0;
    return glyphs + i * 9;
}

int font_char_width(char c) { return glyphs ? glyph(c)[0] : 0; }

int font_text_width(const char *s)
{
    int w = 0;
    while (*s) w += font_char_width(*s++);
    return w;
}

int font_draw(Video *v, int x, int y, const char *s, uint8_t fg, uint8_t bg)
{
    if (!glyphs) return x;
    for (; *s; s++) {
        const uint8_t *g = glyph(*s);
        int w = g[0];
        if (x + w > SCREEN_W) break;            /* the original stops at the right edge */
        for (int row = 0; row < 8; row++) {
            int py = y + row;
            if (py < 0 || py >= SCREEN_H) continue;
            for (int col = 0; col < w; col++) {
                int px = x + col;
                if (px < 0) continue;
                v->fb[py * SCREEN_W + px] = (g[1 + row] & (0x80 >> col)) ? fg : bg;
            }
        }
        x += w;
    }
    return x;
}
