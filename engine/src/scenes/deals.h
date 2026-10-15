/* Garage deals that run over the garage picture: selling parts (1001:3f5a) and the cars list with
 * switching and selling (1001:4852, including the haggling). */
#ifndef SR_DEALS_H
#define SR_DEALS_H

#include "../game.h"

void deals_sell_parts(Game *g);
void deals_cars(Game *g);
int  deals_active(void);
void deals_key(Game *g, SDL_Scancode sc, SDL_Keycode k);
void deals_text(Game *g, const char *s);
void deals_click(Game *g, int x, int y);
void deals_mouse_move(Game *g, int x, int y);
void deals_draw(Game *g);
const char *deals_hover_text(void);

#endif
