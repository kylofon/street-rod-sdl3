/* Hook registration of the race subsystem (modules.h), and a developer aid. Owned by the race port. */
#include "game/race.h"
#include "game/race_int.h"

#include <stdlib.h>
#include <string.h>

#include "game/flow.h"
#include "game/garage.h"
#include "modules.h"
#include "platform/platform.h"
#include "platform/video.h"

void race_register_hooks(void)
{
    modules.race_phys_step = race_phys_step;      /* race_isr 0000:238c from 253b */
}

/* ------------------------------------------------------------------------------------------------ */
/* DEVELOPER AID (not part of the original, off by default).
 *
 * SR_DEBUG_RACE=drag|road|cruise|bob skips the title and the menus: the set-up of title_and_setup
 * 0000:39c0 without the title, new_game 0000:56a9, a stock car (SR_DEBUG_MODEL, default 5) as the
 * current car, then
 *   drag / road   a race against opponent SR_DEBUG_OPP (default 0; 21 = the King) for kicks,
 *                 race_results, then exit;
 *   cruise        the cruise to Bob's (drive_to, DS:0284 = 2), then exit;
 *   bob           the cruise, then Bob's Drive-In (bob_drive_in), then exit.
 * Combine with SDL_VIDEO_DRIVER=dummy SR_SNAPSHOT_DIR=... SR_KEYS=... (host.h) to run headless, e.g.
 * SR_KEYS="3:e048p" holds the gas pedal from 3 s on. */
static s16 env_int(const char *name, s16 def)
{
    const char *s = getenv(name);
    return s && *s ? (s16)atoi(s) : def;
}

bool race_debug_main(void)
{
    const char *mode = getenv("SR_DEBUG_RACE");
    if (!mode || !*mode) return false;
    /* title_and_setup 0000:39c0 without the title sequence */
    gfx_screen_mode(0);
    drv_pal_black();
    gfx_screen_mode(1);
    if (DSW(DS_libs_preloaded) == 0) lib_read_dir(2);
    cursor_ctl(-4);
    track_build_all();
    if (DSW(DS_libs_preloaded) == 0) {
        hot_data_load();
        if (DSS(DS_driver_id) == -2) pic_park_list(DS_resident_pics);
    }
    pic_load_list(DS_resident_pics, 1);
    cursor_ctl(-2);
    new_game();
    status_label(ds_str(DSW(0x49DC)));
    u16 car = car_new(env_int("SR_DEBUG_MODEL", 5), 0, 0);
    if (DSW(DS_car) == 0) DSW(DS_car) = car;
    CARW(cur_car(), CAR_GAS) = 0xAA;                               /* full tank */
    car_setup(cur_car(), PLAYER);
    if (strcmp(mode, "drag") == 0 || strcmp(mode, "road") == 0) {
        DSW(DS_race_type) = strcmp(mode, "road") == 0 ? 1 : 0;
        opponent_load(env_int("SR_DEBUG_OPP", 0));
        DSW(DS_bet_kind) = 0;
        DSW(DS_bet_amount) = 0;
        DSW(DS_bet_button) = 1;
        race_start();
        race_results();
    } else {
        DSW(DS_drive_dest) = 2;
        drive_to();
        if (strcmp(mode, "bob") == 0) bob_drive_in();
    }
    platform_exit();
}
