/* The 0f38 UI toolkit: hot-spot screens ("menus"), 0f38:4265 - 4f14, and hotspots_reset 0000:0d58.
 * Ported from the disassembly (video.md §4.9 lists these without pseudocode; the decompile of
 * 0f38:465e is unusable). See game/ui.h for the record format and the globals. */
#include "game/ui.h"

#include <string.h>

#include "platform/platform.h"
#include "platform/video.h"

/* UI globals (DGROUP) */
#define U_DEPTH     0x8B8E      /* stack depth */
#define U_STACK     0x8B86      /* u16[4]: W[bx - 0x747A] */
#define U_TOP       0x8EFE
#define U_FIRST     0x8BA6      /* first hot-spot record (DS offset) or 0 */
#define U_FOUND     0x8BA8
#define U_CODE      0x8BAA
#define U_RX0       0x8B90
#define U_RX1       0x8B92
#define U_RY0       0x8B94
#define U_RY1       0x8B96
#define U_NOFRAME   0x8B98      /* u8 */
#define U_ROW0      0x8B9A
#define U_NROWS     0x8B9C
#define U_EDITREC   0x8BA2
#define U_EDITFLAG  0x8BA4      /* u8 */
#define U_HDRLABEL  0x8BAC
#define U_CHAIN     0x07A8      /* FFFFh while ui_push draws a whole chain */
#define U_BX0       0x6C56      /* bounding box of the drawn records */
#define U_BY0       0x6C58
#define U_BX1       0x6C5A
#define U_BY1       0x6C5C

static inline s16 rw(u16 rec, u16 off) { return DSS((u16)(rec + off)); }

/* The 5 colour bytes DS:8242..8246 of scheme entry `style` (movsw movsw movsb). */
static void set_scheme(s16 style)
{
    u16 src = (u16)(DSW(DS_g_scheme) + (u16)(style * 5));
    for (u16 i = 0; i < 5; i++) DSB((u16)(DS_g_text_fg + i)) = DSB((u16)(src + i));
}

static void mirror_rect(s16 x0, s16 y0, s16 x1, s16 y1)
{
    page_copy_rect(g_back(), g_front(), x0, y0, x1, y1);
}

/* 0f38:4265: sorts n records from `first` by x0 (swap of 18-byte records). */
void ui_sort_records(s16 first, s16 n)
{
    u8 tmp[UI_REC_SIZE];
    if (n <= 1) return;
    s16 i = 0;
    do {
        u16 a = UI_REC(first + i);
        for (s16 j = i + 1; j < n; j++) {
            u16 b = UI_REC(first + j);
            if (rw(b, UI_R_X0) < rw(a, UI_R_X0)) {
                memcpy(tmp, mp(DGROUP, a), UI_REC_SIZE);
                memcpy(mp(DGROUP, a), mp(DGROUP, b), UI_REC_SIZE);
                memcpy(mp(DGROUP, b), tmp, UI_REC_SIZE);
            }
        }
        i++;
    } while (i < n - 1);
}

/* 0f38:465e: draws record `rec`; while DS:07A8 = FFFFh (ui_push) it follows the +10 chain and
 * neither hides the pointer nor mirrors (the caller does). Types 4/12 framed box with a title,
 * 5 text, 6 centred text, 7 (DS:8BA4 = 1) and 8 edit line, 10 button, 13 list row; the others are
 * only linked. The bounding box DS:6C56.. collects every drawn record. */
void ui_draw_records(u16 rec)
{
    s16 cur_style = 0;                                    /* [bp-2] */
    if (DSW(U_CHAIN) == 0 && DSW(DS_g_mirror) != 0) cursor_ctl(-3);
    for (;;) {
        s16 type = rw(rec, UI_R_TYPE);
        if ((u16)(type - 1) > 0x0D) break;
        s16 w, h;
        u16 lab;
        bool bounds = true;
        switch (type) {
        case 4: case 12:                                  /* 469d */
            if (rw(rec, UI_R_STYLE) <= 0) break;
            if (rw(rec, UI_R_STYLE) != cur_style) { cur_style = rw(rec, UI_R_STYLE); set_scheme(cur_style); }
            h = (s16)(rw(rec, UI_R_Y1) - rw(rec, UI_R_Y0));
            w = (s16)(rw(rec, UI_R_X1) - rw(rec, UI_R_X0));
            fill_rect(g_back(), rw(rec, UI_R_X0), rw(rec, UI_R_Y0), w, h);
            if (DSB(DS_g_frame_style) != 0) draw_frame3(g_back(), rw(rec, UI_R_X0), rw(rec, UI_R_Y0), w, h);
            if (rw(rec, UI_R_LABEL) != 0) {
                lab = (u16)(rw(rec, UI_R_LABEL) + 0x239E);
                s16 tw = font_string_width(ds_str(lab));
                drv_draw_text(g_back(), (s16)(((s16)(w - tw) >> 1) + rw(rec, UI_R_X0)), rw(rec, UI_R_Y0), ds_str(lab));
                hline(g_back(), rw(rec, UI_R_X0), (s16)(rw(rec, UI_R_Y0) + 9), w);
            }
            break;
        case 5:                                           /* 4895 */
            if (rw(rec, UI_R_STYLE) != cur_style) { cur_style = rw(rec, UI_R_STYLE); set_scheme(cur_style); }
            lab = (u16)(rw(rec, UI_R_LABEL) + 0x239E);
            drv_draw_text(g_back(), rw(rec, UI_R_X0), rw(rec, UI_R_Y0), ds_str(lab));
            break;
        case 6: {                                         /* 48cd */
            if (rw(rec, UI_R_STYLE) != cur_style) { cur_style = rw(rec, UI_R_STYLE); set_scheme(cur_style); }
            lab = (u16)(rw(rec, UI_R_LABEL) + 0x239E);
            s16 y = (s16)(((s16)(rw(rec, UI_R_Y1) - rw(rec, UI_R_Y0) - 8) >> 1) + rw(rec, UI_R_Y0));
            s16 tw = font_string_width(ds_str(lab));
            w = (s16)(rw(rec, UI_R_X1) - rw(rec, UI_R_X0));
            drv_draw_text(g_back(), (s16)(((s16)(w - tw) >> 1) + rw(rec, UI_R_X0)), y, ds_str(lab));
            break;
        }
        case 7:                                           /* 4926 */
            DSB(U_EDITFLAG) = 1;
            /* fall through */
        case 8:                                           /* 492b */
            if (rw(rec, UI_R_STYLE) != cur_style) { cur_style = rw(rec, UI_R_STYLE); set_scheme(cur_style); }
            w = (s16)(rw(rec, UI_R_X1) - rw(rec, UI_R_X0));
            fill_rect(g_back(), rw(rec, UI_R_X0), rw(rec, UI_R_Y0), w, 8);
            lab = (u16)(rw(rec, UI_R_LABEL) + 0x239E);
            if (rw(rec, UI_R_TYPE) == 8) {
                s16 tw = font_string_width(ds_str(lab));
                drv_draw_text(g_back(), (s16)(((s16)(w - tw) >> 1) + rw(rec, UI_R_X0)), rw(rec, UI_R_Y0), ds_str(lab));
            } else {
                drv_draw_text(g_back(), rw(rec, UI_R_X0), rw(rec, UI_R_Y0), ds_str(lab));
            }
            if (DSW(U_EDITREC) == 0) DSW(U_EDITREC) = rec;
            break;
        case 10: {                                        /* 47e0 */
            if (rw(rec, UI_R_STYLE) != cur_style) { cur_style = rw(rec, UI_R_STYLE); set_scheme(cur_style); }
            h = (s16)(rw(rec, UI_R_Y1) - rw(rec, UI_R_Y0));
            w = (s16)(rw(rec, UI_R_X1) - rw(rec, UI_R_X0));
            fill_rect(g_back(), rw(rec, UI_R_X0), rw(rec, UI_R_Y0), w, h);
            draw_frame3(g_back(), rw(rec, UI_R_X0), rw(rec, UI_R_Y0), w, h);
            lab = (u16)(rw(rec, UI_R_LABEL) + 0x239E);
            s16 y = (s16)(((s16)(h - 8) >> 1) + rw(rec, UI_R_Y0));
            s16 tw = font_string_width(ds_str(lab));
            drv_draw_text(g_back(), (s16)(((s16)(w - tw) >> 1) + rw(rec, UI_R_X0)), y, ds_str(lab));
            break;
        }
        case 13:                                          /* 49d1 */
            if (DSW(U_NROWS)++ == 0) DSW(U_ROW0) = rec;
            break;
        default:                                          /* 1 2 3 9 11 14: link only */
            bounds = false;
            break;
        }
        if (bounds) {                                     /* 477b */
            s16 v;
            v = rw(rec, UI_R_X0); if (v > DSS(U_BX0)) v = DSS(U_BX0); DSS(U_BX0) = v;
            v = rw(rec, UI_R_X1); if (v < DSS(U_BX1)) v = DSS(U_BX1); DSS(U_BX1) = v;
            v = rw(rec, UI_R_Y0); if (v > DSS(U_BY0)) v = DSS(U_BY0); DSS(U_BY0) = v;
            v = rw(rec, UI_R_Y1); if (v < DSS(U_BY1)) v = DSS(U_BY1); DSS(U_BY1) = v;
        }
        s16 next = (s16)(rw(rec, UI_R_NEXT) & DSS(U_CHAIN));   /* 47ba */
        if (next == 0) break;
        rec = UI_REC(next);
    }
    if (DSW(U_CHAIN) == 0 && DSW(DS_g_mirror) != 0) {    /* 4a0f */
        cursor_ctl(-1);
        mirror_rect((s16)(rw(rec, UI_R_X0) - 3), (s16)(rw(rec, UI_R_Y0) - 3),
                    (s16)(rw(rec, UI_R_X1) + 6), (s16)(rw(rec, UI_R_Y1) + 6));
    }
}

/* 0f38:4a66 */
void ui_push(s16 screen)
{
    if (DSS(U_DEPTH) >= 4) return;
    cursor_ctl(-3);
    s16 d = DSS(U_DEPTH);
    DSW(U_DEPTH) = (u16)(d + 1);
    DSW(U_TOP) = (u16)screen;
    DSW((u16)(U_STACK + 2 * d)) = (u16)screen;
    DSW(U_RY1) = DSW(U_RY0) = DSW(U_RX1) = DSW(U_RX0) = 0xFFFF;
    u16 hdr = ui_screen_rec(screen);
    DSW(U_HDRLABEL) = DSW((u16)(hdr + UI_R_LABEL));
    DSW(U_FIRST) = rw((u16)(hdr + UI_REC_SIZE), UI_R_TYPE) > 9 ? (u16)(hdr + UI_REC_SIZE) : 0;
    DSW(U_EDITREC) = 0;
    DSW(U_ROW0) = 0;
    DSW(U_NROWS) = 0;
    DSB(U_EDITFLAG) = 0;
    DSB(U_NOFRAME) = rw(hdr, UI_R_TYPE) == 3 ? 1 : 0;
    u8 save[5];
    memcpy(save, mp(DGROUP, DS_g_text_fg), 5);
    DSW(U_CHAIN) = 0xFFFF;
    DSW(U_BY0) = DSW(U_BX0) = 0x3E8;
    DSW(U_BY1) = DSW(U_BX1) = 0xFFFF;
    if (rw(hdr, UI_R_NEXT) != 0) ui_draw_records(UI_REC(rw(hdr, UI_R_NEXT)));
    DSW(U_CHAIN) = 0;
    memcpy(mp(DGROUP, DS_g_text_fg), save, 5);
    cursor_ctl(-1);
    if (DSW(DS_g_mirror) != 0 && DSS(U_BX1) >= 0)
        mirror_rect((s16)(DSS(U_BX0) - 3), (s16)(DSS(U_BY0) - 3), (s16)(DSS(U_BX1) + 6), (s16)(DSS(U_BY1) + 6));
    kbd_remap_screen(screen);
    click_clear();
}

/* 0f38:4ba1 */
void ui_pop(s16 redraw)
{
    if (DSW(U_DEPTH) == 0) return;
    kbd_remap_restore(0);
    DSW(U_FIRST) = 0;
    if (DSS(U_DEPTH) - 1 == 0 || redraw == 0) {
        DSW(U_TOP) = 0;
        DSW(U_FIRST) = 0;
        DSW(U_CODE) = 0xFFFF;
        DSW(U_RY1) = DSW(U_RX1) = DSW(U_RY0) = DSW(U_RX0) = 0xFFFF;
        DSW(U_DEPTH) = (u16)(DSS(U_DEPTH) - 1);
    } else {
        DSW(U_DEPTH) = (u16)(DSS(U_DEPTH) - 2);
        ui_push(DSS((u16)(U_STACK + 2 * DSS(U_DEPTH))));
    }
}

/* 0f38:4bff */
void ui_state_reset(void)
{
    DSW(U_HDRLABEL) = 0;
    DSW(U_DEPTH) = 0;
    DSW(U_FIRST) = 0;
    DSW(U_CODE) = 0;
    DSW(U_RY1) = DSW(U_RX1) = DSW(U_RY0) = DSW(U_RX0) = 0;
}

/* 0f38:4c1a: bounding box of the records chained from the screen header, 3 px margin, clipped to
 * the screen; *w / *h are the size. */
void ui_bounds(s16 screen, s16 *x, s16 *y, s16 *w, s16 *h)
{
    u16 rec = ui_screen_rec(screen);
    DSW(U_BX0) = 0x3E8; DSW(U_BX1) = 1; DSW(U_BY0) = 0x3E8; DSW(U_BY1) = 1;
    while (rw(rec, UI_R_NEXT) != 0) {
        rec = UI_REC(rw(rec, UI_R_NEXT));
        s16 v;
        v = rw(rec, UI_R_X0); if (DSS(U_BX0) < v) v = DSS(U_BX0); DSS(U_BX0) = v;
        v = rw(rec, UI_R_X1); if (v < DSS(U_BX1)) v = DSS(U_BX1); DSS(U_BX1) = v;
        v = rw(rec, UI_R_Y0); if (DSS(U_BY0) < v) v = DSS(U_BY0); DSS(U_BY0) = v;
        v = rw(rec, UI_R_Y1); if (v < DSS(U_BY1)) v = DSS(U_BY1); DSS(U_BY1) = v;
    }
    *x = (s16)(DSS(U_BX0) - 3); if (*x < 0) *x = 0;
    *y = (s16)(DSS(U_BY0) - 3); if (*y < 0) *y = 0;
    *w = (s16)(DSS(U_BX1) + 3); if (*w > 0x140) *w = 0x140;
    *h = (s16)(DSS(U_BY1) + 3); if (*h > 200) *h = 200;
    *w = (s16)(*w - *x);
    *h = (s16)(*h - *y);
}

/* 0f38:4d10 */
FarPtr ui_save_bg(s16 screen, s16 *x, s16 *y, s16 pool)
{
    s16 w, h;
    ui_bounds(screen, x, y, &w, &h);
    s16 x0 = *x, y0 = *y;
    u16 m = DSW(DS_g_x_align);
    s16 xe = (s16)((u16)(x0 + w + (u16)~m) & m);
    if (xe > 0x140) xe = 0x140;
    s16 ye = (s16)(y0 + h);
    if (ye > 200) ye = 200;
    s16 xa = (s16)(x0 & m);
    FarPtr b = arena_bitmap_alloc((s16)(xe - xa), (s16)(ye - y0), 0, pool);
    return screen_save_rect(x0, y0, w, h, b);
}

/* 0f38:4dad (see game/ui.h): walks the hot-spot records (sorted by x0) and narrows the rectangle
 * DS:8B90..96 in which the answer cannot change (ui_wait skips the search inside it). */
s16 hotspot_at(s16 x, s16 y)
{
    u16 si = DSW(U_FIRST);
    if (si == 0) {
        DSW(U_RY0) = DSW(U_RX0) = 0xFFFF;
        DSW(U_RY1) = DSW(U_RX1) = 0x3E8;
        DSW(U_CODE) = 0;
        return 0;
    }
    DSW(U_RY0) = DSW(U_RX0) = 0;
    DSW(U_RX1) = 0x13F;
    DSW(U_RY1) = 0xC7;
    for (;;) {
        s16 cx = rw(si, UI_R_TYPE);
        if (cx <= 9) break;
        if (cx == 0x0E) { si = (u16)(si + UI_REC_SIZE); continue; }
        if (rw(si, UI_R_X0) > x) { DSW(U_RX1) = (u16)rw(si, UI_R_X0); break; }
        if (rw(si, UI_R_X1) > x) {
            if ((s16)(rw(si, UI_R_Y0) - 3) > y) {
                if ((s16)(rw(si, UI_R_Y0) - 3) < DSS(U_RY1)) DSW(U_RY1) = (u16)(rw(si, UI_R_Y0) - 3);
            } else if (rw(si, UI_R_Y1) > y) {
                if (cx == 0x0C) { si = (u16)(si + UI_REC_SIZE); continue; }
                DSW(U_RX0) = (u16)rw(si, UI_R_X0);
                DSW(U_RX1) = (u16)rw(si, UI_R_X1);
                DSW(U_RY0) = (u16)rw(si, UI_R_Y0);
                DSW(U_RY1) = (u16)rw(si, UI_R_Y1);
                DSW(U_FOUND) = si;
                DSW(U_CODE) = (u16)rw(si, UI_R_CODE);
                return rw(si, UI_R_CODE);
            } else {
                if (rw(si, UI_R_Y1) > DSS(U_RY0)) DSW(U_RY0) = (u16)rw(si, UI_R_Y1);
            }
        } else {
            if (rw(si, UI_R_X1) > DSS(U_RX0)) DSW(U_RX0) = (u16)rw(si, UI_R_X1);
        }
        if (cx == 0x0C) si = UI_REC(rw(si, UI_R_CODE));
        else si = (u16)(si + UI_REC_SIZE);
    }
    DSW(U_FOUND) = 0;
    DSW(U_CODE) = 0;
    return 0;
}

/* 0f38:4ec1 */
s16 ui_dialog(s16 screen)
{
    s16 x, y;
    FarPtr b = ui_save_bg(screen, &x, &y, 2);
    s16 r = ui_menu(screen);
    screen_put_bitmap_mirror(b, x, y);
    arena_pop(1);
    return r;
}

/* 0f38:4f14 */
s16 ui_menu(s16 screen)
{
    s16 r;
    ui_push(screen);
    do r = ui_wait(32000); while (r == 0);
    ui_pop(1);
    return r;
}

/* 0000:0d58 (platform range; PORT: not provided by platform.h, so it lives here) */
void hotspots_reset(void)
{
    arena_reset();
    DSW(0x051A) = 0;
    DSW(0x051E) = 0;
    DSW(0x051C) = 0;
    ui_pop(1);
}
