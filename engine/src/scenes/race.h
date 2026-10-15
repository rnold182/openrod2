/* Street races. Only the drag race is rebuilt so far. */
#ifndef SR_RACE_H
#define SR_RACE_H

#include "../game.h"

/* Start a race against `rival` (stake 0..5 as in dialogs 17/18, amount in dollars). */
void race_start(Game *g, int rival, int race_type, int stake, int amount);
/* Testing: a driver that holds the gas and shifts at 5000 rpm. */
void race_autodrive(int on);

#endif
