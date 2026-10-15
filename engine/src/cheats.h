/* Cheat console: F12 or ` opens it; type a command and press Enter. Also used by test scripts. */
#ifndef SR_CHEATS_H
#define SR_CHEATS_H

#include "game.h"

int  cheats_active(void);
void cheats_toggle(Game *g);
void cheats_key(Game *g, SDL_Keycode k);
void cheats_text(Game *g, const char *utf8);
void cheats_draw(Game *g);
/* Run one command line; the reply is shown in the console. */
void cheats_run(Game *g, const char *line);

#endif
