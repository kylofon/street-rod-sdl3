/* The jail screen and Gus's gas station (SR.EXE segment 0000, b045-bed4). Specs: port/spec/race.md
 * §4.15 and port/spec/garage.md §4.20 (both "likely"; this port follows the disassembly).
 *
 * Conventions (srport/PORTING.md): all game state stays in mem[] at its original DGROUP address; near
 * pointers are u16 DS offsets. Stack variables of the original are C locals. */
#include "game/race_int.h"
#include "game/race.h"
#include "game/garage.h"
#include "game/race_xref.h"
#include "game/ui.h"
#include "game/flow.h"
#include "platform/platform.h"
#include "platform/video.h"
#include "sound/sound.h"

#define BOB_REC          0x20AA  /* UI records 0x143..0x147 of screen 0x25 (shared with Bob's) */
#define BOB_NAME_BUF     0x7F32
#define BOB_NAME_X       0x51EE
#define BOB_NAME_W       0x8BC0
#define STATUS_MONEY     0x49DC  /* near char*: label of the money status line */
#define PUMP_X           0x826E  /* i16: pump nozzle position (garage car_draw sets it) */
#define PUMP_Y           0x8270
#define PIC_CAP_ON       0x028C  /* far Desc*: resident picture of the closed tank cap */
#define PIC_CAP_OFF      0x0290  /* far Desc*: the open filler */
#define GAS_RAG_OK       0x6CA6  /* i16: hot spot 1 (windshield rag) enabled */
#define GAS_CAP_OK       0x6CA8  /* i16: hot spot 2 (tank cap) enabled */
#define GAS_NOZZLE_OK    0x6CA4  /* i16: hot spot 4 (nozzle) enabled */
#define GAS_SPOT_CODES   0x53B6  /* i16[6]: codes of the gas-station hot spots */
#define GAS_REC_FIRST    0x19EA  /* first gas-station UI record (DS offset) */
#define GAS_REC_LAST     0x1A44

static void set_deadline(u16 n) { DSL(DS_wait_deadline) = ticks_now() + n; }

static s16 wait_event(void)
{
    s16 b;
    do b = ui_wait(3000); while (b == 0);
    return b;
}

/* ================================================================================== jail */

/* 0000:b045: label of record 0x143 and the type of the four hot spots after it; the hot key byte of
 * screen 0x25 (its per-screen remap entry, DS:57E9 = &DS:579F[0x25]). */
static void jail_hotspots(s16 type, u8 key)
{
    if (type == 0x0E) DSW(BOB_REC + UI_R_LABEL) = 0x28EC - 0x239E;
    else DSW(BOB_REC + UI_R_LABEL) = 0x28F9 - 0x239E;
    DSW(BOB_REC + 0x12) = (u16)type;
    DSW(BOB_REC + 0x24) = (u16)type;
    DSW(BOB_REC + 0x36) = (u16)type;
    DSW(BOB_REC + 0x48) = (u16)type;
    DSB(DSW(0x57E9)) = key;
}

/* 0000:b08c: the jail screen (Bob's picture behind bars); opponents and the dancer may show up;
 * button 1 leaves. */
void jail(void)
{
    s16 mode = 4;      /* char in the original */
    s16 quick = 0;     /* never set: the original's copy of Bob's loop keeps it */
    s16 pct;

    DSW(DS_g_mirror) = 0;
    cursor_ctl(-3);
    arena_reset();
    ds_far_wr(0x82B8, pic_get(0x4E7, 1));
    ds_far_wr(0x6E64, pic_get(0x4E9, 1));
    show_picture(0x445, -1);
    pal_reg12_13();
    FarPtr pic = pic_get(0x4E8, 2);
    Rect r = { (s16)desc_w(pic), 5, 0, 0, 0, 0 };
    drv_blit(pic, g_back(), &r, 1);
    r.h = (s16)(desc_h(pic) - 5);
    r.sy = 5;
    r.dy = (s16)(0xBE - r.h);
    drv_blit(pic, g_back(), &r, 1);
    for (u16 e = 0x5338; e < 0x5338 + 7 * 0x0E; e += 0x0E) {    /* {pic*, sx, sy, w, h, dx, dy} */
        r.w  = DSS(e + 6);
        r.h  = DSS(e + 8);
        r.sx = DSS(e + 2);
        r.sy = DSS(e + 4);
        r.dx = DSS(e + 0x0A);
        r.dy = DSS(e + 0x0C);
        drv_blit(ds_far(DSW(e)), g_back(), &r, 1);
    }
    status_print(-1, ds_str(0x53B2), 0);
    status_label(ds_str(DSW(STATUS_MONEY)));
    drv_copy_page(desc_planes(g_back()), desc_planes(g_front()));
    DSW(DS_g_mirror) = 1;
    drv_pal_normal();
    DSW(DS_opp_present) = 0;
    DSW(DS_waitress_state) = 0xFFFF;
    DSW(DS_drink_ordered) = 0;
    cursor_ctl(1);
    cursor_ctl(-1);
    DSW(DS_drive_dest) = 6;
    jail_hotspots(0x0E, 0x10);

    for (;;) {
        ui_push(0x25);
        if (!quick) set_deadline(mode == 4 ? 0x8C : 0x118);
        else set_deadline(5);
        quick = 0;
        pct = mode == 4 ? 0 : king_chance();
        s16 b = wait_event();
        if (b == 1) {
            ui_pop(1);
            jail_hotspots(0x0B, 0x22);
            DSW(DS_drive_dest) = 1;
            return;
        }
        if (b == 1000) {
            if (rnd(100) < 6) {
                cursor_ctl(-4);
                status_print(-1, ds_str(0x53B3), 0);
                dancing_girl();
                cursor_ctl(-2);
            }
            opponent_select(pick_opponent(pct, 1), mode, 1);
            mode = 1;
            DSB(BOB_NAME_BUF) = 0;
            status_print(DSS(BOB_NAME_X), ds_str(0x53B4), DSS(BOB_NAME_W));
        }
        ui_pop(0);
    }
}

/* ============================================================================ gas station */

/* The gas station's state block (a stack structure of 0000:b8a1 at bp-1Ch, passed to b61b/b717). */
typedef struct {
    s16    model;        /* +00 */
    s16    flags;        /* +02 car_flags */
    FarPtr bg_cap_on;    /* +04 screen under the nozzle with the cap on */
    FarPtr bg_cap_off;   /* +08 same with the filler open */
    s16    shape;        /* +0C pointer shape: 1 hand, 2 holding the cap, 3 holding the rag */
    s16    units;        /* +0E 3-gallon steps that still fit */
    s16    self_serve;   /* +10 0 once the attendant took over ("full service") */
    s16    hose;         /* +12 nozzle in the car */
    s16    cap_on;       /* +14 */
    s16    full_service; /* +16 */
    s16    fine;         /* +18 $10 fine pending for leaving the hose / cap */
    s16    pumping;      /* +1A */
} GasState;

/* 0000:b3ca: index of the gas-station UI record with hot-spot code `code`. */
static s16 gas_hotspot_find(s16 code)
{
    s16 first = (s16)((GAS_REC_FIRST - UI_REC_BASE) / UI_REC_SIZE);
    s16 n = (s16)((GAS_REC_LAST - GAS_REC_FIRST) / UI_REC_SIZE + 1);
    s16 ax = n;
    for (s16 i = 0; i < n; i++) {
        ax = (s16)((first + i) * UI_REC_SIZE);
        if (DSS((u16)(UI_REC_BASE + 6 + (u16)ax)) == code) return (s16)(first + i);
    }
    return ax;   /* sic: not found = AX of the last compare (never happens) */
}

/* 0000:b425 */
static void gas_hotspots_apply(void)
{
    s16 first = (s16)((GAS_REC_FIRST - UI_REC_BASE) / UI_REC_SIZE);
    s16 n = (s16)((GAS_REC_LAST - GAS_REC_FIRST) / UI_REC_SIZE + 1);
    ui_sort_records(first, n);
}

/* 0000:b456 */
static void gas_hotspots_clear(void)
{
    DSW(GAS_RAG_OK) = 0;
    DSW(GAS_CAP_OK) = 0;
    DSW(GAS_NOZZLE_OK) = 0;
    for (u16 i = 0; i < 6; i++)
        DSW((u16)(UI_REC_BASE + gas_hotspot_find(DSS((u16)(GAS_SPOT_CODES + 2 * i))) * UI_REC_SIZE)) = 0x0E;
}

/* 0000:b49c */
static void gas_hotspot_set(s16 code, s16 x, s16 y, s16 w, s16 h)
{
    u16 rec = (u16)(gas_hotspot_find(code) * UI_REC_SIZE + UI_REC_BASE);
    DSW(rec + UI_R_X0) = (u16)x;
    DSW(rec + UI_R_X1) = (u16)(x + w - 1);
    DSW(rec + UI_R_Y0) = (u16)y;
    DSW(rec + UI_R_Y1) = (u16)(y + h - 1);
    DSW(rec + UI_R_TYPE) = 0x0B;
}

/* 0000:b4de: cap and rag */
static void gas_hotspots_a(void)
{
    gas_hotspots_clear();
    gas_hotspot_set(2, 0x3F, 0x64, 0x0C, 0x17);
    gas_hotspot_set(1, 0x54, 0xA8, 0x1F, 0x0F);
    gas_hotspots_apply();
    DSW(GAS_RAG_OK) = 1;
    DSW(GAS_CAP_OK) = 1;
}

/* 0000:b525: the nozzle while pumping */
static void gas_hotspots_b(void)
{
    gas_hotspots_clear();
    gas_hotspot_set(4, (s16)(DSS(PUMP_X) - 0x0A), (s16)(DSS(PUMP_Y) - 5), 0x12, 0x0C);
    gas_hotspots_apply();
    DSW(GAS_NOZZLE_OK) = 1;
}

/* 0000:b552: cap (filler open) and the nozzle on the pump */
static void gas_hotspots_c(void)
{
    gas_hotspots_clear();
    gas_hotspot_set(2, 0x34, 0x64, 0x10, 0x17);
    gas_hotspot_set(4, (s16)(DSS(PUMP_X) - 1), (s16)(DSS(PUMP_Y) - 1), 0x0A, 9);
    gas_hotspots_apply();
    DSW(GAS_CAP_OK) = 1;
    DSW(GAS_NOZZLE_OK) = 1;
}

/* 0000:b59b: the windshields while holding the rag */
static void gas_hotspots_d(void)
{
    gas_hotspots_clear();
    if (DSW(0x8264)) gas_hotspot_set(6, DSS(0x826A), DSS(0x826C), 0x14, 0x13);
    gas_hotspot_set(5, DSS(0x8266), DSS(0x8268), 0x16, 0x13);
    gas_hotspot_set(1, 0x3E, 0xA5, 0x35, 0x12);
    gas_hotspot_set(3, 0xC4, 0x5A, 0x10, 0x0E);
    gas_hotspots_apply();
    DSW(GAS_RAG_OK) = 1;
}

/* 0000:b617 */
static void gas_hotspots_none(void) { gas_hotspots_clear(); }

/* 0000:b61b: the nozzle picture `pic_id` over the saved background at the pump. */
static void gas_draw_pump(GasState *st, s16 pic_id)
{
    FarPtr bg = st->cap_on ? st->bg_cap_on : st->bg_cap_off;
    cursor_ctl(-3);
    FarPtr p = pic_get(pic_id, 2);
    cursor_ctl(-1);
    FarPtr tmp = arena_bitmap_alloc((s16)desc_w(bg), (s16)desc_h(bg), 0, 2);
    bitmap_copy_into(bg, tmp, 1);
    Rect r;
    r.w  = (s16)desc_w(p);
    r.h  = (s16)desc_h(p);
    r.sx = 0;
    r.sy = 0;
    r.dx = (s16)((~DSW(DS_g_x_align) & DSW(PUMP_X)) - r.w + 0x18);
    r.dy = (s16)(0x0C - desc_h(p));
    bitmap_blit_masked_shift(p, tmp, &r, 0);
    screen_put_bitmap_mirror(tmp, (s16)(DSS(PUMP_X) - 0x10), (s16)(DSS(PUMP_Y) - 5));
    arena_pop(2);
}

static s16 fine10(void) { return DSSL(DS_money) >= 10 ? -10 : (s16)-(s16)DSW(DS_money); }

static u16 cur_tyre_list(void)
{
    return (u16)(DS_TYRE_PICS + (part_grade(DSW((u16)(cur_car() + CAR_TYRES))) << 3));
}

/* 0000:b717: fines, the attendant, clamp the tank to 17.0 gal, drive home. */
static void gas_leave(GasState *st)
{
    if (st->shape != 1) cursor_ctl(1);
    if (st->hose) {
        gas_draw_pump(st, 0x472);
        if (st->fine) {
            money_add(fine10());
            st->fine = 0;
        }
    }
    if (st->cap_on == 0) {
        screen_put_bitmap_mirror(ds_far(PIC_CAP_ON), 0x40, 0x64);
        if (st->fine) money_add(fine10());
    }
    if (st->self_serve == 0) {
        cursor_ctl(-3);
        FarPtr p = pic_get(0x4D1, 2);
        cursor_ctl(-1);
        walker_anim(p, 0x60, 0, 0x30, 0x40, 0xC8, 0x51, 0xA9, 2);
    }
    u16 car = cur_car();
    s16 gas = CARS(car, CAR_GAS);
    if (gas > 0xAA) gas = 0xAA;
    CARW(car, CAR_GAS) = (u16)gas;
    car_draw(2, st->model, st->flags, (s16)((CARW(cur_car(), CAR_FLAGS) >> 8) & 0x1F), 0, 0xA9, 1,
             cur_tyre_list());
    ui_pop(1);
    arena_reset();
    DSW(DS_drive_dest) = 1;
    drive_to();
}

static bool price_over_money(s16 price) { return (s32)price > DSSL(DS_money); }

/* 0000:b8a1: Gus's gas station. */
void gas_station(void)
{
    GasState st;
    s16 price, paid, wait;

    status_print(-1, ds_str(0x53C2), 0);
    DSW(DS_drive_dest) = 3;
    show_picture(0x442, -1);
    status_label(ds_str(DSW(STATUS_MONEY)));
    DSW(DS_g_mirror) = 1;
    drv_pal_normal();
    st.shape = 1;
    cursor_ctl(1);
    st.model = CARS(cur_car(), CAR_MODEL);
    st.flags = (s16)car_flags(cur_car());
    car_draw(4, st.model, st.flags, (s16)((CARW(cur_car(), CAR_FLAGS) >> 8) & 0x1F), 0, 0xA9, 1,
             cur_tyre_list());
    music_mute(0);
    FarPtr save_rag = screen_save_rect(0x50, 0xAA, 0x28, 0x0C, far_make(0, 0));
    st.bg_cap_on = screen_save_rect((s16)(DSS(PUMP_X) - 0x10), (s16)(DSS(PUMP_Y) - 5), 0x18, 0x0C, far_make(0, 0));
    DSW(DS_g_mirror) = 0;
    screen_put_bitmap_mirror(ds_far(PIC_CAP_OFF), 0x40, 0x64);
    st.bg_cap_off = screen_save_rect((s16)(DSS(PUMP_X) - 0x10), (s16)(DSS(PUMP_Y) - 5), 0x18, 0x0C, far_make(0, 0));
    screen_put_bitmap_mirror(ds_far(PIC_CAP_ON), 0x40, 0x64);
    DSW(DS_g_mirror) = 1;
    price = 1;
    st.pumping = 0;
    wait = 0x28;
    paid = 0;
    st.hose = 0;
    st.cap_on = 1;
    st.self_serve = 1;
    st.full_service = 0;
    st.fine = 1;
    st.units = (s16)((s16)(0xBE - CARS(cur_car(), CAR_GAS)) / 0x1E);
    gas_draw_pump(&st, 0x472);
    gas_hotspots_a();
    ui_push(0x17);
    cursor_ctl(-2);

short_deadline:
    set_deadline(0x14);
wait_next:
    for (;;) {
        s16 b = wait_event();
        switch (b) {
        case 3:                                             /* the attendant */
            msg_box(0x10F6);
            continue;

        case 1000:                                          /* time-out */
            if (st.shape == 3) goto short_deadline;
            if (st.pumping) {
                st.units--;
                CARW(cur_car(), CAR_GAS) += 0x1E;
                paid = 1;
                money_add((s16)-(price_over_money(price) ? (s16)DSW(DS_money) : price));
                if (st.units != 0 && !price_over_money(price)) {
                    set_deadline(0x4C);
                    continue;
                }
                if (anim_active(2)) {
                    loop_tick(0);
                    anim_stop(2);
                }
                if (st.self_serve) goto stop_pumping;
                goto leave;
            }
            if (--wait > 0) goto short_deadline;
            ui_pop(1);                                      /* "Yo, you need full service, dummy!" */
            gas_hotspots_none();
            ui_push(0x17);
            price = (s16)(price << 1);
            st.full_service = 1;
            sfx_play_wait(3);
            msg_box(0x19E1);
            st.self_serve = 0;
            if (paid) goto leave;
            st.fine = 0;
            if (st.shape == 1) {
                screen_put_bitmap_mirror(ds_far(PIC_CAP_OFF), 0x40, 0x64);
                st.shape = 2;
                cursor_ctl(2);
                st.cap_on = 0;
            }
            gas_draw_pump(&st, 0x471);
            st.hose = 1;
            st.shape = 1;
            cursor_ctl(1);
            anim_start(2);
            loop_tick(1);
            st.pumping = 1;
            set_deadline(0x4C);
            continue;

        case 1:                                             /* the rag */
            if (!DSW(GAS_RAG_OK)) continue;
            if (st.shape == 1) {
                st.shape = 3;
                cursor_ctl(3);
                show_picture_at(0x473, 0x50, 0xAA, -1);
                ui_pop(1);
                gas_hotspots_d();
            } else {
                st.shape = 1;
                cursor_ctl(1);
                screen_put_bitmap_mirror(save_rag, 0x50, 0xAA);
                ui_pop(1);
                gas_hotspots_a();
            }
            ui_push(0x17);
            goto short_deadline;

        case 2:                                             /* the tank cap */
            if (!DSW(GAS_CAP_OK)) continue;
            if (st.shape == 1) {
                st.shape = 2;
                cursor_ctl(2);
                screen_put_bitmap_mirror(ds_far(PIC_CAP_OFF), 0x40, 0x64);
                st.cap_on = 0;
                goto spots_c;
            }
            if (price_over_money(price) || paid) {
                st.fine = 0;
                goto leave;
            }
            st.shape = 1;
            cursor_ctl(1);
            screen_put_bitmap_mirror(ds_far(PIC_CAP_ON), 0x40, 0x64);
            st.cap_on = 1;
            ui_pop(1);
            gas_hotspots_a();
            goto push_done;

        case 4:                                             /* the nozzle */
            if (!DSW(GAS_NOZZLE_OK)) continue;
            if (st.shape == 1) {                            /* back on the pump */
                if (anim_active(2)) {
                    loop_tick(0);
                    anim_stop(2);
                }
                gas_draw_pump(&st, 0x472);
                st.hose = 0;
                st.shape = 2;
                cursor_ctl(2);
                if (st.units > 0 && !price_over_money(price)) msg_box(0x18DD);   /* "It's not full, speedy!" */
                goto spots_c;
            }
            if (st.units == 0 || price_over_money(price)) {
                msg_box(0x10B5);                            /* "Pay attention, dummy!" */
                continue;
            }
            gas_draw_pump(&st, 0x471);
            st.hose = 1;
            st.shape = 1;
            cursor_ctl(1);
            anim_start(2);
            loop_tick(1);
            ui_pop(1);
            gas_hotspots_b();
            ui_push(0x17);
            set_deadline(0x4C);
            st.pumping = 1;
            continue;

        default:
            continue;
        }
    spots_c:
        ui_pop(1);
        gas_hotspots_c();
    push_done:
        ui_push(0x17);
    stop_pumping:
        set_deadline(0x14);
        st.pumping = 0;
        goto wait_next;
    }
leave:
    gas_leave(&st);
}
