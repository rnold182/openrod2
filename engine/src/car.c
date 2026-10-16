#include "car.h"

#include <string.h>
#include <SDL3/SDL.h>

#include "assets/exe.h"
#include "data.h"
#include "game.h"
#include "rivals.h"
#include "road.h"

void player_new(Player *p, const char *name)
{
    memset(p, 0, sizeof *p);
    SDL_strlcpy(p->name, name, sizeof p->name);
    p->money = 1200;                    /* 1001:5400 */
    p->current = -1;
    p->ads_week = -1;
    memset(p->ads, 0xFF, sizeof p->ads);
    p->ads_available = 42;
    p->header_raw[0x28] = p->header_raw[0x29] = 1;     /* DS:aa00 / aa01 (1001:5400) */
    memcpy(p->rivals, data_rivals_initial(), sizeof p->rivals);
    road_new_world(p);                                  /* DS:a9f8 and 7dfe (1dcb:2866 / 24d8) */
    rivals_new_game(p);                                 /* 1001:534c */
}

int player_day(const Player *p)
{
    long d = (long)((p->time_actions + (long)p->time_ticks) / 6552);
    return d > 91 ? 91 : (int)d;
}

/* 1001:64b2 */
void player_date(const Player *p, int *month, int *dom) { day_date(player_day(p), month, dom); }

void day_date(int d, int *month, int *dom)
{
    if (d < 15) { *month = 0; *dom = d + 16; }
    else if (d < 46) { *month = 1; *dom = d - 14; }
    else if (d <= 76) { *month = 2; *dom = d - 45; }
    else { *month = 3; *dom = d < 91 ? d - 76 : 15; }
}

Car *player_car(Player *p) { return p->current >= 0 ? &p->cars[p->current] : NULL; }

static PartInst make_part(int idx)
{
    PartInst pi = { (int16_t)idx, (int16_t)g_parts[idx].price, part_initial_condition(g_parts[idx].type) };
    return pi;
}

static const PartInst NO_PART = { -1, 0, 0 };

/* 1001:42aa */
Car *player_add_car(Player *p, int model, int price, int used)
{
    if (p->car_count >= MAX_CARS) return NULL;
    Car *c = &p->cars[p->car_count];
    car_make_stock(c, model, price, used);
    if (p->current < 0) p->current = p->car_count;
    return &p->cars[p->car_count++];
}

void car_make_stock(Car *c, int model, int price, int used)
{
    memset(c, 0, sizeof *c);
    const CarModel *m = &g_models[model];
    c->model = model;
    c->price = (int16_t)price;
    c->fuel = 40;
    c->cls = m->cls;
    c->colour = m->colour;
    c->wires = 1;
    c->timing = (int8_t)(SDL_rand(9) - 5);

    int make = (m->spec1 & 0x0e) >> 1;
    c->trans = make_part(make + (((m->spec1 & 0xc0) >> 6) + 3) * 3);
    c->engine = make_part(((m->spec1 >> 8) & 3) * 3 + make);
    c->tires = make_part(((m->spec1 & 0x30) >> 4) + 50);
    int ig = ((m->spec1 >> 8) & 0x3c) >> 2;
    int intake = (ig + 8) * 3 + make;
    if (ig == 8 && make == 4) intake--;
    c->intake = make_part(intake);
    int ncarbs = carbs_for_intake_grade(ig);
    int carb_type = ((m->spec1 >> 8) >> 6) + 21;
    for (int i = 0; i < 3; i++) c->carb[i] = i < ncarbs ? make_part(carb_type) : NO_PART;
    for (int i = 0; i < 5; i++) c->tune[i] = 3;
    for (int i = 0; i < ncarbs && i < 3; i++) c->tune[5 + i] = 3;
    c->exhaust = make_part((m->spec2 & 0x1f) * 3 + make + 53);
    c->muffler = make_part(((m->spec2 & 0x3e0) >> 5) + 59);
    c->diff = make_part(((m->spec2 >> 8) >> 2) + 61);
    if (used) {                         /* 1001:11a2: the cheaper ad has a worn engine and transmission */
        c->trans.wear = (int16_t)(SDL_rand(3000) + 6000);
        c->engine.wear = (int16_t)(SDL_rand(3000) + 6000);
    }
}

int player_add_spare(Player *p, PartInst part)
{
    if (p->part_count >= MAX_PARTS) return -1;
    memmove(&p->parts[1], &p->parts[0], sizeof p->parts[0] * (size_t)p->part_count);
    p->parts[0] = part;
    p->part_count++;
    return 0;
}

void player_remove_spare(Player *p, int i)
{
    if (i < 0 || i >= p->part_count) return;
    memmove(&p->parts[i], &p->parts[i + 1], sizeof p->parts[0] * (size_t)(p->part_count - i - 1));
    p->part_count--;
}

int player_add_part(Player *p, int part, int price)
{
    PartInst pi = make_part(part);
    pi.value = (int16_t)price;
    return player_add_spare(p, pi);
}

int car_runnable(const Car *c, int street)
{
    if (c->engine.part < 0 || c->wires <= 0 || c->intake.part < 0) return 0;
    int ncarbs = carbs_for_intake_grade(g_parts[c->intake.part].grade);
    for (int k = 0; k < ncarbs && k < 3; k++) if (c->carb[k].part < 0) return 0;
    for (int b = 0; b < ncarbs + 3; b++) if (c->tune[2 + b] != 3) return 0;
    if (!street) return 1;
    if (c->trans.part < 0 || c->tires.part < 0) return 0;
    return c->tune[0] == 3 && c->tune[1] == 3;
}

/* 2a21:89fa centring rule, with the garage's right-side limit. */
int car_garage_x(int w)
{
    int x = ((320 - w) / 2 + 4) & ~7;
    int lim = (0x120 - w) & ~7;
    return lim < x ? lim : x;
}

/* Copy `src` into `dst` at (x, y): opaque (zeros erase, like 2a21:ad92) or as a sprite. */
static void compose(Image *dst, const Image *src, int x, int y, int opaque)
{
    if (!src) return;
    for (int j = 0; j < src->h; j++)
        for (int i = 0; i < src->w; i++) {
            int dx = x + i, dy = y + j;
            if (dx < 0 || dy < 0 || dx >= dst->w || dy >= dst->h) continue;
            uint8_t c = src->pixels[j * src->w + i];
            if (opaque || c) dst->pixels[dy * dst->w + dx] = c;
        }
}

/* Body picture with its mods and wheels (2a21:8546). */
int car_draw(Video *v, Game *g, const Car *car, int model, int x, int bottom)
{
    const CarModel *m = &g_models[model];
    int body = car ? car->body : 0;
    int blower = car && car->intake.part >= 0 && g_parts[car->intake.part].grade == 8;
    int tires = car ? (car->tires.part >= 0 ? g_parts[car->tires.part].grade : -1) : ((m->spec1 & 0x30) >> 4);
    return car_draw_ex(v, g, model, body, blower, tires, 0, x, bottom);
}

int car_draw_ex(Video *v, Game *g, int model, int body_flags, int blower, int tire_grade, int wheel_frame,
                int x, int bottom)
{
    const CarModel *m = &g_models[model];
    const Image *body = game_picture(g, m->picture);
    if (!body) return x;
    if (x == CAR_CENTRED) x = ((320 - body->w) / 2 + 4) & ~7;
    static uint8_t buf[320 * 200];
    Image img = { body->w, body->h, body->flags, buf };
    memcpy(buf, body->pixels, (size_t)body->w * body->h);
    if ((body_flags & BODY_ROOF_CHOPPED) && m->patch[1]) compose(&img, game_picture(g, m->patch[1]), m->geo[4], 0, 1);
    if ((body_flags & BODY_REAR_STRIPPED) && m->patch[3]) compose(&img, game_picture(g, m->patch[3]), 0, m->geo[7], 1);
    if ((body_flags & BODY_FRONT_STRIPPED) && m->patch[4]) compose(&img, game_picture(g, m->patch[4]), m->geo[8], m->geo[9], 1);
    if (blower && model != 21 && m->patch[2])
        compose(&img, game_picture(g, m->patch[2]), m->geo[5], m->geo[6], 0);     /* supercharger blower */
    int top = bottom - body->h + 1;
    video_blit(v, &img, x, top);
    const Image *wheel = tire_grade >= 0 ? game_picture(g, tire_picture(tire_grade, wheel_frame)) : NULL;
    if (wheel) {
        int wy = top + body->h - wheel->h;
        video_blit(v, wheel, x + m->geo[0], wy);
        video_blit(v, wheel, x + m->geo[1], wy);
    }
    return x;
}

int car_body_width(Game *g, int model)
{
    const Image *body = game_picture(g, g_models[model].picture);
    return body ? body->w : 0;
}

int bolt_mode_of(int state) { return state == 0 ? 0 : state == 1 ? 1 : 2; }

int bolt_turn(uint8_t *state, int *mode)
{
    if (*mode == 2) {
        if (--*state > 1) return 0;
        *state = 0;
        *mode = 0;
        return 1;
    }
    if (*mode == 1) {
        if (*state == 3) *mode = 2;
        else ++*state;
    } else {
        *state = 1;
        *mode = 1;
    }
    return 0;
}

static int16_t perf(uint16_t table, int i) { return (int16_t)exe_ds_u16((uint16_t)(table + 2 * i)); }
static int grade(const PartInst *p) { return p->part >= 0 ? g_parts[p->part].grade : 0; }

long car_power(const Car *c)
{
    const CarModel *m = &g_models[c->model];
    int ig = grade(&c->intake), tg = grade(&c->trans), dg = grade(&c->diff);
    int cg = carbs_for_intake_grade(ig) >= 1 ? grade(&c->carb[0]) : 0;
    int roof = (c->body & BODY_ROOF_CHOPPED) || !m->patch[1];
    int front = (c->body & BODY_FRONT_STRIPPED) || !m->patch[4];
    int rear = (c->body & BODY_REAR_STRIPPED) || !m->patch[3];
    int t = c->timing < 0 ? -c->timing : c->timing;
    long p = (perf(0x5294, cg * 9 + ig) + perf(0x52d0, tg)) * 15
           + ((t * 4 / -3 + 2) * 44 + perf(0x52ec, c->cls)) * 10
           + (((roof * 2 + front) * 4 + rear) * 4 + perf(0x52ca, grade(&c->engine))) * 25;
    if (dg == 0) p += p * 5 / 100;                  /* 2.78:1 */
    else if (dg == 2) p += p * 5 / -100;            /* 4.11:1 */
    p /= 100;
    p *= 10000 - c->engine.wear;
    p /= 10000;
    p *= tg == 0 ? 8 : 10;                          /* automatic transmissions lose some */
    return p * 3 / 100 + 70;                        /* the +70 is easy to miss in the decompile */
}

int car_max_speed(const Car *c)
{
    long v = car_power(c) * 13 / 10;
    return v > 130 ? 130 : (int)v;
}

void part_wear_text(const PartInst *p, char out[16])
{
    out[0] = 0;
    if (p->wear == -128) return;
    int pct = (int8_t)(p->wear / 100);
    if (pct == 0) { SDL_strlcpy(out, "(`new`)", 16); return; }
    SDL_snprintf(out, 16, "(%d%%%s worn)", pct, pct < 10 && pct > -10 ? " " : "");
}
