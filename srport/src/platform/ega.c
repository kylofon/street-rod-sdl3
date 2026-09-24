#include "platform/ega.h"

#include <string.h>

#include "host.h"

static u8 planes[4][EGA_PLANE_SIZE];
static u16 start_addr;
static int line_compare = 400;
static bool dirty = true;

const u32 ega_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

static bool compose(u32 *xrgb)
{
    if (!dirty) return false;
    dirty = false;
    for (int y = 0; y < 200; y++) {
        u16 row = y < line_compare ? (u16)(start_addr + y * EGA_BYTES_PER_LINE)
                                   : (u16)((y - line_compare) * EGA_BYTES_PER_LINE);
        u32 *out = xrgb + y * 320;
        for (int b = 0; b < EGA_BYTES_PER_LINE; b++) {
            u16 o = (u16)(row + b);
            u8 p0 = planes[0][o], p1 = planes[1][o], p2 = planes[2][o], p3 = planes[3][o];
            for (int bit = 7; bit >= 0; bit--) {
                int c = (p0 >> bit & 1) | (p1 >> bit & 1) << 1 | (p2 >> bit & 1) << 2 | (p3 >> bit & 1) << 3;
                *out++ = ega_palette[c];
            }
        }
    }
    return true;
}

void ega_init(void)
{
    memset(planes, 0, sizeof planes);
    start_addr = 0;
    line_compare = 400;
    dirty = true;
    host_set_frame_source(compose, 320, 200);
}

u8 *ega_plane(int plane) { return planes[plane & 3]; }

void ega_set_start(u16 offset)
{
    if (offset != start_addr) { start_addr = offset; dirty = true; }
}

void ega_set_line_compare(int line)
{
    if (line != line_compare) { line_compare = line; dirty = true; }
}

void ega_touch(void) { dirty = true; }
