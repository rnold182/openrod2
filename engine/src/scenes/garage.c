/* The garage, the game's hub screen (1001:6912, dialog 0). */
#include "../assets/exe.h"
#include "../car.h"
#include "../data.h"
#include "../game.h"
#include "../sound.h"
#include "../ui/font.h"
#include "../ui/partlist.h"
#include "deals.h"
#include "files.h"

/* Hotspot ids from dialog 0. */
enum {
    G_NEWSPAPER = 1, G_STREET = 2, G_CALENDAR = 3, G_QUIT = 4, G_SELL_PARTS = 5, G_CARS = 6,
    G_PAINT = 7, G_TIRES = 8, G_TRANSMISSION = 9, G_HOOD = 10, G_REAR_BUMPER = 11, G_FRONT_BUMPER = 12,
    G_ROOF = 13, G_UNDER_CAR = 14, G_TANK = 15, G_CAR_INFO = 16, G_GRUDGE = 17, G_RADIO = -40,
};

/* Modal dialogs shown on top of the garage. */
enum { MODAL_NONE, MODAL_QUIT_MENU, MODAL_EXIT_DOS, MODAL_PAINT, MODAL_BODY, MODAL_INFO, MODAL_CALENDAR };
static Uint64 calendar_start;

static Dialog dlg, modal;
static int modal_kind;
static PartList tires;          /* "Change tires" (1001:71f6) is a parts list over the garage */
static int paint_colour;        /* colour shown while choosing a paint job (1001:6d5e) */
static int body_flag, body_cost; /* roof / bumper job being offered (1001:6edc) */
static long body_time;

static void offer_body_job(Game *g, int id);

static void open_modal(int kind, int dialog)
{
    modal_kind = kind;
    dialog_load(&modal, dialog);
}

static void enter(Game *g)
{
    (void)g;
    dialog_load(&dlg, 0);
    modal_kind = MODAL_NONE;
    tires.active = 0;
}

/* 1001:67b8: hood, transmission, roof and bumper hotspots follow the car. */
static void place_car_hotspots(const Car *car, int x, int y)
{
    static const int ids[5] = { G_REAR_BUMPER, G_ROOF, G_TRANSMISSION, G_HOOD, G_FRONT_BUMPER };
    const uint8_t *h = g_models[car->model].hot;
    for (int i = 0; i < dlg.count; i++)
        for (int k = 0; k < 5; k++)
            if (dlg.w[i].id == ids[k]) {
                int x1 = h[4 * k] + x, y1 = h[4 * k + 1] + y;
                dialog_set_rect(&dlg, i, x1, y1, x1 + h[4 * k + 2], y1 + h[4 * k + 3]);
                if (SDL_getenv("SR2_DEBUG"))
                    SDL_Log("garage hotspot %d: %d,%d - %d,%d", ids[k], x1, y1, x1 + h[4 * k + 2], y1 + h[4 * k + 3]);
            }
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
    if (needs_car(id) && g->player.current < 0) {
        game_message(g, 9);                 /* "You've got no car, dummy!" */
        return;
    }
    switch (id) {
    case G_NEWSPAPER:
        if (g->player.car_count >= 16) { game_message(g, 34); break; }    /* 1001:3e08 */
        if (g->player.part_count > 180) { game_message(g, 35); break; }
        g->player.time_actions += 0x222;
        game_set_scene(g, &scene_newspaper);
        break;
    case G_HOOD:
        game_set_scene(g, &scene_hood);
        break;
    case G_TRANSMISSION:
        game_set_scene(g, &scene_trans);
        break;
    case G_UNDER_CAR:
        game_set_scene(g, &scene_under);
        break;
    case G_TIRES:
        if (!partlist_open(&tires, &g->player, PT_TIRES, NULL)) game_message(g, 30);
        break;
    case G_PAINT:
        open_modal(MODAL_PAINT, 13);
        paint_colour = player_car(&g->player)->colour;
        break;
    case G_ROOF:
    case G_REAR_BUMPER:
    case G_FRONT_BUMPER:
        offer_body_job(g, id);
        break;
    case G_CAR_INFO:
        modal_kind = MODAL_INFO;
        break;
    case G_CALENDAR:
        modal_kind = MODAL_CALENDAR;
        calendar_start = SDL_GetTicks();
        break;
    case G_SELL_PARTS:
        deals_sell_parts(g);
        break;
    case G_CARS:
        deals_cars(g);
        break;
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

/* 1001:6edc: chop/rebuild the roof ($70) or strip/restore a bumper ($15). */
static void offer_body_job(Game *g, int id)
{
    Car *c = player_car(&g->player);
    const CarModel *m = &g_models[c->model];
    uint16_t text;
    if (id == G_ROOF) {
        if (!m->patch[1]) { game_message(g, 31); return; }         /* "Ya can't chop this roof, dummy!" */
        body_flag = BODY_ROOF_CHOPPED;
        text = (c->body & body_flag) ? 0x4e04 : 0x4e02;            /* Rebuild roof / Chop roof - $70 */
        body_cost = 70;
        body_time = 0x2220;
    } else {
        int rear = id == G_REAR_BUMPER;
        if (!m->patch[rear ? 3 : 4]) { game_message(g, 32); return; }
        body_flag = rear ? BODY_REAR_STRIPPED : BODY_FRONT_STRIPPED;
        text = (c->body & body_flag) ? 0x4e08 : 0x4e06;            /* Restore / Strip bumper - $15 */
        body_cost = 15;
        body_time = 0x888;
    }
    open_modal(MODAL_BODY, 15);
    dialog_set_text(&modal, 0, exe_ds_str(exe_ds_u16(text)));
}

/* 2a21:1e50: pay, or show `msg` if the bankroll is too small. */
static int pay(Game *g, long amount, int msg)
{
    if (g->player.money < amount) { game_message(g, msg); return 0; }
    g->player.money -= amount;
    return 1;
}

/* 1001:72f2: the "Car parts" window. */
static void draw_car_info(Game *g)
{
    Video *v = &g->video;
    Car *c = player_car(&g->player);
    if (!c) return;
    ui_frame(v, 0x20, 0x19, 0x100, 0x8c, 1);
    const char *title = exe_ds_str(exe_ds_u16(0x4e68));
    ui_text(v, ((0x100 - font_text_width(title)) >> 1) + 0x20, 0x19, title);
    const uint8_t *sc = exe_ds(0x1d06 + 5, 5);
    video_fill(v, 0x20, 0x22, 0x100, 1, sc ? sc[3] : 15);
    const PartInst *list[10] = { &c->engine, &c->intake, &c->carb[0], &c->carb[1], &c->carb[2],
                                 &c->trans, &c->tires, &c->exhaust, &c->muffler, &c->diff };
    int y = 0x24;
    for (int k = 0; k < 10; k++) {
        if (list[k]->part < 0) continue;
        ui_text(v, (int16_t)exe_ds_u16((uint16_t)(0x4e54 + 2 * k)) + 0x20, y, g_parts[list[k]->part].ad);
        char wear[16];
        part_wear_text(list[k], wear);
        if (wear[0]) ui_text(v, 0x110 - 9 * 6, y, wear);
        y += 10;
    }
    char line[48];
    SDL_snprintf(line, sizeof line, "Fuel %2d gallons", c->fuel / 10);
    ui_text(v, 0x20, y + 10, line);
    SDL_snprintf(line, sizeof line, "Max speed %3d mph", car_runnable(c, 1) ? car_max_speed(c) : 0);
    ui_text(v, 0x20, y + 20, line);
}

/* 1001:650a: the month page with today marked; it stays up for 500 ticks or until a click. */
static void draw_calendar(Game *g)
{
    int day = player_day(&g->player), month, dom;
    player_date(&g->player, &month, &dom);
    int x = ((day + 22) % 7) * 18 + (int16_t)exe_ds_u16(0x4d48);
    int y = ((day + 22) / 7 - (int16_t)exe_ds_u16((uint16_t)(0x4d52 + 2 * month))) * 7
            + (int16_t)exe_ds_u16((uint16_t)(0x4d4a + 2 * month));
    game_draw_picture(g, month < 2 ? 0x478 : 0x47a, 0x50, 4);
    game_draw_picture(g, 0x479, 0x50 + x, 4 + y);
    if (game_ticks_since(calendar_start) >= 500) modal_kind = MODAL_NONE;
}

static void choose_modal(Game *g, int i)
{
    if (modal_kind == MODAL_INFO || modal_kind == MODAL_CALENDAR) { modal_kind = MODAL_NONE; return; }
    if (i < 0) return;
    int id = modal.w[i].id;
    Car *car = player_car(&g->player);
    if (modal_kind == MODAL_PAINT) {
        if (id == 1) paint_colour = paint_colour > 0 ? paint_colour - 1 : 5;        /* Previous color */
        else if (id == 2) paint_colour = paint_colour < 5 ? paint_colour + 1 : 0;   /* Next color */
        else if (id == 3) {                                                          /* Go ahead */
            modal_kind = MODAL_NONE;
            if (pay(g, 20, 25)) {
                car->colour = (uint8_t)paint_colour;
                g->player.time_actions += 0x888;
                sound_spray(600);
            }
        } else if (id == -23) modal_kind = MODAL_NONE;
        return;
    }
    if (modal_kind == MODAL_BODY) {
        modal_kind = MODAL_NONE;
        if (id == -20 && pay(g, body_cost, 33)) {
            car->body ^= (uint16_t)body_flag;
            g->player.time_actions += body_time;
            sound_sfx(SFX_DROP);                                        /* slot 9 after the redraw */
        }
        return;
    }
    if (modal_kind == MODAL_QUIT_MENU) {
        if (id == -20) open_modal(MODAL_EXIT_DOS, 35);                  /* Quit */
        else if (id == -1) game_set_scene(g, &scene_license);           /* New Game */
        else if (id == -23) files_open(g, FILES_SAVE, &scene_garage);   /* Save Game */
        else if (id == -2) files_open(g, FILES_LOAD, &scene_garage);    /* Old Game */
        else if (id == -22) modal_kind = MODAL_NONE;                    /* Forget it */
        else SDL_Log("quit menu: '%s' not rebuilt yet", modal.w[i].text);
    } else if (modal_kind == MODAL_EXIT_DOS) {
        if (id == -20) g->quit = 1;                                     /* Yes */
        else modal_kind = MODAL_NONE;
    }
}

/* Fit the chosen set of tires; the old set goes to the spares. */
static void tires_picked(Game *g, int r)
{
    Car *c = player_car(&g->player);
    if (r < 0 || !c) return;
    Player *p = &g->player;
    PartInst t = p->parts[r];
    player_remove_spare(p, r);
    if (c->tires.part >= 0) { player_add_spare(p, c->tires); c->price = (int16_t)(c->price - c->tires.value); }
    c->tires = t;
    c->price = (int16_t)(c->price + t.value);
    p->time_actions += 0x444;
}

static void key(Game *g, SDL_Scancode sc, SDL_Keycode k)
{
    (void)k;
    if (deals_active()) deals_key(g, sc, k);
    else if (tires.active) tires_picked(g, partlist_key(&tires, sc));
    else if (modal_kind) choose_modal(g, dialog_key(&modal, sc));
    else choose(g, dialog_key(&dlg, sc));
}

static void mouse_move(Game *g, int x, int y)
{
    (void)g;
    if (deals_active()) deals_mouse_move(g, x, y);
    else if (tires.active) partlist_mouse_move(&tires, x, y);
    else if (modal_kind) dialog_mouse_move(&modal, x, y);
    else dialog_mouse_move(&dlg, x, y);
}

static void click(Game *g, int x, int y)
{
    if (deals_active()) deals_click(g, x, y);
    else if (tires.active) tires_picked(g, partlist_click(&tires, x, y));
    else if (modal_kind) choose_modal(g, dialog_click(&modal, x, y));
    else choose(g, dialog_click(&dlg, x, y));
}

void garage_draw_background(Game *g)
{
    game_set_palette(g, PAL_SCREENS);
    Car *car = player_car(&g->player);
    if (car) game_set_paint(g, car->colour);
    g->video.palette[12] = car ? colour_paint_reg(car->colour) : 5;
    video_clear(&g->video, 0);
    game_draw_picture(g, 1018, 0, 0);
    game_draw_picture(g, 1020, 0x90, 0xa3);
    game_draw_picture(g, 1198, 0x50, 0xac);
    if (car) {
        const Image *body = game_picture(g, g_models[car->model].picture);
        if (body) {
            int x = car_draw(&g->video, g, car, car->model, car_garage_x(body->w), 0x9e);
            place_car_hotspots(car, x, 0x9e - body->h + 1);
        }
    } else {
        for (int i = 0; i < dlg.count; i++)                   /* 1001:6796 */
            if (dlg.w[i].id >= 9 && dlg.w[i].id <= 13) dlg.w[i].flags &= (uint16_t)~WF_ENABLED;
    }
}

static void update(Game *g)
{
    garage_draw_background(g);
    if (modal_kind == MODAL_PAINT) g->video.palette[12] = colour_paint_reg(paint_colour);   /* the paint gun shows it */
    game_draw_status_bar(g, deals_active() ? deals_hover_text() : NULL);
    dialog_draw(&g->video, &dlg);
    if (modal_kind == MODAL_INFO) draw_car_info(g);
    else if (modal_kind == MODAL_CALENDAR) draw_calendar(g);
    else if (modal_kind) dialog_draw(&g->video, &modal);
    if (tires.active) partlist_draw(&g->video, &tires);
    deals_draw(g);
}

static void text(Game *g, const char *s) { deals_text(g, s); }

const Scene scene_garage = { enter, key, text, mouse_move, click, update };
