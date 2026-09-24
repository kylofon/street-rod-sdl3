/* game_flow: the rest of main 0000:066f, the title and the game loop 0000:503f with the garage dispatch
 * — port/spec/game_flow.md §4.1-§4.4. */
#include "game/game.h"
#include "game/flow.h"

#include "game/garage.h"
#include "game/ui.h"
#include "game/race.h"
#include "platform/platform.h"
#include "platform/video.h"
#include "sound/sound.h"

/* 0f38:0bf0 title_sequence (VGA path) */
void title_sequence(void)
{
    u16 save = DSW(0x04C2);                           /* picture clip height */
    if (DSS(DS_driver_id) == -2 || DSS(DS_driver_id) == -6) drv_pal_black();
    DSW(0x04C2) = 200;
    /* (other drivers: 0000:01f2(0)) */
    show_picture_at(3, 0x28, 0x28, 0);                /* LIB1 #2 "California Dreams presents" */
    pic_free(3);
    drv_pal_normal();
    DSW(DS_g_mirror) = 0;
    show_picture(1, -1);                              /* LIB1 #0 the title, on the back page */
    pic_free(1);
    wait_click_or_key(400);
    drv_pal_normal();
    drv_blinds(desc_planes(g_front()), desc_planes(g_back()));   /* title visible */
    show_picture(4, 0);                               /* LIB1 #3 credits, on the back page */
    pic_free(4);
    wait_click_or_key(400);
    if (DSS(DS_driver_id) == -2 || DSS(DS_driver_id) == -6) ega_set_palette(DS_pal_intro_tab);   /* DS:02D4 */
    drv_blinds(desc_planes(g_front()), desc_planes(g_back()));   /* credits visible */
    wait_click_or_key(400);
    DSW(0x04C2) = save;
    screen_fill_rect(0, 0, 0x140, 200, 0);
    drv_blinds(desc_planes(g_front()), desc_planes(g_back()));   /* black */
}

/* 0000:39c0 title_and_setup: returns -1 (the new-game screen follows) */
s16 title_and_setup(void)
{
    gfx_screen_mode(0);
    tune_start(2, 10);
    title_sequence();
    drv_pal_black();
    gfx_screen_mode(1);
    rnd(-1);                                          /* seed from the time of day */
    drv_pal_black();
    if (DSW(DS_libs_preloaded) == 0) lib_read_dir(2);
    if (DSW(DS_demo_switch) == 0) {
        /* PORT: copy protection 0000:2fc8 skipped (PLAN.md decision 3). SR.EXE itself jumps over the
         * call at 0000:3a1a; the port behaves as if the question had been answered. */
        cursor_ctl(-4);
    }
    track_build_all();                                /* 2645:0d48 */
    DSB(DS_player_name) = 0;
    DSW(DS_demo_active) = DSW(DS_demo_switch);
    if (DSW(DS_libs_preloaded) == 0) {
        hot_data_load();
        if (DSS(DS_driver_id) == -2) pic_park_list(DS_resident_pics);
    }
    pic_load_list(DS_resident_pics, 1);
    cursor_ctl(-2);
    return -1;
}

/* 0000:503f game_loop */
void game_loop(void)
{
    s16 again = 1;                                    /* [bp-2] */
    s16 code = title_and_setup();                     /* [bp-6] */
    s16 prev;                                         /* [bp-8] */
    s16 redraw;                                       /* [bp-4] */
    s16 r;

new_or_load:                                          /* 0000:5050 */
    do {
        if (code == -1) again = 1;
        prev = code;
        code = code == -1 ? new_game_prompt() : load_game_screen();
        if (code == -0x29A) { code = -1; again = 1; }
    } while (code != 0 && again != 0);
    status_label(ds_str(DSW(0x49DC)));                /* "Bankroll:" and the money */
    redraw = 1;                                       /* (a "prev == -2 && car -> 2" test is overwritten) */
    if (prev == -1 && DSS(DS_car_count) <= 0x0F && DSS(DS_part_count) <= 0x82) {
        code = newspaper_front();                     /* a new game starts at the paper */
        if (code == 2 || code == 1) classifieds(code);
    }
    again = 0;

    for (;;) {
        code = garage_screen(redraw);                 /* 0000:50f9 */
        if (code == 0) goto new_or_load;
        music_mute(0);
        if (code == FLOW_GAME_OVER) {
            if (DSW(DS_game_end_flags) == FLOW_END_SUMMER) summer_over_screen();
            hall_of_fame_screen((s16)(DSW(DS_game_end_flags) & FLOW_END_KING));
            cursor_ctl(1);
            DSW(DS_game_end_flags) = 0;
            again = 1;
            code = game_over_menu();
            if (code == 4) return;                   /* Quit */
            cursor_ctl(-2);
            DSW(0x4F4A) = 0;
            goto new_or_load;                         /* -1 new game, other codes the load screen */
        }
        redraw = 2;
        switch (code) {
        case 1:                                       /* N "Check out the newspaper" */
            if (garage_full_check()) { redraw = 0; continue; }
            if (DSW(DS_car) == 0) redraw = 1;
            code = newspaper_front();
            if (code == 2 || code == 1) classifieds(code);
            if (DSW(DS_car) == 0) redraw = 2;
            if (DSW(DS_car) == 0 && DSW(DS_car2) != 0) redraw = 1;
            clock_hour();
            continue;
        case 2:                                       /* D "Hit the street": Bob's Drive-In */
        case 15:                                      /* G "Wanna tank ?": the gas station */
            if (garage_full_check()) { redraw = 0; continue; }
            if (DSW(DS_car) == 0) {
                msg_box(DSW(DS_car2) ? 0x0F16 : 0x0EFA);   /* "Pick a car first!" / "You've got no car, dummy!" */
                redraw = 0; continue;
            }
            if (car_runnable(DSW(DS_car), 1) == 0) { msg_box(0x0E7F); redraw = 0; continue; }   /* "Your car's not running, speedy!" */
            car_setup(DSW(DS_car), 0x78E0);
            if (code == 2) {
                if (DSS((u16)(DSW(DS_car) + 0x22)) < 9) { msg_box(0x172E); redraw = 0; continue; }   /* not enough gas */
                DSW(DS_drive_dest) = 2;
                drive_to();
                clock_hour();
                bob_drive_in();
            } else {
                if (DSS((u16)(DSW(DS_car) + 0x22)) >= 0x8C) { msg_box(0x19B1); redraw = 0; continue; }  /* "You've got a full tank!" */
                if (DSL(DS_money) == 0) { msg_box(0x199F); redraw = 0; continue; }                     /* "Gas ain't free!" */
                DSW(DS_drive_dest) = 3;
                drive_to();
                clock_hour();
                gas_station();
            }
            clock_hour();
            redraw = 1;
            continue;
        case 3:                                       /* X "Calendar" */
            calendar_show(0);
            redraw = 0;
            continue;
        case 4:                                       /* Q "Time to quit" */
            redraw = 0;
            r = quit_menu();
            code = r;
            if (r == -20) return;                     /* Quit */
            if (r == -22 || r == -4) continue;        /* Forget it */
            if (r == -3) {                            /* Save Game */
                if (garage_full_check()) { redraw = 0; code = 0x309; continue; }
                save_game_screen();
                redraw = 2;
                if (DSW(DS_car) == 0 && DSW(DS_car2) != 0) redraw = 1;
                continue;
            }
            goto new_or_load;                         /* -1 New Game, -2 Old Game (again = 0) */
        case 5:  sell_spare_parts();     redraw = 0; continue;    /* L "Sell parts" */
        case 6:  redraw = your_cars();   continue;                /* C "Cars" */
        case 7:  paint_job();            redraw = 0; continue;    /* P "New paint job - $20" */
        case 8:  change_tires();         redraw = 0; continue;    /* T "Change tires" */
        case 9:  change_transmission();  continue;                /* A "Change transmission" (redraw 2) */
        case 10: arena_reset_stacks(); engine_bay(DSW(DS_car)); continue;   /* H "Pop the hood" */
        case 11: customize(2);           redraw = 0; continue;    /* B "Rear bumper" */
        case 12: customize(1);           redraw = 0; continue;    /* F "Front bumper" */
        case 13: customize(4);           redraw = 0; continue;    /* R "The roof" */
        case 14: stickers();             redraw = 0; continue;    /* S "Stickers - $5" */
        case 16: car_info();             redraw = 0; continue;    /* I "Car info" */
        case 17: msg_box(0x18F6);        redraw = 0; continue;    /* "Wanna drink ?": "Don't drink and drive, bub!" */
        case -40:                                     /* the juke box: "Catch some tunes" / "Squelch it !" */
            if (DSW(DS_music_enabled)) { music_stop(); DSW(DS_music_enabled) = 0; }
            else { DSW(DS_music_enabled) = 1; music_resume(); }
            redraw = 0;
            continue;
        default:
            goto new_or_load;                         /* again = 0: one new-game (-1) or load call */
        }
    }
}

/* The rest of main 0000:066f after platform_main_init: game_loop, then exit_to_dos. */
int game_main(void)
{
    race_debug_main();                                /* developer aid (SR_DEBUG_RACE), off by default */
    game_loop();
    platform_exit();
}
