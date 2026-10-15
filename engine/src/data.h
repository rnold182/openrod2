/* Static game data: car models and rivals from SR2_DATA, the parts catalogue and other tables from SR.EXE. */
#ifndef SR_DATA_H
#define SR_DATA_H

#include <stdint.h>

#define MODEL_COUNT 22          /* 21 for sale + The King's Shelby */
#define RIVAL_COUNT 22
#define PART_COUNT  64

enum { PT_ENGINE, PT_TRANSMISSION, PT_CARB, PT_INTAKE, PT_TIRES, PT_EXHAUST, PT_MUFFLER, PT_DIFF, PT_TYPES };
enum { MAKE_GM = 1, MAKE_FORD = 2, MAKE_CHRYSLER = 4, MAKE_ANY = 7 };

typedef struct {
    uint16_t price, used_price;     /* new-ish ad price and the cheaper worn variant */
    uint8_t  cls;                   /* +4 */
    uint8_t  colour;                /* +6, randomised 0..4 when the data is loaded (as in SR.EXE) */
    uint16_t spec1, spec2, spec3;   /* +8, +10, +18: bit fields selecting the factory parts */
    const char *ad;                 /* ad text: NUL-separated strings, '+' continues a line */
    uint16_t ad_lines;
    uint16_t picture;               /* side-view body picture */
    uint8_t  geo[12];               /* wheel x (front, rear), driver x/y, patch positions */
    uint16_t patch[5];              /* -, chopped roof, blower, front bumper, rear bumper pictures */
} CarModel;

typedef struct {
    uint16_t price;
    uint8_t  grade;                 /* low byte of the make/grade word */
    uint8_t  make;                  /* MAKE_* bits */
    uint8_t  type;                  /* PT_* */
    const char *ad;
    uint16_t ad_lines;
} PartInfo;

extern CarModel g_models[MODEL_COUNT];
extern PartInfo g_parts[PART_COUNT];
extern const char *g_rival_names[RIVAL_COUNT];

int data_load(const char *data_dir);    /* after exe_load() */

/* Ad text helpers: the first string of an ad is the item's name. */
const char *ad_next_string(const char *s);
int  ad_format_lines(const char *ad, int lines, long price, char out[][64], int max_lines);

uint8_t  colour_paint_reg(int colour);  /* DS:4d1e: dark shade (palette registers 6 and 12) */
uint8_t  colour_paint_light(int colour);/* DS:4d1f: light shade (palette register 7) */
uint8_t  colour_backdrop(int colour);   /* DS:4d1e+2: background used by "See it" */
uint16_t tire_picture(int tire_grade, int frame);   /* DS:4d30 */
int16_t  part_initial_condition(int type);          /* DS:478c */
int      carbs_for_intake_grade(int grade);         /* DS:4e6e */

#endif
