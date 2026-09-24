#pragma once
/* Calls from the platform / video layer into subsystems that are ported separately (race, game_flow,
 * the 0f38 UI; sound is called directly through sound/sound.h). The original calls them directly; the port goes through these function
 * pointers so each subsystem can be built and tested alone. modules_init() (modules.c) sets every
 * hook to its port implementation, or to a documented stand-in while that subsystem is missing.
 * The coordinator wires new implementations in modules.c only. */
#include "types.h"

typedef struct {
    /* ---- race (port/spec/race.md §4.7) */
    void (*race_phys_step)(void);        /* race_isr 0000:238c from 253b (DS:0609 countdown, controls,
                                            one car step), called on BIOS-chain ticks while DS:8ACA != 0
                                            and DS:0286 == 0 */

    /* ---- game_flow (port/spec/game_flow.md) */
    void (*demo_step)(void);             /* 0000:1bae, from timer_isr in demo mode */

    /* ---- 0f38 UI (menus, hotspots, message boxes, animation scripts) */
    void (*timer_callback)(s16 arg);     /* 0f38:1e41 anim_tick: type-1 tick timers (ui_wait) */
    s16  (*hotspot_at)(s16 x, s16 y);    /* 0f38:4dad: hotspot id under (x, y), 0 = none (ui_wait) */
    void (*fatal_message)(const char *s);/* 0f38:23ce: message box "... Hit Return when ready" (errors,
                                            "Quit demo") */
} Modules;

extern Modules modules;

void modules_init(void);
