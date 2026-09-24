/* Track builder of segment 2645 (port/spec/race_render.md §4.2, §4.3, §5.1, §5.2): the town and the
 * race road at new game (track_build_all 2645:0d48) and the course part (track_build_course
 * 2645:0b91) with its eleven features 2645:017a..0abb.
 *
 * The five track arrays live in far segment 389b (TRKW/TRKS, race_int.h) and are written in place;
 * like the original there are no bounds checks (L[i-1] at i = 0 reads 389b:33EE, the prefill of a
 * feature near the end of the road writes past YAW[0x45F] into C[]), so the flat layout of mem[]
 * gives the same results. The build state (DS:6DFE yaw, DS:6E00 cursor, DS:6E02 yaw step) stays in
 * DGROUP. All arithmetic is 16-bit as in the original (idiv / imul low word). */
#include "game/race_int.h"
#include "game/race.h"
#include "platform/platform.h"

#define L_   TRK_L
#define R_   TRK_R
#define YAW_ TRK_YAW
#define C_   TRK_C
#define HGT_ TRK_HGT

/* build state (DGROUP) */
#define BUILD_YAW  DSS(DS_build_yaw)        /* DS:6DFE */
#define BUILD_SEG  DSS(DS_build_seg)        /* DS:6E00 */
#define BUILD_STEP DSS(DS_build_yaw_step)   /* DS:6E02 */

/* 2645:016a track_rnd: 0f38:5eb6 rnd(n), 0 .. n-1 */
static s16 track_rnd(s16 n)
{
    return rnd(n);
}

/* ------------------------------------------------------------------------------ feature helpers */

/* YAW[pos .. pos+n-1] = yaw; pos not advanced (the "cx = n; rep" prologue of every curve feature,
 * guarded by pos < pos + n in 16 bits) */
static void prefill(s16 n)
{
    s16 pos = BUILD_SEG;
    if (!((s16)pos < (s16)(pos + n))) return;
    s16 yaw = BUILD_YAW;
    for (s16 j = 0; j < n; j++) TRKS(YAW_, pos + j) = yaw;
}

/* Heading ramp of the curve features (2645:0206 / 031b / 042b / 04b2 / 05bc / 0643): delta > 0 turns
 * left (heading grows), delta < 0 right. Every ramped segment gets the dash bit (solid centre line).
 * The ramp stops at segment 0x442. The yaw is always re-stored as b * step. */
static void ramp(s16 delta)
{
    s16 step = BUILD_STEP;
    s16 b = idiv32_16((s32)BUILD_YAW, step, NULL);           /* cwd / idiv */
    s16 target = (s16)(b + delta);
    if (delta > 0 ? (target > b) : (target < b)) {
        s16 di = b, cx = BUILD_SEG;
        s16 si = (s16)(b * step);                           /* imul, low word */
        while (cx < 0x442) {
            TRKW(C_, cx) |= 8;                              /* or byte ptr es:[bx], 8 */
            TRKS(YAW_, cx) = si;
            if (delta > 0) { si = (s16)(si + step); di++; }
            else           { si = (s16)(si - step); di--; }
            cx++;
            if (delta > 0 ? !(di < target) : !(di > target)) break;
        }
        b = di;
        BUILD_SEG = cx;
    }
    BUILD_YAW = (s16)(b * step);
}

/* L[i] &= 0x1801; R[i] &= 0x1801 (keep bit 0 and the road-works sign/barricade) */
static void keep_side_flags(s16 i)
{
    TRKW(L_, i) &= 0x1801;
    TRKW(R_, i) &= 0x1801;
}

/* YAW[s .. pos-1] = yaw (epilogue of the hill / lane features, guarded by s < pos) */
static void yaw_fill_from(s16 s)
{
    s16 pos = BUILD_SEG;
    if (!(s < pos)) return;
    s16 yaw = BUILD_YAW;
    for (s16 j = s; j != pos; j++) TRKS(YAW_, j) = yaw;
}

/* height profile of the hill features: n values of DS:tab (negated when neg) from pos, stops at 0x442 */
static void height_profile(u16 tab, s16 n, bool neg)
{
    s16 cx = BUILD_SEG;
    for (s16 k = 0; k < n; k++) {
        if (!(cx < 0x442)) break;
        s16 v = DSS((u16)(tab + 2 * k));
        TRKS(HGT_, cx) = neg ? (s16)-v : v;
        cx++;
    }
    BUILD_SEG = cx;
}

/* ----------------------------------------------------------------------------------- features */

/* 2645:017a track_feat_curve_left (feature 1) */
static void track_feat_curve_left(void)
{
    prefill(0x32);
    TRKW(C_, BUILD_SEG) |= 0x0200;                          /* chevron '<' */
    BUILD_SEG += 5;
    keep_side_flags(BUILD_SEG);
    TRKW(C_, BUILD_SEG) |= 0x0200;
    BUILD_SEG += 6;
    ramp(+0x28);
}

/* 2645:0293 track_feat_curve_right (feature 2) */
static void track_feat_curve_right(void)
{
    prefill(0x32);
    keep_side_flags(BUILD_SEG);
    TRKW(C_, BUILD_SEG) |= 0x0400;                          /* chevron '>' */
    BUILD_SEG += 3;
    TRKW(C_, BUILD_SEG) |= 0x0400;
    BUILD_SEG += 3;
    ramp(-0x2D);
}

/* 2645:03a8 track_feat_s_bend_left (feature 10) */
static void track_feat_s_bend_left(void)
{
    prefill(0x5A);
    BUILD_SEG++;
    keep_side_flags(BUILD_SEG);
    TRKW(R_, BUILD_SEG) |= 0x2000;                          /* sign shape 0x12 */
    BUILD_SEG += 6;
    TRKW(R_, BUILD_SEG) |= 0x2000;
    ramp(+0x28);
    ramp(-0x2B);
}

/* 2645:0539 track_feat_s_bend_right (feature 11) */
static void track_feat_s_bend_right(void)
{
    prefill(0x5A);
    BUILD_SEG++;
    keep_side_flags(BUILD_SEG);
    TRKW(R_, BUILD_SEG) |= 0x4000;                          /* sign shape 0x13 */
    BUILD_SEG += 6;
    TRKW(R_, BUILD_SEG) |= 0x4000;
    ramp(-0x23);
    ramp(+0x30);
}

/* 2645:06ca track_feat_hill (feature 3) */
static void track_feat_hill(void)
{
    s16 s = BUILD_SEG;
    BUILD_SEG += 0x14;
    height_profile(DS_hill_profile_22, 0x16, false);
    BUILD_SEG += 8;
    yaw_fill_from(s);
}

/* 2645:075d track_feat_dip (feature 4) */
static void track_feat_dip(void)
{
    s16 s = BUILD_SEG;
    BUILD_SEG += 0x0A;
    height_profile(DS_hill_profile_22, 0x16, true);
    BUILD_SEG += 0x12;
    yaw_fill_from(s);
}

/* 2645:07f2 track_feat_hills_long (feature 5) */
static void track_feat_hills_long(void)
{
    s16 s = BUILD_SEG;
    BUILD_SEG += 6;
    height_profile(DS_hill_profile_38, 0x26, false);
    BUILD_SEG += 6;
    yaw_fill_from(s);
}

/* 2645:0885 track_feat_dips_long (feature 6) */
static void track_feat_dips_long(void)
{
    s16 s = BUILD_SEG;
    BUILD_SEG += 8;
    height_profile(DS_hill_profile_38, 0x26, true);
    BUILD_SEG += 4;
    yaw_fill_from(s);
}

/* 2645:091a track_feat_roadworks_right (feature 7): right lane closed for 10 segments */
static void track_feat_roadworks_right(void)
{
    s16 s = BUILD_SEG;
    BUILD_SEG += 3;
    TRKW(R_, BUILD_SEG) |= 0x0800;                          /* "road work ahead" */
    BUILD_SEG += 4;
    TRKW(C_, BUILD_SEG) &= 0x0FF7; BUILD_SEG++;
    TRKW(R_, BUILD_SEG) |= 0x1000; BUILD_SEG++;             /* barricade */
    s16 p = BUILD_SEG;
    BUILD_SEG += 0x0A;
    for (s16 k = 0; k < 0x0A; k++, p++) {                   /* no 0x442 bound (original) */
        TRKW(C_, p) |= 0x40;
        TRKW(C_, p) &= 0x0FF7;
    }
    TRKW(C_, BUILD_SEG) &= 0x0FF7; BUILD_SEG++;
    BUILD_SEG += 3;
    yaw_fill_from(s);
}

/* 2645:09f0 track_feat_narrow_bridge (feature 9): one lane for 20 segments */
static void track_feat_narrow_bridge(void)
{
    s16 s = BUILD_SEG;
    TRKW(C_, BUILD_SEG) |= 0x0800;                          /* "road narrows" */
    BUILD_SEG += 4;
    keep_side_flags(BUILD_SEG);
    TRKW(C_, BUILD_SEG) |= 0x0800;
    BUILD_SEG += 6;
    s16 p = BUILD_SEG;
    BUILD_SEG += 0x14;
    for (s16 k = 0; k < 0x14; k++, p++) {
        TRKW(C_, p) |= 0x80;
        TRKW(C_, p) &= 0x0FF7;
    }
    TRKW(C_, BUILD_SEG) &= 0x0FFF;                          /* and byte ptr [+1], 0x0F */
    yaw_fill_from(s);
}

/* 2645:0abb track_feat_roadworks_left (feature 8): left side closed for 10 segments */
static void track_feat_roadworks_left(void)
{
    s16 s = BUILD_SEG;
    BUILD_SEG += 4;
    TRKW(L_, BUILD_SEG) |= 0x0800;
    BUILD_SEG += 4;
    TRKW(C_, BUILD_SEG) &= 0x0FF7; BUILD_SEG++;
    TRKW(L_, BUILD_SEG) |= 0x1000; BUILD_SEG++;
    s16 p = BUILD_SEG;
    BUILD_SEG += 0x0A;
    for (s16 k = 0; k < 0x0A; k++, p++) {
        TRKW(C_, p) |= 0x20;
        TRKW(C_, p) &= 0x0FF7;
    }
    TRKW(C_, BUILD_SEG) &= 0x0FF7; BUILD_SEG++;
    BUILD_SEG += 2;
    yaw_fill_from(s);
}

/* ------------------------------------------------------------------------------------ course */

/* 2645:0b91 track_build_course */
void track_build_course(s16 arg)
{
    BUILD_SEG = 0x186;
    BUILD_YAW = 0;
    BUILD_STEP = DSW(DS_slow_machine) ? 2 : 3;
    s16 per = (s16)((DSS(DS_slow_machine) << 2) + 4);
    for (s16 j = 0; j < 0x2E4; j++) TRKW(YAW_, 0x17C + j) = 0;     /* rep stosw */
    for (s16 j = 0; j < 0x2E4; j++) TRKW(HGT_, 0x17C + j) = 0;
    for (s16 i = 0x17C; i < 0x460; i++) {
        TRKW(C_, i) &= 0xF117;
        TRKW(L_, i) &= 0x87FF;                              /* high byte &= 87h */
        TRKW(R_, i) &= 0xE7FF;                              /* high byte &= E7h */
        s16 rem;
        idiv32_16((s32)(s16)(i + 1), per, &rem);
        if (rem == 0) TRKW(C_, i) |= 8;
    }
    if (arg == 100) DSS(DS_course_layout) = track_rnd(10);
    for (s16 k = 0; k < DSS(DS_course_len); k++) {
        s16 f = DSS((u16)(DS_course_layouts + (s16)(0x18 * DSS(DS_course_layout)) + 2 * k));
        switch (f) {                                        /* jump table 2645:0cc8, 1..11 */
        case 1:  track_feat_curve_left();      break;
        case 2:  track_feat_curve_right();     break;
        case 3:  track_feat_hill();            break;
        case 4:  track_feat_dip();             break;
        case 5:  track_feat_hills_long();      break;
        case 6:  track_feat_dips_long();       break;
        case 7:  track_feat_roadworks_right(); break;
        case 8:  track_feat_roadworks_left();  break;
        case 9:  track_feat_narrow_bridge();   break;
        case 10: track_feat_s_bend_left();     break;
        case 11: track_feat_s_bend_right();    break;
        default: break;
        }
    }
    if (BUILD_SEG < 0x460) {                                /* rest of the road: flat, final heading */
        s16 start = BUILD_SEG;
        s16 n = (s16)(0x460 - start);
        for (s16 j = 0; j < n; j++) TRKW(HGT_, start + j) = 0;
        BUILD_SEG += n;
        s16 yaw = BUILD_YAW;
        for (s16 j = 0; j < n; j++) TRKS(YAW_, start + j) = yaw;
    }
}

/* ------------------------------------------------------------------------------------ whole track */

static bool divides(s16 v, s16 d)                   /* cwd / idiv, remainder == 0 */
{
    s16 rem;
    idiv32_16((s32)v, d, &rem);
    return rem == 0;
}

/* overhead route sign: s = base + rnd(n), (R[s] & 2: s-1, never set), clear the side flags, C |= bit */
static void route_sign(s16 n, s16 base, u16 bit)
{
    s16 s = (s16)(track_rnd(n) + base);
    if (TRKW(R_, s) & 2) s--;
    keep_side_flags(s);
    TRKW(C_, s) |= bit;
}

/* 2645:0d48 track_build_all */
void track_build_all(void)
{
    if (DSS(DS_driver_id) == -2) {                          /* VGA: pages A000 / A200 / A400 */
        /* PORT: the far segments the original stores are relocated at load time: SEG() */
        ds_far_wr(DS_page0_ptr, far_make(SEG(0x2FA3), 0x0000));
        ds_far_wr(DS_page1_ptr, far_make(DGROUP, DS_page1_desc));
        ds_far_wr(DS_page2_ptr, far_make(DGROUP, DS_page2_desc));
    } else {                                                /* other drivers (parked) */
        ds_far_wr(DS_page0_ptr, ds_far(DS_g_front));
        ds_far_wr(DS_page1_ptr, ds_far(DS_g_back));
        ds_far_wr(DS_page2_ptr, far_make(SEG(0x2E3E), 0x0060));
    }
    s16 lvl = DSS(DS_slow_machine);                         /* [bp-2] */
    s16 i;

    /* ---- town: segments 0 .. 0xB3 */
    for (i = 0; i < 0xB4; i++) {
        TRKW(HGT_, i) = 0; TRKW(YAW_, i) = 0; TRKW(C_, i) = 0; TRKW(R_, i) = 0; TRKW(L_, i) = 0;
        TRKW(C_, i) |= 1;
        if (divides((s16)(i + 1), (s16)((lvl << 2) + 4))) TRKW(C_, i) |= 8;
        if (divides((s16)(i + 2), 0x10)) {                  /* intersection */
            TRKW(C_, i + 2) |= 4; TRKW(C_, i + 1) |= 4; TRKW(C_, i) |= 4;
            TRKW(R_, i - 1) |= 0x40;
            TRKW(L_, i + 3) |= 0x40;
            TRKW(C_, i + 1) |= 1; TRKW(C_, i + 2) |= 1; TRKW(C_, i + 3) |= 1;
            TRKW(HGT_, i + 1) = 0; TRKW(YAW_, i + 1) = 0;
            TRKW(HGT_, i + 2) = 0; TRKW(YAW_, i + 2) = 0;
            TRKW(HGT_, i + 3) = 0; TRKW(YAW_, i + 3) = 0;
            i += 3;
            continue;
        }
        /* left buildings */
        if (divides((s16)(i + 4), 0x10) || divides((s16)(i + 8), 0x10) || divides((s16)(i + 12), 0x10)) {
            if (track_rnd(2) == 0) {
                TRKW(L_, i - 1) |= 4; TRKW(L_, i) |= 4;
                if (track_rnd(3) == 0) { TRKW(L_, i - 1) |= 8; TRKW(L_, i) |= 8; }
                TRKW(L_, i) |= 0x10;
            } else {
                TRKW(L_, i - 2) |= 4; TRKW(L_, i) |= 4; TRKW(L_, i - 1) |= 4;
                if (track_rnd(3) == 0) { TRKW(L_, i - 2) |= 8; TRKW(L_, i) |= 8; TRKW(L_, i - 1) |= 8; }
                TRKW(L_, i - 1) |= 0x10;
            }
        } else
            TRKW(L_, i) |= 0x80;
        /* right buildings */
        if (divides((s16)(i + 5), 0x10) || divides((s16)(i + 9), 0x10) || divides((s16)(i + 12), 0x10)) {
            if (track_rnd(3) != 0) {
                TRKW(R_, i - 1) |= 4; TRKW(R_, i) |= 4;
                if (track_rnd(2) == 0) { TRKW(R_, i - 1) |= 8; TRKW(R_, i) |= 8; }
                TRKW(R_, i) |= 0x10;
            } else {
                TRKW(R_, i - 2) |= 4; TRKW(R_, i) |= 4; TRKW(R_, i - 1) |= 4;
                if (track_rnd(4) == 0) { TRKW(R_, i - 2) |= 8; TRKW(R_, i) |= 8; TRKW(R_, i - 1) |= 8; }
                TRKW(R_, i - 1) |= 0x10;
            }
        } else
            TRKW(R_, i) |= 0x80;
        if (track_rnd((s16)(lvl * 0x28 + 6)) == 0 && !(TRKW(L_, i) & 0x10)) TRKW(L_, i) |= 0x20;
        if (track_rnd((s16)(lvl * 0x28 + 7)) == 0 && !(TRKW(R_, i) & 0x10)) TRKW(R_, i) |= 0x20;
        if (track_rnd((s16)(lvl * 0x28 + 3)) == 0 && !(TRKW(L_, i) & 0x10)) TRKW(L_, i) |= 0x100;
        if (track_rnd((s16)(lvl * 0x28 + 4)) == 0 && !(TRKW(R_, i) & 0x10)) TRKW(R_, i) |= 0x100;
    }
    /* town billboards */
    TRKW(C_, track_rnd(0x2D) + 0x0A) |= 0x4000;
    TRKW(C_, track_rnd(0x2D) + 0x0A) |= 0x8000;
    TRKW(C_, track_rnd(0x37) + 0x41) |= 0x4000;
    TRKW(C_, track_rnd(0x37) + 0x41) |= 0x8000;
    TRKW(C_, track_rnd(0x37) + 0x73) |= 0x4000;
    TRKW(C_, track_rnd(0x37) + 0x73) |= 0x8000;
    TRKW(R_, 0x2C) |= 1;                                    /* location signs, road works */
    TRKW(L_, 0x1D) |= 0x1000;
    TRKW(L_, 0x18) |= 0x0800;
    TRKW(L_, 0x6C) |= 1;
    TRKW(L_, 0x57) |= 0x1000;
    TRKW(L_, 0x52) |= 0x0800;
    TRKW(L_, 0x9C) |= 1;
    TRKW(L_, 0x87) |= 0x1000;
    TRKW(L_, 0x82) |= 0x0800;

    /* ---- race road: segments 0xB4 .. 0x45F */
    for (i = 0xB4; i < 0x460; i++) {
        TRKW(HGT_, i) = 0; TRKW(YAW_, i) = 0; TRKW(C_, i) = 0; TRKW(L_, i) = 0; TRKW(R_, i) = 0;
        if (divides((s16)(i + 1), (s16)((DSS(DS_slow_machine) << 2) + 4))) TRKW(C_, i) |= 8;
        if (divides(i, (s16)((lvl << 2) + 4))) { TRKW(L_, i) |= 1; TRKW(R_, i) |= 1; }
        if (divides((s16)(i + 2), (s16)((lvl << 3) + 8))) TRKW(C_, i) |= 2;
        if (track_rnd((s16)(lvl * 0xC8 + 7)) == 0) TRKW(L_, i) |= 0x20;
        if (track_rnd((s16)(lvl * 0xC8 + 9)) == 0) TRKW(R_, i) |= 0x20;
        if (lvl == 0) {                                     /* 2645:14f7 */
            if (track_rnd(0x23) == 0) {
                TRKW(L_, i) |= 0x40; TRKW(L_, i - 1) |= 0x40; TRKW(L_, i - 2) |= 0x40;
                TRKW(L_, i) &= ~0x20; TRKW(L_, i - 1) &= ~0x20; TRKW(L_, i - 2) &= ~0x20;
            }
            if (track_rnd(0x19) == 0) {
                TRKW(R_, i) |= 0x40; TRKW(R_, i - 2) |= 0x40;
                TRKW(R_, i) &= ~0x20; TRKW(R_, i - 2) &= ~0x20;
            }
        } else {                                            /* 2645:120b */
            if (track_rnd((s16)(lvl * 0x64 + 0x23)) == 0) { TRKW(L_, i) |= 0x40; TRKW(L_, i) &= ~0x20; }
            if (track_rnd((s16)(lvl * 0x64 + 0x19)) == 0) { TRKW(R_, i) |= 0x40; TRKW(R_, i) &= ~0x20; }
        }
        /* 2645:1267 */
        if (track_rnd((s16)(lvl * 0x64 + 7)) == 0 && !(TRKW(L_, i) & 0x80)) TRKW(L_, i) |= 0x80;
        if (track_rnd((s16)(lvl * 0x64 + 9)) == 0 && !(TRKW(R_, i) & 0x80)) TRKW(R_, i) |= 0x80;
        if (track_rnd((s16)(lvl * 0x64 + 0x15)) == 0) TRKW(L_, i) |= 4;
        if (track_rnd((s16)(lvl * 0x64 + 0x19)) == 0) TRKW(R_, i) |= 4;
        if (track_rnd((s16)(lvl * 0x64 + 0x17)) == 0 && !(TRKW(L_, i) & 4)) TRKW(L_, i) |= 8;
        if (track_rnd((s16)(lvl * 0x64 + 0x1E)) == 0 && !(TRKW(R_, i) & 4)) TRKW(R_, i) |= 8;
        if (track_rnd((s16)(lvl * 0x32 + 4)) == 0) {
            TRKW(L_, i) |= 0x100;
            if (track_rnd(2) == 0) TRKW(L_, i) |= 0x200;
        }
        if (track_rnd((s16)(lvl * 0x32 + 4)) == 0) {
            TRKW(R_, i) |= 0x100;
            if (track_rnd(2) == 0) TRKW(R_, i) |= 0x200;
        }
    }
    /* finish lines */
    TRKW(L_, 0x17B) &= 0x1801; TRKW(R_, 0x17B) &= 0x1801; TRKW(C_, 0x17B) |= 0x1010;
    TRKW(L_, 0x3FB) &= 0x1801; TRKW(R_, 0x3FB) &= 0x1801; TRKW(C_, 0x3FB) |= 0x2010;
    /* overhead route signs */
    route_sign(0x8C, 0xDC, 0x8000);
    route_sign(0x258, 0x186, 0x8000);
    route_sign(0x258, 0x186, 0x4000);
    /* hills on the first race stretch */
    i = 0xDC;
    do {
        for (s16 k = 0; k < 0x11; k++) TRKS(HGT_, i + k) = DSS((u16)(DS_hill_profile_17 + 2 * k));
        i += 0x11; i += 0x14;
        for (s16 k = 0; k < 0x11; k++) TRKS(HGT_, i + k) = (s16)-DSS((u16)(DS_hill_profile_17 + 2 * k));
        i += 0x11; i += 0x1E;
    } while ((s16)(i + 0x5A) < 0x17C);
    TRKW(C_, 0x181) |= 0x100;                               /* speed-limit sign */
    track_build_course(100);
}
