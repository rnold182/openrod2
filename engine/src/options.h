/* Modern options (the future options menu). The originals' behaviour is what you get with every
 * option at its "classic" value; the defaults below are what the rebuild ships with. */
#ifndef SR_OPTIONS_H
#define SR_OPTIONS_H

/* ui_highlight / ui_highlight_colour live in ui/dialog.h. */
extern int opt_game_cursor;     /* 1: the game's own mouse cursors (wrench / hand), 0: the Windows arrow */
extern int opt_anim_speed;      /* percent of the default speed (slider: 25..400) */
extern int opt_animations;      /* 1: the original's animations (parts moving, car driving in...), 0: instant */
extern int opt_save_info;       /* 1: save/load lists show each save's file date and game day */
extern int opt_tune_music_duck; /* music turned down while tuning, in 0.75 dB steps (0 = as the original) */

#endif
