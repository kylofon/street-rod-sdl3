#pragma once
/* Public API of the race subsystem (port/spec/race.md, port/spec/race_render.md), owned by the race
 * port. Other game modules include this header to call into race.
 *
 * Conventions (srport/PORTING.md): all state stays in mem[] at its original DGROUP address; car,
 * part, phys and opponent records are near pointers = u16 DS offsets (the car record of the current
 * car is DSW(DS_car) = DS:7EB0, the player's physics block DS_player_phys = DS:78E0, the opponent's
 * DS_opp_phys = DS:79E2). */
#include "types.h"
#include "mem.h"

/* Registers this subsystem's cross-module hooks in `modules` (modules.h); called by modules_init.
 * Sets modules.race_phys_step = race_phys_step. */
void race_register_hooks(void);

/* ---- entry points of the game loop 0000:503f (game_flow) */

/* 0000:8d26: cruise to DS:0284 (1 garage, 2 Bob's, 3 gas station): 0.2 gal of fuel, set-up, the
 * "Cruising in town..." drive. Returns when the car has arrived (or the drive was aborted, DS:47EE). */
void drive_to(void);
/* 0000:ab8b: Bob's Drive-In main loop (opponents, challenges, races, results, jail, King). Returns
 * when the player drove home (it cruises back itself) or DS:0282 (game_end_flags) became non-zero
 * (|1 summer over via 0000:6529, |2 King beaten: the caller runs the ending, |4 jailed). */
void bob_drive_in(void);
/* 0000:b8a1: Gus's gas station (after drive_to with DS:0284 = 3); drives back to the garage. */
void gas_station(void);
/* 0000:b08c: the jail screen (after "sentenced for debts"). DS:0284 = 6 meanwhile. */
void jail(void);

/* ---- car physics set-up (race.md §4.4), also used by the garage and the game loop */

/* 0000:dcbe: gearbox ratios and rpm curves of the physics block `phys` from the parts of `car`. */
void car_setup(u16 car, u16 phys);
/* 0000:e218: top speed in mph of model `model` as bought (stock parts), or of the current car
 * (DS:7EB0) when model < 0. Garage "Max speed %3i mph" (0000:79eb). */
s16  car_max_speed(s16 model);
/* 0000:e0e2: stock car of `model`, then car_setup into `phys`. */
void model_car_setup(s16 model, u16 phys);

/* ---- opponents (race.md §4.14), also used by the ending 0000:a544 (game_flow) */

/* 0000:98ba: make opponent `opp` (0..0x15, 0x15 = the King) current: DS:7F82, DS:7D84, DS:7648,
 * DS:7D7A, DS:7D40, random sticker / customising, the picture globals DS:827A..8280, palette 6/7. */
void opponent_load(s16 opp);
/* 0000:8d9c: EGA palette registers 0Ch / 03h = opponent paint pair DS:50C2[idx]; -1 restores. */
void opp_palette_set(s16 idx);

/* ---- track (race_render.md §4.2 / §4.3) */

/* 2645:0d48: page pointers, the town and the race road (random scenery), then the course. From the
 * new-game set-up 0000:39c0. */
void track_build_all(void);
/* 2645:0b91: rebuild the course part (segments 0x17C..0x45F): arg 100 picks a random layout into
 * DS:5E0C, any other value rebuilds DS:5E0C (after loading a saved game, 0000:56a9 / 0000:5b4a). */
void track_build_course(s16 arg);

/* ---- the drive / race loop, used internally and by developer aids */

/* 0f38:7b22: cockpit, split screen, the frame loop (road_step + race_logic + dashboard), restore.
 * Returns race_logic's non-zero result. */
s16  race_run(void);
/* 0000:238c from 253b (race.md §4.7): DS:0609 countdown, controls, one player physics step. The
 * platform ISR calls it through modules.race_phys_step on every BIOS-chain tick while racing. */
void race_phys_step(void);
/* 0000:2374: zero steering / wheel / rpm gains, DS:799E = DS:79A0. */
void race_stop_inputs(void);
/* 2645:206b: 1 if the car hits a road-side obstacle or is far off the road; sets DS:5E0E on the
 * shoulder. */
s16  road_edge_collision(void);

/* DEVELOPER AID (hooks_race.c, not in the original): if the environment variable SR_DEBUG_RACE is
 * set (drag | road | cruise | bob) runs that drive straight after platform_main_init and exits;
 * returns false (doing nothing) when it is not set. Meant to be called by main.c / game_main before
 * the game loop: `race_debug_main();`. */
bool race_debug_main(void);

/* PORT: frame pacing of road_frame 2645:1e9e — the original busy-waits until DS:05F8 (72.8 Hz
 * ticks) reached the frame's start + 9. Kept as a named constant; the physics step runs every 12th
 * tick in the platform ISR (DS:0608 / DS:0609 dividers), i.e. 4 steps per 3 frames. */
#define RACE_TICKS_PER_FRAME 9
