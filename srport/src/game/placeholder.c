/* Skeleton stand-in for the game loop (removed when game_flow is ported).
 *
 * game_main runs what the original does after main's initialisation (platform_main_init) as far as
 * the platform and video layers alone can show it: the title part of title_and_setup 0000:39c0 with a
 * copy of the title sequence 0f38:0bf0 (California Dreams logo, title, credits: 400 ticks each or a
 * key / click; the background tune), then the empty garage background of garage_screen 0000:6b06
 * (LIB2 #32) with the status line and the mouse pointer, until Esc. Everything goes through the
 * ported API (pictures, driver slots, palette, ui_wait). */
#include "game/game.h"
#include "platform/platform.h"
#include "platform/video.h"
#include "sound/sound.h"

/* Copy of title_sequence 0f38:0bf0 (game_flow.md §4.2) — VGA path. */
static void title_sequence(void)
{
    u16 save = DSW(0x04C2);                       /* picture clip height */
    drv_pal_black();
    DSW(0x04C2) = 200;
    show_picture_at(3, 0x28, 0x28, 0);            /* LIB1 #2 "California Dreams presents" */
    pic_free(3);
    drv_pal_normal();
    DSW(DS_g_mirror) = 0;
    show_picture(1, -1);                          /* LIB1 #0 the title, on the back page */
    pic_free(1);
    wait_click_or_key(400);
    drv_pal_normal();
    drv_blinds(desc_planes(g_front()), desc_planes(g_back()));  /* title visible */
    show_picture(4, 0);                           /* LIB1 #3 credits, on the back page */
    pic_free(4);
    wait_click_or_key(400);
    ega_set_palette(DS_pal_intro_tab);            /* DS:02D4 credits palette */
    drv_blinds(desc_planes(g_front()), desc_planes(g_back()));  /* credits visible */
    wait_click_or_key(400);
    DSW(0x04C2) = save;
    screen_fill_rect(0, 0, 0x140, 200, 0);
    drv_blinds(desc_planes(g_front()), desc_planes(g_back()));  /* black */
}

int game_main(void)
{
    /* title_and_setup 0000:39c0 (game_flow.md §4.2), without the game_flow parts */
    gfx_screen_mode(0);
    tune_start(2, 10);
    title_sequence();
    drv_pal_black();
    gfx_screen_mode(1);
    rnd(-1);
    drv_pal_black();
    if (DSW(DS_libs_preloaded) == 0) lib_read_dir(2);
    cursor_ctl(-4);
    if (DSW(DS_libs_preloaded) == 0) {
        hot_data_load();
        if (DSS(DS_driver_id) == -2) pic_park_list(DS_resident_pics);
    }
    pic_load_list(DS_resident_pics, 1);
    cursor_ctl(-2);

    /* the garage without a car (garage_screen 0000:6b06, garage.md §4.7) */
    if (DSC(DS_g_ptr_show) > 0) cursor_ctl(-4);
    cursor_ctl(0);
    show_picture(0x408, -1);                      /* LIB2 #32 */
    pal_reg12_5();
    drv_pal_normal();
    page_copy_rect(g_back(), g_front(), 0, 0, 0x13F, 199);
    status_label(ds_str(DSW(0x49DC)));            /* "Bankroll:" and the money */
    /* the pointer's show counter starts at 0 and title_and_setup hid it once; the game's screens
     * before the garage show it again: here, show it until visible */
    while (DSC(DS_g_ptr_show) <= 0) cursor_ctl(-2);

    /* wait for Esc: ui_wait returns 3EBh on any key with DS:05D0 set */
    DSW(DS_wait_any_key) = 1;
    for (;;) {
        s16 r = ui_wait(1000);
        if (r == 0x3EB && DSW(DS_wait_key) == 0x1B) break;
    }
    DSW(DS_wait_any_key) = 0;
    platform_exit();
}
