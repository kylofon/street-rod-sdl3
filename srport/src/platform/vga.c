/* VGA register model — video.md §4.1, §4.3, §6 (see vga.h). */
#include "platform/vga.h"

#include <string.h>

#include "host.h"
#include "platform/ega.h"

VgaRegs vga;

/* CRTC registers 00h-18h of BIOS mode 0Dh on a VGA (400 scan lines, double scanned). */
static const u8 crtc_mode_0d[0x19] = {
    0x2D, 0x27, 0x28, 0x90, 0x2B, 0x80, 0xBF, 0x1F, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x8E, 0x8F, 0x14, 0x00, 0x96, 0xB9, 0xE3, 0xFF,
};

static void crtc_apply(void)
{
    ega_set_start((u16)(vga.crtc[0x0C] << 8 | vga.crtc[0x0D]));
    /* Line compare (10 bits on the VGA): scan lines after L restart at offset 0. Mode 0Dh is double
     * scanned, so screen row r shows scan lines 2r and 2r+1: the first row from offset 0 is
     * ceil((L + 1) / 2). */
    u16 l = (u16)(vga.crtc[0x18] | (vga.crtc[0x07] >> 4 & 1) << 8 | (vga.crtc[0x09] >> 6 & 1) << 9);
    int first = (l + 2) / 2;
    ega_set_line_compare(first >= 200 ? 200 : first);
}

void vga_mode_0d(void)
{
    static const u8 pal[17] = { 0, 1, 2, 3, 4, 5, 6, 7, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0 };
    for (int k = 0; k < 4; k++) memset(ega_plane(k), 0, EGA_PLANE_SIZE);
    memset(&vga, 0, sizeof vga);
    vga.map_mask = 0x0F;
    vga.dont_care = 0x0F;
    vga.bit_mask = 0xFF;
    memcpy(vga.crtc, crtc_mode_0d, sizeof vga.crtc);
    for (int i = 0; i < 17; i++) ega_set_palette_reg(i, pal[i]);
    crtc_apply();
    ega_touch();
}

void vga_out(u16 port, u8 v)
{
    switch (port) {
    case 0x3C4: vga.seq_index = v; break;
    case 0x3C5:
        if ((vga.seq_index & 7) == 2) vga.map_mask = v & 0x0F;
        break;
    case 0x3CE: vga.gc_index = v; break;
    case 0x3CF:
        switch (vga.gc_index & 0x0F) {
        case 0: vga.set_reset = v & 0x0F; break;
        case 1: vga.enable_sr = v & 0x0F; break;
        case 2: vga.compare = v & 0x0F; break;
        case 3: vga.func = v; break;
        case 4: vga.read_map = v & 3; break;
        case 5: vga.mode = v; break;
        case 7: vga.dont_care = v & 0x0F; break;
        case 8: vga.bit_mask = v; break;
        default: break;
        }
        break;
    case 0x3D4: vga.crtc_index = v; break;
    case 0x3D5:
        if (vga.crtc_index < sizeof vga.crtc) {
            vga.crtc[vga.crtc_index] = v;
            crtc_apply();
        }
        break;
    default: break;
    }
}

void vga_outw(u16 port, u16 ax)
{
    vga_out(port, (u8)ax);
    vga_out((u16)(port + 1), (u8)(ax >> 8));
}

u8 vga_in(u16 port)
{
    switch (port) {
    case 0x3DA: {
        bool vr = host_in_vretrace();
        return (u8)((vr ? 0x08 : 0) | (vr ? 0x01 : 0));
    }
    case 0x3C5: return (vga.seq_index & 7) == 2 ? vga.map_mask : 0;
    case 0x3CF:
        switch (vga.gc_index & 0x0F) {
        case 0: return vga.set_reset;
        case 1: return vga.enable_sr;
        case 2: return vga.compare;
        case 3: return vga.func;
        case 4: return vga.read_map;
        case 5: return vga.mode;
        case 7: return vga.dont_care;
        case 8: return vga.bit_mask;
        default: return 0;
        }
    case 0x3D5: return vga.crtc_index < sizeof vga.crtc ? vga.crtc[vga.crtc_index] : 0;
    default: return 0xFF;
    }
}

void vga_wait_retrace_edge(void)
{
    host_wait_vretrace();
}

void vga_wait_in_retrace(void)
{
    if (!host_in_vretrace()) host_wait_vretrace();
}

void vga_wait_blank_then_retrace(void)
{
    host_wait_vretrace();
}

u8 vga_rd(u16 off)
{
    for (int k = 0; k < 4; k++) vga.latch[k] = ega_plane(k)[off];
    if (vga.mode & 0x08) {                               /* read mode 1: colour compare */
        u8 differ = 0;
        for (int k = 0; k < 4; k++) {
            if (!(vga.dont_care >> k & 1)) continue;
            u8 want = (vga.compare >> k & 1) ? 0xFF : 0x00;
            differ |= (u8)(vga.latch[k] ^ want);
        }
        return (u8)~differ;
    }
    return vga.latch[vga.read_map & 3];
}

void vga_wr(u16 off, u8 v)
{
    for (int k = 0; k < 4; k++) {
        if (!(vga.map_mask >> k & 1)) continue;
        u8 *p = &ega_plane(k)[off];
        u8 x;
        switch (vga.mode & 3) {
        case 1:                                          /* latches, no function / bit mask */
            *p = vga.latch[k];
            continue;
        case 2:
            x = (v >> k & 1) ? 0xFF : 0x00;
            break;
        case 3:                                          /* VGA write mode 3: never used by the game */
            x = (vga.set_reset >> k & 1) ? 0xFF : 0x00;
            break;
        default:
            x = (vga.enable_sr >> k & 1) ? ((vga.set_reset >> k & 1) ? 0xFF : 0x00) : v;
            break;
        }
        u8 l = vga.latch[k];
        switch (vga.func >> 3 & 3) {
        case 1: x &= l; break;
        case 2: x |= l; break;
        case 3: x ^= l; break;
        default: break;
        }
        u8 m = vga.bit_mask;
        if ((vga.mode & 3) == 3) m &= v;
        *p = (u8)((x & m) | (l & (u8)~m));
    }
    ega_touch();
}

void vmovedata(u16 sseg, u16 soff, u16 dseg, u16 doff, u16 n)
{
    while (n--) {
        u8 b = vrd(sseg, soff);
        vwr(dseg, doff, b);
        soff++;
        doff++;
    }
}
