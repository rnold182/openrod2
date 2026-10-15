#include "anim.h"

#include "options.h"
#include "sound.h"

#define ANIM_MAX     24
#define PIXELS_PER_S 110.0      /* one pixel per step of 2a21:a2f2 on a period PC */
#define FRAME_PIXELS 3          /* a turning bolt shows its other frame every few pixels */

enum { STEP_MOVE, STEP_CALL, STEP_MESSAGE };

typedef struct {
    int     kind;
    AnimMove m;
    AnimFn  fn;
    int     arg;
} Step;

static Step queue[ANIM_MAX];
static int head, count;
static int started;
static Uint64 start_ms;

void anim_reset(void)
{
    if (count && started && queue[head].kind == STEP_MOVE && queue[head].m.hum) sound_hum(SFX_HUM_OFF);
    head = count = started = 0;
}

int anim_busy(void) { return count > 0; }

static Step *push(int kind)
{
    if (count >= ANIM_MAX) return NULL;
    Step *s = &queue[(head + count++) % ANIM_MAX];
    SDL_zerop(s);
    s->kind = kind;
    return s;
}

void anim_move(const AnimMove *m) { Step *s = push(STEP_MOVE); if (s) s->m = *m; }
void anim_call(AnimFn fn, int arg) { Step *s = push(STEP_CALL); if (s) { s->fn = fn; s->arg = arg; } }
void anim_message(int n) { Step *s = push(STEP_MESSAGE); if (s) s->arg = n; }

static void pop(void) { head = (head + 1) % ANIM_MAX; count--; started = 0; }

static void draw(Game *g, const AnimMove *m, int y, int frame)
{
    const Image *im = game_picture(g, m->pic);
    if (!im) return;
    int w = m->w > 0 ? m->w : im->w, h = m->h > 0 ? m->h : im->h;
    int sx = frame && m->alt_sx >= 0 ? m->alt_sx : m->sx, sy = m->sy;
    int top = y < m->clip_top ? m->clip_top - y : 0;            /* rows cut off by the band */
    int bottom = y + h > m->clip_bottom ? y + h - m->clip_bottom : 0;
    if (top + bottom >= h) return;
    video_blit_part(&g->video, im, sx, sy + top, w, h - top - bottom, m->x, y + top);
}

void anim_run(Game *g)
{
    while (count) {
        Step *s = &queue[head];
        if (s->kind == STEP_CALL) { pop(); s->fn(g, s->arg); continue; }
        if (s->kind == STEP_MESSAGE) { pop(); game_message(g, s->arg); continue; }

        AnimMove *m = &s->m;
        if (!started) {
            started = 1;
            start_ms = SDL_GetTicks();
            if (m->hum && opt_animations) sound_hum(m->hum);
        }
        int dist = m->y1 > m->y0 ? m->y1 - m->y0 : m->y0 - m->y1;
        int moved = opt_animations ? (int)((SDL_GetTicks() - start_ms) * PIXELS_PER_S * opt_anim_speed / 100000.0) : dist;
        if (moved < dist) {
            int y = m->y0 + (m->y1 > m->y0 ? moved : -moved);
            draw(g, m, y, (moved / FRAME_PIXELS) & 1);
            return;
        }
        if (started == 1) {                                      /* arrived */
            started = 2;
            if (m->hum && opt_animations) sound_hum(SFX_HUM_OFF);
            if (m->end_sfx >= 0) sound_sfx(m->end_sfx);
            if (m->message) game_message(g, m->message);
        }
        if (m->message && g->msg) { draw(g, m, m->y1, 0); return; }
        pop();
    }
}
