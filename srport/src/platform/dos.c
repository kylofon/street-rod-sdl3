/* DOS and C runtime replacement: the DOS state at start-up, DOS memory (INT 21h 48h/49h/4Ah on an MCB
 * chain in mem[]), the MS C 5.1 far heap (_fmalloc 1e16:1593, _ffree 1e16:157e, the near heap fallback
 * 1e16:1538, _brkctl 1e16:17bc) and the DOS file calls (_dos_open & co., intdos lseek 0f38:6016) —
 * platform.md §2.4, §2.5, §2.9, §4.9, §6.
 *
 * DOS memory. The chain starts at the program's MCB (PORT: DOS itself, the environment block and the
 * memory below the PSP are not modelled): PSP at LOAD_SEG - 10h, its MCB right below; _astart shrinks
 * the program block to DGROUP + 1000h = HEAP_BOTTOM (INT 21h 4Ah); the rest up to HEAP_TOP (A000h) is
 * one free block. Each MCB: byte 0 'M' / 'Z', word 1 owner (0 = free), word 3 size in paragraphs.
 * 48h is first fit (DOS's default strategy), joining free neighbours while it walks; 4Ah grows a block
 * into the free blocks behind it or fails with error 8.
 *
 * Far heap (MS C 5.1, transcribed register by register). Heap segments are DOS blocks allocated through
 * the brk table DS:62A2..62F1 ({u16 size in bytes, u16 segment} x 20; entry 0 = DGROUP with the stack
 * top as size, DS:62F2 = last used entry). A far heap segment starts with a descriptor at offset 0:
 *   +0 first block header (0Ah), +2 rover, +6 end (offset after the FFFEh end marker), +8 next segment,
 *   +0A the first block header.
 * Block headers are one word before the data: size | 1 when free, size when used; FFFEh ends the
 * segment. DS:64A0 first segment, DS:64A2 last, DS:64A4 rover segment, DS:64A8 highest segment,
 * DS:64AA pass counter, DS:64A6 _amblksiz (growth granularity, 800h from mem_pools_init). The near heap
 * (fallback) has the same descriptor at DS:6496 in DGROUP (start, rover, -, end, next = 0).
 *
 * Files. DOS handles are host FILE pointers (PORT: an OS resource, not game state) in a 20-entry job
 * file table; 0..4 are the standard devices (not backed by files), new handles are the lowest free
 * entry as DOS hands them out (5, 6, ...). Names: the part after the last ':', '\' or '/' is looked up
 * case-insensitively in the game directory (host_game_path). Errors set _doserrno DS:6318 and errno
 * DS:630D through the runtime's _dosmaperr (1e16:0578) and return the DOS error code. */
#include "platform/platform.h"
#include "platform/vga.h"
#include "host.h"

#include <stdio.h>
#include <string.h>

/* ======================================================================== C runtime variables */

#define PSP_SEG        ((u16)(LOAD_SEG - 0x10))
#define RT_ASIZDS      0x629C  /* u16 DGROUP size - 1 (brk limit without a DOS call) */
#define RT_STKTOP      0x629E  /* u16 initial SP */
#define RT_BRKTAB      0x62A2  /* {u16 size, u16 seg}[20]: [0] = DGROUP */
#define RT_BRKTAB_LAST 0x62F2  /* u16 near ptr to the last used entry (also the table end) */
#define RT_PSP         0x6313  /* u16 PSP segment */
#define RT_OSVERSION   0x6315  /* u8 major, u8 minor */
#define RT_DOSERRNO    0x6318  /* _doserrno (byte written by _dosmaperr, read as a word by lib_seek) */
#define RT_ERRNO       0x630D  /* errno */
#define RT_ERRTAB      0x6352  /* DOS error -> errno table */
#define RT_ENVP        0x6334  /* char **envp */
#define NH_DESC        0x6496  /* near heap descriptor: start, rover, -, end, next */
#define FH_FIRST       0x64A0  /* first far heap segment */
#define FH_LAST        0x64A2  /* last far heap segment */
#define FH_ROVER       0x64A4  /* segment the search starts in */
#define FH_AMBLKSIZ    0x64A6  /* _amblksiz */
#define FH_MAXSEG      0x64A8  /* highest far heap segment */
#define FH_PASS        0x64AA  /* u8 pass counter of the search */
#define BSS_START      0x6C22  /* _astart clears DS:6C22 .. DS:8F0F */
#define BSS_END        0x8F10
#define ENTRY_SP       0x0A00  /* SP at entry (SS = 4787h, from the EXEPACK header) */
#define STACK_BIAS     0x8F0E  /* _astart: ss = DGROUP, "add sp, 8F0Eh" -> stack top DS:990Eh */

/* 1e16:0578 _dosmaperr: DS:6318 = AL, errno DS:630D from the table DS:6352 (AH != 0: errno = AH). */
static void crt_dosmaperr(u16 ax)
{
    u8 al = (u8)ax, ah = (u8)(ax >> 8);
    DSB(RT_DOSERRNO) = al;
    if (ah != 0) {
        al = ah;
        DSW(RT_ERRNO) = (u16)(s16)(s8)al;
        return;
    }
    if (DSB(RT_OSVERSION) >= 3) {
        if (al >= 0x22) al = 0x13;
        else if (al >= 0x20) al = 5;
    }
    if (al > 0x13) al = 0x13;
    DSW(RT_ERRNO) = (u16)(s16)DSC(RT_ERRTAB + al);
}

/* ============================================================================== DOS memory */

#define MCB_FIRST ((u16)(PSP_SEG - 1))

static u8  mcb_type(u16 m)  { return rd8(m, 0); }
static u16 mcb_owner(u16 m) { return rd16(m, 1); }
static u16 mcb_size(u16 m)  { return rd16(m, 3); }
static bool mcb_valid(u16 m) { u8 t = mcb_type(m); return t == 'M' || t == 'Z'; }
static void mcb_set(u16 m, u8 type, u16 owner, u16 size)
{
    wr8(m, 0, type);
    wr16(m, 1, owner);
    wr16(m, 3, size);
}

/* Joins the free blocks that follow the block of MCB m into it (DOS does this while walking). */
static void mcb_join_free(u16 m)
{
    while (mcb_type(m) == 'M') {
        u16 n = (u16)(m + 1 + mcb_size(m));
        if (n >= HEAP_TOP || !mcb_valid(n) || mcb_owner(n) != 0) break;
        mcb_set(m, mcb_type(n), mcb_owner(m), (u16)(mcb_size(m) + 1 + mcb_size(n)));
    }
}

/* Cuts the block of MCB m to paras paragraphs; the rest becomes a free block. */
static void mcb_split(u16 m, u16 paras)
{
    u16 size = mcb_size(m);
    if (size <= paras) return;
    u16 n = (u16)(m + 1 + paras);
    mcb_set(n, mcb_type(m), 0, (u16)(size - paras - 1));
    mcb_set(m, 'M', mcb_owner(m), paras);
    mcb_join_free(n);
}

/* INT 21h AH=48h: segment of the new block, or 0 with *err = 8 (BX = largest block not modelled). */
static u16 dos_mem_alloc(u16 paras, u16 *err)
{
    u16 m = MCB_FIRST;
    for (;;) {
        if (!mcb_valid(m)) { *err = 7; return 0; }                  /* arena trashed */
        if (mcb_owner(m) == 0) {
            mcb_join_free(m);
            if (mcb_size(m) >= paras) {                              /* first fit */
                mcb_split(m, paras);
                wr16(m, 1, PSP_SEG);
                *err = 0;
                return (u16)(m + 1);
            }
        }
        if (mcb_type(m) == 'Z') break;
        m = (u16)(m + 1 + mcb_size(m));
        if (m >= HEAP_TOP) break;
    }
    *err = 8;
    return 0;
}

/* INT 21h AH=4Ah on the block at seg: 0 or the error (7 bad arena / 9 bad block, 8 not enough memory;
 * the block is left unchanged on failure). */
static u16 dos_mem_resize(u16 seg, u16 paras)
{
    u16 m = (u16)(seg - 1);
    if (seg < PSP_SEG || seg >= HEAP_TOP || !mcb_valid(m) || mcb_owner(m) == 0) return 9;
    u16 size = mcb_size(m);
    if (paras <= size) {
        mcb_split(m, paras);
        return 0;
    }
    /* grow: into the free blocks behind this one */
    u32 avail = size;
    u16 n = (u16)(m + 1 + size);
    u8 t = mcb_type(m);
    while (t == 'M' && n < HEAP_TOP && mcb_valid(n) && mcb_owner(n) == 0) {
        avail += 1u + mcb_size(n);
        t = mcb_type(n);
        n = (u16)(n + 1 + mcb_size(n));
    }
    if (avail < paras) return 8;
    mcb_join_free(m);
    mcb_split(m, paras);
    return 0;
}

/* (INT 21h AH=49h is never called: the MS C heap does not give memory back to DOS.) */

/* ================================================================== start-up (1e16:001e _astart) */

/* PORT: dos_init — platform.h. The state _astart (1e16:001e-00e4), _setenvp (1e16:044c) and DOS leave. */
void dos_init(void)
{
    /* The loader: program block = all conventional memory, PSP at LOAD_SEG - 10h. */
    memset(mp(PSP_SEG, 0), 0, 0x100);
    wr8(PSP_SEG, 0, 0xCD);
    wr8(PSP_SEG, 1, 0x20);                                       /* INT 20h */
    wr16(PSP_SEG, 2, HEAP_TOP);                                  /* top of memory */
    mcb_set(MCB_FIRST, 'Z', PSP_SEG, (u16)(HEAP_TOP - PSP_SEG));

    /* _astart: si = min(top - DGROUP, 1000h) paragraphs for DGROUP */
    u16 si = (u16)(rd16(PSP_SEG, 2) - DGROUP);
    if (si >= 0x1000) si = 0x1000;
    u16 sp = (u16)((ENTRY_SP + STACK_BIAS) & 0xFFFE);            /* 990Eh */
    DSW(RT_BRKTAB) = sp;                                         /* brk table entry 0: {990E, DGROUP} */
    DSW(RT_STKTOP) = sp;
    DSW(RT_ASIZDS) = (u16)((si << 4) - 1);                       /* FFFFh */
    si = (u16)(si + DGROUP);
    wr16(PSP_SEG, 2, si);
    dos_mem_resize(PSP_SEG, (u16)(si - PSP_SEG));                /* setblock: ends at HEAP_BOTTOM */
    DSW(RT_PSP) = PSP_SEG;
    memset(mp(DGROUP, BSS_START), 0, BSS_END - BSS_START);      /* BSS (DS:6C22..6C2F are FFh in the file) */

    /* 1e16:00e4: INT 21h 30h */
    DSW(RT_OSVERSION) = 0x0005;                                  /* PORT: DOS 5.00 */

    /* PORT: 1e16:044c _setenvp with an empty environment (PSP:2Ch = 0): envp = {NULL}, 2 bytes taken
     * from the DGROUP brk (1e16:0510). Only the near heap (never used by SR, see crt_fmalloc) starts
     * there. */
    u16 envp = DSW(RT_BRKTAB);
    DSW(RT_BRKTAB) = (u16)(envp + 2);
    DSW(envp) = 0;
    DSW(RT_ENVP) = envp;
}

/* ===================================================== MS C 5.1 heap (1e16:1538 - 1e16:187f) */

/* The registers of the heap routines. ds = the heap segment being worked on (DGROUP for the near
 * heap), es is always DGROUP (DSB/DSW), ss = DGROUP. */
typedef struct { u16 ax, bx, cx, dx, si, di, ds; bool zf; } HeapRegs;

static u16  hw(const HeapRegs *r, u16 off)         { return rd16(r->ds, off); }
static void hw_set(const HeapRegs *r, u16 off, u16 v) { wr16(r->ds, off, v); }

/* (x + 0Fh) >> 4 with the carry kept (add bx,0Fh; rcr bx,1; shr bx,3) */
static u16 paras17(u16 bytes) { return (u16)(((u32)bytes + 0x0F) >> 4); }

/* 1e16:182a: resize the brk table entry of segment cx by dx bytes. di = RT_BRKTAB. Returns false
 * (carry) on failure; else dx:ax = segment : old size. */
static bool brk_resize_182a(u16 di, u16 cx, u16 *dx, u16 *ax, u16 *si_out)
{
    u16 si = di;
    while (DSW(si + 2) != cx) {
        si = (u16)(si + 4);
        if (si == RT_BRKTAB_LAST) return false;                  /* stc */
    }
    *si_out = si;
    u32 t = (u32)*dx + DSW(si);
    if (t > 0xFFFF) return false;                                /* jb: carry */
    u16 bx = (u16)t;
    *dx = bx;                                                    /* new size */
    u16 es = cx;
    if (!(si == di && DSW(RT_ASIZDS) >= bx)) {
        bx = paras17(bx);
        if (si == di) {                                          /* DGROUP: resize the PSP block */
            bx = (u16)(bx + cx - DSW(RT_PSP));
            es = DSW(RT_PSP);
        }
        u16 err = dos_mem_resize(es, bx);                        /* INT 21h 4Ah */
        if (err) return false;
        if (si == di) DSW(RT_ASIZDS) = *dx;
    }
    u16 old = DSW(si);                                           /* xchg dx,ax; xchg [si],ax */
    DSW(si) = *dx;
    *ax = old;
    *dx = cx;
    return true;
}

/* 1e16:17bc _brkctl(mode, incr_lo, incr_hi, seg): mode 1 resizes the block of seg, mode 2 allocates a
 * new DOS block of incr bytes (a new brk table entry), other modes try to grow the last block first.
 * Returns dx:ax (FFFF:FFFF on failure): mode 1 seg:old size, else seg:0. */
static u16 crt_brkctl(u16 mode, u16 lo, u16 hi, u16 seg, u16 *dx_out)
{
    u16 di = RT_BRKTAB, si, ax, dx, bx;
    if (hi != 0) goto fail;
    dx = lo;
    ax = mode;
    if (--ax == 0) {                                             /* mode 1 */
        if (!brk_resize_182a(di, seg, &dx, &ax, &si)) goto fail;
        goto done;
    }
    si = DSW(RT_BRKTAB_LAST);
    if (--ax != 0 && si != di) {                                 /* mode >= 3: grow the last block */
        u16 s2;                                                  /* push si / pop si */
        seg = DSW(si + 2);
        if (brk_resize_182a(di, seg, &dx, &ax, &s2)) goto done;
    }
    si = (u16)(si + 4);
    if (si >= RT_BRKTAB_LAST) goto fail;
    if (dx == 0) goto fail;
    bx = paras17(dx);
    {
        u16 err, s = dos_mem_alloc(bx, &err);                    /* INT 21h 48h */
        if (!s) goto fail;
        DSW(si) = dx;
        DSW(si + 2) = s;
        DSW(RT_BRKTAB_LAST) = si;
        ax = 0;
        dx = s;
    }
done:
    *dx_out = dx;
    return ax;
fail:
    *dx_out = 0xFFFF;
    return 0xFFFF;
}

/* 1e16:179a: _brkctl(1, ax, 0, ds). ax = its result (old size), dx = the increment, bx kept;
 * ZF set on failure or when the increment is 0. */
static void heap_brk_179a(HeapRegs *r)
{
    u16 incr = r->ax, bx = r->bx, dxr;
    r->ax = crt_brkctl(1, incr, 0, r->ds, &dxr);
    r->dx = incr;
    r->bx = bx;
    r->zf = (dxr == 0xFFFF) ? true : (r->dx == 0);
}

/* 1e16:1778: grow the heap segment ds by ax bytes: the old end marker (at [si-2], si = old end)
 * becomes a free block up to the new end marker. Returns ax = the caller's di, di = old end,
 * si = new end; ZF set on failure (nothing changed, ax = _brkctl's result). */
static void heap_grow_1778(HeapRegs *r)
{
    u16 sdx = r->dx, scx = r->cx;
    heap_brk_179a(r);
    if (r->zf) { r->cx = scx; r->dx = sdx; return; }
    u16 sdi = r->di;
    r->di = r->si;
    r->si = (u16)(r->ax + r->dx);
    hw_set(r, (u16)(r->si - 2), 0xFFFE);
    hw_set(r, (u16)(r->bx + 6), r->si);
    r->dx = (u16)(r->si - r->di);
    r->dx--;
    r->zf = (r->dx == 0);
    hw_set(r, (u16)(r->di - 2), r->dx);
    r->ax = sdi;
    r->cx = scx;
    r->dx = sdx;
}

/* 1e16:173e: grow the heap segment for a request of cx bytes (less a free last block at [di-2]),
 * rounded up to the _amblksiz granularity, halving the granularity when that fails. */
static void heap_expand_173e(HeapRegs *r)
{
    u16 scx = r->cx;
    r->ax = hw(r, (u16)(r->di - 2));
    if (r->ax & 1) {
        r->cx = (u16)(r->cx - r->ax);
        r->cx--;
    }
    r->cx = (u16)(r->cx + 2);
    r->dx = 0x7FFF;
    while (r->dx > DSW(FH_AMBLKSIZ)) {                           /* cmp dx,[64A6]; jbe */
        r->dx >>= 1;
        if (r->dx == 0) break;
    }
    for (;;) {                                                   /* 1759 */
        u32 t = (u32)r->cx + r->si;
        if (t > 0xFFFF) break;                                   /* -> 1774 */
        r->ax = (u16)t;
        t = (u32)r->ax + r->dx;
        r->ax = (u16)t;
        if (t <= 0xFFFF) {
            r->dx = (u16)~r->dx;
            r->ax &= r->dx;
            r->ax = (u16)(r->ax - r->si);
            heap_grow_1778(r);
            if (!r->zf) { r->cx = scx; return; }                 /* 1776 */
            r->dx = (u16)~r->dx;
        }
        r->dx >>= 1;                                             /* 1770 */
        if (r->dx == 0) break;
    }
    r->ax = 0;                                                   /* 1774 */
    r->zf = true;
    r->cx = scx;
}

/* 1e16:165b: the allocator. ds:bx = heap descriptor, cx = size. Returns dx:ax = the block (dx = 0 on
 * failure). Walks from the rover, joining free neighbours; at the end of the segment chain it wraps
 * once to the first far segment (pass counter DS:64AA), then grows the last segment (1706). */
static void heap_alloc_165b(HeapRegs *r)
{
    r->cx++;
    if (r->cx == 0) goto L1729;
    r->cx &= 0xFFFE;
    if (r->cx >= 0xFFEE) goto L1729;
    r->si = hw(r, (u16)(r->bx + 2));
    r->ax = hw(r, r->si); r->si += 2;                           /* 166a lodsw */
    r->di = r->si;
    if (!(r->ax & 1)) goto L16b3;
L1671:
    r->ax--;
    if (r->ax >= r->cx) goto L168b;
    r->dx = r->ax;
    r->si = (u16)(r->si + r->ax);
    r->ax = hw(r, r->si); r->si += 2;
    if (!(r->ax & 1)) goto L16b3;
    r->ax = (u16)(r->ax + r->dx + 2);                            /* join the next free block */
    r->si = r->di;
    hw_set(r, (u16)(r->si - 2), r->ax);
    goto L1671;
L168b:
    r->di = r->si;
    if (r->ax != r->cx) {                                        /* split */
        r->di = (u16)(r->di + r->cx);
        hw_set(r, (u16)(r->si - 2), r->cx);
        r->ax = (u16)(r->ax - r->cx);
        r->ax--;
        hw_set(r, r->di, r->ax);
    } else {                                                     /* 169b: exact fit */
        r->di = (u16)(r->di + r->cx);
        wr8(r->ds, (u16)(r->si - 2), (u8)(rd8(r->ds, (u16)(r->si - 2)) - 1));
    }
    r->ax = r->si;                                               /* 16a0 */
    r->dx = r->ds;
    r->cx = DGROUP;
    if (r->dx != r->cx) DSW(FH_ROVER) = r->ds;
    hw_set(r, (u16)(r->bx + 2), r->di);
    return;
L16b3:
    DSB(FH_PASS) = 2;
L16b9:
    if (r->ax == 0xFFFE) goto L16e3;
    r->di = r->si;
    r->si = (u16)(r->si + r->ax);
L16c2:
    r->ax = hw(r, r->si); r->si += 2;
    if (!(r->ax & 1)) goto L16b9;
    r->di = r->si;
L16c9:
    r->ax--;
    if (r->ax >= r->cx) goto L168b;
    r->dx = r->ax;
    r->si = (u16)(r->si + r->ax);
    r->ax = hw(r, r->si); r->si += 2;
    if (!(r->ax & 1)) goto L16b9;
    r->ax = (u16)(r->ax + r->dx + 2);
    r->si = r->di;
    hw_set(r, (u16)(r->si - 2), r->ax);
    goto L16c9;
L16e3:                                                           /* end marker */
    r->ax = hw(r, (u16)(r->bx + 8));
    if (r->ax != 0) { r->ds = r->ax; goto L1702; }               /* next segment */
    if (--DSB(FH_PASS) == 0) goto L1706;
    r->ax = r->ds;
    r->di = DGROUP;                                              /* mov di, ss */
    if (r->ax != r->di) r->ds = DSW(FH_FIRST);                   /* wrap to the first far segment */
L1702:
    r->si = hw(r, r->bx);
    goto L16c2;
L1706:                                                           /* grow the last segment */
    r->si = hw(r, (u16)(r->bx + 6));
    r->ax = 0;
    heap_grow_1778(r);                                           /* ax = the brk table size */
    if (r->ax != r->si) {
        r->ax = (u16)((r->ax & 1) + 2);                          /* and al,1; inc ax x2; cbw */
        heap_grow_1778(r);
        if (r->zf) goto L1729;
        wr8(r->ds, (u16)(r->di - 2), (u8)(rd8(r->ds, (u16)(r->di - 2)) - 1));   /* filler: used */
    }
    heap_expand_173e(r);                                         /* 171f */
    if (r->zf) goto L1729;
    { u16 t = r->si; r->si = r->ax; r->ax = t; }                 /* xchg si, ax */
    r->si -= 2;
    goto L16c2;
L1729:
    r->ax = r->ds;
    r->cx = DGROUP;
    if (r->ax != r->cx) DSW(FH_ROVER) = r->ax;
    r->ax = hw(r, r->bx);
    hw_set(r, (u16)(r->bx + 2), r->ax);                          /* rover = start */
    r->ax = 0;
    r->dx = 0;
}

/* 1e16:15d2: a new far heap segment for `size` bytes (at least F0h, even) + 0Eh bytes of descriptor
 * and end marker, linked behind DS:64A2. Returns ax = segment, ZF on failure. */
static void heap_new_seg_15d2(HeapRegs *r, u16 size)
{
    u16 bx = 0xF0;
    if (size > bx) bx = (u16)((size + 1) & 0xFFFE);
    u16 local = bx, dx;
    r->ax = crt_brkctl(2, (u16)(bx + 0x0E), 0, DGROUP, &dx);
    if (dx == 0xFFFF) { r->dx = dx; r->zf = true; return; }
    r->ax = dx;
    u16 t = DSW(FH_LAST); DSW(FH_LAST) = dx; dx = t;             /* xchg [64A2], dx */
    DSW(FH_ROVER) = r->ax;
    if (r->ax > DSW(FH_MAXSEG)) DSW(FH_MAXSEG) = r->ax;
    if (dx != 0) wr16(dx, 8, r->ax);                             /* link from the previous last one */
    bx = local;
    u16 s = r->ax;
    wr16(s, 8, 0);
    wr16(s, (u16)(bx + 0x0C), 0xFFFE);
    wr16(s, 0, 0x0A);
    wr16(s, 2, 0x0A);
    wr16(s, 0x0A, (u16)(bx + 1));
    wr16(s, 6, (u16)(bx + 1 + 0x0D));
    r->ax = s;
    r->dx = dx;
    r->zf = false;
}

/* 1e16:1640: search the far heap from the rover segment. ZF = failure. */
static void heap_search_1640(HeapRegs *r, u16 size)
{
    r->cx = size;
    r->bx = 0;
    r->ds = DSW(FH_ROVER);
    heap_alloc_165b(r);
    r->zf = (r->dx == 0);
}

/* 1e16:1538: near heap malloc (DGROUP), initialised on first use: 2 bytes from the DGROUP brk hold a
 * free block of size 0 and the end marker. Returns dx:ax. */
static FarPtr crt_nmalloc(u16 size)
{
    HeapRegs r = { 0 };
    r.bx = NH_DESC;
    r.ds = DGROUP;
    if (DSW(r.bx) == 0) {
        r.ax = 5;
        heap_brk_179a(&r);
        if (r.zf) return far_make(0, 0);
        r.ax++;
        r.ax &= 0xFFFE;
        DSW(0x6496) = r.ax;
        DSW(0x6498) = r.ax;
        r.si = r.ax;
        DSW(r.si) = 1;
        r.si += 4;
        DSW(r.si - 2) = 0xFFFE;
        DSW(0x649C) = r.si;
    }
    r.cx = size;
    heap_alloc_165b(&r);
    return far_make(r.dx, r.ax);
}

/* 1e16:1593 _fmalloc — platform.md §2.9. Sizes >= FFF1h and a far heap failure fall back to the near
 * heap (1e16:1538), whose DGROUP blocks far_alloc 0000:3709 (the only caller) always rejects. */
FarPtr crt_fmalloc(u16 size)
{
    HeapRegs r = { 0 };
    if (size >= 0xFFF1) goto near;
    if (DSW(FH_FIRST) == 0) {
        heap_new_seg_15d2(&r, size);
        if (r.zf) goto near;
        DSW(FH_FIRST) = r.ax;
    }
    heap_search_1640(&r, size);
    if (!r.zf) return far_make(r.dx, r.ax);
    heap_new_seg_15d2(&r, size);
    if (r.zf) goto near;
    heap_search_1640(&r, size);
    if (!r.zf) return far_make(r.dx, r.ax);
near:
    return crt_nmalloc(size);
}

/* 1e16:157e _ffree — platform.md §2.9: sets the free bit of the block header (no joining here; the
 * allocator joins free neighbours when it walks over them). */
void crt_ffree(FarPtr p)
{
    if (far_is_null(p)) return;
    u16 off = (u16)(p.off - 2);
    vwr(p.seg, off, (u8)(vrd(p.seg, off) | 1));                  /* or byte es:[bx-2], 1 */
}

/* ================================================================================= DOS files */

#define DOS_MAX_HANDLES 20
#define DOS_STD_HANDLES 5

typedef enum { H_FREE, H_STD, H_FILE } HandleKind;
static struct { HandleKind kind; FILE *f; } dos_jft[DOS_MAX_HANDLES];
static bool dos_jft_ready;

static void dos_jft_init(void)
{
    if (dos_jft_ready) return;
    for (int i = 0; i < DOS_STD_HANDLES; i++) dos_jft[i].kind = H_STD;    /* stdin .. stdprn */
    dos_jft_ready = true;
}

static FILE *dos_file(u16 h)
{
    dos_jft_init();
    if (h >= DOS_MAX_HANDLES || dos_jft[h].kind != H_FILE) return NULL;
    return dos_jft[h].f;
}

/* PORT: the file name part of a DOS path ("C:lib1", ":HOTROD.SAV", "A:\X\NAME" -> "lib1" ...). */
static const char *dos_base_name(const char *s)
{
    const char *b = s;
    for (const char *p = s; *p; p++)
        if (*p == ':' || *p == '\\' || *p == '/') b = p + 1;
    return b;
}

/* INT 21h 3Dh / 3Ch: 0 and *handle, or the DOS error (2 not found, 4 too many files, 5 denied). */
static u16 dos_open_file(const char *name, const char *fmode, bool create, u16 *handle)
{
    dos_jft_init();
    u16 h;
    for (h = 0; h < DOS_MAX_HANDLES && dos_jft[h].kind != H_FREE; h++) {}
    if (h >= DOS_MAX_HANDLES) return 4;
    const char *base = dos_base_name(name);
    if (!*base) return 2;
    char *path = host_game_path(base, create);
    if (!path) return 2;
    FILE *f = fopen(path, fmode);
    host_free(path);
    if (!f) return create ? 5 : 2;
    dos_jft[h].kind = H_FILE;
    dos_jft[h].f = f;
    *handle = h;
    return 0;
}

/* The runtime's error exit of the _dos_* calls (1e16:055a): _dosmaperr, return the DOS error. */
static u16 dos_ret(u16 err)
{
    if (err) crt_dosmaperr(err);
    return err;
}

/* 1e16:2330 _dos_open — platform.h. Mode 0 read, 1 write, 2 read/write (sharing bits ignored). */
u16 dos_open(const char *name, u16 mode, u16 *handle)
{
    u8 access = (u8)(mode & 7);
    if (access > 2) return dos_ret(0x0C);                        /* invalid access code */
    return dos_ret(dos_open_file(name, access == 0 ? "rb" : "r+b", false, handle));
}

/* 1e16:2305 _dos_creat — platform.h (INT 21h 3Ch: create or truncate, opened read/write). */
u16 dos_creat(const char *name, u16 attr, u16 *handle)
{
    (void)attr;
    return dos_ret(dos_open_file(name, "w+b", true, handle));
}

/* DOS 3Fh / 40h on mem[] from seg:off on (video memory through the register model). */
static u16 dos_xfer(FILE *f, FarPtr buf, u16 n, bool write)
{
    if (is_vram(buf.seg)) {
        u16 k = 0;
        for (; k < n; k++) {
            u16 off = (u16)(buf.off + k);
            if (write) {
                if (fputc(vrd(buf.seg, off), f) == EOF) break;
            } else {
                int c = fgetc(f);
                if (c == EOF) break;
                vwr(buf.seg, off, (u8)c);
            }
        }
        if (write) fflush(f);
        return k;
    }
    u32 at = far_lin(buf);
    if (at >= MEM_SIZE) return 0;
    if (n > MEM_SIZE - at) n = (u16)(MEM_SIZE - at);
    size_t k = write ? fwrite(mem + at, 1, n, f) : fread(mem + at, 1, n, f);
    if (write) fflush(f);
    return (u16)k;
}

/* 1e16:2348 _dos_read — platform.h */
u16 dos_read(u16 handle, FarPtr buf, u16 n, u16 *got)
{
    FILE *f = dos_file(handle);
    if (!f) return dos_ret(6);
    *got = dos_xfer(f, buf, n, false);
    return 0;
}

/* 1e16:234f _dos_write — platform.h */
u16 dos_write(u16 handle, FarPtr buf, u16 n, u16 *put)
{
    FILE *f = dos_file(handle);
    if (!f) return dos_ret(6);
    u16 k = dos_xfer(f, buf, n, true);
    if (k < n && ferror(f)) return dos_ret(5);
    *put = k;
    return 0;
}

/* 1e16:22f0 _dos_close — platform.h. Closing a standard handle (0..4) frees its entry, as DOS does. */
u16 dos_close(u16 handle)
{
    dos_jft_init();
    if (handle >= DOS_MAX_HANDLES || dos_jft[handle].kind == H_FREE) return dos_ret(6);
    if (dos_jft[handle].kind == H_FILE) fclose(dos_jft[handle].f);
    dos_jft[handle].kind = H_FREE;
    dos_jft[handle].f = NULL;
    return 0;
}

/* 1e16:22de remove — platform.h: 0, or -1 (1e16:0552 / 0567) with _dosmaperr. */
u16 dos_remove(const char *name)
{
    const char *base = dos_base_name(name);
    char *path = *base ? host_game_path(base, false) : NULL;
    int rc = path ? remove(path) : -1;
    if (path) host_free(path);
    if (rc != 0) {
        crt_dosmaperr(path ? 5 : 2);
        return 0xFFFF;
    }
    return 0;
}

/* 1e16:2382 _dos_getdrive — platform.h. PORT: always drive 3 (C:), so every floppy path is dead. */
void dos_getdrive(u16 *drive)
{
    *drive = 3;
}

/* 0f38:6016 lib_seek — platform.md §2.5: intdos(AX=4200h, BX=handle, CX:DX=pos); 0 on success, else
 * _doserrno DS:6318 (set by 1e16:0572 from AL) which is also copied to DS:630D. */
s16 lib_seek(u16 handle, s32 pos)
{
    FILE *f = dos_file(handle);
    if (f && fseek(f, (long)(u32)pos, SEEK_SET) == 0) return 0;
    crt_dosmaperr(f ? 0x19 : 6);                  /* 6 invalid handle; PORT: 19h if the host seek fails */
    DSW(RT_ERRNO) = DSW(RT_DOSERRNO);
    return DSS(RT_DOSERRNO);
}
