#include "video.h"

#include <string.h>

const uint8_t EGA_RGB[16][3] = {
    { 0, 0, 0 },      { 0, 0, 170 },    { 0, 170, 0 },    { 0, 170, 170 },
    { 170, 0, 0 },    { 170, 0, 170 },  { 170, 85, 0 },   { 170, 170, 170 },
    { 85, 85, 85 },   { 85, 85, 255 },  { 85, 255, 85 },  { 85, 255, 255 },
    { 255, 85, 85 },  { 255, 85, 255 }, { 255, 255, 85 }, { 255, 255, 255 },
};

int video_init(Video *v, const char *title)
{
    memset(v, 0, sizeof *v);
    /* 320x200 on a 4:3 CRT: pixels are 1.2x taller than wide, so 320x240 logical. */
    if (!SDL_CreateWindowAndRenderer(title, 1280, 960, SDL_WINDOW_RESIZABLE, &v->window, &v->renderer))
        return -1;
    SDL_SetRenderVSync(v->renderer, 1);
    SDL_SetRenderLogicalPresentation(v->renderer, 320, 240, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    v->texture = SDL_CreateTexture(v->renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                   SCREEN_W, SCREEN_H);
    if (!v->texture) return -1;
    SDL_SetTextureScaleMode(v->texture, SDL_SCALEMODE_NEAREST);
    for (int i = 0; i < 16; i++) v->palette[i] = (uint8_t)i;
    return 0;
}

void video_shutdown(Video *v)
{
    if (v->texture) SDL_DestroyTexture(v->texture);
    if (v->renderer) SDL_DestroyRenderer(v->renderer);
    if (v->window) SDL_DestroyWindow(v->window);
}

void video_present(Video *v)
{
    uint32_t lut[16];
    for (int i = 0; i < 16; i++) {
        const uint8_t *c = EGA_RGB[v->palette[i] & 15];
        lut[i] = (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
    }
    void *pixels;
    int pitch;
    if (SDL_LockTexture(v->texture, NULL, &pixels, &pitch)) {
        for (int y = 0; y < SCREEN_H; y++) {
            uint32_t *dst = (uint32_t *)((uint8_t *)pixels + y * pitch);
            const uint8_t *src = v->fb + y * SCREEN_W;
            for (int x = 0; x < SCREEN_W; x++) dst[x] = lut[src[x] & 15];
        }
        SDL_UnlockTexture(v->texture);
    }
    SDL_SetRenderDrawColor(v->renderer, 0, 0, 0, 255);
    SDL_RenderClear(v->renderer);
    SDL_FRect dst = { 0, 0, 320, 240 };
    SDL_RenderTexture(v->renderer, v->texture, NULL, &dst);
    SDL_RenderPresent(v->renderer);
}

void video_toggle_fullscreen(Video *v)
{
    int fs = (SDL_GetWindowFlags(v->window) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(v->window, !fs);
}

void video_clear(Video *v, uint8_t colour) { memset(v->fb, colour, sizeof v->fb); }

void video_set_palette(Video *v, const uint8_t regs[16]) { memcpy(v->palette, regs, 16); }

void video_fill(Video *v, int x, int y, int w, int h, uint8_t colour)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    for (int j = 0; j < h; j++) memset(v->fb + (y + j) * SCREEN_W + x, colour, (size_t)(w > 0 ? w : 0));
}

void video_blit_part(Video *v, const Image *img, int sx, int sy, int w, int h, int x, int y)
{
    if (!img) return;
    int transparent = (img->flags & IMAGE_SPRITE) != 0;
    for (int j = 0; j < h; j++) {
        int dy = y + j, iy = sy + j;
        if (dy < 0 || dy >= SCREEN_H || iy < 0 || iy >= img->h) continue;
        for (int i = 0; i < w; i++) {
            int dx = x + i, ix = sx + i;
            if (dx < 0 || dx >= SCREEN_W || ix < 0 || ix >= img->w) continue;
            uint8_t c = img->pixels[iy * img->w + ix];
            if (transparent && c == 0) continue;
            v->fb[dy * SCREEN_W + dx] = c;
        }
    }
}

void video_blit(Video *v, const Image *img, int x, int y)
{
    if (img) video_blit_part(v, img, 0, 0, img->w, img->h, x, y);
}

int video_save_bmp(const Video *v, const char *path)
{
    SDL_Surface *s = SDL_CreateSurface(SCREEN_W, SCREEN_H, SDL_PIXELFORMAT_XRGB8888);
    if (!s) return -1;
    for (int y = 0; y < SCREEN_H; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch);
        for (int x = 0; x < SCREEN_W; x++) {
            const uint8_t *c = EGA_RGB[v->palette[v->fb[y * SCREEN_W + x] & 15] & 15];
            row[x] = (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
        }
    }
    int ok = SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
    return ok ? 0 : -1;
}
