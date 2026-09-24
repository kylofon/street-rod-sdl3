# Video: the EGA/VGA 16-colour driver (`21a0`), blit helpers and drawing primitives

Scope: the VGA 16-colour path of SR.EXE (menu choice 3, `DS:8236 = -2`, `DS:0254 = 1`; choice 2 "EGA"
is the same code with `DS:0254 = 0`). Covers the driver vector table and everything it points to,
the page / descriptor model, every routine of segment `21a0` that the EGA path uses, the helper
segments `0e6c`, `0e92`, `24e7`, `24f4`, `2595`, `2634`, the two EGA span routines of `2645`, and the
drawing / text / rectangle primitives of `0f38` (section 4.9). The Tandy half of `21a0` and the
Tandy helpers (`2462`, `2487`, `24be`, `24d8`, `0e6c:0002/00c1`) are identified only (parked with the
Tandy build). Library loading (`0f38:6016`–`6f0a`), the timer and the mode menu are **platform**;
callers are game logic (**game_flow**, **garage**, **race**).

Conventions as in `port/RE_GUIDE.md`. "GCn" = graphics controller register n (index at 3CEh, data at
3CFh), "SEQ2" = sequencer map mask (index 2 at 3C4h, data at 3C5h), "CRTCn" = CRT controller
register n (3D4h/3D5h). All EGA register writes are listed because the port reproduces them through
the register model of section 4.1; results must be bit-exact in the four planes.

## 1. Overview

### 1.1 Mode, memory map, palette

* BIOS mode **0Dh** (320×200, 16 colours, 40 bytes per line, 4 planes), set by `0000:04f4` →
  `0000:01d1(0x0D)` (platform). The BIOS clears all planes. On VGA the mode is double-scanned
  (400 scan lines, 70 Hz); on EGA it is 200 lines at 60 Hz.
* **The palette IS programmed** (this corrects `RE_GUIDE.md`, `FORMATS.md` and `ega.h`): the
  direct `int 10h` instructions are only mode set / detection (`0000:2bdd`, `2bff`, `e627`,
  `e637`, `e64d`), but `0f38:1f4b` and `0f38:1fa4` call INT 10h through the C runtime `int86x`
  (`1e16:1ce8`): AX=1000h (one attribute palette register), AX=1001h (overscan) and AX=1002h (all 16
  + overscan). No DAC access (no 3C7h–3C9h, no AX=1010h+). `main` loads the palette `DS:0440` right
  after `gfx_init` (`0000:07a6`, EGA and Tandy), `gfx_init` itself loads it through slot 8, slot 7
  blacks the screen out (`DS:0452`, all 0), and the game sets single registers for colour effects
  (§4.10). Values > 7 are stored with bit 4 set (`v | 10h`, the 200-line intensity bit), so the
  visible colour of pixel value c is the standard 16-colour entry `ega_palette[pal[c]]` with
  `pal[c]` from the loaded table: the pictures of `LIB1`/`LIB2` are **not** meant to be seen with the
  identity palette (`tools/srlib.py` output is colour-shifted). Default table `DS:0440`:
  `00 00 08 07 0F 07 09 0B 03 02 04 0C 05 0A 06 0E` (value 1 shows black, 2 dark grey, …).
* Video memory (offsets in segment A000h, identical in the four planes):

| Offset | Size | Use | Descriptor |
|---|---|---|---|
| `0000`–`1F3F` | 8000 | page 0: the visible menu screen; in the race the dashboard (rows 0–99 are shown below the split) | `2e3e:0000` (`DS:822A` at start, `DS:7316`) |
| `1F40`–`1F6F` | 48 | mouse pointer save-under (3 bytes × 16 rows, all 4 planes via the latches) | `DS:7AEC` (built by `0f38:2b28`) |
| `2000`–`3F3F` | 8000 | page 1: back buffer of the menus (descriptor pointer `A200:0000`); race frame buffer A | `2e3e:0030` (`DS:822E`, `DS:731A`) |
| `4000`–`5F3F` | 8000 | page 2: race frame buffer B (`A400:0000`); `21a0:28ec` save area at `4AF0` | `2e3e:0060` (`DS:731E`, built by `0f38:1859`) |
| `6000`–`7F3F` | 8000 | page 3 | `2e68:0000` (`DS:7322`, built by `0f38:1859`) |
| `7D00`–`FBFF` | 32000 per plane | **picture cache**: packed LIB pictures stored in *one* plane each (`0000:3967`, `0f38:62ba`) | – |

  Page 3 (`6000`–`7F3F`) overlaps the first 0x240 bytes of the cache (`7D00`–`7F3F`, from row 185
  of page 3) in each plane. Whether page 3 is ever filled below row 185 while the cache holds
  pictures is an open question (§8); the port gets the same result for free because it keeps one
  64 KB array per plane.
* The screen is shown from the CRTC start address (`0000` except while the race / `21a0:25d9`
  flip pages with `21a0:182e`); in the race the **line compare** splits the screen: rows 0–99 come
  from the CRTC start (page 1 or 2), rows 100–199 from offset 0 (page 0 rows 0–99), see §4.3.

### 1.2 Driver vector table

`0000:0316` (platform, driver selection) calls `21a0:1128` for menu choices 2/3 after setting
`DS:8236 = -2` (and `DS:0254 = 1` for choice 3); for Tandy (choice 5, `DS:8236 = -6`) it calls
`21a0:0952`. Both copy 15 far pointers (0x1E words) from a DGROUP template into **`DS:78A2`**; the
game calls the driver only through `lcall [0x78A2 + 4*k]` (the "no direct callers" functions of
`21a0`). The table is not in the descriptor segment `DS:68E2`: `es:[0x24]`, `[0x54]`, `[0x84]` that
`0000:0316` writes are the *far pointer fields* (`+0x24`) of the three static descriptors in segment
`2e3e` (`DS:68E2 = 2e3e`), only for CGA/Tandy/Hercules; the EGA path leaves the static values.

| k | `DS:` | EGA/VGA (`DS:5BD0`) | Tandy (`DS:5B8A`) | calls | EGA meaning |
|---|---|---|---|---|---|
| 0 | 78A2 | `21a0:1862` | `21a0:04ab` | 97 | `blit(src, dst, rect, flags)` descriptor → descriptor |
| 1 | 78A6 | `21a0:223b` | `21a0:07ac` | 2 | `blit_shifted(src, dst, rect)` memory → screen at any x |
| 2 | 78AA | `21a0:1cd5` | `21a0:0358` | 39 | `blit_masked(src, dst, rect, mode)` colour 0 transparent |
| 3 | 78AE | `21a0:113a` | `21a0:0964` | 20 | `draw_text(dst, x, y, str)` |
| 4 | 78B2 | `0e6c:0175` | `0e6c:0002` | 1 | `draw_cursor(...)` mouse pointer with save-under |
| 5 | 78B6 | `21a0:16dc` | `21a0:0c09` | 20 | `copy_page(src_ptr, dst_ptr)` 8000 bytes |
| 6 | 78BA | `21a0:1508` | `21a0:0a4f` | 2 | `recolour_rect(dst, from, to, x0, y0, x1, y1)` |
| 7 | 78BE | `0f38:0b2d` | `0f38:0b2d` | 11 | `pal_black()`: load palette `DS:0452` (all 0), `DS:8249 = 0` (§4.10) |
| 8 | 78C2 | `0f38:0b3f` | `0f38:0b3f` | 21 | `pal_normal()`: load palette `DS:0440`, `DS:8249 = 1` |
| 9 | 78C6 | `21a0:25d9` | `21a0:0d62` | 12 | `anim_step(anim)` sprite animation with page flipping |
| 10 | 78CA | `21a0:1722` | `21a0:0c26` | 5 | `blinds(dst_ptr, src_ptr)` 5-pass interleaved page copy |
| 11 | 78CE | `21a0:28ec` | `0f38:a87e` | 7 | `slide_sprite(top, bottom, x, from, to, sprite)` |
| 12 | 78D2 | `21a0:23bc` | `2487:000a` | 6 | `line(x0, y0, x1, y1, colour)` into segment `DS:787A` |
| 13 | 78D6 | `2645:0034` | `2487:0257` | 33 | `hspan(x0, x1, y, colour)` clipped to `DS:5E4E`–`5E54` |
| 14 | 78DA | `2645:00e7` | `2487:0309` | 1 | `fill_rows(y, colour)` rows y … `DS:5E52`-1 |

Calls = number of `lcall [slot]` sites in the program. Slot users (function: slot×count) are listed
in §8.1 for the other specs.

### 1.3 Call graph (EGA path)

```
0000:0316 driver select (platform) ── 21a0:1128 install vectors (DS:5BD0 → DS:78A2)
0000:066f main ── 0f38:1859 gfx_init(&desc[0], &desc[1])  → DS:822A/822E, 8236..823E, colours, page table DS:7316
               └─ 0f38:0dbd gfx_screen_mode(n)            → CRTC start 0, CRTC13 = 14h, 21a0:0014(-1)
game code ──► [DS:78A2+4k] ──┬─ 21a0:1862 blit ──┬─ 0f38:1680 clip_rect
                             │                    ├─ 24e7:0008 latched rect copy (screen→screen)
                             │                    └─ 1e16:1dc8 movedata (per plane, read map / map mask)
                             ├─ 21a0:223b blit_shifted ── 21a0:1f8d put_shifted_row ── 21a0:1ea2 shift_row
                             ├─ 21a0:1cd5 blit_masked ── 0f38:1680, [2634:000a | 2595:0004] per plane
                             ├─ 21a0:113a draw_text (font DS:6A2A)
                             ├─ 0e6c:0175 draw_cursor
                             ├─ 21a0:16dc copy_page ── 1e16:1dc8 (write mode 1)
                             ├─ 21a0:1508 recolour_rect (read mode 1 / write mode 2)
                             ├─ 21a0:25d9 anim_step ── 0f38:a1ee/a4b9/a0fa/aac2/aa37 (rects), 21a0:2568 ── 24e7:0008,
                             │                           21a0:1ae9 ── 0f38:1680, 24f4:000a, 24e7:008b,
                             │                           21a0:1cd5, 21a0:182e show_page, 24e7:0008
                             ├─ 21a0:1722 blinds (waits on the tick counter DS:05F8)
                             ├─ 21a0:28ec slide_sprite ── 2634:00c1, 21a0:1e2d, 24e7:008b, 2595:0004,
                             │                             [DS:78AA], 21a0:0006 wait retrace, 24e7:0008
                             ├─ 21a0:23bc line ── 21a0:252e pixel address
                             ├─ 2645:0034 hspan  (needs 2645:0008 set/reset on … 2645:001d off around it)
                             └─ 2645:00e7 fill_rows
0f38:7b22 (race) ── 21a0:0014(0x63) split on / (-1) off;  2645:224e/2288 ── 21a0:182e flip
0f38:65ae draw picture (platform) ── 0e92:0006 unpack (reads the VRAM cache through GC4), 2634:00c1 make mask
```

## 2. Function table

### 2.1 EGA driver (segment 21a0, `1128`–`2566`)

| Address | Proposed name | Signature | Purpose | Conf. |
|---|---|---|---|---|
| `21a0:0006` | `ega_wait_vretrace` | `void far(void)` | wait while 3DAh bit0 set, then until bit3 set | verified |
| `21a0:0014` | `ega_set_split` | `void far(int line)` | CRTC line compare (waits for retrace first); -1 = off; VGA adds 100 | verified |
| `21a0:1128` | `ega_install_vectors` | `void far(void)` | copy 15 far ptrs `DS:5BD0` → `DS:78A2` | verified |
| `21a0:113a` | `ega_draw_text` | `void far(Desc far *d, int x, int y, char near *s)` | slot 3: render string to an 8-row bit buffer, write it with write mode 2 in bg `DS:8243` / fg `DS:8242`; end x → `DS:824C`, y → `DS:824E` | verified |
| `21a0:1508` | `ega_recolour_rect` | `void far(Desc far *d, int from, int to, int x0, int y0, int x1, int y1)` | slot 6: replace colour `from` by `to` in [x0,x1)×[y0,y1) (read mode 1 colour compare, write mode 2) | verified |
| `21a0:16dc` | `ega_copy_page` | `int far(u8 far *src, u8 far *dst)` | slot 5: latched copy of 8000 bytes (write mode 1), returns 0 | verified |
| `21a0:1722` | `ega_blinds` | `int far(u8 far *dst, u8 far *src)` | slot 10: 5-pass interleaved latched page copy, ≥3 ticks per pass | verified |
| `21a0:182e` | `ega_show_page` | `void far(int page)` | CRTC start = page×2000h, then wait for vertical retrace | verified |
| `21a0:1862` | `ega_blit` | `u16 far(Desc far *src, Desc far *dst, Rect near *r, u8 flags)` | slot 0: clipped copy; flags bit0 colour planes, bit1 mask plane | verified |
| `21a0:1ae9` | `ega_sprite_over_bg` | `int far(Desc far *spr, Desc far *dst, Desc far *bg, Tail near *t, Rect near *r)` | per plane dst = (bg & mask) \| sprite, then optional latched tail rows | verified |
| `21a0:1cd5` | `ega_blit_masked` | `int far(Desc far *src, Desc far *dst, Rect near *r, int mode)` | slot 2: transparent blit via `2634:000a` (mode 0) or `2595:0004` (mode ≠ 0) per plane | verified |
| `21a0:1e2d` | `ega_save_rect` | `void far(u8 far *src, int w, int h, u8 far *dst, int src_stride)` | latched copy of a w×h rect into a packed VRAM buffer | verified |
| `21a0:1ea2` | `shift_row` | `void near(u8 near *row, int shift, int n)` | shift n bytes right (shift>0) / left (<0) by \|shift\| bits in place | verified |
| `21a0:1f8d` | `ega_put_shifted_row` | `void near(u8 far *dst, u8 near *buf4, int w, int bit, int shift)` | write one 4-plane row buffer (4×40 bytes) at bit offset `bit` | verified |
| `21a0:223b` | `ega_blit_shifted` | `void far(Desc far *src, Desc far *dst, Rect near *r)` | slot 1: unclipped memory → screen blit at any pixel x | verified |
| `21a0:23bc` | `ega_line` | `void far(int x0, int y0, int x1, int y1, int colour)` | slot 12: Bresenham line with set/reset into segment `DS:787A` | verified |
| `21a0:252e` | `ega_pixel_addr` | near asm, in AX=y BX=x, out ES:BX, CL=7-(x&7), AH=1 | address of pixel in segment `DS:787A` | verified |
| `21a0:254d` | `ega_get_pixel` | near asm | reads the 4-bit colour of a pixel via GC4; **no caller (dead)** | verified |
| `21a0:2568` | `anim_restore_rect` | `void near(Rect4 near *r, Desc far *page, Desc far *bg, int mode)` | latched copy of a dirty rect from the background page | likely |
| `21a0:25d9` | `ega_anim_step` | `int far(Anim near *a)` | slot 9: one frame of a sprite animation over a VRAM background, flips pages | likely |
| `21a0:28ec` | `ega_slide_sprite` | `void far(int top, int bottom, int x, int from, int to, Desc far *spr)` | slot 11: move a sprite vertically inside a window, 2 rows per frame, back page → front | likely |

The first half of `21a0` (`0006`/`0014` aside) is the Tandy driver: `00ac`, `017c`, `020b`, `0358`
(slot 2), `04ab` (slot 0), `0677`, `072c`, `07ac` (slot 1), `0952` (install), `0964` (slot 3, text,
uses `24be:0002/00cb`), `0a4f` (slot 6), `0c09` (slot 5), `0c26` (slot 10), `0cf4`, `0d62` (slot 9).
Parked.

### 2.2 Helper segments

| Address | Proposed name | Signature | Purpose | Conf. |
|---|---|---|---|---|
| `0e6c:0175` | `ega_draw_cursor` | `void far(u8 far *img, u8 far *mask, u8 far *save, u8 far *scr, int xb, int y, int wb, int rows)` | slot 4: save one plane under the pointer, write 4 image planes through the mask as bit mask | verified |
| `0e6c:0002`, `0e6c:00c1` | `tdy_draw_cursor`, `tdy_…` | | Tandy slot 4 / helper (parked) | guess |
| `0e92:0006` | `pic_unpack` | `int far(u8 far *src, u8 far *dst, int n, u8 near *tokens, int ntok)` | picture unpacker (`FORMATS.md`, ported in `srport/src/platform/pic.c`) | verified |
| `24e7:0008` | `ega_latch_copy` | `void far(u8 far *src, u8 far *dst, int wb, int h, int xb, int y, int stride)` | VRAM→VRAM rect copy in write mode 1, same stride both sides | verified |
| `24e7:008b` | `ega_latch_copy_to40` | `void far(u8 far *src, int wb, int h, u8 far *dst, int src_stride)` | VRAM→VRAM copy, destination stride fixed 40 | verified |
| `24f4:000a` | `rows_composite` | `void far(u8 far *dst, u16 seg, u16 src, u16 mask, u16 bg, int stride, int wb, int h, int bg_stride, int dst_stride)` | one plane: dst = (mask & bg) \| src (unrolled, words) | verified |
| `2595:0004` | `rows_mask_or` | `void far(u8 far *dst, u16 seg, u16 src, u16 mask, int stride, int wb, int h, int dst_stride)` | one plane: dst = (dst & mask) \| src (unrolled, words) | verified |
| `2634:000a` | `rows_mask_blend` | same as `2595:0004` | one plane: dst = (dst & mask) \| (src & ~mask) | verified |
| `2634:00c1` | `make_mask_plane` | `void far(u8 far *planes, u8 far *mask, u16 total)` | mask = ~(p0\|p1\|p2\|p3), n = total/4 bytes per plane | verified |
| `2645:0008` | `ega_setreset_on` | `void far(void)` | if EGA: GC1 = 0Fh, GC3 = high byte of `DS:5C1E` (0) | verified |
| `2645:001d` | `ega_setreset_off` | `void far(void)` | if EGA: GC0 = 0, GC1 = 0, GC3 = 0, GC8 = FFh | verified |
| `2645:0034` | `ega_hspan` | `void far(int x0, int x1, int y, int colour)` | slot 13: clipped horizontal span, relies on `2645:0008` | verified |
| `2645:00e7` | `ega_fill_rows` | `void far(int y, int colour)` | slot 14: fill rows y … `DS:5E52`-1 with a colour | verified |
| `2462:000e`–`0229` | `tdy_pic_*` | | Tandy picture unpack (`005a`, `0120`) and mask (`0229`), called by `0f38:65ae` for `-6` | guess |
| `2487:000a`, `0257`, `0309` | `tdy_line`, `tdy_hspan`, `tdy_fill_rows` | | Tandy slots 12–14; `2487:022b` pixel address | likely |
| `24be:0002`, `00cb` | `tdy_text_*` | | helpers of Tandy text `21a0:0964` | guess |
| `24d8:0004`, `0047` | `tdy_blit_*` | | helpers of Tandy blit `21a0:04ab` | guess |

### 2.3 Graphics set-up and helpers outside the driver

| Address | Proposed name | Signature | Purpose | Conf. |
|---|---|---|---|---|
| `0f38:1859` | `gfx_init` | `void far(Desc far *a, Desc far *b)` | set the two page descriptors, mode constants, colour scheme, page table; calls slot 8 and `0f38:4bff` | verified |
| `0f38:0dbd` | `gfx_screen_mode` | `void far(int n)` | 0 save pages, 1/4 restore + split off + start 0, 2 start 0, 3 single-buffer (front = back) | verified |
| `0f38:1680` | `clip_rect` | `int far(Desc far *src, Desc far *dst, Rect near *in, Rect near *out)` | pixel rect → byte rect, clipped to src and dst; 1 if non-empty | verified |
| `0f38:1640` | `far_memset` | `void far(u8 far *p, u8 v, u16 n)` | `rep stosb` | verified |
| `0f38:177d` | `wait_vretraces` | `void far(int n)` | n × wait for start of vertical retrace (3DAh bit3; Hercules 3BAh bit7) | verified |
| `0000:3967` | `vram_pool_alloc` | `u8 far *far(u16 size, u8 near *plane)` | first-fit in 4 per-plane pools of 32000 bytes at A000:7D00; plane or FFh (see platform) | verified |
| `0000:376b`, `3880`, `3904` | `bitmap_alloc`, `bitmap_make`, `bitmap_init` | | create / fill bitmap descriptors (see platform, layout in §5.1) | likely |
| `1e16:1dc8` | `movedata` | `void far(u16 sseg, u16 soff, u16 dseg, u16 doff, u16 n)` | MS C runtime `rep movsb`; on VRAM it goes through the latches (see §4.1) | verified |
| `0f38` primitives | | | see §4.9 | |

## 3. Globals table

| DS offset | Proposed name | Type | Meaning | Written by | Read by |
|---|---|---|---|---|---|
| `0254` | `g_vga` | u16 | 1 = VGA (menu 3): split screen uses the 400-line formula | `0000:0316` | `21a0:0014`, `0f38:7b22`, `0000:c1d8`, `0000:0226` |
| `02D0` | `g_anim_yoff` | i16 | subtracted from the sprite y in `21a0:25d9` | game | `21a0:25d9` |
| `02E6`, `02EA` | `g_saved_page_b`, `g_saved_page_a` | far Desc* | pages saved by `gfx_screen_mode(0)` | `0f38:0dbd` | `0f38:0dbd` |
| `03A0`… | `g_scheme_ega` | u8[] | EGA/Tandy colour scheme; `+5..+9` = `0D 09 09 0F 01` copied to `8242..8246` | const | `0f38:1859` |
| `03F0`… | `g_scheme_cga` | u8[] | CGA scheme (`+5..+9` = `01 02 02 03 01`) | const | `0f38:1859` |
| `0792` | `g_slide_overlay` | far Desc* | foreground drawn over each `slide_sprite` frame (if non-null) | game | `21a0:28ec` |
| `4C14` | `g_vram_pool_base` | far ptr | `A000:0000` | const | `0000:3967` |
| `4C18` | `g_vram_pool_used` | u16[4] | bytes used in each plane's cache pool | `0000:3967` | `0000:3967` |
| `5B8A` | `g_vec_tandy` | far ptr[15] | driver template, Tandy | const | `21a0:0952` |
| `5BD0` | `g_vec_ega` | far ptr[15] | driver template, EGA/VGA (values in §1.2) | const | `21a0:1128` |
| `5C0C` | `g_blinds_rows` | u16[5] | `0000 0050 00A0 0028 0078`: start offsets (rows 0,2,4,1,3) of the 5 passes | const | `21a0:1722` |
| `5C15` | `g_lmask` | u8[9] | `00 80 C0 E0 F0 F8 FC FE FF`: the n leftmost bits | const | `21a0:1ea2`, `1f8d` |
| `5C1E` | `g_gc3` | u16 | high byte → GC3 (data rotate / function) for lines and spans; 0 = replace. Never written | const | `21a0:23bc`, `2645:0008`, `2645:00e7` |
| `5E3E`, `5E40` | `g_race_draw_page`, `g_race_show_page` | u16 | race double buffering (1 or 2) | `2645:2288` | `2645:224e/2288` (see race) |
| `5E4E`, `5E50`, `5E52`, `5E54` | `g_span_xmin`, `g_span_ymin`, `g_span_ymax`, `g_span_xmax` | i16 | clip window of `hspan` / `fill_rows` | `2645:23f7/2410`, `0f38:1094/1130` | `2645:0034`, `00e7` |
| `68E2` | `g_desc_seg` | u16 | segment of the static descriptors (`2e3e`) | const | `0000:0316` |
| `691A` | `g_font_seg2` | u16 | font segment (`0e9f`) used by `0f38:17c8` string width | const | `0f38:17c8` |
| `6A2A` | `g_font_seg` | u16 | font segment (`0e9f`) of `ega_draw_text` | const | `21a0:113a` |
| `70AC` | `g_slide_overlay_rect` | Rect | rect for `g_slide_overlay` | game | `21a0:28ec` |
| `7316`, `731A`, `731E`, `7322` | `g_page[4]` | far Desc*[4] | page 0 (`822A`), page 1 (`822E`), `2e3e:0060` (A000:4000), `2e68:0000` (A000:6000) | `0f38:1859` | `0f38:819a/8336/8e48/990f/b82c/be93` |
| `767C` | `g_draw_off` | u16 | draw offset (non-EGA line/span drivers) | `0f38:01d2…`, `2645:224e/2288` | Tandy/CGA drivers |
| `787A` | `g_draw_seg` | u16 | segment drawn by `line`, `hspan`, `fill_rows` (A000/A200/A400 on EGA) | same | `21a0:252e`, `2645:014b`, `2645:00e7` |
| `787E` | `g_scheme` | near u8* | colour scheme in use (`03A0` / `03F0`) | `0f38:1859` | `0f38:1859` |
| `78A2` | `drv_vec` | far fn[15] | driver vector table (§1.2) | `21a0:1128`, `21a0:0952` | everyone |
| `822A` | `g_front` | far Desc* | front (visible) page descriptor | `0f38:1859`, `0f38:0dbd` | 56 functions |
| `822E` | `g_back` | far Desc* | back page descriptor (= front after `gfx_screen_mode(3)`) | same | 58 functions |
| `8232`, `8234` | `g_view_x`, `g_view_y` ? | i16 | zeroed by `gfx_init`; meaning open (§8 q5, garage) | `0f38:1859`, `0f38:3e35`, `0000:81ab`, `0000:a265` | `0f38:3697`, `37fc`, `3a58`, `3c19`… |
| `8236` | `driver_id` | i16 | -2 EGA/VGA, -3 CGA (`FDh`), -4 Hercules, -6 Tandy | `0000:0316`, `0f38:1859` | 70 functions |
| `8238` | `g_nplanes` | u16 | 4 on EGA (1 otherwise) | `0f38:1859` | desc allocators, … |
| `823A` | `g_px_shift` | u16 | pixel → byte shift: 3 on EGA | `0f38:1859` | `0f38:1680`, allocators |
| `823C` | `g_px_factor` | u16 | bits per pixel in a byte: 1 on EGA (2 CGA, 4 Tandy) | `0f38:1859` | |
| `823E` | `g_x_align` | i16 | byte alignment mask of x: `FFF8h` on EGA | `0f38:1859` | |
| `8240` | `g_mirror` | u16 | 1 = draw on page B then mirror the rect to page A (`MIRROR`); 0 after `gfx_screen_mode(3)` (single buffer) | many | `21a0:28ec`, `0f38` UI |
| `8242` | `g_text_fg` | u8 | text foreground (scheme +5, `0D`) | `0f38:1859`, UI | `21a0:113a` |
| `8243` | `g_text_bg` | u8 | text background (scheme +6, `09`) | same | `21a0:113a` |
| `8244` | `g_fill_col` | u8 | fill colour of `fill_rect` | scheme, UI | `0f38:4302` |
| `8245` | `g_line_col` | u8 | line / frame colour | scheme, UI | `0f38:439c/43ff/4432/45d7` |
| `8246` | `g_frame_style` | u8 | 0 none, 1 single, 2 double, 3 solid | scheme, UI | `0f38:4432`, menus |
| `8247` | `g_ui_level` | u8 | 0 text mode, 1 graphics (`gfx_init`), 2 pointer ready (`0f38:2b28`) | `0f38:1859`, `2b28` | UI, `0f38:23ce` |
| `8248` | `g_overscan` | u8 | border colour for `ega_set_palette` (BSS, 0) | – | `0f38:1fa4` |
| `8249` | `g_pal_visible` | u8 | 0 after `pal_black`, 1 after `pal_normal` | `0f38:0b2d/0b3f` | UI |
| `824C`, `824E` | `g_text_end_x`, `g_text_y` | i16 | end x and y of the last `draw_text` | `21a0:113a` | UI |
| `8266`, `8268` | | i16 | set to -1 by `gfx_init` | `0f38:1859` | UI |
| `0440` | `pal_shadow` | u8[17] | palette shadow / default table (§4.10), updated by `ega_set_palreg` | `0f38:1f4b`, `182c`, `181c` | `0f38:0b3f`, `0000:07a6` |
| `0452`, `0462`, `04AC`, `02D4`, `0692` | `pal_black_tab`, `pal_alt_tab`, `pal_fatal_tab`, `pal_intro_tab`, `pal_protect_tab` | u8[16] | palette tables (§4.10) | const | `ega_set_palette` callers |
| `472A`, `472C` | `g_mouse_x`, `g_mouse_y` | i16 | pointer position (platform) | platform | `0f38:2d7e` |
| `4733` | `g_ptr_show` | i8 | pointer visible when > 0 | `0f38:2d7e` | same |
| `7AEC` | `g_ptr_save` | far Desc* | save-under descriptor (planes A000:1F40) | `0f38:2b28` | `0f38:2e8b` |
| `7AF0`–`7B12` | `g_ptr_*` | | saved w/h/x/y, bank pointers `7AFC`/`7B00`, shape offset `7B04`, stride `7B06`, shape `7B08`, mode `7B0A`, last rect `7B0C`–`7B12` | `0f38:2d7e/2e8b/2b28` | same |
| `05F8` | `g_ticks` | u32 | timer tick counter (platform) | ISR | `21a0:1722`, `0f38:62ba` |

## 4. Pseudocode

### 4.1 EGA register model (port base, on top of `platform/ega.h`)

The driver programs the card directly and many routines rely on registers left by a previous one
(e.g. `0e6c:0175` never writes the sequencer index, `2645:0034` never enables set/reset, `0f38:65ae`
leaves GC4 at the cache plane). The port therefore models the registers, exactly like
`../TestDrive2/td2port/src/platform/gfx_ega.h`, and every routine is transcribed with `out`/`in`,
`vrd`/`vwr`:

```c
/* platform/vga16.h (new, private to the video module) */
typedef struct {
    u8 seq_index, gc_index, crtc_index;   /* last index written to 3C4h / 3CEh / 3D4h */
    u8 map_mask;     /* SEQ2, reset 0Fh */
    u8 set_reset;    /* GC0 */
    u8 enable_sr;    /* GC1 */
    u8 compare;      /* GC2 colour compare */
    u8 func;         /* GC3 bits 3-4: 0 replace 1 AND 2 OR 3 XOR (rotate bits 0-2 unused: always 0) */
    u8 read_map;     /* GC4 */
    u8 mode;         /* GC5: bits 0-1 write mode, bit 3 read mode */
    u8 dont_care;    /* GC7, BIOS value 0Fh, never written by the game */
    u8 bit_mask;     /* GC8, reset FFh */
    u8 latch[4];
    u16 start;       /* CRTC 0Ch/0Dh */
    u16 line_compare;/* CRTC 18h + 07h bit4 (+ 09h bit6 on VGA) */
} VgaRegs;
extern VgaRegs vga;

void vga_out(u16 port, u8 v);        /* out dx,al: 3C4h/3CEh/3D4h store the index, 3C5h/3CFh/3D5h the data */
void vga_outw(u16 port, u16 ax);     /* out dx,ax = vga_out(port, al); vga_out(port+1, ah) */
u8   vga_in(u16 port);               /* 3DAh (retrace, host timing), 3D5h (VGA readback of 07h/09h) */
u8   vga_rd(u16 off);                /* CPU read of A000:off: latch[k] = plane[k][off];
                                        read mode 0: plane[read_map & 3][off];
                                        read mode 1: bits where ((plane bits ^ compare) & dont_care) == 0 */
void vga_wr(u16 off, u8 v);          /* CPU write: for each plane k with map_mask bit k:
                                        mode 1: plane = latch[k];
                                        mode 2: x = (v>>k&1) ? FF : 00, then func, then bit_mask vs latch;
                                        mode 0: x = enable_sr bit k ? (set_reset bit k ? FF:00) : v, func, bit_mask vs latch */
/* 0xA000 <= seg < 0xB000 is video memory: linear (seg-0xA000)*16+off (A200:0000 = offset 2000h). */
u8   vrd(u16 seg, u16 off);          /* VRAM → vga_rd, else rd8(seg,off) */
void vwr(u16 seg, u16 off, u8 v);    /* VRAM → vga_wr, else wr8 */
void vmovedata(u16 sseg, u16 soff, u16 dseg, u16 doff, u16 n);  /* 1e16:1dc8 byte by byte through vrd/vwr */
```

Rules that fall out of the model and matter:

* Bit mask and function apply against the **latches**, not the current memory: the driver always
  reads the destination byte before a masked write (the port must do the same reads).
* In write mode 1 each byte copied VRAM→VRAM copies all 4 planes (map mask permitting); the
  8000-byte `copy_page` is therefore a full 4-plane page copy.
* 16-bit offset arithmetic wraps inside the 64 KB plane (`u16`).
* `ega.h`'s `ega_set_start()` / `ega_set_line_compare()` are driven from `vga.start` and the
  line-compare formula of §4.3.

### 4.2 Descriptors, rects, clipping

```c
typedef struct {            /* "Desc", 0x30 bytes, in mem[] (static ones in segments 2e3e/2e68) */
    u16 w;                  /* +00 width in pixels */
    u16 h;                  /* +02 height */
    u16 size;               /* +04 memory bitmap: bytes of all colour planes (= stride*h*4 on EGA);
                               screen page: 1F40h (8000 = one plane) */
    u8  pad[0x1e];          /* +06 unused by the driver (+1C FFFFh / +1E FFh in the static pages) */
    FarPtr planes;          /* +24 plane 0; plane k at planes + k*stride*h (screen: A000:off, planes parallel) */
    FarPtr mask;            /* +28 1 bit = transparent (colour 0); 0:0 = none. Same segment as planes */
    u16 stride;             /* +2C bytes per row ((w-1)>>DS:823A)+1; 28h for screen pages */
    u8  type;               /* +2E FEh = EGA screen page (FDh CGA, FCh Hercules, FAh Tandy); memory bitmap: flag byte */
} Desc;

typedef struct { i16 w, h, sx, sy, dx, dy; } Rect;   /* x in pixels on input, bytes after clip_rect */
```

Static descriptors (segment `2e3e`, `DS:68E2`):

| | w | h | +4 | +24 | +28 | +2C | +2E |
|---|---|---|---|---|---|---|---|
| `2e3e:0000` page 0 | 320 | 200 | 1F40h | A000:0000 | 0 | 28h | FEh |
| `2e3e:0030` page 1 | 320 | 200 | 1F40h | **A200:0000** | 0 | 28h | FEh |
| `2e3e:0060` | 320 | 200 | 4000h | A000:2000 | 0 | 50h | FDh (CGA default) → overwritten by `gfx_init` with a copy of page 1 and +24 = **A000:4000** |
| `2e68:0000` | – | – | – | – | – | – | zero → copy of page 1, +24 = A000:6000 |

`0f38:1680 clip_rect(src, dst, in, out)` (far):

```c
int clip_rect(Desc far *s, Desc far *d, Rect *in, Rect *o)
{
    i16 sh = DS_823A;                       /* 3 */
    i16 sx = in->sx >> sh, sy = in->sy;     /* sar */
    i16 w  = in->w  >> sh, h  = in->h;
    if (sx + w > s->stride) w = s->stride - sx;      /* signed compares */
    if (sy + h > s->h)      h = s->h - sy;
    i16 dx = in->dx >> sh, dy = in->dy;
    if (dy < 0) { h += dy; sy -= dy; dy = 0; }
    if (d->h < dy + h)      h = d->h - dy;
    if (dx < 0) { w += dx; sx -= dx; dx = 0; }
    if (d->stride < dx + w) w = d->stride - dx;
    o->sx = sx; o->sy = sy; o->dx = dx; o->dy = dy; o->w = w; o->h = h;
    return o->w > 0 && h > 0;
}
```

Negative *source* coordinates are not clipped. `w` is in bytes: sub-byte x is lost (callers pass
multiples of 8 except to `blit_shifted`).

### 4.3 CRTC: split screen, page flip, retrace waits

```c
void ega_wait_vretrace(void)                 /* 21a0:0006 */
{
    while (in(0x3DA) & 1) ;                  /* while display disabled (in a blanking interval) */
    while (!(in(0x3DA) & 8)) ;               /* until vertical retrace */
}

void ega_set_split(int line)                 /* 21a0:0014 */
{
    u16 crtc = rd16(0x40, 0x63);             /* BIOS CRTC base, 3D4h */
    while (in(crtc + 6) & 8) ;               /* wait for the start of a vertical retrace */
    while (!(in(crtc + 6) & 8)) ;
    if (line == -1) line = DS_0254 ? 0x3FF : 0x1FF;
    if (!DS_0254) {                          /* EGA: overflow register written blind */
        cli; outw(crtc, (line & 0xFF) << 8 | 0x18);
        outw(crtc, (0x11 & 0xEF | (line >> 8 & 1) << 4) << 8 | 0x07); sti;
    } else {                                 /* VGA: 400 scan lines */
        if (line <= 0xC8) line += 0x64;      /* signed compare; 99 -> 199 */
        cli; outw(crtc, (line & 0xFF) << 8 | 0x18);
        out(crtc, 7); r = in(crtc + 1); outw(crtc, (r & 0xEF | (line >> 8 & 1) << 4) << 8 | 7);
        out(crtc, 9); r = in(crtc + 1); outw(crtc, (r & 0xBF | (line >> 9 & 1) << 6) << 8 | 9); sti;
    }
}
```

The game uses only `0x63` (`0f38:7b22`, race start, EGA only: `DS:8236 == -2`) and `-1`
(`0f38:0dbd` 1/4, `0f38:7b22` end). The line compare register L makes scan lines after L restart at
offset 0: EGA L = 99 → rows 0–99 from the start address, rows 100–199 from offset 0; VGA L = 199 on
400 scan lines → the same 100/100 rows. Port: `ega_set_line_compare(first_row)` with
`first_row = DS_0254 ? (L + 1 + 1) / 2 : L + 1` for L < 400 / 200, `200` (off) for `0x1FF`/`0x3FF`;
for the only value used, 100.

```c
void ega_show_page(int page)                 /* 21a0:182e */
{
    out(0x3D4, 0x0C); out(0x3D5, (u8)(page << 5));   /* start = page * 2000h */
    out(0x3D4, 0x0D); out(0x3D5, 0);
    while (in(0x3DA) & 8) ;  while (!(in(0x3DA) & 8)) ;
}
```

Other CRTC writers: `0f38:1859` (EGA: start = 0), `0f38:0dbd` (§4.8). CRTC 13h (offset) is written
only with 14h (the mode 0Dh value). Retrace waits: `21a0:0006`, `21a0:0014`, `21a0:182e`,
`0f38:177d` (n times "wait while bit3, then until bit3"; Hercules: 3BAh bit 7), `0f38:1f4b` (§4.9).

### 4.4 Blits

#### `21a0:1862 ega_blit(src, dst, r, flags)` — slot 0

```c
u16 ega_blit(Desc far *s, Desc far *d, Rect *r, u8 flags)
{
    bool mask2 = (flags & 2) && s->mask && d->mask;
    Rect c;
    if (!clip_rect(s, d, r, &c)) return 0;
    FarPtr sp, dp; int p0;
    if (flags & 1) { sp = s->planes; dp = d->planes; p0 = 0; }
    else           { sp = s->mask;   dp = d->mask;   p0 = 4; }
    int pend = mask2 ? 5 : 4;
    int sstr = s->type == 0xFE ? 0x28 : s->stride;
    int sstep = s->type == 0xFE ? 0 : s->h * sstr;          /* imul, 16-bit */
    sp.off += c.sy * sstr + c.sx;
    int dstr = d->type == 0xFE ? 0x28 : d->stride;
    int dstep = d->type == 0xFE ? 0 : d->h * dstr;
    dp.off += c.dy * dstr + c.dx;
    if (d->type == 0xFE && s->type == 0xFE)                  /* screen -> screen: latches */
        return ega_latch_copy(sp, dp, c.w, c.h, 0, 0, 0x28);
    bool scr = dstep == 0 || sstep == 0;
    if (scr) { out(0x3CE, 4); out(0x3C4, 2); }               /* select GC4 and SEQ2 */
    u8 mm = 1;
    for (int p = p0; p < pend; p++) {
        if (scr) {
            if (sstep == 0) out(0x3CF, p);                   /* read map (4 = plane 0 on the card) */
            if (dstep == 0) out(0x3C5, mm);                  /* map mask (10h = no plane) */
        }
        if (c.w == dstr && c.w == sstr)
            vmovedata(sp.seg, sp.off, dp.seg, dp.off, c.w * c.h);
        else for (int y = c.h, so = sp.off, dof = dp.off; y > 0; y--, so += sstr, dof += dstr)
            vmovedata(sp.seg, so, dp.seg, dof, c.w);
        mm <<= 1; sp.off += sstep; dp.off += dstep;
    }
    if (scr) { out(0x3C4, 2); out(0x3C5, 0x0F); return 0x0F; }
    return pend;
}
```

The copies run in whatever write mode is set (always 0 here, set/reset off, bit mask FFh): a
screen source is read through GC4, a screen destination written through the map mask. With
flags 3 on a memory→memory copy, the fifth "plane" is the mask, contiguous after plane 3 (the
allocator guarantees it). The `if (c.w == dstr …)` single-copy path is taken for full-width copies.

#### `24e7:0008 ega_latch_copy(src, dst, wb, h, xb, y, stride)` and `24e7:008b`

```c
void ega_latch_copy(FarPtr s, FarPtr d, int wb, int h, int xb, int y, int stride)
{
    if (wb < 1 || h < 1) return;
    out(0x3CE, 5); out(0x3CF, 1);                   /* write mode 1 */
    u16 o = (u8)y * (u8)stride + xb;               /* "mul dl": 8-bit operands */
    s.off += o; d.off += o;
    if (wb == 1)            for (; h; h--) { vwr(d, vrd(s)); s.off += stride; d.off += stride; }
    else if (wb == stride)  vmovedata(s, d, (u8)h * (u8)stride);
    else for (; h; h--) { vmovedata(s, d, wb); s.off += stride; d.off += stride; } /* advances by wb then stride-wb */
    out(0x3CE, 5); out(0x3CF, 0);
}

void ega_latch_copy_to40(FarPtr s, int wb, int h, FarPtr d, int sstride)   /* 24e7:008b */
{
    if (h < 1 || wb < 1) return;
    out(0x3CE, 5); out(0x3CF, 1);
    for (u8 n = (u8)h; n; n--) {                   /* 8-bit row counter */
        vmovedata(s, d, wb); s.off += sstride; d.off += 0x28;
    }
    out(0x3CE, 5); out(0x3CF, 0);
}
```

Both only make sense VRAM→VRAM (the latches are loaded by the reads).

#### `21a0:1cd5 ega_blit_masked(src, dst, r, mode)` — slot 2

```c
int ega_blit_masked(Desc far *s, Desc far *d, Rect *r, int mode)
{
    Rect c;
    if (!clip_rect(s, d, r, &c)) return 0;
    int sstr = s->stride, sstep = s->h * sstr;
    int dstr = d->type == 0xFE ? 0x28 : d->stride;
    int dstep = d->type == 0xFE ? 0 : d->h * dstr;
    FarPtr dp = { d->planes.seg, d->planes.off + c.dy * dstr + c.dx };
    u16 seg = s->planes.seg;
    u16 so = s->planes.off + c.sx + c.sy * sstr;
    u16 mo = s->mask.off   + c.sx + c.sy * sstr;          /* mask assumed in the planes' segment */
    void (*fn)() = mode ? rows_mask_or /*2595:0004*/ : rows_mask_blend /*2634:000a*/;
    if (dstep == 0) { out(0x3CE, 4); out(0x3C4, 2); }
    for (int p = 0, mm = 1; mm < 9; p++, mm <<= 1) {
        if (dstep == 0) { out(0x3CF, p); out(0x3C5, mm); }  /* read map = write plane */
        fn(dp, seg, so, mo, sstr, c.w, c.h, dstr);
        so += sstep; dp.off += dstep;
    }
    if (dstep == 0) { out(0x3C4, 2); out(0x3C5, 0x0F); return 0x0F; }
    return 0;
}
```

#### Plane row helpers (`2634:000a`, `2595:0004`, `24f4:000a`)

All three process one plane: rows of `wb` bytes, `h` rows; source, mask (and background) share the
segment `seg`; all use CPU reads/writes (so on screen the GC4/SEQ2 set by the caller select the
plane). Pointers advance by the strides per row (the byte offset inside a row is fixed).

```c
void rows_mask_blend(FarPtr d, u16 seg, u16 s, u16 m, int sstr, int wb, int h, int dstr) /* 2634:000a */
{   /* h: low byte only (dec cl); dstr: only the low byte is reduced by the word part (sub dl,ch) */
    if (h <= 0 || wb <= 0) return;
    for (u8 y = (u8)h; y; y--) {
        int i = 0;
        for (int n = (wb & 0xFE) >> 1; n; n--, i += 2) {       /* words */
            u16 mw = rd16(seg, m + i), dw = vrd16(d, i);
            vwr16(d, i, (dw & mw) | (~mw & rd16(seg, s + i)));
        }
        if (wb & 1) { u8 mb = rd8(seg, m + i); vwr(d, i, (vrd(d, i) & mb) | (~mb & rd8(seg, s + i))); }
        s += sstr; m += sstr; d.off += dstr;                   /* (as pointer deltas stride - (wb&~1)) */
    }
}

void rows_mask_or(FarPtr d, u16 seg, u16 s, u16 m, int sstr, int wb, int h, int dstr)  /* 2595:0004 */
{   /* unrolled: words at wb-2, wb-4, …, then the byte at 0 if wb is odd (right to left); wb <= A0h */
    if (h <= 0 || wb <= 0) return;
    for (int y = h; y; y--) {
        for (int i = wb - 2; i >= (wb & 1); i -= 2)
            vwr16(d, i, (rd16(seg, m + i) & vrd16(d, i)) | rd16(seg, s + i));
        if (wb & 1) vwr(d, 0, (rd8(seg, m) & vrd(d, 0)) | rd8(seg, s));
        s += sstr; m += sstr; d.off += dstr;
    }
}

void rows_composite(FarPtr d, u16 seg, u16 s, u16 m, u16 bg, int sstr, int wb, int h,
                    int bgstr, int dstr)                                            /* 24f4:000a */
{   /* same unrolled order as 2595:0004 */
    if (h <= 0 || wb <= 0) return;
    for (int y = h; y; y--) {
        for (int i = wb - 2; i >= (wb & 1); i -= 2)
            vwr16(d, i, (rd16(seg, m + i) & rd16(seg, bg + i)) | rd16(seg, s + i));
        if (wb & 1) vwr(d, 0, (rd8(seg, m) & rd8(seg, bg)) | rd8(seg, s));
        s += sstr; m += sstr; bg += bgstr; d.off += dstr;
    }
}
```

`vwr16`/`vrd16` = two byte accesses, low byte first (x86 word access to VRAM is two byte cycles, each
loading the latches). `rows_mask_or` does not clear the source under the mask: with masks built by
`make_mask_plane` (mask = 1 only where all planes are 0) it gives the same result as
`rows_mask_blend`; with other masks it differs, keep both.

```c
void make_mask_plane(FarPtr planes, FarPtr mask, u16 total)   /* 2634:00c1 */
{
    u16 n = total >> 2, i = 0;         /* bytes per plane */
    u16 cx = n;
    if (n & 1) { wr8(mask, 0, ~(p(0,0)|p(1,0)|p(2,0)|p(3,0))); if (--cx == 0) return; i = 1; }
    do {                               /* words: p(k,i) = planes[k*n + i] */
        wr16(mask, i, ~(p16(0,i)|p16(1,i)|p16(2,i)|p16(3,i)));
        i += 2; cx -= 2;
    } while (cx != 0);                 /* n == 0 would run 32 K words (never happens) */
}
```

#### `21a0:1ae9 ega_sprite_over_bg(spr, dst, bg, tail, r)`

```c
typedef struct { u16 off, seg, _4, rows; } Tail;    /* near; rows copied with latches after the main part */

int ega_sprite_over_bg(Desc far *sp, Desc far *d, Desc far *bg, Tail *t, Rect *r)
{
    int trows; FarPtr tsrc;
    if (t) {
        trows = t->rows;
        tsrc.off = (r->sx >> 3) + t->off; tsrc.seg = t->seg;
        if (r->dx < 0) tsrc.off += (7 - r->dx) >> 3;     /* uses the unclipped rect */
    } else { trows = 0; tsrc = 0:0; }
    Rect c;
    if (!clip_rect(sp, d, r, &c)) return 0;
    int sstr = sp->stride, sstep = sp->h * sstr;
    int bgstep = bg->stride * bg->h;
    int hmain = c.h - trows;
    int dstr = d->stride;                               /* not forced to 28h */
    int dstep = d->type == 0xFE ? 0 : d->h * dstr;
    FarPtr dp = { d->planes.seg, d->planes.off + c.dy * dstr + c.dx };
    u16 seg = sp->planes.seg;
    u16 so = sp->planes.off + c.sx + c.sy * sstr;
    u16 mo = sp->mask.off   + c.sx + c.sy * sstr;
    u16 bo = (sstr == bg->stride)
           ? bg->planes.off + c.dy * bg->stride              /* (sic) no x offset */
           : bg->planes.off + c.sy * bg->stride + c.dx;     /* (sic) source row, destination column */
    for (int mm = 1; mm < 9; mm <<= 1) {
        if (dstep == 0) { out(0x3C4, 2); out(0x3C5, mm); }
        rows_composite(dp, seg, so, mo, bo, sstr, c.w, hmain, bg->stride, dstr);
        dp.off += dstep; so += sstep; bo += bgstep;          /* the mask is not advanced */
    }
    if (dstep == 0) { out(0x3C4, 2); out(0x3C5, 0x0F); }
    if (trows > 0) {
        dp.off += 0x28 * hmain;
        ega_latch_copy_to40(tsrc, c.w, trows, dp, sstr);
    }
    return …;
}
```

The background is read **in the sprite's segment** (`ds` of `24f4:000a`): the caller keeps both in
one allocation. Note that the read map is not set: the background must not be the screen.

#### `21a0:223b ega_blit_shifted(src, dst, r)` — slot 1 (no clipping)

```c
void ega_blit_shifted(Desc far *s, Desc far *d, Rect *r)
{
    int sxb = r->sx / 8;                                 /* C division (toward zero) */
    FarPtr sp = { s->planes.seg, s->stride * r->sy + s->planes.off + sxb };
    FarPtr dp = { d->planes.seg, d->stride * r->dy + r->dx / 8 + d->planes.off };
    int nb = (r->sx + r->w - 1) / 8 - sxb + 1;
    int h = r->h;
    u16 pstep = s->size >> 2;                            /* shr: bytes per plane */
    out(0x3CE, 5); out(0x3CF, 0);
    int bit = r->dx & 7, shift = bit - (r->sx & 7);
    u8 buf[4][40];                                       /* stack, [bp-0xAA] */
    if (h <= 0) return;
    do {
        for (int b = 0; b < nb; b++)
            for (int k = 0; k < 4; k++) buf[k][b] = rd8(sp.seg, sp.off + b + k * pstep);
        ega_put_shifted_row(dp, &buf[0][0], r->w, bit, shift);
        sp.off += s->stride; dp.off += d->stride;
    } while (--h);
}

void shift_row(u8 *row, int s, int n)                    /* 21a0:1ea2 */
{
    u8 carry = 0;
    if (s > 0) {
        u8 hi = DS_5C15[s], lo = ~hi;
        for (; n > 0; n--, row++) {                      /* n <= 0: nothing */
            u8 v = ror8(*row, s);
            *row = (v & lo) | carry; carry = v & hi;
        }
    } else if (s < 0) {
        s = -s; row += n - 1;
        u8 hi = DS_5C15[8 - s], lo = ~hi;                /* 5C16[7-s] */
        for (; n > 0; n--, row--) {
            u8 v = rol8(*row, s);
            *row = (v & hi) | carry; carry = lo & v;
        }
    }
}

void ega_put_shifted_row(FarPtr d, u8 *buf, int w, int bit, int shift)   /* 21a0:1f8d */
{
    if (shift) for (int k = 0; k < 4; k++) shift_row(buf + 40 * k, shift, (w + 7) / 8);
    u8 *p[4] = { buf, buf + 40, buf + 80, buf + 120 };
    if (bit) {                                            /* left partial byte */
        u8 m = ~DS_5C15[bit];
        if (bit + w < 8) m &= DS_5C15[bit + w];
        out(0x3CE, 8); out(0x3CF, m);
        for (int k = 0; k < 4; k++) {
            out(0x3C4, 2); out(0x3C5, 1 << k);
            vrd(d); vwr(d, *p[k]++ & m);
        }
        d.off++;
        w -= 8 - bit;
    }
    out(0x3CE, 8); out(0x3CF, 0xFF);
    if (w > 7) {
        int n = -w / -8;                                  /* idiv */
        w += -8 * n;
        do { for (int k = 0; k < 4; k++) { out(0x3C4, 2); out(0x3C5, 1 << k); vrd(d); vwr(d, *p[k]++); }
             d.off++; } while (--n);
    }
    if (w > 0) {                                          /* right partial byte */
        u8 m = DS_5C15[w];
        out(0x3CE, 8); out(0x3CF, m);
        for (int k = 0; k < 4; k++) { out(0x3C4, 2); out(0x3C5, 1 << k); vrd(d); vwr(d, *p[k] & m); }
        d.off++;                                          /* only after plane 3 */
    }
    out(0x3CE, 8); out(0x3CF, 0xFF); out(0x3C4, 2); out(0x3C5, 0x0F);
}
```

(Only the plane-3 step of each byte increments `d`; the planes are written through SEQ2 one after
the other at the same offset.)

#### `21a0:16dc ega_copy_page(src, dst)` — slot 5 and `21a0:1722 ega_blinds(dst, src)` — slot 10

```c
int ega_copy_page(FarPtr src, FarPtr dst)
{
    out(0x3CE, 5); out(0x3CF, 1); out(0x3C4, 2); out(0x3C5, 0x0F);
    vmovedata(src, dst, 0x1F40);
    out(0x3CE, 5); out(0x3CF, 0);
    return 0;
}

int ega_blinds(FarPtr dst, FarPtr src)
{
    out(0x3C4, 2); out(0x3C5, 0x0F); out(0x3CE, 8); out(0x3CF, 0xFF);
    for (int pass = 0; pass < 5; pass++) {
        u32 until = DS_05F8 + 3;                              /* 32-bit tick counter */
        u16 o = DS_5C0C[pass];                                /* 0, 50h, A0h, 28h, 78h */
        for (int i = 0; i < 0x28; i++, o += 0xC8) {           /* every 5th row */
            out(0x3CE, 5); out(0x3CF, 2);                     /* write mode 2: clear row to colour 0 */
            for (int b = 0; b < 0x28; b++) vwr(dst.seg, dst.off + o + b, 0);
            out(0x3CE, 5); out(0x3CF, 1);                     /* then latched copy of the row */
            vmovedata(src.seg, src.off + o, dst.seg, dst.off + o, 0x28);
        }
        while ((i32)DS_05F8 < (i32)until) ;                   /* hi signed, lo unsigned: ticks >= until */
    }
    out(0x3CE, 8); out(0x3CF, 0xFF); out(0x3CE, 5); out(0x3CF, 0);
    return 0;
}
```

The clear-then-copy leaves the same bytes; the visible effect is the order of the rows (0,5,10…,
then 2,7,…, 4,…, 1,…, 3,…) and the ≥3-tick pause after each pass. The port must present a frame
after each pass (the host timer gives the pause).

#### `21a0:1e2d ega_save_rect(src, w, h, dst, sstride)`

```c
void ega_save_rect(FarPtr s, int w, int h, FarPtr d, int sstride)
{
    if (h < 1) return;
    out(0x3CE, 5); out(0x3CF, 1);
    for (int y = 0; y < h; y++, s.off += sstride)
        for (int x = 0; x < w; x++) vwr(d.seg, d.off++, vrd(s.seg, s.off + x));   /* packed */
    out(0x3CE, 5); out(0x3CF, 0);
}
```

### 4.5 Text: `21a0:113a ega_draw_text(d, x, y, s)` — slot 3

Font: segment `DS:6A2A` (= image segment `0e9f`), 9 bytes per character from 20h: byte 0 = advance
width in pixels (≤ 8), bytes 1–8 = rows, MSB = leftmost. Characters 20h–7Ah; anything else (after
`c - 20h` as a byte, > 5Ah) is drawn as the space.

```c
void ega_draw_text(Desc far *d, int x, int y, const char *s)
{
    int xs = x & 7;
    u8 lmask = 0xFF, rmask = 0xFF;
    u8 buf[8][40] = {0};                    /* [bp-0x14A], 320 bytes zeroed */
    int bit = xs, width = xs;               /* width in pixels from the byte boundary */
    u8 *bp = &buf[0][0];
    int w;
    while (*s) {
        u8 c = (u8)(*s++ - 0x20); if (c > 0x5A) c = 0;
        u8 far *g = MK_FP(DS_6A2A, c * 9);
        w = g[0];
        width += w;
        if (width > 0x140) { width -= w; break; }
        int nb = bit + w;
        if (nb <= 8) {
            for (int r = 0; r < 8; r++) bp[r * 40] |= g[1 + r] >> bit;
            bit = nb;
            if (bit == 8) { bp++; *bp = 0; bit = 0; }
        } else {
            bp[1] = 0;                                        /* first row only */
            for (int r = 0; r < 8; r++) {                     /* 16-bit ror, OR as a word */
                u16 v = ror16(g[1 + r], bit);
                bp[r * 40] |= (u8)v; bp[r * 40 + 1] |= v >> 8;
            }
            bit = nb & 7; bp++;
        }
    }
    if (x + width - xs > d->w) width = d->w - x;             /* (sic) xs dropped after clipping */
    int a0 = (xs + 7) & ~7, a1 = width & ~7;
    lmask >>= (u8)(xs - a0 + 8);            /* count 1..8; 8 gives 0 (xs == 0: no left byte) */
    rmask <<= (u8)(a1 - width + 8);
    int nmid = a0 < a1 ? (a1 - a0) >> 3 : 0;
    if (nmid == 0 && xs >= a1) { u8 m = lmask & rmask; if (m) { lmask = m; rmask = 0; } }
    int rows = 8; if (y + 8 > d->h) rows = d->h - y;
    DS_824C = x + width - xs; DS_824E = y;
    out(0x3CE, 5); out(0x3CF, 2);            /* write mode 2 */
    out(0x3C4, 2); out(0x3C5, 0x0F);
    out(0x3CE, 8); out(0x3CF, 0xFF);         /* GC index stays 8 from here on */
    u16 row = d->planes.off + y * d->stride + (x >> 3), seg = d->planes.seg;
    u8 *src = &buf[0][0];
    for (; rows; rows--, row += d->stride, src += 40) {      /* next buffer row */
        u16 o = row; u8 *q = src;
        if (lmask) { vrd(seg, o); out(0x3CF, lmask); vwr(seg, o, DS_8243);
                     vrd(seg, o); out(0x3CF, *q++); vwr(seg, o, DS_8242); o++; }
        for (int n = nmid; n; n--) { out(0x3CF, 0xFF); vwr(seg, o, DS_8243);
                     vrd(seg, o); out(0x3CF, *q++); vwr(seg, o, DS_8242); o++; }
        if (rmask) { vrd(seg, o); out(0x3CF, rmask); vwr(seg, o, DS_8243);
                     vrd(seg, o); out(0x3CF, *q++); vwr(seg, o, DS_8242); }
    }
    out(0x3CE, 8); out(0x3CF, 0xFF); out(0x3CE, 5); out(0x3CF, 0);
}
```

Each covered byte is first filled with the background colour under the edge mask, then the glyph
bits are written in the foreground colour. The glyph bits of the edge bytes are **not** ANDed with
the edge masks (bits beyond a clipped right edge are drawn). In the middle loop the background
write uses latches from the previous byte, harmless because the bit mask is FFh. `8242`/`8243`
are set by the UI (colour scheme, §4.9).

### 4.6 Recolour, line, spans

```c
void ega_recolour_rect(Desc far *d, int from, int to, int x0, int y0, int x1, int y1)  /* 21a0:1508 */
{
    u8 lm = 0xFF, rm = 0xFF;
    int a0 = (x0 + 7) & ~7, a1 = x1 & ~7;
    if (x1 > d->w) { a1 = d->w; x1 = d->w; }
    if (d->h < y1) y1 = d->h;
    lm >>= (u8)(x0 - a0 + 8);  rm <<= (u8)(a1 - x1 + 8);
    int nmid = a0 < a1 ? (a1 - a0) >> 3 : 0;
    if (nmid == 0 && x0 >= a1) { u8 m = lm & rm; if (m) { lm = m; rm = 0; } }
    out(0x3CE, 5); out(0x3CF, 0x0A);          /* write mode 2, read mode 1 (colour compare) */
    out(0x3CE, 2); out(0x3CF, from);          /* GC2 colour compare; GC7 = 0Fh (BIOS) */
    out(0x3C4, 2); out(0x3C5, 0x0F);
    out(0x3CE, 8); out(0x3CF, 0xFF);          /* GC index stays 8 */
    int rows = y1 - y0;                        /* [y0, y1) */
    u16 row = d->planes.off + d->stride * y0 + (x0 >> 3), seg = d->planes.seg;
    for (; rows; rows--, row += d->stride) {
        u16 o = row; u8 b;
        if (lm) { b = vrd(seg, o) & lm; if (b) { out(0x3CF, b); vwr(seg, o, to); } o++; }
        for (int n = nmid; n; n--, o++) { b = vrd(seg, o); if (b) { out(0x3CF, b); vwr(seg, o, to); } }
        if (rm) { b = vrd(seg, o) & rm; if (b) { out(0x3CF, b); vwr(seg, o, to); } }
    }
    out(0x3CE, 8); out(0x3CF, 0xFF); out(0x3CE, 5); out(0x3CF, 0);
}
```

(`rows` counts down with `!= 0`: y1 < y0 would loop 65536 times; callers never do it.)

```c
/* 21a0:252e (asm, near): in AX = y, BX = x; out ES = DS:787A, BX = y*28h + (x >> 3) (unsigned mul/shr),
   CL = (x & 7) ^ 7, AH = 1; DX preserved. */

void ega_line(int x0, int y0, int x1, int y1, int colour)   /* 21a0:23bc — slot 12 */
{
    outw(0x3CE, colour << 8 | 0);  outw(0x3CE, 0x0F01);  outw(0x3CE, (DS_5C1E & 0xFF00) | 3);
    int ystep = 0x28;
    int dx = x1 - x0;
    if (dx == 0) {                                    /* vertical */
        int n = y1 - y0, ys = y0; if (n < 0) { n = -n; ys = y1; } n++;
        addr(ys, x0) -> es:o, m = 1 << cl;
        outw(0x3CE, m << 8 | 8);
        for (; n; n--, o += ystep) { vrd(o); vwr(o, 8); }      /* "or es:[bx],al" */
        goto done;
    }
    if (dx < 0) { dx = -dx; swap(x0, x1); swap(y0, y1); }
    int dy = y1 - y0;
    if (dy == 0) {                                    /* horizontal: x0 < x1 */
        addr(y0, x0) -> o;  u8 lm = ~(~1 << cl);  u8 rm = 0xFF << ((x1 & 7) ^ 7);
        int n = (x1 >> 3) - (x0 >> 3);               /* unsigned shr */
        /* movsb es:[di] <- es:[si], si = di: read then write the same byte */
        if (!(lm & 0x80)) {
            if (n == 0) { rm &= lm; goto last; }
            outw(0x3CE, lm << 8 | 8); vrd(o); vwr(o, ·); o++; n--;
        }
        outw(0x3CE, 0xFF08); for (; n; n--, o++) { vrd(o); vwr(o, ·); }
    last:
        outw(0x3CE, rm << 8 | 8); vrd(o); vwr(o, ·);
        goto done;
    }
    if (dy < 0) { dy = -dy; ystep = -0x28; }
    bool ymajor = dy > dx;
    int maj = dx, min = dy; if (ymajor) { maj = dy; min = dx; }
    int d1 = 2 * min, err = 2 * min - maj, d2 = 2 * min - 2 * maj;
    addr(y0, x0) -> o;  u8 m = 1 << cl;  int n = maj + 1;
    if (!ymajor) {                                    /* 21a0:24bf: accumulate a byte of pixels */
        u8 acc;
    next: acc = m;
    more: acc |= m;
        bool wrap = m & 1; m = ror8(m, 1);
        if (!wrap) {
            if (err < 0) { err += d1; if (--n) goto more; outw(0x3CE, acc << 8 | 8); vrd(o); vwr(o, 8); goto done; }
            err += d2; outw(0x3CE, acc << 8 | 8); vrd(o); vwr(o, 8); o += ystep;
            if (--n) goto next; goto done;
        }
        outw(0x3CE, acc << 8 | 8); vrd(o); vwr(o, 8); o++;
        if (err < 0) err += d1; else { err += d2; o += ystep; }
        if (--n) goto next; goto done;
    } else {                                          /* 21a0:24fe */
        do {
            outw(0x3CE, m << 8 | 8); vrd(o); vwr(o, 8); o += ystep;
            if (err >= 0) { err += d2; bool c = m & 1; m = ror8(m, 1); o += c; }
            else err += d1;
        } while (--n);
    }
done:
    outw(0x3CE, 0x0000); outw(0x3CE, 0x0001); outw(0x3CE, 0x0003); outw(0x3CE, 0xFF08);
}
```

Coordinates are not clipped; `o` is a 16-bit offset in `DS:787A` (no page offset added: the page is
selected by the segment, A000/A200/A400). The written value is irrelevant (set/reset on all planes).
In the x-major loop, pixels of one byte on one row are merged into one bit-mask write.

```c
void ega_setreset_on(void)  { if (DS_8236 == -2) { outw(0x3CE, 0x0F01); outw(0x3CE, (DS_5C1E & 0xFF00) | 3); } } /* 2645:0008 */
void ega_setreset_off(void) { if (DS_8236 == -2) { outw(0x3CE, 0); outw(0x3CE, 1); outw(0x3CE, 3); outw(0x3CE, 0xFF08); } } /* 2645:001d */

void ega_hspan(int x0, int x1, int y, int colour)         /* 2645:0034 — slot 13 */
{
    if (DS_5E50 > y || DS_5E52 < y) return;                /* signed */
    if (DS_5E4E > x1 || DS_5E54 < x0) return;
    if (x0 <= DS_5E4E) x0 = DS_5E4E;
    if (x1 >= DS_5E54) x1 = DS_5E54;
    if (x0 > x1) swap(x0, x1);
    outw(0x3CE, colour << 8 | 0);                          /* GC0 only: GC1/GC3 from ega_setreset_on */
    /* then exactly the horizontal case of ega_line (2645:014b = 21a0:252e), x0..x1 inclusive */
}

void ega_fill_rows(int y, int colour)                      /* 2645:00e7 — slot 14 */
{
    if (DS_5E52 < y) return;
    outw(0x3CE, colour << 8); outw(0x3CE, 0x0F01); outw(0x3CE, (DS_5C1E & 0xFF00) | 3);
    u16 n = (u16)(DS_5E52 - y) * 0x28, o = (u16)y * 0x28;  /* rows y .. 5E52-1 */
    outw(0x3CE, 0xFF08);
    for (; n; n--, o++) { vrd(DS_787A, o); vwr(DS_787A, o, ·); }
    outw(0x3CE, 0); outw(0x3CE, 1); outw(0x3CE, 3); outw(0x3CE, 0xFF08);
}
```

`ega_hspan` leaves GC0 = colour; without a preceding `ega_setreset_on` (enable set/reset = 0) the
span would write the latches back unchanged (its callers in `2645` always bracket it).

### 4.7 Cursor, animation, slide

```c
void ega_draw_cursor(FarPtr img, FarPtr mask, FarPtr save, FarPtr scr,
                     int xb, int y, int wb, int rows)      /* 0e6c:0175 — slot 4 */
{
    u16 o = scr.off + (u8)0x28 * (u8)y + xb;              /* 8-bit mul */
    u16 s = img.off, m = mask.off, v = save.off;          /* mask read in img's segment */
    out(0x3CE, 5); out(0x3CF, 0);                          /* write mode 0; GC index then 8 */
    out(0x3CE, 8);
    for (;;) {
        for (int n = wb; n; n--) {
            out(0x3CF, 0);                                 /* bit mask 0 */
            out(0x3C5, 0x0F);                              /* SEQ data: index 2 assumed (never written) */
            vwr(save.seg, v++, vrd(scr.seg, o));           /* save is VRAM (A000:1F40): with bit mask 0
                                                              the write stores the 4 latched planes */
            out(0x3CF, rd8(img.seg, m++));                 /* bit mask = cursor mask */
            out(0x3C5, 1); vwr(scr.seg, o, rd8(img.seg, s));
            out(0x3C5, 2); vwr(scr.seg, o, rd8(img.seg, s + 0x600));
            out(0x3C5, 4); vwr(scr.seg, o, rd8(img.seg, s + 0xC00));
            out(0x3C5, 8); vwr(scr.seg, o, rd8(img.seg, s + 0x1200)); o++;
            s++;
        }
        if (--rows == 0) break;
        s += 0x18 - wb; m += 0x18 - wb; v += 3 - wb; o += 0x28 - wb;
    }
    out(0x3C5, 0x0F); out(0x3CF, 0xFF);
}
```

Cursor image: 4 planes 600h apart, 24 bytes per row (8 pre-shifted copies of 3 bytes side by
side, built by `0f38:2b28`); mask 1 = cursor pixel (inverted on EGA by `2b28`). The save-under is a
full 4-plane copy in VRAM at `A000:1F40` (3 bytes per row); `0f38:2e8b` restores it with the latched
copy `24e7:008b` (§4.10).

`21a0:25d9 ega_anim_step(Anim *a)` — slot 9 (structure from the code; confidence likely):

```c
typedef struct PageNode {        /* ring of pages */
    Desc far *page;              /* +0 */
    i16 rect[4];                 /* +4 last drawn rect (x0,y0,x1,y1) */
    i16 show;                    /* +C page number for ega_show_page */
    struct PageNode *next;       /* +E */
} PageNode;
typedef struct {
    Desc far **sprite;           /* +00 near ptr to the far ptr of the current frame desc */
    Desc far *bgsrc;             /* +02 background to restore dirty rects from */
    Desc far *bg;                /* +06 background composited under the sprite / overlay source */
    Rect4 *overlay[2];           /* +0A, +0C optional overlay rects redrawn with blit_masked */
    i16 x, y;                    /* +0E, +10 */
    i8 *frame;                   /* +12 frame info: [0] < 0 = finished, [2] = mode */
    PageNode *node;              /* +14 */
} Anim;

int ega_anim_step(Anim *a)
{
    if (*(i16 *)a->frame < 0) {                  /* end: copy the last page into the next one */
        PageNode *n = a->node;
        ega_latch_copy(n->page->planes, n->next->page->planes, 0x28, 0xBE, 0, 0, 0x28);
        if (n->show) { n->next->rect = n->next->next->rect; ega_show_page(0); }
        return 0;
    }
    PageNode *n = a->node = a->node->next;
    Desc far *sp = *a->sprite;
    i16 box[4], dirty[4];
    rect_of(sp->w, sp->h - DS_02D0, a->x, a->y, n->page->w, n->page->h, box);      /* 0f38:a1ee */
    if (rect_union_clip(box, n->rect, dirty, a->frame[2]))                         /* 0f38:a4b9 */
        anim_restore_rect(dirty, n->page, a->bgsrc, a->frame[2]);                  /* 21a0:2568 */
    n->rect = box;
    Tail t = { .rows = … };  /* built on the stack: {w = sp->w, 0, x, y …}; see race/garage for the 6-copy mode */
    if (DS_0284 == 6)
        for (int i = 0; i < 6; i++) { if (tile_rect(DS_539A[i*2], DS_539C[i*2], a, &r)) /* 0f38:aac2 */
                                          ega_sprite_over_bg(sp, n->page, a->bg, sp_tail, &r); }
    else ega_sprite_over_bg(sp, n->page, a->bg, sp_tail, &r /* {sp->w, sp->h - DS_02D0, 0, 0, x, y} */);
    for (int k = 0; k < 2; k++) if (a->overlay[k]) {
        i16 o[4] = *a->overlay[k];
        if (sp->stride == a->bg->stride) { o[0] += a->x; o[2] += a->x; } else { o[1] += a->y; o[3] += a->y; }
        if (rect_intersect(o, n->rect, dirty)) {                                       /* 0f38:a0fa */
            Rect r = { dirty[2]-dirty[0]+1, dirty[3]-dirty[1]+1,
                       sp->stride == a->bg->stride ? 0 : dirty[0],
                       sp->stride == a->bg->stride ? dirty[1] : dirty[1] - a->y,
                       dirty[0], dirty[1] };
            ega_blit_masked(a->bg, n->page, &r, 0);
        }
    }
    anim_advance(a);                                                               /* 0f38:aa37 */
    ega_show_page(n->show);
    return 1;
}

void anim_restore_rect(i16 *r, Desc far *page, Desc far *bgsrc, int mode)     /* 21a0:2568 */
{
    u16 back = mode == 0 ? r[0] >> 3 : r[1] * 0x28;     /* background covers a strip: origin shift */
    ega_latch_copy((FarPtr){ bgsrc->planes.seg, bgsrc->planes.off - back }, page->planes,
                   (r[2] >> 3) - (r[0] >> 3) + 1, r[3] - r[1] + 1, r[0] >> 3, r[1], page->stride);
}
```

The helpers `0f38:a0fa/a1ee/a4b9/aa37/aac2` are rectangle / animation bookkeeping (§4.9). Callers
of slot 9: `0f38:8e48`, `0f38:99d9`, `0f38:a87e`, `0f38:b82c`, `0f38:be93` (garage / Bob's screens,
see those specs).

```c
void ega_slide_sprite(int top, int bottom, int x, int from, int to, Desc far *sp)  /* 21a0:28ec — slot 11 */
{
    int xb = x / 8;                                   /* idiv */
    int w = sp->stride, h = sp->h, pstep = w * h;
    Desc far *B = DS_822E;
    int bstr = B->stride;
    FarPtr save = { B->planes.seg, B->planes.off + 0x2AF0 };     /* A000:4AF0 in page 2 */
    FarPtr win  = { B->planes.seg, top * bstr + B->planes.off + xb };
    int d = to - from, step, rowstep;
    if (d < 0) { if (d & 1) { d--; from++; } rowstep = -2 * bstr; step = -2; d = -d; }
    else       { if (d & 1) { d++; from--; } rowstep =  2 * bstr; step =  2; }
    int frames = (d >> 1) + 1;
    FarPtr cur = { B->planes.seg, from * bstr + B->planes.off + xb };
    make_mask_plane(sp->planes, sp->mask, sp->size);
    int wrows = bottom - top + 1;
    ega_save_rect(win, w, wrows, save, bstr);
    for (int y = from; frames; frames--, y += step, cur.off += rowstep) {
        ega_latch_copy_to40(save, w, wrows, win, w);             /* restore the window */
        FarPtr dp; u16 so, mo; int rows;
        if (y < top) { int skip = (top - y) * w; rows = h - top + y;
                       so = sp->planes.off + skip; mo = sp->mask.off + skip; dp = win; }
        else { dp = cur; so = sp->planes.off; mo = sp->mask.off;
               rows = (y + h - 1 > bottom) ? bottom - y + 1 : h; }
        for (int k = 0; k < 4; k++, so += pstep) {
            out(0x3C4, 2); out(0x3C5, 1 << k); out(0x3CE, 4); out(0x3CF, k);
            rows_mask_or(dp, sp->planes.seg, so, mo, (i8)w, (i8)w, (i8)rows, (i8)bstr);
        }
        out(0x3C4, 2); out(0x3C5, 0x0F);
        if (DS_8240) {
            if (DS_0792) ega_blit_masked(DS_0792, DS_822E, (Rect *)&DS_70AC, 1);   /* via [78AA] */
            ega_wait_vretrace();
            ega_latch_copy(B->planes, DS_822A->planes, w, wrows, xb, top, bstr);   /* back → front */
            ega_wait_vretrace();
        }
    }
}
```

Byte arguments: `w`, `rows`, `bstr` are passed through `cbw` (values < 80h in practice). With
`DS:8240 == 0` (single buffer) the frames are drawn straight on the visible page without waits.
Callers: `0f38:3074`, `31a5`, `32ce`, `3697`, `37fc`, `3a58` (garage, see garage).

### 4.8 Set-up: `0f38:1859 gfx_init` and `0f38:0dbd gfx_screen_mode`

```c
void gfx_init(Desc far *a, Desc far *b)            /* main: gfx_init(2e3e:0000, 2e3e:0030) */
{
    DS_822A = a; DS_822E = b; DS_8234 = DS_8232 = 0; DS_8240 = 1;
    DS_8236 = (i8)a->type;
    if (DS_8236 == -6) { DS_8238 = 1; DS_823A = 1; DS_823C = 4; DS_823E = 0xFFFE; DS_787E = 0x3A0; }
    else if (DS_8236 == -2) {
        DS_8238 = 4; DS_823A = 3; DS_823C = 1; DS_823E = 0xFFF8; DS_787E = 0x3A0;
        out(0x3D4, 0x0C); out(0x3D5, 0); out(0x3D4, 0x0D); out(0x3D5, 0);
    } else { DS_8238 = 1; DS_823A = 2; DS_823C = 2; DS_823E = 0xFFFC; DS_787E = 0x3F0; a->type = 0xFD; }
    memcpy(&DS_8242, (u8 *)DS_787E + 5, 5);         /* 0D 09 09 0F 01 */
    DS_8268 = DS_8266 = -1;
    for (int i = 1; i <= 5; i++) { DS_7644[-0x16 * i] = -1; DS_7646[-0x16 * i] = -1; }  /* words at 7618,75EC,… (UI cache, §4.9) */
    call [DS_78C2]();                                /* slot 8 = 0f38:0b3f */
    f_0f38_4bff();
    DS_8247 = 1;
    DS_7316 = DS_822A; DS_731A = DS_822E; DS_731E = MK_FP(0x2E3E, 0x60);
    if (DS_8236 == -2) {
        *DS_731E = *DS_822E;  DS_731E->planes = MK_FP(0xA000, 0x4000);    /* 18h words copied */
        DS_7322 = MK_FP(0x2E68, 0); *DS_7322 = *DS_822E; DS_7322->planes = MK_FP(0xA000, 0x6000);
    }
}

void gfx_screen_mode(int n)                         /* 0f38:0dbd */
{
    switch (n) {
    case 0: if (!DS_02EA) { DS_02EA = DS_822A; DS_02E6 = DS_822E; } DS_004C = 1; break;
    case 1: case 4:
        DS_822A = DS_02EA; DS_822E = DS_02E6;
        if (DS_8236 == -2) { out(0x3D4, 0x13); out(0x3D5, 0x14); ega_set_split(-1);
                             out(0x3D4, 0x0C); out(0x3D5, 0); out(0x3D4, 0x0D); out(0x3D5, 0); }
        DS_8240 = 1;
        if (n == 1) f_0f38_2022(0xBE); else call [DS_78C2]();
        break;
    case 2: if (DS_8236 == -2) { CRTC 0Ch = 0; CRTC 0Dh = 0; } DS_8240 = 1; break;
    case 3: DS_822A = DS_822E = DS_02EA; DS_8240 = 0;
            f_0f38_2022(DS_8236 == -2 ? 0x5A : 0xBE);
            if (DS_8ACA) { f_0f38_2213(DS_49DE); f_0f38_2022(…); }   /* jumps back into case 1's tail */
            break;
    }
}
```

Case 3 is the single-buffered mode (front = back = page 0; used by the race, `0f38:7b22`). Callers:
`0000:066f`, `0000:39c0`, `0f38:7b22`, `0f38:8e48`.

### 4.9 Primitives in segment 0f38

All far. "page B" = `DS:822E` (back, drawn into), "page A" = `DS:822A` (visible). `HIDE` =
`cursor_ctl(-3)`, `SHOW` = `cursor_ctl(-1)`. `MIRROR(x0,y0,x1,y1)` =
`if (DS_8240) page_copy_rect(pageB, pageA, x0, y0, x1, y1)`. Colours: `8242` text fg, `8243` text
bg, `8244` fill, `8245` line/frame, `8246` frame style (0 none, 1 single, 2 double, 3 solid).

| Address | Proposed name | Signature | Purpose | Class | Conf. |
|---|---|---|---|---|---|
| `0f38:17c8` | `font_string_width` | `int(char near *s)` | sum of glyph widths, font `DS:691A` (`imul byte`: signed char) | video | verified |
| `0f38:1f4b` | `ega_set_palreg` | `void(int idx, int v)` | wait until 3DAh bit3, INT 10h AX=1000h/1001h (§4.10) | video | verified |
| `0f38:1fa4` | `ega_set_palette` | `void(u8 near *p16)` | INT 10h AX=1002h, 16 values + overscan `DS:8248` | video | verified |
| `0f38:0b2d`, `0b3f` | `pal_black`, `pal_normal` | slots 7, 8 | see §1.2 | video | verified |
| `0f38:17fc`, `180c` | `pal_set12_5`, `pal_set12_13` | `void(void)` | `ega_set_palreg(0x0C, 5)` / `(0x0C, 0x0D)` | video | verified |
| `0f38:181c` | `pal_shadow_patch` | `void(void)` | `DS:0446..0448 = 09 01 03` (shadow only) | video | verified |
| `0f38:182c` | `pal_shadow_swap` | `void(void)` | swap 17 bytes `DS:0440` ↔ `DS:0462` | video | verified |
| `0f38:4302` | `fill_rect` | `void(Desc far *d, int x, int y, int w, int h)` | h lines through slot 12 in colour `8244`, clipped to d | video | verified |
| `0f38:439c` | `vline` | `void(Desc far *d, int x, int y, int h)` | slot 12, colour `8245` | video | verified |
| `0f38:43ff` | `hline` | `void(Desc far *d, int x, int y, int w)` | `fill_rect(…, 1)` in colour `8245` | video | verified |
| `0f38:4432` | `draw_frame3` | `void(Desc far *d, int x, int y, int w, int h)` | 3-px frame outside the rect, style `8246` | video | verified |
| `0f38:45d7` | `draw_box1` | same | 1-px outline outside the rect, colour `8245` | video | verified |
| `0f38:b5aa` | `page_copy_rect` | `void(Desc far *s, Desc far *d, int x0, int y0, int x1, int y1)` | inclusive rect copy (byte aligned) via slot 0 flags 1 | video | verified |
| `0f38:b31f` | `bitmap_blit_at` | `void(Desc far *s, Desc far *d, int dx, int dy, int flags)` | whole bitmap via slot 0 | video | likely |
| `0f38:b51e` | `bitmap_extract` | `Desc far *(Desc far *s, i16 near r[4], int pool)` | new bitmap (+mask if s has one) from a sub-rect, slot 0 flags 3 | video | likely |
| `0f38:ab50` | `bitmap_copy_into` | `void(Desc far *s, Desc far *d, int flags)` | init d like s (`0000:376b`), full slot-0 copy | video | likely |
| `0f38:9fce` | `arena_bitmap_alloc` | `Desc far *(int w, int h, int flags, int pool)` | header + planes (+ mask) from the arena | video/memory | verified |
| `0f38:683e` | `pic_get` | `Desc far *(int id, int pool)` | `67ff` size → `9fce` → `65ae` decode (34 callers) | video/picture | verified |
| `0f38:67ff` | `pic_info` | `void(int id, int *w, int *h, int *flags)` | from the picture directory `[DS:691C]` (0x30 per entry) | picture | verified |
| `0f38:acbf` | `bitmap_make_shifted` | `Desc far *(Desc far *s, int shift, Desc far *d)` | copy shifted right by shift&7 px (width +8 if needed), mask padded with 1 | video | likely |
| `0f38:af50` | `bitmap_blit_masked_shift` | `void(Desc far *s, Desc far *d, i16 near *r, int and_mask)` | pre-shift if `r.sx & 7`, slot 2; optionally AND src mask into d's mask | video | likely |
| `0f38:b0f2` | `bitmap_composite_behind` | `void(Desc far *s, Desc far *d, int x, int y)` | per plane `d \|= ~smask & s & dmask`, then `dmask &= smask` unless `DS:5AA6` | video | likely |
| `0f38:b3b4` | `picture_draw_masked` | `void(Desc far *d, int id, u8 x, u8 y, int flag)` | temp `pic_get` + `af50` + free | video | likely |
| `0f38:abc2` | `bitmap_opaque_extent` | `void(Desc far *b, int *left, int *right)` | first/last opaque column in the mask | sprite | likely |
| `0f38:a0fa` | `rect_intersect` | `i16 *(i16 *a, i16 *b, i16 *out)` | {x0,y0,x1,y1}; out x snapped with `DS:823E`; NULL if empty | geometry | verified |
| `0f38:a198` | `rect_clip_to_view` | `i16 *(int x, int y, int w, int h, i16 *out)` | intersect with the view `DS:825A`/`825E`/`8260`/`8262` | geometry | verified |
| `0f38:a1ee` | `rect_clip_make` | `void(int w, int h, int x, int y, int maxw, int maxh, i16 *out)` | build an aligned, clipped {x0,y0,x1,y1} | geometry | verified |
| `0f38:a4b9` | `rect_subtract_edge` | `i16 *(i16 *a, i16 *b, i16 *out, int horiz)` | part of b not covered by a (dirty strip) | geometry | likely |
| `0f38:aa37`, `aac2`, `a273` | `sprite_anim_advance`, `sprite_hspan_clip`, `sprite_scene_setup` | | bookkeeping of the `21a0:25d9` animations (see garage) | sprite | likely |
| `0f38:231a` | `screen_fill_rect` | `void(int x, int y, int w, int h, u8 c)` | HIDE, `fill_rect(pageB)` in c, MIRROR, SHOW | video | verified |
| `0f38:2656` | `screen_text` | `void(int x, int y, char near *s)` | slot 3 on page B, MIRROR(x, y, `824C`-1, y+7) | video | verified |
| `0f38:26a4` | `screen_hline` | `void(int x, int y, int w)` | `hline(pageB)`, MIRROR | video | verified |
| `0f38:26f2` | `screen_save_rect` | `Desc far *(int x, int y, int w, int h, Desc far *dst)` | copy a page-B rect (byte aligned) into a bitmap (pool 2 if dst null) | video | verified |
| `0f38:27ef`, `2853` | `screen_put_bitmap`, `screen_put_bitmap_mirror` | `void(Desc far *b, int x, int y)` | whole bitmap to page B at (x & `823E`, y) [+ MIRROR] | video | verified |
| `0f38:2d7e` | `cursor_ctl` | `void(int op)` | ≥0 shape, -1/-2 show (-2 also to page A), -3/-4 hide; nesting counter `DS:4733` (56 callers) | video | verified |
| `0f38:2e8b` | `cursor_redraw` | `void(int x, int y)` | restore save-under, redraw through slot 4, mirror | video | verified |
| `0f38:2b28` | `cursor_init` | `void(void)` | pre-shifted pointer bank from picture 2, save-under desc at A000:1F40, `DS:8247 = 2` | video | verified |
| `0f38:2b1c` | `cursor_shape_offset` | `u16(int n)` | `n * DS:7B06` | video | verified |
| `0f38:8126` | `ega_blit_4planes` | near | per plane SEQ2 = 1<<p, GC4 = p, `2595:0004` | garage (EGA ports) | likely |
| `0f38:819a` | `ega_vram_cache_strips` | near | caches car strips in VRAM page `DS:731E` from row 46h (`1e2d`, write mode 2 fill, `8126`) | garage | guess |
| `0f38:2022` | `status_init` | `void(int y)` | status line at row y (scheme 11), clears 320×10 | ui | verified |
| `0f38:20a1` | `status_print` | `void(int x, char near *s, int w)` | status-line message (x<0 centred) | ui | verified |
| `0f38:2213` | `status_label` | `void(char near *s)` | label at x = 0, then the cash | ui | verified |
| `0f38:21c0` | `money_add` | `void(int d)` | `DS:7EAA` (long) += d, redisplay | ui/game | verified |
| `0f38:22e9` | `status_gear_char` | `void(int i)` | gear letter (race) | race | verified |
| `0f38:2554`, `2638` | `show_picture`, `show_picture_at` | | full picture screen (optional clear), mirror, free | ui | verified |
| `0f38:28a4`, `2a62`, `2adf` | `msgbox_show`, `msgbox_show_at`, `msgbox_hide` | | picture message boxes `DS:04C4` | ui | verified |
| `0f38:239c`, `23ce` | `dialog_msg`, `fatal_message` | | text boxes ("Hit Return when ready ...") | ui | verified |
| `0f38:465e`, `4a66`, `4ba1`, `4bff`, `4265`, `500e` | `menu_*` | | menus (items `DS:09F4`, 0x12 bytes) | ui | verified |
| `0f38:5910`, `5d37` | `text_input`, `prompt_number` | | edit field with a blinking `vline` caret at `824C`/`824E` | ui | verified |
| `0f38:1abd`, `1dc9`, `1e41`, `1e72`, `1e93` | `anim_run`, `anim_start`, `anim_tick`, `anim_active`, `anim_blink_led` | | animation-script interpreter (5 slots `DS:7568`) | ui | verified |
| `0f38:9f2c`, `a054`, `a07e`, `a09e`, `a0b8` | `arena_alloc`, `arena_reset`, `arena_reset_stacks`, `arena_pop_low`, `arena_pop` | | two-ended 64 KB arena (`a0b8` = free temporaries, 36 callers, not drawing) | platform (memory) | verified |
| `0f38:b502` | `far_memcpy` | | → `1e16:1dc8` | runtime | verified |
| `0f38:0b51`, `0bf0`, `0d62` | `wait_click_or_key`, `title_sequence`, `summer_over_screen` | | see platform / game_flow | – | |
| `0f38:01d2`, `0349`, `0425`, `05f2`, `0f53`, `1094`, `1130`, `12f7`, `14f2`, `000a`, `0098`, `0137`, `02ae` | `dash_*` | | race dashboard (needles through slot 12 into `DS:787A`, clock, gear knob, shift pattern) | race | |
| `0f38:8e48`, `990f`, `b632`, `b82c`, `be93`, `a87e` | | | garage / driving scenes using slots 0/2/5/9 | garage / race | |

```c
int font_string_width(const char *s)                        /* 0f38:17c8 */
{
    int w = 0;
    for (; *s; s++) w += rd8(DS_691A, (i8)*s * 9 - 0x120);  /* byte 0 of the glyph */
    return w;
}

void fill_rect(Desc far *d, int x, int y, int w, int h)    /* 0f38:4302 */
{
    if (x + w > d->w) w = d->w - x;
    if (y + h > d->h) h = d->h - y;
    u16 sseg = DS_787A, soff = DS_767C;
    DS_787A = d->planes.seg; DS_767C = d->planes.off;       /* line target = the descriptor's memory */
    int c = (i8)DS_8244, x1 = x + w - 1;
    for (int i = 0; i < h; i++) call [DS_78D2](x, y + i, x1, y + i, c);   /* ega_line: horizontal */
    DS_787A = sseg; DS_767C = soff;
}
void vline(Desc far *d, int x, int y, int h)                /* 0f38:439c: same 787A/767C swap */
{   if (y + h > d->h) h = d->h - y;  call [DS_78D2](x, y, x, y + h - 1, (i8)DS_8245); }
void hline(Desc far *d, int x, int y, int w)                /* 0f38:43ff */
{   u8 s = DS_8244; DS_8244 = DS_8245; fill_rect(d, x, y, w, 1); DS_8244 = s; }
void draw_box1(Desc far *d, int x, int y, int w, int h)     /* 0f38:45d7 */
{   vline(d, x-1, y-1, h+2); hline(d, x, y+h, w); vline(d, x+w, y-1, h+2); hline(d, x, y-1, w); }
void draw_frame3(Desc far *d, int x, int y, int w, int h)   /* 0f38:4432 */
{
    u8 sv = DS_8244; if (DS_8246 == 3) DS_8244 = DS_8245;
    fill_rect(d, x-3, y-3, 3, h+6); fill_rect(d, x, y+h, w, 3);
    fill_rect(d, x+w, y-3, 3, h+6); fill_rect(d, x, y-3, w, 3);
    if (DS_8246 == 3) { DS_8244 = sv; return; }
    vline(d, x-2, y-2, h+4); hline(d, x-2, y+h+1, w+4); vline(d, x+w+1, y-2, h+4); hline(d, x-2, y-2, w+4);
    if (DS_8246 == 2) { vline(d, x-1, y-1, h+2); hline(d, x-1, y+h, w+2);
                        vline(d, x+w, y-1, h+2); hline(d, x-1, y-1, w+2); }
}
```

The line driver draws into `DS:787A` at offset `y*28h + x/8` and ignores `DS:767C`: on EGA the
page is selected by the descriptor's **segment** only. Pages 0 and 1 are `A000:0000` and
`A200:0000`, so `fill_rect`/`vline`/`hline` work on them; the page-2/3 descriptors built by
`gfx_init` are `A000:4000` / `A000:6000` (offset form), so a line primitive given one of those
would draw into page 0 (blits use the full far pointer and are fine). Keep the model faithful
(`vrd`/`vwr` on seg:off with the 767C offset dropped) and the behaviour follows.

```c
void page_copy_rect(Desc far *s, Desc far *d, int x0, int y0, int x1, int y1)   /* 0f38:b5aa */
{
    if (x0 < 0) x0 = 0;  if (x1 < 0) x1 = 0;  if (x1 > 0x13F) x1 = 0x13F;  if (y1 > 0xC7) y1 = 0xC7;
    u16 m = DS_823E;                                    /* FFF8h */
    Rect r = { ((x1 + ~m + 1) & m) - (x0 & m), y1 - y0 + 1, x0, y0, x0, y0 };   /* y0 not clamped */
    call [DS_78A2](s, d, &r, 1);
}

Desc far *arena_bitmap_alloc(int w, int h, int flags, int pool)  /* 0f38:9fce */
{
    int bpr = ((w - 1) >> DS_823A) + 1, plane = bpr * h;
    int size = 0x30 + (flags ? plane : 0) + ((flags & 2) ? 0 : plane * DS_8238);
    Desc far *d = arena_alloc(size, pool);
    d->planes = (u8 far *)d + 0x30;
    bitmap_alloc(w, h, flags, -1, d);                     /* 0000:376b fills +0,+2,+4,+28,+2C,+2E */
    return d;
}

void screen_fill_rect(int x, int y, int w, int h, u8 c)   /* 0f38:231a */
{ u8 s = DS_8244; HIDE; DS_8244 = c; fill_rect(DS_822E, x, y, w, h); MIRROR(x, y, x+w-1, y+h-1); DS_8244 = s; SHOW; }
void screen_text(int x, int y, const char *s)               /* 0f38:2656 (no HIDE) */
{ call [DS_78AE](DS_822E, x, y, s); MIRROR(x, y, DS_824C - 1, y + 7); }
```

The status line, message boxes, menus and the animation interpreter are **ui** (listed above for
the other specs; pseudocode belongs to game_flow / garage). The only register-level code outside
the driver is `0f38:62ba`/`65ae` (platform, §5.4), `0f38:8126`:

```c
for (p = 0, bit = 1; bit < 9; bit <<= 1, p++) {           /* 0f38:8126 */
    out(0x3C4, 2); out(0x3C5, bit); out(0x3CE, 4); out(0x3CF, p);
    rows_mask_or(dst, src_seg, src_off, mask_off, stride_a, stride_b, rows, stride_a);   /* 2595:0004 */
    src_off += plane_size;
}
out(0x3C4, 2); out(0x3C5, 0x0F);
```

and `0f38:819a` (write mode 2 fill: `out 3CEh,5 / 3CFh,2`, write the colour byte, back to mode 0;
strip copies with `ega_save_rect`), owned by garage.

### 4.10 Palette and mouse pointer

```c
void ega_set_palreg(int idx, int v)                            /* 0f38:1f4b */
{
    if ((i8)v > 7) v |= 0x10;
    DS_0440[idx] = v;                                       /* shadow (bx = idx, no bound check) */
    union REGS r; r.h.ah = 0x10; r.h.al = idx > 0x0F; r.h.bl = idx; r.h.bh = v;
    while (!(in(0x3DA) & 8)) ;                              /* until vertical retrace (no edge wait) */
    int86x(0x10, &r, &r, &sr);                             /* AL=0 palette register, AL=1 overscan */
}

void ega_set_palette(const u8 *p)                                  /* 0f38:1fa4: no wait, no shadow update */
{
    u8 t[17];
    for (int i = 0; i < 16; i++) t[i] = (i8)p[i] > 7 ? p[i] | 0x10 : p[i];
    t[16] = DS_8248;                                        /* overscan (border) */
    r.h.ah = 0x10; r.h.al = 2; r.x.dx = FP_OFF(t); sr.es = SS; int86x(0x10, &r, &r, &sr);
}
```

Palette tables: `DS:0440` default `00 00 08 07 0F 07 09 0B 03 02 04 0C 05 0A 06 0E` (+ `00`),
`DS:0452` black, `DS:0462` alternate `09 01 08 00 0F 07 0B 03 0E 02 04 0C 0D 0A 06 0E`, `DS:04AC`
= default (fatal box), `DS:02D4` intro `00 08 07 0F 0E 01 09 0B 00 02 04 0C 0D 0A 05 06`,
`DS:0692` (copy-protection screen `0000:2fc8`). Single-register effects: `0000:1417` (regs 6, 7, 8),
`0000:67cf` (6, 7), `0000:6f6b` (12 = 5 / 13), `0000:8d9c` (12, 3), `0f38:17fc`/`180c` (12) — see
game_flow / garage / race for when. Port: `vga_pal[17]` (attribute registers + overscan);
displayed RGB of pixel c = `ega_palette[(vga_pal[c] & 7) | (vga_pal[c] & 0x10 ? 8 : 0)]` (200-line
interpretation: bits 3 and 5 ignored); after mode set `vga_pal[k] = k<8 ? k : k|0x10`, overscan 0.

Mouse pointer (`0f38:2d7e`, `0f38:2e8b`), faithful outline; pointer position `DS:472A/472C`
(platform, INT 33h), visible counter `DS:4733` (visible when > 0):

```c
void cursor_ctl(int op)                                  /* 0f38:2d7e */
{
    int x = clamp(DS_472A, 0, 0x13F), y = clamp(DS_472C, 0, 0xC7);
    if (op >= 0) { DS_7B08 = op; DS_4742 = DS_06EC[op*2]; DS_4744 = DS_06EE[op*2];   /* hotspot */
                   DS_7B04 = op * DS_7B06; cursor_redraw(x, y); return; }
    if (op >= -2) { if (++DS_4733 <= 0) return; DS_7B0A = op == -1 ? 2 : 1; }      /* show */
    else          { if (--DS_4733 < 0)  return; DS_7B0A = op == -3 ? -2 : -1; }    /* hide */
    if (DS_7B10 >= 0) cursor_redraw(DS_7B0C, DS_7B0E); else cursor_redraw(x, y);
    DS_7B0A = op >= -2 ? 1 : 0;
}

void cursor_redraw(int x, int y)                     /* 0f38:2e8b */
{
    if (!DS_7B0A) return;
    if (DS_7AF0 > 0) {                                     /* restore the save-under on page B */
        if (DS_8236 == -2)
            ega_latch_copy_to40(DS_7AEC->planes, DS_7AF0 >> 3, DS_7AF2,
                                DS_822E->planes + 0x28 * DS_7AFA + (DS_7AF8 >> 3), 3);
        else call [DS_78A2](DS_7AEC, DS_822E, (Rect *)&DS_7AF0, 1);
        DS_7AF0 = 0;
    } else { DS_7B0C = DS_7B0E = 0x3E8; DS_7B10 = DS_7B12 = -1; }
    if (DS_7B0A < 0) { if (DS_7B10 < 0 || DS_7B0A < -1) return; DS_7B0A = 1; goto mirror; }
    DS_7AF8 = x & DS_823E; DS_7AF0 = min(0x140 - DS_7AF8, 0x18); DS_7AF2 = min(0xC8 - y, 0x10); DS_7AFA = y;
    u16 off = (x & ~DS_823E) * (0x18 >> DS_823A) + DS_7B04;          /* pre-shifted copy */
    call [DS_78B2](bank_planes(DS_7AFC) + off, bank_mask(DS_7B00) + off, DS_7AEC->planes,
                   DS_822E->planes, DS_7AF8 >> 3, y, DS_7AF0 >> 3, DS_7AF2);   /* 0e6c:0175 */
mirror:
    if (DS_8240 && DS_7B0A == 1)
        page_copy_rect(DS_822E, DS_822A, min(DS_7B0C, x), min(DS_7B0E, y),
                       max(DS_7B10, x + 15), max(DS_7B12, y + 15));
    DS_7B0C = x; DS_7B0E = y; DS_7B10 = x + 15; DS_7B12 = y + 15;
}
```

The pointer is 24×16 (picture 2 of LIB2: 4 shapes 24×16), 8 pre-shifted copies (`0f38:2b28` via
`acbf`), hotspots `DS:06EC`: (1,1), (2,2), (1,1), (8,8); start position (9Eh, 80h).

## 5. File formats and in-exe tables

### 5.1 Bitmap descriptor (0x30 bytes)

See §4.2. Memory bitmaps are created by `0000:376b` / `0000:3880` / `0000:3904` (platform range):
header 30h bytes, then colour planes (plane after plane, `stride*h` each, `size` = all four),
then the mask (`stride*h`, 1 = transparent). `0f38:65ae` (platform) unpacks a LIB picture into
`+24` (or into `+28` when `+24` is null — the one-plane pictures 96, 98, 140, 226 of LIB2 are masks
/ shapes) and builds the mask with `make_mask_plane` when the picture is "masked".

### 5.2 Font (segment `0e9f`, `DS:6A2A` = `DS:691A`)

One font: 91 entries (20h–7Ah) × 9 bytes at image offset `0E9F0`: `u8 advance` (2–7 px, mostly 6;
space = 6), `u8 rows[8]` (MSB left). `DS:6A26`/`6A28` (`4049`, `0ED2`) and the other words of the
`DS:6A20` block are resource segments of other subsystems (platform).

### 5.3 Tables

| Where | Content |
|---|---|
| `DS:5B8A` / `DS:5BD0` | driver templates (§1.2) |
| `DS:5C0C` | blinds pass offsets `0 50 A0 28 78` |
| `DS:5C15` | left masks `00 80 C0 E0 F0 F8 FC FE FF`, `DS:5C1E` = `0000` |
| `DS:03A0`, `DS:03F0` | colour schemes (+5..+9 = text fg/bg and 3 UI colours) |
| `2e3e:0000`–`008F`, `2e68:0000` | static descriptors (§4.2) |

### 5.4 VRAM picture cache

`0f38:6887` (for `DS:8236 == -2`, unless the `nouemem` switch sets `DS:0280`) preloads the pictures
of the list `DS:4C28` with `0f38:62ba(pic, 1, 0, 0)`: `0000:3967` gives `A000:(7D00 + used[k])` in the
first plane k whose pool has room (`used[k] + size < 7D00h`), the directory entry records the plane
(`+1E`) and the far pointer (`+20`); the packed bytes are read into a RAM buffer and copied with SEQ2
= `1 << k` (write mode 0, default registers) into that plane, then SEQ2 = 0Fh. `0f38:65ae` sets GC4 =
the plane before unpacking straight from video memory and leaves GC4 there. Port: keep the cache in
the plane arrays (the reads go through `vga_rd` with the read map) so that corruption by page 3
(`6000`–`7F3F`) behaves as in the original.

## 6. Hardware / DOS dependencies

| Original | Where | SDL3 port |
|---|---|---|
| INT 10h AH=00h mode 0Dh | `0000:01d1`/`2bc0`/`2be2` (platform) | `ega_init()`: clear planes, reset `vga` registers (SEQ2 = 0Fh, GC7 = 0Fh, GC8 = FFh, others 0) |
| 3C4h/3C5h SEQ2 map mask | driver, `0f38:62ba/65ae/8126`, `0e6c:0175` | `vga.map_mask` (§4.1) |
| 3CEh/3CFh GC0/1/2/3/4/5/8 | driver, `2645`, `0f38:65ae/8126/819a` | `vga` fields |
| A000:xxxx reads / writes | all | `vrd`/`vwr` on `ega_plane(k)` |
| 3D4h/3D5h CRTC 0Ch/0Dh start | `21a0:182e`, `0f38:1859`, `0f38:0dbd` | `ega_set_start()` |
| CRTC 18h/07h/09h line compare, 13h offset | `21a0:0014`, `0f38:0dbd` | `ega_set_line_compare()` (§4.3); 13h ignored (always 14h) |
| 3DAh bit 3 / bit 0 polling | `21a0:0006`, `0014`, `182e`, `0f38:177d`, `1f4b` | `host_wait_vretrace()`: block until the next 70 Hz frame boundary, present the frame |
| 0040:0063 CRTC base | `21a0:0014` | constant 3D4h |
| INT 10h AX=1000h/1001h/1002h via `int86x` (`1e16:1ce8`) | `0f38:1f4b`, `0f38:1fa4` | `vga_pal[17]`; scan-out maps each pixel through it (§4.10); the overscan colour is not shown (no border in the port window) |
| tick counter `DS:05F8` | `21a0:1722` | platform timer (host tick) |

The port presents a frame at every vertical-retrace wait and at every tick wait (so the blinds and
the slide animation show their intermediate states); menus that never wait for retrace are
presented by the host loop at its own rate.

## 7. Timing

* Nothing in the driver is timer driven; it runs synchronously in the caller.
* Retrace-bound: `ega_show_page` (one retrace per flip → race / animation frame rate ≤ 70 Hz on
  VGA), `ega_set_split` (one retrace), `slide_sprite` (2 retraces per 2-row step when double
  buffered), `wait_vretraces(n)` (explicit delays in the game).
* Tick-bound: `ega_blinds` waits until `ticks ≥ start + 3` after each of 5 passes (≥ 15 ticks,
  ≈ 0.2 s at 72.8 Hz; the first pass's deadline is taken before its rows are copied).
* Speed-dependent: the cursor, blits and text are unthrottled; on the original they took real time
  (visible tearing); the port need not reproduce it.

## 8. Open questions

1. Page 3 (`A000:6000`, rows 186–199 overlap the picture cache `7D00`–`7F3F`): does any screen draw
   there while cached pictures are live (`0f38:be93` uses `DS:7322`)? The port keeps the overlap.
2. `ega_sprite_over_bg` background addressing (`sstr == bg->stride` → no x offset, else source row +
   destination column) looks like a bug or a deliberate strip layout; check with the callers
   (`21a0:25d9` only).
3. VGA colours of the attribute values: the port assumes the VGA BIOS loads the 200-line
   (CGA-compatible) DAC table for mode 0Dh, i.e. value bits 0-2 = RGB, bit 4 = intensity, bits 3/5
   ignored, brown at 6. Check against DOSBox (`machine=vga`) with the palette `DS:0440`.
4. VGA split for values other than `0x63`: `line + 100` is only right for 99; the game uses no other.
5. `DS:8232`/`8234`: probably a view offset used by the `223b` callers (garage); confirm there.
6. `0f38:1f4b` waits only for "in retrace" (no edge) and writes the shadow with an unchecked
   index; callers use 3..0x0C only.
7. `21a0:1ae9`: `Tail` field `+4` unused; the tail source pointer uses the unclipped `r->sx`/`r->dx`.

### 8.1 Driver slot users (for the other specs)

`function: slot×count` from `lcall [DS:78A2+4k]`:

* game_flow / platform (`0000:0fea` 15 callers: 8, `0000:09cb` 0×2 3×2, `0000:2fc8` 8 10, `0000:315b` 8,
  `0000:339f` 3×3 7×2 8, `0000:39c0` 7×2, `0000:570c` 8, `0000:6750` 3, `0000:6b06` 8).
* garage (`0000:7367` 0×2 2, `0000:7fb1` 0, `0f38:3074` 0 11, `31a5` 0 11, `32ce` 2 11×2, `3697` 1 11,
  `37fc` 1 11, `3a58` 2 11, `3c19` 0, `3d1c` 0, `3f2d` 0).
* race (`0000:8d26` 7, `0000:8e2d` 7, `0000:a35c` 0×2, `0000:a544` 0×8 2×3 5×2 8×2 10, `0000:ab8b` 7 8×2,
  `0000:b08c` 0×3 5 8, `0000:b8a1` 8, `0000:bf94` 0 2 3×2 5, `0000:c1d8` 0 2×3 5×2 8, `0000:c613` 8,
  `0f38:7b22` 0×8 5×2 7 8, `2645:*` 0, 2, 12, 13, 14).
* UI (`0f38`, §4.9).

## Index additions

`tools/srindex.py` `PTR_TABLES` takes near-pointer tables; the driver tables are **far** pointers
(offset, segment pairs), so they need either far-pointer support or the targets as seeds:

| Table | Entries | Targets |
|---|---|---|
| `DS:5BD0` far ×15 | EGA/VGA driver | `21a0:1862 223b 1cd5 113a`, `0e6c:0175`, `21a0:16dc 1508`, `0f38:0b2d 0b3f`, `21a0:25d9 1722 28ec 23bc`, `2645:0034 00e7` |
| `DS:5B8A` far ×15 | Tandy driver | `21a0:04ab 07ac 0358 0964`, `0e6c:0002`, `21a0:0c09 0a4f`, `0f38:0b2d 0b3f`, `21a0:0d62 0c26`, `0f38:a87e`, `2487:000a 0257 0309` |
| immediates in `21a0:1d97`/`1d9f` | far code pointers loaded as `mov ax,off / mov dx,seg` | `2595:0004`, `2634:000a` |
| `21a0:2409`/`2412` | near jump targets inside `21a0:23bc` (`jmp [bp-8]`) | `21a0:24bf`, `21a0:24fe` (labels, not functions) |

`21a0:254d` (get-pixel helper) is unreferenced dead code and need not be indexed. Segment `2beb`
is the Microsoft C 8087 emulator (`MSEM87` signature at `2beb:0000`), not a graphics driver;
segment `2645` is game code (race renderer, save/load helpers `2645:0b91`) that happens to hold the
EGA span routines `2645:0008`–`014b`.
