#include "partlist.h"

#include "../data.h"

int partlist_open(PartList *pl, const Player *p, int type, const char *extra)
{
    int n = 0;
    if (extra) {
        pl->spare[n] = -1;
        SDL_strlcpy(pl->lb.text[n++], extra, sizeof pl->lb.text[0]);
    }
    for (int i = 0; i < p->part_count && n < LISTBOX_MAX; i++) {
        const PartInst *s = &p->parts[i];
        if (type >= 0 && g_parts[s->part].type != type) continue;
        char wear[16];
        part_wear_text(s, wear);
        SDL_snprintf(pl->lb.text[n], sizeof pl->lb.text[0], "%s %s", g_parts[s->part].ad, wear);
        pl->spare[n++] = i;
    }
    pl->active = 0;
    if (!n) return 0;
    listbox_open(&pl->lb, 7);
    pl->lb.count = n;
    pl->active = 1;
    return n;
}

static int result(PartList *pl, int id)
{
    pl->active = pl->lb.active;
    if (id == -3) return pl->spare[pl->lb.sel] < 0 ? PL_EXTRA : pl->spare[pl->lb.sel];   /* Change / Sell it */
    if (id == -4) return PL_CANCEL;                                                       /* Forget it */
    return PL_NONE;
}

int partlist_click(PartList *pl, int x, int y) { return result(pl, listbox_click(&pl->lb, x, y)); }
int partlist_key(PartList *pl, SDL_Scancode sc) { return result(pl, listbox_key(&pl->lb, sc)); }
void partlist_mouse_move(PartList *pl, int x, int y) { listbox_mouse_move(&pl->lb, x, y); }
const char *partlist_hover_text(const PartList *pl) { return listbox_hover_text(&pl->lb); }
void partlist_draw(Video *v, PartList *pl) { listbox_draw(v, &pl->lb); }
