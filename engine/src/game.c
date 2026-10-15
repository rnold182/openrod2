#include "game.h"

#include "assets/exe.h"
#include "ui/font.h"

#define MSG_TABLE    0x11ea     /* DS: 6 bytes per message: layout, line 1, line 2 */
#define MSG_LAYOUTS  0x1e46     /* DS: 18 bytes: picture, lines, x, y, w, h, text dx, text dy, - */
#define MSG_SCHEME   14
#define MSG_TIMEOUT  0x1f4a     /* DS: ticks a message stays up (350) */

void game_set_scene(Game *g, const Scene *s) { g->next_scene = s; }

const Image *game_picture(Game *g, int id)
{
    if (id >= 2000) return lib_image(&g->lib[2], id - 2000);
    if (id >= 1000) return lib_image(&g->lib[1], id - 1000);
    return lib_image(&g->lib[0], id - 1);
}

void game_draw_picture(Game *g, int id, int x, int y) { video_blit(&g->video, game_picture(g, id), x, y); }

void game_set_palette(Game *g, uint16_t ds_table)
{
    const uint8_t *p = exe_ds(ds_table, 16);
    if (p) video_set_palette(&g->video, p);
}

double game_ticks_since(Uint64 ms) { return (SDL_GetTicks() - ms) * TICK_HZ / 1000.0; }

void game_message(Game *g, int n)
{
    g->msg = n;
    g->msg_start = SDL_GetTicks();
}

static int16_t layout_word(int layout, int k) { return (int16_t)exe_ds_u16((uint16_t)(MSG_LAYOUTS + layout * 18 + k * 2)); }

void game_draw_message(Game *g)
{
    if (!g->msg) return;
    if (game_ticks_since(g->msg_start) >= (int16_t)exe_ds_u16(MSG_TIMEOUT)) {
        g->msg = 0;
        return;
    }
    int n = g->msg < 0 ? -g->msg : g->msg;
    int layout = exe_ds_u16((uint16_t)(MSG_TABLE + n * 6));
    int pic = layout_word(layout, 0), lines = layout_word(layout, 1);
    int x = layout_word(layout, 2), y = layout_word(layout, 3), w = layout_word(layout, 4);
    int dx = layout_word(layout, 6), dy = layout_word(layout, 7);
    game_draw_picture(g, pic, x, y);
    ui_set_scheme(MSG_SCHEME);
    for (int i = 0; i < lines && i < 2; i++) {
        const char *s = exe_ds_str(exe_ds_u16((uint16_t)(MSG_TABLE + n * 6 + 2 + 2 * i)));
        ui_text(&g->video, ((w - font_text_width(s)) >> 1) + x + dx, y + dy + 9 * i, s);
    }
}
