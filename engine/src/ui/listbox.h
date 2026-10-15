/* Scrolling list dialogs (SR.EXE 2a21:55ea): dialogs 7 (spare parts), 9 (your cars), 16 (looking for).
 * Items are W_ITEM widgets with ids 1..10, the W_LISTROW shows the selection, ids -7..-10 scroll. */
#ifndef SR_LISTBOX_H
#define SR_LISTBOX_H

#include "dialog.h"

#define LISTBOX_MAX 192

typedef struct {
    Dialog dlg;
    int    active;
    int    count, sel, top;
    char   text[LISTBOX_MAX][64];
} ListBox;

void listbox_open(ListBox *lb, int dialog);     /* then fill text[] and count */
/* Returns the id of a pressed button (closing the list), or 0 if nothing was chosen. */
int  listbox_click(ListBox *lb, int x, int y);
int  listbox_key(ListBox *lb, SDL_Scancode sc);
void listbox_mouse_move(ListBox *lb, int x, int y);
void listbox_draw(Video *v, ListBox *lb);
void listbox_set_button_text(ListBox *lb, int id, const char *text);
const char *listbox_hover_text(const ListBox *lb);

#endif
