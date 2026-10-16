/* The 21 street rivals and The King (SR2_DATA block 0 -> DS:ab7e, 22 records of 18 bytes, kept in
 * Player.rivals so they are saved exactly as the original saves them).
 * Record: +0/+1/+2 skills, +4 portrait picture, +6 name, +8 car model, +0xa colour, +0xb met
 * (0 never, 1 met, -1 out of the game), +0xc races run, +0xd tuning (1/2 bumpers stripped, 4 roof
 * chopped, 0x20 blower), +0xf/+0x10 reputation (compare Player header +0x28/+0x29). */
#ifndef SR_RIVALS_H
#define SR_RIVALS_H

#include "car.h"

#define KING 21

enum { RACE_DRAG = 1, RACE_AQUEDUCT = 8, RACE_MULHOLLAND = 0x10 };   /* DS:add6 */

int  rival_get(const Player *p, int r, int off);           /* signed byte */
void rival_set(Player *p, int r, int off, int v);
int  rival_model(const Player *p, int r);
const char *rival_name(int r);

/* Player block bytes +0x28/+0x29 (DS:aa00/aa01): the two reputation ratings, 1..8. */
int  player_rep(const Player *p, int which);
/* Counters at DS:479c: 0 races, 1 drag wins, 2 Mulholland wins, 3 Aqueduct wins, 4 Grudge Night, 5 King stage. */
int  player_counter(const Player *p, int k);
void player_set_counter(Player *p, int k, int v);

void rivals_new_game(Player *p);                            /* 1001:534c */
int  rivals_progress(const Player *p);                      /* 1001:975e */
int  rivals_king_chance(const Player *p, int current);      /* 1001:9802 */
/* 1001:999a: who pulls up next; KING when the dice say so or nobody suits. */
int  rivals_pick_arrival(const Player *p, int king_chance);
void rivals_meet(Player *p, int r);                         /* 1001:9a02 (the bookkeeping part) */
int  rivals_accepts_bet(const Player *p, int r, int stake); /* 1001:9c8e, stake 0..5 */
extern int rivals_always_agree;                             /* cheat "agree" */

int  model_quality(const Player *p, int model);             /* 1001:c866, model < 0 = your car */
int  model_max_speed(const Player *p, int model);           /* 1001:c6f6, model < 0 = your car */

#endif
