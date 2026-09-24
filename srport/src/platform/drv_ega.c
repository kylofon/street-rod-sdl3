/* EGA/VGA driver, code segment 21a0 (video.md §1.2, §4.3 - §4.7), and the driver vector wrappers.
 *
 * Every routine is transcribed from the disassembly (tools/x86dis.py work/SR_unp.exe dis 21a0:xxxx)
 * on top of the VGA register model (vga.h): port writes go through out()/outw(), video memory through
 * vrd()/vwr() (RAM bitmaps too: vrd/vwr fall through to mem[] outside segments A000h-AFFFh), so the
 * latches, bit mask, map mask and write modes behave as on the card and the planes come out
 * bit-exact. Register quirks of the original (8-bit muls, 16-bit counters that wrap, reads before
 * masked writes, registers left set) are kept.
 *
 * PORT: the Tandy half of segment 21a0 (00ac-0d62, install 0952) is not ported: the port is the VGA
 * path (DS:8236 = -2, DS:0254 = 1). */
#include "codeptr.h"
#include "host.h"
#include "platform/platform.h"
#include "platform/vga.h"
#include "platform/video.h"

/* DGROUP globals without a symbols.h name. */
#define DS_tile_x 0x539A   /* s16 pairs {x, w} at 539A + 4i (6 entries): jail bars for anim_step */
#define DS_tile_w 0x539C

static inline u8 ror8(u8 v, unsigned n) { n &= 7; return (u8)(v >> n | v << ((8 - n) & 7)); }
static inline u16 ror16(u16 v, unsigned n) { n &= 15; return n ? (u16)(v >> n | v << (16 - n)) : v; }
/* Byte shifts by CL as a 286+ executes them (count masked to 5 bits, >= 8 shifts everything out). */
static inline u8 shr8(u8 v, u8 cl) { cl &= 31; return cl >= 8 ? 0 : (u8)(v >> cl); }
static inline u8 shl8(u8 v, u8 cl) { cl &= 31; return cl >= 8 ? 0 : (u8)(v << cl); }

/* ================================================================== driver vector table (§1.2) */

static CodeFn drv_slot(int k) { return codeptr_lookup(ds_far((u16)(DS_drv_vec + 4 * k))); }

typedef u16  (*FnBlit)(FarPtr, FarPtr, const Rect *, u8);
typedef void (*FnBlitShifted)(FarPtr, FarPtr, const Rect *);
typedef s16  (*FnBlitMasked)(FarPtr, FarPtr, const Rect *, s16);
typedef void (*FnDrawText)(FarPtr, s16, s16, const char *);
typedef void (*FnDrawCursor)(FarPtr, FarPtr, FarPtr, FarPtr, s16, s16, s16, s16);
typedef s16  (*FnCopyPage)(FarPtr, FarPtr);
typedef void (*FnRecolour)(FarPtr, s16, s16, s16, s16, s16, s16);
typedef void (*FnVoid)(void);
typedef s16  (*FnAnimStep)(u16);
typedef s16  (*FnBlinds)(FarPtr, FarPtr);
typedef void (*FnSlide)(s16, s16, s16, s16, s16, FarPtr);
typedef void (*FnLine)(s16, s16, s16, s16, s16);
typedef void (*FnHspan)(s16, s16, s16, s16);
typedef void (*FnFillRows)(s16, s16);

u16  drv_blit(FarPtr src, FarPtr dst, const Rect *r, u8 flags) { return ((FnBlit)drv_slot(0))(src, dst, r, flags); }
void drv_blit_shifted(FarPtr src, FarPtr dst, const Rect *r) { ((FnBlitShifted)drv_slot(1))(src, dst, r); }
s16  drv_blit_masked(FarPtr src, FarPtr dst, const Rect *r, s16 mode) { return ((FnBlitMasked)drv_slot(2))(src, dst, r, mode); }
void drv_draw_text(FarPtr d, s16 x, s16 y, const char *s) { ((FnDrawText)drv_slot(3))(d, x, y, s); }
void drv_draw_cursor(FarPtr img, FarPtr mask, FarPtr save, FarPtr scr, s16 xb, s16 y, s16 wb, s16 rows)
{
    ((FnDrawCursor)drv_slot(4))(img, mask, save, scr, xb, y, wb, rows);
}
s16  drv_copy_page(FarPtr src, FarPtr dst) { return ((FnCopyPage)drv_slot(5))(src, dst); }
void drv_recolour_rect(FarPtr d, s16 from, s16 to, s16 x0, s16 y0, s16 x1, s16 y1)
{
    ((FnRecolour)drv_slot(6))(d, from, to, x0, y0, x1, y1);
}
void drv_pal_black(void) { ((FnVoid)drv_slot(7))(); }
void drv_pal_normal(void) { ((FnVoid)drv_slot(8))(); }
s16  drv_anim_step(u16 anim) { return ((FnAnimStep)drv_slot(9))(anim); }
s16  drv_blinds(FarPtr dst, FarPtr src) { return ((FnBlinds)drv_slot(10))(dst, src); }
void drv_slide_sprite(s16 top, s16 bottom, s16 x, s16 from, s16 to, FarPtr spr)
{
    ((FnSlide)drv_slot(11))(top, bottom, x, from, to, spr);
}
void drv_line(s16 x0, s16 y0, s16 x1, s16 y1, s16 colour) { ((FnLine)drv_slot(12))(x0, y0, x1, y1, colour); }
void drv_hspan(s16 x0, s16 x1, s16 y, s16 colour) { ((FnHspan)drv_slot(13))(x0, x1, y, colour); }
void drv_fill_rows(s16 y, s16 colour) { ((FnFillRows)drv_slot(14))(y, colour); }

/* The 15 targets of the EGA template DS:5BD0 (checked against the loaded image: the far pointers
 * stored there are exactly these addresses). */
void video_register_codeptrs(void)
{
    codeptr_register(FN_ega_blit,         (CodeFn)ega_blit);           /* slot 0  21a0:1862 */
    codeptr_register(FN_ega_blit_shifted, (CodeFn)ega_blit_shifted);   /* slot 1  21a0:223b */
    codeptr_register(FN_ega_blit_masked,  (CodeFn)ega_blit_masked);    /* slot 2  21a0:1cd5 */
    codeptr_register(FN_ega_draw_text,    (CodeFn)ega_draw_text);      /* slot 3  21a0:113a */
    codeptr_register(FN_ega_draw_cursor,  (CodeFn)ega_draw_cursor);    /* slot 4  0e6c:0175 */
    codeptr_register(FN_ega_copy_page,    (CodeFn)ega_copy_page);      /* slot 5  21a0:16dc */
    codeptr_register(FN_ega_recolour_rect,(CodeFn)ega_recolour_rect);  /* slot 6  21a0:1508 */
    codeptr_register(FN_pal_black,        (CodeFn)pal_black);          /* slot 7  0f38:0b2d */
    codeptr_register(FN_pal_normal,       (CodeFn)pal_normal);         /* slot 8  0f38:0b3f */
    codeptr_register(FN_ega_anim_step,    (CodeFn)ega_anim_step);      /* slot 9  21a0:25d9 */
    codeptr_register(FN_ega_blinds,       (CodeFn)ega_blinds);         /* slot 10 21a0:1722 */
    codeptr_register(FN_ega_slide_sprite, (CodeFn)ega_slide_sprite);   /* slot 11 21a0:28ec */
    codeptr_register(FN_ega_line,         (CodeFn)ega_line);           /* slot 12 21a0:23bc */
    codeptr_register(FN_ega_hline,        (CodeFn)ega_hspan);          /* slot 13 2645:0034 */
    codeptr_register(FN_ega_fill_rows,    (CodeFn)ega_fill_rows);      /* slot 14 2645:00e7 */
}

/* ============================================================= CRTC, retrace waits (§4.3) */

/* 21a0:0006 */
void ega_wait_vretrace(void)
{
    vga_wait_blank_then_retrace();           /* while (in(3DAh) & 1) ; while (!(in(3DAh) & 8)) ; */
}

/* 21a0:0014 — video.md §4.3 */
void ega_set_split(s16 line)
{
    u16 crtc = 0x3D4;                        /* PORT: BIOS CRTC base 0040:0063, always 3D4h here */
    vga_wait_retrace_edge();                 /* in(crtc + 6): wait for the start of a retrace */
    u16 ax = (u16)line;
    if (ax == 0xFFFF) ax = DSW(DS_g_vga) ? 0x3FF : 0x1FF;
    if (DSW(DS_g_vga) == 0) {                /* EGA: overflow register written blind */
        u8 bh = (u8)((ax >> 8 & 1) << 4);
        outw(crtc, (u16)((ax & 0xFF) << 8 | 0x18));
        outw(crtc, (u16)(((0x11 & 0xEF) | bh) << 8 | 0x07));
        return;
    }
    if ((s16)ax <= 0xC8) ax = (u16)(ax + 0x64);   /* VGA, 400 scan lines */
    u8 bl = (u8)((ax >> 8 & 1) << 4);            /* line bit 8 -> CRTC 07h bit 4 */
    u8 bh = (u8)((ax >> 9 & 1) << 6);            /* line bit 9 -> CRTC 09h bit 6 */
    outw(crtc, (u16)((ax & 0xFF) << 8 | 0x18));
    out(crtc, 7);
    u8 r = in((u16)(crtc + 1));
    outw(crtc, (u16)(((r & 0xEF) | bl) << 8 | 7));
    out(crtc, 9);
    r = in((u16)(crtc + 1));
    outw(crtc, (u16)(((r & 0xBF) | bh) << 8 | 9));
}

/* 21a0:1128: copy the 15 far pointers DS:5BD0 -> DS:78A2 (rep movsw, 1Eh words) */
void ega_install_vectors(void)
{
    for (u16 i = 0; i < 0x3C; i += 2) DSW(DS_drv_vec + i) = DSW(DS_g_vec_ega + i);
}

/* 21a0:182e — video.md §4.3 */
void ega_show_page(s16 page)
{
    out(0x3D4, 0x0C);
    out(0x3D5, (u8)((u16)page << 5));        /* start = page * 2000h */
    out(0x3D4, 0x0D);
    out(0x3D5, 0);
    vga_wait_retrace_edge();                 /* while (in(3DAh) & 8) ; while (!(in(3DAh) & 8)) ; */
}

/* ========================================================================= text (§4.5) */

#define TEXT_BUF 0x140                       /* 8 rows x 40 bytes at [bp-14Ah] */

/* Buffer byte; PORT: the original reads its stack frame past the buffer (rows > 8 only happen for
 * y > d->h, where the row counter wraps); the port reads 0 there. */
static inline u8 text_buf(const u8 *buf, unsigned i) { return i < TEXT_BUF ? buf[i] : 0; }

/* 21a0:113a (slot 3) — video.md §4.5 */
void ega_draw_text(FarPtr d, s16 x, s16 y, const char *s)
{
    u8 buf[TEXT_BUF + 48];                   /* +48: writes one row past the end stay inside (see below) */
    s16 xs = (s16)(x & 7);                   /* [bp-154h] */
    u8 lmask = 0xFF, rmask = 0xFF;           /* [bp-156h], [bp-170h] */
    for (unsigned i = 0; i < sizeof buf; i++) buf[i] = 0;
    s16 bit = xs, width = xs;                /* [bp-168h], [bp-6] */
    unsigned bp = 0;                         /* [bp-4]: current byte of row 0 */
    u16 fseg = DSW(DS_g_font_seg);
    while (*s) {
        u8 c = (u8)(*s++ - 0x20);
        if (c > 0x5A) c = 0;
        u16 g = (u16)(c * 9);                /* glyph: advance, 8 rows */
        s16 w = rd8(fseg, g);
        width = (s16)(width + w);
        if (width > 0x140) { width = (s16)(width - w); break; }
        s16 nb = (s16)(bit + w);
        if (nb <= 8) {
            for (int r = 0; r < 8; r++) buf[bp + 40 * r] |= (u8)(rd8(fseg, (u16)(g + 1 + r)) >> (bit & 15));
            bit = nb;
            if (bit == 8) { bp++; buf[bp] = 0; bit = 0; }   /* bp = 40 zeroes row 1 byte 0 (as the original) */
        } else {
            buf[bp + 1] = 0;
            for (int r = 0; r < 8; r++) {    /* ror ax,cl; or word [row], ax */
                u16 v = ror16(rd8(fseg, (u16)(g + 1 + r)), (unsigned)bit);
                buf[bp + 40 * r] |= (u8)v;
                buf[bp + 40 * r + 1] |= (u8)(v >> 8);
            }
            bit = (s16)(nb & 7);
            bp++;
        }
    }
    s16 dw = (s16)desc_w(d);
    if ((s16)(x + width - xs) > dw) width = (s16)(dw - x);     /* (sic) xs dropped */
    s16 a0 = (s16)((xs + 7) & 0xFFF8);       /* [bp-14Ch] */
    s16 a1 = (s16)(width & 0xFFF8);          /* [bp-16Eh] */
    lmask = shr8(lmask, (u8)((u8)xs - (u8)a0 + 8));
    rmask = shl8(rmask, (u8)((u8)a1 - (u8)width + 8));
    s16 nmid = a0 < a1 ? (s16)((a1 - a0) >> 3) : 0;          /* [bp-16Ah] */
    if (nmid == 0 && xs >= a1) {
        u8 m = lmask & rmask;
        if (m) { lmask = m; rmask = 0; }
    }
    s16 rows = 8;                            /* [bp-158h] */
    if ((s16)(y + 8) > (s16)desc_h(d)) rows = (s16)(desc_h(d) - y);
    DSW(DS_g_text_end_x) = (u16)(x + width - xs);
    DSW(DS_g_text_y) = (u16)y;
    out(0x3CE, 5); out(0x3CF, 2);            /* write mode 2 */
    out(0x3C4, 2); out(0x3C5, 0x0F);
    out(0x3CE, 8); out(0x3CF, 0xFF);         /* GC index stays 8 from here on */
    u8 fg = DSB(DS_g_text_fg), bg = DSB(DS_g_text_bg);
    FarPtr pl = desc_planes(d);
    u16 stride = desc_stride(d);
    u16 row = (u16)(pl.off + (u16)(stride * (u16)y) + (x >> 3)), seg = pl.seg;
    unsigned src = 0;
    for (u16 n = (u16)rows; n != 0; n--, row = (u16)(row + stride), src += 40) {
        u16 o = row;
        unsigned q = src;
        if (lmask) {
            vrd(seg, o); out(0x3CF, lmask); vwr(seg, o, bg);
            vrd(seg, o); out(0x3CF, text_buf(buf, q++)); vwr(seg, o, fg);
            o++;
        }
        for (u16 k = (u16)nmid; k != 0; k--) {
            out(0x3CF, 0xFF); vwr(seg, o, bg);  /* no read: latches of the previous byte, mask FFh */
            vrd(seg, o); out(0x3CF, text_buf(buf, q++)); vwr(seg, o, fg);
            o++;
        }
        if (rmask) {
            vrd(seg, o); out(0x3CF, rmask); vwr(seg, o, bg);
            vrd(seg, o); out(0x3CF, text_buf(buf, q++)); vwr(seg, o, fg);
        }
    }
    out(0x3CE, 8); out(0x3CF, 0xFF);
    out(0x3CE, 5); out(0x3CF, 0);
}

/* ============================================================== recolour (§4.6) */

/* 21a0:1508 (slot 6) — video.md §4.6 */
void ega_recolour_rect(FarPtr d, s16 from, s16 to, s16 x0, s16 y0, s16 x1, s16 y1)
{
    u8 lm = 0xFF, rm = 0xFF;                 /* [bp-0Eh], [bp-20h] */
    s16 a0 = (s16)((x0 + 7) & 0xFFF8);       /* [bp-0Ah] */
    s16 a1 = (s16)(x1 & 0xFFF8);             /* [bp-1Eh] */
    s16 w = (s16)desc_w(d);
    if (x1 > w) { a1 = w; x1 = w; }
    if ((s16)desc_h(d) < y1) y1 = (s16)desc_h(d);
    lm = shr8(lm, (u8)((u8)x0 - (u8)a0 + 8));
    rm = shl8(rm, (u8)((u8)a1 - (u8)x1 + 8));
    s16 nmid = a0 < a1 ? (s16)((a1 - a0) >> 3) : 0;
    if (nmid == 0 && x0 >= a1) {
        u8 m = lm & rm;
        if (m) { lm = m; rm = 0; }
    }
    out(0x3CE, 5); out(0x3CF, 0x0A);         /* write mode 2, read mode 1 (colour compare) */
    out(0x3CE, 2); out(0x3CF, (u8)from);     /* GC2 colour compare; GC7 = 0Fh (BIOS) */
    out(0x3C4, 2); out(0x3C5, 0x0F);
    out(0x3CE, 8); out(0x3CF, 0xFF);         /* GC index stays 8 */
    FarPtr pl = desc_planes(d);
    u16 stride = desc_stride(d);
    u16 row = (u16)((u16)(stride * (u16)y0) + pl.off + (x0 >> 3)), seg = pl.seg;
    for (u16 rows = (u16)(y1 - y0); rows != 0; rows--, row = (u16)(row + stride)) {  /* y1 < y0 wraps */
        u16 o = row;
        u8 b;
        if (lm) {
            b = vrd(seg, o) & lm;
            if (b) { out(0x3CF, b); vwr(seg, o, (u8)to); }
            o++;
        }
        for (u16 n = (u16)nmid; n != 0; n--, o++) {
            b = vrd(seg, o);
            if (b) { out(0x3CF, b); vwr(seg, o, (u8)to); }
        }
        if (rm) {
            b = vrd(seg, o) & rm;
            if (b) { out(0x3CF, b); vwr(seg, o, (u8)to); }
        }
    }
    out(0x3CE, 8); out(0x3CF, 0xFF);
    out(0x3CE, 5); out(0x3CF, 0);
}

/* =========================================================== page copies (§4.4) */

/* 21a0:16dc (slot 5): latched copy of 8000 bytes (all four planes) */
s16 ega_copy_page(FarPtr src, FarPtr dst)
{
    out(0x3CE, 5); out(0x3CF, 1);
    out(0x3C4, 2); out(0x3C5, 0x0F);
    vmovedata(src.seg, src.off, dst.seg, dst.off, 0x1F40);
    out(0x3CE, 5); out(0x3CF, 0);
    return 0;
}

/* 21a0:1722 (slot 10): 5 interleaved passes (rows 0,5,10.. then 2,7.. 4.. 1.. 3..), each row cleared
 * in write mode 2 then latch-copied; >= 3 ticks per pass (deadline taken before the pass). */
s16 ega_blinds(FarPtr dst, FarPtr src)
{
    out(0x3C4, 2); out(0x3C5, 0x0F);
    out(0x3CE, 8); out(0x3CF, 0xFF);
    for (s16 pass = 0; pass < 5; pass++) {
        u32 until = ticks_now() + 3;                    /* add ax,3 / adc dx,0 */
        u16 o = DSW(DS_g_blinds_rows + 2 * pass);
        u16 d = (u16)(o + dst.off), s = (u16)(o + src.off);
        for (s16 i = 0; i < 0x28; i++) {
            out(0x3CE, 5); out(0x3CF, 2);               /* write mode 2: row to colour 0 */
            u16 p = d;
            for (u16 n = 0x28; n != 0; n--) vwr(dst.seg, p++, 0);
            out(0x3CE, 5); out(0x3CF, 1);               /* then the latched copy of the row */
            vmovedata(src.seg, s, dst.seg, d, 0x28);
            d = (u16)(d + 0xC8);
            s = (u16)(s + 0xC8);
        }
        while (tick_after(until, ticks_now())) host_pump();   /* hi signed, lo unsigned */
    }
    out(0x3CE, 8); out(0x3CF, 0xFF);
    out(0x3CE, 5); out(0x3CF, 0);
    return 0;
}

/* ================================================================ blits (§4.4) */

/* 21a0:1862 (slot 0) — video.md §4.4. Returns AX as the original: 0 (clipped away), 0Fh (a screen
 * side), the plane count (memory -> memory) or what 24e7:0008 leaves (screen -> screen). */
u16 ega_blit(FarPtr s, FarPtr d, const Rect *r, u8 flags)
{
    u16 mask2 = ((flags & 2) && !far_is_null(desc_mask(s)) && !far_is_null(desc_mask(d))) ? 1 : 0;
    Rect c;
    if (!clip_rect(s, d, r, &c)) return 0;
    FarPtr sp, dp;
    s16 p;
    if (flags & 1) { sp = desc_planes(s); dp = desc_planes(d); p = 0; }
    else           { sp = desc_mask(s);   dp = desc_mask(d);   p = 4; }
    s16 pend = mask2 ? 5 : 4;
    u16 sstr = desc_type(s) == 0xFE ? 0x28 : desc_stride(s);
    u16 sstep = desc_type(s) == 0xFE ? 0 : (u16)(desc_h(s) * sstr);
    sp.off = (u16)(sp.off + (u16)((u16)c.sy * sstr) + (u16)c.sx);
    u16 dstr = desc_type(d) == 0xFE ? 0x28 : desc_stride(d);
    u16 dstep = desc_type(d) == 0xFE ? 0 : (u16)(desc_h(d) * dstr);
    dp.off = (u16)(dp.off + (u16)((u16)c.dy * dstr) + (u16)c.dx);
    if (desc_type(d) == 0xFE && desc_type(s) == 0xFE) {      /* screen -> screen: latches */
        ega_latch_copy(sp, dp, c.w, c.h, 0, 0, 0x28);
        /* AX left by 24e7:0008: high byte of its last AX (h * 28h via mul dl when wb == 28h) */
        return (c.w == 0x28 && c.h >= 1) ? (u16)((u8)c.h * 0x28 & 0xFF00) : 0;
    }
    bool scr = dstep == 0 || sstep == 0;
    if (scr) { out(0x3CE, 4); out(0x3C4, 2); }              /* select GC4 and SEQ2 */
    u16 mm = 1;
    if (p < pend) {
        u16 n = (u16)(c.w * c.h);
        do {
            if (scr) {
                if (sstep == 0) out(0x3CF, (u8)p);          /* read map (4 = plane 0 on the card) */
                if (dstep == 0) out(0x3C5, (u8)mm);         /* map mask (10h = no plane) */
            }
            if ((u16)c.w == dstr && (u16)c.w == sstr) {
                vmovedata(sp.seg, sp.off, dp.seg, dp.off, n);
            } else if (c.h > 0) {
                u16 so = sp.off, dof = dp.off;
                for (u16 y = (u16)c.h; y != 0; y--) {
                    vmovedata(sp.seg, so, dp.seg, dof, (u16)c.w);
                    so = (u16)(so + sstr);
                    dof = (u16)(dof + dstr);
                }
            }
            p++;
            mm <<= 1;
            sp.off = (u16)(sp.off + sstep);
            dp.off = (u16)(dp.off + dstep);
        } while (p < pend);
    }
    if (scr) { out(0x3C4, 2); out(0x3C5, 0x0F); return 0x0F; }
    return (u16)pend;
}

/* 21a0:1ae9 — video.md §4.4: per plane dst = (bg & mask) | sprite (24f4:000a), then the tail rows
 * through the latches (24e7:008b). Returns AX as the original. */
s16 ega_sprite_over_bg(FarPtr spr, FarPtr dst, FarPtr bg, const Tail *t, const Rect *r)
{
    s16 trows;                               /* [bp-2] */
    FarPtr tsrc;                             /* [bp-3Ah] */
    if (t) {
        trows = (s16)t->rows;
        tsrc.off = (u16)((r->sx >> 3) + t->off);
        tsrc.seg = t->seg;
        if (r->dx < 0) tsrc.off = (u16)(tsrc.off + ((7 - r->dx) >> 3));   /* the unclipped rect */
    } else {
        trows = 0;
        tsrc = far_make(0, 0);
    }
    Rect c;
    if (!clip_rect(spr, dst, r, &c)) return 0;
    u16 sstr = desc_stride(spr);
    u16 sstep = (u16)(desc_h(spr) * sstr);
    u16 bgstr = desc_stride(bg);
    u16 bgstep = (u16)(bgstr * desc_h(bg));
    s16 hmain = (s16)(c.h - trows);
    u16 dstr = desc_stride(dst);                                   /* not forced to 28h */
    u16 dstep = desc_type(dst) == 0xFE ? 0 : (u16)(desc_h(dst) * dstr);
    FarPtr dp = desc_planes(dst);
    dp.off = (u16)((u16)((u16)c.dy * dstr) + dp.off + (u16)c.dx);
    FarPtr spl = desc_planes(spr);
    u16 seg = spl.seg;
    u16 si = (u16)((u16)c.sy * sstr);
    u16 so = (u16)(spl.off + (u16)c.sx + si);
    u16 mo = (u16)(desc_mask(spr).off + (u16)c.sx + si);
    FarPtr bpl = desc_planes(bg);
    u16 bo = sstr == bgstr ? (u16)((u16)((u16)c.dy * bgstr) + bpl.off)            /* (sic) no x offset */
                           : (u16)((u16)((u16)c.sy * bgstr) + bpl.off + (u16)c.dx); /* (sic) */
    for (u16 mm = 1; mm < 9; mm <<= 1) {
        if (dstep == 0) { out(0x3C4, 2); out(0x3C5, (u8)mm); }
        rows_composite(dp, seg, so, mo, bo, (s16)sstr, c.w, hmain, (s16)bgstr, (s16)dstr);
        dp.off = (u16)(dp.off + dstep);
        so = (u16)(so + sstep);
        bo = (u16)(bo + bgstep);                                   /* the mask is not advanced */
    }
    u16 ax = sstep;
    if (dstep == 0) { out(0x3C4, 2); out(0x3C5, 0x0F); ax = 0x0F; }
    if (trows > 0) {
        ax = (u16)(0x28 * hmain);
        dp.off = (u16)(dp.off + ax);
        ega_latch_copy_to40(tsrc, c.w, trows, dp, (s16)sstr);
        if (c.w >= 1) ax = (u16)(c.w & 0xFF00);                    /* AX left by 24e7:008b */
    }
    return (s16)ax;
}

/* 21a0:1cd5 (slot 2) — video.md §4.4: transparent blit, 2634:000a (mode 0) or 2595:0004 per plane.
 * Returns AX: 0 (clipped away), 0Fh (screen destination) or the destination plane step. */
s16 ega_blit_masked(FarPtr s, FarPtr d, const Rect *r, s16 mode)
{
    Rect c;
    if (!clip_rect(s, d, r, &c)) return 0;
    u16 sstr = desc_stride(s);
    u16 sstep = (u16)(desc_h(s) * sstr);
    u16 dstr = desc_type(d) == 0xFE ? 0x28 : desc_stride(d);
    u16 dstep = desc_type(d) == 0xFE ? 0 : (u16)(desc_h(d) * dstr);
    FarPtr dp = desc_planes(d);
    dp.off = (u16)((u16)((u16)c.dy * dstr) + dp.off + (u16)c.dx);
    FarPtr spl = desc_planes(s);
    u16 seg = spl.seg;
    u16 si = (u16)((u16)c.sy * sstr);
    u16 so = (u16)(spl.off + (u16)c.sx + si);
    u16 mo = (u16)(desc_mask(s).off + (u16)c.sx + si);        /* mask assumed in the planes' segment */
    void (*fn)(FarPtr, u16, u16, u16, s16, s16, s16, s16) = mode ? rows_mask_or : rows_mask_blend;
    if (dstep == 0) { out(0x3CE, 4); out(0x3C4, 2); }
    u16 p = 0;
    for (u16 mm = 1; mm < 9; mm <<= 1, p++) {
        if (dstep == 0) { out(0x3CF, (u8)p); out(0x3C5, (u8)mm); }   /* read map = write plane */
        fn(dp, seg, so, mo, (s16)sstr, c.w, c.h, (s16)dstr);
        so = (u16)(so + sstep);
        dp.off = (u16)(dp.off + dstep);
    }
    if (dstep == 0) { out(0x3C4, 2); out(0x3C5, 0x0F); return 0x0F; }
    return (s16)dstep;
}

/* 21a0:1e2d — video.md §4.4: latched copy of a w x h rect into a packed buffer */
void ega_save_rect(FarPtr src, s16 w, s16 h, FarPtr dst, s16 sstride)
{
    if (h < 1) return;
    out(0x3CE, 5); out(0x3CF, 1);
    for (s16 y = 0; y < h; y++, src.off = (u16)(src.off + sstride)) {
        u16 p = src.off;
        for (s16 x = 0; x < w; x++) vwr(dst.seg, dst.off++, vrd(src.seg, p++));
    }
    out(0x3CE, 5); out(0x3CF, 0);
}

/* 21a0:1ea2 (near) — video.md §4.4: shift n bytes right (shift > 0) / left (< 0) by |shift| bits */
static void shift_row(u8 *row, s16 shift, s16 n)
{
    if (shift > 0) {
        u8 hi = DSB(DS_g_lmask + shift), lo = (u8)~hi;
        u8 carry = 0;
        for (s16 k = n; k > 0; k--, row++) {             /* ror ax,cl on the zero-extended byte */
            u8 v = ror8(*row, (unsigned)shift);
            *row = (u8)((v & lo) | carry);
            carry = v & hi;
        }
    } else if (shift < 0) {
        row += n - 1;
        shift = (s16)-shift;
        u8 hi = DSB(DS_g_lmask + 1 + 7 - shift), lo = (u8)~hi;   /* 5C16[7 - s] */
        u8 carry = 0;
        for (s16 k = n; k > 0; k--, row--) {
            u8 v = (u8)(*row << (shift & 7) | *row >> ((8 - shift) & 7));   /* rol */
            *row = (u8)((v & hi) | carry);
            carry = lo & v;
        }
    }
}

/* 21a0:1f8d (near) — video.md §4.4: one 4-plane row buffer (4 x 40 bytes) at bit offset `bit` */
static void ega_put_shifted_row(FarPtr d, u8 *buf, s16 w, s16 bit, s16 shift)
{
    if (shift) {
        s16 nb = (s16)((w + 7) / 8);
        for (int k = 0; k < 4; k++) shift_row(buf + 0x28 * k, shift, nb);
    }
    u8 *p0 = buf, *p1 = buf + 0x28, *p2 = buf + 0x50, *p3 = buf + 0x78;
    if (bit) {                                            /* left partial byte */
        u8 m = (u8)~DSB(DS_g_lmask + bit);
        s16 t = (s16)(bit + w);
        if (t < 8) m &= DSB((u16)(DS_g_lmask + t));
        out(0x3CE, 8); out(0x3CF, m);
        out(0x3C4, 2); out(0x3C5, 1); vrd(d.seg, d.off); vwr(d.seg, d.off, (u8)(*p0++ & m));
        out(0x3C4, 2); out(0x3C5, 2); vrd(d.seg, d.off); vwr(d.seg, d.off, (u8)(*p1++ & m));
        out(0x3C4, 2); out(0x3C5, 4); vrd(d.seg, d.off); vwr(d.seg, d.off, (u8)(*p2++ & m));
        out(0x3C4, 2); out(0x3C5, 8); vrd(d.seg, d.off); vwr(d.seg, d.off, (u8)(*p3++ & m));
        d.off++;
        w = (s16)(w - (8 - bit));
    }
    out(0x3CE, 8); out(0x3CF, 0xFF);
    if (w > 7) {
        s16 n = (s16)(-w / -8);                           /* idiv */
        w = (s16)(w + -8 * n);
        do {
            out(0x3C4, 2); out(0x3C5, 1); vrd(d.seg, d.off); vwr(d.seg, d.off, *p0++);
            out(0x3C4, 2); out(0x3C5, 2); vrd(d.seg, d.off); vwr(d.seg, d.off, *p1++);
            out(0x3C4, 2); out(0x3C5, 4); vrd(d.seg, d.off); vwr(d.seg, d.off, *p2++);
            out(0x3C4, 2); out(0x3C5, 8); vrd(d.seg, d.off); vwr(d.seg, d.off, *p3++);
            d.off++;
        } while (--n);
    }
    if (w > 0) {                                          /* right partial byte */
        u8 m = DSB(DS_g_lmask + w);
        out(0x3CE, 8); out(0x3CF, m);
        out(0x3C4, 2); out(0x3C5, 1); vrd(d.seg, d.off); vwr(d.seg, d.off, (u8)(*p0 & m));
        out(0x3C4, 2); out(0x3C5, 2); vrd(d.seg, d.off); vwr(d.seg, d.off, (u8)(*p1 & m));
        out(0x3C4, 2); out(0x3C5, 4); vrd(d.seg, d.off); vwr(d.seg, d.off, (u8)(*p2 & m));
        out(0x3C4, 2); out(0x3C5, 8); vrd(d.seg, d.off); vwr(d.seg, d.off, (u8)(*p3 & m));
    }
    out(0x3CE, 8); out(0x3CF, 0xFF);
    out(0x3C4, 2); out(0x3C5, 0x0F);
}

/* 21a0:223b (slot 1) — video.md §4.4: unclipped memory -> screen blit at any pixel x */
void ega_blit_shifted(FarPtr s, FarPtr d, const Rect *r)
{
    s16 sxb = (s16)(r->sx / 8);                           /* C division (toward zero) */
    FarPtr spl = desc_planes(s), dpl = desc_planes(d);
    FarPtr sp = far_make(spl.seg, (u16)((u16)(desc_stride(s) * (u16)r->sy) + spl.off + (u16)sxb));
    FarPtr dp = far_make(dpl.seg, (u16)((u16)(desc_stride(d) * (u16)r->dy) + (u16)(r->dx / 8) + dpl.off));
    s16 nb = (s16)((r->sx + r->w - 1) / 8 - sxb + 1);
    s16 h = r->h;
    u16 pstep = (u16)(desc_size(s) >> 2);                 /* bytes per plane */
    out(0x3CE, 5); out(0x3CF, 0);
    s16 bit = (s16)(r->dx & 7);
    s16 shift = (s16)(bit - (r->sx & 7));
    /* 4 x 40 bytes at [bp-0AAh]; PORT: slack so that nb > 40 (never in the game) stays in bounds. */
    u8 buf[0xA0 + 8];
    if (h <= 0) return;
    s16 w = r->w;
    for (s16 n = h;;) {
        for (s16 b = 0; b < nb; b++) {
            u16 o = (u16)(b + sp.off);
            for (int k = 0; k < 4; k++, o = (u16)(o + pstep)) {
                unsigned i = (unsigned)(b + 0x28 * k);
                u8 v = vrd(sp.seg, o);
                if (i < sizeof buf) buf[i] = v;
            }
        }
        ega_put_shifted_row(dp, buf, w, bit, shift);
        sp.off = (u16)(sp.off + desc_stride(s));
        dp.off = (u16)(dp.off + desc_stride(d));
        if (--n == 0) break;
    }
}

/* =========================================================================== line (§4.6) */

/* 21a0:252e (near asm): AX = y, BX = x -> ES:BX = DS:787A : y*28h + (x >> 3) (unsigned), CL = (x & 7) ^ 7,
 * AH = 1. */
static u16 pixel_addr(s16 y, s16 x, u8 *cl)
{
    *cl = (u8)(((u8)x & 7) ^ 7);
    return (u16)((u16)y * 0x28 + ((u16)x >> 3));
}

/* or es:[di],al / movsb with ds:si = es:di: a CPU read (latches) then a write of the same byte */
static inline void or_byte(u16 seg, u16 o) { vwr(seg, o, (u8)(vrd(seg, o) | 8)); }
static inline void movsb_self(u16 seg, u16 o) { vwr(seg, o, vrd(seg, o)); }

/* 21a0:23bc (slot 12) — video.md §4.6: Bresenham line with set/reset into segment DS:787A */
void ega_line(s16 x0, s16 y0, s16 x1, s16 y1, s16 colour)
{
    outw(0x3CE, (u16)((u8)colour << 8 | 0));
    outw(0x3CE, 0x0F01);
    outw(0x3CE, (u16)((DSW(DS_g_gc3) & 0xFF00) | 3));
    u16 seg = DSW(DS_g_draw_seg);
    u16 ystep = 0x28;                                     /* si */
    u8 cl;
    u16 o;
    s16 cx = (s16)(x1 - x0);
    if (cx == 0) {                                        /* vertical (21a0:243e) */
        s16 ay = y0;
        u16 n = (u16)(y1 - y0);
        if (!(y1 >= y0)) { n = (u16)-n; ay = y1; }        /* sub cx,ax; jge */
        n++;
        o = pixel_addr(ay, x0, &cl);
        outw(0x3CE, (u16)((u8)(1 << cl) << 8 | 8));
        do { or_byte(seg, o); o = (u16)(o + ystep); } while (--n);
        goto done;
    }
    if (cx < 0) {                                         /* jns: sign of the difference */
        cx = (s16)-cx;
        s16 t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }
    s16 bx = (s16)(y1 - y0);
    if (bx == 0) {                                        /* horizontal (21a0:2466), x0 < x1 */
        o = pixel_addr(y0, x0, &cl);
        u8 lm = (u8)~(u8)(0xFE << cl);
        u8 rm = (u8)(0xFF << (((u8)x1 & 7) ^ 7));
        u16 n = (u16)(((u16)x1 >> 3) - ((u16)x0 >> 3));
        if (!(lm & 0x80)) {
            if (n == 0) { rm &= lm; goto last; }
            outw(0x3CE, (u16)(lm << 8 | 8));
            movsb_self(seg, o++);
            n--;
        }
        outw(0x3CE, 0xFF08);
        for (; n != 0; n--) movsb_self(seg, o++);         /* rep movsb */
    last:
        outw(0x3CE, (u16)(rm << 8 | 8));
        movsb_self(seg, o);
        goto done;
    }
    if (bx < 0) { bx = (s16)-bx; ystep = (u16)-0x28; }
    bool ymajor = bx > cx;
    if (ymajor) { s16 t = cx; cx = bx; bx = t; }          /* cx = major, bx = minor */
    s16 d1 = (s16)(bx << 1);                              /* [bp-4] */
    s16 err = (s16)(d1 - cx);                             /* si */
    s16 d2 = (s16)(err - cx);                             /* [bp-6] */
    o = pixel_addr(y0, x0, &cl);
    u8 m = (u8)(1 << cl);                                 /* ah = bl */
    u16 n = (u16)(cx + 1);
    if (!ymajor) {                                        /* 21a0:24bf: a byte of pixels per write */
        u8 acc;
    next:
        acc = m;
    more:
        acc |= m;
        {
            bool wrap = m & 1;
            m = ror8(m, 1);
            if (wrap) {                                   /* 21a0:24e4 */
                outw(0x3CE, (u16)(acc << 8 | 8));
                or_byte(seg, o);
                o++;
                if (err < 0) err = (s16)(err + d1);
                else { err = (s16)(err + d2); o = (u16)(o + ystep); }
                if (--n) goto next;
                goto done;
            }
        }
        if (err < 0) {
            err = (s16)(err + d1);
            if (--n) goto more;
            outw(0x3CE, (u16)(acc << 8 | 8));
            or_byte(seg, o);
            goto done;
        }
        err = (s16)(err + d2);                            /* 21a0:24d6 */
        outw(0x3CE, (u16)(acc << 8 | 8));
        or_byte(seg, o);
        o = (u16)(o + ystep);
        if (--n) goto next;
        goto done;
    }
    do {                                                  /* 21a0:24fe */
        outw(0x3CE, (u16)(m << 8 | 8));
        or_byte(seg, o);
        o = (u16)(o + ystep);
        if (err < 0) err = (s16)(err + d1);
        else {
            err = (s16)(err + d2);
            bool c = m & 1;
            m = ror8(m, 1);
            o = (u16)(o + c);                             /* adc di,0 */
        }
    } while (--n);
done:
    outw(0x3CE, 0x0000);
    outw(0x3CE, 0x0001);
    outw(0x3CE, 0x0003);
    outw(0x3CE, 0xFF08);
}

/* ================================================================= animation (§4.7) */

/* Anim record / PageNode / SpriteRef layout: see anim_rect.c. */

/* 21a0:2568 (near) — video.md §4.7: latched copy of the dirty rect r {x0,y0,x1,y1} from the
 * background, whose origin is shifted back (mode 0: by x0 bytes, else by y0 rows). */
static void anim_restore_rect(const s16 *r, FarPtr page, FarPtr bgsrc, s16 mode)
{
    u16 back = mode != 0 ? (u16)(0x28 * r[1]) : (u16)(r[0] >> 3);
    s16 xb = (s16)(r[0] >> 3);
    FarPtr src = desc_planes(bgsrc);
    src.off = (u16)(src.off - back);
    ega_latch_copy(src, desc_planes(page), (s16)((r[2] >> 3) - xb + 1), (s16)(r[3] - r[1] + 1), xb, r[1],
                   (s16)desc_stride(page));
}

/* Tail record (near pointer in the original) read from DGROUP. */
static const Tail *tail_at(u16 off, Tail *t)
{
    if (!off) return NULL;
    t->off = DSW(off); t->seg = DSW(off + 2); t->unused4 = DSW(off + 4); t->rows = DSW(off + 6);
    return t;
}

/* 21a0:25d9 (slot 9) — video.md §4.7: one frame of a sprite animation over a VRAM background. */
s16 ega_anim_step(u16 a)
{
    if (DSS(DSW(a + 0x12)) < 0) {                 /* finished: copy the last page into the next one */
        u16 n = DSW(a + 0x14);
        ega_latch_copy(desc_planes(ds_far(n)), desc_planes(ds_far(DSW(n + 0x0E))), 0x28, 0xBE, 0, 0, 0x28);
        if (DSW(n + 0x0C) != 0) {
            u16 n2 = DSW(n + 0x0E), n3 = DSW(n2 + 0x0E);
            for (u16 i = 0; i < 8; i += 2) DSW(n2 + 4 + i) = DSW(n3 + 4 + i);
            ega_show_page(0);
        }
        return 0;
    }
    u16 n = DSW(DSW(a + 0x14) + 0x0E);            /* next page node */
    DSW(a + 0x14) = n;
    u16 ref = DSW(a);                             /* [bp-36h] */
    FarPtr bgsrc = ds_far(a + 2), bg = ds_far(a + 6);
    s16 box[4], dirty[4];
    rect_clip_make((s16)desc_w(ds_far(ref)), (s16)(desc_h(ds_far(ref)) - DSW(DS_g_anim_yoff)),
                   DSS(a + 0x0E), DSS(a + 0x10), (s16)desc_w(ds_far(n)), (s16)desc_h(ds_far(n)), box);
    u16 fr = (u16)(DSW(a + 0x12) + 2);            /* si: frame record +2 (s8 dx = mode) */
    s16 *nrect = (s16 *)mp(DGROUP, (u16)(n + 4));
    if (rect_subtract_edge(box, nrect, dirty, (s16)DSC(fr)))
        anim_restore_rect(dirty, ds_far(n), bgsrc, (s16)DSC(fr));
    for (int i = 0; i < 4; i++) DSS(n + 4 + 2 * i) = box[i];

    Rect r;                                       /* [bp-10h] */
    Tail tl;
    r.h = (s16)(desc_h(ds_far(ref)) - DSW(DS_g_anim_yoff));
    r.sy = 0;
    r.dy = DSS(a + 0x10);
    if (DSW(DS_drive_dest) != 6) {
        r.w = (s16)desc_w(ds_far(ref));
        r.sx = 0;
        r.dx = DSS(a + 0x0E);
        ega_sprite_over_bg(ds_far(ref), ds_far(n), bg, tail_at(DSW(ref + 4), &tl), &r);
    } else {                                      /* jail: six vertical strips between the bars */
        for (u16 i = 0; i < 6; i++) {
            if (sprite_hspan_clip(DSS(DS_tile_x + 4 * i), DSS(DS_tile_w + 4 * i), a, (s16 *)&r))
                ega_sprite_over_bg(ds_far(ref), ds_far(n), bg, tail_at(DSW(ref + 4), &tl), &r);
        }
    }
    for (u16 k = 0; k < 2; k++) {                 /* overlays redrawn over the sprite */
        u16 op = DSW(a + 0x0A + 2 * k);
        if (op == 0) continue;
        s16 o[4];
        for (int i = 0; i < 4; i++) o[i] = DSS(op + 2 * i);
        bool same = desc_stride(ds_far(ref)) == desc_stride(bg);
        if (same) { o[0] = (s16)(o[0] + DSS(a + 0x0E)); o[2] = (s16)(o[2] + DSS(a + 0x0E)); }
        else      { o[1] = (s16)(o[1] + DSS(a + 0x10)); o[3] = (s16)(o[3] + DSS(a + 0x10)); }
        if (rect_intersect(o, nrect, dirty)) {
            r.w = (s16)(dirty[2] - dirty[0] + 1);
            r.h = (s16)(dirty[3] - dirty[1] + 1);
            if (desc_stride(ds_far(ref)) == desc_stride(bg)) { r.sx = 0; r.sy = dirty[1]; }
            else { r.sx = dirty[0]; r.sy = (s16)(dirty[1] - DSS(a + 0x10)); }
            r.dx = dirty[0];
            r.dy = dirty[1];
            ega_blit_masked(bg, ds_far(n), &r, 0);
        }
    }
    sprite_anim_advance(a);
    ega_show_page(DSS(n + 0x0C));
    return 1;
}

/* 21a0:28ec (slot 11) — video.md §4.7: move a sprite vertically inside the window rows top..bottom,
 * 2 rows per frame, drawn on the back page and copied to the front page when double buffered. */
void ega_slide_sprite(s16 top, s16 bottom, s16 x, s16 from, s16 to, FarPtr spr)
{
    s16 xb = (s16)(x / 8);                                /* idiv */
    s16 w = (s16)desc_stride(spr);                        /* [bp-28h] */
    s16 h = (s16)desc_h(spr);                             /* [bp-2Ah] */
    u16 pstep = (u16)(w * h);                             /* [bp-10h] */
    FarPtr B = g_back();
    u16 bstr = desc_stride(B);                            /* [bp-30h] */
    FarPtr bpl = desc_planes(B);
    FarPtr save = far_make(bpl.seg, (u16)(bpl.off + 0x2AF0));
    FarPtr win = far_make(bpl.seg, (u16)((u16)(top * bstr) + bpl.off + (u16)xb));
    s16 d = (s16)(to - from);
    s16 step;
    u16 rowstep;
    FarPtr spl = desc_planes(spr);                        /* [bp-38h] */
    if (d < 0) {
        if (d & 1) { d--; from++; }
        rowstep = (u16)-(u16)(bstr << 1);
        step = -2;
        d = (s16)-d;
    } else {
        if (d & 1) { d++; from--; }
        rowstep = (u16)(bstr << 1);
        step = 2;
    }
    s16 frames = (s16)((d >> 1) + 1);
    B = g_back();
    bpl = desc_planes(B);
    FarPtr cur = far_make(bpl.seg, (u16)((u16)(from * bstr) + bpl.off + (u16)xb));
    make_mask_plane(desc_planes(spr), desc_mask(spr), desc_size(spr));
    FarPtr msk = desc_mask(spr);                          /* [bp-34h] */
    ega_save_rect(win, w, (s16)(bottom - top + 1), save, (s16)bstr);
    s16 y = from;                                         /* [bp-2Eh] */
    if (frames <= 0) return;
    s16 wrows = (s16)(bottom - top + 1);                  /* [bp-44h] */
    s16 bstr8 = (s8)bstr, w8 = (s8)w;                     /* cbw of the low bytes */
    s16 h_top = (s16)(h - top);
    for (s16 n = frames;;) {
        ega_latch_copy_to40(save, w, wrows, win, w);      /* restore the window */
        FarPtr dp;
        u16 so, mo;
        s16 rows;
        if (y < top) {
            u16 skip = (u16)((top - y) * w);
            so = (u16)(skip + spl.off);
            dp = win;
            mo = (u16)(skip + msk.off);
            rows = (s16)(h_top + y);
        } else {
            dp = cur;
            so = spl.off;
            mo = msk.off;
            rows = (s16)(y + h - 1) > bottom ? (s16)(bottom - y + 1) : h;
        }
        s16 rows8 = (s8)rows;
        u16 mm = 1;
        for (u16 p = 0; mm < 9; mm <<= 1, p++) {
            out(0x3C4, 2); out(0x3C5, (u8)mm);
            out(0x3CE, 4); out(0x3CF, (u8)p);
            rows_mask_or(dp, spl.seg, so, mo, w8, w8, rows8, bstr8);
            so = (u16)(so + pstep);
        }
        out(0x3C4, 2); out(0x3C5, 0x0F);
        if (DSW(DS_g_mirror)) {
            if (!far_is_null(ds_far(DS_g_slide_overlay))) {
                Rect r;                                   /* near pointer DS:70AC in the original */
                r.w = DSS(DS_g_slide_overlay_rect);
                r.h = DSS(DS_g_slide_overlay_rect + 2);
                r.sx = DSS(DS_g_slide_overlay_rect + 4);
                r.sy = DSS(DS_g_slide_overlay_rect + 6);
                r.dx = DSS(DS_g_slide_overlay_rect + 8);
                r.dy = DSS(DS_g_slide_overlay_rect + 10);
                drv_blit_masked(ds_far(DS_g_slide_overlay), g_back(), &r, 1);   /* lcall [78AAh] */
            }
            ega_wait_vretrace();
            ega_latch_copy(desc_planes(g_back()), desc_planes(g_front()), w, wrows, xb, top, (s16)bstr);
            ega_wait_vretrace();
        }
        y = (s16)(y + step);
        cur.off = (u16)(cur.off + rowstep);
        if (--n == 0) break;
    }
}
