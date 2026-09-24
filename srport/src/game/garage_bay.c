/* Garage: the transmission screen 0000:7aa4, the tune-up 0000:7fb1, the engine bay 0000:81ab
 * (garage.md §4.10 - §4.13) and their drawing helpers 0f38:3074 - 4265, ported from the
 * disassembly (the decompile garbles the slot-11 calls).
 *
 * Engine bay drawing: DS:8232 (far) is the bay background bitmap (LIB2 #45 = 0x42D, 1 plane set per
 * part composed into it), drawn at (0x60, 9) on page B; bay_ctx DS:82AE selects a part position
 * (0 engine, 1 manifold, 4..13 carburettor positions of the manifold layouts), DS:82B2 the side of a
 * bolt pair. Position tables: DS:0702 engine, DS:070E engine bolt, DS:071A connector, DS:0732
 * manifold bolts, DS:0742 carburettors, DS:076A carb bolt offsets, DS:077E slot per context,
 * DS:078C first carb context per manifold grade. DS:06FC = 0 composes without animating. */
#include "game/garage.h"

#include <string.h>

#include "game/flow.h"
#include "game/ui.h"
#include "platform/platform.h"
#include "platform/video.h"
#include "sound/sound.h"

#define BAY_BG      0x8232      /* far: bay background */
#define BAY_CTX     0x82AE
#define BAY_SIDE    0x82B2
#define SLOT_PIC    0x8290      /* [5] engine, manifold, carb 1-3 */
#define BOLT_PIC    0x829A      /* [9] */
#define CONN_PIC    0x82AC
#define ANIMATE     0x06FC      /* u8 */
#define BOLT_PICS   0x49CA
#define BAY_PICS    0x4998
#define TRANS_PICS  0x49D2
#define CARBS_PER   0x5126
#define BOLTS_PER   0x5130
#define LAYER_CAT   0x513C
#define BAY_LAYOUT  0x5148
#define BOLT_CTX    0x5184
#define BOLT_SIDE   0x5198
#define LAYOUT_CUR  0x518C

static inline void clock_add(u16 v) { DSL(DS_game_clock) += v; }
static inline FarPtr bay_bg(void) { return ds_far(BAY_BG); }
static inline bool has_mask(FarPtr d) { return !far_is_null(desc_mask(d)); }
static inline s16 slot_pic(s16 i) { return DSS((u16)(SLOT_PIC + 2 * i)); }
static inline s16 grade_of(u16 part) { return parts_grade(DSS((u16)(part + PT_TYPE))); }

/* ================================================================ transmission screen helpers */

/* 0f38:3074: bolt `bolt` (y from DS:06FE) at tightness `tight` (LIB2 #79 = 0x44F, 16x6 frames);
 * a new bolt (tight 1) first slides in from the top. */
static void trans_bolt_draw(s16 tight, s16 bolt)
{
    cursor_ctl(-3);
    FarPtr pic = pic_get(0x44F, 2);
    s16 x = 0xB8;
    s16 y = (s16)(DSS((u16)(0x06FE + 2 * bolt)) + 10);
    if (tight == 1 && DSW(DS_g_mirror) != 0) {
        s16 r[4] = { 0x10, 6, 0x40, 0 };
        FarPtr spr = bitmap_extract(pic, r, 2);
        drv_slide_sprite(0x0E, 0x94, 0xB8, 8, y, spr);
        arena_pop(1);
    }
    Rect rc = { 0x10, 6, (s16)(tight << 4), 0, x, y };
    drv_blit(pic, g_back(), &rc, 1);
    cursor_ctl(-1);
    if (DSW(DS_g_mirror) != 0) page_copy_rect(g_back(), g_front(), x, y, (s16)(x + 0xF), (s16)(y + 5));
    arena_pop(1);
}

/* 0f38:31a5: the bolt comes out and falls */
static void trans_bolt_remove_anim(s16 bolt)
{
    cursor_ctl(-3);
    FarPtr pic = pic_get(0x44F, 2);
    s16 y = (s16)(DSS((u16)(0x06FE + 2 * bolt)) + 10);
    Rect rc = { 0x10, 6, 0, 0, 0xB8, y };
    drv_blit(pic, g_back(), &rc, 1);
    cursor_ctl(-1);
    if (DSW(DS_g_mirror) != 0) {
        page_copy_rect(g_back(), g_front(), 0xB8, y, 0xB8 + 0xF, (s16)(y + 5));
        fx_thud();
        cursor_ctl(-4);
        s16 r[4] = { 0x10, 6, 0x40, 0 };
        FarPtr spr = bitmap_extract(pic, r, 2);
        drv_slide_sprite(0x0E, 0x94, 0xB8, y, 0x94, spr);
        arena_pop(1);
        cursor_ctl(-2);
    }
    arena_pop(1);
}

/* 0f38:32ce: the transmission slides up from below into place (reject: 24 px short of it) */
static void trans_slide_anim(s16 id, s16 reject)
{
    cursor_ctl(-4);
    FarPtr p = pic_get(id, 2);
    s16 w = (s16)desc_w(p), h = (s16)desc_h(p);
    s16 x = (s16)(200 - w);
    s16 y = (s16)(0x7D - h);
    if (reject != 0) x = (s16)(x - 0x18);
    if (DSW(DS_g_mirror) != 0) {
        s16 sx = (s16)(x - 8), sw = (s16)(w + 8);
        FarPtr none = { 0, 0 };
        FarPtr save = screen_save_rect(sx, y, sw, h, none);
        drv_slide_sprite(0x0E, 0x94, sx, 0x94, y, p);
        Rect rc = { sw, h, 0, 0, 8, 0 };
        drv_blit_masked(p, save, &rc, 1);
        cursor_ctl(-2);
        screen_put_bitmap_mirror(save, sx, y);
        arena_pop(1);
    } else {
        drv_slide_sprite(0x0E, 0x94, x, y, y, p);
        cursor_ctl(-2);
    }
    arena_pop(1);
}

/* 0f38:3404: the empty transmission panel again (LIB2 #46); the original's arguments are unused */
static void trans_remove_anim(void)
{
    cursor_ctl(-3);
    FarPtr p = pic_get(0x42E, 2);
    cursor_ctl(-1);
    screen_put_bitmap_mirror(p, 0x20, 10);
    arena_pop(1);
}

/* 0f38:345b: transmission and its two bolts, drawn without mirroring, then mirrored at once */
static void trans_screen_draw(s16 pic, u16 bolts)
{
    u16 m = DSW(DS_g_mirror);
    if (pic == 0) return;
    DSB(ANIMATE) = 0;
    DSW(DS_g_mirror) = 0;
    cursor_ctl(-3);
    trans_slide_anim(pic, 0);
    for (s16 i = 0; i < 2; i++) trans_bolt_draw(DSC((u16)(bolts + i)), i);
    cursor_ctl(-1);
    DSW(DS_g_mirror) = m;
    if (m != 0) page_copy_rect(g_back(), g_front(), 0x20, 10, 0xE7, 0xB8);
    DSB(ANIMATE) = 1;
}

/* ================================================================ engine bay helpers */

/* 0f38:34fb (near): 0/1/2 for the engine pictures 0x434-0x436, else the picture value itself (sic) */
static s16 bay_engine_idx(void)
{
    s16 v = slot_pic(0);
    if (v == 0x434) return 0;
    if (v == 0x435) return 1;
    if (v == 0x436) return 2;
    return v;
}

/* 0f38:351c (near): carburettor position of context DS:82AE (>= 4); returns the bolt-offset base
 * 0/2/4 of the carburettor picture (0x43C-0x43E). */
static s16 bay_carb_pos(s16 *x, s16 *y)
{
    s16 r = 0;                                    /* PORT: [bp-2] uninitialised for other pictures */
    s16 ctx = DSS(BAY_CTX);
    s16 v = slot_pic(DSC((u16)(0x077E + ctx)));
    if (v == 0x43C) r = 0;
    else if (v == 0x43D) r = 2;
    else if (v == 0x43E) r = 4;
    *x = DSS((u16)(0x0742 + 4 * (ctx - 4)));
    *y = DSS((u16)(0x0744 + 4 * (ctx - 4)));
    return r;
}

/* 0f38:3589: where the part of context DS:82AE goes (bottom-left, bay coordinates) */
static void bay_part_pos(s16 *x, s16 *y)
{
    s16 ctx = DSS(BAY_CTX);
    if (ctx == 0) {
        s16 i = bay_engine_idx();
        *x = DSS((u16)(0x0702 + 4 * i));
        *y = DSS((u16)(0x0704 + 4 * i));
    } else if (ctx == 1) {
        *x = 0x38;
        *y = 0x4C;
    } else {
        bay_carb_pos(x, y);
    }
}

/* 0f38:35e4: position of bolt side DS:82B2 of the part of context DS:82AE */
static void bay_bolt_pos(s16 *x, s16 *y)
{
    s16 ctx = DSS(BAY_CTX);
    if (ctx == 0) {
        s16 i = bay_engine_idx();
        *x = DSS((u16)(0x070E + 4 * i));
        *y = DSS((u16)(0x0710 + 4 * i));
    } else if (ctx == 1) {
        s16 v = slot_pic(1);
        s16 k = (v == 0x438 || v == 0x437) ? 0 : 2;
        if (DSS(BAY_SIDE) == 1) k++;
        *x = DSS((u16)(0x0732 + 4 * k));
        *y = DSS((u16)(0x0734 + 4 * k));
    } else {
        s16 k = (s16)(bay_carb_pos(x, y) + DSS(BAY_SIDE));
        *x = (s16)(*x + DSS((u16)(0x076A + 4 * k)));
        *y = (s16)(*y + DSS((u16)(0x076C + 4 * k)));
    }
}

/* 0f38:3697: bolt picture `pic` at the bolt position; the background comes back from the bay
 * bitmap, the bolt slides in (a new one, 0x43F, from the top). */
static void bay_bolt_draw(s16 pic)
{
    s16 x, y;
    bay_bolt_pos(&x, &y);                         /* [bp-22h], [bp-28h] */
    cursor_ctl(-4);
    FarPtr p = pic_get(pic, 2);                   /* [bp-4] */
    FarPtr q = p;                                 /* [bp-8] */
    s16 h = (s16)desc_h(q);
    y = (s16)(y - h);
    x = (s16)(x - 2);
    Rect rc = { 6, h, x, y, (s16)(x + 0x60), (s16)(y + 9) };
    y = (s16)(y + 9);
    drv_blit_shifted(bay_bg(), g_back(), &rc);
    s16 shift = (s16)(x & (s16)~DSW(DS_g_x_align));
    if (shift != 0) {
        q = arena_bitmap_alloc((s16)(desc_w(p) + 8), (s16)desc_h(p), has_mask(p) ? 1 : 0, 2);
        bitmap_make_shifted(p, shift, q);
    }
    x = (s16)((x & (s16)DSW(DS_g_x_align)) + 0x60);
    s16 from = (pic == 0x43F && DSW(DS_g_mirror) != 0) ? (s16)(0x0D - h) : y;
    drv_slide_sprite(0x0D, 0x93, x, from, y, q);
    cursor_ctl(-2);
    arena_pop(1);
    if (p.off != q.off || p.seg != q.seg) arena_pop(1);
}

/* 0f38:37fc: the bolt comes out: background restored, the bolt falls */
static void bay_bolt_remove_anim(void)
{
    s16 x, y;
    bay_bolt_pos(&x, &y);                         /* [bp-20h], [bp-24h] */
    s16 sx = (s16)(x - 2);                        /* [bp-1Ah] */
    s16 sy = y;                                   /* [bp-1Eh] */
    cursor_ctl(-4);
    FarPtr p = pic_get(0x43F, 2);
    s16 w = 8;                                    /* [bp-1Ch] */
    s16 r = (s16)(x & 7);
    if (r < 2) { x = (s16)(x - 8); w = 0x10; }
    else if (r > 4) w = 0x10;
    x = (s16)(x & (s16)DSW(DS_g_x_align));
    s16 h = (s16)desc_h(p);
    sy = (s16)(sy - h);
    Rect rc = { (s16)desc_w(p), h, sx, sy, (s16)(sx + 0x60), (s16)(sy + 9) };
    drv_blit_shifted(bay_bg(), g_back(), &rc);
    if (DSW(DS_g_mirror) != 0) {
        x = (s16)(x + 0x60);
        y = (s16)(y + 9 - h);
        page_copy_rect(g_back(), g_front(), x, y, (s16)(x + w), (s16)(y + h));
        fx_thud();
        if (w == 0x10) x = (s16)(x + 8);
        drv_slide_sprite(0x0D, 0x93, (s16)(x - 8), (s16)(y + 3), 0x93, p);
    }
    cursor_ctl(-2);
    arena_pop(1);
}

/* 0f38:3951 (near): when a carburettor slides in over a loose manifold bolt, the bolt is drawn over
 * it: pre-shifted into the slide overlay DS:0792 with its rect DS:70AC. */
static void bay_carb_overlay(void)
{
    if (slot_pic(1) != 0x437) return;
    s8 c = DSC((u16)(0x077E + DSS(BAY_CTX)));
    if (c == 3) return;
    s16 k = c == 2 ? 0 : 1;
    s16 pic = DSS((u16)(0x829C + 2 * k));
    if (pic != 0x43F && pic != 0x440) return;
    s16 x = (s16)(DSS((u16)(0x0732 + 4 * k)) - 2);
    s16 y = DSS((u16)(0x0734 + 4 * k));
    FarPtr p = pic_get(pic, 1);
    FarPtr q = arena_bitmap_alloc((s16)(desc_w(p) + 8), (s16)desc_h(p), has_mask(p) ? 1 : 0, 2);
    ds_far_wr(0x0792, q);
    bitmap_make_shifted(p, (s16)(x & (s16)~DSW(DS_g_x_align)), q);
    arena_pop_low(1);
    q = ds_far(0x0792);
    DSW(0x70AC) = desc_w(q);
    DSW(0x70AE) = desc_h(q);
    DSW(0x70B2) = DSW(0x70B0) = 0;
    DSW(0x70B4) = (u16)((x & (s16)DSW(DS_g_x_align)) + 0x60);
    DSW(0x70B6) = (u16)(y - DSS(0x70AE) + 9);
}

/* 0f38:3a58: part `pic` of context DS:82AE into the bay bitmap (not when rejected) and, with
 * DS:06FC, slid in from the top on the screen (a rejected part stops 20 px above its place). */
static void bay_part_in_anim(s16 pic, s16 reject)
{
    s16 x, y;
    bay_part_pos(&x, &y);                         /* [bp-24h], [bp-2Ah] */
    if (DSB(ANIMATE) != 0) cursor_ctl(-4);
    FarPtr p = pic_get(pic, 2);
    FarPtr q = p;
    s16 w = (s16)desc_w(q);
    s16 shift = (s16)(x & (s16)~DSW(DS_g_x_align));
    if (shift != 0) {
        q = arena_bitmap_alloc((s16)(desc_w(p) + 8), (s16)desc_h(p), has_mask(p) ? 1 : 0, 2);
        bitmap_make_shifted(p, shift, q);
        w = (s16)desc_w(q);
    }
    x = (s16)(x & (s16)DSW(DS_g_x_align));
    if (pic == 0x43C) bay_carb_overlay();
    if (pic != 0x434 && pic != 0x435 && pic != 0x436) y = (s16)(y - desc_h(q));
    if (reject != 0) y = (s16)(y - 0x14);
    s16 h = (s16)desc_h(q);
    Rect rc = { w, h, 0, 0, 0, 0 };
    if (reject == 0) {
        rc.dx = x;
        rc.dy = y;
        drv_blit_masked(q, bay_bg(), &rc, 1);
    }
    if (DSB(ANIMATE) != 0) {
        s16 from = DSW(DS_g_mirror) != 0 ? (s16)(0x0D - h) : y;
        drv_slide_sprite(0x0D, 0x93, (s16)(x + 0x60), from, (s16)(y + 9), q);
        cursor_ctl(-2);
    }
    if (!far_is_null(ds_far(0x0792))) {
        arena_pop(1);
        DSW(0x0794) = DSW(0x0792) = 0;
    }
    arena_pop(1);
    if (p.off != q.off || p.seg != q.seg) arena_pop(1);
}

/* 0f38:3c19: a rejected part goes away: the screen above its place is restored from the bay bitmap
 * (the second argument of the original is unused) */
static void bay_part_out_anim(s16 pic)
{
    s16 x, y;
    bay_part_pos(&x, &y);                         /* [bp-20h], [bp-24h] */
    y = (s16)(y - 0x14);
    cursor_ctl(-3);
    FarPtr p = pic_get(pic, 2);
    if (pic != 0x434 && pic != 0x435 && pic != 0x436) y = (s16)(y - desc_h(p));
    s16 w = (s16)(desc_w(p) + 8), h = (s16)desc_h(p);
    Rect rc = { w, h, x, y, (s16)(x + 0x60), (s16)(y + 9) };
    x = (s16)(x + 0x60);
    y = (s16)(y + 9);
    drv_blit(bay_bg(), g_back(), &rc, 1);
    arena_pop(1);
    cursor_ctl(-1);
    if (DSW(DS_g_mirror) != 0) page_copy_rect(g_back(), g_front(), x, y, (s16)(x + w), (s16)(y + h));
}

/* 0f38:3d1c: the engine connector (0x446 disconnected / 0x447 connected; positions DS:071A) */
static void bay_connector_draw(void)
{
    s16 k = (s16)(bay_engine_idx() * 2);
    s16 draw_i, erase_i;                          /* [bp-1Ch], [bp-20h] */
    if (DSW(CONN_PIC) == 0x446) { draw_i = (s16)(k + 1); erase_i = k; }
    else { draw_i = k; erase_i = (s16)(k + 1); }
    cursor_ctl(-3);
    s16 ex = DSS((u16)(0x071A + 4 * erase_i));
    s16 ey = DSS((u16)(0x071C + 4 * erase_i));
    FarPtr bg = bay_bg();
    Rect rc = { 0x20, (s16)(desc_h(bg) - ey - 4), ex, ey, (s16)(ex + 0x60), (s16)(ey + 9) };
    drv_blit(bg, g_back(), &rc, 1);
    s16 dy = (s16)(DSS((u16)(0x071C + 4 * draw_i)) + 9);
    s16 dx = (s16)(DSS((u16)(0x071A + 4 * draw_i)) + 0x60);
    pic_draw_masked(g_back(), DSS(CONN_PIC), (u8)dx, (u8)dy, 0);
    cursor_ctl(-1);
    if (DSW(DS_g_mirror) != 0) {
        s16 x0 = dx > rc.dx ? rc.dx : dx;
        s16 y0 = dy > rc.dy ? rc.dy : dy;
        page_copy_rect(g_back(), g_front(), x0, y0, (s16)(rc.w + dx + 0x18), (s16)(rc.h + dy + 0x18));
    }
}

/* 0f38:3e35: rebuilds the bay bitmap (LIB2 #45) with the fitted parts, bottom-up */
static void bay_draw_car(u16 car)
{
    s16 save_ctx = DSS(BAY_CTX);
    DSB(ANIMATE) = 0;
    if (far_is_null(bay_bg())) ds_far_wr(BAY_BG, pic_get(0x42D, 1));
    else pic_decode(0x42D, bay_bg());
    s16 pic = slot_pic(0);
    if (pic != 0) {
        DSW(BAY_CTX) = 0;
        bay_part_in_anim(pic, 0);
        DSB(0x078C) = pic == 0x434 ? 4 : 5;
        pic = slot_pic(1);
        if (pic != 0) {
            DSW(BAY_CTX) = 1;
            bay_part_in_anim(pic, 0);
            s16 si = DSC((u16)(0x078C + grade_of(DSW((u16)(car + CR_MANIFOLD)))));
            for (u16 i = 0; i < 3; i++) {
                DSW(BAY_CTX) = (u16)si;
                si++;
                s16 cp = DSS((u16)(SLOT_PIC + 4 + 2 * i));
                if (cp != 0) bay_part_in_anim(cp, 0);
            }
        }
    }
    DSB(ANIMATE) = 1;
    DSW(BAY_CTX) = (u16)save_ctx;
}

/* 0f38:3f2d: the bay bitmap to the screen with the bolts and the connector; own: the tool bar
 * (LIB2 #198 = 0x4C6) below it. ctx / side: DS tables of the bolts' contexts and sides. */
static void bay_draw_parts(u16 ctx_tbl, u16 side_tbl, s16 own)
{
    s16 save_ctx = DSS(BAY_CTX), save_side = DSS(BAY_SIDE);
    u16 m = DSW(DS_g_mirror);
    DSW(DS_g_mirror) = 0;
    FarPtr bg = bay_bg();
    Rect rc = { (s16)desc_w(bg), (s16)desc_h(bg), 0, 0, 0x60, 9 };
    drv_blit(bg, g_back(), &rc, 1);
    for (u16 i = 0; i < 9; i++) {
        s16 bp = DSS((u16)(BOLT_PIC + 2 * i));
        if (bp == 0) continue;
        DSW(BAY_CTX) = DSW((u16)(ctx_tbl + 2 * i));
        DSW(BAY_SIDE) = DSW((u16)(side_tbl + 2 * i));
        bay_bolt_draw(bp);
    }
    if (DSW(CONN_PIC) != 0) bay_connector_draw();
    if (own != 0) {
        show_picture_at(0x4C6, 0x60, 0x94, -1);
        rc.h = 0xAF;
    }
    DSW(DS_g_mirror) = m;
    if (m != 0) page_copy_rect(g_back(), g_front(), 0x60, 9, (s16)(0x60 + rc.w - 1), (s16)(9 + rc.h - 1));
    DSW(BAY_CTX) = (u16)save_ctx;
    DSW(BAY_SIDE) = (u16)save_side;
}

/* 0f38:404b (near): inserts record `rec` into the n records at `base`, sorted by x0 then y0 */
static void hot_insert(u16 base, u16 rec, s16 n)
{
    u16 si = base;
    s16 i = 0;
    for (; i < n; i++, si = (u16)(si + UI_REC_SIZE)) {
        if (DSS((u16)(rec + UI_R_X0)) < DSS((u16)(si + UI_R_X0))) break;
        if (DSS((u16)(rec + UI_R_X0)) > DSS((u16)(si + UI_R_X0))) continue;
        if (DSS((u16)(rec + UI_R_Y0)) < DSS((u16)(si + UI_R_Y0))) break;
    }
    if (i < n) memmove(mp(DGROUP, (u16)(si + UI_REC_SIZE)), mp(DGROUP, si), (size_t)(u16)((n - i) * UI_REC_SIZE));
    memcpy(mp(DGROUP, si), mp(DGROUP, rec), UI_REC_SIZE);
}

/* 0f38:40ca: builds the hot spots of screen 0x15 at DS:184C: the 3 fixed records of DS:1948, one per
 * bolt of a fitted layer (code 1..9) and the connector (13), sorted; type 9 ends the list. */
static void bay_draw_bolts(u16 ctx_tbl, u16 side_tbl)
{
    s16 save_ctx = DSS(BAY_CTX), save_side = DSS(BAY_SIDE);
    s16 n = 3;
    memmove(mp(DGROUP, 0x184C), mp(DGROUP, 0x1948), 3 * UI_REC_SIZE);
    DSW(0x079A) = DSW(0x2BA5);
    s16 k = 0, layer = 0;
    for (s16 si = 0; si < 9; si++) {
        if (slot_pic(layer) != 0) {
            DSW(BAY_CTX) = DSW((u16)(ctx_tbl + 2 * si));
            DSW(BAY_SIDE) = DSW((u16)(side_tbl + 2 * si));
            s16 x, y;
            bay_bolt_pos(&x, &y);
            x = (s16)(x + 0x60);
            y = (s16)(y + 9);
            DSW(0x079C) = (u16)(si + 1);
            DSW(0x079E) = (u16)(x - 2);
            DSW(0x07A2) = (u16)(x - 2 + 5);
            DSW(0x07A4) = (u16)(y + 2);
            DSW(0x07A0) = (u16)(y + 2 - 0xF);
            hot_insert(0x184C, 0x0796, n);
            n++;
        }
        k++;
        if (DSS((u16)(BOLTS_PER + 2 * layer)) == k) { layer++; k = 0; }
    }
    if (slot_pic(0) != 0) {
        DSW(0x079A) = DSW(0x2BA7);
        DSW(0x079C) = 0x0D;
        s16 e = (s16)(bay_engine_idx() * 2 + (DSW(CONN_PIC) == 0x446 ? 1 : 0));
        DSW(0x079E) = (u16)(DSS((u16)(0x071A + 4 * e)) + 0x5B);
        DSW(0x07A2) = (u16)(DSS(0x079E) + 0x0A);
        DSW(0x07A0) = (u16)(DSS((u16)(0x071C + 4 * e)) + 2);
        DSW(0x07A4) = (u16)(DSS(0x07A0) + 0x0F);
        hot_insert(0x184C, 0x0796, n);
        n++;
    }
    DSW((u16)(0x184C + UI_REC_SIZE * n)) = 9;
    DSW(BAY_CTX) = (u16)save_ctx;
    DSW(BAY_SIDE) = (u16)save_side;
    click_clear();
}

/* ============================================================================== 0000:7aa4 */

void change_transmission(void)
{
    u16 node[142];
    s8 wear[142];
    s16 st[3];                                    /* [1], [2]: 0 out, 1 tightening, 2 loosening */
    u16 car = DSW(G_CUR_CAR);
    if (car == 0) { msg_box(0xEFA); return; }
    for (s16 i = 0; i < 2; i++) {
        s8 v = DSC((u16)(car + CR_TRANS_BOLT + i));
        st[i + 1] = v == 0 ? 0 : (v == 1 ? 1 : 2);
        DSW(UI_REC(200 + i)) = 0x0B;
    }
    u16 tr = DSW((u16)(car + CR_TRANS));
    DSW(DS_trans_pic) = tr == 0 ? 0 : DSW((u16)(TRANS_PICS + 2 * grade_of(tr)));
    DSW(DS_g_mirror) = DSW(DS_trans_pic) == 0 ? 1 : 0;
    show_picture_at(0x42E, 0x20, 10, -1);         /* LIB2 #46 */
    DSW(DS_g_mirror) = 1;
    trans_screen_draw(DSS(DS_trans_pic), (u16)(car + CR_TRANS_BOLT));
    ui_push(0x14);
    for (;;) {
        s16 b;
        do b = ui_wait(3000); while (b == 0);
        if (b == -1) break;                       /* Done */
        if (b == -2) {                            /* Parts */
            spares_collect(1, node, wear, 0);
            for (;;) {
                s16 sel;
                s16 r = list_box(4, 1, NULL, (s16 *)mp(DGROUP, G_LIST_BUF), wear, NULL);
                if (r == -4) sel = 0;
                else if (r == -3) {
                    if (DSW((u16)(car + CR_TRANS)) != 0) { msg_box(0x1875); sel = 0; }   /* Where will you put it */
                    else sel = list_selected();
                } else if (r == 0x3EA) { msg_box(0xF72); sel = 0; }   /* no spare transmissions */
                else sel = r;
                if (sel == 0) break;
                s16 old = DSS(DS_trans_pic);
                s16 t = DSS((u16)(node[sel] + PT_TYPE));
                DSW(DS_trans_pic) = DSW((u16)(TRANS_PICS + 2 * parts_grade(t)));
                if ((DSB((u16)(MODELS_TAB + 10 * DSS((u16)(car + CR_MODEL)) + 4)) & parts_make(t) & 0x0F) != 0) {
                    part_install(spare_unlink(node[sel]), (u16)(car + CR_TRANS));
                    cursor_ctl(-4);
                    loop_wrench(1);
                    trans_slide_anim(DSS(DS_trans_pic), 0);
                    loop_wrench(0);
                    fx_chirp_lo();
                    clock_add(0x666);
                    for (s16 i = 1; i >= 0; i--) {
                        st[i + 1] = 1;
                        DSB((u16)(car + CR_TRANS_BOLT + i)) = 1;
                        trans_bolt_draw(1, i);
                        fx_chirp_hi();
                        DSW(UI_REC(200 + i)) = 0x0B;
                    }
                    click_clear();
                    cursor_ctl(-2);
                    break;
                }
                trans_slide_anim(DSS(DS_trans_pic), 1);
                msg_box(0x10CD);                  /* That won't fit! */
                trans_remove_anim();
                clock_add(0x888);
                DSW(DS_trans_pic) = (u16)old;
            }
            continue;
        }
        if (b > 0 && b < 3 && DSW(DS_trans_pic) != 0) {
            s16 i = (s16)(b - 1);
            u16 bolt = (u16)(car + CR_TRANS_BOLT + i);
            if (st[b] == 2) {                     /* loosen */
                DSB(bolt) = (u8)(DSC(bolt) - 1);
                if (DSC(bolt) < 2) {
                    DSB(bolt) = 0;
                    st[b] = 0;
                    trans_bolt_remove_anim(i);
                    fx_chirp_hi();
                    click_clear();
                } else {
                    trans_bolt_draw(DSC(bolt), i);
                    fx_thud();
                }
                if (st[1] == 0 && st[2] == 0) {   /* both out: the transmission drops out */
                    trans_remove_anim();
                    clock_add(0x666);
                    DSW(DS_trans_pic) = 0;
                    DSB((u16)(car + CR_TRANS_BOLT + 1)) = 0;
                    DSB((u16)(car + CR_TRANS_BOLT)) = 0;
                    part_uninstall((u16)(car + CR_TRANS));
                    click_clear();
                }
            } else if (st[b] == 1) {              /* tighten */
                if (DSC(bolt) == 3) st[b] = 2;
                else {
                    DSB(bolt) = (u8)(DSC(bolt) + 1);
                    trans_bolt_draw(DSC(bolt), i);
                    fx_thud();
                }
            } else {                              /* put a bolt in */
                cursor_ctl(-4);
                DSB(bolt) = 1;
                trans_bolt_draw(1, i);
                fx_chirp_hi();
                cursor_ctl(-2);
                st[i + 1] = 1;
            }
        }
    }
    ui_pop(1);
}

/* ============================================================================== 0000:7fb1 */

void ignition_tune(u16 car)
{
    ui_push(0x1C);                                /* -7 Retard, -10 Advance, -1 done */
    Rect rc = { 8, 0x0B, 0x30, 0, 0xB0, 0 };
    show_picture_at(0x42F, 0x60, 9, -1);          /* LIB2 #47 timing light */
    s16 slot = anim_start(8);
    u16 a = ANIM_SLOT(slot);
    u32 t0 = DSL(DS_bios_ticks);
    ignition_tune_begin();
    s16 b;
    do {
        ignition_tune_gate();
        s8 ign = DSC((u16)(car + CR_IGNITION));
        ignition_tune_tone(ign);
        s16 y = (s16)(ign * 2 + 0x4B);
        rc.dy = y;
        cursor_ctl(-4);
        drv_blit(far_rd(DGROUP, (u16)(a + 8)), g_back(), &rc, 1);
        page_copy_rect(g_back(), g_front(), 0xB0, y, 0xB7, (s16)(ign * 2 + 0x55));
        cursor_ctl(-2);
        while ((b = ui_wait(0)) == 0) ignition_tune_gate();
        u16 ig = (u16)(car + CR_IGNITION);
        if (b == -10) {
            DSB(ig) = (u8)(DSC(ig) + 1);
            if (DSC(ig) > 4) DSB(ig) = 4;
        } else if (b == -7) {
            DSB(ig) = (u8)(DSC(ig) - 1);
            if (DSC(ig) < -8) DSB(ig) = (u8)-8;
        }
    } while (b != -1);
    ignition_tune_end();
    anim_stop(8);
    u16 dt = (u16)((u16)(DSL(DS_bios_ticks) - t0) * 3);
    if (dt > 0x444) dt = 0x444;
    clock_add(dt);
    ui_pop(1);
}

/* ============================================================================== 0000:81ab */

void engine_bay(u16 car)
{
    u16 node[142];
    s8 wear[142];
    s16 st[10];                                   /* [1..9]: 0 out, 1 tightening, 2 loosening */
    if (car == 0) { msg_box(0xEFA); return; }
    for (s16 i = 1; i < 10; i++) {
        s8 v = DSC((u16)(car + CR_BAY_BOLT + i - 1));
        if (v == 0) { st[i] = 0; DSW((u16)(BOLT_PIC + 2 * (i - 1))) = 0; }
        else { st[i] = v == 1 ? 1 : 2; DSW((u16)(BOLT_PIC + 2 * (i - 1))) = DSW((u16)(BOLT_PICS + 2 * v)); }
    }
    s8 link = DSC((u16)(car + CR_ENGINE_LINK));
    DSW(CONN_PIC) = link == 0 ? 0 : (link < 1 ? 0x446 : 0x447);
    if (DSW((u16)(car + CR_ENGINE)) != 0)
        DSW(DS_bay_block_pic) = grade_of(DSW((u16)(car + CR_ENGINE))) == 0 ? 0x438 : 0x439;
    for (s16 s = 0; s < 5; s++) {
        u16 p = DSW((u16)(car + CR_ENGINE + 2 * s));
        DSW((u16)(SLOT_PIC + 2 * s)) = p == 0 ? 0 : DSW((u16)(BAY_PICS + 10 * s + 2 * grade_of(p)));
    }
    DSW(BAY_LAYOUT) = slot_pic(0) == 0x434 ? 4 : 5;
    s16 layers = (s16)(5 - (slot_pic(0) == 0) - (slot_pic(1) == 0) - (slot_pic(2) == 0) -
                       (slot_pic(3) == 0) - (slot_pic(4) == 0));
    DSW((u16)(BAY_LAYOUT + 2)) = DSW(BAY_LAYOUT);
    s16 max_layers;                               /* [bp-1BEh] */
    s16 mg = 0;                                   /* [bp-12Eh] PORT: uninitialised without a manifold */
    u16 man = DSW((u16)(car + CR_MANIFOLD));
    if (man == 0) max_layers = 5;
    else {
        mg = grade_of(man);
        max_layers = (s16)(DSS((u16)(CARBS_PER + 2 * mg)) + 2);
        memmove(mp(DGROUP, LAYOUT_CUR), mp(DGROUP, (u16)(BAY_LAYOUT + 12 * mg)), 12);
    }
    cursor_ctl(-4);
    bay_draw_car(car);
    bay_draw_parts(0x5186, 0x519A, car == DSW(G_CUR_CAR));
    cursor_ctl(-2);
    if (car != DSW(G_CUR_CAR)) return;            /* an opponent's engine (race): view only */
    bay_draw_bolts(0x5186, 0x519A);
    ui_push(0x15);
    for (;;) {
        s16 b;
        u16 new_bolt;
        do { b = ui_wait(3000); new_bolt = DSW(0x49CC); } while (b == 0);
        s16 k126 = b;                             /* [bp-126h] */
        if (b == -3) {                            /* Tune */
            if (car_runnable(car, 0) == 0) msg_box(0xEA1);   /* Can't tune that mess, klutz! */
            else {
                ignition_tune(car);
                cursor_ctl(-4);
                bay_draw_parts(0x5186, 0x519A, 1);
                cursor_ctl(-2);
            }
            continue;
        }
        if (b == -2) {                            /* Parts: fit the next layer */
            s16 cat = DSS((u16)(LAYER_CAT + 2 * layers));
            spares_collect(cat, node, wear, 0);
            for (;;) {
                s16 sel = list_box(4, 1, NULL, (s16 *)mp(DGROUP, G_LIST_BUF), wear, NULL);
                if (sel == -4) sel = 0;
                else if (sel == -3) sel = list_selected();
                else if (sel == 0x3EA) {
                    if (cat == 0) msg_box(0xF97);
                    if (cat == 3) msg_box(0xFD7);
                    if (cat == 2) msg_box(0xFB5);
                    sel = 0;
                }
                if (sel == 0) break;
                if (max_layers <= layers) { msg_box(0x1875); break; }   /* Where will you put it, speedy? */
                s16 slot = layers;                /* [bp-120h] */
                s16 first_bolt;                   /* [bp-12Ch] */
                bool fits;                        /* [bp-128h] */
                s16 t = DSS((u16)(node[sel] + PT_TYPE));
                u16 spec = model_spec(DSS((u16)(car + CR_MODEL)));
                if (layers == 0) {                /* engine */
                    fits = ((s16)(s8)parts_make(t) & spec & 0x0F) != 0;
                    DSW(BAY_CTX) = 0;
                    first_bolt = 0;
                    DSB((u16)(car + CR_ENGINE_LINK)) = 0xFF;
                    DSW(CONN_PIC) = 0x446;
                    clock_add(fits ? 0x666 : 0x888);
                } else if (layers == 1) {         /* manifold */
                    fits = false;
                    if ((parts_make(t) & (u8)spec & 0x0F) != 0) {
                        mg = parts_grade(t);
                        fits = DSB((u16)(0x4978 + parts_grade(t) + grade_of(DSW((u16)(car + CR_ENGINE))) * 5)) != 0;
                    }
                    DSW(BAY_CTX) = 1;
                    first_bolt = 1;
                    max_layers = (s16)(DSS((u16)(CARBS_PER + 2 * parts_grade(t))) + 2);
                    memmove(mp(DGROUP, LAYOUT_CUR), mp(DGROUP, (u16)(BAY_LAYOUT + 12 * mg)), 12);
                    clock_add(fits ? 0x333 : 0x444);
                } else if (layers <= 4) {         /* carburettor */
                    fits = false;
                    if ((parts_make(t) & (u8)spec & 0x0F) != 0) {
                        mg = grade_of(DSW((u16)(car + CR_MANIFOLD)));
                        fits = DSB((u16)(0x4988 + parts_grade(t) + mg * 3)) != 0;
                    }
                    clock_add(fits ? 0x199 : 0x222);
                    first_bolt = 3;
                    slot = 2;
                    s16 ctx = DSS((u16)(BAY_LAYOUT + 12 * mg));
                    DSW(BAY_CTX) = (u16)ctx;
                    bool back = false;
                    switch (ctx) {
                    case 4: case 5: case 6:
                        break;
                    case 7:
                        if (DSW((u16)(car + CR_CARB)) != 0) {
                            if (DSW((u16)(car + CR_CARB + 2)) == 0) { DSW(BAY_CTX) = 8; first_bolt = 5; slot = 3; }
                            else { DSW(BAY_CTX) = 9; first_bolt = 7; slot = 4; }
                        }
                        break;
                    case 10: case 12:
                        if (DSW((u16)(car + CR_CARB)) != 0) { DSW(BAY_CTX) = (u16)(ctx + 1); first_bolt = 5; slot = 3; }
                        break;
                    default:
                        back = true;                  /* back to the list */
                        break;
                    }
                    if (back) continue;
                } else {
                    continue;
                }
                s16 pic = DSS((u16)(BAY_PICS + 10 * slot + 2 * parts_grade(t)));
                DSW((u16)(SLOT_PIC + 2 * slot)) = (u16)pic;
                if (!fits) {
                    loop_wrench(1);
                    bay_part_in_anim(pic, 1);
                    loop_wrench(0);
                    fx_chirp_lo();
                    msg_box(0x10CD);              /* Pay attention, dummy! That won't fit! */
                    loop_wrench(1);
                    bay_part_out_anim(pic);
                    loop_wrench(0);
                    DSW((u16)(SLOT_PIC + 2 * slot)) = 0;
                    if (slot == 0) { DSB((u16)(car + CR_ENGINE_LINK)) = 0; DSW(CONN_PIC) = 0; }
                    continue;                     /* the part stays a spare */
                }
                part_install(spare_unlink(node[sel]), (u16)(car + CR_ENGINE + 2 * slot));
                cursor_ctl(-4);
                loop_wrench(1);
                bay_part_in_anim(slot_pic(slot), 0);
                loop_wrench(0);
                fx_chirp_lo();
                if (slot == 0) {
                    if (pic == 0x434) { DSW(DS_bay_block_pic) = 0x438; DSW(BAY_LAYOUT) = 4; }
                    else { DSW(DS_bay_block_pic) = 0x439; DSW(BAY_LAYOUT) = 5; }
                    DSW(CONN_PIC) = 0x446;
                    DSW((u16)(BAY_LAYOUT + 2)) = DSW(BAY_LAYOUT);
                    bay_connector_draw();
                }
                for (s16 k = DSS((u16)(BOLTS_PER + 2 * layers)); --k >= 0;) {   /* loose bolts */
                    s16 i = (s16)(first_bolt + k);
                    DSW(BAY_SIDE) = (u16)k;
                    DSW((u16)(BOLT_PIC + 2 * i)) = DSW(0x49CC);
                    bay_bolt_draw(DSS(0x49CC));
                    fx_chirp_hi();
                    st[i + 1] = 1;
                    DSB((u16)(car + CR_BAY_BOLT + i)) = 1;
                }
                bay_draw_bolts(0x5186, 0x519A);
                cursor_ctl(-2);
                layers++;
                DSB((u16)(car + CR_IGNITION)) = (u8)(rnd(0x0D) - 8);
                break;
            }
            continue;
        }
        if (b == -1) {                            /* Done */
            ui_pop(1);
            DSW(0x8234) = DSW(0x8232) = 0;
            arena_reset_stacks();
            return;
        }
        bool check = false;
        if (b == 0x0D) {                          /* the engine connector */
            u16 lk = (u16)(car + CR_ENGINE_LINK);
            DSB(lk) = (u8)-DSC(lk);
            if (DSC(lk) == 0) continue;
            if (DSC(lk) < 1) {
                DSW(CONN_PIC) = 0x446;
                bay_connector_draw();
                clock_add(0x6D);
                DSW(BAY_CTX) = 0;
                bay_draw_bolts(0x5186, 0x519A);
                check = true;
            } else {
                DSW(CONN_PIC) = 0x447;
                bay_connector_draw();
                clock_add(0x6D);
                bay_draw_bolts(0x5186, 0x519A);
                continue;
            }
        } else {
            if (b < 1 || b > 9) continue;
            s16 L = 0, sum = 0;                   /* which layer does bolt b hold? */
            for (u16 p = BOLTS_PER; L < 5 && (sum = (s16)(sum + DSS(p))) < b; p = (u16)(p + 2)) L++;
            if (slot_pic(L) == 0) continue;
            clock_add(0x36);
            u16 bolt = (u16)(car + CR_BAY_BOLT + b - 1);
            if (st[b] == 2) {                     /* loosen */
                DSW(BAY_CTX) = DSW((u16)(BOLT_CTX + 2 * b));
                DSW(BAY_SIDE) = DSW((u16)(BOLT_SIDE + 2 * b));
                DSB(bolt) = (u8)(DSC(bolt) - 1);
                if (DSC(bolt) < 2) {
                    DSB(bolt) = 0;
                    st[b] = 0;
                    DSW((u16)(BOLT_PIC + 2 * (b - 1))) = 0;
                    bay_bolt_remove_anim();
                    fx_chirp_hi();
                } else {
                    u16 v = DSW((u16)(BOLT_PICS + 2 * DSC(bolt)));
                    DSW((u16)(BOLT_PIC + 2 * (b - 1))) = v;
                    bay_bolt_draw((s16)v);
                    fx_thud();
                }
                check = true;
            } else if (st[b] == 1) {              /* tighten */
                if (DSC(bolt) == 3) st[b] = 2;
                else {
                    DSB(bolt) = (u8)(DSC(bolt) + 1);
                    DSW((u16)(BOLT_PIC + 2 * (b - 1))) = DSW((u16)(BOLT_PICS + 2 * DSC(bolt)));
                    DSW(BAY_CTX) = DSW((u16)(BOLT_CTX + 2 * b));
                    DSW(BAY_SIDE) = DSW((u16)(BOLT_SIDE + 2 * b));
                    bay_bolt_draw(DSS((u16)(BOLT_PIC + 2 * (b - 1))));
                    fx_thud();
                }
            } else {                              /* insert a bolt */
                DSW(BAY_CTX) = DSW((u16)(BOLT_CTX + 2 * b));
                DSW(BAY_SIDE) = DSW((u16)(BOLT_SIDE + 2 * b));
                DSW((u16)(BOLT_PIC + 2 * (b - 1))) = DSW(0x49CC);
                bay_bolt_draw((s16)new_bolt);
                fx_chirp_hi();
                st[b] = 1;
                DSB((u16)(car + CR_BAY_BOLT + k126 - 1)) = 1;
            }
        }
        if (!check) continue;
        /* removal: runs again while a removed part lets the one below go too */
        s16 cascade;
        do {
            s16 slot = -1;
            cascade = 0;
            s16 ctx = DSS(BAY_CTX);
            if (ctx == 0) {                       /* engine: last layer, disconnected, bolt 1 out */
                if (layers == 1 && DSC((u16)(car + CR_ENGINE_LINK)) == -1) {
                    k126 = 1;
                    while (st[k126] == 0 && ++k126 < 2) ;
                    if (k126 == 2) {
                        slot = 0;
                        DSB((u16)(car + CR_ENGINE_LINK)) = 0;
                        DSW(CONN_PIC) = 0;
                        clock_add(0x666);
                    }
                }
            } else if (ctx == 1) {                /* manifold: bolts 2 and 3 out, no carbs */
                if (layers == 2) {
                    k126 = 2;
                    while (st[k126] == 0 && ++k126 < 4) ;
                    if (k126 == 4) {
                        slot = 1;
                        cascade = 1;
                        clock_add(0x333);
                    }
                }
            } else {                              /* a carb pair */
                s16 first = DSS((u16)(BOLT_SIDE + 2 * k126)) == 1 ? (s16)(k126 - 1) : k126;
                if (st[first] == 0 && st[first + 1] == 0) {
                    slot = (s16)((first - 4) / 2 + 2);
                    if (layers == 3) cascade = 2;
                    clock_add(0x199);
                }
            }
            if (slot >= 0) {
                layers--;
                part_uninstall((u16)(car + CR_ENGINE + 2 * slot));
                cursor_ctl(-4);
                DSW((u16)(SLOT_PIC + 2 * slot)) = 0;
                if (cascade != 0) DSW(BAY_CTX) = (u16)(cascade - 1);
                bay_draw_car(car);
                bay_draw_parts(0x5186, 0x519A, 1);
                bay_draw_bolts(0x5186, 0x519A);
                DSB((u16)(car + CR_IGNITION)) = (u8)(rnd(0x0D) - 8);
                cursor_ctl(-2);
            }
        } while (cascade != 0);
    }
}
