/* The "Spare parts" chooser (dialog 7, SR.EXE 2a21:55ea with lists built by 1001:70d2). */
#ifndef SR_PARTLIST_H
#define SR_PARTLIST_H

#include "../car.h"
#include "listbox.h"

enum { PL_NONE = -1, PL_CANCEL = -2, PL_EXTRA = -3 };

typedef struct {
    ListBox lb;
    int     active;
    int     spare[LISTBOX_MAX];     /* index into Player.parts, -1 for the extra entry */
} PartList;

/* Lists the player's spares of `type` (-1 = all types); `extra` (or NULL) is an additional first
 * entry such as "Remove current muffler". Returns the number of entries (0 = list not opened). */
int  partlist_open(PartList *pl, const Player *p, int type, const char *extra);
/* Input: returns PL_NONE, PL_CANCEL, PL_EXTRA, or the chosen spare's index. */
int  partlist_click(PartList *pl, int x, int y);
int  partlist_key(PartList *pl, SDL_Scancode sc);
void partlist_mouse_move(PartList *pl, int x, int y);
void partlist_draw(Video *v, PartList *pl);
const char *partlist_hover_text(const PartList *pl);

#endif
