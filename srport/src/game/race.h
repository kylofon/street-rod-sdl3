#pragma once
/* Public API of the race subsystem (port/spec/race.md), owned by the race port. Other game
 * modules include this header to call into race. */
#include "types.h"
#include "mem.h"

/* Registers this subsystem's cross-module hooks in `modules` (modules.h); called by modules_init. */
void race_register_hooks(void);
