/* Sound output: an emulated OPL chip driven by the AdLib driver, streamed through SDL. */
#ifndef SR_SOUND_H
#define SR_SOUND_H

#include "audio/mus.h"

/* Song slots, numbered as in SR.EXE (MSR20.MUS is never used by the game). */
enum { SONG_MSRBEG = 1, SONG_MSR2, SONG_MSR24, SONG_MSR21, SONG_PAUSE, SONG_SLOTS };

/* Playlists from SR.EXE (DS:73a0 / DS:73b0): slot numbers, -1 = loop to start, -2 = stop. */
extern const int PLAYLIST_MAIN[];      /* started at boot, plays throughout the game */
extern const int PLAYLIST_SPECIAL[];   /* MSR21 twice, then stops */

int  sound_init(const char *data_dir);
void sound_shutdown(void);
void sound_play_playlist(const int *list);
void sound_stop(void);
void sound_toggle_music(void);         /* 'M' key in the original */
void sound_record(const char *wav_path);
/* Rebuild addition: turn the music (not the effect voices 3..5) down by `steps` x 0.75 dB; 0 restores it. */
void sound_music_duck(int steps);

/* Sound effects on the voices the songs leave free (3..5), as SR.EXE drives them through ADLIB.C. */
void sound_voice_timbre(int voice, const char *timbre, int volume);   /* 2c57:27e5 */
void sound_voice_volume(int voice, int volume);
void sound_voice_bend(int voice, int bend);                           /* 0..0x3fff */
void sound_voice_note_on(int voice, int note);
void sound_voice_note_off(int voice);
void sound_pitch_range(int semitones);

/* Workshop effects from SR.EXE's sound table (DS:73c2, driver 2c57:0ea4..12e0). */
enum {
    SFX_HUM_OFF,
    SFX_HUM_PART,   /* slot 10 (0f7c): voice 4 held while a part moves */
    SFX_HUM_BOLT,   /* slot 5 (0edc): voice 4 held while a bolt moves */
};
void sound_hum(int kind);
enum {
    SFX_CLINK,      /* slot 13 (113e): "turn", a random high note - a bolt lands */
    SFX_CLUNK,      /* slot 14 (118e): "turn" 0x3d - a part sits down */
    SFX_TURN,       /* slot 17 (12e0): "turn1" 0x10 - a bolt is turned */
    SFX_DROP,       /* slot 9 (0ea4): "hitcar" 0x2e - the gearbox comes down */
};
void sound_sfx(int sfx);
/* Slot 2 (0dcc): the paint gun, voice 5, held while SR.EXE repainted the garage - here for `ms`. */
void sound_spray(int ms); /* testing: also write all generated audio to a WAV */

#endif
