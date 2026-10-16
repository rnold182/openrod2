/* The King is beaten on the Aqueduct (1001:a626 -> a51c): his farewell text (picture 1083) for
 * 0x2ee ticks, then the SPECIAL music and the animation of 3925:27da, played from SR2_END: 84
 * frame rectangles in the sheet 1181 (w, h, x word, y, -) and 183 steps (frame, x, y, delay) over
 * the picture 1211. A step with a delay shows the picture (every 10 ticks, 2a21:1924(10)) and the
 * next step first puts back the background where the sprites went; a negative delay also pastes a
 * part of 1195 into the background and draws the rest of the frames from 1195. A negative frame
 * ends it, after 0x50 retraces. Then MAIN plays again and a626 sets DS:1bc6 |= 2 - the game's
 * ending (1001:36ac), which is not rebuilt yet: the rebuild goes back to the garage. */
#include <string.h>

#include "../game.h"
#include "../sound.h"

#define TEXT_PIC   0x43b      /* 1083: "Well, you've done it..." */
#define BACKGROUND 0x4bb      /* 1211 */
#define SHEET      0x49d      /* 1181 */
#define SHEET2     0x4ab      /* 1195 */
#define FRAMES     84
#define STEPS      183

enum { K_TEXT, K_ANIM, K_END };

static uint8_t *end_data;
static uint8_t bg[SCREEN_W * SCREEN_H], canvas[SCREEN_W * SCREEN_H], shown[SCREEN_W * SCREEN_H];
static int phase, step, restore, sheet;
static int dx0, dy0, dx1, dy1;                          /* where sprites went since the last show */
static Uint64 phase_start, last_show;

static void put(uint8_t *dst, const Image *im, int sx, int sy, int w, int h, int x, int y, int sprite)
{
    if (!im) return;
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            int u = sx + c, v = sy + r, tx = x + c, ty = y + r;
            if (u < 0 || v < 0 || u >= im->w || v >= im->h || tx < 0 || ty < 0 || tx >= SCREEN_W || ty >= SCREEN_H) continue;
            uint8_t px = im->pixels[v * im->w + u];
            if (sprite && !px) continue;
            dst[ty * SCREEN_W + tx] = px;
        }
}

static void reset_dirty(void) { dx0 = dy0 = 1000; dx1 = dy1 = -1; }

static void enter(Game *g)
{
    game_set_palette(g, 0x1dec);                        /* a51c: 2a21:198a(0x1dec) */
    if (!end_data) {
        char path[1100];
        size_t len = 0;
        SDL_snprintf(path, sizeof path, "%s/SR2_END", g->data_dir);
        end_data = SDL_LoadFile(path, &len);
        if (end_data && len < FRAMES * 6 + STEPS * 4) { SDL_free(end_data); end_data = NULL; }
    }
    phase = K_TEXT;
    phase_start = SDL_GetTicks();
}

static void start_anim(Game *g)
{
    sound_play_playlist(PLAYLIST_SPECIAL);              /* sound vector 19 (DS:bbd8) */
    memset(bg, 0, sizeof bg);
    put(bg, game_picture(g, BACKGROUND), 0, 0, SCREEN_W, SCREEN_H, 0, 0, 0);
    memcpy(canvas, bg, sizeof bg);
    memcpy(shown, bg, sizeof bg);
    step = restore = 0;
    sheet = SHEET;
    reset_dirty();
    phase = end_data ? K_ANIM : K_END;
    phase_start = last_show = SDL_GetTicks();
}

static void finish(Game *g)
{
    sound_play_playlist(PLAYLIST_MAIN);                 /* sound vector 18 (DS:bbd4) */
    SDL_Log("king: the game's ending (1001:36ac) is not rebuilt yet");
    game_set_scene(g, &scene_garage);
}

/* Runs steps up to the next one that shows the picture. */
static void run_steps(Game *g)
{
    while (step < STEPS) {
        const uint8_t *s = end_data + FRAMES * 6 + step * 4;
        int frame = (int8_t)s[0];
        if (frame < 0) { phase = K_END; phase_start = SDL_GetTicks(); return; }
        if (restore) {                                  /* the background back where sprites were */
            for (int y = SDL_max(dy0, 0); y <= dy1 && y < SCREEN_H; y++)
                for (int x = SDL_max(dx0, 0); x <= dx1 && x < SCREEN_W; x++) canvas[y * SCREEN_W + x] = bg[y * SCREEN_W + x];
            reset_dirty();
            restore = 0;
        }
        const uint8_t *f = end_data + (frame < FRAMES ? frame : 0) * 6;
        int w = f[0], h = f[1], sx = f[2] | f[3] << 8, sy = f[4], x = s[1], y = s[2];
        put(canvas, game_picture(g, sheet), sx, sy, w, h, x, y, 1);
        dx0 = SDL_min(dx0, x & ~7);
        dy0 = SDL_min(dy0, y);
        dx1 = SDL_max(dx1, ((x + w + 7) & ~7) - 1);
        dy1 = SDL_max(dy1, y + h - 1);
        int delay = (int8_t)s[3];
        step++;
        if (delay != 0) {
            memcpy(shown, canvas, sizeof canvas);
            restore = 1;
            if (delay < 0) {                            /* from here on the frames come from 1195 */
                sheet = SHEET2;
                put(bg, game_picture(g, SHEET2), 0x60, 0x68, 0x88, 0x2e, 0x88, 0x92, 0);
                dx0 = 0x88; dy0 = 0x92; dx1 = 0x88 + 0x87; dy1 = 0x92 + 0x2d;
            }
            return;
        }
    }
    phase = K_END;
    phase_start = SDL_GetTicks();
}

static void skip(Game *g)
{
    if (phase == K_TEXT) start_anim(g);                 /* 3d80: a key or click ends the wait */
}

static void key(Game *g, SDL_Scancode sc, SDL_Keycode k) { (void)sc; (void)k; skip(g); }
static void click(Game *g, int x, int y) { (void)x; (void)y; skip(g); }

static void update(Game *g)
{
    Video *v = &g->video;
    game_set_palette(g, 0x1dec);
    if (phase == K_TEXT) {
        if (game_ticks_since(phase_start) >= 0x2ee) { start_anim(g); return; }
        video_fill(v, 0, 0, SCREEN_W, SCREEN_H, 8);     /* 21be: cleared to colour 8, 0x1e44 = 200 rows */
        game_draw_picture(g, TEXT_PIC, 8, 0x14);
        return;
    }
    if (phase == K_ANIM && game_ticks_since(last_show) >= 10) {
        last_show = SDL_GetTicks();
        run_steps(g);
    }
    memcpy(v->fb, shown, sizeof shown);
    if (phase == K_END && SDL_GetTicks() - phase_start >= 0x50 * 1000 / 70) finish(g);   /* 18e2(0x50) */
}

const Scene scene_king = { enter, key, NULL, NULL, click, update };
