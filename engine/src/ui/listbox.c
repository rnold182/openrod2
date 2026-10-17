#include "listbox.h"

/* The list's rows: its W_ITEM widgets (dialog 16 has 9, the others 10). */
static int rows(const ListBox *lb)
{
    int n = 0;
    for (int i = 0; i < lb->dlg.count; i++) if (lb->dlg.w[i].type == W_ITEM && lb->dlg.w[i].id >= 1) n++;
    return n ? n : 10;
}

void listbox_open(ListBox *lb, int dialog)
{
    dialog_load(&lb->dlg, dialog);
    lb->count = lb->sel = lb->top = 0;
    lb->active = 1;
}

static int choose(ListBox *lb, int i)
{
    if (i < 0) return 0;
    const Widget *w = &lb->dlg.w[i];
    int id = w->id;
    if (w->type == W_ITEM && id >= 1) {
        if (lb->top + id - 1 < lb->count) lb->sel = lb->top + id - 1;
    } else if (id == -7) {                             /* 2a21:4ebc: the up arrow, one back */
        if (lb->top > 0) lb->top--;
    } else if (id == -10) {                            /* the down arrow, one on */
        if (lb->top + 1 < lb->count) lb->top++;
    } else if (id == -9) {
        lb->top += rows(lb);
        if (lb->top > lb->count - 1) lb->top = lb->count - 1;
        if (lb->top < 0) lb->top = 0;
    } else if (id == -8) {
        lb->top -= rows(lb);
        if (lb->top < 0) lb->top = 0;
    } else if (id < 0) {
        lb->active = 0;
        return id;
    }
    return 0;
}

int listbox_click(ListBox *lb, int x, int y) { return choose(lb, dialog_click(&lb->dlg, x, y)); }
int listbox_key(ListBox *lb, SDL_Scancode sc) { return choose(lb, dialog_key(&lb->dlg, sc)); }
void listbox_mouse_move(ListBox *lb, int x, int y) { dialog_mouse_move(&lb->dlg, x, y); }

void listbox_set_button_text(ListBox *lb, int id, const char *text)
{
    for (int i = 0; i < lb->dlg.count; i++)
        if (lb->dlg.w[i].id == id && lb->dlg.w[i].type == W_BUTTON) dialog_set_text(&lb->dlg, i, text);
}

const char *listbox_hover_text(const ListBox *lb)
{
    int h = lb->dlg.hover;
    return h >= 0 && lb->dlg.w[h].type == W_BUTTON ? dialog_text(&lb->dlg, h) : NULL;
}

void listbox_draw(Video *v, ListBox *lb)
{
    for (int i = 0; i < lb->dlg.count; i++) {
        Widget *w = &lb->dlg.w[i];
        if (w->type == W_ITEM && w->id >= 1) {
            int k = lb->top + w->id - 1;
            dialog_set_text(&lb->dlg, i, k < lb->count ? lb->text[k] : "");
        } else if (w->type == W_LISTROW) {
            dialog_set_text(&lb->dlg, i, lb->count ? lb->text[lb->sel] : "");
        }
    }
    dialog_draw(v, &lb->dlg);
}
