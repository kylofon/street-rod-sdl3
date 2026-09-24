/* Bob's Drive-In: opponents, the challenge, the car-hop and the juke-box dancer (SR.EXE segment 0000,
 * 914c-a544 and ab8b). Spec: port/spec/race.md §4.14 (verified against the disassembly; where the
 * spec and the code differ, the code is followed and the difference noted).
 *
 * Conventions (srport/PORTING.md): all game state stays in mem[] at its original DGROUP address; near
 * pointers are u16 DS offsets. Stack buffers of the original are C locals. */
#include <string.h>

#include "game/race_int.h"
#include "game/race.h"
#include "game/garage.h"
#include "game/race_xref.h"
#include "game/ui.h"
#include "game/flow.h"
#include "platform/platform.h"
#include "platform/video.h"
#include "sound/sound.h"

/* DGROUP globals of this file without a name in symbols.h */
#define BOB_OPP_LEAVES   0x5A24  /* i16: the opponent drives off at once (jail / King not interested) */
#define BOB_CARHOP_SEEN  0x51F0  /* i16: opponent_show calls since the car-hop left */
#define BOB_NAME_BUF     0x7F32  /* char[]: "name + car" status line text */
#define BOB_NAME_X       0x51EE  /* i16: x of that text */
#define BOB_NAME_W       0x8BC0  /* i16: its width in pixels */
#define BOB_STATUS_X0    0x82C0  /* i16: left edge of the status text area */
#define BOB_DEMO_OPP     0x4FF4  /* i16: opponent the demo always meets */
#define BOB_FIRST_PICK   0x6CA2  /* i16: first pick at Bob's this visit (demo) */
#define BOB_CHALLENGED   0x8256  /* i16 */
#define BOB_MOUSE_X      DS_cursor_x
#define BOB_MOUSE_Y      DS_cursor_y
#define BOB_STATUS_MONEY 0x49DC  /* near char*: label of the money status line */
#define BOB_7B10         0x7B10
#define BOB_TYRE_SET     0x827E  /* i16: opponent tyre grade (wheel pictures DS:5030 + 8*n) */
#define BOB_OPP_PIC      0x827A
#define BOB_OPP_COLOUR   0x827C
#define BOB_OPP_FACE     0x8280
#define BOB_REC          0x20AA  /* UI record 0x143 of screen 0x25 (Bob's hot spots) */
#define MSG_CHICKEN      0x122C
#define MSG_NO_GAS       0x1761

static void set_deadline(u16 n) { DSL(DS_wait_deadline) = ticks_now() + n; }

static s16 wait_event(void)
{
    s16 b;
    do b = ui_wait(3000); while (b == 0);
    return b;
}

static u16 tyre_list(void) { return (u16)(DS_TYRE_PICS + (DSW(BOB_TYRE_SET) << 3)); }

/* 0000:914c: the car-hop appears (mode 0, pct_in %) and may leave again at once (pct_out %), or
 * leaves (mode 1, pct_in %). Returns 1 if she moved. */
static s16 waitress_toggle(s16 mode, s16 arg, s16 pct_in, s16 pct_out)
{
    s16 moved = 0;
    if (mode == 0) {
        if (DSS(DS_waitress_state) == 0 && DSS(BOB_CARHOP_SEEN) >= 3 && rnd(100) < pct_in) {
            moved = 1;
            cursor_ctl(-4);
            carhop_anim(0, arg);
            DSW(DS_waitress_state) = 1;
            if (rnd(100) < pct_out) {
                wait_vretraces(100);
                carhop_anim(1, arg);
                DSW(DS_waitress_state) = 0;
                DSW(DS_drink_ordered) = 0;
            }
        }
    } else if (mode == 1) {
        if (DSS(DS_waitress_state) == 1 && rnd(100) < pct_in) {
            moved = 1;
            cursor_ctl(-4);
            carhop_anim(1, arg);
            DSW(DS_waitress_state) = 0;
            DSW(DS_drink_ordered) = 0;
            DSW(BOB_CARHOP_SEEN) = 0;
        }
    }
    if (moved) cursor_ctl(-2);
    return moved;
}

/* 0000:9256: cut the DGROUP string at its last blank. */
static void str_drop_last_word(u16 s)
{
    s16 i = (s16)(strlen(ds_str(s)) - 1);
    while (i >= 0) {
        if (DSB((u16)(s + i)) == ' ') break;
        DSB((u16)(s + i)) = 0;
        i = (s16)(strlen(ds_str(s)) - 1);
    }
    if (i >= 0) DSB((u16)(s + i)) = 0;
}

/* strcat into a DGROUP buffer (a plain loop: GCC's -Wrestrict cannot see that the mem[] strings
 * do not overlap) */
static void ds_append(char *d, const char *s)
{
    d += strlen(d);
    while ((*d++ = *s++) != 0) {}
}

static s16 carhop_chance(s16 jail_screen)
{
    return (s16)(10 * jail_screen + 0x4B * DSS(DS_drink_ordered) + (DSS(DS_demo_active) << 3) + 5);
}

/* 0000:92a4: status line "driver + car", the opponent's car (car_draw mode 1 drive in, 2 drive off,
 * 4 first arrival) and the car-hop. */
static void opponent_show(s16 mode, s16 model, s16 driver, s16 y, s16 smoke, u16 tyres)
{
    s16 leaves = DSS(BOB_OPP_LEAVES);
    u16 rec = DSW(DS_opp_rec);
    char *buf = ds_str(BOB_NAME_BUF);
    strcpy(buf, ds_str((u16)(DSW((u16)(rec + OR_NAME)) + 0x2C02)));
    ds_append(buf, ds_str(0x51F2));
    ds_append(buf, ds_str((u16)(DSW((u16)(DSW(DS_opp_model_ptr) + 6)) + 0x2C02)));

    s16 jail_screen = DSS(DS_drive_dest) == 6;
    if (mode == 4) DSW(DS_waitress_state) = 0xFFFF;
    DSW(DS_opp_present) = mode == 2;
    if (mode == 1) {
        if (!waitress_toggle(0, DSS(DS_opp_present), carhop_chance(jail_screen), 0))
            waitress_toggle(1, DSS(DS_opp_present), 0x14, 0);
    }

    s16 last = (s16)(strlen(buf) - 1);
    if (DSB((u16)(BOB_NAME_BUF + last)) == '.') DSB((u16)(BOB_NAME_BUF + last)) = 0;
    for (;;) {
        DSW(BOB_NAME_W) = (u16)font_string_width(buf);
        s16 v = (s16)-(s16)(DSS(BOB_STATUS_X0) + DSS(BOB_NAME_W) - 0x140);
        s16 x = v > 0 ? (s16)(v >> 1) : 0;
        if (x < 15) x = 15;
        x = (s16)((x + DSS(BOB_STATUS_X0)) & 0xFFF8);
        DSW(BOB_NAME_X) = (u16)x;
        if ((s16)(x + DSS(BOB_NAME_W)) <= 0x140) break;
        str_drop_last_word(BOB_NAME_BUF);
    }
    status_print(-1, ds_str(0x51F5), 0);
    status_print(DSS(BOB_NAME_X), buf, DSS(BOB_NAME_W));
    car_draw(mode, model, DSC((u16)(rec + OR_CUSTOM)), DSC((u16)(rec + OR_STICKER)), driver, y, smoke, tyres);

    if (leaves) mode = 2;
    if (mode == 2) DSW(BOB_CARHOP_SEEN)++;
    if (mode == 2) {
        buf[0] = 0;
        status_print(DSS(BOB_NAME_X), ds_str(0x51F6), DSS(BOB_NAME_W));
    }
    if (mode == 4) DSW(DS_waitress_state) = 0;
    DSW(DS_opp_present) = mode != 2;
    if (!jail_screen || !DSW(DS_opp_present)) {
        if (!waitress_toggle(0, DSS(DS_opp_present), carhop_chance(jail_screen),
                             DSW(DS_opp_present) == 0 ? 8 : 0))
            waitress_toggle(1, DSS(DS_opp_present), 0x28, 0);
    }
}

/* 0000:9524: model class byte < 3 -> 0, < 7 -> 1, else 2. */
s16 model_class3(s16 model)
{
    s16 c = DSB(MODEL(model) + 2);
    if (c < 3) return 0;
    if (c < 7) return 1;
    return 2;
}

/* 0000:9556: 0 = the King accepts, 1..5 = why not. */
s16 king_status(void)
{
    s16 any = 0;
    for (s16 i = 0; i < 0x15; i++)
        if (DSB((u16)(OREC(i) + OR_STATUS)) != 0xFF) any = 1;
    if (!any) return 0;
    s16 t = (s16)(DSS(DS_drag_wins) + DSS(DS_road_wins));
    if (t < 3) return 1;
    if (t >= 7 && DSS(DS_drag_wins) < 6 && DSS(DS_road_wins) < 6) return 3;
    if (t < 7 && DSS(DS_drag_wins) < 6 && DSS(DS_road_wins) < 6) return 2;
    if (DSS(DS_drag_wins) < 6) return 4;
    if (DSS(DS_road_wins) < 6) return 5;
    if (t < (s16)(DSS(DS_races_total) >> 1)) return 3;
    return 0;
}

/* 0000:95f1: % chance that the King drives in. */
s16 king_chance(void)
{
    if (DSS(DS_opp_model) == 0x19) return 0;
    s16 s = king_status();
    if (s == 0) return DSB(DS_king_ready) == 1 ? 10 : 0x32;
    if (s == 1) return 5;
    if (s == 2) return 8;
    return 12;
}

/* 0000:9646: candidate list DS:6C8A ordered by top-speed difference. */
static void opp_sort(s16 all)
{
    s16 spd[0x15], used[0x15], cls[0x15];
    for (s16 i = 0; i < 0x15; i++) {
        s16 m = DSS((u16)(OREC(i) + OR_MODEL));
        s16 vp = car_max_speed(-1);
        s16 vo = car_max_speed(m);
        spd[i] = abs16((s16)(vo - vp));
        s16 cp = DSB(MODEL(DSS((u16)(cur_car() + CAR_MODEL))) + 2);
        cls[i] = abs16((s16)(DSB(MODEL(m) + 2) - cp));
        used[i] = DSB((u16)(OREC(i) + OR_STATUS)) == 0xFF;
    }
    DSW(DS_cand_count) = 0;
    for (;;) {
        if ((all ? 0x15 : 9) <= DSS(DS_cand_count)) break;
        s16 found = 0, best = 0x7D00, bi = 0;
        for (s16 i = 0; i < 0x15; i++)
            if (!used[i] && spd[i] < best) { bi = i; best = spd[i]; found = 1; }
        if (found) {
            DSW((u16)(DS_cand + 2 * DSS(DS_cand_count))) = (u16)bi;
            DSW(DS_cand_count)++;
            used[bi] = 1;
        }
        if (!found) break;
    }
    if (!all) {
        /* sic: tests the class difference of OPPONENT k-1, not of cand[k-1] (race.md §4.14) */
        u16 src = DS_cand;
        for (s16 k = DSS(DS_cand_count); k > 6; k--) {
            if (cls[k - 1] > 1) {
                DSW((u16)(DS_cand + 2 * (k - 1))) = DSW(src);
                src += 2;
            }
        }
    }
    if (DSW(DS_demo_active)) {
        u16 o = DSW(BOB_DEMO_OPP);
        DSW((u16)(DS_cand + 2 * DSS(DS_cand_count))) = o; DSW(DS_cand_count)++;
        DSW((u16)(DS_cand + 2 * DSS(DS_cand_count))) = o; DSW(DS_cand_count)++;
        if (DSW(BOB_FIRST_PICK)) { DSW(DS_cand_count) = 1; DSW(DS_cand) = o; }
        DSW(BOB_FIRST_PICK) = 0;
    }
}

/* 0000:984e: the King (0x15) with pct_king %, else a random candidate, no immediate repeat. */
s16 pick_opponent(s16 pct_king, s16 all)
{
    if (rnd(100) < pct_king) return 0x15;
    opp_sort(all);
    if (DSW(DS_cand_count) == 0) return 0x15;
    s16 o = DSS((u16)(DS_cand + 2 * rnd(DSS(DS_cand_count))));
    if (o == DSS(DS_last_pick)) o = DSS((u16)(DS_cand + 2 * rnd(DSS(DS_cand_count))));
    DSW(DS_last_pick) = (u16)o;
    return o;
}

/* 0000:98ba: make opponent `opp` current; random sticker / customising; picture globals. */
void opponent_load(s16 opp)
{
    DSW(DS_opp_index) = (u16)opp;
    u16 rec = OREC(opp);
    DSW(DS_opp_rec) = rec;
    DSW(DS_opp_model) = DSW((u16)(rec + OR_MODEL));
    DSW(DS_opp_model_ptr) = MODEL(DSS(DS_opp_model));
    if (opp == 0x15) {
        DSW(DS_vs_king) = 1;
    } else {
        DSW(DS_vs_king) = 0;
        DSB((u16)(rec + OR_STATUS)) = 1;
        s16 chance = (s16)((DSB((u16)(DSW(DS_opp_model_ptr) + 2)) >> 1) + 1);
        rec = DSW(DS_opp_rec);
        if (DSC((u16)(rec + OR_STICKER)) == 0 && DSC((u16)(rec + OR_RACES)) > 5) {
            s8 st = rnd(0x14) < chance ? (s8)rnd(9) : 0;
            DSC((u16)(DSW(DS_opp_rec) + OR_STICKER)) = st;
        } else if (DSC((u16)(rec + OR_STICKER)) > 0 && DSC((u16)(rec + OR_RACES)) > 10) {
            s8 st = rnd(0x28) < chance ? (s8)rnd(9) : DSC((u16)(DSW(DS_opp_rec) + OR_STICKER));
            DSC((u16)(DSW(DS_opp_rec) + OR_STICKER)) = st;
        }
        s16 m = DSS(DS_opp_model);
        rec = DSW(DS_opp_rec);
        if (MODEL_8DF6(m) > 0 && MODEL_8DF8(m) > 0 && DSC((u16)(rec + OR_RACES)) > 8) {
            u8 v = rnd(0x19) < chance ? 3 : DSB((u16)(DSW(DS_opp_rec) + OR_CUSTOM));
            DSB((u16)(DSW(DS_opp_rec) + OR_CUSTOM)) |= v;
        }
        if (MODEL_8DF2(m) > 0 && DSC((u16)(DSW(DS_opp_rec) + OR_RACES)) > 8) {
            u8 v = rnd(0x1E) < chance ? 4 : DSB((u16)(DSW(DS_opp_rec) + OR_CUSTOM));
            DSB((u16)(DSW(DS_opp_rec) + OR_CUSTOM)) |= v;
        }
    }
    u16 mp_ = DSW(DS_opp_model_ptr);
    DSW(BOB_OPP_PIC) = DSW((u16)(mp_ + 8));
    DSW(BOB_TYRE_SET) = (u16)((DSW((u16)(mp_ + 4)) >> 4) & 3);
    DSW(BOB_OPP_COLOUR) = (u16)(s16)DSC((u16)(DSW(DS_opp_rec) + OR_COLOUR));
    set_paint_palette(DSS(BOB_OPP_COLOUR));
    DSW(BOB_OPP_FACE) = DSW((u16)(DSW(DS_opp_rec) + OR_PIC));
}

/* 0000:9a27: load opponent `opp` and show him (car_draw mode); flag 1 = he drives off at once,
 * flag 2 = the King drives off when he is not interested (king_status 1). Also used by jail. */
void opponent_select(s16 opp, s16 mode, s16 flag)
{
    opponent_load(opp);
    if (flag == 1) DSW(BOB_OPP_LEAVES) = 1;
    if (flag == 2 && DSS(DS_opp_index) == 0x15) DSW(BOB_OPP_LEAVES) = king_status() == 1;
    opponent_show(mode, DSS(DS_opp_model), 1, 0xA3, 0, tyre_list());
}

/* 0000:9a83: opponents met (status 1), the King included: names[0] = count, names[1..] message
 * ids, idx[0..] opponent numbers, xoff[1..] = 7. Also refreshes DS:817D. */
static void opp_known_list(s16 *names, s16 *idx, s16 *xoff)
{
    DSB(DS_king_ready) = king_status() == 0 ? 1 : 0;
    s16 n = 0;
    for (s16 i = 0; i <= 0x15; i++) {
        if (DSB((u16)(OREC(i) + OR_STATUS)) == 1) {
            idx[n] = i;
            n++;
            names[n] = DSS((u16)(OREC(i) + OR_NAME));
            xoff[n] = 7;
        }
    }
    names[0] = n;
}

/* 0000:9af4: "Looking for" list; 1 = an opponent was selected, 0 = none / cancelled. */
static s16 opponent_search(s16 mode)
{
    s16 names[0x19] = {0}, idx[0x18] = {0}, xoff[0x18] = {0};
    status_print(-1, ds_str(0x51FA), 0);
    opp_known_list(names, idx, xoff);
    s16 r = list_box(0x24, 1, xoff, names, NULL, NULL);
    if (r == -4) return 0;
    if (r == -3) {
        if (DSW(DS_opp_present)) {
            status_print(-1, ds_str(0x51FB), 0);
            opponent_show(2, DSS(DS_opp_model), 1, 0xA3, 0, tyre_list());
        }
        s16 sel = list_selected();
        opponent_select(idx[sel - 1], mode, 0);
        return 1;
    }
    if (r == 0x3EA) { msg_box(0x198C); return 0; }
    return r;
}

/* 0000:9bc5: does the opponent accept bet kind `bet`? */
static s16 challenge_accept(s16 bet)
{
    s16 col = 0, row = 0;   /* PORT: uninitialised in the original for bet kinds > 5 (never passed) */
    if (bet == 0) { col = 0; row = 0; }
    else if (bet == 5) { col = 2; row = 3; }
    else if (bet == 1 || bet == 2) { col = 1; row = 1; }
    else if (bet == 3 || bet == 4) { col = 1; row = 2; }
    u16 rec = DSW(DS_opp_rec);
    s16 dclass = DSC(rec);
    s16 c3o = model_class3(DSS(DS_opp_model));
    s16 c3p = model_class3(DSS((u16)(cur_car() + CAR_MODEL)));
    s16 eo = (s16)((DSW((u16)(DSW(DS_opp_model_ptr) + 4)) >> 8) & 7);
    s16 gp = part_grade(DSW((u16)(cur_car() + CAR_ENGINE)));
    u16 t  = DSW((u16)(0x8BAE + dclass * 6 + col * 2));
    u16 vo = DSW((u16)(0x7D68 + c3o * 6 + eo * 2));
    u16 vp = DSW((u16)(0x7D68 + c3p * 6 + gp * 2));
    s16 cmp = (u16)(t + vo) < vp ? 0 : (vo > vp ? 2 : 1);   /* unsigned compares (jae / jbe) */
    s16 c = DSS((u16)(0x8DD8 + row * 6 + cmp * 2));
    u16 fl = DSW((u16)(cur_car() + CAR_FLAGS));
    s16 paint = !(fl & 0x1F00) ? 0 : (fl & 0x100) ? 4 : 6;
    s16 x = (s16)((s16)(DSC((u16)(rec + OR_WINS)) * 2 - DSC((u16)(rec + OR_RACES))) * c);
    s16 sg = x < 0 ? -1 : 0;
    s16 a = (s16)((x ^ sg) - sg);
    a = (s16)(a >> 2);
    a = (s16)((a ^ sg) - sg);
    c = (s16)(c + (s16)(a + paint));
    if (DSW(DS_demo_active)) c = (s16)(c + (s16)((c >> 2) + 10));
    return rnd(100) <= c;
}

/* 0000:9d6d: race type, bet and the King's rules; runs the race. 2 = raced, 0 = no race. */
s16 challenge(s16 king)
{
    arena_reset_stacks();
    if (king) {
        s16 s = king_status();
        if (s != 0) {
            if (s == 2) msg_box(0x1113);
            else if (s == 3) msg_box(0x113D);
            else if (s == 4) msg_box(0x117D);
            else if (s == 5) msg_box(0x11A7);
            arena_reset_stacks();
            return 0;
        }
        if (!DSW(DS_king_drag_won)) {
            if (CARS(cur_car(), CAR_GAS) < 0x16) { msg_box(MSG_NO_GAS); return 0; }
            DSW(DS_race_type) = 0;
            msg_box(0x11D1);
            DSW(DS_bet_kind) = 0;
        } else {
            DSW(DS_race_type) = 1;
            msg_box(0x11F7);
            DSW(DS_bet_kind) = 5;
        }
        DSW(DS_bet_amount) = 0;
        race_start();
        flow_clock_add(DSW(DS_race_type) == 0 ? 0x444 : 0x777);
        return 2;
    }
    if (CARS(cur_car(), CAR_GAS) < 7) { msg_box(MSG_NO_GAS); return 0; }

    s16 sx = 0, sy = 0;
    FarPtr save = ui_save_bg(0x12, &sx, &sy, 1);
    ui_push(0x16);
    set_deadline(0x320);
    s16 b = wait_event();
    /* PORT: the bet menu is uninitialised in the original when another code ends the first menu */
    s16 menu = (s16)(0x12 + (DSS(DS_race_type) == 1));
    if (b == -0x17 || b == 1000) { msg_box(MSG_CHICKEN); goto cancel; }
    if (b == 1) { menu = 0x12; DSW(DS_race_type) = 0; }
    else if (b == 2) { menu = 0x13; DSW(DS_race_type) = 1; }
    if (DSS(DS_race_type) == 1 && CARS(cur_car(), CAR_GAS) < 0x11) { msg_box(MSG_NO_GAS); goto cancel; }

    ui_pop(1);
    s16 last = -1;
    ui_push(menu);
    s16 amount = 0, kind = 0;   /* PORT: uninitialised in the original until a bet button is hit */
    for (s16 tries = 0; tries < 2; tries++) {
        set_deadline(0x320);
        b = wait_event();
        DSW(DS_bet_button) = (u16)b;
        switch (b) {
        case 1: amount = 0;    kind = 0; break;
        case 2: amount = 10;   kind = 1; break;
        case 3: amount = 0x32; kind = 2; break;
        case 7: amount = 0x19; kind = 3; break;
        case 8: amount = 0x64; kind = 4; break;
        case 9: amount = 0;    kind = 5; break;
        case -0x17: case 1000: msg_box(MSG_CHICKEN); goto cancel;
        default: break;
        }
        if ((s32)(u16)amount > DSSL(DS_money)) {
            msg_box(0x131B);
            continue;
        }
        if (challenge_accept(kind)) {
            DSW(DS_bet_kind) = (u16)kind;
            DSW(DS_bet_amount) = (u16)amount;
            ui_pop(1);
            screen_put_bitmap_mirror(save, sx, sy);
            s16 k = rnd(3);
            msg_box(DSS((u16)(0x5210 + DSS(DS_race_type) * 6 + k * 2)));
            race_start();
            flow_clock_add(DSW(DS_race_type) == 0 ? 0x444 : 0x777);
            return 2;
        }
        s16 r;
        do r = rnd(5); while (r == last);
        last = r;
        msg_box(DSS((u16)(0x51FC + DSS(DS_race_type) * 10 + r * 2)));
    }
cancel:
    ui_pop(1);
    screen_put_bitmap_mirror(save, sx, sy);
    arena_reset_stacks();
    return 0;
}

static void rec_swap(u16 a, u16 b)
{
    u8 tmp[UI_REC_SIZE];
    memcpy(tmp, mp(DGROUP, a), UI_REC_SIZE);
    memcpy(mp(DGROUP, a), mp(DGROUP, b), UI_REC_SIZE);
    memcpy(mp(DGROUP, b), tmp, UI_REC_SIZE);
}

/* 0000:a0f8: Bob's hot spots (records 0x143..0x147 of screen 0x25): the car-hop "Order" spot
 * (code 4 record kept in the 5th slot), "See it" / "Click to challenge" rectangles on the car. */
static void bob_hotspots(s16 opp_present, s16 order_ok)
{
    const u16 R = BOB_REC;
    if (DSW(0x20F8) != 4) rec_swap(0x20E0, 0x20F2);
    DSW(R + 0x50) = 0xC2;
    DSW(R + 0x54) = 0xD8;
    DSW(R + 0x52) = 0x49;
    DSW(R + 0x56) = 0x77;
    DSW(R + 0x24) = order_ok == 1 ? 0x0B : 0x0E;
    if (opp_present == 0) {
        DSW(R + 0x12) = 0x0E;
        DSW(R + 0x36) = 0x0E;
        return;
    }
    u16 si = (u16)(DSS(DS_opp_model) * 0x14);
    s16 x  = (s16)(DSB((u16)(0x70E8 + si)) + DSS(DS_car_x));
    s16 y  = (s16)(DSB((u16)(0x70E9 + si)) + DSS(DS_car_y));
    s16 x1 = (s16)(DSB((u16)(0x70EA + si)) + x - 1);
    s16 y1 = (s16)(DSS(R + 0x0A) - 1);
    DSW(R + 0x1A) = (u16)x;
    DSW(R + 0x1E) = (u16)x1;
    DSW(R + 0x1C) = (u16)y;
    DSW(R + 0x20) = (u16)y1;
    DSW(R + 0x12) = 0x0B;
    x  = (s16)(DSB((u16)(0x70F0 + si)) + DSS(DS_car_x));
    y  = (s16)(DSB((u16)(0x70F1 + si)) + DSS(DS_car_y));
    x1 = (s16)(DSB((u16)(0x70F2 + si)) + x - 1);
    y1 = (s16)(DSB((u16)(0x70F3 + si)) + y - 1);
    if (DSS(R + 0x1E) >= x) x = (s16)(DSS(R + 0x1E) + 1);
    DSW(R + 0x3E) = (u16)x;
    DSW(R + 0x42) = (u16)x1;
    DSW(R + 0x40) = (u16)y;
    DSW(R + 0x44) = (u16)y1;
    DSW(R + 0x36) = 0x0B;
    if (DSS(R + 0x56) >= DSS(R + 0x1C)) DSW(R + 0x56) = (u16)(DSS(R + 0x1C) - 1);
    if (DSS(R + 0x50) < DSS(R + 0x3E)) rec_swap(R + 0x36, R + 0x48);
}

/* PORT: 0000:a265 builds the temporary car record and its 7 part records on its stack and passes
 * near pointers to car_new / engine_bay (SS = DS). The port needs DGROUP addresses: it uses the
 * original's stack area DS:8F10-990E, which the port never uses (C locals are native), at the same
 * relative layout as bp-0x66..bp-0x04. */
#define A265_TMP   0x9000u
#define A265_CAR   (A265_TMP + 0x10)

/* 0000:a265 "See it": the opponent's car (a stock car of his model) in the hood view. */
static void opponent_car_view(void)
{
    arena_reset_stacks();
    FarPtr save = screen_save_rect(0x60, 9, 0xC8, 0x8F, far_make(0, 0));
    memset(mp(DGROUP, A265_TMP), 0, 0x66);   /* PORT: stack garbage in the original */
    DSW(A265_CAR + CAR_TRANS) = A265_TMP;
    DSW(A265_CAR + CAR_TYRES) = A265_TMP + 8;
    for (u16 i = 0; i < 5; i++)
        DSW((u16)(A265_CAR + CAR_ENGINE + 2 * i)) = (u16)(A265_TMP + 0x3A + 8 * i);
    car_new(DSS(DS_opp_model), 0, A265_CAR);
    engine_bay(A265_CAR);
    status_print(-1, ds_str(0x521C), 0);
    ui_push(0x18);
    set_deadline(0x320);
    wait_event();
    DSW(0x8234) = 0;
    DSW(0x8232) = 0;
    screen_put_bitmap_mirror(save, 0x60, 9);
    arena_reset_stacks();
    ui_pop(1);
}

/* 0000:a35c: one 40x50 cell of the dancer picture (7 per row) to (0xB8, 0x49) of dst; in jail the
 * bars (screen 0xD0..0xDF) are copied into the cell first. */
static void girl_frame(FarPtr pic, s16 frame, FarPtr dst)
{
    s16 row, col;
    if (frame < 0x0E) {
        row = (s16)(frame / 7);
        col = (s16)(frame - 7 * row);
    } else {
        row = 0;
        col = 0;
    }
    Rect r = { 0x28, 0x32, (s16)(0x28 * col), (s16)(0x32 * row), 0xB8, 0x49 };
    if (DSS(DS_drive_dest) == 6) {
        Rect bars = { 0x10, 0x32, 0xD0, 0x49, (s16)(r.sx + 0x18), r.sy };
        drv_blit(g_front(), pic, &bars, 1);
    }
    drv_blit(pic, dst, &r, 1);
}

/* 0000:a416: the juke-box dancer (LIB2 #225), 31 steps {cell, ticks} at DS:521E; any hot-spot code
 * -0x1A stops her. */
void dancing_girl(void)
{
    arena_reset_stacks();
    FarPtr pic = pic_get(0x4C9, 2);
    s16 n = 0;
    ui_push(0x18);
    set_deadline(10);
    s16 go = 1;
    u16 p = 0x521E, q = 0x521F;
    for (;;) {
        s16 b = wait_event();
        if (b == -0x1A) go = 0;
        else if (b == 1000) {
            if (n < 0x1F) {
                wait_vretraces(1);
                girl_frame(pic, DSC((u16)(0x525B + DSB(p))), g_front());
                DSL(DS_wait_deadline) = ticks_now() + DSB(q);
                p += 2;
                q += 2;
                n++;
            } else go = 0;
        }
        if (!go) break;
    }
    wait_vretraces(1);
    girl_frame(pic, DSC(0x525C), g_front());
    girl_frame(pic, DSC(0x525C), g_back());
    ui_pop(1);
    arena_reset_stacks();
}

static void bob_redraw_money(void) { status_label(ds_str(DSW(BOB_STATUS_MONEY))); }

/* 0000:ab8b: Bob's Drive-In main loop. */
void bob_drive_in(void)
{
    s16 seen = 0;
    s16 mode = 4;      /* char in the original: 4 = nobody yet, 1 = an opponent came */
    s16 quick = 0;
    s16 pct = 0;

    status_print(-1, ds_str(0x532E), 0);
    DSW(BOB_MOUSE_X) = 0x9E;
    DSW(BOB_MOUSE_Y) = 0x80;
    show_picture(0x445, -1);
    pal_reg12_13();
    bob_redraw_money();
    DSW(BOB_7B10) = 0xFFFF;
    cursor_ctl(1);
    drv_pal_normal();
    music_mute(0);
    if (rnd(100) < 0x19) dancing_girl();
    cursor_ctl(-2);
    DSW(DS_opp_present) = 0;
    DSW(DS_waitress_state) = 0xFFFF;
    DSW(DS_drink_ordered) = 0;
    DSW(BOB_FIRST_PICK) = 1;
    DSW(DS_drive_dest) = 2;
    DSB(DS_click_held) = 0;
    DSB(DS_joy_buttons) = 0;
    DSW(DS_mouse_left) = 0;

    for (;;) {
        bob_hotspots(DSS(DS_opp_present), DSW(DS_waitress_state) == 0 && DSW(DS_drink_ordered) == 0);
        ui_push(0x25);
        if (!quick) set_deadline(mode == 4 ? 0x8C : 0x118);
        else set_deadline(5);
        quick = 0;
        pct = mode == 4 ? 0 : king_chance();
        summer_over_check();
        if (DSW(DS_game_end_flags)) return;
        s16 b = wait_event();
        switch (b) {
        case 1:                                             /* back to the garage */
            DSB(BOB_NAME_BUF) = 0;
            status_print(DSS(BOB_NAME_X), ds_str(0x532F), DSS(BOB_NAME_W));
            ui_pop(1);
            DSW(DS_drive_dest) = 1;
            drive_to();
            return;
        case 1000:                                          /* time-out: somebody drives in */
            summer_over_check();
            if (DSW(DS_game_end_flags)) return;
            if (DSW(DS_opp_present)) {
                status_print(-1, ds_str(0x5331), 0);
                goto opp_leaves;
            }
            goto new_opponent;
        case 2:                                             /* challenge */
            if (!DSW(DS_opp_present)) {
                DSL(DS_wait_deadline) = ticks_now();
                goto new_opponent;
            }
            DSW(BOB_CHALLENGED) = 1;
            status_print(-1, ds_str(0x5332), 0);
            status_print(DSS(BOB_NAME_X), ds_str(0x5333), DSS(BOB_NAME_W));
            for (;;) {
                status_print(-1, ds_str(0x5334), 0);
                if (!challenge(DSS(DS_vs_king))) goto opp_leaves;
                DSB(BOB_NAME_BUF) = 0;
                status_print(DSS(BOB_NAME_X), ds_str(0x5335), DSS(BOB_NAME_W));
                s16 r = (s16)race_results();
                drv_pal_black();
                opp_palette_set(-1);
                if (DSW(DS_bet_button) != 1) DSW(DS_races_total)++;
                if (DSW(DS_vs_king)) {
                    if (!DSW(DS_player_won)) {
                        DSW(DS_king_drag_won) = 0;
                        DSB(DS_king_ready) = 0;
                        DSW(DS_drag_wins) -= 3;
                        DSW(DS_road_wins) -= 2;
                    } else if (DSW(DS_race_type)) {         /* the King's road race won: the end */
                        music_mute(0);
                        ui_pop(1);
                        ending_king();
                        DSB(DS_game_end_flags) |= 2;
                        return;
                    } else {
                        DSW(DS_king_drag_won) = 1;
                    }
                }
                if (r == 4) {                               /* jailed */
                    music_mute(0);
                    ui_pop(1);
                    jail();
                    cursor_ctl(-4);
                    DSB(DS_game_end_flags) |= 4;
                    return;
                }
                if (DSW(DS_player_won) && DSW(DS_bet_button) != 1) {
                    if (DSW(DS_race_type) == 0) DSW(DS_drag_wins)++;
                    else DSW(DS_road_wins)++;
                }
                if (DSW(DS_bet_button) != 1) {
                    u16 rec = DSW(DS_opp_rec);
                    DSB((u16)(rec + OR_RACES))++;
                    if (!DSW(DS_player_won)) DSB((u16)(rec + OR_WINS))++;
                }
                if (r == 2 || r == 1) {                     /* drive off: r == 1 -> DS:0284 = 1, r == 2 -> 0 */
                    ui_pop(1);
                    DSW(DS_drive_dest) = r == 2 ? 0 : 1;
                    if (DSW(DS_drive_dest) == 1) DSW(DS_skip_fx) = 1;
                    drive_to();
                    DSW(DS_skip_fx) = 0;
                    return;
                }
                music_mute(0);
                show_picture(0x445, -1);
                bob_redraw_money();
                drv_pal_normal();
                mode = 4;
                DSW(DS_opp_present) = 0;
                DSW(DS_waitress_state) = 0xFFFF;
                DSW(DS_drink_ordered) = 0;
                seen = 0;
                if (!DSW(DS_king_drag_won)) goto next;
                opponent_select(0x15, 4, 0);                /* the King stays for the road race */
            }
        case 3:                                             /* "See it" */
            if (!DSW(DS_opp_present)) break;
            if (seen > 0 || DSW(DS_vs_king)) {
                msg_box(MSG_CHICKEN);
                goto opp_leaves;
            }
            opponent_car_view();
            seen++;
            flow_clock_add(0x111);
            break;
        case 4:                                             /* "Looking for" */
            if (opponent_search(mode)) { seen = 0; break; }
            quick = 1;
            break;
        case 5:                                             /* "Order" */
            if (DSW(DS_waitress_state) == 0) DSW(DS_drink_ordered) = 1;
            break;
        default:
            break;
        }
        goto next;
    opp_leaves:
        opponent_show(2, DSS(DS_opp_model), 1, 0xA3, 0, tyre_list());
        seen = 0;
        goto next;
    new_opponent:
        opponent_select(pick_opponent(pct, 0), mode, 2);
        mode = 1;
    next:
        ui_pop(1);
    }
}
