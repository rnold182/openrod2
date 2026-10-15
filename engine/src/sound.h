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
void sound_record(const char *wav_path); /* testing: also write all generated audio to a WAV */

#endif
