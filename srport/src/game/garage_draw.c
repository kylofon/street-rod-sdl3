/* Garage: car pictures and their animations (garage.md §4.17), segment 0f38, ported from the
 * disassembly: ega_blit_4planes 8126, ega_vram_cache_strips 819a, car_strip_setup 84be, the pump
 * overlay 872e, car_compose 8866, car_draw 8e48, the town background save 9e5d, the sprite scene
 * builder a273 and the frame scripts a5a5 / a65d / a6ad / a7ee, and the tyre change animation be93
 * with b632 / b82c.
 *
 * VGA path only (DS:8236 = -2): the Tandy (-6) and CGA/Hercules branches of these routines (and
 * 0f38:8336, their strip builder) are not ported — PORT, like the rest of the port (PORTING.md).
 * The data layouts of the animation records (Anim DS:7D18, SpriteRef DS:77BA, FrameRec DS:764E,
 * PageNode DS:7D42) are described in platform/anim_rect.c. */
#include "game/garage.h"

#include <string.h>

#include "game/ui.h"
#include "host.h"
#include "platform/platform.h"
#include "platform/video.h"
#include "platform/vga.h"
#include "sound/sound.h"

#define DF(o)        ds_far(o)
#define DFW(o, v)    ds_far_wr((o), (v))
#define LOC          DSS(DS_drive_dest)          /* DS:0284 */
#define YOFF         0x02D0                      /* DS_g_anim_yoff */

static inline bool has_mask(FarPtr d) { return !far_is_null(desc_mask(d)); }
static inline u16 x_align(void) { return DSW(DS_g_x_align); }
static inline FarPtr planes_plus(FarPtr d, u16 n) { FarPtr p = desc_planes(d); p.off = (u16)(p.off + n); return p; }
static inline void desc_set_planes(FarPtr d, FarPtr p) { far_wr(d.seg, (u16)(d.off + DESC_PLANES), p); }

/* ============================================================== 0f38:8126, 819a (EGA registers) */

/* 0f38:8126 (near): per plane p, map mask 1 << p / read map p, rows_mask_or of the source plane (and
 * its mask) onto the VRAM strip d. */
static void ega_blit_4planes(FarPtr src, FarPtr d, u16 mask_off, s16 wb, s16 rows, s16 sstr, u16 plane_size)
{
    u16 s = src.off;
    for (u16 bit = 1, p = 0; bit < 9; bit <<= 1, p++) {
        out(0x3C4, 2); out(0x3C5, (u8)bit);
        out(0x3CE, 4); out(0x3CF, (u8)p);
        rows_mask_or(d, src.seg, s, mask_off, sstr, wb, rows, wb);
        s = (u16)(s + plane_size);
    }
    out(0x3C4, 2); out(0x3C5, 0x0F);
}

/* 0f38:819a (near): for every frame, the bottom `rows` rows of the car are composited over the
 * background strip cached in VRAM (page 2 from row 46h): the strip's Tail record (tails + 8*i) gets
 * its far pointer, the first row of the car covered by it and the row count. The background comes
 * from bg (a screen copy) or, when bg is null, is a plain fill in `colour` (write mode 2). */
static void ega_vram_cache_strips(u16 tails, const FarPtr *frames, s16 n, s16 yoff, s16 rows, FarPtr bg, s16 colour)
{
    FarPtr pg = g_page(2);
    FarPtr p = planes_plus(pg, (u16)(0x46 * desc_stride(pg)));
    for (s16 i = 0; i < n; i++) {
        u16 t = (u16)(tails + 8 * i);
        FarPtr fr = frames[i];
        far_wr(DGROUP, t, p);
        DSW((u16)(t + 4)) = (u16)(desc_h(fr) - yoff - rows);
        DSW((u16)(t + 6)) = (u16)rows;
        p.off = (u16)(p.off + desc_stride(fr) * rows);
        FarPtr tp = far_rd(DGROUP, t);
        if (!far_is_null(bg)) {
            ega_save_rect(bg, (s16)desc_stride(fr), rows, tp, 0x28);
        } else {
            out(0x3CE, 5); out(0x3CF, 2);
            for (s16 c = 0; (s16)(desc_stride(fr) * rows) > c; c++) vwr(tp.seg, (u16)(tp.off + c), (u8)colour);
            out(0x3CE, 5); out(0x3CF, 0);
        }
        s16 s = (s16)desc_stride(fr);
        u16 off = (u16)(DSS((u16)(t + 4)) * s);
        ega_blit_4planes(planes_plus(fr, off), tp, (u16)(desc_mask(fr).off + off), s, rows, s, (u16)(desc_h(fr) * s));
    }
}

/* ============================================================== 0f38:84be, 872e, 9e5d */

/* 0f38:84be: background of a driving car: a descriptor for rows top-h .. top of bg (arena pool 1,
 * the planes shared) as *out_bg, a copy of it as *out_bgsrc (with a mask when there is smoke: the
 * bottom of the smoke picture's mask), and for smoke the overlay rects DS:5A10 / DS:5A18. */
void car_strip_setup(FarPtr bg, FarPtr *out_bgsrc, s16 smoke, FarPtr *out_bg, s16 top, s16 h, u16 *ov0, u16 *ov1)
{
    s16 stride = (s16)desc_stride(bg);
    s16 off = (s16)(top - h);
    s16 with_smoke = smoke != 0;
    FarPtr d = arena_alloc(0x30, 1);
    memcpy(far_mp(d), far_mp(bg), 0x30);
    wr16(d.seg, (u16)(d.off + DESC_PLANES), (u16)(rd16(d.seg, (u16)(d.off + DESC_PLANES)) + off * stride));
    wr16(d.seg, (u16)(d.off + DESC_H), (u16)h);
    wr16(d.seg, (u16)(d.off + DESC_SIZE), (u16)(h * stride * DSS(DS_g_nplanes)));
    *out_bgsrc = arena_bitmap_alloc((s16)desc_w(bg), h, with_smoke, 1);
    bitmap_copy_into(d, *out_bgsrc, 1);
    *out_bg = d;                                  /* VGA: the shared-plane descriptor itself */
    *ov0 = 0;
    *ov1 = 0;
    if (with_smoke == 0) return;
    FarPtr sp = pic_get(smoke, 2);
    u16 si = (u16)(desc_size(*out_bgsrc) / DSW(DS_g_nplanes));
    u16 skip = (u16)(desc_size(sp) / DSW(DS_g_nplanes) - si);
    far_memcpy(far_add(desc_mask(sp), skip), desc_mask(*out_bgsrc), si);
    arena_pop(1);
    *ov0 = 0x5A10;
    DSW(0x5A16) = (u16)(desc_h(*out_bgsrc) - 1);
    *ov1 = 0x5A18;
    DSW(0x5A1A) = (u16)(desc_h(*out_bgsrc) - 0x0F);
    DSW((u16)(*ov1 + 6)) = (u16)(desc_h(*out_bgsrc) - 1);
}

/* 0f38:872e (near): the gas pump picture pair (pic_a with the mask of pic_b) over the view at the
 * car's filler, into dst. */
static void pump_overlay(s16 x, s16 dy0, s16 carx, s16 y, FarPtr dst, s16 pic_a, s16 pic_b)
{
    DSW(DS_decode_alloc) = 0;
    FarPtr b = arena_bitmap_alloc(0x10, 0x46, 1, 2);
    pic_decode(pic_a, b);
    FarPtr pl = desc_planes(b);
    FarPtr z = { 0, 0 };
    desc_set_planes(b, z);
    pic_decode(pic_b, b);
    desc_set_planes(b, pl);
    DSW(DS_decode_alloc) = 1;
    bitmap_copy_into(b, dst, 1);
    FarPtr view = DF(0x825A);
    s16 d = (s16)(x - (carx & 0xFFF8));
    Rect r;
    r.h = (s16)desc_h(view);
    r.sy = 0;
    r.dy = (s16)(y - desc_h(view) - dy0 + 1);
    r.sx = d < 0 ? 0 : d;
    r.w = (s16)(d + 0x10) > 0x10 ? 0x10 : (s16)(d + 0x10);
    r.dx = -d < 0 ? 0 : (s16)-d;
    drv_blit_masked(view, dst, &r, 1);
    Rect r2 = { 0x10, 0x46, 0, 0, 0, 0 };
    drv_blit_masked(b, dst, &r2, 0);
    arena_pop(1);
}

/* 0f38:9e5d: the part of the town background behind the car (0xC8, 0x44) into DS:6CCC */
static void town_bg_save(void)
{
    FarPtr a = arena_bitmap_alloc(0x30, 0x29, 0, 0);
    DFW(0x6CCC, a);
    Rect r = { (s16)desc_w(a), (s16)desc_h(a), 0xC8, 0x44, 0, 0 };
    drv_blit(g_back(), a, &r, 1);
}

/* ============================================================== 0f38:a273 - a7ee */

/* 0f38:a273 sprite_scene_setup: the SpriteRef ring DS:77BA of n frames (tails: Tail records, 8 bytes
 * each, or 0), the Anim DS:7D18 at (x, y - h + 1) with its backgrounds and overlay rects, the PageNode
 * ring DS:7D42 of pages 0 and 1, and the first node's rect. Returns the Anim (DS:7D18). */
u16 sprite_scene_setup(s16 x, s16 y, const FarPtr *frames, u16 tails, s16 n, FarPtr bgsrc, FarPtr bg, u16 ov0, u16 ov1)
{
    y = (s16)(y - (desc_h(frames[0]) - 1));
    s16 i;
    for (i = 0; i < n; i++) {
        u16 e = (u16)(0x77BA + 0xC * i);
        far_wr(DGROUP, e, frames[i]);
        if (tails != 0) {
            if (DSS(DS_driver_id) == -2) { DSW((u16)(e + 4)) = tails; tails = (u16)(tails + 8); }
            else DSW((u16)(e + 4)) = (u16)(0x30 * i + 0x6E68);
        } else {
            DSW((u16)(e + 4)) = 0;
        }
        DSW((u16)(e + 6)) = (u16)(e + 0xC);
    }
    DSW((u16)(0x77B4 + 0xC * (n > 0 ? i : 0))) = 0x77BA;
    DSW(0x7D18) = 0x77BA;
    for (i = 0; i < n; i++) DSW((u16)(0x77C2 + 0xC * i)) = DSW((u16)(DSW((u16)(0x77C0 + 0xC * i)) + 6));
    for (i = 0; i < n; i++) DSW((u16)(0x77C4 + 0xC * i)) = DSW((u16)(DSW((u16)(0x77C2 + 0xC * i)) + 8));
    DFW(0x7D1A, bgsrc);
    DSW(0x7D26) = (u16)x;
    DSW(0x7D28) = (u16)y;
    DFW(0x7D1E, bg);
    DSW(0x7D22) = ov0;
    DSW(0x7D24) = ov1;
    if (DSS(DS_driver_id) == -2) {
        for (i = 0; i < 2; i++) {
            u16 nd = (u16)(0x7D42 + 0x10 * i);
            far_wr(DGROUP, nd, g_page(i));
            DSW((u16)(nd + 0xC)) = (u16)i;
            DSW((u16)(nd + 6)) = 0;
            DSW((u16)(nd + 4)) = 0;
            DSW((u16)(nd + 0xA)) = 0xFFFF;
            DSW((u16)(nd + 8)) = 0xFFFF;
            DSW((u16)(nd + 0xE)) = (u16)(nd + 0x10);
        }
        DSW((u16)(0x7D40 + 0x10 * i)) = 0x7D42;
    } else {
        DFW(0x7D42, g_front());
        DSW(0x7D4E) = 0;
        DSW(0x7D48) = DSW(0x7D46) = 0;
        DSW(0x7D4C) = DSW(0x7D4A) = 0xFFFF;
        DSW(0x7D50) = 0x7D42;
    }
    DSW(0x7D2C) = 0x7D42;
    FarPtr pg = DF(0x7D42);
    FarPtr f0 = far_rd(DGROUP, DSW(0x7D18));
    rect_clip_make((s16)desc_w(f0), (s16)desc_h(f0), DSS(0x7D26), DSS(0x7D28), (s16)desc_w(pg), (s16)desc_h(pg),
                   (s16 *)mp(DGROUP, 0x7D46));
    DSW(0x7D2A) = 0x764E;
    return 0x7D18;
}

/* 0f38:a5a5: reverses the 4-frame ring (driving out to the left) and starts at the frame matching
 * the car's x (DS:8B7C & 7) */
static void anim_ring_reverse(u16 anim)
{
    DSW((u16)(anim + 0xE)) = DSW(DS_anim_engine_ref);
    u16 e0 = DSW(anim);
    u16 e1 = DSW((u16)(e0 + 6));
    u16 e2 = DSW((u16)(e1 + 6));
    u16 e3 = DSW((u16)(e2 + 6));
    DSW((u16)(e0 + 6)) = e3;
    DSW((u16)(e1 + 6)) = e0;
    DSW((u16)(e2 + 6)) = e1;
    DSW((u16)(e3 + 6)) = e2;
    for (u16 i = 0; i < 4; i++) DSW((u16)(0x77C2 + 0xC * i)) = DSW((u16)(DSW((u16)(0x77C0 + 0xC * i)) + 6));
    for (u16 i = 0; i < 4; i++) DSW((u16)(0x77C4 + 0xC * i)) = DSW((u16)(DSW((u16)(0x77C2 + 0xC * i)) + 8));
    switch (DSW(DS_anim_engine_ref) & 7) {
    case 2: DSW(anim) = e1; break;
    case 4: DSW(anim) = e2; break;
    case 6: DSW(anim) = e3; break;
    default: break;
    }
}

/* 0f38:a65d: frame record i of the Anim's script: count steps of (dx, dy); the next record ends it */
void anim_phase_set(u16 anim, s16 i, s16 count, s16 dx, s16 dy)
{
    u16 f = (u16)(DSW((u16)(anim + 0x12)) + 4 * i);
    DSW(f) = (u16)count;
    DSB((u16)(f + 2)) = (u8)dx;
    DSB((u16)(f + 3)) = (u8)dy;
    DSW((u16)(f + 4)) = 0xFFFF;
    DSB((u16)(f + 6)) = 0;
    DSB((u16)(f + 7)) = 0;
}

/* The 16-bit `imul; cwd; idiv` of the original: the product is truncated to 16 bits. */
static inline s16 mdiv(s16 a, s16 b, s16 c) { return idiv32_16((s16)(a * b), c, NULL); }
static inline s16 abs16(s16 v) { return v < 0 ? (s16)-v : v; }

/* 0f38:a6ad: three speed phases over `dist` pixels: d1 per step, then d2 (p2 % of the way), then d3
 * (the rest); modes 2 / 6 (leaving) use magnitudes */
static void anim_phases(u16 anim, s16 mode, s16 d1, s16 d2, s16 d3, s16 dist, s16 p1, s16 p2)
{
    s16 n0 = 0, n1 = 0, n2 = 0;                   /* PORT: uninitialised for other modes */
    if (mode == 1 || mode == 4) {
        n2 = (s16)(idiv32_16(mdiv((s16)-(p1 + p2 - 0x64), dist, 0x64), d3, NULL) & ~3);
        n1 = (s16)(idiv32_16(mdiv(p2, dist, 0x64), d2, NULL) & ~1);
        n0 = idiv32_16((s16)(dist - (s16)(n1 * d2) - (s16)(n2 * d3)), d1, NULL);
    } else if (mode == 2 || mode == 6) {
        n0 = (s16)(abs16(idiv32_16(mdiv(p1, dist, 0x64), d1, NULL)) & ~3);
        n1 = (s16)(abs16(idiv32_16(mdiv(p2, dist, 0x64), d2, NULL)) & ~1);
        n2 = abs16(idiv32_16((s16)(dist - (s16)(abs16(d1) * n0) - (s16)(abs16(d2) * n1)), d3, NULL));
    }
    anim_phase_set(anim, 0, (s16)(n0 + 1), (s8)d1, 0);
    anim_phase_set(anim, 1, n1, (s8)d2, 0);
    anim_phase_set(anim, 2, n2, (s8)d3, 0);
    anim_phase_set(anim, 3, -1, 0, 0);
}

/* 0f38:a7ee: the frame script of a car_draw mode */
static void anim_script(u16 anim, s16 mode, s16 dist)
{
    if (mode == 1 || mode == 4) {
        if (LOC == 1) anim_phases(anim, 1, 8, 4, 2, dist, 0, 0x3C);
        else anim_phases(anim, 1, 8, 4, 2, dist, 0x19, 0x37);
    } else if (mode == 6) {
        anim_ring_reverse(anim);
        anim_phases(anim, 6, -2, -4, -4, dist, 0x28, 0x3C);
    } else if (mode == 2) {
        anim_phases(anim, 2, 2, 4, 8, dist, 0x0A, 0x3C);
    }
}

/* ============================================================== 0f38:8866 car_compose */

/* The row of the first opaque pixel in the mask of the last pic_blit_at picture (DS:70A8). */
static s16 roof_first_row(void)
{
    FarPtr p = DF(0x70A8);
    s16 k = 0;
    u16 n = (u16)(desc_size(p) / DSW(DS_g_nplanes));
    if (n != 0) {
        FarPtr m = desc_mask(p);
        u16 cx = 0;
        for (;;) {
            if (rd8(m.seg, (u16)(m.off + cx)) != 0xFF) { k = (s16)cx; break; }
            cx++;
            if (cx >= n) { k = (s16)cx; break; }
        }
    }
    return idiv32_16(k, (s16)desc_stride(p), NULL);
}

/* 0f38:8866: frames[0] = the car body of `model` (LIB2, arena pool 1) with the customisations of
 * flags (2 rear bumper stripped, 1 front bumper stripped, 4 chopped roof, 8 scoop), the sticker, the
 * driver (picture DS:8280), then frames 1..n-1 shifted by 2 px each, the wheels tyre_pics[i] (DS
 * offset of 4 pictures) and, with smoke, the ground shadow (LIB2 #157 = 0x49D). Also the filler /
 * windshield points DS:8264-8270 and the sticker position DS:8272-8276. */
void car_compose(FarPtr *frames, s16 model, s16 flags, s16 sticker, s16 driver, u16 tyre_pics, s16 smoke, s16 n)
{
    u16 P = (u16)(0x8DF0 + 10 * model);
    u16 L = (u16)(0x7682 + 12 * model);
    u16 pts = (u16)(0x818C + 6 * model);
    frames[0] = pic_get(model_pic(model), 1);
    DSW(0x826E) = DSB(pts);
    DSW(0x8270) = DSB((u16)(pts + 1));
    DSW(0x826A) = DSB((u16)(pts + 2));
    DSW(0x826C) = DSB((u16)(pts + 3));
    DSW(0x8264) = (DSB((u16)(pts + 2)) != 0 && DSB((u16)(pts + 3)) != 0) ? 1 : 0;
    DSW(0x8266) = DSB((u16)(pts + 4));
    DSW(0x8268) = DSB((u16)(pts + 5));
    u16 sx = DSB((u16)(L + 10));
    DSW(0x8276) = (u16)(sx & (u16)~x_align());
    DSW(0x8272) = (u16)(sx & x_align());
    DSW(0x8274) = DSB((u16)(L + 11));
    if (flags & 2) pic_blit_at(frames[0], DSS((u16)(P + 6)), 0, DSB((u16)(L + 7)));
    if (flags & 1) pic_blit_at(frames[0], DSS((u16)(P + 8)), DSB((u16)(L + 8)), DSB((u16)(L + 9)));
    s16 roof = 0;                                 /* [bp-2] */
    if (flags & 4) {
        pic_blit_at(frames[0], DSS((u16)(P + 2)), DSB((u16)(L + 4)), 0);
        roof = roof_first_row();
    }
    if ((flags & 8) && model != 0x19)
        pic_draw_masked(frames[0], DSS((u16)(P + 4)), DSB((u16)(L + 5)), DSB((u16)(L + 6)), 1);
    FarPtr under = DF(0x0294);                    /* the car picture under the sticker */
    Rect r = { (s16)desc_w(under), (s16)desc_h(under), DSB((u16)(L + 10)), DSB((u16)(L + 11)), 0, 0 };
    drv_blit(frames[0], under, &r, 1);
    if (sticker > 0) {
        u16 S = (u16)(0x7844 + 6 * sticker);
        pic_draw_masked(frames[0], DSS(S), (u8)(DSB((u16)(L + 10)) + DSW((u16)(S + 2))),
                        (u8)(DSB((u16)(L + 11)) + DSW((u16)(S + 4))), 0);
    }
    if (driver != 0) {
        FarPtr d = pic_get(DSS(0x8280), 2);
        bitmap_composite_behind(d, frames[0], DSB((u16)(L + 2)),
                                (s16)(DSB((u16)(L + 3)) - desc_h(d) + roof + 1));
        arena_pop(1);
    }
    for (s16 i = 1; i < n; i++)
        frames[i] = arena_bitmap_alloc((s16)(desc_w(frames[0]) + 8), (s16)desc_h(frames[0]), 1, 1);
    if (n > 1)                                    /* VGA: each frame 2 px right of the previous */
        for (s16 i = 1; i < n; i++) bitmap_make_shifted(frames[i - 1], 2, frames[i]);
    if (tyre_pics != 0) {
        for (s16 i = 0; i < n; i++) {
            FarPtr w = pic_get(DSS((u16)(tyre_pics + 2 * i)), 2);
            s16 dy = (s16)(desc_h(frames[i]) - desc_h(w));
            s16 si = (s16)(2 * i);
            bitmap_composite_behind(w, frames[i], (s16)(DSB(L) + si), dy);
            bitmap_composite_behind(w, frames[i], (s16)(DSB((u16)(L + 1)) + si), dy);
            arena_pop(1);
        }
    }
    if (smoke != 0) {
        FarPtr s = pic_get(0x49D, 2);
        s16 left, right;
        bitmap_opaque_extent(frames[0], &left, &right);
        s16 dy = (s16)(desc_h(frames[0]) - desc_h(s));
        for (s16 i = 0; i < n; i++) {
            s16 si = (s16)(2 * i);
            bitmap_composite_behind(s, frames[i], (s16)(left + si), dy);
            bitmap_composite_behind(s, frames[i], (s16)(right - desc_w(s) + si + 1), dy);
        }
        arena_pop(1);
    }
}

/* ============================================================== 0f38:8e48 car_draw */

void car_draw(s16 mode, s16 model, s16 flags, s16 sticker, s16 driver, s16 y, s16 smoke, u16 tyre_pics)
{
    s16 ytop70 = (s16)(y - 0x45);                 /* [bp-3Eh] */
    FarPtr frames[4] = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };   /* [bp-34h] */
    FarPtr bgsrc = { 0, 0 }, bg = { 0, 0 };       /* [bp-4Eh], [bp-42h] */
    u16 ov0 = 0, ov1 = 0;                         /* [bp-38h], [bp-3Ch] */
    cursor_ctl(-4);
    for (;;) {
        bool stat = mode == 3 || mode == 7 || mode == 8;
        s16 nfr = stat ? 1 : 4;                   /* [bp-2] */
        s16 w, h, masked;
        pic_info(model_pic(model), &w, &h, &masked);
        DSW(YOFF) = DSW((u16)(0x02A6 + 2 * LOC));
        s16 colour = (DSS(DS_driver_id) == -2 || DSS(DS_driver_id) == -6)
                         ? DSS((u16)(0x02C2 + 2 * LOC)) : DSS((u16)(0x02B4 + 2 * LOC));   /* [bp-58h] */
        s16 rows = stat ? (s16)(h - DSS(YOFF)) : DSS((u16)(0x0298 + 2 * LOC));   /* [bp-1Ch] */
        s16 x = (s16)(((s16)(0x140 - w) / 2 + 4) & 0xFFF8);   /* [bp-18h] */
        s16 xend = mode == 2 ? (s16)(0x140 - x) : (s16)(x + w);   /* [bp-4Ah] */
        if (DSW(0x5A24) != 2) {
            if (mode == 4) {
                DSW(0x4F4A) = 1;
                FarPtr strip = DF(0x5A0A);
                FarPtr none = { 0, 0 };
                bitmap_init(strip, desc_planes(g_page(2)), none, 0x140, (s16)(0x46 - DSS(YOFF)), -2);
                FarPtr dp = desc_planes(strip);
                dp.off = (u16)(dp.off - (u16)(ytop70 * 0x28));
                ega_latch_copy(desc_planes(g_back()), dp, 0x28, (s16)desc_h(strip), 0, ytop70, 0x28);
            }
            if (mode == 4) {
                arena_reset();
                if (LOC == 3) {
                    s16 w2, h2, m2;
                    pic_info(0x449, &w2, &h2, &m2);
                    DFW(0x028C, arena_bitmap_alloc(w2, h2, 0, 0));
                    DFW(0x0290, arena_bitmap_alloc(w2, h2, 0, 0));
                }
                if (LOC == 2 || LOC == 6) town_bg_save();
            } else {
                arena_reset_stacks();
            }
            if (far_is_null(DF(0x0294))) DFW(0x0294, arena_bitmap_alloc(0x40, 0x19, 0, 0));
            car_compose(frames, model, flags, sticker, driver, tyre_pics, 1, nfr);
            if (!stat) {
                if (DSW(0x5A24) == 1) { DSW(0x5A22) = 0x140; DSW(0x5A20) = 0; }
                else { DSW(0x5A22) = (u16)xend; DSW(0x5A20) = (u16)(mode == 2 ? x : 0); }
                car_strip_setup(DF(0x5A0A), &bgsrc, smoke != 0 ? 0x448 : 0, &bg, 0x46, h, &ov0, &ov1);
            }
        }
        DSW(DS_car_x) = (u16)x;
        DSW(DS_car_y) = (u16)(y - desc_h(frames[0]) + 1);
        DSW(0x8262) = (u16)(desc_h(frames[0]) - DSS(YOFF));
        s16 cy = DSS(DS_car_y);
        if (DSS(0x8266) > 0) {
            DSW(0x8266) = (u16)(DSS(0x8266) + x);
            DSW(0x8268) = (u16)(DSS(0x8268) + cy);
            DSW(0x826A) = (u16)(DSS(0x826A) + x);
            DSW(0x826C) = (u16)(DSS(0x826C) + cy);
        }
        DSW(0x826E) = (u16)(DSS(0x826E) + x);
        DSW(0x8270) = (u16)(DSS(0x8270) + cy);
        DSW(0x8272) = (u16)(DSS(0x8272) + x);
        DSW(0x8274) = (u16)(DSS(0x8274) + cy);
        s16 ax_x = (mode == 1 || mode == 4) ? (s16)(x - xend) : x;   /* [bp-24h] */
        if (DSW(0x5A24) != 2) {
            if (DSS(DS_driver_id) == -2) {
                FarPtr bgp = { 0, 0 };            /* [bp-54h] */
                if (mode == 3 || mode == 8) {
                    FarPtr s = DF(0x5A0A);
                    s16 xb = (s16)(x / 8);        /* abs / sar 3 / sign: truncation toward zero */
                    bgp = desc_planes(s);
                    bgp.off = (u16)((u16)((desc_h(s) - rows) * desc_stride(s)) + (u16)xb + bgp.off);
                }
                ega_vram_cache_strips(0x7EC2, frames, nfr, DSS(YOFF), rows, bgp, colour);
            }
            /* PORT: the Tandy / CGA / Hercules strips (0f38:8336, driver slot 0 copies) are not ported */
            if (mode == 3) {
                FarPtr d = desc_planes(g_back());
                d.off = (u16)((u16)((y - h) * 0x28) + (u16)(x >> 3) + d.off + 0x28);
                ega_latch_copy_to40(DF(0x7EC2), (s16)desc_stride(frames[0]), (s16)(desc_h(frames[0]) - DSS(YOFF)), d,
                                    (s16)desc_stride(frames[0]));
                drv_copy_page(desc_planes(g_back()), desc_planes(g_front()));
                break;
            }
            if (mode == 7 || mode == 8) {
                FarPtr d = desc_planes(g_front());
                d.off = (u16)((u16)((y - h) * 0x28) + (u16)(x >> 3) + d.off + 0x28);
                ega_latch_copy_to40(DF(0x7EC2), (s16)desc_stride(frames[0]), (s16)(h - DSS(YOFF)), d,
                                    (s16)desc_stride(frames[0]));
                page_copy_rect(g_front(), g_back(), x, (s16)(y - h + 1), (s16)(x + w - 1), y);
                break;
            }
        }
        /* NB: 84be's first out-parameter (the copy) becomes the Anim's bg (+6), its second (the shared
         * VRAM strip descriptor) the bgsrc (+2) the dirty rects are restored from */
        u16 anim = sprite_scene_setup(ax_x, y, frames, 0x7EC2, nfr, bg, bgsrc, ov0, ov1);
        anim_script(anim, mode, xend);
        if (mode == 6) memcpy(mp(DGROUP, (u16)(DSW((u16)(anim + 0x14)) + 4)), mp(DGROUP, 0x6CBC), 8);
        if (mode == 4 || mode == 1 || mode == 2 || mode == 6) {
            DSW(DS_anim_mode) = 1;
            DSW(DS_anim_engine_snd) = 1;
            if (DSW(0x5A24) != 2) race_isr_enter();
            while (drv_anim_step(anim) != 0) ;
            if (DSW(0x5A24) != 1) race_isr_leave();
            DFW(0x825A, DF(0x0288));
            DSW(DS_anim_engine_snd) = 0;
            DSW(DS_anim_mode) = 0;
            memcpy(mp(DGROUP, 0x6CBC), mp(DGROUP, (u16)(DSW((u16)(anim + 0x14)) + 4)), 8);
        }
        if (LOC == 3 && mode == 4) {              /* gas station: the pump nozzle over the car */
            FarPtr t = arena_bitmap_alloc(0x18, 7, 0, 2);
            Rect r = { 0x10, (s16)desc_h(t), (s16)(DSS(0x826E) - DSS(DS_car_x)), (s16)(DSS(0x8270) - DSS(DS_car_y)), 0, 0 };
            drv_blit(frames[0], t, &r, 1);
            pic_draw_masked(frames[0], 0x472, (u8)(s8)r.sx, (u8)(s8)r.sy, 0);
            pump_overlay(0x40, 0x64, DSS(DS_anim_engine_ref), y, DF(0x028C), 0x4C8, 0x4CA);
            pump_overlay(0x40, 0x64, DSS(DS_anim_engine_ref), y, DF(0x0290), 0x449, 0x44A);
            r.dx = r.sx; r.dy = r.sy; r.sx = 0; r.sy = 0;
            drv_blit(t, frames[0], &r, 1);
        }
        gfx_screen_mode(2);
        if (DSW(0x5A24) != 1) break;
        mode = 2;
        DSW(0x5A24) = 2;
    }
    DSW(0x5A20) = 0;
    DSW(0x5A22) = 0;
    DSW(YOFF) = 0;
    arena_reset_stacks();
    DSW(0x5A24) = 0;
    cursor_ctl(-2);
}

/* ============================================================== 0f38:be93 tire change */

#define TC_X        0x6DA0      /* x of the car (DS:8B7C) */
#define TC_CAR      0x6DF6      /* far: the car without wheels (frames array of 1) */
#define TC_TOOLS    0x6CDA      /* far[3]: jack pictures (ids DS:5AAA) */
#define TC_WREAR    0x6DD4      /* rear wheel x */
#define TC_WFRONT   0x6CD8      /* front wheel x */
#define TC_Y        0x5AA8      /* y base (0 on VGA) */
#define TC_RECT     0x6CE8      /* overlay rect {x0, y0, x1, y1} */

static inline void copy_page01(void) { drv_copy_page(desc_planes(g_page(0)), desc_planes(g_page(1))); }

/* 0f38:b632 (near): the hub picture (DS:6D30) at wheel x on page B at y 0x78; returns a 40-px strip of
 * the car around the wheel over the background (arena pool 1). */
static FarPtr tc_wheel_strip(s16 wx)
{
    FarPtr hub = DF(0x6D30);
    FarPtr q = arena_bitmap_alloc((s16)(desc_w(hub) + 8), (s16)desc_h(hub), has_mask(hub) ? 1 : 0, 1);
    bitmap_make_shifted(hub, (s16)(wx & (s16)~x_align()), q);
    s16 qw = (s16)desc_w(q);
    FarPtr t = arena_bitmap_alloc(qw, (s16)desc_h(q), 0, 1);
    s16 sx = (s16)((wx + DSS(TC_X)) & x_align());
    screen_save_rect(sx, 0x78, qw, (s16)desc_h(q), t);
    Rect r = { qw, (s16)desc_h(q), 0, 0, 0, 0 };
    drv_blit_masked(q, t, &r, 1);
    Rect r2 = { (s16)desc_w(q), (s16)desc_h(q), 0, 0, sx, 0x78 };
    drv_blit(t, g_back(), &r2, 1);
    arena_pop_low(2);
    FarPtr car = DF(TC_CAR);
    Rect r3 = { 0x28, (s16)desc_h(car), (s16)(wx & x_align()), 0, 0,
                (s16)-(desc_h(car) + DSS(TC_Y) - 0x95) };
    FarPtr s = arena_bitmap_alloc(0x28, (s16)(0x9C - DSS(TC_Y)), 1, 1);
    screen_save_rect((s16)(r3.sx + DSS(TC_X)), DSS(TC_Y), 0x28, (s16)(0x9C - DSS(TC_Y)), s);
    far_memset(desc_mask(s), 0xFF, (u16)(desc_size(s) / DSW(DS_g_nplanes)));
    drv_blit_masked(car, s, &r3, 1);
    drv_blit(car, s, &r3, 2);
    return s;
}

/* 0f38:b82c (near): at wheel x: the old wheel goes down (a hand holds it), rolls away to the left,
 * the new one rolls in from the left and goes up into place. DS:6D2C = the car strip at the wheel. */
static void tc_wheel_swap(s16 x, u16 old_pics, u16 new_pics)
{
    DFW(0x6D38, pic_get(0x4D2, 1));               /* the hand */
    DFW(0x6CF0, pic_get(DSS(old_pics), 1));
    DSW(0x6D3E) = (u16)(x & (s16)~x_align());
    FarPtr w0 = DF(0x6CF0);
    DFW(0x6DE2, arena_bitmap_alloc((s16)(desc_w(w0) + 8), (s16)desc_h(w0), 1, 1));
    bitmap_make_shifted(w0, DSS(0x6D3E), DF(0x6DE2));
    DFW(0x6DF2, arena_bitmap_alloc((s16)desc_w(DF(0x6DE2)), (s16)desc_h(DF(0x6DE2)), 1, 1));
    bitmap_copy_into(DF(0x6DE2), DF(0x6DF2), 3);
    DSW(0x6CE6) = (u16)(desc_h(DF(0x6DE2)) - desc_h(DF(0x6D38)));
    bitmap_composite_behind(DF(0x6D38), DF(0x6DE2), DSS(0x6D3E), DSS(0x6CE6));
    copy_page01();
    FarPtr strip = DF(0x6D2C);
    DSW(TC_RECT) = 0;
    DSW((u16)(TC_RECT + 2)) = 0;
    DSW((u16)(TC_RECT + 4)) = (u16)(desc_w(strip) - 1);
    DSW((u16)(TC_RECT + 6)) = (u16)(desc_h(strip) + DSS(TC_Y) - 1);
    u16 anim = sprite_scene_setup((s16)(x & x_align()), 0x94, (const FarPtr *)mp(DGROUP, 0x6DF2), 0, 1,
                                  DF(0x5AB0), strip, TC_RECT, 0);
    DSW(0x6D3C) = anim;
    anim_phase_set(anim, 0, 8, 0, 1);             /* down 8 */
    while (drv_anim_step(DSW(0x6D3C)) != 0) {
        if (DSS((u16)(DSW(0x6D3C) + 0x10)) == 0x7D) memcpy(far_mp(DF(0x6DF2)), far_mp(DF(0x6DE2)), 0x30);
        wait_vretraces(3);
    }
    copy_page01();
    for (s16 si = 1, di = 2; si < 4; si++, di = (s16)(di + 2)) {   /* the old wheel's rolling frames */
        DSW(0x6DA2) = (u16)((di + DSS(0x6D3E)) & (s16)~x_align());
        u16 slot = (u16)(0x6DE2 + 4 * (4 - si));
        if (DSW(0x6DA2) != 0) {
            DFW(0x6CF0, pic_get(DSS((u16)(old_pics + di)), 2));
            FarPtr p = DF(0x6CF0);
            far_wr(DGROUP, slot, arena_bitmap_alloc((s16)(desc_w(p) + 8), (s16)desc_h(p), 1, 1));
            bitmap_make_shifted(p, DSS(0x6DA2), far_rd(DGROUP, slot));
            arena_pop(1);
        } else {
            far_wr(DGROUP, slot, pic_get(DSS((u16)(old_pics + 2 * si)), 1));
        }
        bitmap_composite_behind(DF(0x6D38), far_rd(DGROUP, slot), DSS(0x6DA2), DSS(0x6CE6));
    }
    anim = sprite_scene_setup(x, 0x9B, (const FarPtr *)mp(DGROUP, 0x6DE2), 0, 4, DF(0x5AB4), DF(0x6D24), 0, 0);
    DSW(0x6D3C) = anim;
    anim_phase_set(anim, 0, (s16)((s16)(x + desc_w(DF(0x6DE2))) >> 1), -2, 0);   /* rolls away */
    loop_idle_putt(1);
    while (drv_anim_step(DSW(0x6D3C)) != 0) wait_vretraces(2);
    loop_idle_putt(0);
    arena_pop_low(6);
    copy_page01();
    DFW(0x6DE2, pic_get(DSS(new_pics), 1));
    DSW(0x6D3E) = (u16)(((u8)x & 7) >> 1);
    for (s16 k = 0, si = 2; k < 3; k++, si = (s16)(si + 2)) {       /* the new wheel's frames */
        u16 slot = (u16)(0x6DE6 + 4 * k);
        DSW(0x6DA2) = (u16)(si & (s16)~x_align());
        if (DSW(0x6DA2) != 0) {
            DFW(0x6CF0, pic_get(DSS((u16)(new_pics + si)), 2));
            FarPtr p = DF(0x6CF0);
            far_wr(DGROUP, slot, arena_bitmap_alloc((s16)(desc_w(p) + 8), (s16)desc_h(p), 1, 1));
            bitmap_make_shifted(p, DSS(0x6DA2), far_rd(DGROUP, slot));
            arena_pop(1);
        } else {
            far_wr(DGROUP, slot, pic_get(DSS((u16)(new_pics + 2 + 2 * k)), 1));
        }
    }
    for (s16 di = 0; di < 4; di++) {
        u16 slot = (u16)(0x6DE2 + 4 * di);
        if (di == DSS(0x6D3E)) {
            FarPtr f = far_rd(DGROUP, slot);
            DFW(0x6DF2, arena_bitmap_alloc((s16)desc_w(f), (s16)desc_h(f), 1, 1));
            bitmap_copy_into(f, DF(0x6DF2), 3);
        }
        bitmap_composite_behind(DF(0x6D38), far_rd(DGROUP, slot), (s16)((2 * di) & (s16)~x_align()), DSS(0x6CE6));
    }
    anim = sprite_scene_setup((s16)-(s16)desc_w(DF(0x6DE2)), 0x9B, (const FarPtr *)mp(DGROUP, 0x6DE2), 0, 4,
                              DF(0x5AB4), DF(0x6D24), 0, 0);
    DSW(0x6D3C) = anim;
    anim_phase_set(anim, 0, (s16)(((s16)(x + desc_w(DF(0x6DE2))) >> 1) + 1), 2, 0);   /* rolls in */
    loop_idle_putt(1);
    while (drv_anim_step(DSW(0x6D3C)) != 0) wait_vretraces(2);
    loop_idle_putt(0);
    copy_page01();
    strip = DF(0x6D2C);
    DSW(TC_RECT) = 0;
    DSW((u16)(TC_RECT + 2)) = 0;
    DSW((u16)(TC_RECT + 4)) = (u16)(desc_w(strip) - 1);
    DSW((u16)(TC_RECT + 6)) = (u16)(desc_h(strip) + DSS(TC_Y) - 1);
    anim = sprite_scene_setup(x, 0x9B, (const FarPtr *)mp(DGROUP, (u16)(0x6DE2 + 4 * DSS(0x6D3E))), 0, 1,
                              DF(0x5AB0), strip, TC_RECT, 0);
    DSW(0x6D3C) = anim;
    anim_phase_set(anim, 0, 8, 0, -1);            /* up 8 */
    while (drv_anim_step(DSW(0x6D3C)) != 0) {
        if (DSS((u16)(DSW(0x6D3C) + 0x10)) == 0x7D)
            memcpy(far_mp(far_rd(DGROUP, (u16)(0x6DE2 + 4 * DSS(0x6D3E)))), far_mp(DF(0x6DF2)), 0x30);
        wait_vretraces(3);
    }
    arena_pop_low(6);
}

/* The jack cranking of be93 (@c89a / @cb36): while the car moves, the jack pictures DS:6CDA[si]
 * cycle into the car's background DS:6DFA; `dir` is added to the car's y on the steps where the
 * jack changes direction (sic). */
static void tc_crank(u16 anim, s16 si, s16 di, s16 dir, Rect *r3c)
{
    DSW(0x6D3C) = anim;
    loop_crank(1);
    while (drv_anim_step(DSW(0x6D3C)) != 0) {
        wait_vretraces(3);
        if (di == 0) {
            if (si != 0) si--;
            else { si = 1; di = 1; }
        } else if (si == 2) { si = 1; di = 0; }
        else si++;
        if (di == 0) DSW((u16)(DSW(0x6D3C) + 0x10)) = (u16)(DSS((u16)(DSW(0x6D3C) + 0x10)) + dir);
        FarPtr p = DF((u16)(TC_TOOLS + 4 * si));
        drv_blit_masked(p, DF(0x6DFA), r3c, 1);
        drv_blit(p, DF(0x6DFA), r3c, 2);
    }
    loop_crank(0);
}

/* 0f38:be93 (garage.md §4.9): jack under the car, the car goes up, each wheel is swapped
 * (tc_wheel_swap), the car goes down, the jack goes away. old_pics / new_pics: DS offsets of the 4
 * wheel pictures (tyre_pics(grade)). */
void tire_change_anim(u16 old_pics, u16 new_pics)
{
    u16 save_mirror = DSW(DS_g_mirror);
    u16 save_sx = DSW(DS_sticker_x), save_sy = DSW(DS_sticker_y);
    arena_reset_stacks();
    cursor_ctl(-4);
    DSW(DS_g_mirror) = 0;
    DSW(TC_X) = DSW(DS_anim_engine_ref);
    u16 cur = DSW(G_CUR_CAR);
    s16 model = DSS((u16)(cur + CR_MODEL));
    s16 fl = (s16)car_flags(cur);
    s16 sticker = (s16)((DSW((u16)(cur + CR_FLAGS)) >> 8) & 0x1F);
    for (u16 k = 0; k < 3; k++) DFW((u16)(TC_TOOLS + 4 * k), pic_get(DSS((u16)(0x5AAA + 2 * k)), 1));
    FarPtr B = DF(0x6CDE);
    u16 colour = (DSS(DS_driver_id) == -2 || DSS(DS_driver_id) == -6) ? DSW(0x02C4) : DSW(0x02B6);
    screen_fill_rect(0x88, (s16)(0xBA - desc_h(B)), (s16)desc_w(B), (s16)desc_h(B), (u8)colour);
    DSW(TC_Y) = 0;
    DFW(0x6D28, arena_bitmap_alloc((s16)desc_w(B), (s16)(0xBA - DSS(TC_Y)), 0, 1));
    screen_save_rect((s16)(x_align() & 0x88), DSS(TC_Y), (s16)desc_w(B), (s16)(0xBA - DSS(TC_Y)), DF(0x6D28));
    car_compose((FarPtr *)mp(DGROUP, TC_CAR), model, fl, sticker, 0, 0, 0, 1);
    DSW(TC_WREAR) = DSB((u16)(0x7682 + 12 * model));
    DSW(TC_WFRONT) = DSB((u16)(0x7683 + 12 * model));
    FarPtr car = DF(TC_CAR);
    s16 wy = (s16)(desc_h(car) - 0x1C);           /* [bp-48h] */
    {
        s16 r[4] = { (s16)desc_w(car), 0x1C, 0, wy };
        DFW(0x6DD6, bitmap_extract(car, r, 1));  /* the bottom of the car */
    }
    show_picture(0x408, -1);                      /* the garage (not mirrored) */
    DFW(0x6DDE, pic_get(0x49D, 2));               /* ground shadow */
    s16 left, right;
    bitmap_opaque_extent(car, &left, &right);
    FarPtr sh = DF(0x6DDE);
    DFW(0x6D34, arena_bitmap_alloc((s16)desc_w(car), (s16)desc_h(sh), 0, 2));
    screen_save_rect(DSS(TC_X), (s16)(0x99 - desc_h(sh)), (s16)desc_w(car), (s16)desc_h(sh), DF(0x6D34));
    DFW(0x6DDA, arena_bitmap_alloc((s16)(desc_w(sh) + 8), (s16)desc_h(sh), 1, 2));
    bitmap_make_shifted(sh, (s16)(left & (s16)~x_align()), DF(0x6DDA));
    Rect r = { (s16)desc_w(DF(0x6DDA)), (s16)desc_h(DF(0x6DDA)), 0, 0, (s16)(left & x_align()), 0 };
    drv_blit_masked(DF(0x6DDA), DF(0x6D34), &r, 1);
    arena_pop(1);
    DFW(0x6DDA, arena_bitmap_alloc((s16)(desc_w(sh) + 8), (s16)desc_h(sh), 1, 2));
    bitmap_make_shifted(sh, (s16)((right + 1) & (s16)~x_align()), DF(0x6DDA));
    r.w = (s16)desc_w(DF(0x6DDA));
    r.dx = (s16)(right - r.w + (s16)(u16)~x_align() + 1);
    drv_blit_masked(DF(0x6DDA), DF(0x6D34), &r, 1);
    arena_pop(1);
    r.w = (s16)desc_w(DF(0x6D34));
    r.dx = DSS(TC_X);
    r.dy = (s16)(0x99 - r.h);
    drv_blit(DF(0x6D34), g_back(), &r, 1);
    arena_pop(2);
    DSW(TC_Y) = 0;
    DFW(0x6DFA, arena_bitmap_alloc((s16)desc_w(car), (s16)(0x99 - DSS(TC_Y)), 1, 1));
    screen_save_rect(DSS(TC_X), DSS(TC_Y), (s16)desc_w(car), (s16)(0x99 - DSS(TC_Y)), DF(0x6DFA));
    far_memset(desc_mask(DF(0x6DFA)), 0xFF, (u16)(desc_size(DF(0x6DFA)) / DSW(DS_g_nplanes)));
    B = DF(0x6CDE);
    Rect r3c = { (s16)desc_w(B), (s16)desc_h(B), 0, 0, (s16)(0x88 - DSS(TC_X)), (s16)-(desc_h(B) + DSS(TC_Y) - 0x9A) };
    drv_blit_masked(DF(0x6CDA), DF(0x6DFA), &r3c, 1);
    drv_blit(DF(0x6CDA), DF(0x6DFA), &r3c, 2);
    DFW(0x6D30, pic_get(0x4C7, 2));               /* the hub */
    DSW(TC_Y) = 0;
    FarPtr front_strip = tc_wheel_strip(DSS(TC_WFRONT));   /* [bp-20h] */
    FarPtr rear_strip = tc_wheel_strip(DSS(TC_WREAR));     /* [bp-1Ah] */
    arena_pop(1);
    car = DF(TC_CAR);
    DFW(0x6D24, arena_bitmap_alloc((s16)(desc_w(car) + DSS(TC_X)), 0x1C, 0, 1));
    screen_save_rect(0, 0x80, (s16)(desc_w(car) + DSS(TC_X)), 0x1C, DF(0x6D24));
    {
        Rect q = { (s16)desc_w(car), (s16)(desc_h(DF(0x6D24)) - 8), 0, (s16)(desc_h(car) - 0x15), DSS(TC_X), 0 };
        drv_blit_masked(car, DF(0x6D24), &q, 1);
        B = DF(0x6CDE);
        Rect q2 = { (s16)desc_w(B), (s16)desc_h(DF(0x6D24)), 0, 0, 0x88, 0 };
        q2.sy = (s16)(desc_h(B) - q2.h + 2);
        drv_blit_masked(DF(0x6CE2), DF(0x6D24), &q2, 1);
    }
    if (DSS(DS_driver_id) == -2) {                /* the backgrounds cached in VRAM pages 2 / 3 */
        FarPtr none = { 0, 0 };
        DFW(0x6CD4, far_make(DGROUP, 0x6CF4));
        bitmap_init(DF(0x6CD4), desc_planes(g_page(2)), none, (s16)desc_w(car), 0xC8, -2);
        bitmap_init(DF(0x5AB0), planes_plus(g_page(2), desc_stride(car)), none, (s16)desc_w(front_strip), 0xC8, -2);
        bitmap_init(DF(0x5AB4), desc_planes(g_page(3)), none, 0x140, (s16)desc_h(DF(0x6D24)), -2);
        bitmap_init(DF(0x5AB8), desc_planes(g_page(3)), none, (s16)desc_w(DF(0x6D28)), 0xC8, -2);
        Rect q = { (s16)desc_w(DF(0x6DFA)), (s16)(desc_h(car) + 4), 0, (s16)(0x95 - desc_h(car)), 0,
                   (s16)(0x95 - desc_h(car)) };
        drv_blit(DF(0x6DFA), DF(0x6CD4), &q, 1);
        q.w = (s16)desc_w(front_strip); q.h = 0x23; q.sy = 0x79; q.dy = 0x79;
        drv_blit(rear_strip, DF(0x5AB0), &q, 1);
        q.w = (s16)desc_w(DF(0x6D24)); q.h = (s16)desc_h(DF(0x6D24)); q.sy = 0; q.dy = 0;
        drv_blit(DF(0x6D24), DF(0x5AB4), &q, 1);
        FarPtr A = DF(0x6CDA);
        q.w = (s16)desc_w(DF(0x6D28)); q.h = (s16)(desc_h(A) + 0x21); q.sy = (s16)(0x99 - desc_h(A)); q.dy = q.sy;
        drv_blit(DF(0x6D28), DF(0x5AB8), &q, 1);
    }
    /* the jack comes up */
    DSW(TC_Y) = 0;
    copy_page01();
    FarPtr none = { 0, 0 };
    u16 anim = sprite_scene_setup(0x88, 0xB9, (const FarPtr *)mp(DGROUP, 0x6CDE), 0, 1, DF(0x5AB8), DF(0x6D28), 0, 0);
    DSW(0x6D3C) = anim;
    anim_phase_set(anim, 0, 0x21, 0, -1);
    while (drv_anim_step(DSW(0x6D3C)) != 0) wait_vretraces(3);
    music_mute(1);
    DSW(TC_Y) = 0;
    /* the old wheels on the car picture */
    DFW(0x6CF0, pic_get(DSS(old_pics), 2));
    bitmap_composite_behind(DF(0x6CF0), DF(TC_CAR), DSS(TC_WREAR), wy);
    bitmap_composite_behind(DF(0x6CF0), DF(TC_CAR), DSS(TC_WFRONT), wy);
    arena_pop(1);
    copy_page01();
    /* the car goes up 7 */
    FarPtr bgv = DF(0x6DFA);
    DSW(TC_RECT) = 0;
    DSW((u16)(TC_RECT + 2)) = 0;
    DSW((u16)(TC_RECT + 4)) = (u16)(desc_w(bgv) - 1);
    DSW((u16)(TC_RECT + 6)) = (u16)(desc_h(bgv) + DSS(TC_Y) - 1);
    anim = sprite_scene_setup(DSS(TC_X), 0x98, (const FarPtr *)mp(DGROUP, TC_CAR), 0, 1, DF(0x6CD4), bgv, TC_RECT, 0);
    anim_phase_set(anim, 0, 7, 0, -1);
    tc_crank(anim, 0, 0, 1, &r3c);
    DSW(TC_Y) = 0;
    /* rear wheel */
    DFW(0x6D2C, rear_strip);
    tc_wheel_swap((s16)(DSS(TC_X) + DSS(TC_WREAR)), old_pics, new_pics);
    DFW(0x6D2C, front_strip);
    {
        Rect q = { (s16)desc_w(front_strip), 0x23, 0, 0x79, 0, 0x79 };
        drv_blit(front_strip, DF(0x5AB0), &q, 1);
        FarPtr s24 = DF(0x6D24);
        Rect q2 = { 0x28, (s16)desc_h(s24), (s16)(DSS(TC_X) + DSS(TC_WREAR)), 0, (s16)(DSS(TC_X) + DSS(TC_WREAR)), 0 };
        q2.sy = (s16)(0x9C - q2.h);
        drv_blit(g_front(), s24, &q2, 1);
        drv_blit(g_front(), DF(0x5AB4), &q2, 1);
    }
    /* front wheel */
    tc_wheel_swap((s16)(DSS(TC_X) + DSS(TC_WFRONT)), old_pics, new_pics);
    DSW(TC_Y) = 0;
    /* the new wheels on the car picture */
    DFW(0x6DE2, pic_get(DSS(new_pics), 1));
    car = DF(TC_CAR);
    {
        FarPtr nw = DF(0x6DE2);
        Rect q = { (s16)desc_w(car), (s16)desc_h(nw), 0, 0, 0, (s16)(desc_h(car) - desc_h(nw)) };
        drv_blit(DF(0x6DD6), car, &q, 3);
        bitmap_composite_behind(nw, car, DSS(TC_WREAR), wy);
        bitmap_composite_behind(nw, car, DSS(TC_WFRONT), wy);
    }
    copy_page01();
    /* the car goes down 9 */
    drv_blit_masked(DF(0x6CE2), DF(0x6DFA), &r3c, 1);
    drv_blit(DF(0x6CE2), DF(0x6DFA), &r3c, 2);
    bgv = DF(0x6DFA);
    DSW(TC_RECT) = 0;
    DSW((u16)(TC_RECT + 2)) = 0;
    DSW((u16)(TC_RECT + 4)) = (u16)(desc_w(bgv) - 1);
    DSW((u16)(TC_RECT + 6)) = (u16)(desc_h(bgv) + DSS(TC_Y) - 1);
    anim = sprite_scene_setup(DSS(TC_X), 0x94, (const FarPtr *)mp(DGROUP, TC_CAR), 0, 1, DF(0x6CD4), bgv, TC_RECT, 0);
    anim_phase_set(anim, 0, 9, 0, 1);
    tc_crank(anim, 2, 1, -1, &r3c);
    music_mute(0);
    DSW(TC_Y) = 0;
    copy_page01();
    /* the jack goes down */
    anim = sprite_scene_setup(0x88, 0x99, (const FarPtr *)mp(DGROUP, 0x6CDE), 0, 1, DF(0x5AB8), DF(0x6D28), 0, 0);
    DSW(0x6D3C) = anim;
    anim_phase_set(anim, 0, 0x21, 0, 1);
    while (drv_anim_step(DSW(0x6D3C)) != 0) wait_vretraces(3);
    arena_reset_stacks();
    drv_copy_page(desc_planes(g_back()), desc_planes(g_front()));
    DSW(DS_anim_engine_ref) = DSW(TC_X);
    DSW(DS_g_mirror) = save_mirror;
    DSW(DS_sticker_x) = save_sx;
    DSW(DS_sticker_y) = save_sy;
    DSW(TC_Y) = 0;
    (void)none;
    cursor_ctl(-2);
}

/* ============================================================== 0f38:975b - 99d9 (Bob's car-hop) */

/* 0f38:975b (race: gas_leave, car-hop): rows y .. y+h-1 of picture `pic` (source sx, sy, width w)
 * into the car strip DS:5A0A (rows base-0x45 ..) at x dx, clipped to the strip; at Bob's (DS:0284 =
 * 6) the 7 background columns DS:533E / DS:5342 (width, x; 14-byte records) are refreshed from the
 * visible page. The ninth argument of the callers is unused. */
void walker_anim(FarPtr pic, s16 sx, s16 sy, s16 w, s16 h, s16 dx, s16 y, s16 base, s16 unused)
{
    (void)unused;
    FarPtr strip = DF(0x5A0A);
    s16 top = (s16)(base - 0x45);                 /* [bp-1Ch] */
    s16 bot = (s16)(desc_h(strip) + top - 1);     /* [bp-1Eh] */
    s16 y0 = y < top ? top : y;
    s16 y1 = (s16)(y + h - 1) > bot ? bot : (s16)(y + h - 1);
    Rect r;
    if (y0 <= y1) {
        r.w = w;
        r.h = (s16)(y1 - y0 + 1);
        r.sx = sx;
        r.sy = (s16)(sy + y0 - y);
        r.dx = dx;
        r.dy = (s16)(y0 - top);
        drv_blit(pic, strip, &r, 1);
    }
    if (LOC == 6) {
        r.h = (s16)desc_h(strip);
        r.sy = (s16)(base - 0x45);
        r.dy = 0;
        for (u16 k = 0; k < 7; k++) {
            r.w = DSS((u16)(0x533E + 0xE * k));
            r.dx = r.sx = DSS((u16)(0x5342 + 0xE * k));
            drv_blit(g_front(), strip, &r, 1);
        }
    }
}

/* 0f38:985c (near): the car-hop's frames of `mode` (DS:5A26 + 6*mode: picture, columns, count), cut
 * from a sheet of two rows */
static void carhop_frames(FarPtr *frames, s16 mode)
{
    u16 t = (u16)(0x5A26 + 6 * mode);
    s16 cols = DSS((u16)(t + 2)), n = DSS((u16)(t + 4));
    FarPtr p = pic_get(DSS(t), 2);
    s16 r[4] = { idiv32_16((s16)desc_w(p), cols, NULL), (s16)((s16)desc_h(p) >> 1), 0, 0 };
    for (s16 i = 0; i < n; i++) {
        if (i == cols) { r[2] = 0; r[3] = r[1]; }
        frames[i] = bitmap_extract(p, r, 2);
        r[2] = (s16)(r[2] + r[0]);
    }
}

/* 0f38:990f (near): descriptor *d over the VRAM strip (page 2 row 46h) of picture 0x474's size, with
 * the visible page's rows from 44h copied in */
static void carhop_strip(FarPtr d)
{
    s16 w, h, m;
    pic_info(0x474, &w, &h, &m);
    FarPtr none = { 0, 0 };
    bitmap_init(d, planes_plus(g_page(2), (u16)(0x46 * desc_stride(g_page(2)))), none, w, h,
                DSS(DS_driver_id) == -2 ? -2 : -1);
    FarPtr dp = desc_planes(d);
    dp.off = (u16)(dp.off - 0xAA0);
    ega_latch_copy(desc_planes(g_front()), dp, (s16)desc_stride(d), (s16)desc_h(d), 0, 0x44, 0x28);
}

/* PORT: the original keeps this descriptor, the overlay rect and the frame script on its stack (SS =
 * DGROUP). The descriptor goes to an otherwise unused paragraph above 1 MB of mem[], the rect to the
 * tyre-change scratch rect DS:6CE8, the script to the default script area DS:764E. */
#define CARHOP_DESC far_make(0xFFFF, 0x0010)

/* 0f38:99d9: the car-hop at Bob's comes in (mode 0, from the left) or leaves (mode 1, to the right)
 * behind the opponent's car (opp != 0: the car picture DS:825A in front of her). */
void carhop_anim(s16 mode, s16 opp)
{
    FarPtr D = CARHOP_DESC;
    arena_reset_stacks();
    carhop_strip(D);
    Rect r;
    if (mode == 1) {
        Rect q = { 0x30, 0x29, 0, 0, 0xC8, 0 };
        drv_blit(DF(0x6CCC), D, &q, 1);
    }
    FarPtr ov = { 0, 0 };
    if (opp != 0) {
        s16 c[4];
        if (rect_clip_to_view(0, 0x44, (s16)desc_w(D), (s16)desc_h(D), c) != NULL) {
            s16 e[4] = { (s16)(c[2] - c[0]), (s16)(c[3] - c[1] + 1), (s16)(c[0] - DSS(DS_car_x)), (s16)(c[1] - DSS(DS_car_y)) };
            ov = bitmap_extract(DF(0x825A), e, 2);
            r.w = e[0]; r.h = e[1]; r.sx = 0; r.sy = 0; r.dx = c[0];
            r.dy = (s16)(desc_h(D) - desc_h(ov));
            drv_blit_masked(ov, D, &r, 1);
        }
    }
    FarPtr bgc, bgs;                              /* [bp-0D4h] copy with mask, [bp-9Eh] shared */
    u16 ovx;
    car_strip_setup(D, &bgc, 0x474, &bgs, 0x29, 0x29, &ovx, &ovx);
    FarPtr a = { 0, 0 };                          /* [bp-88h] */
    if (mode == 0) {
        a = arena_bitmap_alloc(0x30, 0x29, 1, 2);
        Rect q = { 0x30, 0x29, 0, 0, 0, 0 };
        drv_blit(DF(0x6CCC), a, &q, 1);
        q.sx = 0xC8;
        drv_blit(bgc, a, &q, 2);
    }
    if (opp != 0 && !far_is_null(ov)) mask_and_rect(ov, bgc, &r);
    DSW(0x6CE8) = 0;
    DSW(0x6CEA) = 0;
    DSW(0x6CEC) = (u16)(desc_w(bgc) - 1);
    DSW(0x6CEE) = (u16)(desc_h(bgc) - 1);
    FarPtr frames[14];
    carhop_frames(frames, mode);
    FarPtr fb[1] = { arena_bitmap_alloc(0x30, 0x29, 1, 2) };   /* [bp-84h] */
    u16 anim = sprite_scene_setup(mode == 0 ? -0x28 : 0xC8, 0x6C, fb, 0, 1, bgs, bgc, 0x6CE8, 0);
    memcpy(mp(DGROUP, 0x764E), mp(DGROUP, mode == 0 ? 0x5A86 : 0x5A96), 16);
    DSW((u16)(anim + 0x12)) = 0x764E;
    if (mode == 1) {
        u16 nd = DSW((u16)(anim + 0x14));
        memcpy(mp(DGROUP, (u16)(nd + 4)), mp(DGROUP, 0x6CC4), 8);
        memcpy(mp(DGROUP, (u16)(DSW((u16)(nd + 0xE)) + 4)), mp(DGROUP, 0x6CC4), 8);
    }
    DSW(YOFF) = 0;
    for (u16 si = 0; si < 0x54; si++) {
        s16 x = DSS((u16)(anim + 0xE));
        s16 fi = DSC((u16)(0x5A32 + si));
        if (fi >= DSS((u16)(0x5A26 + 6 * mode + 4))) fi = (s16)(DSS((u16)(0x5A26 + 6 * mode + 4)) - 1);
        /* PORT: the last entry (11) indexes past the 11 frames into the original's stack (garbage);
         * the port uses the last frame */
        bitmap_make_shifted(frames[fi], (s16)(x & (s16)~x_align()), fb[0]);
        u32 until = DSL(DS_ticks) + 2;
        drv_anim_step(anim);
        while (tick_after(until, ticks_now())) host_pump();
    }
    drv_anim_step(anim);
    memcpy(mp(DGROUP, 0x6CC4), mp(DGROUP, (u16)(DSW((u16)(anim + 0x14)) + 4)), 8);
    if (mode == 1) {
        walker_anim(DF(0x6CCC), 0, 0, 0x30, 0x29, 0xC8, 0x44, 0xA3, 2);
    } else {
        s16 w = (s16)desc_w(fb[0]);
        FarPtr t = arena_bitmap_alloc(w, 0x29, 1, 2);
        Rect q = { w, 0x29, 0, 0, 0, 0 };
        drv_blit(DF(0x6CCC), t, &q, 1);
        drv_blit_masked(fb[0], t, &q, 1);
        drv_blit_masked(a, t, &q, 0);
        walker_anim(t, 0, 0, (s16)desc_w(t), 0x29, 0xC8, 0x44, 0xA3, 2);
    }
    arena_reset_stacks();
}
