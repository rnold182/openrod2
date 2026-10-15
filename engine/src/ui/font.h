/* The game's proportional 8-pixel font, read from SR.EXE (segment 0x26e6). */
#ifndef SR_FONT_H
#define SR_FONT_H

#include "../video.h"

int  font_init(void);                        /* after exe_load() */
int  font_char_width(char c);
int  font_text_width(const char *s);
/* Draws opaque glyph cells (fg on bg, 8 rows) like the original; returns the end x. */
int  font_draw(Video *v, int x, int y, const char *s, uint8_t fg, uint8_t bg);

#endif
