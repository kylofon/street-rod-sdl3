/* Race: the cockpit dashboard in segment 0f38 (port/spec/race.md §4.16, read from the disassembly):
 * needles (speedometer / tachometer), the clock of automatic cars, the steering wheel and its knob,
 * the shifter gate and knob, the gear letter on the status line.
 *
 *   0f38:000a dash_restore_rect   0f38:0098 dash_save_rect     0f38:0137 speedo_points
 *   0f38:01d2 speedo_init         0f38:02ae tach_points        0f38:0349 tach_init
 *   0f38:0425 needle_update       0f38:05f2 clock_draw         0f38:0f53 wheel_load
 *   0f38:1094 wheel_knob_crash    0f38:1130 wheel_draw         0f38:12f7 shifter_draw
 *   0f38:14f2 shifter_knob        0f38:22e9 gear_label
 *
 * VGA path only (DS:8236 == -2, DS:58E4 == 0): the dashboard is page 0 (DS:7678, A000), whose rows
 * >= 100 keep clean copies of the needle areas (x 108h, rows 100 + 5 / 0Ch / 23h); the shifter keeps
 * its gate / knob cut-outs in page 2 (DS:82B4). The CGA / Hercules / Tandy branches are parked. */
#include "game/race_int.h"

#include <math.h>
#include <string.h>

#include "game/flow.h"
#include "platform/platform.h"
#include "platform/video.h"

static long double dconst(u16 off)
{
    double d;
    memcpy(&d, mp(DGROUP, off), 8);
    return d;
}

static bool vga(void) { return DSS(DS_driver_id) == -2; }

/* 0f38:000a: copy the clean copy of a needle area back (erase). kind 1 knob, 2 speedo, 3 tach. */
static void dash_restore_rect(s16 x, s16 y, s16 w, s16 h, s16 kind)
{
    s16 ky = (kind == 1 ? 5 : kind == 2 ? 0xC : 0x23) + (vga() ? 0x64 : 0);
    Rect r = { (s16)((w & ~7) + 0x10), h, 0x108, ky, (s16)(x & ~7), y };
    FarPtr src = vga() ? ds_far(DS_page0_ptr) : ds_far(DS_page2_ptr);
    drv_blit(src, ds_far(DS_page0_ptr), &r, 1);
}

/* 0f38:0098: save a needle area of the dashboard into the clean-copy store. */
static void dash_save_rect(s16 x, s16 y, s16 w, s16 h, s16 kind)
{
    s16 ky = (kind == 1 ? 5 : kind == 2 ? 0xC : 0x23) + (vga() ? 0x64 : 0);
    Rect r = { (s16)((w & ~7) + 0x10), h, (s16)(x & ~7), y, 0x108, ky };
    FarPtr dst = vga() ? ds_far(DS_page0_ptr) : ds_far(DS_page2_ptr);
    drv_blit(ds_far(DS_page0_ptr), dst, &r, 1);
}

/* 0f38:0137 speedo_points / 0f38:02ae tach_points: needle ends from the far tables at the segment in
 * DS:68E4 (2e47:0000 tip, 2e47:0106 tail; {i8 x, i8 y} per value 0..82h). */
static void needle_points(s16 v, s16 cx, s16 *x0, s16 *y0, s16 *x1, s16 *y1)
{
    if (v < 0) v = 0;
    else if (v > 0x82) v = 0x82;
    u16 seg = DSW(0x68E4);
    u16 si = (u16)(v * 2);
    s16 yb = vga() ? 0 : 0x64;
    *x0 = (s16)((s8)rd8(seg, si) + cx);
    *y0 = (s16)(yb - (s8)rd8(seg, (u16)(si + 1)) + 0x2A);
    *x1 = (s16)((s8)rd8(seg, (u16)(si + 0x106)) + cx);
    *y1 = (s16)(yb - (s8)rd8(seg, (u16)(si + 0x107)) + 0x2A);
}

static void dash_line(s16 x0, s16 y0, s16 x1, s16 y1, s16 c)
{
    u16 s7a = DSW(DS_g_draw_seg), s7c = DSW(0x767C);
    DSW(DS_g_draw_seg) = 0xA000;                                  /* VGA (B000 Hercules, B800 CGA) */
    DSW(0x767C) = 0;
    drv_line(x0, y0, x1, y1, c);
    DSW(DS_g_draw_seg) = s7a;
    DSW(0x767C) = s7c;
}

static void needle_init(s16 v, s16 cx, s16 kind, s16 colour)
{
    s16 x0, y0, x1, y1;
    needle_points(v, cx, &x0, &y0, &x1, &y1);
    s16 l = x0 > x1 ? x1 : x0, t = y0 > y1 ? y1 : y0;
    dash_save_rect(l, t, (s16)(abs16((s16)(x0 - x1)) + 1), (s16)(abs16((s16)(y0 - y1)) + 1), kind);
    dash_line(x0, y0, x1, y1, colour);
}

/* 0f38:01d2 speedo_init */
void speedo_init(s16 mph)
{
    s16 c = (DSS(DS_driver_id) == -2 || DSS(DS_driver_id) == -6) ? 0xB : 3;
    needle_init(mph, 0xC4, 2, c);
}

/* 0f38:0349 tach_init */
void tach_init(s16 rpm)
{
    s16 c = DSS(DS_driver_id) == -2 ? 0xB : 3;
    needle_init((s16)(rpm / 0x2B), 0x91, 3, c);
}

/* 0f38:0425 needle_update: erase the old needle, save the new area, draw the new needle */
s16 needle_update(s16 nv, s16 old, s16 tach)
{
    if (DSS(DS_needle_colour) < 0)
        DSS(DS_needle_colour) = (DSS(DS_driver_id) == -2 || DSS(DS_driver_id) == -6) ? 0xB : 3;
    u16 s7a = DSW(DS_g_draw_seg), s7c = DSW(0x767C);
    DSW(DS_g_draw_seg) = 0xA000;
    DSW(0x767C) = 0;
    s16 x0, y0, x1, y1, kind = tach ? 3 : 2, cx = tach ? 0x91 : 0xC4;
    needle_points(tach ? (s16)(old / 0x2B) : old, cx, &x0, &y0, &x1, &y1);
    dash_restore_rect(x0 > x1 ? x1 : x0, y0 > y1 ? y1 : y0,
                      (s16)(abs16((s16)(x0 - x1)) + 1), (s16)(abs16((s16)(y0 - y1)) + 1), kind);
    needle_points(tach ? (s16)(nv / 0x2B) : nv, cx, &x0, &y0, &x1, &y1);
    dash_save_rect(x0 > x1 ? x1 : x0, y0 > y1 ? y1 : y0,
                   (s16)(abs16((s16)(x0 - x1)) + 1), (s16)(abs16((s16)(y0 - y1)) + 1), kind);
    drv_line(x0, y0, x1, y1, DSS(DS_needle_colour));
    DSW(DS_g_draw_seg) = s7a;
    DSW(0x767C) = s7c;
    return nv;
}

/* ---- the clock (0f38:05f2). PORT: sin / cos of the MSC runtime (1e16:3310 / 3316) are libm's
 * sinl / cosl in extended precision. */
static void clock_pt(long double c, long double s, u16 len, s16 *x, s16 *y)
{
    *x = (s16)((s16)ftol(c * dconst(len)) + 0x91);
    *y = (s16)((vga() ? 0 : 0x64) - (s16)ftol(s * dconst(len)) + 0x2A);
}

static void clock_hand(s16 a, s16 delta, u16 tip)
{
    long double pi = (double)dconst(0x68EA);                       /* fstp qword [bp-1Eh] */
    long double ang = (long double)a * pi / dconst(0x68F2);
    double angd = (double)ang;                                     /* fst qword [bp-12h] */
    long double c = cosl(ang), s = sinl((long double)angd);
    s16 bx, by, tx, ty, px, py, mx, my;
    clock_pt(c, s, 0x68FA, &bx, &by);                             /* base, length 5 */
    clock_pt(c, s, tip, &tx, &ty);                                /* tip 13 / 18 */
    long double ap = (long double)(s16)(a + delta) * pi / dconst(0x68F2);
    clock_pt(cosl(ap), sinl(ap), 0x690A, &px, &py);               /* sides, length 9 */
    long double am = (long double)(s16)(a - delta) * pi / dconst(0x68F2);
    clock_pt(cosl(am), sinl(am), 0x690A, &mx, &my);
    s16 col = DSS(DS_needle_colour);
    drv_line(bx, by, tx, ty, col);
    drv_line(bx, by, px, py, col);
    drv_line(bx, by, mx, my, col);
    drv_line(tx, ty, px, py, col);
    drv_line(tx, ty, mx, my, col);
}

/* 0f38:05f2 clock_draw: the dash clock of automatic cars (face LIB2 #240) */
void clock_draw(void)
{
    if (DSS(DS_needle_colour) < 0)
        DSS(DS_needle_colour) = (DSS(DS_driver_id) == -2 || DSS(DS_driver_id) == -6) ? 0xB : 3;
    u16 s7a = DSW(DS_g_draw_seg), s7c = DSW(0x767C);
    DSW(DS_g_draw_seg) = 0xA000;
    DSW(0x767C) = 0;
    s16 hh, mm;
    clock_hm(&hh, &mm);                                            /* 0000:64a3 */
    s16 h = (s16)((s16)((hh >> 1) + 0x14) % 12 + 1);
    s16 a = (s16)(0x5A - (s16)(30 * h + (mm >> 1)));
    s16 t = abs16((s16)(a - (s16)(a / 0x5A) * 0x5A));
    clock_hand(a, (t < 10 || t > 0x50) ? 8 : 5, 0x6902);
    a = (s16)(0x5A - (s16)(6 * mm));
    t = abs16((s16)(a - (s16)(a / 0x5A) * 0x5A));
    clock_hand(a, (t < 10 || t > 0x50) ? 8 : 4, 0x6912);
    DSW(DS_g_draw_seg) = s7a;
    DSW(0x767C) = s7c;
}

/* ---- steering wheel */

/* 0f38:0f53 wheel_load: the three wheel pictures DS:58E8 -> far pointers DS:6C28[3] */
void wheel_load(void)
{
    for (int i = 0; i < 3; i++)                                    /* VGA branch */
        ds_far_wr((u16)(0x6C28 + 4 * i), pic_get(DSS((u16)(DS_wheel_pics + 2 * i)), 1));
}

/* knob position of wheel angle w: bytes DS:031E + 2w (w = -24..24) */
static s16 knob_x(s16 w) { return DSB((u16)(DS_wheel_knob_xy + 2 * w)); }
static s16 knob_y(s16 w) { return DSB((u16)(DS_wheel_knob_xy + 2 * w + 1)); }

/* 0f38:1094 wheel_knob_crash: the knob on the crash screen (page A200, rows 100..190) */
void wheel_knob_crash(s16 pos)
{
    u16 s7a = DSW(DS_g_draw_seg), s7c = DSW(0x767C);
    DSW(DS_g_draw_seg) = 0xA200;
    DSW(0x767C) = 0;
    clip_set_ymin0();
    s16 x = knob_x(pos), y = (s16)(knob_y(pos) + 0x64);
    u16 ymax = DSW(DS_g_span_ymax), ymin = DSW(DS_g_span_ymin);
    DSW(DS_g_span_ymax) = 0xBE;
    DSW(DS_g_span_ymin) = 0x64;
    shape_draw_dot(x, y);
    DSW(DS_g_span_ymax) = ymax;
    DSW(DS_g_span_ymin) = ymin;
    clip_set_view();
    DSW(DS_g_draw_seg) = s7a;
    DSW(0x767C) = s7c;
}

/* 0f38:1130 wheel_draw(new, old, dst): wheel picture when its index changes, then the knob */
void wheel_draw(s16 nw, s16 old, FarPtr dst)
{
    s16 idx = (s16)((nw != 0) + (nw > 0));
    if (idx < 3 && idx != DSS(0x0350)) {
        Rect r = { 0xB8, 0x1A, 0, 0, 0x50, (s16)((vga() ? 0 : 0x64) + 0x3F) };
        drv_blit(ds_far((u16)(0x6C28 + 4 * idx)), dst, &r, 1);
        DSS(0x0350) = idx;
    }
    if (old != 100) dash_restore_rect((s16)(knob_x(old) - 2), (s16)(knob_y(old) - 2), 5, 5, 1);
    s16 x = knob_x(nw), y = knob_y(nw);
    dash_save_rect((s16)(x - 2), (s16)(y - 2), 5, 5, 1);
    u16 s7a = DSW(DS_g_draw_seg), s7c = DSW(0x767C);
    DSW(DS_g_draw_seg) = 0xA000;
    DSW(0x767C) = 0;
    clip_set_ymin0();
    shape_draw_dot(x, y);
    clip_set_view();
    DSW(DS_g_draw_seg) = s7a;
    DSW(0x767C) = s7c;
}

/* ---- shifter: record DS:036E + 10*i {n positions, gate pic, knob pic, knob height, near* positions
 * (i8 x, i8 y pairs)}, cut-out store positions DS:038C {dx, dy} per position in page 2 */

/* 0f38:12f7 shifter_draw(automatic, ngears) */
void shifter_draw(s16 automatic, s16 ngears)
{
    u16 rec = (u16)(DS_shifter_tab + 10 * (automatic != 0 ? 0 : ngears == 3 ? 1 : 2));
    DSW(0x6C34) = rec;
    FarPtr gate = pic_get(DSS(rec + 2), 2);
    Rect r = { 0x30, 0x2B, 0, 0, 0x108, 0x17 };                  /* VGA: +0 */
    drv_blit(gate, ds_far(DS_page0_ptr), &r, 1);
    FarPtr knob = pic_get(DSS(rec + 4), 2);
    s16 kw = (s16)desc_w(knob), kh = (s16)desc_h(knob);
    Rect r2 = { kw, kh, 0, 0, 0, 0 };
    Rect r3 = { (s16)(kw + 8), kh, 0, 0, 0, 0 };
    s16 ext[4] = { (s16)(kw + 8), kh, 0, 0 };                      /* {w, h, x, y} for bitmap_extract */
    for (s16 i = 0; i < DSS(rec); i++) {
        u16 pos = (u16)(DSW(rec + 8) + 2 * i);
        s16 x = DSC(pos), y = DSC((u16)(pos + 1));
        ext[2] = (s16)(x & ~7);
        ext[3] = y;
        FarPtr bmp = bitmap_extract(gate, ext, 1);
        r3.dx = DSS((u16)(0x038C + 4 * i));
        r3.dy = DSS((u16)(0x038E + 4 * i));
        drv_blit(bmp, ds_far(DS_page2_ptr), &r3, 1);               /* the gate at that position */
        r2.dx = (s16)(x - (x & ~7));
        bitmap_blit_masked_shift(knob, bmp, &r2, 0);
        r3.dy = (s16)(r3.dy + 0x13);
        drv_blit(bmp, ds_far(DS_page2_ptr), &r3, 1);               /* with the knob */
        arena_pop_low(1);
    }
    DSW(0x6C36) = 0xFFFF;                                          /* knob position shown */
    arena_pop(2);
}

static void shifter_blit(FarPtr dst, s16 pos, s16 knob)
{
    u16 rec = DSW(0x6C34);
    Rect r;
    r.h = DSS(rec + 6);
    r.sx = DSS((u16)(0x038C + 4 * pos));
    r.sy = (s16)(DSS((u16)(0x038E + 4 * pos)) + (knob ? 0x13 : 0));
    u16 p = (u16)(DSW(rec + 8) + 2 * pos);
    s16 xb = (s16)(DSC(p) & ~7);
    r.dx = (s16)(xb + 0x108);
    r.dy = (s16)((vga() ? 0 : 0x64) + DSC((u16)(p + 1)) + 0x17);
    s16 w = (s16)(0x30 - xb);
    r.w = w > 0x20 ? 0x20 : w;
    drv_blit(ds_far(DS_page2_ptr), dst, &r, 1);
}

/* 0f38:14f2 shifter_knob(dst, phys) */
void shifter_knob(FarPtr dst, u16 phys)
{
    s16 pos;
    if (PHW(phys, PH_AUTO_MODE) == 0) pos = PHS(phys, PH_GEAR);
    else if (PHW(phys, PH_GEAR) == 0) pos = 0;
    else pos = (s16)(PHS(phys, PH_AUTO_MODE) - 1);
    if (DSS(0x6C36) >= 0) shifter_blit(dst, DSS(0x6C36), 0);      /* erase the old knob */
    shifter_blit(dst, pos, 1);
    DSS(0x6C36) = pos;
    PHW(phys, PH_GEAR_DIRTY) = 0;
}

/* 0f38:22e9 gear_label: "0 1 2 3 4 N D" (DS:0482 pointers) after "Gear:" on the status line */
void gear_label(s16 i)
{
    u16 save = DSW(0x8256);
    DSW(0x8256) = 1;
    status_print(DSS(0x8258), ds_str(DSW((u16)(DS_gear_names + 2 * i))), 8);
    DSW(0x8256) = save;
}
