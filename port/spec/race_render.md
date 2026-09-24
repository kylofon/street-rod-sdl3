# race_render — Street Rod, SR.EXE, code segment `2645` (track, road renderer, rear-view mirror, opponent sprite)

Porting spec for the whole code segment `2645` (0x0000–0x5A5B, ≈23 KB). Despite the first-pass segment map
(RE_GUIDE: "graphics drivers for the other modes") it is **race/cruising code**: it builds the road ("track") at
new game / load time, advances the car along it, projects the road with a fixed-row pseudo-3D scheme, draws the
sky/horizon, road bands, town buildings, road-side objects (vector "hline sprites" and scaled billboard pictures),
the opponent car, the rear-view mirror (a second, smaller projection of the road behind), the cockpit overlays and
the drag-race start light, and flips the two VGA pages. A few functions are EGA/VGA driver slots that live here
(`0034`, `00e7`) and picture shrinkers for the three driver families (`5407`, `5670`, `5885`).

Conventions follow `port/RE_GUIDE.md` (`SSSS:OOOO`, `DS:xxxx` in DGROUP 3E96, far data as `389b:xxxx`).
Everything was read from the disassembly (`tools/x86dis.py`), the Ghidra output was only a guide (it is broken for
`0b91`, jump table at `2645:0cc8`). Confidence is **verified** unless a row says otherwise.

Ghidra shows segment `389b` as `0x489b`, and names offsets in it after unrelated strings; all such references are
written here as `389b:xxxx`. The segment words `DS:6A2C`…`DS:6A38` all hold the constant `0x389B` (initialised data),
the code loads `ES` from them.

-----------------------------------------------------------------------------------------------

## 1. Overview

### 1.1 What the track is

The world is a 1-D list of **road segments** (index `DS:7D30`, 0…0x45F). Each segment has five 16-bit attributes in
far segment `389b` (1120 words each, 0x8C0 bytes apart):

| Array | Name here | Meaning |
|---|---|---|
| `389b:33F0[i]` | `L[i]` | left road-side flags (objects on the left, town walls) |
| `389b:3CB0[i]` | `R[i]` | right road-side flags |
| `389b:4570[i]` | `YAW[i]` | road heading ("yaw") in heading units; accumulates through curves |
| `389b:4E30[i]` | `C[i]` | centre flags: road width, centre dashes, intersections, banners, signs |
| `389b:56F0[i]` | `HGT[i]` | road height/pitch offset (hills), 0 on flat road |

Segments `0…0xB3` (180) are the **town** (Hollywood-style street with sidewalks, buildings, intersections every 16
segments, billboards); segments `0xB4…0x45F` are the **race road** (open road, palms, trees, bushes, posts, poles).
A race starts at segment 200 (`0xC8`); the finish line is at segment `0x17B` for the short race (`DS:7EC0 == 0`) and
at `0x3FB` for the long one (`DS:7EC0 == 1`). Segments 200…0x185 are straight (with one hill pair at 220/257); from
0x186 on, `track_build_course` (`0b91`) lays out one of **10 fixed course layouts** (`DS:5CBC`, chosen at random at
new game, saved in the save game as `DS:5E0C`) made of 12 "features" each: left/right curves, S-bends, hills, dips,
lane closures with road works, a one-lane bridge.

One segment = 8 sub-steps (`DS:7D32`, 0…7). The car moves `DS:7D34` sub-steps per frame.

### 1.2 Coordinates and the projection

World lateral coordinate `x` (the car is at `DS:7D36`, 0xA00 at start): road edges are at fixed `x` values:
outer-left `0x44C`, left edge `0x5DC` (or `0x7D0` when the left lane is closed), right edge `0xCE4` (or `0xAF0`),
outer-right `0xE74`. Centre of the full road = 0x960; the player drives in the right lane (0xA00).
World height: road surface `0x7D0`, eye height `DS:7D38 = 0x76C`. The screen view is x 0…319, rows 18…101 (`0x12…0x65`),
horizon row 60 (`0x3C`), screen centre x 160 (`0xA0`).

There is **no rotation**: curves and hills are pure per-row shifts. For a row at depth `z` (in sub-steps, 4…116):

```
screen_x = 0xA0 - ((camX - (x + ((z*dx)>>2))) * 4) / z      dx = heading(car) - YAW[row segment]
screen_y = ((y + z*dy) - camY) * 4 / z + 0x3C               dy = HGT[current] - HGT[row segment]
```

i.e. `x ≈ 0xA0 + 4(x-camX)/z + dx` — the heading difference `dx` shifts the whole row by `dx` pixels, the height
difference shifts it by `4·dy` pixels. The rear-view mirror uses the same formula with centre x 0x118, horizon 0x24
and no ×4.

The front view uses **15 rows** (row 0 at z=4, rows 1…14 at `z = 12 - frac + 8(k-1)`), each row being one segment
(`seg+k`). The mirror uses **8 rows** behind (`seg-k`). For every row six screen values are computed: left edge,
right edge, road y, outer-left, outer-right, "eye-height y" (for banners and town walls).

### 1.3 Buffers and screen layout (VGA path, `DS:8236 == -2`)

| Page | Far pointer (descriptor) | Video segment | Contents |
|---|---|---|---|
| page 0 | `DS:7678` → `2fa3:0000` | `A000` | dashboard page shown in the lower split screen (`21a0:0014`); rows ≥ 100 used as off-screen storage for the "Vegas Gambler" (LIB2 #261, at y 0x64) and "Play Block Out" (#262, at y 0x8F) billboard sheets |
| page 1 | `DS:8188` → `DS:5DAC` | `A200` | 3D view double buffer 1 (rows 18…101); off-screen rows 102…153 = sky picture LIB2 #5, rows 163…199 = horizon panorama LIB2 #267 |
| page 2 | `DS:82B4` → `DS:5DDC` | `A400` | 3D view double buffer 2; off-screen rows 102…144 = location sign sheet (`DS:5E62[...]`, LIB2 #258/#237/#236/#238) |

`DS:5E3E` = back page number being drawn (1 → A200/`8188`, else A400/`82B4`), `DS:5E40` = page shown.
`DS:787A` = video segment of the back page, used directly by the hline/fill driver slots. The frame is drawn
entirely into the back page, then `road_page_flip` (`2288`) swaps and sets the CRTC start address (vsync-locked).
Descriptors (48-byte picture/page headers): `+0` width 0x140, `+2` height 0xC8, `+4` 0x1F40 bytes per plane,
`+0x26` segment (A000/A200/A400), `+0x2C` 0x28 bytes per row.

Other drivers (EGA without split, CGA, Tandy, Hercules): `7678`/`8188` come from `DS:822A`/`DS:822E`, `82B4` = `2e3e:0060`
(an off-screen RAM buffer), and `2288` copies the view rectangle `DS:5E42` = {320, 82, 0, 18, 0, 18} to the screen
instead of flipping. Parked (RE_GUIDE).

### 1.4 Frame order (`road_step` 2645:213d, called once per frame by the drive loop `0f38:7b22`)

1. steering input → car heading `DS:7D3C` (`78E2`), cruise speed in town, start-light retract.
2. **`road_frame` (`1e9e`)**: set segment pointers → `road_project_front` (`18fb`, 15 rows) → `road_project_mirror`
   (`1bcc`, 8 rows) → reset pointers → remember `ticks+9` → **`road_draw_frame` (`4b8c`)** → busy-wait until
   the 72.8 Hz tick counter `DS:05F8` reaches the remembered value (≥ 9 ticks per frame, ≈ 8.1 fps).
3. (not demo/auto) lateral drift of the car from the heading error (`7D36 -= (7D3C-YAW)*(7D34+78EC) >> 2`).
4. town only: advance `7D32` by `7D34`, `road_advance_segment` (`1efc`) when a whole segment was passed, road-side
   post "nudges" of the heading (`2126`). In races the advance is done by `0000:d624` (physics) → `1efc`.

`road_draw_frame` (`4b8c`), in this order, all into the back page:

1. clip = view (0,18)-(319,101); **sky** (2 scrolled blits) + **horizon mountains** (2 blits, parallax ×2) +
   **ground fill** from the highest road row down (`2982`).
2. for row = 14 … 1 (far to near): **road band** between row and row-1 (+ town walls), then road-side objects of the
   row (rows < 11 only), then the **opponent** if it is in this row's segment.
3. opponent if it is in the current segment; **rear-view mirror** (`43fd`): background, 7 bands + objects + opponent
   behind, frame lines, a copy of part of the mirror into page 0; **cockpit overlays** (A-pillar LIB2 #6, hood line #7);
   drag-race **start light** (#13); **page flip**.

### 1.5 Call graph

```
0000:39c0 new game ─ 2645:0d48 track_build_all ─ 016a (0f38:5eb6 rnd) · 0b91 track_build_course(100)
0000:56a9 / 0000:5b4a (load game) ─ 2645:0b91 track_build_course(DS:5E0C)
          0b91 ─ 016a · 017a 0293 03a8 0539 06ca 075d 07f2 0885 091a 0abb 09f0  (jump table 2645:0cc8)
0f38:7b22 drive loop
 ├ 2645:2429 road_load_graphics ─ 0f38:683e (load picture) · 0f38:a054/a09e/a0b8 (heap marks) · [78A2]/[78AA] blits
 │                               · shrinkers 5407 (VGA/EGA) | 5885 (Tandy) | 5670 (CGA/Herc) ─ 52b7 (528c) · 2634:00c1
 ├ 2645:2114 road_race_init ─ 185d road_state_init (171d, rnd) · 527a road_first_frame ─ 224e road_pages_init · 1e9e
 └ loop: 2645:213d road_step
          ├ 1e9e road_frame ─ 171d · 18fb road_project_front (176f 17b1) · 1bcc road_project_mirror (17ed 1828)
          │                 └ 4b8c road_draw_frame
          │                      ├ 2410 · 2982 road_draw_sky (17b1 22fc [78A2] [78DA]=00e7) · 0008/001d
          │                      ├ per row: 3a3b ─ (town) 39de/3a0d ─ 34da ─ 2d20 (2cfc) · 2f48 road_draw_band_front (2cfc 2f2b [78D6]=0034)
          │                      │          3aeb/3b3a ─ 2c0a shape_draw · 3b79 sign picture (22fc/2331 [78A2])
          │                      │          3ccc banner (2381 [78D2], 3aeb, 3b79) · 3f8d opponent_draw
          │                      ├ 43fd road_draw_mirror ─ 3edb · 23f7 · 3ab4 (3a57/3a86 ─ 37af ─ 2d20 · 324d) · objects · 3f8d · 2381
          │                      ├ 3e3c cockpit overlay · 3dc3 start light · 2288 road_page_flip (21a0:182e)
          ├ 1efc road_advance_segment (171d)       [also from 0000:d624 in races]
          └ 2126 road_nudge_heading
0000:238c timer ISR ─ 2645:206b road_edge_collision (every tick)
0f38:1094 / 0f38:1130 (UI) ─ 3abf shape_draw_dot · 23f0 · 2410
driver vector table DS:78A2.. (copied from DS:5BD0 by 21a0:1128): DS:78D6 = 2645:0034 hline, DS:78DA = 2645:00e7 fill rows
```

-----------------------------------------------------------------------------------------------

## 2. Function table

| address | proposed name | signature | purpose | confidence |
|---|---|---|---|---|
| 2645:0008 | road_fillmode_begin | far void() | VGA/EGA only: GC reg1 (enable set/reset) = 0x0F, reg3 = `DS:5C1F`<<8\|3 (replace) — solid-colour writes for `hline` | verified |
| 2645:001d | road_fillmode_end | far void() | VGA/EGA only: GC reg0=0, reg1=0, reg3=0, bit mask=0xFF — normal mode for blits | verified |
| 2645:0034 | ega_hline | far void(int x1,int x2,int y,int colour) | driver slot `DS:78D6`: clipped horizontal line, x1..x2 inclusive, into segment `DS:787A` | verified |
| 2645:00e7 | ega_fill_rows | far void(int y,int colour) | driver slot `DS:78DA`: fill rows y … `DS:5E52`-1 (full 40-byte rows) with colour | verified |
| 2645:014b | ega_pixel_addr | near (AX=y,BX=x) → ES:BX, CL | byte address y*40+x/8 in `787A`, CL = 7-(x&7), AH=1 | verified |
| 2645:016a | track_rnd | near int(int n) | `0f38:5eb6(n)`: random 0…n-1 | verified |
| 2645:017a | track_feat_curve_left | near void() | feature 1: chevron-left signs, 40-step heading ramp up | verified |
| 2645:0293 | track_feat_curve_right | near void() | feature 2: chevron-right signs, 45-step ramp down | verified |
| 2645:03a8 | track_feat_s_bend_left | near void() | feature 10: "winding road" sign 0x12, ramp up 40 then down 43 | verified |
| 2645:0539 | track_feat_s_bend_right | near void() | feature 11: sign 0x13, ramp down 35 then up 48 | verified |
| 2645:06ca | track_feat_hill | near void() | feature 3: 22-segment height profile `DS:5C20` | verified |
| 2645:075d | track_feat_dip | near void() | feature 4: negated `DS:5C20` | verified |
| 2645:07f2 | track_feat_hills_long | near void() | feature 5: 38-segment profile `DS:5C4C` | verified |
| 2645:0885 | track_feat_dips_long | near void() | feature 6: negated `DS:5C4C` | verified |
| 2645:091a | track_feat_roadworks_right | near void() | feature 7: road-work sign + barricade right, right lane closed 10 segs | verified |
| 2645:09f0 | track_feat_narrow_bridge | near void() | feature 9: "road narrows" signs, both lanes narrowed 20 segs | verified |
| 2645:0abb | track_feat_roadworks_left | near void() | feature 8: road-work sign + barricade left, left side closed 10 segs | verified |
| 2645:0b91 | track_build_course | far void(int arg) | clear/rebuild segments 0x17C…0x45F: dashes, course layout `DS:5E0C` (random if arg==100) | verified |
| 2645:0d48 | track_build_all | far void() | page pointers; build the town (0…0xB3) and the race road (0xB4…0x45F), specials; then `0b91(100)` | verified |
| 2645:171d | road_set_seg_ptrs | far void(int seg) | far ptrs `72EE`,`8634`,`7D62`,`7880`,`7560` = &L/R/C/HGT/YAW[seg] | verified |
| 2645:176f | road_proj_x | far int(int z,int x,int dx) | front view screen x | verified |
| 2645:17b1 | road_proj_y | far int(int z,int y,int dy) | front view screen y | verified |
| 2645:17ed | road_proj_x_mirror | far int(int z,int x,int dx) | mirror screen x | verified |
| 2645:1828 | road_proj_y_mirror | far int(int z,int y,int dy) | mirror screen y | verified |
| 2645:185d | road_state_init | far void() | start segment, camera, lane edges, AI start state | verified |
| 2645:18fb | road_project_front | far void() | 15 front rows → 6 row tables | verified |
| 2645:1bcc | road_project_mirror | far void() | 8 mirror rows → 6 row tables | verified |
| 2645:1e9e | road_frame | far void() | project, draw, wait for ≥ 9 ticks | verified |
| 2645:1efc | road_advance_segment | far void() | `7D30 += 7D32>>3`, road edges, height, autopilot after finish/demo | verified |
| 2645:206b | road_edge_collision | far int() | timer tick: 1 if car hits road-side obstacle / is far off road, sets `5E0E` on shoulder | verified |
| 2645:2114 | road_race_init | far void() | clear nudge latches, `185d`, `527a` | verified |
| 2645:2126 | road_nudge_heading | far void(int dir) | `7D3C += 3` (dir 1) or `-= 3` | verified |
| 2645:213d | road_step | far void() | per-frame step (see 1.4) | verified |
| 2645:224e | road_pages_init | near void() | back-page segment, show page `5E40` | verified |
| 2645:2288 | road_page_flip | near void() | swap `5E3E`/`5E40`, CRTC start (vsync) | verified |
| 2645:22fc | blit_clip_y | near void(Rect *r) | clip a blit rect vertically to `5E50`..`5E52` | verified |
| 2645:2331 | blit_clip_xy | near void(Rect *r) | + clip the left side to `5E4E` (srcx += d+7) | verified |
| 2645:2381 | road_vline | near void(int colour,int x,int y1,int y2) | clipped vertical line via `[78D2]` | verified |
| 2645:23f0 | clip_set_ymin0 | far void() | `5E50 = 0` (UI) | verified |
| 2645:23f7 | clip_set_mirror | near void() | clip = x 0xF2…0x13D, y 0x16…0x2A | verified |
| 2645:2410 | clip_set_view | far void() | clip = x 0…0x13F, y 0x12…0x65 | verified |
| 2645:2429 | road_load_graphics | far void() | load the drive-screen pictures into pages/buffers, build shrunk car sprites | verified |
| 2645:2982 | road_draw_sky | near void() | sky + horizon blits (scrolled by heading), ground fill, find top road row | verified |
| 2645:2c0a | shape_draw | far void(int x,int y,int size,int nsizes,ShapeSize *tab,int side) | draw a vector hline sprite, mirrored for side -1 | verified |
| 2645:2cfc | slope16 | near int(int xa,int ya,int xb,int yb) | `((xb-xa)<<4)/(yb-ya)`, 0 if ya==yb | verified |
| 2645:2d20 | quad_fill | near void(int c,int x0,int x1,int y0,int y1,int y2,int y3) | fill a quad with two vertical sides | verified (see §8 bug) |
| 2645:2f2b | sign | near int(int v) | -1/0/1 | verified |
| 2645:2f48 | road_draw_band_front | far void(int row) | scan-fill road/shoulders/centre dash between rows | verified |
| 2645:324d | road_draw_band_mirror | far void() | same for the mirror (grass drawn explicitly) | verified |
| 2645:34da | town_walls_front | near void(a,b,c,d,e,f,int left) | building façades, upper storeys, cross-street gaps | verified |
| 2645:37af | town_walls_mirror | near void(a,b,c,d,e,f,int left) | mirror version | verified |
| 2645:39de / 3a0d | town_walls_front_left / _right | near void() | argument marshalling for `34da` | verified |
| 2645:3a3b | road_draw_row_front | near void(int row) | town: walls; then `2f48(row)` | verified |
| 2645:3a57 / 3a86 | town_walls_mirror_left / _right | near void() | marshalling for `37af` | verified |
| 2645:3ab4 | road_draw_row_mirror | near void() | walls + `324d` | verified |
| 2645:3abf | shape_draw_dot | far void(int x,int y) | UI: fill mode, `2c0a` with shape `DS:60A0` (5-line cyan dot) — see video/UI spec | verified |
| 2645:3aeb | scenery_draw_shape | near void(int x,int y,int depth,int side,int type) | size = clamp(9-depth,0,6), y ≤ 0x65, shape `DS:6078[type]` | verified |
| 2645:3b3a | scenery_draw_hydrant | near void(int x,int y,int depth,int side) | size = clamp(9-depth,0,7), shape `DS:607C` | verified |
| 2645:3b79 | scenery_draw_sign_picture | near void(int x,int y,int depth,int align,int xclip,int kind) | blit one of 7 pre-scaled billboard sizes from an off-screen page | verified |
| 2645:3ccc | scenery_draw_banner | near void(int xl,int xr,int ytop,int ybot,int depth,int xclip,int flags) | overhead banner: two posts, cross bar, text shape or picture | verified |
| 2645:3dc3 | road_draw_start_light | near void() | drag start light LIB2 #13 retracting upward | verified |
| 2645:3e3c | road_draw_cockpit_overlay | near void() | LIB2 #6 at (0,18), #7 at (88,95), masked | verified |
| 2645:3edb | mirror_draw_background | near void() | LIB2 #9 at (240,18); 2 rows of #243 at the mirror horizon | verified |
| 2645:3f8d | opponent_draw | near void() | choose opponent sprite by distance/offset, project, blit; alongside/rear-end flags | verified |
| 2645:43fd | road_draw_mirror | near void() | whole rear-view mirror | verified |
| 2645:4b8c | road_draw_frame | far void() | whole frame (see 1.4) | verified |
| 2645:527a | road_first_frame | far void() | reset sprite hysteresis, `224e`, one `1e9e` | verified |
| 2645:528c | popcount8 | near int(uint8 m) | number of set bits | verified |
| 2645:52b7 | pic_alloc_shrunk | near Pic far*(Pic far *src,uint8 ymask,uint8 xmask) | allocate the header+data of a shrunk picture, clear data, mask = 0xFF | verified |
| 2645:5407 | pic_shrink_planar | far Pic far*(Pic far *src,uint8 ymask,uint8 xmask) | VGA/EGA: keep rows/columns whose bit (0x80>>(n&7)) is set in the masks; rebuild mask (`2634:00c1`) | verified |
| 2645:55f8 | cga_expand_bits | near void(uint8 m,uint8 *hi,uint8 *lo) | CGA: 1 bit/pixel mask → 2 bits/pixel | likely |
| 2645:5670 | pic_shrink_cga | far Pic far*(Pic far*,uint8,uint8) | CGA/Hercules version (parked) | likely |
| 2645:5885 | pic_shrink_tandy | far Pic far*(Pic far*,uint8,uint8) | Tandy version (parked) | likely |

External helpers used: `0f38:5eb6` rnd(n) (0…n-1, three combined LCGs, `DS:6C62..66`); `0f38:683e` load picture
(id < 1000 → LIB1 id-1, id ≥ 1000 → LIB2 id-1000; mode 2 = temporary, released by `0f38:a0b8(1)`); `0f38:9fce`
alloc picture; `0f38:1640` far memset; `21a0:182e` show page (CRTC start high = n<<5, waits for the start of vertical
retrace); `2634:00c1` build mask; driver slots `DS:78A2` blit(src, dst, Rect*, 1) (`21a0:1862`), `DS:78AA` masked
blit (`21a0:1cd5`), `DS:78D2` line(x1,y1,x2,y2,c) (`21a0:23bc`), `DS:78D6` hline, `DS:78DA` fill rows (video spec).

-----------------------------------------------------------------------------------------------

## 3. Globals

### 3.1 Far track arrays (segment 389b, word arrays of 0x460 entries)

| address | name | type | meaning | written by | read by |
|---|---|---|---|---|---|
| 389b:33F0 | track_left | u16[0x460] | `L[i]` left flags, §5.2 | 0d48, 0b91, 017a…0abb | 171d ptr, 18fb (no), 4b8c/43fd (via `6E20`), 34da/37af, 206b, 213d, 0000:8ea8 (bit0) |
| 389b:3CB0 | track_right | u16[0x460] | `R[i]` right flags | same | 4b8c/43fd (`6E24`), 206b, 213d |
| 389b:4570 | track_yaw | s16[0x460] | `YAW[i]` road heading | 0d48 (0), 0b91, features | 18fb, 1bcc (`7560`), 1efc, 213d, 3f8d |
| 389b:4E30 | track_centre | u16[0x460] | `C[i]` centre flags, §5.2 | 0d48, 0b91, features | 18fb, 1bcc, 1efc (seg, seg+4), 206b (seg+1), 4b8c/43fd (`8B82`) |
| 389b:56F0 | track_height | s16[0x460] | `HGT[i]` hill offset | 0d48, 0b91, features 3–6 | 18fb, 1bcc (`7880`), 1efc, 3f8d |

### 3.2 DGROUP

| DS offset | proposed name | type | meaning | written by | read by |
|---|---|---|---|---|---|
| DS:05F8 | timer_ticks | u32 | 72.8 Hz tick counter (platform) | 0000:238c | 1e9e |
| DS:0610 | demo_mode | u16 | demo: autopilot always | main | 1efc, 213d |
| DS:0612 | auto_mode | u16 | `auto` switch: autopilot always | main | 1efc, 213d |
| DS:0284 | town_location | u16 | 1…3: where cruising starts (seg 10 / 0x41 / 0x73), index of the location sign sheet | game flow | 185d, 2429 |
| DS:5C1E | gc_func_word | u16 | high byte = GC data rotate/function (0 = replace) | init 0 | 0008, 00e7 |
| DS:5C20 | hill_profile_22 | s16[22] | `1 2 3 4 4 4 4 3 2 1 0 0 -1 -2 -3 -4 -4 -4 -4 -3 -2 -1` | const | 06ca, 075d |
| DS:5C4C | hill_profile_38 | s16[38] | §5.1 | const | 07f2, 0885 |
| DS:5C98 | hill_profile_17 | s16[17] | `1 2 3 3 3 2 1 0 0 0 -1 -2 -3 -3 -3 -2 -1` | const | 0d48 |
| DS:5CBA | course_len | s16 | 12 features per layout | const | 0b91 |
| DS:5CBC | course_layouts | s16[10][12] | §5.1 | const | 0b91 |
| DS:5DAC | page1_desc | Pic | page A200 descriptor | const | via 8188 |
| DS:5DDC | page2_desc | Pic | page A400 descriptor | const | via 82B4 |
| DS:5E0C | course_layout | s16 | 0…9, saved/loaded with the game | 0b91, load 0000:5b4a | 0b91, save 0000:5eea |
| DS:5E0E | on_shoulder | u16 | 1 when the car is on a road shoulder (not hit) | 206b | 0000:1b27, ISR |
| DS:5E10 / 5E12 | nudge_right_latch / nudge_left_latch | u16 | once set, `7D3C -= 3` / `+= 3` every town frame | 213d; 2114 (0) | 213d |
| DS:5E14 | car_class_pics | s16[5][4] | per opponent class: picture ids {side, rear 3/4, rear, front} §5.3 | const | 2429 |
| DS:5E3E | back_page | s16 | 1 = A200 (`8188`), else A400 (`82B4`); initial 1 | 2288 | all blits, 224e |
| DS:5E40 | shown_page | s16 | page shown; initial 2 | 2288 | 224e, 2288 |
| DS:5E42 | view_copy_rect | Rect | {320,82,0,18,0,18} non-VGA present | const | 2288 |
| DS:5E4E/50/52/54 | clip_xmin/ymin/ymax/xmax | s16 | clip rectangle (inclusive) used by hline, fill, rect clippers; initial 0,18,101,319 | 2410, 23f7, 23f0 | 0034, 00e7, 22fc, 2331, 2381, 34da, 3f8d |
| DS:5E56 | shrink_masks_rear | u8[4] | 0x57 0x25 0x22 (0): sprites 3,4,5 | const | 2429 |
| DS:5E5A | shrink_masks_far | u8[4] | 0xF7 0x75 0x52: sprites 6,7,8 | const | 2429 |
| DS:5E5E | shrink_masks_front | u8[4] | 0x75 0x54 0x21: sprites 10,11,12 (and police) | const | 2429 |
| DS:5E62 | loc_sign_sheet | s16[4] | {1258,1237,1236,1238}: race → #258 "County Line", town loc 1…3 → #237 garage, #236 drive-in, #238 Gus gas | const | 2429 |
| DS:5E6A | dash_width_front | s16[15] | `8 6 5 4 3 2 2 2 1 1 1 0 0 0 0` centre-dash width / banner post thickness per front row | const | 2f48, 3ccc (via `6E28`) |
| DS:5E84 | dash_width_mirror | s16[8] | all 0 | const | 324d, 3ccc |
| DS:5E9C | sign_sizes | {s16 srcx,w,h}[7] | billboard sizes in the 264×43 sheets: (256,8,8) (240,16,13) (208,32,19) (168,40,24) (120,48,30) (64,56,37) (0,64,43) | const | 3b79 |
| DS:5EC6 | opp_substep_frac | s16 | `(d+8)&15` if d>16 else 0 (unused here) | 3f8d | — |
| DS:6078 | shape_table | u16[20] | near ptrs to `ShapeSet{u16 nsizes; ShapeSize *sizes}` §5.4 | const | 3aeb |
| DS:607C | hydrant_shape | u16 | = `6078[2]` (fire hydrant, 8 sizes) | const | 3b3a |
| DS:60A0 | dot_shape | u16 | ShapeSet of a 5-line cyan dot (UI) | const | 3abf |
| DS:6A2C…6A38 | seg389b_* | u16 | constant 0x389B, used to load ES | const | track code |
| DS:6DFE | build_yaw | s16 | current heading while building | 0b91, features | features |
| DS:6E00 | build_seg | s16 | build cursor (segment) | 0b91, features | features |
| DS:6E02 | build_yaw_step | s16 | 2 if `82C6` else 3 | 0b91 | features |
| DS:6E04 | ground_top_y | s16 | top row of the ground fill = min(horizon+8, min road y) | 2982 | 3f8d |
| DS:6E06 | top_band_row | s16 | row index (1…14) whose road y is highest; bands farther than this are not drawn | 2982 | 4b8c |
| DS:6E08…6E1E | row_ptr_* | u16 | near pointers into the row tables for the current band (§4.8) | 4b8c, 43fd | 2f48, 324d, 34da, 37af, objects |
| DS:6E20 / 6E24 | left_ptr / right_ptr | far u16* | &L / &R of the current row | 4b8c, 43fd | same |
| DS:6E28 | dash_ptr | u16 | &dash_width[row] | 4b8c, 43fd | 2f48, 324d, 3ccc |
| DS:6E2A | opp_sprite_prev2 | s16 | sprite index before the previous one (hysteresis) | 3f8d, 527a | 3f8d |
| DS:6E60 | pic_start_light | Pic far* | LIB2 #13 (race) | 2429 | 3dc3 |
| DS:70C0 / 7D2E | ai_edge_left / ai_edge_right | s16 | road edges 4 segments ahead (for the opponent AI) | 1efc | 0000:cf5c… |
| DS:70C2 / 70D2 | mir_xol / mir_xor | s16[8] | mirror row tables: x of 0x44C / 0xE74 | 1bcc | 324d, 37af, objects |
| DS:72EE | seg_left_ptr | far u16* | &L[seg] | 171d | 4b8c, 43fd |
| DS:72F2 | opp_rear_end | u16 | opponent right ahead, overlapping and slower (collision) | 3f8d | 0000:d992, da25 |
| DS:72F4 | pic_pillar | Pic far* | LIB2 #6 (88×82, A-pillar) | 2429 | 3e3c |
| DS:72F8 | row_xl | s16[15] | front: x of the left road edge | 18fb | 2f48, objects, 3ccc |
| DS:7526 | police_sprites | Pic far*[13] | [0] = `8ACE` (class side view), [1] = LIB2 #241, [2]…[6] = shrink(#242, 0x57), [7],[8] not set here, [9] = #246, [10]…[12] = shrink(#246, `5E5E[k]`) | 2429 | 0000:d992 (police, race spec) |
| DS:7560 | seg_yaw_ptr | far s16* | &YAW[seg] (advanced by the projections) | 171d, 18fb, 1bcc | 18fb, 1bcc, 1efc, 213d |
| DS:764A | pic_town_skyline | Pic far* | LIB2 #8 (town only) | 2429 | 2429 |
| DS:7648 | opp_car_id | s16 | opponent car; class = byte `DS:8DF0 + 10*id` | race code | 2429 |
| DS:7678 | page0_ptr | Pic far* | page A000 (`2fa3:0000`) on VGA | 0d48 | 2429, 3b79, 43fd |
| DS:767E | pic_mountains | Pic far* | LIB2 #267 | 2429 | 2429 |
| DS:787A | draw_seg | u16 | video segment of the back page (A200/A400) | 224e, 2288 | 0034, 00e7, 014b |
| DS:787C | pulled_over | u8 | ≠0: car pushed right 0x19 per frame (police stop) | race code | 213d |
| DS:7880 | seg_hgt_ptr | far s16* | &HGT[seg] | 171d, 18fb, 1bcc | 18fb, 1bcc, 1efc |
| DS:7884 | row_y | s16[15] | front: screen y of the road surface | 18fb | 2982, 2f48, objects |
| DS:78E0 | speed | s16 | car speed (≠0 = moving) | physics | 213d |
| DS:78E2 | steer | s16 | steering input; heading -= (steer>>1)+1 per frame | input 0000:1aad/2374 | 213d |
| DS:78E4 | wheel_pull | s16 | 2·(heading-YAW) clamped ±0x18 (autopilot only) | 1efc | dashboard |
| DS:78EC | speed_bonus | s16 | added to 7D34 for the drift term | race code | 213d |
| DS:7AAE | start_light_h | s16 | visible height of the start light, 32 → 0 by 4/frame once moving | 2429, 213d | 3dc3 |
| DS:7AB0 | row_yeye | s16[15] | front: screen y at eye height 0x76C (only for rows with `C&1` or `C&0xF000`) | 18fb | 34da, objects |
| DS:7ACE | row_xr | s16[15] | front: x of the right road edge | 18fb | … |
| DS:7D30 | seg | s16 | current road segment | 185d, 1efc | everything |
| DS:7D32 | substep | s16 | 0…7 within the segment (may exceed 7 until `1efc`) | 185d, 213d, 0000:d624, 1efc | 18fb, 1bcc, 3f8d |
| DS:7D34 | step_per_frame | s16 | sub-steps per frame (town: 4, or 6 if `82C6`; race: 0000:d544(speed)) | 213d, 0000:d624 | 213d, 3f8d |
| DS:7D36 | cam_x | s16 | car lateral position (0xA00 at start) | 185d, 213d, 1efc, race code | projections, 206b, 3f8d |
| DS:7D38 | cam_y | s16 | eye height 0x76C | 185d | 17b1, 1828 |
| DS:7D3A | — | s16 | cleared by 185d, unused here | 185d | — |
| DS:7D3C | heading | s16 | car heading in YAW units | 185d, 213d, 2126, 1efc, race code | projections, 2982, 3f8d |
| DS:7D3E | cur_height | s16 | HGT[seg] | 185d (0), 1efc | 18fb, 1bcc, 2982, 3f8d |
| DS:7D62 | seg_centre_ptr | far u16* | &C[seg] | 171d, 18fb, 1bcc | 18fb, 1bcc, 1efc, 4b8c, 43fd |
| DS:7D66 | start_light_green | u16 | ≠0: show the other half of #13 and set `8228` | race code | 3dc3 |
| DS:7D7C | opp_sprite | s16 | current opponent sprite index (−1…12) | 3f8d, 527a (0) | 3f8d, race code |
| DS:7D7E, 8648, 54D4 | ai_* | s16 | AI start state (1, rnd lane, 1) | 185d | 0000:d67d |
| DS:7D82 / 8BD0 | edge_left / edge_right | s16 | road edges at the current segment (0x5DC/0x7D0, 0xCE4/0xAF0) | 185d, 1efc | 206b, race code |
| DS:7E8A / 7E8C | opp_seg / opp_substep | s16 | opponent position | race code | 3f8d, 4b8c, 43fd |
| DS:7E8E | opp_step | s16 | opponent sub-steps per frame | race code | 3f8d |
| DS:7E90 | opp_x | s16 | opponent lateral position | race code | 3f8d, 1efc |
| DS:7EC0 | long_race | u16 | 0: finish at seg 0x17B, 1: at 0x3FB | game flow | 1efc |
| DS:7F22 / 865A | mir_xl / mir_xr | s16[8] | mirror left / right edge x | 1bcc | mirror |
| DS:7F8A / 7FD8 | row_xol / row_xor | s16[15] | front: x of 0x44C / 0xE74 | 18fb | 2f48, 34da, objects |
| DS:8184 | pic_mirror | Pic far* | LIB2 #9 (80×27 mirror) | 2429 | 3edb, 43fd |
| DS:8188 | page1_ptr | Pic far* | page 1 (A200) | 0d48 | blits |
| DS:8228 | light_flag | u16 | set to 1 by 3dc3 when `7D66` | 3dc3 | 0f38:7b22 |
| DS:8236 | video_driver | s16 | −2 = EGA/VGA | startup | many |
| DS:82B4 | page2_ptr | Pic far* | page 2 (A400) | 0d48 | blits |
| DS:82C6 | easy_mode | u16 | `DS:8ACC < 4` (0000:0659): sparser scenery, longer dash period, yaw step 2, town speed 6 | 0000:0659 | 0b91, 0d48, 213d |
| DS:8638 / 864A | mir_y / mir_yeye | s16[8] | mirror road y / eye-height y | 1bcc | mirror |
| DS:8646 | — | s16 | = `mir_y[7]` (used by 3edb for the mirror horizon strip) | 1bcc | 3edb |
| DS:8634 | seg_right_ptr | far u16* | &R[seg] | 171d | 4b8c, 43fd |
| DS:8ACA | in_race | u8/u16 | ≠0 during races (0 = cruising the town) | game | many |
| DS:8ACC | opp_level | s16 | opponent level; ≥4 enables sprite hysteresis and wider rear-end window | game | 3f8d, 0000:0659 |
| DS:8ACE | opp_sprites | Pic far*[13] | opponent sprites §5.3 | 2429 | 3f8d |
| DS:8B7E | pic_mirror_sky | Pic far* | LIB2 #243 | 2429 | 3edb |
| DS:8B82 | centre_ptr | far u16* | &C of the current row | 4b8c, 43fd | 2f48, 324d, 34da, 37af, 3ccc |
| DS:8BC2 | opp_alongside | u16 | opponent sprite 0, or sprite 2 at d ≤ 10 | 3f8d | 0000:d992, da25 |
| DS:8BCA | pic_hood | Pic far* | LIB2 #7 (232×5) | 2429 | 3e3c |

<!--PART2-->
