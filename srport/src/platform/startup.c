/* Platform: start-up — main 0000:066f up to the game loop, driver selection 0000:0316, the video mode
 * set 0000:04f4, the CPU speed class — platform.md §2.1, §4.1, §6. */
#include "platform/platform.h"

#include <string.h>

#include "host.h"
#include "platform/vga.h"
#include "platform/video.h"
#include "sound/sound.h"

/* 0000:0226 video_detect_menu: adapter detection, the recommendation and the text-mode "Video
 * Options" menu (0000:0082); returns the mode code.
 * PORT: skipped. The port is the VGA path: the state the original leaves after detecting a VGA with
 * 256 KB and the choice 3 ("VGA" = EGA driver + VGA split screen) is set directly. */
static s16 video_detect_menu(void)
{
    DSB(0x5AEE) = 0x03;                           /* BIOS mode at start (80x25 colour text) */
    DSB(0x5AF0) = rd8(0x0040, 0x0010);            /* equipment byte */
    DSB(0x5AEF) = 0;                              /* EGA info: colour */
    DSB(0x5AF1) = 3;                              /* 256 KB */
    DSB(0x5AF2) = 9;                              /* switch setting */
    DSW(DS_ega_present) = 1;
    DSW(DS_g_vga) = 1;                            /* choice 3 */
    return 2;
}

/* 0000:0316 driver_select — platform.md §4.1. Only the EGA/VGA cases exist in the port. */
void driver_select(s16 mode)
{
    switch (mode) {
    case 3:
        DSW(DS_g_vga) = 1;
        /* fall through */
    case 2:
        DSW(DS_driver_id) = 0xFFFE;
        wr8(0x0000, 0x0410, (u8)((rd8(0x0000, 0x0410) & 0xCF) | 0x20));   /* equipment: 80x25 colour */
        ega_install_vectors();                    /* 21a0:1128: DS:5BD0 -> DS:78A2 */
        return;
    default:
        /* PORT: CGA (1, 6), Hercules (4) and Tandy (5) drivers are not part of the port. */
        host_fatal("driver_select: video mode %d is not supported", mode);
    }
}

/* 0000:04f4 video_mode_set — platform.md §4.1 */
void video_mode_set(s16 mode)
{
    switch (mode) {
    case 2:
    case 3:
        vga_mode_0d();                            /* bios_set_mode 0000:01d1 (0Dh): planes cleared */
        break;
    default:
        host_fatal("video_mode_set: video mode %d is not supported", mode);   /* PORT: see driver_select */
    }
    DSW(DS_gfx_mode_set) = 1;
}

/* 0000:0659 slow_flag_set */
void slow_flag_set(void)
{
    DSW(DS_slow_machine) = DSS(DS_cpu_speed) < 4 ? 1 : 0;
}

/* 1e16:2130 stricmp(a, b) == 0 (ASCII case folding, as the MS C runtime) */
static bool stricmp_eq(const char *a, const char *b)
{
    for (;; a++, b++) {
        u8 ca = (u8)*a, cb = (u8)*b;
        if (ca >= 'A' && ca <= 'Z') ca = (u8)(ca + 0x20);
        if (cb >= 'A' && cb <= 'Z') cb = (u8)(cb + 0x20);
        if (ca != cb) return false;
        if (ca == 0) return true;
    }
}

/* rnd 0f38:5eb6 for the sound effects (sound.h takes an int (*)(int)). */
static int rnd_for_sound(int n) { return rnd((s16)n); }

/* 0000:066f main, initialisation part — platform.md §4.1 */
void platform_main_init(int argc, char **argv)
{
    dos_init();                                   /* PORT: the DOS state at program start */
    video_register_codeptrs();                    /* PORT: far code pointers stored in game data */
    snd_set_rnd(rnd_for_sound);                   /* PORT: the effects call rnd directly in the original */

    DSB(DS_g_ui_level) = 0;
    /* PORT: 0000:05c6 cpu_speed_calibrate (loop count during one BIOS tick) -> a fixed fast class. */
    DSW(DS_cpu_speed) = SR_CPU_SPEED;
    s16 disks = 2;
    DSW(DS_mouse_allowed) = 1;
    DSW(DS_demo_switch) = 0;
    DSW(DS_auto_drive) = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (memcmp(a, ds_str(0x0264), 2) == 0) disks = 1;           /* "1", exact */
        else if (stricmp_eq(a, ds_str(0x0266))) DSW(DS_mouse_allowed) = 0;     /* nomouse */
        else if (stricmp_eq(a, ds_str(0x026E))) DSW(DS_demo_switch) = 1;       /* demo */
        else if (stricmp_eq(a, ds_str(0x0273))) DSW(DS_auto_drive) = 1;        /* auto */
        else if (stricmp_eq(a, ds_str(0x0278))) DSW(DS_no_ega_park) = 1;       /* nouemem */
    }
    s16 mode = video_detect_menu();
    mem_pools_init();
    arena_init();
    driver_select(mode);
    video_mode_set(mode);
    gfx_init(far_make(SEG(0x2E3E), 0x0000), far_make(SEG(0x2E3E), 0x0030));
    platform_install();
    if (DSS(DS_driver_id) == -2 || DSS(DS_driver_id) == -6) ega_set_palette(DS_pal_shadow);   /* DS:0440 */
    lib_open_all(disks, mode);
    slow_flag_set();
    lib_read_dir(1);
    cursor_init();
    snd_init();
    gfx_screen_mode(0);
    /* (CGA on an EGA card: a 320x100 back bitmap in DS:755C; not on the VGA path) */
    ds_far_wr(DS_pack_buf, far_alloc((DSS(DS_driver_id) == -2 || DSS(DS_driver_id) == -6) ? 0x4650 : 0x3E80, 1));
    if (pic_load_list(DS_big_pics, 0) == 3) {     /* enough memory for the big LIB1 pictures {3, 1, 4} */
        DSW(DS_libs_preloaded) = 1;
        lib_read_dir(2);
        hot_data_load();
        if (DSS(DS_driver_id) == -2) pic_park_list(DS_resident_pics);
        pic_load_list(DS_resident_pics, 1);
    } else {
        DSW(DS_libs_preloaded) = 0;
        pic_free_list(DS_big_pics);
    }
    /* main continues with game_loop 0000:503f and platform_exit (game_main, game/game.h) */
}
