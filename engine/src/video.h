/* Emulated EGA display: a 320x200 indexed framebuffer plus 16 palette registers,
 * presented through SDL with 4:3 aspect correction. */
#ifndef SR_VIDEO_H
#define SR_VIDEO_H

#include <stdint.h>
#include <SDL3/SDL.h>

#include "assets/lib.h"

#define SCREEN_W 320
#define SCREEN_H 200

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    uint8_t       fb[SCREEN_W * SCREEN_H];
    uint8_t       palette[16];      /* palette register -> EGA colour 0..15 */
    int           clip_bottom;      /* text is not drawn at or below this row */
} Video;

extern const uint8_t EGA_RGB[16][3];

int  video_init(Video *v, const char *title);
void video_shutdown(Video *v);
void video_present(Video *v);
void video_toggle_fullscreen(Video *v);
int  video_save_bmp(const Video *v, const char *path);  /* 320x200 frame, current palette */

void video_clear(Video *v, uint8_t colour);
void video_set_palette(Video *v, const uint8_t regs[16]);
void video_blit(Video *v, const Image *img, int x, int y);
void video_fill(Video *v, int x, int y, int w, int h, uint8_t colour);   /* clipped */
/* Blit a sub-rectangle of an image (sprite sheets). */
void video_blit_part(Video *v, const Image *img, int sx, int sy, int w, int h, int x, int y);

#endif
