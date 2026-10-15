/* Save Game / Old Game screens. */
#ifndef SR_FILES_H
#define SR_FILES_H

#include "../game.h"

enum { FILES_LOAD, FILES_SAVE };
void files_open(Game *g, int mode, const Scene *return_to);

#endif
