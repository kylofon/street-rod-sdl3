/* Platform: the keyboard — INT 9 handler 0000:2c42 with the translation table DS:4766, the single-byte
 * key slot, per-screen hot keys and text mode — platform.md §2.2, §4.4, §5.2.
 *
 * The host delivers the XT set-1 byte stream (make / break codes, E0/E1 prefixes) in event order; each
 * byte is one INT 9 with that value at port 60h. The translation tables live in mem[] and are patched
 * by the game exactly as in the original. */
#include "platform/platform.h"

#include "host.h"

/* 0000:2c42 kbd_isr — platform.md §4.4 */
void kbd_isr(u8 sc)
{
    /* in al,61h; out 61h, al|80h; out 61h, al: the XT keyboard acknowledge (bit 7). The host speaker
     * model stores bits 2-7 only, so this has no audible effect. */
    u8 p = host_port61_get();
    host_port61((u8)(p | 0x80));
    host_port61(p);

    if (DSW(DS_demo_active) != 0 && sc != 0x1D && sc != 0x9D && sc != 0x32) {
        if (sc == 0x01) DSW(DS_demo_active) = 0x63;       /* Esc aborts the demo */
        return;                                           /* out 20h,20h */
    }
    if (sc == 0xFF || sc >= 0xE0) return;                 /* E0 / E1 prefixes and overruns ignored */

    bool make = !(sc & 0x80);
    u8 i = sc & 0x7F;
    if (i > 0x55) { i = 0x56; make = true; }              /* table[56h] = 0 */
    s8 k = (s8)DSB((u16)(DS_kbd_xlat + i));
    if (k == 0) return;
    if (k < 0 && k >= -30) {                              /* direction key: -2, -4, ... -16 */
        u16 dxy = (u16)(DS_kbd_xlat + k);                 /* (dx, dy) at DS:4756..4765 */
        u16 held = (u16)(0x47E8 + k);                     /* held flags DS:47CA..47E7 */
        if (make) {
            if (DSB(held) != 0) return;
            DSB(held) = 1;
            DSW(DS_kbd_dir_lock) = 0xFFFF;
            DSB(DS_kbd_dir_x) = (u8)(DSB(DS_kbd_dir_x) + DSB(dxy));
            DSB(DS_kbd_dir_y) = (u8)(DSB(DS_kbd_dir_y) + DSB((u16)(dxy + 1)));
            DSW(DS_kbd_last_dir) = DSW(dxy);
            DSW(DS_kbd_dir_lock) = 0;
        } else {
            if (DSB(held) == 0) return;
            DSW(DS_kbd_dir_lock) = 0xFFFF;
            DSB(held) = 0;
            DSB(DS_kbd_dir_x) = (u8)(DSB(DS_kbd_dir_x) - DSB(dxy));
            DSB(DS_kbd_dir_y) = (u8)(DSB(DS_kbd_dir_y) - DSB((u16)(dxy + 1)));
            DSW(DS_kbd_dir_lock) = 0;
        }
        return;
    }
    u8 key = (u8)k;
    if (make) {
        if (key == 0x1F) {                                /* primary click: Space, Ins, keypad 0, 55h */
            if (DSB(DS_click_held) == 0) {
                DSW(DS_kbd_dir_lock) = 0xFFFF;
                DSB(DS_click_code) = 0x1F;
                DSW(DS_click_x) = DSW(DS_cursor_x);
                DSW(DS_click_y) = DSW(DS_cursor_y);
                DSW(DS_kbd_dir_lock) = 0;
            }
            DSB(DS_click_held) |= 1;
            DSB(DS_click_edge) = 1;
            DSW(DS_click_seen) = 1;
        } else if (key == 0x1E) {                         /* secondary click (no key in the default table) */
            if (DSB(DS_click_held) == 0) {
                DSW(DS_kbd_dir_lock) = 0xFFFF;
                DSB(DS_click2_code) = 0x1E;
                DSW(DS_click2_x) = DSW(DS_cursor_x);
                DSW(DS_click2_y) = DSW(DS_cursor_y);
                DSW(DS_kbd_dir_lock) = 0;
            }
        } else if (key == 0x1D) {
            DSB(DS_sound_on) ^= 1;                        /* Ctrl: sound on / off */
        } else {
            DSB(DS_key) = key;                            /* the single key slot (overwrites) */
        }
    } else if (key == 0x1F) {
        DSW(DS_kbd_dir_lock) = 0xFFFF;
        DSB(DS_click_held) &= 0xFE;
        DSW(DS_kbd_dir_lock) = 0;
    }
}

/* 0000:2db6 key_read: cli; al = DS:47D0; DS:47D0 = 0; sti (AH = 0) */
s16 key_read(void)
{
    u8 k = DSB(DS_key);
    DSB(DS_key) = 0;
    return k;
}

/* Keys released while the window has no focus never send their break code; the host reports the loss
 * of focus and the port sends the break codes of every key (breaks of keys that are up are no-ops in
 * kbd_isr; Ctrl acts on its make code only).
 * PORT: host-side, no counterpart in the original. */
static void kbd_focus_lost(void)
{
    for (u8 sc = 0x01; sc <= 0x55; sc++) kbd_isr((u8)(sc | 0x80));
}

/* 0000:2dc3 kbd_install: INT 9 = 0000:2c42, the old vector in DS:47D4/47D6 */
void kbd_install(void)
{
    ds_far_wr(DS_old_int9, far_make(0xF000, 0xE987));    /* PORT: the BIOS INT 9 entry, never called */
    host_set_kbd_handler(kbd_isr);
    host_set_focus_lost_handler(kbd_focus_lost);
}

/* 0000:2de2 kbd_restore */
void kbd_restore(void)
{
    host_set_kbd_handler(NULL);
    host_set_focus_lost_handler(NULL);
}

/* 0000:12de kbd_remap_screen — platform.md §4.4 */
void kbd_remap_screen(s16 screen)
{
    if (DSW(DS_demo_active) != 0) DSB(0x47CC) = 0;
    DSW(DS_screen_id) = (u16)screen;
    kbd_remap_restore(0);
    DSB(0x064E) = (u8)screen;
    u16 si = DSW((u16)(0x579F + screen * 2));     /* pairs {scan code, new key}, 0-terminated */
    u16 di = 0x0650;
    for (s16 n = 1; DSB(si) != 0 && n < 0x1E; n++, si += 2, di += 2) {
        u8 sc = DSB(si);
        DSB(di) = sc;
        u16 slot = (u16)(DS_kbd_xlat + (s8)sc);   /* cbw */
        DSB((u16)(di + 1)) = DSB(slot);
        DSB(slot) = DSB((u16)(si + 1));
    }
    si = DSW(0x57F3);                             /* -> DS:579C = {32h 'M', A6h}: the music toggle */
    u8 sc = DSB(si);
    DSB(di) = sc;
    u16 slot = (u16)(DS_kbd_xlat + (s8)sc);
    DSB((u16)(di + 1)) = DSB(slot);
    DSB(slot) = DSB((u16)(si + 1));
    DSB((u16)(di + 2)) = 0;
}

/* 0000:1391 kbd_remap_restore — platform.md §4.4 */
s16 kbd_remap_restore(s16 keep_low)
{
    for (u16 i = 0; i < 16; i++) DSB((u16)(0x47D8 + i)) = 0;
    DSB(DS_kbd_last_dir + 1) = 0;
    DSB(DS_kbd_last_dir) = 0;
    DSB(DS_kbd_dir_y) = 0;
    DSB(DS_kbd_dir_x) = 0;
    s16 was = DSC(0x064E);
    if (was != 0) {
        u16 si = 0x0650;
        if (keep_low != 0) {
            for (; DSB(si) != 0; si += 2)
                if (DSC((u16)(si + 1)) >= 0x20) DSB((u16)(DS_kbd_xlat + (s8)DSB(si))) = DSB((u16)(si + 1));
        } else {
            for (; DSB(si) != 0; si += 2) DSB((u16)(DS_kbd_xlat + (s8)DSB(si))) = DSB((u16)(si + 1));
            DSB(0x064E) = 0;
        }
    }
    return was;
}

/* 0000:2c04 kbd_text_mode_on (cli ... sti) */
void kbd_text_mode_on(s16 keep_space)
{
    if (keep_space == 0) DSB(0x479F) = 0x20;      /* Space (39h) -> ' ' */
    DSB(0x4774) = 0x08;                           /* Backspace (0Eh) */
    DSB(0x4799) = 0x2C;                           /* ',' (33h) */
    DSB(0x479A) = 0x2E;                           /* '.' (34h) */
    DSB(DS_click_held) &= 0xFE;
}

/* 0000:2c2b kbd_text_mode_off (cli ... sti) */
void kbd_text_mode_off(void)
{
    DSB(0x4774) = 0;
    DSB(0x4799) = 0;
    DSB(0x479A) = 0;
    DSB(0x479F) = 0x1F;                           /* Space: the click again */
}
