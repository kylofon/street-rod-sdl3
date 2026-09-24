#pragma once
/* Platform layer of SR.EXE: public API for the game code (game_flow, garage, race) and sound.
 *
 * Implements port/spec/platform.md:
 *   §2.1 / §4.1  startup: main 0000:066f initialisation part (platform_main_init), driver selection
 *                0000:0316 for VGA, video mode set, CPU calibration (constant), slow flag
 *   §2.2 / §4.2 / §4.3 / §4.4 / §4.5 / §4.6 / §4.8  timer ISRs 0000:1fe2 / 0000:238c (platform part),
 *                timer install/restore/exit, keyboard INT 9 0000:2c42 with its tables, per-screen
 *                remaps, text mode, the tick-timer queue, the UI wait loop 0000:1417, joystick
 *   §2.3 / §4.7  mouse (INT 33h replacement), wait_click_or_key 0f38:0b51
 *   §2.4 / §4.9  memory: DOS arena + MS C far heap in mem[], far_alloc 0000:3709, bitmap descriptors,
 *                off-screen EGA pools 0000:3967, the 65000-byte work arena 0f38:9ee9-a0b8
 *   §2.5 / §4.10 files and picture libraries 0f38:6016-6f0a, HOT_DATA, hall_dat
 *   §2.6 / §4.11 rnd 0f38:5eb6
 *   §5           file formats and tables (read from the loaded image at run time)
 *   §6 / §7      SDL3 replacements and timing (see srport/PORTING.md)
 *
 * Conventions (srport/PORTING.md): all game state lives in mem[] at its original address; functions are
 * named after port/symbols.csv and carry the original address in a comment; near pointers to DGROUP
 * data are u16 DS offsets, far pointers FarPtr. Integer widths as the original: int = s16/u16.
 * Near pointers the original takes to *stack* buffers (rects, token copies, out-parameters) are C
 * pointers in the port.
 *
 * Sound is called directly (sound/sound.h); hooks into subsystems ported separately (race step, demo
 * step, the 0f38 UI: tick-timer callback, hotspot test, message box) are function pointers in
 * modules.h, wired up in modules.c. */
#include "mem.h"
#include "symbols.h"

/* DGROUP string / buffer as a C pointer (mem[] is byte addressed, so this is always valid). */
static inline char *ds_str(u16 off) { return (char *)mp(DGROUP, off); }

/* ============================================================================================ startup */

/* PORT: 0000:05c6 cpu_speed_calibrate counts loop iterations during one BIOS tick; the port stores a
 * fixed "fast machine" value (race.md §7 proposes 20; any value >= 4 selects the same game logic, 1..3
 * the slow-machine paths). */
#define SR_CPU_SPEED 20

/* 0000:066f main, initialisation part (platform.md §4.1): DS:8ACC, the command-line switches ("1",
 * nomouse, demo, auto, nouemem; argv[0] is the program name), video set-up (PORT: no detection and no
 * menu: always the VGA path as if choice 3 had been taken), heap, arena, driver vectors, gfx_init,
 * platform_install, palette DS:0440, lib_open_all, slow flag, LIB1 directory, cursor_init, snd_init
 * (hook), gfx_screen_mode(0), the packed-picture buffer DS:7564 and the resident pictures.
 * Returns when main would call game_loop 0000:503f; main.c then calls game_main(). */
void platform_main_init(int argc, char **argv);

/* PORT: the DOS state the original finds at start-up: the memory arena (MCB chain in mem[]: program
 * block from the PSP at LOAD_SEG-10h to DGROUP+1000h as _astart leaves it, the rest free up to A000h),
 * the C runtime heap variables and the file handle table. Called first by platform_main_init. */
void dos_init(void);

void driver_select(s16 mode);                 /* 0000:0316: mode 2/3 only (VGA path): DS:8236 = -2, vectors */
void video_mode_set(s16 mode);                /* 0000:04f4: mode 0Dh (vga_mode_0d), DS:0048 = 1 */
void slow_flag_set(void);                     /* 0000:0659: DS:82C6 = (s16)DS:8ACC < 4 */

/* ============================================================================ timer, exit, the ISRs */

/* 0000:1111 platform_install: joystick detection + calibration (a gamepad connected now), mouse reset,
 * PIT 4000h (72.8 Hz), INT 8 = timer_isr, INT 9 = kbd_isr (host keyboard), input_reset. */
void platform_install(void);
void timer_restore(void);                     /* 0000:0fb7: tick handler off, DS:05E0 = DS:0044 = 0 */
_Noreturn void platform_exit(void);           /* 0000:0fea: "Quit demo" box, restore, speaker off, exit(5) */

void timer_isr(void);                         /* 0000:1fe2: normal INT 8 body, run once per host tick */
void race_isr(void);                          /* 0000:238c: platform part; sound and race step via hooks */
void race_isr_enter(void);                    /* 0000:1aad: tick handler = race_isr, mouse y shift 6 */
void race_isr_leave(void);                    /* 0000:1b27: tick handler = timer_isr, mouse shifts 1/1 */
s16  sign(s16 v);                             /* 0000:1b91: -1 / 0 / 1 */

/* Tick counter DS:05F8 (u32, 72.8 Hz) and the 18.2 Hz counter DS:0600. */
static inline u32 ticks_now(void) { return DSL(DS_ticks); }
/* The original's signed 32-bit compare "deadline > now" (high word signed, low word unsigned). */
static inline bool tick_after(u32 a, u32 b) { return (s32)a > (s32)b; }

/* ======================================================================================= keyboard */

void kbd_isr(u8 sc);                          /* 0000:2c42: one scan code byte (port 60h), XT set 1 */
s16  key_read(void);                          /* 0000:2db6: read-and-clear the key slot DS:47D0 (AL, 0-extended) */
void kbd_install(void);                       /* 0000:2dc3: host keyboard -> kbd_isr */
void kbd_restore(void);                       /* 0000:2de2 */
void kbd_remap_screen(s16 screen);            /* 0000:12de: per-screen hot keys DS:579F[screen] + 'M' (never 0) */
s16  kbd_remap_restore(s16 keep_low);         /* 0000:1391: undo 12de; returns the previous screen byte */
void kbd_text_mode_on(s16 keep_space);        /* 0000:2c04: Backspace, ',', '.', Space become keys */
void kbd_text_mode_off(void);                 /* 0000:2c2b */

/* ===================================================================== UI wait loop and tick timers */

void input_reset(void);                       /* 0000:0e24: timer queue reset, 10 key-free ticks, state cleared */
void click_clear(void);                       /* 0000:0d78 */
/* 0000:1417 ui_wait: busy loop over key slot, tick timers, mouse, clicks, hotspot hover, pointer
 * redraw and the palette rotation; returns an event code (hot key 1..10 / negative screen codes, a
 * hotspot id, 3EBh "any key / click", 1000 = DS:05CC deadline passed) or 0 after `ticks` ticks.
 * PORT: the palette rotation runs once per qualifying tick instead of on every loop iteration of it
 * (platform.md §7). */
s16  ui_wait(s16 max_ticks);
void wait_ticks_or_input(s16 n);          /* 0000:3a83: ui_wait(30000) with DS:05CC = now + ticks, DS:05D0 = 1 */
void wait_click_or_key(s16 n);                  /* 0f38:0b51: ticks, click, button, Enter or Esc; 'M' = music */

s16  tq_insert(s16 delay, s16 prio);          /* 0000:1843: -1 if the queue (DS:0572, 5 entries) is full */
void tq_remove(s16 e);                        /* 0000:0f0b */
void tq_schedule_call(s16 arg, s16 delay);    /* 0000:198a: timer_callback(arg) (0f38:1e41, hook) when due */
void tq_cancel_call(s16 arg);                 /* 0000:19bc: only looks at the head (sic) */

/* ================================================================================ mouse, joystick */

s16  mouse_reset(void);                       /* 0f38:799a: -1 = mouse present (0 with nomouse / no host mouse) */
void mouse_buttons(void);                     /* 0f38:79d7: DS:05EE = left, DS:05F0 = right (value 2) */
void mouse_motion(s16 *dx, s16 *dy);          /* 0f38:7a14: mickeys since the last call */
void mouse_poll(void);                        /* 0f38:7a47: DS:05E2/05E4 scaled delta, left button -> click */
void joy_read(void);                          /* 0000:2df4: DS:4718/471A, DS:471C, click (gamepad) */

/* ========================================================================================= memory */

/* 0000:3709 far_alloc: MS C _fmalloc (far heap segments in DOS memory inside mem[], placed as the
 * original's: the first one right above the 64 KB DGROUP block); blocks inside DGROUP are rejected.
 * NULL on failure, or out_of_memory() if fatal. */
FarPtr far_alloc(u16 size, s16 fatal);
void   far_free(FarPtr p);                    /* 0000:36d6: _ffree */
_Noreturn void out_of_memory(void);           /* 0000:36e9: "Not enough memory to run Street Rod !" */
u16    get_ds(void);                          /* 0000:36a8: DGROUP segment */
void   mem_pools_init(void);                  /* 0000:36be: DS:64A6 = 800h, DS:6C60 = DS, pools_reset(1) */
void   pools_reset(s16 full);                 /* 0000:3614: game object free lists DS:83B0 / DS:866A */

/* MS C 5.1 runtime far heap (1e16:1593 / 1e16:157e) and DOS memory (INT 21h 48h/49h/4Ah) in mem[]. */
FarPtr crt_fmalloc(u16 size);
void   crt_ffree(FarPtr p);

/* Bitmap descriptors (48 bytes, platform.md §5.5, video.md §4.2; accessors in video.h). */
FarPtr bitmap_alloc(s16 w, s16 h, s16 flags, s16 drv, FarPtr d);        /* 0000:376b (d NULL: far_alloc) */
FarPtr bitmap_make(FarPtr data, FarPtr mask, s16 w, s16 h, s16 drv);    /* 0000:3880 */
void   bitmap_init(FarPtr d, FarPtr data, FarPtr mask, s16 w, s16 h, s16 drv); /* 0000:3904 */
FarPtr vram_pool_alloc(u16 size, u8 *plane);  /* 0000:3967: A000:7D00+ in one plane; NULL, *plane = FFh if full */

/* The 65000-byte work arena DS:6CD0 with its four allocation modes (platform.md §4.9):
 * 0 permanent bump, 1 low stack, 2 top stack (released by arena_pop), 3 top permanent. */
void   arena_init(void);                      /* 0f38:9ee9 */
void   arena_clear(void);                     /* 0f38:9ebc */
FarPtr arena_alloc(s16 n, s16 mode);          /* 0f38:9f2c */
void   arena_low_shrink(s16 n);               /* 0f38:9f1a: top of the low stack -= n */
void   arena_reset(void);                     /* 0f38:a054 */
void   arena_reset_stacks(void);              /* 0f38:a07e */
void   arena_pop_low(s16 n);                  /* 0f38:a09e: n = 0 empties the low stack */
void   arena_pop(s16 n);                      /* 0f38:a0b8: releases n top (mode 2) allocations; 0 = all */

/* ========================================================================== DOS files (INT 21h) */

/* The original's file calls (MS C _dos_* and intdos). Names are C strings as the game builds them
 * ("C:lib1", ":HOTROD.SAV"; DGROUP strings via ds_str()): a drive prefix "X:" and any directory are
 * dropped and the file is looked up case-insensitively in the game directory (host_game_path). Buffers are far
 * pointers into mem[]. Handles are small integers (5, 6, ... as DOS hands them out); 0 = none.
 * Return 0 on success, else a DOS error code (2 file not found, 5 access denied, 6 bad handle). */
u16  dos_open(const char *name, u16 mode, u16 *handle);            /* 1e16:2330 _dos_open (mode 0 read, 1 write, 2 r/w) */
u16  dos_creat(const char *name, u16 attr, u16 *handle);           /* 1e16:2305 _dos_creat (create / truncate) */
u16  dos_read(u16 handle, FarPtr buf, u16 n, u16 *got);    /* 1e16:2348 _dos_read */
u16  dos_write(u16 handle, FarPtr buf, u16 n, u16 *put);   /* 1e16:234f _dos_write */
u16  dos_close(u16 handle);                                /* 1e16:22f0 _dos_close */
u16  dos_remove(const char *name);                                /* 1e16:22de remove: 0 or -1 */
void dos_getdrive(u16 *drive);                             /* 1e16:2382 _dos_getdrive: PORT: always 3 (C:) */
s16  lib_seek(u16 handle, s32 pos);                        /* 0f38:6016: intdos AX=4200h; 0 or the error */

/* ================================================================================ picture libraries */

/* Picture directory: segment 389b (DS:691C), 48 bytes per picture (platform.md §5.3). Ids: < 1000 =
 * LIB1 #(id-1), >= 1000 = LIB2 #(id-1000). */
#define PICDIR_SEG_FILE 0x389B
s16  pic_index(s16 id);                       /* 0f38:6057 */
void lib_select(s16 id);                      /* 0f38:6089: one library open at a time */
s16  lib_read_dir(s16 lib);                   /* 0f38:61ab: lib 1 / 2; returns the largest packed size */
s16  pic_load(s16 id, s16 to_vram, FarPtr dst);  /* 0f38:62ba: 0 no memory, 1 loaded, 2 RAM, 3 parked */
void pic_free(s16 id);                        /* 0f38:655e */
FarPtr pic_decode(s16 id, FarPtr dst);        /* 0f38:65ae: unpack into the bitmap dst (DS:4F0E: re-init it) */
void pic_info(s16 id, s16 *w, s16 *h, s16 *masked);   /* 0f38:67ff */
FarPtr pic_get(s16 id, s16 arena_mode);       /* 0f38:683e: arena bitmap + pic_decode */
s16  pic_park_list(u16 ids);                  /* 0f38:6887: 0-terminated u16 list at DS:ids; skipped with nouemem */
s16  pic_load_list(u16 ids, s16 keep_going);  /* 0f38:68ca: returns the number loaded */
void pic_free_list(u16 ids);                  /* 0f38:6915 */
void lib_open_all(s16 disks, s16 mode);       /* 0f38:6943 */

/* ===================================================================================== HOT_DATA, hall */

void hot_data_load(void);                     /* 0f38:6b79: 13 blocks into DGROUP (DS:4EA4/4EBE), then hall_load */
s16  data_disk_check(void);                   /* 0f38:6c3d: the data drive letter ('C' in the port) */
void hall_scramble(void);                     /* 0f38:6ccf: DS:82CA[230] |= 80h */
void hall_unscramble(void);                   /* 0f38:6cec: DS:82CA[230] &= 7Fh */
void hall_load(void);                         /* 0f38:6d09: ?:hall_dat -> DS:7646, DS:82CA (or count 0) */
void hall_save(void);                         /* 0f38:6dd2 */

/* ========================================================================================= utility */

s16  rnd(s16 n);                              /* 0f38:5eb6: Wichmann-Hill; n <= 0 seeds from time(), returns -1 */
