#pragma once
/* EGA/VGA 16-colour display memory and scan-out (BIOS mode 0Dh, 320x200, as driver 21a0 programs it
 * with DS:0254 = 1). video.md §1.1, §4.3, §4.10.
 *
 * Video memory is four 64 KB planes (not in mem[], see mem.h). Offset o of segments A000h-AFFFh
 * addresses byte o of every plane; a byte holds 8 pixels, MSB leftmost; plane k supplies bit k of the
 * colour. The screen is 40 bytes per line starting at the CRTC start address; scan lines past the CRTC
 * line compare restart at offset 0 (the dashboard split screen, 21a0:0014). Colours go through the 16
 * attribute-controller palette registers, which the game sets with INT 10h AX=1002h/1000h (pal_set
 * 0f38:1fa4, pal_reg 0f38:1f4b); the DAC is never programmed.
 *
 * This module owns the plane memory, the palette registers and the scan-out. The CPU-side access
 * through the sequencer / graphics controller (map mask, set/reset, write modes, latches ...) is the
 * register model in platform/vga.h; game and video code never write the planes directly. */
#include "types.h"

#define EGA_PLANE_SIZE 0x10000u
#define EGA_BYTES_PER_LINE 40

void ega_init(void);                         /* installs the frame source, clears the planes */
u8  *ega_plane(int plane);                   /* plane 0..3, EGA_PLANE_SIZE bytes (vga.c, compose) */

void ega_set_start(u16 offset);              /* CRTC start address (bytes) */
void ega_set_line_compare(int line);         /* first screen row (0..199) shown from offset 0; >= 200 = off */
void ega_touch(void);                        /* marks the screen changed (after writes to the planes) */

/* Palette register 0..15 = value as the attribute controller holds it: bits 0-2 = BGR, bit 4 =
 * intensity (the 200-line interpretation of mode 0Dh; the game ORs 10h into values 8..15), register 16 =
 * overscan (stored, not shown: the port window has no border). */
void ega_set_palette_reg(int reg, u8 value);
u8   ega_get_palette_reg(int reg);

/* The 16 IRGB colours as XRGB8888. */
extern const u32 ega_palette[16];
