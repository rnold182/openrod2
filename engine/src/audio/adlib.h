/* AdLib driver, ported from the ADLIB.C driver compiled into SR.EXE (segment 2c57).
 * Integer maths, tables and register writes follow the original so the music
 * sounds exactly as it did on a real AdLib card. */
#ifndef SR_ADLIB_H
#define SR_ADLIB_H

#include <stdint.h>

#define ADLIB_PARAMS 28         /* 13 per operator + 2 wave selects */
#define ADLIB_VOICES 11         /* 9 melodic, or 6 melodic + 5 percussion */
#define ADLIB_SLOTS  18

enum { ADLIB_BD = 6, ADLIB_SD, ADLIB_TOM, ADLIB_CYM, ADLIB_HH };

typedef struct {
    char    name[9];
    int16_t p[ADLIB_PARAMS];
} AdlibTimbre;

typedef struct {
    int         count;
    AdlibTimbre *timbres;
} AdlibBank;

typedef void (*AdlibWriteFn)(void *ctx, uint16_t reg, uint8_t val);

typedef struct {
    AdlibWriteFn write;
    void        *ctx;
    int          percussive;
    uint8_t      perc_bits;
    int          pitch_range_step;              /* range * 25 */
    uint8_t      slot_param[ADLIB_SLOTS][14];   /* 13 operator params + wave select */
    uint8_t      slot_volume[ADLIB_SLOTS];      /* 0..127 */
    uint8_t      slot_atten[ADLIB_SLOTS];       /* extra attenuation in 0.75 dB steps (not in ADLIB.C) */
    uint8_t      note_pitch[ADLIB_VOICES];
    uint8_t      key_on[ADLIB_VOICES];
    int          half_tone_offset[ADLIB_VOICES];
    const uint16_t *fnum_ptr[ADLIB_VOICES];
    long         old_t1;
    int          old_half_tone;
    const uint16_t *old_fnum_ptr;
} Adlib;

int  adlib_bank_load(AdlibBank *bank, const char *path);   /* .SND timbre bank */
void adlib_bank_free(AdlibBank *bank);
int  adlib_bank_find(const AdlibBank *bank, const char *name);

void adlib_init(Adlib *a, AdlibWriteFn write, void *ctx);
void adlib_set_mode(Adlib *a, int percussive);
void adlib_set_pitch_range(Adlib *a, int semitones);
void adlib_set_timbre(Adlib *a, int voice, const int16_t *params);
void adlib_set_volume(Adlib *a, int voice, int volume);
void adlib_set_pitch_bend(Adlib *a, int voice, int bend);   /* 0..0x3fff, 0x2000 = centre */
/* Rebuild addition: turn a voice down by `steps` x 0.75 dB on top of its volume (0 = as the original). */
void adlib_set_attenuation(Adlib *a, int voice, int steps);
void adlib_note_on(Adlib *a, int voice, int note);           /* MIDI-style note number */
void adlib_note_off(Adlib *a, int voice);
void adlib_silence(Adlib *a);

#endif
