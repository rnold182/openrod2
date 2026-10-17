/* The gas station (3925:2104, dialog 22): after the drive through town your car pulls up between
 * the pumps (2a21:89fa mode 4, bottom 0xb9). Take a nozzle off the high octane (left, hook id 3)
 * or the regular pump (right, id 4), click the tank cap (id 1) to fill up at $1 a step, click the
 * cap again to take it out, hang the nozzle back up. The outer hooks (2, 5) are "another pump", the
 * labels (6, 7) want you to take a pump. Dawdle too long and the attendant comes out for full
 * service. You leave by hanging up after buying gas, or with "Back to the garage" (8); then the
 * car drives off (mode 2) and home through town (1bc8 1, 1001:8c68).
 *
 * The loop polls with 1001:1d02(7): a click, or nothing after 7 ticks. Pumping adds `rate` tenths
 * of a gallon and takes $1 every 8 polls; the pump's six lamps show the poll count (3925:1e08). */
#include <string.h>

#include "../assets/exe.h"
#include "../car.h"
#include "../data.h"
#include "../game.h"
#include "../sound.h"
#include "race.h"

#define STATION    0x430        /* 1072: Cheapskate Gas Co. */
#define ATTENDANT  0x47e        /* 1150: six frames 24 x 20 */
#define HOSES      0x3e8        /* 1000: the nozzles on their hooks, the hoses pulled off them */
#define NOZZLE_IN  0x456        /* 1110: the nozzle in the tank */
#define CAP        0x457        /* 1111: the tank cap */
#define CAR_BOTTOM 0xb9
#define CUT_ROWS   5            /* DS:1bde[3] */
#define BAND       25           /* DS:1bd0[3] */
#define FULL       0xb9
#define DRIVE_SPEED 160.0
#define POLL       7            /* ticks a 1d02(7) waits */

enum { ARRIVE, PUMPS, ATTEND, LEAVE };
enum { CAP_ID = 1, OUTER_L = 2, HOOK_HIGH = 3, HOOK_REG = 4, OUTER_R = 5, LABEL_REG = 6, LABEL_HIGH = 7, BACK = 8 };

static Dialog dlg;
static int phase;
static Uint64 phase_start, poll_start;
static int car_x, car_w, cap_x, cap_y;   /* DS:ad62 / ad64: the tank cap on the screen */
static int hand;                        /* bp-0x12: 1 the hand cursor, 2 holding a nozzle */
static int nozzle;                      /* bp-0xa: the nozzle off its hook (1 high octane, 2 regular) */
static int in_tank;                     /* bp-0xc */
static int pumping;                     /* bp-4 */
static int rate;                        /* bp-0x10: tenths of a gallon a step */
static int bought;                      /* bp-0x20 */
static int self_service;                /* bp-0xe: until the attendant comes */
static int fee;                         /* bp-6: $10 if he comes after you bought gas */
static int idle;                        /* bp-0x24: polls before the attendant comes */
static int polls;                       /* bp-0x28: polls of the current step */
static int count[3];                    /* DS:9160: polls pumped, per pump (1, 2) */
static int attend_frame;

static int blower_of(const Car *c) { return c->intake.part >= 0 && g_parts[c->intake.part].grade == 8; }
static long money(Game *g) { return g->player.money; }
static int fuel(Game *g) { Car *c = player_car(&g->player); return c ? c->fuel : 0; }

static void pay(Game *g, long dollars)  /* 2a21:1df4: no more than you have */
{
    if (g->player.money >= dollars) g->player.money -= dollars;
    else if (g->player.money > 0) g->player.money = 0;
}

/* 3925:1ab8 / 1ad2 / 1af0: hotspots off, one on, the cap's rectangle. */
static void all_off(void) { for (int i = 0; i < dlg.count; i++) dlg.w[i].flags &= ~WF_ENABLED; }
static void on(int id) { int i = dialog_find(&dlg, id); if (i >= 0) dlg.w[i].flags |= WF_ENABLED; }
static void cap_rect(int x, int y, int w, int h) { int i = dialog_find(&dlg, CAP_ID); if (i >= 0) dialog_set_rect(&dlg, i, x, y, x + w, y + h); }

static void pumps_on(void)              /* 1b28: everything but the cap */
{
    all_off();
    for (int id = OUTER_L; id <= BACK; id++) on(id);
}

static void cap_on(void)                /* 1b84: only the cap (a nozzle in the tank) */
{
    all_off();
    cap_rect(cap_x - 0xa, cap_y - 5, 0x12, 0xc);
}

static void holding_on(int n)           /* 1bba: the nozzle's hook and the cap */
{
    all_off();
    on(n == 1 ? HOOK_HIGH : HOOK_REG);
    cap_rect(cap_x - 1, cap_y - 1, 0xa, 9);
}

static void pump_sound(int on_)         /* 20be 1 / 3: sound vector 4 (2c57:0d7c) */
{
    if (on_) {
        sound_voice_timbre(4, "paint", 0x6a);
        sound_voice_note_off(4);
        sound_voice_note_on(4, 0x54);
    } else sound_voice_note_off(4);
}

static void enter(Game *g)
{
    dialog_load(&dlg, 22);
    all_off();
    Car *c = player_car(&g->player);
    const Image *body = c ? game_picture(g, g_models[c->model].picture) : NULL;
    car_w = body ? body->w : 0;
    car_x = ((0x140 - car_w) / 2 + 4) & ~7;                             /* 89fa */
    int top = body ? CAR_BOTTOM - body->h + 1 : CAR_BOTTOM;
    cap_x = c ? g_models[c->model].geo[10] + car_x : 0;                  /* 8546: DS:9bf4[model].10 / 11 */
    cap_y = c ? g_models[c->model].geo[11] + top : 0;
    if (SDL_getenv("SR2_DEBUG")) SDL_Log("gas: car x %d w %d, cap %d,%d", car_x, car_w, cap_x, cap_y);
    hand = 1;
    nozzle = in_tank = pumping = bought = polls = 0;
    self_service = fee = 1;
    idle = 0xa0;
    rate = 0x14;
    count[1] = count[2] = 0;
    attend_frame = -1;
    phase = ARRIVE;
    phase_start = SDL_GetTicks();
    if (c) drive_motor_start(g, blower_of(c));
}

static void leave(Game *g)               /* 3925:1f38 */
{
    if (pumping) pump_sound(0);
    if (in_tank) { in_tank = 0; if (fee) pay(g, 10); }                  /* the nozzle out of the tank */
    if (nozzle) { nozzle = 0; if (fee) pay(g, 10); }                    /* and back on its hook */
    Car *c = player_car(&g->player);
    if (c && c->fuel > 0xaa) c->fuel = 0xaa;
    count[1] = count[2] = 0;
    pumping = 0;
    hand = 1;
    all_off();
    phase = LEAVE;
    phase_start = SDL_GetTicks();
    if (c) drive_motor_start(g, blower_of(c));
}

/* The pump's lamps light up for each bit of its count (1e08): three in a row at y 134 and three
 * at 139, x 44 / 42 / 40 (the regular pump 0x9b further right); colour 4 lit, 5 dark. */
static void lamps(Game *g)
{
    static const int lx[6] = { 44, 42, 40, 44, 42, 40 }, ly[6] = { 134, 134, 134, 139, 139, 139 };
    static const int bit[6] = { 1, 4, 16, 2, 8, 32 };
    for (int p = 1; p <= 2; p++)
        for (int k = 0; k < 6; k++)
            video_fill(&g->video, lx[k] + (p == 2 ? 0x9b : 0), ly[k], 1, 1, count[p] & bit[k] ? 4 : 5);
}

static void put_nozzle(int n)           /* back on the hook */
{
    nozzle = 0;
    (void)n;
    hand = 1;
    pumps_on();
    rate = 0x14;
}

static void choose(Game *g, int id)     /* the switch at 3925:26f2 */
{
    switch (id) {
    case CAP_ID:
        if (hand == 1) {                                                /* 25fa: take it out of the tank */
            if (pumping) pump_sound(0);
            in_tank = 0;
            hand = 2;
            if (FULL - fuel(g) >= rate && money(g) > 0) game_message(g, 0x46);   /* "It's not full, speedy!" */
            holding_on(nozzle);
        } else {
            if (FULL - fuel(g) < rate || money(g) <= 0) {
                game_message(g, money(g) > 0 ? 0x25 : 0x26);           /* "You've got a full tank!" / "Gas ain't free!" */
                return;
            }
            in_tank = 1;                                                /* 26a6 */
            hand = 1;
            pump_sound(1);
            cap_on();
            pumping = 1;
            return;
        }
        break;
    case OUTER_L: case HOOK_HIGH: case HOOK_REG: case OUTER_R:
        if (hand == 1) {
            if (id == OUTER_L || id == OUTER_R) { game_message(g, 0x62); return; }   /* "Use another pump, dummy!" */
            nozzle = id == HOOK_HIGH ? 1 : 2;
            hand = 2;
            holding_on(nozzle);
            rate = id == HOOK_HIGH ? 0x14 : 0xf;
        } else {
            if (money(g) <= 0 || bought) { fee = 0; leave(g); return; }
            put_nozzle(nozzle);
            pumping = 0;
            return;
        }
        break;
    case LABEL_REG: case LABEL_HIGH:
        game_message(g, 0x64);                                          /* "Take a pump, dummy!" */
        return;
    case BACK:
        if (fuel(g) > 4) { fee = 0; leave(g); }
        else game_message(g, 0x63);                                     /* "Get some gas first, speedy!" */
        return;
    default:
        return;
    }
    pumping = 0;
}

/* One poll that found nothing (case 0). */
static void tick(Game *g)
{
    if (pumping) {
        count[nozzle]++;                                                /* 20be(2) */
        if (++polls < 8) return;
        polls = 0;
        Car *c = player_car(&g->player);
        if (c) c->fuel += rate;
        bought = 1;
        pay(g, 1);
        if (FULL - fuel(g) >= rate && money(g) > 0) return;
        pump_sound(0);
        if (self_service) { pumping = 0; return; }
        leave(g);
        return;
    }
    if (--idle > 0) return;
    all_off();                                                          /* 1c06: the attendant takes over */
    rate = nozzle == 2 ? 0x10 : 0xc;
    phase = ATTEND;
    attend_frame = 0;
    phase_start = SDL_GetTicks();
}

static void attendant_done(Game *g)
{
    game_message(g, 0x45);                                              /* "Yo, you need full service, dummy!" */
    self_service = 0;
    phase = PUMPS;
    if (bought) { leave(g); return; }
    fee = 0;
    if (hand == 1) nozzle = 1;                                          /* he takes the high octane one */
    in_tank = 1;
    pump_sound(1);
    hand = 1;
    pumping = 1;
    polls = 0;
    poll_start = SDL_GetTicks();
}

static void key(Game *g, SDL_Scancode sc, SDL_Keycode k)
{
    (void)k;
    if (phase != PUMPS) return;
    int i = dialog_key(&dlg, sc);
    if (i >= 0) { choose(g, dlg.w[i].id); poll_start = SDL_GetTicks(); }
}

static void mouse_move(Game *g, int x, int y) { (void)g; dialog_mouse_move(&dlg, x, y); }

static void click(Game *g, int x, int y)
{
    if (phase != PUMPS) return;
    int i = dialog_click(&dlg, x, y);
    if (i < 0) return;
    choose(g, dlg.w[i].id);
    poll_start = SDL_GetTicks();
}

static void draw_car(Game *g, int x, double moved)
{
    Car *c = player_car(&g->player);
    if (!c) return;
    game_set_paint(g, c->colour);
    /* 2a21:80ec: the car's lowest BAND rows above the cut (DS:1bd0[3]) only cover the black under
     * it, so the pumps stand in front; its last CUT_ROWS rows aren't drawn at all. */
    static uint8_t keep[(CUT_ROWS + BAND) * SCREEN_W];
    uint8_t *below = &g->video.fb[(CAR_BOTTOM - CUT_ROWS - BAND + 1) * SCREEN_W];
    memcpy(keep, below, sizeof keep);
    int tires = c->tires.part >= 0 ? g_parts[c->tires.part].grade : -1;
    car_draw_ex(&g->video, g, c->model, c->body, blower_of(c), tires, ((int)moved / 6) & 3, x, CAR_BOTTOM);
    for (int k = 0; k < BAND * SCREEN_W; k++) if (keep[k]) below[k] = keep[k];
    memcpy(below + BAND * SCREEN_W, keep + BAND * SCREEN_W, CUT_ROWS * SCREEN_W);
}

/* A piece of 1000: entries (x, w) 0 (0, 8), 1 (8, 8), 2 (24, 16), 3 (40, 8) the nozzles on hooks
 * 8 / 56 / 176 / 224, 4 (48, 8) and 5 (56, 16) the hoses pulled off the two middle ones. */
static void hose(Game *g, int entry, int x)
{
    const Image *im = game_picture(g, HOSES);
    if (!im) return;
    int sx = (int16_t)exe_ds_u16((uint16_t)(0x5104 + 2 * entry)), w = (int16_t)exe_ds_u16((uint16_t)(0x50f8 + 2 * entry));
    for (int r = 0; r < 0x3a && r < im->h; r++)
        for (int c = 0; c < w; c++) {
            uint8_t px = im->pixels[r * im->w + sx + c];
            if (px) g->video.fb[(0x7b + r) * SCREEN_W + x + c] = px;
        }
}

static void update(Game *g)
{
    Video *v = &g->video;
    game_set_palette(g, PAL_SCREENS);
    video_clear(v, 0);
    game_draw_picture(g, STATION, 0, 0);
    hose(g, 0, 8);                                                      /* 1cae: the outer hooks, behind the car */
    hose(g, 3, 224);
    double moved = (SDL_GetTicks() - phase_start) * DRIVE_SPEED / 1000.0;
    int x = car_x;
    if (phase == ARRIVE) {
        x = (int)moved - car_w;
        if (x >= car_x) { x = car_x; drive_motor_stop(); pumps_on(); phase = PUMPS; poll_start = SDL_GetTicks(); }
        else drive_motor_update(car_x - x);
    } else if (phase == LEAVE) {
        x = car_x + (int)moved;
        drive_motor_update(x - car_x);
        if (x >= 320) {
            drive_motor_stop();
            g->player.time_actions += 0x222;                            /* 1001:4dd6 after 2104 */
            town_drive(g, 1, &scene_garage);                            /* 1f38: 1bc8 = 1, 8c68 */
            return;
        }
    }
    draw_car(g, x, phase == ARRIVE || phase == LEAVE ? moved : 0);
    hose(g, nozzle == 1 ? 4 : 1, 56);                                   /* 9a4a / 9acc: in front of the car */
    hose(g, nozzle == 2 ? 5 : 2, 176);
    if (phase != ARRIVE && phase != LEAVE) {
        if (in_tank) game_draw_picture(g, NOZZLE_IN, cap_x - 0x10, cap_y - 5);   /* 1d00 */
        else game_draw_picture(g, CAP, cap_x, cap_y);
    }
    lamps(g);
    if (attend_frame >= 0) {                                            /* 1c10: six frames at (0xc0, 0x59) */
        const Image *a = game_picture(g, ATTENDANT);
        int f = SDL_min(attend_frame, 5);
        if (a) video_blit_part(v, a, f * 0x18, 0, 0x18, 0x14, 0xc0, 0x59);
    }
    if (phase == ATTEND) {
        attend_frame = (int)(game_ticks_since(phase_start) / 0x10);    /* 3d80(0x10) a frame */
        if (attend_frame >= 6) { attend_frame = 5; attendant_done(g); }
    } else if (phase == PUMPS && !g->msg) {
        while (game_ticks_since(poll_start) >= POLL && phase == PUMPS) {
            poll_start += (Uint64)(POLL * 1000 / TICK_HZ);
            tick(g);
        }
    } else poll_start = SDL_GetTicks();
    const Widget *h = dlg.hover >= 0 ? &dlg.w[dlg.hover] : NULL;
    game_draw_status_bar(g, h && (h->flags & WF_ENABLED) && h->text && h->text[0] ? h->text : NULL);
}

int gas_shows_nozzle(void) { return hand == 2; }

const Scene scene_gas = { enter, key, NULL, mouse_move, click, update };
