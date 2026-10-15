/* "Save Game" (1001:5d82, dialog 28) and "Old Game" (1001:59e2, dialog 26) screens.
 * Rebuild additions: the pointed-at (or chosen) save's file date and in-game date are shown in the
 * status bar, the arrow keys move through the list, and clicking a chosen game again loads it. */
#include <string.h>

#include "../assets/exe.h"
#include "../game.h"
#include "../options.h"
#include "../save.h"
#include "../ui/font.h"
#include "files.h"

#define BACKDROP    1001
#define NAME_BOX    1003        /* behind the name field (save only) */
#define LOAD_BUTTON 1004
#define SAVE_BUTTON 1005
#define QUIT_BUTTON 1006
#define ROW_SCHEME  12

static int mode;                /* FILES_LOAD / FILES_SAVE */
static const Scene *back;       /* where "Forget it" goes */
static Dialog dlg, box;         /* box: dialog 27 (message + OK) or 29 (replace?) */
static int box_kind;            /* 0 none, 1 message, 2 confirm overwrite */
static int box_then_leave;      /* close the screen when the message is acknowledged */
static SaveSlot slots[SAVE_SLOTS];
static int rows[SAVE_SLOTS], row_count;   /* row -> slot */
static int sel = -1;            /* chosen row */
static int field = -1;
static char name[SAVE_NAME];

void files_open(Game *g, int m, const Scene *return_to)
{
    mode = m;
    back = return_to;
    game_set_scene(g, &scene_files);
}

static void message(const char *text, int leave)
{
    dialog_load(&box, 27);
    for (int i = 0; i < box.count; i++) if (box.w[i].type == W_CTEXT) dialog_set_text(&box, i, text);
    box_kind = 1;
    box_then_leave = leave;
}

static void leave(Game *g)
{
    SDL_StopTextInput(g->video.window);
    game_set_scene(g, back);
}

static void enter(Game *g)
{
    game_set_palette(g, PAL_SCREENS);
    int used = save_scan(g->data_dir, slots);
    dialog_load(&dlg, mode == FILES_SAVE ? 28 : 26);
    box_kind = 0;
    row_count = 0;
    sel = -1;
    field = -1;
    for (int s = 0; s < SAVE_SLOTS; s++)
        if (mode == FILES_SAVE || slots[s].used) rows[row_count++] = s;   /* the load list skips gaps */
    int r = 0;
    for (int i = 0; i < dlg.count; i++) {
        Widget *w = &dlg.w[i];
        if (w->type == W_ITEM) {
            int s = r < row_count ? rows[r] : -1;
            dialog_set_text(&dlg, i, s >= 0 && slots[s].used ? slots[s].name : "");
            if (r >= row_count) w->flags &= (uint16_t)~WF_ENABLED;
            r++;
        } else if (w->type == W_FIELD) {
            field = i;
        }
    }
    if (mode == FILES_SAVE) {
        SDL_strlcpy(name, g->player.name, sizeof name);
        for (int k = 0; k < row_count && sel < 0; k++) if (!slots[rows[k]].used) sel = k;   /* first free slot */
        if (sel < 0) sel = 0;
        SDL_StartTextInput(g->video.window);
    } else {
        if (!used) message(exe_ds_str(0x4cc8), 1);     /* "No saved games found !" */
        else sel = 0;
    }
}

static int row_of_widget(int i)
{
    int r = 0;
    for (int k = 0; k < dlg.count; k++)
        if (dlg.w[k].type == W_ITEM) { if (k == i) return r; r++; }
    return -1;
}

static int blank(const char *s)             /* 1001:5494: only spaces or underscores */
{
    for (; *s; s++) if (*s != ' ' && *s != '_') return 0;
    return 1;
}

static void do_load(Game *g)
{
    if (sel < 0 || sel >= row_count) return;
    if (save_read(g->data_dir, rows[sel], &g->player)) { message(exe_ds_str(0x4ce0), 0); return; }   /* "Cannot read that game !" */
    SDL_Log("loaded %s from slot %d", g->player.name, rows[sel]);
    SDL_StopTextInput(g->video.window);
    game_set_scene(g, &scene_garage);
}

static void write_save(Game *g)
{
    int slot = rows[sel];
    const char *n = blank(name) ? slots[slot].name : name;
    if (save_write(g->data_dir, slot, &g->player, n)) { message(exe_ds_str(0x4d06), 0); return; }   /* "Disk error during Save" */
    SDL_Log("saved %s to slot %d", n, slot);
    leave(g);
}

static void do_save(Game *g)
{
    if (sel < 0) return;
    size_t len = strlen(name);
    while (len > 0 && name[len - 1] == ' ') name[--len] = 0;    /* 1001:54ba */
    if (blank(name) && !slots[rows[sel]].used) { message(exe_ds_str(0x4c8e), 0); return; }   /* "The game must have a name !" */
    if (!blank(name))
        for (int k = 0; k < row_count; k++)                     /* the same name means the same game */
            if (slots[rows[k]].used && !strcmp(slots[rows[k]].name, name)) sel = k;
    if (slots[rows[sel]].used) { dialog_load(&box, 29); box_kind = 2; return; }   /* "The old game will be replaced !" */
    write_save(g);
}

static void choose(Game *g, int i)
{
    if (i < 0) return;
    const Widget *w = &dlg.w[i];
    if (w->type == W_ITEM) {
        int r = row_of_widget(i);
        if (r < 0 || r >= row_count) return;
        if (mode == FILES_LOAD && r == sel) { do_load(g); return; }
        sel = r;
        if (mode == FILES_SAVE && slots[rows[r]].used) SDL_strlcpy(name, slots[rows[r]].name, sizeof name);
        return;
    }
    if (w->id == -3) do_load(g);                    /* Load Game */
    else if (w->id == -23) do_save(g);              /* Save Game */
    else if (w->id == -4 || w->id == -24) leave(g); /* Forget it */
}

static void choose_box(Game *g, int i)
{
    if (i < 0) return;
    int id = box.w[i].id;
    if (box_kind == 1 && id == -20) {
        box_kind = 0;
        if (box_then_leave) leave(g);
    } else if (box_kind == 2 && (id == -20 || id == -21)) {
        box_kind = 0;
        if (id == -20) write_save(g);               /* Go ahead */
    }
}

static void key(Game *g, SDL_Scancode sc, SDL_Keycode k)
{
    if (box_kind) {
        if (k == SDLK_RETURN && box_kind == 1) { choose_box(g, dialog_find(&box, -20)); return; }
        choose_box(g, dialog_key(&box, sc));
        return;
    }
    if (k == SDLK_UP && sel > 0) sel--;
    else if (k == SDLK_DOWN && sel < row_count - 1) sel++;
    else if (mode == FILES_SAVE && k == SDLK_BACKSPACE) { size_t n = strlen(name); if (n) name[n - 1] = 0; }
    else if (mode == FILES_SAVE && k == SDLK_RETURN) do_save(g);
    else if (mode == FILES_SAVE && k == SDLK_ESCAPE) leave(g);
    else if (mode == FILES_LOAD) choose(g, dialog_key(&dlg, sc));     /* L / Enter, F / Esc */
    if (mode == FILES_SAVE && (k == SDLK_UP || k == SDLK_DOWN) && sel >= 0 && slots[rows[sel]].used)
        SDL_strlcpy(name, slots[rows[sel]].name, sizeof name);
}

static void text(Game *g, const char *s)
{
    (void)g;
    if (mode != FILES_SAVE || box_kind) return;
    for (; *s; s++) {
        size_t n = strlen(name);
        if (n >= SAVE_NAME - 1) break;
        if (*s >= ' ' && *s <= 'z') { name[n] = *s; name[n + 1] = 0; }
    }
}

static void click(Game *g, int x, int y)
{
    if (box_kind) choose_box(g, dialog_click(&box, x, y));
    else choose(g, dialog_click(&dlg, x, y));
}

static void mouse_move(Game *g, int x, int y)
{
    (void)g;
    if (box_kind) dialog_mouse_move(&box, x, y);
    else dialog_mouse_move(&dlg, x, y);
}

/* "Saved 15 Oct 2026 21:40 - July 3": the file's time and the summer day it holds. */
static void slot_info(int slot, char *out, size_t n)
{
    const SaveSlot *s = &slots[slot];
    out[0] = 0;
    if (!s->used || !opt_save_info) return;
    SDL_DateTime dt;
    int month, dom;
    day_date(s->day, &month, &dom);
    const char *mname = exe_ds_str(exe_ds_u16((uint16_t)(0x4d82 + 2 * month)));
    static const char *mon[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    if (s->saved_at && SDL_TimeToDateTime(s->saved_at, &dt, true))
        SDL_snprintf(out, n, "%d %s %d %02d:%02d - %s %d", dt.day, mon[(dt.month - 1) % 12], dt.year, dt.hour, dt.minute, mname, dom);
    else
        SDL_snprintf(out, n, "%s %d", mname, dom);
}

static void update(Game *g)
{
    Video *v = &g->video;
    video_clear(v, 0);
    game_draw_picture(g, BACKDROP, 0, 0);
    if (mode == FILES_SAVE) {
        game_draw_picture(g, NAME_BOX, 0x48, 0xa9);
        game_draw_picture(g, SAVE_BUTTON, 0xf0, 100);
        game_draw_picture(g, QUIT_BUTTON, 0xf0, 0x8c);
        char shown[SAVE_NAME + 1];
        SDL_snprintf(shown, sizeof shown, "%s%s", name, (SDL_GetTicks() / 300) % 2 && !box_kind ? "_" : "");
        dialog_set_text(&dlg, field, shown);
    } else if (row_count) {
        game_draw_picture(g, LOAD_BUTTON, 0xf0, 100);
        game_draw_picture(g, QUIT_BUTTON, 0xf0, 0x8c);
    }

    const Widget *hw = !box_kind && dlg.hover >= 0 ? &dlg.w[dlg.hover] : NULL;
    int info_row = hw && hw->type == W_ITEM ? row_of_widget(dlg.hover) : sel;
    char info[64] = "";
    if (info_row >= 0 && info_row < row_count) slot_info(rows[info_row], info, sizeof info);
    game_draw_status_bar(g, hw && hw->type == W_HOTSPOT ? hw->text : info);

    if (!box_kind || mode == FILES_SAVE) dialog_draw(v, &dlg);
    /* The chosen row in reverse colours, as the original's list chooser marks it. */
    const uint8_t *sc = exe_ds((uint16_t)(0x1d06 + ROW_SCHEME * 5), 5);
    for (int i = 0, r = 0; i < dlg.count && sc; i++) {
        const Widget *w = &dlg.w[i];
        if (w->type != W_ITEM) continue;
        if (r++ != sel || sel < 0) continue;
        video_fill(v, w->x1, w->y1, w->x2 - w->x1, w->y2 - w->y1, sc[0]);
        font_draw(v, w->x1 + 2, w->y1, dialog_text(&dlg, i), sc[1], sc[0]);
    }
    if (box_kind) dialog_draw(v, &box);
}

const Scene scene_files = { enter, key, text, mouse_move, click, update };
