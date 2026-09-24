#pragma once
/* Host services on top of SDL3: window/present, the programmable timer tick, VGA vertical retrace,
 * keyboard (XT byte stream for the game's own INT 9 handler), mouse, gamepad, PC speaker, game file
 * lookup and fatal errors. No game logic lives here. */
#include "types.h"

#define PIT_HZ        1193182u
#define PIT_DIV_GAME  0x4000u        /* 16384 -> 72.82 Hz, timer install 0000:1111 (RE_GUIDE.md) */

bool host_init(const char *game_dir, int window_scale, bool fullscreen);
void host_shutdown(void);

/* Called once per timer tick from host_pump() (the port's replacement for INT 8). The platform timer
 * module installs the ISR body (0000:1fe2, or 0000:238c while a sound plays). */
void host_set_tick_handler(void (*handler)(void));

/* PIT channel 0 divisor (0 = 65536), as the original programs ports 43h/40h. Ticks run at
 * PIT_HZ / divisor from the last tick on; the default is PIT_DIV_GAME. */
void host_set_pit_divisor(u16 divisor);

/* Source of the displayed image: fills a w x h XRGB8888 frame and returns true if it changed since the
 * last call. Installed by the EGA/VGA model (platform/ega.c, 320x200 16 colours). Shown with 4:3
 * aspect. */
#define HOST_FRAME_MAX_W 320
#define HOST_FRAME_MAX_H 200
void host_set_frame_source(bool (*compose)(u32 *xrgb), int w, int h);

/* Runs due timer ticks (and their audio), handles window events and presents the screen when it
 * changed. Every busy-wait loop of the original (tick waits, key polls, delays) must call this.
 * Sleeps briefly when nothing was due, so tight polling loops do not spin the CPU. */
void host_pump(void);

/* Waits for the start of the next vertical retrace of the emulated VGA (70.086 Hz), pumping
 * meanwhile. Replaces the port 3DAh polls. */
void host_wait_vretrace(void);

/* ---- Keyboard: the game's INT 9 handler (0000:2c42). The handler receives the XT byte sequence the
 * keyboard would send, in event order: normal keys sc / sc|80h, grey keys E0 sc / E0 sc|80h, Pause
 * E1 1D 45 E1 9D C5, Print Screen E0 2A E0 37 / E0 B7 E0 AA. Key repeats feed the make code again. */
void host_set_kbd_handler(void (*handler)(u8 byte));
/* Called when the window loses keyboard focus (keys released outside it never send their break). */
void host_set_focus_lost_handler(void (*handler)(void));

/* ---- Mouse (INT 33h replacement): position in 320x200 screen pixels, buttons bit0 left, bit1 right.
 * Motion in mickeys since the last call (for the driver's relative reads). */
void host_mouse_read(s16 *x, s16 *y, u8 *buttons);
void host_mouse_motion(s16 *dx, s16 *dy);

/* ---- Joystick (port 201h replacement): first connected gamepad. Axes -32768..32767, buttons bit0 = A,
 * bit1 = B. */
bool host_joy_read(s16 *x, s16 *y, u8 *buttons);

/* ---- PC speaker: PIT channel 2 divisor (0 = 65536) and the port 61h gate. Takes effect from the
 * current tick on. */
void host_speaker(u16 divisor, bool on);

/* ---- Game files: case-insensitive lookup inside the game directory. Returns a malloc'd path (free with
 * host_free) or NULL if the file does not exist. For new files (saved games) pass create = true. */
char *host_game_path(const char *name, bool create);
void  host_free(void *p);

/* ---- Errors: shows a message box, shuts down and exits with code 3. */
_Noreturn void host_fatal(const char *fmt, ...);

/* ---- Developer aids (environment variables):
 *   SR_SNAPSHOT_DIR=dir   every presented frame >= 2 s after the previous one is saved as snapNNNN.bmp
 *                         (works with SDL_VIDEO_DRIVER=dummy)
 *   SR_KEYS="<seconds>:<xt>[+<xt>...],..."  presses (in order) and releases (in reverse) the XT keys
 *                         at that many seconds after start-up; a "p" after the codes only presses
 *                         (held), an "r" only releases: "9:48p,20:48r" holds Up for 11 s. Grey keys
 *                         are written with their E0 prefix as e048. */
