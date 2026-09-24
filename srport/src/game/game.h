#pragma once
/* Cross-subsystem entry points of the game code. */
#include "types.h"

/* The rest of the original main (0000:066f) after platform_main_init (platform.h): game_loop 0000:503f,
 * then platform_exit 0000:0fea (which does not return). The skeleton's stand-in is
 * game/placeholder.c until game_flow is ported. Returns the process exit code if it ever returns. */
int game_main(void);
