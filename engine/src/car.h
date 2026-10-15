/* Player-owned cars and parts (structures follow SR.EXE's car record built in 1001:42aa). */
#ifndef SR_CAR_H
#define SR_CAR_H

#include <stdint.h>

#include "video.h"

struct Game;

typedef struct {
    int16_t part;               /* index into g_parts, -1 = none */
    int16_t value;              /* what it is worth */
    int16_t wear;               /* condition counter (engines/transmissions/tires wear out) */
} PartInst;

typedef struct {
    int      model;
    int16_t  price;             /* price paid */
    uint8_t  colour;
    uint8_t  cls;
    PartInst engine, trans, intake, carb[3], tires, exhaust, muffler, diff;
    uint8_t  tune[8];           /* "bolt" settings (car+0x14..0x1b) */
    int8_t   wires;             /* ignition wires: 1 connected, -1 disconnected, 0 no engine */
    int8_t   timing;            /* ignition advance/retard, -5..3 */
    int16_t  fuel;              /* gallons? starts at 40 */
    uint16_t body;              /* body mods (car+0x26): roof chopped, bumpers stripped */
} Car;

#define BODY_ROOF_CHOPPED   0x2000   /* car+0x27 bit 0x20 */
#define BODY_REAR_STRIPPED  0x4000   /* car+0x27 bit 0x40 */
#define BODY_FRONT_STRIPPED 0x8000   /* car+0x27 bit 0x80 */

#define MAX_CARS  16
#define MAX_PARTS 181

typedef struct {
    char     name[16];
    long     money;
    Car      cars[MAX_CARS];
    int      car_count;
    int      current;           /* index of the car in the garage, -1 = none */
    PartInst parts[MAX_PARTS];  /* spare parts in the garage */
    int      part_count;
    long     time_actions;      /* DS:1f6a: each action advances the clock */
    double   time_ticks;        /* DS:1f6e-1f72: real time spent playing, in 72.8 Hz ticks */

    /* Classified ads state (DS:9b40..9b88). */
    int      ads_week;          /* week of the last issue read, -1 = never */
    uint8_t  ads[26];           /* ad slots of the current issue (0..41), 0x80 = sold, 0xFF = end */
    uint8_t  ad_cooldown[42];
    int      ads_available;

    /* Saved as the original keeps them, decoded once the street and races are rebuilt. */
    uint8_t  header_raw[0x38];  /* DS:a9d8 player block (name and money are taken from above) */
    uint8_t  misc_479c[12];     /* DS:479c */
    uint8_t  rivals[396];       /* DS:ab7e, starts as SR2_DATA's rivals block */
    int16_t  word_7dfe;         /* DS:7dfe, 100 in a new game (1dcb:24d8) */
    uint8_t  ads_tail[0x38];    /* DS:9b88..9bbf, the rest of the newspaper block */
} Player;

void player_new(Player *p, const char *name);
int  player_day(const Player *p);                 /* 0 = June 16 .. 91 */
void player_date(const Player *p, int *month, int *day_of_month);   /* month 0 = June */
void day_date(int day, int *month, int *day_of_month);

/* Create a car of `model` as the original does; `used` gives the worn engine/transmission variant. */
Car *player_add_car(Player *p, int model, int price, int used);
void car_make_stock(Car *c, int model, int price, int used);   /* the factory car, as 42aa builds it */
int  player_add_part(Player *p, int part, int price);
int  player_add_spare(Player *p, PartInst part);     /* to the front of the spares list, like SR.EXE */
void player_remove_spare(Player *p, int index);
/* 1001:7a44: engine, wires, manifold, carbs and their bolts all tight; `street` also needs
 * transmission and tires and the two under-car bolts. */
int  car_runnable(const Car *c, int street);

/* Bolts (1001:80a8 / 75b8): tightness 0 (out) .. 3; mode 0 out, 1 tightening, 2 loosening.
 * One click: out -> loose; tightening 1->2->3, then switches to loosening; loosening 3->2->out.
 * Returns 1 when the bolt has just come out. */
int  bolt_mode_of(int state);
/* 1001:c02c: the car's power figure; 1001:c6f6: top speed in mph (max 130). */
long car_power(const Car *c);
int  car_max_speed(const Car *c);
/* 2a21:4e0c: "(new)", "(NN% worn)", or "" for parts without a wear counter. */
void part_wear_text(const PartInst *p, char out[16]);
int  bolt_turn(uint8_t *state, int *mode);
Car *player_car(Player *p);

/* Draw a car side view with its bottom edge on `bottom`; returns the x used (CAR_CENTRED centres it). */
int  car_draw(Video *v, struct Game *g, const Car *car, int model, int x, int bottom);
#define CAR_CENTRED (-32768)
/* The same with explicit body patches (BODY_*), blower, tire grade (-1 = none) and wheel frame 0..3;
 * x may be off screen (cars driving in and out). */
int  car_draw_ex(Video *v, struct Game *g, int model, int body_flags, int blower, int tire_grade, int wheel_frame,
                 int x, int bottom);
int  car_body_width(struct Game *g, int model);
int  car_garage_x(int body_width);

#endif
