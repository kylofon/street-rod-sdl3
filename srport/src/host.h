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
/* True during the first VRETRACE_WINDOW of each emulated 70.086 Hz frame (the vertical blanking of mode
 * 0Dh on a VGA, ~1.5 ms): port 3DAh bit 3 for the "wait until in retrace" loops. */
bool host_in_vretrace(void);

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
/* Mickeys since the last call as the INT 33h driver counts them at its default ratio (8 mickeys per 8
 * pixels, 2 mickeys per game pixel at 320x200): relative host motion scaled by the size of the game
 * picture in the window. */
void host_mouse_mickeys(s16 *mx, s16 *my);
/* Relative mouse mode (the pointer is hidden and captured: the game draws its own pointer and reads
 * only motion). */
void host_mouse_set_relative(bool on);

/* ---- Joystick (port 201h replacement): first connected gamepad. Axes -32768..32767, buttons bit0 = A,
 * bit1 = B. */
bool host_joy_read(s16 *x, s16 *y, u8 *buttons);

/* ---- PC speaker (PIT channel 2 + port 61h), modelled at PIT-clock resolution.
 *
 * Emulated timeline: timer tick n starts at a fixed PIT-clock time; writes made by the tick handler
 * are stamped at that time, writes from ordinary game code at (time of the last tick + busy-wait time
 * spent since, see host_busy_wait_ns). The model:
 *   channel 2: mode 3 square wave (43h = B6h is implied), the new count takes effect at the next
 *              output transition, the wave restarts on a rising gate;
 *   port 61h:  bit 0 = timer-2 gate, bit 1 = speaker data; other bits are stored only;
 *   speaker level = bit1 && (bit0 ? OUT2 : 1), integrated exactly per output sample, then a ~20 Hz
 *              DC blocker and a ~10 kHz one-pole low-pass. Output lags the timeline by one tick.
 * So bit-banged sound (bit 1 toggled with the gate off and busy waits in between) is reproduced,
 * and ticks that fall inside a busy wait change the speaker at their place, as the original's INT 8
 * did in the middle of its effects. */
void host_pit2_divisor(u16 divisor);        /* out 42h lo, hi (0 = 65536) */
void host_port61(u8 value);                 /* out 61h */
u8   host_port61_get(void);                 /* in 61h: the last value written */
/* The original's busy loops (0f38 delay 2196:0002 and the code around it): advances the emulated
 * timeline, running the tick handler for every tick boundary crossed (after pacing to that tick's
 * real time and handling window events), and returns when real time has caught up with the
 * timeline. Inside the tick handler it only advances the timeline. */
void host_busy_wait_ns(uint64_t ns);
void host_busy_wait_us(u32 us);
/* Old interface: host_pit2_divisor(divisor); port 61h bits 0-1 = on ? 3 : 0. */
void host_speaker(u16 divisor, bool on);

/* Developer aids for offline rendering (tools/snd_dump.c): with realtime off, busy waits never wait
 * for real time; host_run_tick_now() runs the next timer tick at once (handler + audio). A WAV dump
 * receives every rendered sample (44.1 kHz mono s16), with or without an audio device; the
 * environment variable SR_AUDIO_DUMP=file.wav starts one in host_init. */
void host_set_realtime(bool on);
void host_run_tick_now(void);
uint64_t host_audio_clock(void);            /* current emulated time, PIT clocks */
bool host_audio_dump_begin(const char *wav_path);
void host_audio_dump_end(void);

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
