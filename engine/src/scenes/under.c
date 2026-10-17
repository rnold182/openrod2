/* Under the car (2925:3174, dialog 34): exhaust manifold, muffler and differential. */
#include "../assets/exe.h"
#include "../car.h"
#include "../data.h"
#include "../game.h"
#include "../ui/partlist.h"

#define UNDERSIDE 1199

static Dialog dlg;
static PartList list;
static int slot;                /* PT_EXHAUST, PT_MUFFLER or PT_DIFF being changed */
/* 2925:2b26: first you slide under the car on the creeper, from (0x50, 0xac) 2 pixels right
 * and 1 up every 2 retraces until x reaches 0x80 (y stops at 0x8f). */
static int slide_x, slide_y;
static Uint64 slide_start;

static PartInst *slot_part(Car *c, int type)
{
    return type == PT_EXHAUST ? &c->exhaust : type == PT_MUFFLER ? &c->muffler : &c->diff;
}

static void enter(Game *g)
{
    (void)g;
    dialog_load(&dlg, 34);
    list.active = 0;
    slide_x = 0x50;
    slide_y = 0xac;
    slide_start = SDL_GetTicks();
}

static void choose(Game *g, int i)
{
    Car *c = player_car(&g->player);
    if (i < 0 || !c) return;
    int id = dlg.w[i].id;
    if (id == -1) { game_set_scene(g, &scene_garage); return; }
    slot = id == -2 ? PT_EXHAUST : id == -3 ? PT_MUFFLER : id == -4 ? PT_DIFF : -1;
    if (slot < 0) return;
    const char *extra = slot == PT_MUFFLER && c->muffler.part >= 0 ? exe_ds_str(0x8238) : NULL;  /* "Remove current muffler" */
    if (!partlist_open(&list, &g->player, slot, extra))
        game_message(g, slot == PT_EXHAUST ? 116 : slot == PT_MUFFLER ? 117 : 118);
}

static void picked(Game *g, int r)
{
    Car *c = player_car(&g->player);
    if (!c || r == PL_NONE || r == PL_CANCEL) return;
    Player *p = &g->player;
    PartInst *cur = slot_part(c, slot);
    if (r == PL_EXTRA) {                            /* drive without a muffler */
        player_add_spare(p, *cur);
        c->price = (int16_t)(c->price - cur->value);
        cur->part = -1;
        return;
    }
    PartInst part = p->parts[r];
    if (!(g_parts[part.part].make & g_models[c->model].spec1 & 0x0f)) { game_message(g, 24); return; }
    player_remove_spare(p, r);
    if (cur->part >= 0) {
        player_add_spare(p, *cur);
        c->price = (int16_t)(c->price - cur->value);
    }
    *cur = part;
    c->price = (int16_t)(c->price + part.value);
}

static void key(Game *g, SDL_Scancode sc, SDL_Keycode k)
{
    (void)k;
    if (slide_x < 0x80) return;
    if (list.active) picked(g, partlist_key(&list, sc));
    else choose(g, dialog_key(&dlg, sc));
}

static void click(Game *g, int x, int y)
{
    if (slide_x < 0x80) return;
    if (list.active) picked(g, partlist_click(&list, x, y));
    else choose(g, dialog_click(&dlg, x, y));
}

static void mouse_move(Game *g, int x, int y)
{
    (void)g;
    if (list.active) partlist_mouse_move(&list, x, y);
    else dialog_mouse_move(&dlg, x, y);
}

/* 2925:2e60: underside picture plus each fitted part (pictures by grade from DS:822a). */
static void update(Game *g)
{
    Car *c = player_car(&g->player);
    if (slide_x < 0x80) {
        int steps = (int)((SDL_GetTicks() - slide_start) * 70 / 2000);   /* 18e2(2) a step */
        slide_x = SDL_min(0x50 + 2 * steps, 0x80);    /* drawn up to x 0x7e, then the underside */
        slide_y = SDL_max(0xac - steps, 0x8f);
        if (slide_x >= 0x80) return;
        garage_draw_scene(g, slide_x, slide_y);
        game_draw_status_bar(g, NULL);
        return;
    }
    garage_draw_background(g);
    game_draw_picture(g, UNDERSIDE, 0x18, 0x15);
    static const struct { int type; uint16_t table; int x, y; } spots[3] = {
        { PT_EXHAUST, 0x822a, 200, 0x50 }, { PT_MUFFLER, 0x822e, 0xa8, 0x5c }, { PT_DIFF, 0x8232, 0x50, 0x3f },
    };
    for (int k = 0; k < 3 && c; k++) {
        PartInst *pi = slot_part(c, spots[k].type);
        if (pi->part < 0) continue;
        game_draw_picture(g, (int16_t)exe_ds_u16((uint16_t)(spots[k].table + 2 * g_parts[pi->part].grade)), spots[k].x, spots[k].y);
    }
    const char *hover = list.active ? partlist_hover_text(&list)
                      : dlg.hover >= 0 ? dlg.w[dlg.hover].text : NULL;
    game_draw_status_bar(g, hover);
    dialog_draw(&g->video, &dlg);
    if (list.active) partlist_draw(&g->video, &list);
}

const Scene scene_under = { enter, key, NULL, mouse_move, click, update };
