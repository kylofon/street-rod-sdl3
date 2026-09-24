/* game_flow: saved games ?:HOTROD<n>.SAV and the "Save, Restart, or Quit." box —
 * port/spec/game_flow.md §4.11, §5.1 (0000:5795-6476).
 *
 * File layout (little-endian, no header magic): the 0x24 bytes DS:7E9A..7EBD, the current car block,
 * the other cars' blocks, the spare part chain, DS:4970 (8), the opponent table DS:7FF8 (0x18C),
 * game_clock DS:05FC (4), rt_ticks - rt_base (4), DS:5E0C (2). A car block is the 0x28-byte record
 * followed by the part chains of its pointer fields +06, +08..+10, +12 (in that order) that are
 * non-zero; a part chain is 8-byte records, one more following while +06 != 0. Pointer values are
 * written as they are in mem[] (their only meaning in the file is zero / non-zero), so files are
 * byte-identical to the original's for the same state, and a loaded game lands at the same DGROUP
 * addresses (pools reset, records allocated in file order). */
#include "game/flow.h"

#include "game/garage.h"
#include "game/ui.h"
#include "game/race.h"
#include "platform/platform.h"
#include "platform/video.h"

#define TMPL_SCAN 0x4FF6                /* ":HOTROD\0" ".SAV\0" of save_slots_scan */
#define TMPL_LOAD 0x5003                /* the same, load_game_screen */
#define TMPL_SAVE 0x5010                /* the same, save_game_screen */

/* fd of a failed open / creat. PORT: the original tests an uninitialised stack word (">= 0": close
 * and, for load, new_game); the port treats it as "not open". */
#define FD_NONE 0xFFFF

static bool wr_ok(u16 fd, u16 off, u16 n)
{
    u16 put = 0;
    return dos_write(fd, ds_ptr(off), n, &put) == 0 && put == n;
}

static bool rd_ok(u16 fd, u16 off, u16 n)
{
    u16 got = 0;
    return dos_read(fd, ds_ptr(off), n, &got) == 0 && got == n;
}

/* 0000:5795 save_part_chain */
static s16 save_part_chain(u16 fd, u16 p)
{
    for (; p; p = DSW((u16)(p + 6)))
        if (!wr_ok(fd, p, 8)) return 1;
    return 0;
}

/* 0000:57dc load_part_chain: head = DS offset of the chain's head pointer */
static s16 load_part_chain(u16 fd, u16 head)
{
    u16 prev = 0;
    if (DSW(head) == 0) return 0;
    DSW(head) = 0;
    for (;;) {
        if (!rd_ok(fd, FLOW_STK_PART, 8)) return 1;
        u16 p = part_alloc();
        for (u16 i = 0; i < 8; i++) DSB((u16)(p + i)) = DSB((u16)(FLOW_STK_PART + i));
        if (prev) DSW((u16)(prev + 6)) = p;
        else DSW(head) = p;
        prev = p;
        if (DSW((u16)(p + 6)) == 0) return 0;
        DSW((u16)(p + 6)) = 0;
    }
}

/* 0000:586e save_car_chain */
static s16 save_car_chain(u16 fd, u16 c, s16 one)
{
    for (; c; c = DSW((u16)(c + 0x26))) {
        if (!wr_ok(fd, c, 0x28)) return 1;
        if (save_part_chain(fd, DSW((u16)(c + 6)))) return 1;
        for (u16 k = 0; k < 5; k++)
            if (save_part_chain(fd, DSW((u16)(c + 8 + 2 * k)))) return 1;
        if (save_part_chain(fd, DSW((u16)(c + 0x12)))) return 1;
        if (one) return 0;
    }
    return 0;
}

/* 0000:590f load_car_chain: head = DS offset of the list head pointer */
static s16 load_car_chain(u16 fd, u16 head, s16 one)
{
    u16 prev = 0;
    const u16 rec = FLOW_STK_CAR;
    if (DSW(head) == 0) return 0;
    DSW(head) = 0;
    for (;;) {
        if (!rd_ok(fd, rec, 0x28)) return 1;
        if (load_part_chain(fd, (u16)(rec + 6))) return 1;
        for (u16 k = 0; k < 5; k++)
            if (load_part_chain(fd, (u16)(rec + 8 + 2 * k))) return 1;
        if (load_part_chain(fd, (u16)(rec + 0x12))) return 1;
        u16 c = car_alloc();
        for (u16 i = 0; i < 0x28; i++) DSB((u16)(c + i)) = DSB((u16)(rec + i));
        if (prev) DSW((u16)(prev + 0x26)) = c;
        else DSW(head) = c;
        prev = c;
        if (DSW((u16)(c + 0x26)) == 0 || one) return 0;
        DSW((u16)(c + 0x26)) = 0;
    }
}

/* 0000:59f6 save_slots_scan: DS:4FC2[n] = n if HOTROD<n>.SAV has a readable header; the list
 * DS:49E0 gets the slot labels (message ids DS:4FD4[n-1]) of the present files, or of all 15 slots
 * with for_save ("_______________" for the empty ones). */
static void save_slots_scan(s16 for_save)
{
    u16 name = FLOW_STK_FNAME;
    s16 n_list = 0;
    for (s16 n = 1; n <= 15; n++) {
        u16 fd = 0;
        fl_save_name(name, TMPL_SCAN, n);
        u16 id = DSW((u16)(DS_slot_text + 2 * (n - 1)));
        if (dos_open(ds_str(name), 0, &fd) == 0) {
            if (rd_ok(fd, FLOW_STK_HDR, 0x24)) {
                DSB((u16)(DS_slot_present + n)) = (u8)n;
                n_list++;
                DSW((u16)(DS_list_items + 2 * n_list)) = id;
                fl_strncpy((u16)(0x2C02 + id), FLOW_STK_HDR, 0x0F);
            } else {
                DSB((u16)(DS_slot_present + n)) = 0;
            }
            dos_close(fd);
        } else {
            DSB((u16)(DS_slot_present + n)) = 0;
        }
        if (for_save && DSB((u16)(DS_slot_present + n)) == 0) {
            n_list++;
            DSW((u16)(DS_list_items + 2 * n_list)) = id;
            fl_strcpy((u16)(0x2C02 + id), 0x4607);    /* "_______________" */
        }
    }
    DSW(DS_list_items) = (u16)n_list;
}

/* 0000:5b4a load_game_screen: 0 loaded, -1 "Forget it" / no saves, -0x29A read error */
s16 load_game_screen(void)
{
    cursor_ctl(-4);
    save_slots_scan(0);
    u16 s = DSW(DS_g_mirror);
    DSW(DS_g_mirror) = 0;
    show_picture(0x3F7, 0);                           /* LIB2 #15 Highway Patrol book */
    if (DSW(DS_list_items)) {
        show_picture_at(0x3FA, 0xF0, 100, -1);        /* LIB2 #18 "load" */
        show_picture_at(0x3FC, 0xF0, 0x8C, -1);       /* LIB2 #20 "forget it" */
    }
    DSW(DS_g_mirror) = s;
    if (s) page_copy_rect(g_back(), g_front(), 0, 0, 0x140, 0xBE);
    cursor_ctl(-2);
    if (DSW(DS_list_items) == 0) {
        ui_run(0x0F);                                 /* "No saved games found !" */
        return -1;
    }
    s16 r = list_box(3, 1, NULL, (s16 *)mp(DGROUP, DS_list_items), NULL, NULL);
    if (r == -4) return -1;
    if (r == -3) r = list_selected();
    /* (for r >= 1 the original loads DS:49E0 into AX and drops it: r stays) */
    s16 slot = 1, k = 0;
    while (k < r) {                                   /* the r-th existing file */
        if (DSB((u16)(DS_slot_present + slot))) k++;
        slot++;
    }
    slot--;
    cursor_ctl(-4);
    u16 name = fl_save_name(FLOW_STK_FNAME, TMPL_LOAD, (s16)(s8)DSB((u16)(DS_slot_present + slot)));
    u16 fd = FD_NONE;
    if (dos_open(ds_str(name), 0, &fd) != 0) fd = FD_NONE;
    else if (rd_ok(fd, DS_player_name, 0x24)) {
        pools_reset(0);                               /* the header's pointers now mean "present" */
        if (!load_car_chain(fd, DS_car, 1) && !load_car_chain(fd, DS_car2, 0) &&
            !load_part_chain(fd, DS_spare_parts) &&
            rd_ok(fd, 0x4970, 8) && rd_ok(fd, DS_opponents, 0x18C) &&
            rd_ok(fd, DS_game_clock, 4) && rd_ok(fd, FLOW_STK_ELAPSED, 4) &&
            rd_ok(fd, DS_course_layout, 2)) {
            dos_close(fd);
            if (DSW(DS_car)) set_paint_palette((s16)(s8)DSB((u16)(DSW(DS_car) + 4)));
            track_build_course(DSS(DS_course_layout));
            DSL(DS_rt_base) = DSL(DS_bios_ticks) - DSL(FLOW_STK_ELAPSED);
            cursor_ctl(-2);
            return 0;
        }
    }
    cursor_ctl(-2);
    ui_run(0x1F);                                     /* "Cannot read that game !" */
    cursor_ctl(-3);
    if ((s16)fd > -1) {
        dos_close(fd);
        new_game();
    }
    cursor_ctl(-1);
    return -0x29A;
}

/* 0000:5eb3 str_is_blank: only ' ' and '_' */
static s16 str_is_blank(u16 s)
{
    for (; DSB(s); s++)
        if (DSB(s) != ' ' && DSB(s) != '_') return 0;
    return 1;
}

/* Highlight row k of the save list (both pages) when the typed name matches its label. */
static void save_highlight_row(s16 k)
{
    cursor_ctl(-3);
    u16 cur = DSW(0x8B9A);                            /* the list element of menu 5 */
    u16 row = (u16)(k * 0x12 + cur - 0x12);
    u8 save[5];
    for (int i = 0; i < 5; i++) save[i] = DSB((u16)(DS_g_text_fg + i));
    u16 sch = (u16)(DSW(DS_g_scheme) + (u16)(5 * DSW((u16)(cur + 2))));
    for (int i = 0; i < 5; i++) DSB((u16)(DS_g_text_fg + i)) = DSB((u16)(sch + i));
    u8 line = DSB(DS_g_line_col);
    DSB(DS_g_line_col) = DSB(DS_g_text_bg);
    s16 w = (s16)(DSW((u16)(cur + 0x0C)) - DSW((u16)(cur + 8)));
    draw_box1(g_back(), DSS((u16)(row + 8)), DSS((u16)(row + 0x0A)), w, 8);
    draw_box1(g_front(), DSS((u16)(row + 8)), DSS((u16)(row + 0x0A)), w, 8);
    for (int i = 0; i < 5; i++) DSB((u16)(DS_g_text_fg + i)) = save[i];
    DSB(DS_g_line_col) = line;
    cursor_ctl(-1);
}

/* 0000:5eea save_game_screen */
void save_game_screen(void)
{
    s16 sel = 0, r;
    cursor_ctl(-4);
    save_slots_scan(1);                               /* all 15 rows, row n = slot n */
    u16 s = DSW(DS_g_mirror);
    DSW(DS_g_mirror) = 0;
    show_picture(0x3F7, 0);                           /* book */
    show_picture_at(0x3F9, 0x48, 0xA9, -1);           /* LIB2 #17 name field */
    show_picture_at(0x3FB, 0xF0, 100, -1);            /* LIB2 #19 "save" */
    show_picture_at(0x3FC, 0xF0, 0x8C, -1);           /* "forget it" */
    DSW(DS_g_mirror) = s;
    if (s) page_copy_rect(g_back(), g_front(), 0, 0, 0x140, 0xBE);
    fl_strcpy(DS_edit_name, DS_player_name);
    const u16 name = FLOW_STK_FNAME;
    for (;;) {
        /* 5fbd: the first empty slot is the default selection */
        u16 cnt = DSW(DS_list_items);
        u16 e = 1;
        while (e <= cnt && DSB((u16)(DS_slot_present + e)) != 0) e++;
        if (e > cnt) e = cnt;
        if (sel == 0) sel = (s16)e;
        cursor_ctl(-2);
        r = list_box_run(5, sel, NULL, (s16 *)mp(DGROUP, DS_list_items), NULL, NULL);   /* rows, name field, Save -3, Forget it -4 */
        if (r == -4) return;
        if (r == -3) r = list_selected();
        else if (r < 1 || (u16)r > DSW(DS_list_items)) return;
        sel = r;
        if (str_is_blank(DS_edit_name) && str_is_blank((u16)(0x2C02 + DSW((u16)(DS_list_items + 2 * r))))) {
            dialog_msg(ds_str(DSW(0x4FF2)));          /* "The game must have a name !" */
            cursor_ctl(-4);
            continue;
        }
        /* a typed name equal to a slot's label selects that slot */
        s16 k, cmp = 1;
        for (k = 1; k <= 0x0F; k++) {
            cmp = fl_strcmp((u16)(0x2C02 + DSW((u16)(DS_list_items + 2 * k))), DS_edit_name);
            if (cmp == 0) break;
        }
        if (cmp == 0) {
            save_highlight_row(r);
            r = k;
            sel = k;
        }
        /* PORT: the original fills name[0] (the drive) only before the creat below, so the remove()
         * of an overwritten game uses a stale drive byte (without effect: the creat truncates). */
        fl_save_name(name, TMPL_SAVE, r);
        s16 ans = DSB((u16)(DS_slot_present + r)) ? ui_dialog(0x1E) : 0;   /* "The old game will be replaced !" */
        cursor_ctl(-4);
        if (ans == -21) continue;                     /* Forget it */
        if (ans == -20) dos_remove(ds_str(name));     /* Go ahead */
        if (str_is_blank(DS_edit_name)) fl_strcpy(DS_player_name, (u16)(0x2C02 + DSW((u16)(DS_list_items + 2 * r))));
        else fl_strcpy(DS_player_name, DS_edit_name);
        DSL(FLOW_STK_ELAPSED) = DSL(DS_bios_ticks) - DSL(DS_rt_base);
        DSB(name) = (u8)data_disk_check();
        u16 fd = FD_NONE;
        if (dos_creat(ds_str(name), 0, &fd) != 0) fd = FD_NONE;
        else if (wr_ok(fd, DS_player_name, 0x24) &&
                 !save_car_chain(fd, DSW(DS_car), 1) && !save_car_chain(fd, DSW(DS_car2), 0) &&
                 !save_part_chain(fd, DSW(DS_spare_parts)) &&
                 wr_ok(fd, 0x4970, 8) && wr_ok(fd, DS_opponents, 0x18C) &&
                 wr_ok(fd, DS_game_clock, 4) && wr_ok(fd, FLOW_STK_ELAPSED, 4) &&
                 wr_ok(fd, DS_course_layout, 2)) {
            dos_close(fd);
            cursor_ctl(-2);
            return;
        }
        cursor_ctl(-2);
        ui_dialog(6);                                 /* "Disk error during Save" */
        cursor_ctl(-4);
        if ((s16)fd > -1) {
            dos_close(fd);
            dos_remove(ds_str(name));
        }
        save_slots_scan(1);
        cursor_ctl(-4);
    }
}

/* 0000:641e quit_menu: "Save, Restart, or Quit." Save Game -3, Old Game -2, New Game -1, Quit -20,
 * Forget it -22 */
s16 quit_menu(void)
{
    s16 x, y;
    FarPtr bg = ui_save_bg(0x1B, &x, &y, 2);
    s16 r = ui_run(0x1B);
    screen_put_bitmap_mirror(bg, x, y);
    arena_pop(1);
    return r;
}
