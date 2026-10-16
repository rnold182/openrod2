/* The race roads (SR.EXE segment 0dcb): the track tables built from the game's seed and the
 * windshield view drawn from them.
 *
 * Every road is a list of segments (0x3de of them); a race drives along part of it (the drag strip
 * is 200..0x17c, Mulholland Drive runs on past it). Per segment the original keeps, in its far
 * segment 0x3726, a word of road flags, the side flags left and right, the heading (curves are the
 * heading changing from one segment to the next), the height (hills) and the terrain height on
 * each side. 1dcb:2866 + 1dcb:24d8 build them when a game starts or is loaded, from the random
 * state saved in the player block (DS:a9f8) and one of 10 layouts (DS:7dfe). */
#ifndef SR_ROAD_H
#define SR_ROAD_H

#include <stdint.h>

#include "car.h"
#include "game.h"

#define ROAD_SEGS 0x3de

typedef struct {
    uint16_t left[ROAD_SEGS], right[ROAD_SEGS];   /* 27b6 / 2f72: what stands beside the road */
    int16_t  heading[ROAD_SEGS];                  /* 372e */
    uint16_t flags[ROAD_SEGS];                    /* 3eea */
    int16_t  height[ROAD_SEGS];                   /* 46a6 */
    int16_t  ground_l[ROAD_SEGS], ground_r[ROAD_SEGS];   /* 5146 / 5902: terrain beside the road */
    int16_t  object[ROAD_SEGS];                   /* 665a: lateral position of road objects */
} RoadTrack;

extern RoadTrack g_road;

/* Build the tables for this player's world (the load path: 2866(0) then 24d8(DS:7dfe)). Picks the
 * layout first if the game has none yet (DS:7dfe = 100, as 24d8(100) does). */
void road_build(Player *p);
/* A new game's world: a fresh random state (DS:a9f8) and layout (DS:7dfe). */
void road_new_world(Player *p);

/* The camera for one frame of the windshield view. */
typedef struct {
    int seg, sub;           /* DS:a55e, a560 (1/64 segment) */
    int x, h;               /* a564 lateral position, a566 eye height */
    int heading;            /* a56a */
    int race;               /* DS:add6: RACE_DRAG, RACE_MULHOLLAND */
    int opp_seg;            /* a9c6: the opponent is drawn when the painter reaches its segment */
    int fall;               /* DS:7b9c: going over the edge, -1 left / 1 right */
    void (*draw_opponent)(void);
} RoadView;

/* What the last frame found in front of the car (DS:bb88 / add4: -1 left, 1 right, 0 none). */
typedef struct { int object, bump; } RoadHits;
extern RoadHits g_road_hits;

/* 1dcb:062c / 0684: a point `d` units ahead (d = distance in 1/8 segment + 4) at lateral position
 * `lat` and height `h`, `turn` / `rise` being the heading and height differences to it. */
int road_proj_x(const RoadView *rv, int d, int lat, int turn);
int road_proj_y(const RoadView *rv, int d, int h, int rise);

/* 1dcb:07e4 + 3e66 + 6a32: sky, hills, road and everything beside it, back to front. */
void road_draw(Game *g, const RoadView *rv);

#endif
