#include "cheats.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "data.h"
#include "options.h"
#include "scenes/race.h"
#include "ui/font.h"

#define CONSOLE_SCHEME 1
#define LINES 6

static int active;
static char input[48];
static char out[LINES][64];
static int was_text_input;

static void say(const char *fmt, ...)
{
    memmove(out[0], out[1], sizeof out[0] * (LINES - 1));
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(out[LINES - 1], sizeof out[0], fmt, ap);
    va_end(ap);
}

int cheats_active(void) { return active; }

void cheats_toggle(Game *g)
{
    active = !active;
    input[0] = 0;
    if (active) {
        was_text_input = SDL_TextInputActive(g->video.window);
        SDL_StartTextInput(g->video.window);
        if (!out[LINES - 1][0]) say("Cheats. Type help and press Enter.");
    } else if (!was_text_input) {
        SDL_StopTextInput(g->video.window);
    }
}

static void list_models(void)
{
    for (int m = 0; m < MODEL_COUNT; m += 3) {
        char line[64] = "";
        for (int k = m; k < m + 3 && k < MODEL_COUNT; k++) {
            char item[24];
            SDL_snprintf(item, sizeof item, "%d %.14s  ", k, g_models[k].ad);
            SDL_strlcat(line, item, sizeof line);
        }
        say("%s", line);
    }
}

void cheats_run(Game *g, const char *line)
{
    Player *p = &g->player;
    char cmd[16] = "", arg[32] = "";
    SDL_sscanf(line, "%15s %31s", cmd, arg);
    if (!strcmp(cmd, "money")) {
        if (arg[0] == '=') p->money = atol(arg + 1);
        else p->money += arg[0] ? atol(arg) : 1000;
        say("Bankroll: $%ld", p->money);
    } else if (!strcmp(cmd, "car")) {
        if (!strcmp(arg, "list")) { list_models(); return; }
        int m = atoi(arg);
        if (m < 0 || m >= MODEL_COUNT) { say("car 0..%d (car list)", MODEL_COUNT - 1); return; }
        Car *c = player_add_car(p, m, g_models[m].price, 0);
        if (!c) { say("No room for more cars."); return; }
        say("Added %s", g_models[m].ad);
    } else if (!strcmp(cmd, "part")) {
        if (!strcmp(arg, "list")) {
            for (int k = 0; k < PART_COUNT; k += 2)
                say("%d %.26s  %d %.26s", k, g_parts[k].ad, k + 1, g_parts[k + 1].ad);
            return;
        }
        int k = atoi(arg);
        if (k < 0 || k >= PART_COUNT || player_add_part(p, k, g_parts[k].price)) { say("part 0..%d (part list)", PART_COUNT - 1); return; }
        say("Added %s", g_parts[k].ad);
    } else if (!strcmp(cmd, "day")) {
        int d = atoi(arg);
        if (d < 0) d = 0;
        if (d > 91) d = 91;
        p->time_actions = (long)d * 6552;
        p->time_ticks = 0;
        int month, dom;
        player_date(p, &month, &dom);
        say("Day %d: month %d, day %d", d, month, dom);
    } else if (!strcmp(cmd, "rival")) {
        if (g->scene != &scene_street) { say("Only at Burger's"); return; }
        int r = atoi(arg);
        if (r < 0 || r >= RIVAL_COUNT) { say("rival 0..%d (21 = The King)", RIVAL_COUNT - 1); return; }
        street_summon(g, r);
        say("Calling %s", g_rival_names[r]);
    } else if (!strcmp(cmd, "autodrive")) {           /* testing the race */
        race_autodrive(strcmp(arg, "off") != 0);
        say("Autodrive %s", strcmp(arg, "off") ? "on" : "off");
    } else if (!strcmp(cmd, "anim")) {            /* until the options menu exists */
        if (atoi(arg) > 0) {
            opt_anim_speed = SDL_clamp(atoi(arg), 25, 400);
            opt_animations = 1;
        } else {
            opt_animations = !strcmp(arg, "on") ? 1 : !strcmp(arg, "off") ? 0 : !opt_animations;
        }
        say("Animations %s, speed %d%%", opt_animations ? "on" : "off", opt_anim_speed);
    } else if (!strcmp(cmd, "help")) {
        say("money N (add) / money =N (set)");
        say("car N / car list    part N / part list");
        say("day N (0 = June 16 .. 91)");
        say("anim [on|off|speed 25..400]   rival N (at Burger's)");
    } else if (cmd[0]) {
        say("Unknown: %s (try help)", cmd);
    }
}

void cheats_key(Game *g, SDL_Keycode k)
{
    if (k == SDLK_ESCAPE) { cheats_toggle(g); return; }
    if (k == SDLK_BACKSPACE) {
        size_t n = strlen(input);
        if (n) input[n - 1] = 0;
    } else if (k == SDLK_RETURN) {
        say("> %s", input);
        cheats_run(g, input);
        input[0] = 0;
    }
}

void cheats_text(Game *g, const char *s)
{
    (void)g;
    for (; *s; s++) {
        if (*s == '`') continue;            /* the toggle key itself */
        size_t n = strlen(input);
        if (n + 1 < sizeof input && *s >= ' ' && *s <= 'z') { input[n] = *s; input[n + 1] = 0; }
    }
}

void cheats_draw(Game *g)
{
    if (!active) return;
    Video *v = &g->video;
    int h = (LINES + 1) * 9 + 4;
    ui_frame(v, 6, 6, 308, h, CONSOLE_SCHEME);
    for (int i = 0; i < LINES; i++) ui_text(v, 8, 8 + i * 9, out[i]);
    char prompt[64];
    SDL_snprintf(prompt, sizeof prompt, "> %s%s", input, (SDL_GetTicks() / 300) % 2 ? "_" : "");
    ui_text(v, 8, 8 + LINES * 9, prompt);
}
