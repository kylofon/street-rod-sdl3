/* game_flow: the end of a game — the Load / New / Quit menu, the "King Street Rodders" wall (hall of
 * fame, hall_dat) and the ending after beating the King — port/spec/game_flow.md §4.9, §4.10, §5.3
 * (0000:315b-3614, 0000:a544). */
#include "game/flow.h"

#include "game/garage.h"
#include "game/ui.h"
#include "game/race.h"
#include "platform/platform.h"
#include "platform/video.h"

#define HALL_REC   0x17                 /* name[16] + hours[7] */
#define HALL_ROW_Y 0x4BCA               /* u16[10] text rows 0x3B + 11 i */

/* 0000:315b game_over_menu: "Load Game" -2, "New Game" -1, "Quit" 4 */
s16 game_over_menu(void)
{
    status_init(0xBE);
    DSW(DS_g_mirror) = 1;
    u8 fg = DSB(DS_g_text_fg), bg = DSB(DS_g_text_bg), line = DSB(DS_g_line_col), fill = DSB(DS_g_fill_col);
    if (DSS(DS_driver_id) == -2 || DSS(DS_driver_id) == -6) {
        DSB(DS_g_text_fg) = 0x0F; DSB(DS_g_text_bg) = 1; DSB(DS_g_line_col) = 0x0D; DSB(DS_g_fill_col) = 3;
    } else {
        DSB(DS_g_text_fg) = 3; DSB(DS_g_text_bg) = 0; DSB(DS_g_line_col) = 2; DSB(DS_g_fill_col) = 1;
    }
    screen_fill_rect(0, 0, 0x140, 0xBE, DSB(DS_g_text_bg));
    draw_box1(g_back(), 100, 0x32, 0x78, 0x5A);
    draw_box1(g_front(), 100, 0x32, 0x78, 0x5A);
    draw_box1(g_back(), 0x66, 0x34, 0x74, 0x56);
    draw_box1(g_front(), 0x66, 0x34, 0x74, 0x56);
    DSB(DS_g_text_fg) = fg; DSB(DS_g_text_bg) = bg; DSB(DS_g_line_col) = line; DSB(DS_g_fill_col) = fill;
    cursor_ctl(-2);
    drv_pal_normal();
    s16 r = ui_run(0x29);
    cursor_ctl(-4);
    if (r >= -2 && r <= -1 && DSW(DS_game_pics_freed)) {  /* the ending freed the game pictures */
        pic_load_list(DS_resident_pics, 1);
        DSW(DS_game_pics_freed) = 0;
    }
    return r;
}

/* 0000:32d0 hall_is_slower */
static s16 hall_is_slower(s16 i)
{
    return fl_atoi((u16)(DS_hall_records + 0x10 + i * HALL_REC)) > DSS(DS_hall_new_score);
}

/* 0000:32f4 hall_insert: hall_count already includes the new row (or is 10) */
static void hall_insert(s16 i)
{
    s16 n = (s16)(DSS(DS_hall_count) - i - 1);
    if (i < 0) return;
    u16 row = (u16)(DS_hall_records + i * HALL_REC);
    if (n > 0) fl_memmove((u16)(row + HALL_REC), row, (u16)(n * HALL_REC));
    fl_strcpy(row, DS_player_name);
    fl_itoa(DSS(DS_hall_new_score), (u16)(row + 0x10));
}

/* 0000:3375 hall_score: hours since June 16, 00:00 */
static s16 hall_score(void)
{
    s16 h, m;
    clock_hm(&h, &m);
    return (s16)(day_index() * 0x18 + h + 0x0C);
}

/* 0000:339f hall_of_fame_screen */
void hall_of_fame_screen(s16 won)
{
    u16 clip = DSW(0x04C2);
    s16 row = -1;
    drv_pal_black();
    DSW(0x04C2) = 200;
    DSW(DS_g_mirror) = 1;
    show_picture(5, -1);                              /* LIB1 #4 "King Street Rodders" wall */
    DSW(0x04C2) = clip;
    pal_shadow_swap();
    u16 sch = DSW(DS_g_scheme);
    u8 s28[5], s23[5];
    for (int i = 0; i < 5; i++) { s28[i] = DSB((u16)(sch + 0x28 + i)); s23[i] = DSB((u16)(sch + 0x23 + i)); }
    if (DSS(DS_driver_id) == -2 || DSS(DS_driver_id) == -6)
        for (int i = 0; i < 5; i++) {
            DSB((u16)(sch + 0x28 + i)) = DSB((u16)(0x4BE2 + i));
            DSB((u16)(sch + 0x23 + i)) = DSB((u16)(0x4BE8 + i));
        }
    drv_pal_normal();
    if (won) {
        DSW(DS_hall_new_score) = (u16)hall_score();
        s16 i;
        for (i = 0; i < DSS(DS_hall_count); i++)
            if (hall_is_slower(i)) { row = i; break; }
        if (DSS(DS_hall_count) < 10) {
            if (row < 0) row = DSS(DS_hall_count);
            DSW(DS_hall_count)++;
        }
        hall_insert(row);                             /* full and slower than all ten: no entry */
    }
    if (DSS(DS_driver_id) == -2 || DSS(DS_driver_id) == -6) { DSB(DS_g_text_fg) = 4; DSB(DS_g_text_bg) = 0; }
    else { DSB(DS_g_text_fg) = 3; DSB(DS_g_text_bg) = 2; }
    u16 rec = DS_hall_records;
    for (s16 i = 0; i < DSS(DS_hall_count); i++, rec = (u16)(rec + HALL_REC)) {
        s16 y = DSS((u16)(HALL_ROW_Y + 2 * i));
        drv_draw_text(g_back(), 0x2E, y, ds_str(rec));
        s16 x = 0xA1;
        /* right-align the hours: 16-bit v *= 10 until >= 1000. PORT: v = 0 loops forever in the
         * original (never happens: a score is at least 12); the port stops there. */
        for (s16 v = fl_atoi((u16)(rec + 0x10)); v < 1000 && v != 0; v = (s16)(u16)(v * 10)) x += 6;
        drv_draw_text(g_back(), x, y, ds_str((u16)(rec + 0x10)));
        drv_draw_text(g_back(), 0xBF, y, ds_str(0x4BDE));   /* "hr" */
    }
    page_copy_rect(g_back(), g_front(), 0x2E, DSS(HALL_ROW_Y), 0xD3, (s16)(DSS(0x4BDC) + 8));
    if (row < 0) {
        wait_click_or_key(700);
    } else {
        u16 r = (u16)(DS_hall_records + row * HALL_REC);
        fl_strcpy(FLOW_STK_NAME, r);
        text_input(0x2E, DSS((u16)(HALL_ROW_Y + 2 * row)), ds_str(FLOW_STK_NAME), 0x0F);   /* the name may be edited */
        fl_strcpy(r, FLOW_STK_NAME);
        hall_save();
    }
    drv_pal_black();
    for (int i = 0; i < 5; i++) { DSB((u16)(sch + 0x28 + i)) = s28[i]; DSB((u16)(sch + 0x23 + i)) = s23[i]; }
    pal_shadow_swap();
}

/* ========================================================================= the ending (0000:a544) */

/* Rect from a 6-byte frame record {sx, sy, w, h, dx, dy} (bytes). */
static const u8 *frame(u16 off) { return mp(DGROUP, off); }

/* 0000:a544 ending_king (game_flow.md ending_sequence): the text "Well, you've done it..." (LIB1 #8),
 * the garage with the King's car driving in, the girlfriend's faces (LIB1 #5, 19 frames DS:5270) and
 * the lips (LIB1 #6, 9 frames DS:52E2), then "I knew you could do it! You're my hero!". The caller
 * (bob_drive_in) sets DS:0282 |= 2 afterwards. */
void ending_king(void)
{
    s16 skip_lips = 0;                                /* [bp-24h]: always 0 */
    Rect r, win_r;
    cursor_ctl(-4);
    status_init(0xBE);
    show_picture_at(9, 8, 0x14, 2);                   /* LIB1 #8, clear colour 2 */
    DSW(DS_g_mirror) = 0;
    u16 clip = DSW(0x04C2);
    DSW(0x04C2) = 0xC8;
    drv_copy_page(desc_planes(g_front()), desc_planes(g_back()));
    show_picture(0x408, -1);                          /* LIB2 #32 garage, off screen */
    DSW(0x04C2) = clip;
    drv_pal_normal();
    wait_ticks_or_input(0x2EE);
    drv_blinds(desc_planes(g_front()), desc_planes(g_back()));   /* garage visible */
    DSW(DS_drive_dest) = 1;
    FarPtr bg0 = arena_bitmap_alloc(0x50, 0x2C, 0, 3);          /* [bp-22h] the window, empty garage */
    screen_save_rect(0x78, 0x45, 0x50, 0x2C, bg0);
    pic_free_list(DS_resident_pics);                  /* room for the LIB1 pictures */
    DSW(DS_game_pics_freed) = 1;
    pic_load_list(0x5318, 0);
    pic_load_list(0x5328, 0);
    drv_copy_page(desc_planes(g_back()), desc_planes(g_front()));
    DSW(DS_g_mirror) = 0;
    drv_pal_normal();

    /* the King's car drives in */
    DSW(0x8176) = 0x4C1;
    opponent_load(0x15);
    u16 opp = DSW(DS_opp_rec);
    car_draw(4, DSS(DS_opp_model), (s16)(s8)DSB((u16)(opp + 0x0E)), (s16)(s8)DSB((u16)(opp + 0x0F)),
             1, 0x98, 0, (u16)((DSW(0x827E) << 3) + 0x5030));
    DSW(0x8176) = 0x4DC;
    FarPtr bg1 = arena_bitmap_alloc(0x50, 0x2C, 0, 3);          /* [bp-1Eh] the window with the car */
    screen_save_rect(0x78, 0x45, 0x50, 0x2C, bg1);

    /* the girlfriend's face */
    FarPtr faces = pic_get(6, 1);                     /* LIB1 #5 */
    FarPtr win = arena_bitmap_alloc(0x50, 0x2C, 0, 3);          /* [bp-6] */
    const u8 *f0 = frame(0x526A);
    r.w = f0[2]; r.h = f0[3]; r.sx = f0[0]; r.sy = f0[1];
    r.dx = (s16)(f0[4] - 0x50); r.dy = (s16)(0x2C - f0[3]);
    drv_blit_masked(faces, bg0, &r, 1);
    r.w = f0[2]; r.h = f0[3];
    r.sx = (s16)(f0[4] - 0x50); r.sy = (s16)(0x2C - f0[3]);
    r.dx = r.sx; r.dy = r.sy;
    drv_blit(bg0, bg1, &r, 1);
    wait_ticks_or_input(10);
    u16 rec = 0x5270;                                 /* 19 frames */
    for (s16 i = 1; i < 0x14; i++, rec = (u16)(rec + 6)) {
        if (i == 0x0D) wait_vretraces(0x14);
        bitmap_copy_into(bg1, win, 1);
        const u8 *f = frame(rec);
        r.w = f[2]; r.h = f[3]; r.sx = f[0]; r.sy = f[1];
        r.dx = (s16)(f[4] - 0x50); r.dy = (s16)(0x2C - f[3]);
        drv_blit_masked(faces, win, &r, 1);
        win_r.w = 0x50; win_r.h = 0x2C; win_r.sx = 0; win_r.sy = 0; win_r.dx = 0x78; win_r.dy = 0x45;
        if (i < 0x13) {
            wait_vretraces(10);
            drv_blit(win, g_front(), &win_r, 1);
        }
        if (i == 0x12) drv_blit(win, g_back(), &win_r, 1);
    }
    arena_reset();

    /* the lips grow over the screen */
    s16 left = 0, top = 0, w = 0, h = 0;
    if (skip_lips == 0) {
        FarPtr lips = pic_get(7, 1);                  /* LIB1 #6 */
        s16 n = 9;
        if (DSS(DS_driver_id) == -6) { arena_low_shrink(0x2710); n--; }   /* Tandy */
        s16 px = 0x28, py = 0x28, pw = -0x28;         /* previous frame: x [bp-32h], y [bp-38h], w [bp-2Ah] */
        rec = 0x52E2;
        for (s16 k = n; k > 0; k--, rec = (u16)(rec + 6)) {
            const u8 *f = frame(rec);
            s16 fx = f[4], fy = f[5];
            s16 right = (s16)(f[2] + fx), pr = (s16)(px + pw);
            if (right < pr) right = pr;
            left = fx > px ? px : fx;
            w = (s16)(right - left);
            s16 bottom = (s16)(f[3] + fy), pb = (s16)(py + pw);   /* sic: the previous width */
            if (bottom < pb) bottom = pb;
            top = fy > py ? py : fy;
            h = (s16)(bottom - top);
            FarPtr buf = arena_bitmap_alloc(w, h, 0, 2);
            r.w = w; r.h = h; r.sx = left; r.sy = top; r.dx = 0; r.dy = 0;
            drv_blit(g_back(), buf, &r, 1);           /* the background of both frames */
            pw = f[2];
            r.w = f[2]; r.h = f[3]; r.sx = f[0]; r.sy = f[1];
            px = fx; r.dx = (s16)(fx - left);
            py = fy; r.dy = (s16)(fy - top);
            drv_blit_masked(lips, buf, &r, 1);
            r.w = w; r.h = h; r.sx = 0; r.sy = 0; r.dx = left; r.dy = top;
            wait_vretraces(8);
            drv_blit(buf, g_front(), &r, 1);
            arena_pop(1);
        }
    }
    r.sx = left; r.sy = top;                          /* r = {w, h, left, top, left, top} */
    wait_ticks_or_input(0x50);
    wait_vretraces(1);
    drv_blit(g_back(), g_front(), &r, 1);             /* lips gone */
    wait_vretraces(1);
    drv_blit(win, g_front(), &win_r, 1);
    drv_blit(win, g_back(), &win_r, 1);
    wait_ticks_or_input(0x32);
    msg_box(0x183C);                                  /* "I knew you could do it! You're my hero!" */
    DSW(DS_g_mirror) = 0;
    arena_clear();
    pic_free_list(0x5328);
    pic_free_list(0x5318);
}
