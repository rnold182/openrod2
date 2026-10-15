#include "rivals.h"

#include <string.h>
#include <SDL3/SDL.h>

#include "data.h"

#define REC 18

int rival_get(const Player *p, int r, int off) { return (int8_t)p->rivals[r * REC + off]; }
void rival_set(Player *p, int r, int off, int v) { p->rivals[r * REC + off] = (uint8_t)v; }
int rival_model(const Player *p, int r) { return p->rivals[r * REC + 8] | p->rivals[r * REC + 9] << 8; }
const char *rival_name(int r) { return r >= 0 && r < RIVAL_COUNT ? g_rival_names[r] : ""; }

int player_rep(const Player *p, int which) { return (int8_t)p->header_raw[0x28 + which]; }
int player_counter(const Player *p, int k) { return (int16_t)(p->misc_479c[2 * k] | p->misc_479c[2 * k + 1] << 8); }
void player_set_counter(Player *p, int k, int v) { p->misc_479c[2 * k] = (uint8_t)v; p->misc_479c[2 * k + 1] = (uint8_t)(v >> 8); }

/* 1001:51f8: a fresh rival from one class group. */
static void new_rival(Player *p, int r, int *group, int count)
{
    int colour;
    do colour = SDL_rand(6); while (colour == 5 && SDL_rand(2) != 1);   /* white is rarer */
    rival_set(p, r, 0xa, colour);
    for (int k = 0xb; k <= 0xe; k++) rival_set(p, r, k, 0);
    int k;
    do k = SDL_rand(count); while (group[k] < 0);
    int model = group[k];
    group[k] = -1;
    p->rivals[r * REC + 8] = (uint8_t)model;
    p->rivals[r * REC + 9] = 0;
    const CarModel *m = &g_models[model];
    int odds = (m->cls >> 1) + 1, mods = 0;
    if (m->patch[3] && m->patch[4] && SDL_rand(25) < odds) mods |= 3;   /* bumpers stripped */
    if (m->patch[1] && SDL_rand(30) < odds) mods |= 4;                  /* roof chopped */
    if (SDL_rand(20) < odds) mods |= 0x20;                              /* blower */
    rival_set(p, r, 0xd, mods);
    rival_set(p, r, 0xf, 7 - (rival_get(p, r, 1) >> 1));
    rival_set(p, r, 0x10, 6 - (rival_get(p, r, 0) >> 2));
}

void rivals_new_game(Player *p)
{
    int groups[3][MODEL_COUNT], n[3] = { 0 };
    for (int m = 0; m < 21; m++) {                                      /* the King's Shelby is his alone */
        int c = g_models[m].cls, gi = c < 3 ? 0 : c < 6 ? 1 : 2;
        groups[gi][n[gi]++] = m;
    }
    for (int r = 0; r < 21; r++) new_rival(p, r, groups[r / 7], n[r / 7]);
    rival_set(p, KING, 0xb, 0);
}

int rivals_progress(const Player *p)
{
    int active = 0;
    for (int r = 0; r < 21; r++) if (rival_get(p, r, 0xb) != -1) active = 1;
    if (!active) return 0;
    int mull = player_counter(p, 2), aque = player_counter(p, 3), sum = mull + aque;
    if (sum < 3) return 1;
    if (sum < 7 || mull > 5 || aque > 5) {
        if (sum < 7 && mull < 6 && aque < 6) return 2;
        if (mull < 6) return 5;
        if (aque < 6) return 6;
        if (player_counter(p, 4) == 0) return 4;
        if ((player_counter(p, 0) >> 1) <= sum) return 0;
    }
    return 3;
}

int rivals_king_chance(const Player *p, int current)
{
    if (current == KING) return 0;
    switch (rivals_progress(p)) {
    case 0: return rival_get(p, KING, 0xb) == 1 ? 10 : 50;
    case 1: return 5;
    case 2: return 8;
    default: return 12;
    }
}

/* 1001:9848: rivals still in the game, closest to your car's top speed first; a random arrival
 * takes from the nine closest and drops the far-off classes beyond the sixth. */
static int candidates(const Player *p, int out[21])
{
    int you = model_max_speed(p, -1), you_cls = g_models[player_car((Player *)p)->model].cls;
    int diff[21], cls[21], used[21], n = 0;
    for (int r = 0; r < 21; r++) {
        int m = rival_model(p, r);
        diff[r] = SDL_abs(model_max_speed(p, m) - you);
        cls[r] = SDL_abs(g_models[m].cls - you_cls);
        used[r] = rival_get(p, r, 0xb) == -1;
    }
    while (n < 9) {
        int best = -1;
        for (int r = 0; r < 21; r++) if (!used[r] && (best < 0 || diff[r] < diff[best])) best = r;
        if (best < 0) break;
        used[best] = 1;
        out[n++] = best;
    }
    int k = 0;
    for (int i = 0; i < n; i++) if (i < 6 || cls[out[i]] <= 1) out[k++] = out[i];
    return k;
}

int rivals_pick_arrival(const Player *p, int king_chance)
{
    static int last = -1;                                   /* DS:5034: not the same face twice */
    if ((int)SDL_rand(100) < king_chance || !player_car((Player *)p)) return KING;
    int list[21], n = candidates(p, list);
    if (!n) return KING;
    int r = list[SDL_rand(n)];
    if (r == last) r = list[SDL_rand(n)];
    last = r;
    return r;
}

void rivals_meet(Player *p, int r)
{
    if (r == KING) return;
    rival_set(p, r, 0xb, 1);
    if (rival_get(p, r, 0xc) <= 8) return;                  /* regulars tune up their cars */
    const CarModel *m = &g_models[rival_model(p, r)];
    int odds = (m->cls >> 1) + 1, mods = p->rivals[r * REC + 0xd];
    if (m->patch[3] && m->patch[4] && SDL_rand(25) < odds) mods |= 3;
    if (m->patch[1] && SDL_rand(30) < odds) mods |= 4;
    if (SDL_rand(50) < odds) mods |= 0x20;
    rival_set(p, r, 0xd, mods);
}

int rivals_accepts_bet(const Player *p, int r, int stake)
{
    int m = rival_model(p, r);
    int q = model_quality(p, m) - model_quality(p, -1);
    int s = model_max_speed(p, m) - model_max_speed(p, -1);
    int r1 = rival_get(p, r, 0xf) - player_rep(p, 0), r2 = rival_get(p, r, 0x10) - player_rep(p, 1);
    int pct;
    if (!q && !s && !r1 && !r2) {
        pct = 50;
    } else {
        int a = stake < 3 ? 1 : 2, b = stake < 3 ? 2 : 1;      /* big bets weigh the reputation more */
        int num = ((r1 > 0 ? r1 : 0) + (q > 0 ? q : 0)) * a + ((r2 > 0 ? r2 : 0) + (s > 0 ? s : 0)) * b;
        int den = (SDL_abs(q) + SDL_abs(r1)) * a + (SDL_abs(s) + SDL_abs(r2)) * b;
        pct = den ? num * 100 / den : 50;
    }
    if (stake < 2) pct <<= 1;
    else if (stake > 3) pct = pct > 50 ? pct + (pct >> 2) : pct < 50 ? pct - (pct >> 2) : pct;
    return (int)SDL_rand(100) <= pct;
}

int model_quality(const Player *p, int model)
{
    if (model < 0) {
        const Car *c = player_car((Player *)p);
        if (!c) return 1;
        int e = c->engine.part >= 0 ? g_parts[c->engine.part].grade : 0;
        int t = c->trans.part >= 0 ? g_parts[c->trans.part].grade : 0;
        return e + t + 1;
    }
    unsigned spec = g_models[model].spec1;
    return (int)(((spec & 0xc0) >> 6) + ((spec >> 8) & 3) + 1);
}

int model_max_speed(const Player *p, int model)
{
    if (model < 0) {
        const Car *c = player_car((Player *)p);
        return c ? car_max_speed(c) : 0;
    }
    static Car stock;
    car_make_stock(&stock, model, 0, 0);
    return car_max_speed(&stock);
}
