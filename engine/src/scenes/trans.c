/* Change transmission (1001:75b8, dialog 12): two bolts hold the gearbox (car->tune[0..1]). */
#include "../assets/exe.h"
#include "../car.h"
#include "../data.h"
#include "../game.h"
#include "../ui/partlist.h"

#define TRANS_BACKDROP 1055
#define TRANS_BOLTS    1081     /* 16x6 frames by tightness */

static Dialog dlg;
static PartList list;
static int bolt_mode[2];

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
    update_widgets(c);
}

static void install(Game *g, Car *c, int spare)
{
    Player *p = &g->player;
    PartInst part = p->parts[spare];
    if (!(g_parts[part.part].make & g_models[c->model].spec1 & 0x0f)) {
        p->time_actions += 0x888;
        game_message(g, 24);                        /* "That won't fit!" */
        return;
    }
    player_remove_spare(p, spare);
    c->trans = part;
    c->price = (int16_t)(c->price + part.value);
    p->time_actions += 0x666;
    for (int b = 0; b < 2; b++) { c->tune[b] = 1; bolt_mode[b] = 1; }
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
        if (bolt_turn(&c->tune[id - 1], &bolt_mode[id - 1]) && !bolt_mode[0] && !bolt_mode[1]) {
            player_add_spare(&g->player, c->trans);  /* both bolts out: the gearbox comes down */
            c->price = (int16_t)(c->price - c->trans.value);
            c->trans.part = -1;
            c->tune[0] = c->tune[1] = 0;
            g->player.time_actions += 0x666;
            update_widgets(c);
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
    if (list.active) picked(g, partlist_key(&list, sc));
    else choose(g, dialog_key(&dlg, sc));
}

static void click(Game *g, int x, int y)
{
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
    const Image *t = pic ? game_picture(g, pic) : NULL;
    if (t) {
        video_blit(&g->video, t, 200 - t->w, 0x7d - t->h);
        const Image *bolts = game_picture(g, TRANS_BOLTS);
        for (int b = 0; b < 2 && bolts; b++)
            video_blit_part(&g->video, bolts, c->tune[b] * 16, 0, 16, 6, 0xb8, (int16_t)exe_ds_u16((uint16_t)(0x201a + 2 * b)) + 10);
    }
    const char *hover = list.active ? partlist_hover_text(&list)
                      : dlg.hover >= 0 ? dlg.w[dlg.hover].text : NULL;
    game_draw_status_bar(g, hover);
    dialog_draw(&g->video, &dlg);
    if (list.active) partlist_draw(&g->video, &list);
}

const Scene scene_trans = { enter, key, NULL, mouse_move, click, update };
