/* Race: the drive entry points and the frame loop (port/spec/race.md §4.1-§4.3, from the disassembly).
 *
 *   0000:8d26 drive_to        0000:8d9c opp_palette_set     0000:8e2d drive_run
 *   0000:8ea8 race_logic      0f38:7b22 race_run
 *
 * Timing (race.md §7, race_render.md §7): the frame loop calls road_step (2645:213d), whose
 * road_frame busy-waits until RACE_TICKS_PER_FRAME (9) ticks of the 72.8 Hz timer have passed since
 * the frame's start, pumping the host; the player physics runs in the race ISR (platform race_isr ->
 * modules.race_phys_step) on every 12th tick. */
#include "game/race.h"
#include "game/race_int.h"
#include "game/garage.h"

#include "platform/platform.h"
#include "platform/video.h"
#include "sound/sound.h"

/* 0000:8d26 drive_to */
void drive_to(void)
{
    DSB(DS_click_held) = 0;                                        /* DS:4732 fire */
    DSB(DS_joy_buttons) = 0;
    DSW(DS_mouse_left) = 0;
    DSW(DS_click_seen) = 0;                                        /* DS:47EE drive_abort */
    if (DSW(DS_drive_dest) == 0) return;
    u16 car = cur_car();
    if (CARS(car, CAR_GAS) <= 2) CARW(car, CAR_GAS) = 0;           /* 0.2 gal per trip */
    else CARW(car, CAR_GAS) -= 2;
    drv_pal_black();                                               /* (*DS:78BE)() */
    if (DSW(DS_click_seen) != 0) return;
    cursor_ctl(-4);
    DSW(DS_has_clock) = part_grade(CARW(cur_car(), CAR_TRANS)) == 0 ? 1 : 0;   /* automatic: clock */
    phys_reset(PLAYER);
    DSW(DS_race_state) = 0;
    DSB(DS_racing) = 0;
    drive_run();
}

/* 0000:8d9c opp_palette_set: EGA attribute registers 0Ch / 03h = the opponent's paint pair */
void opp_palette_set(s16 idx)
{
    if (DSS(DS_driver_id) != -2 && DSS(DS_driver_id) != -6) return;
    u8 a, b;
    if (idx == -1) {                                               /* restore the saved pair */
        a = DSB(0x51AC);
        b = DSB(0x51AD);
        DSB(0x51AD) = 0xFF;
        DSB(0x51AC) = 0xFF;
    } else {
        if (DSB(0x51AC) == 0xFF) { DSB(0x51AC) = DSB(0x044C); DSB(0x51AD) = DSB(0x0443); }
        a = DSB((u16)(DS_car_colour_pairs + idx * 2));
        b = DSB((u16)(DS_car_colour_pairs + idx * 2 + 1));
    }
    if (DSB(0x8249) != 0) {                                        /* palette not blacked out */
        ega_set_palreg(0xC, (s8)a);
        ega_set_palreg(3, (s8)b);
    }
    DSB(0x044C) = a;
    DSB(0x0443) = b;
}

/* 0000:8e2d drive_run */
s16 drive_run(void)
{
    if (DSB(DS_racing) != 0) drv_pal_black();
    DSW(DS_lights_green) = 0;
    DSW(0x8228) = 0;
    s16 opp_colour = DSS(0x827C);
    set_paint_palette(CARC(cur_car(), CAR_COLOUR));               /* 0000:67cf: registers 6 / 7 */
    if (DSB(DS_racing) != 0) opp_palette_set(opp_colour);
    s16 r = race_run();
    squeal_set(0);                                                 /* 0f38:7741(0) */
    siren_set(0);
    DSW(DS_cursor_x) = DSW(0x4746);                                /* mouse position restore */
    DSW(DS_cursor_y) = DSW(0x4748);
    DSW(0x4746) = 0;
    DSW(0x4748) = 0;
    return r;
}

/* 0000:8ea8 race_logic: per-frame state machine (cruise auto-drive / race start lights) */
u16 race_logic(void)
{
    const u16 p = PLAYER;
    if (DSB(DS_racing) != 0) {
        switch (DSW(DS_race_state)) {
        case 0:
            DSW(0x05D6) = 0;
            if (DSW(DS_skip_fx) == 0) music_mute(1);               /* 0f38:7001(1) */
            race_isr_enter();                                      /* 0000:1aad */
            input_reset();                                         /* 0000:0e24 */
            DSW(DS_race_state) = 1;
            status_print(-1, ds_str(0x51AE), 0);                   /* "Get ready !" */
            {
                s32 r = rnd(0x67);                                 /* cdq: sign-extended */
                DSL(DS_wait_deadline) = (u32)((s32)DSL(DS_ticks) + r + 0x6E);
            }
            return DSW(DS_player_result) | DSW(DS_opp_result);
        case 1:
            if (PHS(p, PH_SPEED) > 0) {                            /* moved before green */
                race_isr_leave();
                DSB(DS_player_result) |= 0x81;
                return 1;
            }
            if (!tick_after(DSL(DS_wait_deadline), ticks_now())) {
                DSW(DS_race_state) = 2;
                status_print(-1, ds_str(0x51BA), 0);               /* "Go !" */
                DSW(DS_lights_green) = 1;
            }
            return DSW(DS_player_result) | DSW(DS_opp_result);
        case 2:
            race_step();                                           /* 0000:da25 */
            if (DSW(DS_player_result) != 0 || DSW(DS_opp_result) != 0) return 1;
            if (PHS(p, PH_SPEED) > 0) {
                status_print(-1, ds_str(0x51BF), 0);               /* "    " */
                DSW(DS_race_state) = 3;
            }
            return 0;
        case 3:
            if (DSW(DS_player_result) != 0 || DSW(DS_opp_result) != 0) return 1;
            race_step();
            return 0;
        default:
            return DSW(DS_race_state);
        }
    }
    /* ---- cruising */
    if (DSW(DS_race_state) == 0) {
        DSW(0x05D6) = 0;
        if (DSW(DS_skip_fx) == 0) music_mute(1);
        race_isr_enter();
        input_reset();
        DSW(DS_race_state) = 1;
        status_print(-1, ds_str(0x51C4), 0);                       /* "Cruising in town..." */
        PHW(p, PH_GEAR_DIRTY) = 1;
        PHW(p, PH_GEAR) = 1;
        if (PHW(p, PH_AUTO_MODE) == 1) PHW(p, PH_AUTO_MODE) = 2;   /* selector to D */
        DSW(DS_cruise_gain) = DSW(DS_cruise_accel);                /* [0] = 2Ah */
        return 0;
    }
    if (DSW(DS_race_state) != 1) return DSW(DS_race_state);
    if (DSW(DS_demo_active) == 0) {
        u8 k = (u8)key_read();                                     /* 0000:2db6 */
        if (k == 0x1B || k == 0x1F) DSW(DS_click_seen) = 1;        /* DS:47EE abort */
        if (DSB(DS_mouse_present) != 0) { mouse_buttons(); DSW(DS_click_seen) |= DSW(DS_mouse_left); }
        if (DSB(DS_joy_present) != 0) { joy_read(); DSW(DS_click_seen) |= (u16)(s16)DSC(DS_joy_buttons); }
    }
    if (DSW(DS_demo_active) == 0x63) { DSW(DS_demo_active)--; platform_exit(); }
    s16 seg = DSS(DS_seg);
    if ((TRKW(TRK_L, seg + 1) & 1) || (TRKW(TRK_R, seg + 1) & 1) || DSW(DS_click_seen) != 0) {
        DSB(DS_click_code) = 0;                                    /* DS:4736 */
        DSB(DS_click_held) = 0;
        race_isr_leave();
        return 1;                                                  /* arrived (or aborted) */
    }
    PHW(p, PH_RPM) += DSW(DS_cruise_gain);
    if (PHS(p, PH_RPM) > 0x125C) PHW(p, PH_RPM) = 0x125C;
    if (PHW(p, PH_GEAR) != 0)
        PHS(p, PH_SPEED) = idiv32_16(PHS(p, PH_RPM), DSS((u16)(DS_cruise_ratio + PHS(p, PH_GEAR) * 2)), NULL);
    if (PHS(p, PH_RPM) <= 0x1194) return 0;
    if (PHS(p, PH_GEAR) < PHS(p, PH_NGEARS)) {                     /* up-shift at 4501 rpm */
        PHW(p, PH_GEAR_DIRTY) = 1;
        PHW(p, PH_GEAR)++;
        u16 g2 = (u16)(PHS(p, PH_GEAR) * 2);
        DSW(DS_cruise_gain) = DSW((u16)(DS_cruise_accel + g2));
        PHW(p, PH_RPM) = (u16)(DSS((u16)(DS_cruise_ratio + g2)) * PHS(p, PH_SPEED));
        return 0;
    }
    DSW(DS_cruise_gain) = 0;
    return 0;
}

/* 0f38:7b22 race_run: cockpit set-up, split screen, the frame loop, restore (VGA path) */
s16 race_run(void)
{
    const u16 p = PLAYER;
    FarPtr pic;
    Rect r;
    s16 pic_w, shown_rpm, shown_speed, w = 0, res;
    do {
        gfx_screen_mode(3);
        arena_reset();                                             /* 0f38:a054 */
        drv_pal_black();
        DSW(0x58E4) = 0;                                           /* CGA double buffer: VGA 0 */
        DSW(0x58EE) = DSS(DS_driver_id) == -2 ? 1 : 0;
        pic = pic_get(0x3F6, 2);                                   /* LIB2 #14 cockpit */
        if (DSW(DS_has_clock) != 0) {
            FarPtr face = pic_get(0x4D8, 2);                       /* LIB2 #240 clock face */
            Rect rf = { (s16)desc_w(face), (s16)desc_h(face), 0, 0, 0x78, 0x7A };
            drv_blit(face, pic, &rf, 1);
            arena_pop(1);
        }
        pic_w = (s16)desc_w(pic);
        r = (Rect){ pic_w, 0x65, 0, 0, 0, 0 };
        drv_blit(pic, ds_far(DS_page1_ptr), &r, 1);
        if (DSS(DS_driver_id) == -2) drv_blit(pic, ds_far(DS_page2_ptr), &r, 1);
        r = (Rect){ pic_w, 0x5A, 0, 0x64, 0, 0 };                  /* VGA: dy 0 */
        drv_blit(pic, ds_far(DS_page0_ptr), &r, 1);
        arena_pop(1);
        road_load_graphics();                                      /* 2645:2429 */
        shifter_draw(PHS(p, PH_AUTO_MODE), PHS(p, PH_NGEARS));
        shifter_knob(ds_far(DS_page0_ptr), p);
        w = 0;
        DSW(DS_wheel_shown) = 100;
        wheel_load();
        wheel_draw(0, DSS(DS_wheel_shown), ds_far(DS_page0_ptr));
        DSW(DS_wheel_shown) = 0;
        shown_rpm = PHS(p, PH_RPM);
        shown_speed = PHS(p, PH_SPEED);
        speedo_init(PHS(p, PH_SPEED));
        shown_speed = needle_update(PHS(p, PH_SPEED), shown_speed, 0);
        if (DSW(DS_has_clock) == 0) {
            tach_init(shown_rpm);
            shown_rpm = needle_update(PHS(p, PH_RPM), shown_rpm, 1);
        } else {
            clock_draw();
        }
    } while (DSS(0x58EE) > 1);
    DSW(0x58EE) = 0;
    if (DSS(DS_driver_id) == -2) {
        ega_set_split(0x63);                                       /* 21a0:0014: dashboard below */
        DSB(0x824A) = 0x64;
    } else {
        DSB(0x824A) = 0;
    }
    road_race_init();                                              /* 2645:2114 */
    drv_pal_normal();                                              /* (*DS:78C2)() */
    for (;;) {
        road_step();                                               /* render, >= 9 ticks per frame */
        if (DSW(DS_demo_active) == 0x63) { DSW(DS_demo_active)--; platform_exit(); }
        res = (s16)race_logic();
        if (res != 0) break;
        w = PHS(p, PH_WHEEL);                                      /* sampled after race_logic */
        if (PHW(p, PH_GEAR_DIRTY) != 0) shifter_knob(ds_far(DS_page0_ptr), p);
        if (abs16((s16)(PHS(p, PH_SPEED) - shown_speed)) > 1)
            shown_speed = needle_update(PHS(p, PH_SPEED), shown_speed, 0);
        if (DSW(DS_has_clock) == 0 && abs16((s16)(PHS(p, PH_RPM) - shown_rpm)) > 1)
            shown_rpm = needle_update(PHS(p, PH_RPM), shown_rpm, 1);
        if (w != DSS(DS_wheel_shown)) {
            wheel_draw(w, DSS(DS_wheel_shown), ds_far(DS_page0_ptr));
            DSS(DS_wheel_shown) = w;
        }
    }
    arena_reset();
    /* VGA: the dashboard under the last road frame in the shown page, split off, all pages alike */
    FarPtr shown = DSW(DS_back_page) == 1 ? ds_far(DS_page2_ptr) : ds_far(DS_page1_ptr);
    FarPtr back  = DSW(DS_back_page) == 1 ? ds_far(DS_page1_ptr) : ds_far(DS_page2_ptr);
    r = (Rect){ pic_w, 0x64, 0, 0, 0, (s16)(DSW(DS_g_vga) != 0 ? 0x64 : 0x65) };
    drv_blit(ds_far(DS_page0_ptr), shown, &r, 1);
    r = (Rect){ pic_w, 1, 0, 0, 0, 0x64 };
    drv_blit(ds_far(DS_page0_ptr), shown, &r, 1);
    ega_set_split(-1);
    DSB(0x824A) = 0;
    drv_copy_page(desc_planes(shown), desc_planes(ds_far(DS_page0_ptr)));
    drv_copy_page(desc_planes(shown), desc_planes(back));
    gfx_screen_mode(1);
    (void)w;
    return res;
}
