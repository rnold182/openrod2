#include "deals.h"

#include <string.h>

#include "../assets/exe.h"
#include "../car.h"
#include "../data.h"
#include "../ui/partlist.h"

enum {
    D_NONE,
    D_PARTS_LIST, D_PARTS_OFFER,            /* sell parts */
    D_CARS_LIST, D_ASK, D_COUNTER,          /* cars list and haggling */
};

static int state;
static PartList parts;
static ListBox cars;
static Dialog dlg;                          /* dialog 8 (offer), 10 (ask) or 11 (counter-offer) */
static int sold;                            /* sold something in this visit (changes the "empty" message) */
static int list_pos;

/* Part sale. */
static int spare_index;
static long offer;

/* Car sale (1001:4852). */
static int car_idx[MAX_CARS], car_index;
static long dealer_max, dealer_offer, asking;
static int stage;
static int field;
static char digits[8];

int deals_active(void) { return state != D_NONE; }

static void finish(Game *g) { game_message_clear(g); state = D_NONE; SDL_StopTextInput(g->video.window); }

/* ---------------------------------------------------------------- selling parts (1001:3f5a) */

static void parts_open(Game *g)
{
    if (!partlist_open(&parts, &g->player, -1, NULL)) {
        game_message(g, sold ? 4 : 5);      /* "All parts gone!" / "You've got no spare parts, dummy!" */
        state = D_NONE;
        return;
    }
    listbox_set_button_text(&parts.lb, -3, exe_ds_str(0x226c));      /* "Sell it" */
    if (list_pos >= parts.lb.count) list_pos = parts.lb.count - 1;
    parts.lb.sel = list_pos < 0 ? 0 : list_pos;
    state = D_PARTS_LIST;
}

void deals_sell_parts(Game *g)
{
    sold = 0;
    list_pos = 0;
    parts_open(g);
}

/* What the man pays for a part. */
static long part_offer(const PartInst *p)
{
    long value = p->value, price;
    int cond = (10000 - p->wear) / 100;
    switch (g_parts[p->part].type) {
    case PT_ENGINE:
    case PT_TRANSMISSION: price = cond * value * 9 / 1000; break;          /* 90%, by condition */
    case PT_CARB:
    case PT_INTAKE:       price = (SDL_rand(20) + 70) * value / 100; break; /* 70-89% */
    case PT_TIRES:        price = 0; break;                                 /* "...for nothing." */
    default:              price = value * 7 / 10; break;                    /* 70% */
    }
    return price / 5 * 5;
}

static void parts_pick(Game *g, int r)
{
    if (r == PL_CANCEL) { finish(g); return; }
    if (r < 0) return;
    spare_index = r;
    list_pos = parts.lb.sel;
    offer = part_offer(&g->player.parts[r]);
    if (offer) game_message_amount(g, 6, offer, 1);       /* "Will you take $X ?" */
    else { game_message(g, 7); g->msg_sticky = 1; }       /* "I'll take it off your hands for nothing." */
    dialog_load(&dlg, 8);
    state = D_PARTS_OFFER;
}

static void parts_answer(Game *g, int id)
{
    game_message_clear(g);
    if (id == -20) {                                      /* OK */
        player_remove_spare(&g->player, spare_index);
        g->player.money += offer;
        g->player.time_actions += 0x444;
        sold++;
    }
    parts_open(g);
}

/* ---------------------------------------------------------------- cars (1001:4852) */

static void cars_open(Game *g)
{
    Player *p = &g->player;
    SDL_StopTextInput(g->video.window);
    int n = 0;
    if (p->current >= 0) car_idx[n++] = p->current;
    for (int i = 0; i < p->car_count; i++)
        if (i != p->current) car_idx[n++] = i;
    if (!n) {
        game_message(g, sold ? 8 : 9);      /* "All cars gone!" / "You've got no car, dummy!" */
        state = D_NONE;
        return;
    }
    listbox_open(&cars, 9);
    for (int k = 0; k < n; k++) SDL_strlcpy(cars.text[k], g_models[p->cars[car_idx[k]].model].ad, sizeof cars.text[0]);
    cars.count = n;
    if (list_pos >= n) list_pos = n - 1;
    cars.sel = list_pos < 0 ? 0 : list_pos;
    state = D_CARS_LIST;
}

void deals_cars(Game *g)
{
    sold = 0;
    list_pos = 0;
    cars_open(g);
}

static void remove_car(Player *p, int i)
{
    memmove(&p->cars[i], &p->cars[i + 1], sizeof p->cars[0] * (size_t)(p->car_count - i - 1));
    p->car_count--;
    if (p->current == i) p->current = -1;
    else if (p->current > i) p->current--;
}

static void ask(Game *g)
{
    dialog_load(&dlg, 10);
    field = -1;
    for (int i = 0; i < dlg.count; i++) {
        if (dlg.w[i].type == W_FIELD) field = i;
        if (dlg.w[i].type == W_CTEXT) dialog_set_text(&dlg, i, g_models[g->player.cars[car_index].model].ad);
    }
    digits[0] = 0;
    state = D_ASK;
    SDL_StartTextInput(g->video.window);
}

static void counter(Game *g)
{
    game_message_amount(g, 14, dealer_offer, 1);           /* "How about $X ?" */
    dialog_load(&dlg, 11);
    state = D_COUNTER;
}

static void close_sale(Game *g)
{
    Player *p = &g->player;
    Car *c = &p->cars[car_index];
    if (dealer_offer < asking * 9 / 10) { game_message(g, 13); cars_open(g); return; }   /* "No thanks!" */
    game_message(g, asking * 2 < c->price ? 15 : 16);      /* "You must be crazy. I'll take it!" / "I'll take it!" */
    p->money += asking;
    p->time_actions += 0x888;
    remove_car(p, car_index);
    sold++;
    cars_open(g);
}

/* The haggle, step by step as in 1001:4852. Stages: 0 first price, 1 after "No way!",
 * 2/4 waiting for an answer to a counter-offer, 3/5 asking again, 10-14 the dealer's side. */
static void haggle(Game *g)
{
    Car *c = &g->player.cars[car_index];
    long value = c->price;
    for (;;) {
        switch (stage) {
        case 0:
            if (asking > value * 115 / 100) { game_message(g, 10); cars_open(g); return; }    /* "You must be kidding!" */
            if (asking > value) { game_message(g, 11); stage = 1; ask(g); return; }           /* "No way!" */
            stage = 10;
            break;
        case 1:
            if (asking > value) { game_message(g, 12); cars_open(g); return; }                /* "Get lost!" */
            stage = 10;
            break;
        case 10:                            /* first counter-offer */
            if (asking > dealer_offer) {
                dealer_offer = dealer_max * (SDL_rand(5) + 85) / 100;
                if (dealer_offer < asking) { stage = 2; counter(g); return; }
            }
            stage = 11;
            break;
        case 11:
            if (dealer_offer < asking) { stage = 3; ask(g); return; }
            stage = 12;
            break;
        case 12:                            /* second counter-offer */
            if (dealer_offer < asking) {
                dealer_offer = dealer_max * (SDL_rand(5) + 91) / 100;
                if (dealer_offer < asking) { stage = 4; counter(g); return; }
            }
            stage = 13;
            break;
        case 13:
            if (dealer_offer < asking) { stage = 5; ask(g); return; }
            close_sale(g);
            return;
        case 14:                            /* price given after the last counter-offer */
            close_sale(g);
            return;
        default:
            return;
        }
    }
}

static void start_sale(Game *g)
{
    car_index = car_idx[cars.sel];
    list_pos = cars.sel;
    Car *c = &g->player.cars[car_index];
    dealer_max = c->price * (SDL_rand(20) + 75) / 100;      /* the dealer's hidden limit */
    dealer_offer = dealer_max;
    stage = 0;
    ask(g);
}

static void submit_price(Game *g)
{
    asking = SDL_atoi(digits);
    if (asking <= 0) return;                /* the original waits for a non-zero amount */
    if (stage == 3) stage = 12;             /* continue where the haggle was */
    else if (stage == 5) stage = 14;
    haggle(g);
}

static void cars_choice(Game *g, int id)
{
    Player *p = &g->player;
    if (id == -3) { finish(g); return; }                    /* Forget it */
    if (id == -1) {                                         /* Switch it */
        p->current = car_idx[cars.sel];
        finish(g);
    } else if (id == -2) {
        start_sale(g);                                      /* Sell it */
    }
}

static void dialog_choice(Game *g, int i)
{
    if (i < 0) return;
    int id = dlg.w[i].id;
    if (state == D_PARTS_OFFER) {
        if (id == -20 || id == -21) parts_answer(g, id);
    } else if (state == D_ASK) {
        if (id == -24) submit_price(g);                     /* Offer */
        else if (id == -22) { game_message_clear(g); cars_open(g); }   /* Never mind */
    } else if (state == D_COUNTER) {
        if (id != -20 && id != -21) return;
        game_message_clear(g);
        if (id == -20) asking = dealer_offer;               /* accepted the counter-offer */
        stage = stage == 2 ? 11 : 13;
        haggle(g);
    }
}

/* ---------------------------------------------------------------- input / drawing */

void deals_key(Game *g, SDL_Scancode sc, SDL_Keycode k)
{
    switch (state) {
    case D_PARTS_LIST: parts_pick(g, partlist_key(&parts, sc)); break;
    case D_CARS_LIST: cars_choice(g, listbox_key(&cars, sc)); break;
    case D_ASK:
        if (k == SDLK_BACKSPACE) { size_t n = strlen(digits); if (n) digits[n - 1] = 0; }
        else if (k == SDLK_RETURN) submit_price(g);
        else dialog_choice(g, dialog_key(&dlg, sc));
        break;
    default: dialog_choice(g, dialog_key(&dlg, sc)); break;
    }
}

void deals_text(Game *g, const char *s)
{
    (void)g;
    if (state != D_ASK) return;
    for (; *s; s++) {
        size_t n = strlen(digits);
        if (*s >= '0' && *s <= '9' && n < 5) { digits[n] = *s; digits[n + 1] = 0; }
    }
}

void deals_click(Game *g, int x, int y)
{
    switch (state) {
    case D_PARTS_LIST: parts_pick(g, partlist_click(&parts, x, y)); break;
    case D_CARS_LIST: cars_choice(g, listbox_click(&cars, x, y)); break;
    default: dialog_choice(g, dialog_click(&dlg, x, y)); break;
    }
}

void deals_mouse_move(Game *g, int x, int y)
{
    (void)g;
    if (state == D_PARTS_LIST) partlist_mouse_move(&parts, x, y);
    else if (state == D_CARS_LIST) listbox_mouse_move(&cars, x, y);
    else dialog_mouse_move(&dlg, x, y);
}

const char *deals_hover_text(void)
{
    if (state == D_PARTS_LIST) return partlist_hover_text(&parts);
    if (state == D_CARS_LIST) return listbox_hover_text(&cars);
    return dlg.hover >= 0 ? dialog_text(&dlg, dlg.hover) : NULL;
}

void deals_draw(Game *g)
{
    Video *v = &g->video;
    switch (state) {
    case D_PARTS_LIST: partlist_draw(v, &parts); break;
    case D_CARS_LIST: listbox_draw(v, &cars); break;
    case D_ASK: {
        char shown[8];
        SDL_snprintf(shown, sizeof shown, "%s%s", digits, (SDL_GetTicks() / 300) % 2 ? "_" : " ");
        dialog_set_text(&dlg, field, shown);
        dialog_draw(v, &dlg);
        break;
    }
    case D_PARTS_OFFER:
    case D_COUNTER: dialog_draw(v, &dlg); break;
    default: break;
    }
}
