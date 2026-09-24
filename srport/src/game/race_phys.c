/* Race: car physics set-up, the player physics step of the race ISR, the opponent AI, police, track
 * progress and the race start (port/spec/race.md §4.4-§4.12, verified against the disassembly).
 *
 *   0000:2374 race_stop_inputs     0000:253b race_phys_step (race_isr 0000:238c from 253b)
 *   0000:c407 finish_stats         0000:cdaf phys_reset         0000:cf5c opponent_setup
 *   0000:d031 race_start           0000:d1de opp_gear           0000:d29d opp_ai
 *   0000:d544 speed_to_step        0000:d624 player_advance     0000:d67d opp_advance_steer
 *   0000:d992 police_spawn         0000:da25 race_step          0000:dcbe car_setup
 *   0000:e0e2 model_car_setup      0000:e218 car_max_speed
 *
 * Floating point (the MSC 8087 emulator, INT 34h-3Dh): evaluated in long double (x87 extended, like
 * the emulator) in the original order; values the original stores to a double (fstp qword) are
 * rounded to double at the same points. The constants are read from their DGROUP doubles. */
#include "game/race.h"
#include "game/race_int.h"

#include <string.h>

#include "platform/platform.h"
#include "platform/video.h"
#include "sound/sound.h"

/* A DGROUP double constant (DS:6954 ... DS:6A20). */
static long double dconst(u16 off)
{
    double d;
    memcpy(&d, mp(DGROUP, off), 8);
    return d;
}

/* ============================================================================ car set-up */

/* The fields of a car record that perf() / car_setup read: the original reads them through a CAR*,
 * either the current car or a temporary stock car built on the stack (e0e2 / e218). */
typedef struct {
    s16 model;
    u16 flags;          /* +24 */
    s8  timing;         /* +20 */
    s16 eng_wear;       /* engine part +02 */
    s16 e, man, carb, tr, ty;   /* grades of engine +08, manifold +0A, carb[0] +0C, trans +06, tyres +12 */
} CarView;

static void car_view_of(u16 car, CarView *v)
{
    v->model = CARS(car, CAR_MODEL);
    v->flags = CARW(car, CAR_FLAGS);
    v->timing = CARC(car, CAR_TIMING);
    v->eng_wear = PARTS(CARW(car, CAR_ENGINE), PART_WEAR);
    v->e = part_grade(CARW(car, CAR_ENGINE));
    v->man = part_grade(CARW(car, CAR_MANIFOLD));
    v->carb = part_grade(CARW(car, CAR_CARB));
    v->tr = part_grade(CARW(car, CAR_TRANS));
    v->ty = part_grade(CARW(car, CAR_TYRES));
}

/* The temporary stock car of e0e2 / e218: part types from the model's drivetrain word, every part
 * with the initial wear DS:496A, ignition 0. PORT: the original never writes the temporary record's
 * flags word (+24, stack garbage at bp-30h / bp-1Ch); the port uses 0 (no customisation), which is
 * what both specs assume. */
static void car_view_stock(s16 model, CarView *v)
{
    u16 w = MODEL_DRIVE(model);
    s16 make = (s16)((w & 0xF) >> 1);
    s16 tg = (s16)((w >> 6) & 3);
    s16 eng_type = (s16)(3 * ((w >> 8) & 7) + make);
    s16 trn_type = (s16)(9 + 3 * tg + make + (tg == 3));
    s16 man_type = (s16)(0x19 + 3 * ((w >> 11) & 7) + make);
    s16 tyr_type = (s16)(0x28 + ((w >> 4) & 3));
    s16 carb_type = (s16)(0x16 + ((w >> 14) & 3));
    v->model = model;
    v->flags = 0;
    v->timing = 0;
    v->eng_wear = DSS(0x496A);
    v->e = DSC((u16)(DS_part_types + eng_type * 8));
    v->man = DSC((u16)(DS_part_types + man_type * 8));
    v->carb = DSC((u16)(DS_part_types + carb_type * 8));
    v->tr = DSC((u16)(DS_part_types + trn_type * 8));
    v->ty = DSC((u16)(DS_part_types + tyr_type * 8));
}

/* The performance factor f and the top speed g (dcbe / e218, identical code): both are stored to
 * doubles (bp-10h / bp-8) before use. */
static void perf(const CarView *c, double *f_out, double *g_out)
{
    s16 m = c->model;
    long double s;
    s = (long double)(((c->flags & 0x2000) || MODEL_8DF2(m) == 0) ? 1 : 0) * dconst(0x6988);
    s += (long double)(((c->flags & 0x8000) || MODEL_8DF8(m) == 0) ? 1 : 0) * dconst(0x6990);
    s += (long double)(((c->flags & 0x4000) || MODEL_8DF6(m) == 0) ? 1 : 0) * dconst(0x6998);
    s += (long double)DSS((u16)(DS_class_bonus + (u8)MODEL_CLASS(m) * 2)) * dconst(0x69A0) / dconst(0x69A8);
    s += (long double)DSS((u16)(DS_intake_bonus + c->carb * 10 + c->man * 2)) * dconst(0x69B0) / dconst(0x69B8);
    s += (long double)DSS((u16)(DS_eng_bonus + c->e * 2)) * dconst(0x69C0) / dconst(0x69B8);
    s += (long double)DSS((u16)(DS_trans_bonus + c->tr * 2)) * dconst(0x69B0) / dconst(0x69B8);
    long double t = (long double)c->timing / dconst(0x69C8);
    if (t < 0) t = -t;
    s += (dconst(0x69D0) - t) * dconst(0x69D8);
    double sd = (double)s;                                        /* fstp qword [bp-10h] */
    sd = (double)((dconst(0x69E0) - (long double)c->eng_wear) / dconst(0x69E0) * sd);
    long double f = (c->tr == 0 ? dconst(0x69E8) : dconst(0x69D0)) * sd * dconst(0x69F0) + dconst(0x69F8);
    *f_out = (double)f;                                           /* fst qword [bp-10h] */
    long double g = f * dconst(0x6A00);
    *g_out = (double)g;                                           /* fst qword [bp-8] */
    if (g > dconst(0x6A00)) *g_out = (double)dconst(0x6A00);      /* fcom: extended value */
}

static void car_setup_view(const CarView *c, u16 p)
{
    double fd, gd;
    perf(c, &fd, &gd);
    long double f = fd, g = gd, r = 0;
    s16 e = c->e;
    u16 last = 0;
    for (s16 gear = 0; gear <= 4; gear++) {
        r = (long double)DSS((u16)(DS_gear_spread + c->tr * 10 + gear * 2)) * g / dconst(0x69B8);
        if (r == dconst(0x6A08)) r = dconst(0x69D8);
        PHW(p, PH_RATIO + gear * 2) = (u16)ftol(dconst(0x6A10) / r);
        u16 b = (u16)(p + CRV(gear, 0));
        last = b;
        DSW(b + 0) = 0;
        DSW(b + 2) = DSW(b + 8) = DSW((u16)(DS_eng_bands + e * 4));
        DSW(b + 0x0A) = DSW(b + 0x10) = DSW((u16)(DS_eng_bands + e * 4 + 2));
        DSW(b + 0x12) = 0x1644;
        s16 v = (s16)ftol((long double)DSS((u16)(DS_eng_torque + e * 10 + gear * 2)) * f);
        s16 t = (s16)(v / (gear ? 5 : 1));
        s16 m1 = (gear == 1 && c->ty != 2) ? 2 : 1;
        s16 m2 = (gear == 1 && c->ty == 2) ? 3 : 1;
        DSW(b + 4) = (u16)(s16)(m2 * m1 * t);
        DSW(b + 0x0C) = (u16)v;
        DSW(b + 0x14) = (u16)(s16)(v / (gear ? 10 : 1));
        for (int k = 0; k < 3; k++)
            DSW((u16)(b + 6 + k * 8)) = (u16)(s16)(gear ? v / (5 - gear) : v);
    }
    u16 a = (c->tr == 0);                                          /* cmp tr,1 / sbb / neg */
    PHW(p, PH_AUTO_MODE) = a;
    PHW(p, PH_AUTOMATIC) = a;
    if (a) {
        PHW(p, PH_F28) = 0xFFFF;
        PHW(p, PH_F26) = 0xFFFF;
        PHW(p, PH_AUTO_SHIFT) = DSW((u16)(last + 0x0A));          /* curve[4][1].rpm_hi */
    }
    PHW(p, PH_REDLINE) = DSW((u16)(DS_eng_redline + e * 2));
    long double gr = (long double)(s16)(0x82 - (u8)MODEL_CLASS(c->model)) * dconst(0x6A18) / dconst(0x6A20)
                   + (long double)DSS((u16)(DS_tire_grip + c->ty * 2));
    PHW(p, PH_GRIP) = (u16)ftol(gr);
    PHW(p, PH_NGEARS) = DSW((u16)(DS_trans_gears + c->tr * 2));
    PHW(p, PH_GEAR_DIRTY) = 1;
}

/* 0000:dcbe car_setup */
void car_setup(u16 car, u16 phys)
{
    CarView v;
    car_view_of(car, &v);
    car_setup_view(&v, phys);
}

/* 0000:e0e2 model_car_setup */
void model_car_setup(s16 model, u16 phys)
{
    CarView v;
    car_view_stock(model, &v);
    car_setup_view(&v, phys);
}

/* 0000:e218 car_max_speed (garage: top_speed) */
s16 car_max_speed(s16 model)
{
    CarView v;
    if (model < 0) car_view_of(cur_car(), &v);
    else car_view_stock(model, &v);
    double f, g;
    perf(&v, &f, &g);
    return (s16)ftol(g);
}

/* ============================================================================ reset / set-up */

/* 0000:cdaf phys_reset */
void phys_reset(u16 p)
{
    /* cli */
    if (DSW(0x4746) == 0 && DSW(0x4748) == 0) {                   /* save the mouse position */
        DSW(0x4746) = DSW(DS_cursor_x);
        DSW(0x4748) = DSW(DS_cursor_y);
    }
    DSB(DS_kbd_last_dir + 1) = 0;
    DSB(DS_kbd_last_dir) = 0;
    DSW(DS_cursor_y) = 0;                                         /* DS:472C pedal step */
    DSW(DS_cursor_x) = 0;                                         /* DS:472A steer step */
    /* sti */
    DSB(DS_click_edge) = 0;                                       /* DS:47CD shift request */
    PHW(p, PH_WHEEL) = 0; PHW(p, PH_STEER_IN) = 0; PHW(p, PH_BAND) = 0;
    PHW(p, PH_GEAR) = 0; PHW(p, PH_SPEED) = 0;
    PHW(p, PH_STEPS) = 0; PHW(p, PH_STEPS_AT_STOP) = 0; PHW(p, PH_RESULT) = 0;
    if (PHW(p, PH_AUTOMATIC) != 0) {
        u16 r = PHW(p, PH_REDLINE);
        PHW(p, CRV(2, 2) + CRV_LO) = r;                          /* +84 */
        PHW(p, CRV(2, 1) + CRV_HI) = r;                          /* +7E */
        PHW(p, PH_AUTO_MODE) = 1;
    }
    PHW(p, PH_F26) = 0xFFFF;
    PHW(p, PH_F28) = 0xFFFF;
    PHW(p, PH_RPM) = 0x5DC;
    PHW(p, PH_CUR_RATIO) = PHW(p, PH_F2A);                        /* quirk: +2A, not ratio[0] */
    PHW(p, PH_RPM_UP) = PHW(p, CRV(0, 0) + CRV_UP);
    PHW(p, PH_RPM_DOWN) = PHW(p, CRV(0, 0) + CRV_DOWN);
    PHW(p, PH_F12) = PHW(p, CRV(0, 0) + CRV_HI);
    PHW(p, PH_F10) = PHW(p, CRV(0, 0) + CRV_LO);
    DSW(0x05D6) = 0;
    DSW(0x05DA) = 0;
    DSW(0x05D8) = 0;
    DSW(0x8630) = 0xFFFF;                                          /* gear label cache */
    DSW(DS_track_end) = DSW(DS_race_type) == 0 ? 0x17C : 0x3FC;
    u16 ty = CARW(cur_car(), CAR_TYRES);                           /* ALWAYS the player's tyres */
    s16 wear = PARTS(ty, PART_WEAR);
    PHW(p, PH_TIRE_SLIP) = DSW((u16)(DS_tire_slip_tab + part_grade(ty) * 10 + (s16)(wear / 2000) * 2));
    if (PARTS(ty, PART_WEAR) >= 0x1F40 && (s16)(PARTS(ty, PART_WEAR) + rnd(0x7D0)) > 0x251C) {
        s16 h = (s16)((s16)(DSS(DS_track_end) - 0xC8) >> 1);
        PHW(p, PH_BLOWOUT_SEG) = (u16)(s16)(rnd((s16)(h >> 1)) + h + 0xC8);
    } else {
        PHW(p, PH_BLOWOUT_SEG) = 0;
    }
    DSB(DS_tires_blown) = 0;
    DSW(DS_ai_steps_done) = 0;
    DSS(DS_opp_vmax) = car_max_speed(DSS(DS_opp_model));
    long double vmax = (long double)DSS(DS_opp_vmax);
    DSS(DS_opp_v70) = (s16)ftol(vmax * dconst(0x696C));
    DSS(DS_opp_v80) = (s16)ftol(vmax * dconst(0x6974));
    DSW(DS_opp_slack) = DSS(DS_cpu_speed) < 4 ? 10 : 16;
    DSW(0x54D2) = 0;
    DSW(DS_stats_done) = 0;
}

/* 0000:cf5c opponent_setup */
void opponent_setup(void)
{
    model_car_setup(DSS(DS_opp_model), OPP);
    DSS(DS_opp_skill) = DSC((u16)(DSW(DS_opp_rec) + OR_SKILL));
    phys_reset(OPP);
    PHW(OPP, PH_GEAR) = 0;
    PHW(OPP, PH_CUR_RATIO) = PHW(OPP, PH_RATIO);
    PHW(OPP, PH_RPM) = 0x5DC;
    PHW(OPP, PH_SPEED) = (u16)idiv32_16(0x5DC, PHS(OPP, PH_CUR_RATIO), NULL);
    s16 band = 1;
    u16 crv = (u16)(OPP + CRV(0, 1));
    if (DSS(crv + CRV_HI) <= 0x5DC) { band = 2; crv = (u16)(crv + 8); }
    else if (DSS(crv + CRV_LO) > PHS(OPP, PH_RPM)) { band = 0; crv = (u16)(crv - 8); }
    DSW(0x6CB8) = (u16)band;
    PHW(OPP, PH_BAND) = (u16)band;
    PHW(OPP, PH_BAND_HI) = DSW(crv + CRV_HI);
    PHW(OPP, PH_BAND_UP) = DSW(crv + CRV_UP);
    DSW(DS_opp_shift_rpm) = PHW(OPP, PH_AUTO_MODE) != 0 ? 0x14B4
                          : (u16)(PHS(OPP, PH_REDLINE) - (s16)(DSS(DS_opp_skill) * 100));
    DSW(DS_opp_seg) = 0xC8;
    DSW(DS_opp_seg_frac) = 0;
    s16 lat = (s16)((s16)(DSS(DS_ai_edge_left) + DSS(DS_ai_edge_right)) >> 1) ;
    lat = (s16)(lat - 0xC8);
    DSS(DS_opp_lat) = lat;
    DSS(DS_opp_lat_target) = lat;
}

/* 0000:d031 race_start */
void race_start(void)
{
    cursor_ctl(-4);
    cursor_ctl(1);
    input_reset();
    phys_reset(PLAYER);
    opponent_setup();
    if (DSW(DS_race_type) == 1 && DSW(DS_vs_king) == 0) {
        u16 st = rnd(4) == 0;
        DSW(DS_police_state) = st;
        if (st) DSS(DS_police_seg) = (s16)(rnd(0xFF) + 0x1FE);
    } else {
        DSW(DS_police_state) = 0;
    }
    DSW(DS_race_state) = 0;
    DSB(DS_racing) = 1;
    drive_run();                                                   /* the race */
    race_isr_leave();
    DSL(DS_wait_deadline) = 0;
    input_reset();
    status_label(ds_str(DSW(0x49DC)));
    if (DSS(DS_seg) <= 0xC8) return;                               /* never left the line: no wear */
    u16 car = cur_car();
    if (rnd(3) > 0) {                                              /* 2/3: ignition timing drifts */
        s8 t = CARC(car, CAR_TIMING);
        u8 al;
        if (t > 0) {
            s16 r = rnd(4);
            al = (t < r) ? (u8)rnd(4) : (u8)t;
        } else if (t < 0) {
            s16 r = (s16)-rnd(8);
            al = (r >= t) ? (u8)t : (u8)(-rnd(8));
        } else {
            al = (u8)((u8)rnd(8) - 4);
        }
        CARB(car, CAR_TIMING) = al;
    }
    u16 ty = CARW(car, CAR_TYRES);
    s16 r = rnd(0x12C);
    s16 add;
    if (DSC((u16)(DS_part_types + PARTS(ty, PART_TYPE) * 8)) == 0) add = DSW(DS_race_type) == 1 ? 0x2BC : 0x12C;
    else add = DSW(DS_race_type) == 1 ? 0x190 : 0xC8;
    PARTS(ty, PART_WEAR) = (s16)(PARTS(ty, PART_WEAR) + add + r);
    if (PARTS(ty, PART_WEAR) > 0x2710) PARTS(ty, PART_WEAR) = 0x2710;
}

/* ============================================================================ the physics step */

/* 0000:2374 race_stop_inputs */
void race_stop_inputs(void)
{
    DSW(0x05D6) = 0;
    PHW(PLAYER, PH_WHEEL) = 0;
    PHW(PLAYER, PH_STEER_IN) = 0;
    PHW(PLAYER, PH_RPM_DOWN) = 0;
    PHW(PLAYER, PH_RPM_UP) = 0;
    PHW(PLAYER, PH_STEPS_AT_STOP) = PHW(PLAYER, PH_STEPS);
}

static s16 track_end_c(void) { return DSW(DS_race_type) == 0 ? 0x17C : 0x3FC; }

/* race_isr 0000:238c from 253b (race.md §4.7). The platform's race_isr has done 238c-2531 (tick
 * count, sound, BIOS chain, DS:8ACA and DS:0286 tests); this is the rest up to the iret. */
void race_phys_step(void)
{
    const u16 p = PLAYER;
    DSB(DS_race_step_count) = (u8)(DSB(DS_race_step_count) - 1);  /* DS:0609 */
    if (DSC(DS_race_step_count) > 0) return;
    DSB(DS_race_step_count) = 3;
    /* cli */
    if (DSB(DS_isr_race_busy) != 0) return;
    DSB(DS_isr_race_busy) = 1;
    /* sti */
    bool shift = 0, brake = 0, steer = 0, gas = 0;

    if (DSW(DS_kbd_dir_lock) == 0) {                               /* DS:4740 */
        s16 kx = DSC(DS_kbd_dir_x);
        if (kx) { steer = 1; DSW(DS_cursor_x) = kx > 0 ? 1 : 0xFFFF; }
        s16 ky = DSC(DS_kbd_dir_y);
        if (ky) { if (ky > 0) brake = 1; else gas = 1; }
        if (DSW(DS_demo_active) == 0 && DSB(DS_joy_present)) {
            joy_read();
            s16 jx = DSS(DS_joy_x);
            if (jx < DSS(DS_joy_thr_x_lo)) { steer = 1; DSB(DS_cursor_moved) = 1; DSW(DS_cursor_x) += 0xFFFF; }
            else if (jx > DSS(DS_joy_thr_x_hi)) { steer = 1; DSB(DS_cursor_moved) = 1; DSW(DS_cursor_x)++; }
            s16 jy = DSS(DS_joy_y);
            if (jy < DSS(DS_joy_thr_y_lo)) { gas = 1; DSB(DS_cursor_moved) = 1; DSW(DS_cursor_y) += 0xFFFF; }
            else if (jy > DSS(DS_joy_thr_y_hi)) { brake = 1; DSB(DS_cursor_moved) = 1; DSW(DS_cursor_y)++; }
        }
        if (DSW(DS_demo_active) == 0 && DSB(DS_mouse_present)) {
            mouse_poll();                                          /* 0f38:7a47 */
            if (DSS(DS_mouse_dy) > 0) { DSB(DS_cursor_moved) = 1; DSW(DS_cursor_y)++; brake = 1; }
            s16 mx = DSS(DS_mouse_dx);
            if (mx) { steer = 1; DSB(DS_cursor_moved) = 1; DSW(DS_cursor_x) += mx > 0 ? 1 : 0xFFFF; }
            if (DSW(DS_mouse_right) != 0) gas = 1;
        }
    }
    shift = DSB(DS_click_held) != 0 && DSB(DS_fire_prev) == 0;      /* DS:4732 rising edge */
    DSB(DS_fire_prev) = DSB(DS_click_held);
    if (DSB(DS_click_edge) != 0) { shift = 1; DSB(DS_click_edge) = 0; }   /* DS:47CD */

    if (DSW(DS_demo_active) != 0 || DSW(DS_auto_drive) != 0) {    /* the computer drives */
        if ((s16)(PHS(p, PH_RPM_UP) * 4 + PHS(p, PH_RPM)) < 0x1644) {
            if (DSS(DS_opp_seg) > DSS(DS_seg)) gas = 1;
            else if ((s16)(track_end_c() - 0x3C) < DSS(DS_seg)) gas = 1;
        } else if (PHS(p, PH_GEAR) < PHS(p, PH_NGEARS) && DSW(0x8228) != 0) {
            shift = 1; gas = 1; DSW(0x06AC)++;
        }
        if (PHS(p, PH_RPM) < 0x7D0 && PHS(p, PH_GEAR) > 1 && !gas) shift = 1;
        if (PHW(p, PH_AUTOMATIC) != 0 && PHS(p, PH_GEAR) > 0) shift = 0;
    }
    if (DSB(DS_racing) != 0) {
        DSL(DS_distance) += (u32)(s32)PHS(p, PH_SPEED);
        PHW(p, PH_STEPS)++;
    }
    if (track_end_c() < DSS(DS_seg)) {                            /* past the finish: stop the car */
        brake = 1; steer = 0; gas = 0;
        if (PHS(p, PH_SPEED) > 0xF) PHW(p, PH_SPEED) -= 0xF;
    }
    if (DSS(DS_ai_brake) > 0) {                                   /* DS:54DA */
        brake = 1; steer = 0; gas = 0;
        DSW(DS_ai_brake)--;
    }

    u16 car = cur_car();
    u16 eng = CARW(car, CAR_ENGINE);
    if (PHS(p, PH_RPM) > 0x1388) {
        PARTS(eng, PART_WEAR)++;
        PARTS(CARW(car, CAR_TRANS), PART_WEAR)++;
    }
    if ((DSL(DS_bios_ticks) & 0xF) == 0) PARTS(CARW(cur_car(), CAR_ENGINE), PART_WEAR)++;
    s16 w = PARTS(CARW(cur_car(), CAR_ENGINE), PART_WEAR);
    if (w >= 0x26AC) { DSB(p + PH_RESULT) |= 5; race_stop_inputs(); }

    if (gas) {
        PHW(p, PH_RPM) += PHW(p, PH_RPM_UP);
        if (PHW(p, PH_AUTOMATIC) != 0 && PHS(p, PH_RPM) > 0x163A) PHW(p, PH_RPM) = 0x163A;
        if (PHS(p, PH_RPM) > 0x1644) {
            PHW(p, PH_RPM) = 0x1644;
            s16 lim = (s16)-(s16)(w / 1000 - 0xF);
            DSW(DS_limiter_count)++;
            if (lim < DSS(DS_limiter_count)) { DSB(p + PH_RESULT) |= 5; race_stop_inputs(); }
        }
    } else if (PHS(p, PH_RPM) > 0x5DC || (PHS(p, PH_GEAR) > 0 && PHS(p, PH_RPM) > PHS(p, PH_RPM_DOWN))) {
        PHW(p, PH_RPM) -= PHW(p, PH_RPM_DOWN);
    }
    if (PHS(p, PH_RPM) < 0x1644) DSW(DS_limiter_count) = 0;

    bool stall = PHS(p, PH_SPEED) < 5 && !gas && PHS(p, PH_GEAR) > 0;
    bool doshift = shift || stall;
    if (!doshift && PHW(p, PH_AUTOMATIC) != 0 && PHS(p, PH_AUTO_MODE) > 1) {
        if (gas && PHS(p, PH_RPM) > PHS(p, PH_AUTO_SHIFT) && PHS(p, PH_GEAR) < PHS(p, PH_NGEARS)) doshift = 1;
        else if (!gas && PHS(p, PH_GEAR) > 1 && PHW(p, PH_BAND) == 0) doshift = 1;
    }
    if (doshift) {
        u16 trn = CARW(cur_car(), CAR_TRANS);
        PARTS(trn, PART_WEAR) = (s16)(PARTS(trn, PART_WEAR) + (PHS(p, PH_RPM) > 0x1388 ? 0xF : 3));
        if (DSW(DS_demo_active) == 0) {
            s16 tw = PARTS(CARW(cur_car(), CAR_TRANS), PART_WEAR);
            bool blow = tw >= 0x26AC;
            if (!blow && PHW(p, PH_AUTOMATIC) == 0 && gas && PHS(p, PH_RPM) > 0x1388) {
                s16 r = rnd(0xCCD);
                if ((s16)(tw / 100 + 100) > r) blow = 1;
            }
            if (blow) { DSB(p + PH_RESULT) |= 0x11; race_stop_inputs(); }
        }
        if ((brake || (PHW(p, PH_BAND) == 0 && !gas)) && PHS(p, PH_GEAR) > 0) {
            PHW(p, PH_GEAR)--;
        } else if ((gas || (PHS(p, PH_BAND) > 0 && !brake)) && PHS(p, PH_GEAR) < PHS(p, PH_NGEARS)) {
            PHW(p, PH_GEAR)++;
        }
        if (PHW(p, PH_AUTOMATIC) != 0) {
            if (shift) PHW(p, PH_AUTO_MODE) = PHW(p, PH_AUTO_MODE) == 1 ? 2 : 1;
            if (stall || PHW(p, PH_GEAR) == 0) PHW(p, PH_AUTO_MODE) = 1;
            if (PHW(p, PH_AUTO_MODE) == 1) PHW(p, PH_GEAR) = 0;
        } else if (stall) {
            PHW(p, PH_GEAR) = 0;
        }
        PHW(p, PH_GEAR_DIRTY) = 1;
        PHW(p, PH_CUR_RATIO) = PHW(p, (u16)(PH_RATIO + PHS(p, PH_GEAR) * 2));
        PHW(p, PH_RPM) = (u16)(PHS(p, PH_SPEED) * PHS(p, PH_CUR_RATIO));    /* imul, low word */
        if (PHW(p, PH_RPM) == 0) PHW(p, PH_RPM) = 0x5E1;
    }
    /* rpm band */
    s16 k = -1;
    u16 crv;
    do {
        k++;
        crv = (u16)(p + PH_CURVE + PHS(p, PH_GEAR) * 0x18 + k * 8);
    } while (k < 2 && DSS(crv + CRV_HI) < PHS(p, PH_RPM));
    if (k != PHS(p, PH_BAND) || PHW(p, PH_GEAR_DIRTY) != 0) {
        PHW(p, PH_BAND) = (u16)k;
        PHW(p, PH_RPM_UP) = DSW(crv + CRV_UP);
        PHW(p, PH_RPM_DOWN) = DSW(crv + CRV_DOWN);
    }
    /* speed */
    if (brake || (DSW(DS_offroad) != 0 && PHS(p, PH_SPEED) > 8)) {
        s16 v = (s16)(PHS(p, PH_SPEED) - ((PHS(p, PH_SPEED) >> 4) + 2));
        if (v < 0) v = 0;
        PHS(p, PH_SPEED) = v;
        PHW(p, PH_RPM) = (u16)(v * PHS(p, PH_CUR_RATIO));
        if (PHS(p, PH_RPM) < 0x5DC) PHW(p, PH_RPM) = 0x5DC;
    } else if (PHW(p, PH_GEAR) == 0 && PHW(p, PH_SPEED) != 0) {
        PHW(p, PH_SPEED)--;                                        /* coasting in neutral */
        PHW(p, PH_RPM) = (u16)(PHS(p, PH_SPEED) * 0x1E + 0x5DC);
    } else if (PHW(p, PH_CUR_RATIO) != 0) {
        PHS(p, PH_SPEED) = idiv32_16(PHS(p, PH_RPM), PHS(p, PH_CUR_RATIO), NULL);
    }
    /* steering */
    s16 ss = DSS(DS_cursor_x);                                     /* DS:472A steer step */
    if (steer && abs16((s16)(PHS(p, PH_STEER_IN) + ss)) > abs16(PHS(p, PH_STEER_IN))) {
        s16 si = abs16(PHS(p, PH_STEER_IN));
        s16 di = abs16(PHS(p, PH_WHEEL));
        if ((s16)(abs16(ss) + di) == abs16((s16)(PHS(p, PH_WHEEL) + ss)) && si < di)
            PHW(p, PH_STEER_IN) = PHW(p, PH_WHEEL);
        PHW(p, PH_STEER_IN) += (u16)(ss * 2);
        if (abs16(PHS(p, PH_STEER_IN)) > 0x18) PHS(p, PH_STEER_IN) = PHS(p, PH_STEER_IN) > 0 ? 0x18 : -0x18;
        DSW(DS_cursor_x) = 0;
        PHW(p, PH_WHEEL) = PHW(p, PH_STEER_IN);
    } else {
        PHW(p, PH_STEER_IN) = 0;
    }
    if (abs16(PHS(p, PH_WHEEL)) > 0) {
        s16 wh = PHS(p, PH_WHEEL);
        PHS(p, PH_WHEEL) = (s16)(wh + (wh > 0 ? -((wh >> 2) + 1) : ((s16)(-wh) >> 2) + 1));
    }
    if (DSB(DS_racing) != 0 && road_edge_collision()) {
        DSB(p + PH_RESULT) |= 0x21;
        race_stop_inputs();
    }
    DSB(DS_isr_race_busy) = 0;
}

/* ============================================================================ opponent */

/* 0000:d1de opp_gear: lowest gear whose band-1 top is above the rpm for speed v */
void opp_gear(s16 v, u16 p)
{
    PHW(p, PH_GEAR) = 1;
    u16 ratio = (u16)(p + 0x36), b1 = (u16)(p + 0x64), b0 = (u16)(p + 0x5C), b2 = (u16)(p + 0x6C);
    u16 si;
    for (;;) {
        PHW(p, PH_CUR_RATIO) = DSW(ratio);
        PHW(p, PH_RPM) = (u16)(PHS(p, PH_CUR_RATIO) * v);
        si = b1;
        if (DSS(si + CRV_HI) > PHS(p, PH_RPM)) {
            u16 di = PHS(p, PH_RPM) >= DSS(si) ? 1 : 0;
            PHW(p, PH_BAND) = di;
            if (di == 0) si = b0;
            DSW(0x6CB8) = di;
            break;
        }
        if (PHW(p, PH_GEAR) == PHW(p, PH_NGEARS)) {
            si = b2;
            PHW(p, PH_BAND) = 2;
            DSW(0x6CB8) = 2;
            break;
        }
        ratio = (u16)(ratio + 2); b1 = (u16)(b1 + 0x18); b0 = (u16)(b0 + 0x18); b2 = (u16)(b2 + 0x18);
        PHW(p, PH_GEAR)++;
    }
    PHW(p, PH_BAND_UP) = DSW(si + CRV_UP);                         /* band_hi NOT updated (quirk) */
    PHS(p, PH_SPEED) = idiv32_16(PHS(p, PH_RPM), PHS(p, PH_CUR_RATIO), NULL);
}

/* 0000:d29d opp_ai: the opponent's engine, once per player physics step since the last frame */
void opp_ai(void)
{
    s16 v0 = PHS(OPP, PH_SPEED);
    s16 lim = 0;              /* PORT: [bp-2] is uninitialised in the original; only read after a write */
    /* cli */
    s16 n = (s16)(PHS(PLAYER, PH_STEPS) - DSS(DS_ai_steps_done));
    DSW(DS_ai_steps_done) = PHW(PLAYER, PH_STEPS);
    /* sti */
    for (s16 i = 0; i < n; i++) {
        s16 os = DSS(DS_opp_seg);
        if (TRKS(TRK_YAW, os + 1) != TRKS(TRK_YAW, os)) {          /* in a curve */
            if (DSW(0x54D6) != 0) continue;
            DSW(0x54D6) = 1;
            s16 cl = DSC((u16)(DSW(DS_opp_rec) + OR_CLUMSY));
            if (DSS(DS_opp_seg) <= DSS(DS_seg)) cl = (s16)(cl >> 1);
            lim = (s16)(DSS(DS_opp_vmax) - cl);
            if (lim < PHS(OPP, PH_SPEED)) opp_gear(lim, OPP);
            continue;
        }
        DSW(0x54D6) = 0;
        s16 d = (s16)(DSS(DS_opp_seg) - DSS(DS_seg));
        if (d > 0xC && !(DSW(DS_race_type) == 1 && DSW(DS_vs_king) != 0)) {
            lim = (PHS(PLAYER, PH_SPEED) >= 0x61 || DSW(DS_vs_king) != 0) ? DSS(DS_opp_v80) : DSS(DS_opp_v70);
            if (lim < PHS(OPP, PH_SPEED)) { opp_gear(lim, OPP); continue; }
        } else if (d > -5 && d < 5) {
            lim = (s16)(PHS(PLAYER, PH_SPEED) + DSS(DS_opp_slack));
            if (DSS(DS_seg) >= 0x1E && lim < DSS(DS_opp_v70)) lim = DSS(DS_opp_v70);
            if (lim < PHS(OPP, PH_SPEED)) { opp_gear(lim, OPP); continue; }
        }
        if (DSS(DS_police_state) >= 2 && PHS(PLAYER, PH_SPEED) > 0x32) {
            s16 l2 = (s16)(PHS(PLAYER, PH_SPEED) + 0x20);
            if (PHS(OPP, PH_SPEED) > l2) { opp_gear(l2, OPP); continue; }
        }
        /* accelerate */
        PHW(OPP, PH_RPM) += PHW(OPP, PH_BAND_UP);
        if (PHS(OPP, PH_RPM) > DSS(DS_opp_shift_rpm)) PHW(OPP, PH_RPM) = DSW(DS_opp_shift_rpm);
        if (PHS(OPP, PH_RPM) > PHS(OPP, PH_BAND_HI)) {
            PHW(OPP, PH_BAND)++;
            u16 c = (u16)(OPP + PH_CURVE + PHS(OPP, PH_GEAR) * 0x18 + PHS(OPP, PH_BAND) * 8);
            PHW(OPP, PH_BAND_HI) = DSW(c + CRV_HI);
            PHW(OPP, PH_BAND_UP) = DSW(c + CRV_UP);
        }
        if (PHS(OPP, PH_GEAR) < PHS(OPP, PH_NGEARS) &&
            ((PHW(OPP, PH_AUTO_MODE) != 0 && PHS(OPP, PH_RPM) >= PHS(OPP, PH_AUTO_SHIFT)) ||
             PHS(OPP, PH_RPM) >= DSS(DS_opp_shift_rpm))) {
            PHW(OPP, PH_GEAR)++;
            if (PHW(OPP, PH_AUTO_MODE) == 0) {
                if (PHS(OPP, PH_GEAR) < PHS(OPP, PH_NGEARS))
                    DSS(DS_opp_shift_rpm) = (s16)(PHS(OPP, PH_REDLINE) - (s16)(DSS(DS_opp_skill) * 100));
                else if (DSS(DS_cpu_speed) < 4 && DSS(DS_driver_id) != -6)
                    DSW(DS_opp_shift_rpm) = 0x14B4;
                else
                    DSW(DS_opp_shift_rpm) = 0x1518;
            }
            PHW(OPP, PH_CUR_RATIO) = PHW(OPP, (u16)(PH_RATIO + PHS(OPP, PH_GEAR) * 2));
            PHW(OPP, PH_RPM) = (u16)(PHS(OPP, PH_SPEED) * PHS(OPP, PH_CUR_RATIO));
            DSW(0x6CB8) = 1;
            u16 c = (u16)(OPP + PH_CURVE + PHS(OPP, PH_GEAR) * 0x18 + 8);
            if (DSS(c + CRV_HI) <= PHS(OPP, PH_RPM)) { DSW(0x6CB8) = 2; c = (u16)(c + 8); }
            else if (DSS(c + CRV_LO) > PHS(OPP, PH_RPM)) { DSW(0x6CB8) = 0; c = (u16)(c - 8); }
            PHW(OPP, PH_BAND) = DSW(0x6CB8);
            PHW(OPP, PH_BAND_HI) = DSW(c + CRV_HI);
            PHW(OPP, PH_BAND_UP) = DSW(c + CRV_UP);
        }
        PHS(OPP, PH_SPEED) = idiv32_16(PHS(OPP, PH_RPM), PHS(OPP, PH_CUR_RATIO), NULL);
    }
    (void)lim;
    if (DSS(DS_opp_seg) >= DSS(DS_track_end)) {                   /* past the line: slow down */
        s16 dec = (s16)(DSS(DS_seg) + 5) < DSS(DS_opp_seg) ? 5 : 10;
        PHS(OPP, PH_SPEED) = v0;
        if (dec < v0) PHS(OPP, PH_SPEED) = (s16)(v0 - dec);
        else PHW(OPP, PH_SPEED) = 0;
    }
}

/* 0000:d544 speed_to_step: eighths of a segment per frame */
s16 speed_to_step(s16 v)
{
    if (v <= 0) return 0;
    if (DSS(DS_cpu_speed) < 4) return v <= 0x47 ? (s16)((v >> 3) + 1) : (s16)((v >> 2) - 8);
    static const s16 lim[15] = { 0x0D, 0x1B, 0x29, 0x37, 0x45, 0x53, 0x61, 0x65, 0x69, 0x6D, 0x71,
                                 0x75, 0x79, 0x7D, 0x81 };
    for (int i = 0; i < 15; i++) if (v <= lim[i]) return (s16)(i + 1);
    return 16;
}

/* 0000:d624 player_advance */
void player_advance(void)
{
    s16 st = PHW(PLAYER, PH_SPEED) != 0 ? speed_to_step(PHS(PLAYER, PH_SPEED)) : 0;
    DSS(DS_step_per_frame) = st;
    DSW(DS_substep) += (u16)st;
    if (DSS(DS_substep) >= 8) road_advance_segment();
    if (DSS(DS_seg) >= DSS(DS_track_end) && DSW(DS_stats_done) != 0 && PHS(PLAYER, PH_SPEED) <= 0) {
        if (DSW(DS_player_ahead) != 0) DSB(DS_opp_result) |= 1;
        else DSB(DS_player_result) |= 1;
        race_stop_inputs();
    }
}

/* 0000:d67d opp_advance_steer: opponent track position and lateral target */
void opp_advance_steer(void)
{
    s16 st = PHW(OPP, PH_SPEED) != 0 ? speed_to_step(PHS(OPP, PH_SPEED)) : 0;
    DSS(DS_opp_seg_step) = st;
    if (PHW(OPP, PH_SPEED) == 0) DSW(DS_opp_seg_step) = 0;
    DSW(DS_opp_seg_frac) += DSW(DS_opp_seg_step);
    if (DSS(DS_opp_seg_frac) >= 8 && DSS(DS_opp_seg_step) > 0) {
        DSS(DS_opp_seg) = (s16)(DSS(DS_opp_seg) + (DSS(DS_opp_seg_frac) >> 3));
        DSW(DS_opp_seg_frac) &= 7;
    }
    if (DSS(DS_opp_seg) >= DSS(DS_track_end) && DSW(DS_stats_done) != 0 && PHS(PLAYER, PH_SPEED) <= 0) {
        if (DSW(DS_player_ahead) == 0) DSB(DS_player_result) |= 1;
        else DSB(DS_opp_result) |= 1;
        race_stop_inputs();
        return;
    }
    s16 fl = (s16)(TRKW(TRK_C, DSS(DS_opp_seg) + 4) & 0xE0);
    s16 d = (s16)(DSS(DS_opp_seg) - DSS(DS_seg));
    s16 seg = DSS(DS_seg);
    s16 cc = (s16)(TRKS(TRK_YAW, seg + 4) - TRKS(TRK_YAW, seg + 5));
    s16 lat = DSS(DS_cam_x);
#define MID ((s16)((s16)(DSS(DS_ai_edge_left) + DSS(DS_ai_edge_right)) >> 1))
#define TARGET DSS(DS_opp_lat_target)
#define SIDE DSW(0x54D4)
#define RECENTRE DSW(0x8648)
    if (d > 0xC) {
        TARGET = MID;
        if (cc != 0) SIDE = cc < 0 ? 1 : 0;
        goto recentre_move;
    }
    if (d > 6) {                                                   /* 7..12 ahead */
        DSW(0x7D7E) = 1;
        goto near;
    }
    if ((DSS(DS_cpu_speed) < 4 ? 4 : 2) < d) {                     /* 3..6 (5..6) ahead */
        if (cc != 0) goto curve_side;
        if (fl != 0) { TARGET = MID; goto move; }
        if (RECENTRE == 0) goto move;
        TARGET = SIDE != 0 ? (s16)(MID + 0x12C) : (s16)(MID - 0xC8);
        RECENTRE = 0;
        SIDE = SIDE == 0 ? 1 : 0;
        goto move;
    }
    if (d > -4) {                                                  /* level with the player */
        if (DSS(DS_opp_seg_step) > DSS(DS_step_per_frame)) {       /* he is faster */
            if (DSS(DS_opp_sprite) >= 9) {                         /* side views: overtaking */
                s16 t = (s16)(lat - 0x14A), s;
                if (t > (s16)(DSS(DS_edge_left) + 0x1E0)) {
                    SIDE = 1; TARGET = t;
                    if ((s16)(lat - DSS(DS_opp_lat)) >= 0x118) goto move;
                    s = seg;
                } else {
                    t = (s16)(lat + 0x1AE);
                    if (t < (s16)(DSS(DS_edge_right) - 0x1E)) {
                        SIDE = 0; TARGET = (s16)(t + 0xC8);
                        if ((s16)(DSS(DS_opp_lat) - lat) >= 0x244) goto move;
                    }
                    s = (s16)(seg - 1);
                }
                DSS(DS_opp_seg) = (s16)(s - 1);                    /* no room: he drops back */
                opp_gear(PHS(PLAYER, PH_SPEED), OPP);
                goto move;
            }
            if (DSS(DS_opp_sprite) != 0 && DSS(DS_opp_sprite) != 2) goto move;
            TARGET = lat > DSS(DS_opp_lat) ? (s16)(lat - 0x14A) : (s16)(lat + 0x212);
            goto move;
        }
        if (DSS(DS_opp_sprite) != 0) goto move;
        TARGET = (s16)(lat - 0x14A);
        goto move;
    }
    if (d > -6) {                                                  /* -5, -4 */
        DSW(0x7D7E) = 1;
        goto near;
    }
    TARGET = MID;                                                  /* far behind */
    goto recentre_move;
near:
    if (cc != 0) goto curve_side;
    if (fl != 0) { TARGET = MID; goto move; }
    TARGET = SIDE != 0 ? (s16)(MID - 0xC8) : (s16)(MID + 0x12C);
    goto move;
curve_side:
    if (cc < 0) { TARGET = (s16)(MID - 0xC8); SIDE = 1; }
    else        { TARGET = (s16)(MID + 0x12C); SIDE = 0; }
    goto move;
recentre_move:
    RECENTRE = 1;
move:
    if (DSS(DS_opp_lat) > TARGET) {
        s16 s = (s16)((s16)(DSS(DS_opp_lat) - TARGET) >> 3);
        if ((s16)(DSS(DS_opp_lat) - s) > TARGET) DSS(DS_opp_lat) = (s16)(DSS(DS_opp_lat) - s);
    } else {
        s16 s = (s16)((s16)(TARGET - DSS(DS_opp_lat)) >> 3);
        if ((s16)(s + DSS(DS_opp_lat)) < TARGET) DSS(DS_opp_lat) = (s16)(DSS(DS_opp_lat) + s);
    }
#undef MID
#undef TARGET
#undef SIDE
#undef RECENTRE
}

/* 0000:d992 police_spawn: the opponent turns into the police car */
void police_spawn(void)
{
    s16 v = PHS(OPP, PH_SPEED);
    DSW(DS_opp_rec) = 0x80AC;                                      /* record 10's driver values */
    model_car_setup(0x19, OPP);
    DSS(DS_opp_skill) = DSC((u16)(DSW(DS_opp_rec) + OR_SKILL));
    opp_gear(v, OPP);
    opp_palette_set(5);
    memmove(mp(DGROUP, DS_opp_sprites), mp(DGROUP, DS_police_sprites), 0x30);
    DSS(DS_opp_vmax) = car_max_speed(0x19);
    long double vmax = (long double)DSS(DS_opp_vmax);
    DSS(DS_opp_v70) = (s16)ftol(vmax * dconst(0x6980));
    DSS(DS_opp_v80) = (s16)ftol(vmax * dconst(0x6974));
}

/* 0000:c407 finish_stats: once per race */
void finish_stats(void)
{
    if (DSW(DS_stats_done) != 0) return;
    s16 a = (s16)((s16)((DSS(DS_opp_seg) - DSS(DS_seg)) << 3) + DSS(DS_opp_seg_frac));
    DSW(DS_player_ahead) = a <= DSS(DS_substep) ? 1 : 0;
    s32 steps = PHS(PLAYER, PH_STEPS);
    if (steps == 0) div_error();                                   /* _aFldiv: divide error */
    DSW(DS_avg_speed) = (u16)(s32)((s32)DSL(DS_distance) / steps);
    float t = (float)((long double)PHS(PLAYER, PH_STEPS) / dconst(0x6954));
    memcpy(mp(DGROUP, DS_race_time), &t, 4);
    long double num = (long double)(s16)(DSS(DS_seg) - 0xC8) * dconst(0x695C);
    s16 den = (s16)(DSS(DS_opp_seg) - 0xC8);
    s16 pct = (s16)ftol(num / (long double)den);                   /* 0/0 or x/0: indefinite -> 0 */
    if (pct > 0x63) pct = 0x63;
    DSS(DS_pct_covered) = pct;
    DSW(DS_stats_done) = 1;
}

/* ============================================================================ per frame */

/* 0000:da25 race_step: per-frame race update */
void race_step(void)
{
    if (PHW(PLAYER, PH_AUTO_MODE) != 0) {
        if (PHW(PLAYER, PH_AUTO_MODE) != DSW(0x8630)) {
            DSW(0x8630) = PHW(PLAYER, PH_AUTO_MODE);
            gear_label((s16)(PHS(PLAYER, PH_AUTO_MODE) + 4));
        }
    } else if (PHW(PLAYER, PH_GEAR) != DSW(0x8630)) {
        DSW(0x8630) = PHW(PLAYER, PH_GEAR);
        gear_label(PHS(PLAYER, PH_GEAR));
    }
    if (PHW(PLAYER, PH_BLOWOUT_SEG) != 0 && DSS(DS_seg) > PHS(PLAYER, PH_BLOWOUT_SEG)) DSB(DS_tires_blown) = 1;
    if (DSW(DS_squeal_on) == 0) { if (abs16(PHS(PLAYER, PH_WHEEL)) >= 0xC) DSW(DS_squeal_on) = 1; }
    else if (abs16(PHS(PLAYER, PH_WHEEL)) < 0xA) DSW(DS_squeal_on) = 0;
    opp_ai();
    switch (DSW(DS_police_state)) {
    case 1:
        if (DSS(DS_seg) >= DSS(DS_police_seg)) {
            if ((s16)(DSS(DS_opp_seg) + 5) > DSS(DS_seg)) DSW(DS_police_state) = 0;
            else { DSW(DS_police_state)++; police_spawn(); }
        }
        break;
    case 2:
        if ((s16)(DSS(DS_opp_seg) + 0xF) >= DSS(DS_seg)) { DSW(DS_police_state)++; siren_set(1); }
        break;
    case 3:
        if ((s16)(DSS(DS_opp_seg) - 2) > DSS(DS_seg)) {            /* the cop passed you: pulled over */
            DSB(DS_player_result) |= 0x41;
            race_stop_inputs();
            return;
        }
        break;
    }
    player_advance();
    opp_advance_steer();
    if (DSS(DS_seg) >= DSS(DS_track_end) || DSS(DS_opp_seg) >= DSS(DS_track_end)) finish_stats();
    if (DSW(DS_player_result) != 0 || DSW(DS_opp_result) != 0) {
        if (DSS(DS_police_state) >= 2) {
            if (DSW(DS_player_result) != 0) DSB(DS_player_result) |= 0x41;
            siren_set(0);
        }
        race_stop_inputs();
        return;
    }
    if (DSW(DS_opp_alongside) == 0) return;                        /* DS:8BC2 */
    if (DSW(DS_demo_active) != 0) return;
    if (track_end_c() <= DSS(DS_seg)) return;
    if (DSW(DS_opp_rear_end) != 0 && DSS(DS_police_state) <= 1) {  /* ran into the opponent */
        DSB(DS_player_result) |= 3;
        race_stop_inputs();
        return;
    }
    if (DSW(DS_opp_sprite) == 0 && DSS(DS_step_per_frame) > 1) {  /* right beside, moving */
        DSW(0x54D8)++;
        if (PHS(PLAYER, PH_STEER_IN) < 0) {                        /* steering left into him */
            s16 seg = DSS(DS_seg);
            if (TRKS(TRK_YAW, seg) < TRKS(TRK_YAW, seg + 1) &&
                (s16)((s16)(DSS(DS_ai_edge_left) + DSS(DS_ai_edge_right)) >> 1) < DSS(DS_cam_x))
                return;
            fx_hit();
            if (DSW(0x54D2) == 0) { DSW(DS_opp_seg)--; DSW(0x54D2) = 1; }
            else DSW(0x54D2) = 0;
            return;
        }
        if (DSW(0x7D7E) == 0 || DSS(0x54D8) <= 3 || DSS(DS_police_state) >= 2) return;
        if (rnd((s16)((DSC((u16)(DSW(DS_opp_rec) + OR_CLUMSY)) >> 2) + 2)) == 0) {   /* bumped off line */
            fx_hit();
            fx_hit();
            DSW(DS_cam_x) += 0x12C;
            if (PHS(PLAYER, PH_SPEED) > 0x14) DSW(DS_ai_brake) = 2;
            DSW(0x7D7E) = 0;
            DSW(DS_heading) += rnd(2) != 0 ? 0xF : 0xFFF1;
            fx_hit();
        }
    } else if (DSW(DS_opp_sprite) == 0) {
        return;
    }
    DSW(0x54D8) = 0;
}
