/* Intro sequence from SR.EXE (1a21:0e82): logo, title, credits. */
#include "../game.h"

typedef struct { int picture, x, y, clear, pause, wait; } IntroStep;
static const IntroStep steps[] = {
    { 3, 40, 40, 1, 0, 400 },   /* California Dreams presents */
    { 1, 0, 0, 0, 0, 400 },     /* title screen */
    { 4, 0, 0, 0, 20, 600 },    /* WANTED credits */
};
#define STEPS (int)(sizeof steps / sizeof steps[0])

static int step;
static Uint64 step_start;

static void show(Game *g)
{
    if (steps[step].clear) video_clear(&g->video, 0);
    game_draw_picture(g, steps[step].picture, steps[step].x, steps[step].y);
    step_start = SDL_GetTicks();
}

static void next(Game *g)
{
    if (++step < STEPS) {
        show(g);
    } else {
        video_clear(&g->video, 0);
        /* The original asks the manual copy-protection question here; the rebuild skips it. */
        game_set_scene(g, &scene_license);
    }
}

static void enter(Game *g)
{
    game_set_palette(g, PAL_STANDARD);
    step = 0;
    show(g);
}

static void key(Game *g, SDL_Scancode sc, SDL_Keycode k)
{
    (void)sc;
    if ((k == SDLK_RETURN || k == SDLK_ESCAPE) && game_ticks_since(step_start) >= steps[step].pause) next(g);
}

static void click(Game *g, int x, int y)
{
    (void)x; (void)y;
    if (game_ticks_since(step_start) >= steps[step].pause) next(g);
}

static void update(Game *g)
{
    if (game_ticks_since(step_start) >= steps[step].pause + steps[step].wait) next(g);
}

const Scene scene_intro = { enter, key, NULL, NULL, click, update };
