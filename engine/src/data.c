#include "data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "assets/exe.h"

CarModel g_models[MODEL_COUNT];
PartInfo g_parts[PART_COUNT];
const char *g_rival_names[RIVAL_COUNT];

static uint8_t *sr2_data;   /* kept alive: ad texts and names point into it */

/* SR2_DATA is read as fixed-size blocks straight into SR.EXE's data segment (2a21:6e6a). */
enum {
    B_RIVALS = 0, B_MODELS = 396, B_PATCHES = 836, B_GEOMETRY = 1056, B_9872 = 1320,
    B_A542 = 1760, B_NAMES = 1762, B_UNUSED = 1850, B_AA9E = 2246, B_TEXT = 2330,
};

const uint8_t *data_rivals_initial(void) { return sr2_data + B_RIVALS; }
const uint8_t *data_race_sprites(void) { return sr2_data + B_AA9E; }

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

int data_load(const char *data_dir)
{
    char path[1100];
    SDL_snprintf(path, sizeof path, "%s/SR2_DATA", data_dir);
    size_t len;
    sr2_data = SDL_LoadFile(path, &len);
    if (!sr2_data || len <= B_TEXT) return -1;
    const char *text = (const char *)sr2_data + B_TEXT;
    size_t text_len = len - B_TEXT;
#define TEXT(off) ((off) < text_len ? text + (off) : "")

    for (int m = 0; m < MODEL_COUNT; m++) {
        const uint8_t *r = sr2_data + B_MODELS + m * 20;
        CarModel *cm = &g_models[m];
        cm->price = rd16(r);
        cm->used_price = rd16(r + 2);
        cm->cls = r[4];
        cm->colour = (uint8_t)SDL_rand(5);
        cm->spec1 = rd16(r + 8);
        cm->spec2 = rd16(r + 10);
        cm->ad = TEXT(rd16(sr2_data + B_NAMES + 2 * m));
        cm->ad_lines = r[14];
        cm->picture = rd16(r + 16);
        cm->spec3 = rd16(r + 18);
        memcpy(cm->geo, sr2_data + B_GEOMETRY + m * 12, 12);
        for (int k = 0; k < 5; k++) cm->patch[k] = rd16(sr2_data + B_PATCHES + m * 10 + 2 * k);
        memcpy(cm->hot, sr2_data + B_9872 + m * 20, 20);
    }
    for (int i = 0; i < RIVAL_COUNT; i++) g_rival_names[i] = TEXT(rd16(sr2_data + B_NAMES + 2 * (22 + i)));
#undef TEXT

    for (int k = 0; k < PART_COUNT; k++) {
        const uint8_t *r = exe_ds((uint16_t)(0x4502 + 10 * k), 10);
        if (!r) return -1;
        g_parts[k].price = rd16(r);
        g_parts[k].grade = r[2];
        g_parts[k].make = r[3];
        g_parts[k].type = r[4];
        g_parts[k].ad = exe_ds_str(rd16(r + 6));
        g_parts[k].ad_lines = r[8];
    }
    return 0;
}

const char *ad_next_string(const char *s)
{
    s += strlen(s) + 1;
    if (*s == 0) s++;           /* strings are word-aligned: skip one padding byte */
    return s;
}

/* Port of the ad renderer in 1001:0ac8: a string starting with '+' continues the current line;
 * if `price` >= 0 the number after '$' is replaced by it. */
int ad_format_lines(const char *ad, int lines, long price, char out[][64], int max_lines)
{
    int n = 0;
    const char *s = ad;
    while (n < lines && n < max_lines) {
        char line[128] = "";
        for (;;) {
            SDL_strlcat(line, s, sizeof line);
            s = ad_next_string(s);
            if (*s != '+') break;
            s++;
        }
        char *dollar = strchr(line, '$');
        if (dollar && price >= 0) {
            char *end = dollar + 1;
            while (*end >= '0' && *end <= '9') end++;
            char rest[64];
            SDL_strlcpy(rest, end, sizeof rest);
            SDL_snprintf(dollar + 1, sizeof line - (size_t)(dollar + 1 - line), "%ld%s", price, rest);
        }
        SDL_strlcpy(out[n++], line, 64);
    }
    return n;
}

uint8_t colour_paint_reg(int colour)
{
    const uint8_t *p = exe_ds((uint16_t)(0x4d1e + colour * 3), 3);
    return p ? p[0] : 5;
}

uint8_t colour_paint_light(int colour)
{
    const uint8_t *p = exe_ds((uint16_t)(0x4d1e + colour * 3), 3);
    return p ? p[1] : 0;
}

uint8_t colour_backdrop(int colour)
{
    const uint8_t *p = exe_ds((uint16_t)(0x4d1e + colour * 3), 3);
    return p ? p[2] : 0;
}

uint16_t tire_picture(int grade, int frame)
{
    return exe_ds_u16((uint16_t)(0x4d30 + grade * 8 + (frame & 3) * 2));
}

int16_t part_initial_condition(int type) { return (int16_t)exe_ds_u16((uint16_t)(0x478c + type * 2)); }

int carbs_for_intake_grade(int grade) { return (int16_t)exe_ds_u16((uint16_t)(0x4e6e + grade * 2)); }
