#include "sound.h"

#include <stdio.h>
#include <SDL3/SDL.h>

#include "../third_party/nuked_opl3/opl3.h"

#define RATE 44100

const int PLAYLIST_MAIN[]    = { SONG_MSRBEG, SONG_MSRBEG, SONG_PAUSE, SONG_MSR2, SONG_PAUSE, SONG_MSR24, SONG_MSR24, -1 };
const int PLAYLIST_SPECIAL[] = { SONG_MSR21, SONG_MSR21, -2 };

static const char *song_files[SONG_SLOTS] = { NULL, "MSRBEG.MUS", "MSR2.MUS", "MSR24.MUS", "MSR21.MUS", "PAUSE.MUS" };

static struct {
    SDL_AudioStream *stream;
    SDL_Mutex       *lock;
    opl3_chip        chip;
    Adlib            adlib;
    AdlibBank        bank;
    MusSong          songs[SONG_SLOTS];
    MusPlayer        player;
    const int       *list_start, *list_pos;
    int              playing;
    int              muted;
    double           tick_acc;
    FILE            *rec;
    uint32_t         rec_bytes;
} S;

static void opl_write(void *ctx, uint16_t reg, uint8_t val) { OPL3_WriteRegBuffered((opl3_chip *)ctx, reg, val); }

static const MusSong *song_at(const int *entry)
{
    int slot = *entry;
    return (slot > 0 && slot < SONG_SLOTS && S.songs[slot].data) ? &S.songs[slot] : NULL;
}

/* Advance the playlist exactly like PLAY.C: -2 stops, -1 restarts the list. */
static const MusSong *next_in_playlist(void *ctx)
{
    (void)ctx;
    if (!S.list_pos) return NULL;
    S.list_pos++;
    if (*S.list_pos == -2) return NULL;
    if (*S.list_pos == -1) S.list_pos = S.list_start;
    return song_at(S.list_pos);
}

static void SDLCALL feed(void *userdata, SDL_AudioStream *stream, int additional, int total)
{
    (void)userdata; (void)total;
    int16_t buf[1024 * 2];
    int frames = additional / 4;
    SDL_LockMutex(S.lock);
    while (frames > 0) {
        int n = frames > 1024 ? 1024 : frames;
        for (int i = 0; i < n; i++) {
            if (S.playing) {
                S.tick_acc += mus_ticks_per_second(&S.player) / RATE;
                while (S.tick_acc >= 1.0) {
                    S.tick_acc -= 1.0;
                    if (!mus_tick(&S.player)) S.playing = 0;
                }
            }
            OPL3_GenerateResampled(&S.chip, buf + 2 * i);
        }
        SDL_PutAudioStreamData(stream, buf, n * 4);
        if (S.rec) {
            fwrite(buf, 4, (size_t)n, S.rec);
            S.rec_bytes += (uint32_t)n * 4;
        }
        frames -= n;
    }
    SDL_UnlockMutex(S.lock);
}

int sound_init(const char *data_dir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/MSR.SND", data_dir);
    if (adlib_bank_load(&S.bank, path)) {
        SDL_Log("cannot load %s", path);
        return -1;
    }
    for (int i = 1; i < SONG_SLOTS; i++) {
        snprintf(path, sizeof path, "%s/%s", data_dir, song_files[i]);
        if (mus_load(&S.songs[i], path)) SDL_Log("cannot load %s", path);
    }
    OPL3_Reset(&S.chip, RATE);
    adlib_init(&S.adlib, opl_write, &S.chip);
    S.lock = SDL_CreateMutex();

    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, RATE };
    S.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
    if (!S.stream) {
        SDL_Log("audio unavailable: %s", SDL_GetError());
        return -1;
    }
    SDL_ResumeAudioStreamDevice(S.stream);
    return 0;
}

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

void sound_record(const char *wav_path)
{
    S.rec = fopen(wav_path, "wb");
    if (!S.rec) return;
    fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, S.rec);
    put32(S.rec, 16); put16(S.rec, 1); put16(S.rec, 2); put32(S.rec, RATE); put32(S.rec, RATE * 4);
    put16(S.rec, 4); put16(S.rec, 16);
    fwrite("data\0\0\0\0", 1, 8, S.rec);
}

void sound_shutdown(void)
{
    if (S.stream) SDL_DestroyAudioStream(S.stream);
    if (S.rec) {
        fseek(S.rec, 4, SEEK_SET); put32(S.rec, 36 + S.rec_bytes);
        fseek(S.rec, 40, SEEK_SET); put32(S.rec, S.rec_bytes);
        fclose(S.rec);
        S.rec = NULL;
    }
    for (int i = 1; i < SONG_SLOTS; i++) mus_free(&S.songs[i]);
    adlib_bank_free(&S.bank);
    if (S.lock) SDL_DestroyMutex(S.lock);
}

static void start_list(const int *list)
{
    S.list_start = S.list_pos = list;
    const MusSong *s = song_at(list);
    if (!s) { S.playing = 0; return; }
    mus_start(&S.player, s, &S.adlib, &S.bank, next_in_playlist, NULL);
    S.tick_acc = 0;
    S.playing = 1;
}

void sound_play_playlist(const int *list)
{
    if (!S.lock) return;
    SDL_LockMutex(S.lock);
    if (S.muted) S.list_start = S.list_pos = list;   /* remembered for when music is re-enabled */
    else start_list(list);
    SDL_UnlockMutex(S.lock);
}

void sound_stop(void)
{
    if (!S.lock) return;
    SDL_LockMutex(S.lock);
    S.playing = 0;
    adlib_silence(&S.adlib);
    SDL_UnlockMutex(S.lock);
}

void sound_toggle_music(void)
{
    if (!S.lock) return;
    SDL_LockMutex(S.lock);
    S.muted = !S.muted;
    if (S.muted) {
        S.playing = 0;
        adlib_silence(&S.adlib);
    } else if (S.list_start) {
        start_list(S.list_start);
    }
    SDL_UnlockMutex(S.lock);
}
