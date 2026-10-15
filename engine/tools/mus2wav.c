/* Render a Street Rod 2 .MUS song with its .SND timbre bank to a WAV file.
 * usage: mus2wav <song.mus> <bank.snd> <out.wav> */
#include <stdio.h>
#include <stdlib.h>

#include "../src/audio/mus.h"
#include "../third_party/nuked_opl3/opl3.h"

#define RATE 44100

static void opl_write(void *ctx, uint16_t reg, uint8_t val) { OPL3_WriteRegBuffered((opl3_chip *)ctx, reg, val); }

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s song.mus bank.snd out.wav\n", argv[0]);
        return 1;
    }
    MusSong song;
    AdlibBank bank;
    if (mus_load(&song, argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
    if (adlib_bank_load(&bank, argv[2])) { fprintf(stderr, "cannot load %s\n", argv[2]); return 1; }

    static opl3_chip chip;
    OPL3_Reset(&chip, RATE);
    Adlib adlib;
    adlib_init(&adlib, opl_write, &chip);
    MusPlayer player;
    mus_start(&player, &song, &adlib, &bank, NULL, NULL);

    FILE *out = fopen(argv[3], "wb");
    if (!out) return 1;
    fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, out);
    put32(out, 16); put16(out, 1); put16(out, 2); put32(out, RATE); put32(out, RATE * 4); put16(out, 4); put16(out, 16);
    fwrite("data\0\0\0\0", 1, 8, out);

    uint32_t frames = 0;
    double acc = 0;
    int tail = RATE * 2;            /* let the last notes ring out */
    int16_t buf[2];
    for (;;) {
        int playing = 1;
        acc += mus_ticks_per_second(&player) / RATE;
        while (acc >= 1.0) {
            acc -= 1.0;
            playing = mus_tick(&player);
        }
        if (player.finished && tail-- <= 0) break;
        (void)playing;
        OPL3_GenerateResampled(&chip, buf);
        fwrite(buf, 2, 2, out);
        frames++;
        if (frames > (uint32_t)RATE * 600) break;   /* safety: 10 minutes */
    }
    uint32_t bytes = frames * 4;
    fseek(out, 4, SEEK_SET); put32(out, 36 + bytes);
    fseek(out, 40, SEEK_SET); put32(out, bytes);
    fclose(out);
    printf("%s: %.1f s\n", argv[3], frames / (double)RATE);
    mus_free(&song);
    adlib_bank_free(&bank);
    return 0;
}
