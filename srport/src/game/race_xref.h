#pragma once
/* PORT: functions of other game subsystems that the race code calls but that were not declared in
 * their public headers (game/garage.h, game/ui.h, game/flow.h) when the race port was written. The
 * coordinator reconciles these declarations with the owners' headers (and removes this file).
 * Near pointers are u16 DS offsets. Names are provisional (no entry in port/symbols.csv). */
#include "types.h"
#include "mem.h"

/* 0f38:99d9 (garage's 0f38:8126-9e5d picture range): the car-hop at Bob's walks in (out = 0) or
 * away (out = 1); arg = opponent present (race.md §4.14 waitress_toggle 0000:914c). */
void carhop_anim(s16 out, s16 arg);
/* 0f38:975b (garage's range): sprite walk animation of picture `pic` (gas_leave 0000:b717 passes the
 * attendant LIB2 #209 with 0x60, 0, 0x30, 0x40, 0xC8, 0x51, 0xA9, 2). */
void walker_anim(FarPtr pic, s16 a, s16 b, s16 c, s16 d, s16 e, s16 f, s16 g, s16 h);
