#include "save.h"

#include <string.h>

#include "data.h"
#include "game.h"
#include "rivals.h"

/* File layout, all little-endian, in this order:
 *   0x38   player block (DS:a9d8): name at +0, money (long) at +0x24, list heads at +0x2a current car,
 *          +0x2c other cars, +0x32 spare parts (only tested for non-zero when loading)
 *   cars   current car, then the car list: each a 0x2a-byte record followed by its part nodes
 *   parts  spare part nodes, 8 bytes each: value, wear, part index, next (non-zero = another follows)
 *   0x0c   DS:479c, 0x18c DS:ab7e (rivals), 4 DS:1f6a (action clock), 4 elapsed ticks,
 *   2      DS:7dfe, 0x80 DS:9b40 (newspaper)
 * Car record: +0 price, +2 model, +4 colour, +5 class, +6 transmission, +8 engine, +0xa intake,
 * +0xc/+0xe/+0x10 carbs, +0x12 tires, +0x14..0x1b bolts, +0x1c exhaust, +0x1e muffler, +0x20 diff,
 * +0x22 wires, +0x23 timing, +0x24 fuel, +0x26 body, +0x28 next. Part fields are near pointers in
 * SR.EXE; only "zero or not" matters, so we write where the original's pools would have put them. */

#define HEADER_SIZE 0x38
#define CAR_SIZE    0x2a
#define NODE_SIZE   8
#define CAR_POOL    0xaec2      /* DS: 16 car records */
#define NODE_POOL   0xb1be      /* DS: 190 part nodes */

static void slot_path(char *out, size_t n, const char *dir, int slot)
{
    SDL_snprintf(out, n, "%s/HOTROD%d.SAV", dir, slot);
}

/* ------------------------------------------------------------------ writing */

typedef struct { uint8_t *buf; size_t len, cap; int nodes; } Out;

static void put(Out *o, const void *p, size_t n)
{
    if (o->len + n > o->cap) {
        o->cap = (o->len + n) * 2;
        o->buf = SDL_realloc(o->buf, o->cap);
    }
    memcpy(o->buf + o->len, p, n);
    o->len += n;
}
static void put16(Out *o, int v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; put(o, b, 2); }
static void put32(Out *o, long v) { put16(o, (int)(v & 0xffff)); put16(o, (int)((v >> 16) & 0xffff)); }
static uint16_t node_ptr(Out *o) { return (uint16_t)(NODE_POOL + NODE_SIZE * (o->nodes++ % 190)); }

static void put_node(Out *o, const PartInst *p, int more)
{
    put16(o, p->value); put16(o, p->wear); put16(o, p->part);
    put16(o, more ? node_ptr(o) : 0);
}

static void put_car(Out *o, const Car *c, int index, int more)
{
    const PartInst *before[6] = { &c->trans, &c->engine, &c->intake, &c->carb[0], &c->carb[1], &c->carb[2] };
    const PartInst *after[3] = { &c->exhaust, &c->muffler, &c->diff };
    put16(o, c->price);
    put16(o, c->model);
    uint8_t cb[2] = { c->colour, c->cls };
    put(o, cb, 2);
    for (int i = 0; i < 6; i++) put16(o, before[i]->part >= 0 ? node_ptr(o) : 0);
    put16(o, c->tires.part >= 0 ? node_ptr(o) : 0);
    put(o, c->tune, 8);
    for (int i = 0; i < 3; i++) put16(o, after[i]->part >= 0 ? node_ptr(o) : 0);
    uint8_t wt[2] = { (uint8_t)c->wires, (uint8_t)c->timing };
    put(o, wt, 2);
    put16(o, c->fuel);
    put16(o, c->body);
    put16(o, more ? CAR_POOL + CAR_SIZE * ((index + 1) % 16) : 0);
    for (int i = 0; i < 6; i++) if (before[i]->part >= 0) put_node(o, before[i], 0);
    if (c->tires.part >= 0) put_node(o, &c->tires, 0);
    for (int i = 0; i < 3; i++) if (after[i]->part >= 0) put_node(o, after[i], 0);
}

int save_write(const char *dir, int slot, Player *p, const char *name)
{
    SDL_strlcpy(p->name, name, sizeof p->name);
    Out o = { 0 };
    uint8_t h[HEADER_SIZE];
    memcpy(h, p->header_raw, sizeof h);
    memset(h, 0, SAVE_NAME);
    SDL_strlcpy((char *)h, p->name, SAVE_NAME);
    long m = p->money;
    h[0x24] = (uint8_t)m; h[0x25] = (uint8_t)(m >> 8); h[0x26] = (uint8_t)(m >> 16); h[0x27] = (uint8_t)(m >> 24);
    int others = p->car_count - (p->current >= 0);
    uint16_t cur = p->current >= 0 ? CAR_POOL : 0, list = others > 0 ? CAR_POOL + CAR_SIZE : 0;
    uint16_t spares = p->part_count > 0 ? NODE_POOL : 0;
    h[0x2a] = (uint8_t)cur; h[0x2b] = (uint8_t)(cur >> 8);
    h[0x2c] = (uint8_t)list; h[0x2d] = (uint8_t)(list >> 8);
    h[0x32] = (uint8_t)spares; h[0x33] = (uint8_t)(spares >> 8);
    put(&o, h, sizeof h);

    if (p->current >= 0) put_car(&o, &p->cars[p->current], 0, 0);
    for (int i = 0, k = 0; i < p->car_count; i++)
        if (i != p->current) { k++; put_car(&o, &p->cars[i], k, k < others); }
    for (int i = 0; i < p->part_count; i++) put_node(&o, &p->parts[i], i + 1 < p->part_count);

    put(&o, p->misc_479c, sizeof p->misc_479c);
    put(&o, p->rivals, sizeof p->rivals);
    put32(&o, p->time_actions);
    put32(&o, (long)p->time_ticks);
    put16(&o, p->word_7dfe);
    put16(&o, p->ads_week);                                 /* DS:9b40 block, 0x80 bytes */
    put(&o, p->ads, sizeof p->ads);
    put(&o, p->ad_cooldown, sizeof p->ad_cooldown);
    put16(&o, p->ads_available);
    put(&o, p->ads_tail, sizeof p->ads_tail);

    char path[1100];
    slot_path(path, sizeof path, dir, slot);
    int ok = SDL_SaveFile(path, o.buf, o.len);
    SDL_free(o.buf);
    if (!ok) SDL_Log("save %s: %s", path, SDL_GetError());
    return ok ? 0 : -1;
}

/* ------------------------------------------------------------------ reading */

typedef struct { const uint8_t *p; size_t len, pos; int bad; } In;

static const uint8_t *get(In *in, size_t n)
{
    if (in->bad || in->pos + n > in->len) { in->bad = 1; return NULL; }
    const uint8_t *r = in->p + in->pos;
    in->pos += n;
    return r;
}
static int16_t get16(In *in) { const uint8_t *b = get(in, 2); return b ? (int16_t)(b[0] | b[1] << 8) : 0; }
static long get32(In *in)
{
    const uint8_t *b = get(in, 4);
    return b ? (long)(int32_t)((uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24) : 0;
}

/* 1001:55fa: a chain of nodes; returns the next field of the last one read. */
static int get_node(In *in, PartInst *out)
{
    out->value = get16(in);
    out->wear = get16(in);
    out->part = get16(in);
    int more = get16(in) != 0;
    if (out->part < 0 || out->part >= PART_COUNT) in->bad = 1;
    return more;
}

static void get_single(In *in, int present, PartInst *out)
{
    out->part = -1; out->value = out->wear = 0;
    if (!present) return;
    if (get_node(in, out)) in->bad = 1;      /* a car's part slot holds one part */
}

/* 1001:5776: one car record and its parts; returns its next field. */
static int get_car(In *in, Car *c)
{
    memset(c, 0, sizeof *c);
    const uint8_t *r = get(in, CAR_SIZE);
    if (!r) return 0;
#define R16(o) ((int16_t)(r[o] | r[(o) + 1] << 8))
    c->price = R16(0);
    c->model = R16(2);
    c->colour = r[4];
    c->cls = r[5];
    memcpy(c->tune, r + 0x14, 8);
    c->wires = (int8_t)r[0x22];
    c->timing = (int8_t)r[0x23];
    c->fuel = R16(0x24);
    c->body = (uint16_t)R16(0x26);
    if (c->model < 0 || c->model >= MODEL_COUNT) in->bad = 1;
    PartInst *slots[10] = { &c->trans, &c->engine, &c->intake, &c->carb[0], &c->carb[1], &c->carb[2],
                            &c->tires, &c->exhaust, &c->muffler, &c->diff };
    static const int offs[10] = { 6, 8, 0xa, 0xc, 0xe, 0x10, 0x12, 0x1c, 0x1e, 0x20 };
    for (int i = 0; i < 10; i++) get_single(in, R16(offs[i]) != 0, slots[i]);
    return R16(0x28) != 0;
#undef R16
}

static int parse(const uint8_t *data, size_t len, Player *p)
{
    In in = { data, len, 0, 0 };
    const uint8_t *h = get(&in, HEADER_SIZE);
    if (!h) return -1;
    memset(p, 0, sizeof *p);
    memcpy(p->header_raw, h, HEADER_SIZE);
    memcpy(p->name, h, SAVE_NAME - 1);
    p->money = (long)(int32_t)((uint32_t)h[0x24] | (uint32_t)h[0x25] << 8 | (uint32_t)h[0x26] << 16 | (uint32_t)h[0x27] << 24);
    int has_cur = h[0x2a] | h[0x2b], has_list = h[0x2c] | h[0x2d], has_spares = h[0x32] | h[0x33];
    p->current = -1;
    if (has_cur) {
        get_car(&in, &p->cars[p->car_count++]);
        p->current = 0;
    }
    if (has_list)
        while (!in.bad && p->car_count < MAX_CARS && get_car(&in, &p->cars[p->car_count++])) {}
    if (has_spares)
        while (!in.bad && p->part_count < MAX_PARTS && get_node(&in, &p->parts[p->part_count++])) {}
    const uint8_t *b = get(&in, sizeof p->misc_479c);
    if (b) memcpy(p->misc_479c, b, sizeof p->misc_479c);
    if ((b = get(&in, sizeof p->rivals))) memcpy(p->rivals, b, sizeof p->rivals);
    p->time_actions = get32(&in);
    p->time_ticks = (double)get32(&in);
    p->word_7dfe = get16(&in);
    p->ads_week = get16(&in);
    if ((b = get(&in, sizeof p->ads))) memcpy(p->ads, b, sizeof p->ads);
    if ((b = get(&in, sizeof p->ad_cooldown))) memcpy(p->ad_cooldown, b, sizeof p->ad_cooldown);
    p->ads_available = get16(&in);
    if ((b = get(&in, sizeof p->ads_tail))) memcpy(p->ads_tail, b, sizeof p->ads_tail);
    return in.bad ? -1 : 0;
}

int save_read(const char *dir, int slot, Player *p)
{
    char path[1100];
    slot_path(path, sizeof path, dir, slot);
    size_t len;
    uint8_t *data = SDL_LoadFile(path, &len);
    if (!data) return -1;
    static Player tmp;                       /* a bad file leaves the current game alone */
    int r = parse(data, len, &tmp);
    SDL_free(data);
    if (r) { SDL_Log("save %s: not a valid saved game", path); return -1; }
    *p = tmp;
    /* Saves from OpenRod builds before the street have the rivals as SR2_DATA ships them
     * (no cars picked yet, reputation 0): set them up the way a new game does. */
    int fresh = 1;
    for (int r = 0; r < 21; r++) if (rival_model(p, r) || rival_get(p, r, 0xf)) fresh = 0;
    if (fresh) rivals_new_game(p);
    return 0;
}

int save_scan(const char *dir, SaveSlot slots[SAVE_SLOTS])
{
    int used = 0;
    for (int s = 0; s < SAVE_SLOTS; s++) {
        SaveSlot *sl = &slots[s];
        memset(sl, 0, sizeof *sl);
        char path[1100];
        slot_path(path, sizeof path, dir, s);
        size_t len;
        uint8_t *data = SDL_LoadFile(path, &len);
        if (!data) continue;
        static Player tmp;
        if (len >= HEADER_SIZE && parse(data, len, &tmp) == 0) {   /* 58b2 only checks the header */
            sl->used = 1;
            SDL_strlcpy(sl->name, tmp.name, sizeof sl->name);
            sl->day = player_day(&tmp);
            SDL_PathInfo info;
            if (SDL_GetPathInfo(path, &info)) sl->saved_at = info.modify_time;
            used++;
        }
        SDL_free(data);
    }
    return used;
}
