/* Garage: the garage screen and its simple screens (garage.md §4.7 - §4.9, §4.18): set_paint_palette
 * 0000:67cf, hot spots 6827 - 68e8, garage_name_clear 6aa6, garage_screen 6b06, paint_job 6f6b,
 * customize 711a, stickers 7367, change_tires 7800, car_info_list 78f1, car_info 79eb. From the
 * decompile, checked against the disassembly. */
#include "game/garage.h"

#include <stdio.h>
#include <string.h>

#include "game/flow.h"
#include "game/race.h"
#include "game/ui.h"
#include "platform/platform.h"
#include "platform/video.h"
#include "sound/sound.h"

#define TEXTS       0x2C02
#define PAINT       0x50C2      /* u8[6][2] palette values of registers 6 / 7 */
#define G_REC0      0x74        /* the 18 garage screen records */

static inline s32 money(void) { return DSSL(DS_money); }
static inline void clock_add(u16 v) { DSL(DS_game_clock) += v; }
static inline u16 cur_car(void) { return DSW(G_CUR_CAR); }
static inline s16 tyre_grade(u16 car) { return parts_grade(DSS((u16)(DSW((u16)(car + CR_TYRES)) + PT_TYPE))); }

/* 0000:67cf */
void set_paint_palette(s16 colour)
{
    if (DSS(DS_driver_id) == -2 || DSS(DS_driver_id) == -6) {
        if (DSB(DS_g_pal_visible) != 0) {
            ega_set_palreg(6, DSC((u16)(PAINT + 2 * colour)));
            ega_set_palreg(7, DSC((u16)(PAINT + 2 * colour + 1)));
        }
        DSB(0x0446) = DSB((u16)(PAINT + 2 * colour));
        DSB(0x0447) = DSB((u16)(PAINT + 2 * colour + 1));
    }
}

/* 0000:6827: returns the code itself when not found (18 when... never) */
s16 hotspot_find(s16 code)
{
    s16 r = 0x12;
    for (s16 i = 0; i <= 0x11; i++) {
        if (DSS((u16)(UI_REC(i + G_REC0) + UI_R_CODE)) == code) return (s16)(i + G_REC0);
        r = code;
    }
    return r;
}

/* 0000:6882 */
void garage_hotspots_register(void)
{
    ui_sort_records(G_REC0, 0x12);
}

/* 0000:68b3: codes DS:508A (11, 13, 9, 10, 12) disabled */
void garage_hotspots_nocar(void)
{
    for (u16 k = 0; k < 5; k++)
        DSW(UI_REC(hotspot_find(DSS((u16)(0x508A + 2 * k))))) = 0x0E;
}

/* 0000:68e8: the 5 car hot spots from MODEL_HOTSPOTS DS:70E4 ({dx, dy, w, h} bytes) */
void garage_hotspots_car(s16 model, s16 x, s16 y)
{
    static const s16 codes[5] = { 0x0B, 0x0D, 9, 10, 0x0C };
    u16 m = (u16)(model * 0x14 + 0x70E4);
    for (u16 k = 0; k < 5; k++) {
        u16 r = UI_REC(hotspot_find(codes[k]));
        DSW((u16)(r + UI_R_X0)) = (u16)(DSB((u16)(m + 4 * k)) + x);
        DSW((u16)(r + UI_R_Y0)) = (u16)(DSB((u16)(m + 4 * k + 1)) + y);
        DSW((u16)(r + UI_R_X1)) = (u16)(DSW((u16)(r + UI_R_X0)) + DSB((u16)(m + 4 * k + 2)));
        DSW((u16)(r + UI_R_Y1)) = (u16)(DSW((u16)(r + UI_R_Y0)) + DSB((u16)(m + 4 * k + 3)));
        DSW(r) = 0x0B;
    }
}

/* 0000:6aa6 */
void garage_name_clear(void)
{
    strcpy(ds_str(DS_garage_title), ds_str(0x5094));
}

/* ============================================================================== 0000:6b06 */

s16 garage_screen(s16 mode)
{
    s16 code = 5;                                  /* [bp-6] */
    if (DSC(DS_g_ptr_show) > 0) cursor_ctl(-4);
    summer_over_check();
    if (DSW(DS_game_end_flags) != 0) {
        if (DSW(DS_game_end_flags) & 1) cursor_ctl(-2);
        return 0x29A;
    }
    cursor_ctl(0);
    status_print(-1, ds_str(0x50B5), 0);
    DSW(DS_drive_dest) = 1;
    if (mode != 0) {
        bool draw = false;
        if (mode == 1) {
            draw = true;
        } else if (mode == 2) {
            DSW(DS_g_mirror) = 0;
            draw = true;
        } else if (mode == 3) {
            if (DSW(DS_switch_new) != 0) {
                u16 old = DSW(DS_switch_old);
                if (old != 0) {
                    u16 fl = car_flags(old);
                    car_draw(6, DSS((u16)(old + CR_MODEL)), (s16)fl, (s16)((DSW((u16)(old + CR_FLAGS)) >> 8) & 0x1F),
                             0, 0x98, 0, tyre_pics(tyre_grade(old)));
                    garage_name_clear();
                    ui_push(10);
                    ui_pop(1);
                }
                DSW(DS_g_mirror) = 0;
                draw = true;
            } else {
                cursor_ctl(-2);
                msg_box(0x1867);                   /* Here it is ! */
                cursor_ctl(-4);
            }
        }
        if (draw) {
            if (mode == 3) {
                mode = 1;                          /* background not redrawn */
            } else {
                show_picture(0x408, -1);           /* LIB2 #32 */
                pal_reg12_5();
                if (mode == 2 && cur_car() == 0 && DSW(G_OTHER_CARS) != 0) {
                    DSW(G_CUR_CAR) = DSW(G_OTHER_CARS);
                    DSW(G_OTHER_CARS) = DSW((u16)(DSW(G_OTHER_CARS) + CR_NEXT));
                    mode = 1;
                }
            }
            DSW(DS_g_mirror) = 1;
            drv_pal_normal();
            u16 c = cur_car();
            if (c == 0) {
                page_copy_rect(g_back(), g_front(), 0, 0, 0x13F, 199);
                DSB(DS_garage_title) = 0;
            } else {
                DSW(0x827A) = (u16)model_pic(DSS((u16)(c + CR_MODEL)));
                DSW(0x827C) = (u16)(s16)DSC((u16)(c + CR_COLOUR));
                DSW(0x827E) = (u16)tyre_grade(c);
                if (mode == 1) set_paint_palette(DSS(0x827C));
                DSW(0x8280) = 0;
                DSW(0x8282) = DSW((u16)(c + CR_MODEL));
                DSW(0x8284) = (u16)((DSW((u16)(c + CR_FLAGS)) & 0xFF00) | (DSW(0x8284) & 0xFF));
                for (u16 i = 0; i < 5; i++)          /* NB: an empty slot reads DS:0004 (as the original) */
                    DSW((u16)(DS_bay_slot_pic + 2 * i)) = DSW((u16)(DSW((u16)(c + CR_ENGINE + 2 * i)) + PT_TYPE));
                u16 fl = car_flags(c);
                strcpy(ds_str(DS_garage_title), ds_str((u16)(TEXTS + model_ad(DSS((u16)(c + CR_MODEL))))));
                car_draw(mode == 2 ? 3 : 4, DSS(0x8282), (s16)fl, (s16)((DSW(0x8284) >> 8) & 0x1F), 0, 0x98, 0,
                         tyre_pics(DSS(0x827E)));
            }
        }
    }
    /* 6bb6: hot spots */
    if (cur_car() == 0) garage_hotspots_nocar();
    else garage_hotspots_car(DSS(0x8282), DSS(DS_car_x), DSS(DS_car_y));
    garage_hotspots_register();
    status_label(ds_str(DSW(0x49DC)));
    cursor_ctl(-2);
    if (DSS(G_CARS_USED) >= 0x10) {
        msg_box(0xD30);
    } else if (DSS(G_PARTS_USED) >= 0x83) {
        if (DSS(G_PARTS_USED) == 0x83) {
            msg_box(0xD76);
        } else {                                   /* "... Must sell 12 parts to make enough room." */
            char *q = ds_str(0x39C9);
            q += strlen(q) + 1;
            q += strcspn(q, ds_str(0x50B6));
            s16 v = (s16)(DSS(G_PARTS_USED) - 0x82);
            sprintf(q, "%d", v);                   /* MS C itoa, radix 10 */
            q[strlen(q)] = ' ';
            msg_box(0xDC7);
        }
    }
    if (broke_check() == 0) {
        music_mute(0);
        input_reset();
        code = ui_menu(10);
    } else {
        msg_box(0x17FF);                           /* You're outta dough. */
        DSW(DS_game_end_flags) |= 0x10;
    }
    summer_over_check();
    if (DSW(DS_game_end_flags) & 0x10) DSW(DS_game_end_flags) = 0x10;
    if (DSW(DS_game_end_flags) != 0) {
        code = 0x29A;
        if ((DSW(DS_game_end_flags) & 1) == 0) cursor_ctl(-4);
    }
    return code;
}

/* ============================================================================== 0000:6f6b */

void paint_job(void)
{
    if (cur_car() == 0) { msg_box(0xEFA); return; }
    if (DSS(DS_driver_id) != -2 && DSS(DS_driver_id) != -6) { msg_box(0x18AD); return; }
    s16 x, y;
    FarPtr saved = ui_save_bg(0x0E, &x, &y, 2);
    ui_push(0x0E);
    s16 c = DSC((u16)(cur_car() + CR_COLOUR));
    s16 b;
    for (;;) {
        ega_set_palreg(0x0C, DSC((u16)(PAINT + 2 * c)));
        do b = ui_wait(3000); while (b == 0);
        if (b == -0x17) break;                     /* Forget it */
        if (b == 1) { if (--c < 0) c = 5; continue; }
        if (b == 2) { if (++c > 5) c = 0; continue; }
        if (b == 3) {
            if (money() >= 20) {
                DSW(DS_g_mirror) = 1;
                ui_pop(1);
                screen_put_bitmap_mirror(saved, x, y);
                arena_pop(1);
                screen_put_bitmap_mirror(ds_far(0x0294), DSS(DS_sticker_x), DSS(DS_sticker_y));
                u16 car = cur_car();
                DSB((u16)(car + CR_FLAGS + 1)) &= 0xE0;
                DSB((u16)(car + CR_COLOUR)) = (u8)c;
                set_paint_palette((s8)c);
                ega_set_palreg(0x0C, 5);
                clock_add(0x888);
                money_add(-20);
                return;
            }
            msg_box(0x10A1);                       /* Paint ain't free! */
        }
        break;
    }
    ui_pop(1);
    ega_set_palreg(0x0C, 5);
    screen_put_bitmap_mirror(saved, x, y);
    arena_pop(1);
}

/* ============================================================================== 0000:711a */

void customize(s16 what)
{
    u16 m = (u16)(DSS((u16)(cur_car() + CR_MODEL)) * 10);   /* computed before the null test (sic) */
    u16 label = 0;                                 /* [bp-8] */
    s16 cost = 0;                                  /* [bp-10h] */
    u16 hours = 0;                                 /* [bp-0Eh] */
    if (cur_car() == 0) { msg_box(0xEFA); return; }
    u16 fl = DSW((u16)(cur_car() + CR_FLAGS));
    if (what & 4) {
        if (DSS((u16)(m + 0x8DF2)) == 0) { msg_box(0x1914); return; }   /* Ya can't chop this roof */
        label = (fl & 0x2000) ? 0x28B1 : 0x28A1;
        cost = 0x46; hours = 0x2220;
    }
    if (what & 2) {
        if (DSS((u16)(m + 0x8DF6)) == 0) { msg_box(0x1936); return; }
        label = (fl & 0x4000) ? 0x28D7 : 0x28C4;
        cost = 0x0F; hours = 0x888;
    }
    if (what & 1) {
        if (DSS((u16)(m + 0x8DF8)) == 0) { msg_box(0x1936); return; }
        label = (fl & 0x8000) ? 0x28D7 : 0x28C4;
        cost = 0x0F; hours = 0x888;
    }
    if (money() < (s32)cost) { msg_box(0x1959); return; }   /* Customization ain't free! */
    s16 x, y;
    FarPtr saved = ui_save_bg(0x22, &x, &y, 2);
    DSW(0x1E38) = (u16)(label - 0x239E);           /* dialog title */
    ui_push(0x22);
    s16 b;
    do b = ui_wait(3000); while (b == 0);
    if (b == -0x16) {
        ui_pop(1);
        screen_put_bitmap_mirror(saved, x, y);
        arena_pop(1);
        return;
    }
    if (b != -0x14) return;                        /* other codes: the dialog stays pushed (sic) */
    u16 car = cur_car();
    if (what & 4) DSB((u16)(car + CR_FLAGS + 1)) ^= 0x20;
    if (what & 2) DSB((u16)(car + CR_FLAGS + 1)) ^= 0x40;
    if (what & 1) DSB((u16)(car + CR_FLAGS + 1)) ^= 0x80;
    cursor_ctl(-4);
    ui_pop(1);
    screen_put_bitmap_mirror(saved, x, y);
    arena_pop(1);
    s16 model = DSS((u16)(car + CR_MODEL));
    u16 cf = car_flags(car);
    car_draw(8, model, (s16)cf, (s16)((DSW((u16)(car + CR_FLAGS)) >> 8) & 0x1F), 0, 0x98, 0, tyre_pics(tyre_grade(car)));
    fx_clank();
    cursor_ctl(-2);
    clock_add(hours);
    money_add((s16)-cost);
}

/* ============================================================================== 0000:7367 */

void stickers(void)
{
    if (cur_car() == 0) { msg_box(0xEFA); return; }
    if (money() < 5) { msg_box(0x1975); return; }  /* Stickers ain't free! */
    DSW(DS_g_mirror) = 1;
    FarPtr save = arena_bitmap_alloc(0x108, 0x45, 0, 2);
    screen_save_rect(0x18, 0x0B, 0x108, 0x45, save);
    show_picture_at(0x4B0, 0x18, 0x0B, -1);        /* LIB2 #200 sticker sheet */
    ui_push(0x23);
    s16 b;
    do b = ui_wait(3000); while (b == 0);
    if (b < -0x26) return;
    if (b > -0x1F) {
        if (b != -0x16) return;                    /* -0x1E..: returns with the sheet pushed (sic) */
        ui_pop(1);
        screen_put_bitmap_mirror(save, 0x18, 0x0B);
        arena_pop(1);
        return;
    }
    cursor_ctl(-3);
    ui_pop(1);
    screen_put_bitmap_mirror(save, 0x18, 0x0B);
    arena_pop(1);
    s16 n = (s16)(-0x1E - b);
    u16 car = cur_car();
    DSW((u16)(car + CR_FLAGS)) = (u16)(((u16)(n & 0x1F) << 8) | (DSW((u16)(car + CR_FLAGS)) & 0xE0FF));
    FarPtr mask = pic_get(0x4C2, 2);               /* LIB2 #194 */
    FarPtr under = ds_far(0x0294);                 /* the car picture under the sticker */
    FarPtr comp = arena_bitmap_alloc((s16)desc_w(under), (s16)desc_h(under), 0, 2);   /* [bp-4] */
    bitmap_copy_into(under, comp, 1);
    u16 st = (u16)(0x7844 + 6 * n);
    pic_draw_masked(comp, DSS(st), (u8)(DSB((u16)(st + 2)) + DSB(0x8276)), DSB((u16)(st + 4)), 0);
    FarPtr tmp = arena_bitmap_alloc((s16)desc_w(comp), (s16)desc_h(comp), 0, 2);    /* [bp-48h] */
    bitmap_copy_into(comp, tmp, 1);
    s16 h = (s16)desc_h(comp);
    Rect r30 = { 8, h, 0, 0, 0, 0 };               /* composite -> tmp, growing */
    Rect r44 = { 8, h, 0, 0, 0, 0 };               /* peel mask onto tmp at the front */
    Rect r1c = { 8, h, 0, 0, DSS(DS_sticker_x), DSS(DS_sticker_y) };   /* tmp -> screen, growing */
    loop_spray(1);
    for (s16 i = 0; (s16)desc_w(comp) > i; i = (s16)(i + 8)) {
        drv_blit(comp, tmp, &r30, 1);
        if ((s16)(desc_w(comp) - 8) > i) drv_blit_masked(mask, tmp, &r44, 1);
        wait_vretraces(2);
        drv_blit(tmp, g_front(), &r1c, 1);
        r30.w = (s16)(r30.w + 8);
        r1c.w = (s16)(r1c.w + 8);
        r44.dx = (s16)(r44.dx + 8);
    }
    screen_put_bitmap_mirror(comp, DSS(DS_sticker_x), DSS(DS_sticker_y));
    arena_pop(3);
    loop_spray(0);
    clock_add(0x222);
    money_add(-5);
    cursor_ctl(-1);
}

/* ============================================================================== 0000:7800 */

void change_tires(void)
{
    u16 node[142];
    s8 wear[142];
    s16 sel = 1;                                   /* PORT: uninitialised for codes other than -3 */
    u16 car = cur_car();
    if (car == 0) { msg_box(0xEFA); return; }
    spares_collect(4, node, wear, 0);
    s16 r = list_box(4, 1, NULL, (s16 *)mp(DGROUP, G_LIST_BUF), wear, NULL);
    if (r == -4) return;
    if (r == -3) sel = list_selected();
    else if (r == 0x3EA) { msg_box(0xF4E); return; }   /* no spare tires */
    s16 og = tyre_grade(car);
    s16 ng = parts_grade(DSS((u16)(node[sel] + PT_TYPE)));
    part_install(spare_unlink(node[sel]), (u16)(car + CR_TYRES));
    tire_change_anim(tyre_pics(og), tyre_pics(ng));
    clock_add(0x444);
}

/* ============================================================================== 0000:78f1, 79eb */

void car_info_list(s16 *xoff, s8 *wear)
{
    u16 car = cur_car();
    u16 parts[7];
    for (u16 i = 0; i < 5; i++) parts[i] = DSW((u16)(car + CR_ENGINE + 2 * i));
    parts[5] = DSW((u16)(car + CR_TRANS));
    parts[6] = DSW((u16)(car + CR_TYRES));
    s16 n = 0;
    DSW(G_LIST_BUF) = 0;
    for (u16 i = 0; i < 7; i++) {
        u16 p = parts[i];
        if (p == 0) continue;
        n++;
        DSW(G_LIST_BUF) = (u16)n;
        xoff[n] = DSS((u16)(0x50CE + 2 * i));
        DSW((u16)(G_LIST_BUF + 2 * n)) = (u16)parts_text(DSS((u16)(p + PT_TYPE)));
        s16 w = DSS((u16)(p + PT_WEAR));
        wear[n] = w == -0x80 ? (s8)0x80 : (s8)(w / 100);
    }
    for (s16 k = 1; k < 5; k++) {
        n++;
        DSW(G_LIST_BUF) = (u16)n;
        DSW((u16)(G_LIST_BUF + 2 * n)) = (u16)-k;
        wear[n] = (s8)0x80;
        xoff[n] = 0;
    }
}

void car_info(void)
{
    s8 wear[144];
    s16 xoff[143];
    if (cur_car() == 0) { msg_box(0xEFA); return; }
    car_info_list(xoff, wear);
    s16 v = car_runnable(cur_car(), 1) == 0 ? 0 : car_max_speed(-1);
    sprintf(ds_str((u16)(DSW(0x5124) + 9)), ds_str(0x5108), v);                 /* " %3i mph" */
    sprintf(ds_str((u16)(DSW(0x5120) + 5)), ds_str(0x5111),
            (s16)(DSS((u16)(cur_car() + CR_GAS)) / 10));                        /* " %2i gallons" */
    s16 r = list_box(0x26, 1, xoff, (s16 *)mp(DGROUP, G_LIST_BUF), wear, (const u16 *)mp(DGROUP, 0x511E));
    if (r == -3) return;
    if (r != 0x3EA) return;
    msg_box(0xF4E);
}
