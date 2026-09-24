#pragma once
/* Public API of the garage subsystem (port/spec/garage.md), owned by the garage port. Other game
 * modules include this header to call into garage. The 0f38 UI toolkit (menus, message boxes, list
 * boxes, text entry, animation scripts) is in game/ui.h.
 *
 * Implemented in game/garage*.c:
 *   garage_data.c   car and part model 3c5a-43bf, spares 76a7-77b7, car_flags 49a2, car_runnable 7ee6
 *   garage_shop.c   classifieds 442b (+ the ad pages 0000:08b4 / 09cb), sell_spare_parts 3c8d,
 *                   how_about 4940, your_cars 49ed, sfx_play_wait 4fca, message_at 4ffa
 *   garage_main.c   garage_screen 6b06 and its hot spots 67cf-6ad8, paint 6f6b, customize 711a,
 *                   stickers 7367, change_tires 7800, car_info 78f1 / 79eb
 *   garage_bay.c    change_transmission 7aa4, ignition_tune 7fb1, engine_bay 81ab and the drawing
 *                   helpers 0f38:3074-40ca
 *   garage_draw.c   car pictures 0f38:8126-9e5d (car_draw 8e48, car_compose 8866), the sprite scene
 *                   helpers 0f38:a273-a7ee, tire_change_anim 0f38:be93 (+ b632 / b82c)
 * Not here (game_flow, flow.h): pools 3614 (platform pools_reset), part_alloc / part_free 3c1b / 3c3e,
 * car_alloc / car_release 3fb1 / 3fd4, msg_box 3ab8, garage_full_check 3b0c, newspaper_front 3b33,
 * opponents / new_game 542e-570c, broke_check 6ad8, the calendar 6476-6750. The gas station 0000:b8a1
 * and top_speed 0000:e218 are race's (race.h: gas_station, car_max_speed).
 *
 * Conventions (srport/PORTING.md): car and part records are near pointers of the original = u16 DS
 * offsets (0 = none); a "slot" is the DS offset of a car's part pointer (car + CR_ENGINE ...). */
#include "types.h"
#include "mem.h"

/* Registers this subsystem's cross-module hooks in `modules` (modules.h); called by modules_init:
 * timer_callback = anim_tick, hotspot_at, fatal_message. */
void garage_register_hooks(void);

/* ============================================================================== records (§4.1) */

/* car record, 0x28 bytes, pool DS:83B0 (16) */
#define CR_VALUE      0x00    /* i16 book value */
#define CR_MODEL      0x02    /* i16, -1 free */
#define CR_COLOUR     0x04    /* i8 paint 0..5 */
#define CR_CLASS      0x05    /* u8 */
#define CR_TRANS      0x06    /* part */
#define CR_ENGINE     0x08    /* part; engine, manifold, carb[3] are 5 consecutive slots */
#define CR_MANIFOLD   0x0A
#define CR_CARB       0x0C    /* [3] */
#define CR_TYRES      0x12
#define CR_TRANS_BOLT 0x14    /* i8[2] */
#define CR_BAY_BOLT   0x16    /* i8[9] */
#define CR_ENGINE_LINK 0x1F   /* i8: 0 none, -1 disconnected, 1 connected */
#define CR_IGNITION   0x20    /* i8 -8..+4 */
#define CR_GAS        0x22    /* i16 tenths of a gallon */
#define CR_FLAGS      0x24    /* u16: bits 8-12 sticker, 0x2000 roof, 0x4000 rear, 0x8000 front bumper */
#define CR_NEXT       0x26
#define CAR_BYTES     0x28

/* part record, 8 bytes, pool DS:866A (140) */
#define PT_VALUE      0x00
#define PT_WEAR       0x02    /* 0..10000, -128 no wear */
#define PT_TYPE       0x04    /* index into PARTS DS:4806, -1 free */
#define PT_NEXT       0x06

/* part catalogue DS:4806, 8 bytes: price, grade, make mask, category, 0, text id */
#define PARTS_TAB     0x4806
static inline s16 parts_price(s16 t)    { return DSS((u16)(PARTS_TAB + 8 * t)); }
static inline s8  parts_grade(s16 t)    { return DSC((u16)(PARTS_TAB + 8 * t + 2)); }
static inline u8  parts_make(s16 t)     { return DSB((u16)(PARTS_TAB + 8 * t + 3)); }
static inline s8  parts_category(s16 t) { return DSC((u16)(PARTS_TAB + 8 * t + 4)); }
static inline s16 parts_text(s16 t)     { return DSS((u16)(PARTS_TAB + 8 * t + 6)); }

/* car models DS:7D86, 10 bytes: price, class, next_for_sale, spec, ad text, picture */
#define MODELS_TAB    0x7D86
static inline s16 model_price(s16 m)    { return DSS((u16)(MODELS_TAB + 10 * m)); }
static inline u8  model_class(s16 m)    { return DSB((u16)(MODELS_TAB + 10 * m + 2)); }
static inline s8  model_next(s16 m)     { return DSC((u16)(MODELS_TAB + 10 * m + 3)); }
static inline u16 model_spec(s16 m)     { return DSW((u16)(MODELS_TAB + 10 * m + 4)); }
static inline s16 model_ad(s16 m)       { return DSS((u16)(MODELS_TAB + 10 * m + 6)); }
static inline s16 model_pic(s16 m)      { return DSS((u16)(MODELS_TAB + 10 * m + 8)); }

/* owner state */
#define G_CUR_CAR     0x7EB0  /* car */
#define G_OTHER_CARS  0x7EB2  /* list, link CR_NEXT */
#define G_CARS_USED   0x7EB6
#define G_SPARES      0x7EB8  /* list, link PT_NEXT */
#define G_PARTS_USED  0x7EBC
#define G_LIST_BUF    0x49E0  /* i16 list for list_box */
#define G_TYRE_PICS   0x5030  /* i16[3][4] wheel pictures per tyre grade */
static inline u16 tyre_pics(s16 grade) { return (u16)(G_TYRE_PICS + 8 * grade); }

/* ============================================================ car and part model (§4.2, §4.3) */

s16  part_value(u16 p);                       /* 0000:3c5a: 100 for a V-6 engine, else the value */
void spare_add(s16 type, s16 value);          /* 0000:3ff0 */
u16  car_new(s16 model, s16 value, u16 reuse);/* 0000:4036: stock car (reuse != 0: fill that record) */
s16  part_release(u16 p, s16 keep);           /* 0000:4386: returns the part's value */
s16  car_free(u16 car, s16 keep);             /* 0000:43bf: returns max(0, body value) */
u16  car_flags(u16 car);                      /* 0000:49a2: picture flags 4 roof, 2 rear, 1 front, 8 big engine */
void spares_collect(s16 cat, u16 *nodes, s8 *wear, s16 n0);   /* 0000:76a7: into DS:49E0 */
void part_install(u16 part, u16 slot);        /* 0000:7741: slot = DS offset of the car's part pointer */
void part_uninstall(u16 slot);                /* 0000:7789 */
u16  spare_unlink(u16 part);                  /* 0000:77b7 */
s16  car_runnable(u16 car, s16 full);         /* 0000:7ee6 */

/* ============================================================== screens (dispatched by game_loop) */

s16  garage_screen(s16 mode);                 /* 0000:6b06: 0 keep, 1 full, 2 after a trip, 3 after a switch;
                                                 returns the hot-spot code or 0x29A (game over) */
void classifieds(s16 section);                /* 0000:442b: 1 Used Cars, 2 Auto Parts */
void sell_spare_parts(void);                  /* 0000:3c8d: code 5 */
s16  your_cars(void);                         /* 0000:49ed: code 6; returns the next garage mode (0 / 3) */
void paint_job(void);                         /* 0000:6f6b: code 7 */
void change_tires(void);                      /* 0000:7800: code 8 */
void change_transmission(void);               /* 0000:7aa4: code 9 */
void engine_bay(u16 car);                     /* 0000:81ab: code 10 (cur_car); race views an opponent's */
void customize(s16 what);                     /* 0000:711a: codes 11/12/13 -> 2 rear, 1 front, 4 roof */
void stickers(void);                          /* 0000:7367: code 14 */
void car_info(void);                          /* 0000:79eb: code 16 */

/* ============================================================================== helpers */

s16  how_about(u16 *offer);                   /* 0000:4940: "How about $%d ?" -20 OK / -21 No */
void sfx_play_wait(s16 n);                    /* 0000:4fca: animation n, wait until it ends */
void message_at(s16 id, s16 x, s16 y);        /* 0000:4ffa: message box at (x, y), wait, close */
void set_paint_palette(s16 colour);           /* 0000:67cf: EGA palette registers 6/7 */
s16  hotspot_find(s16 code);                  /* 0000:6827: garage screen record with that code */
void garage_hotspots_register(void);          /* 0000:6882 */
void garage_hotspots_nocar(void);             /* 0000:68b3 */
void garage_hotspots_car(s16 model, s16 x, s16 y);   /* 0000:68e8 */
void garage_name_clear(void);                 /* 0000:6aa6 */
void car_info_list(s16 *xoff, s8 *wear);      /* 0000:78f1 */
void ignition_tune(u16 car);                  /* 0000:7fb1 */

/* ================================================================ car pictures (§4.17, 0f38) */

/* 0f38:8e48: draw / animate a car: mode 1 drive in from the left (race), 2 leave to the right, 3 in
 * place, 4 drive in, 6 drive out, 7 static (See it), 8 static redraw. tyre_pics = DS offset of the 4
 * wheel pictures (tyre_pics(grade)). Sets DS:825E/8260 (car_x/car_y). */
void car_draw(s16 mode, s16 model, s16 flags, s16 sticker, s16 driver, s16 y, s16 smoke, u16 tyre_pics);
void tire_change_anim(u16 old_pics, u16 new_pics);   /* 0f38:be93 */
/* 0f38:8866: frames[0] = the car body with customisations, sticker, driver (DS:8280); frames 1..n-1
 * shifted by 2 px each; wheels tyre_pics (DS offset, 0 = none); smoke: the ground shadow. */
void car_compose(FarPtr *frames, s16 model, s16 flags, s16 sticker, s16 driver, u16 tyre_pics, s16 smoke, s16 n);
/* 0f38:84be: background of a driving car (rows top-h .. top of bg): *out_bg shares bg's planes,
 * *out_bgsrc is a copy (with the smoke picture's mask when smoke != 0); overlay rects DS:5A10/5A18. */
void car_strip_setup(FarPtr bg, FarPtr *out_bgsrc, s16 smoke, FarPtr *out_bg, s16 top, s16 h, u16 *ov0, u16 *ov1);
/* 0f38:a273 sprite_scene_setup: sprite animation of n frames at (x, y bottom) for the driver slot 9
 * (ega_anim_step); tails: DS offset of 8-byte Tail records or 0; ov0/ov1: DS offsets of overlay rects.
 * Returns the Anim (DS:7D18). Layouts: platform/anim_rect.c. */
u16  sprite_scene_setup(s16 x, s16 y, const FarPtr *frames, u16 tails, s16 n, FarPtr bgsrc, FarPtr bg, u16 ov0, u16 ov1);
void anim_phase_set(u16 anim, s16 i, s16 count, s16 dx, s16 dy);   /* 0f38:a65d: frame record i */
/* 0f38:975b: rows of `pic` into the car strip DS:5A0A (and Bob's background columns); the last
 * argument of the callers is unused. (race: gas_leave, carhop_anim) */
void walker_anim(FarPtr pic, s16 sx, s16 sy, s16 w, s16 h, s16 dx, s16 y, s16 base, s16 unused);
/* 0f38:99d9: Bob's car-hop walks in (mode 0) / away (mode 1); opp: the opponent's car in front. */
void carhop_anim(s16 mode, s16 opp);
