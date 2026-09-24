/* The 0f38 UI toolkit: message boxes 0f38:239c - 2adf (game_flow.md "Message ids"). Ported from the
 * disassembly. Message id p: DS:2C01+p = box type t (1-based) into the geometry table DS:04C4
 * (0x12 bytes: +0 picture, +2 lines, +4 x, +6 y, +8 w, +A h, +C text dx, +E text dy), the lines follow
 * from DS:2C02+p. */
#include "game/ui.h"

#include <stdio.h>
#include <string.h>

#include "host.h"
#include "platform/platform.h"
#include "platform/video.h"

#define M_SAVED     0x04FA      /* far: the background under the box (0 = none) */
#define M_GEOM      0x6C40      /* near: the box geometry record in use */
#define M_TABLE     0x04C4

static void strcpy_ds(u16 dst, const char *s) { strcpy(ds_str(dst), s); }

/* 0f38:239c: message text into DS:291F, then dialog screen 0x1D. */
void dialog_msg(const char *s)
{
    strcpy_ds(0x291F, s);
    ui_dialog(0x1D);
}

/* 0f38:23ce: s into DS:293E, "Hit Return when ready ..." (DS:0490, 26 bytes) into DS:2967, then the
 * dialog screen 0x20 drawn straight on the visible page (its five records moved 0x50 lines down, 5
 * with DS:824A), palette forced on. */
void fatal_message(const char *s)
{
    u16 save_mirror = DSW(DS_g_mirror);
    strcpy_ds(0x293E, s);
    memcpy(ds_str(0x2967), ds_str(0x0490), 0x1A);
    if (DSC(DS_g_ui_level) < 1) {
        /* PORT: text mode (before the graphics set-up): the original puts() the three strings and
         * waits for a key (key_read with the timer installed, else kbhit/getch). */
        fprintf(stderr, "%s%s\n%s\n", ds_str(0x04AA), ds_str(0x293E), ds_str(0x2967));
        while ((s8)key_read() == 0) host_pump();
        return;
    }
    if (DSW(0x58EE) != 0) {
        screen_fill_rect(0, 0, 0x140, 200, 0);
        DSW(0x58EE) = (u16)(DSW(0x58EE) + 1);
    }
    u8 vis = DSB(DS_g_pal_visible);
    if (vis == 0) {
        DSB(DS_g_pal_visible) = 1;
        drv_pal_normal();
    }
    s16 d = DSB(0x824A) != 0 ? 5 : 0x50;
    for (u16 k = 0; k < 5; k++) {
        DSW((u16)(0x1D8A + 0x12 * k)) = (u16)(DSW((u16)(0x1D8A + 0x12 * k)) + d);
        DSW((u16)(0x1D8E + 0x12 * k)) = (u16)(DSW((u16)(0x1D8E + 0x12 * k)) + d);
    }
    if (DSW(0x58EE) != 0) ega_set_palette(DS_pal_fatal_tab);
    DSW(DS_g_mirror) = 1;
    input_reset();
    FarPtr back = g_back();
    ds_far_wr(DS_g_back, g_front());
    ui_dialog(0x20);
    ds_far_wr(DS_g_back, back);
    for (u16 k = 0; k < 5; k++) {
        DSW((u16)(0x1D8A + 0x12 * k)) = (u16)(DSW((u16)(0x1D8A + 0x12 * k)) - d);
        DSW((u16)(0x1D8E + 0x12 * k)) = (u16)(DSW((u16)(0x1D8E + 0x12 * k)) - d);
    }
    DSW(DS_g_mirror) = save_mirror;
    DSB(DS_g_pal_visible) = vis;
    if (vis == 0) drv_pal_black();
}

/* 0f38:28a4 */
void msg_draw(s16 id)
{
    cursor_ctl(-3);
    FarPtr saved = ds_far(M_SAVED);
    if (!far_is_null(saved)) {                    /* a box still open: restore under it */
        u16 g = DSW(M_GEOM);
        screen_put_bitmap(saved, DSS((u16)(g + 4)), DSS((u16)(g + 6)));
        arena_pop(1);
        DSW(0x04FC) = DSW(M_SAVED) = 0;
    }
    bool once = id < 0;                           /* [bp-4] */
    if (once) id = (s16)-id;
    u16 si = (u16)(id + 0x2C02);
    s16 t = (s16)(DSC((u16)(si - 1)) - 1);
    u16 g = (u16)(0x12 * t + M_TABLE);
    DSW(M_GEOM) = g;
    s16 h = DSS((u16)(g + 0xA));                  /* [bp-0Ah] */
    s16 w = DSS((u16)(g + 8));                    /* [bp-6] */
    s16 y = DSS((u16)(g + 6));                    /* [bp-0Eh] */
    s16 x = DSS((u16)(g + 4));                    /* [bp-8] */
    FarPtr none = { 0, 0 };
    ds_far_wr(M_SAVED, screen_save_rect(x, y, w, h, none));
    pic_draw_masked(g_back(), DSS(g), (u8)x, (u8)y, 0);
    for (u16 i = 0; i < 5; i++) DSB((u16)(DS_g_text_fg + i)) = DSB((u16)(DSW(DS_g_scheme) + 0x46 + i));
    x = (s16)(x + DSS((u16)(g + 0xC)));
    y = (s16)(y + DSS((u16)(g + 0xE)));
    s16 n = DSS((u16)(g + 2));
    while (n-- != 0) {
        const char *s = ds_str(si);
        s16 tw = font_string_width(s);
        drv_draw_text(g_back(), (s16)(((s16)(w - tw) >> 1) + x), y, s);
        si = (u16)(si + strlen(s) + 1);
        y = (s16)(y + 9);
    }
    cursor_ctl(-1);
    if (DSW(DS_g_mirror) != 0) {
        g = DSW(M_GEOM);
        page_copy_rect(g_back(), g_front(), DSS((u16)(g + 4)), DSS((u16)(g + 6)),
                       (s16)(DSS((u16)(g + 4)) + w - 1), (s16)(DSS((u16)(g + 6)) + h - 1));
    }
    if (once) {
        arena_pop(1);
        DSW(0x04FC) = DSW(M_SAVED) = 0;
    }
    DSW(DS_click_returns) = 1;
}

/* 0f38:2a62: msg_draw with the geometry's x / y replaced for the call (values < 0 keep them). */
void msg_draw_at(s16 id, s16 x, s16 y)
{
    s16 a = (s16)(id < 0 ? -id : id);
    s16 t = (s16)(DSC((u16)(a + 0x2C01)) - 1);
    u16 g = (u16)(t * 0x12 + M_TABLE);
    u16 ox = DSW((u16)(g + 4)), oy = DSW((u16)(g + 6));
    if (x >= 0) DSW((u16)(g + 4)) = (u16)x;
    if (y >= 0) DSW((u16)(g + 6)) = (u16)y;
    msg_draw(id);
    DSW((u16)(g + 4)) = ox;
    DSW((u16)(g + 6)) = oy;
    DSW(DS_click_returns) = 0;
}

/* 0f38:2adf */
void msg_erase(void)
{
    FarPtr saved = ds_far(M_SAVED);
    if (!far_is_null(saved)) {
        u16 g = DSW(M_GEOM);
        screen_put_bitmap_mirror(saved, DSS((u16)(g + 4)), DSS((u16)(g + 6)));
        arena_pop(1);
        DSW(0x04FC) = DSW(M_SAVED) = 0;
    }
    DSW(DS_click_returns) = 0;
}
