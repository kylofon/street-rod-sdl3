/* Race: results, fines, damage (port/spec/race.md §4.13, read from the disassembly).
 *
 *   0000:bed4 tires_replace    0000:bf94 ticket_show     0000:c1d8 crash_show
 *   0000:c497 race_msg         0000:c52c engine_blown    0000:c5ac trans_blown
 *   0000:c613 race_results
 */
#include "game/race.h"
#include "game/race_int.h"

#include <stdio.h>
#include <string.h>

#include "game/flow.h"
#include "game/garage.h"
#include "game/ui.h"
#include "platform/platform.h"
#include "platform/video.h"
#include "sound/sound.h"

static long double dconst(u16 off)
{
    double d;
    memcpy(&d, mp(DGROUP, off), 8);
    return d;
}

/* MS C itoa(v, buf, 10) (1e16:19d0) */
static void itoa10(s16 v, char *buf)
{
    char tmp[8];
    int n = 0;
    u16 u = (u16)(v < 0 ? -v : v);
    do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) *buf++ = '-';
    while (n) *buf++ = tmp[--n];
    *buf = 0;
}

/* "$123" patch of the repair / scrap messages: p points after the '$' */
static void patch_amount(char *p, s16 v)
{
    p[1] = ' ';
    p[2] = ' ';
    itoa10(v, p);
    p[strlen(p)] = ' ';
}

/* 0000:bed4 tires_replace: blown tyres off; spare tyres or buy plain ones for $50. Returns the
 * unpaid part of the $50 (money is set to 0 then). */
s16 tires_replace(void)
{
    s16 unpaid = 0;
    u16 car = cur_car();
    if (car == 0) return 0;
    s16 v = part_release(CARW(car, CAR_TYRES), 0);
    car = cur_car();
    CARW(car, CAR_VALUE) -= (u16)v;
    CARW(car, CAR_TYRES) = 0;
    u16 nodes[0x8C];                                               /* bp-11Ch */
    memset(nodes, 0, sizeof nodes);
    spares_collect(4, nodes, NULL, 0);
    if (DSW(G_LIST_BUF) != 0) {                                    /* a spare set of tyres */
        part_install(spare_unlink(nodes[1]), (u16)(cur_car() + CAR_TYRES));
    } else {
        DSSL(DS_money) -= 50;
        if (DSSL(DS_money) < 0) {
            unpaid = (s16)-DSSL(DS_money);
            DSSL(DS_money) = 0;
        }
        spare_add(0x28, DSS(0x4946));
        u16 p = DSW(G_SPARES);
        DSW(G_SPARES) = DSW((u16)(p + PT_NEXT));
        part_install(p, (u16)(cur_car() + CAR_TYRES));
    }
    return unpaid;
}

/* width of a glyph of the font at segment DS:6952 (byte 0 of the 9-byte glyph) */
static s16 glyph_w(u8 ch)
{
    return rd8(DSW(0x6952), (u16)((s16)(s8)ch * 9 - 0x120));
}

/* text at DS:off cut to fit max_w pixels: returns the DS offset of the cut */
static u16 fit_text(u16 off, s16 max_w)
{
    u16 si = off;
    s16 cx = 0;
    while (DSB(si) != 0) {
        if (cx >= max_w) break;
        cx = (s16)(cx + glyph_w(DSB(si)));
        si++;
    }
    if (cx >= max_w) si--;
    return si;
}

/* 0000:bf94 ticket_show: the speeding ticket with the player's name and car */
static void ticket_show(void)
{
    arena_reset();
    FarPtr t = pic_get(0x456, 1);                                  /* LIB2 #110 ticket */
    FarPtr s = pic_get(0x4EF, 1);
    Rect r = { (s16)desc_w(s), (s16)desc_h(s), 0, 0, 0, 0x45 };
    drv_blit_masked(s, g_back(), &r, 1);
    r = (Rect){ (s16)desc_w(t), (s16)desc_h(t), 0, 0, (s16)desc_w(s), 0x14 };
    drv_blit(t, g_back(), &r, 1);
    s16 maxw = (s16)(desc_w(t) - 8);
    DSW(0x8240) = 0;
    memcpy(mp(DGROUP, 0x8242), mp(DGROUP, (u16)(DSW(0x787E) + 0x41)), 5);   /* colour scheme */
    /* the name */
    u16 end = fit_text(0x7E9A, maxw);
    u8 save = DSB(end);
    DSB(end) = 0;
    drv_draw_text(g_back(), 0x5E, 0x48, ds_str(0x7E9A));
    DSB(end) = save;
    /* the car: model name from its 8th character up to the first ' ', '`' or '.' */
    s16 id = DSS((u16)(MODEL(CARS(cur_car(), CAR_MODEL)) + 6));
    u16 cut = (u16)(id + (s16)strcspn(ds_str((u16)(0x2C09 + id)), ds_str(0x53C4)) + 7);
    end = fit_text((u16)(0x2C02 + id), maxw);
    save = DSB(end);
    DSB(end) = 0;
    u8 save2 = DSB((u16)(cut + 0x2C02));
    DSB((u16)(cut + 0x2C02)) = 0;
    drv_draw_text(g_back(), 0x5E, 0x63, ds_str((u16)(0x2C02 + id)));
    DSB((u16)(cut + 0x2C02)) = save2;
    DSB(end) = save;
    drv_copy_page(desc_planes(g_back()), desc_planes(g_front()));
    DSW(0x8240) = 1;
    cursor_ctl(-2);
    input_reset();
}

/* 0000:c1d8 crash_show: cockpit crash picture, the wheel knob, glass cracks above 90 mph */
static void crash_show(void)
{
    s16 ega = DSS(DS_driver_id) == -2 && DSW(DS_g_vga) == 0;
    fx_crash();
    arena_reset();
    FarPtr a = pic_get(0x4EB, 1);                                  /* LIB2 #259 */
    FarPtr b = pic_get(0x4EC, 1);                                  /* LIB2 #260 */
    Rect r = { (s16)desc_w(a), 0x5A, 0, 0, 0, 0x0B };
    drv_blit_masked(a, g_back(), &r, 1);
    r = (Rect){ (s16)desc_w(a), (s16)(desc_h(a) - 0x59), 0, 0x59, 0, (s16)((ega ? 0x5A : 0x59) + 0x0B) };
    drv_blit_masked(a, g_back(), &r, 1);
    r = (Rect){ (s16)desc_w(b), (s16)desc_h(b), 0, 0, 0, (s16)(ega ? 0x78 : 0x77) };
    drv_blit(b, g_back(), &r, 1);
    wheel_knob_crash(DSS(DS_wheel_shown));
    drv_copy_page(desc_planes(g_back()), desc_planes(g_front()));
    if (PHS(PLAYER, PH_SPEED) > 0x5A) {                            /* shattered glass */
        u16 t = 0x53D4;
        for (s16 d = 0x14; d > -4; d = (s16)(d - 8), t = (u16)(t + 6)) {
            FarPtr p = pic_get(DSS(t), 1);
            r = (Rect){ (s16)desc_w(p), (s16)desc_h(p), 0, 0, DSS((u16)(t + 2)), DSS((u16)(t + 4)) };
            wait_vretraces(d);
            drv_blit_masked(p, g_back(), &r, 1);
            drv_copy_page(desc_planes(g_back()), desc_planes(g_front()));
        }
    }
    cursor_ctl(-2);
    drv_pal_normal();
    input_reset();
    wait_ticks_or_input(100);
    input_reset();
    arena_reset();
}

/* 0000:c497 race_msg: "Your winning time ..." / "In %2.1f sec you covered only ..." (menu 0x21) */
void race_msg(void)
{
    float t;
    memcpy(&t, mp(DGROUP, DS_race_time), 4);
    if (DSW(DS_player_won) != 0) {
        snprintf(ds_str(0x2B3F), 0x33, ds_str(0x53E8), (double)t, (int)DSS(DS_avg_speed));
        memcpy(mp(DGROUP, 0x2B72), mp(DGROUP, 0x541B), 0x12);     /* "Congratulations !" */
    } else {
        snprintf(ds_str(0x2B3F), 0x33, ds_str(0x542D), (double)t, (int)DSS(DS_pct_covered));
        snprintf(ds_str(0x2B72), 0x40, ds_str(0x5451), (int)DSS(DS_avg_speed));
    }
    ui_push(0x21);
    ui_pop(1);
}

/* 0000:c52c engine_blown: engine, manifold and carburettors gone */
static void engine_blown(s16 msg)
{
    u16 car = cur_car();
    if (car != 0) {
        for (u16 s = CAR_ENGINE; s < CAR_ENGINE + 10; s = (u16)(s + 2)) {
            s16 v = part_release(CARW(car, s), 0);
            CARW(cur_car(), CAR_VALUE) -= (u16)v;
            CARW(car, s) = 0;
        }
        car = cur_car();
        memset(mp(DGROUP, (u16)(car + 0x16)), 0, 9);                /* bay bolts */
        CARB(car, 0x1F) = 0;                                       /* engine link */
    }
    input_reset();
    fx_clank();
    message_at((s16)-msg, -1, 0x49);
}

/* 0000:c5ac trans_blown */
static void trans_blown(s16 msg)
{
    u16 car = cur_car();
    if (car != 0) {
        s16 v = part_release(CARW(car, CAR_TRANS), 0);
        car = cur_car();
        CARW(car, CAR_VALUE) -= (u16)v;
        CARW(car, CAR_TRANS) = 0;
        CARW(car, 0x14) = 0;                                       /* transmission bolts */
    }
    input_reset();
    fx_clank();
    message_at((s16)-msg, -1, 0x49);
}

/* 0000:c613 race_results: 1 = drive home with the win music, 2 = drive home, 3 = stay at Bob's,
 * 4 = jail (or the button code of the repair menu) */
u16 race_results(void)
{
    s16 bet = DSS(DS_bet_kind), amount = DSS(DS_bet_amount);
    u8 eng = DSB(DS_player_result) & 4, trn = DSB(DS_player_result) & 0x10;
    u16 res = DSW(DS_player_result);
    bool breakdown = res == 5 || res == 0x11 || res == 0x15;
    u16 car = cur_car();
    static const u16 slots[3] = { CAR_TRANS, CAR_ENGINE, CAR_TYRES };
    for (int i = 0; i < 3; i++) {
        u16 pw = (u16)(CARW(car, slots[i]) + PART_WEAR);
        if (DSS(pw) > 0x26AC) DSW(pw) = 0x26AC;
    }
    DSW(DS_player_won) = 0;
    DSW(DS_stats_done) = 0;
    DSL(DS_distance) = 0;
    DSB(DS_click_edge) = 0;
    DSB(DS_click_held) = 0;
    if (PHW(PLAYER, PH_AUTOMATIC) != 0) PHW(PLAYER, PH_AUTO_MODE) = 1;
    if (DSB(DS_tires_blown) != 0) DSB(DS_player_result) |= 8;
    ui_push(1);
    s16 used = DSW(DS_race_type) == 0 ? 5 : 0xF;                   /* 0.5 / 1.5 gal */
    if (CARS(car, CAR_GAS) > used) CARW(car, CAR_GAS) -= (u16)used;
    else CARW(car, CAR_GAS) = 0;
    drv_pal_normal();                                              /* (*DS:78C2)() */

    if (DSB(DS_opp_result) & 1) {                                  /* ---- player won */
        DSW(DS_player_won) = 1;
        race_msg();
        cursor_ctl(-2);
        s16 id;
        if (DSW(DS_vs_king) == 0) id = DSS((u16)(0x53C8 + bet * 2));
        else id = DSW(DS_race_type) == 0 ? 0x14BD : 0x14D6;
        msg_box(id);
        money_add(amount);
        if (bet == 5) {                                            /* pink slips: his car */
            u16 n = car_new(DSS(DS_opp_model), DSS(DSW(DS_opp_model_ptr)), 0);
            PARTS(CARW(n, CAR_TYRES), PART_WEAR) = rnd(0x1F4);
            u16 orec = DSW(DS_opp_rec);
            u16 f = CARW(n, CAR_FLAGS);
            CARW(n, CAR_FLAGS) = (u16)((f & 0xE0FF) | ((DSB((u16)(orec + OR_STICKER)) & 0x1F) << 8));
            f = CARW(n, CAR_FLAGS);
            CARW(n, CAR_FLAGS) = (u16)((f & 0xDFFF) | ((DSB((u16)(orec + OR_CUSTOM)) & 4) ? 0x2000 : 0));
            f = CARW(n, CAR_FLAGS);
            CARW(n, CAR_FLAGS) = (u16)((f & 0xBFFF) | ((DSB((u16)(orec + OR_CUSTOM)) & 2) ? 0x4000 : 0));
            f = CARW(n, CAR_FLAGS);
            CARW(n, CAR_FLAGS) = (u16)((f & 0x7FFF) | ((DSB((u16)(orec + OR_CUSTOM)) & 1) ? 0x8000 : 0));
            CARB(n, CAR_COLOUR) = DSB((u16)(orec + OR_COLOUR));
            u16 cc = cur_car();
            if (CARB(n, CAR_CLASS) > CARB(cc, CAR_CLASS)) {        /* better class: drive it */
                CARW(cc, CAR_NEXT) = CARW(DSW(DS_car2), CAR_NEXT);
                DSW(DS_car2) = cc;
                DSW(DS_car) = n;
                car_setup(cur_car(), PLAYER);
            }
            if (DSW(DS_demo_active) == 0 || DSW(DS_opp_index) != DSW(0x4FF4)) {
                bool gone = 1;
                if (DSB((u16)(DSW(DS_opp_rec) + OR_REROLL)) == 0 && DSW(DS_vs_king) == 0) {
                    s16 list[0x1A];                                /* bp-72h: [0] = count */
                    s16 cnt = 0;
                    u8 mc = (u8)DSB((u16)(DSW(DS_opp_model_ptr) + 2));
                    for (s16 m = 0; m < 0x19; m++)
                        if ((u8)MODEL_CLASS(m) < mc) list[++cnt] = m;
                    if (cnt > 0) {                                  /* he buys a cheaper car */
                        list[0] = cnt;
                        opponent_init(DSS(DS_opp_index), list);
                        DSB((u16)(DSW(DS_opp_rec) + OR_REROLL)) = 1;
                        gone = 0;
                    }
                }
                if (gone) DSB((u16)(DSW(DS_opp_rec) + OR_STATUS)) = 0xFF;   /* he is gone */
            }
        }
        ui_pop(1);
        if ((DSB(DS_player_result) & 8) && tires_replace() != 0) return 2;
        return bet == 5 ? 1 : 3;
    }
    if (DSB(DS_player_result) & 0x40) {                            /* ---- police */
        siren_set(0);
        ticket_show();
        s16 fine = (DSB(DS_player_result) & 8) ? tires_replace() : 0;
        if (PHS(PLAYER, PH_SPEED) > 0x50) fine = (s16)(fine + 0x4B);
        else if (PHS(PLAYER, PH_SPEED) > 0x32) fine = (s16)(fine + 0x32);
        else fine = (s16)(fine + 0x14);
        ui_push(0x18);
        ui_wait(0);
        char buf[0x30];
        snprintf(buf, sizeof buf, ds_str(0x547B), (int)fine);
        status_print(-1, buf, (s16)(0x140 - DSS(0x8250)));
        wait_ticks_or_input(0xFA);
        if ((s32)fine > DSSL(DS_money)) {                           /* sentenced for debts */
            msg_box(0x1793);
            ui_pop(1);
            ui_pop(1);
            money_add((s16)-DSS(DS_money));                        /* -(low word) */
            status_print(-1, ds_str(0x54A1), (s16)(0x140 - DSS(0x8250)));
            return 4;
        }
        money_add((s16)-fine);
        u32 deadline = DSL(DS_wait_deadline);
        wait_ticks_or_input(DSS(DS_msg_ticks));
        DSL(DS_wait_deadline) = deadline;
        ui_pop(1);
        status_print(-1, ds_str(0x54A3), (s16)(0x140 - DSS(0x8250)));
        ui_pop(1);
        return 3;
    }
    if (DSW(DS_player_result) == 0x81) {                           /* jumped the light */
        cursor_ctl(-2);
        msg_box((s16)0xECB2);                                      /* "Ya jumped the light, speedy!" */
        ui_pop(1);
        return 3;
    }
    if (bet == 5) {                                                /* pink slip lost */
        car_free(cur_car(), 0);
        DSW(DS_car) = 0;
    }
    money_add((s16)-amount);
    if (DSW(DS_player_result) == 1) {                              /* ---- lost, car intact */
        race_msg();
        cursor_ctl(-2);
        s16 id = bet == 5 ? 0x143A : 0x1427;
        if (DSW(DS_vs_king) != 0) id = DSW(DS_race_type) == 0 ? 0x1450 : 0x146C;
        msg_box(id);
        ui_pop(1);
        return bet != 5 ? 3 : 2;
    }
    if (breakdown) {                                               /* ---- breakdown */
        cursor_ctl(-2);
        if (eng) engine_blown(bet == 5 ? 0x16BD : 0x1634);
        else if (trn) trans_blown(bet == 5 ? 0x1678 : 0x1608);
        ui_pop(1);
        return 2;
    }
    crash_show();                                                  /* ---- crash */
    if (bet == 5) {                                                /* the car is his already */
        if (DSB(DS_player_result) & 8) { msg_box(0x16F6); tires_replace(); }
        else msg_box(0x1658);
        ui_pop(1);
        return 2;
    }
    if (PHS(PLAYER, PH_SPEED) > 0x5A) {                            /* totaled */
        s16 got = (s16)(car_free(cur_car(), 0) / 10 + 0x13);
        DSW(DS_car) = 0;
        money_add(got);
        char *p = ds_str(0x4115);
        while (*p++ != '$') {}
        patch_amount(p, got);
        input_reset();
        msg_box(0x1513);
        ui_pop(1);
        return 2;
    }
    s16 fast;
    if (PHS(PLAYER, PH_SPEED) > 0x3C) fast = 1;
    else if (PHS(PLAYER, PH_SPEED) > 0x1E) fast = 0;
    else {                                                         /* lucky */
        if (DSB(DS_player_result) & 8) {
            msg_box(0x1590);
            tires_replace();
            ui_pop(1);
            return 2;
        }
        msg_box(0x1561);
        ui_pop(1);
        return 1;
    }
    s16 slow = !fast;                                              /* [bp-3Eh] */
    s16 cost = (s16)(rnd(fast ? 0x28 : 0x14) + 0x64);
    char *p = strchr(ds_str(slow ? 0x2B09 : 0x2AB2), '$') + 1;
    patch_amount(p, cost);
    s16 tyres = DSB(DS_player_result) & 8;
    if (tyres) memcpy(mp(DGROUP, 0x2AEA), mp(DGROUP, 0x54A4), 0xD);   /* "Plus tires !" */
    else DSB(0x2AEA) = 0;
    s16 menu = slow ? 0x1A : 0x19;
    u16 mrec = DSW((u16)(0x579F + menu * 2));
    if ((s32)(u16)cost > DSSL(DS_money)) {                          /* "Fix it" greyed */
        DSW(0x1B40) = DSW(0x1AC2) = 0xE;
        DSB((u16)(mrec + 1)) = 0xD0; DSB((u16)(mrec + 2)) = 1; DSB((u16)(mrec + 3)) = 0xD0;
    } else {
        DSW(0x1B40) = DSW(0x1AC2) = 0xA;
        DSB((u16)(mrec + 1)) = 0xCF; DSB((u16)(mrec + 2)) = 0x21; DSB((u16)(mrec + 3)) = 0xCF;
    }
    ui_push(menu);
    input_reset();
    s16 b;
    do b = ui_wait(0xBB8); while (b == 0);
    ui_pop(1);
    if (b == 1) {                                                  /* "Fix it" */
        money_add((s16)-cost);
        if (tyres) tires_replace();
        ui_pop(1);
        return 1;
    }
    if (b != 2) return (u16)b;
    money_add((s16)ftol((long double)CARS(cur_car(), CAR_VALUE) * dconst(0x6964)));   /* "Junk it" */
    DSW(DS_car) = 0;
    ui_pop(1);
    return 2;
}
