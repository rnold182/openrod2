/* Workshop animations: the original's sprite mover (vector DS:a0da = 2a21:a2f2) slides a picture
 * up or down one pixel at a time inside a clip band, optionally flipping between two frames
 * (a turning bolt). Scenes queue steps; while any are pending the scene ignores input, as SR.EXE
 * did (it ran the moves before going back to its input loop). */
#ifndef SR_ANIM_H
#define SR_ANIM_H

#include "game.h"

typedef void (*AnimFn)(Game *g, int arg);

typedef struct {
    int pic;                    /* picture id */
    int sx, sy, w, h;           /* source rectangle (w <= 0: whole picture) */
    int alt_sx;                 /* second frame's x, -1 if none */
    int x, y0, y1;              /* moves from y0 to y1 (top of the picture) */
    int clip_top, clip_bottom;  /* drawn only on rows clip_top .. clip_bottom-1 */
    int hum;                    /* SFX_HUM_* while moving */
    int end_sfx;                /* SFX_* when it arrives, -1 none */
    int message;                /* on arrival show this message and stay drawn until it is gone */
} AnimMove;

void anim_reset(void);
int  anim_busy(void);
void anim_move(const AnimMove *m);
void anim_call(AnimFn fn, int arg);      /* runs when the queue reaches it */
void anim_message(int n);                /* shows message n when reached */
/* Advance and draw; call once per frame after the scene's background. */
void anim_run(Game *g);

#endif
