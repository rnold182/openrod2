/* The "Spare parts" chooser (dialog 7, SR.EXE 2a21:55ea with lists built by 1001:70d2). */
#ifndef SR_PARTLIST_H
#define SR_PARTLIST_H

#include "../car.h"
#include "dialog.h"

enum { PL_NONE = -1, PL_CANCEL = -2 };

typedef struct {
    Dialog dlg;
    int    active;
    int    count, sel, top;
    int    spare[MAX_PARTS + 1];    /* index into Player.parts, -1 for the extra entry */
    char   text[MAX_PARTS + 1][64];
} PartList;

/* Lists the player's spares of `type`; `extra` (or NULL) is an additional first entry such as
 * "Remove current muffler". Returns the number of entries (0 = nothing to show, list not opened). */
int  partlist_open(PartList *pl, const Player *p, int type, const char *extra);
/* Input: returns PL_NONE, PL_CANCEL, or the chosen entry's spare index (-1 = the extra entry is
 * reported as PL_EXTRA). */
#define PL_EXTRA (-3)
int  partlist_click(PartList *pl, int x, int y);
int  partlist_key(PartList *pl, SDL_Scancode sc);
void partlist_mouse_move(PartList *pl, int x, int y);
void partlist_draw(Video *v, PartList *pl);
const char *partlist_hover_text(const PartList *pl);

#endif
