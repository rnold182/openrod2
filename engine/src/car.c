#include "car.h"

#include <string.h>
#include <SDL3/SDL.h>

#include "data.h"
#include "game.h"

void player_new(Player *p, const char *name)
{
    memset(p, 0, sizeof *p);
    SDL_strlcpy(p->name, name, sizeof p->name);
    p->money = 1200;                    /* 1001:5400 */
    p->current = -1;
    p->ads_week = -1;
    memset(p->ads, 0xFF, sizeof p->ads);
    p->ads_available = 42;
}

int player_day(const Player *p)
{
    long d = (long)((p->time_actions + (long)p->time_ticks) / 6552);
    return d > 91 ? 91 : (int)d;
}

/* 1001:64b2 */
void player_date(const Player *p, int *month, int *dom)
{
    int d = player_day(p);
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
    if (p->current < 0) p->current = p->car_count;
    return &p->cars[p->car_count++];
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

/* Body picture plus wheels (2a21:8546); body mod patches come later. */
int car_draw(Video *v, Game *g, const Car *car, int model, int x, int bottom)
{
    const CarModel *m = &g_models[model];
    const Image *body = game_picture(g, m->picture);
    if (!body) return x;
    if (x < 0) x = ((320 - body->w) / 2 + 4) & ~7;
    int top = bottom - body->h + 1;
    video_blit(v, body, x, top);
    int grade = car && car->tires.part >= 0 ? g_parts[car->tires.part].grade : ((m->spec1 & 0x30) >> 4);
    const Image *wheel = game_picture(g, tire_picture(grade, 0));
    if (wheel) {
        int wy = top + body->h - wheel->h;
        video_blit(v, wheel, x + m->geo[0], wy);
        video_blit(v, wheel, x + m->geo[1], wy);
    }
    return x;
}
