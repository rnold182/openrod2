/* The LA News: front page (1001:3e30, dialog 3) and the classified ads (1001:11a2 / 0ac8, dialogs 4-6). */
#include <string.h>

#include "../assets/exe.h"
#include "../car.h"
#include "../data.h"
#include "../game.h"
#include "../ui/font.h"

#define AD_SCHEME      13
#define SEE_IT_SCHEME  11
#define PAGE_BOTTOM    0xbd
#define AD_X           0x44

enum { MODE_FRONT, MODE_ADS, MODE_BUY, MODE_SEE_IT };

/* One classified entry. kind: header, car ad slot (0..41) or catalogue part. */
typedef struct { int kind; int slot; int part; int lines; const char *text; long price; } Entry;
enum { E_HEADER, E_CAR, E_PART };

static Dialog front, page, buy;
static int mode;
static Entry entries[26 + 2 + PART_COUNT + 1];   /* 1-based like the original list */
static int entry_count, first_part;
static int page_start, page_next;
static int chosen;                               /* entry being bought */

static int week_of(const Player *p) { int w = player_day(p) / 7; return w > 11 ? 11 : w; }

/* 1001:0f7e: pick 15..25 car ads for a new issue from 42 slots with cooldowns. */
static void new_issue(Player *p)
{
    memset(p->ads, 0xFF, sizeof p->ads);
    int n = 0;
    int want = (int)SDL_rand(11) + 15;
    if (p->ads_available <= want) {
        want -= p->ads_available;
        p->ads_available = 0;
        for (int i = 0; i < 42; i++) {
            if (p->ad_cooldown[i] == 0) {
                p->ad_cooldown[i]++;
                p->ads[n++] = (uint8_t)i;
            } else if (--p->ad_cooldown[i] == 0) {
                p->ads_available++;
            }
        }
    }
    for (; want > 0 && p->ads_available > 0 && n < 25; want--) {
        int k = (int)SDL_rand(p->ads_available), i = 0, seen = -1;
        for (; i < 42; i++) if (p->ad_cooldown[i] == 0 && ++seen == k) break;
        p->ad_cooldown[i]++;
        p->ads[n++] = (uint8_t)i;
        p->ads_available--;
    }
}

static void build_entries(Player *p)
{
    entry_count = 0;
    Entry *e = &entries[++entry_count];
    *e = (Entry){ E_HEADER, 0, 0, 3, NULL, -1 };
    for (int i = 0; i < 26 && p->ads[i] != 0xFF; i++) {
        int slot = p->ads[i] & 0x7f, model = slot > 20 ? slot - 21 : slot;
        e = &entries[++entry_count];
        *e = (Entry){ E_CAR, i, 0, g_models[model].ad_lines, g_models[model].ad,
                      slot > 20 ? g_models[model].used_price : g_models[model].price };
    }
    e = &entries[++entry_count];
    *e = (Entry){ E_HEADER, 1, 0, 3, NULL, -1 };
    first_part = entry_count + 1;
    /* Same section order as 1001:107c / 10b6 / 10f2 / 112e. */
    static const int ranges[][2] = { { 0, 8 }, { 9, 20 }, { 24, 49 }, { 21, 23 }, { 50, 63 } };
    for (int r = 0; r < 5; r++)
        for (int k = ranges[r][0]; k <= ranges[r][1]; k++) {
            e = &entries[++entry_count];
            *e = (Entry){ E_PART, 0, k, g_parts[k].ad_lines, g_parts[k].ad, -1 };
        }
}

static const char *header_line(int which, int line)
{
    static const uint16_t base[2] = { 0x1e80, 0x1ea4 };   /* " " / "--- USED CARS ---" / " " */
    const char *s = exe_ds_str(base[which]);
    for (int i = 0; i < line; i++) s = ad_next_string(s);
    return s;
}

static uint8_t scheme_byte(int scheme, int k)
{
    const uint8_t *s = exe_ds((uint16_t)(0x1d06 + scheme * 5), 5);
    return s ? s[k] : 0;
}

static int entry_height(int c) { return entries[c].lines * 9 + 3; }

/* Layout of one page, ported from 1001:0ac8. Fills the list widgets of dialog 4.
 * Change from the original: the next page starts at the first entry that was not fully visible
 * (the original always repeated the last entry of the previous page at the top). */
static void layout_page(Video *v, int draw)
{
    uint8_t rule = scheme_byte(AD_SCHEME, 3);
    uint8_t fg = scheme_byte(AD_SCHEME, 0), bg = scheme_byte(AD_SCHEME, 1);
    int hl = ui_highlight ? ui_find_register(v, 7) : -1;     /* light grey behind the pointed-at ad */
    int y = 0, c = page_start, w = 1, last_full = -1;
    for (int i = 1; i <= 24; i++) page.w[i].flags &= (uint16_t)~WF_ENABLED;
    for (;;) {
        if (draw) video_fill(v, AD_X, y + 1, 0xb8, 1, rule);
        if (y + 3 > PAGE_BOTTOM) break;
        Entry *e = &entries[c];
        Widget *wd = w <= 24 ? &page.w[w] : NULL;
        int y1 = y + 1;
        if (wd) {
            wd->flags |= WF_ENABLED;
            wd->y1 = (int16_t)y1;
            wd->id = e->kind == E_HEADER ? 0 : (int16_t)c;
        }
        y += 3;
        char lines[4][64];
        int n;
        if (e->kind == E_HEADER) {
            for (n = 0; n < 3; n++) SDL_strlcpy(lines[n], header_line(e->slot, n), 64);
        } else {
            n = ad_format_lines(e->text, e->lines, e->price, lines, 4);
        }
        int lit = draw && hl >= 0 && wd && page.hover == w && wd->id > 0;
        if (lit) video_fill(v, AD_X, y1 + 1, 0xb8, n * 9 + 1, (uint8_t)hl);
        int drawn = 0;
        for (int l = 0; l < n; l++) {
            if (draw) font_draw(v, AD_X, y, lines[l], fg, lit ? (uint8_t)hl : bg);
            y += 9;
            drawn++;
            if (y > PAGE_BOTTOM) break;
        }
        if (drawn == n && y <= PAGE_BOTTOM + 1) last_full = c;
        if (wd) wd->y2 = (int16_t)(y + 1 > PAGE_BOTTOM ? PAGE_BOTTOM : y + 1);
        w++;
        c = c >= entry_count ? 1 : c + 1;
        if (y >= PAGE_BOTTOM) break;
    }
    if (last_full < 0) page_next = c;
    else page_next = last_full >= entry_count ? 1 : last_full + 1;
    if (draw && y + 1 < PAGE_BOTTOM) video_fill(v, AD_X, y + 1, 0xb8, 1, rule);
}

/* Fill the previous page so that it ends right before the current first entry. */
static void prev_page(void)
{
    int c = page_start - 1;
    if (c < 1) c = entry_count;
    int used = entry_height(c);
    for (;;) {
        int prev = c - 1 < 1 ? entry_count : c - 1;
        if (prev == page_start || used + entry_height(prev) > PAGE_BOTTOM + 1) break;
        used += entry_height(prev);
        c = prev;
    }
    page_start = c;
}

static void open_buy(Game *g, int idx)
{
    Entry *e = &entries[idx];
    if (e->kind == E_CAR && (g->player.ads[e->slot] & 0x80)) {
        game_message(g, 119);           /* "Sold!" */
        return;
    }
    chosen = idx;
    dialog_load(&buy, 5);
    const char *name = e->text;         /* first string of the ad is the name */
    char price_line[64];
    long price = e->kind == E_CAR ? e->price : g_parts[e->part].price;
    SDL_snprintf(price_line, sizeof price_line, "You want to buy it for $%-3ld ?", price);
    for (int i = 0, ct = 0; i < buy.count; i++) {
        if (buy.w[i].id == -25 && e->kind != E_CAR) {     /* "See it" only for cars: 11a2 turns it into */
            buy.w[i].flags = 0;                          /* a disabled, invisible hotspot */
            buy.w[i].type = W_HOTSPOT;
        }
        if (buy.w[i].type == W_CTEXT) dialog_set_text(&buy, i, ct++ == 0 ? name : price_line);
    }
    mode = MODE_BUY;
}

static void leave(Game *g) { game_set_scene(g, &scene_garage); }

static void purchase(Game *g)
{
    Player *p = &g->player;
    Entry *e = &entries[chosen];
    long price = e->kind == E_CAR ? e->price : g_parts[e->part].price;
    if (price > p->money) {
        game_message_short(g, price - p->money);    /* "No go, hot shot. You're $X short." */
        mode = MODE_ADS;
        return;
    }
    if (e->kind == E_CAR) {
        int slot = p->ads[e->slot] & 0x7f, model = slot > 20 ? slot - 21 : slot;
        if (!player_add_car(p, model, (int)price, slot > 20)) { game_message(g, 18); mode = MODE_ADS; return; }
        p->money -= price;
        p->ads[e->slot] |= 0x80;
        p->ad_cooldown[slot] += 3;
        p->time_actions += 0x444;
        game_message(g, 2);                         /* "It's yours!" */
        leave(g);
    } else {
        if (player_add_part(p, e->part, (int)price)) { game_message(g, 19); mode = MODE_ADS; return; }
        p->money -= price;
        p->time_actions += 0x222;
        mode = MODE_ADS;
        if (p->part_count > 180) leave(g);
    }
}

static void enter(Game *g)
{
    Player *p = &g->player;
    int week = week_of(p);
    if (week != p->ads_week) new_issue(p);
    p->ads_week = week;
    build_entries(p);
    dialog_load(&front, 3);
    dialog_load(&page, 4);
    for (int i = 1; i <= 24; i++) page.w[i].no_mark = 1;    /* ad rows get a background highlight */
    dialog_set_mark(&page, dialog_find(&page, -3), 265, 152, 310, 183);   /* frame the garage picture, not the whole corner */
    mode = MODE_FRONT;
}

static void choose_front(Game *g, int i)
{
    if (i < 0) return;
    int id = front.w[i].id;
    if (id == 3) { leave(g); return; }
    page_start = id == 1 ? 1 : first_part - 1;     /* Used Cars / Auto Parts */
    mode = MODE_ADS;
}

static void choose_page(Game *g, int i)
{
    if (i < 0) return;
    int id = page.w[i].id;
    if (id == -3) leave(g);
    else if (id == -2) page_start = page_next;
    else if (id == -1) prev_page();
    else if (id > 0) open_buy(g, id);
}

static void choose_buy(Game *g, int i)
{
    if (i < 0) return;
    int id = buy.w[i].id;
    if (id == -20) purchase(g);
    else if (id == -21) mode = MODE_ADS;
    else if (id == -25) mode = MODE_SEE_IT;
}

static void key(Game *g, SDL_Scancode sc, SDL_Keycode k)
{
    (void)k;
    switch (mode) {
    case MODE_FRONT: choose_front(g, dialog_key(&front, sc)); break;
    case MODE_ADS:   choose_page(g, dialog_key(&page, sc)); break;
    case MODE_BUY:   choose_buy(g, dialog_key(&buy, sc)); break;
    case MODE_SEE_IT: mode = MODE_BUY; break;
    }
}

static void click(Game *g, int x, int y)
{
    switch (mode) {
    case MODE_FRONT: choose_front(g, dialog_click(&front, x, y)); break;
    case MODE_ADS:   choose_page(g, dialog_click(&page, x, y)); break;
    case MODE_BUY:   choose_buy(g, dialog_click(&buy, x, y)); break;
    case MODE_SEE_IT: mode = MODE_BUY; break;
    }
}

static void mouse_move(Game *g, int x, int y)
{
    (void)g;
    if (mode == MODE_FRONT) dialog_mouse_move(&front, x, y);
    else if (mode == MODE_ADS) dialog_mouse_move(&page, x, y);
    else if (mode == MODE_BUY) dialog_mouse_move(&buy, x, y);
}

static void draw_front(Game *g)
{
    Video *v = &g->video;
    static const uint16_t headlines[12] = { 1187, 1008, 1009, 1011, 1010, 1012, 1189, 1190, 1191, 1192, 1193, 1194 };
    game_draw_picture(g, 1007, 0, 0);
    game_draw_picture(g, headlines[week_of(&g->player)], 0, 0x88);
    game_draw_picture(g, 1188, 0, 0xa7);
    int month, dom;
    player_date(&g->player, &month, &dom);
    char date[32];
    SDL_snprintf(date, sizeof date, "%s %d, 1969", exe_ds_str(exe_ds_u16((uint16_t)(0x4d82 + 2 * month))), dom);
    ui_set_scheme(AD_SCHEME);
    ui_text(v, 0x61, 0x79, date);
}

static void draw_page(Game *g)
{
    Video *v = &g->video;
    const Image *side = game_picture(g, 1013);
    if (side) {
        video_blit_part(v, side, 0, 0, 64, 0xbe, 0, 0);
        video_blit_part(v, side, 64, 0, 64, 0xbe, 256, 0);
    }
    video_fill(v, 0x40, 0, 0xc0, 0xbe, scheme_byte(AD_SCHEME, 2));
    v->clip_bottom = 0xbe;              /* 0ac8 clips the page to 190 rows */
    layout_page(v, 1);
    v->clip_bottom = SCREEN_H;
}

static void draw_see_it(Game *g)
{
    Video *v = &g->video;
    Entry *e = &entries[chosen];
    int slot = g->player.ads[e->slot] & 0x7f, model = slot > 20 ? slot - 21 : slot;
    int colour = g_models[model].colour;
    game_set_paint(g, colour);
    video_fill(v, 0, 0x41, 320, 0x46, colour_backdrop(colour));
    video_fill(v, 0, 0x87, 320, 10, scheme_byte(SEE_IT_SCHEME, 1));
    car_draw(v, g, NULL, model, -1, 0x86);
    ui_set_scheme(SEE_IT_SCHEME);
    ui_text(v, (320 - font_text_width(e->text)) / 2, 0x88, e->text);
}

static void update(Game *g)
{
    game_set_palette(g, PAL_SCREENS);
    Car *c = player_car(&g->player);
    if (c) game_set_paint(g, c->colour);
    video_clear(&g->video, 0);
    game_draw_status_bar(g, NULL);
    if (mode == MODE_FRONT) {
        draw_front(g);
        dialog_draw(&g->video, &front);
    } else {
        draw_page(g);
        if (mode == MODE_ADS) dialog_draw(&g->video, &page);   /* hover marks only: its widgets are hotspots */
        if (mode == MODE_BUY) dialog_draw(&g->video, &buy);
        if (mode == MODE_SEE_IT) draw_see_it(g);
    }
}

const Scene scene_newspaper = { enter, key, NULL, mouse_move, click, update };
