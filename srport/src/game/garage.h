#pragma once
/* Public API of the garage subsystem (port/spec/garage.md), owned by the garage port. Other game
 * modules include this header to call into garage. */
#include "types.h"
#include "mem.h"

/* Registers this subsystem's cross-module hooks in `modules` (modules.h); called by modules_init. */
void garage_register_hooks(void);
