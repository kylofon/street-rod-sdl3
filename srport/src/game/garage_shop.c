/* Garage: buying and selling (garage.md §4.4 - §4.6): the classified ads 0000:442b with the ad pages
 * 0000:08b4 / 09b7 / 09cb, selling spare parts 0000:3c8d, how_about 0000:4940, "Your cars"
 * 0000:49ed, sfx_play_wait 0000:4fca, message_at 0000:4ffa.
 *
 * The floating-point code of 3c8d and 49ed was read from the disassembly with the MSC emulator
 * interrupts decoded (garage.md §8): the 8087 emulator computes in 80-bit extended precision, so the
 * port uses long double (x87 extended on the MinGW / GCC x86 targets); _ftol (1e16:2de9) truncates
 * toward zero. `fild dword` of a 16-bit value with dx = 0 is an unsigned extension, `fild / fimul
 * word` a signed one. */
#include "game/garage.h"

#include <stdio.h>
#include <string.h>

#include "game/flow.h"
#include "game/ui.h"
#include "platform/platform.h"
#include "platform/video.h"

#define TEXTS       0x2C02
#define LIST        G_LIST_BUF
#define W49E0(i)    DSW((u16)(LIST + 2 * (i)))

static inline s32 money(void) { return DSSL(DS_money); }
static inline void clock_add(u16 v) { DSL(DS_game_clock) += v; }
static inline s16 ftol16(long double v) { return (s16)(s32)v; }   /* 1e16:2de9: AX of the truncation */

/* ======================================================= the classified ad pages (0000:08b4 / 09cb) */

#define CL_NPAGES   0x0516      /* pages */
#define CL_PAGE     0x0518      /* current page */
#define CL_LIST     0x051A      /* near: the list (DS:49E0) */
#define CL_PIC      0x051C      /* far: the page picture */
#define CL_REDRAW   0x0520      /* u8 */
#define CL_RECT     0x0522      /* Rect {w, h, sx, sy, dx, dy} + 052E */
#define CL_STARTED  0x0536
#define CL_PAGES    0x04FE      /* u16[]: first item of each page */
#define CL_FIRST    0x7EBE      /* first item on the page */
#define CL_LAST     0x7F84      /* last item on the page */

static inline s16 ad_lines(s16 text) { return DSC((u16)(text + 0x2C01)); }   /* imul byte: 9 * (s8) */

/* 0000:08b4: pages of the ad list (190 rows each, lines*9+3 per ad) and the page holding `item`;
 * opens screen 8. */
static void classifieds_open(u16 list, s16 item)
{
    DSW(CL_STARTED) = 0;
    arena_reset();
    DSW(CL_LIST) = list;
    DSW(CL_NPAGES) = 0;
    u16 p = (u16)(list + 2);                       /* [bp-6] */
    s16 y = 0xBE;                                  /* [bp-4] */
    s16 total = DSS(list);                         /* [bp-8] */
    s16 left = total;                              /* [bp-2] */
    item = (s16)(total - item);                    /* [bp+8] */
    DSW(CL_PAGE) = 1;
    while (left-- != 0) {
        y = (s16)(y + (s16)(9 * ad_lines(DSS(p))) + 3);
        if (y > 0xBD) {
            s16 h = (s16)((s16)(9 * ad_lines(DSS(p))) + 3);
            s16 cx;
            do {
                DSW((u16)(CL_PAGES + 2 * DSS(CL_NPAGES))) = (u16)(total - left);
                DSW(CL_NPAGES) = (u16)(DSS(CL_NPAGES) + 1);
                cx = h;
            } while (cx > 0xBD);
            y = cx;
        }
        if (left == item) DSW(CL_PAGE) = (u16)(DSS(CL_NPAGES) - 1);
        p = (u16)(p + 2);
    }
    u16 sch = DSW(DS_g_scheme);
    for (u16 i = 0; i < 5; i++) DSB((u16)(DS_g_text_fg + i)) = DSB((u16)(sch + 0x41 + i));
    cursor_ctl(-3);
    DSW(CL_RECT) = 0x40;
    DSW((u16)(CL_RECT + 2)) = 0xBE;
    DSW((u16)(CL_RECT + 0xC)) = DSW((u16)(CL_RECT + 0xA)) = DSW((u16)(CL_RECT + 8)) =
        DSW((u16)(CL_RECT + 6)) = DSW((u16)(CL_RECT + 4)) = 0;
    DSB(CL_REDRAW) = 1;
    ui_push(8);
    cursor_ctl(-1);
}

/* 0000:09b7 */
static FarPtr classifieds_pic(s16 pool) { return pic_get(0x403, pool); }

/* 0000:09cb: draws the page (hot spots: records from 0xF94 = screen 8's, code = item or 0 for the
 * section headers >= 0xCD8, the rest disabled) and waits; -1 / -2 page back / forward, -3 garage,
 * else the clicked item (1-based list index). */
static s16 classifieds_run(void)
{
    u16 list = DSW(CL_LIST);
    s16 total = DSS(list);                         /* [bp-18h] */
    s16 y = 0;                                     /* [bp-0Ah] */
    for (;;) {
        if (DSB(CL_REDRAW) != 0) {
            cursor_ctl(-4);
            u16 sch = DSW(DS_g_scheme);
            for (u16 i = 0; i < 5; i++) DSB((u16)(DS_g_text_fg + i)) = DSB((u16)(sch + 0x41 + i));
            ds_far_wr(CL_PIC, classifieds_pic(2));
            fill_rect(g_back(), 0x40, 0, 0xC0, 0xBE);
            DSW((u16)(CL_RECT + 8)) = DSW((u16)(CL_RECT + 4)) = 0;
            drv_blit(ds_far(CL_PIC), g_back(), (const Rect *)mp(DGROUP, CL_RECT), 1);
            DSW((u16)(CL_RECT + 4)) = 0x40;
            DSW((u16)(CL_RECT + 8)) = 0x100;
            drv_blit(ds_far(CL_PIC), g_back(), (const Rect *)mp(DGROUP, CL_RECT), 1);
            arena_pop(1);
            FarPtr bk = g_back();
            u16 save_h = rd16(bk.seg, (u16)(bk.off + 2));
            wr16(bk.seg, (u16)(bk.off + 2), 0xBE);
            y = 0;
            s16 it = DSW(CL_STARTED) != 0 ? DSS(CL_FIRST) : DSS((u16)(CL_PAGES + 2 * DSS(CL_PAGE)));
            u16 p = (u16)(list + 2 * it);          /* [bp-12h] */
            u16 rec = 0x0F82;                      /* [bp-14h] */
            DSW(CL_FIRST) = (u16)it;
            for (;;) {
                hline(g_back(), 0x44, (s16)(y + 1), 0xB8);        /* ad7 */
                y = (s16)(y + 3);
                if (y > 0xBD) break;
                rec = (u16)(rec + UI_REC_SIZE);
                DSW((u16)(rec + UI_R_Y0)) = (u16)(y - 2);
                DSW((u16)(rec + UI_R_TYPE)) = 0x0B;
                s16 v = DSS(p);
                p = (u16)(p + 2);
                DSW((u16)(rec + UI_R_CODE)) = (u16)(v >= 0xCD8 ? 0 : it);
                u16 s = (u16)(v + TEXTS);
                drv_draw_text(g_back(), 0x44, y, ds_str(s));
                s16 di = y;
                for (;;) {                                         /* b74 */
                    s = (u16)(s + strlen(ds_str(s)) + 1);
                    s8 c = DSC(s);
                    if (c == '+') {
                        s++;
                        drv_draw_text(g_back(), DSS(DS_g_text_end_x), di, ds_str(s));
                        continue;
                    }
                    if (c < 0x20) break;
                    di = (s16)(di + 9);
                    if (di > 0xBD) break;
                    drv_draw_text(g_back(), 0x44, di, ds_str(s));
                }
                y = (s16)(di + 9);                                 /* b97 */
                s16 y1 = (s16)(y + 1);
                if (y1 > 0xBD) y1 = 0xBD;
                DSW((u16)(rec + UI_R_Y1)) = (u16)y1;
                if (it >= total) {
                    it = 0;
                    p = (u16)(list + 2 * DSS(CL_PAGES));
                }
                if (y >= 0xBD) break;
                it++;
                if (it > total) break;
            }
            DSW(CL_LAST) = (u16)it;                                /* be1 */
            DSW(CL_STARTED) = 1;
            s16 si = (s16)(y + 1);
            s16 y1 = si > 0xBD ? 0xBD : si;
            DSW((u16)(rec + UI_R_Y1)) = (u16)y1;
            if (y1 < 0xBD) hline(g_back(), 0x44, si, 0xB8);
            rec = (u16)(rec + UI_REC_SIZE);
            while (DSS((u16)(rec + UI_R_CODE)) >= 0) {
                DSW((u16)(rec + UI_R_TYPE)) = 0x0E;
                rec = (u16)(rec + UI_REC_SIZE);
            }
            DSB(CL_REDRAW) = 0;
            wr16(bk.seg, (u16)(bk.off + 2), save_h);
            cursor_ctl(-1);
            if (DSW(DS_g_mirror) != 0) page_copy_rect(g_back(), g_front(), 0, 0, 0x13F, 0xBD);
            click_clear();
        }
        s16 r;
        do r = ui_wait(3000); while (r == 0);
        if (r == -3) return -3;
        if (r == -2) {
            DSW(CL_PAGE) = (u16)(DSS(CL_PAGE) + 1);
            if (DSS(CL_PAGE) >= DSS(CL_NPAGES)) DSW(CL_PAGE) = 0;
            DSW(CL_FIRST) = (u16)((y > 0xC1 ? 0 : 1) + DSS(CL_LAST));
            DSB(CL_REDRAW) = 1;
            continue;
        }
        if (r != -1) {
            DSB(CL_REDRAW) = 1;
            return r;
        }
        DSW(CL_PAGE) = (u16)(DSS(CL_PAGE) - 1);
        if (DSS(CL_PAGE) < 0) DSW(CL_PAGE) = (u16)(DSS(CL_NPAGES) - 1);
        s16 c = (s16)(DSS(CL_FIRST) - 1);
        if (c < 1) c = total;
        s16 si = 0xBD;
        for (;;) {
            si = (s16)(si - ((s16)(9 * ad_lines(DSS((u16)(list + 2 * c)))) + 3));
            if (si <= 0) break;
            c--;
            if (c < 1) c = total;
        }
        DSW(CL_LAST) = (u16)c;
        DSW(CL_FIRST) = (u16)c;
        DSB(CL_REDRAW) = 1;
    }
}

/* ============================================================================== 0000:442b */

void classifieds(s16 section)
{
    s8 item[70];                                   /* [bp-4Ch] */
    DSW(DS_drive_dest) = 5;
    W49E0(1) = 0xCD8;                              /* "---------- USED CARS ----------" */
    s16 n = 2, k = 0;
    for (s16 m = (s16)(s8)DSB(0x7D16); m >= 0; m = model_next(m)) {
        W49E0(n) = (u16)model_ad(m);
        n++;
        item[k++] = (s8)m;
    }
    s16 first_part = (s16)(n + 1);
    W49E0(n) = 0xCFD;                              /* "------------ PARTS ------------" */
    s16 bias = (s16)(k - first_part);
    n = first_part;
    for (s16 t = 0; t < 0x2C; t++)
        if (parts_price(t) > 0x13) {
            W49E0(n) = (u16)parts_text(t);
            n++;
            item[k++] = (s8)t;
        }
    W49E0(0) = (u16)(n - 1);
    classifieds_open(LIST, section == 1 ? 2 : 0x1C);
    for (;;) {
        s16 r = classifieds_run();
        if (r == -3) break;
        bool is_part = first_part <= r;
        s16 idx, text;
        u16 price;
        u16 mrec = 0;                              /* [bp-56h] */
        if (!is_part) {
            idx = item[r - 2];
            mrec = (u16)(MODELS_TAB + 10 * idx);
            text = model_ad(idx);
            price = (u16)model_price(idx);
            DSW(0x11B0) = 10;
        } else {
            idx = item[r + bias];
            text = parts_text(idx);
            price = (u16)parts_price(idx);
            DSW(0x11B0) = 0x0E;
        }
        strcpy(ds_str(0x2990), ds_str((u16)(TEXTS + text)));
        sprintf(ds_str(0x29D6), ds_str(0x4F80), (s16)price);
        s8 colour = -1;
        s16 b;
        bool again = false;
        while ((b = ui_menu(9)) != -0x14) {
            if (b != -0x19) { again = true; break; }
            /* "See it" */
            cursor_ctl(-4);
            arena_reset();
            FarPtr save = arena_bitmap_alloc(0x140, 0x52, 0, 0);
            screen_save_rect(0, 0x40, 0x140, 0x52, save);
            u16 sch = DSW(DS_g_scheme);
            DSB(DS_g_text_bg) = DSB((u16)(sch + 0x38));
            DSB(DS_g_line_col) = DSB((u16)(sch + 0x3A));
            DSB(DS_g_frame_style) = DSB((u16)(sch + 0x3B));
            u16 bg = DSW(0x02CC);
            if (DSS(DS_driver_id) != -2 && DSS(DS_driver_id) != -6) bg = DSW(0x02BE);
            screen_fill_rect(0, 0x41, 0x140, 0x46, (u8)bg);
            screen_fill_rect(0, 0x87, 0x140, 10, DSB(DS_g_text_bg));
            u8 lc = DSB(DS_g_line_col);
            if (DSS(DS_driver_id) != -2 && DSS(DS_driver_id) != -6) {
                DSB(DS_g_line_col) = 3;
                vline(g_back(), 0, 0x40, 0x52);
                vline(g_back(), 0x13F, 0x40, 0x52);
                vline(g_front(), 0, 0x40, 0x52);
                vline(g_front(), 0x13F, 0x40, 0x52);
                screen_hline(0, 0x40, 0x140);
                screen_hline(0, 0x91, 0x140);
            }
            DSB(DS_g_line_col) = lc;
            if (colour == -1) {
                colour = (s8)rnd(5);
                set_paint_palette(colour);
            }
            u16 spec = DSW((u16)(mrec + 4));
            car_draw(7, (s16)((u16)(mrec - MODELS_TAB) / 10), (spec >> 8 & 7) == 2 ? 8 : 0, 0, 0, 0x86, 0,
                     tyre_pics((s16)((spec >> 4) & 3)));
            s16 w = font_string_width(ds_str(0x2990));
            screen_text((s16)((s16)(0x140 - w) / 2), 0x88, ds_str(0x2990));
            cursor_ctl(-2);
            DSW(DS_wait_any_key) = 1;
            ui_menu(0x18);
            DSW(DS_wait_any_key) = 0;
            screen_put_bitmap_mirror(save, 0, 0x40);
            arena_reset();
        }
        if (again) continue;
        s32 sp = (s32)(s16)price;
        if (DSW(DS_demo_active) != 0 && money() < sp) DSSL(DS_money) = money() + 1999;
        if (money() < sp) {
            char *q = strchr(ds_str(0x3A5A), '$');
            sprintf(q + 1, ds_str(0x4F88), (s16)(price - (u16)DSW(DS_money)));
            msg_box(-0xE58);
            continue;
        }
        money_add((s16)-price);
        if (!is_part) {
            u16 c = car_new(idx, (s16)price, 0);
            if (colour == -1) colour = (s8)rnd(5);
            DSB((u16)(c + CR_COLOUR)) = (u8)colour;
            msg_box(-0xD22);
            clock_add(0x444);
            break;
        }
        spare_add(idx, (s16)price);
        clock_add(0x222);
        if (DSS(G_PARTS_USED) > 0x82) break;
    }
    hotspots_reset();
    if (DSW(G_CUR_CAR) != 0) set_paint_palette(DSC((u16)(DSW(G_CUR_CAR) + CR_COLOUR)));
    DSW(DS_drive_dest) = 0;
}

/* ============================================================================== 0000:3c8d */

void sell_spare_parts(void)
{
    u16 node[142];                                 /* [bp-11Eh] */
    s8 wear[142];                                  /* [bp-1BAh] */
    s16 rounds = 0;                                /* [bp-12Ah] */
    s16 sel = 1;                                   /* [bp-2] */
    for (;;) {
        s16 n = 0;
        for (u16 p = DSW(G_SPARES); p != 0; p = DSW((u16)(p + PT_NEXT))) {
            n++;
            node[n] = p;
            W49E0(n) = (u16)parts_text(DSS((u16)(p + PT_TYPE)));
            s16 w = DSS((u16)(p + PT_WEAR));
            wear[n] = w == -0x80 ? (s8)0x80 : (s8)(w / 100);
        }
        W49E0(0) = (u16)n;
        memcpy(ds_str(0x2A3E), ds_str(0x4F5C), 8);  /* "Sell it" */
        DSB(DSW(0x57A7)) = 0x1F;
        s16 r = list_box(4, (u16)sel <= (u16)n ? sel : n, NULL, (s16 *)mp(DGROUP, LIST), wear, NULL);
        sel = r;
        DSB(DSW(0x57A7)) = 0x2E;
        memcpy(ds_str(0x2A3E), ds_str(0x4F64), 7);  /* "Change" */
        if (r == -4) return;
        if (r == -3) sel = list_selected();
        else if (r == 0x3EA) {
            msg_box(rounds != 0 ? 0xED7 : 0xF2A);
            return;
        }
        u16 p = node[sel];
        u16 value = (u16)part_value(p);            /* [bp-120h] */
        s16 t = DSS((u16)(p + PT_TYPE));
        s16 cond = (s16)((s16)(0x2710 - DSS((u16)(p + PT_WEAR))) / 100);   /* [bp-122h] */
        u16 offer = value;
        switch ((u8)parts_category(t)) {
        case 0: case 1: {                          /* engine, transmission */
            long double k = (long double)(u32)(u16)cond / 100.0L;
            s16 a = ftol16((long double)(u32)value * k * 0.9L);
            value = (u16)(value - (u16)a);
            offer = (u16)(ftol16((long double)(u32)value * k * 0.9L) + a);
            break;
        }
        case 2: case 3: {                          /* carburettor, manifold */
            s16 rr = (s16)(rnd(0x14) + 0x46);
            offer = (u16)ftol16((long double)(u32)value * ((long double)rr / 100.0L));
            break;
        }
        case 4:
            offer = 0;
            break;
        default:
            break;
        }
        offer = (u16)(offer / 5 * 5);
        strcpy(ds_str(0x2A5F), ds_str((u16)(TEXTS + parts_text(t))));
        s16 id;
        if (offer != 0) {
            id = 0x100A;
            sprintf(ds_str(0x3C0C), ds_str(0x4F6B), (s16)offer);   /* "Will you take $%-d ?" */
        } else {
            id = 0x1021;                           /* "I'll take it off your hands for nothing." */
        }
        msg_draw_at(id, -1, -1);
        s16 b = ui_dialog(0x10);
        msg_erase();
        if (b != -0x15) {
            if (sel == 1) DSW(G_SPARES) = DSW((u16)(p + PT_NEXT));
            else DSW((u16)(node[sel - 1] + PT_NEXT)) = DSW((u16)(p + PT_NEXT));
            part_free(p);
            clock_add(0x444);
            money_add((s16)offer);
        }
        rounds++;
    }
}

/* ============================================================================== 0000:4940 */

s16 how_about(u16 *offer)
{
    u16 r = (u16)(*offer % 5);
    if (r != 0) {
        *offer = (u16)(*offer - r);
        if (*offer == 0) *offer = 5;
    }
    sprintf(ds_str(DSW(0x4FA4)), ds_str(0x4F92), (s16)*offer);   /* "How about $%-d ?" */
    msg_draw_at(0xFF7, -1, -1);
    s16 b = ui_menu(0x0D);
    msg_erase();
    return b;
}

/* ============================================================================== 0000:49ed */

/* The edit loop of the haggle: edit_number until a non-zero ask or "Never mind" (-22). */
static bool haggle_edit(u16 *ask)
{
    do {
        if (edit_number(ask, ds_str(0x29F8)) == -0x16) return false;
    } while (*ask == 0);
    return true;
}

s16 your_cars(void)
{
    u16 node[145];                                 /* [bp-122h]: node[i] */
    FarPtr saved = { 0, 0 };                       /* [bp-4] */
    s16 rounds = 0;                                /* [bp-130h] */
    s16 sel = 1;                                   /* [bp-132h] */
    s16 sx = 0, sy = 0;                            /* [bp-12Ah], [bp-12Eh] */
    u16 ask = 0;                                   /* [bp-134h] */
    u16 offer = 0;                                 /* [bp-6] */
    u16 base;                                      /* [bp-124h] */
    for (;;) {
        s16 n = 0;
        u16 cur = DSW(G_CUR_CAR);
        if (cur != 0) {
            n = 1;
            node[1] = cur;
            W49E0(1) = (u16)model_ad(DSS((u16)(cur + CR_MODEL)));
        }
        for (u16 c = DSW(G_OTHER_CARS); c != 0; c = DSW((u16)(c + CR_NEXT))) {
            n++;
            node[n] = c;
            W49E0(n) = (u16)model_ad(DSS((u16)(c + CR_MODEL)));
        }
        W49E0(0) = (u16)n;
        s16 r = list_box(0x0B, n <= sel ? n : sel, NULL, (s16 *)mp(DGROUP, LIST), NULL, NULL);
        sel = r;
        if (r == -3) return 0;
        if (r == 0x3EA) {
            msg_box(rounds != 0 ? 0xEE9 : 0xEFA);
            return 0;
        }
        if (r == -1) {                             /* Switch it */
            sel = list_selected();
            DSW(DS_switch_new) = node[sel];
            DSW(DS_switch_old) = DSW(G_CUR_CAR);
            u16 nw = DSW(DS_switch_new);
            if (nw == DSW(G_CUR_CAR)) {
                DSW(DS_switch_new) = 0;
                return 3;
            }
            if ((DSW(G_CUR_CAR) != 0 ? 2 : 1) == sel) DSW(G_OTHER_CARS) = DSW((u16)(nw + CR_NEXT));
            else DSW((u16)(node[sel - 1] + CR_NEXT)) = DSW((u16)(nw + CR_NEXT));
            if (DSW(G_CUR_CAR) != 0) {
                DSW((u16)(DSW(G_CUR_CAR) + CR_NEXT)) = DSW(G_OTHER_CARS);
                DSW(G_OTHER_CARS) = DSW(G_CUR_CAR);
            }
            DSW(G_CUR_CAR) = nw;
            return 3;
        }
        /* any other code: Sell it */
        sel = list_selected();
        u16 car = node[sel];                       /* [bp-126h] */
        {
            s16 rr = rnd(0x14);
            base = (u16)ftol16(((long double)rr + 75.0L) * (long double)DSS(car) / 100.0L);
        }
        offer = base;
        strcpy(ds_str(0x2990), ds_str((u16)(TEXTS + model_ad(DSS((u16)(car + CR_MODEL))))));
        DSB(0x29F8) = 0;
        saved = ui_save_bg(0x0C, &sx, &sy, 1);
        ui_push(0x0C);
        s16 b = edit_number(&ask, ds_str(0x29F8));
        while (b != -0x16) {
            if (ask == 0) { b = edit_number(&ask, ds_str(0x29F8)); continue; }
            if ((u16)ftol16((long double)DSS(car) * 1.15L) < ask) { msg_box(0xEC0); break; }   /* You must be kidding! */
            if (DSW(car) < ask) {
                msg_box(0x106E);                   /* No way! */
                if (!haggle_edit(&ask)) break;
                if (DSW(car) < ask) { msg_box(0x1078); break; }   /* Get lost! */
            }
            if (ask > offer) {
                s16 rr = rnd(5);
                offer = (u16)ftol16(((long double)rr + 85.0L) * (long double)(u32)base / 100.0L);
                if (ask > offer && how_about(&offer) == -0x14) ask = offer;
            }
            if (ask > offer && !haggle_edit(&ask)) break;
            if (ask > offer) {
                s16 rr = rnd(5);
                offer = (u16)ftol16(((long double)rr + 91.0L) * (long double)(u32)base / 100.0L);
                if (ask > offer && how_about(&offer) == -0x14) ask = offer;
            }
            if (ask > offer && !haggle_edit(&ask)) break;
            if ((long double)(u32)ask * 0.9L > (long double)(u32)offer) { msg_box(0x1084); break; }   /* No thanks! */
            msg_box(DSW(car) > (u16)(ask << 1) ? 0x104B : 0x1091);   /* You must be crazy... / I'll take it! */
            clock_add(0x888);
            money_add((s16)ask);
            if (sel == 1 && DSW(G_CUR_CAR) != 0) {
                screen_put_bitmap_mirror(saved, sx, sy);
                arena_pop_low(1);
                saved.off = saved.seg = 0;
                cursor_ctl(-4);
                u16 c = DSW(G_CUR_CAR);
                s16 model = DSS((u16)(c + CR_MODEL));
                u16 fl = car_flags(c);
                s16 grade = parts_grade(DSS((u16)(DSW((u16)(c + CR_TYRES)) + PT_TYPE)));
                car_draw(6, model, (s16)fl, (s16)((DSW((u16)(c + CR_FLAGS)) >> 8) & 0x1F), 0, 0x98, 0, tyre_pics(grade));
                ui_pop(1);
                garage_name_clear();
                ui_push(0x0A);
                cursor_ctl(-2);
                car_free(DSW(G_CUR_CAR), 0);
                DSW(G_CUR_CAR) = 0;
            } else {
                if ((DSW(G_CUR_CAR) != 0 ? 2 : 1) == sel) DSW(G_OTHER_CARS) = DSW((u16)(car + CR_NEXT));
                else DSW((u16)(node[sel - 1] + CR_NEXT)) = DSW((u16)(car + CR_NEXT));
                car_free(car, 0);
            }
            break;
        }
        /* 4ddc */
        ui_pop(1);
        if (!far_is_null(saved)) {
            screen_put_bitmap_mirror(saved, sx, sy);
            arena_pop_low(1);
            saved.off = saved.seg = 0;
        }
        rounds++;
    }
}

/* ============================================================================== 0000:4fca, 4ffa */

void sfx_play_wait(s16 n)
{
    anim_start(n);
    while (anim_active(n)) ui_wait(10);
}

void message_at(s16 id, s16 x, s16 y)
{
    u32 saved = DSL(DS_wait_deadline);
    msg_draw_at(id, x, y);
    wait_ticks_or_input(DSS(DS_msg_ticks));
    msg_erase();
    DSL(DS_wait_deadline) = saved;
}
