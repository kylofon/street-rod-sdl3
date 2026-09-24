/* Drawing primitives of code segment 0f38: set-up, clipping, palette, rectangles and lines, page
 * copies, bitmaps (video.md §4.2, §4.8 - §4.10, table §4.9).
 *
 * Transcribed from the disassembly (tools/x86dis.py). Memory accesses to bitmaps go through vrd/vwr so
 * a descriptor pointing into video memory behaves as on the card (latches loaded by every CPU read),
 * and plain RAM bitmaps are mem[]. The driver is called through the drv_* wrappers (vector table
 * DS:78A2) wherever the original uses `lcall [DS:78A2+4k]`. */
#include <string.h>

#include "host.h"
#include "platform/ega.h"
#include "platform/platform.h"
#include "platform/vga.h"
#include "platform/video.h"

/* DGROUP globals without a symbols.h name. */
#define DS_screen_flag   0x004C   /* set to 1 by gfx_screen_mode(0) */
#define DS_status_str    0x49DE   /* near char*: status label shown by gfx_screen_mode(3) in the race */
#define DS_ui_cache_a    0x7644   /* gfx_init: words at 7644 - 2Ch*k, 7646 - 2Ch*k (k = 1..5) = -1 */
#define DS_ui_cache_b    0x7646
#define DS_ui_8266       0x8266
#define DS_ui_8268       0x8268
#define DS_page2         0x731E   /* g_page[2] */
#define DS_page3         0x7322   /* g_page[3] */
#define DS_race_flag     0x8ACA   /* DS_racing (u8) */
#define DS_composite_keep_mask 0x5AA6  /* u8: bitmap_composite_behind leaves the destination mask alone */

static inline u16 x_align(void) { return DSW(DS_g_x_align); }       /* FFF8h */
static inline u8  px_shift(void) { return (u8)DSW(DS_g_px_shift); } /* cl = byte [823A] */
static inline bool has_mask(FarPtr d) { FarPtr m = desc_mask(d); return m.off != 0 || m.seg != 0; }

/* ======================================================================== static descriptors */

/* The static page descriptors of segment 2e3e / 2e68 as the load image holds them (video.md §4.2):
 * page 0 A000:0000, page 1 A200:0000, 2e3e:0060 the CGA default (A000:2000, stride 50h, type FDh;
 * overwritten by gfx_init), 2e68:0000 zero. */
static const u8 static_desc[3][DESC_BYTES] = {
    { 0x40, 0x01, 0xC8, 0x00, 0x40, 0x1F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0x00, 0x00, 0x00, 0xA0, 0, 0, 0, 0, 0x28, 0x00, 0xFE, 0x00 },
    { 0x40, 0x01, 0xC8, 0x00, 0x40, 0x1F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0xFF, 0, 0, 0, 0, 0, 0x00, 0x00, 0x00, 0xA2, 0, 0, 0, 0, 0x28, 0x00, 0xFE, 0x00 },
    { 0x40, 0x01, 0xC8, 0x00, 0x00, 0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0xFF, 0, 0, 0, 0, 0, 0x00, 0x20, 0x00, 0xA0, 0, 0, 0, 0, 0x50, 0x00, 0xFD, 0x00 },
};

/* PORT: sanity check of the loaded image (like the TD2 port's table checks): the page descriptors the
 * EGA path relies on must hold the values above when gfx_init first runs. */
static void check_static_descriptors(void)
{
    static bool checked;
    if (checked) return;
    checked = true;
    for (int i = 0; i < 3; i++)
        if (memcmp(mp(SEG(0x2E3E), (u16)(i * DESC_BYTES)), static_desc[i], DESC_BYTES) != 0)
            host_fatal("gfx_init: static descriptor 2e3e:%04x differs from the expected image", i * DESC_BYTES);
    for (int k = 0; k < DESC_BYTES; k++)
        if (rd8(SEG(0x2E68), (u16)k) != 0)
            host_fatal("gfx_init: static descriptor 2e68:0000 is not zero");
}

/* 0f38:4bff (UI hot-spot / menu state reset, owned by no port module yet): clears 8 words.
 * PORT: transcribed here as a static helper because gfx_init is its only caller outside the UI. */
static void ui_state_reset_4bff(void)
{
    DSW(0x8BAC) = 0; DSW(0x8B8E) = 0; DSW(0x8BA6) = 0; DSW(0x8BAA) = 0;
    DSW(0x8B96) = 0; DSW(0x8B92) = 0; DSW(0x8B94) = 0; DSW(0x8B90) = 0;
}

/* ================================================================================= set-up (§4.8) */

/* 0f38:1859 — video.md §4.8: page descriptors, mode constants, colour scheme, page table. */
void gfx_init(FarPtr a, FarPtr b)
{
    check_static_descriptors();

    ds_far_wr(DS_g_front, a);
    ds_far_wr(DS_g_back, b);
    DSW(0x8234) = 0;
    DSW(0x8232) = 0;
    DSW(DS_g_mirror) = 1;
    DSS(DS_driver_id) = (s16)(s8)rd8(a.seg, (u16)(a.off + DESC_TYPE));
    s16 drv = DSS(DS_driver_id);
    if (drv == -6) {                                    /* Tandy (parked, constants kept) */
        DSW(DS_g_nplanes) = 1; DSW(DS_g_px_shift) = 1; DSW(DS_g_px_factor) = 4;
        DSW(DS_g_x_align) = 0xFFFE; DSW(DS_g_scheme) = 0x3A0;
    } else if (drv == -2) {
        DSW(DS_g_nplanes) = 4; DSW(DS_g_px_shift) = 3; DSW(DS_g_px_factor) = 1;
        DSW(DS_g_x_align) = 0xFFF8; DSW(DS_g_scheme) = 0x3A0;
        out(0x3D4, 0x0C); out(0x3D5, 0);
        out(0x3D4, 0x0D); out(0x3D5, 0);
    } else {                                            /* CGA / Hercules (parked, constants kept) */
        DSW(DS_g_nplanes) = 1; DSW(DS_g_px_shift) = 2; DSW(DS_g_px_factor) = 2;
        DSW(DS_g_x_align) = 0xFFFC; DSW(DS_g_scheme) = 0x3F0;
        wr8(a.seg, (u16)(a.off + DESC_TYPE), 0xFD);
    }
    /* colours 8242..8246 = scheme +5..+9 */
    memmove(mp(DGROUP, DS_g_text_fg), mp(DGROUP, (u16)(DSW(DS_g_scheme) + 5)), 5);
    DSW(DS_ui_8268) = 0xFFFF;
    DSW(DS_ui_8266) = 0xFFFF;
    u16 si = DS_ui_cache_a, di = DS_ui_cache_b;
    for (int k = 5; k > 0; k--) {
        si -= 0x2C; di -= 0x2C;
        DSW(di) = 0xFFFF;
        DSW(si) = 0xFFFF;
    }
    drv_pal_normal();                                   /* lcall [78C2] */
    ui_state_reset_4bff();
    DSB(DS_g_ui_level) = 1;
    FarPtr fa = ds_far(DS_g_front), fb = ds_far(DS_g_back);
    ds_far_wr(DS_g_page, fa);
    ds_far_wr((u16)(DS_g_page + 4), fb);
    ds_far_wr(DS_page2, far_make(SEG(0x2E3E), 0x60));
    if (DSS(DS_driver_id) == -2) {
        FarPtr p2 = ds_far(DS_page2);
        memmove(far_mp(p2), far_mp(fb), 0x30);          /* 18h words */
        far_wr(p2.seg, (u16)(p2.off + DESC_PLANES), far_make(0xA000, 0x4000));
        ds_far_wr(DS_page3, far_make(SEG(0x2E68), 0));
        FarPtr p3 = ds_far(DS_page3);
        memmove(far_mp(p3), far_mp(fb), 0x30);
        far_wr(p3.seg, (u16)(p3.off + DESC_PLANES), far_make(0xA000, 0x6000));
    }
}

static void crtc_start_zero(void)
{
    out(0x3D4, 0x0C); out(0x3D5, 0);
    out(0x3D4, 0x0D); out(0x3D5, 0);
}

/* 0f38:0dbd — video.md §4.8: 0 save the pages, 1/4 restore + split off + start 0, 2 start 0, 3 single
 * buffer. PORT: the return value is AX as far as it is defined by this function itself (cases 0, 2 and
 * out-of-range n); after a call to a void routine (cases 1, 3, 4) the port returns 0. No caller uses it. */
u16 gfx_screen_mode(s16 n)
{
    u16 ax;
    switch (n) {
    case 0: {
        FarPtr sa = ds_far(DS_g_saved_page_a);
        ax = (u16)(sa.off | sa.seg);
        if (ax == 0) {
            ds_far_wr(DS_g_saved_page_a, ds_far(DS_g_front));
            ds_far_wr(DS_g_saved_page_b, ds_far(DS_g_back));
            ax = DSW(DS_g_back);
        }
        DSW(DS_screen_flag) = 1;
        return ax;
    }
    case 1:
    case 4:
        ds_far_wr(DS_g_front, ds_far(DS_g_saved_page_a));
        ds_far_wr(DS_g_back, ds_far(DS_g_saved_page_b));
        if (DSS(DS_driver_id) == -2) {
            out(0x3D4, 0x13); out(0x3D5, 0x14);
            ega_set_split(-1);
            crtc_start_zero();
        }
        DSW(DS_g_mirror) = 1;
        if (n == 1) status_init(0xBE);
        else drv_pal_normal();                          /* lcall [78C2] */
        return 0;
    case 2:
        ax = 2;
        if (DSS(DS_driver_id) == -2) { crtc_start_zero(); ax = 0; }
        DSW(DS_g_mirror) = 1;
        return ax;
    case 3: {
        FarPtr sa = ds_far(DS_g_saved_page_a);
        ds_far_wr(DS_g_front, sa);
        ds_far_wr(DS_g_back, sa);
        DSW(DS_g_mirror) = 0;
        status_init(DSS(DS_driver_id) == -2 ? 0x5A : 0xBE);
        /* (the spec shows a second status_init here: the asm only calls status_label, then leaves) */
        if (DSB(DS_race_flag) != 0) status_label(ds_str(DSW(DS_status_str)));
        return 0;
    }
    default:
        return (u16)n;
    }
}

/* ================================================================================ helpers (§4.2) */

/* 0f38:1680 — video.md §4.2: pixel rect -> byte rect clipped to src and dst; 1 if non-empty. */
s16 clip_rect(FarPtr src, FarPtr dst, const Rect *in, Rect *o)
{
    u8 sh = px_shift();
    s16 sx = (s16)(in->sx >> sh), sy = in->sy;
    s16 w = (s16)(in->w >> sh), h = in->h;
    s16 sstride = (s16)desc_stride(src), sh_ = (s16)desc_h(src);
    if ((s16)(sx + w) > sstride) w = (s16)(sstride - sx);
    if ((s16)(sy + h) > sh_) h = (s16)(sh_ - sy);
    s16 dx = (s16)(in->dx >> sh), dy = in->dy;
    if (dy < 0) { h = (s16)(h + dy); sy = (s16)(sy - dy); dy = 0; }
    s16 dh = (s16)desc_h(dst);
    if (dh < (s16)(dy + h)) h = (s16)(dh - dy);
    if (dx < 0) { w = (s16)(w + dx); sx = (s16)(sx - dx); dx = 0; }
    s16 dstride = (s16)desc_stride(dst);
    if (dstride < (s16)(dx + w)) w = (s16)(dstride - dx);
    o->sx = sx; o->sy = sy; o->dx = dx; o->dy = dy; o->w = w; o->h = h;
    return (o->w > 0 && h > 0) ? 1 : 0;
}

/* 0f38:1640: rep stosb (n = 0: nothing). */
void far_memset(FarPtr p, u8 v, u16 n)
{
    if (n == 0) return;
    for (u16 i = 0; i < n; i++) vwr(p.seg, (u16)(p.off + i), v);
}

/* 0f38:b502: movedata(src.seg, src.off, dst.seg, dst.off, n) (1e16:1dc8). */
void far_memcpy(FarPtr src, FarPtr dst, u16 n)
{
    vmovedata(src.seg, src.off, dst.seg, dst.off, n);
}

/* 0f38:177d — video.md §4.3: n x (wait while in retrace, then until retrace). PORT: the Hercules
 * branch (DS:8236 == -4, port 3BAh bit 7) is dropped. */
void wait_vretraces(s16 n)
{
    for (s16 i = n; i > 0; i--) vga_wait_retrace_edge();
}

/* 0f38:17c8 — video.md §4.9: sum of the glyph advances (byte 0 of each 9-byte glyph), font DS:691A;
 * the index is `imul byte` (signed char * 9) - 120h. */
s16 font_string_width(const char *s)
{
    u16 w = 0;
    u16 fseg = DSW(DS_g_font_seg2);
    for (; *s; s++) {
        u16 bx = (u16)((s16)(s8)*s * 9);
        w = (u16)(w + rd8(fseg, (u16)(bx - 0x120)));
    }
    return (s16)w;
}

/* ================================================================================ palette (§4.10) */

/* 0f38:1f4b — video.md §4.10: v > 7 (signed byte) gets bit 4; shadow DS:0440[idx] (unchecked index);
 * wait until in vertical retrace; INT 10h AX=1000h (BL = idx, BH = v) or, for idx > 0Fh, AX=1001h
 * (overscan = BH). PORT: int86x replaced by ega_set_palette_reg (register 16 = overscan); an AX=1000h
 * call with BL > 0Fh (negative idx) is ignored like an invalid register. */
void ega_set_palreg(s16 idx, s16 v)
{
    u8 val = (u8)v;
    if ((s8)val > 7) val |= 0x10;
    DSB((u16)(DS_pal_shadow + idx)) = val;
    u8 al = idx > 0x0F ? 1 : 0;
    u8 bl = (u8)idx;
    vga_wait_in_retrace();
    if (al == 1) ega_set_palette_reg(16, val);
    else if (bl <= 0x0F) ega_set_palette_reg(bl, val);
}

/* 0f38:1fa4 — video.md §4.10: 16 values at DS:table (signed > 7 gets bit 4) + overscan DS:8248,
 * INT 10h AX=1002h. No retrace wait, no shadow update. PORT: ega_set_palette_reg 0..16. */
void ega_set_palette(u16 table)
{
    u8 t[17];
    for (int i = 0; i < 16; i++) {
        u8 c = DSB((u16)(table + i));
        t[i] = (s8)c > 7 ? (u8)(c | 0x10) : c;
    }
    t[16] = DSB(DS_g_overscan);
    for (int i = 0; i < 17; i++) ega_set_palette_reg(i, t[i]);
}

/* 0f38:0b2d (slot 7): black palette DS:0452, DS:8249 = 0. */
void pal_black(void)
{
    ega_set_palette(DS_pal_black_tab);
    DSB(DS_g_pal_visible) = 0;
}

/* 0f38:0b3f (slot 8): game palette DS:0440, DS:8249 = 1. */
void pal_normal(void)
{
    ega_set_palette(DS_pal_shadow);
    DSB(DS_g_pal_visible) = 1;
}

/* 0f38:17fc */
void pal_reg12_5(void) { ega_set_palreg(0x0C, 5); }

/* 0f38:180c */
void pal_reg12_13(void) { ega_set_palreg(0x0C, 0x0D); }

/* 0f38:181c: shadow only. */
void pal_shadow_patch(void)
{
    DSB(0x0446) = 9;
    DSB(0x0447) = 1;
    DSB(0x0448) = 3;
}

/* 0f38:182c: swap the 17 bytes DS:0440 <-> DS:0462. */
void pal_shadow_swap(void)
{
    for (u16 i = 0; i < 0x11; i++) {
        u8 t = DSB((u16)(DS_pal_shadow + i));
        DSB((u16)(DS_pal_shadow + i)) = DSB((u16)(DS_pal_alt_tab + i));
        DSB((u16)(DS_pal_alt_tab + i)) = t;
    }
}

/* ============================================================ rectangles, lines, frames (§4.9) */

/* 0f38:4302 — video.md §4.9: h horizontal lines through slot 12 in colour DS:8244 (sign-extended),
 * clipped to the descriptor's w/h; the line segment DS:787A (and DS:767C) = the descriptor's planes. */
void fill_rect(FarPtr d, s16 x, s16 y, s16 w, s16 h)
{
    s16 dw = (s16)desc_w(d);
    if ((s16)(x + w) > dw) w = (s16)(dw - x);
    s16 dh = (s16)desc_h(d);
    if ((s16)(y + h) > dh) h = (s16)(dh - y);
    u16 sseg = DSW(DS_g_draw_seg), soff = DSW(DS_g_draw_off);
    FarPtr pl = desc_planes(d);
    DSW(DS_g_draw_seg) = pl.seg;
    DSW(DS_g_draw_off) = pl.off;
    s16 c = (s16)(s8)DSB(DS_g_fill_col);
    s16 x1 = (s16)(x + w - 1);
    for (s16 i = 0; i < h; i++) {
        s16 yy = (s16)(y + i);
        drv_line(x, yy, x1, yy, c);
    }
    DSW(DS_g_draw_seg) = sseg;
    DSW(DS_g_draw_off) = soff;
}

/* 0f38:439c: vertical line in colour DS:8245, clipped to the descriptor's h. */
void vline(FarPtr d, s16 x, s16 y, s16 h)
{
    s16 dh = (s16)desc_h(d);
    if ((s16)(h + y) > dh) h = (s16)(dh - y);
    u16 sseg = DSW(DS_g_draw_seg), soff = DSW(DS_g_draw_off);
    FarPtr pl = desc_planes(d);
    DSW(DS_g_draw_seg) = pl.seg;
    DSW(DS_g_draw_off) = pl.off;
    drv_line(x, y, x, (s16)(y + h - 1), (s16)(s8)DSB(DS_g_line_col));
    DSW(DS_g_draw_seg) = sseg;
    DSW(DS_g_draw_off) = soff;
}

/* 0f38:43ff: fill_rect(d, x, y, w, 1) in the line colour. */
void hline(FarPtr d, s16 x, s16 y, s16 w)
{
    u8 s = DSB(DS_g_fill_col);
    DSB(DS_g_fill_col) = DSB(DS_g_line_col);
    fill_rect(d, x, y, w, 1);
    DSB(DS_g_fill_col) = s;
}

/* 0f38:4432 — video.md §4.9: 3-px frame outside the rect, style DS:8246. */
void draw_frame3(FarPtr d, s16 x, s16 y, s16 w, s16 h)
{
    const s16 k = 3;
    s16 xr = (s16)(x + w), xl = (s16)(x - k), yb = (s16)(y + h), yt = (s16)(y - k);
    s16 hh = (s16)(h + 2 * k), ww = (s16)(w + 2 * k);
    u8 sv = DSB(DS_g_fill_col);
    if (DSB(DS_g_frame_style) == 3) DSB(DS_g_fill_col) = DSB(DS_g_line_col);
    fill_rect(d, xl, yt, k, hh);
    fill_rect(d, x, yb, w, k);
    fill_rect(d, xr, yt, k, hh);
    fill_rect(d, x, yt, w, k);
    if (DSB(DS_g_frame_style) == 3) { DSB(DS_g_fill_col) = sv; return; }
    s16 si = (s16)(xl + 1), di = (s16)(yt + 1);
    s16 h4 = (s16)(hh - 2), w4 = (s16)(ww - 2);
    vline(d, si, di, h4);
    hline(d, si, (s16)(yb + 1), w4);
    vline(d, (s16)(xr + 1), di, h4);
    hline(d, si, di, w4);
    if (DSB(DS_g_frame_style) == 2) {
        si = (s16)(xl + 2); di = (s16)(yt + 2);
        s16 h2 = (s16)(hh - 4), w2 = (s16)(ww - 4);
        vline(d, si, di, h2);
        hline(d, si, yb, w2);
        vline(d, xr, di, h2);
        hline(d, si, di, w2);
    }
}

/* 0f38:45d7: 1-px outline outside the rect in the line colour. */
void draw_box1(FarPtr d, s16 x, s16 y, s16 w, s16 h)
{
    s16 xl = (s16)(x - 1), xr = (s16)(x + w), yt = (s16)(y - 1), yb = (s16)(y + h);
    s16 hh = (s16)(h + 2);
    vline(d, xl, yt, hh);
    hline(d, x, yb, w);
    vline(d, xr, yt, hh);
    hline(d, x, yt, w);
}

/* 0f38:b5aa — video.md §4.9: inclusive, byte-aligned rect copy through slot 0 (flags 1); x0/x1 >= 0,
 * x1 <= 13Fh, y1 <= C7h (y0 not clamped). */
void page_copy_rect(FarPtr s, FarPtr d, s16 x0, s16 y0, s16 x1, s16 y1)
{
    if (x0 < 0) x0 = 0;
    if (x1 < 0) x1 = 0;
    if (x1 > 0x13F) x1 = 0x13F;
    if (y1 > 0xC7) y1 = 0xC7;
    u16 m = x_align();
    Rect r;
    r.w = (s16)((((u16)x1 + (u16)~m + 1) & m) - ((u16)x0 & m));
    r.h = (s16)(y1 - y0 + 1);
    r.sx = x0; r.sy = y0;
    r.dx = x0; r.dy = y0;
    drv_blit(s, d, &r, 1);
}

/* ======================================================================================= bitmaps */

/* 0f38:9fce — video.md §4.9: header + colour planes (unless flags & 2) + mask (flags != 0) from the
 * work arena, then bitmap_alloc(w, h, flags, -1, d) fills the header. */
FarPtr arena_bitmap_alloc(s16 w, s16 h, s16 flags, s16 pool)
{
    s16 bpr = (s16)(((s16)(w - 1) >> px_shift()) + 1);
    u16 plane = (u16)(bpr * h);
    u16 size = (u16)((flags ? plane : 0) + 0x30);
    if (!(flags & 2)) size = (u16)(size + (u16)(plane * DSW(DS_g_nplanes)));
    FarPtr d = arena_alloc((s16)size, pool);
    far_wr(d.seg, (u16)(d.off + DESC_PLANES), far_make(d.seg, (u16)(d.off + 0x30)));
    bitmap_alloc(w, h, flags, -1, d);
    return d;
}

/* 0f38:b31f: the whole of bitmap s to d at (dx, dy) through slot 0. NB: the destination is the first
 * argument (the header had them the other way round). */
void bitmap_blit_at(FarPtr d, FarPtr s, s16 dx, s16 dy, s16 flags)
{
    Rect r;
    r.w = (s16)desc_w(s);
    r.h = (s16)desc_h(s);
    r.sy = 0; r.sx = 0;
    r.dx = dx; r.dy = dy;
    drv_blit(s, d, &r, (u8)flags);
}

/* 0f38:b51e: new arena bitmap (with a mask if s has one) holding the sub-rect r = {w, h, x, y} of s,
 * copied through slot 0 with flags 3. */
FarPtr bitmap_extract(FarPtr s, const s16 *r, s16 pool)
{
    s16 w = r[0], h = r[1], x = r[2], y = r[3];
    FarPtr b = arena_bitmap_alloc(w, h, has_mask(s) ? 1 : 0, pool);
    Rect rr;
    rr.w = w; rr.h = h; rr.sx = x; rr.sy = y;
    rr.dy = 0; rr.dx = 0;
    drv_blit(s, b, &rr, 3);
    return b;
}

/* 0f38:ab50: d re-initialised with s's size (mask if d has one), then the whole of s copied. */
void bitmap_copy_into(FarPtr s, FarPtr d, s16 flags)
{
    s16 w = (s16)desc_w(s);
    bitmap_alloc(w, (s16)desc_h(s), has_mask(d) ? 1 : 0, -1, d);
    Rect r;
    r.w = w;
    r.h = (s16)desc_h(s);
    r.sy = 0; r.sx = 0; r.dy = 0; r.dx = 0;
    drv_blit(s, d, &r, (u8)flags);
}

/* 0f38:abc2: *left / *right = first / last column that is opaque in some row (from the AND of the
 * first and last mask byte of every row; at most 8 transparent pixels are counted on each side). */
void bitmap_opaque_extent(FarPtr b, s16 *left, s16 *right)
{
    u16 stride = desc_stride(b);
    s16 h = (s16)desc_h(b);
    FarPtr m = desc_mask(b);
    FarPtr ml = far_add(m, (u16)(stride - 1));
    u8 accl = 0xFF, accr = 0xFF;
    if (h > 0) {
        for (u16 cx = (u16)h; cx; cx--) {
            accl &= vrdp(m);
            accr &= vrdp(ml);
            m.off = (u16)(m.off + stride);
            ml.off = (u16)(ml.off + stride);
        }
    }
    s16 factor = (s16)DSW(DS_g_px_factor);
    s16 cx = 0;
    u8 dl = 0x80;
    while (accl & dl) {
        cx++;
        dl >>= 1;
        if (cx >= 8) break;
    }
    *left = idiv32_16(cx, factor, NULL);
    cx = 0;
    dl = 1;
    while (accr & dl) {
        cx++;
        dl = (u8)(dl << 1);
        if (cx >= 8) break;
    }
    s16 q = idiv32_16(cx, factor, NULL);
    *right = (s16)((u16)(desc_stride(b) << px_shift()) - 1 - (u16)q);
}

/* 0f38:acbf — video.md §4.9: d = s shifted right by (shift * DS:823C) & 7 pixels; the width grows by
 * 8 when the last mask column is not transparent in the shifted-out bits; the mask is shifted in with
 * 1s (transparent). shift <= 0: plain copy into d. Returns the descriptor of bitmap_alloc (= d). */
FarPtr bitmap_make_shifted(FarPtr s, s16 shift, FarPtr d)
{
    u8 sh7 = (u8)((u16)(shift * (s16)DSW(DS_g_px_factor)) & 7);
    u8 inv = (u8)(8 - sh7);
    u16 lowmask = (u16)(0xFF >> inv);
    s16 h = (s16)desc_h(s);
    u16 stride = desc_stride(s);

    if (shift <= 0) {
        bitmap_copy_into(s, d, has_mask(s) ? 3 : 1);
        return d;
    }

    u8 acc = 0xFF;
    FarPtr ml = far_add(desc_mask(s), (u16)(stride - 1));
    if (h > 0) {
        for (u16 cx = (u16)h; cx; cx--) {
            acc &= vrdp(ml);
            ml.off = (u16)(ml.off + stride);
        }
    }
    u16 extra = ((acc & lowmask) == lowmask) ? 0 : (u16)(1u << px_shift());
    FarPtr nd = bitmap_alloc((s16)(desc_w(s) + extra), (s16)desc_h(s), has_mask(s) ? 1 : 0, -1, d);

    FarPtr dp = desc_planes(nd);
    FarPtr sp = desc_planes(s);
    s16 rows = (s16)(h * (s16)DSW(DS_g_nplanes));
    if (rows > 0) {
        for (u16 r = (u16)rows; r; r--) {
            u8 carry = 0;
            if ((s16)stride > 0) {
                for (u16 n = stride; n; n--) {
                    u8 b = vrdp(sp);
                    vwrp(dp, (u8)((b >> sh7) | (u8)((unsigned)carry << inv)));
                    dp.off++;
                    carry = (u8)(lowmask & b);
                    sp.off++;
                }
            }
            if (extra) {
                vwrp(dp, (u8)((unsigned)carry << inv));
                dp.off++;
            }
        }
    }

    if (!has_mask(s)) return nd;
    FarPtr dm = desc_mask(nd);
    FarPtr sm = desc_mask(s);
    if (h <= 0) return nd;
    u8 fill = (u8)(0xFF >> sh7);
    for (u16 r = (u16)h; r; r--) {
        u8 carry = 0xFF;
        if ((s16)stride > 0) {
            for (u16 n = stride; n; n--) {
                u8 b = vrdp(sm);
                vwrp(dm, (u8)((b >> sh7) | (u8)((unsigned)carry << inv)));
                dm.off++;
                carry = (u8)(lowmask & b);
                sm.off++;
            }
        }
        if (extra) {
            vwrp(dm, (u8)((u8)((unsigned)carry << inv) | fill));
            dm.off++;
        }
    }
    return nd;
}

/* 0f38:af50 — video.md §4.9: transparent blit (slot 2, mode 1) of s at r (pixel x); when r->dx & 7 a
 * shifted copy is made first in arena pool 2 (released at the end). and_mask != 0: the clipped part of
 * the source mask is also ANDed into d's mask. */
void bitmap_blit_masked_shift(FarPtr s, FarPtr d, const Rect *r, s16 and_mask)
{
    Rect lr = *r;                        /* the original copies 20 bytes (0Ah words); only 12 are used */
    s16 shift = (s16)((u16)r->dx & (u16)~x_align());
    FarPtr src;
    if (shift > 0) {
        FarPtr t = arena_bitmap_alloc((s16)(desc_w(s) + 8), (s16)desc_h(s), has_mask(s) ? 1 : 0, 2);
        bitmap_make_shifted(s, shift, t);
        src = t;
        lr.w = (s16)desc_w(t);
    } else {
        src = s;
    }
    drv_blit_masked(src, d, &lr, 1);
    if (and_mask != 0) {
        Rect o;
        if (clip_rect(src, d, &lr, &o)) {
            u16 sstride = desc_stride(src);
            s16 w = o.w, h = o.h;
            u16 sskip = (u16)(sstride - (u16)w);
            u16 dskip = (u16)(desc_stride(d) - (u16)w);
            FarPtr sm = desc_mask(src);
            sm.off = (u16)(sm.off + (u16)(o.sy * (s16)sstride) + (u16)o.sx);
            FarPtr dm = desc_mask(d);
            dm.off = (u16)(dm.off + (u16)((s16)desc_stride(d) * o.dy) + (u16)o.dx);
            if (h > 0) {
                for (u16 rr = (u16)h; rr; rr--) {
                    if (w > 0) {
                        for (u16 n = (u16)w; n; n--) {
                            u8 a = vrdp(sm);
                            vwrp(dm, (u8)(vrdp(dm) & a));
                            dm.off++;
                            sm.off++;
                        }
                    }
                    sm.off = (u16)(sm.off + sskip);
                    dm.off = (u16)(dm.off + dskip);
                }
            }
        }
    }
    if (shift != 0) arena_pop(1);
}

/* 0f38:b0f2 — video.md §4.9: draws s behind what d already holds at (x, y): per plane
 * d |= ~smask & s & dmask, then (unless DS:5AA6) dmask &= smask. No clipping. A shifted copy is made
 * in arena pool 2 when x & 7. */
void bitmap_composite_behind(FarPtr s, FarPtr d, s16 x, s16 y)
{
    s16 shift = (s16)((u16)x & (u16)~x_align());
    FarPtr src;
    if (shift > 0) {
        FarPtr t = arena_bitmap_alloc((s16)(desc_w(s) + 8), (s16)desc_h(s), has_mask(s) ? 1 : 0, 2);
        bitmap_make_shifted(s, shift, t);
        src = t;
    } else {
        src = s;
    }
    u16 dstride = desc_stride(d);
    u16 sstride = desc_stride(src);
    u16 skip = (u16)(dstride - sstride);
    u16 plane_skip = (u16)((s16)(desc_h(d) - desc_h(src)) * (s16)dstride);
    s16 sh = (s16)desc_h(src);
    u16 off = (u16)((u16)(y * (s16)dstride) + (u16)(x >> px_shift()));
    FarPtr dp = far_add(desc_planes(d), off);
    FarPtr dm0 = far_add(desc_mask(d), off), dm = dm0;
    FarPtr sp = desc_planes(src);
    FarPtr sm0 = desc_mask(src), sm = sm0;

    if ((s16)DSW(DS_g_nplanes) > 0) {
        for (u16 p = DSW(DS_g_nplanes); p; p--) {
            if (sh > 0) {
                for (u16 rr = (u16)sh; rr; rr--) {
                    if ((s16)sstride > 0) {
                        for (u16 n = sstride; n; n--) {
                            u8 al = (u8)~vrdp(sm);
                            al &= vrdp(sp);
                            al &= vrdp(dm);
                            vwrp(dp, (u8)(vrdp(dp) | al));
                            dm.off++; sp.off++; sm.off++; dp.off++;
                        }
                    }
                    dp.off = (u16)(dp.off + skip);
                    dm.off = (u16)(dm.off + skip);
                }
            }
            dp.off = (u16)(dp.off + plane_skip);
            dm = dm0;
            sm = sm0;
        }
    }
    if (DSB(DS_composite_keep_mask) == 0) {
        dm = dm0;
        sm = sm0;
        if (sh > 0) {
            for (u16 rr = (u16)sh; rr; rr--) {
                if ((s16)sstride > 0) {
                    for (u16 n = sstride; n; n--) {
                        u8 a = vrdp(sm);
                        vwrp(dm, (u8)(vrdp(dm) & a));
                        sm.off++; dm.off++;
                    }
                }
                dm.off = (u16)(dm.off + skip);
            }
        }
    }
    if (shift != 0) arena_pop(1);
}

/* 0f38:b360: picture id (arena pool 2) to d at (x, y) with slot 0 flags 3; DS:70A8 = the picture's
 * descriptor (left dangling after the arena_pop, as in the original). */
void pic_blit_at(FarPtr d, s16 id, u8 x, u8 y)
{
    FarPtr b = pic_get(id, 2);
    bitmap_blit_at(d, b, x, y, 3);
    ds_far_wr(0x70A8, b);
    arena_pop(1);
}

/* 0f38:b3b4: picture id drawn transparently at (x, y) with bitmap_blit_masked_shift(.., flag). */
void pic_draw_masked(FarPtr d, s16 id, u8 x, u8 y, s16 flag)
{
    FarPtr b = pic_get(id, 2);
    Rect r;
    r.w = (s16)desc_w(b);
    r.h = (s16)desc_h(b);
    r.sy = 0; r.sx = 0;
    r.dx = x; r.dy = y;
    bitmap_blit_masked_shift(b, d, &r, flag);
    arena_pop(1);
}

/* 0f38:b41b: d.mask &= s.mask over the rect r (clipped with clip_rect, byte columns). */
void mask_and_rect(FarPtr s, FarPtr d, const Rect *r)
{
    Rect o;
    if (!clip_rect(s, d, r, &o)) return;
    s16 w = o.w, h = o.h;
    u16 sskip = (u16)(desc_stride(s) - (u16)w);
    u16 dskip = (u16)(desc_stride(d) - (u16)w);
    FarPtr sp = desc_mask(s);
    sp.off = (u16)(sp.off + (u16)((s16)desc_stride(s) * o.sy) + (u16)o.sx);
    FarPtr dp = desc_mask(d);
    dp.off = (u16)(dp.off + (u16)((s16)desc_stride(d) * o.dy) + (u16)o.dx);
    if (h <= 0) return;
    for (u16 rr = (u16)h; rr; rr--) {
        if (w > 0) {
            for (u16 n = (u16)w; n; n--) {
                u8 a = vrdp(sp);
                vwrp(dp, (u8)(vrdp(dp) & a));
                dp.off++;
                sp.off++;
            }
        }
        sp.off = (u16)(sp.off + sskip);
        dp.off = (u16)(dp.off + dskip);
    }
}
