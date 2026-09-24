#pragma once
/* Real-mode memory model.
 *
 * The unpacked SR.EXE load image is placed at segment LOAD_SEG (0x1000, same as the Ghidra project)
 * with its MZ relocations applied. The program is segmented (MSC 5.1 medium model), so:
 *   file address SSSS:OOOO  = segment SEG(0xSSSS) = LOAD_SEG + 0xSSSS, offset OOOO
 *   DS:xxxx (DGROUP)        = segment DGROUP = SEG(0x3E96), i.e. image offset 0x3E960 + xxxx
 * All data tables, strings, the picture directory (segment 389b) and the game state are read and
 * written there. Heap blocks are allocated above DGROUP by the port of the game's allocation calls.
 *
 * Video memory is NOT in mem[]: the 16-colour modes are planar (EGA/VGA mode 0Dh, four 64 KB planes
 * written through the sequencer map mask and the graphics controller). The EGA model
 * (platform/ega.c) owns the planes; the game also parks packed pictures in off-screen video memory
 * (0f38:62ba), which goes through the same model.
 */
#include "types.h"

#define MEM_SIZE    0x110000u
#define LOAD_SEG    0x1000
#define SEG(file_seg) ((u16)(LOAD_SEG + (file_seg)))    /* segment of a file address SSSS:OOOO */
#define DGROUP_FILE_SEG 0x3E96
#define DGROUP      SEG(DGROUP_FILE_SEG)
#define VRAM_SEG    0xA000
#define HEAP_BOTTOM (DGROUP + 0x1000)   /* first segment above the 64 KB DGROUP (data, BSS, stack) */
#define HEAP_TOP    VRAM_SEG            /* DOS memory ends where video memory begins (640 KB) */

extern u8 mem[MEM_SIZE];

static inline u32 lin(u16 seg, u16 off) { return ((u32)seg << 4) + off; }
static inline u8 *mp(u16 seg, u16 off) { return mem + lin(seg, off); }

static inline u8  rd8 (u16 seg, u16 off) { return mem[lin(seg, off)]; }
static inline u16 rd16(u16 seg, u16 off) { u8 *p = mp(seg, off); return (u16)(p[0] | p[1] << 8); }
static inline u32 rd32(u16 seg, u16 off) { u8 *p = mp(seg, off); return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static inline void wr8 (u16 seg, u16 off, u8 v)  { mem[lin(seg, off)] = v; }
static inline void wr16(u16 seg, u16 off, u16 v) { u8 *p = mp(seg, off); p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static inline void wr32(u16 seg, u16 off, u32 v) { u8 *p = mp(seg, off); p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

/* Lvalue accessors for globals (little-endian host). DS offsets come from symbols.h, e.g.
 *   DSW(DS_game_state) = 4;   if (DSB(DS_race_state) == 3) ...
 * CS-style variables live in their own code segment: SEGW(0x0e12, 0x0000) with a file segment, or
 * SEGW(CSSEG_x, CS_x) with the symbols.h constants. */
#define DSB(o) (*(u8  *)mp(DGROUP, (u16)(o)))
#define DSC(o) (*(s8  *)mp(DGROUP, (u16)(o)))
#define DSW(o) (*(u16 *)mp(DGROUP, (u16)(o)))
#define DSS(o) (*(s16 *)mp(DGROUP, (u16)(o)))
#define DSL(o) (*(u32 *)mp(DGROUP, (u16)(o)))
#define DSSL(o) (*(s32 *)mp(DGROUP, (u16)(o)))
#define SEGB(s, o) (*(u8  *)mp(SEG(s), (u16)(o)))
#define SEGC(s, o) (*(s8  *)mp(SEG(s), (u16)(o)))
#define SEGW(s, o) (*(u16 *)mp(SEG(s), (u16)(o)))
#define SEGS(s, o) (*(s16 *)mp(SEG(s), (u16)(o)))

/* 16:16 far pointer as stored in memory (offset first). */
typedef struct { u16 off, seg; } FarPtr;

static inline FarPtr far_rd(u16 seg, u16 off) { FarPtr p = { rd16(seg, off), rd16(seg, (u16)(off + 2)) }; return p; }
static inline void   far_wr(u16 seg, u16 off, FarPtr p) { wr16(seg, off, p.off); wr16(seg, (u16)(off + 2), p.seg); }
static inline FarPtr ds_far(u16 off) { return far_rd(DGROUP, off); }                 /* far ptr stored at DS:off */
static inline void   ds_far_wr(u16 off, FarPtr p) { far_wr(DGROUP, off, p); }
static inline u8    *far_mp(FarPtr p) { return mp(p.seg, p.off); }
static inline FarPtr far_make(u16 seg, u16 off) { FarPtr p = { off, seg }; return p; }
static inline FarPtr far_add(FarPtr p, u16 n) { p.off = (u16)(p.off + n); return p; }  /* far (not huge) arithmetic */
static inline u32    far_lin(FarPtr p) { return lin(p.seg, p.off); }
static inline FarPtr far_norm(u32 linear) { FarPtr p = { (u16)(linear & 0x0F), (u16)(linear >> 4) }; return p; }
static inline bool   far_is_null(FarPtr p) { return p.off == 0 && p.seg == 0; }
static inline FarPtr ds_ptr(u16 off) { return far_make(DGROUP, off); }                /* DGROUP address as far ptr */

/* Division as the original CPU performs it. The MSC runtime hooks INT 0: a divide error (zero divisor
 * or quotient overflow) prints "run-time error R6003 - integer divide by 0" and exits. The port does
 * the same through div_error() (mem.c), except where a spec says to clamp instead (such call sites are
 * marked PORT). */
_Noreturn void div_error(void);

static inline u16 div32_16(u32 dxax, u16 divisor, u16 *rem)
{
    if (divisor == 0 || dxax / divisor > 0xFFFF) div_error();
    if (rem) *rem = (u16)(dxax % divisor);
    return (u16)(dxax / divisor);
}
static inline s16 idiv32_16(s32 dxax, s16 divisor, s16 *rem)
{
    if (divisor == 0) div_error();
    s32 q = dxax / divisor;                         /* truncates toward zero, like IDIV */
    if (q > 32767 || q < -32768) div_error();
    if (rem) *rem = (s16)(dxax % divisor);
    return (s16)q;
}
static inline u16 div16_8(u16 ax, u8 divisor)       /* returns AX: AL = quotient, AH = remainder */
{
    if (divisor == 0 || ax / divisor > 0xFF) div_error();
    return (u16)((ax % divisor) << 8 | (ax / divisor));
}
static inline u16 idiv16_8(s16 ax, s8 divisor)      /* returns AX: AL = quotient, AH = remainder */
{
    if (divisor == 0) div_error();
    int q = ax / divisor;
    if (q > 127 || q < -128) div_error();
    return (u16)((u8)(s8)(ax % divisor) << 8 | (u8)(s8)q);
}

/* MSC 5.1 long-arithmetic helpers (_aFlmul, _aFldiv, _aFlrem, _aFulshr ...) are plain C operators on
 * s32/u32: C division truncates toward zero like the runtime's. */

/* Name of the original executable this build ports. */
#define SR_EXE_NAME "SR.EXE"

/* Loads SR_EXE_NAME (EXEPACK-packed or already unpacked), relocates it to LOAD_SEG and checks that it
 * is the expected build. Returns false and fills err on failure. */
bool mem_load_exe(const char *path, char *err, size_t errlen);

/* Size of the load image in bytes (code + initialised data). */
extern u32 mem_image_size;
