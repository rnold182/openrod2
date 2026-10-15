/* The garage, the game's hub screen (1001:6912, dialog 0). */
#include "../game.h"
#include "../sound.h"

/* Hotspot ids from dialog 0. */
enum {
    G_NEWSPAPER = 1, G_STREET = 2, G_CALENDAR = 3, G_QUIT = 4, G_SELL_PARTS = 5, G_CARS = 6,
    G_PAINT = 7, G_TIRES = 8, G_TRANSMISSION = 9, G_HOOD = 10, G_FRONT_BUMPER = 11, G_REAR_BUMPER = 12,
    G_ROOF = 13, G_UNDER_CAR = 14, G_TANK = 15, G_CAR_INFO = 16, G_GRUDGE = 17, G_RADIO = -40,
};

/* Modal dialogs shown on top of the garage. */
enum { MODAL_NONE, MODAL_QUIT_MENU, MODAL_EXIT_DOS };

static Dialog dlg, modal;
static int modal_kind;

static void open_modal(int kind, int dialog)
{
    modal_kind = kind;
    dialog_load(&modal, dialog);
}

static void enter(Game *g)
{
    game_set_palette(g, PAL_SCREENS);
    g->video.palette[12] = 5;               /* car paint register; no car yet */
    dialog_load(&dlg, 0);
    modal_kind = MODAL_NONE;
}

static int needs_car(int id)
{
    return id == G_STREET || id == G_PAINT || id == G_TIRES || id == G_TRANSMISSION || id == G_HOOD ||
           id == G_FRONT_BUMPER || id == G_REAR_BUMPER || id == G_ROOF || id == G_UNDER_CAR || id == G_TANK ||
           id == G_CAR_INFO || id == G_GRUDGE;
}

static void choose(Game *g, int i)
{
    if (i < 0) return;
    int id = dlg.w[i].id;
    if (needs_car(id) && g->player.car < 0) {
        game_message(g, 9);                 /* "You've got no car, dummy!" */
        return;
    }
    switch (id) {
    case G_QUIT:
        open_modal(MODAL_QUIT_MENU, 1);
        break;
    case G_RADIO:
        sound_toggle_music();
        break;
    default:
        SDL_Log("garage: '%s' (id %d) not rebuilt yet", dlg.w[i].text, id);
        break;
    }
}

static void choose_modal(Game *g, int i)
{
    if (i < 0) return;
    int id = modal.w[i].id;
    if (modal_kind == MODAL_QUIT_MENU) {
        if (id == -20) open_modal(MODAL_EXIT_DOS, 35);                  /* Quit */
        else if (id == -1) game_set_scene(g, &scene_license);           /* New Game */
        else if (id == -22) modal_kind = MODAL_NONE;                    /* Forget it */
        else SDL_Log("quit menu: '%s' not rebuilt yet", modal.w[i].text);
    } else if (modal_kind == MODAL_EXIT_DOS) {
        if (id == -20) g->quit = 1;                                     /* Yes */
        else modal_kind = MODAL_NONE;
    }
}

static void key(Game *g, SDL_Scancode sc, SDL_Keycode k)
{
    (void)k;
    if (modal_kind) choose_modal(g, dialog_key(&modal, sc));
    else choose(g, dialog_key(&dlg, sc));
}

static void mouse_move(Game *g, int x, int y)
{
    (void)g;
    if (modal_kind) dialog_mouse_move(&modal, x, y);
    else dialog_mouse_move(&dlg, x, y);
}

static void click(Game *g, int x, int y)
{
    if (modal_kind) choose_modal(g, dialog_click(&modal, x, y));
    else choose(g, dialog_click(&dlg, x, y));
}

static void update(Game *g)
{
    video_clear(&g->video, 0);
    game_draw_picture(g, 1018, 0, 0);
    game_draw_picture(g, 1020, 0x90, 0xa3);
    game_draw_picture(g, 1198, 0x50, 0xac);
    dialog_draw(&g->video, &dlg);
    if (modal_kind) dialog_draw(&g->video, &modal);
}

const Scene scene_garage = { enter, key, NULL, mouse_move, click, update };
