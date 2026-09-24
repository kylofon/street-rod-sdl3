#pragma once
/* Public API of the flow subsystem (port/spec/game_flow.md), owned by the flow port. Other game
 * modules include this header to call into flow.
 *
 * Implemented in game/flow*.c:
 *   flow_main.c     game_main (main 0000:066f after platform_main_init), game_loop 0000:503f,
 *                   title_and_setup 0000:39c0, title_sequence 0f38:0bf0
 *   flow_state.c    pools / objects 3c1b-3fd4, new game 542e-570c, msg_box 3ab8, garage_full_check
 *                   3b0c, newspaper_front 3b33, broke_check 6ad8
 *   flow_clock.c    calendar and game clock 6476-6750, summer_over_screen 0f38:0d62
 *   flow_save.c     save / load 5795-641e (?:HOTROD<n>.SAV)
 *   flow_end.c      game_over_menu 315b, hall of fame 32d0-339f, ending 0000:a544
 *   flow_demo.c     demo_step 0000:1bae, kbd_inject_scancode 0000:2e93
 * Functions of this range that the platform layer already provides (platform.h): pools_init 3614
 * (= pools_reset), wait_ticks 3a83 (= wait_ticks_or_input), hot_data_load / hall_load / hall_save,
 * money_add 0f38:21c0 (video.h). */
#include "types.h"
#include "mem.h"

/* Registers this subsystem's cross-module hooks in `modules` (modules.h); called by modules_init. */
void flow_register_hooks(void);

/* ---- player state (game_flow.md §3a) */
#define FLOW_CAR_SIZE    0x28     /* car record, pool DS:83B0 (16) */
#define FLOW_PART_SIZE   0x08     /* part record, pool DS:866A (140) */
#define FLOW_OPP_SIZE    0x12     /* opponent record DS:7FF8 + 0x12*i, #21 the King */
#define FLOW_GAME_HOUR   0x222u   /* game_clock units per game hour (546 rt ticks, 30 s) */
#define FLOW_GAME_DAY    0x1998u  /* 12 game hours */
#define FLOW_GAME_OVER   0x29A    /* garage_screen: the game is over (flags DS:0282) */

/* DS:0282 game-over flags */
#define FLOW_END_SUMMER  0x01
#define FLOW_END_KING    0x02
#define FLOW_END_DEBTS   0x04
#define FLOW_END_BROKE   0x10

/* The inline `game_clock += 0x222` of game_loop (u32 add with carry). Callers adding other amounts
 * (0x111, 0x444 ...) use flow_clock_add. */
static inline void flow_clock_add(u32 units) { DSL(0x05FC) += units; }
static inline void clock_hour(void) { flow_clock_add(FLOW_GAME_HOUR); }

/* ---- objects (the pools reset by pools_reset 0000:3614, platform.h); no bounds checks */
u16  part_alloc(void);                 /* 0000:3c1b: pop DS:7EBA, part_count++ (returns a DS offset) */
void part_free(u16 p);                 /* 0000:3c3e: push, +4 = -1, part_count-- */
u16  car_alloc(void);                  /* 0000:3fb1: pop DS:7EB4, car_count++ */
void car_release(u16 c);               /* 0000:3fd4: push, +2 = -1, car_count-- */

/* ---- messages and checks */
void msg_box(s16 id);                  /* 0000:3ab8: message DS:2C02+id for DS:05DC ticks (input cuts it) */
s16  garage_full_check(void);          /* 0000:3b0c: >= 16 cars / >= 0x83 parts -> message, 1 */
s16  broke_check(void);                /* 0000:6ad8: no car, no cars, no spares, money < 400 */
s16  newspaper_front(void);            /* 0000:3b33: front page; menu-7 code (1 cars, 2 parts, else back) */

/* ---- new game */
void opponent_init(s16 i, s16 *models);/* 0000:542e: models[0] = count, models[1..] (used ones -> -1) */
void opponents_init(void);             /* 0000:5584 */
void new_game(void);                   /* 0000:56a9: pools, money, opponents, clocks, race counters */

/* ---- calendar and clock (T = game_clock + rt_ticks - rt_base) */
s16  day_index(void);                  /* 0000:6476: 0..91 */
void clock_hm(s16 *hour, s16 *min);    /* 0000:64a3: hour 0..11, minute 0..59 */
void summer_over_check(void);          /* 0000:6529: day > 90 -> DS:0282 |= 1 */
s16  month_of_day(s16 day, s16 *dom);  /* 0000:653c: 0 June .. 3 September */
void calendar_show(s16 final);         /* 0000:65af */
void date_print(s16 y);                /* 0000:6750: "%s %d, 1963" at x 0x61 */
void summer_over_screen(void);         /* 0f38:0d62 */

/* ---- endings */
void ending_king(void);                /* 0000:a544 (game_flow.md ending_sequence): after the King's race;
                                          the caller (bobs_drive_in) sets DS:0282 |= 2 */
void hall_of_fame_screen(s16 won);     /* 0000:339f */
s16  game_over_menu(void);             /* 0000:315b: -2 load, -1 new, 4 quit */

/* ---- save / load */
s16  load_game_screen(void);           /* 0000:5b4a: 0 loaded, -1 cancelled, -0x29A read error */
void save_game_screen(void);           /* 0000:5eea */
s16  quit_menu(void);                  /* 0000:641e */

/* ---- demo mode */
void demo_step(void);                  /* 0000:1bae (modules.demo_step, from timer_isr) */
void kbd_inject_scancode(void);        /* 0000:2e93: DS:47CC through the keyboard translation */

/* ================================================================== internal to game/flow*.c */

void game_loop(void);                  /* 0000:503f */
s16  title_and_setup(void);            /* 0000:39c0 */
void title_sequence(void);             /* 0f38:0bf0 */
s16  new_game_prompt(void);            /* 0000:570c */

/* PORT: buffers the original keeps in its stack frame (save-file records, the hall name edit) live
 * in the unused part of the DGROUP stack (DS:8F10..990E; the port's C code never uses it), so DOS
 * reads / writes and the UI see them in mem[] as in the original. */
#define FLOW_STK_HDR     0x9400        /* 0x24 save header (save_slots_scan) */
#define FLOW_STK_CAR     0x9430        /* 0x28 car record (load_car_chain) */
#define FLOW_STK_PART    0x9460        /* 0x08 part record (load_part_chain) */
#define FLOW_STK_ELAPSED 0x9470        /* 0x04 rt_ticks - rt_base (load / save) */
#define FLOW_STK_NAME    0x9480        /* 0x10 hall of fame name edit (hall_of_fame_screen) */
#define FLOW_STK_FNAME   0x94A0        /* 0x14 "X:HOTROD<n>.SAV" */

/* MS C 5.1 runtime helpers on DGROUP strings (flow_util.c) */
void fl_strcpy(u16 dst, u16 src);                  /* strcpy */
void fl_strncpy(u16 dst, u16 src, u16 n);          /* strncpy (no NUL when src is longer) */
void fl_strcat(u16 dst, u16 src);                  /* strcat */
s16  fl_strcmp(u16 a, u16 b);                      /* strcmp: <0 / 0 / >0 */
s16  fl_atoi(u16 s);                               /* 1e16:1930 atoi (16-bit wrap) */
void fl_itoa(s16 v, u16 dst);                      /* 1e16:19d0 itoa(v, dst, 10) */
void fl_memmove(u16 dst, u16 src, u16 n);          /* 1e16:21cc */
u16  fl_save_name(u16 buf, u16 tmpl, s16 n);       /* {drive, tmpl ":HOTROD\0"} + itoa(n) + ".SAV" */
