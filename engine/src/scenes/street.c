/* Burger's Bungalow (1001:a626, dialog 20): you sit in your car at the drive-in and the rivals
 * cruise in. Rivals arrive on a timer (999a), park next to you (953c / 2a21:89fa), and leave again
 * when you let them wait. "Look for opponent" calls one you've met (9be0, dialog 16), "Pop the hood"
 * shows their engine (a3ba), and a click on their car is a challenge (9e3c, dialogs 19/17/18).
 * The race itself (1001:b584) is not rebuilt yet. */
#include <string.h>

#include "../assets/exe.h"
#include "../data.h"
#include "../game.h"
#include "../rivals.h"
#include "../ui/listbox.h"
#include "race.h"

#define BUNGALOW     0x431      /* 1073: the drive-in at night, your door at the bottom */
#define SIGN         0x47d      /* 1149: the "look for opponent" sign */
#define PARK_BOTTOM  0xa8       /* bottom row of a parked rival's car */
#define CUT_ROWS     8          /* DS:1bde[2]: rows of it the street doesn't show */
#define DRIVE_SPEED  160.0      /* pixels per second driving in or out */
#define FIRST_WAIT   0x8c       /* ticks before the first car turns up */
#define WAIT         0x118      /* ticks between events */
#define HOOD_TICKS   800        /* how long the rival lets you look (3d80) */

enum { S_EMPTY, S_ARRIVING, S_PARKED, S_LEAVING };
enum { N_NONE, N_TYPE, N_BET };

static Dialog dlg, neg;
static ListBox looking;
static int looking_rival[RIVAL_COUNT];
static int state, rival = -1, next_rival = -1;
static int park_x, car_w;
static Uint64 move_start, wait_start;
static double wait_ticks;
static int looked;              /* "Pop the hood" used for this rival (iVar9 in a626) */
static int hood_view;           /* showing the rival's engine */
static Uint64 hood_start;
static Car hood_car;
static int negotiating, race_type, tries, last_refusal = -1;
static Uint64 neg_start;
static char status[64];
static int race_pending = -1, race_stake;   /* the race starts once his answer has been read */

static void wait_for(double ticks) { wait_start = SDL_GetTicks(); wait_ticks = ticks; }

static int timed_out(void) { return game_ticks_since(wait_start) >= wait_ticks; }

/* 953c: the status line names the driver and the car ("Bubba  '57 Chevy"). */
static void set_status(const Player *p)
{
    if (rival < 0) { status[0] = 0; return; }
    SDL_snprintf(status, sizeof status, "%s%s%s", rival_name(rival), exe_ds_str(0x5030),
                 g_models[rival_model(p, rival)].ad);
    size_t n = strlen(status);
    if (n && status[n - 1] == '.') status[n - 1] = 0;
}

static void arrive(Game *g, int r)
{
    rival = r;
    rivals_meet(&g->player, r);
    car_w = car_body_width(g, rival_model(&g->player, r));
    park_x = ((320 - car_w) / 2 + 4) & ~7;
    state = S_ARRIVING;
    move_start = SDL_GetTicks();
    looked = 0;
    set_status(&g->player);
}

static void leave(void)
{
    if (rival < 0) return;
    state = S_LEAVING;
    move_start = SDL_GetTicks();
}

static void place_hotspots(Game *g)
{
    int parked = state == S_PARKED && !negotiating && !hood_view;
    const uint8_t *h = rival >= 0 ? g_models[rival_model(&g->player, rival)].hot : NULL;
    const Image *body = rival >= 0 ? game_picture(g, g_models[rival_model(&g->player, rival)].picture) : NULL;
    int top = body ? PARK_BOTTOM - body->h + 1 : 0;
    for (int i = 0; i < dlg.count; i++) {
        Widget *w = &dlg.w[i];
        if (w->id == 2 || w->id == 3) {
            if (!parked || !h) { w->flags &= (uint16_t)~WF_ENABLED; continue; }
            int k = w->id == 2 ? 1 : 3;                     /* 1001:a270: roof box = challenge, hood box = hood */
            int x1 = h[4 * k] + park_x, y1 = h[4 * k + 1] + top, x2 = x1 + h[4 * k + 2];
            dialog_set_rect(&dlg, i, x1, y1, x2, w->id == 2 ? 160 : y1 + h[4 * k + 3]);
        }
    }
}

static void enter(Game *g)
{
    game_set_palette(g, PAL_SCREENS);
    dialog_load(&dlg, 20);
    state = S_EMPTY;
    rival = next_rival = -1;
    race_pending = -1;
    hood_view = negotiating = 0;
    looking.active = 0;
    status[0] = 0;
    wait_for(FIRST_WAIT);
}

/* -------------------------------------------------------------------- challenge (1001:9e3c) */

static void end_negotiation(void) { negotiating = N_NONE; }

static void race_agreed(Game *g, int stake)
{
    (void)g;
    end_negotiation();
    race_pending = race_type;
    race_stake = stake;
}

static int fuel(Game *g) { Car *c = player_car(&g->player); return c ? c->fuel : 0; }

static void challenge(Game *g)
{
    Player *p = &g->player;
    if (rival == KING) {
        int prog = rivals_progress(p);
        if (prog == 1) { leave(); return; }
        if (prog) { game_message(g, (int16_t)exe_ds_u16((uint16_t)(0x5054 + 2 * prog))); leave(); return; }
        int stage = player_counter(p, 5);
        static const int type[3] = { RACE_DRAG, RACE_MULHOLLAND, RACE_AQUEDUCT }, need[3] = { 7, 17, 12 }, msg[3] = { 0x3e, 0x3f, 0x40 };
        if (stage > 2 || fuel(g) < need[stage]) { game_message(g, 0x41); return; }   /* "...more gas before you race." */
        race_type = type[stage];
        game_message(g, msg[stage]);
        race_agreed(g, stage == 2 ? 5 : 0);
        return;
    }
    if (fuel(g) < 7) { game_message(g, 0x41); return; }
    if (!car_runnable(player_car(p), 1)) { game_message(g, 39); return; }   /* "Your car's not running, speedy!" */
    dialog_load(&neg, 19);
    negotiating = N_TYPE;
    neg_start = SDL_GetTicks();
}

static void choose_type(Game *g, int id)
{
    if (id == -23) { end_negotiation(); leave(); return; }      /* Forget it */
    race_type = id == 1 ? RACE_DRAG : id == 2 ? RACE_MULHOLLAND : id == 3 ? RACE_AQUEDUCT : 0;
    if (!race_type) return;
    if ((race_type == RACE_MULHOLLAND && fuel(g) <= 16) || (race_type == RACE_AQUEDUCT && fuel(g) <= 11)) {
        game_message(g, 0x41);
        end_negotiation();
        leave();
        return;
    }
    dialog_load(&neg, race_type == RACE_DRAG ? 17 : 18);
    negotiating = N_BET;
    tries = 0;
    last_refusal = -1;
    neg_start = SDL_GetTicks();
}

static void choose_bet(Game *g, int id)
{
    Player *p = &g->player;
    if (id == -23) { end_negotiation(); leave(); return; }
    static const int ids[6] = { 1, 2, 3, 7, 8, 9 }, money[6] = { 0, 10, 50, 25, 150, 0 };
    int stake = -1;
    for (int k = 0; k < 6; k++) if (ids[k] == id) stake = k;
    if (stake < 0) return;
    if (p->money < money[stake]) {
        game_message(g, 0x42);                                  /* "You don't have enough dough for that bet!" */
    } else if (rivals_accepts_bet(p, rival, stake)) {
        game_message(g, (int16_t)exe_ds_u16((uint16_t)(0x504c + 2 * ((race_type != RACE_DRAG) * 3 + SDL_rand(3)))));
        race_agreed(g, stake);
        return;
    } else {
        int k;
        do k = SDL_rand(5); while (k == last_refusal);
        last_refusal = k;
        game_message(g, (int16_t)exe_ds_u16((uint16_t)(0x5038 + 2 * ((race_type != RACE_DRAG) * 5 + k))));
    }
    neg_start = SDL_GetTicks();
    if (++tries >= 2) { end_negotiation(); leave(); }           /* two tries, then he's had enough */
}

/* -------------------------------------------------------------------- input */

static void open_looking(Game *g)
{
    Player *p = &g->player;
    listbox_open(&looking, 16);
    int n = 0;
    for (int r = 0; r < RIVAL_COUNT; r++)
        if (rival_get(p, r, 0xb) == 1) {
            looking_rival[n] = r;
            SDL_strlcpy(looking.text[n++], rival_name(r), sizeof looking.text[0]);
        }
    looking.count = n;
    if (!n) { looking.active = 0; game_message(g, 0x29); }      /* "What's his name?" */
}

static void looked_up(Game *g, int id)
{
    if (id != -3 || !looking.count) return;                     /* Call */
    int r = looking_rival[looking.sel];
    if (rival >= 0) { next_rival = r; leave(); }                /* the one here drives off first */
    else arrive(g, r);
}

static void choose(Game *g, int i)
{
    if (i < 0) return;
    int id = dlg.w[i].id;
    if (id == 1) { game_set_scene(g, &scene_garage); return; }   /* Back to the garage */
    if (state == S_ARRIVING || state == S_LEAVING) return;
    if (id == 4) open_looking(g);
    else if (id == 3 && rival >= 0) {
        if (looked > 0 || rival == KING) { game_message(g, 0x43); return; }
        car_make_stock(&hood_car, rival_model(&g->player, rival), 0, 0);
        hood_view = 1;
        hood_start = SDL_GetTicks();
        looked++;
        g->player.time_actions += 0x111;
    } else if (id == 2 && rival >= 0) {
        challenge(g);
    }
}

static void key(Game *g, SDL_Scancode sc, SDL_Keycode k)
{
    (void)k;
    if (hood_view) { hood_view = 0; wait_for(WAIT); return; }
    if (looking.active) { looked_up(g, listbox_key(&looking, sc)); return; }
    if (negotiating) {
        int i = dialog_key(&neg, sc);
        if (i >= 0) (negotiating == N_TYPE ? choose_type : choose_bet)(g, neg.w[i].id);
        return;
    }
    choose(g, dialog_key(&dlg, sc));
}

static void click(Game *g, int x, int y)
{
    if (hood_view) { hood_view = 0; wait_for(WAIT); return; }
    if (looking.active) { looked_up(g, listbox_click(&looking, x, y)); return; }
    if (negotiating) {
        int i = dialog_click(&neg, x, y);
        if (i >= 0) (negotiating == N_TYPE ? choose_type : choose_bet)(g, neg.w[i].id);
        return;
    }
    choose(g, dialog_click(&dlg, x, y));
}

static void mouse_move(Game *g, int x, int y)
{
    (void)g;
    if (looking.active) listbox_mouse_move(&looking, x, y);
    else if (negotiating) dialog_mouse_move(&neg, x, y);
    else dialog_mouse_move(&dlg, x, y);
}

/* -------------------------------------------------------------------- time and drawing */

static void step(Game *g)
{
    Player *p = &g->player;
    if (race_pending >= 0 && !g->msg) {                         /* 9e3c: message first, then be82 */
        static const int amount[6] = { 0, 10, 50, 25, 150, 0 };
        int t = race_pending;
        race_pending = -1;
        race_start(g, rival, t, race_stake, amount[race_stake]);
        return;
    }
    double moved = (SDL_GetTicks() - move_start) * DRIVE_SPEED / 1000.0;
    if (state == S_ARRIVING && moved >= park_x + car_w) {
        state = S_PARKED;
        wait_for(WAIT);
        place_hotspots(g);
        if (SDL_getenv("SR2_DEBUG"))
            for (int i = 0; i < dlg.count; i++)
                SDL_Log("street hotspot %d: %d,%d - %d,%d", dlg.w[i].id, dlg.w[i].x1, dlg.w[i].y1, dlg.w[i].x2, dlg.w[i].y2);
    } else if (state == S_LEAVING && park_x + moved >= 320) {
        state = S_EMPTY;
        rival = -1;
        status[0] = 0;
        if (next_rival >= 0) { arrive(g, next_rival); next_rival = -1; }
        else wait_for(WAIT);
    }
    if (hood_view && game_ticks_since(hood_start) >= HOOD_TICKS) { hood_view = 0; wait_for(WAIT); }
    if (negotiating && game_ticks_since(neg_start) >= 800) {    /* he won't wait forever */
        game_message(g, 0x43);
        end_negotiation();
        leave();
    }
    if (hood_view || negotiating || looking.active || g->msg || !timed_out()) return;
    if (state == S_EMPTY) {
        arrive(g, rivals_pick_arrival(p, rivals_king_chance(p, rival)));
    } else if (state == S_PARKED) {
        leave();                                                /* nobody bothered him: off he goes */
    }
}

static void draw_rival(Game *g)
{
    if (rival < 0) return;
    Player *p = &g->player;
    double moved = (SDL_GetTicks() - move_start) * DRIVE_SPEED / 1000.0;
    int x = park_x, rolling = 0;
    if (state == S_ARRIVING) { x = (int)(moved - car_w); if (x > park_x) x = park_x; rolling = 1; }
    if (state == S_LEAVING) { x = park_x + (int)moved; rolling = 1; }
    int model = rival_model(p, rival), mods = rival_get(p, rival, 0xd) & 0xff;
    int body = (mods & 1 ? BODY_FRONT_STRIPPED : 0) | (mods & 2 ? BODY_REAR_STRIPPED : 0) | (mods & 4 ? BODY_ROOF_CHOPPED : 0);
    int tires = (g_models[model].spec1 & 0x30) >> 4;
    game_set_paint(g, rival == KING ? g_models[model].colour : rival_get(p, rival, 0xa));
    /* 2a21:89fa places the car's bottom at 0xa8 but leaves its last DS:1bde[2] = 8 rows undrawn at
     * the street, so the wheels end above the window sill. */
    static uint8_t keep[CUT_ROWS * SCREEN_W];
    uint8_t *below = &g->video.fb[(PARK_BOTTOM - CUT_ROWS + 1) * SCREEN_W];
    memcpy(keep, below, sizeof keep);
    car_draw_ex(&g->video, g, model, body, (mods & 0x20) != 0, tires, rolling ? ((int)moved / 6) & 3 : 0, x, PARK_BOTTOM);
    memcpy(below, keep, sizeof keep);
}

static void update(Game *g)
{
    Video *v = &g->video;
    step(g);
    game_set_palette(g, PAL_SCREENS);
    video_clear(v, 0);
    game_draw_picture(g, BUNGALOW, 0, 0);
    game_draw_picture(g, SIGN, 0x122, 0x57);
    draw_rival(g);
    if (hood_view) {
        hood_draw_car(g, &hood_car);
        game_draw_status_bar(g, status);
        return;
    }
    place_hotspots(g);
    const Widget *h = dlg.hover >= 0 ? &dlg.w[dlg.hover] : NULL;
    const char *text = looking.active ? listbox_hover_text(&looking)
                     : negotiating ? NULL
                     : h && (h->flags & WF_ENABLED) ? h->text : NULL;
    game_draw_status_bar(g, text ? text : status);
    if (!negotiating && !looking.active) dialog_draw(v, &dlg);
    if (negotiating) dialog_draw(v, &neg);
    if (looking.active) listbox_draw(v, &looking);
}

void street_summon(Game *g, int r)
{
    if (g->scene != &scene_street || r < 0 || r >= RIVAL_COUNT) return;
    if (rival >= 0) { next_rival = r; leave(); }
    else arrive(g, r);
}

const Scene scene_street = { enter, key, NULL, mouse_move, click, update };
