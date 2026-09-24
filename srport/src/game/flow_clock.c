/* game_flow: the summer calendar and the game clock — port/spec/game_flow.md §4.7, §4.9, §7
 * (0000:6476-6750, 0f38:0d62).
 *
 * The date is T = game_clock DS:05FC + (rt_ticks DS:0600 - rt_base DS:0604) (u32): activities add 0x222
 * per game hour, real time adds 18.2 per second (one game hour = 30 s). Day 0 is Sunday, June 16 1963;
 * the summer is over after day 90. */
#include "game/flow.h"

#include <stdio.h>

#include "game/garage.h"
#include "game/ui.h"
#include "platform/platform.h"
#include "platform/video.h"

static u32 clock_now(void)
{
    return DSL(DS_game_clock) + DSL(DS_bios_ticks) - DSL(DS_rt_base);
}

/* 0000:6476 day_index: T / 0x1998 (1e16:23ec signed long divide), at most 0x5B */
s16 day_index(void)
{
    s16 d = (s16)((s32)clock_now() / (s32)FLOW_GAME_DAY);
    return d > 0x5B ? 0x5B : d;
}

/* 0000:64a3 clock_hm */
void clock_hm(s16 *hour, s16 *min)
{
    s32 t = (s32)clock_now();
    if (t > 0x91908) t = 0x91908;                     /* 91 days */
    u16 r = (u16)(t % (s32)FLOW_GAME_DAY);            /* 1e16:24bc signed long remainder */
    u16 h = (u16)(r / FLOW_GAME_HOUR);                /* div cx (dx = 0) */
    *hour = (s16)h;
    u16 part = (u16)(r - (u16)(h * FLOW_GAME_HOUR));
    *min = (s16)((s32)((u32)part * 0x3C) / (s32)FLOW_GAME_HOUR);   /* _aFlmul, _aFldiv */
}

/* 0000:6529 summer_over_check */
void summer_over_check(void)
{
    if (day_index() > 0x5A) DSB(DS_game_end_flags) |= FLOW_END_SUMMER;
}

/* 0000:653c month_of_day: 0 June .. 3 September */
s16 month_of_day(s16 d, s16 *dom)
{
    if (d < 0x0F) { *dom = (s16)(d + 0x10); return 0; }
    if (d < 0x2E) { *dom = (s16)(d - 0x0E); return 1; }
    if (d < 0x4D) { *dom = (s16)(d - 0x2D); return 2; }
    if (d < 0x5B) { *dom = (s16)(d - 0x4C); return 3; }
    *dom = 0x0F;
    return 3;
}

/* 0000:65af calendar_show */
void calendar_show(s16 final)
{
    summer_over_check();
    if (final == 0 && DSW(DS_game_end_flags) != 0) return;
    s16 d = day_index();
    s16 h, mi, dom;
    clock_hm(&h, &mi);                                /* computed, unused */
    s16 m = month_of_day(d, &dom);
    s16 dd = (s16)(d + 0x15);
    s16 col = (s16)(dd % 7);
    s16 row = (s16)(dd / 7 - DSS((u16)(0x5052 + 2 * m)));
    s16 x = (s16)(col * 0x12 + DSS(0x5048));
    s16 y = (s16)(row * 7 + DSS((u16)(0x504A + 2 * m)));
    DSW(DS_g_mirror) = 1;
    s16 pic = m < 2 ? 0x4C3 : 0x4C5;                  /* LIB2 #219 June/July, #221 August/September */
    cursor_ctl(-3);
    FarPtr buf = pic_get(pic, 2);
    pic_draw_masked(buf, 0x4C4, (u8)x, (u8)y, 0);     /* LIB2 #220 the X */
    u16 w = desc_w(buf), hh = desc_h(buf);
    FarPtr bg = arena_bitmap_alloc((s16)w, (s16)hh, 0, 2);
    screen_save_rect(0x50, 4, (s16)w, (s16)hh, bg);
    screen_put_bitmap_mirror(buf, 0x50, 4);
    arena_pop(1);
    cursor_ctl(-1);
    if (final == 0) {
        status_print(-1, ds_str(0x505A), 0);          /* "" */
        wait_ticks_or_input(500);
        flow_clock_add(0);                            /* add [05FC], 0 (sic) */
        screen_put_bitmap_mirror(bg, 0x50, 4);
    }
    arena_pop(1);
}

/* 0000:6750 date_print */
void date_print(s16 y)
{
    s16 dom;
    s16 m = month_of_day(day_index(), &dom);
    char buf[0x20];
    snprintf(buf, sizeof buf, ds_str(0x5076), ds_str(DSW((u16)(0x5082 + 2 * m))), dom);  /* "%s %d, 1963" */
    u8 save[5];
    for (int i = 0; i < 5; i++) save[i] = DSB((u16)(DS_g_text_fg + i));
    u16 sch = (u16)(DSW(DS_g_scheme) + 0x41);
    for (int i = 0; i < 5; i++) DSB((u16)(DS_g_text_fg + i)) = DSB((u16)(sch + i));
    drv_draw_text(g_back(), 0x61, y, buf);
    for (int i = 0; i < 5; i++) DSB((u16)(DS_g_text_fg + i)) = save[i];
}

/* 0f38:0d62 summer_over_screen */
void summer_over_screen(void)
{
    msg_box(0x17BB);                                  /* "Looks like ya took too much time, jr!" */
    drv_pal_black();
    cursor_ctl(-4);
    DSW(DS_g_mirror) = 1;
    screen_fill_rect(0, 0, 0x140, 200, 0);
    calendar_show(1);
    pal_reg12_5();
    drv_pal_normal();
    wait_ticks_or_input(300);
}
