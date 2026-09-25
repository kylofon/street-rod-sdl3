/* Platform: the UI wait loop, input reset, mouse (INT 33h) and joystick (port 201h) —
 * platform.md §2.2, §2.3, §4.5, §4.6, §4.7, §6, §7. */
#include "platform/platform.h"

#include "host.h"
#include "modules.h"
#include "platform/video.h"
#include "sound/sound.h"

/* ------------------------------------------------------------------------------------------------ */
/* Mouse: INT 33h through int86 (1e16:1b9a), replaced by the host mouse.                            */

/* 0f38:799a mouse_reset: 0 if the INT 33h vector 0000:00CC is null, else INT 33h AX=0 (FFFFh = a
 * mouse). PORT: the host always has a mouse driver (the vector test cannot fail); the "reset" puts the
 * host pointer in relative mode, since the game only reads motion and draws its own pointer. */
/* Developer aid (host.h, SR_KEYS mouse entries): the pointer position DS:472A/472C. */
static bool pointer_query(s16 *x, s16 *y)
{
    *x = DSS(DS_cursor_x);
    *y = DSS(DS_cursor_y);
    return true;
}

s16 mouse_reset(void)
{
    host_set_pointer_query(pointer_query);
    host_mouse_set_relative(true);
    s16 mx, my;
    host_mouse_mickeys(&mx, &my);                 /* the reset clears the mickey counters */
    return -1;
}

/* 0f38:79d7 mouse_buttons: INT 33h AX=3 -> DS:05EE = BX & 1, DS:05F0 = BX & 2 */
void mouse_buttons(void)
{
    u8 b = 0;
    host_mouse_read(NULL, NULL, &b);
    DSW(DS_mouse_left) = b & 1;
    DSW(DS_mouse_right) = b & 2;
}

/* 0f38:7a14 mouse_motion: INT 33h AX=0Bh, mickeys since the last call (CX, DX) */
void mouse_motion(s16 *dx, s16 *dy)
{
    host_mouse_mickeys(dx, dy);
}

/* 0f38:7a47 mouse_poll — platform.md §4.5 */
void mouse_poll(void)
{
    s16 mx, my;
    mouse_motion(&mx, &my);
    DSW(DS_mouse_acc_x) = (u16)(DSW(DS_mouse_acc_x) + mx);
    DSW(DS_mouse_acc_y) = (u16)(DSW(DS_mouse_acc_y) + my);

    s16 acc = DSS(DS_mouse_acc_x);
    bool neg = acc < 0;
    s16 a = (s16)((s16)(neg ? -acc : acc) >> (DSB(DS_mouse_shift_x) & 0x1F));   /* sar ax, cl */
    DSS(DS_mouse_dx) = a;
    if (a) {
        if (neg) DSS(DS_mouse_dx) = (s16)-a;
        DSW(DS_mouse_acc_x) = 0;                  /* the remainder is dropped */
    }
    acc = DSS(DS_mouse_acc_y);
    neg = acc < 0;
    a = (s16)((s16)(neg ? -acc : acc) >> (DSB(DS_mouse_shift_y) & 0x1F));
    DSS(DS_mouse_dy) = a;
    if (a) {
        if (neg) DSS(DS_mouse_dy) = (s16)-a;
        DSW(DS_mouse_acc_y) = 0;
    }

    mouse_buttons();
    if (DSW(DS_mouse_left) != 0) {
        if (DSB(DS_click_held) == 0) {
            DSB(DS_click_code) = 1;
            DSW(DS_click_x) = DSW(DS_cursor_x);
            DSW(DS_click_y) = DSW(DS_cursor_y);
        }
        DSB(DS_click_held) |= 4;
    } else {
        DSB(DS_click_held) &= 0xFB;
    }
}

/* ------------------------------------------------------------------------------------------------ */
/* Joystick                                                                                          */

/* 0000:2df4 joy_read — platform.md §4.6. The original times the gameport one-shots (at most 400h
 * reads); the counts, minus the calibration centre DS:471E/4720, go to DS:4718/471A, the buttons to
 * DS:471C (and a click).
 * PORT: the gameport is the host's first gamepad: raw count = 100h + axis * FFh / 32767 (1..1FFh, so
 * a centred stick reads 100h and the thresholds +-centre/2 are half deflection). Without a gamepad
 * the one-shots never fire and the original leaves through its timeout path: nothing is updated. */
void joy_read(void)
{
    s16 ax, ay;
    u8 b;
    if (!host_joy_read(&ax, &ay, &b)) return;     /* jcxz: timeout */
    s16 cx = (s16)(0x100 + (s32)ax * 0xFF / 32767);
    s16 cy = (s16)(0x100 + (s32)ay * 0xFF / 32767);
    DSS(DS_joy_x) = (s16)(cx - DSS(DS_joy_centre_x));
    DSS(DS_joy_y) = (s16)(cy - DSS(DS_joy_centre_y));
    u8 bt = b & 3;                                /* not al; shr al, 4; and al, 3 */
    DSB(DS_joy_buttons) = bt;
    if (bt) {
        if (DSB(DS_click_held) == 0) {
            DSB(DS_click_code) = bt;
            DSW(DS_click_x) = DSW(DS_cursor_x);
            DSW(DS_click_y) = DSW(DS_cursor_y);
        }
        DSB(DS_click_held) |= 2;
    } else {
        DSB(DS_click_held) &= 0xFD;
    }
}

/* ------------------------------------------------------------------------------------------------ */
/* Input reset, clicks                                                                               */

/* 0000:0e24 input_reset — platform.md §4.5 */
void input_reset(void)
{
    DSW(DS_tq_tail) = 0xFFFF;
    DSW(DS_tq_head) = 0xFFFF;
    DSW(DS_tq_free) = 0;
    DSL(DS_tq_next_due) = 0x7FFFFFFFu;
    DSB(0x0571) = 0;
    DSB(0x0570) = 0;
    for (u16 i = 0; i < 4; i++) DSW((u16)(DS_tq_entries + i * 0x12)) = (u16)(i + 1);
    DSW((u16)(DS_tq_entries + 4 * 0x12)) = 0xFFFF;           /* DS:05BA */

    u32 until = ticks_now() + 10;                 /* 10 ticks without a key */
    while (tick_after(until, ticks_now())) {
        s16 n = 0;
        while ((u8)key_read() != 0) n++;
        if (n) until = ticks_now() + 10;
        host_pump();
    }
    DSL(DS_wait_deadline) = 0;
    if (DSW(DS_demo_active) == 0 && DSB(DS_mouse_present)) {
        mouse_poll();
        DSW(DS_mouse_acc_y) = 0;
        DSW(DS_mouse_acc_x) = 0;
    }
    DSB(DS_click_code) = 0;
    for (u16 i = 0; i < 16; i++) DSB((u16)(0x47D8 + i)) = 0;
    DSB(DS_kbd_last_dir + 1) = 0;
    DSB(DS_kbd_last_dir) = 0;
    DSB(DS_kbd_dir_y) = 0;
    DSB(DS_kbd_dir_x) = 0;
    DSB(DS_click_held) = 0;
}

/* 0000:0d78 click_clear */
void click_clear(void)
{
    DSB(DS_cursor_moved) = 1;
    DSW(0x8B92) = 0xFFFF;                         /* current hotspot rect: none */
    DSW(0x8BAA) = 0x7D00;
    DSB(DS_click_code) = 0;
    DSB(DS_click2_code) = 0;
    key_read();                                   /* discards the key slot */
}

/* ------------------------------------------------------------------------------------------------ */
/* The UI wait loop                                                                                  */

/* PORT: the palette rotation of ui_wait runs on every loop iteration while (ticks & 0Fh) == 0, i.e. a
 * CPU-speed dependent number of times during that tick; the port rotates once per qualifying tick
 * (platform.md §7). Host-side bookkeeping, not game state. */
static u32 pal_rotated_tick = 0xFFFFFFFFu;

/* status_print (0f38:20a1) with a DGROUP string */
static void status_ds(s16 x, u16 s, s16 w) { status_print(x, ds_str(s), w); }

/* 0000:1417 ui_wait — platform.md §4.5 */
s16 ui_wait(s16 max_ticks)
{
    s16 k;                                        /* [bp-12h]: key, later the previous hotspot */
    s16 cx = 0, cy = 0;                           /* [bp-8], [bp-0Ah] */
    bool label_shown = false;                     /* [bp-14h] */

    if (DSW(DS_screen_id) == 0x16) {              /* screen 16h: clear the status texts */
        DSW(0x8256) = 1;
        status_ds(-1, 0x068C, 0);
        DSW(0x8256) = 1;
        status_ds((s16)DSW(0x51EE), 0x068D, (s16)DSW(0x8BC0));
        DSB(0x7F32) = 0;
    }
    bool timed = DSL(DS_wait_deadline) != 0;
    u32 until = ticks_now() + (u32)(s32)max_ticks;
    do {
        host_pump();
        if (DSW(DS_demo_active) == 0x63 && DSW(DS_demo_quit_done) != 0x63) {
            DSW(DS_demo_quit_done) = 0x63;
            platform_exit();
        }
        if (DSW(DS_pal_cycle_on) != 0 && (DSS(DS_driver_id) == -2 || DSS(DS_driver_id) == -6) &&
            (DSB(DS_ticks) & 0x0F) == 0 && DSW(DS_music_enabled) != 0 && DSB(DS_speaker_on) != 0 &&
            DSL(DS_ticks) != pal_rotated_tick) {
            pal_rotated_tick = DSL(DS_ticks);
            u8 c = DSB(0x069A);
            DSB(0x069A) = DSB(0x0699);
            DSB(0x0699) = DSB(0x0698);
            DSB(0x0698) = c;
            ega_set_palreg(6, DSC(0x0698));
            ega_set_palreg(7, DSC(0x0699));
            ega_set_palreg(8, DSC(0x069A));
        }

        k = (s8)key_read();                       /* cbw */
        if (k > 0) {
            if (DSW(DS_wait_any_key) != 0) {
                DSW(DS_wait_key) = (u16)k;
                return 0x3EB;
            }
            if (k == 0x7F) platform_exit();
        } else if (k < 0) {
            k = (s16)(k + 0x32);
            if (k != -0x28) return k;             /* per-screen hot key */
            if (DSW(DS_music_mode) == 1) music_toggle();              /* 'M' (A6h): music on / off */
        }

        if (!tick_after(DSL(DS_tq_next_due), ticks_now())) {        /* a tick timer is due */
            s16 e = DSS(DS_tq_head);
            u16 base = (u16)(DS_tq_entries + (u16)(e * 0x12));
            if (DSW((u16)(base + 0xA)) == 1) {
                s16 arg = DSS((u16)(base + 0xC));
                tq_remove(e);
                modules.timer_callback(arg);      /* 0f38:1e41 */
            } else {
                tq_remove(e);
            }
        }

        if (DSW(DS_demo_active) == 0 && DSB(DS_mouse_present)) {
            mouse_poll();
            u8 moved = (DSW(DS_mouse_dx) != 0 || DSW(DS_mouse_dy) != 0) ? 1 : 0;
            DSB(DS_cursor_moved) |= moved;
            if (DSB(DS_cursor_moved) != 0) {
                DSW(DS_cursor_x) = (u16)(DSW(DS_cursor_x) + DSW(DS_mouse_dx));
                DSW(DS_cursor_y) = (u16)(DSW(DS_cursor_y) + DSW(DS_mouse_dy));
            }
        }

        if (DSC(DS_click2_code) != 0 || DSC(DS_click_code) != 0) {  /* a click event */
            /* cli */
            DSB(DS_last_click_code) = DSB(DS_click_code);
            cx = DSS(DS_click_x);
            cy = DSS(DS_click_y);
            DSB(DS_click_code) = 0;
            DSB(DS_click2_code) = 0;
            /* sti */
            if (DSW(DS_click_returns) != 0) {
                DSW(DS_wait_key) = 0;
                return 0x3EB;
            }
            if (DSW(0x8B8E) != 0) {               /* hotspots active */
                s16 id;
                if (cx >= DSS(0x8B90) && cx < DSS(0x8B92) && cy >= DSS(0x8B94) && cy < DSS(0x8B96))
                    id = DSS(0x8BAA);
                else
                    id = modules.hotspot_at(cx, cy);          /* 0f38:4dad */
                if (id != 0) return id;
            }
            if (DSW(DS_wait_any_key) != 0) {
                DSW(DS_wait_key) = 0;
                return 0x3EB;
            }
        } else if (timed && tick_after(ticks_now(), DSL(DS_wait_deadline))) {
            DSL(DS_wait_deadline) = 0;            /* strict: one tick after the deadline */
            return 1000;
        }

        if (DSC(DS_g_ptr_show) > 0 && DSB(DS_cursor_moved) != 0) {  /* move the drawn pointer */
            /* cli */
            s16 x = DSS(DS_cursor_x);
            DSS(DS_cursor_drawn_x) = x;
            if (x < 0 || x > 0x13A) {
                x = x < 0 ? 0 : 0x13A;
                DSS(DS_cursor_x) = x;
                DSS(DS_cursor_drawn_x) = x;
            }
            s16 y = DSS(DS_cursor_y);
            DSS(DS_cursor_drawn_y) = y;
            if (y < 0 || y > 0xC2) {
                y = y < 0 ? 0 : 0xC2;
                DSS(DS_cursor_y) = y;
                DSS(DS_cursor_drawn_y) = y;
            }
            DSB(DS_cursor_moved) = 0;
            /* sti */
            cy = DSS(DS_cursor_drawn_y);
            cx = DSS(DS_cursor_drawn_x);
            cursor_redraw(cx, cy);                /* 0f38:2e8b */
            if (DSW(0x8B8E) != 0 &&
                !(cx >= DSS(0x8B90) && cx < DSS(0x8B92) && cy >= DSS(0x8B94) && cy < DSS(0x8B96))) {
                k = DSS(0x8BAA);                  /* the previous hotspot */
                s16 id = modules.hotspot_at(cx, cy);
                if (id != 0) {
                    if (k != id) {
                        if (DSW(DS_screen_id) == 0x25) {
                            DSW(0x8256) = 1;
                            status_ds(-1, 0x068E, 0);
                            DSW(0x8256) = 1;
                            status_ds((s16)DSW(0x51EE), 0x068F, (s16)DSW(0x8BC0));
                        }
                        /* hover label from HOT_DATA: 0x239E + current hotspot record [+4] */
                        status_ds(-1, (u16)(0x239E + DSW((u16)(DSW(0x8BA8) + 4))), 0);
                        label_shown = true;
                    }
                } else {
                    if (label_shown) {
                        status_ds(-1, 0x0690, 0);
                        label_shown = false;
                    }
                    if (DSW(DS_screen_id) == 0x25) status_ds((s16)DSW(0x51EE), 0x7F32, (s16)DSW(0x8BC0));
                    else if (k != 0) status_ds(-1, 0x0691, 0);
                }
            }
        }
    } while (tick_after(until, ticks_now()));
    return 0;
}

/* 0000:3a83 wait_ticks_or_input — platform.md §4.10 */
void wait_ticks_or_input(s16 n)
{
    DSL(DS_wait_deadline) = ticks_now() + (u32)(s32)n;
    DSW(DS_wait_any_key) = 1;
    ui_wait(30000);
    DSL(DS_wait_deadline) = 0;
    DSW(DS_wait_any_key) = 0;
}

/* 0f38:0b51 wait_click_or_key — platform.md §4.5 */
void wait_click_or_key(s16 n)
{
    u32 until = ticks_now() + (u32)(s32)n;
    while (tick_after(until, ticks_now())) {
        host_pump();
        if (DSB(DS_click_code) != 0) {
            DSB(DS_click_code) = 0;
            return;
        }
        if (DSB(DS_mouse_present)) {
            mouse_buttons();
            if (DSW(DS_mouse_left) != 0) return;
        }
        if (DSB(DS_joy_present)) {
            joy_read();
            if (DSB(DS_joy_buttons) != 0) return;
        }
        u8 k = (u8)key_read();
        if (k == 'M') music_toggle();
        else if (k == '\r' || k == 0x1B) return;
    }
}
