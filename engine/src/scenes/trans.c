/* Change transmission (1001:75b8, dialog 12): two bolts hold the gearbox (car->tune[0..1]). */
#include "../anim.h"
#include "../assets/exe.h"
#include "../car.h"
#include "../data.h"
#include "../game.h"
#include "../sound.h"
#include "../ui/partlist.h"

#define TRANS_BACKDROP 1055
#define TRANS_BOLTS    1081     /* 16x6 frames by tightness, then the loose bolt */
#define TRANS_CLIP_TOP    0x0e  /* the sprite mover's band here (2a21:2e92 / 2fc4 / 30f2) */
#define TRANS_CLIP_BOTTOM 0x94
#define BOLT_X            0xb8

static Dialog dlg;
static PartList list;
static int bolt_mode[2];
static unsigned hidden;         /* bit 0/1: bolt still on its way in, bit 2: the gearbox */
#define HIDE_BOLT(b) (1u << (b))
#define HIDE_GEARBOX 4u

static int bolt_y(int b) { return (int16_t)exe_ds_u16((uint16_t)(0x201a + 2 * b)) + 10; }
static void unhide(Game *g, int bits) { (void)g; hidden &= ~(unsigned)bits; }

/* The loose bolt (frame 4) sliding between the top of the band, its hole and the bottom. */
static void bolt_move(int y0, int y1)
{
    AnimMove m = { TRANS_BOLTS, 64, 0, 16, 6, -1, BOLT_X, y0, y1, TRANS_CLIP_TOP, TRANS_CLIP_BOTTOM,
                   SFX_HUM_BOLT, SFX_CLINK, 0 };
    anim_move(&m);
}

static void bolt_drop_in(int b)                 /* 2a21:2e92 with state 1 */
{
    hidden |= HIDE_BOLT(b);
    bolt_move(8, bolt_y(b));
    anim_call(unhide, (int)HIDE_BOLT(b));
}

/* 2a21:30f2: the gearbox comes up from below; one that doesn't fit comes up 16 pixels off. */
static void gearbox_up(Game *g, int pic, int fits)
{
    const Image *t = game_picture(g, pic);
    if (!t) return;
    AnimMove m = { pic, 0, 0, 0, 0, -1, (fits ? 200 : BOLT_X) - t->w, TRANS_CLIP_BOTTOM, 0x7d - t->h,
                   TRANS_CLIP_TOP, TRANS_CLIP_BOTTOM, fits ? SFX_HUM_PART : SFX_HUM_OFF, fits ? SFX_CLUNK : -1,
                   fits ? 0 : 24 };             /* "Pay attention, dummy! That won't fit!" */
    anim_move(&m);
}

static void drop_sound(Game *g, int arg) { (void)g; (void)arg; sound_sfx(SFX_DROP); }   /* 2a21:322e */

static int trans_pic(const Car *c)
{
    return c->trans.part >= 0 ? (int16_t)exe_ds_u16((uint16_t)(0x483a + 2 * g_parts[c->trans.part].grade)) : 0;
}

static void update_widgets(const Car *c)
{
    for (int i = 0; i < dlg.count; i++)
        if (dlg.w[i].id == 1 || dlg.w[i].id == 2) {
            if (c->trans.part >= 0) dlg.w[i].flags |= WF_ENABLED;
            else dlg.w[i].flags &= (uint16_t)~WF_ENABLED;
        }
}

static void enter(Game *g)
{
    Car *c = player_car(&g->player);
    dialog_load(&dlg, 12);
    list.active = 0;
    for (int b = 0; b < 2; b++) bolt_mode[b] = bolt_mode_of(c->tune[b]);
    hidden = 0;
    anim_reset();
    update_widgets(c);
}

static void install(Game *g, Car *c, int spare)
{
    Player *p = &g->player;
    PartInst part = p->parts[spare];
    int pic = (int16_t)exe_ds_u16((uint16_t)(0x483a + 2 * g_parts[part.part].grade));
    if (!(g_parts[part.part].make & g_models[c->model].spec1 & 0x0f)) {
        p->time_actions += 0x888;
        gearbox_up(g, pic, 0);
        anim_call(drop_sound, 0);
        return;
    }
    hidden |= HIDE_GEARBOX;
    gearbox_up(g, pic, 1);
    anim_call(unhide, (int)HIDE_GEARBOX);
    player_remove_spare(p, spare);
    c->trans = part;
    c->price = (int16_t)(c->price + part.value);
    p->time_actions += 0x666;
    for (int b = 1; b >= 0; b--) { c->tune[b] = 1; bolt_mode[b] = 1; bolt_drop_in(b); }
    update_widgets(c);
}

static void gearbox_down(Game *g, int arg)      /* both bolts out: the gearbox comes down */
{
    (void)arg;
    Car *c = player_car(&g->player);
    if (!c || c->trans.part < 0) return;
    sound_sfx(SFX_DROP);
    player_add_spare(&g->player, c->trans);
    c->price = (int16_t)(c->price - c->trans.value);
    c->trans.part = -1;
    c->tune[0] = c->tune[1] = 0;
    g->player.time_actions += 0x666;
    update_widgets(c);
}

static void choose(Game *g, int i)
{
    Car *c = player_car(&g->player);
    if (i < 0 || !c) return;
    int id = dlg.w[i].id;
    if (id == -1) {
        game_set_scene(g, &scene_garage);
    } else if (id == -2) {                          /* Select */
        if (c->trans.part >= 0) game_message(g, 22);
        else if (!partlist_open(&list, &g->player, PT_TRANSMISSION, NULL)) game_message(g, 23);
    } else if ((id == 1 || id == 2) && c->trans.part >= 0) {
        int b = id - 1, before = c->tune[b], mode = bolt_mode[b];
        if (bolt_turn(&c->tune[b], &bolt_mode[b])) {
            sound_sfx(SFX_TURN);                     /* 2a21:2fc4: a last turn, then it falls out */
            bolt_move(bolt_y(b), TRANS_CLIP_BOTTOM);
            if (!bolt_mode[0] && !bolt_mode[1]) anim_call(gearbox_down, 0);
        } else if (mode == 0) {
            bolt_drop_in(b);
        } else if (c->tune[b] != before) {
            sound_sfx(SFX_TURN);
        }
    }
}

static void picked(Game *g, int r)
{
    Car *c = player_car(&g->player);
    if (r >= 0 && c) install(g, c, r);
}

static void key(Game *g, SDL_Scancode sc, SDL_Keycode k)
{
    (void)k;
    if (anim_busy()) return;
    if (list.active) picked(g, partlist_key(&list, sc));
    else choose(g, dialog_key(&dlg, sc));
}

static void click(Game *g, int x, int y)
{
    if (anim_busy()) return;
    if (list.active) picked(g, partlist_click(&list, x, y));
    else choose(g, dialog_click(&dlg, x, y));
}

static void mouse_move(Game *g, int x, int y)
{
    (void)g;
    if (list.active) partlist_mouse_move(&list, x, y);
    else dialog_mouse_move(&dlg, x, y);
}

static void update(Game *g)
{
    Car *c = player_car(&g->player);
    garage_draw_background(g);
    game_draw_picture(g, TRANS_BACKDROP, 0x20, 10);
    int pic = trans_pic(c);
    const Image *t = pic && !(hidden & HIDE_GEARBOX) ? game_picture(g, pic) : NULL;
    if (t) {
        video_blit(&g->video, t, 200 - t->w, 0x7d - t->h);
        const Image *bolts = game_picture(g, TRANS_BOLTS);
        for (int b = 0; b < 2 && bolts; b++)
            video_blit_part(&g->video, bolts, hidden & HIDE_BOLT(b) ? 0 : c->tune[b] * 16, 0, 16, 6, BOLT_X, bolt_y(b));
    }
    anim_run(g);
    if (anim_busy()) { game_draw_status_bar(g, NULL); return; }
    const char *hover = list.active ? partlist_hover_text(&list)
                      : dlg.hover >= 0 ? dlg.w[dlg.hover].text : NULL;
    game_draw_status_bar(g, hover);
    dialog_draw(&g->video, &dlg);
    if (list.active) partlist_draw(&g->video, &list);
}

const Scene scene_trans = { enter, key, NULL, mouse_move, click, update };
