/* Road renderer and movement along the road, code segment 2645 (port/spec/race_render.md): segment
 * pointers, the fixed-row projection (front 15 rows, mirror 8 rows), the per-frame step, the segment
 * advance / autopilot, the edge collision test, the page flip, sky / horizon / ground, road bands,
 * town walls, road-side shapes and pictures, overhead banners, the opponent sprite, the rear-view
 * mirror, the cockpit overlays, the drag start light, loading the drive-screen graphics and the
 * planar picture shrinker.
 *
 * Not here: the EGA span helpers 2645:0008 / 001d / 0034 / 00e7 (platform/blit.c: ega_setreset_on /
 * ega_setreset_off, and through the driver slots drv_hspan / drv_fill_rows), the track builder
 * (race_track.c) and the parked CGA / Tandy shrinkers 2645:55f8 / 5670 / 5885 (VGA path only: PORT,
 * road_load_graphics always uses pic_shrink_planar 2645:5407).
 *
 * State stays in mem[]: the far segment pointers DS:72EE / 8634 / 7D62 / 7880 / 7560, the row tables,
 * the near row pointers DS:6E08..6E1E and DS:6E28, the far flag pointers DS:6E20 / 6E24 / 8B82, the
 * clip rectangle DS:5E4E..5E54, the picture pointers (DS:8ACE[13], DS:7526[13], ...). Arithmetic is
 * 16-bit as the original's registers (imul low word, sar, idiv via idiv32_16). */
#include "game/race_int.h"
#include "game/race.h"
#include "platform/platform.h"
#include "platform/video.h"
#include "host.h"

/* ================================================================================ DGROUP names */

/* near pointers into the row tables for the current band, p = row, q = row - 1 (§4.8) */
#define RP_XL   0x6E08      /* DS_row_ptr_xl */
#define RP_XOL  0x6E0A
#define RP_YE   0x6E0C
#define RP_XOR  0x6E0E
#define RQ_XL   0x6E10
#define RQ_XOL  0x6E12
#define RQ_YE   0x6E14
#define RQ_XOR  0x6E16
#define RP_XR   0x6E18
#define RP_Y    0x6E1A
#define RQ_XR   0x6E1C
#define RQ_Y    0x6E1E

#define DS_cam_z_unused   0x7D3A    /* cleared by 185d, unused here */
#define DS_ai_start_a     0x54D4    /* AI start state (race.md) */
#define DS_ai_start_lane  0x8648
#define DS_ai_start_b     0x7D7E
#define DS_mir_y7         0x8646    /* = mir_y[7] */
#define DS_light_flag     0x8228    /* set by the start light when green */

#define CLIP_XMIN DSS(DS_g_span_xmin)   /* DS:5E4E */
#define CLIP_YMIN DSS(DS_g_span_ymin)   /* DS:5E50 */
#define CLIP_YMAX DSS(DS_g_span_ymax)   /* DS:5E52 */
#define CLIP_XMAX DSS(DS_g_span_xmax)   /* DS:5E54 */

/* ================================================================================ helpers */

static inline s16 shl16(s16 v, int n) { return (s16)(u16)((u16)v << n); }
static inline s16 add16(s16 a, s16 b) { return (s16)(a + b); }
static inline s16 sub16(s16 a, s16 b) { return (s16)(a - b); }

/* value at a near row-table pointer DS:ptr */
static inline s16 rowv(u16 ptr) { return DSS(DSW(ptr)); }

/* word at far pointer DS:fp + 2*idx (the flag pointers DS:6E20 / 6E24 / 8B82, the segment pointers) */
static inline u16 fword(u16 fp, s16 idx)
{
    FarPtr p = ds_far(fp);
    return rd16(p.seg, (u16)(p.off + 2 * idx));
}
static inline void fadv(u16 fp, s16 bytes) { DSW(fp) = (u16)(DSW(fp) + bytes); }

/* the back page being drawn: DS:5E3E == 1 ? page 1 (A200) : page 2 (A400) */
static FarPtr back_page(void)
{
    return DSS(DS_back_page) == 1 ? ds_far(DS_page1_ptr) : ds_far(DS_page2_ptr);
}

static void setreset_on(void)  { ega_setreset_on(); }   /* 2645:0008 */
static void setreset_off(void) { ega_setreset_off(); }  /* 2645:001d */

/* forward declarations (statics) */
static void road_draw_frame(void);
static void opponent_draw(void);
static void clip_set_mirror(void);

/* ======================================================================= segment pointers, projection */

/* 2645:171d road_set_seg_ptrs */
void road_set_seg_ptrs(s16 seg)
{
    u16 o = (u16)(seg << 1);
    /* PORT: the original stores the relocated segment 389b (an immediate with a fixup): SEG(0x389B) */
    ds_far_wr(DS_seg_left_ptr,   far_make(SEG(0x389B), (u16)(o + TRK_L)));
    ds_far_wr(DS_seg_right_ptr,  far_make(SEG(0x389B), (u16)(o + TRK_R)));
    ds_far_wr(DS_seg_centre_ptr, far_make(SEG(0x389B), (u16)(o + TRK_C)));
    ds_far_wr(DS_seg_hgt_ptr,    far_make(SEG(0x389B), (u16)(o + TRK_HGT)));
    ds_far_wr(DS_seg_yaw_ptr,    far_make(SEG(0x389B), (u16)(o + TRK_YAW)));
}

/* 2645:176f road_proj_x: front view screen x */
static s16 road_proj_x(s16 z, s16 x, s16 dx)
{
    x = add16(x, (s16)((s16)(z * dx) >> 2));                /* imul low word, sar 2 */
    s16 t = shl16(sub16(DSS(DS_cam_x), x), 2);
    if (z == 0) z++;
    return sub16(0xA0, idiv32_16((s32)t, z, NULL));
}

/* 2645:17b1 road_proj_y: front view screen y */
static s16 road_proj_y(s16 z, s16 y, s16 dy)
{
    y = add16(y, (s16)(z * dy));
    s16 t = shl16(sub16(y, DSS(DS_cam_y)), 2);
    if (z == 0) z++;
    return add16(idiv32_16((s32)t, z, NULL), 0x3C);
}

/* 2645:17ed road_proj_x_mirror */
static s16 road_proj_x_mirror(s16 z, s16 x, s16 dx)
{
    x = add16(x, (s16)((s16)(z * dx) >> 2));
    s16 t = sub16(DSS(DS_cam_x), x);
    if (z == 0) z++;
    return sub16(0x118, idiv32_16((s32)t, z, NULL));
}

/* 2645:1828 road_proj_y_mirror */
static s16 road_proj_y_mirror(s16 z, s16 y, s16 dy)
{
    y = add16(y, (s16)(z * dy));
    s16 t = sub16(y, DSS(DS_cam_y));
    if (z == 0) z++;
    return add16(idiv32_16((s32)t, z, NULL), 0x24);
}

/* 2645:185d road_state_init */
static void road_state_init(void)
{
    if (DSB(DS_racing) != 0) DSS(DS_seg) = 0xC8;
    else switch (DSS(DS_drive_dest)) {
        case 1: DSS(DS_seg) = 0x0A; break;
        case 2: DSS(DS_seg) = 0x41; break;
        case 3: DSS(DS_seg) = 0x73; break;
        default: break;
    }
    road_set_seg_ptrs(DSS(DS_seg));
    DSS(DS_edge_left) = 0x5DC;
    DSS(DS_edge_right) = 0xCE4;
    DSS(DS_substep) = 0;
    DSS(DS_cam_x) = 0xA00;
    DSS(DS_cam_y) = 0x76C;
    DSS(DS_cam_z_unused) = 0;
    DSS(DS_cur_height) = 0;
    DSS(DS_heading) = 0;
    DSS(DS_step_per_frame) = 0;
    DSS(DS_ai_start_a) = 1;
    DSS(DS_ai_start_lane) = (rnd(2) == 0) ? 1 : 0;          /* cmp ax,1 / sbb / neg */
    DSS(DS_ai_start_b) = 1;
}

/* world x of the road edges for the centre flags c & 0xE0 */
static void edges_of(u16 cm, s16 *xl, s16 *xr)
{
    if (cm == 0)          { *xr = 0xCE4; *xl = 0x5DC; }
    else if (cm & 0x80)   { *xr = 0xAF0; *xl = 0x7D0; }     /* one lane (bridge) */
    else if (cm & 0x20)   { *xr = 0xCE4; *xl = 0x7D0; }     /* left side closed */
    else                  { *xr = 0xAF0; *xl = 0x5DC; }     /* right lane closed */
}

/* 2645:18fb road_project_front: 15 rows (row k = segment seg+k) -> row tables */
static void road_project_front(void)
{
    s16 z = 4;
    for (s16 k = 0; k < 15; k++) {
        u16 c = fword(DS_seg_centre_ptr, 0);
        bool eye = (c & 1) || (c & 0xF000);
        fadv(DS_seg_centre_ptr, 2);
        u16 cm = c & 0xE0;
        s16 dy = sub16(DSS(DS_cur_height), (s16)fword(DS_seg_hgt_ptr, 0));
        fadv(DS_seg_hgt_ptr, 2);
        s16 dx = sub16(DSS(DS_heading), (s16)fword(DS_seg_yaw_ptr, 0));
        fadv(DS_seg_yaw_ptr, 2);
        if (k == 1) z = sub16(0x0C, DSS(DS_substep));
        s16 xlw, xrw;
        edges_of(cm, &xlw, &xrw);
        DSS((u16)(DS_row_xr + 2 * k))  = road_proj_x(z, xrw, dx);
        DSS((u16)(DS_row_xl + 2 * k))  = road_proj_x(z, xlw, dx);
        DSS((u16)(DS_row_y + 2 * k))   = road_proj_y(z, 0x7D0, dy);
        DSS((u16)(DS_row_xor + 2 * k)) = road_proj_x(z, 0xE74, dx);
        DSS((u16)(DS_row_xol + 2 * k)) = road_proj_x(z, 0x44C, dx);
        if (eye) DSS((u16)(DS_row_yeye + 2 * k)) = road_proj_y(z, 0x76C, dy);  /* else: old value */
        if (k != 0) z = add16(z, 8);
    }
}

/* 2645:1bcc road_project_mirror: 8 rows behind (row k = segment seg-k) -> mirror row tables */
static void road_project_mirror(void)
{
    s16 z = 8;
    for (s16 k = 0; k < 8; k++) {
        u16 c = fword(DS_seg_centre_ptr, 0);
        bool eye = (c & 1) || (c & 0xF000);
        fadv(DS_seg_centre_ptr, -2);
        u16 cm = c & 0xE0;
        s16 dy = sub16((s16)fword(DS_seg_hgt_ptr, 0), DSS(DS_cur_height));  /* signs reversed */
        fadv(DS_seg_hgt_ptr, -2);
        s16 dx = sub16((s16)fword(DS_seg_yaw_ptr, 0), DSS(DS_heading));
        fadv(DS_seg_yaw_ptr, -2);
        if (k == 1) z = add16(DSS(DS_substep), 8);
        s16 xlw, xrw;
        edges_of(cm, &xlw, &xrw);
        DSS((u16)(DS_mir_y + 2 * k))   = road_proj_y_mirror(z, 0x7D0, dy);
        DSS((u16)(DS_mir_xr + 2 * k))  = road_proj_x_mirror(z, xrw, dx);
        DSS((u16)(DS_mir_xl + 2 * k))  = road_proj_x_mirror(z, xlw, dx);
        DSS((u16)(DS_mir_xor + 2 * k)) = road_proj_x_mirror(z, 0xE74, dx);
        DSS((u16)(DS_mir_xol + 2 * k)) = road_proj_x_mirror(z, 0x44C, dx);
        if (eye) DSS((u16)(DS_mir_yeye + 2 * k)) = road_proj_y_mirror(z, 0x76C, dy);
        if (k != 0) z = add16(z, 8);
    }
}

/* ================================================================================ pages */

/* 2645:224e road_pages_init */
static void road_pages_init(void)
{
    if (DSS(DS_driver_id) == -2) {
        DSW(DS_g_draw_seg) = DSS(DS_back_page) == 1 ? 0xA200 : 0xA400;
        ega_show_page(DSS(DS_shown_page));                  /* 21a0:182e */
    } else {                                                /* other drivers (parked) */
        FarPtr p = ds_far(DS_page1_ptr);
        DSW(DS_g_draw_seg) = rd16(p.seg, (u16)(p.off + 0x26));
        DSW(DS_g_draw_off) = rd16(p.seg, (u16)(p.off + 0x24));
    }
}

/* 2645:2288 road_page_flip */
static void road_page_flip(void)
{
    if (DSS(DS_driver_id) == -2) {
        s16 t = DSS(DS_back_page);
        DSS(DS_back_page) = DSS(DS_shown_page);
        DSS(DS_shown_page) = t;
        DSW(DS_g_draw_seg) = DSS(DS_back_page) == 1 ? 0xA200 : 0xA400;
        ega_show_page(DSS(DS_shown_page));
    } else {                                                /* other drivers (parked): copy the view */
        Rect r;
        r.w = DSS(DS_view_copy_rect + 0);  r.h = DSS(DS_view_copy_rect + 2);
        r.sx = DSS(DS_view_copy_rect + 4); r.sy = DSS(DS_view_copy_rect + 6);
        r.dx = DSS(DS_view_copy_rect + 8); r.dy = DSS(DS_view_copy_rect + 10);
        drv_blit(ds_far(DS_page1_ptr), ds_far(DS_g_front), &r, 1);
        FarPtr p = ds_far(DS_page1_ptr);
        DSW(DS_g_draw_seg) = rd16(p.seg, (u16)(p.off + 0x26));
        DSW(DS_g_draw_off) = rd16(p.seg, (u16)(p.off + 0x24));
    }
}

/* ============================================================================ frame, step, advance */

/* 2645:1e9e road_frame: project, draw, wait for >= 9 ticks since the start of drawing */
static void road_frame(void)
{
    road_set_seg_ptrs(DSS(DS_seg));
    road_project_front();
    road_set_seg_ptrs(DSS(DS_seg));
    road_project_mirror();
    road_set_seg_ptrs(DSS(DS_seg));
    /* PORT: the tick count of the frame lock is the named constant RACE_TICKS_PER_FRAME (race.h) */
    u32 until = DSL(DS_ticks) + RACE_TICKS_PER_FRAME;
    road_draw_frame();
    while (tick_after(until, ticks_now()))                  /* hi word signed, lo word unsigned */
        host_pump();
}

/* 2645:1efc road_advance_segment (also from 0000:d624 in races) */
void road_advance_segment(void)
{
    DSS(DS_seg) = add16(DSS(DS_seg), (s16)(DSS(DS_substep) >> 3));
    road_set_seg_ptrs(DSS(DS_seg));
    DSS(DS_substep) &= 7;
    s16 xl, xr;
    edges_of(fword(DS_seg_centre_ptr, 0) & 0xE0, &xl, &xr);
    DSS(DS_edge_left) = xl;  DSS(DS_edge_right) = xr;
    edges_of(fword(DS_seg_centre_ptr, 4) & 0xE0, &xl, &xr);  /* 4 segments ahead, for the AI */
    DSS(DS_ai_edge_left) = xl;  DSS(DS_ai_edge_right) = xr;
    DSS(DS_cur_height) = (s16)fword(DS_seg_hgt_ptr, 0);
    if (DSS(DS_demo_active) == 0 && DSS(DS_auto_drive) == 0) {
        s16 lim = DSS(DS_race_type) == 0 ? 0x17C : 0x3FC;
        if (!(lim < DSS(DS_seg))) return;
    }
    /* autopilot: demo, auto, or past the finish line */
    s16 target = 0x960;
    if (DSS(DS_opp_lat) > 0x960) target = sub16(target, 0x140);
    else                         target = add16(target, 0xA0);
    s16 d = sub16(DSS(DS_cam_x), target);
    if (abs16(d) > 0x64)
        DSS(DS_cam_x) = add16(target, target < DSS(DS_cam_x) ? 0x64 : -0x64);
    else
        DSS(DS_cam_x) = target;
    s16 y = (s16)fword(DS_seg_yaw_ptr, 0);
    if (abs16(sub16(y, DSS(DS_heading))) > 0) {
        DSS(DS_player_wheel) = shl16(sub16(DSS(DS_heading), y), 1);
        if (abs16(DSS(DS_player_wheel)) > 0x18)
            DSS(DS_player_wheel) = DSS(DS_player_wheel) > 0 ? 0x18 : -0x18;
    }
    DSS(DS_heading) = (s16)fword(DS_seg_yaw_ptr, 0);
}

/* 2645:206b road_edge_collision (every tick from the race ISR 0000:238c) */
s16 road_edge_collision(void)
{
    DSW(DS_offroad) = 0;
    s16 l = sub16(DSS(DS_cam_x), 0x1E);
    if (sub16(DSS(DS_edge_left), 0x104) >= l) return 1;
    if (add16(DSS(DS_cam_x), 0x46) >= add16(DSS(DS_edge_right), 0x104)) return 1;
    s16 s = DSS(DS_seg);
    if (DSS(DS_edge_left) > l) {                            /* left shoulder */
        if ((TRKW(TRK_L, s) & 0x1000) || (TRKW(TRK_L, s - 1) & 0x1000)) return 1;
        if (TRKW(TRK_C, s + 1) & 0xF000) return 1;
        DSW(DS_offroad) = 1;
        return 0;
    }
    if (add16(DSS(DS_cam_x), 0x46) > DSS(DS_edge_right)) {  /* right shoulder */
        if ((TRKW(TRK_R, s) & 0x1000) || (TRKW(TRK_R, s - 1) & 0x1000)) return 1;
        if (TRKW(TRK_C, s + 1) & 0xF002) return 1;
        DSW(DS_offroad) = 1;
        return 0;
    }
    return 0;
}

/* 2645:527a road_first_frame */
static void road_first_frame(void)
{
    DSS(DS_opp_sprite_prev2) = 0;
    DSS(DS_opp_sprite) = 0;
    road_pages_init();
    road_frame();
}

/* 2645:2114 road_race_init */
void road_race_init(void)
{
    DSW(DS_nudge_left_latch) = 0;
    DSW(DS_nudge_right_latch) = 0;
    road_state_init();
    road_first_frame();
}

/* 2645:2126 road_nudge_heading */
static void road_nudge_heading(s16 dir)
{
    if (dir == 1) DSS(DS_heading) = add16(DSS(DS_heading), 3);
    else          DSS(DS_heading) = sub16(DSS(DS_heading), 3);
}

/* 2645:213d road_step: once per frame from the drive loop 0f38:7b22 */
void road_step(void)
{
    if (DSS(DS_player_steer_in) != 0 && DSS(DS_player_phys) != 0) {
        s16 e = sub16(sub16(DSS(DS_heading), (s16)fword(DS_seg_yaw_ptr, 0)), DSS(DS_player_steer_in));
        if (abs16(e) < 0xC8)
            DSS(DS_heading) = sub16(DSS(DS_heading), add16((s16)(DSS(DS_player_steer_in) >> 1), 1));
    }
    if (DSB(DS_racing) == 0) DSS(DS_step_per_frame) = DSW(DS_slow_machine) ? 6 : 4;
    if (DSS(DS_step_per_frame) > 0 && DSS(DS_start_light_h) > 0)
        DSS(DS_start_light_h) = sub16(DSS(DS_start_light_h), 4);
    road_frame();
    if (DSS(DS_demo_active) == 0 && DSS(DS_auto_drive) == 0) {
        if (DSB(DS_tires_blown) != 0)                       /* pulled over (police stop) */
            DSS(DS_cam_x) = add16(DSS(DS_cam_x), 0x19);
        else if (DSS(DS_player_phys) != 0) {
            s16 t = (s16)(sub16(DSS(DS_heading), (s16)fword(DS_seg_yaw_ptr, 0))
                          * add16(DSS(DS_step_per_frame), DSS(DS_player_tire_slip)));   /* imul low */
            DSS(DS_cam_x) = sub16(DSS(DS_cam_x), (s16)(t >> 2));
        }
    }
    if (DSB(DS_racing) != 0) return;                        /* races: advance by 0000:d624 */
    DSS(DS_substep) = add16(DSS(DS_substep), DSS(DS_step_per_frame));
    if (DSS(DS_substep) >= 8) road_advance_segment();
    if ((TRKW(TRK_L, DSS(DS_seg) + 7) & 1) || DSW(DS_nudge_left_latch)) {
        DSW(DS_nudge_left_latch) = 1;
        road_nudge_heading(1);
    }
    if ((TRKW(TRK_R, DSS(DS_seg) + 6) & 1) || DSW(DS_nudge_right_latch)) {
        road_nudge_heading(-1);
        DSW(DS_nudge_right_latch) = 1;
    }
}

/* ================================================================================ clipping */

/* 2645:22fc blit_clip_y */
static void blit_clip_y(Rect *r)
{
    if (r->dy < CLIP_YMIN) {
        s16 d = sub16(CLIP_YMIN, r->dy);
        r->h = sub16(r->h, d);
        r->sy = add16(r->sy, d);
        r->dy = CLIP_YMIN;
    }
    s16 e = add16(r->dy, r->h);
    if (CLIP_YMAX < e) r->h = sub16(r->h, sub16(e, CLIP_YMAX));
}

/* 2645:2331 blit_clip_xy: + left side (srcx += d + 7, original) */
static void blit_clip_xy(Rect *r)
{
    blit_clip_y(r);                                         /* same code inline */
    if (r->dx < CLIP_XMIN) {
        s16 d = sub16(CLIP_XMIN, r->dx);
        r->w = sub16(r->w, d);
        r->sx = add16(r->sx, add16(d, 7));
        r->dx = CLIP_XMIN;
    }
}

/* 2645:2381 road_vline: clipped vertical line via slot 12 */
static void road_vline(s16 colour, s16 x, s16 y1, s16 y2)
{
    if (y2 < CLIP_YMIN || y1 > CLIP_YMAX || x < CLIP_XMIN || x > CLIP_XMAX) return;
    if (y1 < CLIP_YMIN) y1 = CLIP_YMIN;
    if (y1 > CLIP_YMAX) y1 = CLIP_YMAX;
    if (y2 < CLIP_YMIN) y2 = CLIP_YMIN;
    if (y2 > CLIP_YMAX) y2 = CLIP_YMAX;
    drv_line(x, y1, x, y2, colour);
}

/* 2645:23f0 clip_set_ymin0 (UI) */
void clip_set_ymin0(void)
{
    DSS(DS_g_span_ymin) = 0;
}

/* 2645:23f7 clip_set_mirror */
static void clip_set_mirror(void)
{
    DSS(DS_g_span_xmin) = 0xF2;
    DSS(DS_g_span_ymin) = 0x16;
    DSS(DS_g_span_xmax) = 0x13D;
    DSS(DS_g_span_ymax) = 0x2A;
}

/* 2645:2410 clip_set_view */
void clip_set_view(void)
{
    DSS(DS_g_span_xmin) = 0;
    DSS(DS_g_span_ymin) = 0x12;
    DSS(DS_g_span_ymax) = 0x65;
    DSS(DS_g_span_xmax) = 0x13F;
}

/* ======================================================================= picture shrinking */

/* 2645:528c popcount8 */
static s16 popcount8(u8 m)
{
    s16 n = 0;
    for (int i = 0; i < 8; i++) { if (m & 0x80) n++; m = (u8)(m << 1); }
    return n;
}

/* 2645:52b7 pic_alloc_shrunk: header + data of the shrunk picture, data 0, mask FFh */
static FarPtr pic_alloc_shrunk(FarPtr src, u8 ymask, u8 xmask)
{
    s16 ky = popcount8(ymask), kx = popcount8(xmask);
    s16 w = (s16)desc_w(src), h = (s16)desc_h(src);
    s16 nw = (s16)((s16)((w & 0xFFF8) * kx) >> 3);          /* and al,F8h; imul low; sar 3 */
    s16 rem = (s16)(w & 7);
    s16 bit = 0x80;
    for (s16 i = 0; i < rem; i++) { if (xmask & bit) nw++; bit >>= 1; }
    nw = (s16)((u16)(nw + (u16)~DSW(DS_g_x_align)) & DSW(DS_g_x_align));
    s16 nh = (s16)((s16)((h & 0xFFF8) * ky) >> 3);
    rem = (s16)(h & 7);
    bit = 0x80;
    for (s16 i = 0; i < rem; i++) { if (ymask & bit) nh++; bit >>= 1; }
    FarPtr d = arena_bitmap_alloc(nw, nh, far_is_null(desc_mask(src)) ? 0 : 1, 1);    /* 0f38:9fce */
    far_memset(desc_planes(d), 0x00, desc_size(d));                                   /* 0f38:1640 */
    far_memset(desc_mask(d), 0xFF, div32_16(desc_size(d), DSW(DS_g_nplanes), NULL));
    return d;
}

/* 2645:5407 pic_shrink_planar (VGA/EGA): keep the rows / columns whose bit 80h >> (n & 7) is set.
 * PORT: the arena bitmaps are RAM, read and written directly in mem[]. */
static FarPtr pic_shrink_planar(FarPtr src, u8 ymask, u8 xmask)
{
    s16 kx = popcount8(xmask);
    FarPtr d = pic_alloc_shrunk(src, ymask, xmask);
    FarPtr sp = desc_planes(src);
    s16 sh_ = (s16)desc_h(src);
    s16 sstride = (s16)desc_stride(src);
    FarPtr dbase = desc_planes(d);
    s16 dstride = (s16)desc_stride(d);
    s16 dh = (s16)desc_h(d);
    s16 prow = 0;                                           /* [bp-0x3a] */
    s16 si = 0;                                             /* [bp-0x1a] (carried, reset per kept row) */
    for (int plane = 0; plane < 4; plane++) {
        FarPtr dp = far_make(dbase.seg, (u16)(dbase.off + (s16)(prow * dstride)));
        u8 rowbit = 0x80;
        if (sh_ > 0) {
            for (s16 y = sh_; y != 0; y--) {
                if (ymask & rowbit) {
                    si = 0;
                    if (sstride > 0) {
                        for (s16 b = sstride; b != 0; b--) {
                            u8 v = rd8(sp.seg, sp.off), out = 0, m = xmask, ob = 0x80;
                            for (int j = 0; j < 8; j++) {
                                if (m & ob) { out |= (u8)(v & ob); ob >>= 1; }
                                else        { v = (u8)(v << 1); m = (u8)(m << 1); }
                            }
                            wr8(dp.seg, dp.off, (u8)(rd8(dp.seg, dp.off) | (u8)((u16)out >> si)));
                            si = add16(si, kx);
                            if (si > 8) {
                                dp.off++;
                                wr8(dp.seg, dp.off, (u8)(out << (u8)(kx - si + 8)));
                                si &= 7;
                            }
                            if (si == 8) { dp.off++; si = 0; }
                            sp.off++;
                        }
                    }
                    if (si != 0) dp.off++;                  /* rows are byte padded */
                } else
                    sp.off = (u16)(sp.off + sstride);
                rowbit >>= 1;
                if (rowbit == 0) rowbit = 0x80;
            }
        }
        prow = add16(prow, dh);
    }
    if (!far_is_null(desc_mask(src)))
        make_mask_plane(desc_planes(d), desc_mask(d), desc_size(d));   /* 2634:00c1 */
    return d;
}

/* ======================================================================= drive-screen graphics */

static void blit_whole(FarPtr p, FarPtr dst, s16 dx, s16 dy)
{
    Rect r = { (s16)desc_w(p), (s16)desc_h(p), 0, 0, dx, dy };
    drv_blit(p, dst, &r, 1);
}

/* 2645:2429 road_load_graphics */
void road_load_graphics(void)
{
    arena_reset();                                                          /* 0f38:a054 */
    FarPtr p = pic_get(1005, 2);                                            /* LIB2 #5 sky */
    blit_whole(p, ds_far(DS_page1_ptr), 0, 0x66);
    arena_pop(1);
    s16 loc = DSW(DS_racing) != 0 ? 0 : DSS(DS_drive_dest);
    p = pic_get(DSS((u16)(DS_loc_sign_sheet + 2 * loc)), 2);                /* sign sheet */
    blit_whole(p, ds_far(DS_page2_ptr), 0, 0x66);
    arena_pop(1);
    if (DSW(DS_racing) == 0) {                                              /* town billboards */
        bool vga = DSS(DS_driver_id) == -2;
        p = pic_get(1261, 2);
        blit_whole(p, vga ? ds_far(DS_page0_ptr) : ds_far(DS_page2_ptr), 0, (s16)((vga ? 0x63 : 0) + 1));
        arena_pop(1);
        p = pic_get(1262, 2);
        blit_whole(p, vga ? ds_far(DS_page0_ptr) : ds_far(DS_page2_ptr), 0, (s16)((vga ? 0x63 : 0) + 0x2C));
        arena_pop(1);
    }
    ds_far_wr(DS_pic_mountains, pic_get(1267, 1));                          /* horizon panorama */
    if (DSW(DS_racing) == 0) {
        ds_far_wr(DS_pic_town_skyline, pic_get(1008, 1));
        FarPtr s = ds_far(DS_pic_town_skyline);
        Rect r = { (s16)desc_w(s), (s16)desc_h(s), 0, 0, 0x2C, 0x0C };
        drv_blit_masked(s, ds_far(DS_pic_mountains), &r, 1);
        arena_pop_low(1);                                                   /* 0f38:a09e */
    }
    {
        FarPtr m = ds_far(DS_pic_mountains);
        Rect r = { (s16)desc_w(m), (s16)desc_h(m), 0, 0, 0, sub16(0xC8, (s16)desc_h(m)) };
        drv_blit(m, ds_far(DS_page1_ptr), &r, 1);
        arena_pop_low(1);
    }
    ds_far_wr(DS_pic_pillar, pic_get(1006, 1));
    ds_far_wr(DS_pic_hood, pic_get(1007, 1));
    ds_far_wr(DS_pic_mirror, pic_get(1009, 1));
    ds_far_wr(DS_pic_mirror_sky, pic_get(1243, 1));
    if (DSW(DS_racing) == 0) return;

    s16 cls = MODEL_8DF0(DSS(DS_opp_model));                                /* (s8)DS:8DF0[10*id] */
    ds_far_wr(DS_pic_start_light, pic_get(1013, 1));
    DSS(DS_start_light_h) = (s16)desc_h(ds_far(DS_pic_start_light));
    u16 ids = (u16)(DS_car_class_pics + shl16(cls, 3));
    ds_far_wr(DS_opp_sprites + 0, pic_get(DSS((u16)(ids + 0)), 1));        /* side view */
    ds_far_wr(DS_opp_sprites + 4, pic_get(DSS((u16)(ids + 2)), 1));        /* rear 3/4 */
    ds_far_wr(DS_opp_sprites + 8, pic_get(DSS((u16)(ids + 4)), 2));        /* rear (mode 2) */
    /* PORT: VGA path only: the shrinker is always 2645:5407 (the original picks 5885 for Tandy and
     * 5670 for CGA / Hercules, parked) */
    for (s16 k = 0; k < 3; k++) {
        u8 m = DSB((u16)(DS_shrink_masks_rear + k));
        ds_far_wr((u16)(DS_opp_sprites + 12 + 4 * k), pic_shrink_planar(ds_far(DS_opp_sprites + 8), m, m));
    }
    for (s16 k = 0; k < 3; k++) {
        u8 m = DSB((u16)(DS_shrink_masks_far + k));
        ds_far_wr((u16)(DS_opp_sprites + 24 + 4 * k), pic_shrink_planar(ds_far(DS_opp_sprites + 20), m, m));
    }
    ds_far_wr(DS_opp_sprites + 36, pic_get(DSS((u16)(ids + 6)), 1));       /* front view */
    for (s16 k = 0; k < 3; k++) {
        u8 m = DSB((u16)(DS_shrink_masks_front + k));
        ds_far_wr((u16)(DS_opp_sprites + 40 + 4 * k), pic_shrink_planar(ds_far(DS_opp_sprites + 36), m, m));
    }
    /* police sprites (used by the race code) */
    ds_far_wr(DS_police_sprites + 0, ds_far(DS_opp_sprites));
    ds_far_wr(DS_police_sprites + 4, pic_get(1241, 1));
    ds_far_wr(DS_police_sprites + 8, pic_get(1242, 2));
    {
        u8 m = DSB(DS_shrink_masks_rear);
        FarPtr s = pic_shrink_planar(ds_far(DS_police_sprites + 8), m, m);
        ds_far_wr(DS_police_sprites + 12, s);
        ds_far_wr(DS_police_sprites + 8, s);
    }
    arena_pop(1);
    for (s16 k = 0; k < 3; k++)
        ds_far_wr((u16)(DS_police_sprites + 16 + 4 * k), ds_far(DS_police_sprites + 12));
    ds_far_wr(DS_police_sprites + 36, pic_get(1246, 1));
    for (s16 k = 0; k < 3; k++) {
        u8 m = DSB((u16)(DS_shrink_masks_front + k));
        ds_far_wr((u16)(DS_police_sprites + 40 + 4 * k), pic_shrink_planar(ds_far(DS_police_sprites + 36), m, m));
    }
}

/* ================================================================================ sky */

/* scroll of the sky / horizon by the heading (2645:29c6 / 2ac6): v = heading << sh */
static void sky_scroll(int sh, s16 *a, s16 *b)
{
    s16 h = DSS(DS_heading);
    if (h > 0) {
        s16 m = (s16)((s16)(shl16(h, sh) % 0x140) & 0xFFF8);   /* and dl,F8h = & FFF8h */
        *a = m;  *b = sub16(0x140, m);
    } else {
        s16 m = (s16)((s16)(shl16(abs16(h), sh) % 0x140) & 0xFFF8);
        *b = m;  *a = sub16(0x140, m);
    }
}

/* 2645:2982 road_draw_sky: sky, horizon panorama, ground fill, highest road row */
static void road_draw_sky(void)
{
    Rect r;
    s16 hy = 0, a = 0, b = 0;                   /* PORT: [bp-0x1a/18/1e] only used when racing */
    r.dy = 0x12;
    bool race = DSW(DS_racing) != 0;
    if (race) {
        hy = road_proj_y(0xF0, 0x7D0, DSS(DS_cur_height));
        r.h = sub16(hy, 0x1E);
        r.sy = sub16(0x9A, r.h);
        sky_scroll(1, &a, &b);
        r.w = b; r.sx = 0; r.dx = a;
    } else {
        r.w = 0x140; r.sx = 0; r.sy = 0x66; r.dx = 0; r.h = 0x1D;
    }
    drv_blit(ds_far(DS_page1_ptr), back_page(), &r, 1);
    if (race) {
        r.w = a; r.sx = b; r.dx = 0;
        drv_blit(ds_far(DS_page1_ptr), back_page(), &r, 1);
    }
    r.h = race ? 0x14 : 0x10;
    r.sy = 0xA3;
    if (race) {
        sky_scroll(2, &a, &b);
        r.w = b; r.sx = 0; r.dx = a; r.dy = sub16(hy, 0x0C);
    } else {
        r.w = 0x140; r.sx = 0; r.dx = 0; r.dy = 0x2F;
    }
    blit_clip_y(&r);
    drv_blit(ds_far(DS_page1_ptr), back_page(), &r, 1);
    DSS(DS_top_band_row) = 0x0E;
    if (race) {
        r.w = a; r.sx = b; r.dx = 0;
        drv_blit(ds_far(DS_page1_ptr), back_page(), &r, 1);
        DSS(DS_ground_top_y) = add16(hy, 8);
        s16 top = DSS(DS_ground_top_y), idx = DSS(DS_top_band_row);
        for (s16 k = 14; k != 0; k--) {
            s16 yk = DSS((u16)(DS_row_y + 2 * k));
            if (yk < top) { top = yk; idx = k; }
        }
        DSS(DS_top_band_row) = idx;
        DSS(DS_ground_top_y) = top;
        drv_fill_rows(top, 9);
    } else
        drv_fill_rows(0x3F, 1);
}

/* ================================================================================ shapes */

/* 2645:2c0a shape_draw: hline sprite; sizes = near ptr to ShapeSize[nsizes] {s16 n; far HLine*} */
static void shape_draw(s16 x, s16 y, s16 size, s16 nsizes, u16 sizes, s16 side)
{
    if (!(nsizes > size)) return;
    u16 e = (u16)(sizes + (s16)(6 * size));
    s16 n = DSS(e);
    FarPtr ln = ds_far((u16)(e + 2));
    if (n <= 0) return;
    for (; n != 0; n--) {
        s16 ldx = (s8)rd8(ln.seg, ln.off);
        s16 ldy = (s8)rd8(ln.seg, (u16)(ln.off + 1));
        s16 lw  = (s8)rd8(ln.seg, (u16)(ln.off + 2));
        s16 lc  = (s8)rd8(ln.seg, (u16)(ln.off + 3));
        if (side == -1) {
            s16 xr = sub16(x, ldx);
            drv_hspan(sub16(xr, lw), xr, add16(ldy, y), lc);
        } else {
            s16 xl = add16(ldx, x);
            drv_hspan(xl, add16(lw, xl), add16(ldy, y), lc);
        }
        ln.off = (u16)(ln.off + 4);
    }
}

/* 2645:3abf shape_draw_dot (UI): 5-line dot DS:60A0 */
void shape_draw_dot(s16 x, s16 y)
{
    setreset_on();
    u16 set = DSW(DS_dot_shape);
    shape_draw(x, y, 0, DSS(set), DSW((u16)(set + 2)), 1);
    setreset_off();
}

/* 2645:3aeb scenery_draw_shape */
static void scenery_draw_shape(s16 x, s16 y, s16 depth, s16 side, s16 type)
{
    s16 size = sub16(9, depth);
    if (size >= 7) size = 6;
    if (size < 0) size = 0;
    if (y > 0x65) y = 0x65;
    u16 set = DSW((u16)(DS_shape_table + 2 * type));
    shape_draw(x, y, size, DSS(set), DSW((u16)(set + 2)), side);
}

/* 2645:3b3a scenery_draw_hydrant (no y clamp) */
static void scenery_draw_hydrant(s16 x, s16 y, s16 depth, s16 side)
{
    s16 size = sub16(9, depth);
    if (size >= 8) size = 7;
    if (size < 0) size = 0;
    u16 set = DSW(DS_hydrant_shape);
    shape_draw(x, y, size, DSS(set), DSW((u16)(set + 2)), side);
}

/* 2645:3b79 scenery_draw_sign_picture: one of 7 pre-scaled billboard sizes from an off-screen page */
static void scenery_draw_sign_picture(s16 x, s16 y, s16 depth, s16 align, s16 xclip, s16 kind)
{
    s16 size = sub16(9, depth);
    if (size > 6) size = 6;
    if (size < 0) size = 0;
    u16 e = (u16)(DS_sign_sizes + 6 * size);
    Rect r;
    r.w = DSS((u16)(e + 2));
    r.h = DSS((u16)(e + 4));
    r.sx = DSS(e);
    r.sy = 0;                           /* PORT: uninitialised in the original for kind 1..3 never unset */
    switch (kind) {
    case 1: r.sy = 0x66; break;
    case 2: r.sy = (s16)((DSS(DS_driver_id) == -2 ? 0x63 : 0) + 1); break;
    case 3: r.sy = (s16)((DSS(DS_driver_id) == -2 ? 0x63 : 0) + 0x2C); break;
    default: break;
    }
    if (align == 1)      r.dx = sub16(x, r.w);
    else if (align == 0) r.dx = sub16(x, (s16)(r.w >> 1));
    else                 r.dx = x;
    if (align == 0) r.dy = sub16(y, (s16)(r.h >> 1));
    else            r.dy = sub16(y, r.h);
    if (xclip) blit_clip_xy(&r); else blit_clip_y(&r);
    if (kind == 1)
        drv_blit(ds_far(DS_page2_ptr), back_page(), &r, 1);
    else
        drv_blit(DSS(DS_driver_id) == -2 ? ds_far(DS_page0_ptr) : ds_far(DS_page2_ptr), back_page(), &r, 1);
}

/* 2645:3ccc scenery_draw_banner: two posts, cross bar, text shape or picture */
static void scenery_draw_banner(s16 xl, s16 xr, s16 ytop, s16 ybot, s16 depth, s16 xclip, u16 flags)
{
    s16 n = add16(DSS(DSW(DS_dash_ptr)), 1);
    if (n != 0) {
        s16 si = n;
        do {
            setreset_off();
            road_vline(0x0A, sub16(xl, si), ytop, ybot);
            road_vline(0x0A, add16(xr, si), ytop, ybot);
            setreset_on();
            drv_hspan(xl, xr, sub16(add16(ytop, si), 1), 0x0A);
        } while (--si != 0);
    }
    s16 mid = (s16)(add16(xl, xr) >> 1);
    if (flags & 0x1000)
        scenery_draw_shape(mid, ytop, depth, 1, 0x0D);                     /* "CITY LIMITS" */
    else if (flags & 0x2000) {
        setreset_off();
        scenery_draw_sign_picture(mid, ytop, depth, 0, xclip, 1);
        setreset_on();
    } else if (fword(DS_centre_ptr, 0) & 1) {                              /* town billboards */
        setreset_off();
        scenery_draw_sign_picture(mid, ytop, depth, 0, xclip, (flags & 0x4000) ? 2 : 3);
        setreset_on();
    } else
        scenery_draw_shape(mid, ytop, depth, 1, (flags & 0x4000) ? 0x0C : 0x0E);
}

/* ================================================================================ road bands */

/* 2645:2cfc slope16 */
static s16 slope16(s16 xa, s16 ya, s16 xb, s16 yb)
{
    if (yb == ya) return 0;
    return idiv32_16((s32)shl16(sub16(xb, xa), 4), sub16(yb, ya), NULL);
}

/* 2645:2f2b sign. PORT: renamed (platform.h has 0000:1b91 sign) */
static s16 sign_2645(s16 v)
{
    return v > 0 ? 1 : (v < 0 ? -1 : 0);
}

/* 2645:2d20 quad_fill: quad with vertical sides x0 (y3 top .. y0 bottom) and x1 (y2 .. y1) */
static void quad_fill(s16 c, s16 x0, s16 x1, s16 y0, s16 y1, s16 y2, s16 y3)
{
    if (y0 == y1 && y3 == y2) {
        for (s16 y = y3; y <= y1; y++) drv_hspan(x0, x1, y, c);
        return;
    }
    s16 top = (y3 > y2) ? y2 : y3;
    s16 bot = (y0 < y1) ? y1 : y0;
    s16 mode, E = 0, F = 0, A12 = 0, A14 = 0;   /* [bp-0xe] [bp-0x10] [bp-0x12] [bp-0x14] */
    if (y3 < y2 && y0 > y1) {
        mode = 2; E = x0; A14 = shl16(x0, 4);
    } else if (y2 < y3 && y1 > y0) {
        mode = 3; F = x1; E = shl16(x1, 4);
        /* PORT: [bp-0x12] is never initialised in the original (§8); x1 << 4, the evident intent */
        A12 = shl16(x1, 4);
    } else if (!(y3 > y2) && !(y0 > y1)) {
        mode = 4; A14 = A12 = shl16(x0, 4);
    } else {
        mode = 5; A14 = A12 = shl16(x1, 4);
    }
    s16 sTop = slope16(x0, y3, x1, y2);
    s16 sBot = slope16(x0, y0, x1, y1);
    for (s16 y = top; y <= bot && y <= CLIP_YMAX; y++) {
        switch (mode) {
        case 2:
            if (y < y2)       { A14 = add16(A14, sTop); F = (s16)(A14 >> 4); }
            else if (y <= y1) F = x1;
            else              { A14 = add16(A14, sBot); F = (s16)(A14 >> 4); }
            break;
        case 3:
            if (y < y3)       { A12 = add16(A12, sTop); E = (s16)(A12 >> 4); }
            else if (y <= y0) E = x0;
            else              { A12 = add16(A12, sBot); E = (s16)(A12 >> 4); }
            break;
        case 4:
            if (y <= y0) E = x0; else { A12 = add16(A12, sBot); E = (s16)(A12 >> 4); }
            if (y >= y2) F = x1; else { A14 = add16(A14, sTop); F = (s16)(A14 >> 4); }
            break;
        default: /* 5 */
            if (y < y3) { A12 = add16(A12, sTop); E = (s16)(A12 >> 4); } else E = x0;
            if (y <= y1) F = x1; else { A14 = add16(A14, sBot); F = (s16)(A14 >> 4); }
            break;
        }
        if (E > F) drv_hspan(F, E, y, c); else drv_hspan(E, F, y, c);
    }
}

/* PORT: road_draw_band_front compares the remaining line count with the never-written local
 * [bp-0x22] to draw an extra colour-2 line on the town shoulders of rows < 6; treated as never equal
 * (race_render.md §8). */
static inline bool band_g_match(s16 n) { (void)n; return false; }

/* 2645:2f48 road_draw_band_front: band between rows p = row and q = row-1 */
static void road_draw_band_front(s16 row)
{
    s16 xl = rowv(RP_XL), xr = rowv(RP_XR);
    s16 axl = shl16(xl, 4), axr = shl16(xr, 4);
    s16 y = rowv(RP_Y), ybot = rowv(RQ_Y);
    s16 sl = slope16(rowv(RQ_XL), ybot, xl, y);
    s16 sr = slope16(rowv(RQ_XR), ybot, xr, y);
    s16 cross = (s16)(fword(DS_centre_ptr, 0) & 1);
    s16 xol = 0, xor_ = 0, axol = 0, axor = 0, sol = 0, sor = 0;
    if (cross) {
        xol = rowv(RP_XOL); xor_ = rowv(RP_XOR);
        axol = shl16(xol, 4); axor = shl16(xor_, 4);
        sol = slope16(rowv(RQ_XOL), ybot, xol, y);
        sor = slope16(rowv(RQ_XOR), ybot, xor_, y);
    }
    s16 w = 0, dashx = 0;                       /* PORT: uninitialised unless C & 8 (then unused) */
    if (fword(DS_centre_ptr, 0) & 8) {
        w = DSS(DSW(DS_dash_ptr));
        dashx = sub16((s16)(add16(xl, xr) >> 1), (s16)(w >> 1));
    }
    if (sign_2645(sub16(ybot, y)) < 0) return;              /* back-facing band */
    s16 n = sub16(ybot, y);
    while (n != 0 && y <= 0x65) {
        if (!cross) {
            drv_hspan(xl, xr, y, (fword(DS_centre_ptr, 0) & 0x10) ? 0x0F : 0x01);
        } else {
            if (fword(DS_centre_ptr, 0) & 4) return;        /* intersection */
            if (fword(DS_left_ptr, 0) & 0x80) {
                s16 m = add16((s16)(sub16(xl, xol) >> 1), xol);
                drv_hspan(xol, m, y, 4);
                drv_hspan(m, xl, y, 9);
                if (band_g_match(n) && row < 6) drv_hspan(xol, m, y, 2);
            } else {
                drv_hspan(xol, xl, y, 4);
                if (band_g_match(n) && row < 6) drv_hspan(xol, xl, y, 2);
            }
            if (fword(DS_right_ptr, 0) & 0x80) {
                s16 m = sub16(xor_, (s16)(sub16(xor_, xr) >> 1));
                drv_hspan(m, xor_, y, 4);
                drv_hspan(xr, m, y, 9);
                if (band_g_match(n) && row < 6) drv_hspan(m, xor_, y, 2);
            } else {
                drv_hspan(xr, xor_, y, 4);
                if (band_g_match(n) && row < 6) drv_hspan(xr, xor_, y, 2);
            }
        }
        if (fword(DS_centre_ptr, 0) & 8) drv_hspan(dashx, add16(dashx, w), y, 4);   /* centre dash */
        axl = add16(axl, sl); axr = add16(axr, sr);
        xl = (s16)(axl >> 4); xr = (s16)(axr >> 4);
        if (cross) {
            axol = add16(axol, sol); axor = add16(axor, sor);
            xol = (s16)(axol >> 4); xor_ = (s16)(axor >> 4);
        }
        dashx = sub16((s16)(add16(xl, xr) >> 1), (s16)(w >> 1));
        n--; y++;
    }
}

/* 2645:324d road_draw_band_mirror */
static void road_draw_band_mirror(void)
{
    s16 xl = rowv(RP_XL), xr = rowv(RP_XR);
    s16 axl = shl16(xl, 4), axr = shl16(xr, 4);
    s16 y = rowv(RP_Y), ybot = rowv(RQ_Y);
    s16 sl = slope16(rowv(RQ_XL), ybot, xl, y);
    s16 sr = slope16(rowv(RQ_XR), ybot, xr, y);
    s16 cross = (s16)(fword(DS_centre_ptr, 0) & 1);
    s16 xol = 0, xor_ = 0, axol = 0, axor = 0, sol = 0, sor = 0;
    if (cross) {
        xol = rowv(RP_XOL); xor_ = rowv(RP_XOR);
        axol = shl16(xol, 4); axor = shl16(xor_, 4);
        sol = slope16(rowv(RQ_XOL), ybot, xol, y);
        sor = slope16(rowv(RQ_XOR), ybot, xor_, y);
    }
    s16 w = 0, dashx = 0;                       /* PORT: uninitialised unless C & 8 (§8, harmless) */
    if (fword(DS_centre_ptr, 0) & 8) {
        w = DSS(DSW(DS_dash_ptr));
        dashx = sub16((s16)(add16(xl, xr) >> 1), (s16)(w >> 1));
    }
    if (sign_2645(sub16(ybot, y)) < 0) return;
    s16 n = sub16(ybot, y);
    if (n == 0) return;
    s16 half = (s16)(w >> 1);
    for (;;) {
        if (y > 0x2A) break;
        if (!cross) {
            drv_hspan(0xF2, xl, y, 9);                      /* grass drawn explicitly */
            drv_hspan(xr, 0x13D, y, 9);
            drv_hspan(xl, xr, y, 1);
        } else {
            if (fword(DS_centre_ptr, 0) & 4) break;
            if (fword(DS_left_ptr, 0) & 0x80) {
                s16 m = add16((s16)(sub16(xl, xol) >> 1), xol);
                drv_hspan(xol, m, y, 4);
                drv_hspan(m, xl, y, 9);
            } else
                drv_hspan(xol, xl, y, 4);
            if (fword(DS_right_ptr, 0) & 0x80) {
                s16 m = sub16(xor_, (s16)(sub16(xor_, xr) >> 1));
                drv_hspan(m, xor_, y, 4);
                drv_hspan(xr, m, y, 9);
            } else
                drv_hspan(xr, xor_, y, 4);
        }
        if (fword(DS_centre_ptr, 0) & 8) drv_hspan(dashx, add16(dashx, w), y, 4);
        axl = add16(axl, sl); axr = add16(axr, sr);
        xl = (s16)(axl >> 4); xr = (s16)(axr >> 4);
        if (cross) {
            axol = add16(axol, sol); axor = add16(axor, sor);
            xol = (s16)(axol >> 4); xor_ = (s16)(axor >> 4);
        }
        dashx = sub16((s16)(add16(xl, xr) >> 1), half);
        n--; y++;
        if (n == 0) break;
    }
}

/* ================================================================================ town walls */

/* 2645:34da town_walls_front: façade, upper storey, block side wall, storey side face */
static void town_walls_front(s16 a, s16 b, s16 c, s16 d, s16 e, s16 f, s16 left)
{
    u16 fp = left ? DS_left_ptr : DS_right_ptr;
    u16 F = fword(fp, 0);
    u16 Fq4 = fword(fp, -1) & 4;
    if (fword(DS_centre_ptr, 0) & 4) return;
    quad_fill((F & 0x10) ? 2 : 3, a, b, c, d, e, f);
    if (F & 4) {
        s16 t0, t1;
        if (F & 8) { t0 = sub16(add16(f, f), c); t1 = sub16(add16(e, e), d); }
        else       { t0 = sub16(f, (s16)(sub16(c, f) >> 1)); t1 = sub16(e, (s16)(sub16(d, e) >> 1)); }
        quad_fill(4, a, b, f, e, t1, t0);
    }
    if (fword(DS_centre_ptr, -1) & 4) {                     /* next nearer row is a cross street */
        if (left) {
            drv_hspan(CLIP_XMIN, a, sub16(c, 1), 4);
            for (s16 y = f; y < sub16(c, 1); y++) drv_hspan(CLIP_XMIN, a, y, 2);
        } else {
            drv_hspan(b, CLIP_XMAX, sub16(d, 1), 4);
            for (s16 y = e; y < sub16(d, 1); y++) drv_hspan(b, CLIP_XMAX, y, 2);
        }
    }
    if (!(F & 4) || Fq4) return;                            /* end of an upper storey: side face */
    if (F & 8) {
        if (left) { s16 t = sub16(add16(f, f), c); for (s16 y = t; y < f; y++) drv_hspan(sub16(add16(a, a), b), sub16(a, 1), y, 2); }
        else      { s16 t = sub16(add16(e, e), d); for (s16 y = t; y < e; y++) drv_hspan(add16(b, 1), sub16(add16(b, b), a), y, 2); }
    } else {
        if (left) { s16 t = sub16(f, (s16)(sub16(c, f) >> 1)); for (s16 y = t; y < f; y++) drv_hspan(sub16(add16(a, a), b), sub16(a, 1), y, 2); }
        else      { s16 t = sub16(e, (s16)(sub16(d, e) >> 1)); for (s16 y = t; y < e; y++) drv_hspan(add16(b, 1), sub16(add16(b, b), a), y, 2); }
    }
}

/* 2645:37af town_walls_mirror: only C & 1; next entry = ptr[+1]; left storeys always "half", right
 * storeys always "double" */
static void town_walls_mirror(s16 a, s16 b, s16 c, s16 d, s16 e, s16 f, s16 left)
{
    if (!(fword(DS_centre_ptr, 0) & 1)) return;
    u16 fp = left ? DS_left_ptr : DS_right_ptr;
    u16 F = fword(fp, 0);
    u16 Fn4 = fword(fp, 1) & 4;
    if (fword(DS_centre_ptr, 0) & 4) return;
    quad_fill((F & 0x10) ? 2 : 3, a, b, c, d, e, f);
    if (F & 4) {
        s16 t0, t1;
        if (left) { t0 = sub16(f, (s16)(sub16(c, f) >> 1)); t1 = sub16(e, (s16)(sub16(d, e) >> 1)); }
        else      { t0 = sub16(add16(f, f), c); t1 = sub16(add16(e, e), d); }
        quad_fill(4, a, b, f, e, t1, t0);
    }
    if (fword(DS_centre_ptr, 1) & 4) {
        if (left) {
            drv_hspan(CLIP_XMIN, a, sub16(c, 1), 4);
            for (s16 y = f; y < sub16(c, 1); y++) drv_hspan(CLIP_XMIN, a, y, 2);
        } else {
            drv_hspan(b, CLIP_XMAX, sub16(d, 1), 4);
            for (s16 y = e; y < sub16(d, 1); y++) drv_hspan(b, CLIP_XMAX, y, 2);
        }
    }
    if (!(F & 4) || Fn4) return;
    if (left) { s16 t = sub16(f, (s16)(sub16(c, f) >> 1)); for (s16 y = t; y < f; y++) drv_hspan(sub16(add16(a, a), b), sub16(a, 1), y, 2); }
    else      { s16 t = sub16(add16(e, e), d); for (s16 y = t; y < e; y++) drv_hspan(add16(b, 1), sub16(add16(b, b), a), y, 2); }
}

/* 2645:39de town_walls_front_left */
static void town_walls_front_left(void)
{
    town_walls_front(rowv(RQ_XOL), rowv(RP_XOL), rowv(RQ_Y), rowv(RP_Y), rowv(RP_YE), rowv(RQ_YE), 1);
}

/* 2645:3a0d town_walls_front_right */
static void town_walls_front_right(void)
{
    town_walls_front(rowv(RP_XOR), rowv(RQ_XOR), rowv(RP_Y), rowv(RQ_Y), rowv(RQ_YE), rowv(RP_YE), 0);
}

/* 2645:3a3b road_draw_row_front */
static void road_draw_row_front(s16 row)
{
    if (DSW(DS_racing) == 0) {
        town_walls_front_left();
        town_walls_front_right();
    }
    road_draw_band_front(row);
}

/* 2645:3a57 town_walls_mirror_left */
static void town_walls_mirror_left(void)
{
    town_walls_mirror(rowv(RQ_XOL), rowv(RP_XOL), rowv(RQ_Y), rowv(RP_Y), rowv(RP_YE), rowv(RQ_YE), 1);
}

/* 2645:3a86 town_walls_mirror_right */
static void town_walls_mirror_right(void)
{
    town_walls_mirror(rowv(RP_XOR), rowv(RQ_XOR), rowv(RP_Y), rowv(RQ_Y), rowv(RQ_YE), rowv(RP_YE), 0);
}

/* 2645:3ab4 road_draw_row_mirror */
static void road_draw_row_mirror(void)
{
    town_walls_mirror_left();
    town_walls_mirror_right();
    road_draw_band_mirror();
}

/* ================================================================================ overlays */

/* 2645:3dc3 road_draw_start_light (races) */
static void road_draw_start_light(void)
{
    if (DSS(DS_start_light_h) <= 0) return;
    Rect r;
    r.w = 0x18;
    r.h = DSS(DS_start_light_h);
    if (DSW(DS_lights_green) != 0) { DSW(DS_light_flag) = 1; r.sx = 0; }
    else r.sx = 0x18;
    r.sy = sub16(0x20, DSS(DS_start_light_h));
    r.dx = 0x82;
    r.dy = 0x12;
    drv_blit_masked(ds_far(DS_pic_start_light), back_page(), &r, 1);
}

/* 2645:3e3c road_draw_cockpit_overlay */
static void road_draw_cockpit_overlay(void)
{
    FarPtr p = ds_far(DS_pic_pillar);
    Rect r = { (s16)desc_w(p), (s16)desc_h(p), 0, 0, 0, 0x12 };
    drv_blit_masked(p, back_page(), &r, 1);
    p = ds_far(DS_pic_hood);
    Rect r2 = { (s16)desc_w(p), (s16)desc_h(p), 0, 0, 0x58, 0x5F };
    drv_blit_masked(p, back_page(), &r2, 1);
}

/* 2645:3edb mirror_draw_background */
static void mirror_draw_background(void)
{
    FarPtr p = ds_far(DS_pic_mirror);
    Rect r = { (s16)desc_w(p), (s16)desc_h(p), 0, 0, 0xF0, 0x12 };
    drv_blit(p, back_page(), &r, 1);
    p = ds_far(DS_pic_mirror_sky);
    Rect r2 = { (s16)desc_w(p), 2, 0, 0, 0xF0, sub16(DSS(DS_mir_y7), 2) };
    blit_clip_y(&r2);
    drv_blit(p, back_page(), &r2, 1);
}

/* ================================================================================ opponent */

/* 2645:3f8d opponent_draw */
static void opponent_draw(void)
{
    s16 oseg = DSS(DS_opp_seg);
    s16 d = sub16(add16(shl16(sub16(oseg, DSS(DS_seg)), 3), DSS(DS_opp_seg_frac)), DSS(DS_substep));
    s16 ox = sub16(DSS(DS_opp_lat), DSS(DS_cam_x));
    s16 s;
    if (d >= 0x78)       s = -1;
    else if (d >= 0x10)  { s = (s16)(((s16)(d + 8) >> 4) + 2); if (s > 8) s = 8; }
    else if (d < -7)     s = (s16)(((s16)(0x10 - d) >> 4) + 8);
    else if (d <= 8) {
        if (abs16(ox) < 0xB4) s = (DSS(DS_opp_sprite) > 8) ? 9 : 2;
        else                  s = (ox < 0) ? 0 : 2;
    } else                s = (ox < -0xB4) ? 1 : 3;

    if (DSS(DS_cpu_speed) >= 4) {                           /* sprite hysteresis */
        s16 p = DSS(DS_opp_sprite);
        if (s == p) goto chosen;
        if (s < 0)  { if (p == 8) return; s = add16(p, 1); goto chosen; }  /* return: no state update */
        if (s > 12) { if (p == 12) return; s = add16(p, 1); goto chosen; }
        if (s == DSS(DS_opp_sprite_prev2)) goto chosen;
        if ((s <= 8 && p > 9) || (s >= 9 && p <= 8 && p > 0)) { s = sub16(p, 1); goto chosen; }
        if (p == 0)      s = (s < 9) ? 1 : 9;
        else if (p == 9) s = (s >= 9) ? 10 : (ox < 0 ? 0 : 2);
        else if (p == 1) s = (s < 1) ? 0 : 3;
        else if (p == 3 && s == 1) { /* keep 1 */ }
        else             s = (s > p) ? add16(p, 1) : sub16(p, 1);
    }
chosen:
    DSW(DS_opp_alongside) = (s == 0 || (s == 2 && d <= 10)) ? 1 : 0;
    DSW(DS_opp_rear_end) = (s == 2 && d <= 10 && abs16(ox) < 0xB4
                            && (DSS(DS_cpu_speed) < 4 ? 0x64 : 0xB4) > abs16(ox)
                            && DSS(DS_opp_seg_step) < DSS(DS_step_per_frame)) ? 1 : 0;
    DSS(DS_opp_sprite_prev2) = DSS(DS_opp_sprite);
    DSS(DS_opp_sprite) = s;
    DSS(DS_opp_substep_frac) = (d > 0x10) ? (s16)(add16(d, 8) & 0x0F) : 0;
    s16 z = d;
    if (s == 0) z = add16(z, 0x10);
    if (z < -2 && s == 2) z = add16(z, 8);
    s16 dy = sub16(DSS(DS_cur_height), TRKS(TRK_HGT, oseg));
    s16 dx = sub16(DSS(DS_heading), TRKS(TRK_YAW, oseg));
    s16 sx, sy;
    if (s < 9) {
        z = add16(z, 4);
        sx = road_proj_x(z, DSS(DS_opp_lat), dx);
        sy = road_proj_y(z, 0x7D0, dy);
    } else {
        z = (s16)-z;
        sx = road_proj_x_mirror(z, DSS(DS_opp_lat), (s16)-dx);
        sy = road_proj_y_mirror(z, 0x7D0, (s16)-dy);
    }
    /* s = -1 would read DS:8ACA as a picture pointer (race_render.md §8 4): kept as the original */
    FarPtr pic = ds_far((u16)(DS_opp_sprites + shl16(s, 2)));
    s16 pw = (s16)desc_w(pic), ph = (s16)desc_h(pic);
    Rect r;
    r.w = pw; r.h = ph;
    if (s == 0) {
        r.sx = add16(sub16(pw, sx), 0x40);
        if (r.sx < 0) r.sx = 0;
        r.sy = 0; r.dx = 0;
        r.dy = sy;
    } else if (s == 1) {
        r.sx = 0;
        r.dx = sub16(sx, pw);
        r.sy = 0;
        r.dy = add16(sub16(sy, ph), 0x11);
    } else {
        r.sx = 0;
        r.dx = sub16(sx, (s16)(pw >> 1));
        r.sy = 0;
        s16 bb = (s < 9) ? (sy < DSS(DS_ground_top_y) ? DSS(DS_ground_top_y) : sy) : sy;
        r.dy = sub16(bb, ph);
        if (s == 2 && add16(r.dy, ph) >= 0x65) r.dy = sub16(0x65, ph);
    }
    s16 switched = 0;
    if (s >= 9) { if (CLIP_XMIN == 0) { switched = 1; clip_set_mirror(); } }
    else        { if (CLIP_XMIN != 0) { switched = 1; clip_set_view(); } }
    if (add16(r.dx, r.w) < CLIP_XMIN || r.dx >= CLIP_XMAX) return;         /* clip NOT restored (§8) */
    if (s >= 9) blit_clip_xy(&r); else blit_clip_y(&r);
    drv_blit_masked(pic, back_page(), &r, 1);
    if (switched) {
        if (CLIP_XMIN == 0) clip_set_mirror(); else clip_set_view();
    }
}

/* ================================================================================ row objects */

/* the shapes of a row: position x, current row Y, depth, side, type */
static void rshape(s16 x, s16 depth, s16 side, s16 type)
{
    scenery_draw_shape(x, rowv(RP_Y), depth, side, type);
}

/* front row objects of rows 1..10 (4b8c: 5217..5155), race_render.md §4.11 */
static void draw_row_objects_front(s16 row)
{
    u16 c = fword(DS_centre_ptr, 0);
    s16 cross = (s16)(c & 1);
    u16 L = fword(DS_left_ptr, 0), R = fword(DS_right_ptr, 0);
    if (L & 0x20) rshape(cross ? rowv(RP_XL) : rowv(RP_XOL), row, -1, 0);           /* palm */
    if (L & 0x100) {                                                                /* shrub */
        if (cross)          rshape(rowv(RP_XL), row, -1, 4);
        else if (L & 0x200) rshape(sub16(add16(rowv(RP_XOL), rowv(RP_XOL)), rowv(RP_XL)), row, -1, 4);
        else                rshape(rowv(RP_XOL), row, -1, 4);
    }
    if (L & 0x800)  rshape(rowv(RP_XL), row, 1, 3);                                 /* road work ahead */
    if (L & 0x1000) rshape(rowv(RP_XL), row, 1, 6);                                 /* barricade */
    if (L & 1) {
        if (cross) {
            setreset_off();
            scenery_draw_sign_picture(rowv(RP_XL), (s16)(add16(rowv(RP_YE), rowv(RP_Y)) >> 1), row, 1, 0, 1);
            setreset_on();
        } else
            rshape(rowv(RP_XL), row, 1, 1);                                         /* post */
    }
    if (cross) {
        if (L & 0x40) scenery_draw_hydrant(rowv(RP_XL), rowv(RP_Y), row, 1);
        if (R & 0x40) scenery_draw_hydrant(rowv(RP_XR), rowv(RP_Y), row, -1);
    } else {
        if (L & 4) rshape(rowv(RP_XL), row, -1, 7);
        if (L & 8) rshape(rowv(RP_XL), row, -1, 8);
        if (L & 0x40) {
            s16 xo = rowv(RP_XOL);
            rshape((s16)-sub16((s16)(sub16(rowv(RP_XL), xo) >> 1), xo), row, -1, 0x0A);
        }
        if (L & 0x80) rshape(rowv(RP_XOL), row, -1, 0x0B);
        if (R & 4) rshape(rowv(RP_XR), row, 1, 7);
        if (R & 8) rshape(rowv(RP_XR), row, 1, 8);
        if (R & 0x40) rshape(rowv(RP_XOR), row, 1, 0x0A);
        if (R & 0x80) {
            s16 xo = rowv(RP_XOR);
            rshape(add16((s16)(sub16(xo, rowv(RP_XR)) >> 1), xo), row, 1, 0x0B);
        }
        if (c & 2) rshape(rowv(RP_XR), row, 1, 9);                                  /* telephone pole */
    }
    if (R & 0x20) rshape(cross ? rowv(RP_XR) : rowv(RP_XOR), row, 1, 0);
    if (R & 0x100) {
        if (cross)          rshape(rowv(RP_XR), row, 1, 4);
        else if (R & 0x200) rshape(sub16(shl16(rowv(RP_XOR), 1), rowv(RP_XR)), row, 1, 4);
        else                rshape(rowv(RP_XOR), row, 1, 4);
    }
    if (R & 0x2000) rshape(rowv(RP_XR), row, 1, 0x12);
    if (R & 0x4000) rshape(rowv(RP_XR), row, 1, 0x13);
    if (R & 0x800)  rshape(rowv(RP_XR), row, 1, 3);
    if (R & 0x1000) rshape(rowv(RP_XR), row, -1, 6);
    if (R & 1) {
        if (cross) {
            setreset_off();
            scenery_draw_sign_picture(rowv(RP_XR), (s16)(add16(rowv(RP_YE), rowv(RP_Y)) >> 1), row, -1, 0, 1);
            setreset_on();
        } else
            rshape(rowv(RP_XR), row, 1, 1);
    }
    if (c & 0xF000) {
        s16 y = rowv(RP_Y);
        s16 ytop = (s16)-sub16(shl16(sub16(y, rowv(RP_YE)), 2), y);
        scenery_draw_banner(rowv(RP_XL), rowv(RP_XR), ytop, y, row, 0, c & 0xF000);
    }
    if (c & 0x100) rshape(rowv(RP_XR), row, 1, 5);                                  /* speed limit */
    if (c & 0x800) rshape(rowv(RP_XR), row, 1, 0x0F);                               /* road narrows */
    if (c & 0x200) rshape(rowv(RP_XR), row, 1, 0x10);                               /* chevron '<' */
    if (c & 0x400) rshape(rowv(RP_XR), row, 1, 0x11);                               /* chevron '>' */
}

/* mirror row objects of rows 1..5 (43fd: 4a72..4996), depth = row + 5 */
static void draw_row_objects_mirror(s16 row)
{
    s16 dep = add16(row, 5);
    u16 c = fword(DS_centre_ptr, 0);
    s16 cross = (s16)(c & 1);
    u16 L = fword(DS_left_ptr, 0), R = fword(DS_right_ptr, 0);
    if (L & 0x20) rshape(cross ? rowv(RP_XL) : rowv(RP_XOL), dep, -1, 0);
    if (L & 0x100) {
        if (cross)          rshape(rowv(RP_XL), dep, -1, 4);
        else if (L & 0x200) rshape(sub16(add16(rowv(RP_XOL), rowv(RP_XOL)), rowv(RP_XL)), dep, -1, 4);
        else                rshape(rowv(RP_XOL), dep, -1, 4);
    }
    if (L & 0x800)  rshape(rowv(RP_XL), dep, 1, 3);
    if (L & 0x1000) rshape(rowv(RP_XL), dep, 1, 6);
    if (cross) {
        if (L & 0x40) scenery_draw_hydrant(rowv(RP_XL), rowv(RP_Y), dep, 1);
        if (R & 0x40) scenery_draw_hydrant(rowv(RP_XR), rowv(RP_Y), dep, -1);
    } else {
        if (L & 1) rshape(rowv(RP_XL), dep, 1, 1);
        if (L & 4) rshape(rowv(RP_XL), dep, -1, 7);
        if (L & 8) rshape(rowv(RP_XL), dep, -1, 8);
        if (L & 0x40) {
            s16 xo = rowv(RP_XOL);
            rshape((s16)-sub16((s16)(sub16(rowv(RP_XL), xo) >> 1), xo), dep, -1, 0x0A);
        }
        if (L & 0x80) rshape(rowv(RP_XOL), dep, -1, 0x0B);
        if (R & 1) rshape(rowv(RP_XR), dep, 1, 1);
        if (R & 4) rshape(rowv(RP_XR), dep, 1, 7);
        if (R & 8) rshape(rowv(RP_XR), dep, 1, 8);
        if (R & 0x40) rshape(rowv(RP_XOR), dep, 1, 0x0A);
        if (R & 0x80) {
            s16 xo = rowv(RP_XOR);
            rshape(add16((s16)(sub16(xo, rowv(RP_XR)) >> 1), xo), dep, 1, 0x0B);
        }
        if (c & 2) rshape(rowv(RP_XR), dep, 1, 9);
    }
    if (R & 0x20) rshape(cross ? rowv(RP_XR) : rowv(RP_XOR), dep, 1, 0);
    if (R & 0x100) {
        if (cross)          rshape(rowv(RP_XR), dep, 1, 4);
        else if (R & 0x200) rshape(sub16(shl16(rowv(RP_XOR), 1), rowv(RP_XR)), dep, 1, 4);
        else                rshape(rowv(RP_XOR), dep, 1, 4);
    }
    if (R & 0x2000) rshape(rowv(RP_XR), dep, 1, 0x12);
    if (R & 0x4000) rshape(rowv(RP_XR), dep, 1, 0x13);
    if (R & 0x800)  rshape(rowv(RP_XR), dep, 1, 3);
    if (R & 0x1000) rshape(rowv(RP_XR), dep, -1, 6);
    if (c & 0x100) rshape(rowv(RP_XR), dep, 1, 5);
    if (c & 0x200) rshape(rowv(RP_XR), dep, 1, 0x10);
    if (c & 0x400) rshape(rowv(RP_XR), dep, 1, 0x11);
    if (c & 0x800) rshape(rowv(RP_XR), dep, 1, 0x0F);
}

/* ================================================================================ mirror, frame */

static void set_row_ptrs(u16 xl, u16 xr, u16 y, u16 xol, u16 xor_, u16 ye, u16 dash)
{
    DSW(RP_XL) = xl;   DSW(RQ_XL) = (u16)(xl - 2);
    DSW(RP_XR) = xr;   DSW(RQ_XR) = (u16)(xr - 2);
    DSW(RP_Y) = y;     DSW(RQ_Y) = (u16)(y - 2);
    DSW(RP_XOL) = xol; DSW(RQ_XOL) = (u16)(xol - 2);
    DSW(RP_XOR) = xor_; DSW(RQ_XOR) = (u16)(xor_ - 2);
    DSW(RP_YE) = ye;   DSW(RQ_YE) = (u16)(ye - 2);
    DSW(DS_dash_ptr) = dash;
}

/* all twelve near row pointers and the dash pointer -= 2 */
static void row_ptrs_step(void)
{
    static const u16 ptrs[] = { RQ_XL, RQ_XR, RQ_Y, RP_XL, RP_XR, RP_Y, DS_dash_ptr,
                                RP_XOL, RP_XOR, RP_YE, RQ_XOL, RQ_XOR, RQ_YE };
    for (unsigned i = 0; i < sizeof ptrs / sizeof ptrs[0]; i++) DSW(ptrs[i]) = (u16)(DSW(ptrs[i]) - 2);
}

/* 2645:43fd road_draw_mirror (called in fill mode) */
static void road_draw_mirror(void)
{
    set_row_ptrs((u16)(DS_mir_xl + 14), (u16)(DS_mir_xr + 14), (u16)(DS_mir_y + 14),
                 (u16)(DS_mir_xol + 14), (u16)(DS_mir_xor + 14), (u16)(DS_mir_yeye + 14),
                 (u16)(DS_dash_width_mirror + 14));
    ds_far_wr(DS_left_ptr, far_add(ds_far(DS_seg_left_ptr), (u16)-0x10));     /* &arr[seg-8] */
    ds_far_wr(DS_right_ptr, far_add(ds_far(DS_seg_right_ptr), (u16)-0x10));
    ds_far_wr(DS_centre_ptr, far_add(ds_far(DS_seg_centre_ptr), (u16)-0x10));
    setreset_off();
    mirror_draw_background();
    setreset_on();
    clip_set_mirror();
    for (s16 row = 7; row != 0; row--) {
        road_draw_row_mirror();
        u16 c = fword(DS_centre_ptr, 0);
        if (!(c & 4)) {
            if (c & 0xF000) {
                s16 y = rowv(RP_Y);
                s16 ytop = (s16)-sub16(shl16(sub16(y, rowv(RP_YE)), 2), y);
                scenery_draw_banner(rowv(RP_XL), rowv(RP_XR), ytop, y, add16(row, 5), 1,
                                    fword(DS_centre_ptr, 0) & 0xF000);
            }
            if (row < 6) draw_row_objects_mirror(row);
        }
        if (sub16(DSS(DS_seg), DSS(DS_opp_seg)) == row && DSW(DS_racing) != 0) {
            setreset_off();
            opponent_draw();
            setreset_on();
        }
        row_ptrs_step();
        fadv(DS_left_ptr, 2);
        fadv(DS_right_ptr, 2);
        fadv(DS_centre_ptr, 2);
    }
    clip_set_view();
    road_vline(2, 0xF0, 0x17, 0x2A);                        /* mirror frame */
    road_vline(4, 0xF1, 0x17, 0x2A);
    road_vline(4, 0x13E, 0x17, 0x2A);
    road_vline(2, 0x13F, 0x17, 0x2A);
    Rect r = { 0x20, sub16((s16)desc_h(ds_far(DS_pic_mirror)), 6), 0xF8, 0x16, 8,
               (s16)((DSS(DS_driver_id) == -2 ? 0 : 0x64) + 3) };
    drv_blit(back_page(), ds_far(DS_page0_ptr), &r, 1);     /* mirror centre -> page 0 (still in fill mode) */
}

/* 2645:4b8c road_draw_frame */
static void road_draw_frame(void)
{
    set_row_ptrs((u16)(DS_row_xl + 28), (u16)(DS_row_xr + 28), (u16)(DS_row_y + 28),
                 (u16)(DS_row_xol + 28), (u16)(DS_row_xor + 28), (u16)(DS_row_yeye + 28),
                 (u16)(DS_dash_width_front + 28));
    ds_far_wr(DS_left_ptr, far_add(ds_far(DS_seg_left_ptr), 0x1C));           /* &arr[seg+14] */
    ds_far_wr(DS_right_ptr, far_add(ds_far(DS_seg_right_ptr), 0x1C));
    ds_far_wr(DS_centre_ptr, far_add(ds_far(DS_seg_centre_ptr), 0x1C));
    clip_set_view();
    road_draw_sky();
    setreset_on();
    for (s16 row = 14; row != 0; row--) {
        if (row <= DSS(DS_top_band_row)) road_draw_row_front(row);
        if (!(fword(DS_centre_ptr, 0) & 4) && row < 11)
            draw_row_objects_front(row);
        if (sub16(DSS(DS_opp_seg), DSS(DS_seg)) == row && DSW(DS_racing) != 0) {
            setreset_off();
            opponent_draw();
            setreset_on();
        }
        row_ptrs_step();
        fadv(DS_left_ptr, -2);
        fadv(DS_right_ptr, -2);
        fadv(DS_centre_ptr, -2);
    }
    if (DSS(DS_opp_seg) == DSS(DS_seg) && DSW(DS_racing) != 0) {
        setreset_off();
        opponent_draw();
        setreset_on();
    }
    road_draw_mirror();
    setreset_off();
    road_draw_cockpit_overlay();
    if (DSW(DS_racing) != 0) road_draw_start_light();
    road_page_flip();
}
