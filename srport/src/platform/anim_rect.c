/* Rectangle and sprite-animation bookkeeping of segment 0f38 used by the EGA animation step 21a0:25d9
 * (video.md §4.7, table §4.9). Transcribed from the disassembly (tools/x86dis.py work/SR_unp.exe dis
 * 0f38:a0fa ...); all compares are signed as in the original.
 *
 * Rects are {x0, y0, x1, y1} (s16[4], inclusive), passed as C pointers (a DGROUP rect is
 * (s16 *)mp(DGROUP, off)); the functions return out or NULL like the original's AX.
 *
 * Data layout of the animations (DGROUP, near pointers = u16 DS offsets), confirmed from 21a0:25d9,
 * 0f38:aa37, 0f38:aac2 and the builder 0f38:a273 sprite_scene_setup (which fills DS:7D18 and the
 * tables below):
 *
 *   Anim (0x16 bytes, e.g. DS:7D18)
 *     +00 u16    ref      -> SpriteRef of the current frame
 *     +02 FarPtr bgsrc    Desc*: background the dirty rects are restored from (anim_restore_rect)
 *     +06 FarPtr bg       Desc*: background composited under the sprite; source of the overlays
 *     +0A u16    ovl[2]   -> s16[4] overlay rects (0 = none), relative to x (same stride as the
 *                         sprite) or to y; redrawn from bg with blit_masked over the sprite
 *     +0E s16    x        sprite position on the page (pixels)
 *     +10 s16    y
 *     +12 u16    frame    -> FrameRec (the script); count < 0 = finished
 *     +14 u16    node     -> PageNode of the page drawn last
 *
 *   FrameRec (4 bytes, consecutive; the script ends with count -1)
 *     +00 s16 count   frames left in this record (decremented per step; 0 -> next record)
 *     +02 s8  dx      added to x per step; also the "mode" of 21a0:25d9: dx != 0 = horizontal
 *                     motion (rect_subtract_edge horiz, anim_restore_rect by x0 bytes)
 *     +03 s8  dy      added to y per step
 *
 *   SpriteRef (12 bytes, e.g. DS:77BA + 12*i)
 *     +00 FarPtr desc     Desc* of this sprite frame
 *     +04 u16    tail     -> Tail {u16 off, seg, unused, rows} (8 bytes) or 0
 *     +06 u16    next[3]  -> SpriteRef chosen after a step with |dx|: next[|dx| >> 2]
 *                         (a273: next[0] = the following ref, next[k] = next[k-1]->next[k-1])
 *
 *   PageNode (16 bytes, ring, e.g. DS:7D42 + 16*i)
 *     +00 FarPtr page     Desc* of the page
 *     +04 s16    rect[4]  rect drawn on this page last time (initially 0,0,-1,-1)
 *     +0C s16    show     page number for ega_show_page (0, 1, ...)
 *     +0E u16    next     -> next PageNode
 *
 * Other globals: DS:8B7C (x before the step, read by the engine-sound code), DS:0288 (far Desc* of
 * the sprite before the step). */
#include "platform/video.h"

#define DS_anim_prev_desc 0x0288   /* FarPtr: sprite desc before the last sprite_anim_advance */
#define DS_view_desc      0x825A   /* FarPtr: its first word (width) is the view width */
#define DS_view_x         0x825E   /* s16 (symbols.h: car_x) */
#define DS_view_y         0x8260   /* s16 (symbols.h: car_y) */
#define DS_view_h         0x8262   /* s16 */

/* 0f38:a0fa: intersection of a and b into out, x0 rounded down and x1 up with DS:823E (FFF8h: x1 =
 * (x1 + 7) & ~7). NULL if empty. out[0]/out[2] are written before the y test (also when NULL). */
s16 *rect_intersect(const s16 *a, const s16 *b, s16 *out)
{
    if (b[2] < a[0]) return NULL;
    if (!(a[2] >= b[0])) return NULL;
    out[0] = a[0] < b[0] ? b[0] : a[0];
    out[2] = a[2] > b[2] ? b[2] : a[2];
    if (a[1] > b[3]) return NULL;
    if (a[3] < b[1]) return NULL;
    out[1] = a[1] < b[1] ? b[1] : a[1];
    out[3] = a[3] > b[3] ? b[3] : a[3];
    u16 m = DSW(DS_g_x_align);
    out[0] = (s16)(out[0] & m);
    out[2] = (s16)((u16)(out[2] + (u16)~m) & m);
    return out;
}

/* 0f38:a198: the rect x,y,w,h intersected with the view {DS:825E, DS:8260, +width of *DS:825A,
 * +DS:8262} */
s16 *rect_clip_to_view(s16 x, s16 y, s16 w, s16 h, s16 *out)
{
    s16 v[4], r[4];
    FarPtr vd = ds_far(DS_view_desc);
    v[0] = DSS(DS_view_x);
    v[2] = (s16)(rd16(vd.seg, vd.off) + DSS(DS_view_x) - 1);
    v[1] = DSS(DS_view_y);
    v[3] = (s16)(DSS(DS_view_y) + DSS(DS_view_h) - 1);
    r[0] = x;
    r[2] = (s16)(x + w - 1);
    r[1] = y;
    r[3] = (s16)(y + h - 1);
    return rect_intersect(v, r, out);
}

/* 0f38:a1ee: {x0,y0,x1,y1} of a w x h sprite at (x, y) clipped to maxw x maxh, x and w aligned with
 * DS:823E (the x alignment happens before the left clip, the width alignment last) */
void rect_clip_make(s16 w, s16 h, s16 x, s16 y, s16 maxw, s16 maxh, s16 *out)
{
    if (y < 0) { h = (s16)(h + y); y = 0; }
    if ((s16)(y + h) > maxh) {
        h = (s16)(maxh - y);
        if (h < 0) h = 0;
    }
    x = (s16)(x & DSW(DS_g_x_align));
    if (x < 0) { w = (s16)(w + x); x = 0; }
    if ((s16)(x + w) > maxw) {
        w = (s16)(maxw - x);
        if (w < 0) w = 0;
    }
    w = (s16)(w & DSW(DS_g_x_align));
    out[0] = x;
    out[2] = (s16)(x + w - 1);
    out[1] = y;
    out[3] = (s16)(y + h - 1);
}

/* 0f38:a4b9: out = the part of b (the previous rect) not covered by a (the new rect) along one axis
 * (horiz != 0: x, else y), i.e. the strip to restore. Empty -> out = {3E8h, 3E8h, -1, -1}, NULL. */
s16 *rect_subtract_edge(const s16 *a, const s16 *b, s16 *out, s16 horiz)
{
    for (int i = 0; i < 4; i++) out[i] = b[i];
    if (horiz) {
        if (!(b[0] > a[0]) && !(b[2] > a[2])) {          /* b starts and ends left of a's ends */
            if (!(b[2] < a[0])) {
                /* (b[2] > a[2] cannot hold here; the original's out[2] = b[2] branch is dead) */
                out[2] = (s16)(a[0] - 1);
            }
        } else {
            if (!(a[2] < b[0])) out[0] = (s16)(a[2] + 1);
            if (!(b[2] >= a[2])) out[2] = a[2];
        }
    } else {
        if (b[1] < a[1]) {
            if (!(b[3] < a[1])) out[3] = (s16)(a[1] - 1);
        } else if (b[1] > a[1] && !(b[1] > a[3])) {
            out[1] = (s16)(a[3] + 1);
        }
    }
    if (out[2] < out[0] || out[3] < out[1]) {
        out[0] = out[1] = 0x3E8;
        out[2] = out[3] = -1;
        return NULL;
    }
    return out;
}

/* 0f38:aa37: one step of the frame script: remember x (DS:8B7C) and the sprite desc (DS:0288),
 * count down the frame record (0 -> next record), then unless the script ended (-1) move by (dx, dy)
 * and switch the sprite to ref->next[|dx| >> 2] when dx != 0. */
void sprite_anim_advance(u16 anim)
{
    DSW(DS_anim_engine_ref) = DSW(anim + 0x0E);
    u16 ref = DSW(anim);
    DSW(DS_anim_prev_desc) = DSW(ref);
    DSW(DS_anim_prev_desc + 2) = DSW(ref + 2);
    DSW(DSW(anim + 0x12))--;
    if (DSW(DSW(anim + 0x12)) == 0) DSW(anim + 0x12) = (u16)(DSW(anim + 0x12) + 4);
    u16 fr = DSW(anim + 0x12);
    if (DSS(fr) == -1) return;
    DSW(anim + 0x0E) = (u16)(DSW(anim + 0x0E) + (s16)DSC(fr + 2));
    DSW(anim + 0x10) = (u16)(DSW(anim + 0x10) + (s16)DSC(fr + 3));
    s16 t = DSC(fr + 2);
    if (t < 0) t = (s16)-t;
    if (t == 0) return;
    u16 si = (u16)((t >> 1) & 0xFFFE);
    DSW(anim) = DSW(DSW(anim) + si + 6);
}

/* 0f38:aac2: horizontal part of the sprite (x aligned with DS:823E, width of its desc) inside the
 * span [x, x + w): fills the Rect at out (s16[6] {w, h, sx, sy, dx, dy}: w, sx, dx; h/sy/dy untouched),
 * x1 rounded up to a byte. 0 if they do not overlap. */
s16 sprite_hspan_clip(s16 x, s16 w, u16 anim, s16 *out)
{
    s16 sx = (s16)(DSW(anim + 0x0E) & DSW(DS_g_x_align));   /* [bp-4] */
    FarPtr d = ds_far(DSW(anim));
    s16 sw = (s16)desc_w(d);                                  /* [bp-2] */
    if ((s16)(x + w) <= sx) return 0;
    if (!((s16)(sx + sw) >= x)) return 0;
    s16 lo = sx < x ? x : sx;                                 /* [bp-6] */
    s16 hi = (s16)(sx + sw);
    s16 cx = (s16)(x + w);
    if (hi > cx) hi = cx;
    hi = (s16)((u16)(hi + (u16)~DSW(DS_g_x_align)) & DSW(DS_g_x_align));
    s16 off = (s16)(lo - sx);
    out[2] = off < 0 ? 0 : off;                               /* sub / jns: sign of the result */
    out[4] = lo;
    out[0] = (s16)(hi - lo);
    return 1;
}
