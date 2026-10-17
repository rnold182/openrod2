/* Street races (drag, Mulholland Drive, the Aqueduct) and the drive through town. */
#ifndef SR_RACE_H
#define SR_RACE_H

#include "../game.h"

/* Start a race against `rival` (stake 0..5 as in dialogs 17/18, amount in dollars). */
void race_start(Game *g, int rival, int race_type, int stake, int amount);
/* 1001:8c68: drive through town to `dest` (DS:1bc8: 1 home, 2 Burger's, 3 the gas station,
 * 4 Grudge Night) in the race screen, then go on to `then`. Uses 2 gallons. */
void town_drive(Game *g, int dest, const Scene *then);
/* Testing: a driver that holds the gas and shifts at 5000 rpm (1); with 1 it also steers along the
 * road and can't crash, 2 leaves the steering to you. */
void race_autodrive(int on);
/* Testing: the next Mulholland race has the police waiting. */
void race_force_police(void);
/* The race screen hides the mouse cursor except over the crash dialog. */
int  race_shows_cursor(void);

#endif
