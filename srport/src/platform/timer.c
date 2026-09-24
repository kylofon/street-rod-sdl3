/* Platform: timer install / restore / exit, the two INT 8 handlers and the tick-timer queue —
 * platform.md §2.2, §4.2, §4.3, §4.5 (queue), §4.8, §7.
 *
 * The host calls the current tick handler (host_set_tick_handler, the port's INT 8 vector) once per
 * PIT tick at the rate platform_install programs (divisor 4000h = 72.8 Hz), from host_pump(). The
 * handlers run synchronously between two game-code instructions that pump the host, which is where a
 * real interrupt would have been taken as far as any of the game's state is concerned. */
#include "platform/platform.h"

#include <stdlib.h>

#include "host.h"
#include "modules.h"
#include "platform/video.h"
#include "sound/sound.h"

/* ------------------------------------------------------------------------------------------------ */
/* The BIOS INT 8 handler the game chains to every 4th tick (DS:7AAA).                              */

/* PORT: the BIOS handler is modelled as its tick count 0040:006C (u32, in mem[]); the floppy motor
 * count and INT 1Ch are of no use to the port. The game itself never reads 0040:006C after the
 * start-up calibration, which the port replaces by a constant. */
static void bios_int8(void)
{
    wr32(0x0040, 0x006C, rd32(0x0040, 0x006C) + 1);
}

/* ------------------------------------------------------------------------------------------------ */
/* The ISRs                                                                                         */

/* 0000:1fe2 timer_isr — platform.md §4.3 (sound part: sound.md §4.3) */
void timer_isr(void)
{
    DSL(DS_ticks)++;
    u8 old = DSB(DS_chain_count);
    DSB(DS_chain_count) = (u8)(old - 1);
    if (old != 0) {
        /* out 20h,20h (EOI); sti */
    } else {
        bios_int8();                              /* pushf; lcall [DS:7AAA] (the BIOS sends the EOI) */
        DSB(DS_chain_count) = 3;
        DSL(DS_bios_ticks)++;
    }

    snd_timer_gate_check();                       /* 2028: speaker gate off when sound is switched off */

    if (DSW(DS_demo_active) != 0) {               /* 2046: demo */
        u32 mask = DSB(DS_demo_fast) ? 0x0F : 0x7F;
        if ((DSL(DS_ticks) & mask) == 0) modules.demo_step();          /* 0000:1bae */
        if ((DSW(DS_demo_cursor_x) || DSW(DS_demo_cursor_y)) &&
            (DSW(DS_cursor_x) != DSW(DS_demo_cursor_x) || DSW(DS_cursor_y) != DSW(DS_demo_cursor_y))) {
            DSW(DS_cursor_x) = (u16)(DSW(DS_cursor_x) + sign((s16)(DSW(DS_demo_cursor_x) - DSW(DS_cursor_x))));
            DSW(DS_cursor_y) = (u16)(DSW(DS_cursor_y) + sign((s16)(DSW(DS_demo_cursor_y) - DSW(DS_cursor_y))));
            DSB(DS_cursor_moved) = 1;
        }
    }

    snd_music_tick();                             /* 20b8-2294: music sequencer */

    if (DSC(DS_g_ptr_show) > 0) {               /* 2295: pointer visible */
        /* cli */
        if (DSB(DS_isr_cursor_busy) != 0) return;
        DSB(DS_isr_cursor_busy) = 1;
        /* sti */
        s16 dx = 0, dy = 0;
        if (DSW(DS_kbd_dir_lock) == 0) {          /* keyboard ISR not in the middle of an update */
            s16 kx = DSC(DS_kbd_dir_x), ky = DSC(DS_kbd_dir_y);
            if (kx) dx = kx > 0 ? 1 : -1;
            if (ky) dy = ky > 0 ? 1 : -1;
        }
        if (DSW(DS_demo_active) == 0 && DSB(DS_joy_present)) {        /* the joystick overrides per axis */
            joy_read();
            s16 jx = DSS(DS_joy_x), jy = DSS(DS_joy_y);
            if (jx < DSS(DS_joy_thr_x_lo)) dx = -1;
            else if (DSS(DS_joy_thr_x_hi) < jx) dx = 1;
            if (jy < DSS(DS_joy_thr_y_lo)) dy = -1;
            else if (DSS(DS_joy_thr_y_hi) < jy) dy = 1;
        }
        if (dx | dy) {                            /* +-1 px through the tables DS:474C / DS:4752 */
            DSW(DS_cursor_x) = (u16)(DSW(DS_cursor_x) + DSW((u16)(DS_step_x + dx * 2)));
            DSW(DS_cursor_y) = (u16)(DSW(DS_cursor_y) + DSW((u16)(DS_step_y + dy * 2)));
            DSB(DS_cursor_moved) = 1;
        }
        DSB(DS_isr_cursor_busy) = 0;
    }
}

/* 0000:238c race_isr — platform.md §4.3; the sound part is sound.md §4.6 (snd_race_tick), the car step
 * from 253b race.md §4.7 (modules.race_phys_step). */
void race_isr(void)
{
    DSL(DS_ticks)++;
    u8 old = DSB(DS_chain_count);
    DSB(DS_chain_count) = (u8)(old - 1);
    if (old != 0) {
        /* out 20h,20h (EOI); sti; then the engine note / siren / squeal (reads DS:0608 itself) */
        snd_race_tick();
        return;
    }
    DSL(DS_bios_ticks)++;                         /* 2513 */
    bios_int8();
    DSB(DS_chain_count) = 3;
    if (DSB(DS_racing) == 0) return;              /* 2527: DS:8ACA driving */
    if (DSW(DS_anim_mode) != 0) return;           /* 2531: DS:0286 */
    modules.race_phys_step();                     /* 253b: DS:0609 countdown, controls, one car step */
}

/* 0000:1b91 sign */
s16 sign(s16 v)
{
    if (v == 0) return 0;
    return v > 0 ? 1 : -1;
}

/* 0000:1aad race_isr_enter — platform.md §4.8 */
void race_isr_enter(void)
{
    DSW(0x060A) = 0;
    host_set_tick_handler(race_isr);              /* _dos_setvect(8, 0000:238c) */
    if (DSW(DS_demo_active) == 0 && DSB(DS_mouse_present)) {
        mouse_poll();
        DSW(DS_mouse_acc_y) = 0;
        DSW(DS_mouse_acc_x) = 0;
        DSW(DS_mouse_shift_x) = 1;
        DSW(DS_mouse_shift_y) = 6;
    }
    DSB(DS_click_code) = 0;
    DSB(DS_click_held) = 0;
    DSB(DS_kbd_last_dir + 1) = 0;
    DSB(DS_kbd_last_dir) = 0;
    DSW(0x78E4) = 0;                              /* race: steering momentum */
    DSW(0x78E2) = 0;
    DSB(DS_race_step_count) = 0;
    snd_race_isr_enter();                         /* 1b09: ch2 divisor 1Eh, gate on if sound on */
}

/* 0000:1b27 race_isr_leave — platform.md §4.8 */
void race_isr_leave(void)
{
    host_set_tick_handler(timer_isr);             /* _dos_setvect(8, 0000:1fe2) */
    snd_race_isr_leave();                         /* 1b3b: siren / squeal / off-road off, gate off */
    if (DSW(DS_demo_active) == 0 && DSB(DS_mouse_present)) {
        mouse_poll();
        DSW(DS_mouse_acc_y) = 0;
        DSW(DS_mouse_acc_x) = 0;
        DSW(DS_mouse_shift_x) = 1;
        DSW(DS_mouse_shift_y) = 1;
    }
    DSB(DS_click_code) = 0;
    DSB(DS_click_held) = 0;
    DSB(DS_kbd_last_dir + 1) = 0;
    DSB(DS_kbd_last_dir) = 0;
}

/* ------------------------------------------------------------------------------------------------ */
/* Install / restore / exit                                                                         */

/* 0000:1111 platform_install — platform.md §4.2 */
void platform_install(void)
{
    s16 sx = 0, sy = 0;
    DSW(DS_tq_default_prio) = 10;
    DSW(0x8EF8) = 2;
    DSW(0x8EFA) = 5;
    DSW(0x8EFC) = 5;

    /* Joystick detection (ports 201h): trigger the one-shots, then poll until both A axes have timed
     * out with all buttons up ((v & F3h) == F0h), at most 65535 reads.
     * PORT: the gameport is the host's first gamepad (host_joy_read). Connected with no button held,
     * the reads would see the axes running (F3h) and then F0h; absent, the port floats at FFh and the
     * counter wraps. A held button keeps (v & F3h) != F0h, as on the original. */
    host_pump();                                  /* let SDL report gamepads connected at start-up */
    s16 ax = 0, ay = 0;
    u8 buttons = 0;
    bool pad = host_joy_read(&ax, &ay, &buttons);
    u8 first = pad ? 0xF3 : 0xFF;
    u16 n;                                        /* si */
    u8 v;                                         /* di */
    if (pad && (buttons & 3) == 0) { n = 2; v = 0xF0; }
    else { n = 0; v = pad ? (u8)(0xF3 & ~(buttons << 4)) : 0xFF; }
    if (DSW(DS_demo_active) == 0 && n != 0 && v != first) {
        DSB(DS_joy_present) = 1;                  /* calibrate: the centre starts at 0 */
        joy_read(); sx = DSS(DS_joy_x); sy = DSS(DS_joy_y);
        joy_read(); sx = (s16)(sx + DSS(DS_joy_x)); sy = (s16)(sy + DSS(DS_joy_y));
        joy_read(); sx = (s16)(sx + DSS(DS_joy_x)); sy = (s16)(sy + DSS(DS_joy_y));
        DSS(DS_joy_centre_x) = idiv32_16(sx, 3, NULL);
        DSS(DS_joy_centre_y) = idiv32_16(sy, 3, NULL);
        s16 hx = (s16)(DSS(DS_joy_centre_x) >> 1);                   /* sar */
        DSS(DS_joy_thr_x_lo) = (s16)-hx;
        DSS(DS_joy_thr_x_hi) = hx;
        s16 hy = (s16)(DSS(DS_joy_centre_y) >> 1);
        DSS(DS_joy_thr_y_lo) = (s16)-hy;
        DSS(DS_joy_thr_y_hi) = hy;
    }

    if (DSW(DS_demo_active) == 0 && DSW(DS_mouse_allowed) != 0 && mouse_reset() == -1) {
        DSB(DS_mouse_present) = 1;
        DSW(DS_mouse_acc_y) = 0;
        DSW(DS_mouse_acc_x) = 0;
        DSW(DS_mouse_shift_x) = 1;
        DSW(DS_mouse_shift_y) = 1;
    } else {
        DSB(DS_mouse_present) = 0;
    }
    DSW(DS_cursor_drawn_x) = 0xFFFF;
    DSW(DS_cursor_drawn_y) = 0xFFFF;

    snd_song_defaults();                          /* 1231-1274: sound channel defaults (sound.md) */

    /* PORT: _harderr(0000:105b) is not installed: the port has no INT 24h (file errors come back as
     * return codes). */

    /* cli; PIT channel 0 mode 3, divisor 4000h; INT 8 = 0000:1fe2 (old vector kept in DS:7AAA) */
    host_set_pit_divisor(0x4000);
    ds_far_wr(DS_old_int8, far_make(0xF000, 0xFEA5));   /* PORT: the BIOS INT 8 entry, never called */
    host_set_tick_handler(timer_isr);
    /* sti */
    kbd_install();
    DSW(DS_timer_on) = 1;
    /* out 43h, B6h: channel 2 mode 3 (implied by the host speaker model) */
    input_reset();
    DSW(DS_timer_installed) = 1;
}

/* 0000:0fb7 timer_restore — platform.md §4.2 */
void timer_restore(void)
{
    host_set_tick_handler(NULL);                  /* PIT 0 back to 18.2 Hz, old INT 8 */
    host_set_pit_divisor(0);
    kbd_restore();
    DSW(DS_timer_on) = 0;
    DSW(DS_timer_installed) = 0;
}

/* 0000:0fea platform_exit — platform.md §4.2 */
_Noreturn void platform_exit(void)
{
    if (DSW(DS_demo_active) != 0) {
        DSW(DS_demo_active) = DSW(DS_demo_active) == 0x62 ? 0 : 1;
        modules.fatal_message(ds_str(0x0614));    /* 0f38:23ce "Quit demo" */
        screen_fill_rect(0, 0, 0x140, 200, 0);    /* 0f38:231a */
    }
    if (DSW(DS_timer_installed) != 0) timer_restore();
    if (DSW(DS_gfx_mode_set) != 0) {
        drv_pal_normal();                         /* lcall [DS:78C2] */
        /* PORT: 0000:e610 video_restore (equipment byte, BIOS text mode) has no host equivalent: the
         * window closes below. */
    }
    spk_off();                                    /* in 61h, and FCh, out 61h; DS:47CF = 0 */
    /* PORT: exit(5) of the original; the port exits with 0 (the DOS code carries no meaning). */
    host_shutdown();
    exit(0);
}

/* ------------------------------------------------------------------------------------------------ */
/* Tick-timer queue DS:0572: 5 entries of 18 bytes {next (earlier), prev (later), u32 deadline, prio,  */
/* type, arg, 4 bytes}. Entry addresses are computed with 16-bit arithmetic like the original's      */
/* imul, so an index of -1 (tq_schedule_call on a full queue) lands where it did (DS:0566..).        */

#define TQ(e, field) ((u16)(DS_tq_entries + (u16)((e) * 0x12) + (field)))
#define TQ_NEXT 0x0
#define TQ_PREV 0x2
#define TQ_DUE  0x4
#define TQ_PRIO 0x8
#define TQ_TYPE 0xA
#define TQ_ARG  0xC

/* 0000:1843 tq_insert — platform.md §4.5 */
s16 tq_insert(s16 delay, s16 prio)
{
    DSB(0x0571) = 1;
    s16 si = DSS(DS_tq_free);
    if (si < 0) {
        DSB(0x0571) = 0;
        return -1;
    }
    s16 di = DSS(DS_tq_tail);
    s16 pv = -1;                                  /* [bp-0Ch] */
    u32 due = DSL(DS_ticks) + (u32)(s32)delay;
    if (delay != 0) {
        s16 cx = pv;
        for (;;) {
            if (di < 0) break;
            u32 d = DSL(TQ(di, TQ_DUE));
            if (tick_after(d, due)) {             /* later than the new one: walk towards the earliest */
                cx = di;
                di = DSS(TQ(di, TQ_NEXT));
                continue;
            }
            if (d == due) {                       /* same deadline: the higher priority goes first */
                for (;;) {
                    if (DSS(TQ(di, TQ_PRIO)) >= prio) break;
                    cx = di;
                    di = DSS(TQ(di, TQ_NEXT));
                    if (di < 0) break;
                    if (DSL(TQ(di, TQ_DUE)) != d) break;
                }
            }
            break;
        }
        pv = cx;
    }
    DSL(TQ(si, TQ_DUE)) = due;
    DSS(TQ(si, TQ_PRIO)) = prio;
    DSS(DS_tq_free) = DSS(TQ(si, TQ_NEXT));
    DSS(TQ(si, TQ_NEXT)) = di;
    if (di < 0) {
        DSS(DS_tq_head) = si;
        DSL(DS_tq_next_due) = due;
    } else {
        DSS(TQ(di, TQ_PREV)) = si;
    }
    DSS(TQ(si, TQ_PREV)) = pv;
    if (pv < 0) DSS(DS_tq_tail) = si;
    else DSS(TQ(pv, TQ_NEXT)) = si;
    DSB(0x0571) = 0;
    return si;
}

/* 0000:0f0b tq_remove — platform.md §4.5 */
void tq_remove(s16 e)
{
    s16 nx = DSS(TQ(e, TQ_NEXT));
    if (nx >= 0) {
        DSS(TQ(nx, TQ_PREV)) = DSS(TQ(e, TQ_PREV));
    } else {
        s16 pv = DSS(TQ(e, TQ_PREV));
        DSS(DS_tq_head) = pv;
        if (pv < 0) DSL(DS_tq_next_due) = 0x7FFFFFFFu;
        else DSL(DS_tq_next_due) = DSL(TQ(pv, TQ_DUE));
    }
    s16 pv = DSS(TQ(e, TQ_PREV));
    if (pv >= 0) DSS(TQ(pv, TQ_NEXT)) = DSS(TQ(e, TQ_NEXT));
    else DSS(DS_tq_tail) = DSS(TQ(e, TQ_NEXT));
    DSS(TQ(e, TQ_NEXT)) = DSS(DS_tq_free);
    DSS(DS_tq_free) = e;
}

/* 0000:198a tq_schedule_call — platform.md §4.5 (no check for a full queue, as the original) */
void tq_schedule_call(s16 arg, s16 delay)
{
    s16 e = tq_insert(delay, (s16)DSW(DS_tq_default_prio));
    DSW(TQ(e, TQ_TYPE)) = 1;
    DSS(TQ(e, TQ_ARG)) = arg;
}

/* 0000:19bc tq_cancel_call — platform.md §4.5: walks `next` from the head, i.e. towards earlier
 * entries, so only the head is ever visited (sic, kept). */
void tq_cancel_call(s16 arg)
{
    for (s16 e = DSS(DS_tq_head); e >= 0; e = DSS(TQ(e, TQ_NEXT))) {
        if (DSW(TQ(e, TQ_TYPE)) == 1 && DSS(TQ(e, TQ_ARG)) == arg) DSW(TQ(e, TQ_TYPE)) = 0;
    }
}
