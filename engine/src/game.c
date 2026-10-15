#include <string.h>
#include "game.h"

#include "assets/exe.h"
#include "data.h"
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

void game_set_paint(Game *g, int colour)
{
    if (colour < 0) return;
    g->video.palette[6] = colour_paint_reg(colour);
    g->video.palette[7] = colour_paint_light(colour);
}

double game_ticks_since(Uint64 ms) { return (SDL_GetTicks() - ms) * TICK_HZ / 1000.0; }

static void show(Game *g, int n, const char *line1)
{
    g->msg = n;
    g->msg_start = SDL_GetTicks();
    SDL_strlcpy(g->msg_line1, line1, sizeof g->msg_line1);
    g->msg_sticky = 0;
}

void game_message(Game *g, int n)
{
    if (g->msg && !g->msg_sticky) {             /* one is up: show this one after it */
        if (g->msg_queued < 4) {
            g->msg_queue[g->msg_queued] = n;
            g->msg_queue_line1[g->msg_queued][0] = 0;
            g->msg_queued++;
        }
        return;
    }
    show(g, n, "");
}

void game_message_dismiss(Game *g)
{
    g->msg = 0;
    g->msg_sticky = 0;
    if (!g->msg_queued) return;
    show(g, g->msg_queue[0], g->msg_queue_line1[0]);
    g->msg_queued--;
    memmove(&g->msg_queue[0], &g->msg_queue[1], sizeof g->msg_queue[0] * (size_t)g->msg_queued);
    memmove(&g->msg_queue_line1[0], &g->msg_queue_line1[1], sizeof g->msg_queue_line1[0] * (size_t)g->msg_queued);
}

void game_message_clear(Game *g) { g->msg = 0; g->msg_sticky = 0; g->msg_queued = 0; }

void game_message_amount(Game *g, int n, long amount, int sticky)
{
    show(g, n, "");
    g->msg_sticky = sticky;
    const char *s = exe_ds_str(exe_ds_u16((uint16_t)(MSG_TABLE + n * 6 + 2)));
    const char *dollar = SDL_strchr(s, '$');
    if (!dollar) return;
    const char *rest = dollar + 1;
    while (*rest >= '0' && *rest <= '9') rest++;
    SDL_snprintf(g->msg_line1, sizeof g->msg_line1, "%.*s%ld%s", (int)(dollar - s + 1), s, amount, rest);
}

void game_message_short(Game *g, long amount)
{
    show(g, 1, "");
    const char *s = exe_ds_str(exe_ds_u16(MSG_TABLE + 1 * 6 + 2));
    const char *dollar = SDL_strchr(s, '$');
    if (!dollar) return;
    SDL_snprintf(g->msg_line1, sizeof g->msg_line1, "%.*s%ld short.", (int)(dollar - s + 1), s, amount);
}

static int16_t layout_word(int layout, int k) { return (int16_t)exe_ds_u16((uint16_t)(MSG_LAYOUTS + layout * 18 + k * 2)); }

void game_draw_message(Game *g)
{
    if (!g->msg) return;
    if (!g->msg_sticky && game_ticks_since(g->msg_start) >= (int16_t)exe_ds_u16(MSG_TIMEOUT)) {
        game_message_dismiss(g);
        if (!g->msg) return;
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
        if (i == 0 && g->msg_line1[0]) s = g->msg_line1;
        ui_text(&g->video, ((w - font_text_width(s)) >> 1) + x + dx, y + dy + 9 * i, s);
    }
}

#define STATUS_Y       190
#define STATUS_SCHEME  11
#define BANKROLL_LABEL 0x4844   /* DS: pointer to "`Bankroll:`$" */

void game_draw_status_bar(Game *g, const char *text)
{
    Video *v = &g->video;
    const uint8_t *sc = exe_ds((uint16_t)(0x1d06 + STATUS_SCHEME * 5), 5);
    uint8_t fg = sc ? sc[0] : 15, bg = sc ? sc[1] : 0, fill = sc ? sc[2] : 0;
    video_fill(v, 0, STATUS_Y, 320, 10, fill);
    int x = font_draw(v, 0, STATUS_Y + 1, exe_ds_str(exe_ds_u16(BANKROLL_LABEL)), fg, bg);
    x = (x + 7) & ~7;
    char money[16];
    SDL_snprintf(money, sizeof money, "%ld", g->player.money);
    int end = font_draw(v, x, STATUS_Y + 1, money, fg, bg);
    if (text && *text) {
        int room = 320 - end;
        int tx = ((room - font_text_width(text)) / 2 + end) & ~7;
        font_draw(v, tx, STATUS_Y + 1, text, fg, bg);
    }
}
