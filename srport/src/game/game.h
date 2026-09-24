#pragma once
/* Cross-subsystem entry points of the game code. */
#include "types.h"

/* The rest of the original main (0000:066f) after platform_main_init (platform.h): game_loop 0000:503f,
 * then platform_exit 0000:0fea (which does not return). Implemented by game_flow (game/flow_main.c). */
int game_main(void);
