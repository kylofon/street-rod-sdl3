# game_flow — Street Rod (1989), SR.EXE

Porting spec for everything around the garage and the races: `main` after the hardware set-up, the
title sequence, the demo / auto switches, the new-game and load-game loop, the garage dispatch loop
`0000:503f` with its return codes, the summer calendar (game clock), money, the lose conditions
(summer over, broke, debts), the win (beating the King: ending, girlfriend, hall of fame), the
copy protection (skipped), save / load (`?:HOTROD<n>.SAV`), the `hall_dat` file ("King Street
Rodders" wall) and the player state layout in DGROUP.

Conventions follow `port/RE_GUIDE.md`: code addresses `SSSS:OOOO` as stored in the file, `DS:xxxx` =
DGROUP `3E96`. Every function in §2 was read in `port/decomp/sr_ds.c`; `main`, `game_loop` (`503f`,
whose Ghidra output loses the loop structure), the protection routine, `title_and_setup`, the clock
helpers and the hall score were checked against `tools/x86dis.py`. Where the two disagree the
pseudocode follows the disassembly.

Other specs own the screens called from here (**garage**: `3c8d`–`8d26`; **race**: `8d26`–`e0e2`;
**video**/**platform**: `0f38`, `0e92`, `21a0`, `0000:0d58`–`2df4`, memory `3709`–`3967`). Their
functions appear here with provisional names marked *(prov.)* and only their interface is given.

Two message conventions used everywhere below:

* **Message ids.** `msg_box(id)` (`0000:3ab8`) and `0f38:28a4` take an id `p`: DS:`2C01+p` is the
  box type (1 = one line 0x98 wide, 2 = one line 0xE8 wide, 3 = two lines; box geometry table
  DS:04C4, 0x12 bytes per type), the text lines follow from DS:`2C02+p` as NUL-terminated strings.
  A negative id is drawn the same way but the box is removed right after drawing. Ids below
  0x73A point into the text block that `HOT_DATA` loads at DS:2C02 (car and opponent names).
* **Menus.** `ui_menu(n)` (`0f38:4f14`) runs UI menu `n` until an element fires and returns its
  code. Menu `n` starts at element `W[DS:2BAD + 2n]` of the element table DS:09F4 (0x12 bytes:
  `+0 type, +2 style, +4 text (DS:239E + x) or message id, +6 code, +8 x0, +A y0, +C x1, +E y1,
  +10 next`). Button labels below are `DS:239E + text`. Keyboard shortcuts come from the table
  `W[DS:579F + 2n]` (pairs `scancode, v`, NUL-terminated); the key fires code `v + 50`.

---

## 1. Overview

`main` (`0000:066f`) parses the command line, sets up video, memory, sound and the picture
libraries, loads the resident pictures, then calls `game_loop` (`0000:503f`) and finally
`exit_to_dos` (`0000:0fea`).

`game_loop` has three layers:

1. **Title** (`title_and_setup` 0000:39c0): logo, title and credits (LIB1 #2, #0, #3), RNG seed,
   copy protection (jumped over in this SR.EXE), `HOT_DATA` / `hall_dat` / game pictures. Returns -1.
2. **New / load loop**: code -1 → `new_game_prompt` (driver licence, name entry); anything else →
   `load_game_screen` (the Highway Patrol "wanted" book). `new_game_prompt` returns -2 for "Old
   Game"; `load_game_screen` returns -1 for "Forget it" or no saves, and `-0x29A` after a read error
   (state reset, back to new game).
3. **Garage loop**: `garage_screen(redraw)` (`0000:6b06`, garage spec) returns the button code;
   the loop runs the matching screen and advances the game clock. Code `0x29A` means "the game is
   over" (flags in DS:0282): the summer-over screen, the hall of fame, and the Load / New / Quit menu.

```
_astart 1e16:001e
└─ main 0000:066f
   ├─ detect/menu 0000:0226, mem_init 0000:36be (→ pools_init 3614(1)), 0f38:9ee9,
   │  driver_select 0000:0316, 0000:04f4, video_init 0f38:1859, timer_init 0000:1111,
   │  pal_set 0f38:1fa4(DS:0440), lib_open 0f38:6943, 0000:0659, 0f38:61ab(1), 0f38:2b28,
   │  snd_init 0f38:7101, page_mode 0f38:0dbd(0)                        (platform / video / sound)
   ├─ pic_load_list 0f38:68ca(DS:4C20 = {3,1,4}) → DS:70E2 ; hot_data_load 0f38:6b79
   │  (→ hall_load 0f38:6d09) ; pic_preload_list 0f38:6887 / pic_load_list(DS:4C28)
   ├─ game_loop 0000:503f
   │  ├─ title_and_setup 0000:39c0 ── title_sequence 0f38:0bf0, [copy_protection 0000:2fc8]
   │  ├─ new_game_prompt 0000:570c ── new_game 0000:56a9 ── pools_init 3614,
   │  │                                opponents_init 5584 ── opponent_init 542e
   │  ├─ load_game_screen 0000:5b4a ── save_slots_scan 59f6, load_car_chain 590f, load_part_chain 57dc
   │  ├─ newspaper_front 0000:3b33 ── draw_date 6750 ; classifieds 0000:442b (garage)
   │  ├─ garage_screen 0000:6b06 (garage) ── summer_over_check 6529, broke_check 6ad8
   │  ├─ [codes 1..17, -40] garage / race screens (§4.3 table)
   │  ├─ quit_menu 0000:641e ── save_game_screen 0000:5eea ── save_car_chain 586e, save_part_chain 5795
   │  ├─ calendar_show 0000:65af ── day_index 6476, clock_hm 64a3, month_of_day 653c
   │  └─ [0x29A] summer_over_screen 0f38:0d62, hall_of_fame_screen 0000:339f
   │            (hall_insert 32f4, hall_score 3375, hall_save 0f38:6dd2), game_over_menu 0000:315b
   └─ exit_to_dos 0000:0fea
```

The win is detected in the race code: after beating the King at Bob's Drive-In (`0000:ab8b`,
race spec) `ending_sequence` (`0000:a544`, documented here) plays and DS:0282 bit 1 is set.

---

## 2. Function table

### 2a. Functions owned by this spec

| address | name | signature | purpose | conf. |
|---|---|---|---|---|
| 0000:066f | `main` | `void far main(int argc, char **argv)` | switches, init, resident pictures, `game_loop`, exit | verified |
| 0000:503f | `game_loop` | `void near game_loop(void)` | title, new/load loop, garage dispatch, game-over handling | verified |
| 0000:39c0 | `title_and_setup` | `int near (void)` → -1 | title sequence, RNG seed, (protection), data loads | verified |
| 0f38:0bf0 | `title_sequence` | `void far (void)` | California Dreams logo, title, credits | verified |
| 0000:2fc8 | `copy_protection` | `int far (void)` → 1 | juke-box colour question (never called in SR.EXE) | verified |
| 0000:315b | `game_over_menu` | `int far (void)` | Load Game / New Game / Quit after a game ends | verified |
| 0000:32d0 | `hall_is_slower` | `bool near (int i)` | `atoi(hall[i].hours) > DS:6C5E` | verified |
| 0000:32f4 | `hall_insert` | `void near (int i)` | shift down, write player name and hours at row `i` | verified |
| 0000:3375 | `hall_score` | `int near (void)` | hours since June 16 00:00 = day·24 + hour + 12 | verified |
| 0000:339f | `hall_of_fame_screen` | `void far (int won)` | LIB1 #4 wall, list, name entry and save if `won` | verified |
| 0000:3614 | `pools_init` | `void near (int full)` | free lists of the car and part pools, counts; `full`: clear player | verified |
| 0000:3a83 | `wait_ticks` | `void far (int n)` | run the event loop until `n` ticks pass (input can cut it short) | verified |
| 0000:3ab8 | `msg_box` | `void far (int id)` | message box for DS:05DC ticks | verified |
| 0000:3b0c | `garage_full_check` | `int near (void)` | ≥16 cars / ≥131 parts → message, 1 | verified |
| 0000:3b33 | `newspaper_front` | `int near (void)` | front page with the date; returns menu-7 code (1 cars, 2 parts, else) | likely |
| 0000:3c1b | `part_alloc` | `u16 near (void)` | pop a part record from DS:7EBA | verified |
| 0000:3c3e | `part_free` | `void near (u16 p)` | push back, `+4 = -1` | verified |
| 0000:3fb1 | `car_alloc` | `u16 near (void)` | pop a car record from DS:7EB4 | verified |
| 0000:3fd4 | `car_release` | `void near (u16 c)` | push back, `+2 = -1` | verified |
| 0000:542e | `opponent_init` | `void far (int i, int *models)` | random model/colour/flags for opponent `i` | verified |
| 0000:5584 | `opponents_init` | `void near (void)` | 7 slow, 7 medium, 7 fast opponents | verified |
| 0000:56a9 | `new_game` | `void near (void)` | pools, $750, opponents, clocks, race counters | verified |
| 0000:570c | `new_game_prompt` | `int near (void)` | licence picture, name entry; 0 / -2 / other | verified |
| 0000:5795 | `save_part_chain` | `int near (int fd, u16 p)` | write 8-byte parts along `+6` | verified |
| 0000:57dc | `load_part_chain` | `int near (int fd, u16 *head)` | read 8-byte parts while `+6 != 0` | verified |
| 0000:586e | `save_car_chain` | `int near (int fd, u16 car, int one)` | write car + its 7 part chains [+ next cars] | verified |
| 0000:590f | `load_car_chain` | `int near (int fd, u16 *head, int one)` | inverse | verified |
| 0000:59f6 | `save_slots_scan` | `void near (int for_save)` | probe HOTROD1..15.SAV, build the list DS:49E0 | verified |
| 0000:5b4a | `load_game_screen` | `int near (void)` | load list and load; 0 / -1 / -0x29A | verified |
| 0000:5eb3 | `str_is_blank` | `int near (char *s)` | only `' '`/`'_'` → 1 | verified |
| 0000:5eea | `save_game_screen` | `void near (void)` | save list, name edit, overwrite prompt, write | verified |
| 0000:641e | `quit_menu` | `int near (void)` | "Save, Restart, or Quit." box (menu 0x1B) | verified |
| 0000:6476 | `day_index` | `int far (void)` | day 0..91 of the summer | verified |
| 0000:64a3 | `clock_hm` | `void far (int *hour, int *min)` | hour 0..11, minute 0..59 within the day | verified |
| 0000:6529 | `summer_over_check` | `void far (void)` | day > 90 → DS:0282 \|= 1 | verified |
| 0000:653c | `month_of_day` | `int near (int day, int *dom)` | month 0..3 (June..Sept), day of month | verified |
| 0000:65af | `calendar_show` | `void far (int final)` | calendar page with an X on today | verified |
| 0000:6750 | `date_print` | `void far (int y)` | `"%s %d, 1963"` on the newspaper | verified |
| 0000:6ad8 | `broke_check` | `int near (void)` | no car, no parts, money < $400 | verified |
| 0f38:0d62 | `summer_over_screen` | `void far (void)` | "took too much time" + final calendar | verified |
| 0f38:21c0 | `money_add` | `void far (int delta)` | money += delta (sign-extended), redraw bankroll | verified |
| 0f38:6b79 | `hot_data_load` | `void far (void)` | 13 blocks of `hot_data` into DGROUP, then `hall_load` | verified |
| 0f38:6ccf | `hall_scramble` | `void near (void)` | set bit 7 of the 230 bytes at DS:82CA | verified |
| 0f38:6cec | `hall_unscramble` | `void near (void)` | clear bit 7 of the same bytes | verified |
| 0f38:6d09 | `hall_load` | `void far (void)` | read `?:hall_dat` (absent → 0 entries) | verified |
| 0f38:6dd2 | `hall_save` | `void far (void)` | write `?:hall_dat` | verified |
| 0000:a544 | `ending_sequence` | `void near (void)` | LIB1 #8 text, girlfriend faces and lips, "You're my hero" | likely |
| 0000:1bae | `demo_step` | `void far (void)` | demo mode: fake cursor moves and key presses from the timer ISR | likely |
| 0000:2e93 | `kbd_inject_scancode` | `void far (void)` | apply DS:47CC to the key state (used by the demo player) | likely |

### 2b. Functions of other specs used here (interface only)

| address | name *(prov.)* | spec | interface |
|---|---|---|---|
| 0000:6b06 | `garage_screen(int redraw)` | garage | draws the garage (0 = keep, 1 = full, 2 = full + promote first spare car, 3 = after a car switch), shows limit and broke messages, returns the menu-10 code or `0x29A` when DS:0282 ≠ 0 |
| 0000:442b | `classifieds(int kind)` | garage | newspaper ads, 1 = cars, 2 = parts; buying costs `0x444` (car) or `0x222` (part) clock units; in demo mode adds $1999 when too poor |
| 0000:3c8d | `sell_parts` | garage | code 5 |
| 0000:49ed | `your_cars` | garage | code 6; returns the next `garage_screen` mode (3 = car switched: DS:4F48 new current, DS:4F46 old) |
| 0000:6f6b | `paint_shop` | garage | code 7 |
| 0000:7800 | `change_tires` | garage | code 8 |
| 0000:7aa4 | `change_transmission` | garage | code 9 (LIB2 #70 panel) |
| 0000:81ab | `under_the_hood(car)` | garage | code 10 |
| 0000:711a | `customize(int what)` | garage | codes 11/12/13 → 2 rear bumper, 1 front bumper, 4 roof |
| 0000:7367 | `stickers` | garage | code 14 |
| 0000:79eb | `car_info` | garage | code 16 |
| 0000:7ee6 | `car_runs(car, int)` | garage | 0 = cannot drive |
| 0000:dcbe | `car_perf_compute(car, DS:78E0)` | race | before driving |
| 0000:8d26 | `drive_across_town` | race | "Cruising in town..." (DS:0284 = destination) |
| 0000:ab8b | `bobs_drive_in` | race | challenges and races; sets DS:0282 bits 1 (King beaten) / 2 (debts) |
| 0000:b8a1 | `gas_station` | race | fuel |
| 0000:9556 | `king_ready` | race | 0 = the King accepts (≥6 drag wins, ≥6 road wins, wins ≥ races/2, or all opponents beaten) |
| 0000:b08c | `jail_scene` | race | after "You've been sentenced for debts" |
| 0000:c613 | `race_results` | race | returns 4 when the speeding fine cannot be paid (money set to 0) |
| 0000:0fea | `exit_to_dos` | platform | restores the machine and exits; in demo mode prints "Quit demo" |
| 0000:0e24 | `input_flush` | platform | waits until no key for 10 ticks |
| 0000:0d99 / 0dd8 | `music_off` / `music_on` | sound | juke-box button state (element id -40) |
| 0000:1a05 | `music_start(tune, n)` | sound | title music |
| 0000:1417 | `event_loop(int timeout)` | platform | one pass of input / hot spots; returns a code or 0 |
| 0f38:0b51 | `wait_or_key(int ticks)` | platform | returns at timeout, Enter, Esc, click; `M` toggles music |
| 0f38:4a66 / 4ba1 | `ui_push(menu)` / `ui_pop(int)` | video | menu stack (max 4) |
| 0f38:4f14 | `ui_menu(menu)` | video | push, loop `event_loop(32000)` until ≠ 0, pop |
| 0f38:4ec1 | `ui_dialog(menu)` | video | save background, `ui_menu`, restore |
| 0f38:4f4f / 500e | `ui_list(menu, sel, ?, list, ?, ?)` | video | list box (500e with edit field); -3 = main button, -4 = cancel, else row |
| 0f38:590b | `ui_list_row()` | video | current row (DS:8BA0) |
| 0f38:5e54 | `ui_edit(menu, maxlen, buf)` | video | text field; >0 → 0 |
| 0f38:5910 | `text_input(x, y, buf, maxlen)` | video | inline editor |
| 0f38:2554 / 2638 | `pic_draw(id, clr)` / `pic_draw_at(id, x, y, clr)` | video | picture from the libraries; `clr ≥ 0` clears the screen first |
| 0f38:62ba / 655e / 68ca / 6887 / 6915 | `pic_load` / `pic_free` / `pic_load_list(list, all)` / `pic_preload_list` / `pic_free_list` | platform | picture cache; ids: `< 1000` → LIB1 #(id−1), `≥ 1000` → LIB2 #(id−1000) |
| 0f38:1fa4 / 1f4b | `pal_set(tab)` / `pal_reg(i, v)` | video | EGA palette through INT 10h AX=1002h/1000h |
| 0f38:2213 | `status_line(text)` | video | bottom line; "`Bankroll:`$" + money |
| 0f38:20a1 | `status_text(x, text, ?)` | video | text in the status line |
| 0f38:5eb6 | `rnd(n)` | platform | `n ≤ 0`: seed from the clock; else 0..n−1 (3 combined LCGs, DS:6C62..6C66) |
| 0f38:6c3d | `data_disk()` | platform | makes sure the LIB2 disk is in; returns its drive letter |
| 0f38:2d7e | `cursor(int)` | video | -4/-3 hide, -2/-1 show, 0/1 … |
| DS:78BE / 78C2 / 78CA / 78B6 | driver vectors | video | page set-up / show back page / copy page / copy page (reverse) |
| 0f38:b5aa | `copy_rect(back→front, x0,y0,x1,y1)` | video | |

Memory helpers in the 2e93–3c8d range (`36a8` segread DS, `36be` mem init, `36d6` hfree,
`36e9` "Not enough memory to run Street Rod !", `3709` halloc, `376b`/`3880`/`3904` bitmap
headers, `3967` video-RAM carving) belong to **platform/video**.

---

## 3. Globals

### 3a. Player state (saved)

| DS offset | name | type | meaning | written by | read by |
|---|---|---|---|---|---|
| 7E9A | `player_name` | char[16] | licence / save name (≤15 chars) | new_game_prompt, save_game_screen, load | save, hall_insert |
| 7EAA | `money` | i32 | bankroll ($) | new_game (750; demo 10000), money_add, garage, race | everywhere |
| 7EAE | `unk_7eae` | u16 | set to 10 by `pools_init(1)` / `new_game`; no reader found | | |
| 7EB0 | `cur_car` | u16 ptr | car being worked on / driven; 0 = none | garage, load | everywhere |
| 7EB2 | `car_list` | u16 ptr | other cars, linked by `+26` | garage, load | |
| 7EB4 | `car_freelist` | u16 ptr | free list of the car pool | pools | car_alloc |
| 7EB6 | `car_count` | i16 | cars allocated (cur + list), max 16 | car_alloc/free | garage_full_check |
| 7EB8 | `spare_parts` | u16 ptr | spare-part chain (`+6`) | garage, load | |
| 7EBA | `part_freelist` | u16 ptr | free list of the part pool | pools | part_alloc |
| 7EBC | `part_count` | i16 | parts allocated (fitted + spare), limit 0x83 | part_alloc/free | garage_full_check |
| 83B0 | `car_pool` | 16 × 0x28 | car records (§5.1) | | |
| 866A | `part_pool` | 140 × 8 | part records (§5.1) | | |
| 4970 | `races_run` | i16 | races for money / pink slips (DS:8278 ≠ 1) | ab8b | king_ready |
| 4972 | `drag_wins` | i16 | (−3 when beaten by a King's man) | ab8b | king_ready |
| 4974 | `road_wins` | i16 | (−2 idem) | ab8b | king_ready |
| 4976 | `king_man_beaten` | i16 | set after beating an opponent with DS:7D40 ≠ 0 | ab8b | 9d6d |
| 7FF8 | `opponents` | 22 × 0x12 | 21 opponents + the King (#21 at 8172), loaded from `HOT_DATA` block 0 | opponent_init, race | race |
| 05FC | `game_clock` | u32 | activity time, 0x222 per game hour | game_loop, garage, race | day_index |
| 5E0C | `unk_5e0c` | u16 | passed to `2645:0b91` (100 on a new game) — see race | 2645:0b91 | save |

### 3b. Flow state (not saved)

| DS offset | name | type | meaning | written by | read by |
|---|---|---|---|---|---|
| 0600 | `rt_ticks` | u32 | real-time counter, +1 every 4th timer tick (18.2 Hz) | timer ISRs 1fe2/238c | clock |
| 0604 | `rt_base` | u32 | `rt_ticks` at game start (new: now; load: now − saved elapsed) | new_game, load | clock, save |
| 05F8 | `ticks` | u32 | timer ticks (72.8 Hz, PIT divisor 0x4000) | ISRs | waits |
| 05CC/05CE, 05D0 | `wait_until`, `wait_flag` | u32, i16 | deadline for `event_loop` | wait_ticks | 1417 |
| 05DC | `msg_ticks` | i16 | message box duration = 0x15E (350 ticks ≈ 4.8 s) | (init) | msg_box |
| 0282 | `game_over` | u16 | bit0 summer over, bit1 King beaten, bit2 debts, bit4 broke | 6529, ab8b, 6b06, game_loop (clear) | game_loop, 6b06, 65af |
| 0284 | `location` | i16 | 0 idle, 1 garage, 2 Bob's, 3 gas, 5 classifieds, 6 jail | many | race, video |
| 8BCE | `sw_demo` | i16 | `demo` switch | main | title_and_setup |
| 0610 | `demo` | i16 | 0 / 1 demo running / 0x63 Esc pressed / 0x62 quitting | title_and_setup, kbd ISR | many |
| 0612 | `sw_auto` | i16 | `auto` switch: the car drives itself (race code 2645:1efc/213d, ISR 238c) | main | race |
| 0262 | `mouse_enabled` | i16 | 0 with `nomouse` | main | 1111 |
| 0280 | `no_video_cache` | i16 | `nouemem`: no pictures preloaded into spare video memory | main | pic_preload_list |
| 70E2 | `title_resident` | i16 | 1 = title pictures and game pictures fitted in memory at start | main | title_and_setup |
| 4F4C | `game_pics_freed` | i16 | set by the ending (game picture list freed) | a544 | game_over_menu |
| 4F4A | `unk_4f4a` | i16 | cleared after a game over (car drawing cache, video) | | |
| 4F46 / 4F48 | `switch_old_car` / `switch_new_car` | u16 | set by `your_cars` for `garage_screen(3)` | 49ed | 6b06 |
| 4F58 / 4F5A | `headline`, `headline_day` | i16 | newspaper headline 0..4 and the day it was picked | newspaper | newspaper |
| 49E0 | `list` | i16 count + i16[] ids | list-box contents (save slots, ads) | 59f6, 442b | ui_list |
| 4FC2 | `slot_present` | u8[16] | [n] = n if HOTROD<n>.SAV has a readable header, else 0 (1-based) | 59f6 | load/save |
| 4FD4 | `slot_text` | u16[15] | message ids of the slot labels (16-byte buffers at DS:4618 + 17·(n−1)) | const | 59f6 |
| 2A2E | `edit_name` | char[16] | save-name edit field | save | save |
| 7646 | `hall_count` | i16 | 0..10 | hall_load, 339f | 339f |
| 82CA | `hall` | 10 × 23 | hall records (§5.3) | | |
| 6C5E | `hall_new_score` | i16 | score being inserted | 339f | 32d0, 32f4 |
| 58D0 | `music_on` | i16 | juke box / music on | -40 button, `M` key | sound |
| 8240 | `draw_through` | i16 | also copy drawings to the visible page | video | |
| 0440 / 0462 | `pal_cur` / `pal_alt` | u8[17] | current and alternate EGA palettes (swapped by 0f38:182c for the hall) | pal_* | video |

---

## 4. Pseudocode

Types: `i16/u16/i32/u32`; `W[x]`, `B[x]` = word/byte at DS:x. `ticks` = DS:05F8.

### 4.1 main (0000:066f)

```c
void main(int argc, char **argv)
{
    B[0x8247] = 0;
    W[0x8ACC] = cpu_class();                 /* 0000:05c6, platform */
    int drives = 2;
    W[0x0262] = 1; W[0x8BCE] = 0; W[0x0612] = 0;
    for (i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "1") == 0)        drives = 1;   /* DS:0264: one floppy drive */
        else if (stricmp(argv[i], "nomouse") == 0) W[0x0262] = 0;
        else if (stricmp(argv[i], "demo") == 0)    W[0x8BCE] = 1;
        else if (stricmp(argv[i], "auto") == 0)    W[0x0612] = 1;
        else if (stricmp(argv[i], "nouemem") == 0) W[0x0280] = 1;
    }
    mode = video_menu_and_detect();          /* 0000:0226 */
    mem_init();                              /* 0000:36be: _amblksiz=0x800, DS:6C60=DS, pools_init(1) */
    f0f38_9ee9(); driver_select(mode); f0000_04f4(mode);
    video_init(far 2E3E:0030);               /* 0f38:1859 */
    timer_init();                            /* 0000:1111 */
    if (drv == -2 || drv == -6) pal_set(DS:0440);
    lib_open(drives, mode);                  /* 0f38:6943 */
    f0000_0659();                            /* DS:82C6 = cpu_class < 4 */
    f0f38_61ab(1); f0f38_2b28(); snd_init(); page_mode(0);
    if (drv == -3 && W[0x0250]) DS:755C = bitmap_new(0x140, 100, 0, -1, 0, 0);
    DS:7564 = halloc((drv == -2 || drv == -6) ? 0x4650 : 0x3E80, 1);   /* picture work buffer */
    if (pic_load_list(DS:4C20 /* {3,1,4,0} */, 0) == 3) {   /* all three title pictures cached */
        W[0x70E2] = 1;
        f0f38_61ab(2);
        hot_data_load();                      /* also hall_load */
        if (drv == -2) pic_preload_list(DS:4C28);
        pic_load_list(DS:4C28, 1);           /* the game's resident LIB2 pictures */
    } else {
        W[0x70E2] = 0;
        pic_free_list(DS:4C20);              /* title is loaded again from disk while drawing */
    }
    game_loop();
    exit_to_dos();
}
```

### 4.2 title_and_setup (0000:39c0) and title_sequence (0f38:0bf0)

```c
int title_and_setup(void)
{
    page_mode(0);
    music_start(2, 10);
    title_sequence();
    DRV_78BE();
    page_mode(1);
    rnd(-1);                                 /* seed from the time of day */
    DRV_78BE();
    if (!W[0x70E2]) f0f38_61ab(2);
    if (!W[0x8BCE]) {
        /* original: if (copy_protection() == 0) exit_to_dos();
           SR.EXE: "jmp +0Ch" at 0000:3a1a skips the call (SRSE keeps the call, see 4.12) */
        cursor(-4);
    }
    f2645_0d48();                            /* driver-specific race tables, see race */
    B[0x7E9A] = 0;                           /* player_name = "" */
    W[0x0610] = W[0x8BCE];                   /* demo */
    if (!W[0x70E2]) {
        hot_data_load();
        if (drv == -2) pic_preload_list(DS:4C28);
    }
    pic_load_list(DS:4C28, 1);
    cursor(-2);
    return -1;
}

void title_sequence(void)      /* 0f38:0bf0 */
{
    save = W[0x04C2];
    if (drv == -2 || drv == -6) DRV_78BE();
    W[0x04C2] = 200;                         /* picture clip height */
    if (drv not EGA/Tandy) f0000_01f2(0);
    pic_draw_at(3, 0x28, 0x28, 0);           /* LIB1 #2 "California Dreams presents" at (40,40) */
    pic_free(3);
    DRV_78C2();                              /* show */
    W[0x8240] = 0;
    pic_draw(1, -1);                         /* LIB1 #0 title, drawn off screen */
    pic_free(1);
    wait_or_key(400);
    DRV_78C2();                              /* title visible */
    if (drv not EGA/Tandy) f0000_01f2(1);
    DRV_78CA(page0, page1);
    pic_draw(4, 0);                          /* LIB1 #3 credits (320x174), off screen */
    pic_free(4);
    wait_or_key(400);
    if (drv == -2 || drv == -6) pal_set(DS:02D4);   /* credits palette */
    DRV_78CA(...);                           /* credits visible */
    wait_or_key(400);
    W[0x04C2] = save;
    fill_rect(0, 0, 0x140, 200, 0);
    DRV_78CA(...);                           /* black */
}
```

`wait_or_key(400)` = 400 timer ticks ≈ 5.5 s each (72.8 Hz), cut short by Enter, Esc, a mouse
click or the joystick button; `M` toggles the music during the wait.

### 4.3 game_loop (0000:503f)

`again` is `[bp-2]`, `code` `[bp-6]`, `prev` `[bp-8]`, `redraw` `[bp-4]`. `again` is 1 from the
title and from a game over, and 0 when the new/load loop is entered from the garage; with 0 the
loop runs exactly once. `clock_hour()` = `game_clock += 0x222` (u32).

```c
void game_loop(void)
{
    again = 1;
    code = title_and_setup();                        /* -1 */
new_or_load:                                          /* 0000:5050 */
    do {
        if (code == -1) again = 1;
        prev = code;
        code = (code == -1) ? new_game_prompt() : load_game_screen();
        if (code == -0x29A) { code = -1; again = 1; }
    } while (code != 0 && again);
    status_line(W[0x49DC]);                          /* "`Bankroll:`$" + money */
    redraw = 1;                                      /* (a "prev == -2 && cur_car → 2" test is overwritten) */
    if (prev == -1 && car_count < 16 && part_count <= 0x82) {   /* new game starts at the paper */
        r = newspaper_front();
        if (r == 2 || r == 1) classifieds(r);
    }
    again = 0;

    for (;;) {
        code = garage_screen(redraw);                /* 0000:50f9 */
        if (code == 0) goto new_or_load;             /* cannot happen: ui_menu never returns 0 */
        music_restore(0);                            /* 0f38:7001(0) */
        if (code == 0x29A) {                         /* game over */
            if (W[0x0282] == 1) summer_over_screen();
            hall_of_fame_screen(W[0x0282] & 2);
            cursor(1);
            W[0x0282] = 0;
            again = 1;
            code = game_over_menu();
            if (code == 4) return;                   /* Quit → main → exit_to_dos */
            cursor(-2);
            W[0x4F4A] = 0;
            goto new_or_load;                        /* -1 new, -2 (or other) load */
        }
        redraw = 2;
        switch (code) {
        case 1:  /* N  "Check out the newspaper" */
            if (garage_full_check()) { redraw = 0; break; }
            if (!cur_car) redraw = 1;
            r = newspaper_front();
            if (r == 2 || r == 1) classifieds(r);
            if (!cur_car) redraw = car_list ? 1 : 2;  /* car present: 2 */
            clock_hour();
            continue;
        case 2:  /* D  "Hit the street" → Bob's Drive-In */
        case 15: /* G  "Wanna tank ?"   → gas station     */
            if (garage_full_check()) { redraw = 0; break; }
            if (!cur_car) { msg_box(car_list ? 0x0F16 /*Pick a car first!*/
                                             : 0x0EFA /*You've got no car, dummy!*/); redraw = 0; break; }
            if (!car_runs(cur_car, 1)) { msg_box(0x0E7F /*Your car's not running, speedy!*/); redraw = 0; break; }
            car_perf_compute(cur_car, DS:78E0);
            if (code == 2) {
                if (W[cur_car+0x22] < 9) { msg_box(0x172E /*not enough gas to cruise to Bob's*/); redraw = 0; break; }
                W[0x0284] = 2; drive_across_town(); clock_hour(); bobs_drive_in();
            } else {
                if (W[cur_car+0x22] >= 0x8C) { msg_box(0x19B1 /*You've got a full tank!*/); redraw = 0; break; }
                if (money == 0) { msg_box(0x199F /*Gas ain't free!*/); redraw = 0; break; }
                W[0x0284] = 3; drive_across_town(); clock_hour(); gas_station();
            }
            clock_hour();
            redraw = 1;
            continue;
        case 3:  /* X  "Calendar" */       calendar_show(0);      redraw = 0; break;
        case 4:  /* Q  "Time to quit" */
            redraw = 0;
            r = quit_menu();
            if (r == -20) return;                    /* Quit */
            if (r == -22 || r == -4) continue;       /* Forget it */
            if (r == -3) {                           /* Save Game */
                if (garage_full_check()) { redraw = 0; continue; }   /* code 0x309 */
                save_game_screen();
                redraw = 2;
                if (!cur_car && car_list) redraw = 1;
                continue;
            }
            code = r; goto new_or_load;              /* -1 New Game, -2 Old Game (again = 0) */
        case 5:  /* L  "Sell parts" */      sell_parts();          redraw = 0; break;
        case 6:  /* C  "Cars" */            redraw = your_cars();  continue;
        case 7:  /* P  "New paint job - $20" */ paint_shop();      redraw = 0; break;
        case 8:  /* T  "Change tires" */    change_tires();        redraw = 0; break;
        case 9:  /* A  "Change transmission" */ change_transmission(); continue;   /* redraw stays 2 */
        case 10: /* H  "Pop the hood" */    f0f38_a07e(); under_the_hood(cur_car); continue;
        case 11: /* B  "Rear bumper" */     customize(2);          redraw = 0; break;
        case 12: /* F  "Front bumper" */    customize(1);          redraw = 0; break;
        case 13: /* R  "The roof" */        customize(4);          redraw = 0; break;
        case 14: /* S  "Stickers - $5" */   stickers();            redraw = 0; break;
        case 16: /* I  "Car info" */        car_info();            redraw = 0; break;
        case 17: /*    "Wanna drink ?" */   msg_box(0x18F6 /*Don't drink and drive, bub!*/); redraw = 0; break;
        case -40:/*    juke box "Catch some tunes" / "Squelch it !" */
            if (W[0x58D0]) { music_off(); W[0x58D0] = 0; } else { W[0x58D0] = 1; music_on(); }
            redraw = 0; break;
        default:
            goto new_or_load;                        /* again = 0: one new-game (−1) or load call */
        }
    }
}
```

Notes, faithful to the original:

* Every garage-limit failure (`garage_full_check`) shows its message and returns to the garage
  without redraw.
* Case 1 costs one hour even when the newspaper is closed without buying. Cases 2 and 15 cost two
  hours (one for the trip, one after the visit), plus whatever the visit itself adds.
* `code 0x309` is only an internal "back to the garage, redraw 0" marker.
* After `goto new_or_load` from the garage (`again = 0`), a cancelled load or a cancelled name entry
  (`-2` from `new_game_prompt`) simply resumes the current game; a failed load (`-0x29A`) sets
  `again = 1` and forces the new-game screen.

### 4.4 Garage dispatch codes (menu 10) and hotkeys

The keyboard table of menu 10 (DS:561D) maps: N 1, D 2, X 3, Q 4, L 5, C 6, P 7, T 8, A 9, H 10,
B 11, F 12, R 13, S 14, G 15, I 16. Code 17 (drink machine) and -40 (juke box) are mouse only.
The hover labels (DS:2621…2743, "Go to the garage", "Hit the street", "Check out the newspaper",
"Wanna drink ?", "Cars", "Sell parts", "Change tires", "Change transmission", "Time to quit",
"Pop the hood", "Catch some tunes"/"Squelch it !", "New paint job - $20", "Front bumper",
"Rear bumper", "The roof", "Stickers - $5", "Wanna tank ?", "Car info", "Calendar") are the
garage spec's hot spots.

`garage_screen(redraw)` interface summary (details in the garage spec):

```c
int garage_screen(int redraw)
{
    if (B[0x4733] > 0) cursor(-4);
    summer_over_check();
    if (W[0x0282]) { if (W[0x0282] & 1) cursor(-2); return 0x29A; }
    ... draw according to redraw (0 keep, 1 full, 2 full after promoting car_list head to cur_car
        when cur_car == 0, 3 car-switch animation) ...
    status_line(W[0x49DC]);
    cursor(-2);
    if (car_count >= 16)        msg_box(0x0D30);   /* no room for all these cars / sell one */
    else if (part_count == 0x83) msg_box(0x0D76);  /* too much junk / sell a part */
    else if (part_count > 0x83) { patch "12" in message 0x0DC7 with itoa(part_count - 0x82); msg_box(0x0DC7); }
    if (broke_check()) { msg_box(0x17FF /*You're outta dough. / done for the summer!*/); W[0x0282] |= 0x10; }
    else { music_restore(0); input_flush(); r = ui_menu(10); }
    summer_over_check();
    if (W[0x0282] & 0x10) W[0x0282] = 0x10;           /* broke overrides the other flags */
    if (W[0x0282]) { r = 0x29A; if (!(W[0x0282] & 1)) cursor(-4); }
    return r;
}
```

### 4.5 Limits, messages, waits

```c
int garage_full_check(void)                /* 0000:3b0c */
{
    if (car_count >= 0x10) { msg_box(0x0E27 /*Sell some cars first!*/);  return 1; }
    if (part_count >= 0x83){ msg_box(0x0E3F /*Sell some parts first!*/); return 1; }
    return 0;
}

void wait_ticks(int n)                     /* 0000:3a83 */
{
    wait_until = ticks + (i32)n;           /* 32-bit add, n sign-extended */
    W[0x05D0] = 1;
    event_loop(0x7530);                    /* returns at the deadline or on input */
    wait_until = 0; W[0x05D0] = 0;
}

void msg_box(int id)                       /* 0000:3ab8 */
{
    saved = wait_until;
    ui_push(0x18);
    msg_draw(id);                          /* 0f38:28a4 */
    wait_ticks(W[0x05DC]);                 /* 0x15E */
    msg_erase();                           /* 0f38:2adf */
    ui_pop(1);
    wait_until = saved;
}

int broke_check(void)                         /* 0000:6ad8 */
{
    return cur_car == 0 && car_list == 0 && spare_parts == 0 && money < 400;   /* signed 32-bit */
}

void money_add(int delta)                  /* 0f38:21c0 */
{
    money += (i32)delta;                   /* cwd; add/adc — only 16-bit deltas */
    status_text(W[0x8258], ltoa(money));   /* after "Bankroll: $" */
    W[0x82C0] = W[0x824C];
}
```

### 4.6 Pools and new game

```c
void pools_init(int full)            /* 0000:3614 */
{
    car_freelist = 0x83B0;
    for (i = 0, c = 0x83B0; i < 16; i++, c += 0x28) { W[c+0x26] = c + 0x28; W[c+2] = 0xFFFF; }
    W[0x83B0 + 15*0x28 + 0x26] = 0;
    part_freelist = 0x866A;
    for (i = 0, p = 0x866A; i < 0x8C; i++, p += 8) { W[p+6] = p + 8; W[p+4] = 0xFFFF; }
    W[0x866A + 0x8B*8 + 6] = 0;
    part_count = 0; car_count = 0;
    if (full) { cur_car = 0; car_list = 0; spare_parts = 0; money = 0; W[0x7EAE] = 10; }
}

u16 part_alloc(void) { p = part_freelist; part_freelist = W[p+6]; W[p+6] = 0; part_count++; return p; }
void part_free(u16 p){ W[p+6] = part_freelist; W[p+4] = 0xFFFF; part_freelist = p; part_count--; }
u16 car_alloc(void)  { c = car_freelist;  car_freelist  = W[c+0x26]; W[c+0x26] = 0; car_count++; return c; }
void car_release(u16 c) { W[c+0x26] = car_freelist; W[c+2] = 0xFFFF; car_freelist = c; car_count--; }
/* no bounds checks: callers test car_count < 16 / part_count via garage_full_check */

int new_game_prompt(void)                  /* 0000:570c */
{
    W[0x8240] = 0;
    pic_draw_at(0x3F8, 0x10, 6, 0);        /* LIB2 #16 driver licence */
    W[0x8240] = 1;
    edit_colors();                         /* 0f38:181c: DS:0446..0448 = 9,1,3 */
    copy_rect(0, 0, 0x13F, 0xBD);
    DRV_78C2();
    r = ui_edit(2, 0x0F, DS:7E9A);         /* menu 2: name field, "Old Game" -2, "OK" -3; Enter → 0 */
    if (r != -3) {
        if (r == -2) return -2;
        if (r != 0) return r;
    }
    new_game();                       /* an empty name is accepted */
    return 0;
}

void new_game(void)                   /* 0000:56a9 */
{
    pools_init(1);
    money = W[0x0610] ? 10000 : 750;       /* 0x2710 / 0x2EE */
    W[0x7EAE] = 10;
    f2645_0b91(100);                       /* sets DS:5E0C, see race */
    opponents_init();
    game_clock = 0;
    rt_base = rt_ticks;
    W[0x4970] = W[0x4972] = W[0x4974] = W[0x4976] = 0;
}

void opponents_init(void)                  /* 0000:5584 */
{
    int slow[11], mid[11], fast[11];       /* [0] = count, 1-based lists */
    for (m = 0; m < 0x19; m++) {           /* 25 car models, 10-byte records at DS:7D86 */
        c = B[0x7D88 + m*10];              /* class byte */
        if (c < 3)      slow[++slow[0]] = m;
        else if (c < 6) mid[++mid[0]]   = m;
        else            fast[++fast[0]] = m;
    }
    if (W[0x0610]) slow[0]--;              /* demo: last slow model kept for "The Geek" */
    for (i = 0;  i < 7;  i++) opponent_init(i, slow);
    for (i = 7;  i < 14; i++) opponent_init(i, mid);
    for (i = 14; i < 21; i++) opponent_init(i, fast);
    B[0x817D] = 0;                         /* the King's +0B */
    if (W[0x0610]) {
        int one[2] = { 1, 0x18 };
        opponent_init(W[0x4FF4] /*0*/, one);
        W[0x7FF8 + W[0x4FF4]*0x12 + 6] = 0x629;   /* name "The Geek" */
    }
}

void opponent_init(int i, int *models)     /* 0000:542e */
{
    do { col = rnd(6); } while (col == 5 && rnd(2) != 1);   /* colour 5 at half weight */
    r = 0x7FF8 + i*0x12;
    B[r+0x0A] = col;
    B[r+0x0B] = B[r+0x0C] = B[r+0x0D] = B[r+0x0E] = B[r+0x0F] = B[r+0x10] = 0;
    do { k = rnd(models[0]); } while (models[k+1] == -1);
    m = models[k+1]; W[r+8] = m; models[k+1] = -1;
    lvl = (B[0x7D88 + m*10] >> 1) + 1;
    B[r+0x0E] = ((B[0x7D8B + m*10] & 7) == 2) ? 8 : 0;
    B[r+0x0F] = (rnd(0x14) < lvl) ? rnd(9) : 0;
    if ((i16)W[0x8DF6 + m*10] > 0 && (i16)W[0x8DF8 + m*10] > 0)
        B[r+0x0E] |= (rnd(0x19) < lvl) ? 3 : 0;
    if ((i16)W[0x8DF2 + m*10] > 0)
        B[r+0x0E] |= (rnd(0x1E) < lvl) ? 4 : 0;
}
```

The call order of `rnd` matters for reproducing a seed: colour loop, model loop, `rnd(0x14)`
[`rnd(9)`], [`rnd(0x19)`], [`rnd(0x1E)`].

### 4.7 Calendar and game clock

The date is `T = game_clock + (rt_ticks − rt_base)` (u32): activities add `0x222` (546) per game
hour and real time adds 18.2 per second, so one game hour is also 30 s of real time and one day
(`0x1998` = 6552 = 12 hours) is 6 minutes. Day 0 is Sunday June 16, 1963; day 91 is the end.

```c
int day_index(void)                                 /* 0000:6476 */
{
    int d = (i32)T / 0x1998;                       /* 1e16:23ec signed ldiv */
    return d > 0x5B ? 0x5B : d;
}

void clock_hm(int *hour, int *min)               /* 0000:64a3 */
{
    u32 t = T;
    if ((i32)t > 0x91908) t = 0x91908;             /* = 91 days */
    u16 r = (u32)t % 0x1998;                       /* 1e16:24bc */
    *hour = r / 0x222;                             /* 0..11, unsigned div */
    *min  = (i32)((u32)(r - *hour*0x222) * 0x3C) / 0x222;
}

void summer_over_check(void)                       /* 0000:6529 */
{
    if (day_index() > 0x5A) B[0x0282] |= 1;
}

int month_of_day(int d, int *dom)                   /* 0000:653c */
{
    if (d < 0x0F) { *dom = d + 0x10; return 0; }   /* June 16..30   */
    if (d < 0x2E) { *dom = d - 0x0E; return 1; }   /* July 1..31    */
    if (d < 0x4D) { *dom = d - 0x2D; return 2; }   /* August 1..31  */
    if (d > 0x5A) { *dom = 0x0F;     return 3; }   /* September 15  */
    *dom = d - 0x4C; return 3;                     /* September 1..14 */
}

void date_print(int y)                        /* 0000:6750 */
{
    d = day_index(); m = month_of_day(d, &dom);
    sprintf(buf, "%s %d, 1963", W[0x5082 + 2*m] /* June, July, August, September */, dom);
    save text colours; set colours from DS:787E+0x41..0x45;
    DRV_78AE(page, 0x61, y, buf);
    restore colours;
}

void calendar_show(int final)                      /* 0000:65af */
{
    summer_over_check();
    if (!final && W[0x0282]) return;
    d = day_index(); clock_hm(&h, &mi); m = month_of_day(d, &dom);
    row = (d + 0x15) / 7 - W[0x5052 + 2*m];        /* {0, 5, 9, 14} */
    x   = ((d + 0x15) % 7) * 0x12 + W[0x5048];     /* 0x21 */
    y   = row * 7 + W[0x504A + 2*m];               /* {0x0E, 0x6B, 0x0F, 0x69} */
    W[0x8240] = 1;
    pic = (m < 2) ? 0x4C3 : 0x4C5;                 /* LIB2 #219 June/July, #221 Aug/Sept (168x150) */
    cursor(-3);
    buf = pic_load_buffer(pic, 2);                 /* 0f38:683e */
    pic_draw_into(buf, 0x4C4 /*LIB2 #220 X mark 24x9*/, x, y);   /* 0f38:b3b4 */
    bg = save_rect(0x50, 4, w, h);                 /* 0f38:9fce + 26f2 */
    put_rect(buf, 0x50, 4);                        /* page at (80,4) */
    f0f38_a0b8(1); cursor(-1);
    if (!final) {
        status_text(-1, "", 0);                    /* DS:505A */
        wait_ticks(500);
        put_rect(bg, 0x50, 4);
    }
    f0f38_a0b8(1);
}
```

`(d + 0x15) % 7` is 0 on Sundays (June 16, 1963 was a Sunday). `row*7` uses the constant 7 (the
same `cx` as the divisor) as the row pitch; `h`/`mi` are computed but unused.

### 4.8 Newspaper front page (0000:3b33)

```c
int newspaper_front(void)
{
    save = W[0x8240]; W[0x8240] = 0;
    pic_draw(0x3FD, -1);                           /* LIB2 #21 newspaper (320x136) */
    d = day_index();
    if (d != W[0x4F5A]) {                          /* new headline once per day */
        h = rnd(5); if (h == W[0x4F58]) h = rnd(5);
        W[0x4F58] = h; W[0x4F5A] = d;
    }
    pic_draw_at(W[0x4F4E + 2*W[0x4F58]] /*0x3FE..0x402 = LIB2 #22..26*/, 0, 0x88, -1);
    cursor(-3); date_print(0x79); cursor(-1);
    W[0x8240] = save;
    if (save) copy_rect(0, 0, 0x13F, 0xBD);
    cursor(1);
    return ui_menu(7);                             /* 1 = used cars ads, 2 = parts ads, other = back */
}
```

### 4.9 Game over: summer, broke, debts; hall of fame; menu

Lose conditions, all ending in `garage_screen` returning `0x29A`:

| flag | set by | message |
|---|---|---|
| bit 0 | `summer_over_check` (day > 90, i.e. Sept 15) — checked in the garage, in `calendar_show`, in `bobs_drive_in` | `summer_over_screen`: 0x17BB "Looks like ya took too much time, jr! / You're done fer the summer." |
| bit 2 | `bobs_drive_in` when `race_results` returns 4 (a speeding fine larger than the bankroll: "You've been sentenced for debts, Ace!" 0x1793, money := 0) → `jail_scene` | (shown by the race code) |
| bit 4 | `garage_screen` when `broke_check()` | 0x17FF "You're outta dough. / Looks like you're done for the summer!" |
| bit 1 | `bobs_drive_in` after beating the King (DS:7EC0 ≠ 0 race vs a DS:7D40 opponent) → `ending_sequence` | win |

```c
void summer_over_screen(void)                      /* 0f38:0d62 */
{
    msg_box(0x17BB);
    DRV_78BE(); cursor(-4); W[0x8240] = 1;
    fill_rect(0, 0, 0x140, 200, 0);
    calendar_show(1);                              /* September 15 with the X */
    pal_reg(12, 5);                                /* 0f38:17fc */
    DRV_78C2();
    wait_ticks(300);
}

int game_over_menu(void)                           /* 0000:315b */
{
    f0f38_2022(0xBE);
    save colours 8242..8245; W[0x8240] = 1;
    if (drv == -2 || drv == -6) { B[0x8242]=0x0F; B[0x8243]=1; B[0x8245]=0x0D; B[0x8244]=3; }
    else                        { B[0x8242]=3;    B[0x8243]=0; B[0x8245]=2;    B[0x8244]=1; }
    fill_rect(0, 0, 0x140, 0xBE, B[0x8243]);
    frame(front, 100, 0x32, 0x78, 0x5A); frame(back, 100, 0x32, 0x78, 0x5A);
    frame(front, 0x66, 0x34, 0x74, 0x56); frame(back, 0x66, 0x34, 0x74, 0x56);   /* 0f38:45d7 */
    restore colours;
    cursor(-2); DRV_78C2();
    r = ui_menu(0x29);             /* "Load Game" -2 (L), "New Game" -1 (N), "Quit" 4 (Q) */
    cursor(-4);
    if (r > -3 && (u16)r >= 0x8000 && W[0x4F4C]) {   /* -2 or -1 after the ending freed the pictures */
        pic_load_list(DS:4C28, 1);
        W[0x4F4C] = 0;
    }
    return r;
}

int hall_score(void)                               /* 0000:3375 */
{
    clock_hm(&h, &m);
    return day_index() * 0x18 + h + 0x0C;           /* hours since June 16, 00:00 (noon = first hour) */
}

bool hall_is_slower(int i) { return atoi(hall[i].hours) > W[0x6C5E]; }          /* 0000:32d0 */

void hall_insert(int i)                            /* 0000:32f4 */
{
    n = hall_count - i - 1;                        /* hall_count already incremented (or 10) */
    if (i < 0) return;
    if (n > 0) memmove(&hall[i+1], &hall[i], n * 0x17);   /* 1e16:21cc */
    strcpy(hall[i].name, player_name);
    itoa(W[0x6C5E], hall[i].hours, 10);
}

void hall_of_fame_screen(int won)                  /* 0000:339f */
{
    s = W[0x04C2]; row = -1;
    DRV_78BE();
    W[0x04C2] = 200; W[0x8240] = 1;
    pic_draw(5, -1);                               /* LIB1 #4 "King Street Rodders" wall */
    W[0x04C2] = s;
    pal_swap();                                    /* 0f38:182c: DS:0440 <-> DS:0462 */
    save font colours DS:787E+0x23..0x2C; on EGA/Tandy replace them with DS:4BE2..4BEC;
    DRV_78C2();
    if (won) {
        W[0x6C5E] = hall_score();
        for (i = 0, row = -1; i < hall_count; i++)
            if (hall_is_slower(i)) { row = i; break; }
        if (hall_count < 10) { if (row < 0) row = hall_count; hall_count++; }
        hall_insert(row);                          /* full table and slower than all: no entry */
    }
    if (drv EGA/Tandy) { B[0x8242]=4; B[0x8243]=0; } else { B[0x8242]=3; B[0x8243]=2; }
    for (i = 0; i < hall_count; i++) {
        y = W[0x4BCA + 2*i];                       /* 0x3B + 11*i */
        DRV_78AE(page, 0x2E, y, hall[i].name);
        x = 0xA1;
        for (v = atoi(hall[i].hours); v < 1000; v *= 10) x += 6;   /* right-align; v = 0 would hang */
        DRV_78AE(page, x, y, hall[i].hours);
        DRV_78AE(page, 0xBF, y, "hr");             /* DS:4BDE */
    }
    copy_rect(0x2E, W[0x4BCA], 0xD3, W[0x4BDC] + 8);
    if (row < 0) wait_or_key(700);
    else {
        strcpy(tmp, hall[row].name);
        text_input(0x2E, W[0x4BCA + 2*row], tmp, 0x0F);   /* the player may edit the name */
        strcpy(hall[row].name, tmp);
        hall_save();
    }
    DRV_78BE();
    restore font colours;
    pal_swap();
}
```

### 4.10 Win: ending_sequence (0000:a544)

Called by `bobs_drive_in` after the race against the King is won; afterwards DS:0282 |= 2.

```c
void ending_sequence(void)
{
    cursor(-4); f0f38_2022(0xBE);
    pic_draw_at(9, 8, 0x14, 2);                    /* LIB1 #8 "Well, you've done it..." (304x157), clear colour 2 */
    W[0x8240] = 0; s = W[0x04C2]; W[0x04C2] = 200;
    DRV_78B6(page copy);
    pic_draw(0x408, -1);                           /* LIB2 #32 garage, off screen */
    W[0x04C2] = s;
    DRV_78C2();
    wait_ticks(0x2EE);                             /* 750 ticks ≈ 10 s, input skips */
    DRV_78CA(page copy);                           /* garage visible */
    W[0x0284] = 1;
    save 0x50x0x2C at (0x78,0x45);
    pic_free_list(DS:4C28); W[0x4F4C] = 1;         /* room for the LIB1 pictures */
    pic_load_list(DS:5318, 0); pic_load_list(DS:5328, 0);
    ... draw the King's car (8e48, model DS:7648) and the King's face (DS:8176 = 0x4C1 then 0x4DC) ...
    faces = pic_load_buffer(6, 1);                 /* LIB1 #5 girlfriend faces 200x200 */
    for (i = 1; i < 0x14; i++) {                   /* 19 frames, 6-byte records at DS:5270 */
        if (i == 0x0D) sound_fx(0x14);
        blit a frame (src x,y,w,h from the record) into a 0x50x0x2C window, show at (0x78,0x45);
        if (i < 0x13) { sound_fx(10); show on the front page; }
        if (i == 0x12) also on the back page;
    }
    lips = pic_load_buffer(7, 1);                  /* LIB1 #6 lips 272x199 */
    for (k = 0; k < 9; k++)                        /* 8 on Tandy; records at DS:52E2 */
        grow the lips frame k over the screen (sound_fx(8) per step);
    wait_ticks(0x50); sound_fx(1); final frames; wait_ticks(0x32);
    msg_box(0x183C);                               /* "I knew you could do it! You're my hero!" */
    W[0x8240] = 0;
    f0f38_9ebc();
    pic_free_list(DS:5328); pic_free_list(DS:5318);
}
```

The frame tables (DS:526A, DS:5270 19×6, DS:52E2 9×6) and the exact blit rectangles are listed in
the decompile; the port should copy the arithmetic from `sr_ds.c` lines 9960–10150 verbatim
(video spec primitives `DRV_78A2`/`78AA` = masked/unmasked rectangle copy between bitmaps).

### 4.11 Save, load and the slot list

```c
void save_slots_scan(int for_save)                 /* 0000:59f6 */
{
    char name[16]; char hdr[0x24];
    name[0] = data_disk(); strcpy(name+1, ":HOTROD");
    n_list = 0;
    for (n = 1; n <= 15; n++) {
        itoa(n, &name[8], 10); strcat(name, ".SAV");           /* over the NUL of ":HOTROD" */
        if (_dos_open(name, 0, &fd) == 0) {
            if (_dos_read(fd, hdr, 0x24, &got) == 0 && got == 0x24) {
                B[0x4FC2 + n] = n;
                list[++n_list] = W[0x4FD4 + 2*(n-1)];
                strncpy(DS:2C02 + list[n_list], hdr /* name */, 0x0F);
            } else B[0x4FC2 + n] = 0;
            _dos_close(fd);
        } else B[0x4FC2 + n] = 0;
        if (for_save && B[0x4FC2 + n] == 0) {
            list[++n_list] = W[0x4FD4 + 2*(n-1)];
            strcpy(DS:2C02 + list[n_list], "_______________");  /* DS:4607 */
        }
    }
    W[0x49E0] = n_list;
}
```

The name buffer is `{drive, ":HOTROD\0"}` (9 bytes, copied once before the loop);
`itoa(n, &name[8], 10)` writes the number over the terminating NUL and `strcat(name, ".SAV")`
follows, so every iteration yields `X:HOTROD<n>.SAV` (`0000:5a06`–`5a67`). `load_game_screen` and
`save_game_screen` build the name the same way.

```c
int load_game_screen(void)                         /* 0000:5b4a */
{
    cursor(-4);
    save_slots_scan(0);
    s = W[0x8240]; W[0x8240] = 0;
    pic_draw(0x3F7, 0);                            /* LIB2 #15 Highway Patrol book */
    if (W[0x49E0]) {
        pic_draw_at(0x3FA, 0xF0, 100,  -1);        /* LIB2 #18 "load" */
        pic_draw_at(0x3FC, 0xF0, 0x8C, -1);        /* LIB2 #20 "forget it" */
    }
    W[0x8240] = s; if (s) copy_rect(0, 0, 0x140, 0xBE);
    cursor(-2);
    if (W[0x49E0] == 0) { ui_menu(0x0F); return -1; }   /* "No saved games found !" [OK] */
    r = ui_list(3, 1, 0, DS:49E0, 0, 0);           /* rows 1..15 (keys 1-0, F1-F5), -3 Load (L/Enter), -4 Forget it (F/Esc) */
    if (r == -4) return -1;
    if (r == -3) r = ui_list_row();
    for (slot = 1, k = 0; k < r; slot++) if (B[0x4FC2 + slot]) k++;
    slot--;                                        /* r-th existing file */
    cursor(-4);
    build name "X:HOTROD<B[0x4FC2+slot]>.SAV";
    if (_dos_open(name, 0, &fd) == 0 && read(fd, DS:7E9A, 0x24) == 0x24) {
        pools_init(0);                       /* header pointers now act as "present" flags */
        if (!load_car_chain(fd, &cur_car, 1) && !load_car_chain(fd, &car_list, 0)
            && !load_part_chain(fd, &spare_parts)
            && read(fd, DS:4970, 8) == 8 && read(fd, DS:7FF8, 0x18C) == 0x18C
            && read(fd, DS:05FC, 4) == 4 && read(fd, &elapsed, 4) == 4
            && read(fd, DS:5E0C, 2) == 2) {
            _dos_close(fd);
            if (cur_car) set_car_palette(B[cur_car+4]);   /* 0000:67cf */
            f2645_0b91(W[0x5E0C]);
            rt_base = rt_ticks - elapsed;
            cursor(-2);
            return 0;
        }
    }
    cursor(-2);
    ui_menu(0x1F);                                 /* "Cannot read that game !" [OK] */
    cursor(-3);
    if (fd >= 0) { _dos_close(fd); new_game(); }   /* fd is uninitialised when the open failed */
    cursor(-1);
    return -0x29A;
}

int load_car_chain(int fd, u16 *head, int one)    /* 0000:590f */
{
    u8 rec[0x28]; u16 prev = 0;
    if (*head == 0) return 0;
    *head = 0;
    for (;;) {
        if (read(fd, rec, 0x28) != 0x28) return 1;
        if (load_part_chain(fd, &rec[0x06])) return 1;
        for (k = 0; k < 5; k++) if (load_part_chain(fd, &rec[0x08 + 2*k])) return 1;
        if (load_part_chain(fd, &rec[0x12])) return 1;
        c = car_alloc(); memcpy(c, rec, 0x28);
        if (prev == 0) *head = c; else W[prev+0x26] = c;
        if (W[c+0x26] == 0 || one) return 0;
        W[c+0x26] = 0; prev = c;
    }
}

int load_part_chain(int fd, u16 *head)            /* 0000:57dc */
{
    u8 rec[8]; u16 prev = 0;
    if (*head == 0) return 0;
    *head = 0;
    for (;;) {
        if (read(fd, rec, 8) != 8) return 1;
        p = part_alloc(); memcpy(p, rec, 8);
        if (prev == 0) *head = p; else W[prev+6] = p;
        if (W[p+6] == 0) return 0;
        W[p+6] = 0; prev = p;
    }
}
```

With `one` = 1 the current car's own `+26` is ignored (it is always 0 in a saved game anyway).

```c
int save_part_chain(int fd, u16 p)                 /* 0000:5795 */
{ for (; p; p = W[p+6]) if (write(fd, p, 8) != 8) return 1; return 0; }

int save_car_chain(int fd, u16 c, int one)         /* 0000:586e */
{
    for (; c; c = W[c+0x26]) {
        if (write(fd, c, 0x28) != 0x28) return 1;
        if (save_part_chain(fd, W[c+6])) return 1;
        for (k = 0; k < 5; k++) if (save_part_chain(fd, W[c+8+2*k])) return 1;
        if (save_part_chain(fd, W[c+0x12])) return 1;
        if (one) return 0;
    }
    return 0;
}

void save_game_screen(void)                        /* 0000:5eea */
{
    sel = 0;
    cursor(-4);
    save_slots_scan(1);                            /* all 15 rows, row n = slot n */
    s = W[0x8240]; W[0x8240] = 0;
    pic_draw(0x3F7, 0);                            /* book */
    pic_draw_at(0x3F9, 0x48, 0xA9, -1);            /* LIB2 #17 name field */
    pic_draw_at(0x3FB, 0xF0, 100,  -1);            /* LIB2 #19 "save" */
    pic_draw_at(0x3FC, 0xF0, 0x8C, -1);            /* "forget it" */
    W[0x8240] = s; if (s) copy_rect(0, 0, 0x140, 0xBE);
    strcpy(DS:2A2E, player_name);
    for (;;) {
        do {
            for (;;) {
                for (r = 1; r <= W[0x49E0] && B[0x4FC2 + r]; r++) ;   /* first empty slot */
                if (r > W[0x49E0]) r = W[0x49E0];
                if (sel == 0) sel = r;
                cursor(-2);
                r = ui_list_edit(5, sel, 0, DS:49E0, 0, 0);   /* menu 5: rows, edit field DS:2A2E
                                                    ("Click here to edit" -6), Save -3 (S/Enter), Forget it -4 */
                if (r == -4) return;
                if (r == -3) r = ui_list_row();
                else if (r < 1 || r > W[0x49E0]) return;
                sel = r;
                if (!str_is_blank(DS:2A2E) || !str_is_blank(DS:2C02 + list[r])) break;
                flash_text(W[0x4FF2]);             /* ";The game must have a name !" */
                cursor(-4);
            }
            /* a typed name equal to an existing slot's label selects that slot */
            for (k = 1; k < 0x10; k++) if (strcmp(DS:2C02 + list[k], DS:2A2E) == 0) break;
            if (k < 0x10) { highlight row k (xor boxes on both pages); sel = r = k; }
            build name "X:HOTROD<r>.SAV";
            ans = B[0x4FC2 + r] ? ui_dialog(0x1E) : 0;   /* "The old game will be replaced !"
                                                       Go ahead -20 / Forget it -21 */
            cursor(-4);
        } while (ans == -21);
        if (ans == -20) _dos_unlink(name);
        strcpy(player_name, str_is_blank(DS:2A2E) ? DS:2C02 + list[r] : DS:2A2E);
        elapsed = rt_ticks - rt_base;
        name[0] = data_disk();
        if (_dos_creat(name, 0, &fd) == 0
            && write(fd, DS:7E9A, 0x24) == 0x24
            && !save_car_chain(fd, cur_car, 1) && !save_car_chain(fd, car_list, 0)
            && !save_part_chain(fd, spare_parts)
            && write(fd, DS:4970, 8) == 8 && write(fd, DS:7FF8, 0x18C) == 0x18C
            && write(fd, DS:05FC, 4) == 4 && write(fd, &elapsed, 4) == 4
            && write(fd, DS:5E0C, 2) == 2) {
            _dos_close(fd); cursor(-2); return;
        }
        cursor(-2);
        ui_dialog(6);                              /* "Disk error during Save" [OK] */
        cursor(-4);
        if (fd >= 0) { _dos_close(fd); _dos_unlink(name); }
        save_slots_scan(1);
    }
}

int str_is_blank(const char *s)                    /* 0000:5eb3 */
{ for (; *s; s++) if (*s != ' ' && *s != '_') return 0; return 1; }

int quit_menu(void)                                /* 0000:641e */
{
    bg = save_under_menu(0x1B, &w, &h, 2);         /* 0f38:4d10 */
    r = ui_menu(0x1B);   /* "Save, Restart, or Quit.": Save Game -3 (S/Enter), Old Game -2 (O),
                            New Game -1 (N), Quit -20 (Q), Forget it -22 (F/Esc) */
    restore(bg, w, h); f0f38_a0b8(1);
    return r;
}
```

The string compare in `save_game_screen` walks all 15 entries of `list[]` even when fewer are
valid; in save mode `list[]` always has 15 entries, so this is safe.

### 4.12 Copy protection (0000:2fc8) — skipped in the port

```c
int copy_protection(void)
{
    i = rnd(0x33);                                 /* 51 questions, table DS:4B62 {page, colour} */
    icon = (i < 0x12);
    status_text(-1, DS:4AFE, 0); W[0x8240] = 0;
    pic_draw_at(8, 8, 0, 0);                       /* LIB1 #7 juke box with 12 colour buttons */
    pic_free(8);
    if (!icon) { sprintf(l1, "What`is`the`color`of"); sprintf(l2, "the`car`key`on`page`%d?", B[0x4B62+2i]); x2 = 0x72; }
    else       { sprintf(l1, "What`is`the`color`of"); sprintf(l2, "    `the`car`icon`on`page`%d?  ", B[0x4B62+2i]); x2 = 0x55; }
    text(0x79, 0x3F, l1); text(x2, 0x48, l2);
    cursor(-2); DRV_78CA(copy);
    if (drv == -2 || drv == -6) pal_set(DS:0692); else DRV_78C2();   /* 0000:30b5 */
    B[0x8249] = 1; W[0x8240] = 1; W[0x4BC8] = 1;
    a = ui_menu(0x27);                             /* colours 1..12 = Grey, Brown, Red, Pink, Orange, Yellow,
                                                      Dark green, Light green, Dark blue, Light blue,
                                                      Dark purple, Light purple (keys 1-0, F1, F2) */
    W[0x4BC8] = 0;
    status_text(-1, DS:4B61, 0);
    return B[0x4B63 + 2*i] == a;                   /* 0000:314e: je → 1, else 0 */
}
```

Table DS:4B62 (page:colour): 2:2 3:11 4:6 6:5 7:9 8:1 9:4 10:6 11:10 12:1 14:7 15:12 16:2 18:3
19:11 20:5 22:10 23:6 | 1:3 2:5 3:9 4:4 5:8 6:10 7:7 8:2 9:12 10:5 11:1 12:3 13:11 14:9 15:8 16:4
17:6 18:1 19:10 20:8 21:9 22:12 23:7 24:2 25:3 26:1 27:8 28:5 29:11 30:10 31:4 32:12 33:9
(first 18 = car icon questions, rest = car key).

**Patches.** Comparing `work/SR_unp.exe` with `work/SRSE_unp.exe` (41 differing bytes):

| image | SR.EXE (this copy) | SRSE.EXE | effect |
|---|---|---|---|
| `0000:3a1a` | `EB 0C` (jmp over the call) | `9A C8 2F 00 00` `call 0000:2fc8` | SR.EXE never runs the protection |
| `0000:30b5` | `83 3E 36 82 FE 74 07` (original) | `B8 01 00 8B E5 5D CB` (`mov ax,1; leave; retf`) | SRSE draws the question off screen, then returns 1 before showing it |
| `0000:314e` | `90`×6 (always 1) | `74 04 2B C0 EB 03` (original compare) | SR.EXE accepts any answer |

So the shipped `Game/SR.EXE` is itself a cracked copy; the untouched routine is SR's bytes at
`30b5` plus SRSE's bytes at `3a1a` and `314e`. The port does neither: `title_and_setup` does not
call it (`/* PORT: copy protection skipped, PLAN.md decision 3 */`). Demo mode never asked.

### 4.13 Picture / data loading helpers owned here

```c
void hot_data_load(void)                            /* 0f38:6b79 */
{
    path = { B[DS:8DD6][0], ':' } + "hot_data";    /* DS:6C72, the LIB2 drive */
    if (_dos_open(path, 0, &fd)) { fatal("Can't open data file"); }
    for (k = 0; k < 13; k++)
        if (_dos_read(fd, W[0x4EA4 + 2k], W[0x4EBE + 2k], &n) || n != W[0x4EBE + 2k])
            fatal("Data disc read fail");          /* 0f38:23ce + exit_to_dos */
    _dos_close(fd);
    hall_load();
}

void hall_load(void)                               /* 0f38:6d09 */
{
    s_hall[0] = B[DS:8DD6][0];                     /* "?:hall_dat" at DS:4F3A */
    data_disk();
    if (_dos_open(s_hall, 0, &fd) == 0 && fd != 0) {
        if (read(fd, &hall_count, 2) != 2) fatal(DS:4EE8 "bad file format");
        for (i = 0; i < hall_count; i++)
            if (read(fd, 0x82CA + i*0x17, 0x17) != 0x17) fatal("bad file format");
        _dos_close(fd);
        hall_unscramble();                         /* all 0xE6 bytes &= 0x7F */
    } else hall_count = 0;
}

void hall_save(void)                               /* 0f38:6dd2 */
{
    hall_scramble();                               /* all 0xE6 bytes |= 0x80 */
    data_disk();
    rc = _dos_creat(s_hall, 0, &fd);
    if (drive < 3) {                               /* floppy: ask for the disk, 3 tries then exit */
        while (fd == 0 || rc) { if (++tries == 3) exit_to_dos(); prompt("insert disk 1 into drive ?");
                                rc = _dos_creat(s_hall, 0, &fd); }
    } else if (fd == 0 || rc) { fatal("write error"); }
    if (write(fd, &hall_count, 2) == 2) {
        for (i = 0; i < hall_count; i++) if (write(fd, 0x82CA + 0x17*i, 0x17) != 0x17) goto err;
        if (_dos_close(fd) == 0) goto done;
    }
err:
    prompt("write error"); _dos_unlink(s_hall); prompt("write error");
done:
    hall_unscramble();
}
```

### 4.14 Demo mode

`demo` on the command line sets DS:8BCE; `title_and_setup` copies it to DS:0610 (which the rest of
the program tests). Effects found:

* no copy protection; starting money $10000 instead of $750; `classifieds` adds $1999 whenever the
  player cannot afford an ad; opponent 0 is "The Geek" with model 0x18;
* the timer ISR (`0000:1fe2`) calls `demo_step` every 128 ticks (every 16 while a key release is
  pending, DS:06A6): it picks a hotkey of the current menu (DS:6C4C, key table DS:579F) at random,
  with per-menu rules (garage: cycles through its keys, `N` when there is no car; menu 0x14 plays the
  script DS:06B4 …), moves the cursor (DS:06AE/06B0 targets) to the element with that code, then
  injects the scancode through `kbd_inject_scancode` (`0000:2e93`) and its release next time; it
  also gives a free spare part when the newspaper is opened with none;
* races drive themselves (the same as `auto`, see race); the joystick and mouse are not read
  (`DS:0610 == 0` guards);
* Esc (scancode 1) in the keyboard ISR sets DS:0610 = 0x63; the next `event_loop` pass or race
  frame calls `exit_to_dos`, which shows "Quit demo" (DS:0614) and clears the screen.

`auto` (DS:0612) only has the self-driving effect in the race code.

---

## 5. File formats

### 5.1 Save game `?:HOTRODn.SAV` (n = 1..15)

Written by `save_game_screen`, read by `load_game_screen`; located on the drive of the LIB2 files
(`data_disk()` letter + `":HOTROD"` + decimal n + `".SAV"`). No magic, no version, no checksum;
little-endian; length depends on the number of cars and parts. It is a raw dump of the DGROUP
state, with near pointers (DGROUP offsets of the saving program) kept only as "non-zero = present /
more follow" flags.

```
off   size   content
0     0x24   header = DS:7E9A..7EBD verbatim
             +00 char name[16]     player name, NUL-terminated (≤15 chars); the load list shows
                                   its first 15 characters
             +10 i32  money
             +14 u16  DS:7EAE (10)
             +16 u16  cur_car      ≠0: one CAR BLOCK follows
             +18 u16  car_list     ≠0: CAR BLOCKs follow until one has +26 == 0
             +1A u16  car_freelist ignored on load
             +1C u16  car_count    ignored (recounted)
             +1E u16  spare_parts  ≠0: a PART CHAIN follows
             +20 u16  part_freelist ignored
             +22 u16  part_count   ignored (recounted)
             [CAR BLOCK]           if cur_car
             [CAR BLOCK]*          if car_list
             [PART CHAIN]          if spare_parts
+0    8      DS:4970 races_run, drag_wins, road_wins, king_man_beaten (4 × i16)
+8    0x18C  DS:7FF8..8183 opponent table (22 × 0x12, see 5.4)
+194  4      DS:05FC game_clock (u32)
+198  4      rt_ticks − rt_base at save time (u32, 18.2 Hz) — restored as rt_base = now − value
+19C  2      DS:5E0C
```

```
CAR BLOCK:  0x28 bytes car record, then for each pointer field in the order
            +06, +08, +0A, +0C, +0E, +10, +12: if the field ≠ 0, a PART CHAIN.
PART CHAIN: 8-byte part records; a record whose +06 ≠ 0 is followed by another.
```

Car record (0x28 bytes, pool DS:83B0; fields from `car_create` 0000:4036 — the garage spec owns
the details):

```
+00 u16 value ($)            +02 u16 model (0..24; 0xFFFF = free pool slot)
+04 u8  colour (0..5)        +05 u8  model class (copy of DS:7D88[model*10])
+06 ptr engine               +08 ptr transmission
+0A ptr manifold             +0C,+0E,+10 ptr carburettors (1..3, unused = 0)
+12 ptr tires                +14..+1E condition bytes (body panels 3, carbs …)
+1F u8  1                    +20 i8  rnd(13) − 8
+22 u16 fuel (0x28 new; < 9 cannot reach Bob's, ≥ 0x8C full)
+24 u16 flags: bits 8..12 paint, 13 roof chopped, 14 rear bumper, 15 front bumper (see 49a2)
+26 ptr next car
```

Part record (8 bytes, pool DS:866A): `+00 u16 value, +02 u16 wear, +04 u16 part type (index into
the 8-byte catalogue DS:4806; 0xFFFF = free pool slot), +06 ptr next`.

Loading reallocates cars and parts from the freshly reset pools in file order, so a game loaded by
the port lands at the same DGROUP addresses as in the original. The port must write the pointer
fields as they are in `mem[]` (their only meaning in the file is zero / non-zero).

Game data it does **not** contain: the car models, parts catalogue, prices (HOT_DATA / exe), the
calendar headline (DS:4F58, re-picked), music state, `hall_dat`.

### 5.2 `HOT_DATA` block map (read by `hot_data_load`)

13 consecutive blocks, copied verbatim into DGROUP (tables DS:4EA4 destinations, DS:4EBE sizes):

| # | file offset | size | DS dest | content (owner) |
|---|---|---|---|---|
| 0 | 0x000 | 0x18C | 7FF8 | opponent table incl. the King (game_flow §5.4 / race) |
| 1 | 0x18C | 0x104 | 7D86 | 26 × 10 car model records (garage) |
| 2 | 0x290 | 0x104 | 8DF0 | 26 × 10 per-model data (race) |
| 3 | 0x394 | 0x138 | 7682 | (garage/race) |
| 4 | 0x4CC | 0x208 | 70E4 | 26 × 20 per-model hot-spot rectangles (garage, `68e8`) |
| 5 | 0x6D4 | 0x09C | 818C | (race) |
| 6 | 0x770 | 0x73A | 2C02 | text block: message ids 0..0x739 (car and opponent names …) |
| 7 | 0xEAA | 0x002 | 7D16 | head of a model chain (classifieds) |
| 8 | 0xEAC | 0x0C8 | 239E | UI label area (the save-slot underscores) |
| 9 | 0xF74 | 0x012 | 7D68 | |
| 10 | 0xF86 | 0x012 | 8BAE | |
| 11 | 0xF98 | 0x018 | 8DD8 | |
| 12 | 0xFB0 | 0x030 | 784A | |

Total 0xFE0 = 4064 bytes. The original `Game/HOT_DATA` is 4116 bytes: the last 52 bytes are never
read; the Street Rod SE file is exactly 4064.

### 5.3 `?:hall_dat` — the King Street Rodders wall

On the LIB2 drive (`"?:hall_dat"`, DS:4F3A, first character patched). Not shipped; created by the
first win. Absent or unopenable → empty wall.

```
u16  count                 0..10, stored plain
count × 23 bytes           every byte OR 0x80 on disk (hall_scramble)
   +00 char name[16]       NUL-terminated (on disk the NUL is 0x80)
   +10 char hours[7]       decimal ASCII (itoa), NUL-terminated
```

Rows are sorted by `hours` ascending (fewest game hours to beat the King first); a new winner is
inserted before the first slower entry, the 11th entry falls off, and a winner slower than all ten
is not entered. Bytes after each NUL are whatever was in memory (a port writes them unchanged).
A short file (`count` larger than the records) makes `hall_load` exit to DOS with "bad file format".

### 5.4 Opponent record (DS:7FF8 + 0x12·i, i = 0..21; #21 = the King)

As far as game_flow touches it (race spec owns the meaning):

```
+00 u8  class (0 slow, 1 medium, 2 fast)     +01 u8, +02 u8, +03 u8 (from HOT_DATA)
+04 u16 face picture id (0x4A2..0x4DC)       +06 u16 name message id (0x593 "Mike", 0x61F "The King",
                                                     0x629 "The Geek" in demo)
+08 u16 car model (0xFFFF in HOT_DATA, set by opponent_init)
+0A u8  car colour                           +0B u8  status (race: −1 = no longer racing?)
+0C,+0D u8 counters (ab8b: races, wins)      +0E u8  flags (8 auto trans., |3, |4)
+0F u8  rnd(9) or 0                          +10 u8
```

---

## 6. Hardware / DOS dependencies

| where | what | SDL3 port |
|---|---|---|
| 59f6, 5b4a, 5eea, 6b79, 6d09, 6dd2 | INT 21h via MSC `_dos_open` (1e16:2330), `_dos_creat` (2305), `_dos_read` (2348), `_dos_write` (234f), `_dos_close` (22f0), `unlink` (22de) | host file API relative to `--game-dir` (saves and `hall_dat` next to LIB2); keep the exact sizes and order |
| 6943, 6c3d, 6dd2 | drive letters from `_dos_getdrive`, floppy "insert disk" prompts (3 tries, then exit) | no drives: `data_disk()` returns the game directory; drop the prompts |
| main, 0bf0, 2fc8 | `pal_set` = INT 10h AX=1002h (all 16 EGA palette registers + overscan DS:8248) through `int86x` 1e16:1ce8; values ≥ 8 get bit 4 (`\|0x10`) | set the 16-entry palette of the EGA model (`platform/ega`). DS:0440 is the game palette (the title and garage only look right with it), DS:02D4 the credits palette, DS:0692 the protection screen. **FORMATS.md's "no palette code" is wrong** |
| 1f4b | INT 10h AX=1000h single register, after waiting for vertical retrace (port 3DAh bit 3) | same, no wait |
| 1fe2, 238c | timer ISRs (INT 8): DS:05F8 every tick, DS:0600 every 4th tick with BIOS chaining | host tick at the PIT rate (72.8 Hz) |
| 2c60 (kbd ISR) | demo: Esc → DS:0610 = 0x63 | host key event |
| 1bae / 2e93 | demo autoplayer called from the timer ISR | call from the host tick handler in the same place |
| main args | `1`, `nomouse`, `demo`, `auto`, `nouemem` | keep as command-line options |

---

## 7. Timing

* **Game calendar**: `T = game_clock + (rt_ticks − rt_base)`. `rt_ticks` runs at 18.2 Hz
  (72.8 Hz / 4) **all the time**, in menus, message boxes and races alike; one game hour = 0x222 =
  546 rt ticks = 30 s, one day = 12 hours = 6 min, the summer = 91 days ≈ 9 h 6 min of pure real
  time. The port must keep the 72.8 Hz tick and the divide-by-4.
* **Activity costs** (added to `game_clock`, u32 add with carry): newspaper visit 0x222; trip to
  Bob's or the gas station 0x222 + 0x222 after; buying a car in the ads 0x444, a part 0x222; some
  actions at Bob's 0x111 (race spec); garage screens add their own (garage spec).
* **Checks**: `summer_over_check` at every `garage_screen`, in `calendar_show`, and in the Bob's
  loop; the game ends at day 91 (> 90), i.e. September 15, 1963.
* **Waits** (72.8 Hz ticks, cut short by input): title 3 × 400; message box 350 (DS:05DC);
  calendar 500; summer-over 300; hall of fame 700 (no entry); ending 750, 80, 50 (+ frame delays
  through `sound_fx`).

---

## 8. Open questions

1. `new_game_prompt` accepts an empty name, and `hall_insert` then writes an empty name; confirm
   in DOSBox that the name field cannot be left empty by the UI.
2. `load_game_screen` closes `fd` and resets the game when `fd >= 0` after a failure — when
   `_dos_open` itself fails `fd` is an uninitialised stack word (behaviour depends on stack garbage;
   the port should treat it as "not open" and skip `new_game`? — the original usually resets,
   since a stale positive value is likely). Decide after a DOSBox test.
3. `DS:7EAE` (=10) and `DS:5E0C` have no readers in the game_flow code; `5E0C` goes to the garbled
   `2645:0b91` (race). Both must still be saved.
4. The exact semantics of the driver vectors DS:78BE/78C2/78CA/78B6 (page set-up, show, copy) and
   when DS:0440 is uploaded again after the credits palette (probably by the show vector) — video spec.
5. `garage_screen` codes other than 1..17 and −40 (e.g. from the type-6 element of menu 10, code
   51) fall back into the new/load loop with `again = 0`: −1 → new game, anything else → load
   screen. Check whether such a code can occur.
6. `9556 king_ready` thresholds and the King's race conditions (DS:7EC0, DS:7D40) — race spec.
7. `ending_sequence` frame tables (DS:526A, 5270, 52E2) and the King's face ids (DS:8176 = 0x4C1,
   0x4DC) — to be transcribed when the port implements the ending.
