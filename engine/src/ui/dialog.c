#include "dialog.h"

#include <string.h>

#include "../assets/exe.h"
#include "font.h"

#define DIALOG_TABLE   0x3ad8   /* DS: near pointers to widget lists */
#define SCHEMES_EGA    0x1d06   /* DS: 5-byte colour schemes (text fg, text bg, fill, border, style) */
#define ARROW_STRINGS  0x20e2   /* DS: scroll-button glyph strings for ids -7..-10 */

static struct { uint8_t fg, bg, fill, border, style; } cur = { 15, 0, 0, 15, 1 };

int ui_highlight = 1;
int ui_highlight_colour = 14;

int ui_find_register(const Video *v, int ega)
{
    for (int r = 15; r >= 0; r--)
        if (v->palette[r] == ega) return r;
    return -1;
}

static int highlight_colour(const Video *v)
{
    int r = ui_find_register(v, ui_highlight_colour);
    return r >= 0 ? r : ui_find_register(v, 15);   /* colour not in this screen's palette: white */
}

static void draw_hover(Video *v, const Dialog *d)
{
    if (!ui_highlight || d->hover < 0) return;
    const Widget *w = &d->w[d->hover];
    int c = highlight_colour(v);
    if (c < 0 || w->no_mark) return;
    if (w->type == W_BUTTON) {
        int x = w->x1 + 1, y = w->y1 + 1, bw = w->x2 - w->x1 - 2, bh = w->y2 - w->y1 - 2;
        video_fill(v, x - 2, y - 2, bw + 4, 1, (uint8_t)c);
        video_fill(v, x - 2, y + bh + 1, bw + 4, 1, (uint8_t)c);
        video_fill(v, x - 2, y - 2, 1, bh + 4, (uint8_t)c);
        video_fill(v, x + bw + 1, y - 2, 1, bh + 4, (uint8_t)c);
    } else if (w->type == W_HOTSPOT) {
        int x1 = w->x1, y1 = w->y1, x2 = w->x2 - 1, y2 = w->y2 - 1;
        int len = (x2 - x1) < 12 || (y2 - y1) < 12 ? 2 : 4;
        uint8_t k = (uint8_t)c;
        video_fill(v, x1, y1, len, 1, k); video_fill(v, x1, y1, 1, len, k);
        video_fill(v, x2 - len + 1, y1, len, 1, k); video_fill(v, x2, y1, 1, len, k);
        video_fill(v, x1, y2, len, 1, k); video_fill(v, x1, y2 - len + 1, 1, len, k);
        video_fill(v, x2 - len + 1, y2, len, 1, k); video_fill(v, x2, y2 - len + 1, 1, len, k);
    }
}

void ui_set_scheme(int scheme)
{
    if (scheme <= 0) return;
    const uint8_t *s = exe_ds((uint16_t)(SCHEMES_EGA + scheme * 5), 5);
    if (!s) return;
    cur.fg = s[0]; cur.bg = s[1]; cur.fill = s[2]; cur.border = s[3]; cur.style = s[4];
}

void ui_text(Video *v, int x, int y, const char *s) { font_draw(v, x, y, s, cur.fg, cur.bg); }

static void fill(Video *v, int x, int y, int w, int h, uint8_t c) { video_fill(v, x, y, w, h, c); }

/* 2a21:3ff6 — 3-pixel band around the box, then 1- or 2-pixel lines in the border colour. */
static void frame(Video *v, int x, int y, int w, int h)
{
    uint8_t band = cur.style == 3 ? cur.border : cur.fill;
    fill(v, x - 3, y - 3, 3, h + 6, band);
    fill(v, x, y + h, w, 3, band);
    fill(v, x + w, y - 3, 3, h + 6, band);
    fill(v, x, y - 3, w, 3, band);
    if (cur.style == 3) return;
    fill(v, x - 2, y - 2, 1, h + 4, cur.border);
    fill(v, x - 2, y + h + 1, w + 4, 1, cur.border);
    fill(v, x + w + 1, y - 2, 1, h + 4, cur.border);
    fill(v, x - 2, y - 2, w + 4, 1, cur.border);
    if (cur.style == 2) {
        fill(v, x - 1, y - 1, 1, h + 2, cur.border);
        fill(v, x - 1, y + h, w + 2, 1, cur.border);
        fill(v, x + w, y - 1, 1, h + 2, cur.border);
        fill(v, x - 1, y - 1, w + 2, 1, cur.border);
    }
}

void ui_frame(Video *v, int x, int y, int w, int h, int scheme)
{
    ui_set_scheme(scheme);
    fill(v, x, y, w, h, cur.fill);
    frame(v, x, y, w, h);
}

int dialog_count(void)
{
    int n = 0;
    while (n < 64) {
        uint16_t p = exe_ds_u16((uint16_t)(DIALOG_TABLE + 2 * n));
        if (p < 0x1000 || p >= 0x8800) break;
        n++;
    }
    return n;
}

int dialog_load(Dialog *d, int index)
{
    memset(d, 0, sizeof *d);
    d->index = index;
    d->hover = -1;
    d->status = -1;
    uint16_t p = exe_ds_u16((uint16_t)(DIALOG_TABLE + 2 * index));
    if (p < 0x1000 || p >= 0x8800) return -1;
    for (int i = 0; i < DIALOG_MAX_WIDGETS; i++, p += 18) {
        const uint8_t *r = exe_ds(p, 18);
        if (!r) return -1;
        Widget *w = &d->w[d->count++];
#define W16(k) ((uint16_t)(r[2 * (k)] | r[2 * (k) + 1] << 8))
        w->flags = W16(0);
        w->type = w->flags & 0xffd8;
        w->text = exe_ds_str(W16(1));
        w->scheme = (int16_t)W16(2);
        w->id = (int16_t)W16(3);
        w->x1 = (int16_t)W16(4); w->y1 = (int16_t)W16(5);
        w->x2 = (int16_t)W16(6); w->y2 = (int16_t)W16(7);
        w->keys = W16(8);
#undef W16
        if (w->type == W_ITEM && w->text[0] == 0) w->text = "";
        if (w->flags & WF_LAST) break;
    }
    /* Picture screens (hotspot dialogs) describe the pointed-at spot in their centred text line. */
    int has_hotspots = 0;
    for (int i = 0; i < d->count; i++) if (d->w[i].type == W_HOTSPOT) has_hotspots = 1;
    for (int i = 0; i < d->count && has_hotspots; i++)
        if (d->w[i].type == W_CTEXT) { d->status = i; dialog_set_text(d, i, ""); break; }
    return 0;
}

void dialog_set_text(Dialog *d, int i, const char *text)
{
    if (i < 0 || i >= d->count) return;
    SDL_strlcpy(d->w[i].override, text, DIALOG_TEXT_MAX);
    d->w[i].has_override = 1;
}

const char *dialog_text(const Dialog *d, int i)
{
    if (i < 0 || i >= d->count) return "";
    return d->w[i].has_override ? d->w[i].override : d->w[i].text;
}

void dialog_set_rect(Dialog *d, int i, int x1, int y1, int x2, int y2)
{
    if (i < 0 || i >= d->count) return;
    Widget *w = &d->w[i];
    w->x1 = (int16_t)x1; w->y1 = (int16_t)y1; w->x2 = (int16_t)x2; w->y2 = (int16_t)y2;
    w->flags |= WF_ENABLED;
}

/* Port of 2a21:44a8. */
static void draw_widget(Video *v, Dialog *d, int i, int *in_field)
{
    Widget *w = &d->w[i];
    const char *text = dialog_text(d, i);
    ui_set_scheme(w->scheme);
    switch (w->type) {
    case W_LABEL:
        ui_text(v, w->x1, w->y1, text);
        break;
    case W_BUTTON: {
        int x = w->x1 + 1, y = w->y1 + 1;
        int bw = w->x2 - w->x1 - 2, bh = w->y2 - w->y1 - 2;
        fill(v, x, y, bw, bh, cur.fill);
        frame(v, x, y, bw, bh);
        if (w->id >= -10 && w->id <= -7 && !w->has_override)
            text = exe_ds_str((uint16_t)(ARROW_STRINGS + 2 * (-7 - w->id)));
        int ty = ((w->y2 - y - 9) >> 1) + y;
        ui_text(v, ((bw - font_text_width(text)) >> 1) + x, ty, text);
        break;
    }
    case W_WINDOW:
    case W_WINDOW2: {
        int ww = w->x2 - w->x1, wh = w->y2 - w->y1;
        fill(v, w->x1, w->y1, ww, wh, cur.fill);
        frame(v, w->x1, w->y1, ww, wh);
        if (text[0]) {
            ui_text(v, ((ww - font_text_width(text)) >> 1) + w->x1, w->y1, text);
            fill(v, w->x1, w->y1 + 9, ww, 1, cur.border);
        }
        break;
    }
    case W_CTEXT: {
        int ty = ((w->y2 - w->y1 - 8) >> 1) + w->y1;
        if (i == d->status) fill(v, w->x1, w->y1, w->x2 - w->x1, w->y2 - w->y1, cur.bg);
        ui_text(v, ((w->x2 - w->x1 - font_text_width(text)) >> 1) + w->x1, ty, text);
        break;
    }
    case W_FIELD:
        *in_field = 1;
        if (w->has_override) {
            fill(v, w->x1, w->y1, w->x2 - w->x1, 8, cur.bg);
            ui_text(v, w->x1, w->y1, text);
        }
        break;
    case W_LISTROW:
        if (!*in_field) {
            fill(v, w->x1, w->y1, w->x2 - w->x1, 8, cur.fill);
            ui_text(v, ((w->x2 - w->x1 - font_text_width(text)) >> 1) + w->x1, w->y1, text);
        }
        break;
    case W_ITEM:
        if (w->has_override && text[0]) ui_text(v, w->x1 + 2, w->y1, text);
        break;
    default:
        break;
    }
}

void dialog_draw(Video *v, Dialog *d)
{
    int in_field = 0;
    for (int i = 0; i < d->count; i++) draw_widget(v, d, i, &in_field);
    draw_hover(v, d);
}

static int clickable(const Widget *w)
{
    return (w->flags & WF_ENABLED) && (w->type == W_HOTSPOT || w->type == W_BUTTON || w->type == W_ITEM) &&
           (w->x2 > w->x1 || w->y2 > w->y1);
}

static int hit(const Dialog *d, int x, int y)
{
    for (int i = d->count - 1; i >= 0; i--) {
        const Widget *w = &d->w[i];
        if (clickable(w) && x >= w->x1 && x < w->x2 && y >= w->y1 && y < w->y2) return i;
    }
    return -1;
}

int dialog_mouse_move(Dialog *d, int x, int y)
{
    int h = hit(d, x, y);
    if (h != d->hover) {
        d->hover = h;
        if (d->status >= 0)
            dialog_set_text(d, d->status, (h >= 0 && d->w[h].type == W_HOTSPOT) ? d->w[h].text : "");
    }
    return h;
}

int dialog_click(Dialog *d, int x, int y) { return hit(d, x, y); }

/* PC (set 1) scancodes used by the shortcut table. */
static SDL_Scancode pc_scancode(int pc)
{
    static const SDL_Scancode row1[] = { SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R, SDL_SCANCODE_T,
                                         SDL_SCANCODE_Y, SDL_SCANCODE_U, SDL_SCANCODE_I, SDL_SCANCODE_O, SDL_SCANCODE_P };
    static const SDL_Scancode row2[] = { SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F, SDL_SCANCODE_G,
                                         SDL_SCANCODE_H, SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L };
    static const SDL_Scancode row3[] = { SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_B,
                                         SDL_SCANCODE_N, SDL_SCANCODE_M };
    if (pc == 0x01) return SDL_SCANCODE_ESCAPE;
    if (pc == 0x1C) return SDL_SCANCODE_RETURN;
    if (pc == 0x39) return SDL_SCANCODE_SPACE;
    if (pc >= 0x02 && pc <= 0x0A) return (SDL_Scancode)(SDL_SCANCODE_1 + (pc - 0x02));
    if (pc == 0x0B) return SDL_SCANCODE_0;
    if (pc >= 0x10 && pc <= 0x19) return row1[pc - 0x10];
    if (pc >= 0x1E && pc <= 0x26) return row2[pc - 0x1E];
    if (pc >= 0x2C && pc <= 0x32) return row3[pc - 0x2C];
    return SDL_SCANCODE_UNKNOWN;
}

int dialog_key(const Dialog *d, SDL_Scancode sc)
{
    for (int i = 0; i < d->count; i++) {
        const Widget *w = &d->w[i];
        if (!clickable(w) || w->keys == 0xFFFF) continue;
        int k1 = w->keys & 0xFF, k2 = w->keys >> 8;
        if ((k1 != 0xFF && pc_scancode(k1) == sc) || (k2 != 0xFF && k2 && pc_scancode(k2) == sc)) return i;
    }
    return -1;
}
