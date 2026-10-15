/* Shared game state and the scene interface. */
#ifndef SR_GAME_H
#define SR_GAME_H

#include <SDL3/SDL.h>

#include "assets/lib.h"
#include "car.h"
#include "ui/dialog.h"
#include "video.h"

/* The game's timer runs at 1193182/16384 = 72.8 Hz; waits in SR.EXE are counted in these ticks. */
#define TICK_HZ (1193182.0 / 16384.0)

/* Palette tables in SR.EXE's data segment. */
#define PAL_STANDARD 0x1da6
#define PAL_SCREENS  0x1dda     /* license, garage and most menu screens */
#define PAL_INCAR    0x1dc8

typedef struct Game Game;

typedef struct {
    void (*enter)(Game *g);
    void (*key)(Game *g, SDL_Scancode sc, SDL_Keycode key);
    void (*text)(Game *g, const char *utf8);
    void (*mouse_move)(Game *g, int x, int y);
    void (*click)(Game *g, int x, int y);
    void (*update)(Game *g);
} Scene;

struct Game {
    Video        video;
    Lib          lib[3];
    const Scene *scene;
    const Scene *next_scene;
    int          quit;
    char         data_dir[1024];
    Player       player;

    /* Speech-bubble message overlay (1001:3db4). */
    int          msg;           /* message number, 0 = none */
    Uint64       msg_start;
    char         msg_line1[64]; /* replaces line 1 when set (the original sprintf'd into the table) */

    const char  *shot_path;
    Uint64       shot_at;
    int          shot_key_at;
    const char  *record_path;
};

extern const Scene scene_intro, scene_license, scene_garage, scene_newspaper;

void game_set_scene(Game *g, const Scene *s);
const Image *game_picture(Game *g, int id);          /* SR.EXE picture ids: 1.., 1000.., 2000.. */
void game_draw_picture(Game *g, int id, int x, int y);
void game_set_palette(Game *g, uint16_t ds_table);
/* Screen palette with a car's paint in registers 6/7 (1001:672c); colour < 0 leaves them as is. */
void game_set_paint(Game *g, int colour);
double game_ticks_since(Uint64 ms);

/* Show message n from SR.EXE's message table in a speech bubble (blocks scene input until dismissed). */
void game_message(Game *g, int n);
void game_draw_message(Game *g);
void game_message_short(Game *g, long amount);  /* message 1: "No go, hot shot. You're $X short." */

#endif
