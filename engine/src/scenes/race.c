/* The street races: the drag race, Mulholland Drive and the Aqueduct (1001:be82 setup, 2a21:77f4 race screen,
 * 1001:8ef4 start, 1001:d908 per frame, the timer interrupt 0001:2620 for your engine, steering and
 * the roadside collisions (1dcb:148a), 1001:cb98 / d4cc / cf9a for the opponent, 1001:b584 result,
 * 1001:af0a the crash).
 *
 * Timing as in SR.EXE on a fast PC: the timer runs at 72.8 Hz (PIT divisor 0x4000), a frame waits 8
 * ticks (2a21:1924(8) at the end of 1dcb:11fc: 9.1 Hz), the engine steps every 12th tick in the
 * interrupt (6.07 Hz, its own clock here), distance per frame from table DS:8016.
 * The engine models, the opponent's driving, the start, the result and the road view (road.c) are
 * ports. */
#include <math.h>
#include <string.h>

#include "../assets/exe.h"
#include "../data.h"
#include "../game.h"
#include "../options.h"
#include "../rivals.h"
#include "../road.h"
#include "../sound.h"
#include "../ui/dialog.h"
#include "../ui/font.h"
#include "race.h"

#define TIMER_HZ   (1193182.0 / 65536.0 * 4)   /* 72.8: the timer as SR.EXE sets it */
#define FRAME_HZ   (TIMER_HZ / 8)            /* 9.1: 11fc waits 8 ticks a frame (2a21:1924(8)) */
#define ENGINE_HZ  (TIMER_HZ / 12)           /* 6.07: the interrupt's engine step, every 12th tick */
#define REDLINE    0x1644
#define VIEW_BOTTOM 100                    /* the dashboard starts here */
#define CAM_X     0xa00                    /* DS:a564 at the start */
#define CAM_H     0x76c                    /* a566: your eyes */
#define GROUND    2000
#define OPP_X     2200                     /* a9cc */
#define CLIP_TOP  0x12
#define CLIP_LAST 0x65

static int16_t ds16(uint16_t off) { return (int16_t)exe_ds_u16(off); }

/* ------------------------------------------------------------------ engine model (1001:c294) */

typedef struct { int lo, hi, accel, decel; } Band;

typedef struct {
    int  speed, rpm, gear, gears;
    int  shift_rpm;             /* +0x1e: DS:52d8 by engine grade */
    int  automatic, auto_shift_rpm, auto_mode;  /* auto_mode 1 = N, 2 = D */
    int  ratio[5], cur_ratio, band, accel, decel;
    Band bands[5][3];
} Engine;

static int grade(const PartInst *p) { return p->part >= 0 ? g_parts[p->part].grade : -1; }

static void engine_setup(Engine *e, const Car *c)
{
    memset(e, 0, sizeof *e);
    int eg = grade(&c->engine), tg = grade(&c->trans), tires = grade(&c->tires);
    int muffler = grade(&c->muffler), exhaust = grade(&c->exhaust), diff = grade(&c->diff);
    if (eg < 0) eg = 0;
    if (tg < 0) tg = 0;
    long power = car_power(c);
    long vmax = power * 13 / 10;
    if (vmax > 130) vmax = 130;
    for (int g = 0; g < 5; g++) {
        long k = ds16((uint16_t)(0x5242 + 2 * (tg * 5 + g))) * vmax;
        if (!k) k = 20;
        e->ratio[g] = (int)(570000L / k);
        int lo = ds16((uint16_t)(0x5288 + 4 * eg)), hi = ds16((uint16_t)(0x528a + 4 * eg));
        int base = (int)(ds16((uint16_t)(0x526a + 2 * (eg * 5 + g))) * power / 100);
        int a0 = base / (g == 0 ? 1 : 5);
        if (g == 1) a0 *= tires == 2 ? 3 : 2;
        if (muffler < 0) a0 += a0 * 5 / 100;            /* straight pipes */
        else if (muffler == 1) a0 += a0 * 3 / 100;
        if (exhaust == 1) a0 += a0 * 5 / 100;
        if (diff == 0) a0 -= a0 * 5 / 100;
        else if (diff == 2) a0 += a0 * 5 / 100;
        int dec = g == 0 ? base : base / (5 - g);
        e->bands[g][0] = (Band){ 0, lo, a0, dec };
        e->bands[g][1] = (Band){ lo, hi, base, dec };
        e->bands[g][2] = (Band){ hi, REDLINE, base / (g == 0 ? 1 : 10), dec };
    }
    e->automatic = tg == 0;
    e->auto_shift_rpm = ds16((uint16_t)(0x528a + 4 * eg));
    e->shift_rpm = ds16((uint16_t)(0x52d8 + 2 * eg));
    e->gears = ds16((uint16_t)(0x52de + 2 * tg));
    /* 1001:bc88 */
    e->rpm = 0x5dc;
    e->cur_ratio = e->ratio[0];
    e->accel = e->bands[0][0].accel;
    e->decel = e->bands[0][0].decel;
    e->auto_mode = 1;
}

/* Band of the current gear for an rpm: the first whose top is at or above it. */
static void pick_band(Engine *e, int force)
{
    int b = 0;
    while (b < 2 && e->bands[e->gear][b].hi < e->rpm) b++;
    if (b != e->band || force) {
        e->band = b;
        e->accel = e->bands[e->gear][b].accel;
        e->decel = e->bands[e->gear][b].decel;
    }
}

/* ------------------------------------------------------------------ race state */

enum { R_STAGE, R_READY, R_GO, R_RACING, R_CRASH, R_DONE };

static Game *G;
static Car *you;
static Engine me, opp;
static int rival, stake, bet;
static int state;
static Uint64 phase_start, last_frame;
static double frame_acc, engine_acc;
static int frame_no, wait_ticks;
static int steps;                       /* DS:a1be: engine steps since the start */
static long speed_sum;                  /* DS:adae */
static int pos, sub, opp_pos, opp_sub;  /* DS:a55e/a560, a9c6/a9c8 */
static int finished, you_first, time_tenths, avg_speed, covered;
static int limiter_count;               /* DS:2006 */
static int result;                      /* DS:a1bc / a2a0 */
static int opp_steps_done, opp_limit, skill;
static int gas, brake, shift_pressed, shift_latch;
static int blowout_at;                  /* DS:a10a: worn tires give out here */
static char ready_text[32];
static int autodrive;
static int starter_slide, flag_dropped;   /* DS:a2b4, ad0a */
static int race = RACE_DRAG;            /* DS:add6 */
static int start_seg, finish_seg;       /* DS:9ae2, a2aa (1001:bc88) */

/* Where you are on the road and how you steer (DS names). */
static int cam_x, heading;              /* a564 lateral position, a56a heading */
static int steer, wheel, steer_in;      /* a102 steering, a104 the wheel as drawn, 3b32 input this step */
static int grip;                        /* a10c: tires' slip, DS:521e[grade * 5 + wear / 2000] */
static int lane_l, lane_r, lane_l4, lane_r4;   /* a7da/b8d2 here, 985c/a558 four segments on */
static int fall;                        /* 7b9c: going over the edge (-1 left, 1 right) */
static int tire_flat;                   /* 9dc4 */
static int fell_steps;                  /* 1dcb:7da2's eight frames of falling */
/* The opponent beside you. */
static int opp_x, cam_prev, opp_aim;    /* a9cc, a33e, 9702 */
static int opp_flip, opp_wander, opp_close;     /* 8012, b19c, a7d6 */
static int opp_kind = 2, opp_kind_prev = 2;     /* a7d4, 9a80: the picture shown (1dcb:af98) */
static int opp_ahead, opp_hidden;       /* b8c6; af98 skips drawing this frame */
static int rear_hit, alongside;         /* 9a48, b8c0 */
static int side_frames, bumped, in_bend;   /* 809c, 523e, 8014 */
static int shoved;                      /* 809e: steps of braking after he shoves you */
static int crash_speed, crash_ticks;    /* the crash screen (1001:af0a) */
/* The police on Mulholland (DS:ab2c): 1 waiting at segment police_seg (9aca), 2 they've taken the
 * opponent's place behind you (1001:d85c), 3 the siren is on; they pass you and you're stopped. */
static int police, police_seg, fined;
static int force_police;                /* testing: the next Mulholland race has them */
static int ticket_open, ticket_paid, fine;
static Uint64 ticket_start;
void race_force_police(void) { force_police = 1; }
/* The Aqueduct. */
static int aq_side;                     /* b8c8: over the top of the left (1) or right (-1) wall */
static int aq_roll, aq_gap, aq_head0;   /* 1dcb:a1c0's frames of rolling over */
static int aq_skip;                     /* 809a: 1001:cee0 looks ahead every other frame */
static uint8_t aq_pix[SCREEN_W * (CLIP_LAST + 1)];   /* the view as the frame drew it */
static int aq_have;
static int starter_seg;                 /* DS:9e6a: where the starter stands (0dcb:a746) */

void race_autodrive(int on) { autodrive = on; }

#define RES_WON     0x100
#define RES_LOST    0x001
#define RES_ENGINE  0x005
#define RES_TRANS   0x011
#define RES_JUMPED  0x081
#define RES_TIRES   0x009
#define RES_CENTRE  0x101               /* over the middle at the start (1001:d908) */
#define RES_CAUGHT  0x041               /* the police got you (a1bc |= 0x41) */
#define RES_CRASH   0x021               /* off the road or into something (a1bc |= 0x21) */
#define RES_HIT_OPP 0x003               /* into the opponent's back */
#define RACE_TIRE   0x008               /* a1bc |= 8: the tires gave out on the way */

static int dash_type(int model) { return (g_models[model].patch[0] >> 8) & 0xff; }
static int sprite_set(int model) { return g_models[model].patch[0] & 0xff; }

/* 1001:ce9c: distance per frame for a speed (fast machines: table DS:8016). */
static int frame_distance(int speed)
{
    if (speed < 1) return 0;
    if (speed > 0x82) speed = 0x82;
    return (int8_t)exe_ds(0x8016, 0x83)[speed];
}

static int seg_ok(int s) { return SDL_clamp(s, 0, ROAD_SEGS - 1); }
static int F_(int s) { return s < 0 || s >= ROAD_SEGS ? 0 : g_road.flags[s]; }
static int L_(int s) { return s < 0 || s >= ROAD_SEGS ? 0 : g_road.left[s]; }
static int R_(int s) { return s < 0 || s >= ROAD_SEGS ? 0 : g_road.right[s]; }
static int mulholland(void) { return race == RACE_MULHOLLAND; }
static int aqueduct(void) { return race == RACE_AQUEDUCT; }
/* The road's heading; the Aqueduct runs straight (DS:9ad8 points at a 0 word, 1dcb:8840). */
static int HD_(int s) { return aqueduct() ? 0 : g_road.heading[seg_ok(s)]; }
/* The Aqueduct's table, running on into the next one as the original's does (road.c T()). */
static unsigned AQ_(int s) { return s < 0 ? 0 : s < AQ_SEGS ? g_aqueduct[s] : (uint16_t)g_road.ground_l[seg_ok(s - AQ_SEGS)]; }

static void engine_sound_restart(void);
static void sfx_bump(void) { sound_sfx(SFX_BUMP); engine_sound_restart(); }   /* slot 16 (2c57:1246) */

/* ------------------------------------------------------------------ steering (0001:2e8c) */

/* Six times a second: holding left or right winds the steering up by 2 per step to 24; letting
 * go or steering back drops it to 0, and the wheel on the dashboard springs back by a quarter. */
static void steer_step(void)
{
    if (steer_in) {
        int cx = SDL_abs(steer);
        if (SDL_abs(steer_in + steer) > cx) {
            if (SDL_abs(steer_in) + SDL_abs(wheel) == SDL_abs(steer_in + wheel) && cx < SDL_abs(wheel)) steer = wheel;
            steer += steer_in * 2;
            if (SDL_abs(steer) > 0x18) steer = steer > 0 ? 0x18 : -0x18;
            wheel = steer;
        } else steer = 0;
        steer_in = 0;
    } else steer = 0;
    if (wheel > 0) wheel -= (wheel >> 2) + 1;
    else if (wheel < 0) wheel += ((-wheel) >> 2) + 1;
}

/* 1dcb:148a: what's beside the car (its left side 30 left of a564, its right side 70 right).
 * Returns 1 when you hit something. */
static int roadside_hit(void)
{
    if (aqueduct()) {                                                  /* 1dcb:1cd2 */
        if (g_aq.car_l < 0x439) { aq_side = 1; return 1; }             /* over the top of a wall */
        if (g_aq.car_r >= 0xded) { aq_side = -1; return 1; }
        return g_aq.hits != 0;                                         /* into something (862a / 8674) */
    }
    int mul = mulholland(), l = cam_x - 0x1e, r = cam_x + 0x46, k = mul ? 0x96 : 0x104;
    int f1 = F_(pos + 1), r1 = R_(pos + 1), gl = g_road.ground_l[seg_ok(pos)], gr = g_road.ground_r[seg_ok(pos)];
    if (gl > 0 && l < lane_l - (k >> 2)) return 1;                     /* the hillside */
    if (gr > 0 && r > (k >> 1) + lane_r) return 1;
    if (l <= lane_l - k || lane_r + k <= r) {                          /* right off the road */
        if (mul && !fall) {
            if (l < lane_l && gl < 0) fall = -1;
            if (lane_r < r && gr < 0) fall = 1;
        }
        return 1;
    }
    if (f1 & 0x80) {                                                   /* a bridge's railings */
        if (l < (k >> 2) + lane_l) return 1;
        if (r > lane_r - (k >> 1)) return 1;
    }
    if (gl != 0 || lane_l <= l) {
        if (gr != 0 || r <= lane_r) goto on_road;
        if (f1 & 2) return 1;                                          /* a roadside post */
    }
    if (r1 & 0xc0) return 1;                                           /* trees on the shoulder */
on_road:
    if (!mul) return 0;
    if (g_road_hits.object) { g_road_hits.object = 0; if (me.speed > 0x1e) return 1; }
    if (g_road_hits.bump) {
        if (me.speed > 0x32) heading += g_road_hits.bump * -0x28;
        if (me.speed > 0x14) me.speed -= 10;
        g_road_hits.bump = 0;
    }
    if (((L_(pos) & 0x10) && l < 0x79e) || ((R_(pos) & 0x10) && r > 0xb22)) return 1;   /* rocks */
    if ((f1 & 0x60) && me.speed > 0x3c) return 1;                      /* a turn-off taken too fast */
    if (!(r1 & 0x8000) || r < lane_r) return 0;
    if (me.speed > 0x27) return 1;                                     /* through the guard rail */
    sound_sfx(SFX_BUMP);
    cam_x -= 300;
    heading += 0x1e;
    me.speed = me.speed < 0xb ? 0 : me.speed - 10;
    sfx_bump();
    return 0;
}

/* ------------------------------------------------------------------ your engine (the interrupt) */

static void engine_step(void)
{
    Engine *e = &me;
    int accel = gas, brk = brake, shift = 0;
    if (shift_pressed) { shift = 1; shift_pressed = 0; }

    if (e->rpm < 2000 && e->gear > 1 && !accel) shift = 1;            /* lugging: drop a gear */
    if (e->automatic && e->gear > 0) shift = 0;
    steps++;
    speed_sum += e->speed;
    if (pos > finish_seg || shoved > 0) {                              /* past the line: stand on the brakes */
        if (pos <= finish_seg) shoved--;
        brk = 1;
        accel = shift = 0;
        steer_in = 0;
        if (e->speed > 15) e->speed -= 15;
    }
    /* Wear: hard revving wears engine and gearbox. */
    if (e->rpm > 5000) { you->engine.wear++; you->trans.wear++; }
    if ((frame_no & 15) == 0) you->engine.wear++;
    if (you->engine.wear >= 0x26ac) { result = RES_ENGINE; return; }
    if (accel) {
        e->rpm += e->accel;
        if (e->automatic && e->rpm > 0x163a) e->rpm = 0x163a;
        if (e->rpm > REDLINE) {
            e->rpm = REDLINE;                                          /* on the limiter */
            if (++limiter_count > 15 - you->engine.wear / 1000) { result = RES_ENGINE; return; }
        }
    } else if (e->rpm > 0x5dc || (e->gear > 0 && e->decel < e->rpm)) {
        e->rpm -= e->decel;
    }
    if (e->rpm < REDLINE) limiter_count = 0;
    int stalled = e->speed < 5 && !accel && e->gear > 0;

    int changing = shift || stalled;
    if (!changing && e->automatic && e->auto_mode > 1 &&
        ((accel && e->rpm > e->auto_shift_rpm && e->gear < e->gears) ||
         (!accel && e->gear > 1 && e->band == 0)))
        changing = 1;
    if (changing) {
        you->trans.wear += e->rpm > 5000 ? 15 : 3;
        if (you->trans.wear >= 0x26ac) { result = RES_TRANS; return; }
        if (!e->automatic && accel && e->rpm > 5000 &&
            100 + you->trans.wear / 100 > (int)SDL_rand(0xccd)) { result = RES_TRANS; return; }   /* missed shift */
        if (brk || (e->band == 0 && !accel)) { if (e->gear > 0) e->gear--; }
        else if (accel || (e->band > 0 && !brk)) { if (e->gear < e->gears) e->gear++; }
        if (e->automatic) {
            if (shift) e->auto_mode = e->auto_mode == 1 ? 2 : 1;
            if (stalled || e->gear == 0) e->auto_mode = 1;
            if (e->auto_mode == 1) e->gear = 0;
        } else if (stalled) {
            e->gear = 0;
        }
        e->cur_ratio = e->ratio[e->gear];
        e->rpm = e->cur_ratio * e->speed;
        if (!e->rpm) e->rpm = 0x5e1;
        pick_band(e, 1);
    } else {
        pick_band(e, 0);
    }
    if (brk) {
        e->speed -= (e->speed >> 4) + 2;
        if (e->speed < 0) e->speed = 0;
        e->rpm = e->cur_ratio * e->speed;
        if (e->rpm < 0x5dc) e->rpm = 0x5dc;
    } else if (e->gear == 0 && e->speed != 0) {
        e->speed--;                                                    /* coasting in neutral */
        e->rpm = (e->speed + 0x32) * 0x1e;
    } else if (e->cur_ratio) {
        e->speed = e->rpm / e->cur_ratio;
    }
    if (state == R_GO || state == R_RACING) {
        steer_step();
        if (autodrive != 1 && !result && roadside_hit()) result = RES_CRASH;
    }
}

/* ------------------------------------------------------------------ the opponent (1001:cb98) */

static void opp_set_speed(int target)        /* 1001:cb00 */
{
    Engine *e = &opp;
    e->gear = 1;
    for (;;) {
        e->cur_ratio = e->ratio[e->gear];
        e->rpm = e->cur_ratio * target;
        if (e->rpm < e->bands[e->gear][2].lo || e->gear == e->gears) break;
        e->gear++;
    }
    pick_band(e, 1);
    e->speed = e->rpm / e->cur_ratio;
}

/* c6f6: the opponent's top speed; DS:a33c / a2b0 hold 70% / 80% of it (the police 60% / 80%). */
static int opp_top(void) { return model_max_speed(&G->player, rival_model(&G->player, police >= 2 ? KING : rival)); }
static int opp_cruise(void) { return opp_top() * (police >= 2 ? 6000 : 7000) / 10000; }

static void opp_engine(void)
{
    Engine *e = &opp;
    int fresh = steps - opp_steps_done;
    opp_steps_done = steps;
    int before = e->speed;
    if (mulholland() && (F_(opp_pos) & 0x60) && e->speed >= 0x3c) {   /* a turn-off: 59 at most */
        opp_set_speed(0x3b);
        fresh = 0;
    }
    for (int k = 0; k < fresh; k++) {
        if (!aqueduct() && HD_(opp_pos + 1) != HD_(opp_pos)) {         /* into a bend: ease off once */
            if (!in_bend) {
                in_bend = 1;
                int target = opp_top() - (pos < opp_pos ? 0x1e : 3);
                if (target < e->speed) opp_set_speed(target);
                continue;
            }
        } else in_bend = 0;
        int gap = opp_pos - pos, target = -1;
        if (gap < 13) {
            if (gap > -5 && gap < 5) {                                 /* side by side: stay in reach */
                target = 10 + me.speed;
                int cruise = opp_cruise();
                if (pos > 0x1d && target < cruise) target = cruise;
                if (target >= e->speed) target = -1;
            }
        } else {
            int cruise = me.speed < 0x61 && rival != KING ? opp_cruise() : opp_top() * 8000 / 10000;
            if (e->speed > cruise) target = cruise;                    /* way ahead: no need to show off */
        }
        if (target < 0 && police >= 2 && me.speed >= 0x33 && e->speed > me.speed + 0x20)
            target = me.speed + 0x20;                                  /* cb98: the police keep just ahead of you */
        if (target >= 0) { opp_set_speed(target); continue; }
        e->rpm += e->accel;
        if (e->rpm > opp_limit) e->rpm = opp_limit;
        if (e->rpm > e->bands[e->gear][e->band].hi && e->band < 2) {
            e->band++;
            e->accel = e->bands[e->gear][e->band].accel;
        }
        if (e->gear < e->gears &&
            ((e->automatic && e->rpm >= e->auto_shift_rpm) || e->rpm >= opp_limit)) {
            e->gear++;
            if (!e->automatic) opp_limit = e->gear < e->gears ? (skill - 10) * 200 + e->shift_rpm : 0x1518;
            e->cur_ratio = e->ratio[e->gear];
            e->rpm = e->cur_ratio * e->speed;
            pick_band(e, 1);
        }
        e->speed = e->rpm / e->cur_ratio;
    }
    if (opp_pos >= finish_seg) {                                       /* through the lights: brake */
        int d = pos + 5 < opp_pos ? 5 : 10;
        e->speed = before > d ? before - d : 0;
    }
}

/* ------------------------------------------------------------------ per frame (1001:d908) */

static void finish_line(void)            /* 1001:b1b2 */
{
    if (finished) return;
    finished = 1;
    avg_speed = steps ? (int)(speed_sum / steps) : 0;
    time_tenths = steps * 10 / 6;
    int lead = (opp_pos - pos) * 8 + (opp_sub - sub) / 8;
    you_first = lead < 1;
    if (!you_first) {
        int c = opp_pos - start_seg ? (pos - start_seg) * 100 / (opp_pos - start_seg) : 0;
        covered = c > 99 ? 99 : c;
    } else {
        covered = 100;
    }
}

/* 1dcb:12ca: a new segment - the lanes here and four segments on, and past the line (or with the
 * autodrive cheat) the car is steered along the road. */
static int aq_steer(int x, int delta, int seg, int other_x, int other_seg);
/* DS:a7ac: the starter is still in the window (0dcb:a746 clears it once she's slid out). */
static int starter_there(void) { return (starter_slide & ~3) * 2 <= 0x58; }

static void new_segment(void)
{
    int narrow = F_(pos) & 0xe0, narrow4 = F_(pos + 4) & 0x80, aq = aqueduct();
    lane_l = narrow ? 2000 : 0x5dc;
    lane_r = narrow ? 0xaf0 : 0xce4;
    lane_l4 = narrow4 ? 2000 : 0x5dc;
    lane_r4 = narrow4 ? 0xaf0 : 0xce4;
    if (aq) {                                                          /* the tops of the walls */
        lane_l = lane_l4 = g_aq.wall_l + 0x438;
        lane_r = lane_r4 = g_aq.wall_r + 0xe88;
    }
    if ((autodrive == 1 && !starter_there()) || finish_seg < pos) {
        int t = ((aq ? 0x5c8 : 0x5dc) + (aq ? 0xcf8 : 0xce4)) >> 1;
        if (aq || !(F_(pos) & 0x80)) t = t < opp_x ? t - 0x140 : t + 0xa0;
        if (aq) cam_x += aq_steer(cam_x, t - cam_x, pos, opp_x, opp_pos);
        else if (SDL_abs(cam_x - t) < 0x65) cam_x = t;
        else cam_x = t + (t < cam_x ? 100 : -100);
        int hd = aq ? 0 : HD_(pos);                                    /* the channel runs straight (3722:0002) */
        if (hd != heading) wheel = SDL_clamp((heading - hd) * 2, -0x18, 0x18);
        heading = hd;
    }
}

/* 1001:cf9a: the opponent's sideways step towards `delta`, looking six segments ahead for what's
 * in the way: rocks and gantry posts beside the road, things lying on it, you, and bridges. */
static int opp_dodge(int x, int delta, int s0)
{
    int found = 0, lo = 0, hi = 0, mid = 0, v;
    for (int k = 6; k >= 0; k--) {
        int sg = s0 + k;
        if (F_(sg) & 0x80) {                                           /* a bridge: line up for it */
            int d = (s0 - pos) * 8 + opp_sub + sub;
            if (SDL_abs(d) < 0x40 && opp.speed > 0x32 && me.speed < opp.speed) opp_set_speed(SDL_max(me.speed, 0x32));
            return (((lane_r4 + lane_l4) >> 1) - x) / 2;
        }
        if ((F_(sg) & 0x1004) && opp.speed >= 0x33) goto object;
        if (L_(sg) & 0x1010) {
            mid = 0x5dc;
            if (found && hi + 0xe1 > 0x79d) {
                if (lo - 0xe1 < 0x79e) goto d009;
                goto d19c;
            }
        d05f:
            lo = mid;
        d0b6:
            mid += 0x1c2;
            goto d199;
        }
        if (R_(sg) & 0x1010) {
            mid = 0xb22;
            if (!found || lo > 0xb22) goto d05f;
            if (hi > 0xa40) goto d19c;
            goto d0b6;
        }
        if (k + s0 - pos == 1) {                                       /* right behind you */
            if (k != 0 || !found) {
                if (k > 1 || SDL_abs(opp_x - cam_x) > 0x117) {
                    int a = cam_x - 0x136, b = cam_x + 0x15e, nhi = b, nlo = a;
                    mid = b;
                    if (found && (lo <= a || b <= hi)) {
                        nhi = hi;
                        nlo = lo;
                        if (lo - 0xe1 < b && b < hi) nlo = a;
                    }
                    lo = nlo;
                    hi = nhi;
                    if (a <= hi || hi + 0xe1 <= a) goto d19c;
                    goto d199;
                }
                if (found && opp.speed > 0x28) opp_set_speed(SDL_max(opp.speed - 0x1e, 10));
                delta = 0;
                found = 0;
            }
            break;
        }
        continue;
    object:
        mid = g_road.object[seg_ok(sg)] * 0x70 + 0x5dc;               /* 1dcb:1470 */
        v = mid;
        if (found) {
            if (lo <= mid || lo - mid > 0xe0) {
                v = lo;
                if (mid <= hi || mid - hi + 0x70 > 0xe0) goto d19c;
                goto d02e;
            }
        d009:
            lo = mid;
            goto d19c;
        }
    d02e:
        lo = v;
        mid += 0x70;
    d199:
        hi = mid;
    d19c:
        found = 1;
    }
    if (found) {
        delta = 0;
        if (lo <= x + 0xaa && x - 0x82 <= hi) {
            int left = (x + 0xaa - lo) + 100;
            delta = 100 - ((x - 0x82) - hi);
            if (lane_l4 <= lo - 300) {
                if (hi + 300 <= lane_r4) {
                    if (delta < left) { if (x + delta < lane_r4) goto done; }
                    else if (x - left <= lane_l4) goto done;
                }
                delta = -left;
            }
        }
    }
done:
    return delta < 0 ? -((-delta) >> 3) : delta >> 3;
}

/* 1001:d4cc: the opponent moves on, and picks a line: the middle of the road far off, weaving
 * from side to side when close, alongside you to pass or block. */
static void opp_drive(void)
{
    int step = (opp_pos - pos == -1 && SDL_abs(opp_x - cam_x) < 0xb4) ? 0 : frame_distance(opp.speed);
    opp_sub += step;
    if (opp_sub > 0x3f && step > 0) { opp_pos += opp_sub >> 6; opp_sub &= 0x3f; }
    int aq = aqueduct(), bridge = !aq && ((F_(opp_pos + 4) & 0x80) || (F_(opp_pos) & 0x80));
    int curve = aq ? 0 : HD_(pos + 4) - HD_(pos + 5), gap = opp_pos - pos, mid = (lane_r4 + lane_l4) >> 1;
    if (gap > 0xc) {
        opp_aim = mid;
        if (curve) opp_flip = curve < 0;
        opp_wander = 1;
    } else if (gap < 7) {
        if (gap > 2) {
            if (curve) {
                if (curve >= 0) { opp_aim = mid + 300; opp_flip = 0; } else { opp_aim = mid - 200; opp_flip = 1; }
            } else if (bridge) opp_aim = mid;
            else if (opp_wander) {
                opp_aim = opp_flip ? mid + 300 : mid - 200;
                opp_wander = 0;
                opp_flip = !opp_flip;
            }
        } else if (gap < -3) {
            if (gap < -5) { opp_aim = mid + 200; opp_wander = 1; }
            else {
                opp_close = 1;
                if (!curve) opp_aim = bridge ? mid + 300 : (opp_flip ? mid - 200 : mid + 300) + 200;
                else if (curve >= 0) { opp_aim = mid + 400; opp_flip = 0; }
                else { opp_aim = mid - 100; opp_flip = 1; }
            }
        } else if (frame_distance(me.speed) < step) {                  /* side by side, he's quicker */
            if (opp_kind > 8) {
                if (SDL_abs(opp_x - cam_x) < 0xb4 || bridge) {
                    opp_pos = pos - 2;
                    opp_set_speed(SDL_max(me.speed, 0x32));
                }
                if (cam_x - lane_l < -(cam_x - lane_r)) { opp_flip = 0; opp_aim = cam_x + 0x212; }
                else { opp_flip = 1; opp_aim = cam_x - 0x14a; }
            } else if (opp_kind == 0 || opp_kind == 2) opp_aim = cam_x <= opp_x ? cam_x + 0x212 : cam_x - 0x14a;
        } else if (opp_kind == 0) opp_aim = cam_x - 0x14a;
    } else {
        opp_close = 1;
        if (curve) {
            if (curve >= 0) { opp_aim = mid + 300; opp_flip = 0; } else { opp_aim = mid - 200; opp_flip = 1; }
        } else if (bridge) opp_aim = mid;
        else opp_aim = opp_flip ? mid - 200 : mid + 300;
    }
    opp_x += aq ? aq_steer(opp_x, opp_aim - opp_x, opp_pos, cam_x, pos) : opp_dodge(opp_x, opp_aim - opp_x, opp_pos);
    cam_prev = cam_x;
}

/* 1001:d2a8: a car's sideways step in the Aqueduct towards x + delta, steering round the blocks
 * and pillars in the next 8 segments by where the other car is (behind: 1, beside: 2 / 4, ahead: 8),
 * the closer they are the sharper. */
static int aq_steer(int x, int delta, int seg, int other_x, int other_seg)
{
    int target = delta + x, fl = g_aq.s_left + 0x5c8, fr = g_aq.s_right + 0xcf8, line[5];
    if (autodrive != 1) {
        int mid = (fl + fr) / 2;
        line[0] = (fl - 0x438) / 3 + 0x438;
        line[1] = (fr - fl) / 4 + fl;
        line[2] = mid - 0x1e;
        line[3] = (mid + fr) / 2;
        line[4] = (0xe88 + fr) / 2;
    } else {
        static const int fixed[5] = { 0x514, 0x6a4, 0x884, 0xa3c, 0xbcc };
        memcpy(line, fixed, sizeof line);
    }
    int c = (fl + fr) >> 1, other = other_x - c, me_ = x - c, gap = seg - other_seg;
    int mode = gap >= 3 ? 1 : gap < 0 ? 8 : x < other_x ? 2 : 4, k, pick = -1;
    for (k = 0; k < 8; k++) {
        unsigned f = AQ_(seg + k);
        if (!(f & 0x90)) continue;
        if (mode == 2) pick = 0;
        else if (mode == 4) pick = 4;
        else if (mode == 1 && (f & 0x80) && !(f & 0x10)) pick = 2;
        else pick = me_ < 0 ? 0 : 4;
        break;
    }
    if (pick < 0) {
        if (mode == 1) pick = other < 0 ? 1 : 3;
        else if (mode == 8) pick = other < 0 ? 3 : 1;
    }
    if (pick >= 0) target = line[pick];
    if (k > 0) k--;
    if (k > 0) k--;
    return (target - x) * 6 / (k + 1) / 6;
}

/* 0dcb:af98: which picture of the opponent shows, from how far ahead (1/64 segment) and which side:
 * 0 side view through the side window, 1 big rear view ahead to one side, 2 medium rear view,
 * 3..8 the medium view smaller and smaller, 9.. behind you (the mirror, not drawn yet). The picture
 * changes one step per frame. Also notes when you are about to run into his back (9a48). */
static int opponent_kind(int ahead, int side)
{
    if (ahead >= 0x3c0) return -1;
    if (ahead >= 0x80) { int k = ((ahead + 0x40) >> 7) + 2; return k > 8 ? 8 : k; }
    if (ahead < -0x38) return ((0x80 - ahead) >> 7) + 8;
    if (ahead < 0x41) {
        if (SDL_abs(side) < 0xb4) return opp_kind < 9 ? 2 : 9;
        if (ahead < 0) return 9;
        return side < 0 ? 0 : 2;
    }
    return side < -0xb4 ? 1 : 3;
}

static void opp_view(void)
{
    int ahead = (opp_pos - pos) * 64 - sub + opp_sub, side = opp_x - cam_prev;
    int k = opponent_kind(ahead, side), skip = 0;
    if (ahead < 0 || ahead > 0x40 || opp_kind_prev > 8 || opp_kind > 8) rear_hit = 0;
    else rear_hit = SDL_abs(side) < 0xb4 && frame_distance(me.speed) > frame_distance(opp.speed);
    if (result == RES_HIT_OPP || rear_hit) k = 2;
    else if (mulholland() && SDL_abs(ahead - opp_ahead) <= 0x27 && opp_kind == 9 && k >= 9) {
        k = 9;
        ahead = opp_ahead;
    } else if (k != opp_kind) {
        int pk = opp_kind;
        if (k < 0) { if (pk == 8) { opp_hidden = 1; return; } k = pk + 1; }
        else if (k > 0xc) { if (pk == 0xc) { opp_hidden = 1; return; } k = pk + 1; }
        else if (k == opp_kind_prev) { }
        else if ((k < 9 && pk > 9) || (k > 8 && pk > 0 && pk < 9 && side < 0)) k = pk - 1;
        else if (k > 8 && pk > 0 && pk < 9 && side >= 0) { if (rear_hit) k = 2; else skip = 1; }
        else if (pk == 0) { if (k > 8) { k = 9; skip = 1; } else k = 1; }
        else if (pk == 9) { if (k < 9) { k = side < 0 ? 0 : 2; skip = 1; } else k = 10; }
        else if (pk == 1) k = k < 1 ? 0 : 3;
        else if (pk == 3 && k == 1) { }
        else k = k <= pk ? pk - 1 : pk + 1;
    }
    alongside = k == 0 || (k == 2 && ahead < 0x51);
    rear_hit = rear_hit && k == 2;
    opp_kind_prev = opp_kind;
    opp_kind = k;
    opp_ahead = ahead;
    opp_hidden = skip && !rear_hit && result != RES_HIT_OPP;
}

/* The tail of 1001:d908: running into his back, and bumping side by side - steer into him to
 * knock him back, or a rough rival may shove you aside. */
static void contact(void)
{
    if (result || !alongside || pos >= finish_seg) return;
    if (rear_hit && police < 2) { result = RES_HIT_OPP; return; }  /* the police don't mind */
    if (opp_kind == 0 && frame_distance(me.speed) > 1) {
        side_frames++;
        if (steer < 0) {
            if (HD_(pos) - HD_(pos + 1) < 0 && (lane_r4 + lane_l4) >> 1 < cam_x) return;
            sfx_bump();
            if (bumped) { bumped = 0; return; }
            opp_pos--;
            opp_set_speed(SDL_max(opp.speed - 10, 10));
            bumped = 1;
            return;
        }
        if (!opp_close || side_frames < 4 || police >= 2) return;
        int s1 = (int8_t)G->player.rivals[rival * 18 + 1], n = ((-s1 - 0xf) >> 2) + 2;
        if (n > 0 && (int)SDL_rand((Sint32)n) == 0) {                  /* he shoves you */
            sound_sfx(SFX_BUMP);
            cam_x += 300;
            if (me.speed > 0x14) shoved = 2;
            opp_close = 0;
            heading += SDL_rand(2) == 0 ? -0xf : 0xf;
            sfx_bump();
        }
    } else if (opp_kind == 0) return;
    side_frames = 0;
}

static void aq_render(void);

static RoadView view;
static void aq_roll_step(void);

/* Sound slot 11 (2c57:0fcc): the siren, voice 2 "police" held at note 0x50. */
static void siren(int on)
{
    if (on) {
        sound_voice_timbre(2, "police", 0x7f);
        sound_pitch_range(5);
        sound_voice_bend(2, 0x2000);
        sound_voice_note_off(2);
        sound_voice_note_on(2, 0x50);
    } else {
        sound_voice_note_off(2);
        sound_voice_bend(2, 0x2000);
    }
}

/* 1001:d85c: the police take the opponent's place: The King's car figures (c5d8 / c6f6 of rival
 * 0x15) at the opponent's speed, rival 10's record (DS:ac32, its skill byte), the police pictures
 * (DS:9a94, set 5 of DS:aa9e but the opponent's own side view) and paint colour 5 (8d0a). */
static void police_switch(void)
{
    static Car stock;
    int speed = opp.speed;
    car_make_stock(&stock, rival_model(&G->player, KING), 0, 0);
    engine_setup(&opp, &stock);
    skill = (int8_t)G->player.rivals[10 * 18];
    opp_limit = opp.automatic ? 0x14b4 : (skill - 10) * 200 + opp.shift_rpm;
    opp_set_speed(speed);
}

static void race_frame(void)
{
    /* 1dcb:1774: the heading follows the steering, and the car slides across the road by how far
     * the heading is off the road's - quicker the faster you go and the more the tires slip. */
    if (steer && me.speed && SDL_abs(heading - HD_(pos) - steer) < 200) heading -= (steer >> 1) + 1;
    opp_view();                                                        /* drawn by 11fc here */
    if (aqueduct()) {                                                  /* its view moves the car */
        aq_render();
        cam_x += g_aq.slide;
        me.speed -= g_aq.slow;
    }
    int dist = frame_distance(me.speed);
    if (dist > 0) starter_slide += 4;
    if (autodrive != 1) {
        if (!tire_flat) {
            if (me.speed) {
                cam_x -= ((SDL_abs(dist) >> 3) + grip) * (heading - HD_(pos)) >> 2;
                if (mulholland()) cam_x = SDL_clamp(cam_x, lane_l - 0x96, lane_r + 0x96);
            }
        } else cam_x += 0x19;                                          /* a flat pulls to the right */
    }
    /* 1001:d908 */
    if (blowout_at && blowout_at < pos) tire_flat = 1;
    opp_engine();
    if (police == 1 && pos >= police_seg) {                            /* d908: the police wait here */
        if (pos < opp_pos + 7) police = 0;                             /* not far enough ahead: nothing */
        else { police = 2; police_switch(); }
    } else if (police == 2 && pos <= opp_pos + 0xf) {
        police = 3;
        siren(1);
    } else if (police == 3 && pos < opp_pos - 2 && !result) {
        result = RES_CAUGHT;                                           /* pulled over */
        return;
    }
    if (aqueduct()) {                                                  /* 1001:cee0 */
        unsigned f = AQ_(pos + 2);
        if (!aq_skip) aq_skip = 1;
        else if (sub + dist > 0x38 && (f & 0x90)) {                    /* don't jump past a block or a pillar */
            RoadView v = { pos, 0x38, cam_x, CAM_H, heading, race, opp_pos, 0, NULL, me.speed, autodrive == 1, NULL, 0, 0 };
            if (road_aq_ahead(&v, f)) { sub = 0x38; dist = 0; aq_skip = 0; }
        }
    }
    sub += dist;
    if (sub > 0x3f) { pos += sub >> 6; sub &= 0x3f; new_segment(); }
    opp_drive();
    /* d908: while the starter is still there, keep to your side of the middle (DS:a7ac, b7b2). */
    if (!result && starter_there() && cam_x < (((aqueduct() ? 0x5c8 : 0x5dc) + (aqueduct() ? 0xcf8 : 0xce4)) >> 1) + 0xa0)
        result = RES_CENTRE;
    if (pos >= finish_seg || opp_pos >= finish_seg) finish_line();
    if (finished && me.speed < 1 && !result) result = you_first ? RES_WON : RES_LOST;
    contact();
}

/* ------------------------------------------------------------------ sound (2c57:0b2c / 0ba6) */

static void engine_sound(int on)
{
    if (on) {
        sound_voice_timbre(3, "motor", 0x40);
        sound_pitch_range(6);
        sound_voice_bend(3, 0);
        sound_voice_note_off(3);
        sound_voice_note_on(3, 0x18);
    } else {
        sound_voice_volume(3, 0);
        sound_voice_note_off(3);
    }
}

static void engine_sound_restart(void) { if (state != R_DONE) engine_sound(1); }

static void engine_sound_update(void)
{
    int bend = me.rpm == REDLINE ? 0x3fff : me.rpm * 3 - 0x1194;
    if (bend < 0) bend = 0;
    if (bend > 0x3fff) bend = 0x3fff;
    int vol = you->muffler.part < 0 ? 0x7f : (SDL_abs(me.rpm - 0x5dc) >> 7) + 0x50;
    sound_voice_bend(3, bend);
    sound_voice_volume(3, vol > 0x7f ? 0x7f : vol);
}

/* ------------------------------------------------------------------ result (1001:b584) */

static void result_screen(void);

static void lose_car(void)
{
    Player *p = &G->player;
    int i = p->current;
    if (i < 0) return;
    memmove(&p->cars[i], &p->cars[i + 1], sizeof p->cars[0] * (size_t)(p->car_count - i - 1));
    p->car_count--;
    p->current = -1;
}

static void win_pink_slip(void)
{
    Player *p = &G->player;
    int model = rival_model(p, rival);
    Car *c = player_add_car(p, model, g_models[model].price, 0);
    if (!c) return;
    c->tires.wear = (int16_t)SDL_rand(500);
    int mods = rival_get(p, rival, 0xd) & 0xff;
    if (mods & 4) c->body |= BODY_ROOF_CHOPPED;
    if (mods & 2) c->body |= BODY_REAR_STRIPPED;
    if (mods & 1) c->body |= BODY_FRONT_STRIPPED;
    c->colour = (uint8_t)rival_get(p, rival, 0xa);
    if (rival != KING) rival_set(p, rival, 0xb, -1);                    /* he's out of the game */
}

/* 1001:ac68: blown tires come off; the spares are searched for a set, else new ones cost $50. */
static void new_tires(void)
{
    Player *p = &G->player;
    if (!you) return;
    you->price = (int16_t)(you->price - you->tires.value);
    you->tires.part = -1;
    for (int k = 0; k < p->part_count; k++)
        if (p->parts[k].part >= 0 && g_parts[p->parts[k].part].type == PT_TIRES) {
            you->tires = p->parts[k];
            player_remove_spare(p, k);
            return;
        }
    p->money -= 0x32;
    if (p->money < 0) p->money = 0;
    if (player_add_part(p, (int16_t)exe_ds_u16(0x46f6), 0x32) >= 0 && p->part_count > 0) {
        you->tires = p->parts[0];
        player_remove_spare(p, 0);
    }
}

static Dialog repair;                    /* dialogs 24/25 after a crash */
static int repair_open, repair_cost, go_home;

/* The crash part of 1001:b584: what's left of the car depends on how fast you were going. */
static void crash_settle(void)
{
    Player *p = &G->player;
    int tires = blowout_at && tire_flat;
    if (stake == 5) {                                                  /* he gets the car anyway */
        game_message(G, tires ? 0x5c : 0x5b);
        lose_car();
        go_home = 1;
        return;
    }
    if (crash_speed >= 0x5b || fell_steps) {                           /* totaled: scrap value */
        long body = you->price;
        PartInst *parts[10] = { &you->engine, &you->trans, &you->intake, &you->carb[0], &you->carb[1], &you->carb[2],
                                &you->tires, &you->exhaust, &you->muffler, &you->diff };
        for (int k = 0; k < 6; k++) if (parts[k]->part >= 0) body -= parts[k]->value;     /* 1001:4716 */
        if (body < 0) body = 0;
        long scrap = body / 10 + 0x13;
        p->money += scrap;
        lose_car();
        game_message_amount(G, 0x5d, scrap, 0);
        go_home = 1;
        return;
    }
    if (crash_speed < 0x1f) {
        if (tires) { game_message(G, 0x5f); new_tires(); go_home = 1; }
        else game_message(G, 0x5e);
        return;
    }
    int big = crash_speed >= 0x3d;
    repair_cost = (int)SDL_rand(big ? 0x28 : 0x14) + 100;
    dialog_load(&repair, big ? 24 : 25);
    char text[80];
    SDL_strlcpy(text, exe_ds_str(big ? 0x227c : 0x22b4), sizeof text);
    char *d = SDL_strchr(text, '$');
    if (d) { char num[8]; SDL_snprintf(num, sizeof num, "%d", repair_cost); for (int k = 0; num[k] && d[1 + k]; k++) d[1 + k] = num[k]; }
    dialog_set_text(&repair, big ? 4 : 3, text);
    dialog_set_text(&repair, big ? 5 : 4, tires ? exe_ds_str(0x5210) : "");
    if (p->money < repair_cost) repair.w[1].flags &= ~WF_ENABLED;     /* can't pay: no "Fix it" */
    repair_open = 1;
}

static void settle(void)
{
    Player *p = &G->player;
    int burn = mulholland() ? 0xf : aqueduct() ? 10 : 5;               /* b584: DS:add6 1 / 0x10 / 8 */
    if (you->fuel > burn) you->fuel -= burn; else you->fuel = 0;
    if (fined) {
        int extra = 0;
        if (blowout_at && tire_flat) {                                 /* ac68: new tires first */
            long before = p->money;
            new_tires();
            if (before < 0x32 && !p->money) extra = (int)(0x32 - before);
        }
        fine = extra + (me.speed < 0x33 ? 0x14 : me.speed < 0x51 ? 0x32 : 0x4b);
        ticket_open = 1;
        ticket_paid = 0;
        ticket_start = SDL_GetTicks();
        goto standings;                                                /* counted as lost */
    }
    if (result == RES_CRASH || result == RES_HIT_OPP || (result & RACE_TIRE)) {
        p->money -= bet;
        crash_settle();
    } else if (result == RES_WON) {
        p->money += bet;
        if (stake == 5) win_pink_slip();
        game_message(G, rival == KING ? (aqueduct() ? 0x50 : 0x4f) : (int16_t)exe_ds_u16((uint16_t)(0x512a + 2 * stake)));
    } else if (result == RES_LOST) {
        p->money -= bet;
        if (stake == 5) lose_car();
        game_message(G, rival == KING ? (race == RACE_DRAG ? 0x55 : 0x56) : stake == 5 ? 0x53 : 0x54);
    } else if (result == RES_CENTRE) {
        game_message(G, 0x78);                                         /* b584 returns before the bets */
    } else if (result == RES_JUMPED) {
        game_message(G, 0x51);                                         /* "Ya jumped the light, speedy!" */
    } else if (result == RES_ENGINE || result == RES_TRANS) {
        p->money -= bet;
        if (result == RES_ENGINE) {                                    /* 1001:b46e */
            PartInst *gone[5] = { &you->engine, &you->intake, &you->carb[0], &you->carb[1], &you->carb[2] };
            for (int k = 0; k < 5; k++) { you->price = (int16_t)(you->price - gone[k]->value); gone[k]->part = -1; }
            for (int k = 2; k < 8; k++) you->tune[k] = 0;
            you->wires = 0;
        } else {                                                       /* 1001:b4da */
            you->price = (int16_t)(you->price - you->trans.value);
            you->trans.part = -1;
            you->tune[0] = you->tune[1] = 0;
        }
        game_message(G, result == RES_ENGINE ? (stake == 5 ? 0x5a : 0x58) : (stake == 5 ? 0x59 : 0x57));
        if (stake == 5) lose_car();
    }
standings:
    /* Standings (tail of 1001:a626). */
    int won = result == RES_WON;
    if (result != RES_JUMPED) player_set_counter(p, 0, player_counter(p, 0) + 1);
    int kind = mulholland() ? 2 : aqueduct() ? 3 : 1;                  /* DS:479e / 47a0 / 47a2 */
    if (won) player_set_counter(p, kind, player_counter(p, kind) + 1);
    int sr = police >= 2 ? 10 : rival;                                 /* DS:a80a: the police use rival 10's record */
    int rep = player_rep(p, 0), rrep = rival_get(p, sr, 0xf);
    if (won) { if (rep < 8) rep++; if (rrep > 1) rrep--; }
    else { if (rep > 1) rep--; if (rrep < 8) rrep++; }
    p->header_raw[0x28] = (uint8_t)rep;
    rival_set(p, sr, 0xf, rrep);
    rival_set(p, sr, 0xc, rival_get(p, sr, 0xc) + 1);
    int rep2 = player_rep(p, 1), rrep2 = rival_get(p, sr, 0x10);
    if (you_first) { if (rep2 < 8) rep2++; if (rrep2 > 1) rrep2--; }
    else { if (rep2 > 1) rep2--; if (rrep2 < 8) rrep2++; }
    p->header_raw[0x29] = (uint8_t)rep2;
    rival_set(p, sr, 0x10, rrep2);
    if (rival == KING) {                                               /* 1001:a626 after b584 */
        if (!won) {
            player_set_counter(p, 5, 0);                               /* back to the start (DS:47a6, ad03) */
            player_set_counter(p, 2, player_counter(p, 2) - 3);
            player_set_counter(p, 3, player_counter(p, 3) - 2);
        } else if (race == RACE_DRAG) player_set_counter(p, 5, 1);
        else if (mulholland()) player_set_counter(p, 5, 2);
        else {
            player_set_counter(p, 5, 3);                               /* 1001:a51c, DS:1bc6 |= 2: not rebuilt yet */
            SDL_Log("race: The King is beaten (1001:a51c not rebuilt yet)");
        }
    }
}

/* ------------------------------------------------------------------ scene */

void race_start(Game *g, int r, int race_type, int stake_, int amount)
{
    race = race_type == RACE_MULHOLLAND || race_type == RACE_AQUEDUCT ? race_type : RACE_DRAG;
    G = g;
    rival = r;
    stake = stake_;
    bet = amount;
    game_set_scene(g, &scene_race);
}

static void enter(Game *g)
{
    G = g;
    you = player_car(&g->player);
    engine_setup(&me, you);
    road_build(&g->player);
    static Car stock;
    car_make_stock(&stock, rival_model(&g->player, rival), 0, 0);
    engine_setup(&opp, &stock);
    skill = (int8_t)g->player.rivals[rival * 18];
    opp_limit = opp.automatic ? 0x14b4 : (skill - 10) * 200 + opp.shift_rpm;
    pick_band(&opp, 1);
    start_seg = mulholland() ? 0x172 : aqueduct() ? 8 : 200;           /* 1001:bc88 */
    finish_seg = mulholland() ? 0x3c0 : aqueduct() ? 0x156 : 0x17c;
    road_aq_reset();                                                   /* 1dcb:8840 / 1001:8d9e */
    aq_side = aq_roll = aq_skip = aq_have = 0;
    starter_seg = start_seg + 1;
    pos = opp_pos = start_seg;
    sub = opp_sub = 0;
    cam_x = cam_prev = CAM_X;                                          /* 1dcb:0736 */
    opp_x = OPP_X;
    heading = steer = wheel = steer_in = 0;
    fall = tire_flat = fell_steps = 0;
    opp_flip = 1;                                                      /* DS:8012 starts at 1 */
    opp_close = side_frames = bumped = in_bend = shoved = 0;
    opp_wander = SDL_rand(2) == 0;                                     /* DS:b19c (1dcb:0736) */
    opp_aim = OPP_X;
    opp_kind = opp_kind_prev = 2;
    opp_ahead = opp_hidden = rear_hit = alongside = 0;
    repair_open = go_home = 0;
    /* 1001:be82: on Mulholland, unless it's The King, the police may be waiting (1 in 4). */
    police = fined = ticket_open = ticket_paid = 0;
    if (mulholland() && rival != KING && ((int)SDL_rand(4) == 0 || force_police)) {
        police = 1;
        police_seg = (int)SDL_rand(0xf0) + 0x1e0;
        if (force_police) SDL_Log("race: police waiting at segment %d", police_seg);
    }
    force_police = 0;
    g_road_hits.object = g_road_hits.bump = 0;
    new_segment();
    if (you->tires.part >= 0)                                          /* DS:521e: how much they slip */
        grip = ds16((uint16_t)(0x521e + 2 * (g_parts[you->tires.part].grade * 5 + you->tires.wear / 2000)));
    else grip = 14;
    steps = opp_steps_done = 0;
    speed_sum = 0;
    finished = you_first = result = 0;
    limiter_count = 0;
    gas = brake = shift_pressed = shift_latch = 0;
    frame_no = 0;
    frame_acc = engine_acc = 0;
    /* 1001:be82: tire wear for the run, and worn tires may give out (bc88). */
    if (you->tires.part >= 0) {
        int far = (race & 7) == 0;                                     /* Mulholland and the Aqueduct */
        int base = g_parts[you->tires.part].grade == 0 ? (far ? 700 : 300) : (far ? 400 : 200);
        int w = you->tires.wear + (int)SDL_rand(300) + base;
        you->tires.wear = (int16_t)(w > 10000 ? 10000 : w);
    }
    blowout_at = 0;
    if (you->tires.wear >= 8000 && (int)SDL_rand(2000) + you->tires.wear >= 0x251d) {
        int len = finish_seg - start_seg;
        blowout_at = (int)SDL_rand(len >> 2) + (len >> 1) + start_seg;
    }
    state = R_STAGE;                                                    /* 8d9e waits 0xf + 0x19 ticks first */
    phase_start = last_frame = SDL_GetTicks();
    wait_ticks = 0xf + 0x19;
    ready_text[0] = 0;
    starter_slide = flag_dropped = 0;
    game_message_clear(g);
    game_set_palette(g, PAL_INCAR);
    if (aqueduct()) aq_render();
    sound_music_duck(opt_tune_music_duck);                              /* rebuild: hear the engine */
    engine_sound(1);
}

static void leave_race(void)
{
    engine_sound(0);
    sound_music_duck(0);
    G->player.time_actions += mulholland() ? 0x777 : aqueduct() ? 0x555 : 0x444;   /* 1001:9e3c */
    /* b584's answer: a pink-slip race or a broken car means home (towed, or on foot); otherwise
     * it's back to Burger's. */
    int home = go_home || !player_car(&G->player) || stake == 5 || result == RES_ENGINE || result == RES_TRANS ||
               !car_runnable(player_car(&G->player), 1);
    game_set_scene(G, home ? &scene_garage : &scene_street);
}

static void repair_choice(int id)
{
    Player *p = &G->player;
    if (id == 1 && p->money >= repair_cost) {                          /* Fix it */
        p->money -= repair_cost;
        if (blowout_at && tire_flat) { new_tires(); go_home = 1; }
    } else if (id == 2) {                                              /* Junk it: 40% of what you paid */
        p->money += (long)you->price * 4 / 10;
        lose_car();
        go_home = 1;
    } else return;
    repair_open = 0;
    leave_race();
}

int race_shows_cursor(void) { return repair_open; }

static void mouse_move(Game *g, int x, int y) { (void)g; if (repair_open) dialog_mouse_move(&repair, x, y); }

static void key(Game *g, SDL_Scancode sc, SDL_Keycode k)
{
    (void)g;
    if (state == R_CRASH) return;
    if (repair_open) { repair_choice(dialog_key(&repair, sc)); return; }
    if (ticket_open && !ticket_paid) return;
    if (state == R_DONE) { leave_race(); return; }
    if (k == SDLK_SPACE || k == SDLK_RETURN || k == SDLK_LSHIFT || k == SDLK_RSHIFT) shift_pressed = 1;
}

static void click(Game *g, int x, int y)
{
    (void)g;
    if (state == R_CRASH) return;
    if (repair_open) { repair_choice(dialog_click(&repair, x, y)); return; }
    if (ticket_open && !ticket_paid) return;
    if (state == R_DONE) { leave_race(); return; }
    shift_pressed = 1;                                                 /* left button: the gear stick */
}

static void read_controls(void)
{
#define ks(sc) game_key_held(SDL_SCANCODE_##sc)
    SDL_MouseButtonFlags mb = SDL_GetMouseState(NULL, NULL);
    gas = ks(UP) || ks(W) || (mb & SDL_BUTTON_RMASK);
    brake = ks(DOWN) || ks(S);
    /* Steering (0001:2960): left/right keys set the step's input, the mouse adds to it. */
    float mdx = 0;
    SDL_GetRelativeMouseState(&mdx, NULL);
    if (ks(LEFT) || ks(A)) steer_in = -1;
    else if (ks(RIGHT) || ks(D)) steer_in = 1;
    if (mdx < -0.5f) steer_in--;
    else if (mdx > 0.5f) steer_in++;
#undef ks
    if (autodrive) {
        gas = state >= R_GO && !(me.gear == me.gears && me.rpm >= REDLINE - 300);   /* off the limiter */
        brake = 0;
        if (state >= R_GO && (me.gear == 0 || (me.rpm > 5000 && me.gear < me.gears))) shift_pressed = 1;
    }
}

static void frame(void)
{
    frame_no++;
    if (SDL_getenv("SR2_DEBUG") && frame_no % 18 == 0)
        SDL_Log("race f%d st%d you pos %d spd %d rpm %d g%d band %d acc %d x %d hd %d/%d st %d | opp pos %d spd %d rpm %d g%d lim %d x %d k %d res %x",
                frame_no, state, pos, me.speed, me.rpm, me.gear, me.band, me.accel, cam_x, heading, HD_(pos), steer, opp_pos, opp.speed, opp.rpm, opp.gear, opp_limit, opp_x, opp_kind, result);
    read_controls();
    if (state == R_CRASH) {
        if (aq_side && aq_roll < 9) { aq_roll_step(); return; }        /* rolling over the wall first */
        if (fell_steps && fell_steps < 9) {                            /* dropping off the cliff */
            cam_x += fall * 100;
            fell_steps++;
        }
        if (++crash_ticks >= 13 + (crash_speed > 0x5a ? 6 : 0)) { state = R_DONE; settle(); result_screen(); }   /* 100 ticks */
        return;
    }
    switch (state) {
    case R_STAGE:
        if (me.speed > 0) { result = RES_JUMPED; break; }
        if (game_ticks_since(phase_start) >= wait_ticks) {             /* 8ef4 state 0 */
            state = R_READY;
            phase_start = SDL_GetTicks();
            wait_ticks = 0x6e + (int)SDL_rand(0x67);
            SDL_strlcpy(ready_text, exe_ds_str(0x4ff4), sizeof ready_text);    /* "Get ready !" */
        }
        break;
    case R_READY:
        if (me.speed > 0) { result = RES_JUMPED; break; }              /* rolled before the flag */
        if (game_ticks_since(phase_start) >= wait_ticks) {
            state = R_GO;
            flag_dropped = 1;
            SDL_strlcpy(ready_text, exe_ds_str(0x5000), sizeof ready_text);    /* "Go !" */
        }
        break;
    case R_GO:
    case R_RACING:
        race_frame();
        if (state == R_GO && me.speed > 0) { state = R_RACING; ready_text[0] = 0; }
        break;
    default:
        break;
    }
    if (starter_there()) starter_seg = pos < opp_pos ? pos + 1 : opp_pos;  /* a746 */
    if (aqueduct() && (state == R_STAGE || state == R_READY)) aq_render();   /* 8ef4 draws 11fc too */
    if (result && state < R_CRASH && SDL_getenv("SR2_DEBUG"))
        SDL_Log("race over: result %x at pos %d x %d speed %d (opp pos %d x %d kind %d)", result, pos, cam_x, me.speed, opp_pos, opp_x, opp_kind);
    if (fall && !fell_steps && result == RES_CRASH) fell_steps = 1;   /* 1dcb:7da2: over the edge */
    if (result && state < R_CRASH) {
        if (police >= 2) {                                             /* d908 / b584: the ticket */
            if (result != RES_WON) fined = 1;
            siren(0);
            if (result == RES_CAUGHT) result = 0;                      /* no other result to settle */
        }
        engine_sound(0);
        if (tire_flat && result != RES_WON) result |= RACE_TIRE;
        if (result == RES_CRASH || result == RES_HIT_OPP || (result & RACE_TIRE)) {
            state = R_CRASH;                                           /* 1001:af0a */
            crash_speed = me.speed;
            crash_ticks = 0;
            sound_sfx(SFX_CRASH);
            return;
        }
        state = R_DONE;
        settle();
        result_screen();
    }
}

/* b584's police part: the ticket (1001:adc0), "You have been fined $N for speeding" (DS:51e7) for
 * 0xfa ticks, then it's paid; without the money you have, it's message 0x52, all your money, and
 * a spell in jail (1001:a9fc). Bets are not settled. */
static void ticket_step(void)
{
    if (!ticket_open || ticket_paid || game_ticks_since(ticket_start) < 0xfa) return;
    Player *p = &G->player;
    ticket_paid = 1;
    if (p->money >= fine) p->money -= fine;
    else {
        game_message(G, 0x52);
        p->money = 0;
        go_home = 1;                                                   /* a9fc: jail, not rebuilt yet */
        SDL_Log("race: can't pay the fine - jail (1001:a9fc) not rebuilt yet");
    }
}

/* ------------------------------------------------------------------ drawing */

static char result_line1[64], result_line2[64];

static void result_screen(void)        /* 1001:b256: dialog 23 */
{
    result_line1[0] = result_line2[0] = 0;
    if (result != RES_WON && result != RES_LOST) return;
    if (result == RES_WON) {
        SDL_snprintf(result_line1, sizeof result_line1, "Your winning time %d.%d sec., avg. speed %2d mph",
                     time_tenths / 10, time_tenths % 10, avg_speed);
        SDL_strlcpy(result_line2, exe_ds_str(0x517d), sizeof result_line2);
    } else {
        SDL_snprintf(result_line1, sizeof result_line1, "In %d.%d sec you covered only %2d%%",
                     time_tenths / 10, time_tenths % 10, covered);
        SDL_snprintf(result_line2, sizeof result_line2, "of the distance. Your avg. speed %2d mph.", avg_speed);
    }
}

/* The windshield view is road.c's port of the original renderer; the cars and the starter are
 * drawn here. You drive at 2560 (DS:a564) with your eyes at 1900 (a566), the opponent at 2200
 * (a9cc). The view is clipped to rows 18..101. */

static int road_seg(int s) { return SDL_clamp(s, 0, ROAD_SEGS - 1); }

/* Copy part of a picture (colour 0 transparent for sprites), clipped to the view. */
static void blit_clip(const Image *im, int sx, int sy, int w, int h, int dx, int dy)
{
    if (!im) return;
    Video *v = &G->video;
    for (int y = 0; y < h; y++) {
        int ty = dy + y, iy = sy + y;
        if (ty < CLIP_TOP || ty > CLIP_LAST || iy < 0 || iy >= im->h) continue;
        for (int x = 0; x < w; x++) {
            int tx = dx + x, ix = sx + x;
            if (tx < 0 || tx >= SCREEN_W || ix < 0 || ix >= im->w) continue;
            uint8_t c = im->pixels[iy * im->w + ix];
            if ((im->flags & IMAGE_SPRITE) && !c) continue;
            v->fb[ty * SCREEN_W + tx] = c;
        }
    }
}

/* A picture scaled into w x h at (dx, dy), clipped to the view (the original keeps pre-scaled copies). */
static void blit_scaled(const Image *im, int dx, int dy, int w, int h)
{
    if (!im || w <= 0 || h <= 0) return;
    Video *v = &G->video;
    for (int y = 0; y < h; y++) {
        int ty = dy + y;
        if (ty < CLIP_TOP || ty > CLIP_LAST) continue;
        int iy = y * im->h / h;
        for (int x = 0; x < w; x++) {
            int tx = dx + x;
            if (tx < 0 || tx >= SCREEN_W) continue;
            uint8_t c = im->pixels[iy * im->w + x * im->w / w];
            if ((im->flags & IMAGE_SPRITE) && !c) continue;
            v->fb[ty * SCREEN_W + tx] = c;
        }
    }
}

/* A picture scaled to w x h and turned by `angle` degrees round the middle of its bottom edge at
 * (cx, by), the way 1dcb:0379 turns the view (the original keeps 7 turned copies, 2a21 a0ee). */
static void blit_turned(const Image *im, int cx, int by, int w, int h, int angle)
{
    if (!im || w <= 0 || h <= 0) return;
    int sn = road_aq_sin(angle), cs = road_aq_sin(angle + 0x5a), r = w + h;
    Video *v = &G->video;
    for (int ty = by - r; ty <= by + r; ty++) {
        if (ty < CLIP_TOP || ty > CLIP_LAST) continue;
        for (int tx = cx - r; tx <= cx + r; tx++) {
            if (tx < 0 || tx >= SCREEN_W) continue;
            int dx = tx - cx, dy = ty - by;
            int u = ((dx * cs - dy * sn) >> 8) + w / 2, vv = ((dx * sn + dy * cs) >> 8) + h;
            if (u < 0 || u >= w || vv < 0 || vv >= h) continue;
            uint8_t c = im->pixels[(vv * im->h / h) * im->w + u * im->w / w];
            if ((im->flags & IMAGE_SPRITE) && !c) continue;
            v->fb[ty * SCREEN_W + tx] = c;
        }
    }
}

/* af98's Aqueduct part: the opponent up a wall is lifted and tilted (1dcb:ae9c picks one of the 7
 * turned pictures, -1 for steeper); the side and big rear views only show upright. */
static void aq_opponent(int d, int *kind, int *x, int *y, int *turn, int *tilt)
{
    const AqView *q = &g_aq;
    int over = opp_x < q->s_left + 0x5fa ? q->s_left - opp_x + 0x5fa : q->s_right + 0xcc6 < opp_x ? opp_x - q->s_right - 0xcc6 : 0;
    int t = over >= 100 ? 0x2d : over < 1 ? 0 : over * 0x2d / 100;
    if (opp_x - ((q->s_right + q->s_left + 0x12c0) >> 1) < 0) t = -t;
    t += q->bank;
    if (q->bank < 0 && q->bank > -0x5a) t += q->bank >> 2;
    if (t > 0x167) t -= 0x168;
    if (t < -0x167) t += 0x168;
    int a = SDL_abs(t), k = a < 0xd ? 3 : a < 0x17 ? 2 : a < 0x25 ? 1 : a < 0x38 ? 0 : -1;
    if (t > 0 && k != -1) k = 6 - k;
    *turn = k;
    *tilt = t;
    if (*kind == 0 && k != 3) { *kind = -1; return; }
    if (*kind == 1 && k != 3) *kind = 2;
    int h = q->s_floor + 2000;                                         /* up a wall: lifted (1dcb:afd0) */
    if (opp_x < q->s_left + 0x5c8)
        h -= (int)((long)(q->s_left - opp_x + 0x5a0) * (q->s_top - q->s_floor + 0x154) / (400 + q->s_left));
    else if (opp_x > q->s_right + 0xcf8)
        h -= (int)((long)(q->s_top - q->s_floor + 0x154) * (opp_x - q->s_right - 0xcf8) / (400 - q->s_right));
    int px = road_proj_x(&view, d, opp_x, view.heading), py = road_proj_y(&view, d, h, -q->pitch);
    if (rear_hit || result == RES_HIT_OPP) py = 0x68;
    road_aq_rotate(px, py, x, y);
}

static void draw_opponent_mirror(void);
static int opp_paint(void)
{
    Player *p = &G->player;
    return police >= 2 ? 5 : rival == KING ? g_models[rival_model(p, rival)].colour : rival_get(p, rival, 0xa);
}
static int my_paint(void) { return police >= 2 ? 5 : you->colour; }   /* 8d0a(5) repaints register 12 */
/* The opponent's pictures (DS:aa9e record, 14 bytes): the police use set 5 but keep his side view. */
static int opp_pic(int entry)
{
    const uint8_t *t = data_race_sprites() + 14 * sprite_set(rival_model(&G->player, rival));
    if (police >= 2 && entry) t = data_race_sprites() + 14 * 5;
    return t[2 * entry] | t[2 * entry + 1] << 8;
}

static void draw_opponent(void)
{
    int side_pic = opp_pic(0), big_pic = opp_pic(1), med_pic = opp_pic(2);   /* DS:aa9e: side, big and medium rear */
    int ahead = opp_ahead, kind = opp_kind;
    if (opp_hidden || kind < 0 || kind >= 9) return;
    int d = ahead < 0 ? -((-ahead) >> 3) : ahead >> 3;
    if (kind == 0) d += 0x10;
    if (d < -2 && kind == 2) d += 8;
    d = SDL_abs(d) + 4;
    int x, y, turn = 3, tilt = 0;
    if (aqueduct()) {
        aq_opponent(d, &kind, &x, &y, &turn, &tilt);
        if (kind < 0) return;
    } else {
        x = road_proj_x(&view, d, opp_x, view.heading - g_road.heading[road_seg(opp_pos)]);
        y = road_proj_y(&view, d, GROUND, g_road.height[road_seg(pos)] - g_road.height[road_seg(opp_pos)]);
    }
    game_set_paint(G, opp_paint());
    if (kind == 0) {                                                   /* the car beside you, cut by the pillar */
        const Image *im = game_picture(G, side_pic);
        if (!im) return;
        int sx = im->w - x + 0x40;
        if (sx < 0) sx = 0;
        blit_clip(im, sx, 0, im->w - sx, im->h, 0, y);
    } else if (kind == 1) {
        const Image *im = game_picture(G, big_pic);
        if (im) blit_clip(im, 0, 0, im->w, im->h, x - im->w, y - im->h + 0x11);
    } else {
        const Image *im = game_picture(G, med_pic);
        if (!im) return;
        double scale = kind == 2 ? 1.0 : 12.0 / d;                     /* stands in for the pre-scaled copies */
        if (scale > 1.0) scale = 1.0;
        int w = (int)(im->w * scale), h = (int)(im->h * scale);
        if (y > 0x65) y = 0x65;
        if (turn != 3) blit_turned(im, x, y, w, h, turn < 0 ? tilt : (int16_t)exe_ds_u16((uint16_t)(0x7fbe + 2 * turn)));
        else blit_scaled(im, x - w / 2, y - h, w, h);
    }
}

/* af98 for kinds 9..12: his front (picture +0x0a of the DS:aa9e record; 10..12 are pre-shrunk
 * copies in the original, scaled 12/d here) in the mirror, through 06ba / 070e with the turn
 * taken as 10 - (a56a - his road heading). */
static void draw_opponent_mirror(void)
{
    if (opp_hidden || opp_kind < 9 || opp_kind > 12) return;
    const Image *im = game_picture(G, opp_pic(5));
    if (!im) return;
    int a = opp_ahead, d = -(a < 0 ? -((-a) >> 3) : a >> 3), turn = 0, h = GROUND, rise, x, y, angle = 0;
    if (aqueduct()) {
        int kind = 2, t = 3, tilt = 0;                                  /* his lift and tilt as ahead */
        aq_opponent(4, &kind, &x, &y, &t, &tilt);
        const AqView *q = &g_aq;
        h = q->s_floor + 2000;
        if (opp_x < q->s_left + 0x5c8)
            h -= (int)((long)(q->s_left - opp_x + 0x5a0) * (q->s_top - q->s_floor + 0x154) / (400 + q->s_left));
        else if (opp_x > q->s_right + 0xcf8)
            h -= (int)((long)(q->s_top - q->s_floor + 0x154) * (opp_x - q->s_right - 0xcf8) / (400 - q->s_right));
        turn = view.heading;
        rise = q->pitch;
        angle = t == 3 ? 0 : t < 0 ? tilt : (int16_t)exe_ds_u16((uint16_t)(0x7fbe + 2 * t));
    } else {
        turn = view.heading - g_road.heading[road_seg(opp_pos)];
        rise = -(g_road.height[road_seg(pos)] - g_road.height[road_seg(opp_pos)]);
    }
    x = road_mproj_x(&view, d, opp_x, 10 - turn);
    y = road_mproj_y(&view, d, h, rise);
    if (aqueduct()) road_aq_rotate(x, y, &x, &y);
    double scale = opp_kind == 9 ? 1.0 : 12.0 / (d > 0 ? d : 1);
    if (scale > 1.0) scale = 1.0;
    game_set_paint(G, opp_paint());
    road_sprite(im, x, y, (int)(im->w * scale), (int)(im->h * scale), angle);
}

/* 1dcb:a1c0: over the top of a wall the car rolls over, eight frames (slowly: it only tips). */
static void aq_roll_step(void)
{
    if (aq_roll == 0) {
        int a = (opp_pos - pos) * 8 + (opp_sub - sub < 0 ? -((sub - opp_sub) >> 3) : (opp_sub - sub) >> 3);
        aq_gap = a < 0x10 ? -1 : ((a + 8) >> 4) + 2;
        aq_head0 = heading;
    }
    sub += frame_distance(crash_speed);                                /* d908 goes on moving you */
    pos += sub >> 6;
    sub &= 0x3f;
    if (crash_speed < 0x1f) {
        g_aq.roll = aq_side < 1 ? 6 : -6;
        g_aq.lift = 10;
        aq_render();
        g_aq.roll = 0;
        aq_roll = 9;
        return;
    }
    if (aq_roll < 8) {
        if (aq_gap > 1) opp_pos += 2;
        g_aq.roll += aq_side < 1 ? -0x22 : 0x20;
        heading -= aq_head0 / 6;
        cam_x += aq_side < 1 ? -0x28 : 0x28;
        g_aq.lift = (aq_roll > 3 ? 8 - aq_roll : aq_roll) * (crash_speed / 6);
        aq_render();
        aq_roll++;
        return;
    }
    aq_render();
    g_aq.roll = 0;
    aq_roll = 9;
}

static void aq_render(void)
{
    view = (RoadView){ pos, sub, cam_x, CAM_H, heading, race, opp_pos, 0, draw_opponent, me.speed, autodrive == 1,
                       draw_opponent_mirror, !starter_there(), starter_seg };
    road_draw(G, &view);
    memcpy(aq_pix, G->video.fb, sizeof aq_pix);
    aq_have = 1;
}

/* 0dcb:a746: the starter stands close by on the left (torso 0x816, arm frames 0x817); his arm
 * comes down at "Go", and once you roll he slides out of the window, 8 pixels a frame. */
static void draw_starter(void)
{
    int shift = (starter_slide & ~3) * 2;
    if (shift > 0x58) return;
    const Image *torso = game_picture(G, aqueduct() ? 0x813 : 0x816), *arm = game_picture(G, aqueduct() ? 0x814 : 0x817);   /* DS:b7b2 (1001:8d9e) */
    int x = 8 - shift, frame_x = flag_dropped ? 0x30 : 0;
    for (int part = 0; part < 2; part++) {                             /* clipped at x = 8 like the original */
        const Image *im = part ? arm : torso;
        if (!im) continue;
        int sx = part ? frame_x : 0, dx = x + part * 0x30, w = 0x30;
        if (dx < 8) { sx += 8 - dx; w -= 8 - dx; dx = 8; }
        if (w > 0) blit_clip(im, sx, 0, w, 0x52, dx, 0x12);
    }
}

static void needle(int cx, int cy, double deg, int r0, int r1, uint8_t colour)
{
    double a = deg * M_PI / 180.0;
    double dx = cos(a), dy = -sin(a);
    for (int r = r0 * 4; r <= r1 * 4; r++) {
        int x = cx + (int)lround(dx * r / 4.0), y = cy + (int)lround(dy * r / 4.0);
        video_fill(&G->video, x, y, 1, 1, colour);
    }
}

static void draw_dash(void)
{
    int t = dash_type(you->model);
    if (t > 2) t = 0;
    static const uint16_t dash_pic[3] = { 0x7db, 0x7e3, 0x7dc };       /* DS:78c8 */
    game_draw_picture(G, dash_pic[t], 0, VIEW_BOTTOM);
    /* Tachometer (2a21:038e): 9 degrees per 200 rpm around DS:1c0a/1c10. */
    int rpm = SDL_clamp(me.rpm, 0, REDLINE);
    double ta = (3000 - rpm) * 9.0 / 200.0 + 90;
    needle(ds16((uint16_t)(0x1c0a + 2 * t)), ds16((uint16_t)(0x1c10 + 2 * t)) + VIEW_BOTTOM, ta,
           ds16((uint16_t)(0x1c1c + 2 * t)), ds16((uint16_t)(0x1c16 + 2 * t)), 3);
    /* Speedometer (2a21:015c). */
    int v = SDL_clamp(me.speed, 0, 0x82);
    int sx = ds16((uint16_t)(0x1c22 + 2 * t)), sy = ds16((uint16_t)(0x1c28 + 2 * t)) + VIEW_BOTTOM;
    if (t == 0) {                                                      /* the strip speedo: a slanted pointer */
        int x1 = (v - 0x3c) * 6 / 10 + sx, x2 = (v - 0x3c) * 2 / 10 + sx;
        for (int k = 0; k <= 8; k++) video_fill(&G->video, x2 + (x1 - x2) * k / 8, sy - k, 1, 1, 3);
    } else {
        double sa = t == 2 ? (0x69 - v) * 2.0 : (0x46 - v) * 90.0 / 35.0 + 90;
        needle(sx, sy, sa, ds16((uint16_t)(0x1c34 + 2 * t)), ds16((uint16_t)(0x1c2e + 2 * t)), 3);
    }
    /* The steering wheel (2a21:1288): straight, turned left or right (DS:78ce), and the knob on
     * its rim at DS:1c80[a104]. */
    int turned = wheel == 0 ? 0 : wheel < 0 ? 1 : 2;
    game_draw_picture(G, ds16((uint16_t)(0x78ce + 2 * (t * 3 + turned))), 0x50, 0x3c + VIEW_BOTTOM);
    int w = SDL_clamp(wheel, -0x18, 0x18);
    const uint8_t *kp = exe_ds((uint16_t)(0x1c80 + 2 * w), 2);
    if (kp) {
        static const int8_t knob[5][3] = { { -1, -2, 3 }, { -2, -1, 5 }, { -2, 0, 5 }, { -2, 1, 5 }, { -1, 2, 3 } };   /* 3725:0000 */
        uint8_t c = (uint8_t)0xb;
        for (int k = 0; k < 5; k++)
            video_fill(&G->video, kp[0] - knob[k][0] - knob[k][2], kp[1] + VIEW_BOTTOM + knob[k][1], knob[k][2] + 1, 1, c);
    }
    /* Gear plate and knob (2a21:13fe, table DS:1cd0). */
    int type = me.automatic ? 0 : me.gears == 3 ? 1 : 2;
    uint16_t e = (uint16_t)(0x1cd0 + 10 * type);
    int n = ds16(e), plate = ds16((uint16_t)(e + 2)), knob = ds16((uint16_t)(e + 4));
    uint16_t posp = exe_ds_u16((uint16_t)(e + 8));
    game_draw_picture(G, plate, 0x108, 0x27 + VIEW_BOTTOM);
    int idx = me.automatic ? me.auto_mode - 1 : me.gear;
    if (idx >= 0 && idx < n) {
        const int8_t *pp = (const int8_t *)exe_ds((uint16_t)(posp + 2 * idx), 2);
        if (pp) game_draw_picture(G, knob, 0x108 + pp[0], 0x27 + VIEW_BOTTOM + pp[1]);
    }
}

/* 1001:adc0: the officer's hand (2051) at (0, 0x45) gives you the ticket (2009) at (88, 0x14), your
 * name at (0x5e, 0x48) and the car at (0x5e, 99), cut to the ticket's width - 8 (ad36), scheme 13. */
static void draw_ticket(void)
{
    Video *v = &G->video;
    const Image *hand = game_picture(G, 0x803), *ticket = game_picture(G, 0x7d9);
    if (hand) video_blit_part(v, hand, 0, 0, hand->w, hand->h, 0, 0x45);
    if (!ticket) return;
    video_blit_part(v, ticket, 0, 0, ticket->w, ticket->h, hand ? hand->w : 88, 0x14);
    const uint8_t *sc = exe_ds((uint16_t)(0x1d06 + 13 * 5), 5);
    uint8_t fg = sc ? sc[0] : 15, bg = sc ? sc[1] : 0;
    const char *lines[2] = { G->player.name, you ? g_models[you->model].ad : "" };
    for (int k = 0; k < 2; k++) {
        char t[48];
        SDL_strlcpy(t, lines[k], sizeof t);
        while (t[0] && font_text_width(t) > ticket->w - 8) t[SDL_strlen(t) - 1] = 0;
        font_draw(v, 0x5e, k ? 99 : 0x48, t, fg, bg);
    }
}

static void update(Game *g)
{
    Uint64 now = SDL_GetTicks();
    frame_acc += (now - last_frame) * FRAME_HZ / 1000.0;
    engine_acc += (now - last_frame) * ENGINE_HZ / 1000.0;
    last_frame = now;
    if (frame_acc > 10) frame_acc = 10;
    if (engine_acc > 10) engine_acc = 10;
    for (;;) {                                   /* the interrupt and the main loop, interleaved in time */
        double tf = (1.0 - frame_acc) / FRAME_HZ, te = (1.0 - engine_acc) / ENGINE_HZ;
        if (frame_acc < 1.0 && engine_acc < 1.0) break;
        if (engine_acc >= 1.0 && (frame_acc < 1.0 || te <= tf)) {
            engine_acc -= 1.0;
            if (state == R_STAGE || state == R_READY || state == R_GO || state == R_RACING) engine_step();
        } else {
            frame_acc -= 1.0;
            if (state != R_DONE) frame();
        }
    }
    if (state < R_CRASH) engine_sound_update();

    Video *v = &g->video;
    game_set_palette(g, PAL_INCAR);
    if (you) game_set_paint(g, my_paint());
    video_clear(v, 0);
    int t = dash_type(you ? you->model : 0);
    if (t > 2) t = 0;
    int eye = CAM_H, x = cam_x;
    if (fell_steps) eye -= fell_steps * (fell_steps + 1) / 2;          /* 7da2: the view drops away */
    if (aqueduct()) {                                                  /* drawn by the frame: it moves the car */
        if (aq_have) memcpy(v->fb, aq_pix, sizeof aq_pix);
    } else {
        view = (RoadView){ pos, sub, x, eye, heading, race, opp_pos, fall, draw_opponent, me.speed, autodrive == 1,
                           draw_opponent_mirror, !starter_there(), starter_seg };
        road_draw(g, &view);
    }
    if (you) game_set_paint(g, my_paint());
    /* 1dcb:573e: the top of the steering wheel shows above the dashboard (DS:78e0). */
    game_draw_picture(g, ds16((uint16_t)(0x78e0 + 6 * t)), ds16((uint16_t)(0x78e2 + 6 * t)), ds16((uint16_t)(0x78e4 + 6 * t)));
    draw_starter();
    game_draw_picture(g, 0x7d3, 0, 0x12);                             /* 1dcb:56e8: the window's left pillar (DS:9a52) */
    if (you) game_set_paint(g, my_paint());
    game_draw_picture(g, 0x7dd, 0, 0);                                 /* roof and mirror */
    draw_dash();
    if (aq_side && aq_roll < 9) { }                                    /* still rolling */
    else if (state == R_CRASH || (state == R_DONE && (result == RES_CRASH || result == RES_HIT_OPP || (result & RACE_TIRE)))) {
        /* 1001:af0a: the windshield caves in (2064 in two parts round the roof line), the dash
         * edge (DS:78e0's 2048 / 2065) and the corner piece 2066; past 90 mph more comes apart. */
        const Image *glass = game_picture(g, 0x810);
        if (glass) {
            video_blit_part(v, glass, 0, 0, glass->w, 0x5a, 0, 0xb);
            video_blit_part(v, glass, 0, 0x59, glass->w, glass->h - 0x59, 0, 0x65);
        }
        if (t != 2) game_draw_picture(g, t == 0 ? 0x800 : 0x811, 0x40, 0x5b);
        game_draw_picture(g, 0x812, 0, 0x78);
        if (crash_speed > 0x5a)
            for (int k = 0; k < 3 && (state == R_DONE || crash_ticks > 13 + 2 * k); k++)
                game_draw_picture(g, ds16((uint16_t)(0x5136 + 6 * k)), ds16((uint16_t)(0x5138 + 6 * k)), ds16((uint16_t)(0x513a + 6 * k)));
    }
    ticket_step();
    char gear[2] = { 0, 0 };                                           /* d908 -> 2a21:1f5e, DS:1e1e */
    if (state >= R_GO) gear[0] = me.automatic ? (me.auto_mode == 2 ? 'D' : 'N') : (char)('0' + me.gear);
    if (ticket_open) {
        draw_ticket();
        char text[64] = "";
        if (!ticket_paid) SDL_snprintf(text, sizeof text, exe_ds_str(0x51e7), fine);   /* "You have been fined $%2d for speeding" */
        if (ticket_paid) game_draw_status_bar(g, NULL);                /* 1df4: the money paid */
        else game_draw_status_bar(g, text);
    } else game_draw_race_status_bar(g, gear, ready_text[0] ? ready_text : NULL);
    if (repair_open) dialog_draw(v, &repair);
    if (state == R_DONE && result_line1[0]) {
        ui_frame(v, 10, 10, 250, 22, 1);
        ui_set_scheme(5);
        ui_text(v, 13 + (244 - font_text_width(result_line1)) / 2, 12, result_line1);
        ui_text(v, 13 + (244 - font_text_width(result_line2)) / 2, 21, result_line2);
    }
}

const Scene scene_race = { enter, key, NULL, mouse_move, click, update };
