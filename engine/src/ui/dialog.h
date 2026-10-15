/* The game's data-driven dialogs: SR.EXE keeps 36 widget lists (DS:3ad8 -> 18-byte records).
 * Layout, text, colours and keyboard shortcuts all come from the original tables. */
#ifndef SR_DIALOG_H
#define SR_DIALOG_H

#include <stdint.h>
#include <SDL3/SDL.h>

#include "../video.h"

enum {
    W_HOTSPOT = 0x000,  /* invisible clickable area over a picture; text = status-line description */
    W_WINDOW  = 0x008,  /* framed box with centred title */
    W_BUTTON  = 0x010,
    W_WINDOW2 = 0x040,
    W_LABEL   = 0x080,
    W_CTEXT   = 0x100,  /* text centred in its rectangle */
    W_FIELD   = 0x200,  /* text entry / value field */
    W_LISTROW = 0x400,
    W_ITEM    = 0x800,  /* list item slot */
};

#define WF_ENABLED 0x01
#define WF_LAST    0x02

#define DIALOG_MAX_WIDGETS 64
#define DIALOG_TEXT_MAX    64

typedef struct {
    uint16_t flags;
    int      type;              /* flags & 0xffd8 */
    const char *text;           /* original text (may be overridden) */
    int16_t  scheme;            /* colour scheme index, <=0 keeps the current one */
    int16_t  id;                /* value returned when chosen */
    int16_t  x1, y1, x2, y2;
    uint16_t keys;              /* two PC scancodes (low = primary), 0xFF = none */
    char     override[DIALOG_TEXT_MAX];
    int      has_override;
    int      no_mark;           /* hover highlight drawn by the scene itself */
    int16_t  mark[4];           /* hover mark rectangle when it differs from the click area */
    int      has_mark;
} Widget;

typedef struct {
    int    index;
    int    count;
    Widget w[DIALOG_MAX_WIDGETS];
    int    hover;               /* widget under the mouse, -1 if none */
    int    status;              /* widget used as the hotspot description line, -1 if none */
} Dialog;

int  dialog_count(void);
int  dialog_load(Dialog *d, int index);
void dialog_set_text(Dialog *d, int widget, const char *text);
const char *dialog_text(const Dialog *d, int widget);
void dialog_set_rect(Dialog *d, int widget, int x1, int y1, int x2, int y2);   /* enables it */
void dialog_set_mark(Dialog *d, int widget, int x1, int y1, int x2, int y2);   /* hover mark only */
int  dialog_find(const Dialog *d, int id);      /* widget index with this id, or -1 */
void dialog_draw(Video *v, Dialog *d);

/* Input: return the chosen widget index, or -1. Hover also updates the status line. */
int  dialog_mouse_move(Dialog *d, int x, int y);
int  dialog_click(Dialog *d, int x, int y);
int  dialog_key(const Dialog *d, SDL_Scancode sc);

/* Modern addition: mark the widget under the mouse (button outline / hotspot corners). */
extern int ui_highlight;
extern int ui_highlight_colour;     /* EGA colour 0..15 (default 14, yellow); to be set from the options menu */
/* Palette register currently showing EGA colour `ega`, or -1. */
int  ui_find_register(const Video *v, int ega);

/* Draw the frame used by the original UI around (x, y, w, h) with the given scheme. */
void ui_frame(Video *v, int x, int y, int w, int h, int scheme);
void ui_set_scheme(int scheme);
void ui_text(Video *v, int x, int y, const char *s);   /* text in the current scheme */

#endif
