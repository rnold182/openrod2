#include "mus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MUS_HEADER 70

int mus_load(MusSong *s, const char *path)
{
    memset(s, 0, sizeof *s);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)len);
    if (!d || len < MUS_HEADER || fread(d, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(d); return -1; }
    fclose(f);

    uint32_t data_size = d[0x2a] | d[0x2b] << 8 | d[0x2c] << 16 | (uint32_t)d[0x2d] << 24;
    if (data_size > (uint32_t)len - MUS_HEADER) data_size = (uint32_t)len - MUS_HEADER;
    s->tick_beat = d[0x24];
    s->sound_mode = d[0x3a];
    s->pitch_range = d[0x3b] ? d[0x3b] : 1;
    s->basic_tempo = (uint16_t)(d[0x3c] | d[0x3d] << 8);
    s->size = data_size;
    s->data = malloc(data_size);
    memcpy(s->data, d + MUS_HEADER, data_size);
    free(d);
    return 0;
}

void mus_free(MusSong *s)
{
    free(s->data);
    s->data = NULL;
}

static int at_end(const MusPlayer *p) { return p->pos >= p->song->size; }
static uint8_t next(MusPlayer *p) { return at_end(p) ? 0xFC : p->song->data[p->pos++]; }

static void read_delay(MusPlayer *p)
{
    uint32_t t = 0;
    for (;;) {
        uint8_t b = next(p);
        if (b == 0xF8) { t += 240; continue; }
        t += b;
        break;
    }
    p->wait = t;
}

static void start_song(MusPlayer *p)
{
    p->pos = 0;
    p->status = 0;
    p->tempo = p->song->basic_tempo;
    memset(p->last_vol, 0, sizeof p->last_vol);
    read_delay(p);
}

void mus_start(MusPlayer *p, const MusSong *s, Adlib *a, const AdlibBank *bank,
               MusNextFn next_song, void *next_ctx)
{
    memset(p, 0, sizeof *p);
    p->song = s;
    p->adlib = a;
    p->bank = bank;
    p->next_song = next_song;
    p->next_ctx = next_ctx;
    adlib_silence(a);
    adlib_set_mode(a, s->sound_mode);
    adlib_set_pitch_range(a, s->pitch_range);
    start_song(p);
}

/* The original programs the PC timer: rate = tempo*tickBeat/60 (min 19 Hz), divisor = 1193180/rate. */
double mus_ticks_per_second(const MusPlayer *p)
{
    long rate = (long)p->tempo * p->song->tick_beat / 60;
    if (rate < 19) rate = 19;
    return 1193180.0 / (1193180L / rate);
}

/* Execute one event; returns 0 on end-of-song marker. */
static int do_event(MusPlayer *p)
{
    uint8_t b = next(p);
    if (b < 0x80) {             /* running status */
        p->pos--;
        b = p->status;
    } else if (b < 0xF0) {
        p->status = b;
    }
    int voice = b & 0x0f;
    int nv = p->song->sound_mode ? 11 : 9;
    Adlib *a = p->adlib;

    switch (b & 0xF0) {
    case 0x80: {
        next(p); next(p);
        if (voice < nv) adlib_note_off(a, voice);
        break;
    }
    case 0x90: {
        int note = next(p), vol = next(p);
        if (voice >= nv) break;
        if (vol == 0) {
            adlib_note_off(a, voice);
        } else {
            if (vol != p->last_vol[voice]) {
                p->last_vol[voice] = (uint8_t)vol;
                adlib_set_volume(a, voice, vol);
            }
            adlib_note_on(a, voice, note);
        }
        break;
    }
    case 0xA0: {
        int vol = next(p);
        if (voice < nv) adlib_set_volume(a, voice, vol);
        break;
    }
    case 0xB0:
        next(p); next(p);
        break;
    case 0xC0: {
        int t = next(p);
        if (voice < nv && t < p->bank->count) adlib_set_timbre(a, voice, p->bank->timbres[t].p);
        break;
    }
    case 0xD0:
        next(p);
        break;
    case 0xE0: {
        int lo = next(p), hi = next(p);
        if (voice < nv) adlib_set_pitch_bend(a, voice, lo | hi << 7);
        break;
    }
    default:
        if (b == 0xF0) {        /* system exclusive: tempo change is F0 7F 00 int frac F7 */
            uint8_t buf[8];
            int n = 0;
            for (;;) {
                uint8_t c = next(p);
                if (c == 0xF7 || at_end(p)) break;
                if (n < 8) buf[n++] = c;
            }
            if (n >= 4 && buf[0] == 0x7F && buf[1] == 0x00)   /* integer maths as in PLAY.C */
                p->tempo = p->song->basic_tempo * buf[2] + ((long)p->song->basic_tempo * buf[3] >> 7);
        } else if (b == 0xFC) {
            return 0;
        }
        break;
    }
    return 1;
}

int mus_tick(MusPlayer *p)
{
    if (p->finished) return 0;
    while (p->wait == 0) {
        if (!do_event(p) || at_end(p)) {
            const MusSong *n = p->next_song ? p->next_song(p->next_ctx) : NULL;
            if (!n) {
                p->finished = 1;
                adlib_silence(p->adlib);
                return 0;
            }
            /* Chaining (PLAY.C): switch data, pitch range and tempo only; voices keep sounding. */
            p->song = n;
            p->pos = 0;
            p->tempo = n->basic_tempo;
            adlib_set_pitch_range(p->adlib, n->pitch_range);
            read_delay(p);
            continue;
        }
        read_delay(p);
    }
    p->wait--;
    return 1;
}
