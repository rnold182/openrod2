/* Saved games in the original HOTROD<n>.SAV format (1001:5d82 writes, 1001:59e2 reads), so games
 * saved by the DOS version load here and the other way round. */
#ifndef SR_SAVE_H
#define SR_SAVE_H

#include <SDL3/SDL.h>

#include "car.h"

#define SAVE_SLOTS 15           /* HOTROD0..HOTROD14 (1001:58b2) */
#define SAVE_NAME  16

typedef struct {
    int     used;
    char    name[SAVE_NAME];
    SDL_Time saved_at;          /* file time (rebuild addition: shown in the status bar) */
    int     day;                /* in-game day of that save, 0 = June 16 */
} SaveSlot;

/* Scan the slots in `dir` (1001:58b2). Returns how many are used. */
int  save_scan(const char *dir, SaveSlot slots[SAVE_SLOTS]);
/* Returns 0 on success. Saving also renames the player to `name`, as SR.EXE does. */
int  save_write(const char *dir, int slot, Player *p, const char *name);
int  save_read(const char *dir, int slot, Player *p);

#endif
