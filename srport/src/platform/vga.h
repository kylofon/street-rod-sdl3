#pragma once
/* VGA register model (video.md §4.1, §4.3, §6): the port base of every routine that touches the video
 * card. The original programs the card directly and many routines rely on registers left by a previous
 * one (0e6c:0175 never writes the sequencer index, 2645:0034 never enables set/reset, 0f38:65ae leaves
 * GC4 at the cache plane ...), so every routine is transcribed with vga_out()/vga_in() and the video
 * memory accesses vrd()/vwr(); the plane contents then come out bit-exact.
 *
 * Modelled: sequencer map mask (SEQ2), graphics controller set/reset (GC0), enable set/reset (GC1),
 * colour compare (GC2), data rotate / function (GC3, rotate count ignored: always 0 in the game), read
 * map select (GC4), mode (GC5: write modes 0/1/2, read modes 0/1), colour don't care (GC7), bit mask
 * (GC8), the four latches, CRTC start address (0Ch/0Dh), line compare (18h + 07h bit 4 + 09h bit 6,
 * 400 scan lines) and offset (13h, stored only), input status 1 (3DAh: vertical retrace from the host
 * clock). The attribute palette goes through ega_set_palette_reg() (INT 10h AX=10xxh, video.md §4.10).
 *
 * Segments 0xA000 <= seg < 0xB000 are video memory: linear offset (seg - 0xA000) * 16 + off, taken
 * modulo 64 KB (A200:0000 = plane offset 2000h). Any other segment is mem[]. */
#include "mem.h"

typedef struct {
    u8 seq_index, gc_index, crtc_index;   /* last index written to 3C4h / 3CEh / 3D4h */
    u8 map_mask;     /* SEQ2 (0Fh after the mode set) */
    u8 set_reset;    /* GC0 */
    u8 enable_sr;    /* GC1 */
    u8 compare;      /* GC2 colour compare */
    u8 func;         /* GC3: bits 3-4 0 replace, 1 AND, 2 OR, 3 XOR */
    u8 read_map;     /* GC4 */
    u8 mode;         /* GC5: bits 0-1 write mode, bit 3 read mode */
    u8 dont_care;    /* GC7 (0Fh after the mode set; never written by the game) */
    u8 bit_mask;     /* GC8 (FFh after the mode set) */
    u8 latch[4];
    u8 crtc[0x19];   /* CRTC registers 00h-18h as last written (mode 0Dh values after the mode set) */
} VgaRegs;

extern VgaRegs vga;

/* INT 10h AH=00h AL=0Dh as far as the port needs it: clears the four planes, resets the registers
 * above to the mode 0Dh values and loads the default palette (0..7, 10h..17h, overscan 0). */
void vga_mode_0d(void);

/* out dx,al: 3C4h/3CEh/3D4h store the index, 3C5h/3CFh/3D5h the data register selected by it.
 * Other ports are ignored. */
void vga_out(u16 port, u8 v);
/* out dx,ax = vga_out(port, al); vga_out(port + 1, ah) */
void vga_outw(u16 port, u16 ax);
/* in al,dx: 3DAh (bit 3 vertical retrace, bit 0 display disabled, from the host's 70 Hz frame clock),
 * 3C5h/3CFh/3D5h (VGA readback of the selected register). Others read FFh. */
u8   vga_in(u16 port);

/* Retrace waits as the original's port 3DAh polling loops (video.md §4.3). The port presents the frame
 * at these points (host_wait_vretrace pumps the host). */
void vga_wait_retrace_edge(void);   /* while (in(3DAh) & 8) ; while (!(in(3DAh) & 8)) ;  (0f38:177d, 21a0:0014, 21a0:182e) */
void vga_wait_in_retrace(void);     /* while (!(in(3DAh) & 8)) ;  no edge wait (0f38:1f4b) */
void vga_wait_blank_then_retrace(void); /* while (in(3DAh) & 1) ; while (!(in(3DAh) & 8)) ;  (21a0:0006) */

/* CPU read of video memory offset off: loads the 4 latches; read mode 0 returns plane[read_map & 3],
 * read mode 1 the colour-compare result. */
u8   vga_rd(u16 off);
/* CPU write of video memory offset off (write modes 0/1/2, function, bit mask against the latches,
 * map mask). */
void vga_wr(u16 off, u8 v);

static inline bool is_vram(u16 seg) { return seg >= 0xA000 && seg < 0xB000; }
static inline u16  vram_off(u16 seg, u16 off) { return (u16)(((seg - 0xA000u) << 4) + off); }

/* Byte access through seg:off: video memory goes through the register model, anything else is mem[]. */
static inline u8 vrd(u16 seg, u16 off) { return is_vram(seg) ? vga_rd(vram_off(seg, off)) : rd8(seg, off); }
static inline void vwr(u16 seg, u16 off, u8 v)
{
    if (is_vram(seg)) vga_wr(vram_off(seg, off), v);
    else wr8(seg, off, v);
}
/* x86 word access = two byte cycles, low byte first (each loads the latches on VRAM). */
static inline u16 vrd16(u16 seg, u16 off) { u8 lo = vrd(seg, off); return (u16)(lo | vrd(seg, (u16)(off + 1)) << 8); }
static inline void vwr16(u16 seg, u16 off, u16 v) { vwr(seg, off, (u8)v); vwr(seg, (u16)(off + 1), (u8)(v >> 8)); }
static inline u8 vrdp(FarPtr p) { return vrd(p.seg, p.off); }
static inline void vwrp(FarPtr p, u8 v) { vwr(p.seg, p.off, v); }

/* 1e16:1dc8 movedata(srcseg, srcoff, dstseg, dstoff, n): MS C runtime rep movsb, byte by byte through
 * vrd/vwr (so VRAM -> VRAM copies in write mode 1 copy all four planes through the latches). Offsets
 * wrap inside their segment. */
void vmovedata(u16 sseg, u16 soff, u16 dseg, u16 doff, u16 n);

/* The port's port-I/O helpers under the names used in the transcriptions. */
static inline void out(u16 port, u8 v) { vga_out(port, v); }
static inline void outw(u16 port, u16 ax) { vga_outw(port, ax); }
static inline u8 in(u16 port) { return vga_in(port); }
