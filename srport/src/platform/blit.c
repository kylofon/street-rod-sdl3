/* Blit helper segments of the EGA driver (video.md §2.2, §4.4, §4.6, §4.7): the mouse pointer
 * 0e6c:0175, the picture unpacker 0e92:0006, the latched copies 24e7, the plane row helpers
 * 24f4 / 2595 / 2634 and the EGA span routines of segment 2645.
 *
 * Transcribed from the disassembly (tools/x86dis.py) on top of the VGA register model (vga.h): every
 * memory access goes through vrd/vwr (VRAM through the latches, RAM bitmaps in mem[]); x86 word
 * accesses are vrd16/vwr16 (two byte cycles, low byte first). The unrolled / self-modifying helpers
 * (24f4:000a, 2595:0004) are written as loops with the same access order.
 *
 * PORT: the Tandy helpers (0e6c:0002/00c1, 2462, 2487, 24be, 24d8) are not ported (VGA path only). */
#include "platform/vga.h"
#include "platform/video.h"

/* DGROUP tables without a symbols.h name. */
#define DS_run_count 0x4EEE   /* u8[16] run lengths of the 16 picture tokens (0e92:0006) */
#define DS_run_value 0x4EFE   /* u8[16] run byte values */

/* ======================================================================= pointer (§4.7, §4.10) */

/* 0e6c:0175 (slot 4) — video.md §4.7: save the 4 planes under the pointer through the latches (bit
 * mask 0), then write the 4 image planes with the pointer mask as bit mask. The mask is read in the
 * image's segment; the sequencer index is never written (2 assumed). */
void ega_draw_cursor(FarPtr img, FarPtr mask, FarPtr save, FarPtr scr, s16 xb, s16 y, s16 wb, s16 rows)
{
    u16 o = (u16)(scr.off + (u16)(0x28 * (u8)y) + (u16)xb);   /* mul bl: 8-bit y */
    u16 s = img.off, m = mask.off, v = save.off;
    out(0x3CE, 5); out(0x3CF, 0);                              /* write mode 0 */
    out(0x3CE, 8);                                             /* GC index 8 from here on */
    u16 rows16 = (u16)rows;
    for (;;) {
        u16 n = (u16)wb;                                       /* loop: wb = 0 runs 65536 times */
        do {
            out(0x3CF, 0);                                     /* bit mask 0 */
            out(0x3C5, 0x0F);
            vwr(save.seg, v, vrd(scr.seg, o));                 /* stores the 4 latched planes */
            v++;
            out(0x3CF, vrd(img.seg, m));                       /* bit mask = pointer mask */
            m++;
            out(0x3C5, 1); vwr(scr.seg, o, vrd(img.seg, s));
            out(0x3C5, 2); vwr(scr.seg, o, vrd(img.seg, (u16)(s + 0x600)));
            out(0x3C5, 4); vwr(scr.seg, o, vrd(img.seg, (u16)(s + 0xC00)));
            out(0x3C5, 8); vwr(scr.seg, o, vrd(img.seg, (u16)(s + 0x1200)));
            o++;
            s++;
        } while (--n);
        if (--rows16 == 0) break;
        s = (u16)(s + 0x18 - wb);
        m = (u16)(m + 0x18 - wb);
        v = (u16)(v + 3 - wb);
        o = (u16)(o + 0x28 - wb);
    }
    out(0x3C5, 0x0F);
    out(0x3CF, 0xFF);
}

/* ====================================================================== picture unpacker */

/* 0e92:0006 — FORMATS.md: bytes 00/FFh are followed by a count c and give c+1 copies of themselves;
 * a byte equal to token k (1..ntok) gives count[k-1] copies of value[k-1] (tables DS:4EEE/4EFE copied
 * to the stack at the call; a count of 0 gives 65536 bytes, dec cx / jne); any other byte is a
 * literal. src is read through vrd (a picture parked in the VRAM cache is read with the read map
 * select set by the caller), dst written through vwr. Returns n + sum(run length - run bytes). */
u16 pic_unpack(FarPtr src, FarPtr dst, u16 n, const u8 *tokens, u16 ntok)
{
    u8 token_of[256];                        /* [bp-12Ah]: byte -> token number, 0 = literal */
    for (int i = 0; i < 256; i++) token_of[i] = 0;
    for (s16 k = 0; k < (s16)ntok; k++) token_of[tokens[k]] = (u8)(k + 1);
    u8 tab[32];                              /* [bp-2Ah] counts, [bp-1Ah] values */
    for (u16 i = 0; i < 16; i++) {
        tab[i] = DSB(DS_run_count + i);
        tab[16 + i] = DSB(DS_run_value + i);
    }
    u16 out = n;                             /* [bp-0Ah] */
    s16 left = (s16)n;                       /* dx */
    u16 si = src.off, di = dst.off;
    while (left > 0) {
        left--;
        u8 b = vrd(src.seg, si++);
        u16 cnt;
        if (b == 0x00 || b == 0xFF) {
            left--;
            u8 c = vrd(src.seg, si++);
            out = (u16)(out + c - 1);
            cnt = (u16)(c + 1);
        } else {
            u8 k = token_of[b];
            if (k == 0) { vwr(dst.seg, di++, b); continue; }
            /* PORT: tokens beyond 16 read the original's stack frame past the tables; 0 here. */
            b = (unsigned)(k + 15) < 32 ? tab[k + 15] : 0;
            u8 c = (unsigned)(k - 1) < 32 ? tab[k - 1] : 0;
            out = (u16)(out + c - 1);
            cnt = c;
        }
        do vwr(dst.seg, di++, b); while (--cnt);
    }
    return out;
}

/* ===================================================================== latched copies (§4.4) */

/* 24e7:0008 — video.md §4.4: VRAM -> VRAM rect copy in write mode 1, same stride both sides */
void ega_latch_copy(FarPtr s, FarPtr d, s16 wb, s16 h, s16 xb, s16 y, s16 stride)
{
    if (wb < 1 || h < 1) return;
    out(0x3CE, 5); out(0x3CF, 1);
    u16 o = (u16)((u16)((u8)y * (u8)stride) + (u16)xb);       /* mul dl: 8-bit operands */
    s.off = (u16)(s.off + o);
    d.off = (u16)(d.off + o);
    if (wb == 1) {
        u16 dx = (u16)(stride - 1);
        u16 cx = (u16)h;
        do {
            vwr(d.seg, d.off++, vrd(s.seg, s.off++));
            s.off = (u16)(s.off + dx);
            d.off = (u16)(d.off + dx);
        } while (--cx);
    } else if (wb == stride) {
        vmovedata(s.seg, s.off, d.seg, d.off, (u16)((u8)h * (u8)stride));
    } else {
        u16 bx = (u16)h, dx = (u16)(stride - wb);
        do {
            vmovedata(s.seg, s.off, d.seg, d.off, (u16)wb);
            s.off = (u16)(s.off + wb + dx);
            d.off = (u16)(d.off + wb + dx);
        } while (--bx);
    }
    out(0x3CE, 5); out(0x3CF, 0);
}

/* 24e7:008b — video.md §4.4: VRAM -> VRAM copy, destination stride 28h. 8-bit row counter; the
 * destination advances by the byte 28h - wb (zero-extended: add di,cx with ch = 0). */
void ega_latch_copy_to40(FarPtr s, s16 wb, s16 h, FarPtr d, s16 sstride)
{
    if (h < 1 || wb < 1) return;
    out(0x3CE, 5); out(0x3CF, 1);
    u8 bl = (u8)h;
    u16 dx = (u16)(sstride - wb);
    u8 bh = (u8)(0x28 - (u8)wb);
    do {
        vmovedata(s.seg, s.off, d.seg, d.off, (u16)wb);
        s.off = (u16)(s.off + wb + dx);
        d.off = (u16)(d.off + wb + bh);
    } while (--bl);
    out(0x3CE, 5); out(0x3CF, 0);
}

/* ================================================================= plane row helpers (§4.4) */

/* 24f4:000a — one plane: dst = (mask & bg) | src, words from offset wb-2 down to (wb & 1), then the
 * byte at 0 if wb is odd (unrolled, wb <= A0h). Source, mask and background in segment seg. */
void rows_composite(FarPtr d, u16 seg, u16 s, u16 m, u16 bg, s16 sstr, s16 wb, s16 h, s16 bgstr, s16 dstr)
{
    if (h <= 0 || wb <= 0) return;
    u16 cx = (u16)h;
    do {
        for (s16 i = (s16)(wb - 2); i >= (wb & 1); i -= 2) {
            u16 ax = vrd16(seg, (u16)(m + i));
            ax &= vrd16(seg, (u16)(bg + i));
            ax |= vrd16(seg, (u16)(s + i));
            vwr16(d.seg, (u16)(d.off + i), ax);
        }
        if (wb & 1) {
            u8 al = vrd(seg, m);
            al &= vrd(seg, bg);
            al |= vrd(seg, s);
            vwr(d.seg, d.off, al);
        }
        s = (u16)(s + sstr);
        m = (u16)(m + sstr);
        bg = (u16)(bg + bgstr);
        d.off = (u16)(d.off + dstr);
    } while (--cx);
}

/* 2595:0004 — one plane: dst = (mask & dst) | src, same unrolled order as 24f4:000a */
void rows_mask_or(FarPtr d, u16 seg, u16 s, u16 m, s16 sstr, s16 wb, s16 h, s16 dstr)
{
    if (h <= 0 || wb <= 0) return;
    u16 cx = (u16)h;
    do {
        for (s16 i = (s16)(wb - 2); i >= (wb & 1); i -= 2) {
            u16 ax = vrd16(seg, (u16)(m + i));
            ax &= vrd16(d.seg, (u16)(d.off + i));             /* loads the latches */
            ax |= vrd16(seg, (u16)(s + i));
            vwr16(d.seg, (u16)(d.off + i), ax);
        }
        if (wb & 1) {
            u8 al = vrd(seg, m);
            al &= vrd(d.seg, d.off);
            al |= vrd(seg, s);
            vwr(d.seg, d.off, al);
        }
        s = (u16)(s + sstr);
        m = (u16)(m + sstr);
        d.off = (u16)(d.off + dstr);
    } while (--cx);
}

/* 2634:000a — one plane: dst = (dst & mask) | (src & ~mask), left to right. Row counter = low byte
 * of h (dec cl); the destination row step only reduces the low byte of dstr by wb & FEh (sub dl,ch),
 * so a borrow adds 100h. With no whole word (wb & FEh == 0) the odd byte is processed regardless. */
void rows_mask_blend(FarPtr d, u16 seg, u16 s, u16 m, s16 sstr, s16 wb, s16 h, s16 dstr)
{
    if (h <= 0 || wb <= 0) return;
    u8 cl = (u8)h;
    u8 ch = (u8)wb;
    u8 odd = ch & 1;                                          /* cs:[2Bh] */
    ch &= 0xFE;
    u16 sadd = (u16)(sstr - ch);                              /* cs:[28h] */
    u16 dadd = (u16)(((u16)dstr & 0xFF00) | (u8)((u8)dstr - ch));
    u8 words = (u8)(ch >> 1);                                 /* cs:[2Ah] */
    u16 bp = m, si = s, di = d.off;
    do {
        u8 n = words;
        if (n != 0) {
            do {
                u16 ax = vrd16(seg, bp);
                u16 bx = vrd16(d.seg, di);
                bx &= ax;
                ax = (u16)~ax;
                ax &= vrd16(seg, si);
                ax |= bx;
                vwr16(d.seg, di, ax);
                bp = (u16)(bp + 2); si = (u16)(si + 2); di = (u16)(di + 2);
            } while (--n);
        }
        if (words == 0 || odd) {
            u8 al = vrd(seg, bp);
            u8 bl = vrd(d.seg, di);
            bl &= al;
            al = (u8)~al;
            al &= vrd(seg, si);
            al |= bl;
            vwr(d.seg, di, al);
        }
        bp = (u16)(bp + sadd);
        si = (u16)(si + sadd);
        di = (u16)(di + dadd);
    } while (--cl);
}

/* 2634:00c1 — mask = ~(p0 | p1 | p2 | p3), n = total / 4 bytes per plane (odd byte first, then words;
 * n == 0 would run 32 K words as the original) */
void make_mask_plane(FarPtr planes, FarPtr mask, u16 total)
{
    u16 n = (u16)(total >> 2);
    u16 cx = n, si = planes.off, di = mask.off;
    u16 ps = planes.seg;
    if (n & 1) {
        u8 al = vrd(ps, si);
        al |= vrd(ps, (u16)(si + n));
        al |= vrd(ps, (u16)(si + 2 * n));
        al |= vrd(ps, (u16)(si + 3 * n));
        vwr(mask.seg, di, (u8)~al);
        if (--cx == 0) return;
        di++;
        si++;
    }
    do {
        u16 ax = vrd16(ps, si);
        ax |= vrd16(ps, (u16)(si + n));
        ax |= vrd16(ps, (u16)(si + 2 * n));
        ax |= vrd16(ps, (u16)(si + 3 * n));
        vwr16(mask.seg, di, (u16)~ax);
        di = (u16)(di + 2);
        si = (u16)(si + 2);
        cx--;
    } while (--cx);                                           /* dec cx; loop */
}

/* ============================================================ spans, segment 2645 (§4.6) */

/* 2645:0008: enable set/reset on all planes, GC3 = high byte of DS:5C1E (EGA driver only) */
void ega_setreset_on(void)
{
    if (DSS(DS_driver_id) != -2) return;
    outw(0x3CE, 0x0F01);
    outw(0x3CE, (u16)((DSW(DS_g_gc3) & 0xFF00) | 3));
}

/* 2645:001d */
void ega_setreset_off(void)
{
    if (DSS(DS_driver_id) != -2) return;
    outw(0x3CE, 0x0000);
    outw(0x3CE, 0x0001);
    outw(0x3CE, 0x0003);
    outw(0x3CE, 0xFF08);
}

/* 2645:014b (near asm, copy of 21a0:252e): AX = y, BX = x -> ES:BX = DS:787A : y*28h + (x >> 3),
 * CL = (x & 7) ^ 7, AH = 1 */
static u16 span_pixel_addr(s16 y, s16 x, u8 *cl)
{
    *cl = (u8)(((u8)x & 7) ^ 7);
    return (u16)((u16)y * 0x28 + ((u16)x >> 3));
}

/* movsb with ds:si = es:di: a read (latches) and a write of the same byte */
static inline void movsb_self(u16 seg, u16 o) { vwr(seg, o, vrd(seg, o)); }

/* 2645:0034 (slot 13) — video.md §4.6: clipped horizontal span x0..x1 on row y. Writes GC0 only:
 * relies on ega_setreset_on for GC1/GC3; leaves GC0 = colour and the bit mask at the last edge. */
void ega_hspan(s16 x0, s16 x1, s16 y, s16 colour)
{
    if (DSS(DS_g_span_ymin) > y || DSS(DS_g_span_ymax) < y) return;
    s16 xmin = DSS(DS_g_span_xmin), xmax = DSS(DS_g_span_xmax);
    if (xmin > x1 || xmax < x0) return;
    if (!(x0 > xmin)) x0 = xmin;
    if (!(x1 < xmax)) x1 = xmax;
    if (x0 > x1) { s16 t = x0; x0 = x1; x1 = t; }
    outw(0x3CE, (u16)((u8)colour << 8 | 0));
    u16 seg = DSW(DS_g_draw_seg);
    u8 cl;
    u16 o = span_pixel_addr(y, x0, &cl);
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
    for (; n != 0; n--) movsb_self(seg, o++);                 /* rep movsb */
last:
    outw(0x3CE, (u16)(rm << 8 | 8));
    movsb_self(seg, o);
}

/* 2645:00e7 (slot 14) — video.md §4.6: rows y .. DS:5E52-1 of segment DS:787A in one colour */
void ega_fill_rows(s16 y, s16 colour)
{
    if (DSS(DS_g_span_ymax) < y) return;
    outw(0x3CE, (u16)((u8)colour << 8 | 0));
    outw(0x3CE, 0x0F01);
    outw(0x3CE, (u16)((DSW(DS_g_gc3) & 0xFF00) | 3));
    u16 n = (u16)((u16)(DSS(DS_g_span_ymax) - y) * 0x28);     /* mul dx: low word */
    u16 seg = DSW(DS_g_draw_seg);
    u16 o = (u16)((u16)y * 0x28);
    outw(0x3CE, 0xFF08);
    for (; n != 0; n--) movsb_self(seg, o++);                 /* rep movsb */
    outw(0x3CE, 0x0000);
    outw(0x3CE, 0x0001);
    outw(0x3CE, 0x0003);
    outw(0x3CE, 0xFF08);
}
