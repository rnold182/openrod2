/* New game: the California driver license where you type your name (1001:550e, dialog 2). */
#include <string.h>

#include "../game.h"
#include "../ui/font.h"

#define LICENSE_PICTURE 1002
#define NAME_MAX 15

static Dialog dlg;
static int field = -1;
static char name[NAME_MAX + 1];

static void redraw(Game *g)
{
    video_clear(&g->video, 0);
    game_draw_picture(g, LICENSE_PICTURE, 16, 6);
    char shown[NAME_MAX + 2];
    SDL_snprintf(shown, sizeof shown, "%s%s", name, (SDL_GetTicks() / 300) % 2 ? "_" : " ");
    dialog_set_text(&dlg, field, shown);
    dialog_draw(&g->video, &dlg);
}

static void start_game(Game *g)
{
    size_t n = strlen(name);
    while (n > 0 && name[n - 1] == ' ') name[--n] = 0;     /* 1001:54ba trims trailing spaces */
    player_new(&g->player, name);                           /* 1001:5400: $1200 to start */
    SDL_StopTextInput(g->video.window);
    game_set_scene(g, &scene_garage);
}

static void enter(Game *g)
{
    game_set_palette(g, PAL_SCREENS);
    dialog_load(&dlg, 2);
    field = -1;
    for (int i = 0; i < dlg.count; i++) if (dlg.w[i].type == W_FIELD) field = i;
    name[0] = 0;
    SDL_StartTextInput(g->video.window);
}

static void choose(Game *g, int i)
{
    if (i < 0) return;
    if (dlg.w[i].id == -3) start_game(g);                   /* OK */
    else if (dlg.w[i].id == -2) SDL_Log("license: Old Game (load screen) not rebuilt yet");
}

static void key(Game *g, SDL_Scancode sc, SDL_Keycode k)
{
    if (k == SDLK_RETURN) { start_game(g); return; }
    if (k == SDLK_BACKSPACE) {
        size_t n = strlen(name);
        if (n) name[n - 1] = 0;
        return;
    }
    choose(g, dialog_key(&dlg, sc));
}

static void text(Game *g, const char *s)
{
    (void)g;
    for (; *s; s++) {
        size_t n = strlen(name);
        if (n >= NAME_MAX) break;
        if (*s >= ' ' && *s <= 'z') { name[n] = *s; name[n + 1] = 0; }   /* characters the font has */
    }
}

static void mouse_move(Game *g, int x, int y) { (void)g; dialog_mouse_move(&dlg, x, y); }
static void click(Game *g, int x, int y) { choose(g, dialog_click(&dlg, x, y)); }
static void update(Game *g) { redraw(g); }

const Scene scene_license = { enter, key, text, mouse_move, click, update };
