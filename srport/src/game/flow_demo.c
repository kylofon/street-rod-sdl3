/* game_flow: demo mode — the autoplayer demo_step 0000:1bae (called from the timer ISR every 128 ticks,
 * every 16 while a key release is pending) and its key injection 0000:2e93 — port/spec/game_flow.md
 * §4.14.
 *
 * demo_step picks a hot key of the current screen (DS:6C4C, key tables DS:579F) with per-screen rules,
 * moves the demo cursor target (DS:06AE / 06B0; timer_isr glides the pointer there) to the menu element
 * with that code, and once the pointer has arrived injects the key's scan code (DS:47CC) and, on the
 * next call, its release. */
#include "game/flow.h"

#include "game/garage.h"
#include "game/ui.h"
#include "platform/platform.h"

#define DEMO_SC      0x47CC             /* u8 scan code to inject */
#define DEMO_WAITING 0x06A7             /* u8: the pointer is still on its way */
#define DEMO_CYCLE   0x06A8             /* i16: garage key cycle position */
#define DEMO_PREV    0x06AA             /* i16: screen of the previous key */
#define DEMO_06AC    0x06AC             /* i16 */
#define DEMO_SCRIPT  0x06B2             /* i16: position in the script DS:06B4 (screen 14h) */
#define DEMO_PICK    0x6C50             /* i16: the chosen key index */
#define MENU_FIRST   0x2BAD             /* u16[]: first element of each menu */
#define MENU_ELEM    0x09F4             /* 0x12-byte menu elements: +6 code, +8 x0, +A y0, +C x1, +E y1 */

/* 0000:2e93 kbd_inject_scancode: the key part of the INT 9 handler 0000:2c42 for the byte DS:47CC,
 * without the demo guard and without click_edge / click_seen (as the original). */
void kbd_inject_scancode(void)
{
    u8 sc = DSB(DEMO_SC);
    if (sc >= 0xE0) return;
    bool brk = (sc & 0x80) != 0;
    u8 i = sc & 0x7F;
    if (i > 0x55) { i = 0x56; brk = false; }
    s8 k = (s8)DSB((u16)(DS_kbd_xlat + i));
    if (k == 0) return;
    if (k < 0 && k >= -30) {                          /* direction key */
        u16 dxy = (u16)(DS_kbd_xlat + k);
        u16 held = (u16)(0x47E8 + k);
        if (!brk) {
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
    if (!brk) {
        if (key == 0x1F) {
            if (DSB(DS_click_held) == 0) {
                DSW(DS_kbd_dir_lock) = 0xFFFF;
                DSB(DS_click_code) = 0x1F;
                DSW(DS_click_x) = DSW(DS_cursor_x);
                DSW(DS_click_y) = DSW(DS_cursor_y);
                DSW(DS_kbd_dir_lock) = 0;
            }
            DSB(DS_click_held) |= 1;
        } else if (key == 0x1E) {
            if (DSB(DS_click_held) == 0) {
                DSW(DS_kbd_dir_lock) = 0xFFFF;
                DSB(DS_click2_code) = 0x1E;
                DSW(DS_click2_x) = DSW(DS_cursor_x);
                DSW(DS_click2_y) = DSW(DS_cursor_y);
                DSW(DS_kbd_dir_lock) = 0;
            }
        } else if (key == 0x1D) {
            DSB(DS_sound_on) ^= 1;
        } else {
            DSB(DS_key) = key;
        }
    } else if (key == 0x1F) {
        DSW(DS_kbd_dir_lock) = 0xFFFF;
        DSB(DS_click_held) &= 0xFE;
        DSW(DS_kbd_dir_lock) = 0;
    }
}

/* 0000:1bae demo_step */
void demo_step(void)
{
    s16 scr = DSS(DS_screen_id);
    if (scr == 2) rnd(-1);
    if (DSB(DS_demo_fast)) {                          /* release the key pressed last time */
        DSB(DEMO_SC) |= 0x80;
        DSB(DS_demo_fast) = 0;
        kbd_inject_scancode();
        return;
    }
    if (DSB(DEMO_WAITING) == 0) {
        if (DSW(DEMO_06AC) != 0 && scr == 10) {
            DSW(DEMO_06AC) = 0;
            DSW(DEMO_CYCLE) = 0x0F;
        }
        u16 tbl = DSW((u16)(DS_kbd_remap_tables + 2 * scr));
        u16 len = 0;
        while (DSB((u16)(tbl + len)) != 0) len++;
        s16 nkeys = (s16)((s16)len / 2);              /* key pairs of the screen */
        s16 extra = 0;
        switch (scr) {
        case 8:    extra = 4; break;
        case 9:    if (DSW(DS_car) == 0) nkeys--; break;
        case 0x0B: nkeys -= 2; extra = 1; break;
        case 0x25: extra = 1; break;
        case 0x0D: nkeys--; break;
        case 0x0E: case 0x12: case 0x13: case 0x16: case 0x24: nkeys -= 2; break;
        case 0x17: DSW(DEMO_CYCLE) = 4; break;
        case 0x1C: case 0x23: extra = 2; break;
        default: break;
        }
        if (scr == 10 && DSS(DEMO_PREV) != 10) {      /* the garage cycles through its keys */
            s16 c = DSS(DEMO_CYCLE);
            DSW(DEMO_CYCLE) = (u16)(c + 1);
            DSW(DEMO_PICK) = (u16)(nkeys - c - 1);
            s16 rem;
            idiv32_16(DSS(DEMO_CYCLE), nkeys, &rem);
            DSW(DEMO_CYCLE) = (u16)rem;
        } else {
            DSW(DEMO_PICK) = (u16)rnd((s16)(nkeys + extra));
        }
        s16 v = DSS(DEMO_PICK);
        if (scr == 0x14) {                            /* scripted */
            v = DSS((u16)(0x06B4 + 2 * DSS(DEMO_SCRIPT)));
            DSW(DEMO_SCRIPT)++;
            if (v == 2) DSW(DEMO_SCRIPT) = 0;
        } else if (scr == 2) {
            v = 1;
        } else if (scr == 4) {
            v = DSS(DEMO_CYCLE) < 0x0B ? 0 : (s16)(nkeys - 1);
        } else if (scr == 7) {
            if (DSW(DS_car) == 0) v = 0;
        } else if (scr == 0x0B) {
            if (DSS(DEMO_PREV) == 0x0B) v = 10;
        } else if (scr == 0x15) {
            if ((v >= 0 && v < 9) || v == 0x0C) v = 9;
            if (DSS(DEMO_PREV) == 0x1C) v = 0x0B;
        } else if (scr == 0x17) {
            if (v == 0) v = 1;
        } else if (scr == 0x1B) {
            v = 5;
        } else if (scr == 0x22 || scr == 0x29) {
            v = 0;
        } else if (scr == 0x24) {
            if (DSS(DEMO_PREV) == 0x24) v = 0;
        }
        DSW(DEMO_PICK) = (u16)v;
        if (scr == 8) DSW(DS_demo_cursor_x) = 0xA0;
        if (v < 0) {
            DSB(DEMO_SC) = 0x4C;                      /* keypad 5 */
        } else if (v < nkeys) {
            u16 pair = (u16)(tbl + 2 * v);
            DSB(DEMO_SC) = DSB(pair);
            s16 code = (s16)((s8)DSB((u16)(pair + 1)) + 0x32);
            s16 first = DSS((u16)(MENU_FIRST + 2 * scr));
            s16 e = first;
            while ((s16)(e - first) < 0x20 && DSS((u16)(MENU_ELEM + 6 + e * 0x12)) != code) e++;
            u16 el = (u16)(MENU_ELEM + e * 0x12);
            if (DSS((u16)(el + 6)) == code) {         /* aim the pointer at the element's centre */
                s16 x = (s16)((DSS((u16)(el + 8)) + DSS((u16)(el + 0x0C))) / 2);
                if (x != 0) DSW(DS_demo_cursor_x) = (u16)x;
                if (scr == 8) DSW(DS_demo_cursor_x) = 0xA0;
                s16 y = (s16)((DSS((u16)(el + 0x0A)) + DSS((u16)(el + 0x0E))) / 2);
                if (y != 0) DSW(DS_demo_cursor_y) = (u16)y;
            }
        } else {
            if (v == nkeys)     DSB(DEMO_SC) = 0x1C;  /* Enter */
            if (v == nkeys + 1) DSB(DEMO_SC) = 0x39;  /* Space */
            if (v == nkeys + 2) DSB(DEMO_SC) = 0x48;  /* Up */
            if (v == nkeys + 3) DSB(DEMO_SC) = 0x50;  /* Down */
            if (v == nkeys + 4) DSB(DEMO_SC) = 0x4B;  /* Left */
            if (v == nkeys + 5) DSB(DEMO_SC) = 0x4D;  /* Right */
        }
    }
    /* 1f0e: press the key once the pointer has arrived */
    if ((DSW(DS_demo_cursor_x) != 0 && DSW(DS_cursor_x) != DSW(DS_demo_cursor_x)) ||
        (DSW(DS_demo_cursor_y) != 0 && DSW(DS_cursor_y) != DSW(DS_demo_cursor_y))) {
        DSB(DEMO_WAITING) = 1;
        return;
    }
    DSB(DEMO_WAITING) = 0;
    if (scr == 0x25 && DSS(DEMO_06AC) < 10 && DSB(DEMO_SC) == 0x22) DSB(DEMO_SC) = 0x1C;
    if (scr == 10 && DSS(DEMO_PREV) == 10) {
        DSB(DEMO_SC) = 0x1C;
        DSW(DEMO_PREV) = 0;
    } else {
        DSW(DEMO_PREV) = (u16)scr;
    }
    if (DSW(DS_spare_parts) == 0 && scr == 7) spare_add(0x2A, DSS(0x4956));   /* a free part in the paper */
    if (DSW(DS_car) == 0) {
        if (scr == 10) DSB(DEMO_SC) = 0x31;           /* N: the newspaper */
        if (scr == 8 && (DSB(DEMO_SC) == 1 || DSB(DEMO_SC) == 0x22 || DSB(DEMO_SC) == 0x19))
            DSB(DEMO_SC) = 0x39;
    }
    if (DSB(DEMO_SC) == 0x46) DSB(DEMO_SC) = 0x1C;
    DSB(DS_demo_fast) = 1;
    kbd_inject_scancode();
}
