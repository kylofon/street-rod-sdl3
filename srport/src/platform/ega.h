#pragma once
/* EGA/VGA 16-colour model (BIOS mode 0Dh, 320x200, as driver 21a0 programs it with DS:0254 = 1).
 *
 * Video memory is four 64 KB planes (not in mem[], see mem.h). Offset o of segment A000h addresses
 * byte o of every plane; a byte holds 8 pixels, MSB leftmost; plane k supplies bit k of the colour.
 * The screen is 40 bytes per line starting at the CRTC start address; lines past the CRTC line
 * compare wrap to offset 0 (the dashboard split screen, 21a0:0014). Colours go through the 16 EGA
 * palette registers, which the game sets with INT 10h AX=1002h/1000h (pal_set 0f38:1fa4, pal_reg
 * 0f38:1f4b); the DAC is never programmed.
 *
 * The graphics driver (video spec) implements the sequencer map mask, the graphics controller modes
 * and the latches on top of ega_plane(); this module only owns the memory and the scan-out. */
#include "types.h"

#define EGA_PLANE_SIZE 0x10000u
#define EGA_BYTES_PER_LINE 40

void ega_init(void);                         /* installs the frame source, clears the planes */
u8  *ega_plane(int plane);                   /* plane 0..3, EGA_PLANE_SIZE bytes */

void ega_set_start(u16 offset);              /* CRTC start address (bytes) */
void ega_set_line_compare(int line);         /* first line (0..199) shown from offset 0; >= 200 = off */
void ega_touch(void);                        /* marks the screen changed (after writes to the planes) */

/* Palette register (0..15) = value as the game writes it: IRGB with bit 4 as intensity for the
 * 200-line modes (the game ORs 10h into values 8..15). */
void ega_set_palette_reg(int reg, u8 value);

/* The 16 IRGB colours as XRGB8888. */
extern const u32 ega_palette[16];
