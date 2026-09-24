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
| DS:7EC0 | race_type | u16 | 0 drag race (finish at seg 0x17B), 1 road race (0x3FB); set by `challenge 0000:9d6d` (race.md) | 9d6d | 1efc |
| DS:7F22 / 865A | mir_xl / mir_xr | s16[8] | mirror left / right edge x | 1bcc | mirror |
| DS:7F8A / 7FD8 | row_xol / row_xor | s16[15] | front: x of 0x44C / 0xE74 | 18fb | 2f48, 34da, objects |
| DS:8184 | pic_mirror | Pic far* | LIB2 #9 (80×27 mirror) | 2429 | 3edb, 43fd |
| DS:8188 | page1_ptr | Pic far* | page 1 (A200) | 0d48 | blits |
| DS:8228 | light_flag | u16 | set to 1 by 3dc3 when `7D66` | 3dc3 | 0f38:7b22 |
| DS:8236 | video_driver | s16 | −2 = EGA/VGA | startup | many |
| DS:82B4 | page2_ptr | Pic far* | page 2 (A400) | 0d48 | blits |
| DS:82C6 | slow_cpu | u16 | `DS:8ACC < 4` (0000:0659, slow machine): sparser scenery, longer dash period, yaw step 2, town speed 6 | 0000:0659 | 0b91, 0d48, 213d |
| DS:8638 / 864A | mir_y / mir_yeye | s16[8] | mirror road y / eye-height y | 1bcc | mirror |
| DS:8646 | — | s16 | = `mir_y[7]` (used by 3edb for the mirror horizon strip) | 1bcc | 3edb |
| DS:8634 | seg_right_ptr | far u16* | &R[seg] | 171d | 4b8c, 43fd |
| DS:8ACA | in_race | u8/u16 | ≠0 during races (0 = cruising the town) | game | many |
| DS:8ACC | cpu_class | s16 | CPU speed class from the start-up loop (`0000:05c6`, loops per BIOS tick / 950, ≥ 1); ≥ 4 enables sprite hysteresis and the wider rear-end window (race.md §7) | 0000:066f | 3f8d, 0000:0659 |
| DS:8ACE | opp_sprites | Pic far*[13] | opponent sprites §5.3 | 2429 | 3f8d |
| DS:8B7E | pic_mirror_sky | Pic far* | LIB2 #243 | 2429 | 3edb |
| DS:8B82 | centre_ptr | far u16* | &C of the current row | 4b8c, 43fd | 2f48, 324d, 34da, 37af, 3ccc |
| DS:8BC2 | opp_alongside | u16 | opponent sprite 0, or sprite 2 at d ≤ 10 | 3f8d | 0000:d992, da25 |
| DS:8BCA | pic_hood | Pic far* | LIB2 #7 (232×5) | 2429 | 3e3c |

-----------------------------------------------------------------------------------------------

## 4. Pseudocode

### 4.1 Conventions

* `int` = s16, `uint` = u16, all arithmetic 16-bit with wrap-around; `/` and `%` are the 8086 `idiv` after `cwd`
  (truncate toward zero, remainder has the dividend's sign). `>>` on `int` is `sar` (arithmetic).
* `L[i]`, `R[i]`, `C[i]` = u16 words at `389b:33F0/3CB0/4E30 + 2i`; `YAW[i]`, `HGT[i]` = s16 at `389b:4570/56F0 + 2i`.
  Negative or large indices are **not** checked by the original (e.g. `L[i-1]` at i = 0 reads `389b:33EE`); keep the
  same flat layout (`mem[]` at the original address, PORTING.md) so such reads behave identically.
* `rnd(n)` = `0f38:5eb6(n)` → 0…n-1 (game_flow/platform spec). The order of `rnd` calls matters (shared generator);
  the pseudocode keeps it exactly.
* `hline(x1,x2,y,c)` = `[DS:78D6]` (2645:0034 on VGA, §4.14), `fill_rows(y,c)` = `[DS:78DA]` (00e7),
  `blit(src,dst,&rect,1)` = `[DS:78A2]` opaque, `blit_masked(...)` = `[DS:78AA]` (colour 0 transparent),
  `line(x1,y1,x2,y2,c)` = `[DS:78D2]`. `Rect = {w, h, srcx, srcy, dstx, dsty}` (6 words, the order used by every blit
  here). `BACK` = `DS:5E3E == 1 ? *(far*)DS:8188 : *(far*)DS:82B4`. `pic->w`, `pic->h` = words 0 and 2 of a picture.
* `fillmode_begin()` / `fillmode_end()` = 2645:0008 / 001d. On the SDL3 port they are no-ops (§6), but the calls are
  listed to show where blits happen inside the fill sections.

### 4.2 Track generation — new game (`track_build_all`, 2645:0d48)

```c
void track_build_all(void)                        /* 2645:0d48, from 0000:39c0 (new game) */
{
    if (DS_8236 == -2) {                           /* VGA/EGA: pages A000/A200/A400 */
        DS_7678 = FAR(0x2fa3, 0x0000);
        DS_8188 = FAR(DS, 0x5DAC);
        DS_82B4 = FAR(DS, 0x5DDC);
    } else {                                       /* other drivers (parked) */
        DS_7678 = DS_822A;  DS_8188 = DS_822E;  DS_82B4 = FAR(0x2e3e, 0x0060);
    }
    int lvl = DS_82C6;                             /* [bp-2] */
    int i;
    /* ---- town: segments 0 .. 0xB3 ---- */
    for (i = 0; i < 0xB4; i++) {
        HGT[i] = 0; YAW[i] = 0; C[i] = 0; R[i] = 0; L[i] = 0;
        C[i] |= 1;                                 /* town segment: sidewalks, walls */
        if ((i + 1) % (lvl*4 + 4) == 0) C[i] |= 8; /* centre dash */
        if ((i + 2) % 16 == 0) {                   /* intersection: segments i..i+2 are the cross street */
            C[i+2] |= 4; C[i+1] |= 4; C[i] |= 4;
            R[i-1] |= 0x40;                        /* fire hydrant on the right corner */
            L[i+3] |= 0x40;                        /* fire hydrant on the left corner  */
            C[i+1] |= 1; C[i+2] |= 1; C[i+3] |= 1;
            HGT[i+1] = 0; YAW[i+1] = 0; HGT[i+2] = 0; YAW[i+2] = 0; HGT[i+3] = 0; YAW[i+3] = 0;
            i += 3;                                /* + the loop's i++ */
            continue;
        }
        /* left buildings: a block ends at (i+4)%16, (i+8)%16 or (i+12)%16 == 0 */
        if ((i+4) % 16 == 0 || (i+8) % 16 == 0 || (i+12) % 16 == 0) {
            if (rnd(2) == 0) {
                L[i-1] |= 4; L[i] |= 4;            /* 2-segment upper storey */
                if (rnd(3) == 0) { L[i-1] |= 8; L[i] |= 8; }   /* tall (2x) */
                L[i] |= 0x10;                      /* wall colour 2 instead of 3 */
            } else {
                L[i-2] |= 4; L[i] |= 4; L[i-1] |= 4;
                if (rnd(3) == 0) { L[i-2] |= 8; L[i] |= 8; L[i-1] |= 8; }
                L[i-1] |= 0x10;
            }
        } else
            L[i] |= 0x80;                          /* sidewalk with a colour-9 strip */
        /* right buildings: offsets 5, 9, 12 */
        if ((i+5) % 16 == 0 || (i+9) % 16 == 0 || (i+12) % 16 == 0) {
            if (rnd(3) != 0) {
                R[i-1] |= 4; R[i] |= 4;
                if (rnd(2) == 0) { R[i-1] |= 8; R[i] |= 8; }
                R[i] |= 0x10;
            } else {
                R[i-2] |= 4; R[i] |= 4; R[i-1] |= 4;
                if (rnd(4) == 0) { R[i-2] |= 8; R[i] |= 8; R[i-1] |= 8; }
                R[i-1] |= 0x10;
            }
        } else
            R[i] |= 0x80;
        if (rnd(lvl*0x28 + 6) == 0 && !(L[i] & 0x10)) L[i] |= 0x20;   /* palm */
        if (rnd(lvl*0x28 + 7) == 0 && !(R[i] & 0x10)) R[i] |= 0x20;
        if (rnd(lvl*0x28 + 3) == 0 && !(L[i] & 0x10)) L[i] |= 0x100;  /* bush */
        if (rnd(lvl*0x28 + 4) == 0 && !(R[i] & 0x10)) R[i] |= 0x100;
    }
    /* town billboards over/beside the street (type from C&0x4000/0x8000 in the town → pictures #261/#262) */
    C[rnd(0x2D) + 0x0A] |= 0x4000;   C[rnd(0x2D) + 0x0A] |= 0x8000;
    C[rnd(0x37) + 0x41] |= 0x4000;   C[rnd(0x37) + 0x41] |= 0x8000;
    C[rnd(0x37) + 0x73] |= 0x4000;   C[rnd(0x37) + 0x73] |= 0x8000;
    R[0x2C] |= 1;                                   /* location sign (picture, right) */
    L[0x1D] |= 0x1000; L[0x18] |= 0x0800;           /* road works: sign, barricade 5 segs later */
    L[0x6C] |= 1;                                   /* location sign (left) */
    L[0x57] |= 0x1000; L[0x52] |= 0x0800;
    L[0x9C] |= 1;
    L[0x87] |= 0x1000; L[0x82] |= 0x0800;
    /* ---- race road: segments 0xB4 .. 0x45F ---- */
    for (i = 0xB4; i < 0x460; i++) {
        HGT[i] = 0; YAW[i] = 0; C[i] = 0; L[i] = 0; R[i] = 0;
        if ((i + 1) % (DS_82C6*4 + 4) == 0) C[i] |= 8;
        if (i % (lvl*4 + 4) == 0) { L[i] |= 1; R[i] |= 1; }   /* edge posts (shape 1) */
        if ((i + 2) % (lvl*8 + 8) == 0) C[i] |= 2;           /* telephone pole, right */
        if (rnd(lvl*0xC8 + 7) == 0) L[i] |= 0x20;             /* palms */
        if (rnd(lvl*0xC8 + 9) == 0) R[i] |= 0x20;
        if (lvl == 0) {                                       /* 2645:14f7 */
            if (rnd(0x23) == 0) {                             /* tree group, removes palms */
                L[i] |= 0x40; L[i-1] |= 0x40; L[i-2] |= 0x40;
                L[i] &= ~0x20; L[i-1] &= ~0x20; L[i-2] &= ~0x20;
            }
            if (rnd(0x19) == 0) {
                R[i] |= 0x40; R[i-2] |= 0x40;
                R[i] &= ~0x20; R[i-2] &= ~0x20;
            }
        } else {                                              /* 2645:120b */
            if (rnd(lvl*0x64 + 0x23) == 0) { L[i] |= 0x40; L[i] &= ~0x20; }
            if (rnd(lvl*0x64 + 0x19) == 0) { R[i] |= 0x40; R[i] &= ~0x20; }
        }
        /* 2645:1267 */
        if (rnd(lvl*0x64 + 7) == 0 && !(L[i] & 0x80)) L[i] |= 0x80;  /* big tree */
        if (rnd(lvl*0x64 + 9) == 0 && !(R[i] & 0x80)) R[i] |= 0x80;
        if (rnd(lvl*0x64 + 0x15) == 0) L[i] |= 4;                    /* small bush */
        if (rnd(lvl*0x64 + 0x19) == 0) R[i] |= 4;
        if (rnd(lvl*0x64 + 0x17) == 0 && !(L[i] & 4)) L[i] |= 8;     /* bush */
        if (rnd(lvl*0x64 + 0x1E) == 0 && !(R[i] & 4)) R[i] |= 8;
        if (rnd(lvl*0x32 + 4) == 0) { L[i] |= 0x100; if (rnd(2) == 0) L[i] |= 0x200; }  /* shrub, far out */
        if (rnd(lvl*0x32 + 4) == 0) { R[i] |= 0x100; if (rnd(2) == 0) R[i] |= 0x200; }
    }
    /* finish lines */
    L[0x17B] &= 0x1801; R[0x17B] &= 0x1801; C[0x17B] |= 0x1010;   /* short race: white band + "CITY LIMITS" banner */
    L[0x3FB] &= 0x1801; R[0x3FB] &= 0x1801; C[0x3FB] |= 0x2010;   /* long race: white band + sign-sheet banner (#258) */
    /* three overhead route signs */
    int s = rnd(0x8C) + 0xDC;  if (R[s] & 2) s--;                  /* R bit 1 is never set: s unchanged */
    L[s] &= 0x1801; R[s] &= 0x1801; C[s] |= 0x8000;               /* route 69 arrow (shape 0x0E) */
    s = rnd(0x258) + 0x186;    if (R[s] & 2) s--;
    L[s] &= 0x1801; R[s] &= 0x1801; C[s] |= 0x8000;
    s = rnd(0x258) + 0x186;    if (R[s] & 2) s--;
    L[s] &= 0x1801; R[s] &= 0x1801; C[s] |= 0x4000;               /* route 48 arrow (shape 0x0C) */
    /* hills on the first race stretch */
    for (i = 0xDC; ; ) {
        for (k = 0; k < 0x11; k++) HGT[i + k] = DS_5C98[k];   i += 0x11 + 0x14;
        for (k = 0; k < 0x11; k++) HGT[i + k] = -DS_5C98[k];  i += 0x11 + 0x1E;
        if (i + 0x5A >= 0x17C) break;                          /* → one pair: 220..236 and 257..273 */
    }
    C[0x181] |= 0x100;                                         /* speed-limit sign */
    track_build_course(100);
}
```

### 4.3 Track generation — the course (`track_build_course`, 2645:0b91) and its features

```c
void track_build_course(int arg)                  /* 2645:0b91 (far); also called after loading a game */
{
    DS_6E00 = 0x186;  DS_6DFE = 0;
    DS_6E02 = DS_82C6 ? 2 : 3;                     /* heading change per segment in a curve */
    int per = DS_82C6*4 + 4;
    memset16(&YAW[0x17C], 0, 0x2E4);               /* 0x17C .. 0x45F */
    memset16(&HGT[0x17C], 0, 0x2E4);
    for (int i = 0x17C; i < 0x460; i++) {           /* remove previous course features, keep scenery */
        C[i] &= 0xF117;
        L[i] &= 0x87FF;                            /* high byte &= 0x87 */
        R[i] &= 0xE7FF;                            /* high byte &= 0xE7 */
        if ((i + 1) % per == 0) C[i] |= 8;
    }
    if (arg == 100) DS_5E0C = rnd(10);
    for (int k = 0; k < DS_5CBA /*12*/; k++)
        switch (DS_5CBC[DS_5E0C*12 + k]) {         /* jump table 2645:0cc8, values 1..11, others ignored */
        case 1:  track_feat_curve_left();      break;   /* 017a */
        case 2:  track_feat_curve_right();     break;   /* 0293 */
        case 3:  track_feat_hill();            break;   /* 06ca */
        case 4:  track_feat_dip();             break;   /* 075d */
        case 5:  track_feat_hills_long();      break;   /* 07f2 */
        case 6:  track_feat_dips_long();       break;   /* 0885 */
        case 7:  track_feat_roadworks_right(); break;   /* 091a */
        case 8:  track_feat_roadworks_left();  break;   /* 0abb */
        case 9:  track_feat_narrow_bridge();   break;   /* 09f0 */
        case 10: track_feat_s_bend_left();     break;   /* 03a8 */
        case 11: track_feat_s_bend_right();    break;   /* 0539 */
        }
    if (DS_6E00 < 0x460) {                          /* rest of the road: flat, final heading */
        int n = 0x460 - DS_6E00;
        memset16(&HGT[DS_6E00], 0, n);
        for (int j = 0; j < n; j++) YAW[DS_6E00 + j] = DS_6DFE;
        DS_6E00 += n;
    }
}
```

Common building blocks of the features (all near, no arguments; `pos` = `DS:6E00`, `yaw` = `DS:6DFE`, `step` = `DS:6E02`):

```c
static void prefill(int n)            /* YAW[pos .. pos+n-1] = yaw; pos NOT advanced */
{ for (int j = 0; j < n; j++) YAW[pos + j] = yaw; }

static void ramp(int delta)           /* delta = +40 etc.: heading ramp, solid centre line */
{
    int b = yaw / step;               /* cwd/idiv */
    int target = b + delta;
    if (delta > 0 ? target > b : target < b) {
        int di = b, cx = pos, si = b * step;       /* imul low word */
        while (cx < 0x442 && (delta > 0 ? di < target : di > target)) {
            C[cx] |= 8;                /* solid line: dash bit on every segment */
            YAW[cx] = si;
            si += delta > 0 ? step : -step;
            di += delta > 0 ? 1 : -1;
            cx++;
        }
        b = di;  pos = cx;
    }
    yaw = b * step;                   /* always re-stored, even if the loop did not run */
}

static void keep_side_flags(int i) { L[i] &= 0x1801; R[i] &= 0x1801; }   /* 0x1801: roadworks sign/barricade + bit0 */
```

| Feature (address) | Exact sequence |
|---|---|
| 1 curve left (`017a`) | `prefill(0x32)`; `C[pos] \|= 0x200` (chevron `<`); `pos += 5`; `keep_side_flags(pos)`; `C[pos] \|= 0x200`; `pos += 6`; `ramp(+0x28)` |
| 2 curve right (`0293`) | `prefill(0x32)`; `keep_side_flags(pos)`; `C[pos] \|= 0x400` (chevron `>`); `pos += 3`; `C[pos] \|= 0x400`; `pos += 3`; `ramp(-0x2D)` |
| 10 S-bend (`03a8`) | `prefill(0x5A)`; `pos += 1`; `keep_side_flags(pos)`; `R[pos] \|= 0x2000` (sign 0x12); `pos += 6`; `R[pos] \|= 0x2000`; `ramp(+0x28)`; `ramp(-0x2B)` |
| 11 S-bend (`0539`) | `prefill(0x5A)`; `pos += 1`; `keep_side_flags(pos)`; `R[pos] \|= 0x4000` (sign 0x13); `pos += 6`; `R[pos] \|= 0x4000`; `ramp(-0x23)`; `ramp(+0x30)` |
| 3 hill (`06ca`) | `s = pos`; `pos += 0x14`; for k < 0x16 while `pos < 0x442`: `HGT[pos++] = DS_5C20[k]`; `pos += 8`; `YAW[s … pos-1] = yaw` |
| 4 dip (`075d`) | as 3 with lead `0x0A`, values `-DS_5C20[k]`, then `pos += 0x12` |
| 5 hills (`07f2`) | lead 6, 0x26 values of `DS_5C4C`, then `pos += 6` |
| 6 dips (`0885`) | lead 8, 0x26 values of `-DS_5C4C`, then `pos += 4` |
| 7 roadworks right (`091a`) | `s = pos`; `pos += 3`; `R[pos] \|= 0x0800` (sign); `pos += 4`; `C[pos++] &= 0x0FF7`; `R[pos++] \|= 0x1000` (barricade); 10×: `C[pos] \|= 0x40; C[pos] &= 0x0FF7; pos++` (right lane closed); `C[pos++] &= 0x0FF7`; `pos += 3`; `YAW[s … pos-1] = yaw` |
| 8 roadworks left (`0abb`) | `s = pos`; `pos += 4`; `L[pos] \|= 0x0800`; `pos += 4`; `C[pos++] &= 0x0FF7`; `L[pos++] \|= 0x1000`; 10×: `C[pos] \|= 0x20; C[pos] &= 0x0FF7; pos++` (left lane closed); `C[pos++] &= 0x0FF7`; `pos += 2`; `YAW[s … pos-1] = yaw` |
| 9 narrow bridge (`09f0`) | `s = pos`; `C[pos] \|= 0x0800` ("road narrows" sign); `pos += 4`; `keep_side_flags(pos)`; `C[pos] \|= 0x0800`; `pos += 6`; 20×: `C[pos] \|= 0x80; C[pos] &= 0x0FF7; pos++`; `C[pos] &= 0x0FFF` (high byte &= 0x0F); `YAW[s … pos-1] = yaw` |

Notes: the lane loops of 7/8/9 have no 0x442 bound check. `&= 0x0FF7` is a word AND: it clears the dash **and** all
banner bits 0xF000 of those segments. Curves never touch `HGT`, hills never touch `YAW` except the final re-fill.
With `step` 3, a left curve changes the heading by +120, a right curve by −135.

### 4.4 Segment pointers, projection primitives

```c
void road_set_seg_ptrs(int seg)                   /* 2645:171d */
{
    DS_72EE = FAR(0x389b, 0x33F0 + 2*seg);   /* &L   */
    DS_8634 = FAR(0x389b, 0x3CB0 + 2*seg);   /* &R   */
    DS_7D62 = FAR(0x389b, 0x4E30 + 2*seg);   /* &C   */
    DS_7880 = FAR(0x389b, 0x56F0 + 2*seg);   /* &HGT */
    DS_7560 = FAR(0x389b, 0x4570 + 2*seg);   /* &YAW */
}

int road_proj_x(int z, int x, int dx)             /* 2645:176f — front view */
{
    x += (int)(z * dx) >> 2;                       /* imul low word, sar 2 */
    int t = (DS_7D36 - x) << 2;                    /* 16-bit */
    if (z == 0) z = 1;
    return 0xA0 - t / z;
}
int road_proj_y(int z, int y, int dy)             /* 2645:17b1 */
{
    y += z * dy;
    int t = (y - DS_7D38) << 2;
    if (z == 0) z = 1;
    return t / z + 0x3C;
}
int road_proj_x_mirror(int z, int x, int dx)      /* 2645:17ed — no <<2 on the difference */
{
    x += (int)(z * dx) >> 2;
    int t = DS_7D36 - x;
    if (z == 0) z = 1;
    return 0x118 - t / z;
}
int road_proj_y_mirror(int z, int y, int dy)      /* 2645:1828 */
{
    y += z * dy;
    int t = y - DS_7D38;
    if (z == 0) z = 1;
    return t / z + 0x24;
}
```

Useful values on a flat straight road with the car at 0xA00: road y = 400/z + 60 → row 1 (z = 12−frac … 5) is
below the view (y ≥ 93), row 14 (z ≈ 116) at y 63. Right edge x = 160 + 2960/z, left edge 160 − 4240/z.

### 4.5 Row tables (`road_project_front` 2645:18fb, `road_project_mirror` 2645:1bcc)

```c
/* Front: 15 rows, row k uses segment seg+k. Tables (s16[15]):
   XL = DS:72F8, XR = DS:7ACE, Y = DS:7884, XOL = DS:7F8A, XOR = DS:7FD8, YE = DS:7AB0 */
void road_project_front(void)                     /* 2645:18fb; pointers set by 171d(seg) before */
{
    for (int k = 0; k < 15; k++) {
        uint c = *DS_7D62;  DS_7D62 += 2;           /* C[seg+k] */
        int hasEye = (c & 1) || (c & 0xF000);
        int cm = c & 0xE0;
        int dy = DS_7D3E - *DS_7880;  DS_7880 += 2; /* HGT */
        int dx = DS_7D3C - *DS_7560;  DS_7560 += 2; /* YAW */
        int z  = (k == 0) ? 4 : 0x0C - DS_7D32 + 8*(k-1);
        if (k == 0) Y[0] = road_proj_y(4, 0x7D0, dy);     /* row 0: Y first */
        int xr_w, xl_w;                                     /* world x of the edges */
        if (cm == 0)            { xr_w = 0xCE4; xl_w = 0x5DC; }
        else if (cm & 0x80)     { xr_w = 0xAF0; xl_w = 0x7D0; }   /* one-lane (bridge) */
        else if (cm & 0x20)     { xr_w = 0xCE4; xl_w = 0x7D0; }   /* left side closed  */
        else /* 0x40 */         { xr_w = 0xAF0; xl_w = 0x5DC; }   /* right lane closed */
        XR[k] = road_proj_x(z, xr_w, dx);
        XL[k] = road_proj_x(z, xl_w, dx);
        if (k != 0) Y[k] = road_proj_y(z, 0x7D0, dy);     /* rows 1..14: Y after the edges */
        XOR[k] = road_proj_x(z, 0xE74, dx);
        XOL[k] = road_proj_x(z, 0x44C, dx);
        if (hasEye) YE[k] = road_proj_y(z, 0x76C, dy);    /* else YE[k] keeps its old value */
    }
}
/* Order of evaluation only matters for the (pure) function calls; results are identical in any order. */

/* Mirror: 8 rows behind, row k uses segment seg-k; z = 8 for row 0, DS_7D32 + 8k for rows 1..7.
   Tables (s16[8]): XL = DS:7F22, XR = DS:865A, Y = DS:8638, XOL = DS:70C2, XOR = DS:70D2, YE = DS:864A */
void road_project_mirror(void)                    /* 2645:1bcc */
{
    for (int k = 0; k < 8; k++) {
        uint c = *DS_7D62;  DS_7D62 -= 2;           /* C[seg-k] */
        int hasEye = (c & 1) || (c & 0xF000);
        int cm = c & 0xE0;
        int dy = *DS_7880 - DS_7D3E;  DS_7880 -= 2; /* note: signs reversed */
        int dx = *DS_7560 - DS_7D3C;  DS_7560 -= 2;
        int z  = (k == 0) ? 8 : DS_7D32 + 8*k;
        Y[k]   = road_proj_y_mirror(z, 0x7D0, dy);
        /* edges exactly as the front, using road_proj_x_mirror */
        XR[k] = road_proj_x_mirror(z, xr_w, dx);  XL[k] = road_proj_x_mirror(z, xl_w, dx);
        XOR[k] = road_proj_x_mirror(z, 0xE74, dx); XOL[k] = road_proj_x_mirror(z, 0x44C, dx);
        if (hasEye) YE[k] = road_proj_y_mirror(z, 0x76C, dy);
    }
}
```

`road_frame` resets the pointers with `171d(seg)` before and after each projection, because both advance them.

### 4.6 Per-frame step, segment advance, collisions

```c
void road_frame(void)                             /* 2645:1e9e */
{
    road_set_seg_ptrs(DS_7D30);  road_project_front();
    road_set_seg_ptrs(DS_7D30);  road_project_mirror();
    road_set_seg_ptrs(DS_7D30);
    u32 until = DS_05F8 + 9;                       /* 32-bit add */
    road_draw_frame();
    while ((s32)until > (s32)DS_05F8)              /* hi word signed compare, lo word unsigned */
        host_pump();                               /* PORTING.md: every busy-wait pumps the host */
}

void road_step(void)                              /* 2645:213d — once per frame from 0f38:7b22 */
{
    if (DS_78E2 != 0 && DS_78E0 != 0) {
        int e = DS_7D3C - *DS_7560 - DS_78E2;
        if (abs(e) < 0xC8) DS_7D3C -= (DS_78E2 >> 1) + 1;       /* steering changes the heading */
    }
    if (!DS_8ACA) DS_7D34 = DS_82C6 ? 6 : 4;                    /* town: constant cruising speed */
    if (DS_7D34 > 0 && DS_7AAE > 0) DS_7AAE -= 4;               /* start light retracts */
    road_frame();
    if (!DS_0610 && !DS_0612) {
        if (DS_787C) DS_7D36 += 0x19;                            /* pulled over */
        else if (DS_78E0) {
            int t = (DS_7D3C - *DS_7560) * (DS_7D34 + DS_78EC);  /* imul, low word */
            DS_7D36 -= t >> 2;                                   /* heading error → lateral drift */
        }
    }
    if (DS_8ACA) return;                                         /* races: advance done by 0000:d624 */
    DS_7D32 += DS_7D34;
    if (DS_7D32 >= 8) road_advance_segment();
    if ((L[DS_7D30 + 7] & 1) || DS_5E12) { DS_5E12 = 1; road_nudge_heading(1); }   /* 7D3C += 3 */
    if ((R[DS_7D30 + 6] & 1) || DS_5E10) { road_nudge_heading(-1); DS_5E10 = 1; }  /* 7D3C -= 3 */
}

void road_nudge_heading(int dir)                  /* 2645:2126 */
{ if (dir == 1) DS_7D3C += 3; else DS_7D3C -= 3; }

void road_advance_segment(void)                   /* 2645:1efc — also from 0000:d624 */
{
    DS_7D30 += DS_7D32 >> 3;
    road_set_seg_ptrs(DS_7D30);
    DS_7D32 &= 7;
    uint cm = C[DS_7D30] & 0xE0;                   /* edges of the current segment */
    if (cm == 0)          { DS_7D82 = 0x5DC; DS_8BD0 = 0xCE4; }
    else if (cm & 0x80)   { DS_7D82 = 0x7D0; DS_8BD0 = 0xAF0; }
    else if (cm & 0x20)   { DS_7D82 = 0x7D0; DS_8BD0 = 0xCE4; }
    else                  { DS_7D82 = 0x5DC; DS_8BD0 = 0xAF0; }
    cm = C[DS_7D30 + 4] & 0xE0;                    /* 4 segments ahead, for the opponent AI */
    /* same table into DS_70C0 (left) / DS_7D2E (right) */
    DS_7D3E = HGT[DS_7D30];
    if (!DS_0610 && !DS_0612 && (DS_7EC0 ? 0x3FC : 0x17C) >= DS_7D30) return;
    /* autopilot: demo, auto, or after the finish line */
    int target = 0x960;
    if (DS_7E90 > 0x960) target -= 0x140; else target += 0xA0;   /* lane away from the opponent */
    int d = DS_7D36 - target;
    if (abs(d) > 0x64) DS_7D36 = target + (target < DS_7D36 ? 0x64 : -0x64);
    else               DS_7D36 = target;
    int y = YAW[DS_7D30];
    if (abs(y - DS_7D3C) > 0) {
        DS_78E4 = (DS_7D3C - y) << 1;
        if (abs(DS_78E4) > 0x18) DS_78E4 = DS_78E4 > 0 ? 0x18 : -0x18;
    }
    DS_7D3C = y;
}

int road_edge_collision(void)                     /* 2645:206b — every timer tick (0000:238c) */
{
    DS_5E0E = 0;
    int l = DS_7D36 - 0x1E;                        /* car's left side  */
    int r = DS_7D36 + 0x46;                        /* car's right side */
    if (DS_7D82 - 0x104 >= l)  return 1;           /* far off the road */
    if (r >= DS_8BD0 + 0x104)  return 1;
    int s = DS_7D30;
    if (DS_7D82 > l) {                             /* on the left shoulder */
        if ((L[s] & 0x1000) || (L[s-1] & 0x1000)) return 1;    /* barricade */
        if (C[s+1] & 0xF000) return 1;                          /* banner post */
        DS_5E0E = 1;  return 0;
    }
    if (r > DS_8BD0) {                             /* on the right shoulder */
        if ((R[s] & 0x1000) || (R[s-1] & 0x1000)) return 1;
        if (C[s+1] & 0xF002) return 1;                          /* banner post or telephone pole */
        DS_5E0E = 1;  return 0;
    }
    return 0;
}

void road_state_init(void)                        /* 2645:185d */
{
    if (DS_8ACA) DS_7D30 = 0xC8;                   /* race start */
    else switch (DS_0284) { case 1: DS_7D30 = 0x0A; break; case 2: DS_7D30 = 0x41; break;
                            case 3: DS_7D30 = 0x73; break; }        /* other: unchanged */
    road_set_seg_ptrs(DS_7D30);
    DS_7D82 = 0x5DC; DS_8BD0 = 0xCE4; DS_7D32 = 0;
    DS_7D36 = 0xA00; DS_7D38 = 0x76C;
    DS_7D3A = 0; DS_7D3E = 0; DS_7D3C = 0; DS_7D34 = 0;
    DS_54D4 = 1;
    DS_8648 = (rnd(2) == 0);                       /* cmp ax,1 / sbb / neg */
    DS_7D7E = 1;
}

void road_race_init(void)                         /* 2645:2114 */
{ DS_5E12 = 0; DS_5E10 = 0; road_state_init(); road_first_frame(); }

void road_first_frame(void)                       /* 2645:527a */
{ DS_6E2A = 0; DS_7D7C = 0; road_pages_init(); road_frame(); }
```

### 4.7 Pages

```c
void road_pages_init(void)                        /* 2645:224e */
{
    if (DS_8236 == -2) {
        DS_787A = (DS_5E3E == 1) ? 0xA200 : 0xA400;
        show_page(DS_5E40);                        /* 21a0:182e: CRTC reg 0Ch = n<<5, 0Dh = 0, wait vsync */
    } else { DS_787A = DS_8188->seg /*+0x26*/; DS_767C = DS_8188->off /*+0x24*/; }
}
void road_page_flip(void)                         /* 2645:2288 */
{
    if (DS_8236 == -2) {
        swap(DS_5E3E, DS_5E40);
        DS_787A = (DS_5E3E == 1) ? 0xA200 : 0xA400;
        show_page(DS_5E40);                        /* the page just drawn becomes visible at the next retrace */
    } else {
        blit(DS_8188, DS_822A, &DS_5E42, 1);       /* copy the view to the screen */
        DS_787A = DS_8188->seg; DS_767C = DS_8188->off;
    }
}
```

### 4.8 Drawing a frame (`road_draw_frame`, 2645:4b8c)

The band/object code reads the row tables through near pointers set up here and decremented by one entry per row
(front: from row 14 down to 1; `p` = current row, `q` = row−1):

| pointer | front (start = row 14) | mirror (start = row 7) |
|---|---|---|
| `6E08`/`6E10` XL[p]/XL[q] | 7314 / 7312 | 7F30 / 7F2E |
| `6E18`/`6E1C` XR | 7AEA / 7AE8 | 8668 / 8666 |
| `6E1A`/`6E1E` Y | 78A0 / 789E | 8646 / 8644 |
| `6E0A`/`6E12` XOL | 7FA6 / 7FA4 | 70D0 / 70CE |
| `6E0E`/`6E16` XOR | 7FF4 / 7FF2 | 70E0 / 70DE |
| `6E0C`/`6E14` YE | 7ACC / 7ACA | 8658 / 8656 |
| `6E28` dash width | 5E86 (= `5E6A[14]`) | 5E92 (= `5E84[7]`) |
| `6E20`,`6E24`,`8B82` &L,&R,&C | `seg+14`, then −1 per row | **`seg-8`**, then +1 per row (row r reads flags of `seg-r-1`) |

```c
void road_draw_frame(void)                        /* 2645:4b8c */
{
    setup_front_pointers();                        /* table above */
    clip_set_view();
    road_draw_sky();
    fillmode_begin();
    for (int row = 14; row != 0; row--) {
        if (row <= DS_6E06) road_draw_row_front(row);      /* 3a3b */
        uint c = *DS_8B82;
        if (!(c & 4) && row < 11)
            draw_row_objects_front(row);                     /* §4.11 */
        if (DS_7E8A - DS_7D30 == row && DS_8ACA) { fillmode_end(); opponent_draw(); fillmode_begin(); }
        all 12 near pointers -= 2;  DS_6E20 -= 2; DS_6E24 -= 2; DS_8B82 -= 2;
    }
    if (DS_7E8A == DS_7D30 && DS_8ACA) { fillmode_end(); opponent_draw(); fillmode_begin(); }
    road_draw_mirror();                            /* 43fd */
    fillmode_end();
    road_draw_cockpit_overlay();                   /* 3e3c */
    if (DS_8ACA) road_draw_start_light();          /* 3dc3 */
    road_page_flip();
}

void road_draw_row_front(int row)                 /* 2645:3a3b */
{
    if (!DS_8ACA) { town_walls_front(/*left*/ ...); town_walls_front(/*right*/ ...); }  /* 39de, 3a0d */
    road_draw_band_front(row);
}
```

### 4.9 Sky, horizon, ground (`road_draw_sky`, 2645:2982)

```c
void road_draw_sky(void)                          /* 2645:2982 — clip is the view */
{
    Rect r;  int hy, a, b;                        /* a = [bp-0x18], b = [bp-0x1e] */
    r.dsty = 0x12;
    if (DS_8ACA) {
        hy = road_proj_y(0xF0, 0x7D0, DS_7D3E);  /* horizon line of the current height */
        r.h = hy - 0x1E;  r.srcy = 0x9A - r.h;   /* bottom of the sky picture (page-1 rows 102..153) */
        scroll(DS_7D3C << 1, &a, &b);            /* see below */
        r.w = b; r.srcx = 0; r.dstx = a;
    } else {
        r = (Rect){0x140, 0x1D, 0, 0x66, 0, 0x12};
    }
    blit(DS_8188, BACK, &r, 1);                   /* sky, part 1 */
    if (DS_8ACA) { r.w = a; r.srcx = b; r.dstx = 0; blit(DS_8188, BACK, &r, 1); }  /* part 2 (wrap) */
    /* horizon panorama (LIB2 #267 at page-1 rows 163..199), scrolls twice as fast */
    r.h = DS_8ACA ? 0x14 : 0x10;  r.srcy = 0xA3;
    if (DS_8ACA) { scroll(DS_7D3C << 2, &a, &b); r.w = b; r.srcx = 0; r.dstx = a; r.dsty = hy - 0x0C; }
    else         { r.w = 0x140; r.srcx = 0; r.dstx = 0; r.dsty = 0x2F; }
    blit_clip_y(&r);
    blit(DS_8188, BACK, &r, 1);
    DS_6E06 = 0x0E;
    if (DS_8ACA) {
        r.w = a; r.srcx = b; r.dstx = 0;
        blit(DS_8188, BACK, &r, 1);               /* (rect already y-clipped) */
        int top = hy + 8, idx = DS_6E06;
        for (int k = 14; k != 0; k--)             /* highest road row */
            if (row_y[k] < top) { top = row_y[k]; idx = k; }
        DS_6E06 = idx;  DS_6E04 = top;
        fill_rows(top, 9);                        /* ground colour 9 from `top` to row 0x64 */
    } else
        fill_rows(0x3F, 1);                       /* town: colour 1 from row 63 */
}

/* scroll(v): v is the 16-bit value 7D3C<<1 or <<2, computed from |7D3C| when 7D3C <= 0:
     if (DS_7D3C > 0) { m = (v % 0x140) & 0xFFF8; a = m; b = 0x140 - m; }
     else             { m = ((abs(DS_7D3C) << k) % 0x140) & 0xFFF8; b = m; a = 0x140 - m; }
   (the & only masks DL: identical to & 0xFFF8 for 0 <= m < 0x140). The picture's left part [0, b) goes to x = a,
   its right part [b, 320) to x = 0. */
```

`fill_rows(y, c)` fills rows `y … DS_5E52-1` (i.e. up to row 100, **not** row 101) over the whole 320-pixel width
(no x clip). Row 101 of the ground is covered by the road/cockpit overlays in practice.

### 4.10 Road bands

```c
int slope16(int xa, int ya, int xb, int yb)       /* 2645:2cfc */
{ return (yb == ya) ? 0 : ((xb - xa) << 4) / (yb - ya); }

void road_draw_band_front(int row)                /* 2645:2f48 (far) — band between rows p=row and q=row-1 */
{
    int xl = XL[p], xr = XR[p];
    int axl = xl << 4, axr = xr << 4;
    int y = Y[p], ybot = Y[q];
    int sl = slope16(XL[q], Y[q], xl, y), sr = slope16(XR[q], Y[q], xr, y);
    uint c = *DS_8B82;  int cross = c & 1;        /* "cross" = town/shoulder mode */
    int xol, xor_, axol, axor, sol, sor;
    if (cross) { xol = XOL[p]; xor_ = XOR[p]; axol = xol << 4; axor = xor_ << 4;
                 sol = slope16(XOL[q], Y[q], xol, y); sor = slope16(XOR[q], Y[q], xor_, y); }
    int w, dashx;                                  /* w only set when c & 8 */
    if (c & 8) { w = *DS_6E28; dashx = ((xl + xr) >> 1) - (w >> 1); }
    if (sign(ybot - y) < 0) return;                /* back-facing band (behind a crest) */
    int n = ybot - y;
    int G;                                         /* [bp-0x22]: NEVER initialised in the original, §8 */
    while (n != 0 && y <= 0x65) {
        if (!cross) {
            hline(xl, xr, y, (c & 0x10) ? 0x0F : 0x01);          /* asphalt, white finish band */
        } else {
            if (c & 4) return;                                   /* intersection: leave the ground colour */
            uint lf = *DS_6E20, rf = *DS_6E24;
            if (lf & 0x80) { int m = ((xl - xol) >> 1) + xol;
                             hline(xol, m, y, 4); hline(m, xl, y, 9);
                             if (n == G && row < 6) hline(xol, m, y, 2); }
            else           { hline(xol, xl, y, 4);
                             if (n == G && row < 6) hline(xol, xl, y, 2); }
            if (rf & 0x80) { int m = xor_ - ((xor_ - xr) >> 1);
                             hline(m, xor_, y, 4); hline(xr, m, y, 9);
                             if (n == G && row < 6) hline(m, xor_, y, 2); }
            else           { hline(xr, xor_, y, 4);
                             if (n == G && row < 6) hline(xr, xor_, y, 2); }
        }
        if (c & 8) hline(dashx, dashx + w, y, 4);                /* centre dash */
        axl += sl; axr += sr; xl = axl >> 4; xr = axr >> 4;
        if (cross) { axol += sol; axor += sor; xol = axol >> 4; xor_ = axor >> 4; }
        dashx = ((xl + xr) >> 1) - (w >> 1);
        n--; y++;
    }
}

void road_draw_band_mirror(void)                  /* 2645:324d (far, no argument) */
{
    /* same set-up as the front (mirror pointers, w = *DS_6E28 = 0) */
    if (sign(ybot - y) < 0) return;
    int n = ybot - y;  if (n == 0) return;
    int half = w >> 1;                             /* uninitialised when !(c & 8); only used for dashx */
    for (;;) {
        if (y > 0x2A) break;
        if (!cross) {
            hline(0xF2, xl, y, 9);  hline(xr, 0x13D, y, 9);      /* grass is drawn explicitly */
            hline(xl, xr, y, 1);                                  /* no white finish band in the mirror */
        } else {
            if (c & 4) break;
            /* shoulders exactly as the front, without the colour-2 extra lines */
        }
        if (c & 8) hline(dashx, dashx + w, y, 4);
        advance xl/xr (and xol/xor if cross) as the front; dashx = ((xl + xr) >> 1) - half;
        n--; y++;
        if (n == 0) break;
    }
}
```

Colour indices (default EGA palette per FORMATS.md, see §8): 1 asphalt (and the town ground fill), 9 ground/grass
and kerb strip, 4 sidewalk/shoulder and centre line, 0x0F white finish band, 2 wall shadow lines.

### 4.11 Road-side objects (front rows 1…10, mirror rows 1…5)

`scenery_draw_shape(x, y, depth, side, type)` (3aeb): `size = 9 - depth; if (size >= 7) size = 6; if (size < 0) size = 0;
if (y > 0x65) y = 0x65; shape_draw(x, y, size, set->nsizes, set->sizes, side)` with `set = DS_6078[type]`.
`scenery_draw_hydrant(x, y, depth, side)` (3b3a): size clamped 0…7, set `DS_607C`, **no** y clamp.
Front: `depth = row` (row 1…3 → size 6 = largest, row 9…10 → 0). Mirror: `depth = row + 5`.

Front row objects, in drawing order (`p` = current row; `cross = C&1`; L, R, C of the row; `Y`, `XL`, … of row p):

| # | condition | call |
|---|---|---|
| 1 | `L&0x20` | shape(cross ? XL : XOL, Y, row, −1, 0 palm) |
| 2 | `L&0x100` | shape(cross ? XL : (L&0x200 ? 2·XOL−XL : XOL), Y, row, −1, 4 shrub) |
| 3 | `L&0x800` | shape(XL, Y, row, +1, 3 "road work ahead") |
| 4 | `L&0x1000` | shape(XL, Y, row, +1, 6 barricade) |
| 5 | `L&1` | cross: fillmode_end; sign_picture(XL, (YE+Y)>>1, row, align 1, xclip 0, kind 1); fillmode_begin — else shape(XL, Y, row, +1, 1 post) |
| 6 | cross: `L&0x40` / `R&0x40` | hydrant(XL, Y, row, +1) / hydrant(XR, Y, row, −1); then go to 13 |
| 7 | not cross: `L&4`, `L&8` | shape(XL, Y, row, −1, 7 small bush) / (…, 8 bush) |
| 8 | `L&0x40` / `L&0x80` | shape(XOL − ((XL−XOL)>>1), Y, row, −1, 0x0A tree) / shape(XOL, …, −1, 0x0B big tree) |
| 9 | `R&4`, `R&8` | shape(XR, Y, row, +1, 7) / (…, 8) |
| 10 | `R&0x40` / `R&0x80` | shape(XOR, Y, row, +1, 0x0A) / shape(XOR + ((XOR−XR)>>1), Y, row, +1, 0x0B) |
| 11 | `C&2` | shape(XR, Y, row, +1, 9 telephone pole) |
| 13 | `R&0x20` | shape(cross ? XR : XOR, Y, row, +1, 0 palm) |
| 14 | `R&0x100` | shape(cross ? XR : (R&0x200 ? 2·XOR−XR : XOR), Y, row, +1, 4) |
| 15 | `R&0x2000`, `R&0x4000` | shape(XR, Y, row, +1, 0x12) / (…, 0x13) winding-road signs |
| 16 | `R&0x800`, `R&0x1000` | shape(XR, Y, row, +1, 3) / shape(XR, Y, row, **−1**, 6) |
| 17 | `R&1` | cross: sign_picture(XR, (YE+Y)>>1, row, align −1, 0, kind 1) (in fillmode_end/begin) — else shape(XR, Y, row, +1, 1) |
| 18 | `C&0xF000` | banner(XL, XR, Y − 4·(Y−YE), Y, row, xclip 0, C & 0xF000) |
| 19 | `C&0x100`, `C&0x800`, `C&0x200`, `C&0x400` | shape(XR, Y, row, +1, 5 speed limit / 0x0F road narrows / 0x10 chevron `<` / 0x11 chevron `>`) |

Mirror (`43fd`), per row: band (`3ab4`); if `C&4` skip to the opponent test; if `C&0xF000`:
banner(XL, XR, Y − 4·(Y−YE), Y, row+5, xclip **1**, C & 0xF000); if `row >= 6` skip; then the same list with
`depth = row + 5` except: no picture signs (items 5 and 17 become posts only when not cross; when cross nothing),
the cross test for hydrants comes first (item 6 then jump to 13), item 18 is not repeated, item 19 order is
`0x100, 0x200, 0x400, 0x800`. Exact mirror order: 1, 2, 3, 4, [cross: 6 → 13] else: L&1 post, 7, 8,
R&1 post, 9, 10, 11; 13, 14, 15, 16, 19.

```c
void shape_draw(int x, int y, int size, int nsizes, ShapeSize *sizes, int side)   /* 2645:2c0a (far) */
{
    if (nsizes <= size) return;
    ShapeSize s = sizes[size];                     /* {int nlines; HLine far *lines} */
    if (s.nlines <= 0) return;
    for (int i = 0; i < s.nlines; i++) {
        HLine h = s.lines[i];                      /* 4 signed bytes: dx, dy, w, colour */
        if (side == -1) { int xr = x - h.dx; hline(xr - h.w, xr, y + h.dy, h.colour); }
        else            { int xl = x + h.dx; hline(xl, xl + h.w, y + h.dy, h.colour); }
    }
}
```

The sprite is anchored at its base (dy ≤ 0 goes up); side −1 mirrors it horizontally around x.

```c
void scenery_draw_sign_picture(int x, int y, int depth, int align, int xclip, int kind)   /* 2645:3b79 */
{
    int size = 9 - depth;  if (size > 6) size = 6;  if (size < 0) size = 0;
    Rect r;  r.srcx = DS_5E9C[size].srcx; r.w = DS_5E9C[size].w; r.h = DS_5E9C[size].h;
    switch (kind) { case 1: r.srcy = 0x66; break;               /* page 2 rows 102.. */
                    case 2: r.srcy = (DS_8236 == -2 ? 0x63 : 0) + 1; break;     /* page 0: Vegas */
                    case 3: r.srcy = (DS_8236 == -2 ? 0x63 : 0) + 0x2C; break;  /* page 0: Block Out */
                    /* other: r.srcy uninitialised (never happens) */ }
    r.dstx = (align == 1) ? x - r.w : (align == 0) ? x - (r.w >> 1) : x;
    r.dsty = (align == 0) ? y - (r.h >> 1) : y - r.h;
    if (xclip) blit_clip_xy(&r); else blit_clip_y(&r);
    if (kind == 1) blit(DS_82B4, BACK, &r, 1);
    else           blit(DS_8236 == -2 ? DS_7678 : DS_82B4, BACK, &r, 1);
}

void scenery_draw_banner(int xl, int xr, int ytop, int ybot, int depth, int xclip, uint flags)  /* 2645:3ccc */
{
    int n = *DS_6E28 + 1;                          /* post thickness = dash width + 1 */
    for (int si = n; si != 0; si--) {              /* (n is never 0: widths are >= 0) */
        fillmode_end();
        road_vline(0x0A, xl - si, ytop, ybot);     /* left post  */
        road_vline(0x0A, xr + si, ytop, ybot);     /* right post */
        fillmode_begin();
        hline(xl, xr, ytop + si - 1, 0x0A);        /* cross bar, n rows */
    }
    int mid = (xl + xr) >> 1;
    if (flags & 0x1000)       scenery_draw_shape(mid, ytop, depth, 1, 0x0D);          /* "CITY LIMITS" */
    else if (flags & 0x2000)  { fillmode_end(); scenery_draw_sign_picture(mid, ytop, depth, 0, xclip, 1); fillmode_begin(); }
    else if (*DS_8B82 & 1)    { fillmode_end();                                        /* town billboards */
                                scenery_draw_sign_picture(mid, ytop, depth, 0, xclip, (flags & 0x4000) ? 2 : 3);
                                fillmode_begin(); }
    else scenery_draw_shape(mid, ytop, depth, 1, (flags & 0x4000) ? 0x0C : 0x0E);     /* route arrows */
}

void road_vline(int colour, int x, int y1, int y2)       /* 2645:2381 */
{
    if (y2 < DS_5E50 || y1 > DS_5E52 || x < DS_5E4E || x > DS_5E54) return;
    y1 = clamp(y1, DS_5E50, DS_5E52);  y2 = clamp(y2, DS_5E50, DS_5E52);   /* max then min */
    line(x, y1, x, y2, colour);                   /* [DS:78D2] */
}
```

Blit rectangle clippers (`Rect = {w,h,srcx,srcy,dstx,dsty}`):

```c
void blit_clip_y(Rect *r)                         /* 2645:22fc */
{
    if (r->dsty < DS_5E50) { int d = DS_5E50 - r->dsty; r->h -= d; r->srcy += d; r->dsty = DS_5E50; }
    int e = r->dsty + r->h;
    if (DS_5E52 < e) r->h -= e - DS_5E52;          /* note: bottom = ymax exclusive (h ends at ymax) */
}
void blit_clip_xy(Rect *r)                        /* 2645:2331 */
{
    blit_clip_y(r);                                /* same code inline */
    if (r->dstx < DS_5E4E) { int d = DS_5E4E - r->dstx; r->w -= d; r->srcx += d + 7; r->dstx = DS_5E4E; }
}
```

(The `+7` is original; the planar blitter works on byte columns. The right side is never clipped here: the driver
blitter must cope with rectangles that run past x 319 — video spec.)

### 4.12 Town walls (`town_walls_front` 2645:34da, `quad_fill` 2645:2d20)

Called only in the town (`!DS_8ACA`), before the road band of the same row. Left call (`39de`):
`a = XOL[q], b = XOL[p], c = Y[q], d = Y[p], e = YE[p], f = YE[q], left = 1`. Right call (`3a0d`):
`a = XOR[p], b = XOR[q], c = Y[p], d = Y[q], e = YE[q], f = YE[p], left = 0` (p = row, q = row−1).

```c
void town_walls_front(int a, int b, int c, int d, int e, int f, int left)
{
    uint F   = left ? *DS_6E20 : *DS_6E24;         /* side flags of the row */
    uint Fq  = (left ? DS_6E20 : DS_6E24)[-1];     /* side flags of row-1 (nearer) */
    uint Cr  = *DS_8B82;
    if (Cr & 4) return;                            /* the cross street itself: nothing */
    quad_fill((F & 0x10) ? 2 : 3, a, b, c, d, e, f);                 /* façade: ground (Y) up to eye height (YE) */
    if (F & 4) {                                                      /* upper storey */
        int t0, t1;
        if (F & 8) { t0 = 2*f - c; t1 = 2*e - d; }                    /* as tall again */
        else       { t0 = f - ((c - f) >> 1); t1 = e - ((d - e) >> 1); }   /* half as tall */
        quad_fill(4, a, b, f, e, t1, t0);
    }
    if (DS_8B82[-1] & 4) {                         /* the next nearer row is a cross street: side wall of the block */
        if (left) { hline(DS_5E4E, a, c - 1, 4); for (int y = f; y < c - 1; y++) hline(DS_5E4E, a, y, 2); }
        else      { hline(b, DS_5E54, d - 1, 4); for (int y = e; y < d - 1; y++) hline(b, DS_5E54, y, 2); }
    }
    if (!(F & 4) || (Fq & 4)) return;              /* end of an upper storey: its side face */
    int t = (F & 8) ? (left ? 2*f - c : 2*e - d)
                    : (left ? f - ((c - f) >> 1) : e - ((d - e) >> 1));
    if (left) { for (int y = t; y < f; y++) hline(2*a - b, a - 1, y, 2); }
    else      { for (int y = t; y < e; y++) hline(b + 1, 2*b - a, y, 2); }
}
```

`town_walls_mirror` (37af), mirror args built the same way from the mirror tables (`3a57` left, `3a86` right):
returns at once unless `C&1`; `Fn` = side flags of the **next** entry (`ptr[+1]`, the mirror walks the other way) and
`C[+1]&4` for the cross-street test; the storey height does **not** depend on `F&8`: left storeys always use the
"half" formula, right storeys always the "double" formula (both for the quad and for the side face). Otherwise identical.

```c
void quad_fill(int c, int x0, int x1, int y0, int y1, int y2, int y3)   /* 2645:2d20 */
/* Quad with vertical sides: left side x0 from y3 (top) to y0 (bottom), right side x1 from y2 to y1.
   Locals: xa [bp-0xe], xb [bp-0x10], accA [bp-0x12], accB [bp-0x14] (12.4 fixed point). */
{
    if (y0 == y1 && y3 == y2) { for (int y = y3; y <= y1; y++) hline(x0, x1, y, c); return; }
    int top = min(y3, y2), bot = max(y0, y1);
    int mode, xa, xb, accA, accB;
    if (y3 < y2 && y0 > y1)      { mode = 2; xa = x0; accB = x0 << 4; }
    else if (y2 < y3 && y1 > y0) { mode = 3; xb = x1; xa = x1 << 4; /* accA NOT initialised: bug, §8 */ }
    else if (y3 <= y2 && y0 <= y1) { mode = 4; accB = accA = x0 << 4; }
    else                         { mode = 5; accB = accA = x1 << 4; }
    int sTop = slope16(x0, y3, x1, y2), sBot = slope16(x0, y0, x1, y1);
    for (int y = top; y <= bot && y <= DS_5E52; y++) {
        switch (mode) {
        case 2: if (y < y2)        { accB += sTop; xb = accB >> 4; }
                else if (y <= y1)  xb = x1;
                else               { accB += sBot; xb = accB >> 4; }
                break;
        case 3: if (y < y3)        { accA += sTop; xa = accA >> 4; }
                else if (y <= y0)  xa = x0;
                else               { accA += sBot; xa = accA >> 4; }
                break;
        case 4: if (y <= y0) xa = x0; else { accA += sBot; xa = accA >> 4; }
                if (y >= y2) xb = x1; else { accB += sTop; xb = accB >> 4; }
                break;
        case 5: if (y < y3) { accA += sTop; xa = accA >> 4; } else xa = x0;
                if (y <= y1) xb = x1; else { accB += sBot; xb = accB >> 4; }
                break;
        }
        if (xa > xb) hline(xb, xa, y, c); else hline(xa, xb, y, c);
    }
}
```

### 4.13 Opponent car (`opponent_draw`, 2645:3f8d)

Distance `d` in sub-steps (1/8 segment), lateral offset `ox`:

```c
void opponent_draw(void)                          /* 2645:3f8d — called only when the opponent is in a drawn row */
{
    int oseg = DS_7E8A;
    int d  = ((oseg - DS_7D30) << 3) + DS_7E8C - DS_7D32;
    int ox = DS_7E90 - DS_7D36;
    int s;
    if (d >= 0x78)       s = -1;
    else if (d >= 0x10)  { s = ((d + 8) >> 4) + 2; if (s > 8) s = 8; }      /* 3..8: shrinking rear views */
    else if (d < -7)     s = ((0x10 - d) >> 4) + 8;                          /* 9..12: front views (mirror) */
    else if (d <= 8)     {                                                   /* -7..8: level with the player */
        if (abs(ox) < 0xB4) s = (DS_7D7C <= 8) ? 2 : 9;
        else                s = (ox < 0) ? 0 : 2;                            /* 0 = side view, car on the left */
    } else               s = (ox < -0xB4) ? 1 : 3;                           /* 9..15: 1 = rear 3/4 on the left */

    if (DS_8ACC >= 4) {                                                      /* sprite hysteresis */
        int p = DS_7D7C;
        if (s == p) goto chosen;
        if (s < 0)  { if (p == 8) return; s = p + 1; goto chosen; }          /* return: no state update */
        if (s > 12) { if (p == 12) return; s = p + 1; goto chosen; }
        if (s == DS_6E2A) goto chosen;
        if ((s <= 8 && p > 9) || (s >= 9 && p <= 8 && p > 0)) { s = p - 1; goto chosen; }
        if (p == 0)      s = (s < 9) ? 1 : 9;
        else if (p == 9) s = (s >= 9) ? 10 : (ox < 0 ? 0 : 2);
        else if (p == 1) s = (s < 1) ? 0 : 3;
        else if (p == 3 && s == 1) ;                                         /* keep 1 */
        else             s = (s > p) ? p + 1 : p - 1;
    }
chosen:
    DS_8BC2 = (s == 0 || (s == 2 && d <= 10));                               /* alongside */
    DS_72F2 = (s == 2 && d <= 10 && abs(ox) < 0xB4
               && abs(ox) < (DS_8ACC < 4 ? 0x64 : 0xB4) && DS_7E8E < DS_7D34);   /* rear-end hit */
    DS_6E2A = DS_7D7C;  DS_7D7C = s;
    DS_5EC6 = (d > 0x10) ? ((d + 8) & 0x0F) : 0;
    int z = d;
    if (s == 0) z += 0x10;
    if (z < -2 && s == 2) z += 8;
    int dy = DS_7D3E - HGT[oseg], dx = DS_7D3C - YAW[oseg];
    int sx, sy;
    if (s < 9) { z += 4; sx = road_proj_x(z, DS_7E90, dx); sy = road_proj_y(z, 0x7D0, dy); }
    else       { z = -z; sx = road_proj_x_mirror(z, DS_7E90, -dx); sy = road_proj_y_mirror(z, 0x7D0, -dy); }
    Pic far *pic = DS_8ACE[s];                    /* s = -1 would read DS:8ACA as a pointer: cannot happen, see §8 */
    Rect r = { pic->w, pic->h, 0, 0, 0, 0 };
    if (s == 0)      { r.srcx = pic->w - sx + 0x40; if (r.srcx < 0) r.srcx = 0; r.dstx = 0; r.dsty = sy; }
    else if (s == 1) { r.dstx = sx - pic->w; r.dsty = sy - pic->h + 0x11; }
    else {
        r.dstx = sx - (pic->w >> 1);
        int b = (s < 9) ? max(sy, DS_6E04) : sy;  /* never above the ground line */
        r.dsty = b - pic->h;
        if (s == 2 && r.dsty + pic->h >= 0x65) r.dsty = 0x65 - pic->h;
    }
    int switched = 0;
    if (s >= 9) { if (DS_5E4E == 0) { switched = 1; clip_set_mirror(); } }
    else        { if (DS_5E4E != 0) { switched = 1; clip_set_view(); } }
    if (r.dstx + pic->w < DS_5E4E || r.dstx >= DS_5E54) return;             /* clip NOT restored, §8 */
    if (s >= 9) blit_clip_xy(&r); else blit_clip_y(&r);
    blit_masked(pic, BACK, &r, 1);
    if (switched) { if (DS_5E4E == 0) clip_set_mirror(); else clip_set_view(); }
}
```

Opponent sprites (`DS:8ACE[13]`, built by `road_load_graphics`): 0 = side view (close, on the left), 1 = rear 3/4
view (on the left), 2 = straight rear, 3/4/5 = rear shrunk with masks 0x57/0x25/0x22 (5/8, 3/8, 2/8), 6/7/8 = sprite 5
shrunk again with 0xF7/0x75/0x52 (7/8, 5/8, 3/8 of 2/8), 9 = front view (mirror), 10/11/12 = front shrunk with
0x75/0x54/0x21. The police car (drawn by the race code, not here) uses the same scheme with `DS:7526`.

### 4.14 Rear-view mirror (`road_draw_mirror`, 2645:43fd), background, overlays

```c
void road_draw_mirror(void)                       /* 2645:43fd — called in fill mode */
{
    setup_mirror_pointers();                       /* §4.8 table; flag ptrs = &arr[seg-8] */
    fillmode_end();  mirror_draw_background();  fillmode_begin();
    clip_set_mirror();                             /* x 0xF2..0x13D, y 0x16..0x2A */
    for (int row = 7; row != 0; row--) {
        road_draw_row_mirror();                    /* 3ab4: walls (37af, town only via C&1) + band 324d */
        uint c = *DS_8B82;
        if (!(c & 4)) {
            if (c & 0xF000) banner(XL, XR, Y - 4*(Y - YE), Y, row + 5, 1, c & 0xF000);
            if (row < 6) draw_row_objects_mirror(row);          /* §4.11 */
        }
        if (DS_7D30 - DS_7E8A == row && DS_8ACA) { fillmode_end(); opponent_draw(); fillmode_begin(); }
        near pointers -= 2;  DS_6E20 += 2; DS_6E24 += 2; DS_8B82 += 2;
    }
    clip_set_view();
    road_vline(2, 0xF0,  0x17, 0x2A);              /* mirror frame */
    road_vline(4, 0xF1,  0x17, 0x2A);
    road_vline(4, 0x13E, 0x17, 0x2A);
    road_vline(2, 0x13F, 0x17, 0x2A);
    Rect r = { 0x20, DS_8184->h - 6, 0xF8, 0x16, 8, (DS_8236 == -2 ? 0 : 0x64) + 3 };
    blit(BACK, DS_7678, &r, 1);                    /* copy the mirror centre into page 0 at (8,3) (still in fill mode) */
}

void mirror_draw_background(void)                 /* 2645:3edb */
{
    Rect r = { DS_8184->w, DS_8184->h, 0, 0, 0xF0, 0x12 };  blit(DS_8184, BACK, &r, 1);   /* LIB2 #9 */
    r = (Rect){ DS_8B7E->w, 2, 0, 0, 0xF0, DS_8646 - 2 };    blit_clip_y(&r);
    blit(DS_8B7E, BACK, &r, 1);                                                           /* LIB2 #243, 2 rows */
}

void road_draw_cockpit_overlay(void)              /* 2645:3e3c */
{
    Rect r = { DS_72F4->w, DS_72F4->h, 0, 0, 0, 0x12 };    blit_masked(DS_72F4, BACK, &r, 1);  /* #6 A-pillar */
    r = (Rect){ DS_8BCA->w, DS_8BCA->h, 0, 0, 0x58, 0x5F }; blit_masked(DS_8BCA, BACK, &r, 1);  /* #7 hood line */
}

void road_draw_start_light(void)                  /* 2645:3dc3 — races only */
{
    if (DS_7AAE <= 0) return;
    Rect r = { 0x18, DS_7AAE, 0x18, 0x20 - DS_7AAE, 0x82, 0x12 };
    if (DS_7D66) { DS_8228 = 1; r.srcx = 0; }      /* other half of LIB2 #13 */
    blit_masked(DS_6E60, BACK, &r, 1);
}
```

### 4.15 Loading the drive-screen graphics (`road_load_graphics`, 2645:2429)

`load(id, mode)` = `0f38:683e`, returns a far picture; mode 2 allocations are released by `0f38:a0b8(1)` (heap
mark stack, platform spec). Rect order `{w,h,srcx,srcy,dstx,dsty}`.

```c
void road_load_graphics(void)                     /* 2645:2429, once per drive screen from 0f38:7b22 */
{
    heap_mark_reset();                                                    /* 0f38:a054 */
    Pic *p = load(1005, 2);                                               /* LIB2 #5 sky 320x52 */
    blit(p, DS_8188, &(Rect){p->w, p->h, 0, 0, 0, 0x66}, 1);  heap_release(1);
    int loc = DS_8ACA ? 0 : DS_0284;
    p = load(DS_5E62[loc], 2);                                            /* sign sheet 264x43 */
    blit(p, DS_82B4, &(Rect){p->w, p->h, 0, 0, 0, 0x66}, 1);  heap_release(1);
    if (!DS_8ACA) {                                                       /* town billboards → page 0 */
        Pic *pg = (DS_8236 == -2) ? DS_7678 : DS_82B4;
        p = load(1261, 2); blit(p, pg, &(Rect){p->w, p->h, 0, 0, 0, (DS_8236 == -2 ? 0x63 : 0) + 1}, 1);    heap_release(1);
        p = load(1262, 2); blit(p, pg, &(Rect){p->w, p->h, 0, 0, 0, (DS_8236 == -2 ? 0x63 : 0) + 0x2C}, 1); heap_release(1);
    }
    DS_767E = load(1267, 1);                                              /* horizon panorama 320x37 */
    if (!DS_8ACA) {
        DS_764A = load(1008, 1);                                          /* 184x4 strip */
        blit_masked(DS_764A, DS_767E, &(Rect){DS_764A->w, DS_764A->h, 0, 0, 0x2C, 0x0C}, 1);
        heap_release_other(1);                                            /* 0f38:a09e(1) */
    }
    blit(DS_767E, DS_8188, &(Rect){DS_767E->w, DS_767E->h, 0, 0, 0, 0xC8 - DS_767E->h}, 1);
    heap_release_other(1);
    DS_72F4 = load(1006, 1);  DS_8BCA = load(1007, 1);                    /* A-pillar, hood line */
    DS_8184 = load(1009, 1);  DS_8B7E = load(1243, 1);                    /* mirror, mirror sky */
    if (!DS_8ACA) return;
    int cls = (s8)*(u8*)(0x8DF0 + 10*DS_7648);                            /* opponent class 0..4 */
    DS_6E60 = load(1013, 1);  DS_7AAE = DS_6E60->h;                       /* start light, 32 */
    DS_8ACE[0] = load(DS_5E14[cls*4 + 0], 1);                             /* side view      */
    DS_8ACE[1] = load(DS_5E14[cls*4 + 1], 1);                             /* rear 3/4       */
    DS_8ACE[2] = load(DS_5E14[cls*4 + 2], 2);                             /* rear (temporary heap) */
    shrink = (DS_8236 == -2) ? pic_shrink_planar : (DS_8236 == -6) ? pic_shrink_tandy : pic_shrink_cga;
    for (k = 0; k < 3; k++) DS_8ACE[3+k] = shrink(DS_8ACE[2], DS_5E56[k], DS_5E56[k]);
    for (k = 0; k < 3; k++) DS_8ACE[6+k] = shrink(DS_8ACE[5], DS_5E5A[k], DS_5E5A[k]);
    DS_8ACE[9] = load(DS_5E14[cls*4 + 3], 1);                             /* front view     */
    for (k = 0; k < 3; k++) DS_8ACE[10+k] = shrink(DS_8ACE[9], DS_5E5E[k], DS_5E5E[k]);
    /* police sprites (used by the race code) */
    DS_7526[0] = DS_8ACE[0];
    DS_7526[1] = load(1241, 1);
    DS_7526[2] = load(1242, 2);
    DS_7526[3] = DS_7526[2] = shrink(DS_7526[2], DS_5E56[0], DS_5E56[0]);
    heap_release(1);
    DS_7526[4] = DS_7526[5] = DS_7526[6] = DS_7526[3];
    DS_7526[9] = load(1246, 1);
    for (k = 0; k < 3; k++) DS_7526[10+k] = shrink(DS_7526[9], DS_5E5E[k], DS_5E5E[k]);
}
```

(The rear picture `DS:8ACE[2]` is loaded with mode 2 and is never explicitly released here; see §8.)

### 4.16 Picture shrinking (`pic_shrink_planar`, 2645:5407; `pic_alloc_shrunk`, 52b7)

```c
Pic far *pic_alloc_shrunk(Pic far *src, u8 ymask, u8 xmask)   /* 2645:52b7 */
{
    int w = src->w, h = src->h;
    int nw = ((w & ~7) * popcount8(xmask)) >> 3;
    for (int i = 0, bit = 0x80; i < (w & 7); i++, bit >>= 1) if (xmask & bit) nw++;
    nw = (nw + ~DS_823E) & DS_823E;                               /* round up to the byte (DS:823E = alignment mask) */
    int nh = ((h & ~7) * popcount8(ymask)) >> 3;
    for (int i = 0, bit = 0x80; i < (h & 7); i++, bit >>= 1) if (ymask & bit) nh++;
    Pic far *d = pic_alloc(nw, nh, (src->mask != 0), 1);          /* 0f38:9fce */
    fmemset(d->data, 0x00, d->plane_bytes);                      /* +4 */
    fmemset(d->mask, 0xFF, d->plane_bytes / DS_8238);
    return d;
}

Pic far *pic_shrink_planar(Pic far *src, u8 ymask, u8 xmask)  /* 2645:5407 — VGA/EGA */
{
    Pic far *d = pic_alloc_shrunk(src, ymask, xmask);
    int k = popcount8(xmask);
    u8 far *sp = src->data;                                       /* planes stored one after the other */
    for (int plane = 0; plane < 4; plane++) {
        u8 far *dp = d->data + plane * d->h * d->bpr;
        u8 rowbit = 0x80;
        for (int y = 0; y < src->h; y++) {
            if (ymask & rowbit) {
                int sh = 0;
                for (int b = 0; b < src->bpr; b++) {
                    u8 v = *sp++, m = xmask, out = 0, ob = 0x80;
                    for (int j = 0; j < 8; j++) {                  /* compaction of the kept bits, MSB first */
                        if (m & ob) { out |= v & ob; ob >>= 1; }
                        else        { v <<= 1; m <<= 1; }
                    }
                    *dp |= out >> sh;
                    sh += k;
                    if (sh > 8) { dp++; *dp = out << (k - sh + 8); sh &= 7; }
                    if (sh == 8) { dp++; sh = 0; }
                }
                if (sh != 0) dp++;                                 /* rows are byte padded */
            } else sp += src->bpr;
            rowbit >>= 1; if (rowbit == 0) rowbit = 0x80;
        }
    }
    if (src->mask) build_mask(d->data, d->mask, d->plane_bytes);  /* 2634:00c1 */
    return d;
}
```

The same row/column masks are always passed for x and y, so a mask with `n` set bits scales by `n/8` in both
directions (bit `0x80 >> (i & 7)` keeps row/column `i`). `5670` (CGA, 2 bits/pixel via `55f8`) and `5885` (Tandy)
are the parked equivalents.

### 4.17 VGA driver slots in this segment

```c
void ega_hline(int x1, int x2, int y, int c)      /* 2645:0034 = [DS:78D6] */
{
    if (y < DS_5E50 || y > DS_5E52) return;
    if (DS_5E4E > x2 || DS_5E54 < x1) return;      /* tested before the swap */
    if (x1 <= DS_5E4E) x1 = DS_5E4E;
    if (x2 >= DS_5E54) x2 = DS_5E54;
    if (x1 > x2) swap(x1, x2);
    set pixels x1..x2 (inclusive) of row y in the page at segment DS_787A to colour c;
}
void ega_fill_rows(int y, int c)                  /* 2645:00e7 = [DS:78DA] */
{
    if (y > DS_5E52) return;
    set every pixel of rows y .. DS_5E52-1 (all 320 columns) of the page at DS_787A to colour c;
}
```

On the hardware both write through the set/reset register (colour in GC reg 0, enable 0x0F, function from
`DS:5C1F` = 0 = replace); the fill does a `rep movsb` of the rows onto themselves (latch read + set/reset write).

-----------------------------------------------------------------------------------------------

## 5. File formats / in-exe tables

All tables are initialised data in DGROUP (image offset `0x3E960 + DS`) or in the far data segments `2fa6`/`3324`
(image offset `seg*16 + off`). Dumped with python from `work/SR_unp.exe`.

### 5.1 Course data

`DS:5CBC` — 10 layouts × 12 feature codes (`DS:5CBA` = 12). Codes: 1 curve left, 2 curve right, 3 hill, 4 dip,
5 long hills, 6 long dips, 7 road works right lane, 8 road works left side, 9 narrow bridge, 10 S-bend (left first),
11 S-bend (right first).

| layout | features |
|---|---|
| 0 | 2 3 1 7 4 10 5 8 6 2 9 1 |
| 1 | 11 4 8 1 6 9 2 5 7 2 3 1 |
| 2 | 1 5 1 3 8 11 6 7 2 4 9 1 |
| 3 | 3 1 4 9 2 6 8 1 3 8 10 2 |
| 4 | 10 9 5 1 6 2 3 7 4 2 8 2 |
| 5 | 2 3 8 10 5 11 4 7 1 6 9 2 |
| 6 | 1 5 9 1 4 2 3 10 8 4 1 9 |
| 7 | 2 4 7 11 2 5 8 4 1 7 6 2 |
| 8 | 2 6 8 10 6 1 3 7 5 1 4 9 |
| 9 | 1 3 1 6 1 4 9 10 6 8 2 3 |

Height profiles (s16): `DS:5C20[22]` = 1 2 3 4 4 4 4 3 2 1 0 0 −1 −2 −3 −4 −4 −4 −4 −3 −2 −1;
`DS:5C4C[38]` = 1 2 3 4 4 4 4 3 2 1 0 0 −1 −2 −3 −4 −5 −5 −5 −5 −5 −5 −4 −3 −2 −1 0 0 1 2 3 4 4 4 4 3 2 1;
`DS:5C98[17]` = 1 2 3 3 3 2 1 0 0 0 −1 −2 −3 −3 −3 −2 −1. One unit = 4 screen pixels of vertical shift.

### 5.2 Segment flag bits

`C[i]` (centre, `389b:4E30`):

| bit | meaning | set by | used by |
|---|---|---|---|
| 0x0001 | town/"shoulder" segment: shoulders and sidewalks drawn, road area left in the ground colour, town walls, eye-height row computed | 0d48 (town) | 2f48, 324d, 34da/37af, objects (cross), 3ccc, 18fb/1bcc |
| 0x0002 | telephone pole on the right (shape 9); collision on the right shoulder | 0d48 | objects, 206b |
| 0x0004 | intersection (cross street): band skipped, no objects | 0d48 | 2f48, 324d, 34da/37af, 4b8c/43fd |
| 0x0008 | centre dash on this segment (every 4th/8th; every segment in curves = solid line) | 0d48, 0b91, ramps | 2f48, 324d |
| 0x0010 | white band across the road (finish line) | 0d48 | 2f48 |
| 0x0020 | left side closed: left edge 0x7D0 | feature 8 | 18fb, 1bcc, 1efc |
| 0x0040 | right lane closed: right edge 0xAF0 | feature 7 | same |
| 0x0080 | one lane (bridge): 0x7D0…0xAF0 (priority 0x80 > 0x20 > 0x40) | feature 9 | same |
| 0x0100 | speed-limit sign (shape 5) right | 0d48 (seg 0x181) | objects |
| 0x0200 / 0x0400 | chevron `<` / `>` sign (shapes 0x10 / 0x11) right | features 1 / 2 | objects |
| 0x0800 | "road narrows" sign (shape 0x0F) right | feature 9 | objects |
| 0x1000 | overhead banner "CITY LIMITS" (shape 0x0D) | 0d48 (0x17B) | 3ccc, 206b, eye row |
| 0x2000 | overhead banner with the location sign sheet picture (#258 in races) | 0d48 (0x3FB) | 3ccc |
| 0x4000 | town: billboard picture "Vegas Gambler" (#261) on posts; race: route 48 arrow (shape 0x0C) | 0d48 | 3ccc |
| 0x8000 | town: billboard "Play Block Out" (#262); race: route 69 arrow (shape 0x0E) | 0d48 | 3ccc |

`L[i]` / `R[i]` (sides, `389b:33F0` / `389b:3CB0`):

| bit | race road (cross = 0) | town (cross = 1) |
|---|---|---|
| 0x0001 | edge post (shape 1) at the road edge | location sign picture (kind 1) half-way between road y and eye y; **town heading nudge** (213d: `L[seg+7]`, `R[seg+6]`); `0000:8ea8` tests `L` bit 0 |
| 0x0004 | small bush (shape 7) at the edge | upper storey (`F&4`) |
| 0x0008 | bush (shape 8) at the edge | tall upper storey |
| 0x0010 | — | wall colour 2 (block end) |
| 0x0020 | palm (shape 0) at the outer edge | palm at the road edge |
| 0x0040 | tree (shape 0x0A): L at XOL−((XL−XOL)>>1), R at XOR | fire hydrant (`607C`) at the corner |
| 0x0080 | big tree (shape 0x0B): L at XOL, R at XOR+((XOR−XR)>>1) | sidewalk with a colour-9 strip (instead of a building) |
| 0x0100 | shrub (shape 4) at the outer edge (0x0200: twice as far out) | shrub at the road edge |
| 0x0800 | "road work ahead" sign (shape 3) | same |
| 0x1000 | barricade (shape 6); collision | same |
| 0x2000 / 0x4000 | R only: winding-road signs (shapes 0x12 / 0x13) | — |

### 5.3 Pictures (LIB2 index = id − 1000; `tools/srlib.py Game/LIB2`)

| id / LIB2 # | size | use |
|---|---|---|
| 1005 / #5 | 320×52 | sky, stored at page-1 rows 102…153; the bottom `hy−30` rows are shown |
| 1267 / #267 | 320×37 | horizon panorama (mountains), page-1 rows 163…199; town: #8 (184×4, rooftops) is masked onto it at (44,12) |
| 1258, 1237, 1236, 1238 / #258, #237, #236, #238 | 264×43 | sign sheets, 7 sizes side by side (`DS:5E9C`): "County Line" (race banner), "Garage", "Drive-In", "Gus Gas" (town location signs) — page-2 rows 102…144 |
| 1261 / #261, 1262 / #262 | 264×43 | town billboards "Try Vegas Gambler", "Play Block Out" — page 0 rows 100…142 / 143…185 |
| 1006 / #6 | 88×82 m | left A-pillar, at (0,18) |
| 1007 / #7 | 232×5 m | hood edge, at (88,95) |
| 1009 / #9 | 80×27 | rear-view mirror frame/background, at (240,18) |
| 1243 / #243 | 80×11 | mirror sky; 2 rows at the mirror horizon |
| 1013 / #13 | 48×32 m | drag start light, 24-pixel halves, at (130,18) |
| `DS:5E14[cls][0..3]` | | opponent class `cls` = byte `DS:8DF0 + 10*DS:7648`: {side, rear 3/4, rear, front} = class 0: 1003 1004 1057 1091; 1: 1010 1012 1011 1239; 2: 1010 1247 1248 1092; 3: 1010 1249 1250 1251; 4: 1010 1253 1252 1254 |
| 1241, 1242, 1246 / #241, #242, #246 | | police car rear (lights on/off) and front (race code) |

### 5.4 Road-side shapes ("hline sprites")

`DS:6078[20]` → `ShapeSet {u16 nsizes; u16 sizes}` (near) → `ShapeSize[nsizes] {s16 nlines; u16 off; u16 seg}` →
`nlines` × `HLine {s8 dx; s8 dy; s8 w; s8 colour}` in far segments `2fa6` (types 0–8, the hydrant and the dot) and
`3324` (types 9–19). A line covers `x+dx … x+dx+w` (inclusive; `w = 0` is one pixel) at row `y+dy`; `dy ≤ 0` (base
at y). Sizes 0 (far) … 6 (near); the hydrant has 8. Port: copy the tables from the exe image at load time (as the
other specs do) rather than transcribing them.

| type | ShapeSet | sizes (lines per size) | picture |
|---|---|---|---|
| 0 | 5EF2 | 13 22 47 88 127 165 245 | palm tree |
| 1 | 5F20 | 2 2 4 6 9 11 14 | edge post (reflector) |
| 2 (= `607C`) | 5F54 | 15 20 36 43 49 64 73 88 | fire hydrant |
| 3 | 5F82 | 10 25 33 56 97 156 165 | "ROAD WORK AHEAD" diamond |
| 4 | 5FB0 | 3 5 10 22 38 53 65 | shrub |
| 5 | 5FDE | 17 31 47 75 109 175 211 | "SPEED 35 LIMIT" |
| 6 | 600C | 20 40 62 89 114 137 155 | striped barricade |
| 7 | 603A | 7 7 7 12 18 32 46 | small bush |
| 8 | 6068 | 22 22 22 46 59 71 95 | bush |
| 9 | 60CC | 5 21 34 48 64 75 95 | telephone pole |
| 0x0A | 60FA | 21 35 58 87 132 181 199 | tree |
| 0x0B | 6128 | 18 33 71 121 167 229 306 | big tree |
| 0x0C | 6156 | 7 17 35 53 73 105 127 | overhead: left arrow + route 48 shield |
| 0x0D | 6184 | 12 38 77 103 129 161 195 | overhead: "CITY LIMITS" |
| 0x0E | 61B2 | 7 19 35 51 71 103 127 | overhead: up arrow + route 69 shield |
| 0x0F | 61E0 | 21 42 50 74 109 145 157 | "road narrows" |
| 0x10 | 620E | 8 17 27 41 60 71 84 | chevron `<` (left curve) |
| 0x11 | 623C | 8 17 27 41 60 71 83 | chevron `>` |
| 0x12 | 626A | 10 25 37 48 65 86 101 | winding road (left first) |
| 0x13 | 6298 | 10 25 37 48 65 86 101 | winding road (right first) |
| `60A0` | 6072 | 5 | 5×5 cyan dot (UI, `3abf`) |

The colour indices in the shapes (foliage 13, trunks 14, signs 15/4/1/2) are palette indices; see §8 about the palette.

### 5.5 Other constants

`DS:5E6A[15]` = 8 6 5 4 3 2 2 2 1 1 1 0 0 0 0 (front dash width / banner post thickness − 1, by row);
`DS:5E84[8]` = 0 (mirror). `DS:5E9C` sign sizes (§3). `DS:5E56` = 57 25 22, `DS:5E5A` = F7 75 52, `DS:5E5E` = 75 54 21.
`DS:5DAC` / `DS:5DDC` / `2fa3:0000` page descriptors: `{0x140, 0xC8, 0x1F40, 0 × 12, 0xFF, 0, 0, 0, seg (A200/A400/A000), 0, 0, 0x28, 0xFE}`.

-----------------------------------------------------------------------------------------------

## 6. Hardware / DOS dependencies → SDL3

| Where | Original | SDL3 port |
|---|---|---|
| 0008, 001d | GC regs 0/1/3/8 (set/reset fill mode, restore) | no-ops; `hline`/`fill_rows` write the colour index directly into the planar model (`platform/ega`) |
| 0034 (`[78D6]`) | planar hline through set/reset + bit mask, segment `DS:787A` | write the pixel range into the back page of `platform/ega` (page = `787A` A200/A400) with the same clipping |
| 00e7 (`[78DA]`) | `rep movsb` of whole rows under set/reset | fill rows `y … 5E52-1`, all 320 pixels, of the back page |
| 2288/224e → `21a0:182e` | CRTC start address high = page<<5 (A200/A400), then waits for the start of vertical retrace (port 3DAh) | set the ega module's display start; present the frame; the retrace wait = wait for the next 70 Hz vblank (host), or simply present — frame pacing is dominated by the 9-tick wait |
| 1e9e | busy-wait on `DS:05F8` (timer ISR ticks) | loop `host_pump()` until the tick counter reaches the target (PORTING.md) |
| 206b | runs inside the timer ISR | call from the host tick handler, like the original, every tick |
| blits `[78A2]`/`[78AA]`, line `[78D2]` | driver `21a0` | video spec (page-to-page and picture blits must accept source rows ≥ 102 and pages A000/A200/A400) |
| split screen | `21a0:0014` (called by 0f38:7b22) | video spec; the view pages are A200/A400, the lower split shows A000 |

No interrupts, BIOS calls or DOS calls in this segment besides the port I/O above.

-----------------------------------------------------------------------------------------------

## 7. Timing

* **Per frame** (drive loop `0f38:7b22` → `road_step`): steering → heading; one complete projection + draw into the
  back page; page flip at the next vertical retrace; then **wait until 9 ticks of the 72.8 Hz timer have passed since
  the start of drawing** (`road_frame`). With a fast machine the game runs at 72.8/9 ≈ **8.09 frames/s**; on a slow
  machine (drawing > 9 ticks) the frame rate drops and the car moves less per second, because all movement
  (`7D34` sub-steps, `DS:7AAE` light, heading nudges, drift) is **per frame**. Faithful port: keep the 9-tick lock,
  do not interpolate. (If the sound code reprograms the PIT — PORTING.md — the tick rate and therefore the frame rate
  change with it; see §8.)
* Speed: in town the car moves 4 (or 6) sub-steps per frame = ½ (¾) segment per frame. In races `7D34` comes from
  `0000:d544(speed)` (race spec).
* **Per timer tick**: `road_edge_collision` (from the ISR 0000:238c) — reads `7D36`, `7D82`, `8BD0`, `7D30` which the
  frame code changes; the ISR can run in the middle of a frame (port: run it from the tick callback with the same data).
* **Per segment** (`road_advance_segment`): road edges of the current segment and 4 ahead, height, autopilot.
* **Once per drive screen**: `road_load_graphics`, `road_race_init` (initial frame without waiting for a flip first).
* **New game / load**: `track_build_all` / `track_build_course` (random, uses the shared `rnd` generator: the town and
  scenery layout depends on the generator state at that moment).

-----------------------------------------------------------------------------------------------

## 8. Open questions

1. **Palette.** The road/shape colour indices only make sense with a non-default palette (palms are colour 13 =
   light magenta with trunks 14 = yellow, grass/ground colour 9 = light blue, asphalt 1 = blue in the default EGA
   palette). FORMATS.md says there is no palette code; either `tools/srlib.py`'s plane order/palette assumption is
   off or the palette is set somewhere outside the code searched so far. Check against a screenshot of the original
   before choosing colours in `platform/ega`.
2. **Uninitialised locals (original bugs).** (a) `road_draw_band_front` compares the remaining line count with the
   never-written local `[bp-0x22]` to draw an extra colour-2 line on the town shoulders for rows < 6; (b) `quad_fill`
   mode 3 (right-hand town walls whose near edge is taller — the normal case for right walls!) accumulates into the
   never-initialised `[bp-0x12]`; (c) `road_draw_band_mirror` uses `w>>1` when `C&8` is clear (harmless). The stack
   slots hold whatever an earlier call at the same depth left. Recommendation: (a) treat as never equal (no extra
   line); (b) initialise the accumulator with `x1 << 4` (the evident intent), then compare with the original in DOSBox:
   if the right-hand walls look wrong in the original, emulate the stack slot instead.
3. **Town heading nudges** (`213d`): once the car has passed within 6 segments of `R[44]` (bit 0, the right location
   sign) `DS:7D3C` is decreased by 3 **every town frame** until `L[seg+7]&1` latches the opposite +3 (cancelling it).
   Starting at location 1 (segment 10) this gives a constant heading drift between segments 38 and 101. Is this
   really how cruising behaves (road "pulling" the car), or is `7D3C` reset elsewhere (`0000:d992`/`da25`)? The
   latches are only cleared by `2114`.
4. `opponent_draw` with sprite index −1 would use `DS:8ACA`/`8ACC` as a picture pointer; impossible as long as it is
   only called for rows 0…14 / mirror rows 1…7 (`d < 0x78`). If the visibility test fails after a clip switch the
   clip rectangle is **not** restored (mirror clip can stay active for the rest of the front rows when the
   hysteresis gives a mirror sprite for a front row). Keep as is.
5. The rear sprite `DS:8ACE[2]` and the police `#242` are loaded as temporary (mode 2) pictures; `0f38:a0b8(1)`
   after the police shrink may release the heap region they live in while `8ACE[2]` is still used by `opponent_draw`.
   Needs the heap-mark semantics from the platform spec (`0f38:a054/a09e/a0b8`).
6. At the end of the mirror, a 32×(h−6) block of the mirror (from x 248, y 22 of the back page) is copied to page 0
   (A000) at (8,3). Where is it shown? (Dashboard/split-screen area; video/race spec.)
7. `DS:7EC0` (0 = finish at 0x17B, 1 = at 0x3FB) is set in `0000:9d6d` from `DS:4976`; which race type is which
   (drag vs. road race / pink slips) belongs to the race spec. Also `DS:8ACC` semantics.
8. The mirror reads flags at `seg-8…seg-2` and geometry at `seg-7…seg`; at the town start (segment 10) the mirror
   reads segments ≥ 2 only, but `road_state_init` never places the car below 10, so negative indices are not reached
   in normal play (keep the flat memory layout anyway).
9. The PIT rate during driving (engine sound may reprogram it): if the tick rate differs from 72.8 Hz while driving,
   the 9-tick frame lock changes accordingly — confirm in the sound/platform spec.

**Answers from race.md (added when merging):** (1) the race code *does* program EGA attribute
registers: `0000:67cf` sets regs 6/7 to the player's paint pair and `0000:8d9c` regs 0Ch/03h to the
opponent's (police: pair 5) from `DS:50C2` — check whether other palette registers are set elsewhere
before deciding on (1). (6) The mirror copy goes to the dashboard page, which the VGA split shows below
scan line 99 (race.md §4.2). (7) `DS:7EC0` = 0 drag race, 1 road race; `DS:8ACC` is the CPU speed class,
not an opponent level (race.md §7; the port should use a value ≥ 4). (9) The PIT stays at 72.8 Hz
while driving (divisor 0x4000 set once by `0000:1111`; the sound code only writes channel 2).
