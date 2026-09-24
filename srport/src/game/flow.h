#pragma once
/* Public API of the flow subsystem (port/spec/game_flow.md), owned by the flow port. Other game
 * modules include this header to call into flow. */
#include "types.h"
#include "mem.h"

/* Registers this subsystem's cross-module hooks in `modules` (modules.h); called by modules_init. */
void flow_register_hooks(void);
