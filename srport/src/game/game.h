#pragma once
/* Cross-subsystem entry points of the game code. */
#include "types.h"

/* The original main (0000:066f) from the point the port takes over (after argument parsing):
 * returns the process exit code. */
int game_main(void);
