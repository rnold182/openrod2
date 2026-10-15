#include "adlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Operator parameter order inside a timbre and inside slot_param[]. */
enum { P_KSL, P_MULTI, P_FB, P_AR, P_SL, P_EG, P_DR, P_RR, P_TL, P_AM, P_VIB, P_KSR, P_FM, P_WAVE };

#define NR_STEP_PITCH 25        /* pitch-bend resolution: steps per semitone */
#define MID_PITCH     0x2000
#define TOM_PITCH     24
#define TOM_TO_SD     7

/* Tables from SR.EXE's data segment (DS:782c..7889). */
static const uint8_t perc_mask[5]     = { 0x10, 0x08, 0x04, 0x02, 0x01 };
static const uint8_t slot_voice[9][2] = { { 0, 3 }, { 1, 4 }, { 2, 5 }, { 6, 9 }, { 7, 10 },
                                          { 8, 11 }, { 12, 15 }, { 13, 16 }, { 14, 17 } };
static const uint8_t slot_perc[5][2]  = { { 12, 15 }, { 16, 0 }, { 14, 0 }, { 17, 0 }, { 13, 0 } };
static const uint8_t offset_slot[18]  = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x08, 0x09, 0x0a,
                                          0x0b, 0x0c, 0x0d, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15 };
static const uint8_t oper_slot[18]    = { 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1 };
static const uint8_t chan_slot[18]    = { 0, 1, 2, 0, 1, 2, 3, 4, 5, 3, 4, 5, 6, 7, 8, 6, 7, 8 };

static uint16_t fnum_notes[NR_STEP_PITCH][12];
static int fnum_ready;

/* ADLIB.C CalcPremFNum: F-number * 8 for C, offset by num/den semitones. */
static long calc_prem_fnum(long num, long den)
{
    long d100 = den * 100;
    long f8 = (d100 + 6 * num) * 52088L / (d100 * 25);
    return f8 * 147456L / 111875L;
}

static void init_fnums(void)
{
    for (int i = 0; i < NR_STEP_PITCH; i++) {
        long val = calc_prem_fnum(i * 4, 100);
        fnum_notes[i][0] = (uint16_t)((val + 4) >> 3);
        for (int k = 1; k < 12; k++) {
            val = val * 106 / 100;
            fnum_notes[i][k] = (uint16_t)((val + 4) >> 3);
        }
    }
    fnum_ready = 1;
}

int adlib_bank_load(AdlibBank *bank, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)len);
    if (!d || fread(d, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(d); return -1; }
    fclose(f);

    int n = d[2] | d[3] << 8;
    int defs = d[4] | d[5] << 8;
    if (len < defs + n * ADLIB_PARAMS * 2) { free(d); return -1; }
    bank->count = n;
    bank->timbres = calloc((size_t)n, sizeof(AdlibTimbre));
    for (int i = 0; i < n; i++) {
        memcpy(bank->timbres[i].name, d + 6 + 9 * i, 9);
        bank->timbres[i].name[8] = 0;
        const uint8_t *p = d + defs + i * ADLIB_PARAMS * 2;
        for (int k = 0; k < ADLIB_PARAMS; k++)
            bank->timbres[i].p[k] = (int16_t)(p[2 * k] | p[2 * k + 1] << 8);
    }
    free(d);
    return 0;
}

void adlib_bank_free(AdlibBank *bank)
{
    free(bank->timbres);
    bank->timbres = NULL;
    bank->count = 0;
}

int adlib_bank_find(const AdlibBank *bank, const char *name)
{
    for (int i = 0; i < bank->count; i++)
        if (strcmp(bank->timbres[i].name, name) == 0) return i;
    return -1;
}

static void wr(Adlib *a, int reg, int v) { a->write(a->ctx, (uint16_t)reg, (uint8_t)v); }

static void write_bd(Adlib *a)      /* AM/VIB depth are always 0 in Street Rod 2 */
{
    wr(a, 0xBD, (a->percussive ? 0x20 : 0) | a->perc_bits);
}

/* SndSetAllPrm: write every register belonging to one slot. */
static void write_slot(Adlib *a, int slot)
{
    const uint8_t *p = a->slot_param[slot];
    int off = offset_slot[slot];
    write_bd(a);
    wr(a, 0x08, 0);
    int t = 63 - (p[P_TL] & 0x3f);
    t = (t * a->slot_volume[slot] * 2 + 127) / 254;
    wr(a, 0x40 + off, (p[P_KSL] << 6) | (63 - t));
    if (!oper_slot[slot])
        wr(a, 0xC0 + chan_slot[slot], (p[P_FB] << 1) | (p[P_FM] ? 0 : 1));
    wr(a, 0x60 + off, (p[P_AR] << 4) | (p[P_DR] & 15));
    wr(a, 0x80 + off, (p[P_SL] << 4) | (p[P_RR] & 15));
    wr(a, 0x20 + off, (p[P_AM] ? 0x80 : 0) | (p[P_VIB] ? 0x40 : 0) | (p[P_EG] ? 0x20 : 0) |
                      (p[P_KSR] ? 0x10 : 0) | (p[P_MULTI] & 15));
    wr(a, 0xE0 + off, p[P_WAVE] & 3);
}

static void set_slot_params(Adlib *a, int slot, const int16_t *op, int wave)
{
    for (int k = 0; k < 13; k++) a->slot_param[slot][k] = (uint8_t)op[k];
    a->slot_param[slot][P_WAVE] = (uint8_t)(wave & 3);
    write_slot(a, slot);
}

/* SetFreq: `pitch` is already in chip range (MIDI note - 12). */
static void set_freq(Adlib *a, int voice, int pitch, int key_on)
{
    a->note_pitch[voice] = (uint8_t)pitch;
    a->key_on[voice] = (uint8_t)key_on;
    pitch += a->half_tone_offset[voice];
    if (pitch > 95) pitch = 95;
    if (pitch < 0) pitch = 0;
    unsigned fn = a->fnum_ptr[voice][pitch % 12];
    wr(a, 0xA0 + voice, fn & 0xff);
    wr(a, 0xB0 + voice, ((pitch / 12) << 2) | (key_on ? 0x20 : 0) | ((fn >> 8) & 3));
}

static void key_off_channel(Adlib *a, int chan)
{
    wr(a, 0xA0 + chan, 0);
    wr(a, 0xB0 + chan, 0);
}

void adlib_init(Adlib *a, AdlibWriteFn write, void *ctx)
{
    if (!fnum_ready) init_fnums();
    memset(a, 0, sizeof *a);
    a->write = write;
    a->ctx = ctx;
    for (int v = 0; v < ADLIB_VOICES; v++) a->fnum_ptr[v] = fnum_notes[0];
    a->old_t1 = -1;
    a->old_fnum_ptr = fnum_notes[0];
    adlib_set_mode(a, 1);
    adlib_set_pitch_range(a, 1);
    for (int c = 0; c < 9; c++) key_off_channel(a, c);
    for (int s = 0; s < ADLIB_SLOTS; s++) wr(a, 0xE0 + offset_slot[s], 0);
    wr(a, 0x01, 0x20);      /* enable wave select */
    for (int s = 0; s < ADLIB_SLOTS; s++) a->slot_volume[s] = 127;
}

void adlib_set_mode(Adlib *a, int percussive)
{
    a->percussive = percussive;
    a->perc_bits = 0;
    write_bd(a);
    if (percussive) {
        key_off_channel(a, 6);
        key_off_channel(a, 7);
        key_off_channel(a, 8);
        set_freq(a, ADLIB_TOM, TOM_PITCH, 0);
        set_freq(a, ADLIB_SD, TOM_PITCH + TOM_TO_SD, 0);
    }
}

void adlib_set_pitch_range(Adlib *a, int semitones)
{
    if (semitones > 12) semitones = 12;
    if (semitones < 1) semitones = 1;
    a->pitch_range_step = semitones * NR_STEP_PITCH;
}

void adlib_set_timbre(Adlib *a, int voice, const int16_t *p)
{
    const int16_t *op0 = p, *op1 = p + 13;
    if (a->percussive && voice >= ADLIB_BD) {
        if (voice == ADLIB_BD) {
            set_slot_params(a, slot_perc[0][0], op0, p[26]);
            set_slot_params(a, slot_perc[0][1], op1, p[27]);
        } else {
            set_slot_params(a, slot_perc[voice - ADLIB_BD][0], op0, p[26]);
        }
    } else {
        set_slot_params(a, slot_voice[voice][0], op0, p[26]);
        set_slot_params(a, slot_voice[voice][1], op1, p[27]);
    }
}

void adlib_set_volume(Adlib *a, int voice, int volume)
{
    int slot;
    if (a->percussive && voice >= ADLIB_BD)
        slot = slot_perc[voice - ADLIB_BD][voice == ADLIB_BD ? 1 : 0];
    else
        slot = slot_voice[voice][1];
    if (volume > 127) volume = 127;
    a->slot_volume[slot] = (uint8_t)volume;
    const uint8_t *p = a->slot_param[slot];
    int t = 63 - (p[P_TL] & 0x3f);
    t = (t * volume * 2 + 127) / 254;
    wr(a, 0x40 + offset_slot[slot], (p[P_KSL] << 6) | (63 - t));
}

/* ChangePitch: convert a 14-bit bend into a half-tone offset plus a fine F-number table. */
static void change_pitch(Adlib *a, int voice, int bend)
{
    long t1 = (long)(bend - MID_PITCH) * a->pitch_range_step;
    if (t1 == a->old_t1) {
        a->fnum_ptr[voice] = a->old_fnum_ptr;
        a->half_tone_offset[voice] = a->old_half_tone;
        return;
    }
    long t2 = t1 / MID_PITCH;
    int delta;
    if (t2 < 0) {
        long t3 = (NR_STEP_PITCH - 1 - t2) / -NR_STEP_PITCH;   /* rounds toward zero, like MSC idiv */
        a->half_tone_offset[voice] = (int)t3;
        delta = (int)((-t2) % NR_STEP_PITCH);
        if (delta) delta = NR_STEP_PITCH - delta;
    } else {
        a->half_tone_offset[voice] = (int)(t2 / NR_STEP_PITCH);
        delta = (int)(t2 % NR_STEP_PITCH);
    }
    a->old_half_tone = a->half_tone_offset[voice];
    a->fnum_ptr[voice] = a->old_fnum_ptr = fnum_notes[delta];
    a->old_t1 = t1;
}

void adlib_set_pitch_bend(Adlib *a, int voice, int bend)
{
    if (a->percussive && voice > ADLIB_BD) return;
    if (bend > 0x3fff) bend = 0x3fff;
    change_pitch(a, voice, bend);
    set_freq(a, voice, a->note_pitch[voice], a->key_on[voice]);
}

void adlib_note_on(Adlib *a, int voice, int note)
{
    int pitch = note - 12;
    if (pitch < 0) pitch = 0;
    if (a->percussive && voice >= ADLIB_BD) {
        if (voice == ADLIB_BD) {
            set_freq(a, ADLIB_BD, pitch, 0);
        } else if (voice == ADLIB_TOM) {
            set_freq(a, ADLIB_TOM, pitch, 0);
            set_freq(a, ADLIB_SD, pitch + TOM_TO_SD, 0);
        }
        a->perc_bits |= perc_mask[voice - ADLIB_BD];
        write_bd(a);
    } else {
        set_freq(a, voice, pitch, 1);
    }
}

void adlib_note_off(Adlib *a, int voice)
{
    if (a->percussive && voice >= ADLIB_BD) {
        a->perc_bits &= (uint8_t)~perc_mask[voice - ADLIB_BD];
        write_bd(a);
    } else {
        set_freq(a, voice, a->note_pitch[voice], 0);
    }
}

void adlib_silence(Adlib *a)
{
    for (int v = 0; v < ADLIB_VOICES; v++) adlib_note_off(a, v);
}
