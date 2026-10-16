/* Street races. Only the drag race is rebuilt so far. */
#ifndef SR_RACE_H
#define SR_RACE_H

#include "../game.h"

/* Start a race against `rival` (stake 0..5 as in dialogs 17/18, amount in dollars). */
void race_start(Game *g, int rival, int race_type, int stake, int amount);
/* Testing: a driver that holds the gas and shifts at 5000 rpm (1); with 1 it also steers along the
 * road and can't crash, 2 leaves the steering to you. */
void race_autodrive(int on);
/* The race screen hides the mouse cursor except over the crash dialog. */
int  race_shows_cursor(void);

#endif
