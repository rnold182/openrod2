/* Pop the hood: the engine bay (1001:80a8, dialog 14; drawing 2a21:3b34/3c14/3d10/34a2/3a5a).
 * Layers: 0 engine, 1 intake manifold, 2..4 carburettors. Bolts 1..6: 1 engine, 2-3 intake, 4-6 carbs
 * (car->tune[2..7]); a bolt's state is 0 (out) .. 3 (tight). */
#include <string.h>

#include "../assets/exe.h"
#include "../car.h"
#include "../data.h"
#include "../game.h"
#include "../sound.h"
#include "../ui/font.h"
#include "../ui/partlist.h"

#define BAY_FRAME   1054
#define BAY_PANEL   1147
#define BOLT_SHEET  1186
#define WIRES_OFF   0x432
#define WIRES_ON    0x433
#define SUPERCHARGER_PIC 0x4b8

static const int layer_of_bolt[7] = { -1, 0, 1, 1, 2, 3, 4 };
static const int part_type_of_layer[5] = { PT_ENGINE, PT_INTAKE, PT_CARB, PT_CARB, PT_CARB };  /* DS:4ea8 */

static Dialog dlg, tune;
static PartList spares;
static int tuning;              /* timing-light screen (1001:7aec, dialog 30) */
static int tune_frame;
static Uint64 tune_last;
static int bolt_mode[7];        /* 0 out, 1 tightening, 2 loosening (aiStack in 80a8) */

static int16_t ds16(uint16_t off) { return (int16_t)exe_ds_u16(off); }

static PartInst *layer_part(Car *c, int layer)
{
    if (layer == 0) return &c->engine;
    if (layer == 1) return &c->intake;
    return &c->carb[layer - 2];
}

static int grade_of(const PartInst *p) { return p->part >= 0 ? g_parts[p->part].grade : 0; }

/* Picture of a layer (table DS:47e0, layer*9 + grade; the V6 gets its own stock manifold). */
static int layer_pic(Car *c, int layer)
{
    PartInst *p = layer_part(c, layer);
    if (p->part < 0) return 0;
    int g = grade_of(p);
    if (layer == 1 && g == 0) return c->engine.part >= 0 && grade_of(&c->engine) == 0 ? 0x429 : 0x42a;
    return ds16((uint16_t)(0x47e0 + 2 * ((layer > 2 ? 2 : layer) * 9 + g)));
}

static int layers_installed(Car *c)
{
    int n = 0;
    for (int l = 0; l < 5; l++) if (layer_part(c, l)->part >= 0) n++;
    return n;
}

/* Next empty layer: engine, then manifold, then the first free carb slot. */
static int next_layer(Car *c)
{
    for (int l = 0; l < 5; l++) if (layer_part(c, l)->part < 0) return l;
    return 5;
}

static int max_layers(Car *c)
{
    return c->intake.part >= 0 ? carbs_for_intake_grade(grade_of(&c->intake)) + 2 : 5;
}

static int carb_slot(Car *c, int k)     /* DS:209c: first carb slot for each intake grade */
{
    return exe_ds(0x209c, 16)[grade_of(&c->intake)] + k;
}

/* 2a21:33aa: where a layer sits (x, bottom y) inside the bay. */
static void layer_pos(Car *c, int layer, int *x, int *y)
{
    if (layer == 0) {
        int e = grade_of(&c->engine);
        *x = ds16((uint16_t)(0x201e + 4 * e)); *y = ds16((uint16_t)(0x2020 + 4 * e));
    } else if (layer == 1) {
        *x = 0x38; *y = 0x5b;
    } else {
        int slot = carb_slot(c, layer - 2);
        *x = ds16((uint16_t)(0x204e + 4 * (slot - 4))); *y = ds16((uint16_t)(0x2050 + 4 * (slot - 4)));
    }
}

/* 2a21:33fe: bolt position. */
static void bolt_pos(Car *c, int bolt, int *x, int *y)
{
    int layer = layer_of_bolt[bolt];
    if (layer == 0) {
        int e = grade_of(&c->engine);
        *x = ds16((uint16_t)(0x202a + 4 * e)); *y = ds16((uint16_t)(0x202c + 4 * e));
    } else if (layer == 1) {
        int pic = layer_pic(c, 1);
        if (bolt == 3) *x = (pic == 0x429 || pic == 0x428) ? 0x84 : 0x9a;
        else *x = 0x4b;
        if (pic == SUPERCHARGER_PIC) (*x)++;
        *y = 0x5a;
    } else {
        layer_pos(c, layer, x, y);
        int kind = layer_pic(c, layer) - 0x42d;
        if (kind < 0 || kind > 2) kind = 0;
        *x += ds16((uint16_t)(0x2082 + 4 * kind)); *y += ds16((uint16_t)(0x2084 + 4 * kind));
    }
}

static void place_hotspots(Car *c)
{
    for (int i = 0; i < dlg.count; i++) {
        Widget *w = &dlg.w[i];
        if (w->id >= 1 && w->id <= 6) {
            int b = w->id;
            if (layer_part(c, layer_of_bolt[b])->part >= 0) {
                int x, y;
                bolt_pos(c, b, &x, &y);
                dialog_set_rect(&dlg, i, x + 0x13, y - 0xd, x + 0x1d, y + 2);
            } else {
                w->flags &= (uint16_t)~WF_ENABLED;
            }
        } else if (w->id >= 7 && w->id <= 9) {
            w->flags &= (uint16_t)~WF_ENABLED;
        } else if (w->id == -13) {      /* wires, next to the distributor */
            if (c->engine.part >= 0 && c->wires) {
                int e = grade_of(&c->engine);
                int a = c->wires < 0 ? 0x21 : 0x24, b = c->wires < 0 ? 0x16 : 0x14;
                int x = ds16((uint16_t)(0x2036 + 4 * e)) - a, y = ds16((uint16_t)(0x2038 + 4 * e)) - b;
                dialog_set_rect(&dlg, i, x + 0xe, y - 7, x + 0x22, y + 8);
            } else {
                w->flags &= (uint16_t)~WF_ENABLED;
            }
        }
    }
}

static void log_hotspots(void)
{
    for (int i = 0; i < dlg.count; i++)
        if (dlg.w[i].flags & WF_ENABLED)
            SDL_Log("hood hotspot id %d '%s' (%d,%d)-(%d,%d)", dlg.w[i].id, dlg.w[i].text, dlg.w[i].x1, dlg.w[i].y1, dlg.w[i].x2, dlg.w[i].y2);
}

static void sync_bolt_modes(Car *c)
{
    for (int b = 1; b <= 6; b++) bolt_mode[b] = bolt_mode_of(c->tune[1 + b]);
}

static void enter(Game *g)
{
    Car *c = player_car(&g->player);
    dialog_load(&dlg, 14);
    spares.active = 0;
    tuning = 0;
    if (c) {
        sync_bolt_modes(c);
        place_hotspots(c);
        if (SDL_getenv("SR2_DEBUG")) log_hotspots();
    }
}

static void remove_layer(Game *g, Car *c, int layer)
{
    PartInst *p = layer_part(c, layer);
    player_add_spare(&g->player, *p);
    c->price = (int16_t)(c->price - p->value);
    p->part = -1;
    if (layer == 0) c->wires = 0;
    c->timing = (int8_t)(SDL_rand(9) - 5);
}

/* After a bolt comes out (or the wires are pulled), lift off whatever is now free (80a8 @ 8a16). */
static void auto_remove(Game *g, Car *c, int bolt)
{
    Player *p = &g->player;
    int layer = bolt > 0 ? layer_of_bolt[bolt] : -1;
    if (layer >= 2 && bolt_mode[bolt] == 0 && c->carb[layer - 2].part >= 0) {
        remove_layer(g, c, layer);
        p->time_actions += 0x199;
        layer = 1;                      /* removing the last carb may free the manifold */
    }
    if (layer == 1 && layers_installed(c) == 2 && bolt_mode[2] == 0 && bolt_mode[3] == 0) {
        remove_layer(g, c, 1);
        p->time_actions += 0x333;
    }
    if (layers_installed(c) == 1 && c->wires == -1 && bolt_mode[1] == 0) {
        remove_layer(g, c, 0);
        p->time_actions += 0x666;
    }
}

static void click_bolt(Game *g, Car *c, int b)
{
    if (layer_part(c, layer_of_bolt[b])->part < 0) return;
    g->player.time_actions += 0x36;
    if (bolt_turn(&c->tune[1 + b], &bolt_mode[b])) auto_remove(g, c, b);
}

static void open_spares(Game *g, Car *c)
{
    int layer = next_layer(c);
    int type = part_type_of_layer[layer < 5 ? layer : 4];
    if (!partlist_open(&spares, &g->player, type, NULL))
        game_message(g, type == PT_ENGINE ? 27 : type == PT_INTAKE ? 28 : 29);   /* no spare engine/manifold/carb */
}

/* Install the chosen spare in the next empty layer (80a8, Parts branch). */
static void install(Game *g, Car *c, int spare)
{
    Player *p = &g->player;
    int layer = next_layer(c);
    if (layer >= max_layers(c)) { game_message(g, 22); return; }     /* "Where will you put it, speedy?" */
    PartInst part = p->parts[spare];
    const PartInfo *pi = &g_parts[part.part];
    const CarModel *m = &g_models[c->model];
    int fits = (pi->make & m->spec1 & 0x0f) != 0;
    long cost;
    if (layer == 0) {
        cost = fits ? 0x666 : 0x888;
    } else if (layer == 1) {
        fits = fits && exe_ds(0x47a8, 32)[pi->grade + grade_of(&c->engine) * 9];
        cost = fits ? 0x333 : 0x444;
    } else {
        fits = fits && exe_ds(0x47c4, 32)[pi->grade + grade_of(&c->intake) * 3];
        cost = fits ? 0x199 : 0x222;
    }
    p->time_actions += cost;
    if (!fits) { game_message(g, 24); return; }                      /* "That won't fit!" */
    player_remove_spare(p, spare);
    *layer_part(c, layer) = part;
    c->price = (int16_t)(c->price + part.value);
    if (layer == 0) c->wires = -1;
    for (int b = 1; b <= 6; b++)                                     /* its bolts go in loose */
        if (layer_of_bolt[b] == layer) { c->tune[1 + b] = 1; bolt_mode[b] = 1; }
    c->timing = (int8_t)(SDL_rand(9) - 5);
}

/* Engine idle sound on voices 3 and 4 ("motor"), pitch following the timing (2c57:0b2c..0c5c). */
static void tune_sound(const Car *c)
{
    int t = c->timing, vol = t == 0 ? 0x4a : 0x52;
    sound_voice_bend(3, ((t + 16) * 2) << 8);
    sound_voice_volume(3, vol);
    sound_voice_bend(4, (t * 3 + 0x20) << 8);
    sound_voice_volume(4, vol);
}

static void tune_open(Car *c)
{
    dialog_load(&tune, 30);
    tuning = 1;
    tune_frame = 0;
    tune_last = SDL_GetTicks();
    for (int v = 3; v <= 4; v++) {
        sound_voice_timbre(v, "motor", 0x40);
        sound_pitch_range(6);
        sound_voice_bend(v, 0);
        sound_voice_note_off(v);
        sound_voice_note_on(v, 0x18);
    }
    tune_sound(c);
}

static void tune_close(Game *g)
{
    sound_voice_volume(3, 0);
    sound_voice_note_off(3);
    sound_voice_note_off(4);
    sound_voice_bend(4, 0x2000);
    sound_voice_timbre(4, "paint", 0x6a);
    g->player.time_actions += 0x444;
    tuning = 0;
}

static void choose_tune(Game *g, int i)
{
    Car *c = player_car(&g->player);
    if (i < 0 || !c) return;
    int id = tune.w[i].id;
    if (id == -10 && c->timing < 3) c->timing++;            /* Advance */
    else if (id == -7 && c->timing > -5) c->timing--;       /* Retard */
    else if (id == -1) { tune_close(g); return; }
    tune_sound(c);
}

static void draw_tune(Game *g, Car *c)
{
    Video *v = &g->video;
    const Image *pulley = game_picture(g, 0x420), *spin = game_picture(g, 0x424), *belt = game_picture(g, 0x434);
    video_fill(v, 0x1c, 4, 0xc0, 0xb5, 0);
    video_fill(v, 0xf2, 0x1f, 0x27, 9, 5);              /* cover TUNE and PARTS: only DONE stays */
    video_fill(v, 0xf2, 0x96, 0x27, 9, 5);
    video_blit(v, pulley, 0x40, 0x21);
    video_fill(v, 0x96, 0x71, 3, 1, 0x0e);
    if (game_ticks_since(tune_last) >= 4) { tune_frame = (tune_frame + 1) % 3; tune_last = SDL_GetTicks(); }
    /* Pulley: frame 0 is the still picture, 1 and 2 come from picture 1060. */
    if (tune_frame > 0 && spin) video_blit_part(v, spin, (tune_frame - 1) * 32, 0, 32, 0x2d, 0x48, 99);
    /* Belt: frames run right to left over picture 1076 (the third is the still picture). */
    if (tune_frame < 2 && belt) video_blit_part(v, belt, tune_frame == 0 ? 16 : 0, 0, 16, 0x21, 0x60, 0x3c);
    /* Timing mark (table DS:4f1c, 5 lines per setting). */
    for (int k = 0; k < 5; k++) {
        const int8_t *seg = (const int8_t *)exe_ds((uint16_t)(0x4f1c + 2 * (c->timing * 5 + k)), 2);
        if (!seg || seg[0] < 0) continue;
        video_fill(v, 0x90 + seg[0], (c->timing + 0x25) * 3 + k, seg[1] - seg[0] + 1, 1, 0x0b);
    }
}

static void choose(Game *g, int i)
{
    Car *c = player_car(&g->player);
    if (i < 0 || !c) return;
    int id = dlg.w[i].id;
    if (id >= 1 && id <= 6) click_bolt(g, c, id);
    else if (id == -13 && c->wires) {
        c->wires = (int8_t)-c->wires;
        g->player.time_actions += 0x6d;
        if (c->wires < 0) auto_remove(g, c, 0);
    } else if (id == -2) open_spares(g, c);
    else if (id == -3) {
        if (!car_runnable(c, 0)) game_message(g, 26);                 /* "Can't tune that mess, klutz!" */
        else tune_open(c);
    } else if (id == -1) game_set_scene(g, &scene_garage);
    place_hotspots(c);          /* bolt modes persist while the hood is open (aiStack in 80a8) */
}

static void picked(Game *g, int r)
{
    Car *c = player_car(&g->player);
    if (r >= 0 && c) { install(g, c, r); place_hotspots(c); }
}

static void key(Game *g, SDL_Scancode sc, SDL_Keycode k)
{
    (void)k;
    if (tuning) choose_tune(g, dialog_key(&tune, sc));
    else if (spares.active) picked(g, partlist_key(&spares, sc));
    else choose(g, dialog_key(&dlg, sc));
}

static void click(Game *g, int x, int y)
{
    if (SDL_getenv("SR2_DEBUG")) SDL_Log("hood click %d,%d -> widget %d (list %d)", x, y, dialog_click(&dlg, x, y), spares.active);
    if (tuning) choose_tune(g, dialog_click(&tune, x, y));
    else if (spares.active) picked(g, partlist_click(&spares, x, y));
    else choose(g, dialog_click(&dlg, x, y));
}

static void mouse_move(Game *g, int x, int y)
{
    (void)g;
    if (tuning) dialog_mouse_move(&tune, x, y);
    else if (spares.active) partlist_mouse_move(&spares, x, y);
    else dialog_mouse_move(&dlg, x, y);
}

static void draw_bolt(Game *g, Car *c, int b)
{
    int state = c->tune[1 + b];
    if (state == 0 || layer_part(c, layer_of_bolt[b])->part < 0) return;
    const Image *sheet = game_picture(g, BOLT_SHEET);
    if (!sheet) return;
    int x, y;
    bolt_pos(c, b, &x, &y);
    int h = 16 - 4 * state;                     /* 12/8/4 rows: the bolt sinks in as it tightens */
    video_blit_part(&g->video, sheet, 0, 0, 8, h, x + 0x18, y - h);   /* 2a21:34a2: 8x12 box at (x+24, y-12) */
}

static void update(Game *g)
{
    Video *v = &g->video;
    Car *c = player_car(&g->player);
    game_set_palette(g, PAL_SCREENS);
    if (c) game_set_paint(g, c->colour);
    video_clear(v, 0);
    game_draw_picture(g, BAY_FRAME, 0x18, 0);
    if (c) {
        for (int l = 0; l < 5; l++) {
            int pic = layer_pic(c, l);
            const Image *im = pic ? game_picture(g, pic) : NULL;
            if (!im) continue;
            int x, y;
            layer_pos(c, l, &x, &y);
            video_blit(v, im, x + 24 - (pic == SUPERCHARGER_PIC), y - im->h);
        }
        for (int b = 1; b <= 6; b++) draw_bolt(g, c, b);
        if (c->engine.part >= 0 && c->wires) {
            int e = grade_of(&c->engine);
            int wx = ds16((uint16_t)(0x2036 + 4 * e)), wy = ds16((uint16_t)(0x2038 + 4 * e));
            if (c->wires < 0) game_draw_picture(g, WIRES_OFF, wx - 9, wy - 0x16);
            else game_draw_picture(g, WIRES_ON, wx - 0xc, wy - 0x14);
        }
    }
    game_draw_picture(g, BAY_PANEL, 0xe0, 0);
    if (tuning && c) {
        draw_tune(g, c);
        const Widget *h = tune.hover >= 0 ? &tune.w[tune.hover] : NULL;
        game_draw_status_bar(g, h ? h->text : NULL);
        dialog_draw(v, &tune);
        return;
    }
    const char *hover = spares.active ? partlist_hover_text(&spares)
                      : dlg.hover >= 0 && dlg.w[dlg.hover].type == W_HOTSPOT ? dlg.w[dlg.hover].text : NULL;
    game_draw_status_bar(g, hover);
    dialog_draw(v, &dlg);
    if (spares.active) partlist_draw(v, &spares);
}

const Scene scene_hood = { enter, key, NULL, mouse_move, click, update };
