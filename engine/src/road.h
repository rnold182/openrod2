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
    int race;               /* DS:add6: RACE_TOWN, RACE_DRAG, RACE_MULHOLLAND, RACE_AQUEDUCT */
    int opp_seg;            /* a9c6: the opponent is drawn when the painter reaches its segment */
    int fall;               /* DS:7b9c: going over the edge, -1 left / 1 right */
    void (*draw_opponent)(void);
    int speed;              /* a100: your speed (the Aqueduct's middle stripe slows you) */
    int autopilot;          /* DS:9730 / 1f82: demo or autodrive (no sliding down the Aqueduct's walls) */
    /* The rear-view mirror (1dcb:0ce4 / 5aee, Aqueduct 1b02 / 9ce8). */
    void (*draw_opponent_mirror)(void);   /* af98 for kinds 9..12, drawn with road_mproj_* */
    int starter_gone;       /* DS:a7ac clear: the starter has left the window and shows in the mirror */
    int starter_seg;        /* DS:9e6a: where she stands */
    int sign;               /* town: the destination's sign sheet (DS:7e1a[1bc8], 2032..2034) */
} RoadView;

/* What the last frame found in front of the car (DS:bb88 / add4: -1 left, 1 right, 0 none). */
typedef struct { int object, bump; } RoadHits;
extern RoadHits g_road_hits;

/* af98's floor for the shrunk rear views: the ground row of slice `i` (DS:9ef0), or for i >= 15
 * the highest row of road seen (975e). From the last road_draw. */
int road_ground_row(int i);

/* 1dcb:062c / 0684: a point `d` units ahead (d = distance in 1/8 segment + 4) at lateral position
 * `lat` and height `h`, `turn` / `rise` being the heading and height differences to it. */
int road_proj_x(const RoadView *rv, int d, int lat, int turn);
int road_proj_y(const RoadView *rv, int d, int h, int rise);

/* 1dcb:06ba / 070e: the same for the rear-view mirror (a quarter of the scale, centred at x 0x118,
 * y 0x24); `turn` / `rise` are taken the other way round by the callers. */
int road_mproj_x(const RoadView *rv, int d, int lat, int turn);
int road_mproj_y(const RoadView *rv, int d, int h, int rise);
/* A picture scaled to w x h, the middle of its bottom edge at (cx, by), turned `angle` degrees,
 * clipped to the part being drawn (the windshield or the mirror). */
void road_sprite(const Image *im, int cx, int by, int w, int h, int angle);

/* 1dcb:07e4 + 3e66 + 6a32: sky, hills, road and everything beside it, back to front, then the
 * rear-view mirror (0ce4 + 5aee). */
void road_draw(Game *g, const RoadView *rv);

/* ------------------------------------------------------------------ the Aqueduct (add6 8) */

/* A concrete channel with sloping walls; its own table (DS:4e62 in the far segment, 1dcb:33ac),
 * one word per segment: 1 a joint across the channel, 2 a crossing over the middle stripe, 4 / 8
 * (0x100 / 0x200 bigger) things on the left / right wall, 0x10 a block in the middle, 0x20 / 0x40
 * bushes on the walls, 0x80 bridge pillars, 0x400 the finish line. */
#define AQ_SEGS 0x172
extern uint16_t g_aqueduct[AQ_SEGS];

/* The view's state from one frame to the next (1dcb:18ec / a094 / 9d9c / 9e4e / 9f08 / 9f9a). */
typedef struct {
    int bank;               /* 9e70: the view tilts on the walls (degrees) */
    int roll;               /* b8bc: added to it while the car rolls over (1dcb:a1c0) */
    int lift;               /* 9a56: the picture moved down (1dcb:0684) */
    int pitch, pitch_prev, bank_prev;   /* b7b0, 9e68, b868 */
    int side_l, side_r, prev_l, prev_r; /* 9758, 9868, a7e8, aaf2: the car's edges against the stripe */
    int floor, left, right, top, wall_l, wall_r;   /* 985e, a2b8, a7a6, a5fa, 9a5a, 9b14 */
    int s_floor, s_left, s_right, s_top;           /* aa9c, b8be, bb8a, b8e6: their last values */
    int hits;               /* a7d2: what the car runs into, found while drawing (862a / 8674) */
    int slide, slow;        /* the frame's a564 += slide (sliding down a wall) and a100 -= slow */
    int abs_bank, variant;  /* b190, ad68: which of the 7 pre-turned shapes to draw */
    int car_l, car_r;       /* 9ac8 / 9a58: the car's left and right sides (a564 - 0x6e, + 0x208) */
} AqView;
extern AqView g_aq;

void road_aq_reset(void);                              /* the part of 1dcb:8840 that clears it */
/* 1dcb:a2d8: would the car run into the block or the pillars of `flags` just ahead (8674 bits)? */
int  road_aq_ahead(const RoadView *rv, unsigned flags);
/* 1dcb:0379: turn a point of the Aqueduct view with its tilt (after a frame was drawn). */
void road_aq_rotate(int x, int y, int *ox, int *oy);
int  road_aq_sin(int deg);                             /* 1dcb:02b4: sin * 255 */

#endif
