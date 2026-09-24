#pragma once
/* Video: the EGA/VGA 16-colour driver of SR.EXE and the drawing primitives, public API for the game code.
 *
 * Implements port/spec/video.md (VGA path: DS:8236 = -2, DS:0254 = 1):
 *   §1.2 / §4.1   driver vector table DS:78A2 (drv_* calls below), the VGA register model (vga.h)
 *   §4.2          bitmap / page descriptors, clip_rect 0f38:1680
 *   §4.3          CRTC: split screen 21a0:0014, page flip 21a0:182e, retrace waits 21a0:0006, 0f38:177d
 *   §4.4 - §4.7   every EGA routine of segment 21a0 (slots 0-6, 9-12), the helpers 0e6c:0175, 0e92:0006,
 *                 24e7:0008/008b, 24f4:000a, 2595:0004, 2634:000a/00c1, 2645:0008/001d/0034/00e7
 *   §4.8          gfx_init 0f38:1859, gfx_screen_mode 0f38:0dbd
 *   §4.9          0f38 primitives: fonts, rectangles, frames, lines, page copies, bitmaps, screen_*,
 *                 the status line (2022/20a1/2213), full-screen pictures (2554/2638), rect helpers
 *   §4.10         palette (1f4b/1fa4, slots 7/8, 17fc-182c) and the mouse pointer (2b28/2d7e/2e8b)
 *   §5            descriptor layout, font, tables (all read from mem[])
 *   §6 / §7       hardware replacements and timing (srport/PORTING.md)
 *
 * Conventions: see platform.h. Descriptors are far pointers (FarPtr) to 48-byte records in mem[];
 * the screen pages have their planes in video memory (segments A000h-AFFFh, the register model).
 * Rects and other structures the original passes as near pointers to the caller's stack are C
 * pointers here; structures that live in DGROUP (animation records, rect lists) are u16 DS offsets.
 * Colours are pixel values 0..15 (shown through the palette registers). */
#include "mem.h"
#include "symbols.h"

/* ================================================================================ descriptors */

/* Bitmap / page descriptor, 0x30 bytes (video.md §4.2, platform.md §5.5). */
#define DESC_W      0x00    /* u16 width in pixels */
#define DESC_H      0x02    /* u16 height */
#define DESC_SIZE   0x04    /* u16 RAM bitmap: bytes of all colour planes; screen page: 1F40h */
#define DESC_PLANES 0x24    /* far plane 0 (plane k at + k*stride*h; screen: A000:off, planes parallel) */
#define DESC_MASK   0x28    /* far mask (1 bit = transparent), 0:0 = none; same segment as the planes */
#define DESC_STRIDE 0x2C    /* u16 bytes per row ((w-1) >> DS:823A) + 1; 28h for screen pages */
#define DESC_TYPE   0x2E    /* u8 FEh = EGA screen page; RAM bitmaps: the drv byte of bitmap_alloc */
#define DESC_BYTES  0x30

#define DESC_TYPE_EGA 0xFE

static inline u16    desc_w(FarPtr d)      { return rd16(d.seg, d.off); }
static inline u16    desc_h(FarPtr d)      { return rd16(d.seg, (u16)(d.off + DESC_H)); }
static inline u16    desc_size(FarPtr d)   { return rd16(d.seg, (u16)(d.off + DESC_SIZE)); }
static inline FarPtr desc_planes(FarPtr d) { return far_rd(d.seg, (u16)(d.off + DESC_PLANES)); }
static inline FarPtr desc_mask(FarPtr d)   { return far_rd(d.seg, (u16)(d.off + DESC_MASK)); }
static inline u16    desc_stride(FarPtr d) { return rd16(d.seg, (u16)(d.off + DESC_STRIDE)); }
static inline u8     desc_type(FarPtr d)   { return rd8(d.seg, (u16)(d.off + DESC_TYPE)); }

/* Current pages: DS:822A front (visible, page A), DS:822E back (drawn into, page B); DS:7316[4] the
 * page table (page 0, 1, A000:4000, A000:6000). */
static inline FarPtr g_front(void) { return ds_far(DS_g_front); }
static inline FarPtr g_back(void)  { return ds_far(DS_g_back); }
static inline FarPtr g_page(int i) { return ds_far((u16)(DS_g_page + 4 * i)); }

/* Driver rectangle (video.md §4.2): x in pixels on input (clip_rect converts to bytes). */
typedef struct { s16 w, h, sx, sy, dx, dy; } Rect;

/* ============================================================== driver vector table DS:78A2 (§1.2) */

/* ega_install_vectors (21a0:1128) copies the 15 far pointers of DS:5BD0 into DS:78A2, as the
 * original; every target is registered in codeptr. The game calls the driver only through these
 * wrappers, which look up the far pointer currently stored in the slot and call its C function. */
u16  drv_blit(FarPtr src, FarPtr dst, const Rect *r, u8 flags);            /* slot 0  [78A2] 21a0:1862 */
void drv_blit_shifted(FarPtr src, FarPtr dst, const Rect *r);              /* slot 1  [78A6] 21a0:223b */
s16  drv_blit_masked(FarPtr src, FarPtr dst, const Rect *r, s16 mode);     /* slot 2  [78AA] 21a0:1cd5 */
void drv_draw_text(FarPtr d, s16 x, s16 y, const char *s);                 /* slot 3  [78AE] 21a0:113a */
void drv_draw_cursor(FarPtr img, FarPtr mask, FarPtr save, FarPtr scr,
                     s16 xb, s16 y, s16 wb, s16 rows);                     /* slot 4  [78B2] 0e6c:0175 */
s16  drv_copy_page(FarPtr src, FarPtr dst);                                /* slot 5  [78B6] 21a0:16dc */
void drv_recolour_rect(FarPtr d, s16 from, s16 to, s16 x0, s16 y0, s16 x1, s16 y1); /* slot 6 [78BA] 21a0:1508 */
void drv_pal_black(void);                                                  /* slot 7  [78BE] 0f38:0b2d */
void drv_pal_normal(void);                                                 /* slot 8  [78C2] 0f38:0b3f */
s16  drv_anim_step(u16 anim);                                              /* slot 9  [78C6] 21a0:25d9 */
s16  drv_blinds(FarPtr dst, FarPtr src);                                   /* slot 10 [78CA] 21a0:1722 */
void drv_slide_sprite(s16 top, s16 bottom, s16 x, s16 from, s16 to, FarPtr spr); /* slot 11 [78CE] 21a0:28ec */
void drv_line(s16 x0, s16 y0, s16 x1, s16 y1, s16 colour);                 /* slot 12 [78D2] 21a0:23bc */
void drv_hspan(s16 x0, s16 x1, s16 y, s16 colour);                         /* slot 13 [78D6] 2645:0034 */
void drv_fill_rows(s16 y, s16 colour);                                     /* slot 14 [78DA] 2645:00e7 */

/* Registers the 15 slot targets (and the other video code pointers) in codeptr; called once at start. */
void video_register_codeptrs(void);

/* ======================================================= EGA driver, segment 21a0 (§4.3 - §4.7) */

void ega_wait_vretrace(void);                 /* 21a0:0006: wait while 3DAh bit0, then until bit3 */
/* 21a0:0014: CRTC line compare after a retrace; -1 = off; VGA adds 100 when <= 200 (race: 0x63 puts
 * rows 100-199 from offset 0 = page 0 rows 0-99 below the road). */
void ega_set_split(s16 line);
void ega_install_vectors(void);               /* 21a0:1128: copy DS:5BD0 -> DS:78A2 (60 bytes) */
/* 21a0:113a (slot 3): string in the font DS:6A2A, write mode 2, bg DS:8243 under the cell, fg DS:8242;
 * end x -> DS:824C, y -> DS:824E. */
void ega_draw_text(FarPtr d, s16 x, s16 y, const char *s);
/* 21a0:1508 (slot 6): replace colour `from` by `to` in [x0,x1) x [y0,y1). */
void ega_recolour_rect(FarPtr d, s16 from, s16 to, s16 x0, s16 y0, s16 x1, s16 y1);
s16  ega_copy_page(FarPtr src, FarPtr dst);   /* 21a0:16dc (slot 5): latched copy of 8000 bytes, returns 0 */
s16  ega_blinds(FarPtr dst, FarPtr src);      /* 21a0:1722 (slot 10): 5 interleaved passes, >= 3 ticks each */
void ega_show_page(s16 page);                 /* 21a0:182e: CRTC start = page * 2000h, wait for the retrace */
/* 21a0:1862 (slot 0): clipped copy; flags bit0 colour planes, bit1 the mask too; screen->screen via
 * the latches. */
u16  ega_blit(FarPtr src, FarPtr dst, const Rect *r, u8 flags);
/* 21a0:1ae9: per plane dst = (bg & mask) | sprite, then `tail` rows through the latches (anim_step). */
typedef struct { u16 off, seg, unused4, rows; } Tail;
s16  ega_sprite_over_bg(FarPtr spr, FarPtr dst, FarPtr bg, const Tail *t, const Rect *r);
/* 21a0:1cd5 (slot 2): transparent blit (colour 0 / mask) via 2634:000a (mode 0) or 2595:0004. */
s16  ega_blit_masked(FarPtr src, FarPtr dst, const Rect *r, s16 mode);
void ega_save_rect(FarPtr src, s16 w, s16 h, FarPtr dst, s16 sstride);    /* 21a0:1e2d */
void ega_blit_shifted(FarPtr src, FarPtr dst, const Rect *r);              /* 21a0:223b (slot 1), unclipped */
void ega_line(s16 x0, s16 y0, s16 x1, s16 y1, s16 colour);                 /* 21a0:23bc (slot 12) into DS:787A */
s16  ega_anim_step(u16 anim);                 /* 21a0:25d9 (slot 9): Anim record at DS:anim (video.md §4.7) */
void ega_slide_sprite(s16 top, s16 bottom, s16 x, s16 from, s16 to, FarPtr spr); /* 21a0:28ec (slot 11) */

/* ============================================================================ helper segments */

void ega_draw_cursor(FarPtr img, FarPtr mask, FarPtr save, FarPtr scr,
                     s16 xb, s16 y, s16 wb, s16 rows);                     /* 0e6c:0175 (slot 4) */
/* 0e92:0006: picture unpacker (FORMATS.md). Reads src through vrd (the VRAM picture cache is read with
 * the read map select set by pic_decode) and writes dst through vwr. tokens = the picture's 16 run
 * tokens (a stack copy in the original). Returns the number of bytes written. */
u16  pic_unpack(FarPtr src, FarPtr dst, u16 n, const u8 *tokens, u16 ntok);
void ega_latch_copy(FarPtr s, FarPtr d, s16 wb, s16 h, s16 xb, s16 y, s16 stride);   /* 24e7:0008 */
void ega_latch_copy_to40(FarPtr s, s16 wb, s16 h, FarPtr d, s16 sstride);           /* 24e7:008b */
/* One plane each; src / mask / bg are offsets in segment seg (video.md §4.4). */
void rows_composite(FarPtr d, u16 seg, u16 s, u16 m, u16 bg, s16 sstr, s16 wb, s16 h, s16 bgstr, s16 dstr); /* 24f4:000a */
void rows_mask_or(FarPtr d, u16 seg, u16 s, u16 m, s16 sstr, s16 wb, s16 h, s16 dstr);    /* 2595:0004 */
void rows_mask_blend(FarPtr d, u16 seg, u16 s, u16 m, s16 sstr, s16 wb, s16 h, s16 dstr); /* 2634:000a */
void make_mask_plane(FarPtr planes, FarPtr mask, u16 total);               /* 2634:00c1 */
void ega_setreset_on(void);                   /* 2645:0008: GC1 = 0Fh, GC3 = hi(DS:5C1E) (EGA only) */
void ega_setreset_off(void);                  /* 2645:001d */
void ega_hspan(s16 x0, s16 x1, s16 y, s16 colour);   /* 2645:0034 (slot 13): clip DS:5E4E-5E54, GC0 only */
void ega_fill_rows(s16 y, s16 colour);        /* 2645:00e7 (slot 14): rows y .. DS:5E52-1 of DS:787A */

/* ================================================================ set-up, palette (§4.8, §4.10) */

void gfx_init(FarPtr a, FarPtr b);            /* 0f38:1859: main passes 2e3e:0000, 2e3e:0030 */
/* 0f38:0dbd: 0 save pages, 1/4 restore + split off + start 0, 2 start 0, 3 single buffer. Returns AX
 * as the original (callers ignore it). */
u16  gfx_screen_mode(s16 n);
s16  clip_rect(FarPtr src, FarPtr dst, const Rect *in, Rect *out);         /* 0f38:1680: 1 if non-empty */
void far_memset(FarPtr p, u8 v, u16 n);       /* 0f38:1640 */
void far_memcpy(FarPtr src, FarPtr dst, u16 n);  /* 0f38:b502: movedata */
void wait_vretraces(s16 n);                   /* 0f38:177d: n x start of vertical retrace */
s16  font_string_width(const char *s);        /* 0f38:17c8: font DS:691A */

void ega_set_palreg(s16 idx, s16 v);          /* 0f38:1f4b: shadow DS:0440[idx], wait in retrace, AX=1000h/1001h */
void ega_set_palette(u16 table);              /* 0f38:1fa4: 16 values at DS:table (> 7 |= 10h) + overscan DS:8248 */
void pal_black(void);                         /* 0f38:0b2d (slot 7): palette DS:0452, DS:8249 = 0 */
void pal_normal(void);                        /* 0f38:0b3f (slot 8): palette DS:0440, DS:8249 = 1 */
void pal_reg12_5(void);                       /* 0f38:17fc: ega_set_palreg(0Ch, 5) */
void pal_reg12_13(void);                      /* 0f38:180c: ega_set_palreg(0Ch, 0Dh) */
void pal_shadow_patch(void);                  /* 0f38:181c: DS:0446..0448 = 09 01 03 (shadow only) */
void pal_shadow_swap(void);                   /* 0f38:182c: swap 17 bytes DS:0440 <-> DS:0462 */

/* ======================================================================= 0f38 primitives (§4.9) */

/* Lines and rectangles through slot 12 into the descriptor's segment (DS:787A). Colours: DS:8244
 * fill, DS:8245 line, DS:8246 frame style (0 none 1 single 2 double 3 solid). */
void fill_rect(FarPtr d, s16 x, s16 y, s16 w, s16 h);       /* 0f38:4302 */
void vline(FarPtr d, s16 x, s16 y, s16 h);                  /* 0f38:439c */
void hline(FarPtr d, s16 x, s16 y, s16 w);                  /* 0f38:43ff */
void draw_frame3(FarPtr d, s16 x, s16 y, s16 w, s16 h);     /* 0f38:4432: 3-px frame outside the rect */
void draw_box1(FarPtr d, s16 x, s16 y, s16 w, s16 h);       /* 0f38:45d7: 1-px outline outside the rect */
/* 0f38:b5aa: inclusive, byte-aligned rect copy via slot 0 (MIRROR = page B -> page A). */
void page_copy_rect(FarPtr s, FarPtr d, s16 x0, s16 y0, s16 x1, s16 y1);

FarPtr arena_bitmap_alloc(s16 w, s16 h, s16 flags, s16 pool);              /* 0f38:9fce */
void   bitmap_blit_at(FarPtr d, FarPtr s, s16 dx, s16 dy, s16 flags);      /* 0f38:b31f: whole bitmap s -> d, slot 0 (dst first) */
FarPtr bitmap_extract(FarPtr s, const s16 *r, s16 pool);                   /* 0f38:b51e: r = {w, h, x, y} */
void   bitmap_copy_into(FarPtr s, FarPtr d, s16 flags);                    /* 0f38:ab50 */
void   bitmap_opaque_extent(FarPtr b, s16 *left, s16 *right);              /* 0f38:abc2 */
FarPtr bitmap_make_shifted(FarPtr s, s16 shift, FarPtr d);                 /* 0f38:acbf */
void   bitmap_blit_masked_shift(FarPtr s, FarPtr d, const Rect *r, s16 and_mask); /* 0f38:af50 */
void   bitmap_composite_behind(FarPtr s, FarPtr d, s16 x, s16 y);          /* 0f38:b0f2 */
void   pic_blit_at(FarPtr d, s16 id, u8 x, u8 y);                          /* 0f38:b360: pic_get + bitmap_blit_at(.., 3) */
void   pic_draw_masked(FarPtr d, s16 id, u8 x, u8 y, s16 flag);            /* 0f38:b3b4 */
void   mask_and_rect(FarPtr s, FarPtr d, const Rect *r);                   /* 0f38:b41b: d.mask &= s.mask in r */

/* Rect helpers ({x0,y0,x1,y1} s16[4], inclusive), used by the sprite animations. C pointers; a
 * DGROUP rect is (s16 *)mp(DGROUP, off). Return out or NULL like the original. */
s16 *rect_intersect(const s16 *a, const s16 *b, s16 *out);                 /* 0f38:a0fa */
s16 *rect_clip_to_view(s16 x, s16 y, s16 w, s16 h, s16 *out);              /* 0f38:a198 */
void rect_clip_make(s16 w, s16 h, s16 x, s16 y, s16 maxw, s16 maxh, s16 *out);  /* 0f38:a1ee */
s16 *rect_subtract_edge(const s16 *a, const s16 *b, s16 *out, s16 horiz);  /* 0f38:a4b9 */
void sprite_anim_advance(u16 anim);                                        /* 0f38:aa37 */
s16  sprite_hspan_clip(s16 x, s16 w, u16 anim, s16 *out);                  /* 0f38:aac2 */

/* Screen helpers: page B (DS:822E) + MIRROR to page A when DS:8240, pointer hidden meanwhile. */
void   screen_fill_rect(s16 x, s16 y, s16 w, s16 h, u8 c);                 /* 0f38:231a */
void   screen_text(s16 x, s16 y, const char *s);                           /* 0f38:2656 */
void   screen_hline(s16 x, s16 y, s16 w);                                  /* 0f38:26a4 */
FarPtr screen_save_rect(s16 x, s16 y, s16 w, s16 h, FarPtr dst);           /* 0f38:26f2 (dst NULL: arena pool 2) */
void   screen_put_bitmap(FarPtr b, s16 x, s16 y);                          /* 0f38:27ef */
void   screen_put_bitmap_mirror(FarPtr b, s16 x, s16 y);                   /* 0f38:2853 */

/* Status line (colour scheme DS:787E +37h..+3Bh). */
void status_init(s16 y);                      /* 0f38:2022: status line at row y, clears 320x10 */
void status_print(s16 x, const char *s, s16 w);  /* 0f38:20a1: x < 0 centred in w (0 = DS:8254) */
void status_label(const char *s);             /* 0f38:2213: label at x = 0, then the bankroll */
void money_add(s16 d);                        /* 0f38:21c0: DS:7EAA (s32) += d, redisplay */

/* Full-screen pictures (title, screens): pic_get into the arena, slot 0 to page B at (DS:04BE,
 * DS:04C0) clipped to DS:04C2 rows, MIRROR, arena_pop(1). clr >= 0: clear the screen first. */
void show_picture(s16 id, s16 clr);           /* 0f38:2554 */
void show_picture_at(s16 id, s16 x, s16 y, s16 clr);  /* 0f38:2638 */

/* ========================================================================= mouse pointer (§4.10) */

void cursor_init(void);                       /* 0f38:2b28: pre-shifted pointer bank (LIB2 picture 2) */
/* 0f38:2d7e: op >= 0 shape op; -1/-2 show (++DS:4733, -2 also page A); -3/-4 hide. */
void cursor_ctl(s16 op);
void cursor_redraw(s16 x, s16 y);             /* 0f38:2e8b: restore save-under, slot 4, mirror */
u16  cursor_shape_offset(s16 n);              /* 0f38:2b1c: n * DS:7B06 */
