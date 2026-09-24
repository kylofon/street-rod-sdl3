/* Memory: far allocation with the "Not enough memory" exit, the game object pools, bitmap descriptors,
 * the off-screen EGA picture pools and the 65000-byte work arena — platform.md §2.4, §4.9, §5.5.
 *
 * The far heap itself (MS C _fmalloc/_ffree on DOS memory in mem[]) is in dos.c. */
#include "platform/platform.h"
#include "platform/video.h"
#include "modules.h"

/* ============================================================================= far allocation */

/* 0000:36a8 get_ds — platform.md §2.4: segread() -> DS. */
u16 get_ds(void)
{
    return DGROUP;
}

/* 0000:3614 pools_reset — platform.md §2.4: the free lists of the game objects (see game_flow):
 * 16 x 28h bytes at DS:83B0 (head DS:7EB4; +26h next, +02h = FFFFh) and 8Ch x 8 bytes at DS:866A
 * (head DS:7EBA; +06h next, +04h = FFFFh), counts DS:7EBC / DS:7EB6 = 0; full: also DS:7EB0, 7EB2,
 * 7EB8, the money DS:7EAA (s32) = 0 and DS:7EAE = 10. */
void pools_reset(s16 full)
{
    u16 cx = 0x83B0, bx;
    DSW(DS_car_freelist) = cx;
    for (u16 dx = 0x10; dx != 0; dx--) {
        bx = cx;
        DSW(bx + 0x26) = (u16)(cx + 0x28);
        DSW(bx + 2) = 0xFFFF;
        cx = (u16)(cx + 0x28);
    }
    DSW((u16)(cx - 0x28) + 0x26) = 0;                            /* last: next = 0 */

    cx = 0x866A;
    DSW(DS_part_freelist) = cx;
    for (u16 dx = 0x8C; dx != 0; dx--) {
        bx = cx;
        DSW(bx + 6) = (u16)(cx + 8);
        DSW(bx + 4) = 0xFFFF;
        cx = (u16)(cx + 8);
    }
    DSW((u16)(cx - 8) + 6) = 0;

    DSW(DS_part_count) = 0;
    DSW(DS_car_count) = 0;
    if (full != 0) {
        DSW(DS_car) = 0;
        DSW(DS_car2) = 0;
        DSW(DS_spare_parts) = 0;
        DSW(DS_money + 2) = 0;
        DSW(DS_money) = 0;
        DSW(DS_unk_7eae) = 0x0A;
    }
}

/* 0000:36be mem_pools_init — platform.md §2.4: _amblksiz DS:64A6 = 800h, DS:6C60 = DS, pools_reset(1). */
void mem_pools_init(void)
{
    DSW(0x64A6) = 0x800;
    DSW(DS_dgroup_seg) = get_ds();
    pools_reset(1);
}

/* 0000:36d6 far_free — platform.md §2.4 */
void far_free(FarPtr p)
{
    crt_ffree(p);
}

/* 0000:36e9 out_of_memory — platform.md §2.4: message box DS:4BEE, platform_exit. */
_Noreturn void out_of_memory(void)
{
    modules.fatal_message(ds_str(DS_msg_no_memory));
    platform_exit();
}

/* 0000:3709 far_alloc — platform.md §4.9: _fmalloc; a block in DGROUP (the near heap fallback) is
 * given back and counts as a failure, as is a block at or above 8000:0000 on Tandy. */
FarPtr far_alloc(u16 size, s16 fatal)
{
    FarPtr p = crt_fmalloc(size);
    if (!far_is_null(p)) {
        if (p.seg != DSW(DS_dgroup_seg)) {
            if (DSS(DS_driver_id) != -6) return p;
            if (p.seg < 0x8000) return p;                        /* Tandy: not in the video RAM */
        }
        crt_ffree(p);
    }
    if (fatal != 0) out_of_memory();
    return far_make(0, 0);
}

/* ======================================================================== bitmap descriptors */

/* 0000:376b bitmap_alloc — platform.md §4.9, §5.5: descriptor (+ planes + mask) for w x h; flags != 0
 * adds a mask, flags & 2 = mask only; d NULL: far_alloc(30h + planes + mask) with the planes behind
 * the descriptor. */
FarPtr bitmap_alloc(s16 w, s16 h, s16 flags, s16 drv, FarPtr d)
{
    s16 bpr = (s16)(((s16)(w - 1) >> (DSW(DS_g_px_shift) & 0x1F)) + 1);  /* sar ax, cl */
    u16 plane = (u16)((s32)bpr * h);                             /* imul: low word */
    u16 total = (u16)((u32)plane * DSW(DS_g_nplanes));           /* mul: low word */
    u16 hdr = 0x30, maskb = 0;
    if (flags != 0) maskb = plane;
    if (!(flags & 2)) hdr = (u16)(hdr + total);
    if (far_is_null(d)) {
        d = far_alloc((u16)(hdr + maskb), 0);
        if (far_is_null(d)) return far_make(0, 0);
        far_wr(d.seg, (u16)(d.off + DESC_PLANES), far_make(d.seg, (u16)(d.off + 0x30)));
    }
    if (flags & 2) {
        FarPtr data = desc_planes(d);
        if (!far_is_null(data)) {
            far_wr(d.seg, (u16)(d.off + DESC_MASK), data);
            far_wr(d.seg, (u16)(d.off + DESC_PLANES), far_make(0, 0));
        } else {
            far_wr(d.seg, (u16)(d.off + DESC_MASK), far_make(d.seg, (u16)(d.off + 0x30)));
        }
    } else if (flags != 0) {
        FarPtr data = desc_planes(d);
        far_wr(d.seg, (u16)(d.off + DESC_MASK), far_make(data.seg, (u16)(data.off + total)));
    } else {
        far_wr(d.seg, (u16)(d.off + DESC_MASK), far_make(0, 0));
    }
    wr16(d.seg, (u16)(d.off + DESC_SIZE), total);
    wr16(d.seg, (u16)(d.off + DESC_W), (u16)w);
    wr16(d.seg, (u16)(d.off + DESC_H), (u16)h);
    wr16(d.seg, (u16)(d.off + DESC_STRIDE), (u16)bpr);
    wr8(d.seg, (u16)(d.off + DESC_TYPE), (u8)drv);
    return d;
}

/* 0000:3904 bitmap_init — platform.md §4.9: fills an existing descriptor (size = bpr * h * planes,
 * both imul). */
void bitmap_init(FarPtr d, FarPtr data, FarPtr mask, s16 w, s16 h, s16 drv)
{
    s16 bpr = (s16)(((s16)(w - 1) >> (DSW(DS_g_px_shift) & 0x1F)) + 1);
    u16 size = (u16)((u16)((s32)bpr * h) * DSW(DS_g_nplanes));  /* low words of the products */
    far_wr(d.seg, (u16)(d.off + DESC_PLANES), data);
    far_wr(d.seg, (u16)(d.off + DESC_MASK), mask);
    wr16(d.seg, (u16)(d.off + DESC_SIZE), size);
    wr16(d.seg, (u16)(d.off + DESC_W), (u16)w);
    wr16(d.seg, (u16)(d.off + DESC_H), (u16)h);
    wr16(d.seg, (u16)(d.off + DESC_STRIDE), (u16)bpr);
    wr8(d.seg, (u16)(d.off + DESC_TYPE), (u8)drv);
}

/* 0000:3880 bitmap_make — platform.md §4.9: a bare descriptor over existing data (no callers). */
FarPtr bitmap_make(FarPtr data, FarPtr mask, s16 w, s16 h, s16 drv)
{
    FarPtr d = far_alloc(0x30, 0);
    if (far_is_null(d)) return far_make(0, 0);
    bitmap_init(d, data, mask, w, h, drv);
    return d;
}

/* 0000:3967 vram_pool_alloc — platform.md §4.9: first fit over the four plane pools DS:4C18[4] (bytes
 * used from offset 7D00h of DS:4C14 = A000:0000); used + size < 7D00h, 16-bit wrap as the original. */
FarPtr vram_pool_alloc(u16 size, u8 *plane)
{
    u16 si = DS_g_vram_pool_used;
    for (u16 cx = 0; (s16)cx < 4; cx++, si = (u16)(si + 2)) {
        if ((u16)(DSW(si) + size) < 0x7D00) {
            FarPtr base = ds_far(DS_g_vram_pool_base);
            u16 off = (u16)(DSW(si) + base.off);
            off = (u16)(off + 0x7D00);                           /* add ah, 7Dh */
            DSW(si) = (u16)(DSW(si) + size);
            *plane = (u8)cx;
            return far_make(base.seg, off);
        }
    }
    *plane = 0xFF;
    return far_make(0, 0);
}

/* ============================================================================ the work arena */

/* The arena state: DS:6CD0 arena far ptr, DS:8F00 permanent bump, DS:8B02 u16 W[] (W[0] = bump top,
 * low stack W[0..DS:7B14], top stack W[DS:7D80..3Ch]), DS:78DE top of the permanent top area,
 * DS:8B7A = W[3Ch]. */
#define AR_W(i)   DSW((u16)(0x8B02 + 2 * (u16)(i)))
#define AR_BUMP   0x8F00
#define AR_LOW    0x7B14
#define AR_TOP    0x7D80
#define AR_TOPPERM 0x78DE

/* 0f38:9ebc arena_clear — platform.md §4.9 */
void arena_clear(void)
{
    DSW(AR_LOW) = 0;
    DSW(AR_BUMP) = 0;
    AR_W(0) = 0;
    DSW(AR_TOP) = 0x3C;
    DSW(AR_TOPPERM) = 0xFDE8;
    AR_W(0x3C) = 0xFDE8;
    DSW(0x0296) = 0;
    DSW(0x0294) = 0;
}

/* 0f38:9ee9 arena_init — platform.md §4.9: DS:6CD0 = far_alloc(65000, fatal). */
void arena_init(void)
{
    if (DSS(DS_driver_id) == -6) {
        /* PORT: Tandy (arena at 8000:0000 in the video RAM) not ported. */
        ds_far_wr(DS_arena, far_make(0x8000, 0));
    } else {
        ds_far_wr(DS_arena, far_alloc(0xFDE8, 1));
    }
    arena_clear();
}

/* 0f38:9f1a arena_low_shrink — platform.md §2.4: W[DS:7B14] -= n. */
void arena_low_shrink(s16 n)
{
    AR_W(DSW(AR_LOW)) = (u16)(AR_W(DSW(AR_LOW)) - (u16)n);
}

/* 0f38:9f2c arena_alloc — platform.md §4.9: 0 permanent bump, 1 low stack, 2 top stack, 3 top
 * permanent; other modes return the arena start. */
FarPtr arena_alloc(s16 n, s16 mode)
{
    FarPtr p = ds_far(DS_arena);
    u16 si;
    switch (mode) {
    case 0:
        p.off = (u16)(p.off + DSW(AR_BUMP));
        DSW(AR_BUMP) = (u16)(DSW(AR_BUMP) + (u16)n);
        AR_W(0) = DSW(AR_BUMP);
        break;
    case 1:
        si = DSW(AR_LOW);
        p.off = (u16)(p.off + AR_W(si));
        AR_W((u16)(si + 1)) = (u16)((u16)n + AR_W(si));
        DSW(AR_LOW) = (u16)(DSW(AR_LOW) + 1);
        break;
    case 2:
        si = DSW(AR_TOP);
        AR_W(si) = (u16)(AR_W(si) - (u16)n);
        p.off = (u16)(p.off + AR_W(si));
        AR_W((u16)(si - 1)) = (u16)(AR_W(si) - 1);
        DSW(AR_TOP) = (u16)(DSW(AR_TOP) - 1);
        break;
    case 3: {
        DSW(AR_TOPPERM) = (u16)(DSW(AR_TOPPERM) - (u16)n);
        u16 ax = DSW(AR_TOPPERM);
        DSW(AR_TOPPERM) = (u16)(DSW(AR_TOPPERM) - 1);
        p.off = (u16)(p.off + ax);
        AR_W(0x3C) = DSW(AR_TOPPERM);                            /* DS:8B7A */
        break;
    }
    default:
        break;
    }
    return p;
}

/* 0f38:a054 arena_reset — platform.md §2.4: both stacks and the bump area emptied, the top permanent
 * area kept. */
void arena_reset(void)
{
    DSW(AR_LOW) = 0;
    DSW(AR_BUMP) = 0;
    AR_W(0) = 0;
    DSW(AR_TOP) = 0x3C;
    AR_W(0x3C) = DSW(AR_TOPPERM);
    DSW(0x0296) = 0;
    DSW(0x0294) = 0;
}

/* 0f38:a07e arena_reset_stacks — platform.md §2.4: both stacks emptied, the bump area kept. */
void arena_reset_stacks(void)
{
    DSW(AR_LOW) = 0;
    AR_W(0) = DSW(AR_BUMP);
    DSW(AR_TOP) = 0x3C;
    AR_W(0x3C) = DSW(AR_TOPPERM);
}

/* 0f38:a09e arena_pop_low — platform.md §2.4: DS:7B14 -= n (0 = empty). */
void arena_pop_low(s16 n)
{
    if (n == 0) DSW(AR_LOW) = 0;
    else DSW(AR_LOW) = (u16)(DSW(AR_LOW) - (u16)n);
}

/* 0f38:a0b8 arena_pop — platform.md §4.9 */
void arena_pop(s16 n)
{
    if (n != 0) {
        DSW(AR_TOP) = (u16)(DSW(AR_TOP) + (u16)n);
        if (DSW(AR_TOP) != 0x3C) {
            u16 si = DSW(AR_TOP);
            AR_W(si) = (u16)(AR_W((u16)(si + 1)) - 1);
            return;
        }
    } else {
        DSW(AR_TOP) = 0x3C;
    }
    AR_W(0x3C) = DSW(AR_TOPPERM);
}
