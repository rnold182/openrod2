/* The drag race (1001:be82 setup, 2a21:77f4 race screen, 1001:8ef4 start, 1001:d908 per frame,
 * the timer interrupt 0001:2620 for your engine, 1001:cb98 for the opponent's, 1001:b584 result).
 *
 * Timing as in SR.EXE on a fast PC: one frame per BIOS tick (18.2 Hz), the engines step every third
 * frame (the interrupt's 4 x 3 divider of the 72.8 Hz clock), distance per frame from table DS:8016.
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
#include "../ui/font.h"
#include "race.h"

#define FRAME_HZ   (1193182.0 / 65536.0)   /* 18.2 */
#define START_SEG  200                     /* DS:9ae2 for a drag race */
#define FINISH_SEG 0x17c                   /* DS:a2aa */
#define REDLINE    0x1644
#define VIEW_BOTTOM 100                    /* the dashboard starts here */

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

enum { R_STAGE, R_READY, R_GO, R_RACING, R_DONE };

static Game *G;
static Car *you;
static Engine me, opp;
static int rival, stake, bet;
static int state;
static Uint64 phase_start, last_frame;
static double frame_acc;
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

void race_autodrive(int on) { autodrive = on; }

#define RES_WON     0x100
#define RES_LOST    0x001
#define RES_ENGINE  0x005
#define RES_TRANS   0x011
#define RES_JUMPED  0x081
#define RES_TIRES   0x009

static int dash_type(int model) { return (g_models[model].patch[0] >> 8) & 0xff; }
static int sprite_set(int model) { return g_models[model].patch[0] & 0xff; }

/* 1001:ce9c: distance per frame for a speed (fast machines: table DS:8016). */
static int frame_distance(int speed)
{
    if (speed < 1) return 0;
    if (speed > 0x82) speed = 0x82;
    return (int8_t)exe_ds(0x8016, 0x83)[speed];
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
    if (pos > FINISH_SEG) {                                            /* past the line: stand on the brakes */
        brk = 1;
        accel = shift = 0;
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

static void opp_engine(void)
{
    Engine *e = &opp;
    int fresh = steps - opp_steps_done;
    opp_steps_done = steps;
    int before = e->speed;
    for (int k = 0; k < fresh; k++) {
        int gap = opp_pos - pos, target = -1;
        if (gap < 13) {
            if (gap > -5 && gap < 5) {                                 /* side by side: stay in reach */
                target = 10 + me.speed;
                int cruise = model_max_speed(&G->player, rival_model(&G->player, rival)) * 7000 / 10000;
                if (pos > 0x1d && target < cruise) target = cruise;
                if (target >= e->speed) target = -1;
            }
        } else {
            int top = model_max_speed(&G->player, rival_model(&G->player, rival));
            int cruise = me.speed < 0x61 && rival != KING ? top * 7000 / 10000 : top * 8000 / 10000;
            if (e->speed > cruise) target = cruise;                    /* way ahead: no need to show off */
        }
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
    if (opp_pos >= FINISH_SEG) {                                       /* through the lights: brake */
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
        int c = opp_pos - START_SEG ? (pos - START_SEG) * 100 / (opp_pos - START_SEG) : 0;
        covered = c > 99 ? 99 : c;
    } else {
        covered = 100;
    }
}

static void race_frame(void)
{
    if (frame_distance(me.speed) > 0) starter_slide += 4;               /* 0dcb:1774 */
    if (blowout_at && pos >= blowout_at && !result) { result = RES_TIRES; return; }
    opp_engine();
    sub += frame_distance(me.speed);
    if (sub > 0x3f) { pos += sub >> 6; sub &= 0x3f; }
    opp_sub += frame_distance(opp.speed);
    if (opp_sub > 0x3f) { opp_pos += opp_sub >> 6; opp_sub &= 0x3f; }
    if (pos >= FINISH_SEG || opp_pos >= FINISH_SEG) finish_line();
    if (pos >= FINISH_SEG && finished && me.speed < 1 && !result) result = you_first ? RES_WON : RES_LOST;
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

static void settle(void)
{
    Player *p = &G->player;
    if (you->fuel > 5) you->fuel -= 5; else you->fuel = 0;            /* a drag race burns half a gallon */
    if (result == RES_WON) {
        p->money += bet;
        if (stake == 5) win_pink_slip();
        game_message(G, rival == KING ? 0x4f : (int16_t)exe_ds_u16((uint16_t)(0x512a + 2 * stake)));
    } else if (result == RES_LOST) {
        p->money -= bet;
        if (stake == 5) lose_car();
        game_message(G, rival == KING ? 0x55 : stake == 5 ? 0x53 : 0x54);
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
    } else if (result == RES_TIRES) {
        p->money -= bet;
        you->price = (int16_t)(you->price - you->tires.value);
        you->tires.part = -1;
        game_message(G, stake == 5 ? 0x5c : 0x5f);
        if (stake == 5) lose_car();
    }
    /* Standings (tail of 1001:a626). */
    int won = result == RES_WON;
    if (result != RES_JUMPED) player_set_counter(p, 0, player_counter(p, 0) + 1);
    if (won) player_set_counter(p, 1, player_counter(p, 1) + 1);
    int rep = player_rep(p, 0), rrep = rival_get(p, rival, 0xf);
    if (won) { if (rep < 8) rep++; if (rrep > 1) rrep--; }
    else { if (rep > 1) rep--; if (rrep < 8) rrep++; }
    p->header_raw[0x28] = (uint8_t)rep;
    rival_set(p, rival, 0xf, rrep);
    rival_set(p, rival, 0xc, rival_get(p, rival, 0xc) + 1);
    int rep2 = player_rep(p, 1), rrep2 = rival_get(p, rival, 0x10);
    if (you_first) { if (rep2 < 8) rep2++; if (rrep2 > 1) rrep2--; }
    else { if (rep2 > 1) rep2--; if (rrep2 < 8) rrep2++; }
    p->header_raw[0x29] = (uint8_t)rep2;
    rival_set(p, rival, 0x10, rrep2);
    if (rival == KING && won) player_set_counter(p, 5, player_counter(p, 5) + 1);
}

/* ------------------------------------------------------------------ scene */

void race_start(Game *g, int r, int race_type, int stake_, int amount)
{
    (void)race_type;
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
    pos = opp_pos = START_SEG;
    sub = opp_sub = 0;
    steps = opp_steps_done = 0;
    speed_sum = 0;
    finished = you_first = result = 0;
    limiter_count = 0;
    gas = brake = shift_pressed = shift_latch = 0;
    frame_no = 0;
    frame_acc = 0;
    /* 1001:be82: tire wear for the run, and worn tires may give out (bc88). */
    if (you->tires.part >= 0) {
        int base = g_parts[you->tires.part].grade == 0 ? 300 : 200;
        int w = you->tires.wear + (int)SDL_rand(300) + base;
        you->tires.wear = (int16_t)(w > 10000 ? 10000 : w);
    }
    blowout_at = 0;
    if (you->tires.wear >= 8000 && (int)SDL_rand(2000) + you->tires.wear >= 0x251d) {
        int len = FINISH_SEG - START_SEG;
        blowout_at = (int)SDL_rand(len >> 2) + (len >> 1) + START_SEG;
    }
    state = R_STAGE;                                                    /* 8d9e waits 0xf + 0x19 ticks first */
    phase_start = last_frame = SDL_GetTicks();
    wait_ticks = 0xf + 0x19;
    ready_text[0] = 0;
    starter_slide = flag_dropped = 0;
    game_message_clear(g);
    game_set_palette(g, PAL_INCAR);
    sound_music_duck(opt_tune_music_duck);                              /* rebuild: hear the engine */
    engine_sound(1);
}

static void leave_race(void)
{
    engine_sound(0);
    sound_music_duck(0);
    G->player.time_actions += 0x444;
    /* b584's answer: a pink-slip race or a broken car means home (towed, or on foot); otherwise
     * it's back to Burger's. */
    int home = !player_car(&G->player) || stake == 5 || result == RES_ENGINE || result == RES_TRANS ||
               result == RES_TIRES || !car_runnable(player_car(&G->player), 1);
    game_set_scene(G, home ? &scene_garage : &scene_street);
}

static void key(Game *g, SDL_Scancode sc, SDL_Keycode k)
{
    (void)g; (void)sc;
    if (state == R_DONE) { leave_race(); return; }
    if (k == SDLK_SPACE || k == SDLK_RETURN || k == SDLK_LSHIFT || k == SDLK_RSHIFT) shift_pressed = 1;
}

static void click(Game *g, int x, int y)
{
    (void)g; (void)x; (void)y;
    if (state == R_DONE) { leave_race(); return; }
    shift_pressed = 1;                                                 /* left button: the gear stick */
}

static void read_controls(void)
{
    const bool *ks = SDL_GetKeyboardState(NULL);
    SDL_MouseButtonFlags mb = SDL_GetMouseState(NULL, NULL);
    gas = ks[SDL_SCANCODE_UP] || ks[SDL_SCANCODE_W] || (mb & SDL_BUTTON_RMASK);
    brake = ks[SDL_SCANCODE_DOWN] || ks[SDL_SCANCODE_S];
    if (autodrive) {
        gas = state >= R_GO;
        brake = 0;
        if (state >= R_GO && (me.gear == 0 || (me.rpm > 5000 && me.gear < me.gears))) shift_pressed = 1;
    }
}

static void frame(void)
{
    frame_no++;
    if (SDL_getenv("SR2_DEBUG") && frame_no % 18 == 0)
        SDL_Log("race f%d st%d you pos %d spd %d rpm %d g%d band %d acc %d | opp pos %d spd %d rpm %d g%d lim %d res %x",
                frame_no, state, pos, me.speed, me.rpm, me.gear, me.band, me.accel, opp_pos, opp.speed, opp.rpm, opp.gear, opp_limit, result);
    read_controls();
    if (frame_no % 3 == 0 && state != R_DONE) engine_step();
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
    if (result && state != R_DONE) {
        state = R_DONE;
        engine_sound(0);
        settle();
        result_screen();
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
#define CAM_X     0xa00
#define CAM_H     0x76c
#define GROUND    2000
#define OPP_X     2200
#define CLIP_TOP  0x12
#define CLIP_LAST 0x65

static RoadView view;
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

/* 0dcb:af98: which picture of the opponent shows, from how far ahead (1/64 segment) and which side:
 * 0 side view through the side window, 1 big rear view ahead to one side, 2 medium rear view,
 * 3..8 the medium view smaller and smaller, 9.. behind you (the mirror, not drawn yet). */
static int opponent_kind(int ahead, int side)
{
    if (ahead >= 0x3c0) return -1;
    if (ahead >= 0x80) { int k = ((ahead + 0x40) >> 7) + 2; return k > 8 ? 8 : k; }
    if (ahead < -0x38) return ((0x80 - ahead) >> 7) + 8;
    if (ahead < 0x41) {
        if (SDL_abs(side) < 0xb4) return 2;
        if (ahead < 0) return 9;
        return side < 0 ? 0 : 2;
    }
    return side < -0xb4 ? 1 : 3;
}

static void draw_opponent(void)
{
    Player *p = &G->player;
    int set = sprite_set(rival_model(p, rival));
    const uint8_t *tbl = data_race_sprites() + 14 * set;               /* DS:aa9e: side, big and medium rear */
    int side_pic = tbl[0] | tbl[1] << 8, big_pic = tbl[2] | tbl[3] << 8, med_pic = tbl[4] | tbl[5] << 8;
    int ahead = (opp_pos - pos) * 64 + opp_sub - sub;
    int kind = opponent_kind(ahead, OPP_X - CAM_X);
    if (kind < 0 || kind >= 9) return;
    int d = SDL_abs(ahead) >> 3;
    if (kind == 0) d += 0x10;
    d += 4;
    int x = road_proj_x(&view, d, OPP_X, view.heading - g_road.heading[road_seg(opp_pos)]);
    int y = road_proj_y(&view, d, GROUND, g_road.height[road_seg(pos)] - g_road.height[road_seg(opp_pos)]);
    game_set_paint(G, rival == KING ? g_models[rival_model(p, rival)].colour : rival_get(p, rival, 0xa));
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
        blit_scaled(im, x - w / 2, y - h, w, h);
    }
}

/* 0dcb:a746: the starter stands close by on the left (torso 0x816, arm frames 0x817); his arm
 * comes down at "Go", and once you roll he slides out of the window, 8 pixels a frame. */
static void draw_starter(void)
{
    int shift = (starter_slide & ~3) * 2;
    if (shift > 0x58) return;
    const Image *torso = game_picture(G, 0x816), *arm = game_picture(G, 0x817);
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

static void update(Game *g)
{
    Uint64 now = SDL_GetTicks();
    frame_acc += (now - last_frame) * FRAME_HZ / 1000.0;
    last_frame = now;
    if (frame_acc > 10) frame_acc = 10;
    while (frame_acc >= 1.0) { frame_acc -= 1.0; if (state != R_DONE) frame(); }
    if (state != R_DONE) engine_sound_update();

    Video *v = &g->video;
    game_set_palette(g, PAL_INCAR);
    if (you) game_set_paint(g, you->colour);
    video_clear(v, 0);
    view = (RoadView){ pos, sub, CAM_X, CAM_H, 0, RACE_DRAG, opp_pos, draw_opponent };
    if (pos > FINISH_SEG) view.heading = g_road.heading[road_seg(pos)];          /* 1dcb:12ca steers you on */
    road_draw(g, &view);
    draw_starter();
    if (you) game_set_paint(g, you->colour);
    game_draw_picture(g, 0x7dd, 0, 0);                                 /* roof and mirror */
    draw_dash();
    game_draw_status_bar(g, ready_text[0] ? ready_text : NULL);
    if (state == R_DONE && result_line1[0]) {
        ui_frame(v, 10, 10, 250, 22, 1);
        ui_set_scheme(5);
        ui_text(v, 13 + (244 - font_text_width(result_line1)) / 2, 12, result_line1);
        ui_text(v, 13 + (244 - font_text_width(result_line2)) / 2, 21, result_line2);
    }
}

const Scene scene_race = { enter, key, NULL, NULL, click, update };
