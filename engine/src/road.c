/* The race roads: track builder (1dcb:2866, 24d8 and its road pieces) and the windshield view
 * (1dcb:07e4 projection, 3e66 backdrop, 6a32 painter, 4322 road rows, 5158/4154 roadside shapes).
 * See road.h and NOTES.md "The roads". */
#include "road.h"

#include <string.h>
#include <SDL3/SDL.h>

#include "assets/exe.h"
#include "rivals.h"

RoadTrack g_road;
uint16_t g_aqueduct[AQ_SEGS];
AqView g_aq;
static void build_aqueduct(void);
static void load_aq_shapes(size_t len);

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
    build_aqueduct();
    if (SDL_getenv("SR2_DEBUG"))
        for (int i = 200; i < 0x17c; i++)
            if (F(i) & 0xf000) SDL_Log("road: gantry over segment %d", i);
}

/* ------------------------------------------------------------------ projection (1dcb:062c/0684) */

int road_proj_x(const RoadView *rv, int d, int lat, int turn_)
{
    int16_t t = (int16_t)(turn_ * d);
    int cam = rv->x;
    if (rv->race == RACE_AQUEDUCT) cam = SDL_clamp(cam, 0x564, 0xd5c);
    if (d == 0) d = 1;
    int16_t a = (int16_t)((int16_t)(cam - (int16_t)(lat + (t >> 2))) * 4);
    return -(a / d - 0xa0);
}

int road_proj_y(const RoadView *rv, int d, int h, int rise)
{
    uint16_t u = (uint16_t)(rise * d);
    if (d == 0) d = 1;
    int16_t a = (int16_t)(uint16_t)((u & 0xfffe) * 2 + (h - rv->h) * 4);
    return a / d + g_aq.lift + 0x3c;                   /* 9a56: the Aqueduct's roll-over */
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

/* DS:7e12..7e18: the windshield (1dcb:36f0) or the mirror (36d6). */
static int CLIP_X0 = 0, CLIP_Y0 = 0x12, CLIP_Y1 = 0x65, CLIP_X1 = 0x13f;
static void clip_view(void) { CLIP_X0 = 0; CLIP_Y0 = 0x12; CLIP_Y1 = 0x65; CLIP_X1 = 0x13f; }        /* 36f0 */
static void clip_mirror(void) { CLIP_X0 = 0xf0; CLIP_Y0 = 0x16; CLIP_Y1 = 0x2a; CLIP_X1 = 0x13f; }   /* 36d6 */

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

static void line(int x0, int y0, int x1, int y1)        /* 1dcb:7de4 -> a0de, clipped to the view */
{
    int dx = SDL_abs(x1 - x0), dy = -SDL_abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (int n = 0; n < 2000; n++) {
        if (x0 >= CLIP_X0 && x0 <= CLIP_X1 && y0 >= CLIP_Y0 && y0 <= CLIP_Y1) V->fb[y0 * SCREEN_W + x0] = colour;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

/* 1dcb:58f4 -> 7f30 (clip) + a0f6: a filled polygon of n points; `edge` != `fill` outlines it. */
static void poly(const int *pt, int n, uint8_t fill, uint8_t edge)
{
    int top = 1 << 30, bottom = -(1 << 30);
    for (int k = 0; k < n; k++) { if (pt[2 * k + 1] < top) top = pt[2 * k + 1]; if (pt[2 * k + 1] > bottom) bottom = pt[2 * k + 1]; }
    if (bottom < CLIP_Y0 || top > CLIP_Y1) return;
    top = SDL_max(top, CLIP_Y0);
    bottom = SDL_min(bottom, CLIP_Y1);
    colour = fill;
    for (int y = top; y <= bottom; y++) {
        int lo = 1 << 30, hi = -(1 << 30);
        for (int k = 0; k < n; k++) {
            int ax = pt[2 * k], ay = pt[2 * k + 1], bx = pt[2 * ((k + 1) % n)], by = pt[2 * ((k + 1) % n) + 1];
            if ((y < ay && y < by) || (y > ay && y > by)) continue;
            int x0, x1;
            if (ay == by) { x0 = SDL_min(ax, bx); x1 = SDL_max(ax, bx); }
            else x0 = x1 = ax + (bx - ax) * (y - ay) / (by - ay);
            if (x0 < lo) lo = x0;
            if (x1 > hi) hi = x1;
        }
        if (lo <= hi) hline(lo, hi, y);
    }
    if (edge != fill) {
        colour = edge;
        for (int k = 0; k < n; k++) line(pt[2 * k], pt[2 * k + 1], pt[2 * ((k + 1) % n)], pt[2 * ((k + 1) % n) + 1]);
    }
}
#define POLY(n, c, ...) do { const int pt_[] = { __VA_ARGS__ }; poly(pt_, n, c, c); } while (0)

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
    load_aq_shapes(len);
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
static int page_made = -1;                             /* the race type it was made for */

static void page_put(Game *g, int id, int y)
{
    const Image *im = game_picture(g, id);
    if (!im) return;
    for (int r = 0; r < im->h && y + r < SCREEN_H; r++)
        for (int c = 0; c < im->w && c < SCREEN_W; c++)
            if (y + r >= 0) page[(y + r) * SCREEN_W + c] = im->pixels[r * im->w + c];
}

static void make_page(Game *g, int race)               /* 1dcb:37ce */
{
    if (page_made == race) return;
    page_made = race;
    memset(page, 0, sizeof page);
    if (race == RACE_MULHOLLAND) {                     /* the hills above the city (2056) */
        page_put(g, 0x808, 0x66);
        return;
    }
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
static int road_top;                                   /* DS:975e */
int road_ground_row(int i) { return i >= 0 && i < 15 ? gy[i] : road_top; }

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
    road_top = top;
    if (top < 0x12) top = 0x12;
    colour = 9;                                        /* a0e6: the fields */
    for (int y = top; y <= CLIP_Y1; y++) hline(CLIP_X0, CLIP_X1, y);
}

/* 1dcb:3cca: Mulholland's backdrop is one picture, lifted and lowered with the road ahead. */
static void backdrop_mulholland(const RoadView *rv)
{
    int rise = HT(rv->seg) - HT(rv->seg - 4);
    if (rise > 8) rise = 8;
    else if (rise < -8 && !rv->fall) rise = -8;
    int hz = road_proj_y(rv, 0xf0, 2000, rise);
    int sy = 0x6e - rise, h = 199 - sy, dy = 0x12, sx = 0;
    if (h > 0x52) h = 0x53;
    int a, b;
    wrap(rv->heading, 2, &a, &b);
    if (dy < CLIP_Y0) { h += dy - CLIP_Y0; sy += CLIP_Y0 - dy; dy = CLIP_Y0; }
    if (h + dy > CLIP_Y1) h = CLIP_Y1 - dy;
    copy_rect(b, h, sx, sy, a, dy);
    copy_rect(a, h, b, sy, 0, dy);
    int top = hz + 8;
    horizon_slice = 14;
    for (int k = 14; k > 0; k--)
        if (gy[k] < top) { top = gy[k]; horizon_slice = k; }
    road_top = top;
    if (rv->fall && h + 0x12 < 0x65) {
        colour = 0xe;
        for (int y = h + 0x12; y <= CLIP_Y1; y++) hline(CLIP_X0, CLIP_X1, y);
    }
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

static int dash_w(int i) { return (int16_t)exe_ds_u16((uint16_t)(0x7e52 + 2 * i)); }   /* *DS:96ee */

/* 1dcb:55ce: a gantry across the road, its sign by the flag bits 0xf000. */
static void gantry_at(int l, int r, int w, int top, int ground, int size, unsigned bits, int f)
{
    for (int k = w + 1; k != 0; k--) {
        colour = 10;
        vline(l - k, top, ground);
        vline(r + k, top, ground);
        hline(l, r, k + top - 1);
    }
    int mid = (r + l) >> 1;
    if (bits & 0x1000) object(mid, top, size, 1, 0xd, 0);
    else if (bits & 0x2000) { /* a picture sign from the street sheet (52e2): town only */ }
    else if (!(f & 1)) object(mid, top, size, 1, bits & 0x4000 ? 0xc : 0xe, 0);
}
static void gantry(int i, int top, int ground, unsigned bits, int f) { gantry_at(xl[i], xr[i], dash_w(i), top, ground, i, bits, f); }

/* ------------------------------------------------------------------ Mulholland's extras */

static int ds_w(int off) { return (int16_t)exe_ds_u16((uint16_t)off); }
#define T1E(k) ds_w(0x7e1e + 4 * (k))
#define T20(k) ds_w(0x7e20 + 4 * (k))
#define T22(k) ds_w(0x7e22 + 4 * (k))
#define T24(k) ds_w(0x7e24 + 4 * (k))
#define T3A(k) ds_w(0x7e3a + 4 * (k))

/* 6a32: the hillsides beside the road (colour 14), from the screen edge at the terrain's height to
 * the shoulder; across a ravine ahead (`bridge`) the drop is drawn as well. side -1 left, 1 right. */
static void hillside(const RoadView *rv, int i, int side, int bridge)
{
    int seg = rv->seg, edge = side < 0 ? 0 : 0x13f, fall = rv->fall;
    const int *xe = side < 0 ? xl : xr, *xs = side < 0 ? xsl : xsr, *ty = side < 0 ? tly : try_;
    int off = rv->heading - HD(seg);
    if (side < 0 ? !(-100 < off) : !(off < 100)) return;
    if (gy[i - 1] == ty[i - 1]) return;
    int k = 15 - i, sg = side;                          /* sg turns the ravine's offsets outwards */
    if (bridge && ty[i] <= gy[i]) {
        if (i >= 2 && (F(seg + i - 1) & 0x80)) {
            POLY(6, 0xe, xe[i - 2], 0x65, xe[i - 2], gy[i - 2], xe[i - 1], gy[i - 1], xs[i - 1], gy[i - 1],
                 xs[i - 1] + sg * T22(k), gy[i - 1], side < 0 ? T3A(k) : 0x13f - T3A(k), 0x65);
            if (ty[i] < gy[i])
                POLY(3, 0xe, xs[i - 1], gy[i - 1], xs[i - 1] + sg * (T22(k) / 2), gy[i - 1] - T24(k),
                     xs[i - 1] + sg * T22(k), gy[i - 1]);
            else
                POLY(4, 0xe, xs[i - 1], gy[i - 1], xs[i - 1] + sg * T22(k), gy[i - 1], xs[i] + sg * T22(k), gy[i],
                     xs[i], gy[i]);
        }
        if (ty[i] < gy[i])
            POLY(4, 0xe, xs[i - 1], gy[i - 1], xs[i - 1] + sg * (T22(k) / 2), gy[i - 1] - T24(k),
                 xs[i] + sg * (T1E(k) / 2), gy[i] - T20(k), xs[i], gy[i]);
    }
    if (bridge) return;
    if (i == 1 || !(F(seg + i - 1) & 0x80)) {
        if (xsr[i - 1] <= xsl[i - 1] + 0x1e) return;
        POLY(4, 0xe, edge, ty[i - 1], xs[i - 1] - fall, gy[i - 1], xs[i] + fall, gy[i], side < 0 ? 0 : 0x140, ty[i]);
        if (side < 0 ? xs[i - 1] < 1 : xs[i - 1] > 0x13e) return;
        int tri = i == 1 ? ty[i - 1] < gy[i - 1] : gy[i - 2] == ty[i - 2];
        if (tri) POLY(3, 0xe, edge, ty[i - 1], xs[i - 1], gy[i - 1], edge, gy[i - 1]);
    } else {
        POLY(6, 0xe, edge, ty[i - 1], xs[i - 1], gy[i - 1], xe[i - 1], gy[i - 1], xe[i - 2], gy[i - 2],
             xe[i - 2], 0x65, edge, 0x65);
        POLY(4, 0xe, edge, ty[i - 1], xs[i - 1], gy[i - 1], xs[i], gy[i], edge, ty[i]);
    }
}

/* 1dcb:51e0: a pile of rocks beside the road (side flag 0x10 on raised ground). */
static void rocks(int x, int y, int i, int side)
{
    int i2 = i + 2;
    if (i == 0) x += side * 100;
    if (i == 1) x += side * 0x28;
    if (i == 2) x += side * 10;
    int a = ds_w(0x7ed0 + 4 * i), b = ds_w(0x7e9a + 4 * i), up = ds_w(0x7ece + 4 * i), c = ds_w(0x7e9c + 4 * i2);
    object(x, y, i, side, 8, 1);
    object(a * side + x, y, i, side, 7, 1);
    object((a + b) * side + x, y, i, side, 8, 1);
    object((a >> 1) * side + x, y - up, i2, side, 7, 1);
    object(((a >> 1) + c) * side + x, y - up, i2, side, 8, 1);
}

/* 1dcb:69d4: does something this wide at x, in the nearest segment, reach the car's front corners? */
static int hits_car(int x, int w, int y)
{
    if (y > 0x57) {
        if ((x < 0x65 && x + w > 99) || (x < 0x83 && x + w > 0x81)) return -1;
        if ((x < 0x140 && x + w > 0x13e) || (x < 0x122 && x + w > 0x120)) return 1;
    }
    return 0;
}

static int rail_start;                                 /* DS:96f4 */
RoadHits g_road_hits;

/* 1dcb:5404: a bridge's railings between a nearer (1..4) and a farther (5..8) cross-section,
 * `w` posts thick (DS:96ee); `odd` / `near` pick which diagonal is drawn. */
static void bridge_sides(int p1, int p2, int p3, int p4, int p5, int p6, int p7, int p8, int segno, int near, int w)
{
    colour = 10;
    int odd = (segno + 0xe) % 2 != 0 && near;
    for (int k = w + 1; k != 0; k--) {
        line(p1 - k, p3, p1 - k, p4);
        line(p2 + k, p3, p2 + k, p4);
        line(p1, p3 + k - 1, p2, p3 + k - 1);
        int a, b;
        if (!odd) { line(p1 - k, p4, p5 - k, p7); a = p4; b = p7; }
        else { line(p1 - k, p3, p5 - k, p8); a = p3; b = p8; }
        line(p2 + k, a, p6 + k, b);
        line(p1 - k, p3, p5 - k, p7);
        line(p2 + k, p3, p6 + k, p7);
        line(p1 - k, p4, p5 - k, p8);
        line(p2 + k, p4, p6 + k, p8);
    }
}

static void bridge_start(int l, int r, int top, int ground, int w)    /* 1dcb:555e */
{
    colour = 10;
    for (int k = w + 1; k != 0; k--) {
        line(l - k, top, l - k, ground);
        line(r + k, top, r + k, ground);
        line(l, top + k - 1, r, top + k - 1);
    }
}

/* 6a32's last part for Mulholland: bridge railings (5404 / 555e) and the guard rail. */
static void mull_rails(const RoadView *rv, int i, int f, int rf)
{
    int seg = rv->seg, w = (int16_t)exe_ds_u16((uint16_t)(0x7e52 + 2 * i));
    colour = 10;
    if ((f & 0x80) && (F(seg + i - 1) & 0x80))           /* 5404: the bridge's sides */
        bridge_sides(xl[i - 1], xr[i - 1], y19[i - 1] * 4 - gy[i - 1] * 3, gy[i - 1],
                     xl[i], xr[i], y19[i] * 4 - gy[i] * 3, gy[i], seg - i, i < 7, w);
    if ((f & 0x80) && !(F(seg + i + 1) & 0x80))          /* 555e: where it starts */
        bridge_start(xl[i], xr[i], y19[i] * 4 - gy[i] * 3, gy[i], w);
    if ((rf & 0x8000) && !(f & 0x80)) {                  /* the guard rail on the drop side */
        if (((rail_start - i) - seg + 0xe) % 2 != 0) {
            int a = -0x1e / (i + 1) + xsr[i - 1];
            const int post[] = { a, y195[i - 1] - 1, a, gy[i - 1], xsr[i - 1], gy[i - 1], xsr[i - 1], y195[i - 1] - 1 };
            poly(post, 4, 5, 2);
        }
        const int rail[] = { xsr[i], y19[i], xsr[i], y195[i], xsr[i - 1], y195[i - 1], xsr[i - 1], y19[i - 1] };
        poly(rail, 4, 5, 5);
        colour = 2;
        line(xsr[i], y19[i], xsr[i - 1], y19[i - 1]);
        line(xsr[i], y195[i], xsr[i - 1], y195[i - 1]);
    }
}

/* ------------------------------------------------------------------ the painter (1dcb:6a32) */

static void aq_draw(const RoadView *rv);
static void mirror_draw(const RoadView *rv);
static Game *G_;

void road_draw(Game *g, const RoadView *rv)
{
    V = &g->video;
    G_ = g;
    clip_view();
    load_shapes(g);
    if (rv->race == RACE_AQUEDUCT) { aq_draw(rv); return; }
    make_page(g, rv->race);
    project(rv);
    int seg = rv->seg, mul = rv->race == RACE_MULHOLLAND;
    for (int k = 14; k > 0; k--)
        if ((R(seg + k) & 0x8000) && !(R(seg + k - 1) & 0x8000)) { rail_start = seg + k; break; }
    if (mul) backdrop_mulholland(rv);
    else backdrop(rv);
    int bridge = bridge_ahead(rv);
    g_road_hits.object = g_road_hits.bump = 0;
    for (int i = 14; i > 0; i--) {
        if (mul && !(F(seg + i) & 0x80)) {
            hillside(rv, i, -1, bridge);
            hillside(rv, i, 1, bridge);
        }
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
                if (mul && (lf & 0x10)) rocks(xl[i], y, i, 1);
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
                if (mul && (rf & 0x10)) rocks(xr[i], y, i, -1);
                for (int big = 1; big >= 0; big--) {            /* things lying on the road */
                    if (!mul || !(f & (big ? 4 : 0x1000))) continue;
                    int side = OB(seg + i) < 9 ? -1 : 1;
                    int x = (OB(seg + i) * width >> 4) + xl[i];
                    object(x, y, i, side, big ? 7 : 0x14, 1);
                    if (side == 1) side = 0;
                    int w = big ? 0x28 : 0x32;
                    if (i == 1) {
                        int h = hits_car(side * w + x, w, y);
                        if (big) g_road_hits.object = h;
                        else g_road_hits.bump = h;
                    }
                }
                if ((f & 0xf000) && !mul) gantry(i, y19[i] * 4 - y * 3, y, (unsigned)(f & 0xf000), f);
                if (f & 0x100) object(xr[i], y, i, 1, 5, 0);
                if (f & 0x800) object(xr[i], y, i, 1, 0xf, 0);
                if (f & 0x200) object(xr[i], y, i, 1, 0x10, 0);
                if (f & 0x400) object(xr[i], y, i, 1, 0x11, 0);
            }
        }
        if (mul && !town) mull_rails(rv, i, f, rf);
        if (rv->opp_seg - i == seg && rv->draw_opponent) rv->draw_opponent();
    }
    if (seg == rv->opp_seg && rv->draw_opponent) rv->draw_opponent();
    mirror_draw(rv);
}

/* ================================================================== the Aqueduct (add6 8) */
static void aq_mirror(const RoadView *rv);
static const Image *starter_pic(const RoadView *rv, int p);
void road_sprite(const Image *im, int cx, int by, int w, int h, int angle);


/* 1dcb:33ac (called from 24d8): the channel's table. */
static void build_aqueduct(void)
{
    uint16_t *t = g_aqueduct;
    for (int i = 6; i < AQ_SEGS; i += 0x18) t[i] = t[i - 1] = t[i - 2] = t[i - 3] = 2;     /* crossings */
    for (int i = 1; i < AQ_SEGS; i += 6) t[i] |= 1;                                        /* joints */
    static const int16_t block[] = { 4, 0x4c, 0xdc, 0x13c, 0x16c }, pillars[] = { 0x7c, 0x10c };
    static const int16_t bush_l[] = { 5, 0x22, 0x3b, 0x64, 0xb3, 0xd6, 0x142 };
    static const int16_t bush_r[] = { 0x17, 0x52, 0x82, 0x9b, 0xee, 0x106, 0x166 };
    for (int k = 0; k < 5; k++) t[block[k]] |= 0x10;
    for (int k = 0; k < 2; k++) t[pillars[k]] |= 0x80;
    for (int i = 0x12; i < AQ_SEGS; i += 0x18) t[i - 1] |= 4;
    t[0x89] = (uint16_t)((t[0x89] & ~4) | 0x100);
    t[0x119] = (uint16_t)((t[0x119] & ~4) | 0x100);
    for (int i = 0x12; i < AQ_SEGS; i += 0x18) t[i - 2] |= 8;
    t[0x88] = (uint16_t)((t[0x88] & ~8) | 0x200);
    t[0x118] = (uint16_t)((t[0x118] & ~8) | 0x200);
    for (int k = 0; k < 7; k++) { t[bush_l[k]] |= 0x20; t[bush_r[k]] |= 0x40; }
    t[0x155] |= 0x400;                                 /* the finish line */
}

/* The table's neighbour in the far segment is the left terrain table (5146), which a car past the
 * end of the channel reads. */
static unsigned T(int i) { return i < 0 ? 0 : i < AQ_SEGS ? g_aqueduct[i] : (uint16_t)GL(i - AQ_SEGS); }

void road_aq_reset(void)
{
    memset(&g_aq, 0, sizeof g_aq);
    g_aq.variant = 3;
}

int road_aq_sin(int a)                                 /* 1dcb:02b4, table DS:1504 */
{
    if (a < -0x167) a += 0x168;
    if (a > 0x167) a -= 0x168;
    int neg = a < 0;
    if (neg) a = -a;
    if (a > 0x59) {
        if (a < 0xb4) a = 0xb4 - a;
        else if (a < 0x10e) { a -= 0xb4; neg = !neg; }
        else if (a < 0x168) { a = 0x168 - a; neg = !neg; }
        else a = 0;
    }
    const uint8_t *s = exe_ds(0x1504, 91);
    int v = s ? s[a] : 0;
    return neg ? -v : v;
}

/* ---- projection (1dcb:18ec) */

/* By distance (index 0 nearest at d 4, 14 farthest): floor y (9ef0), floor edges (9a5c / a2e2),
 * the middle (a5a8), wall top y (a300) and wall tops (ab2e / ab5e). */
static int A_fy[15], A_xl[15], A_xr[15], A_xc[15], A_ty[15], A_wl[15], A_wr[15];
static int st_nl, st_nr, st_fl, st_fr, st_ny, st_fy;  /* the stripe: b16e / b872 near, b7ea / b8d8 far, ada6 / b162 */
static int car_sl, car_sr;                             /* a2b6 / a55a: the car's sides on the screen */
static int piv_x, piv_y, sinv, cosv;                   /* a7a8 / a7cc, a540 / ba90 */

static int aq_px(const RoadView *rv, int d, int lat) { return road_proj_x(rv, d, lat, rv->heading); }
static int aq_py(const RoadView *rv, int d, int h) { return road_proj_y(rv, d, h, -g_aq.pitch); }   /* 986a = a56c (0) - b7b0 */

static void aq_project(const RoadView *rv)
{
    AqView *q = &g_aq;
    int c = (q->right + q->left + 0x12c0) >> 1;
    st_nl = aq_px(rv, 4, c + q->left - 0x28);
    st_nr = aq_px(rv, 4, q->right + c + 0x28);
    q->car_l = rv->x - 0x6e;                           /* 9ac8 / 9a58 */
    q->car_r = rv->x + 0x208;
    car_sl = aq_px(rv, 4, q->car_l);
    car_sr = aq_px(rv, 4, q->car_r);
    int s8 = rv->sub < 0 ? -((-rv->sub) >> 3) : rv->sub >> 3, d = 4;
    for (int i = 0; i < 15; i++) {
        if (i == 1) d = 0xc - s8;
        if (i == 12) d += s8;
        A_fy[i] = aq_py(rv, d, q->floor + 2000);
        A_xl[i] = aq_px(rv, d, q->left + 0x5c8);
        A_xr[i] = aq_px(rv, d, q->right + 0xcf8);
        A_xc[i] = aq_px(rv, d, c + q->right);
        A_ty[i] = aq_py(rv, d, q->top + 0x67c);
        A_wl[i] = aq_px(rv, d, q->wall_l + 0x438);
        A_wr[i] = aq_px(rv, d, q->wall_r + 0xe88);
        if (i < 14) d += 8;
    }
    st_fl = aq_px(rv, d, c + q->left - 0x28);
    st_fr = aq_px(rv, d, c + q->right + 0x28);
    st_ny = A_fy[0];
    st_fy = A_fy[14];
}

/* ---- the tilt: on a wall (9d9c left, 9e4e right), over the middle stripe (9f08), 9f9a */

static void aq_wall_left(const RoadView *rv)          /* 1dcb:9d9c */
{
    AqView *q = &g_aq;
    piv_x = A_xl[14];
    int v = 0x5c8 - q->car_l;
    if (q->car_l < 0x514) v = 0xb4;
    q->bank = v * 0x6c / 400;
    q->left = q->right = q->wall_r = v >> 2;
    q->floor = v - (v >> 3);
    if (!rv->autopilot) q->slide += 0xf;               /* sliding back down */
    if (q->car_l < 0x514) {
        if (!rv->autopilot) q->slide += 0x19;
        int u = 0x514 - q->car_l;
        q->wall_l = u > 200 ? 200 : u;
        q->left += q->wall_l;
        q->floor += q->wall_l;
        q->top = u > 200 ? 200 : u;
    }
}

static void aq_wall_right(const RoadView *rv)               /* 1dcb:9e4e */
{
    AqView *q = &g_aq;
    piv_x = A_xr[14];
    int v = q->car_r - 0xcf8;
    if (q->car_r > 0xd80) v = 0x88;
    q->bank = v * -0x5a / 400;
    q->right = v * -3;
    q->floor = q->wall_r = v - (v >> 2);
    q->top = (v >> 2) - v;
    q->left += v * -2;
    q->wall_l += v * -2;
    if (!rv->autopilot) q->slide -= 0xf;
    if (q->car_r > 0xd80) {
        if (!rv->autopilot) q->slide -= 0x19;
        /* (a56a += 1 when DS:7fcc is set: it never is) */
        q->wall_r += 0xd80 - q->car_r;
        q->right += 0xd80 - q->car_r;
        q->top += q->car_r - 0xd80;
        q->floor += q->car_r - 0xd80;
    }
}

static int stripe_side(int x) { return x < st_nl ? -1 : st_nr < x ? 1 : 0; }

static void aq_stripe(const RoadView *rv)             /* 1dcb:9f08: bumping over the middle */
{
    AqView *q = &g_aq;
    if (T(rv->seg) & 2) return;                        /* a crossing */
    q->side_l = stripe_side(car_sl);
    q->side_r = stripe_side(car_sr);
    if (q->side_l == 0 || q->prev_l + q->side_l == 0) q->bank = -4;
    if (q->side_r == 0 || q->prev_r + q->side_r == 0) q->bank = 4;
    if (q->bank && rv->speed > 10) q->slow += 2;
}

static void aq_tilt(void)                              /* 1dcb:9f9a */
{
    AqView *q = &g_aq;
    q->bank += q->roll;
    if (q->bank > 0x167) q->bank -= 0x168;
    if (q->bank < -0x167) q->bank += 0x168;
    int a = SDL_abs(q->bank);
    q->abs_bank = a;
    q->variant = a >= 0x1f ? 0 : a >= 0x10 ? 1 : a >= 8 ? 2 : 3;
    if (q->bank < 0) q->variant = 6 - q->variant;
    int d = q->bank - q->bank_prev, p;
    if (d >= 8) p = 3;
    else if (d >= 6) p = 2;
    else if (d >= 4) p = 1;
    else if (d < -7) p = -3;
    else if (d < -5) p = -2;
    else if (d < -3) p = -1;
    else p = 0;
    if (q->bank < 0) p = -p;
    if (SDL_abs(p - q->pitch_prev) > 1) p = (q->pitch_prev + p) >> 1;
    q->pitch = p;
    q->bank_prev = q->bank;
    q->pitch_prev = p;
    q->prev_l = q->side_l;
    q->prev_r = q->side_r;
}

static void rot(int x, int y, int *ox, int *oy)       /* 1dcb:0379 */
{
    int16_t dx = (int16_t)(x - piv_x), dy = (int16_t)(y - piv_y);
    *ox = (int16_t)(((int32_t)dx * cosv) >> 8) + (int16_t)(((int32_t)dy * sinv) >> 8) + piv_x;
    *oy = (int16_t)(((int32_t)dy * cosv) >> 8) + (int16_t)(((int32_t)-dx * sinv) >> 8) + piv_y;
}

void road_aq_rotate(int x, int y, int *ox, int *oy) { rot(x, y, ox, oy); }

/* ---- polygons: 7f30 clips to the view (Sutherland-Hodgman), a0f6 fills */

static int y_at(int x1, int y1, int x2, int y2, int x)    /* 1dcb:0350 */
{
    if (x2 == x1) return y2;
    return (int16_t)((int32_t)(int16_t)(x - x1) * (int16_t)(y2 - y1) / (int16_t)(x2 - x1)) + y1;
}

static int x_at(int x1, int y1, int x2, int y2, int y)    /* 1dcb:0327 */
{
    if (y2 == y1) return x2;
    return (int16_t)((int32_t)(int16_t)(y - y1) * (int16_t)(x2 - x1) / (int16_t)(y2 - y1)) + x1;
}

static int inside(const int *p, int e)
{
    return e == 0 ? p[0] >= CLIP_X0 : e == 1 ? p[0] <= CLIP_X1 : e == 2 ? p[1] >= CLIP_Y0 : p[1] <= CLIP_Y1;
}

static int clip(const int *in, int n, int *out)        /* 1dcb:7f30 */
{
    int buf[2][2 * 24];
    const int *src = in;
    for (int e = 0; e < 4; e++) {
        int *dst = e == 3 ? out : buf[e & 1], m = 0;
        for (int k = 0, j = n - 1; k < n && m < 22; j = k++) {
            const int *p = src + 2 * j, *c = src + 2 * k;
            int pin = inside(p, e), cin = inside(c, e);
            if (!pin && !cin) continue;
            if (!pin || !cin) {
                int edge = e == 0 ? CLIP_X0 : e == 1 ? CLIP_X1 : e == 2 ? CLIP_Y0 : CLIP_Y1;
                if (e < 2) { dst[2 * m] = edge; dst[2 * m + 1] = y_at(p[0], p[1], c[0], c[1], edge); }
                else { dst[2 * m] = x_at(p[0], p[1], c[0], c[1], edge); dst[2 * m + 1] = edge; }
                m++;
                if (pin || !cin) continue;
            }
            dst[2 * m] = c[0];
            dst[2 * m + 1] = c[1];
            m++;
        }
        src = dst;
        n = m;
    }
    return n;
}

static int fill4(const int *pt, uint8_t fill, uint8_t edge, int *out)
{
    int n = clip(pt, 4, out);
    if (n > 0) poly(out, n, fill, edge);
    return n;
}

static unsigned bottom_hit(const int *p, int n)        /* 1dcb:8674: does it reach the car? */
{
    int l = 0, r = 0, any = 0;
    for (int k = 0; k < n; k++)
        if (p[2 * k + 1] == 0x65) {
            if (p[2 * k] < 0x50) l++;
            if (p[2 * k] > 0x13f) r++;
            any = 1;
        }
    unsigned v = (l & 1) ? 0x20 : 0;
    if (r & 1) v |= 0x40;
    if (any && l == r) v = 0x60;
    return v;
}

static int spans_bottom(int x0, const int *p, int n)   /* 1dcb:862a */
{
    int l = 0, r = 0;
    for (int k = 0; k < n; k++)
        if (p[2 * k + 1] == 0x65) {
            if (p[2 * k] < x0) l = 1;
            else if (x0 < p[2 * k]) r = 1;
        }
    return l & r;
}

/* ---- the Aqueduct's shapes: SR2_SHP kinds 23 (cloud), 24 (small cloud), 25 (bush, 9 sizes),
 * each size in 7 versions turned -45..45 degrees (DS:7fbe), loaded by 2a21:7446. */
typedef struct { int sizes, turns; ShapeSize v[10][7]; } AqShape;
static AqShape aq_shapes[3];

static void load_aq_shapes(size_t len)
{
    for (int k = 0; k < 3; k++) {
        int kind = (int16_t)exe_ds_u16(0x81da) + k;
        uint32_t off = exe_ds_u16((uint16_t)(0x80a2 + 8 * kind)) | (uint32_t)exe_ds_u16((uint16_t)(0x80a4 + 8 * kind)) << 16;
        uint32_t size = exe_ds_u16((uint16_t)(0x80a6 + 8 * kind));
        if (!size || off + size > len || size < 8) continue;
        const uint8_t *b = shp_file + off;
        int n = b[0] | b[1] << 8, turns = b[2] | b[3] << 8;
        if (n < 1 || n > 10 || turns != 7 || 8u + 4u * (uint32_t)n + 16u * (uint32_t)(n * turns) > size) continue;
        const uint8_t *rec = b + 8 + 4 * n;
        const int8_t *data = (const int8_t *)(rec + 16 * n * turns), *end = (const int8_t *)(b + size);
        AqShape *s = &aq_shapes[k];
        for (int i = 0; i < n * turns; i++) {
            ShapeSize *z = &s->v[i / turns][i % turns];
            memcpy(z->layers, rec + 16 * i, 12);
            z->spans = data;
            for (int l = 0; l < 6 && z->layers[l][1]; l++) data += z->layers[l][1] * 3;
            if (data > end) return;
        }
        s->sizes = n;
        s->turns = turns;
    }
}

static void aq_shape(int x, int y, int size, int k)    /* 1dcb:8288 */
{
    const AqShape *s = &aq_shapes[k];
    if (size < 0 || size >= s->sizes) return;
    if (g_aq.abs_bank < 0x37) {                        /* the version turned nearest the view */
        const ShapeSize *z = &s->v[size][g_aq.variant];
        int rx, ry;
        rot(x, y, &rx, &ry);
        const int8_t *p = z->spans;
        for (int l = 0; l < 6 && z->layers[l][1]; l++) {
            colour = z->layers[l][0];
            for (int n = z->layers[l][1]; n; n--, p += 3) hline(rx + p[0], rx + p[0] + p[2], ry + p[1]);
        }
        return;
    }
    const ShapeSize *z = &s->v[size][3];                /* steeper: the upright one, turned span by span */
    const int8_t *p = z->spans;
    for (int l = 0; l < 6 && z->layers[l][1]; l++) {
        colour = z->layers[l][0];
        for (int n = z->layers[l][1]; n; n--, p += 3) {
            int x0, y0, x1, y1;
            rot(x + p[0], y + p[1], &x0, &y0);
            rot(x + p[0] + p[2], y + p[1], &x1, &y1);
            line(x0, y0, x1, y1);
        }
    }
}

/* ---- the sky (1dcb:8b16 -> 84c4): night, stars (0dcb:01da, DS:7f2e / 7f76) and clouds */

static void stars(uint16_t tbl, int n, int dx, uint8_t c)
{
    const uint8_t *p = exe_ds(tbl, (size_t)n * 4);
    if (!p) return;
    for (int k = 0; k < n; k++) {
        int x = (int16_t)(p[4 * k] | p[4 * k + 1] << 8) + dx, y = (int16_t)(p[4 * k + 2] | p[4 * k + 3] << 8);
        if (g_aq.bank) rot(x, y, &x, &y);
        if (x >= CLIP_X0 && x <= CLIP_X1 && y >= CLIP_Y0 && y <= CLIP_Y1) V->fb[y * SCREEN_W + x] = c;
    }
}

static void aq_sky(const RoadView *rv)
{
    int bottom = g_aq.bank == 0 ? piv_y : CLIP_Y1;
    colour = 1;
    for (int y = CLIP_Y0; y <= bottom && y <= CLIP_Y1; y++) hline(CLIP_X0, CLIP_X1, y);
    int s = (int16_t)(rv->heading * 2) % 0x140;        /* 96fa */
    stars(0x7f2e, 0x12, s >> 1, 2);                    /* colours DS:ad48 / ad4a */
    stars(0x7f76, 6, s >> 1, 4);
    aq_shape(s + 0x5c, 0x20, 0, 0);
    aq_shape(s + 0xd2, 0x2d, 0, 1);
    aq_shape(s + 0xb4, 0x16, 0, 1);
}

/* ---- the painter (1dcb:905c), the view turned: [0] farthest .. [14] nearest */

static int R_lx[15], R_ly[15], R_rx[15], R_ry[15], R_wlx[15], R_wly[15], R_wrx[15], R_wry[15];

/* 1dcb:8ece: the patch on a wall (4 / 8 inset by a quarter, 0x100 / 0x200 the whole height). */
static int wall_patch(int k, unsigned f, int *pt)
{
    const int *ax, *ay, *bx, *by;
    if (f & 0x208) { ax = R_rx; ay = R_ry; bx = R_wrx; by = R_wry; }
    else if (f & 0x104) { ax = R_lx; ay = R_ly; bx = R_wlx; by = R_wly; }
    else return 0;
    int q0 = (int16_t)(ax[k] - bx[k]) / 4, q1 = (int16_t)(ax[k + 1] - bx[k + 1]) / 4;
    if (f & 0xc) {
        pt[0] = ax[k] - q0;
        pt[1] = y_at(ax[k], ay[k], bx[k], by[k], pt[0]);
        pt[6] = ax[k + 1] - q1;
        pt[7] = y_at(ax[k + 1], ay[k + 1], bx[k + 1], by[k + 1], pt[6]);
    } else {
        pt[0] = ax[k]; pt[1] = ay[k];
        pt[6] = ax[k + 1]; pt[7] = ay[k + 1];
    }
    pt[2] = bx[k] + q0;
    pt[3] = y_at(ax[k], ay[k], bx[k], by[k], pt[2]);
    pt[4] = bx[k + 1] + q1;
    pt[5] = y_at(ax[k + 1], ay[k + 1], bx[k + 1], by[k + 1], pt[4]);
    return 1;
}

/* 1dcb:8b5c: a box from its far face (0..3: bottom left, bottom right, top left, top right) and
 * near face (4..7); kind 1 (pillars) checks its foot against the car, kind 0 (blocks) its top. */
static void box(int *p, int kind, int main)
{
    for (int k = 0; k < 8; k++) rot(p[2 * k], p[2 * k + 1], &p[2 * k], &p[2 * k + 1]);
#define X(i) p[2 * (i)]
#define Y(i) p[2 * (i) + 1]
#define EDGE(a, b) ((SDL_abs(X(a) - X(b)) < 3 || SDL_abs(Y(a) - Y(b)) < 3) ? 2 : 0)
    int out[48], n;
    if (X(0) < X(4)) { int q[8] = { X(0), Y(0), X(2), Y(2), X(6), Y(6), X(4), Y(4) }; fill4(q, 2, EDGE(0, 6), out); }
    if (X(5) < X(1)) { int q[8] = { X(1), Y(1), X(3), Y(3), X(7), Y(7), X(5), Y(5) }; fill4(q, 2, EDGE(1, 7), out); }
    if (Y(2) < Y(6)) {
        int q[8] = { X(2), Y(2), X(3), Y(3), X(7), Y(7), X(6), Y(6) };
        n = clip(q, 4, out);
        if (n > 0) {
            if (kind == 0 && main) g_aq.hits |= bottom_hit(out, n);
            poly(out, n, 5, EDGE(2, 7));
        }
    }
    { int q[8] = { X(5), Y(5), X(4), Y(4), X(6), Y(6), X(7), Y(7) }; fill4(q, 2, EDGE(5, 6), out); }
    if (kind == 1) {
        int q[8] = { X(0), Y(0), X(1), Y(1), X(5), Y(5), X(4), Y(4) };
        n = clip(q, 4, out);
        if (n > 0 && main) g_aq.hits |= bottom_hit(out, n);
    }
#undef X
#undef Y
#undef EDGE
}

/* n = 14 for the windshield, 7 for the mirror (DS:b194); the mirror reads the reversed table (628e). */
static void aq_paint(const RoadView *rv, int n)
{
    int seg = rv->seg, out[48], main = n == 14;
#define TF(k) (main ? T(seg + 14 - (k)) : T(seg - 8 + (k)))
#define TB(j) (main ? T(seg + (j)) : T(seg - 1 - (j)))
    int l42 = R_lx[0], l40 = R_ly[0], lb2 = R_rx[0], lba = R_ry[0];
    int l3e = R_lx[n], l1e = R_ly[n], la0 = R_rx[n], la8 = R_ry[n];
    int l46 = R_wlx[0], l44 = R_wly[0], l50 = R_wrx[0], l98 = R_wry[0];
    int la = R_wlx[n], l10 = R_wly[n], l22 = R_wrx[n], l28 = R_wry[n];
    if (g_aq.bank) {                                   /* reach past the screen's corners */
        l3e = l3e * 4 - l42 * 3; l1e = l1e * 4 - l40 * 3;
        la0 = la0 * 4 - lb2 * 3; la8 = la8 * 4 - lba * 3;
        la = la * 4 - l46 * 3; l10 = l10 * 4 - l44 * 3;
        l22 = l22 * 4 - l50 * 3; l28 = l28 * 4 - l98 * 3;
    }
    if (g_aq.bank == 0) {
        colour = 5;                                    /* a0e6: the concrete */
        for (int y = SDL_max(A_fy[n], CLIP_Y0); y <= CLIP_Y1; y++) hline(CLIP_X0, CLIP_X1, y);
        int q[8] = { la, l10, l46, l44, l42, l40, 0, l40 };
        fill4(q, 5, 5, out);
        int r[8] = { l22, l28, l50, l98, lb2, lba, 0x13f, lba };
        fill4(r, 5, 5, out);
    } else {
        int q[8] = { la, l10, l46, l44, l42, l40, l3e, l1e };
        fill4(q, 5, 5, out);
        int r[8] = { l22, l28, l50, l98, lb2, lba, la0, la8 };
        fill4(r, 5, 5, out);
        int f[8] = { l42, l40, lb2, lba, la0, la8, l3e, l1e };
        fill4(f, 5, 5, out);
    }
    colour = 0;
    line(l42, l40, l3e, l1e);                          /* the floor's edges */
    line(lb2, lba, la0, la8);
    colour = 15;
    line(l46, l44, la, l10);                           /* the tops of the walls */
    line(l50, l98, l22, l28);
    int sfx, sfy, sgx, sgy, snx, sny, sox, soy;        /* the middle stripe: far left / right, near left / right */
    rot(st_fl, st_fy, &sfx, &sfy);
    rot(st_fr, st_fy, &sgx, &sgy);
    rot(st_nl, st_ny, &snx, &sny);
    rot(st_nr, st_ny, &sox, &soy);
    for (int k = 0; k < n; k++) {
        unsigned f = TF(k);
        if (f & 0x400) {                               /* the finish line */
            int q[8] = { R_lx[k], R_ly[k], R_rx[k], R_ry[k], R_rx[k + 1], R_ry[k + 1], R_lx[k + 1], R_ly[k + 1] };
            fill4(q, 15, 15, out);
        }
        if ((f & 1) && A_fy[n] < A_fy[n - k]) {      /* a joint across the channel */
            colour = 0;
            line(R_lx[k], R_ly[k], R_rx[k], R_ry[k]);
            line(R_lx[k], R_ly[k], R_wlx[k], R_wly[k]);
            line(R_rx[k], R_ry[k], R_wrx[k], R_wry[k]);
        }
        int pt[8];
        if (k > 1 && wall_patch(k, f, pt)) {
            int m = clip(pt, 4, out);
            if (m > 0) {
                if (main && spans_bottom(0x5a, out, m)) g_aq.hits |= 1;
                if (main && spans_bottom(0x13e, out, m)) g_aq.hits |= 4;
                poly(out, m, 2, 0);
            }
        }
        int j = n - k, size = main ? j >> 1 : ((j - 1) >> 1) + 6;      /* 8482 */
        if ((f & 0x20) && k > 0) aq_shape(((A_xr[j] - A_xl[j]) >> 2) + A_xl[j], A_fy[j], size, 2);
        if ((f & 0x40) && k > 0) aq_shape(A_xr[j] - ((A_xr[j] - A_xl[j]) >> 3), A_fy[j], size, 2);
    }
    int aa = -1, b4;
    for (int k = 0; k < n; k++) if (TF(k) & 2) { aa = k; break; }
    for (b4 = aa + 1; b4 < n && (TF(b4) & 2); b4++) { }
    if (b4 < 2) {
        int q[8] = { sfx, sfy, snx, sny, sox, soy, sgx, sgy };
        fill4(q, 2, 0, out);
    } else {                                           /* the stripe stops at a crossing */
        int x0, y0, x1, y1;
        if (aa > 0) {
            int y = A_fy[n - aa];
            rot(x_at(st_fl, st_fy, st_nl, st_ny, y), y, &x0, &y0);
            rot(x_at(st_fr, st_fy, st_nr, st_ny, y), y, &x1, &y1);
            int q[8] = { sfx, sfy, x0, y0, x1, y1, sgx, sgy };
            fill4(q, 2, 0, out);
        }
        if (b4 < n - 1) {
            int y = A_fy[n - b4];
            rot(x_at(st_fl, st_fy, st_nl, st_ny, y), y, &x0, &y0);
            rot(x_at(st_fr, st_fy, st_nr, st_ny, y), y, &x1, &y1);
            int q[8] = { x0, y0, snx, sny, sox, soy, x1, y1 };
            fill4(q, 2, 0, out);
        }
    }
    for (int j = n; j > 0; j--) {                      /* blocks, pillars and the opponent */
        unsigned f = TB(j);
        if (f & 0x10) {
            int a2 = (A_xc[j] - A_xl[j]) / 3 + A_xl[j], b0 = (A_xc[j] - A_xr[j]) / 3 + A_xr[j];
            int a4 = A_fy[j], b8 = a4 + (A_ty[j] - a4) / 7;
            int c6 = A_fy[j - 1], c = c6 - (c6 - A_ty[j - 1]) / 7;
            int c4 = (A_xc[j - 1] - A_xl[j - 1]) / 3 + A_xl[j - 1], c0 = (A_xc[j - 1] - A_xr[j - 1]) / 3 + A_xr[j - 1];
            int p[16] = { a2, a4, b0, a4, a2, b8, b0, b8, c4, c6, c0, c6, c4, c, c0, c };
            box(p, 0, main);
        }
        if (f & 0x80) {
            int v = (A_xr[j] - A_xl[j]) / 7, x1 = v + A_xl[j], x2 = x1 + (v >> 3);
            int w = (A_xr[j - 1] - A_xl[j - 1]) / 7, x3 = w + A_xl[j - 1], x4 = x3 + (w >> 3);
            int p[16] = { x1, A_fy[j], x2, A_fy[j], x1, A_ty[j], x2, A_ty[j],
                          x3, A_fy[j - 1], x4, A_fy[j - 1], x3, A_ty[j - 1], x4, A_ty[j - 1] };
            box(p, 1, main);
            int r1 = A_xr[j] - v - (v >> 3), r3 = A_xr[j - 1] - w - (w >> 3);
            int q[16] = { r1, A_fy[j], r1 + (v >> 3), A_fy[j], r1, A_ty[j], r1 + (v >> 3), A_ty[j],
                          r3, A_fy[j - 1], r3 + (w >> 3), A_fy[j - 1], r3, A_ty[j - 1], r3 + (w >> 3), A_ty[j - 1] };
            box(q, 1, main);
        }
        if (main) {
            if (rv->opp_seg - seg == j && rv->draw_opponent) rv->draw_opponent();
            continue;
        }
        if (j - n - rv->starter_seg + seg == 1) {                       /* a92a: the starter */
            int p = n - j;
            const Image *im = starter_pic(rv, p);
            if (im) {
                int y = A_fy[p], x = x_at(st_fl, st_fy, st_nl, st_ny, y) + 1;
                if (g_aq.bank) rot(x, y, &x, &y);
                road_sprite(im, x, y, im->w, im->h, g_aq.abs_bank < 6 ? 0 : g_aq.bank);
            }
        }
        if (seg - rv->opp_seg == j && rv->draw_opponent_mirror) rv->draw_opponent_mirror();
    }
    if (main && rv->opp_seg == seg && rv->draw_opponent) rv->draw_opponent();
#undef TF
#undef TB
}

/* 11fc's Aqueduct branch for the windshield: 18ec, then a094 (the tilt, 9f08 / 9f9a, the turned
 * view, 36f0 / 8b16 sky, 905c painter). The tilt moves the car (g_aq.slide / slow). */
static void aq_draw(const RoadView *rv)
{
    AqView *q = &g_aq;
    q->slide = q->slow = 0;
    aq_project(rv);
    piv_x = (A_xl[14] + A_xr[14]) >> 1;
    piv_y = A_fy[14];
    q->s_floor = q->floor; q->s_left = q->left; q->s_right = q->right; q->s_top = q->top;
    q->floor = q->left = q->right = q->top = q->wall_l = q->wall_r = 0;
    if (q->car_l > 0x5c8 && q->car_r < 0xcf8) q->bank = 0;
    else if (q->car_l <= 0x5c8) aq_wall_left(rv);
    else aq_wall_right(rv);
    aq_stripe(rv);
    aq_tilt();
    sinv = road_aq_sin(q->bank);
    cosv = road_aq_sin(q->bank + 0x5a);
    for (int i = 0; i < 15; i++) {
        rot(A_xl[i], A_fy[i], &R_lx[14 - i], &R_ly[14 - i]);
        rot(A_xr[i], A_fy[i], &R_rx[14 - i], &R_ry[14 - i]);
        rot(A_wl[i], A_ty[i], &R_wlx[14 - i], &R_wly[14 - i]);
        rot(A_wr[i], A_ty[i], &R_wrx[14 - i], &R_wry[14 - i]);
    }
    aq_sky(rv);
    aq_paint(rv, 14);
    aq_mirror(rv);
}

int road_aq_ahead(const RoadView *rv, unsigned flags)
{
    AqView *q = &g_aq;
    int c = (q->s_right + q->s_left + 0x12c0) >> 1, d = 0xc - (rv->sub >> 3);
    int fy[2], xl[2], xr[2], xc[2], ty[2];
    for (int i = 0; i < 2; i++, d += 8) {
        fy[i] = aq_py(rv, d, q->s_floor + 2000);
        xl[i] = aq_px(rv, d, q->s_left + 0x5c8);
        xr[i] = aq_px(rv, d, q->s_right + 0xcf8);
        xc[i] = aq_px(rv, d, q->s_right + c);
        ty[i] = aq_py(rv, d, q->s_top + 0x67c);
    }
    int out[48], n;
    unsigned hit = 0;
    if (flags & 0x10) {
        int y1 = (ty[1] - fy[1]) / 7 + fy[1], y0 = (ty[0] - fy[0]) / 7 + fy[0];
        int p[8] = { (xc[1] - xl[1]) / 3 + xl[1], y1, (xc[1] - xr[1]) / 3 + xr[1], y1,
                     (xc[0] - xl[0]) / 3 + xl[0], y0, (xc[0] - xr[0]) / 3 + xr[0], y0 };
        if ((n = clip(p, 4, out)) > 0) hit = bottom_hit(out, n);
    } else if (flags & 0x80) {
        int v = (xr[1] - xl[1]) / 7, w = (xr[0] - xl[0]) / 7;
        int p[8] = { v + xl[1], fy[1], v + xl[1] + (v >> 3), fy[1], w + xl[0] + (w >> 3), fy[0], w + xl[0], fy[0] };
        if ((n = clip(p, 4, out)) > 0) hit = bottom_hit(out, n);
        int r1 = xr[1] - v - (v >> 3), r0 = xr[0] - w - (w >> 3);
        int q2[8] = { r1, fy[1], xr[1] - v, fy[1], xr[0] - w, fy[0], r0, fy[0] };
        if ((n = clip(q2, 4, out)) > 0) hit |= bottom_hit(out, n);
    }
    return (int)hit;
}

/* ------------------------------------------------------------------ the rear-view mirror */

int road_mproj_x(const RoadView *rv, int d, int lat, int turn_)        /* 1dcb:06ba */
{
    int16_t t = (int16_t)(turn_ * d);
    int cam = rv->x;
    if (rv->race == RACE_AQUEDUCT) cam = SDL_clamp(cam, 0x564, 0xd5c);
    if (d == 0) d = 1;
    return -((int16_t)(cam - (int16_t)(lat + (t >> 2))) / d - 0x118);
}

int road_mproj_y(const RoadView *rv, int d, int h, int rise)          /* 1dcb:070e */
{
    int16_t t = (int16_t)(rise * d);
    if (d == 0) d = 1;
    return (int16_t)(t - rv->h + h) / d + 0x24;
}


/* A picture scaled to w x h with the middle of its bottom edge at (cx, by), turned by `angle`
 * degrees the way 0379 turns the view, clipped to the window or the mirror (2a21:a9ac). */
void road_sprite(const Image *im, int cx, int by, int w, int h, int angle)
{
    if (!im || w <= 0 || h <= 0 || !V) return;
    int sn = angle ? road_aq_sin(angle) : 0, cs = angle ? road_aq_sin(angle + 0x5a) : 256, r = angle ? w + h : 0;
    int x0 = angle ? cx - r : cx - w / 2, x1 = angle ? cx + r : x0 + w - 1;
    int y0 = angle ? by - r : by - h, y1 = angle ? by + r : by - 1;
    for (int ty = SDL_max(y0, CLIP_Y0); ty <= y1 && ty <= CLIP_Y1; ty++)
        for (int tx = SDL_max(x0, CLIP_X0); tx <= x1 && tx <= CLIP_X1; tx++) {
            int u, v;
            if (angle) {
                int dx = tx - cx, dy = ty - by;
                u = ((dx * cs - dy * sn) >> 8) + w / 2;
                v = ((dx * sn + dy * cs) >> 8) + h;
            } else { u = tx - x0; v = ty - y0; }
            if (u < 0 || u >= w || v < 0 || v >= h) continue;
            uint8_t c = im->pixels[(v * im->h / h) * im->w + u * im->w / w];
            if ((im->flags & IMAGE_SPRITE) && !c) continue;
            V->fb[ty * SCREEN_W + tx] = c;
        }
}

static void copy_pic(const Image *im, int sx, int sy, int w, int h, int dx, int dy, int clipped)   /* a0ae */
{
    if (!im) return;
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            int x = dx + c, y = dy + r, u = sx + c, v = sy + r;
            if (u < 0 || u >= im->w || v < 0 || v >= im->h || x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) continue;
            if (clipped && (x < CLIP_X0 || x > CLIP_X1 || y < CLIP_Y0 || y > CLIP_Y1)) continue;   /* 3602 */
            V->fb[y * SCREEN_W + x] = im->pixels[v * im->w + u];
        }
}

/* 1dcb:57a6: the mirror's glass and frame (2005, put on the spare page at (0xe8, 0x66) by 370a) and
 * the strip of hills at its horizon (2063, Mulholland 2037, at (0xe8, 0x81)); the Aqueduct has
 * stars instead (85a0). `horizon` is the farthest slice's ground row (DS:9efe). */
static void mirror_backdrop(const RoadView *rv, int horizon)
{
    const Image *glass = game_picture(G_, 0x7d5);
    copy_pic(glass, 0x20, 0, 0x10, 2, 0x110, 0x12, 0);
    copy_pic(glass, 0, 2, 0x50, 0x19, 0xf0, 0x14, 0);
    if (rv->race == RACE_AQUEDUCT) {
        int s = (int16_t)(rv->heading * 2) % 0x140;
        stars(0x7f8e, 9, -(s >> 2), 2);                /* colours DS:ad48 / ad54 */
        stars(0x7fb2, 3, -(s >> 2), 5);
        return;
    }
    int mul = rv->race == RACE_MULHOLLAND;
    copy_pic(game_picture(G_, mul ? 0x7f5 : 0x80f), 0, 0, 0x50, mul ? 0x14 : 2, 0xf0, horizon - (mul ? 10 : 2), 1);
}

static void mirror_frame(void)                         /* 1dcb:5a2c */
{
    clip_view();
    colour = 2; vline(0xf0, 0x16, 0x2a);
    colour = 4; vline(0xf1, 0x16, 0x2a);
    colour = 4; vline(0x13e, 0x16, 0x2a);
    colour = 2; vline(0x13f, 0x16, 0x2a);
}

/* 1dcb:a85c (a92a in the Aqueduct): the starter seen behind you once she has left the window, in
 * one of four sizes (DS:9a2a, loaded by a5aa). */
static const Image *starter_pic(const RoadView *rv, int p)
{
    if (!rv->starter_gone || p == 6) return NULL;
    int idx = p < 3 ? p >> 1 : p - 1;
    if (idx > 3) idx = 3;
    static const uint16_t road_pics[4] = { 0x7d0, 0x7e6, 0x809, 0x80a }, aq_pics[4] = { 0x81e, 0x81f, 0x820, 0x821 };
    return game_picture(G_, rv->race == RACE_AQUEDUCT ? aq_pics[idx] : road_pics[idx]);
}

/* By distance behind (0 at d 8 .. 7 at d 56). The 0x79e row (a7ea) is written twice per slice from
 * slice 1 on, so the painter's "slice k" entries of it lag behind (kept as the original has it). */
static int mxl[8], mxr[8], mgy[8], mxsl[8], mxsr[8], my19[8], my195[16], mtly[8], mtry[8], mxfl[8], mxfr[8];

static void mirror_project(const RoadView *rv)         /* 1dcb:0ce4 */
{
    int seg = rv->seg, mul = rv->race == RACE_MULHOLLAND, edge = mul ? 200 : 400, here = HT(seg);
    int rise = HT(seg) - here, turn_ = HD(seg) - rv->heading, d = 8;
    if (mul) turn_ = turn_ > 0x3c ? 0x46 : turn_ < -0x3c ? -0x46 : turn_;
    int s8 = SDL_abs(rv->sub) >> 3;
    for (int k = 0; k < 8; k++) {
        int s = seg - k, f = F(s) & 0xff, a, b;
        if (k) {
            if (k == 1) d = s8 + 8;
            else if (k == 5) d = d + 8 - s8;
            else d += 8;
            rise = HT(s) - here;
            turn_ = HD(s) - rv->heading;
        }
        if (!(f & 0xe0)) { a = 0xce4; b = 0x5dc; }
        else if (f & 0x80) { a = 0xaf0; b = 2000; }
        else if (f & 0x20) { a = b = 0xce4; }
        else { a = b = 0x5dc; }
        mxr[k] = road_mproj_x(rv, d, a, turn_);
        mxl[k] = road_mproj_x(rv, d, b, turn_);
        mgy[k] = road_mproj_y(rv, d, 2000, rise);
        if (k == 0) my195[0] = road_mproj_y(rv, d, 0x79e, rise);
        else my195[2 * k - 1] = my195[2 * k] = road_mproj_y(rv, d, 0x79e, rise);
        mxsr[k] = road_mproj_x(rv, d, edge + 0xce4, turn_);
        mxsl[k] = road_mproj_x(rv, d, 0x5dc - edge, turn_);
        my19[k] = road_mproj_y(rv, d, 0x76c, rise);
        mtly[k] = road_mproj_y(rv, d, 2000 - GL(seg + k), rise);     /* these two run forwards */
        mtry[k] = road_mproj_y(rv, d, 2000 - GR(seg + k), rise);
        mxfl[k] = road_mproj_x(rv, d, (edge - 0x177) * -4, turn_);
        mxfr[k] = road_mproj_x(rv, d, (edge + 0x339) * 4, turn_);
    }
}

static void mirror_strip(const RoadView *rv, int k, int f)   /* 1dcb:4852 */
{
    int mul = rv->race == RACE_MULHOLLAND, seg = rv->seg;
    int l = mxl[k], r = mxr[k], y = mgy[k], yn = mgy[k - 1];
    int l16 = l << 4, r16 = r << 4, dl = slope(mxl[k - 1], yn, l, y), dr = slope(mxr[k - 1], yn, r, y);
    int sl = mxsl[k], sr = mxsr[k], sl16 = sl << 4, sr16 = sr << 4, dsl = 0, dsr = 0;
    if (mul) { dsl = slope(mxsl[k - 1], yn, sl, y); dsr = slope(mxsr[k - 1], yn, sr, y); }
    int w = 0, cl = 0;
    if (f & 8) { w = dash_w(13 + k); cl = ((r + l) >> 1) - (w >> 1); }
    if (yn - y < 0) return;
    int white = (f & 0x10) && seg >= (mul ? 0x3ad : 0x169);
    for (int n = yn - y; n != 0 && y < 0x2b; y++, n--) {
        if (mul && !(f & 0x80)) {                      /* HT(pos) - 0xb <= a56c always holds */
            colour = 2;
            hline(sl - 6, l, y);
            hline(r, sr + 6, y);
        }
        colour = 9;
        if (!mul) { hline(0xf0, l, y); hline(r, 0x13f, y); }
        else {
            if (mtly[k - 1] == yn) hline(0xf0, l, y);
            if (mtry[k - 1] == yn) hline(r, 0x13f, y);
        }
        colour = white ? 15 : 1;
        hline(l, r, y);
        if (f & 8) { colour = 4; hline(cl, cl + w, y); }
        l16 += dl; l = l16 >> 4;
        r16 += dr; r = r16 >> 4;
        if (mul) { sl16 += dsl; sl = sl16 >> 4; sr16 += dsr; sr = sr16 >> 4; }
        cl = ((r + l) >> 1) - (w >> 1);
    }
}

/* 5aee's hillsides for Mulholland, one side (-1 left, 1 right). */
static void mirror_hill(const RoadView *rv, int k, int side)
{
    int s = rv->seg - 1 - k, fall = rv->fall, edge = side < 0 ? CLIP_X0 : CLIP_X1;
    const int *xs = side < 0 ? mxsl : mxsr, *xe = side < 0 ? mxl : mxr, *ty = side < 0 ? mtly : mtry;
    if (fall == -side || mgy[k - 1] == ty[k - 1]) return;
    int bridge = k != 1 && (F(s - 1) & 0x80);
    int inside = side < 0 ? 0xf0 < xs[k] : xs[k] < 0x13f, inside1 = side < 0 ? 0xf0 < xs[k - 1] : xs[k - 1] < 0x13f;
    if (!bridge) {
        POLY(4, 0xe, edge, ty[k - 1], xs[k - 1] - fall, mgy[k - 1], xs[k] + fall, mgy[k], edge, ty[k]);
        if (k == 1 && inside) POLY(3, 0xe, edge, ty[k - 1], xs[k], mgy[k], edge, mgy[k]);
        else if (k >= 2 && mgy[k - 2] == ty[k - 2] && inside1) POLY(3, 0xe, edge, ty[k - 1], xs[k - 1], mgy[k - 1], edge, mgy[k - 1]);
    } else {
        POLY(4, 0xe, edge, ty[k - 1], xs[k - 1], mgy[k - 1], xs[k], mgy[k], edge, ty[k]);
        POLY(6, 0xe, edge, ty[k - 1], xs[k - 1], mgy[k - 1], xe[k - 1], mgy[k - 1], xe[k - 2], mgy[k - 2],
             xe[k - 2], CLIP_Y1, edge, CLIP_Y1);
    }
}

/* 1dcb:5aee: the mirror, farthest slice (7) to nearest; per slice the segment one further back
 * than its index (the original's pointers start at seg - 8). */
static void mirror_draw(const RoadView *rv)
{
    int seg = rv->seg, mul = rv->race == RACE_MULHOLLAND, drag = !mul;
    mirror_project(rv);
    clip_mirror();
    mirror_backdrop(rv, mgy[7]);
    for (int k = 7; k > 0; k--) {
        int s = seg - 1 - k;
        unsigned f = F(s), lf = L(s), rf = R(s);
        if (mul && !(f & 0x80)) { mirror_hill(rv, k, -1); mirror_hill(rv, k, 1); }
        mirror_strip(rv, k, (int)f);
        if (!(f & 4) || !(f & 1)) {
            if ((f & 0xf000) && drag)
                gantry_at(mxl[k], mxr[k], dash_w(13 + k), my19[k] * 4 - mgy[k] * 3, mgy[k], k + 5, f & 0xf000, (int)f);
            if (k < 7) {
                int ic = (k >> 1) + 7, width = mxr[k] - mxl[k], y = mgy[k];
                int flat_l = mgy[k - 1] == mtly[k - 1], flat_r = mgy[k - 1] == mtry[k - 1];
                if (!mul && (lf & 0x20)) object(mxl[k], y, ic, -1, 0, 0);
                if ((lf & 0x100) && !(mul && !flat_l))
                    object(lf & 0x200 ? mxsl[k] * 2 - mxl[k] : mxsl[k], y, ic, -1, mul ? 0x15 : 4, 1);
                if (mul && (lf & 0x800)) object(mxl[k], y, ic, -1, 3, 0);
                if (mul && (lf & 0x1000)) object(mxl[k], y, ic, 1, 6, 0);
                if ((lf & 1) && flat_l) object(mxl[k], y, ic, 1, 1, 1);
                if ((rf & 1) && flat_r) object(mxr[k], y, ic, -1, 1, 1);
                if (drag || flat_l) {
                    if (lf & 0x40) object(mxsl[k], y, ic, -1, 10, 0);
                    if (lf & 0x80) object(lf & 2 ? (mxsl[k] + CLIP_X0) >> 1 : mxsl[k], y, ic, -1, mul ? 0x16 : 0xb, 0);
                }
                if (drag || flat_r) {
                    if (rf & 0x40) object(mxsr[k], y, ic, 1, 10, 0);
                    if (rf & 0x80) object(rf & 2 ? (mxfr[k] + CLIP_X1) >> 1 : mxsr[k], y, ic, 1, mul ? 0x16 : 0xb, 0);
                }
                if (mul && (lf & 0x10)) rocks(mxl[k], y, ic, 1);
                if (mul && (rf & 0x10)) rocks(mxr[k], y, ic, -1);
                if (drag || flat_l) {
                    if (lf & 4) object(mxl[k], y, ic, -1, 7, 1);
                    if (lf & 8) object(mxl[k], y, ic, -1, 8, 1);
                }
                if (drag || flat_r) {
                    if (rf & 4) object(mxr[k], y, ic, 1, 7, 1);
                    if (rf & 8) object(mxr[k], y, ic, 1, 8, 1);
                }
                if ((f & 2) && (drag || flat_r)) object(mxsr[k] - (width >> 3), y, ic, 1, 9, 0);
                if (!mul && (rf & 0x20)) object(mxr[k], y, ic, 1, 0, 0);
                if ((rf & 0x100) && !(mul && !flat_r))
                    object(rf & 0x200 ? mxsr[k] * 2 - mxr[k] : mxsr[k], y, ic, 1, mul ? 0x15 : 4, 1);
                if (rf & 0x2000) object(mxr[k], y, ic, 1, 0x12, 0);
                if (rf & 0x4000) object(mxr[k], y, ic, 1, 0x13, 0);
                if (rf & 0x800) object(mxr[k], y, ic, 1, 3, 0);
                if (rf & 0x1000) object(mxr[k], y, ic, -1, 6, 0);
                if (mul && (f & 4)) object((OB(s) * width >> 4) + mxl[k], y, ic, OB(s) < 9 ? -1 : 1, 7, 1);
                if (mul && (f & 0x1000)) object((OB(s) * width >> 4) + mxl[k], y, ic, OB(s) < 9 ? -1 : 1, 0x14, 1);
                if (f & 0x100) object(mxr[k], y, ic, 1, 5, 0);
                if (f & 0x200) object(mxr[k], y, ic, 1, 0x10, 0);
                if (f & 0x400) object(mxr[k], y, ic, 1, 0x11, 0);
                if (f & 0x800) object(mxr[k], y, ic, 1, 0xf, 0);
                if (seg - k == rv->starter_seg) {                          /* a85c */
                    const Image *im = starter_pic(rv, k - 1);
                    if (im) road_sprite(im, (mxr[k] + mxl[k]) >> 1, 0x27, im->w, im->h, 0);
                }
            }
            if (mul) {
                int w = dash_w(13 + k);
                if ((f & 0x80) && (F(s + 1) & 0x80) && (F(s + 2) & 0x80))
                    bridge_sides(mxl[k - 1], mxr[k - 1], my19[k - 1] * 4 - mgy[k - 1] * 3, mgy[k - 1],
                                 mxl[k], mxr[k], my19[k] * 4 - mgy[k] * 3, mgy[k], seg - k, k < 4, w);
                if ((f & 0x80) && !(F(s - 1) & 0x80)) bridge_start(mxl[k], mxr[k], my19[k] * 4 - mgy[k] * 3, mgy[k], w);
                if ((rf & 0x8000) && !(f & 0x80)) {        /* the guard rail */
                    if (((rail_start - k) - seg + 7) % 2 != 0) {
                        int a = -10 / (k + 1) + mxsr[k - 1];
                        const int post[] = { a, my195[k - 1] - 1, a, mgy[k - 1], mxsr[k - 1], mgy[k - 1], mxsr[k - 1], my195[k - 1] - 1 };
                        poly(post, 4, 5, 2);
                    }
                    const int rail[] = { mxsr[k], my19[k] - 1, mxsr[k], my195[k], mxsr[k - 1], my195[k - 1], mxsr[k - 1], my19[k - 1] - 1 };
                    poly(rail, 4, 5, 5);
                    colour = 2;
                    line(mxsr[k], my19[k] - 1, mxsr[k - 1], my19[k - 1] - 1);
                    line(mxsr[k], my195[k], mxsr[k - 1], my195[k - 1]);
                }
            }
        }
        if (seg - k == rv->opp_seg && rv->draw_opponent_mirror) rv->draw_opponent_mirror();
    }
    mirror_frame();
}

/* 1dcb:1b02 + 9ce8: the Aqueduct in the mirror, 8 slices behind (d 2, then as 0ce4), the walls
 * at the offsets the windshield used, turned with its tilt round the mirror's own far middle. */
static void aq_mirror(const RoadView *rv)
{
    const AqView *q = &g_aq;
    int c = (q->s_right + q->s_left + 0x12c0) >> 1, turn_ = -rv->heading, s8 = SDL_abs(rv->sub) >> 3, d = 2;
    st_nl = road_mproj_x(rv, 2, c + q->s_left - 0x28, turn_);
    st_nr = road_mproj_x(rv, 2, q->s_right + c + 0x28, turn_);
    for (int i = 0; i < 8; i++) {
        if (i == 1) d = s8 + 8;
        else if (i == 5) d = d + 8 - s8;
        else if (i) d += 8;
        A_fy[i] = road_mproj_y(rv, d, q->s_floor + 2000, q->pitch);
        A_xl[i] = road_mproj_x(rv, d, q->s_left + 0x5c8, turn_);
        A_xr[i] = road_mproj_x(rv, d, q->s_right + 0xcf8, turn_);
        A_xc[i] = road_mproj_x(rv, d, c + q->s_right, rv->heading);
        A_ty[i] = road_mproj_y(rv, d, q->s_top + 0x67c, q->pitch);
        A_wl[i] = road_mproj_x(rv, d, 0x438, turn_);
        A_wr[i] = road_mproj_x(rv, d, 0xe88, turn_);
    }
    st_fl = road_mproj_x(rv, d, c + q->s_left - 0x28, turn_);
    st_fr = road_mproj_x(rv, d, c + q->s_right + 0x28, turn_);
    st_ny = A_fy[0];
    st_fy = A_fy[7];
    piv_x = (A_xl[7] + A_xr[7]) >> 1;
    piv_y = A_fy[7];
    for (int i = 0; i < 8; i++) {
        rot(A_xl[i], A_fy[i], &R_lx[7 - i], &R_ly[7 - i]);
        rot(A_xr[i], A_fy[i], &R_rx[7 - i], &R_ry[7 - i]);
        rot(A_wl[i], A_ty[i], &R_wlx[7 - i], &R_wly[7 - i]);
        rot(A_wr[i], A_ty[i], &R_wrx[7 - i], &R_wry[7 - i]);
    }
    clip_mirror();
    mirror_backdrop(rv, A_fy[7]);
    aq_paint(rv, 7);
    mirror_frame();
}
