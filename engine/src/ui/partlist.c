#include "partlist.h"

#include <string.h>

#include "../data.h"

#define ROWS 10

int partlist_open(PartList *pl, const Player *p, int type, const char *extra)
{
    pl->count = 0;
    if (extra) {
        pl->spare[0] = -1;
        SDL_strlcpy(pl->text[0], extra, sizeof pl->text[0]);
        pl->count = 1;
    }
    for (int i = 0; i < p->part_count; i++) {
        const PartInst *s = &p->parts[i];
        if (g_parts[s->part].type != type) continue;
        char wear[16];
        if (s->wear == -128) SDL_strlcpy(wear, "(`new`)", sizeof wear);
        else SDL_snprintf(wear, sizeof wear, "(%d%% worn)", s->wear / 100);
        SDL_snprintf(pl->text[pl->count], sizeof pl->text[0], "%s %s", g_parts[s->part].ad, wear);
        pl->spare[pl->count++] = i;
    }
    if (!pl->count) return 0;
    dialog_load(&pl->dlg, 7);
    pl->sel = 0;
    pl->top = 0;
    pl->active = 1;
    return pl->count;
}

static int choose(PartList *pl, int i)
{
    if (i < 0) return PL_NONE;
    const Widget *w = &pl->dlg.w[i];
    int id = w->id;
    if (w->type == W_ITEM && id >= 1) {
        if (pl->top + id - 1 < pl->count) pl->sel = pl->top + id - 1;
    } else if (id == -3) {                      /* Change */
        pl->active = 0;
        return pl->spare[pl->sel] < 0 ? PL_EXTRA : pl->spare[pl->sel];
    } else if (id == -4) {                      /* Forget it */
        pl->active = 0;
        return PL_CANCEL;
    } else if (id == -7 && pl->top + ROWS < pl->count) {
        pl->top++;
    } else if (id == -10 && pl->top > 0) {
        pl->top--;
    } else if (id == -9) {
        pl->top += ROWS;
        if (pl->top > pl->count - 1) pl->top = pl->count - 1;
    } else if (id == -8) {
        pl->top -= ROWS;
        if (pl->top < 0) pl->top = 0;
    }
    return PL_NONE;
}

int partlist_click(PartList *pl, int x, int y) { return choose(pl, dialog_click(&pl->dlg, x, y)); }
int partlist_key(PartList *pl, SDL_Scancode sc) { return choose(pl, dialog_key(&pl->dlg, sc)); }
void partlist_mouse_move(PartList *pl, int x, int y) { dialog_mouse_move(&pl->dlg, x, y); }

const char *partlist_hover_text(const PartList *pl)
{
    int h = pl->dlg.hover;
    return h >= 0 && pl->dlg.w[h].type == W_BUTTON ? pl->dlg.w[h].text : NULL;
}

void partlist_draw(Video *v, PartList *pl)
{
    for (int i = 0; i < pl->dlg.count; i++) {
        Widget *w = &pl->dlg.w[i];
        if (w->type == W_ITEM && w->id >= 1) {
            int k = pl->top + w->id - 1;
            dialog_set_text(&pl->dlg, i, k < pl->count ? pl->text[k] : "");
        } else if (w->type == W_LISTROW) {
            dialog_set_text(&pl->dlg, i, pl->text[pl->sel]);
        }
    }
    dialog_draw(v, &pl->dlg);
}
