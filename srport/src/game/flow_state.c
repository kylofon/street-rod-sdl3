/* game_flow: player objects, messages, checks, newspaper front page and the new-game set-up —
 * port/spec/game_flow.md §4.5, §4.6, §4.8 (0000:3ab8-3fd4, 0000:542e-5795, 0000:6ad8). */
#include "game/flow.h"

#include "game/garage.h"
#include "game/ui.h"
#include "game/race.h"
#include "platform/platform.h"
#include "platform/video.h"

/* ============================================================================== object pools */

/* 0000:3c1b part_alloc */
u16 part_alloc(void)
{
    u16 p = DSW(DS_part_freelist);
    DSW(DS_part_freelist) = DSW((u16)(p + 6));
    DSW((u16)(p + 6)) = 0;
    DSW(DS_part_count)++;
    return p;
}

/* 0000:3c3e part_free */
void part_free(u16 p)
{
    DSW((u16)(p + 6)) = DSW(DS_part_freelist);
    DSW((u16)(p + 4)) = 0xFFFF;
    DSW(DS_part_freelist) = p;
    DSW(DS_part_count)--;
}

/* 0000:3fb1 car_alloc */
u16 car_alloc(void)
{
    u16 c = DSW(DS_car_freelist);
    DSW(DS_car_freelist) = DSW((u16)(c + 0x26));
    DSW((u16)(c + 0x26)) = 0;
    DSW(DS_car_count)++;
    return c;
}

/* 0000:3fd4 car_release */
void car_release(u16 c)
{
    DSW((u16)(c + 0x26)) = DSW(DS_car_freelist);
    DSW((u16)(c + 2)) = 0xFFFF;
    DSW(DS_car_freelist) = c;
    DSW(DS_car_count)--;
}

/* ============================================================================ messages, checks */

/* 0000:3ab8 msg_box */
void msg_box(s16 id)
{
    u32 saved = DSL(DS_wait_deadline);
    ui_push(0x18);
    msg_draw(id);
    wait_ticks_or_input(DSS(DS_msg_ticks));
    msg_erase();
    ui_pop(1);
    DSL(DS_wait_deadline) = saved;
}

/* 0000:3b0c garage_full_check */
s16 garage_full_check(void)
{
    if (DSS(DS_car_count) > 0x0F) { msg_box(0x0E27); return 1; }     /* "Sell some cars first!" */
    if (DSS(DS_part_count) > 0x82) { msg_box(0x0E3F); return 1; }    /* "Sell some parts first!" */
    return 0;
}

/* 0000:6ad8 broke_check: no car, no parts, money < $400 (signed 32-bit) */
s16 broke_check(void)
{
    return DSW(DS_car) == 0 && DSW(DS_car2) == 0 && DSW(DS_spare_parts) == 0 &&
           DSSL(DS_money) < 0x190;
}

/* 0000:3b33 newspaper_front */
s16 newspaper_front(void)
{
    u16 save = DSW(DS_g_mirror);
    DSW(DS_g_mirror) = 0;
    show_picture(0x3FD, -1);                          /* LIB2 #21 */
    s16 d = day_index();
    if (d != DSS(DS_headline_day)) {                  /* a new headline once per day */
        s16 h = rnd(5);
        if (h == DSS(DS_headline)) h = rnd(5);
        DSW(DS_headline) = (u16)h;
        DSW(DS_headline_day) = (u16)d;
    }
    show_picture_at((s16)DSW((u16)(0x4F4E + 2 * DSW(DS_headline))), 0, 0x88, -1);   /* LIB2 #22..26 */
    cursor_ctl(-3);
    date_print(0x79);
    cursor_ctl(-1);
    DSW(DS_g_mirror) = save;
    if (save) page_copy_rect(g_back(), g_front(), 0, 0, 0x13F, 0xBD);
    cursor_ctl(1);
    return ui_run(7);
}

/* ================================================================================== new game */

/* 0000:542e opponent_init: models[0] = count, models[1..count] = model numbers (-1 = taken) */
void opponent_init(s16 i, s16 *models)
{
    s16 col;
    for (;;) {                                        /* colour 5 at half weight */
        col = rnd(6);
        if (col != 5 || rnd(2) == 1) break;
    }
    u16 r = (u16)(DS_opponents + i * 0x12);
    DSB((u16)(r + 0x0A)) = (u8)col;
    for (u16 k = 0x0B; k <= 0x10; k++) DSB((u16)(r + k)) = 0;
    s16 n = models[0], k;
    do { k = rnd(n); } while (models[k + 1] == -1);
    s16 m = models[k + 1];
    DSW((u16)(r + 8)) = (u16)m;
    models[k + 1] = -1;
    u16 mr = (u16)(m * 10);
    s16 lvl = (s16)((DSB((u16)(0x7D88 + mr)) >> 1) + 1);
    DSB((u16)(r + 0x0E)) = (DSB((u16)(0x7D8B + mr)) & 7) == 2 ? 8 : 0;
    DSB((u16)(r + 0x0F)) = rnd(0x14) < lvl ? (u8)rnd(9) : 0;
    if (DSS((u16)(0x8DF6 + mr)) > 0 && DSS((u16)(0x8DF8 + mr)) > 0)
        DSB((u16)(r + 0x0E)) |= rnd(0x19) < lvl ? 3 : 0;
    if (DSS((u16)(0x8DF2 + mr)) > 0)
        DSB((u16)(r + 0x0E)) |= rnd(0x1E) < lvl ? 4 : 0;
}

/* 0000:5584 opponents_init */
void opponents_init(void)
{
    /* The three lists are 11-word arrays on the original's stack (fast, medium, slow upwards). A class
     * with more than 10 models would run into the next list; HOT_DATA has at most 10 per class.
     * PORT: the arrays are sized for all 25 models so nothing is overwritten either way. */
    s16 slow[26] = {0}, mid[26] = {0}, fast[26] = {0};
    for (s16 m = 0; m < 0x19; m++) {
        u8 c = DSB((u16)(0x7D88 + m * 10));
        if (c < 6) {
            if (c < 3) slow[++slow[0]] = m;
            else       mid[++mid[0]] = m;
        } else fast[++fast[0]] = m;
    }
    if (DSW(DS_demo_active)) slow[0]--;               /* demo: the last slow model for "The Geek" */
    s16 i = 0;
    for (s16 n = 7; n; n--) opponent_init(i++, slow);
    for (s16 n = 7; n; n--) opponent_init(i++, mid);
    for (s16 n = 7; n; n--) opponent_init(i++, fast);
    DSB(0x817D) = 0;                                  /* the King's +0B */
    if (DSW(DS_demo_active)) {
        s16 one[2] = { 1, 0x18 };
        opponent_init((s16)DSW(0x4FF4), one);
        DSW((u16)(DS_opponents + 6 + DSS(0x4FF4) * 0x12)) = 0x629;   /* "The Geek" */
    }
}

/* 0000:56a9 new_game */
void new_game(void)
{
    pools_reset(1);
    DSL(DS_money) = 0x2EE;                            /* $750 */
    if (DSW(DS_demo_active)) DSL(DS_money) = 0x2710;  /* demo: $10000 */
    DSW(DS_unk_7eae) = 10;
    track_build_course(100);
    opponents_init();
    DSL(DS_game_clock) = 0;
    DSL(DS_rt_base) = DSL(DS_bios_ticks);
    DSW(0x4976) = 0; DSW(0x4974) = 0; DSW(0x4972) = 0; DSW(0x4970) = 0;
}

/* 0000:570c new_game_prompt: 0 new game, -2 "Old Game", other codes as returned */
s16 new_game_prompt(void)
{
    DSW(DS_g_mirror) = 0;
    show_picture_at(0x3F8, 0x10, 6, 0);               /* LIB2 #16 driver licence */
    DSW(DS_g_mirror) = 1;
    pal_shadow_patch();                               /* 0f38:181c */
    page_copy_rect(g_back(), g_front(), 0, 0, 0x13F, 0xBD);
    drv_pal_normal();
    s16 r = ui_edit(2, 0x0F, ds_str(DS_player_name));         /* name field, "Old Game" -2, "OK" -3, Enter 0 */
    if (r == -3 || r == 0) {                          /* an empty name is accepted */
        new_game();
        return 0;
    }
    if (r == -2) return -2;
    return r;
}
