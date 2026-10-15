/* AdLib Visual Composer .MUS song sequencer. */
#ifndef SR_MUS_H
#define SR_MUS_H

#include "adlib.h"

typedef struct MusSong {
    uint8_t  *data;
    uint32_t  size;
    uint8_t   tick_beat;
    uint8_t   sound_mode;       /* 0 melodic, 1 percussive */
    uint8_t   pitch_range;
    uint16_t  basic_tempo;
} MusSong;

/* Called when a song ends; return the song to chain into, or NULL to stop. */
typedef const struct MusSong *(*MusNextFn)(void *ctx);

typedef struct {
    const MusSong   *song;
    Adlib           *adlib;
    const AdlibBank *bank;
    uint32_t pos;
    uint8_t  status;
    uint32_t wait;              /* ticks until next event */
    double   tempo;             /* beats per minute */
    int      finished;
    MusNextFn next_song;
    void     *next_ctx;
    uint8_t  last_vol[ADLIB_VOICES];   /* PLAY.C caches the last note-on volume per voice */
} MusPlayer;

int  mus_load(MusSong *s, const char *path);
void mus_free(MusSong *s);

void   mus_start(MusPlayer *p, const MusSong *s, Adlib *a, const AdlibBank *bank,
                 MusNextFn next_song, void *next_ctx);
/* Advance one tick; returns 0 once playback has ended. */
int    mus_tick(MusPlayer *p);
double mus_ticks_per_second(const MusPlayer *p);

#endif
