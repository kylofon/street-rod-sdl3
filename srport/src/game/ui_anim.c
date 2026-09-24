/* The 0f38 UI toolkit: the animation-script interpreter 0f38:19ec - 1e93 (timing light, gas-station
 * pump, blinking lamps). Ported from the disassembly.
 *
 * Slot i (5 slots): DS:7568 + 0x2C*i = { +0 animation id (-1 free), +2 script position, +4 pending
 * tick timers, +8 far picture, +C far (cleared by anim_stop), +10 Rect {w, h, sx, sy, dx, dy} }.
 * Script steps, 6 bytes at DS:58F0 + 6*k: { u8 op, u8 sub, i16 a, i16 b }. Animation id n starts at
 * step t = W[DS:59F8 + 2n]; step t is a header whose `a` holds the running slot (-1 = none).
 * Ops: 2 blit, 4 masked blit (after the sub-operand update), 5 lamp blink (anim 2 only; others stop),
 * 6 stop, 7 jump to step a, 8 picture a, 9 sub-operand update only, 10 size (w, h) = (a, b),
 * 11 wait a ticks (tq_schedule_call -> anim_tick), next position b (0 = the following step),
 * 12 show the pointer + mirror the rect, 13 copy the view background into the picture in tiles;
 * sub-operand: 1 source (a, b), 2 destination (a, b), 3 both. */
#include "game/ui.h"

#include "platform/platform.h"
#include "platform/video.h"
#include "sound/sound.h"

#define A_CUR   0x6C3A          /* near: slot in use */
#define A_STEP  0x6C38          /* near: script step */
#define A_RECT  0x6C3C          /* near: slot + 0x10 */
#define A_BLINK 0x6C3E          /* lamp blink counter */
#define SCRIPT  0x58F0
#define STARTS  0x59F8

static inline u16 script_hdr(s16 id) { return (u16)(DSW((u16)(STARTS + 2 * id)) * 6 + SCRIPT); }
static inline Rect *ds_rect(u16 off) { return (Rect *)mp(DGROUP, off); }

/* 0f38:1a5f */
static void anim_set_params(void)
{
    u16 r = DSW(A_RECT), p = DSW(A_STEP);
    u8 sub = DSB((u16)(p + 1));
    if (sub == 0) return;
    if (sub == 1) {
        DSW((u16)(r + 4)) = DSW((u16)(p + 2));
        DSW((u16)(r + 6)) = DSW((u16)(p + 4));
        return;
    }
    if (sub != 2) {
        if (sub != 3) return;
        DSW((u16)(r + 4)) = DSW((u16)(p + 2));
        DSW((u16)(r + 6)) = DSW((u16)(p + 4));
    }
    DSW((u16)(r + 8)) = DSW((u16)(p + 2));
    DSW((u16)(r + 10)) = DSW((u16)(p + 4));
}

/* 0f38:1e93: the blinking lamps of animation 2 (two 2-px lines at (0x33, 0x6F) / (0x30, 0x6F) in
 * colour 4 / 0); returns non-zero to stop the animation. */
static s16 anim_blink_led(u16 a, u16 p)
{
    s16 id = DSS(a);
    if (id != 2) return id;
    if (DSW((u16)(p + 2)) == 0) {
        u16 v = (u16)(DSW(A_BLINK) - 1);
        if (DSW(A_BLINK) == 0) {
            DSW(A_BLINK) = v;
            loop_tick(0);
            return 1;
        }
        DSB(DS_g_line_col) = (v & 1) ? 4 : 0;
        DSW(A_BLINK) = v;
        hline(g_back(), 0x33, 0x6F, 2);
        if ((DSW(A_BLINK) & 3) == 0) {
            DSB(DS_g_line_col) = (DSW(A_BLINK) & 4) ? 4 : 0;
            hline(g_back(), 0x30, 0x6F, 2);
        }
    } else {
        DSW(A_BLINK) = DSW((u16)(p + 2));
        loop_tick(1);
    }
    return 0;
}

/* 0f38:1abd (near) */
static void anim_run(s16 slot)
{
    s16 hidden = 1;                               /* [bp-16h] */
    cursor_ctl(-3);
    u16 a = (u16)(0x2C * slot + ANIM_SLOTS);
    DSW(A_CUR) = a;
    s16 n = DSS((u16)(a + 2));                    /* [bp-24h] */
    DSW(A_STEP) = (u16)(n * 6 + SCRIPT);
    DSW(A_RECT) = (u16)(a + 0x10);
    for (; n >= 0; n++) {
        u16 p = DSW(A_STEP);
        u16 r = DSW(A_RECT);
        a = DSW(A_CUR);
        FarPtr pic = far_rd(DGROUP, (u16)(a + 8));
        switch ((s8)DSB(p)) {
        case 2:
            anim_set_params();
            drv_blit(pic, g_back(), ds_rect(DSW(A_RECT)), 1);
            break;
        case 4:
            anim_set_params();
            drv_blit_masked(pic, g_back(), ds_rect(DSW(A_RECT)), 1);
            break;
        case 5:
            if (anim_blink_led(a, p) == 0) break;
            /* fall through */
        case 6:
            anim_stop(DSS(DSW(A_CUR)));
            cursor_ctl(-1);
            return;
        case 7:
            DSW(A_STEP) = (u16)(DSS((u16)(p + 2)) * 6 + SCRIPT);
            continue;                             /* no +6 (1c87: only n++) */
        case 8:
            far_wr(DGROUP, (u16)(a + 8), pic_get(DSS((u16)(p + 2)), 2));
            break;
        case 9:
            anim_set_params();
            break;
        case 10:
            DSW(r) = DSW((u16)(p + 2));
            DSW((u16)(r + 2)) = DSW((u16)(p + 4));
            break;
        case 11:
            DSW((u16)(a + 2)) = DSW((u16)(p + 4));
            if (DSW((u16)(a + 2)) == 0) {
                n++;
                DSW((u16)(a + 2)) = (u16)n;
            }
            tq_schedule_call(DSS(a), DSS((u16)(p + 2)));
            DSW((u16)(a + 4)) = (u16)(DSW((u16)(a + 4)) + 1);
            if (hidden != 0) cursor_ctl(-1);
            if (DSB((u16)(DSW(A_STEP) + 1)) == 4 && DSW(DS_g_mirror) != 0) {
                r = DSW(A_RECT);
                page_copy_rect(g_back(), g_front(), DSS((u16)(r + 8)), DSS((u16)(r + 10)),
                               (s16)(DSS((u16)(r + 8)) + DSS(r) - 1), (s16)(DSS((u16)(r + 10)) + DSS((u16)(r + 2)) - 1));
            }
            return;
        case 12:
            cursor_ctl(-1);
            hidden = 0;
            if (DSW(DS_g_mirror) != 0) {
                page_copy_rect(g_back(), g_front(), DSS((u16)(r + 8)), DSS((u16)(r + 10)),
                               (s16)(DSS((u16)(r + 8)) + DSS(r) - 1), (s16)(DSS((u16)(r + 10)) + DSS((u16)(r + 2)) - 1));
            }
            break;
        case 13: {
            s16 c[4];                             /* [bp-22h] x0 y0 x1 y1 */
            if (rect_clip_to_view(DSS((u16)(r + 8)), DSS((u16)(r + 10)), DSS(r), DSS((u16)(r + 2)), c) == NULL) break;
            Rect t;                               /* [bp-14h] */
            t.w = (s16)(c[2] - c[0]);
            t.h = (s16)(c[3] - c[1] + 1);
            t.sx = (s16)(c[0] - DSS(DS_car_x));
            t.sy = (s16)(c[1] - DSS(DS_car_y));
            s16 ox = (s16)(c[0] - DSS((u16)(r + 8)));   /* [bp-26h] */
            s16 oy = (s16)(c[1] - DSS((u16)(r + 10)));  /* [bp-28h] */
            for (s16 yy = 0; (s16)desc_h(pic) > yy; yy = (s16)(yy + DSS((u16)(DSW(A_RECT) + 2)))) {
                for (s16 si = 0; (s16)desc_w(pic) > si; si = (s16)(si + DSS(DSW(A_RECT)))) {
                    t.dx = (s16)(ox + si);
                    t.dy = (s16)(oy + yy);
                    drv_blit_masked(ds_far(0x825A), pic, &t, 1);
                    pic = far_rd(DGROUP, (u16)(DSW(A_CUR) + 8));
                }
            }
            break;
        }
        default:                                  /* 1, 3 and anything else: nothing */
            break;
        }
        DSW(A_STEP) = (u16)(DSW(A_STEP) + 6);     /* 1c82 */
    }
}

/* 0f38:19ec */
void anim_stop(s16 id)
{
    u16 h = script_hdr(id);
    s16 i = DSS((u16)(h + 2));
    u16 a = (u16)(i * 0x2C + ANIM_SLOTS);
    if (DSW((u16)(a + 8)) != 0 || DSW((u16)(a + 10)) != 0) { DSW((u16)(a + 10)) = 0; DSW((u16)(a + 8)) = 0; }
    if (DSW((u16)(a + 12)) != 0 || DSW((u16)(a + 14)) != 0) { DSW((u16)(a + 14)) = 0; DSW((u16)(a + 12)) = 0; }
    DSW((u16)(h + 2)) = 0xFFFF;
    DSW(a) = 0xFFFF;
    if (DSW((u16)(a + 4)) != 0) tq_cancel_call(id);
}

/* 0f38:1dc9 */
s16 anim_start(s16 id)
{
    s16 i = 0;
    do {
        u16 a = ANIM_SLOT(i);
        if (DSS(a) == -1) {
            s16 t = DSS((u16)(STARTS + 2 * id));
            DSW((u16)(t * 6 + SCRIPT + 2)) = (u16)i;
            DSW(a) = (u16)id;
            DSW((u16)(a + 2)) = (u16)(t + 1);
            DSW((u16)(a + 4)) = 0;
            anim_run(i);
            return i;
        }
        i++;
    } while (i < 5);
    return i;
}

/* 0f38:1e41 */
void anim_tick(s16 id)
{
    s16 i = DSS((u16)(script_hdr(id) + 2));
    u16 a = ANIM_SLOT(i);
    DSW((u16)(a + 4)) = (u16)(DSW((u16)(a + 4)) - 1);
    anim_run(i);
}

/* 0f38:1e72 */
s16 anim_active(s16 id)
{
    return DSS((u16)(script_hdr(id) + 2)) >= 0;
}
