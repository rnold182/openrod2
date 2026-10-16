/* The race roads: track builder (1dcb:2866, 24d8 and its road pieces) and the windshield view
 * (1dcb:07e4 projection, 3e66 backdrop, 6a32 painter, 4322 road rows, 5158/4154 roadside shapes).
 * See road.h and NOTES.md "The roads". */
#include "road.h"

#include <string.h>
#include <SDL3/SDL.h>

#include "assets/exe.h"
#include "rivals.h"

RoadTrack g_road;

/* ------------------------------------------------------------------ the game's random numbers */

/* 2a21:6180: state = state * 0x015a4e35 + 1, result (high word & 0x7fff) % n. */
static uint32_t rng;
static int orand(int n)
{
    rng = rng * 0x015a4e35u + 1;
    return (int)(((rng >> 16) & 0x7fff) % (uint32_t)n);
}

/* The tables are written a few entries either side of the segment being built; the original lets
 * those land in its neighbouring tables, the rebuild drops them. */
static uint16_t scratch16;
static uint16_t *U(uint16_t *t, int i) { if (i < 0 || i >= ROAD_SEGS) { scratch16 = 0; return &scratch16; } return &t[i]; }
static int16_t *S(int16_t *t, int i) { return (int16_t *)U((uint16_t *)t, i); }
#define L(i)   (*U(g_road.left, i))
#define R(i)   (*U(g_road.right, i))
#define F(i)   (*U(g_road.flags, i))
#define HD(i)  (*S(g_road.heading, i))
#define HT(i)  (*S(g_road.height, i))
#define GL(i)  (*S(g_road.ground_l, i))
#define GR(i)  (*S(g_road.ground_r, i))
#define OB(i)  (*S(g_road.object, i))

#define DETAIL 0            /* DS:add8: 0 on a fast PC (1001:079a), 1 makes the roads plainer */

/* 1dcb:2866 and 24d8 both start with this: terrain beside the road rises or falls 200 per
 * segment in a few stretches past the drag strip, and stays level elsewhere. */
static void build_ground(void)
{
    for (int s = 200; s < ROAD_SEGS; s += 10)
        for (int k = 0; k < 10; k++) {
            int i = s + k, d;
            if (s < 0x17c) { GR(i) = GL(i) = 0; continue; }
            if (s <= 0x18e || (s >= 600 && s < 0x26b) || (s >= 700 && s < 0x2cf)) d = 200;
            else if ((s >= 400 && s <= 0x1a2) || (s >= 500 && s <= 0x206)) d = -200;
            else d = 0;
            GL(i) = (int16_t)(GL(i - 1) + d);
            GR(i) = (int16_t)(GR(i - 1) - d);
        }
}

/* 1dcb:2866 (new game part): the town streets, segments 0..0xb3. */
static void build_town(void)
{
    const int every = (DETAIL + 1) * 4;
    for (int i = 0; i < 0xb4; i++) {
        HT(i) = HD(i) = 0;
        R(i) = L(i) = 0;
        F(i) = 1;
        if ((i + 1) % every == 0) F(i) = 9;
        if ((i + 2) % 0x10 == 0) {                     /* a crossing */
            F(i + 2) |= 4; F(i + 1) |= 4; F(i) |= 4;
            R(i - 1) |= 0x40;
            L(i + 3) |= 0x40;
            F(i + 1) |= 1; F(i + 2) |= 1; F(i + 3) |= 1;
            for (int k = 1; k <= 3; k++) HT(i + k) = HD(i + k) = 0;
            i += 3;
            continue;
        }
        if ((i + 4) % 0x10 == 0 || (i + 8) % 0x10 == 0 || (i + 0xc) % 0x10 == 0) {
            if (orand(2) == 0) {
                L(i - 1) |= 4; L(i) |= 4;
                if (orand(3) == 0) { L(i - 1) |= 8; L(i) |= 8; }
                L(i) |= 0x10;
            } else {
                L(i - 2) |= 4; L(i) |= 4; L(i - 1) |= 4;
                if (orand(3) == 0) { L(i - 2) |= 8; L(i) |= 8; L(i - 1) |= 8; }
                L(i - 1) |= 0x10;
            }
        } else L(i) |= 0x80;
        if ((i + 5) % 0x10 == 0 || (i + 9) % 0x10 == 0 || (i + 0xc) % 0x10 == 0) {
            if (orand(3) == 0) {
                R(i - 2) |= 4; R(i) |= 4; R(i - 1) |= 4;
                if (orand(4) == 0) { R(i - 2) |= 8; R(i) |= 8; R(i - 1) |= 8; }
                R(i - 1) |= 0x10;
            } else {
                R(i - 1) |= 4; R(i) |= 4;
                if (orand(2) == 0) { R(i - 1) |= 8; R(i) |= 8; }
                R(i) |= 0x10;
            }
        } else R(i) |= 0x80;
        if (orand(DETAIL * 0x28 + 6) == 0 && !(L(i) & 0x10)) L(i) |= 0x8000;
        if (orand(DETAIL * 0x28 + 7) == 0 && !(R(i) & 0x10)) R(i) |= 0x8000;
        if (orand(DETAIL * 0x28 + 3) == 0 && !(L(i) & 0x10)) L(i) |= 0x100;
        if (orand(DETAIL * 0x28 + 4) == 0 && !(R(i) & 0x10)) R(i) |= 0x100;
    }
    F(orand(0x14) + 0x0a) |= 0x4000;
    F(orand(0x0f) + 0x23) |= 0x8000;
    F(orand(0x14) + 0x41) |= 0x4000;
    F(orand(0x1e) + 0x5a) |= 0x8000;
    F(orand(0x14) + 0x73) |= 0x4000;
    F(orand(0x14) + 0x8c) |= 0x8000;
    R(0x2c) |= 1;
    L(0x6c) |= 1;
    L(0x9c) |= 1;
}

/* 1dcb:2866 (both paths): segments 0xb4.. - the race roads' scenery, from the saved random state. */
static void build_scenery(void)
{
    const int every = (DETAIL + 1) * 4;
    int since_gantry = 0, since_object = 0;          /* local_4, local_c */
    for (int i = 0xb4; i < ROAD_SEGS; i++) {
        HT(i) = HD(i) = 0;
        F(i) = L(i) = R(i) = 0;
        if ((i + 1) % every == 0) F(i) = 8;            /* centre line dash */
        if (i % every == 0) { L(i) |= 1; R(i) |= 1; }
        if ((i + 2) % ((DETAIL + 1) * 8) == 0) F(i) |= 2;
        if (orand(DETAIL * 200 + 7) == 0) L(i) |= 0x8000;
        if (GR(i) < 0) { R(i) |= 0x8000; R(i) &= ~1; }
        if (i > 400 && orand(0x40) == 0) {
            int ok = since_object > 0x46;
            since_object++;
            if (ok) {
                F(i) |= 4;
                OB(i) = (int16_t)((int8_t)orand(0x10) + 1);
                since_object = 0;
            }
        }
        if (i > 400 && orand(0x50) == 0 && !(F(i) & 4)) {
            F(i) |= 0x1000;
            OB(i) = (int16_t)((int8_t)orand(0x10) + 1);
        }
        if (DETAIL == 0) {
            if (orand(0x23) == 0) { L(i) |= 0x40; L(i - 1) |= 0x40; L(i - 2) |= 0x40; }
            if (orand(0x19) == 0) { R(i) |= 0x40; R(i - 2) |= 0x40; }
        } else {
            if (orand(DETAIL * 100 + 0x23) == 0) L(i) |= 0x40;
            if (orand(DETAIL * 100 + 0x19) == 0) R(i) |= 0x40;
        }
        if (orand(DETAIL * 0x4b + 4) == 0 && !(L(i) & 0x40)) { L(i) |= 0x80; if (orand(2) == 0) L(i) |= 2; }
        if (orand(DETAIL * 0x4b + 6) == 0 && !(R(i) & 0x40)) { R(i) |= 0x80; if (orand(2) == 0) R(i) |= 2; }
        if (orand(DETAIL * 100 + 0x15) == 0) L(i) |= 4;
        if (orand(DETAIL * 100 + 0x19) == 0) R(i) |= 4;
        if (orand(DETAIL * 100 + 0x17) == 0 && !(L(i) & 4)) L(i) |= 8;
        if (orand(DETAIL * 100 + 0x1e) == 0 && !(R(i) & 4)) R(i) |= 8;
        if (orand(DETAIL * 0x32 + 2) == 0) { L(i) |= 0x100; if (orand(2) == 0) L(i) |= 0x200; }
        if (orand(DETAIL * 0x32 + 2) == 0) { R(i) |= 0x100; if (orand(2) == 0) R(i) |= 0x200; }
        if (i > 0xf0 && since_gantry > 0x46) {
            if (orand(DETAIL * 100 + 0x50) == 0 && GL(i + 1) > 0) { L(i) |= 0x10; L(i - 1) |= 0x1000; since_gantry = 0; }
            if (orand(DETAIL * 100 + 0x50) == 0 && GR(i + 1) > 0) { R(i) |= 0x10; R(i - 1) |= 0x1000; since_gantry = 0; }
        }
        since_object++;
        since_gantry++;
    }
    L(0x17b) &= 0x1811;
    R(0x17b) &= 0x1811;
    F(0x17b) |= 0x10;                                  /* the drag strip's finish line */
    int r = orand(0x8c), i = r + 0xdc;
    if (R(i) & 2) i = r + 0xdb;
    L(i) &= 0x9811;
    R(i) &= 0x9811;
    F(i) |= 0x8000;                                    /* a gantry over the drag strip */
    static const int8_t bump[17] = { 1, 2, 3, 4, 5, 4, 3, 2, 1, 0, -1, -2, -3, -4, -3, -2, -1 };   /* DS:7c1c */
    for (int s = 0xdc; s + 0x5a < 0x17c; s += 0x1f) {
        for (int k = 0; k < 17; k++, s++) HT(s) = bump[k];
        s += 0x15 - 1;
        for (int k = 0; k < 17; k++, s++) HT(s) = (int16_t)-bump[k];
        s--;
    }
    F(0x181) |= 0x100;
}

/* ---- 1dcb:24d8's road pieces. `at` (DS:96aa) is where the next piece goes, `dir` (96a8) the
 * heading reached so far, `rate` (96ac) how fast curves turn. */
static int at, dir, rate;

static void hold_heading(int n)
{
    for (int j = at; j < at + n && j < ROAD_SEGS; j++) HD(j) = (int16_t)dir;
}

static void turn(int step, int n)                      /* heading changes by `step` per segment */
{
    int k = rate + 1, q = dir / k, end = q + n * step;
    while ((step > 0 ? q < end : end < q) && at < ROAD_SEGS) {
        F(at) |= 8;
        HD(at) = (int16_t)(k * q);
        at++;
        q += step;
    }
    dir = k * q;
}

static void piece_right(void)                          /* 1dcb:1d04 */
{
    hold_heading(0x32);
    F(at) |= 0x200;
    at += 5;
    L(at) &= 0x9811; R(at) &= 0x9811;
    F(at) |= 0x200;
    at += 6;
    turn(1, 0x28);
}

static void piece_left(void)                           /* 1dcb:1dfa */
{
    hold_heading(0x32);
    L(at) &= 0x9811; R(at) &= 0x9811;
    F(at) |= 0x400;
    at += 3;
    F(at) |= 0x400;
    at += 3;
    turn(-1, 0x2d);
}

static void piece_s(int first, int sign, int n1, int n2)   /* 1dcb:1eea / 2034 */
{
    hold_heading(0x5a);
    at++;
    L(at) &= 0x9811; R(at) &= 0x9811;
    R(at) |= (uint16_t)sign;
    at += 6;
    R(at) |= (uint16_t)sign;
    int k = rate + 1;
    turn(first, n1);
    /* the second half's end is measured from the heading reached by the first */
    int q = dir / k, end = q - first * n2;
    while ((first > 0 ? end < q : q < end) && at < ROAD_SEGS) {
        F(at) |= 8;
        HD(at) = (int16_t)(k * q);
        at++;
        q -= first;
    }
    dir = k * q;
}

static void piece_turnoff(int side)                    /* 1dcb:217e (side 1) / 23d4 (side 0) */
{
    if (GR(at) == GL(at)) return;
    int s = at;
    uint16_t *near = side ? g_road.right : g_road.left, *far = side ? g_road.left : g_road.right;
    at += side ? 3 : 4;
    *U(near, at) |= 0x800;
    at += 4;
    F(at) &= 0xff7;
    at += 1;
    *U(far, at) &= 0xefee;
    *U(near, at) |= 0x1000;
    at += 1;
    for (int k = 0; k < 5 && at < ROAD_SEGS; k++, at++) {
        F(at) |= side ? 0x40 : 0x20;
        F(at) &= 0xff5;
        R(at) &= 0xefee;
        L(at) &= 0xefee;
    }
    F(at) &= 0xff5;
    at += side ? 4 : 3;
    for (int j = s; j < at && j < ROAD_SEGS; j++) HD(j) = (int16_t)dir;
}

static void piece_bridge(void)                         /* 1dcb:2282 */
{
    int s = at;
    F(at) |= 0x800;
    at += 4;
    L(at) &= 0x9811; R(at) &= 0x9811;
    F(at) |= 0x800;
    for (int k = 0; k < 6 && at < ROAD_SEGS; k++, at++) { L(at) &= 0x8000; R(at) &= 0x8000; }
    for (int k = 0; k < 0x14 && at < ROAD_SEGS; k++, at++) {
        F(at) |= 0x80;
        F(at) &= 0xff3;
        R(at) &= 0x6fee;
        L(at) &= 0x6fee;
        GR(at) = GL(at) = -500;
    }
    F(at) &= 0xffb;
    R(at) &= 0x6fee;
    L(at) &= 0x6fee;
    for (int k = 0; k < 3 && at < ROAD_SEGS; k++, at++) { L(at) &= 0x8000; R(at) &= 0x8000; }
    for (int j = s; j < at && j < ROAD_SEGS; j++) HD(j) = (int16_t)dir;
}

/* 1dcb:24d8: the bends, bridges and hills of the road past the drag strip. */
static void build_layout(int layout)
{
    build_ground();
    at = 0x186;
    dir = 0;
    rate = DETAIL == 0 ? 6 : 5;
    for (int i = 0x17c; i < ROAD_SEGS; i++) {
        HD(i) = 0;
        HT(i) = 0;
        F(i) &= 0xf117;
        L(i) &= 0xf7ff;
        R(i) &= 0x97ff;
        if ((R(i) & 0x1000) && !(R(i + 1) & 0x10)) R(i) &= 0xefff;
        if ((L(i) & 0x1000) && !(L(i + 1) & 0x10)) L(i) &= 0xefff;
        if ((i + 1) % ((DETAIL + 1) * 4) == 0) F(i) |= 8;
    }
    for (int p = 0; p < 13 && at < ROAD_SEGS; p++) {
        switch (exe_ds_u16((uint16_t)(0x7c3e + 2 * (layout * 13 + p)))) {   /* DS:7c3e, 10 x 13 */
        case 1: case 4: case 6: piece_right(); break;
        case 2: case 3: case 5: piece_left(); break;
        case 7: piece_turnoff(1); break;
        case 8: piece_turnoff(0); break;
        case 9: piece_bridge(); break;
        case 10: piece_s(1, 0x2000, 0x28, 0x2b); break;
        case 11: piece_s(-1, 0x4000, 0x23, 0x30); break;
        }
    }
    for (int s = 0x17c; s < ROAD_SEGS; s += 10)                        /* hills, DS:7d42 per 10 */
        for (int k = 0; k < 10; k++) {
            int i = s + k;
            if ((F(i) & 0x80) || (s > 0x397 && s < 0x3c1)) HT(i) = HT(i - 1);
            else HT(i) = (int16_t)(HT(i - 1) + (int16_t)exe_ds_u16((uint16_t)(0x7d42 + 2 * ((s - 0x17c) / 10))));
        }
    for (; at < ROAD_SEGS; at++) HD(at) = (int16_t)dir;
    F(0x3bf) |= 0x10;                                  /* Mulholland's finish line */
}

static uint32_t get_state(const Player *p)
{
    const uint8_t *h = p->header_raw + 0x20;           /* DS:a9f8 */
    return (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
}

void road_new_world(Player *p)
{
    rng = SDL_rand_bits();
    build_town();
    uint8_t *h = p->header_raw + 0x20;
    for (int k = 0; k < 4; k++) h[k] = (uint8_t)(rng >> (8 * k));
    p->word_7dfe = (int16_t)orand(10);
}

void road_build(Player *p)
{
    memset(&g_road, 0, sizeof g_road);
    rng = SDL_rand_bits();
    build_town();                                      /* the load path leaves these as they were */
    build_ground();
    rng = get_state(p);
    build_scenery();
    if (p->word_7dfe < 0 || p->word_7dfe > 9) p->word_7dfe = (int16_t)orand(10);
    build_layout(p->word_7dfe);
    if (SDL_getenv("SR2_DEBUG"))
        for (int i = 200; i < 0x17c; i++)
            if (F(i) & 0xf000) SDL_Log("road: gantry over segment %d", i);
}

/* ------------------------------------------------------------------ projection (1dcb:062c/0684) */

int road_proj_x(const RoadView *rv, int d, int lat, int turn_)
{
    int16_t t = (int16_t)(turn_ * d);
    int cam = rv->x;
    if (d == 0) d = 1;
    int16_t a = (int16_t)((int16_t)(cam - (int16_t)(lat + (t >> 2))) * 4);
    return -(a / d - 0xa0);
}

int road_proj_y(const RoadView *rv, int d, int h, int rise)
{
    uint16_t u = (uint16_t)(rise * d);
    if (d == 0) d = 1;
    int16_t a = (int16_t)(uint16_t)((u & 0xfffe) * 2 + (h - rv->h) * 4);
    return a / d + 0x3c;
}

/* The 15 distances of the view: [0] at d 4, then one per segment ahead. */
static int xl[15], xr[15], gy[15], xsl[15], xsr[15], y19[15], y195[15], tly[15], try_[15], xfl[15], xfr[15];

static void edges(const RoadView *rv, int d, int i, int f, int turn_)
{
    int a, b;                                          /* right edge, left edge */
    if (!(f & 0xe0)) { a = 0xce4; b = 0x5dc; }        /* two lanes */
    else if (f & 0x80) { a = 0xaf0; b = 2000; }       /* one lane: bridges */
    else if (f & 0x20) { a = b = 0xce4; }
    else { a = b = 0x5dc; }
    xr[i] = road_proj_x(rv, d, a, turn_);
    xl[i] = road_proj_x(rv, d, b, turn_);
}

static void project(const RoadView *rv)                /* 1dcb:07e4 */
{
    int mul = rv->race == RACE_MULHOLLAND, edge = mul ? 200 : 400;
    int seg = rv->seg, here = HT(seg);
    int rise = here - HT(seg), turn_ = rv->heading - HD(seg);
    if (mul) turn_ = SDL_clamp(turn_, -0x30, 0x30);
    gy[0] = road_proj_y(rv, 4, 2000, rise);
    edges(rv, 4, 0, F(seg) & 0xff, turn_);
    xsr[0] = road_proj_x(rv, 4, edge - rv->x + 0x19c8, turn_);
    xsl[0] = road_proj_x(rv, 4, rv->x - edge, turn_);
    y19[0] = road_proj_y(rv, 4, 0x76c, rise);
    y195[0] = road_proj_y(rv, 4, 0x79e, rise);
    tly[0] = road_proj_y(rv, 4, 2000 - GL(seg), rise);
    try_[0] = road_proj_y(rv, 4, 2000 - GR(seg), rise);
    xfl[0] = road_proj_x(rv, 4, (edge - 0x177) * -4, turn_);
    xfr[0] = road_proj_x(rv, 4, (edge + 0x339) * 4, turn_);
    int s8 = SDL_abs(rv->sub) >> 3, d = 4;
    for (int i = 1; i <= 14; i++) {
        if (i == 1) d = 12 - s8;
        if (i == 14) d += s8;
        int k = seg + i;
        rise = here - HT(k);
        turn_ = rv->heading - HD(k);
        edges(rv, d, i, F(k) & 0xff, turn_);
        gy[i] = road_proj_y(rv, d, 2000, rise);
        xsr[i] = road_proj_x(rv, d, edge + 0xce4, turn_);
        xsl[i] = road_proj_x(rv, d, 0x5dc - edge, turn_);
        y19[i] = road_proj_y(rv, d, 0x76c, rise);
        y195[i] = road_proj_y(rv, d, 0x79e, rise);
        tly[i] = road_proj_y(rv, d, 2000 - GL(k), rise);
        try_[i] = road_proj_y(rv, d, 2000 - GR(k), rise);
        xfl[i] = road_proj_x(rv, d, (edge - 0x177) * -4, turn_);
        xfr[i] = road_proj_x(rv, d, (edge + 0x339) * 4, turn_);
        d += 8;
    }
}

/* ------------------------------------------------------------------ drawing primitives */

#define CLIP_X0 0                                      /* DS:7e12..7e18 (1dcb:36f0) */
#define CLIP_Y0 0x12
#define CLIP_Y1 0x65
#define CLIP_X1 0x13f

static Video *V;
static uint8_t colour;

static void hline(int x0, int x1, int y)               /* a0e2 */
{
    if (y < CLIP_Y0 || y > CLIP_Y1) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x0 < CLIP_X0) x0 = CLIP_X0;
    if (x1 > CLIP_X1) x1 = CLIP_X1;
    if (x0 > x1) return;
    memset(&V->fb[y * SCREEN_W + x0], colour, (size_t)(x1 - x0 + 1));
}

static void vline(int x, int y0, int y1)               /* 1dcb:3664 */
{
    if (y1 < CLIP_Y0 || y0 > CLIP_Y1 || x < CLIP_X0 || x > CLIP_X1) return;
    int top = SDL_clamp(y0, CLIP_Y0, CLIP_Y1), bottom = SDL_clamp(y1, CLIP_Y0, CLIP_Y1);
    for (int y = top; y <= bottom; y++) V->fb[y * SCREEN_W + x] = colour;
}

/* ---- the roadside shapes: SR2_SHP, one set per kind (index DS:80a2: offset, size), each set
 * holding the shape at 7 sizes as up to 6 colour layers of horizontal spans (dx, dy, length). */
typedef struct { uint8_t layers[6][2]; const int8_t *spans; } ShapeSize;
typedef struct { int sizes; ShapeSize size[8]; } Shape;
#define SHAPE_KINDS 0x17
static Shape shapes[SHAPE_KINDS];
static uint8_t *shp_file;
static int shapes_loaded;

static void load_shapes(Game *g)
{
    if (shapes_loaded) return;
    shapes_loaded = 1;
    char path[1100];
    SDL_snprintf(path, sizeof path, "%s/SR2_SHP", g->data_dir);
    size_t len;
    shp_file = SDL_LoadFile(path, &len);
    if (!shp_file) return;
    for (int k = 0; k < SHAPE_KINDS; k++) {            /* 2a21:7304 */
        uint32_t off = exe_ds_u16((uint16_t)(0x80a2 + 8 * k)) | (uint32_t)exe_ds_u16((uint16_t)(0x80a4 + 8 * k)) << 16;
        uint32_t size = exe_ds_u16((uint16_t)(0x80a6 + 8 * k));
        if (!size || off + size > len || size < 6) continue;
        const uint8_t *b = shp_file + off;
        int n = b[0] | b[1] << 8;
        if (n < 0 || n > 8 || 6 + 16u * (uint32_t)n > size) continue;
        const int8_t *data = (const int8_t *)(b + 6 + 16 * n), *end = (const int8_t *)(b + size);
        Shape *sh = &shapes[k];
        for (int s = 0; s < n; s++) {
            ShapeSize *z = &sh->size[s];
            memcpy(z->layers, b + 6 + 16 * s, 12);
            z->spans = data;
            for (int l = 0; l < 6 && z->layers[l][1]; l++) data += z->layers[l][1] * 3;
            if (data > end) { n = s; break; }
        }
        sh->sizes = n;
    }
}

static void draw_shape(int x, int y, int size, int kind, int side)     /* 1dcb:4154 */
{
    if (kind < 0 || kind >= SHAPE_KINDS || size >= shapes[kind].sizes) return;
    const ShapeSize *z = &shapes[kind].size[size];
    const int8_t *p = z->spans;
    for (int l = 0; l < 6 && z->layers[l][1]; l++) {
        colour = z->layers[l][0];
        for (int n = z->layers[l][1]; n; n--, p += 3) {
            if (side < 1) hline(x - p[0] - p[2], x - p[0], y + p[1]);
            else hline(x + p[0] + p[2], x + p[0], y + p[1]);
        }
    }
}

static void object(int x, int y, int slice, int side, int kind, int below)    /* 1dcb:5158 */
{
    int size = SDL_clamp(9 - slice, 0, 6);
    if (!below && y > 0x65) y = 0x65;
    draw_shape(x, y, size, kind, side);
}

/* ------------------------------------------------------------------ the backdrop (1dcb:3e66) */

/* The original keeps the city (2002) and the hills (2055) on a spare EGA page, the city at row
 * 102 and the hills at the bottom, and copies rows of it into the view, scrolled sideways with
 * the heading (the city half as fast as the hills). */
static uint8_t page[SCREEN_W * SCREEN_H];
static int page_made;

static void page_put(Game *g, int id, int y)
{
    const Image *im = game_picture(g, id);
    if (!im) return;
    for (int r = 0; r < im->h && y + r < SCREEN_H; r++)
        for (int c = 0; c < im->w && c < SCREEN_W; c++)
            if (y + r >= 0) page[(y + r) * SCREEN_W + c] = im->pixels[r * im->w + c];
}

static void make_page(Game *g)                         /* 1dcb:37ce */
{
    if (page_made) return;
    page_made = 1;
    memset(page, 0, sizeof page);
    page_put(g, 0x7d2, 0x66);
    const Image *hills = game_picture(g, 0x807);
    if (hills) page_put(g, 0x807, 200 - hills->h);
}

static void copy_rect(int w, int h, int sx, int sy, int dx, int dy)    /* a0ae */
{
    for (int r = 0; r < h; r++) {
        int ty = dy + r, fy = sy + r;
        if (ty < 0 || ty >= SCREEN_H || fy < 0 || fy >= SCREEN_H) continue;
        for (int c = 0; c < w; c++) {
            int tx = dx + c, fx = sx + c;
            if (tx < 0 || tx >= SCREEN_W || fx < 0 || fx >= SCREEN_W) continue;
            V->fb[ty * SCREEN_W + tx] = page[fy * SCREEN_W + fx];
        }
    }
}

static void wrap(int heading, int mul, int *left, int *right)
{
    int16_t a = (int16_t)(SDL_abs(heading) * mul);
    int v = (a % 0x140) & 0xfff8;
    if (heading < 1) { *right = v; *left = 0x140 - v; }
    else { *left = v; *right = 0x140 - v; }
}

static int horizon_slice;                              /* DS:9856 */

static void backdrop(const RoadView *rv)
{
    int rise = HT(rv->seg) - HT(rv->seg - 1);
    int hz = road_proj_y(rv, 0xf0, 2000, rise);
    if (hz > 0x51 || hz < 0x13) hz = hz < 0x12 ? 0x12 : 0x52;
    int a, b;                                          /* local_1a, local_22 */
    wrap(rv->heading, 2, &a, &b);
    int h = hz - 0x1e, sy = 0xb8 - hz;
    copy_rect(b, h, 0, sy, a, 0x12);
    if (hz > 0x12) copy_rect(a, h, b, sy, 0, 0x12);
    wrap(rv->heading, 4, &a, &b);
    int dy = hz - 0xc, hh = 0x14, sy2 = 0xa3;
    if (dy < CLIP_Y0) { hh += dy - CLIP_Y0; sy2 += CLIP_Y0 - dy; dy = CLIP_Y0; }   /* 1dcb:35c4 */
    if (hh + dy > CLIP_Y1) hh = CLIP_Y1 - dy;
    copy_rect(b, hh, 0, sy2, a, dy);
    horizon_slice = 14;
    copy_rect(a, hh, b, sy2, 0, dy);
    int top = hz + 8;
    for (int k = 14; k > 0; k--)
        if (gy[k] < top) { top = gy[k]; horizon_slice = k; }
    if (top < 0x12) top = 0x12;
    colour = 9;                                        /* a0e6: the fields */
    for (int y = top; y <= CLIP_Y1; y++) hline(CLIP_X0, CLIP_X1, y);
}

/* ------------------------------------------------------------------ one strip of road (1dcb:4322) */

static int bridge_ahead(const RoadView *rv)            /* local_16 / local_30 */
{
    if (rv->race != RACE_MULHOLLAND) return 0;
    for (int k = 14; k > 9; k--)
        if (!(F(rv->seg + k) & 0x80) && (F(rv->seg + k - 1) & 0x80)) return 1;
    return 0;
}

static int slope(int xn, int yn, int xf, int yf) { return yf == yn ? 0 : (int16_t)((xf - xn) * 16) / (yf - yn); }   /* 1dcb:4268 */

static void road_strip(const RoadView *rv, int i)
{
    int seg = rv->seg, mul = rv->race == RACE_MULHOLLAND;
    int f = F(seg + i);
    if (f & 1) return;                                 /* town streets: not used by the races */
    int l = xl[i], r = xr[i], y = gy[i], yn = gy[i - 1];
    int l16 = l << 4, r16 = r << 4;
    int dl = slope(xl[i - 1], yn, l, y), dr = slope(xr[i - 1], yn, r, y);
    int sl = xsl[i], sr = xsr[i], sl16 = sl << 4, sr16 = sr << 4, dsl = 0, dsr = 0;
    if (mul) { dsl = slope(xsl[i - 1], yn, sl, y); dsr = slope(xsr[i - 1], yn, sr, y); }
    int w = (int16_t)exe_ds_u16((uint16_t)(0x7e52 + 2 * i)), cl = 0;   /* centre line width */
    if (f & 8) cl = ((r + l) >> 1) - (w >> 1);
    int bridge = mul && i > 9 && bridge_ahead(rv), back = 16 - i;
    int spread = bridge ? (int16_t)exe_ds_u16((uint16_t)(0x7e22 + 4 * back)) : 0;
    if (yn - y < 0) return;
    for (int n = yn - y; n != 0 && y < 0x66; y++, n--) {
        if (mul && !(f & 0x80) && HT(seg) - 0xb <= HT(seg)) {
            colour = 2;                                /* gravel shoulders */
            hline(sl, l, y);
            hline(r, sr, y);
        }
        colour = 9;
        if (!mul) {
            hline(0, l, y);
            hline(r, 0x13f, y);
        } else {
            if (tly[i - 1] == gy[i - 1]) hline(bridge ? l - spread : 0, l, y);
            if (try_[i - 1] == gy[i - 1]) hline(r, bridge ? spread + r : 0x13f, y);
        }
        colour = (f & 0x10) && (!mul || seg >= 0x3ad) ? 15 : 1;     /* finish lines are white */
        hline(l, r, y);
        if (f & 8) {
            colour = 4;
            if (l < 0x13f) hline(cl, cl + w, y);
        }
        l16 += dl; l = l16 >> 4;
        r16 += dr; r = r16 >> 4;
        if (mul) { sl16 += dsl; sl = sl16 >> 4; sr16 += dsr; sr = sr16 >> 4; }
        cl = ((r + l) >> 1) - (w >> 1);
    }
}

/* 1dcb:55ce: a gantry across the road, its sign by the flag bits 0xf000. */
static void gantry(int i, int top, int ground, unsigned bits, int f)
{
    int w = (int16_t)exe_ds_u16((uint16_t)(0x7e52 + 2 * i));
    for (int k = w + 1; k != 0; k--) {
        colour = 10;
        vline(xl[i] - k, top, ground);
        vline(xr[i] + k, top, ground);
        hline(xl[i], xr[i], k + top - 1);
    }
    int mid = (xr[i] + xl[i]) >> 1;
    if (bits & 0x1000) object(mid, top, i, 1, 0xd, 0);
    else if (bits & 0x2000) { /* a picture sign from the street sheet (52e2): town only */ }
    else if (!(f & 1)) object(mid, top, i, 1, bits & 0x4000 ? 0xc : 0xe, 0);
}

/* ------------------------------------------------------------------ the painter (1dcb:6a32) */

void road_draw(Game *g, const RoadView *rv)
{
    V = &g->video;
    load_shapes(g);
    make_page(g);
    project(rv);
    backdrop(rv);
    int seg = rv->seg, mul = rv->race == RACE_MULHOLLAND;
    for (int i = 14; i > 0; i--) {
        if (i <= horizon_slice) road_strip(rv, i);
        int lf = L(seg + i), rf = R(seg + i), f = F(seg + i);
        int town = f & 1;
        if (!(f & 4) || !town) {
            if (i < 0xb) {
                int width = xr[i] - xl[i], y = gy[i];
                int flat_l = tly[i - 1] == gy[i - 1], flat_r = try_[i - 1] == gy[i - 1];
                if (!mul && (lf & 0x20)) object(xl[i], y, i, -1, 0, 0);
                if ((lf & 0x100) && !(mul && !flat_l)) {
                    if (!(lf & 0x200)) object(xsl[i], y, i, -1, mul ? 0x15 : 4, 1);
                    else object(xsl[i] * 2 - xl[i], y, i + 2, -1, mul ? 0x15 : 4, 1);
                }
                if (mul && (lf & 0x800)) object(xl[i], y, i, 1, 3, 0);
                if (mul && (lf & 0x1000)) object(xl[i], y, i, 1, 6, 0);
                if ((lf & 1) && flat_l) object(xl[i], y, i, -1, 1, 1);             /* lamp posts */
                /* 51e0 (Mulholland's cliff posts, side flag 0x10) is not ported yet */
                if (!mul || flat_l) {
                    if (lf & 0x40) object(xsl[i], y, i, -1, 10, 0);
                    if (lf & 0x80) {
                        if (lf & 2) object((xfl[i] + CLIP_X0) >> 1, y, i + 3, -1, mul ? 0x16 : 0xb, 0);
                        else object(xsl[i], y, i, -1, mul ? 0x16 : 0xb, 0);
                    }
                }
                if (!mul || flat_r) {
                    if (rf & 0x40) object(xsr[i], y, i, 1, 10, 0);
                    if (rf & 0x80) {
                        if (rf & 2) object((xfr[i] + CLIP_X1) >> 1, y, i + 3, 1, mul ? 0x16 : 0xb, 0);
                        else object(xsr[i] + ((xsr[i] - xr[i]) >> 1), y, i, 1, mul ? 0x16 : 0xb, 0);
                    }
                }
                if (!mul || flat_l) {
                    if (lf & 4) object(xl[i] - 2, y, i, -1, 7, 1);
                    if (lf & 8) object(xl[i] - 2, y, i, -1, 8, 1);
                }
                if (!mul || flat_r) {
                    if (rf & 4) object(xr[i] + 2, y, i, 1, 7, 1);
                    if (rf & 8) object(xr[i] + 2, y, i, 1, 8, 1);
                }
                if ((f & 2) && (!mul || flat_r)) object(xsr[i] - (width >> 3), y, i, 1, 9, 0);
                if (!mul && (rf & 0x20)) object(xr[i], y, i, 1, 0, 0);
                if ((rf & 0x100) && !(mul && !flat_r)) {
                    if (!(rf & 0x200)) object(xsr[i], y, i, 1, mul ? 0x15 : 4, 1);
                    else object(xsr[i] * 2 - xr[i], y, i + 2, 1, mul ? 0x15 : 4, 1);
                }
                if (rf & 0x2000) object(xr[i], y, i, 1, 0x12, 0);
                if (rf & 0x4000) object(xr[i], y, i, 1, 0x13, 0);
                if (rf & 0x800) object(xr[i], y, i, 1, 3, 0);
                if (rf & 0x1000) object(xr[i], y, i, -1, 6, 0);
                if ((rf & 1) && flat_r) object(xr[i], y, i, -1, 1, 1);
                /* Mulholland's road objects (flags 4 / 0x1000 with DS:665a) are not ported yet */
                if ((f & 0xf000) && !mul) gantry(i, y19[i] * 4 - y * 3, y, (unsigned)(f & 0xf000), f);
                if (f & 0x100) object(xr[i], y, i, 1, 5, 0);
                if (f & 0x800) object(xr[i], y, i, 1, 0xf, 0);
                if (f & 0x200) object(xr[i], y, i, 1, 0x10, 0);
                if (f & 0x400) object(xr[i], y, i, 1, 0x11, 0);
            }
        }
        if (rv->opp_seg - i == seg && rv->draw_opponent) rv->draw_opponent();
    }
    if (seg == rv->opp_seg && rv->draw_opponent) rv->draw_opponent();
}
