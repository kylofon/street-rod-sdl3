#pragma once
/* Private header of the race subsystem (game/race*.c). Not for other modules: they use race.h.
 *
 * Record layouts (race.md §3, garage.md §4.1) are accessed in mem[] through the DS offset of the
 * record (a near pointer of the original) plus a field offset:
 *     PHW(DS_player_phys, PH_RPM) += ...;   CARW(car, CAR_GAS) ...;   PARTW(p, PART_WEAR) ...
 */
#include "types.h"
#include "mem.h"
#include "symbols.h"

/* ============================================================================ records */

/* phys block, 0xC2 bytes: player DS:78E0, opponent DS:79E2 (race.md §3.1) */
#define PH_SPEED        0x00
#define PH_STEER_IN     0x02
#define PH_WHEEL        0x04
#define PH_BLOWOUT_SEG  0x0A
#define PH_TIRE_SLIP    0x0C
#define PH_RPM          0x0E
#define PH_F10          0x10    /* player: rpm_lo copy; opponent: unused */
#define PH_F12          0x12    /* player: rpm_hi copy; opponent: band_hi */
#define PH_RPM_UP       0x14    /* opponent: band_up */
#define PH_RPM_DOWN     0x16
#define PH_GEAR         0x18
#define PH_GEAR_DIRTY   0x1A
#define PH_NGEARS       0x1C
#define PH_REDLINE      0x1E
#define PH_AUTOMATIC    0x20
#define PH_AUTO_SHIFT   0x22
#define PH_AUTO_MODE    0x24
#define PH_F26          0x26
#define PH_F28          0x28
#define PH_F2A          0x2A
#define PH_RATIO        0x34    /* s16[5] */
#define PH_GRIP         0x3E
#define PH_CUR_RATIO    0x40
#define PH_BAND         0x42
#define PH_CURVE        0x44    /* [5 gears][3 bands] {rpm_lo, rpm_hi, up, down}, gear stride 0x18 */
#define PH_RESULT       0xBC
#define PH_STEPS_AT_STOP 0xBE
#define PH_STEPS        0xC0
#define PH_BAND_HI      PH_F12
#define PH_BAND_UP      PH_RPM_UP
#define CRV(gear, band) ((u16)(PH_CURVE + (gear) * 0x18 + (band) * 8))
#define CRV_LO   0
#define CRV_HI   2
#define CRV_UP   4
#define CRV_DOWN 6

#define PHS(p, off) DSS((u16)((p) + (off)))
#define PHW(p, off) DSW((u16)((p) + (off)))
#define PLAYER DS_player_phys
#define OPP    DS_opp_phys

/* car record, 0x28 bytes (garage.md §4.1) */
#define CAR_VALUE     0x00
#define CAR_MODEL     0x02
#define CAR_COLOUR    0x04      /* i8 */
#define CAR_CLASS     0x05      /* u8 */
#define CAR_TRANS     0x06
#define CAR_ENGINE    0x08
#define CAR_MANIFOLD  0x0A
#define CAR_CARB      0x0C      /* [3] */
#define CAR_TYRES     0x12
#define CAR_TIMING    0x20      /* i8 */
#define CAR_GAS       0x22
#define CAR_FLAGS     0x24
#define CAR_NEXT      0x26
#define CARS(c, off) DSS((u16)((c) + (off)))
#define CARW(c, off) DSW((u16)((c) + (off)))
#define CARC(c, off) DSC((u16)((c) + (off)))
#define CARB(c, off) DSB((u16)((c) + (off)))

/* part record, 8 bytes */
#define PART_VALUE 0x00
#define PART_WEAR  0x02
#define PART_TYPE  0x04
#define PARTS(p, off) DSS((u16)((p) + (off)))
/* grade byte of the part type: (s8)DS:4808[type*8] */
static inline s16 part_grade(u16 part) { return DSC((u16)(DS_part_types + PARTS(part, PART_TYPE) * 8)); }
static inline u16 cur_car(void) { return DSW(DS_car); }

/* car model, 10 bytes at DS:7D86 (+02 class byte, +04 drivetrain word, +06 name, +08 picture) and
 * the runtime overlay columns at DS:8DF0 + m*10 ({b0, _, roof, scoop, rear, front} words) */
#define MODEL(m)        ((u16)(DS_car_models + (m) * 10))
#define MODEL_CLASS(m)  DSC((u16)(MODEL(m) + 2))
#define MODEL_DRIVE(m)  DSW((u16)(MODEL(m) + 4))
#define MODEL_8DF0(m)   DSC((u16)(0x8DF0 + (m) * 10))
#define MODEL_8DF2(m)   DSS((u16)(0x8DF2 + (m) * 10))
#define MODEL_8DF6(m)   DSS((u16)(0x8DF6 + (m) * 10))
#define MODEL_8DF8(m)   DSS((u16)(0x8DF8 + (m) * 10))

/* opponent record, 0x12 bytes at DS:7FF8 (race.md §3.3) */
#define OREC(i)      ((u16)(DS_opponents + (i) * 0x12))
#define OR_DCLASS  0x00   /* i8 */
#define OR_SKILL   0x01   /* i8 */
#define OR_CLUMSY  0x02   /* i8 */
#define OR_PIC     0x04
#define OR_NAME    0x06
#define OR_MODEL   0x08
#define OR_COLOUR  0x0A   /* i8 */
#define OR_STATUS  0x0B   /* i8 */
#define OR_RACES   0x0C   /* i8 */
#define OR_WINS    0x0D   /* i8 */
#define OR_CUSTOM  0x0E   /* i8 */
#define OR_STICKER 0x0F   /* i8 */
#define OR_REROLL  0x10   /* i8 */

/* ============================================================================ track arrays */
/* far word arrays in segment 389b (race_render.md §3.1); no bounds checks, like the original */
#define TRK_L    0x33F0
#define TRK_R    0x3CB0
#define TRK_YAW  0x4570
#define TRK_C    0x4E30
#define TRK_HGT  0x56F0
#define TRKW(arr, i) SEGW(0x389B, (u16)((arr) + 2 * (i)))
#define TRKS(arr, i) SEGS(0x389B, (u16)((arr) + 2 * (i)))

/* ============================================================================ helpers */

static inline s16 abs16(s16 v) { return (s16)(v < 0 ? -v : v); }   /* cwd/xor/sub: abs(-32768) = -32768 */

/* 1e16:2de9 _ftol: truncate toward zero. PORT: the 8087 emulator computes in 80-bit; the port uses
 * long double (x87 extended on the MinGW/GCC x86 hosts). Out of range: the integer indefinite
 * 0x80000000 of FISTP. Callers take the low word where the original uses AX only. */
static inline s32 ftol(long double v)
{
    if (!(v > -2147483649.0L && v < 2147483648.0L)) return (s32)0x80000000u;
    return (s32)v;
}

/* ============================================================================ race_phys.c */
void phys_reset(u16 p);                      /* 0000:cdaf */
void opponent_setup(void);                   /* 0000:cf5c */
void race_start(void);                       /* 0000:d031 */
void opp_gear(s16 v, u16 p);                 /* 0000:d1de */
void opp_ai(void);                           /* 0000:d29d */
s16  speed_to_step(s16 mph);                 /* 0000:d544 */
void player_advance(void);                   /* 0000:d624 */
void opp_advance_steer(void);                /* 0000:d67d */
void police_spawn(void);                     /* 0000:d992 */
void race_step(void);                        /* 0000:da25 */
void finish_stats(void);                     /* 0000:c407 */

/* ============================================================================ race_drive.c */
s16  drive_run(void);                        /* 0000:8e2d */
u16  race_logic(void);                       /* 0000:8ea8 */

/* ============================================================================ race_results.c */
u16  race_results(void);                     /* 0000:c613 */
void race_msg(void);                         /* 0000:c497 */
s16  tires_replace(void);                    /* 0000:bed4 */

/* ============================================================================ race_bob.c */
s16  model_class3(s16 model);                /* 0000:9524 */
s16  king_status(void);                      /* 0000:9556 */
s16  king_chance(void);                      /* 0000:95f1 */
s16  pick_opponent(s16 pct_king, s16 all);   /* 0000:984e */
void dancing_girl(void);                     /* 0000:a416 */
s16  challenge(s16 king);                    /* 0000:9d6d */
void opponent_select(s16 opp, s16 mode, s16 flag);   /* 0000:9a27 (also the jail b08c) */

/* ============================================================================ race_dash.c (0f38) */
void speedo_init(s16 mph);                   /* 0f38:01d2 */
void tach_init(s16 rpm);                     /* 0f38:0349 */
s16  needle_update(s16 nv, s16 old, s16 tach); /* 0f38:0425 */
void clock_draw(void);                       /* 0f38:05f2 */
void wheel_load(void);                       /* 0f38:0f53 */
void wheel_knob_crash(s16 pos);              /* 0f38:1094 */
void wheel_draw(s16 nw, s16 old, FarPtr dst);/* 0f38:1130 */
void shifter_draw(s16 automatic, s16 ngears);/* 0f38:12f7 */
void shifter_knob(FarPtr dst, u16 phys);     /* 0f38:14f2 */
void gear_label(s16 i);                      /* 0f38:22e9 */

/* ============================================================================ race_road.c / race_track.c (2645) */
void road_set_seg_ptrs(s16 seg);             /* 2645:171d */
void road_advance_segment(void);             /* 2645:1efc */
void road_race_init(void);                   /* 2645:2114 */
void road_step(void);                        /* 2645:213d */
void clip_set_ymin0(void);                   /* 2645:23f0 */
void clip_set_view(void);                    /* 2645:2410 */
void road_load_graphics(void);               /* 2645:2429 */
void shape_draw_dot(s16 x, s16 y);           /* 2645:3abf */
