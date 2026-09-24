/* The 0f38 UI toolkit: list boxes 0f38:4f4f - 590b and text entry 0f38:5910 - 5e54. Ported from the
 * disassembly (garage.md §4.19 describes the list box). */
#include "game/ui.h"

#include <string.h>

#include "platform/platform.h"
#include "platform/video.h"
#include "sound/sound.h"

#define U_NOFRAME   0x8B98
#define U_ROW0      0x8B9A
#define U_NROWS     0x8B9C
#define U_LIST      0x8B9E
#define U_SEL       0x8BA0
#define U_EDITREC   0x8BA2
#define U_EDITFLAG  0x8BA4
#define U_CODE      0x8BAA
#define NUMERIC     0x09F2      /* u8: digits only (edit_number) */

static inline s16 rw(u16 rec, u16 off) { return DSS((u16)(rec + off)); }

static void set_scheme(s16 style)
{
    u16 src = (u16)(DSW(DS_g_scheme) + (u16)(style * 5));
    for (u16 i = 0; i < 5; i++) DSB((u16)(DS_g_text_fg + i)) = DSB((u16)(src + i));
}

/* MS C atoi (1e16:197c) on a DGROUP / stack string: whitespace, sign, digits, 16-bit result. */
static u16 crt_atoi(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    bool neg = false;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    u16 v = 0;
    while (*s >= '0' && *s <= '9') v = (u16)(v * 10 + (u16)(*s++ - '0'));
    return neg ? (u16)-v : v;
}

/* 0f38:4fc1 (near): "(`new`)" (DS:07B6) or the number written into "(NN% worn)" at DS:07AA. */
u16 wear_text(s16 pct)
{
    if (pct == 0) return 0x07B6;
    DSB(0x07AD) = ' ';
    char tmp[8];
    int v = pct, n = 0;                           /* MS C itoa, radix 10 (signed) */
    char *d = ds_str(0x07AB);
    if (v < 0) { *d++ = '-'; v = -v; }
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *d++ = tmp[--n];
    *d = 0;
    DSB((u16)(0x07AA + strlen(ds_str(0x07AB)) + 1)) = '%';
    return 0x07AA;
}

/* 0f38:4f4f */
s16 list_box(s16 screen, s16 sel, const s16 *xoff, s16 *list, const s8 *wear, const u16 *extra)
{
    if (list[0] == 0) return 0x3EA;
    s16 x, y;
    FarPtr b = ui_save_bg(screen, &x, &y, 2);
    s16 r = list_box_run(screen, sel, xoff, list, wear, extra);
    screen_put_bitmap_mirror(b, x, y);
    arena_pop(1);
    return r;
}

/* The text of the edit line: TEXTS[list[1 + idx]] + " " + wear text (0f38:500e @50cc / @559b). */
static void edit_line_fill(s16 screen, char *buf, const s16 *list, const s8 *wear, s16 idx, bool click)
{
    strcpy(buf, ds_str((u16)(list[1 + idx] + 0x2C02)));
    if (click && screen == 5 && strcmp(ds_str(0x4607), buf) == 0) memcpy(buf, ds_str(0x07C2), 2);
    if (wear != NULL && wear[1 + idx] != (s8)0x80) {
        s8 w = wear[1 + idx];
        strcat(buf, ds_str(0x07BE));
        strcat(buf, ds_str(wear_text(w)));
    }
}

/* 0f38:500e (garage.md §4.19). sel is 1-based; internally the selection is kept as a row slot
 * ([bp+8]); the highlight frame is drawn at that slot even when the list is scrolled (sic). */
s16 list_box_run(s16 screen, s16 sel, const s16 *xoff, s16 *list, const s8 *wear, const u16 *extra)
{
    char *ebuf = NULL;                            /* [bp-20h] */
    s16 top = 0;                                  /* [bp-2Eh] */
    s16 hl = -1;                                  /* [bp-6]: hover-highlighted row slot */
    s16 maxlen = 0;                               /* [bp-12h] */
    if (list[0] == 0) return 0x3EA;
    sel--;                                        /* [bp+8] */
    s16 count = list[0];                          /* [bp-3Eh] */
    ui_push(screen);
    s16 rows = DSS(U_NROWS);                      /* [bp-2Ch] */
    {   /* DS:8B9E = the list (near pointer); PORT: 0 when the list is not in DGROUP */
        ptrdiff_t o = (const u8 *)list - mp(DGROUP, 0);
        DSW(U_LIST) = (o >= 0 && o < 0x10000) ? (u16)o : 0;
    }
    DSW(U_SEL) = (u16)(sel + 1);
    if (count > rows) top = (s16)(sel + rows) > count ? (s16)(count - rows) : sel;
    u16 erec = DSW(U_EDITREC);                    /* [bp-1Ah] */
    if (erec != 0) {
        ebuf = ds_str((u16)(rw(erec, UI_R_LABEL) + 0x239E));
        maxlen = (s16)((s16)(rw(erec, UI_R_X1) - rw(erec, UI_R_X0)) / 6);
        if (DSB(U_EDITFLAG) == 0) {
            edit_line_fill(screen, ebuf, list, wear, sel, false);
            ui_draw_records(erec);
        }
    }
    u16 row0 = DSW(U_ROW0);
    s16 rx0 = rw(row0, UI_R_X0);                  /* [bp-0Ch] */
    s16 rw_ = (s16)(rw(row0, UI_R_X1) - rx0);     /* [bp-10h] */
    s16 rx1 = (s16)(rx0 + rw_ - 1);               /* [bp-32h] */
    s16 ry0 = rw(row0, UI_R_Y0);                  /* [bp-0Eh] */
    s16 rh = (s16)(rw(row0, UI_R_Y1) - ry0);      /* [bp-18h] */
    s16 ry1 = (s16)(rows * rh + ry0 - 1);         /* [bp-3Ch] */
    s16 style = rw(row0, UI_R_STYLE);             /* [bp-14h] */
    u16 sch = (u16)(DSW(DS_g_scheme) + (u16)(style * 5));
    for (u16 i = 0; i < 5; i++) DSB((u16)(DSW(DS_g_scheme) + 0x2D + i)) = DSB((u16)(sch + i));
    s16 hcol = DSC((u16)(sch + 3));               /* [bp-3Ah] highlight colour */

    for (;;) {
        /* 51cd: draw the visible rows */
        set_scheme(style);
        u16 rec = DSW(U_ROW0);                    /* [bp-0Ah] */
        cursor_ctl(-3);
        s16 n = (s16)(count - top);
        if (n > rows) n = rows;
        for (s16 i = 0; n-- != 0; i++, rec = (u16)(rec + UI_REC_SIZE)) {
            s16 v = list[top + 1 + i];
            const char *s = v > -1 ? ds_str((u16)(v + 0x2C02)) : ds_str(extra[-v - 1]);
            s16 x = (s16)((xoff != NULL ? xoff[1 + top + i] : 0) + rw(rec, UI_R_X0));
            drv_draw_text(g_back(), x, rw(rec, UI_R_Y0), s);
            fill_rect(g_back(), (s16)(DSS(DS_g_text_end_x) - 1), DSS(DS_g_text_y),
                      (s16)(rw(rec, UI_R_X1) - DSS(DS_g_text_end_x) + 1), 8);
            if (wear != NULL && wear[1 + top + i] != (s8)0x80) {
                u16 wt = wear_text(wear[1 + top + i]);
                s16 len = (s16)strlen(ds_str(0x07AA));     /* sic: always the length of DS:07AA */
                drv_draw_text(g_back(), (s16)(rw(rec, UI_R_X1) - (s16)(len * 6)), rw(rec, UI_R_Y0), ds_str(wt));
            }
        }
        if (DSB(U_NOFRAME) == 0) {
            u16 r = (u16)(UI_REC_SIZE * sel + DSW(U_ROW0));
            set_scheme(rw(r, UI_R_STYLE));
            draw_box1(g_back(), rw(r, UI_R_X0), rw(r, UI_R_Y0), rw_, 8);
        }
        cursor_ctl(-1);
        if (DSW(DS_g_mirror) != 0)
            page_copy_rect(g_back(), g_front(), (s16)(rx0 - 1), (s16)(ry0 - 1), (s16)(rx1 + 1), (s16)(ry1 + 1));

        s16 code;
        for (;;) {
            /* 53ad */
            set_scheme(style);
            if (DSS(DS_driver_id) == -4) DSB(DS_g_text_fg) = 0;
            for (;;) {                            /* 554d: wait, hover highlight */
                code = ui_wait(0);
                if (code != 0) break;
                bool hov = DSS(U_CODE) > 0 && (s16)(top + DSS(U_CODE)) <= count;
                if (!hov && hl < 0) continue;
                if (hl == DSS(U_CODE) - 1) continue;
                cursor_ctl(-3);
                s16 bx0 = 0x3E8, by0 = 0x3E8, bx1 = -1, by1 = -1;
                s16 nhl;
                if (hov) {
                    nhl = (s16)(DSS(U_CODE) - 1);
                    u16 r = (u16)(nhl * UI_REC_SIZE + DSW(U_ROW0));
                    bx0 = rw(r, UI_R_X0); by0 = rw(r, UI_R_Y0); bx1 = rw(r, UI_R_X1);
                    if (screen != 0x26)
                        drv_recolour_rect(g_back(), DSC(DS_g_text_fg), hcol, bx0, by0, bx1, (s16)(rw(r, UI_R_Y0) + 8));
                    by1 = rw(r, UI_R_Y1);
                } else {
                    nhl = -1;
                }
                if (hl >= 0) {
                    u16 r = (u16)(UI_REC_SIZE * hl + DSW(U_ROW0));
                    if (screen != 0x26)
                        drv_recolour_rect(g_back(), hcol, DSC(DS_g_text_fg), rw(r, UI_R_X0), rw(r, UI_R_Y0),
                                          rw(r, UI_R_X1), (s16)(rw(r, UI_R_Y0) + 8));
                    if (rw(r, UI_R_X0) < bx0) bx0 = rw(r, UI_R_X0);
                    if (rw(r, UI_R_Y0) < by0) by0 = rw(r, UI_R_Y0);
                    if (rw(r, UI_R_X1) > bx1) bx1 = rw(r, UI_R_X1);
                    if (rw(r, UI_R_Y1) > by1) by1 = rw(r, UI_R_Y1);
                }
                cursor_ctl(-1);
                if (DSW(DS_g_mirror) != 0) page_copy_rect(g_back(), g_front(), bx0, by0, bx1, by1);
                hl = nhl;
            }
            if (code <= 0) break;
            /* 5569: a row */
            s16 si = (s16)(top + code);
            if (count < si) continue;
            DSW(U_SEL) = (u16)si;
            s16 idx = (s16)(si - 1);              /* [bp-1Eh] */
            if (erec != 0 && (DSB(U_EDITFLAG) == 0 || idx == sel)) {
                edit_line_fill(screen, ebuf, list, wear, idx, true);
                ui_draw_records(erec);
            }
            if (DSB(U_NOFRAME) == 0) {
                cursor_ctl(-3);
                u16 r = (u16)(UI_REC_SIZE * sel + DSW(U_ROW0));
                set_scheme(style);
                u8 lc = DSB(DS_g_line_col);
                DSB(DS_g_line_col) = DSB(DS_g_text_bg);
                s16 ox0 = rw(r, UI_R_X0), oy0 = rw(r, UI_R_Y0);
                draw_box1(g_back(), ox0, oy0, rw_, 8);
                s16 ox1 = (s16)(ox0 + rw_), oy1 = (s16)(oy0 + 8);
                ox0--; oy0--;
                DSB(DS_g_line_col) = lc;
                r = (u16)(UI_REC_SIZE * idx + DSW(U_ROW0));
                draw_box1(g_back(), rw(r, UI_R_X0), rw(r, UI_R_Y0), rw_, 8);
                cursor_ctl(-1);
                if (DSW(DS_g_mirror) != 0) {
                    s16 y1 = (s16)(rw(r, UI_R_Y0) + rh + 2); if (y1 < oy1) y1 = oy1;
                    s16 x1 = (s16)(rw(r, UI_R_X0) + rw_ + 2); if (x1 < ox1) x1 = ox1;
                    s16 y0 = (s16)(rw(r, UI_R_Y0) - 1); if (y0 > oy0) y0 = oy0;
                    s16 x0 = (s16)(rw(r, UI_R_X0) - 1); if (x0 > ox0) x0 = ox0;
                    page_copy_rect(g_back(), g_front(), x0, y0, x1, y1);
                }
            }
            sel = idx;
        }
        /* 57a3: negative codes */
        s16 old = top;                            /* [bp-1Eh] */
        if (code == -10) {
            if (top + 1 < count) top++;
        } else if (code == -9) {
            top = (s16)(top + rows);
            if (top >= count) top = (s16)(count - 1);
        } else if (code == -8) {
            top = (s16)(top - rows);
            if (top < 0) top = 0;
            continue;
        } else if (code == -7) {
            if (top != 0) top--;
            continue;
        } else if (code == -6) {
            if (screen == 5 && strcmp(ds_str(0x4607), ebuf) == 0) memcpy(ebuf, ds_str(0x07C4), 2);
            set_scheme(rw(erec, UI_R_STYLE));
            ui_push(0x18);
            text_input(rw(erec, UI_R_X0), rw(erec, UI_R_Y0), ebuf, maxlen);
            ui_pop(1);
            continue;
        } else {
            ui_pop(1);
            return code;
        }
        /* 58a7: -9 / -10 clear the rows below the end of the list */
        if (old < top) {
            s16 filled = (s16)(count - top);      /* [bp-26h] */
            if (filled < rows) {
                u16 r = (u16)(UI_REC_SIZE * filled + DSW(U_ROW0));
                s16 k = (s16)(top - old);
                if (k > rows - filled) k = (s16)(rows - filled);
                fill_rect(g_back(), rw(r, UI_R_X0), rw(r, UI_R_Y0), rw_, (s16)(k * rh));
            }
        }
    }
}

/* 0f38:590b */
s16 list_selected(void)
{
    return DSS(U_SEL);
}

/* Caret: 1-px vline of 8 rows in the line colour. */
static void caret(s16 x, s16 y) { vline(g_back(), x, y, 8); }

/* 0f38:5910: line editor at (x, y) in the current colours (fg text, bg field). Returns 13 on Enter
 * or the negative code that ended it. DS:09F2 = digits only (Enter on an empty field clicks). */
s16 text_input(s16 x, s16 y, char *buf, s16 maxlen)
{
    u8 save_fill = DSB(DS_g_fill_col), save_line = DSB(DS_g_line_col);
    s16 wpx = (s16)(maxlen * 6);                  /* [bp-0Ah] */
    s16 dw = (s16)desc_w(g_back());
    if ((s16)(wpx + x) >= dw) {
        maxlen = (s16)((s16)(dw - x - 1) / 6);
        buf[maxlen] = 0;
        wpx = (s16)(maxlen * 6);
    }
    DSB(DS_g_fill_col) = DSB(DS_g_text_bg);
    DSB(DS_g_line_col) = DSB(DS_g_text_fg);
    cursor_ctl(-3);
    fill_rect(g_back(), x, y, wpx, 8);
    drv_draw_text(g_back(), x, y, buf);
    s16 cy = DSS(DS_g_text_y);                    /* [bp-1Ah] */
    s16 cx = DSS(DS_g_text_end_x);                /* [bp-18h] */
    caret(cx, cy);
    u8 blink = 0xFF;                              /* [bp-4] */
    cursor_ctl(-1);
    if (DSW(DS_g_mirror) != 0) page_copy_rect(g_back(), g_front(), x, y, (s16)(x + wpx), (s16)(y + 8));
    u8 save_290c = DSB(0x290C);                   /* "Click here to edit" */
    DSB(0x290C) = 0;
    click_clear();
    s16 saved_screen = kbd_remap_restore(1);      /* [bp-16h] */
    kbd_text_mode_on(DSC(NUMERIC));
    u16 save_any = DSW(DS_wait_any_key);
    DSW(DS_wait_any_key) = 1;
    DSW(DS_wait_key) = 0;
    char one[2] = { 0, 0 };                       /* [bp-2] */
    s16 len = (s16)strlen(buf);                   /* [bp-0Ch] */
    s16 lastx = cx;                               /* [bp-0Eh] PORT: uninitialised in the original */
    s16 r;                                        /* [bp-12h] */
    for (;;) {
        r = ui_wait(0x23);
        if (r == 0) {                             /* 5caf: blink */
            cursor_ctl(-3);
            DSB(DS_g_line_col) = blink ? DSB(DS_g_text_bg) : DSB(DS_g_text_fg);
            caret(cx, cy);
            blink = (u8)~blink;
            cursor_ctl(-1);
            if (DSW(DS_g_mirror) != 0) page_copy_rect(g_back(), g_front(), cx, cy, (s16)(cx + 1), (s16)(cy + 8));
            DSW(DS_wait_key) = 0;
            continue;
        }
        if (r < 0) break;
        r = (s16)DSW(DS_wait_key);
        if (r == 0x0D) {
            if (DSB(NUMERIC) != 0 && buf[0] == 0) { fx_click(); DSW(DS_wait_key) = 0; continue; }
            break;
        }
        if (r == 0) { DSW(DS_wait_key) = 0; continue; }
        if (r != 8) {
            if (r < 0x20 || r > 0x5A) { DSW(DS_wait_key) = 0; continue; }
            if (DSB(NUMERIC) != 0 && (r < 0x30 || r > 0x39)) { fx_click(); DSW(DS_wait_key) = 0; continue; }
        }
        cursor_ctl(-3);
        DSB(DS_g_line_col) = DSB(DS_g_text_bg);
        caret(cx, cy);
        if (r == 8) {
            if (len > 0) {
                len--;
                one[0] = buf[len];
                cx = (s16)(cx - font_string_width(one));
                lastx = cx;
                drv_draw_text(g_back(), cx, cy, ds_str(0x09F0));
            } else {
                fx_click();
            }
        } else if (len < maxlen) {
            buf[len++] = (char)r;
            buf[len] = 0;
            lastx = cx;
            drv_draw_text(g_back(), cx, cy, &buf[len - 1]);
            cx = DSS(DS_g_text_end_x);
        } else {
            fx_click();
        }
        DSB(DS_g_line_col) = DSB(DS_g_text_fg);   /* 5c1a */
        caret(cx, cy);
        blink = 0xFF;
        cursor_ctl(-1);
        if (DSW(DS_g_mirror) != 0) page_copy_rect(g_back(), g_front(), lastx, cy, (s16)(lastx + 6), (s16)(cy + 8));
        DSW(DS_wait_key) = 0;
    }
    /* 5ab5 */
    buf[len] = 0;
    cursor_ctl(-3);
    DSB(DS_g_line_col) = DSB(DS_g_text_bg);
    caret(cx, cy);
    cursor_ctl(-1);
    if (DSW(DS_g_mirror) != 0) page_copy_rect(g_back(), g_front(), (s16)(cx - 6), y, cx, (s16)(y + 8));
    DSB(DS_g_line_col) = save_line;
    DSB(DS_g_fill_col) = save_fill;
    DSB(0x290C) = save_290c;
    click_clear();
    DSW(DS_wait_any_key) = save_any;
    kbd_text_mode_off();
    if (saved_screen != 0) kbd_remap_screen(saved_screen);
    return r;
}

/* 0f38:5d37 (no callers): label, then a 20-digit numeric field after it. */
s16 prompt_number(s16 x, s16 y, const char *label, u16 *v)
{
    char buf[20];
    u16 sch = DSW(DS_g_scheme);
    DSW(DS_g_text_fg) = DSW((u16)(sch + 0x19));
    DSW(DS_g_fill_col) = DSW((u16)(sch + 0x1B));
    DSB(DS_g_frame_style) = DSB((u16)(sch + 0x1D));
    screen_text(x, y, label);
    DSB(NUMERIC) = 1;
    buf[0] = 0;
    s16 r = text_input(DSS(DS_g_text_end_x), DSS(DS_g_text_y), buf, 0x14);
    DSB(NUMERIC) = 0;
    *v = crt_atoi(buf);
    if (r > 0) r = 0;
    return r;
}

/* 0f38:5da7 */
s16 edit_number(u16 *v, char *buf)
{
    u16 erec = DSW(U_EDITREC);
    set_scheme(rw(erec, UI_R_STYLE));
    DSB(NUMERIC) = 1;
    s16 n = (s16)((s16)(rw(erec, UI_R_X1) - rw(erec, UI_R_X0)) / 6);
    if (n > 10) n = 10;
    s16 r = text_input(rw(erec, UI_R_X0), rw(erec, UI_R_Y0), buf, n);
    DSB(NUMERIC) = 0;
    *v = crt_atoi(buf);
    if (r > 0) r = 0;
    return r;
}

/* 0f38:5e1e (no callers) */
s16 ui_edit_number(s16 screen, u16 *v)
{
    char buf[12];
    ui_push(screen);
    buf[0] = 0;
    s16 r = edit_number(v, buf);
    ui_pop(1);
    return r;
}

/* 0f38:5e54 */
s16 ui_edit(s16 screen, s16 maxlen, char *buf)
{
    ui_push(screen);
    u16 erec = DSW(U_EDITREC);
    set_scheme(rw(erec, UI_R_STYLE));
    s16 r = text_input(rw(erec, UI_R_X0), rw(erec, UI_R_Y0), buf, maxlen);
    ui_pop(1);
    if (r > 0) r = 0;
    return r;
}
