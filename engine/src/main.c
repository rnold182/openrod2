/* Street Rod 2 rebuild — entry point and platform loop. */
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "assets/exe.h"
#include "cheats.h"
#include "data.h"
#include "game.h"
#include "sound.h"
#include "ui/font.h"

/* --script "ms:key:NAME;ms:hold:NAME;ms:release:NAME;ms:text:abc;ms:click:x,y;ms:shot:file.bmp;ms:quit" drives the game for tests. */
typedef struct { Uint64 at; char kind[8]; char arg[64]; int done; } ScriptStep;
static ScriptStep script[160];
static int script_len;

static void parse_script(const char *s)
{
    char buf[8192];
    SDL_strlcpy(buf, s, sizeof buf);
    for (char *tok = strtok(buf, ";"); tok && script_len < 160; tok = strtok(NULL, ";")) {
        ScriptStep *st = &script[script_len];
        char *c1 = strchr(tok, ':');
        if (!c1) continue;
        *c1 = 0;
        st->at = SDL_strtoull(tok, NULL, 10);
        char *c2 = strchr(c1 + 1, ':');
        if (c2) { *c2 = 0; SDL_strlcpy(st->arg, c2 + 1, sizeof st->arg); }
        SDL_strlcpy(st->kind, c1 + 1, sizeof st->kind);
        script_len++;
    }
}

static int has_data(const char *dir)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/LIB1", dir);
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

/* Original game files may live next to the exe, one level up (dev build), or be given on the command line. */
static int find_data_dir(Game *g, int argc, char **argv)
{
    const char *base = SDL_GetBasePath();
    char cands[4][1024];
    int n = 0;
    if (argc > 1 && argv[1][0] != '-') snprintf(cands[n++], 1024, "%s", argv[1]);
    if (base) {
        snprintf(cands[n++], 1024, "%s", base);
        snprintf(cands[n++], 1024, "%s..", base);
    }
    snprintf(cands[n++], 1024, ".");
    for (int i = 0; i < n; i++)
        if (has_data(cands[i])) {
            snprintf(g->data_dir, sizeof g->data_dir, "%s", cands[i]);
            return 0;
        }
    return -1;
}

static int fail(const char *what)
{
    char msg[1200];
    snprintf(msg, sizeof msg, "%s\n\nPut the original Street Rod 2 files next to the executable "
                              "or pass their folder as an argument.", what);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Street Rod 2", msg, NULL);
    return 1;
}

static void window_to_screen(Game *g, float wx, float wy, int *x, int *y)
{
    float lx, ly;
    SDL_RenderCoordinatesFromWindow(g->video.renderer, wx, wy, &lx, &ly);
    *x = (int)lx;
    *y = (int)(ly * SCREEN_H / 240.0f);     /* 320x240 logical (4:3) -> 320x200 framebuffer */
}

static void dispatch_key(Game *g, SDL_Scancode sc, SDL_Keycode k)
{
    if (k == SDLK_F11 || (k == SDLK_RETURN && (SDL_GetModState() & SDL_KMOD_ALT))) {
        video_toggle_fullscreen(&g->video);
        return;
    }
    if (k == SDLK_F12 || k == SDLK_GRAVE) {
        if (g->scene != &scene_intro && g->scene != &scene_license) cheats_toggle(g);
        return;
    }
    if (cheats_active()) { cheats_key(g, k); return; }
    if (g->msg && !g->msg_sticky) { game_message_dismiss(g); return; }   /* any key dismisses a message */
    if (k == SDLK_M && g->scene != &scene_license) { sound_toggle_music(); return; }
    if (g->scene->key) g->scene->key(g, sc, k);
}

static void dispatch_click(Game *g, int x, int y)
{
    if (g->msg && !g->msg_sticky) { game_message_dismiss(g); return; }
    if (g->scene->click) g->scene->click(g, x, y);
}

static void run_script(Game *g, Uint64 now)
{
    for (int i = 0; i < script_len; i++) {
        ScriptStep *st = &script[i];
        if (st->done || now < st->at) continue;
        st->done = 1;
        if (!strcmp(st->kind, "key")) {
            SDL_Keycode k = SDL_GetKeyFromName(st->arg);
            dispatch_key(g, SDL_GetScancodeFromKey(k, NULL), k);
        } else if (!strcmp(st->kind, "hold") || !strcmp(st->kind, "release")) {
            SDL_Keycode k = SDL_GetKeyFromName(st->arg);
            game_script_hold(SDL_GetScancodeFromKey(k, NULL), st->kind[0] == 'h');
        } else if (!strcmp(st->kind, "text")) {
            if (cheats_active()) cheats_text(g, st->arg);
            else if (g->scene->text && !g->msg) g->scene->text(g, st->arg);
        } else if (!strcmp(st->kind, "click") || !strcmp(st->kind, "move")) {
            int x = 0, y = 0;
            SDL_sscanf(st->arg, "%d,%d", &x, &y);
            g->mouse_x = x; g->mouse_y = y; g->mouse_in = 1;
            if (g->scene->mouse_move) g->scene->mouse_move(g, x, y);
            if (!strcmp(st->kind, "click")) dispatch_click(g, x, y);
        } else if (!strcmp(st->kind, "cheat")) {
            cheats_run(g, st->arg);
        } else if (!strcmp(st->kind, "shot")) {
            video_save_bmp(&g->video, st->arg);
        } else if (!strcmp(st->kind, "quit")) {
            g->quit = 1;
        }
    }
}

int main(int argc, char **argv)
{
    static Game g;
    const char *new_game_name = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--script") && i + 1 < argc) parse_script(argv[++i]);
        else if (!strcmp(argv[i], "--record-audio") && i + 1 < argc) g.record_path = argv[++i];
        else if (!strcmp(argv[i], "--new-game") && i + 1 < argc) new_game_name = argv[++i];   /* testing */
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (find_data_dir(&g, argc, argv)) return fail("Original game files (LIB1, SR.EXE, ...) not found.");

    char path[1100];
    snprintf(path, sizeof path, "%s/SR.EXE", g.data_dir);
    if (exe_load(path) || font_init()) return fail("SR.EXE could not be read.");
    if (data_load(g.data_dir)) return fail("SR2_DATA could not be read.");
    static const char *libs[3] = { "LIB1", "LIB2", "LIB3" };
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", g.data_dir, libs[i]);
        if (lib_open(&g.lib[i], path)) return fail("A picture library (LIB1-3) could not be read.");
    }
    if (video_init(&g.video, "Street Rod 2")) {
        SDL_Log("video init failed: %s", SDL_GetError());
        return 1;
    }
    sound_init(g.data_dir);
    if (g.record_path) sound_record(g.record_path);
    sound_play_playlist(PLAYLIST_MAIN);     /* SR.EXE starts the music before the intro */

    g.scene = &scene_intro;
    if (new_game_name) {
        player_new(&g.player, new_game_name);
        g.scene = &scene_garage;
    }
    g.scene->enter(&g);

    Uint64 last = SDL_GetTicks();
    while (!g.quit) {
        Uint64 now_ms = SDL_GetTicks();
        if (g.scene != &scene_intro && g.scene != &scene_license)   /* the summer clock runs in real time */
            g.player.time_ticks += (now_ms - last) * TICK_HZ / 1000.0;
        last = now_ms;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                g.quit = 1;
            } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                dispatch_key(&g, e.key.scancode, e.key.key);
            } else if (e.type == SDL_EVENT_TEXT_INPUT) {
                if (cheats_active()) cheats_text(&g, e.text.text);
                else if (g.scene->text && (!g.msg || g.msg_sticky)) g.scene->text(&g, e.text.text);
            } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
                int x, y;
                window_to_screen(&g, e.motion.x, e.motion.y, &x, &y);
                g.mouse_x = x; g.mouse_y = y;
                g.mouse_in = x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H;
                if (g.scene->mouse_move) g.scene->mouse_move(&g, x, y);
            } else if (e.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
                g.mouse_in = 0;
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                int x, y;
                window_to_screen(&g, e.button.x, e.button.y, &x, &y);
                dispatch_click(&g, x, y);
            }
        }
        run_script(&g, SDL_GetTicks());
        if (g.next_scene) {
            g.scene = g.next_scene;
            g.next_scene = NULL;
            g.scene->enter(&g);
        }
        if (g.scene->update) g.scene->update(&g);
        game_draw_message(&g);
        cheats_draw(&g);
        game_draw_cursor(&g);
        video_present(&g.video);
    }

    sound_shutdown();
    video_shutdown(&g.video);
    for (int i = 0; i < 3; i++) lib_close(&g.lib[i]);
    exe_free();
    SDL_Quit();
    return 0;
}
