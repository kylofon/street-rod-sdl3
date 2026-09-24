# garage — Street Rod (1989), SR.EXE segment `0000:3614`–`8d26` (+ `0f38` helpers)

Porting spec for everything that happens off the road: the car and part data model, the garage
screen and its hot spots, the newspaper and its classified ads (Used Cars, Auto Parts), buying,
selling and haggling, the spare-part lists, changing tyres and transmission, the engine bay ("Pop the
hood"), tuning the ignition, painting, stickers, chopping the roof and stripping bumpers, "Car info",
the calendar, and the gas station (whose code sits in the race range but is a garage-style screen).

Conventions follow `port/RE_GUIDE.md`: code `SSSS:OOOO` as stored in the file, `DS:xxxx` = DGROUP
`3E96`. Every function here was read in `port/decomp/sr_ds.c`; **the two floating-point functions
(`3c8d`, `49ed`) and the stock-speed routine `e218` were decoded from the disassembly** because
Ghidra does not understand the MSC floating-point emulator interrupts (`INT 34h`–`3Dh`; the decompile
of `3c8d` even swaps the switch cases). To read them, patch `CD 34+n` → `9B D8+n`, `CD 3D` → `90 9B`
before disassembling (a helper is described in §8). Ground truth for doubt: `tools/x86dis.py`.

Data tables are printed by **`python tools/srtables.py [exe] [hot_data]`** (new tool, this spec):
part catalogue, car models, picture tables, compatibility, stock top speeds, opponents, layout blocks.
Pass `Game/datadisk/HOT_DATA` to see the Street Rod SE cars.

Names of functions owned by other specs are provisional *(prov.)* descriptive names; the owning spec
may rename them. Text references: **message id `n` = string at `DS:2C02+n`** (the byte before it,
`DS:2C01+n`, is the number of lines / box style); **button label id `n` = `DS:239E+n`**.

---

## 1. Overview

### 1.1 What the player sees

The garage (`0000:6b06`, picture LIB2 #32) is a full-screen picture with 18 hot spots (records
`0x74`–`0x85` of the screen-record table, §5.9). The current car stands in the middle; five car hot
spots (hood, transmission, roof, front and rear bumper) are moved onto the car picture per model.
`game_loop` (`0000:503f`, game_flow) dispatches the code returned by the garage:

| code | hot spot label | handler |
|---|---|---|
| 1 | Check out the newspaper | `newspaper_front` `0000:3b33` → `classifieds` `0000:442b` (1 = Used Cars, 2 = Auto Parts) |
| 2 | Hit the street | checks, then drive to Bob's (`race`: `8d26`, `ab8b`) |
| 3 | Calendar | `calendar_show(0)` `0000:65af` |
| 4 | Time to quit | save / load / quit menu `0000:641e` (game_flow) |
| 5 | Sell parts | `sell_spare_parts` `0000:3c8d` |
| 6 | Cars | `your_cars` `0000:49ed` (Switch it / Sell it) |
| 7 | New paint job - $20 | `paint_job` `0000:6f6b` |
| 8 | Change tires | `change_tires` `0000:7800` |
| 9 | Change transmission | `change_transmission` `0000:7aa4` |
| 10 | Pop the hood | `engine_bay(cur_car)` `0000:81ab` (incl. tuning `0000:7fb1`) |
| 11 / 12 / 13 | Rear bumper / Front bumper / The roof | `customize(2 / 1 / 4)` `0000:711a` |
| 14 | Stickers - $5 | `stickers` `0000:7367` |
| 15 | Wanna tank ? | checks, then gas station (`0000:b8a1`) |
| 16 | Car info | `car_info` `0000:79eb` |
| 17 | Wanna drink ? | message "Don't drink and drive, bub!" (`0x18f6`) |
| −40 | Squelch it ! | toggles the music (`DS:58D0`, sound: `0000:0dd8` / `0d99`) |

The juke box ("Catch some tunes") and the drive-in are in the race range (Bob's Drive-In) and are
not garage screens.

Checks done by `game_loop` before codes 2 and 15 (for reference; game_flow owns the code):
`garage_full_check()` must pass; no `cur_car` → `0xEFA` "You've got no car, dummy!" (or `0xF16`
"Pick a car first!" if `other_cars`); `car_runnable(cur_car, 1) == 0` → `0xE7F` "Your car's not
running, speedy!"; then `0000:dcbe(cur_car, DS:78E0)` (race set-up). Code 15: `gas < 0x8C` and
`money != 0` → `location = 3`, drive (`8d26`), `game_clock += 0x222`, `gas_station()`,
`game_clock += 0x222`; `money == 0` → `0x199F` "Gas ain't free!"; `gas ≥ 0x8C` → `0x19B1` "You've
got a full tank!". Code 2: `gas > 8` → `location = 2`, drive, `+0x222`, town (`ab8b`), `+0x222`;
else `0x172E` "You don't got enough gas to cruise down to Bob's.". Code 1 (newspaper) and the start
of a new game go through `newspaper_front` only when `cars_used < 16 && parts_used < 0x83`
(code 1 first calls `garage_full_check`), then `game_clock += 0x222`.

### 1.2 Call graph

```
game_loop 0000:503f (game_flow)
├─ new_game 0000:56a9 ── pools_init 0000:3614, opponents_init 0000:5584 ── opponent_init 0000:542e
├─ newspaper_front 0000:3b33 ── day_index 6476, date_print 6750 (month_of_day 653c)
├─ classifieds 0000:442b ── classifieds_open 0000:08b4, classifieds_run 0000:09cb (platform/UI)
│     car_new 0000:4036, spare_add 0000:3ff0, set_paint_palette 67cf, car_draw 0f38:8e48, message 3ab8
├─ garage_screen 0000:6b06 ── car_flags 49a2, set_paint_palette 67cf, car_draw 0f38:8e48,
│     garage_hotspots_nocar 68b3 / garage_hotspots_car 68e8 (hotspot_find 6827), garage_hotspots_register 6882,
│     garage_name_clear 6aa6, broke_check 6ad8, summer_over_check 6529, ui_run 0f38:4f14(10)
├─ sell_spare_parts 0000:3c8d ── part_value 3c5a, list_box 0f38:4f4f, dialog 0f38:4ec1, part_free 3c3e
├─ your_cars 0000:49ed ── list_box, edit_number 0f38:5da7, how_about 4940, car_free 43bf (part_release 4386)
├─ paint_job 0000:6f6b ── ega_set_palette 0f38:1f4b
├─ customize 0000:711a ── car_draw(8), sfx 0f38:7841
├─ stickers 0000:7367
├─ change_tires 0000:7800 ── spares_collect 76a7, spare_unlink 77b7, part_install 7741, tire_change_anim 0f38:be93
├─ change_transmission 0000:7aa4 ── spares_collect, part_install, part_uninstall 7789, 0f38:3074/31a5/32ce/3404/345b
├─ engine_bay 0000:81ab ── car_runnable 7ee6, ignition_tune 7fb1, spares_collect, part_install/uninstall,
│     0f38:3697/37fc/3a58/3c19/3d1c/3e35/3f2d/40ca (engine-bay drawing)
├─ car_info 0000:79eb ── car_info_list 78f1, car_runnable 7ee6, top_speed 0000:e218 (race range, §4.16)
├─ calendar_show 0000:65af ── day_index, clock_hm 64a3, month_of_day
└─ gas_station 0000:b8a1 (race range) ── b61b/b4de/b525/b552/b59b/b617, gas_leave b717
```

### 1.3 What `race` must know (interface)

* **Current car** `DS:7EB0` (0x28-byte record, §4.1). Parts hang off it by pointer: `+6`
  transmission, `+8` engine, `+0xa` manifold, `+0xc/+0xe/+0x10` carburettors, `+0x12` tyres. Each
  part node (8 bytes) has `+4` = catalogue index into `DS:4806`, from which `grade` (`DS:4808`),
  `make mask` (`DS:4809`) and `category` (`DS:480a`) are read, and `+2` = **wear** in hundredths of
  a percent (0 = new, 10000 = worn out; −128 = does not wear: carbs and manifolds).
* **Top speed** (`0000:e218`, §4.16, in the race range) is the only closed formula for "how parts
  affect performance" in the garage: body mods (chop 9 %, front bumper 5 %, rear bumper 1 %),
  model class, carb/manifold match, engine grade, transmission grade, ignition timing, engine wear,
  automatic penalty. Car info shows it; the race uses the same routine at the start of a drive
  (`0000:cdaf` stores it in `DS:6CAC`).
* **Tyres**: race start `0000:cdaf` picks the grip from `DS:54B2[tyre_grade*5 + tyre_wear/2000]`
  and arms a blow-out when `tyre_wear ≥ 8000` and `wear + random > 0x251c` (see race).
* **Ignition timing** `car+0x20` (signed, −8…+4, set to `random(13) − 8` whenever a part of the
  engine stack is fitted or removed; the tuning screen moves it by 1 in −8…+4).
* **Gas** `car+0x22` in tenths of a gallon (new car 40 = 4 gal; the garage calls ≥ 140 "a full
  tank"; the pump sells 3-gallon units while `gas + 30 ≤ 190`; leaving the station clamps to 170;
  "Hit the street" needs > 8). Each drive through `0000:8d26` costs 2 (floored at 0).
* **Runnable** check `car_runnable(car, full)` `0000:7ee6` (§4.12): all parts present, all bolts
  tight (value 3), engine connected (`car+0x1f ≥ 1`).
* **Opponents** 22 records of 18 bytes at `DS:7FF8` (HOT_DATA block 0); `new_game` randomises the
  model / colour / customisation of records 0–20 (§4.14). Record 21 is The King (model 25).
* **Race-win counters** `DS:4970`–`4976` are zeroed by `new_game` and used by the race (`9556`,
  `9d6d`, `ab8b`: "You're gonna hafta win more drag races").
* The race builds / rebuilds cars with `car_new(model, value, reuse)` `0000:4036` (callers `bed4`,
  `c52c`, `c5ac`: pink slips and wrecks) and frees them with `car_free` `0000:43bf`.
* Car pictures for any screen go through `car_draw` `0f38:8e48` (§4.17).

---

## 2. Function table

| address | proposed name | signature | purpose | confidence |
|---|---|---|---|---|
| `0000:3614` | `pools_init` | `void(int reset_owner)` | build the free lists of 16 cars (`DS:83B0`) and 140 part nodes (`DS:866A`); with `reset_owner` also clear the owner's lists, money, `DS:7EAE=10` (game_flow range, documented here) | verified |
| `0000:3b0c` | `garage_full_check` | `int(void)` | 16 cars → "Sell some cars first!", ≥131 parts → "Sell some parts first!"; returns 1 if full | verified |
| `0000:3b33` | `newspaper_front` | `int(void)` | front page (LIB2 #21 + random headline #22–26 + date); returns 1 Used Cars, 2 Auto Parts, 4 garage | verified |
| `0000:3c1b` | `part_alloc` | `PART*(void)` | pop a part node from the free list, `DS:7EBC++` | verified |
| `0000:3c3e` | `part_free` | `void(PART*)` | push back, `type=-1`, `DS:7EBC--` | verified |
| `0000:3c5a` | `part_value` | `far int(PART*)` | 100 for a V-6 engine (category 0, grade 0), else `part->value` | verified |
| `0000:3c8d` | `sell_spare_parts` | `void(void)` | "Sell parts": list, offer (wear / random based), sell | verified (asm) |
| `0000:3fb1` | `car_alloc` | `CAR*(void)` | pop a car record, `DS:7EB6++` | verified |
| `0000:3fd4` | `car_release` | `void(CAR*)` | push back, `model=-1`, `DS:7EB6--` | verified |
| `0000:3ff0` | `spare_add` | `void(int type, int value)` | new part of catalogue `type` onto the spare list | verified |
| `0000:4036` | `car_new` | `CAR*(int model, int value, CAR *reuse)` | build a stock car of a model (all parts, bolts tight); new cars go to the owner | verified |
| `0000:4386` | `part_release` | `int(PART*, int keep)` | returns `value`; keep → spare list, else free | verified |
| `0000:43bf` | `car_free` | `int(CAR*, int keep)` | releases trans, engine, manifold, 3 carbs (keep), tyres (always freed), frees the car; returns max(0, car value − part values) | verified |
| `0000:442b` | `classifieds` | `void(int section)` | Used Cars / Auto Parts ads; "See it", buy | verified |
| `0000:4940` | `how_about` | `int(uint *offer)` | round offer down to a multiple of 5 (min 5), "How about $%d ?" OK/No | verified |
| `0000:49a2` | `car_flags` | `far uint(CAR*)` | picture flags: 4 roof chopped, 2 rear bumper stripped, 1 front stripped, 8 big engine (grade 2) | verified |
| `0000:49ed` | `your_cars` | `int(void)` | "Your cars": Switch it / Sell it with haggling; returns 0 or 3 | verified (asm) |
| `0000:4fca` | `sfx_play_wait` | `far void(int n)` | start effect `n`, wait until it ends (sound; used by the gas station) | verified |
| `0000:4ffa` | `message_at` | `far void(int id, int x, int y)` | `message` at a position (race results) | verified |
| `0000:542e` | `opponent_init` | `far void(int i, int *pool)` | random model/colour/customisation for opponent `i` | verified |
| `0000:5584` | `opponents_init` | `void(void)` | 3 pools of models by class, 7 opponents each | verified |
| `0000:56a9` | `new_game` | `void(void)` | pools, money $750 (demo $10000), opponents, clock 0 (game_flow range) | verified |
| `0000:570c` | `new_game_prompt` | `int(void)` | driver's licence name entry then `new_game` (game_flow) | likely |
| `0000:6476` | `day_index` | `far int(void)` | day of the summer 0…91 from the game clock | verified |
| `0000:64a3` | `clock_hm` | `far void(int *h, int *m)` | hour and minute of the day | verified |
| `0000:6529` | `summer_over_check` | `far void(void)` | day > 90 → `DS:0282 |= 1` | verified |
| `0000:653c` | `month_of_day` | `int(int day, int *dom)` | 0 June … 3 September; day of month | verified |
| `0000:65af` | `calendar_show` | `far void(int quick)` | calendar page with the day marker | verified |
| `0000:6750` | `date_print` | `far void(int y)` | "%s %d, 1963" in the newspaper | verified |
| `0000:67cf` | `set_paint_palette` | `far void(int colour)` | EGA palette registers 6/7 = paint colour | verified |
| `0000:6827` | `hotspot_find` | `far int(int code)` | index of the garage hot-spot record with that code | verified |
| `0000:6882` | `garage_hotspots_register` | `void(void)` | `0f38:4265(0x74, 18, 18)` | likely |
| `0000:68b3` | `garage_hotspots_nocar` | `void(void)` | disable the 5 car hot spots (type 0xE) | verified |
| `0000:68e8` | `garage_hotspots_car` | `void(int model, int x, int y)` | place the 5 car hot spots from `DS:70E4` | verified |
| `0000:6aa6` | `garage_name_clear` | `far void(void)` | `strcpy(DS:29FF, DS:5094)` (blank title) | verified |
| `0000:6ad8` | `broke_check` | `int(void)` | no car, no spare, money < $400 → 1 | verified |
| `0000:6b06` | `garage_screen` | `int(int mode)` | draw the garage, warnings, run hot spots; returns code | verified |
| `0000:6f6b` | `paint_job` | `void(void)` | choose one of 6 colours, $20 | verified |
| `0000:711a` | `customize` | `void(int what)` | 4 roof $70, 2 rear bumper / 1 front bumper $15 | verified |
| `0000:7367` | `stickers` | `void(void)` | pick one of 8 stickers, $5, peel-on animation | verified |
| `0000:76a7` | `spares_collect` | `void(int cat, PART **nodes, char *wear, int n0)` | fill `DS:49E0` list with the spares of a category | verified |
| `0000:7741` | `part_install` | `void(PART *p, PART **slot)` | old part → spares, `p` into slot, car value adjusted | verified |
| `0000:7789` | `part_uninstall` | `void(PART **slot)` | slot part → spares, car value adjusted | verified |
| `0000:77b7` | `spare_unlink` | `PART*(PART *p)` | remove `p` from the spare list | verified |
| `0000:7800` | `change_tires` | `void(void)` | pick spare tyres, swap, animation | verified |
| `0000:78f1` | `car_info_list` | `void(int *xoff, char *wear)` | 7 part rows + 4 text rows into `DS:49E0` | verified |
| `0000:79eb` | `car_info` | `far void(void)` | Car info list with gas and top speed | verified |
| `0000:7aa4` | `change_transmission` | `void(void)` | 2 bolts, remove / fit a transmission | verified |
| `0000:7ee6` | `car_runnable` | `far int(CAR*, int full)` | engine stack complete and tight (+ trans, tyres if `full`) | verified |
| `0000:7fb1` | `ignition_tune` | `void(CAR*)` | timing light, Retard / Advance | verified |
| `0000:81ab` | `engine_bay` | `void(CAR*)` | "Pop the hood": bolts, fit/remove engine, manifold, carbs; also viewer for other cars | verified |
| `0000:b8a1` | `gas_station` | `far void(void)` | pump, tank cap, windshields, attendant (race range) | likely |
| `0000:b717` | `gas_leave` | `void(int *st)` | fines, attendant animation, drive off (race range) | likely |
| `0000:e218` | `top_speed` | `int(int model)` | mph (model ≥ 0: stock car; −1: current car) (race range) | verified (asm) |
| `0f38:4f4f` | `list_box` | `far int(int scr, int sel, int *xoff, int *list, char *wear, char **extra)` | save bg, `list_box_run`, restore | verified |
| `0f38:4fc1` | `wear_text` | `char*(int pct)` | `"(`new`)"` for 0, else `"(NN% worn)"` built in `DS:07AA` | verified |
| `0f38:500e` | `list_box_run` | `far int(...)` | scrolling list engine (§4.3) | likely |
| `0f38:590b` | `list_selected` | `far int(void)` | `DS:8BA0` (1-based highlighted row) | verified |
| `0f38:5da7` | `edit_number` | `far int(uint *v, char *buf)` | numeric edit field (`0f38:5910`), `*v = atoi(buf)`; returns the button (≤0) | verified |
| `0f38:8e48` | `car_draw` | `far void(int mode, int model, int flags, int sticker, int driver, int y, int smoke, int *tyre_pics)` | draw / animate a car (§4.17) | likely |
| `0f38:8866` | `car_compose` | `far void(...)` | compose the car picture from the base + overlays | likely |
| `0f38:be93` | `tire_change_anim` | `far void(int *old_pics, int *new_pics)` | non-interactive tyre swap animation | likely |
| `0f38:7b22` | — | | cockpit / dashboard set-up of a drive (called from `0000:8e2d`): **see race** | verified |
| `0f38:5910` | — | | text line editor (save names, licence, numbers): see game_flow / platform | likely |
| `0f38:3074`, `31a5`, `32ce`, `3404`, `345b` | `trans_bolt_draw`, `trans_bolt_remove_anim`, `trans_slide_anim(pic,in)`, `trans_remove_anim(pic,mode)`, `trans_screen_draw(pic, bolts, …)` | | transmission screen drawing *(prov.)* | guess |
| `0f38:3697`, `37fc`, `3a58`, `3c19`, `3d1c`, `3e35`, `3f2d`, `40ca` | `bay_bolt_draw`, `bay_bolt_remove_anim`, `bay_part_in_anim(pic,reject)`, `bay_part_out_anim`, `bay_connector_draw`, `bay_draw_car(car)`, `bay_draw_parts(tbl, bolts, own)`, `bay_draw_bolts(tbl, bolts)` | | engine bay drawing *(prov.)* | guess |

Platform / video / sound helpers used here *(prov. names)*: `0000:1417 ui_wait(timeout)` (hot-spot
code, 0 = none, 1000 = time-out), `0000:3a83 wait_ticks_or_click(n)`, `0000:3ab8 message(id)`
(box, wait `DS:05DC`, close), `0f38:4a66 hotspots_push(screen)`, `0f38:4ba1 hotspots_pop(all)`,
`0f38:4f14 ui_run(screen)` (push, poll until non-zero, pop), `0f38:4ec1 dialog(screen)` (save bg +
`ui_run` + restore), `0f38:4d10 save_rect(screen,&x,&y,2)`, `0f38:2853 restore_rect`, `0f38:a0b8 /
a09e free_buffers`, `0f38:2a62 msgbox_open(id,x,y)` / `2adf msgbox_close`, `0f38:2638
pic_draw(id,x,y,flags)`, `0f38:2554 pic_draw_screen`, `0f38:2d7e mouse_cursor` (video: ≥0 pointer shape, −1/−2 show, −3/−4 hide), `0f38:b5aa copy_to_screen`,
`0f38:5eb6 random(n)` (0…n−1), `0f38:21c0 money_add(int)`, `0f38:2213 money_draw`, `0f38:1f4b
ega_set_palette(reg,val)`, `0f38:1dc9 / 19ec / 1e72 anim_start / anim_stop / anim_running`,
`0f38:7573, 7613, 76da, 77e1, 7817, 7841, 7973` sound effects, `0000:0d78` sound update,
`0000:08b4 / 09cb` classified-ads page. Names already fixed by the other specs: `ega_set_palette` =
video `pal_set_reg`, `money_add` = video `status_add_cash`, `money_draw` = `status_label`,
`random` = platform `rnd` (Wichmann–Hill), `free_buffers` = platform `arena_pop`, `ui_wait` =
platform `ui_wait`, `hotspots_push/pop` = game_flow `ui_push/ui_pop`.

---

## 3. Globals

| DS | name | type | meaning | written by | read by |
|---|---|---|---|---|---|
| `7EAA`/`7EAC` | `money` | i32 | bankroll ($) | `money_add`, `new_game`, `classifieds` (demo) | everywhere |
| `7EAE` | `unk_7eae` | i16 | set to 10 by `pools_init(1)` / `new_game`; no reader found | `3614`, `56a9` | — |
| `7EB0` | `cur_car` | CAR* | the car in the garage (0 = none) | `car_new`, `your_cars`, `garage_screen`, save/load | all |
| `7EB2` | `other_cars` | CAR* | list of the other owned cars, link `+0x26` | same | same |
| `7EB4` | `car_free_list` | CAR* | free car records, link `+0x26` | `pools_init`, `car_alloc/release` | |
| `7EB6` | `cars_used` | i16 | records in use (limit 16) | same | `garage_full_check`, `game_loop`, `garage_screen` |
| `7EB8` | `spares` | PART* | spare-part list, link `+6` (newest first) | many | many |
| `7EBA` | `part_free_list` | PART* | free part nodes | `pools_init`, `part_alloc/free` | |
| `7EBC` | `parts_used` | i16 | part nodes in use (all parts incl. installed; limit 131) | same | same |
| `83B0` | `car_pool` | 16 × 0x28 | car records | | |
| `866A` | `part_pool` | 140 × 8 | part nodes | | |
| `05FC`/`05FE` | `game_clock` | u32 | game time, `0x222` = one hour, `0x1998` = one 12-hour day | every action | `day_index`, `clock_hm` |
| `0600`/`0602` | `tick_clock` | u32 | timer ticks (platform); `ignition_tune` uses it | platform | |
| `0604`/`0606` | `clock_origin` | u32 | copy of `0600` at the new game; day = (`05FC+0600−0604`)/0x1998 | `new_game`, load | |
| `0282` | `end_flags` | u16 | bit0 summer over, bit4 broke; ≠0 → garage returns 0x29a | `summer_over_check`, `garage_screen` | game_flow |
| `0284` | `location` | u16 | 1 garage, 2 street, 3 gas station, 5 newspaper, 0 elsewhere (selects backgrounds in `car_draw`) | several | `car_draw`, race |
| `0610` | `demo_mode` | u16 | `demo` switch copy | game_flow | `classifieds`, `new_game`, `opponents_init` |
| `49E0` | `list_buf` | i16[~146] | `[0]` count, `[1..]` text ids (negative = index into an extra string table) | list builders | `list_box`, `classifieds_open` |
| `2990` | `ad_buf` | char[] | ad text of the item being bought / sold | `classifieds`, `your_cars` | dialogs 9, 12 |
| `29D6` | — | char[] | "$%-3d ?" after "You want to buy it for " (`DS:29BF`) | `classifieds` | screen 9 |
| `29F8` | `ask_buf` | char[] | the "I'll take $" edit field | `your_cars` | `edit_number` |
| `29FF` | `garage_title` | char[] | ad text of the current car (drawn by the garage screen) | `garage_screen`, `garage_name_clear` | UI |
| `2A3E` | `btn_change` | char[8] | label of screen 4's "Change" button; `sell_spare_parts` copies "Sell it" (`DS:4F5C`) and restores "Change" (`DS:4F64`) | `3c8d` | UI |
| `57A7` | — | char* | byte at `*DS:57A7` set to 0x1F during the sell list, 0x2E after (a glyph in the list header) | `3c8d` | UI |
| `11B0` | — | i16 | 10 when a car ad is chosen, 0x0E for a part (layout of dialog 9) | `classifieds` | UI |
| `4F46` / `4F48` | `switch_old` / `switch_new` | CAR* | car leaving / arriving after "Switch it" | `your_cars` | `garage_screen(3)` |
| `4F58` / `4F5A` | `news_pic` / `news_day` | i16 | headline picture index (0–4) and the day it was chosen | `newspaper_front` | |
| `4F4E` | `NEWS_PICS` | i16[5] | 1022,1023,1025,1024,1026 (LIB2 #22–26) | const | |
| `7D16` | `forsale_head` | i8 (word) | first model in the Used Cars ads (HOT_DATA block 7, = 2) | HOT_DATA | `classifieds` |
| `7D86` | `MODELS` | 26 × 10 | car models (HOT_DATA block 1), §5.2 | HOT_DATA | everywhere |
| `8DF0` | `MODEL_PICS` | 26 × 10 | per-model overlay pictures (HOT_DATA block 2), §5.3 | HOT_DATA | |
| `7682` | `MODEL_LAYOUT` | 26 × 12 | overlay positions (HOT_DATA block 3), §5.4 | HOT_DATA | `car_compose` |
| `70E4` | `MODEL_HOTSPOTS` | 26 × 20 | 5 hot-spot rectangles per model (HOT_DATA block 4) | HOT_DATA | `garage_hotspots_car` |
| `818C` | `MODEL_POINTS` | 26 × 6 | tank filler / windshield points (HOT_DATA block 5) | HOT_DATA | `car_compose`, gas station |
| `7FF8` | `OPPONENTS` | 22 × 18 | opponents (HOT_DATA block 0) | HOT_DATA, `opponent_init` | race |
| `2C02` | `TEXTS` | 1850 B | ads, opponent names, messages (HOT_DATA block 6 overwrites the EXE copy) | HOT_DATA | |
| `239E` | `UI_TEXTS` | 200 B | button labels (HOT_DATA block 8) | HOT_DATA | UI |
| `7844` | `STICKERS` | 9 × 6 | `{pic, dx, dy}`, entry 0 unused (HOT_DATA block 12 starts at `784A`) | HOT_DATA | `stickers`, `car_compose` |
| `4806` | `PARTS` | 44 × 8 | part catalogue (§5.1) | const | |
| `4966` | `INIT_WEAR` | i16[5] | initial wear per category: 0, 0, −128, −128, 0 | const | `car_new`, `spare_add` |
| `4970`–`4976` | `race_wins[4]` | i16 | zeroed by `new_game` (see race) | race | race |
| `4978` | `MANIFOLD_FITS` | u8[3][5] | `[engine_grade][manifold_grade]` | const | `engine_bay` |
| `4988` | `CARB_FITS` | u8[5][3] | `[manifold_grade][carb_grade]` | const | `engine_bay` |
| `4998` | `BAY_PICS` | i16[5][5] | engine-bay picture per `[slot][grade]` | const | `engine_bay`, `garage_screen` |
| `49A2` | `bay_block_pic` | i16 | 0x438 (V-6) / 0x439 (V-8) engine block | `engine_bay` | bay drawing |
| `49CA` | `BOLT_PICS` | i16[4] | bolt picture per tightness 0–3 (0, 1087, 1088, 1089); `DS:49CC` = loose | const | |
| `49D2` | `TRANS_PICS` | i16[4] | transmission picture per grade (LIB2 #99–102) | const | `change_transmission` |
| `5030` | `TYRE_PICS` | i16[3][4] | 4 wheel frames per tyre grade | const | `car_draw` callers |
| `50C2` | `PAINT` | u8[6][2] | EGA palette values for registers 6, 7 | const | `set_paint_palette`, `paint_job` |
| `0446`/`0447` | `pal_shadow[6..7]` | u8 | shadow of EGA palette registers (`DS:0440+reg`) | `set_paint_palette`, `ega_set_palette` | video |
| `8249` | `pal_enabled` | u8 | palette writes allowed | video | `set_paint_palette` |
| `5126` | `CARBS_PER_MANIFOLD` | i16[5] | 1, 1, 3, 2, 2 | const | |
| `5130` | `BOLTS_PER_LAYER` | i16[5] | 1, 2, 2, 2, 2 | const | `engine_bay` |
| `513C` | `LAYER_CATEGORY` | i16[5] | 0, 3, 2, 2, 2 (engine, manifold, carb ×3) | const | `engine_bay` |
| `5148` | `BAY_LAYOUT` | i16[6][6] | per-manifold bolt / carb layout; `[0]`,`[1]` rewritten 4 (V-6) / 5 (V-8) | `engine_bay` | bay drawing |
| `518C` | `bay_layout_cur` | i16[6] | copy of `BAY_LAYOUT[manifold_grade]` | `engine_bay` | bay drawing |
| `5184`, `5198` | `BOLT_CTX`, `BOLT_SIDE` | i16[] | per bolt: part context (0 engine, 1 manifold, 5 carb), side 0/1 of a pair | const | `engine_bay` |
| `8286` | `trans_pic` | i16 | transmission picture on the trans screen (0 = none) | `change_transmission` | 0f38:3074… |
| `8290`–`8298` | `bay_slot_pic[5]` | i16 | pictures of engine, manifold, carb 1–3 (also set by the garage screen to part types) | `engine_bay`, `garage_screen` | bay drawing |
| `829A`–`82AA` | `bay_bolt_pic[9]` | i16 | bolt pictures | `engine_bay` | bay drawing |
| `82AC` | `bay_connector_pic` | i16 | 0, 0x446 disconnected, 0x447 connected | `engine_bay` | bay drawing |
| `82AE`, `82B2` | `bay_ctx`, `bay_bolt_side` | i16 | context for the drawing helpers | `engine_bay` | bay drawing |
| `1804` | — | screen rec 200/201 `type` | set 0xB (active) for the two transmission bolt hot spots | `change_transmission` | UI |
| `825E`/`8260` | `car_x`/`car_y` | i16 | where `car_draw` put the car | `car_draw` | hot spots, gas station |
| `8272`/`8274`/`8276` | `sticker_x`/`y`/`dx` | i16 | sticker position | `car_compose` | `stickers` |
| `826E`… `8268`, `8264` | | i16 | filler and windshield points (`MODEL_POINTS` + car pos) | `car_compose` | gas station |
| `827A`–`8284` | `garage_car_*` | i16 | picture, colour, tyre grade, driver, model, flags word (copy of `+0x24`) | `garage_screen` | `car_draw` |
| `8240` | `present_flag` | i16 | copy the back page to the screen after drawing (video) | many | video |
| `58D0` | `music_off` | i16 | "Squelch it !" toggle | `game_loop` | sound |

---

## 4. Data model and pseudocode

### 4.1 Records

```c
typedef struct PART {        /* 8 bytes, pool DS:866A (140 nodes) */
    i16  value;   /* +0 $ paid / book value (the catalogue price when new) */
    i16  wear;    /* +2 0..10000 (hundredths of %), -128 = does not wear; -1 in a free node's +4 */
    i16  type;    /* +4 index into PARTS (DS:4806), -1 when free */
    struct PART *next; /* +6 spare list / free list */
} PART;

typedef struct CAR {         /* 0x28 bytes, pool DS:83B0 (16 records) */
    i16   value;     /* +00 book value of the whole car (body + installed parts) */
    i16   model;     /* +02 index into MODELS, -1 when free */
    i8    colour;    /* +04 paint 0..5 (PAINT table) */
    u8    klass;     /* +05 copy of MODELS[model].klass */
    PART *trans;     /* +06 */
    PART *engine;    /* +08 */
    PART *manifold;  /* +0A */
    PART *carb[3];   /* +0C +0E +10 (unused slots 0) */
    PART *tyres;     /* +12 */
    i8    trans_bolt[2];   /* +14 +15  0 = out, 1..3 tightness (3 = tight) */
    i8    bay_bolt[9];     /* +16..+1E bolt 1 engine, 2-3 manifold, 4-5 / 6-7 / 8-9 carbs 1-3 */
    i8    engine_link;     /* +1F 0 no engine, -1 fitted but disconnected, 1 connected */
    i8    ignition;        /* +20 timing -8..+4 */
    u8    _21;             /* +21 not set here */
    i16   gas;             /* +22 tenths of a gallon */
    u16   flags;           /* +24 low byte: 0 on creation (race use?);
                              bits 8-12 sticker 0..8, bit 13 (0x2000) roof chopped,
                              bit 14 (0x4000) rear bumper stripped, bit 15 (0x8000) front bumper stripped */
    struct CAR *next;      /* +26 other_cars / free list */
} CAR;
```

Owner state: `cur_car` (`DS:7EB0`), `other_cars` (`DS:7EB2`), `spares` (`DS:7EB8`). The part of an
installed slot is **not** on the spare list. `parts_used` counts every allocated node (installed and
spare); the garage refuses to buy more at 131 (`0x83`), leaving 9 nodes for a car bought with its
parts. `cars_used` counts every allocated car record (owner's cars; the race also allocates records
while building opponents' cars, see race).

### 4.2 Pools and part helpers

```c
void pools_init(int reset_owner)              /* 0000:3614 */
{
    car_free_list = &car_pool[0];             /* DS:83B0, 16 x 0x28, last link 0 */
    for (i = 0; i < 16; i++) { car_pool[i].next = &car_pool[i+1]; car_pool[i].model = -1; }
    car_pool[15].next = 0;
    part_free_list = &part_pool[0];           /* DS:866A, 140 x 8 */
    for (i = 0; i < 140; i++) { part_pool[i].next = &part_pool[i+1]; part_pool[i].type = -1; }
    part_pool[139].next = 0;
    parts_used = 0; cars_used = 0;
    if (reset_owner) { cur_car = other_cars = spares = 0; money = 0; DS_7EAE = 10; }
}
PART *part_alloc(void)  { p = part_free_list; part_free_list = p->next; p->next = 0; parts_used++; return p; } /* 3c1b */
void  part_free(PART *p){ p->next = part_free_list; p->type = -1; part_free_list = p; parts_used--; }     /* 3c3e */
CAR  *car_alloc(void)   { c = car_free_list; car_free_list = c->next; c->next = 0; cars_used++; return c; }  /* 3fb1 */
void  car_release(CAR*c){ c->next = car_free_list; c->model = -1; car_free_list = c; cars_used--; }     /* 3fd4 */

int part_value(PART *p)                       /* 0000:3c5a  V-6 engines are worth $100 */
{   return (PARTS[p->type].category == 0 && PARTS[p->type].grade == 0) ? 100 : p->value; }

void spare_add(int type, int value)           /* 0000:3ff0 */
{   p = part_alloc(); p->type = type; p->wear = INIT_WEAR[PARTS[type].category];
    p->value = value; p->next = spares; spares = p; }

int part_release(PART *p, int keep)           /* 0000:4386 */
{   if (!p) return 0;
    v = p->value;
    if (keep) { p->next = spares; spares = p; } else part_free(p);
    return v; }

int car_free(CAR *c, int keep)                /* 0000:43bf */
{   c->value -= part_release(c->trans, keep);
    for (i = 0; i < 5; i++) c->value -= part_release((&c->engine)[i], keep); /* engine, manifold, carbs */
    part_release(c->tyres, 0);                /* tyres are never kept */
    v = c->value < 0 ? 0 : c->value;
    car_release(c);
    return v; }

PART *spare_unlink(PART *p)                   /* 0000:77b7 (p must be on the list) */
{   prev = 0; for (q = spares; q && q != p; q = q->next) prev = q;
    if (!prev) spares = q->next; else prev->next = q->next;
    return q; }

void part_install(PART *p, PART **slot)       /* 0000:7741 */
{   if (*slot) { (*slot)->next = spares; spares = *slot; cur_car->value -= part_value(*slot); }
    *slot = p; p->next = 0; cur_car->value += part_value(p); }

void part_uninstall(PART **slot)              /* 0000:7789 */
{   (*slot)->next = spares; spares = *slot; cur_car->value -= part_value(spares); *slot = 0; }

void spares_collect(int cat, PART **nodes, char *wear, int n) /* 0000:76a7 */
{   for (p = spares; p; p = p->next)
        if (PARTS[p->type].category == cat) {
            n++; nodes[n] = p; list_buf[n] = PARTS[p->type].text;
            wear[n] = (p->wear == -128) ? 0x80 : p->wear / 100;   /* signed idiv, truncates */
        }
    list_buf[0] = n; }
```

`part_install`/`part_uninstall` always adjust **`cur_car`**, even though `engine_bay` is written
for any car (it is only interactive for `cur_car`).

### 4.3 `car_new` (0000:4036)

```c
CAR *car_new(int model, int value, CAR *reuse)
{
    M = &MODELS[model]; spec = M->spec;
    c = reuse ? reuse : car_alloc();
    c->model = model; c->value = value; c->gas = 40; c->klass = M->klass;
    do { col = random(6); } while (col == 5 && random(2) != 1);   /* colour 5 (grey) at half odds */
    c->colour = col;
    c->engine_link = 1;
    c->ignition = random(13) - 8;
    make = (spec & 0xF) >> 1;                     /* 1->0 GM, 2->1 Ford, 4->2 Chrysler */
#define PART_FOR(slot, t, cat) { p = reuse ? slot : part_alloc(); slot = p; p->type = t; \
        p->value = PARTS[t].price; p->wear = INIT_WEAR[cat]; p->next = 0; }
    tg = (spec >> 6) & 3;   PART_FOR(c->trans,    9 + make + tg*3 + (tg == 3), 1);   /* racing: 19+make */
    eg = (spec >> 8) & 7;   PART_FOR(c->engine,   eg*3 + make, 0);
    PART_FOR(c->tyres, 0x28 + ((spec >> 4) & 3), 4);
    memset(&c->bay_bolt[3], 0, 6);                /* bytes +19..+1E */
    mg = (spec >> 11) & 7;  PART_FOR(c->manifold, 0x19 + mg*3 + make, 3);
    carb_type = (spec >> 14) + 0x16;
    n = CARBS_PER_MANIFOLD[mg] - 1;               /* 0..2 */
    if (n < 2) for (k = 2; k > n; k--) c->carb[k] = 0;
    for (k = n; k >= 0; k--) { PART_FOR(c->carb[k], carb_type, 2);
                               c->bay_bolt[3 + 2*k] = 3; c->bay_bolt[4 + 2*k] = 3; }
    c->bay_bolt[0] = c->bay_bolt[1] = c->bay_bolt[2] = 3;   /* +16..+18 */
    c->trans_bolt[0] = c->trans_bolt[1] = 3;                /* +14,+15 */
    c->flags &= 0x0000;                           /* byte +24 = 0; byte +25 bits cleared one by one */
    if (!reuse) {
        if (cur_car) { c->next = other_cars; other_cars = c; }
        else cur_car = c;
    }
    return c;
}
```

Note: a reused record keeps its part nodes, including a carb node for a slot now unused — callers
(race) reuse only the same model. `PARTS[t].price` (not the car's price) is the value of each part;
the car's own `value` is the total price paid, so `car_free` returns the "body" value.

### 4.4 Newspaper, classified ads and buying

```c
int newspaper_front(void)                     /* 0000:3b33 */
{
    save = present_flag; present_flag = 0;
    pic_draw_screen(0x3FD /*LIB2 #21*/, -1);
    d = day_index();
    if (d != news_day) {                      /* new headline once per day, not the same twice */
        k = random(5); if (k == news_pic) k = random(5);
        news_pic = k; news_day = d;
    }
    pic_draw(NEWS_PICS[news_pic], 0, 0x88, -1);
    mouse_cursor(-3); date_print(0x79); mouse_cursor(-1);
    present_flag = save;
    if (save) copy_to_screen(page0, page1, 0, 0, 0x13F, 0xBD);
    mouse_cursor(1);
    return ui_run(7);                         /* 1 Used Cars, 2 Auto Parts, 4 Go to the garage */
}
```

`game_loop` calls it at the start of a new game (with `cars_used < 16 && parts_used < 131`) and for
garage code 1; results 1/2 go to `classifieds(section)`.

```c
void classifieds(int section)                 /* 0000:442b; section 1 used cars, 2 parts */
{
    location = 5;
    list_buf[1] = 0xCD8;                      /* "---------- USED CARS ----------" */
    n = 2; k = 0;
    for (m = forsale_head; m >= 0; m = MODELS[m].next_for_sale) {
        list_buf[n++] = MODELS[m].ad; item[k++] = m;
    }
    first_part = n + 1; list_buf[n] = 0xCFD;  /* "------------ PARTS ------------" */
    bias = k - first_part;
    for (t = 0, n = first_part; t < 0x2C; t++)
        if (PARTS[t].price > 0x13) { list_buf[n++] = PARTS[t].text; item[k++] = t; } /* no V-6 engines */
    list_buf[0] = n - 1;
    classifieds_open(list_buf, section == 1 ? 2 : 0x1C);   /* page holding item 2 / item 28 */
  pick:
    for (;;) {
        r = classifieds_run();                /* -3 = Go to the garage, else the clicked ad (1-based) */
        if (r == -3) break;
        is_part = (r >= first_part);
        if (!is_part) { idx = item[r - 2]; M = &MODELS[idx]; text = M->ad; price = M->price; DS_11B0 = 10; }
        else          { idx = item[r + bias]; text = PARTS[idx].text; price = PARTS[idx].price; DS_11B0 = 0x0E; }
        strcpy(ad_buf, &TEXTS[text]);
        sprintf(DS_29D6, "$%-3d ?", price);   /* after "You want to buy it for " */
        colour = -1;
        while ((b = ui_run(9)) != -20) {      /* -25 See it, -20 Yeah, -21 Forget it */
            if (b != -25) goto pick;
            /* "See it": draw the car (only models reach here in practice) */
            mouse_cursor(-4); free_buffers();
            save = alloc_rect(0x140, 0x52); copy_rect(0, 0x40, 0x140, 0x52, save);
            fill(0, 0x41, 0x140, 0x46, colour_bg); fill(0, 0x87, 0x140, 10, text_colour);
            /* non-EGA: frame lines (0f38:439c, 26a4) */
            if (colour == -1) { colour = random(5); set_paint_palette(colour); }
            car_draw(7, idx, ((M->spec >> 8) & 7) == 2 ? 8 : 0, 0, 0, 0x86, 0,
                     &TYRE_PICS[(M->spec >> 4) & 3]);
            w = text_width(ad_buf); text_at((0x140 - w) / 2, ...);   /* 0f38:17c8, 2656 */
            mouse_cursor(-2); DS_05D0 = 1; ui_run(0x18); DS_05D0 = 0;    /* click to continue */
            restore_rect(save, 0, 0x40); free_buffers();
        }
        if (demo_mode && money < price) money += 1999;               /* demo cheat, once per try */
        if (money < price) {                                         /* signed 32-bit */
            p = strchr(TEXTS + 0xE58, '$');                          /* "No go, hot shot. You're $12345 short." */
            sprintf(p + 1, "%d short.", price - money);
            message(-0xE58);                                         /* negative id: see platform */
            continue;
        }
        money_add(-price);
        if (!is_part) {
            c = car_new(idx, price, 0);
            if (colour == -1) colour = random(5);
            c->colour = colour;                                      /* the colour you saw */
            message(-0xD22);                                         /* "It's yours!" */
            game_clock += 0x444;
            break;
        }
        spare_add(idx, price);
        game_clock += 0x222;
        if (parts_used > 0x82) break;
    }
    sound_update();                                                  /* 0000:0d58 */
    if (cur_car) set_paint_palette(cur_car->colour);
    location = 0;
}
```

Bought cars are painted in colours 0–4 only (the random is `random(5)`), and the first car bought
becomes `cur_car`. The ad list is paged by the classified-ads page (`0000:08b4/09cb`, platform/UI):
ads have 1–3 lines (byte before the text), a string starting with `+` continues the previous line.

### 4.5 Selling spare parts (0000:3c8d)

```c
void sell_spare_parts(void)
{
    rounds = 0; sel = 1;
    for (;;) {
        n = 0;
        for (p = spares; p; p = p->next) {
            n++; node[n] = p; list_buf[n] = PARTS[p->type].text;
            wear[n] = (p->wear == -128) ? 0x80 : p->wear / 100;
        }
        list_buf[0] = n;
        strcpy(btn_change, "Sell it"); *DS_57A7 = 0x1F;
        r = list_box(4, sel < n ? sel : n, 0, list_buf, wear, 0);
        *DS_57A7 = 0x2E; strcpy(btn_change, "Change");
        if (r == -4) return;                                   /* Forget it */
        if (r == 0x3EA) { message(rounds ? 0xED7 /*All parts gone!*/ : 0xF2A /*no spare parts, dummy!*/); return; }
        if (r == -3) sel = list_selected(); else sel = r;
        p = node[sel];
        value = part_value(p);                                 /* u16 */
        cond  = (0x2710 - p->wear) / 100;                      /* signed idiv; -128 -> 101 */
        switch (PARTS[p->type].category) {
        case 0: case 1: {                                      /* engine, transmission */
            double k = (double)(u32)cond / 100.0;
            a = ftol((double)(u32)value * k * 0.9);
            value -= a;
            offer = a + ftol((double)(u32)value * k * 0.9);
            break; }
        case 2: case 3:                                        /* carb, manifold */
            offer = ftol((double)(u32)value * ((random(20) + 0x46) / 100.0)); break;
        case 4: offer = 0; break;                              /* tyres: nothing */
        }
        offer = (offer / 5) * 5;                               /* unsigned */
        if (offer == 0) id = 0x1021;                           /* "I'll take it off your hands for nothing." */
        else { id = 0x100A; sprintf(TEXTS + 0x100A, "Will you take $%-d ?", offer); }
        msgbox_open(id, -1, -1);
        b = dialog(0x10);                                      /* -20 OK, -21 No thanks */
        msgbox_close();
        if (b != -21) {
            if (sel == 1) spares = p->next; else node[sel - 1]->next = p->next;
            part_free(p);
            game_clock += 0x444;
            money_add(offer);
        }
        rounds++;
    }
}
```

`ftol` is the MSC `_ftol` (`1e16:2de9`): truncation toward zero. The engine/transmission offer is
`a + trunc((v − a)·k·0.9)` with `a = trunc(v·k·0.9)`, i.e. about `v·(1.8k − 0.81k²)`; a new part
(`k = 1`) sells for 99 % of its value, rounded down to $5. A V-6 engine is valued $100.

### 4.6 Your cars, switching and selling (0000:49ed, 4940)

```c
int how_about(uint *offer)                    /* 0000:4940 */
{   r = *offer % 5;
    if (r) { *offer -= r; if (*offer == 0) *offer = 5; }
    sprintf(DS_4FA4, "How about $%-d ?", *offer);
    msgbox_open(0xFF7, -1, -1);
    b = ui_run(0x0D);                         /* -20 OK, -21 No */
    msgbox_close();
    return b; }

int your_cars(void)
{
    saved = 0; rounds = 0; sel = 1;
    for (;;) {
        n = 0;
        if (cur_car) { n = 1; node[1] = cur_car; list_buf[1] = MODELS[cur_car->model].ad; }
        for (c = other_cars; c; c = c->next) { n++; node[n] = c; list_buf[n] = MODELS[c->model].ad; }
        list_buf[0] = n;
        r = list_box(0x0B, n < sel ? n : sel, 0, list_buf, 0, 0);   /* -1 Switch, -2 Sell, -3 Forget */
        sel = r;
        if (r == -3) return 0;
        if (r == 0x3EA) { message(rounds ? 0xEE9 /*All cars gone!*/ : 0xEFA /*no car, dummy!*/); return 0; }
        if (r == -1) {                                              /* Switch it */
            sel = list_selected();
            switch_new = node[sel]; switch_old = cur_car;
            if (switch_new == cur_car) { switch_new = 0; return 3; }
            if (sel == (cur_car ? 2 : 1)) other_cars = switch_new->next;
            else node[sel - 1]->next = switch_new->next;
            if (cur_car) { cur_car->next = other_cars; other_cars = cur_car; }
            cur_car = switch_new;
            return 3;
        }
        /* any other code (Sell it): */
        sel = list_selected(); car = node[sel];
        base = offer = ftol((random(20) + 75.0) * car->value / 100.0);   /* 75..94 % */
        strcpy(ad_buf, &TEXTS[MODELS[car->model].ad]);
        ask_buf[0] = 0;
        saved = save_rect(0x0C, &sx, &sy, 1);
        hotspots_push(0x0C);                                        /* "I'll take $" [edit] Offer / Never mind */
        b = edit_number(&ask, ask_buf);
        while (b != -22) {
            if (ask == 0) { b = edit_number(&ask, ask_buf); continue; }
            if ((uint)ftol(car->value * 1.15) < ask) { message(0xEC0); break; }   /* You must be kidding! */
            if ((uint)car->value < ask) {
                message(0x106E);                                    /* No way! */
                do { if (edit_number(&ask, ask_buf) == -22) goto close; } while (ask == 0);
                if ((uint)car->value < ask) { message(0x1078); break; }   /* Get lost! */
            }
            if (ask > offer) {
                offer = ftol((random(5) + 85.0) * (u32)base / 100.0);
                if (ask > offer && how_about(&offer) == -20) ask = offer;
            }
            if (ask > offer) do { if (edit_number(&ask, ask_buf) == -22) goto close; } while (ask == 0);
            if (ask > offer) {
                offer = ftol((random(5) + 91.0) * (u32)base / 100.0);
                if (ask > offer && how_about(&offer) == -20) ask = offer;
            }
            if (ask > offer) do { if (edit_number(&ask, ask_buf) == -22) goto close; } while (ask == 0);
            if ((double)(u32)ask * 0.9 > (double)(u32)offer) { message(0x1084); break; }  /* No thanks! */
            message(car->value > (u16)(ask << 1) ? 0x104B      /* You must be crazy. I'll take it! */
                                                 : 0x1091);    /* I'll take it! */
            game_clock += 0x888;
            money_add(ask);
            if (sel == 1 && cur_car) {                           /* selling the car in the garage */
                restore_rect(saved, sx, sy); free_buffers(); saved = 0;
                mouse_cursor(-4);
                car_draw(6, cur_car->model, car_flags(cur_car), (cur_car->flags >> 8) & 0x1F, 0, 0x98, 0,
                         &TYRE_PICS[PARTS[cur_car->tyres->type].grade]);    /* drives out */
                hotspots_pop(1); garage_name_clear(); hotspots_push(10); mouse_cursor(-2);
                car_free(cur_car, 0); cur_car = 0;
            } else {
                if (sel == (cur_car ? 2 : 1)) other_cars = car->next;
                else node[sel - 1]->next = car->next;
                car_free(car, 0);
            }
            break;
        }
      close:
        hotspots_pop(1);
        if (saved) { restore_rect(saved, sx, sy); free_buffers(); saved = 0; }
        rounds++;
    }
}
```

Unsigned comparisons (`jae`/`jbe`) as written. The buyer never shows `base`: an ask ≤ `base` is paid
at once; above it he counters twice (85–89 %, then 91–95 % of `base`, each rounded down to $5 by
`how_about`), and finally accepts any ask ≤ offer / 0.9. Selling a car frees its parts (tyres
included) — nothing goes to the spare list.

### 4.7 Garage screen (0000:6b06) and hot spots

```c
int garage_screen(int mode)   /* 0 no redraw, 1 full redraw, 2 redraw after a trip, 3 after Switch it */
{
    if (DS_4733 > 0) mouse_cursor(-4);
    summer_over_check();
    if (end_flags) { if (end_flags & 1) mouse_cursor(-2); return 0x29A; }
    mouse_cursor(0); text_draw(-1, DS_50B5, 0); location = 1;
    if (mode == 0) goto hotspots;
    if (mode == 3) {
        if (!switch_new) { mouse_cursor(-2); message(0x1867 /*Here it is !*/); mouse_cursor(-4); goto hotspots; }
        if (switch_old) {
            car_draw(6, switch_old->model, car_flags(switch_old), (switch_old->flags >> 8) & 0x1F, 0, 0x98, 0,
                     &TYRE_PICS[PARTS[switch_old->tyres->type].grade]);   /* old car drives out */
            garage_name_clear(); hotspots_push(10); hotspots_pop(1);
        }
        present_flag = 0; mode = 1;                          /* background not redrawn */
    } else {
        if (mode == 2) present_flag = 0;
        pic_draw_screen(0x408 /*LIB2 #32*/, -1); video_17fc();
        if (mode == 2 && !cur_car && other_cars) {           /* promote the first other car */
            cur_car = other_cars; other_cars = other_cars->next; mode = 1;
        }
    }
    present_flag = 1; driver_hook_78C2();
    if (!cur_car) { copy_to_screen(page0, page1, 0, 0, 0x13F, 199); garage_title[0] = 0; }
    else {
        garage_car_pic = MODELS[cur_car->model].picture; garage_car_colour = cur_car->colour;
        garage_car_tyre = PARTS[cur_car->tyres->type].grade;
        if (mode == 1) set_paint_palette(garage_car_colour);
        garage_car_driver = 0; garage_car_model = cur_car->model;
        garage_car_flags = (garage_car_flags & 0xFF) | (cur_car->flags & 0xFF00);
        for (i = 0; i < 5; i++) bay_slot_pic[i] = (&cur_car->engine)[i]->type;   /* NB: derefs empty slots */
        strcpy(garage_title, &TEXTS[MODELS[cur_car->model].ad]);
        car_draw(mode == 2 ? 3 : 4, garage_car_model, car_flags(cur_car), (garage_car_flags >> 8) & 0x1F,
                 0, 0x98, 0, &TYRE_PICS[garage_car_tyre]);   /* 3 = parked, 4 = drives in */
    }
  hotspots:
    if (!cur_car) garage_hotspots_nocar(); else garage_hotspots_car(garage_car_model, car_x, car_y);
    garage_hotspots_register(); money_draw(DS_49DC); mouse_cursor(-2);
    if (cars_used >= 16)          message(0xD30);  /* no room for all these cars, sell one */
    else if (parts_used == 0x83)  message(0xD76);  /* too much junk, sell a part */
    else if (parts_used > 0x83) {                  /* "... Must sell 12 parts to make enough room." */
        q = TEXTS + 0xDC7; q += strlen(q) + 1;      /* the second line */
        q += strcspn(q, "1234567890");
        itoa(parts_used - 0x82, q, 10); q[strlen(q)] = ' ';
        message(0xDC7);
    }
    if (broke_check()) { message(0x17FF /*You're outta dough.*/); end_flags |= 0x10; }
    else { sound_off_7001(0); video_0e24(); code = ui_run(10); }
    summer_over_check();
    if (end_flags & 0x10) end_flags = 0x10;
    if (end_flags) { code = 0x29A; if (!(end_flags & 1)) mouse_cursor(-4); }
    return code;
}

int broke_check(void)   /* 0000:6ad8 */
{ return !cur_car && !other_cars && !spares && money < 400; }

void garage_hotspots_nocar(void)                        /* 0000:68b3: codes 11,13,9,10,12 */
{ for (k = 0; k < 5; k++) SCREEN_REC[hotspot_find(DS_508A[k])].type = 0x0E; }

void garage_hotspots_car(int model, int x, int y)       /* 0000:68e8 */
{   const u8 *r = &MODEL_HOTSPOTS[model * 20];           /* 5 x {dx, dy, w, h} */
    static const int code[5] = { 11, 13, 9, 10, 12 };   /* rear bumper, roof, trans, hood, front bumper */
    for (k = 0; k < 5; k++) {
        R = &SCREEN_REC[hotspot_find(code[k])];
        R->x0 = r[4*k] + x; R->y0 = r[4*k+1] + y;
        R->x1 = R->x0 + r[4*k+2]; R->y1 = R->y0 + r[4*k+3];
        R->type = 0x0B;
    } }

int hotspot_find(int code)                               /* 0000:6827 */
{   for (i = 0; i < 18; i++) if (SCREEN_REC[0x74 + i].code == code) return 0x74 + i;
    return code; }            /* not found: returns the code itself (never happens) */
```

Screen records are 18 bytes at `DS:09F4` (`type, style, label, code, x0, y0, x1, y1, next`);
`type 0x0B` = active text hot spot, `0x0E` = disabled (see platform/video for the format). The 18
garage records (codes, rectangles) are listed in §5.9.

`car_flags` (`0000:49a2`): `(flags&0x2000 ? 4 : 0) | (flags&0x4000 ? 2 : 0) | (flags&0x8000 ? 1 : 0)
| (PARTS[car->engine->type].grade == 2 ? 8 : 0)` — the engine must be present.

### 4.8 Paint, customising, stickers

```c
void set_paint_palette(int colour)                     /* 0000:67cf */
{   if (driver == -2 /*EGA/VGA*/ || driver == -6 /*Tandy*/) {
        if (pal_enabled) { ega_set_palette(6, PAINT[colour][0]); ega_set_palette(7, PAINT[colour][1]); }
        pal_shadow[6] = PAINT[colour][0]; pal_shadow[7] = PAINT[colour][1];
    } }

void paint_job(void)                                    /* 0000:6f6b */
{   if (!cur_car) { message(0xEFA); return; }
    if (driver != -2 && driver != -6) { message(0x18AD); return; }   /* need a better graphics card */
    saved = save_rect(0x0E, &x, &y, 2); hotspots_push(0x0E);
    c = cur_car->colour;
    for (;;) {
        ega_set_palette(0x0C, PAINT[c][0]);           /* colour sample */
        while (!(b = ui_wait(3000))) ;
        if (b == -23) break;                           /* Forget it */
        if (b == 1) { if (--c < 0) c = 5; continue; }  /* Previous color */
        if (b == 2) { if (++c > 5) c = 0; continue; }  /* Next color */
        if (b == 3) {                                  /* Go ahead */
            if (money >= 20) {
                present_flag = 1; hotspots_pop(1); restore_rect(saved, x, y); free_buffers();
                restore_rect(DS_0294, sticker_x, sticker_y);        /* remove the sticker picture */
                cur_car->flags &= ~0x1F00;             /* byte +25 &= 0xE0: the sticker is gone */
                cur_car->colour = c; set_paint_palette(c);
                ega_set_palette(0x0C, 5);
                game_clock += 0x888; money_add(-20);
                return;
            }
            message(0x10A1);                           /* Paint ain't free! */
        }
        break;
    }
    hotspots_pop(1); ega_set_palette(0x0C, 5); restore_rect(saved, x, y); free_buffers();
}

void customize(int what)          /* 0000:711a; 4 roof, 2 rear bumper, 1 front bumper */
{   P = &MODEL_PICS[cur_car->model];                   /* computed before the null test */
    if (!cur_car) { message(0xEFA); return; }
    if (what & 4) {
        if (P->roof == 0) { message(0x1914); return; }  /* Ya can't chop this roof, dummy! */
        label = (cur_car->flags & 0x2000) ? "Rebuild roof - $70" : "Chop roof - $70";
        cost = 70; hours = 0x2220;
    }
    if (what & 2) {
        if (P->rear_bumper == 0) { message(0x1936); return; }   /* You've already lost your bumper! */
        label = (cur_car->flags & 0x4000) ? "Restore bumper - $15" : "Strip bumper - $15";
        cost = 15; hours = 0x888;
    }
    if (what & 1) {
        if (P->front_bumper == 0) { message(0x1936); return; }
        label = (cur_car->flags & 0x8000) ? "Restore bumper - $15" : "Strip bumper - $15";
        cost = 15; hours = 0x888;
    }
    if (money < cost) { message(0x1959); return; }      /* Customization ain't free! */
    saved = save_rect(0x22, &x, &y, 2);
    DS_1E38 = label - UI_TEXTS;                         /* dialog title */
    hotspots_push(0x22);
    while (!(b = ui_wait(3000))) ;
    if (b == -22) { hotspots_pop(1); restore_rect(saved, x, y); free_buffers(); return; }  /* Forget it */
    if (b != -20) return;                               /* other codes: returns with the dialog pushed */
    if (what & 4) cur_car->flags ^= 0x2000;
    if (what & 2) cur_car->flags ^= 0x4000;
    if (what & 1) cur_car->flags ^= 0x8000;
    mouse_cursor(-4); hotspots_pop(1); restore_rect(saved, x, y); free_buffers();
    car_draw(8, cur_car->model, car_flags(cur_car), (cur_car->flags >> 8) & 0x1F, 0, 0x98, 0,
             &TYRE_PICS[PARTS[cur_car->tyres->type].grade]);
    sfx_7841(); mouse_cursor(-2);
    game_clock += hours; money_add(-cost);
}

void stickers(void)                                      /* 0000:7367 */
{   if (!cur_car) { message(0xEFA); return; }
    if (money < 5) { message(0x1975); return; }         /* Stickers ain't free! */
    present_flag = 1;
    save = alloc_rect(0x108, 0x45); copy_rect(0x18, 0x0B, 0x108, 0x45, save);
    pic_draw(0x4B0 /*LIB2 #200 sticker sheet*/, 0x18, 0x0B, -1);
    hotspots_push(0x23);
    while (!(b = ui_wait(3000))) ;
    if (b < -0x26) return;
    if (b < -0x1E) {                                     /* -0x1F..-0x26 = sticker 1..8 */
        n = -b - 0x1E;
        mouse_cursor(-3); hotspots_pop(1); restore_rect(save, 0x18, 0x0B); free_buffers();
        cur_car->flags = (cur_car->flags & 0xE0FF) | (n << 8);
        /* peel-on animation: the sticker picture STICKERS[n] is composed on a copy of the car
           picture under it (DS:0294) at (sticker_dx + STICKERS[n].dx, STICKERS[n].dy) and wiped in
           8 pixels at a time with a mask picture (0x4C2 = LIB2 #194), 2 ticks per step (0f38:177d),
           sound 0f38:7573(1)/(0) around it */
        game_clock += 0x222; money_add(-5); mouse_cursor(-1);
        return;
    }
    if (b != -22) return;                                /* Forget it */
    hotspots_pop(1); restore_rect(save, 0x18, 0x0B); free_buffers();
}
```

`b == -0x1E` falls into "return" with the hot spots still pushed (never produced by screen 0x23).
Painting keeps customisations but removes the sticker.

### 4.9 Change tyres (0000:7800)

```c
void change_tires(void)
{   car = cur_car;
    if (!car) { message(0xEFA); return; }
    spares_collect(4, node, wear, 0);
    r = list_box(4, 1, 0, list_buf, wear, 0);            /* -3 Change, -4 Forget it */
    if (r == -4) return;
    if (r == -3) sel = list_selected();
    else if (r == 0x3EA) { message(0xF4E); return; }    /* no spare tires */
    /* other codes: sel keeps its (uninitialised) value — cannot happen with screen 4 */
    old_g = PARTS[car->tyres->type].grade; new_g = PARTS[node[sel]->type].grade;
    part_install(spare_unlink(node[sel]), &car->tyres);  /* old tyres become spares */
    tire_change_anim(&TYRE_PICS[old_g], &TYRE_PICS[new_g]);
    game_clock += 0x444;
}
```

### 4.10 Change transmission (0000:7aa4)

```c
void change_transmission(void)
{   car = cur_car;
    if (!car) { message(0xEFA); return; }
    for (i = 0; i < 2; i++) {                    /* bolt states: 0 out, 1 tightening, 2 loosening */
        v = car->trans_bolt[i];
        st[i+1] = v == 0 ? 0 : (v == 1 ? 1 : 2);
        SCREEN_REC[200 + i].type = 0x0B;
    }
    trans_pic = car->trans ? TRANS_PICS[PARTS[car->trans->type].grade] : 0;
    present_flag = (trans_pic == 0);
    pic_draw(0x42E /*LIB2 #46*/, 0x20, 10, -1);
    present_flag = 1;
    trans_screen_draw(trans_pic, car->trans_bolt, ...);
    hotspots_push(0x14);
    for (;;) {
        while (!(b = ui_wait(3000))) ;
        if (b == -1) break;                      /* Done */
        if (b == -2) {                           /* Parts */
            spares_collect(1, node, wear, 0);
            for (;;) {
                r = list_box(4, 1, 0, list_buf, wear, 0);
                if (r == -4) break;
                if (r == -3) { if (car->trans) { message(0x1875); break; } /* Where will you put it, speedy? */
                               sel = list_selected(); }
                else if (r == 0x3EA) { message(0xF72); break; }            /* no spare transmissions */
                else sel = r;
                if (sel == 0) break;
                old = trans_pic;
                t = node[sel]->type; trans_pic = TRANS_PICS[PARTS[t].grade];
                if (MODELS[car->model].spec & PARTS[t].make & 0xF) {       /* fits */
                    part_install(spare_unlink(node[sel]), &car->trans);
                    mouse_cursor(-4); sfx_76da(1); trans_slide_anim(trans_pic, 0); sfx_76da(0); sfx_7817();
                    game_clock += 0x666;
                    for (i = 1; i >= 0; i--) { st[i+1] = 1; car->trans_bolt[i] = 1; trans_bolt_draw(1, i); sfx_77e1();
                                               SCREEN_REC[200 + i].type = 0x0B; }
                    sound_update(); mouse_cursor(-2);
                    break;
                }
                trans_slide_anim(trans_pic, 1); message(0x10CD);          /* That won't fit! */
                trans_remove_anim(trans_pic, 1);
                game_clock += 0x888; trans_pic = old;
            }
            continue;
        }
        if (b > 0 && b < 3 && trans_pic) {       /* bolt 1 / 2 */
            i = b - 1;
            if (st[b] == 2) {                    /* loosen */
                if (--car->trans_bolt[i] < 2) {
                    car->trans_bolt[i] = 0; st[b] = 0; trans_bolt_remove_anim(i); sfx_77e1(); sound_update();
                } else { trans_bolt_draw(car->trans_bolt[i], i); sfx_7973(); }
                if (st[1] == 0 && st[2] == 0) {  /* both out: the transmission drops out */
                    trans_remove_anim(trans_pic, 0); game_clock += 0x666; trans_pic = 0;
                    car->trans_bolt[1] = car->trans_bolt[0] = 0;
                    part_uninstall(&car->trans); sound_update();
                }
            } else if (st[b] == 1) {             /* tighten */
                if (car->trans_bolt[i] == 3) st[b] = 2;
                else { car->trans_bolt[i]++; trans_bolt_draw(car->trans_bolt[i], i); sfx_7973(); }
            } else {                             /* put a bolt in */
                mouse_cursor(-4); car->trans_bolt[i] = 1; trans_bolt_draw(1, i); sfx_77e1(); mouse_cursor(-2);
                st[b] = 1;
            }
        }
    }
    hotspots_pop(1);
}
```

A click on a tight bolt (3) does nothing but switch it to "loosening"; each further click loosens by
one; at 1 the bolt comes out.

### 4.11 Engine bay (0000:81ab)

Layers are fitted bottom-up: **engine** (layer 0, 1 bolt), **manifold** (layer 1, bolts 2–3), then
1–3 **carburettors** (bolts 4–5, 6–7, 8–9, as many as the manifold takes). `layers` = number of
filled slots; parts are removed top-down by loosening their bolts; the engine additionally has a
connector (hot spot code 13, "Wires"/"The pump") that must be disconnected.

```c
void engine_bay(CAR *car)
{   if (!car) { message(0xEFA); return; }
    for (i = 1; i < 10; i++) {                       /* bolt states and pictures */
        v = car->bay_bolt[i-1];
        if (v == 0) { st[i] = 0; bay_bolt_pic[i-1] = 0; }
        else { st[i] = v == 1 ? 1 : 2; bay_bolt_pic[i-1] = BOLT_PICS[v]; }
    }
    bay_connector_pic = car->engine_link == 0 ? 0 : (car->engine_link < 1 ? 0x446 : 0x447);
    if (car->engine) bay_block_pic = PARTS[car->engine->type].grade == 0 ? 0x438 : 0x439;
    for (s = 0; s < 5; s++) { p = (&car->engine)[s]; bay_slot_pic[s] = p ? BAY_PICS[s][PARTS[p->type].grade] : 0; }
    BAY_LAYOUT[0][0] = BAY_LAYOUT[0][1] = (bay_slot_pic[0] == 0x434) ? 4 : 5;
    layers = 5 - (pic[0]==0) - (pic[1]==0) - (pic[2]==0) - (pic[3]==0) - (pic[4]==0);
    if (!car->manifold) max_layers = 5;
    else { mg = PARTS[car->manifold->type].grade; max_layers = CARBS_PER_MANIFOLD[mg] + 2;
           memcpy(bay_layout_cur, BAY_LAYOUT[mg], 12); }
    mouse_cursor(-4); bay_draw_car(car); bay_draw_parts(DS_5186, DS_519A, car == cur_car); mouse_cursor(-2);
    if (car != cur_car) return;                      /* opponent's engine (race 0000:a265): view only */
    bay_draw_bolts(DS_5186, DS_519A); hotspots_push(0x15);
    for (;;) {
        while (!(b = ui_wait(3000))) ;
        if (b == -1) { hotspots_pop(1); DS_8232 = DS_8234 = 0; video_a07e(); return; }   /* Done */
        if (b == -3) {                               /* Tune */
            if (!car_runnable(car, 0)) message(0xEA1);            /* Can't tune that mess, klutz! */
            else { ignition_tune(car); mouse_cursor(-4); bay_draw_parts(DS_5186, DS_519A, 1); mouse_cursor(-2); }
            continue;
        }
        if (b == -2) { fit_next_layer(); continue; }            /* Parts, below */
        if (b == 13) {                                          /* engine connector */
            car->engine_link = -car->engine_link;
            if (car->engine_link == 0) continue;
            game_clock += 0x6D;
            if (car->engine_link < 1) { bay_connector_pic = 0x446; bay_connector_draw(); bay_ctx = 0;
                                        bay_draw_bolts(...); goto check_removal; }
            bay_connector_pic = 0x447; bay_connector_draw(); bay_draw_bolts(...);
            continue;
        }
        if (b < 1 || b > 9) continue;
        /* bolt b: which layer does it hold? */
        for (L = 0, sum = 0; L < 5 && (sum += BOLTS_PER_LAYER[L]) < b; L++) ;
        if (!bay_slot_pic[L]) continue;
        game_clock += 0x36;
        if (st[b] == 2) {                                       /* loosen */
            bay_ctx = BOLT_CTX[b]; bay_bolt_side = BOLT_SIDE[b];
            if (--car->bay_bolt[b-1] < 2) { car->bay_bolt[b-1] = 0; st[b] = 0; bay_bolt_pic[b-1] = 0;
                                            bay_bolt_remove_anim(); sfx_77e1(); }
            else { bay_bolt_pic[b-1] = BOLT_PICS[car->bay_bolt[b-1]]; bay_bolt_draw(...); sfx_7973(); }
          check_removal:
            do {
                slot = -1; cascade = 0;
                if (bay_ctx == 0) {                 /* engine: last layer, disconnected, bolt 1 out */
                    if (layers == 1 && car->engine_link == -1 && st[1] == 0) {
                        slot = 0; car->engine_link = 0; bay_connector_pic = 0; game_clock += 0x666; }
                } else if (bay_ctx == 1) {          /* manifold: bolts 2 and 3 out, no carbs */
                    if (layers == 2 && st[2] == 0 && st[3] == 0) { slot = 1; cascade = 1; game_clock += 0x333; }
                } else {                            /* carb pair */
                    first = BOLT_SIDE[b] == 1 ? b - 1 : b;
                    if (st[first] == 0 && st[first+1] == 0) {
                        slot = (first - 4) / 2 + 2;
                        if (layers == 3) cascade = 2;
                        game_clock += 0x199;
                    }
                }
                if (slot >= 0) {
                    layers--;
                    part_uninstall(&(&car->engine)[slot]);
                    mouse_cursor(-4); bay_slot_pic[slot] = 0;
                    if (cascade) bay_ctx = cascade - 1;
                    bay_draw_car(car); bay_draw_parts(DS_5186, DS_519A, 1); bay_draw_bolts(DS_5186, DS_519A);
                    car->ignition = random(13) - 8;
                    mouse_cursor(-2);
                }
            } while (cascade);      /* NB: 'cascade' is re-zeroed at the top: loop runs once more */
        } else if (st[b] == 1) {                                /* tighten */
            if (car->bay_bolt[b-1] == 3) st[b] = 2;
            else { car->bay_bolt[b-1]++; bay_bolt_pic[b-1] = BOLT_PICS[car->bay_bolt[b-1]];
                   bay_ctx = BOLT_CTX[b]; bay_bolt_side = BOLT_SIDE[b]; bay_bolt_draw(...); sfx_7973(); }
        } else {                                                /* insert a bolt */
            bay_ctx = BOLT_CTX[b]; bay_bolt_side = BOLT_SIDE[b];
            bay_bolt_pic[b-1] = BOLT_PICS[1]; bay_bolt_draw(...); sfx_77e1();
            st[b] = 1; car->bay_bolt[b-1] = 1;
        }
    }
}
```

The removal loop is `do { … } while (cascade != 0)` in the original with `cascade` (`local_128`)
cleared at the start of every pass: removing the manifold with `cascade = 1` sets `bay_ctx = 0` and
re-tests the engine; removing the only carb with `cascade = 2` sets `bay_ctx = 1` and re-tests the
manifold. A pass that removes nothing ends the loop.

`fit_next_layer` (the `b == -2` branch, inline in the original):

```c
    cat = LAYER_CATEGORY[layers];
    spares_collect(cat, node, wear, 0);
    for (;;) {
        r = list_box(4, 1, 0, list_buf, wear, 0);
        if (r == -4) return;
        if (r == -3) sel = list_selected();
        else if (r == 0x3EA) { if (cat == 0) message(0xF97); if (cat == 3) message(0xFD7);
                               if (cat == 2) message(0xFB5); return; }   /* no spare engine / manifold / carburator */
        if (sel == 0) return;
        if (layers >= max_layers) { message(0x1875); return; }        /* Where will you put it, speedy? */
        t = node[sel]->type; mk = MODELS[car->model].spec & PARTS[t].make & 0xF;
        slot = layers;
        if (layers == 0) {                          /* engine */
            fits = mk != 0; bay_ctx = 0; first_bolt = 0;
            car->engine_link = -1; bay_connector_pic = 0x446;
            game_clock += fits ? 0x666 : 0x888;
        } else if (layers == 1) {                   /* manifold */
            mg = PARTS[t].grade;
            fits = mk && MANIFOLD_FITS[PARTS[car->engine->type].grade][mg];
            bay_ctx = 1; first_bolt = 1;
            max_layers = CARBS_PER_MANIFOLD[mg] + 2; memcpy(bay_layout_cur, BAY_LAYOUT[mg], 12);
            game_clock += fits ? 0x333 : 0x444;
        } else if (layers <= 4) {                   /* carburettor */
            mg = PARTS[car->manifold->type].grade;
            fits = mk && CARB_FITS[mg][PARTS[t].grade];
            game_clock += fits ? 0x199 : 0x222;
            first_bolt = 3; slot = 2; bay_ctx = BAY_LAYOUT[mg][0];
            switch (bay_ctx) {
            case 4: case 5: case 6: break;
            case 7: if (car->carb[0]) { if (!car->carb[1]) { bay_ctx = 8; first_bolt = 5; slot = 3; }
                                        else              { bay_ctx = 9; first_bolt = 7; slot = 4; } } break;
            case 10: case 12: if (car->carb[0]) { bay_ctx++; first_bolt = 5; slot = 3; } break;
            default: continue;                      /* back to the list */
            }
        } else continue;
        bay_slot_pic[slot] = BAY_PICS[slot][PARTS[t].grade];
        if (!fits) {
            sfx_76da(1); bay_part_in_anim(bay_slot_pic[slot], 1); sfx_76da(0); sfx_7817();
            message(0x10CD);                        /* Pay attention, dummy! That won't fit! */
            sfx_76da(1); bay_part_out_anim(bay_slot_pic[slot], 1); sfx_76da(0);
            bay_slot_pic[slot] = 0;
            if (slot == 0) { car->engine_link = 0; bay_connector_pic = 0; }
            continue;                               /* the part stays a spare */
        }
        part_install(spare_unlink(node[sel]), &(&car->engine)[slot]);
        mouse_cursor(-4); sfx_76da(1); bay_part_in_anim(bay_slot_pic[slot], 0); sfx_76da(0); sfx_7817();
        if (slot == 0) { bay_block_pic = bay_slot_pic[0] == 0x434 ? 0x438 : 0x439;
                         BAY_LAYOUT[0][0] = BAY_LAYOUT[0][1] = bay_slot_pic[0] == 0x434 ? 4 : 5;
                         bay_connector_pic = 0x446; bay_connector_draw(); }
        for (k = BOLTS_PER_LAYER[layers] - 1; k >= 0; k--) {   /* loose bolts */
            i = first_bolt + k; bay_bolt_side = k;
            bay_bolt_pic[i] = BOLT_PICS[1]; bay_bolt_draw(BOLT_PICS[1]); sfx_77e1();
            st[i+1] = 1; car->bay_bolt[i] = 1;
        }
        bay_draw_bolts(DS_5186, DS_519A); mouse_cursor(-2);
        layers++;
        car->ignition = random(13) - 8;
    }
```

(`first_bolt` is `local_12c`; for the engine it is 0 → bolt 1, manifold 1 → bolts 2–3, carbs 3/5/7 →
bolts 4–5 / 6–7 / 8–9.) Making the car run again after an engine swap: fit engine, tighten its
bolt ×2 more clicks (1→3), click the connector (−1 → 1), fit manifold and carbs, tighten all to 3.

### 4.12 Runnable check (0000:7ee6)

```c
int car_runnable(CAR *c, int full)
{   if (!c->engine || c->engine_link < 1 || !c->manifold) return 0;
    n = CARBS_PER_MANIFOLD[PARTS[c->manifold->type].grade];
    for (k = 0; k < n; k++) if (!c->carb[k]) return 0;
    for (i = 2*n + 2; i >= 0; i--) if (c->bay_bolt[i] != 3) return 0;   /* bolts 1..3+2n */
    if (full) {
        if (!c->trans || !c->tyres) return 0;
        if (c->trans_bolt[1] != 3 || c->trans_bolt[0] != 3) return 0;
    }
    return 1; }
```

### 4.13 Tuning the ignition (0000:7fb1)

```c
void ignition_tune(CAR *car)
{   hotspots_push(0x1C);                       /* -7 Retard, -10 Advance, -1 done */
    pic_draw(0x42F /*LIB2 #47*/, 0x60, 9, -1);
    a = &ANIM[anim_start(8)];                  /* timing-light strobe, DS:7568 + 0x2C*slot */
    t0 = tick_clock; music_off_0d99(); DS_58D4 = 2;
    if (sound_on) { outp(0x61, inp(0x61) | 3); speaker_on = 1; }
    do {
        if (sound_on && sound_on != speaker_on) { outp(0x61, inp(0x61) | 3); speaker_on = 1; }
        div = 0x7530 + abs(car->ignition) * 0x400;   /* engine note: 30000 at ignition 0 */
        outp(0x42, div & 0xFF); outp(0x42, div >> 8);
        y = car->ignition * 2 + 0x4B;
        mouse_cursor(-4); blit(a->frame, page, {8, 0x0B, 0x30, 0, 0xB0, ...}); copy_to_screen(..., 0xB0, y, 0xB7, y + 10); mouse_cursor(-2);
        while (!(b = ui_wait(0))) keep_speaker_on();
        if (b == -10 && ++car->ignition > 4) car->ignition = 4;
        else if (b == -7 && --car->ignition < -8) car->ignition = -8;
    } while (b != -1);
    outp(0x61, inp(0x61) & 0xFC); speaker_on = 0; DS_58D4 = 1; music_on_0dd8();
    anim_stop(8);
    dt = (u16)(tick_clock - t0) * 3; if (dt > 0x444) dt = 0x444;   /* 16-bit */
    game_clock += dt;
    hotspots_pop(1);
}
```

The best setting is 0 (`top_speed` term `(1 − |ign|/8)·0.2`); the strobe's timing mark moves 2
lines per step and the tone drops with `|ignition|`.

### 4.14 New game and opponents (0000:56a9, 5584, 542e)

```c
void new_game(void)                                /* 0000:56a9 */
{   pools_init(1);
    money = demo_mode ? 10000 : 750; DS_7EAE = 10;
    fn_2645_0b91(100);                             /* video/driver, see video */
    opponents_init();
    game_clock = 0; clock_origin = tick_clock;
    race_wins[0..3] = 0; }

void opponents_init(void)                          /* 0000:5584 */
{   int cheap[11], mid[11], rich[11];              /* [0] = count, then model indices */
    for (m = 0; m < 25; m++) {
        k = MODELS[m].klass;
        if (k >= 6) rich[++rich[0]] = m; else if (k >= 3) mid[++mid[0]] = m; else cheap[++cheap[0]] = m;
    }
    if (demo_mode) cheap[0]--;
    for (i = 0; i < 7; i++)  opponent_init(i, cheap);
    for (; i < 14; i++)      opponent_init(i, mid);
    for (; i < 21; i++)      opponent_init(i, rich);
    DS_817D = 0;                                  /* OPPONENTS[21] byte +0x0B (The King) */
    if (demo_mode) { cheap[0] = 1; cheap[1] = 24; opponent_init(DS_4FF4 /*0*/, cheap);
                     OPPONENTS[DS_4FF4].name = 0x629; }    /* "The Geek" */
}

void opponent_init(int i, int *pool)               /* 0000:542e; o = DS:7FF8 + i*18 */
{   do { col = random(6); } while (col == 5 && random(2) != 1);
    o->colour = col; memset(&o->b0B, 0, 6);        /* +0A colour, +0B..+10 = 0 */
    do { k = random(pool[0]); } while (pool[k+1] == -1);
    m = o->model = pool[k+1]; pool[k+1] = -1;      /* +08 */
    lvl = (MODELS[m].klass >> 1) + 1;
    o->flags = ((MODELS[m].spec >> 8) & 7) == 2 ? 8 : 0;           /* +0E, car_flags bits */
    o->sticker = random(20) < lvl ? random(9) : 0;                   /* +0F */
    if (MODEL_PICS[m].rear_bumper > 0 && MODEL_PICS[m].front_bumper > 0)
        o->flags |= random(25) < lvl ? 3 : 0;
    if (MODEL_PICS[m].roof > 0) o->flags |= random(30) < lvl ? 4 : 0;
}
```

Opponent record (18 bytes, HOT_DATA block 0 — other fields are race's): `+0` group 0/1/2, `+1`,
`+2` race parameters, `+4` picture (driver), `+6` name text id, `+8` model, `+0A` colour, `+0E`
car-flags, `+0F` sticker. Note: `random(pool[0])` with a pool of 7 or more models always finds a
free one because each pool is used 7 times (the demo reduces the cheap pool to 6 + the Geek).

### 4.15 Calendar and date (0000:6476–6750)

```c
int day_index(void)          /* 0000:6476: days since the start, max 91 */
{   d = (u32)(game_clock + tick_clock - clock_origin) / 0x1998;  /* 1e16:23ec long div */
    return d > 0x5B ? 0x5B : d; }

void clock_hm(int *h, int *m)                        /* 0000:64a3 */
{   t = game_clock + tick_clock - clock_origin; if (t > 0x91908) t = 0x91908;
    r = t % 0x1998; *h = r / 0x222; *m = (r - *h * 0x222) * 60 / 0x222; }

void summer_over_check(void) { if (day_index() > 0x5A) end_flags |= 1; }   /* 0000:6529 */

int month_of_day(int d, int *dom)                    /* 0000:653c */
{   if (d < 15)  { *dom = d + 16; return 0; }        /* June 16..30 */
    if (d < 46)  { *dom = d - 14; return 1; }        /* July */
    if (d < 77)  { *dom = d - 45; return 2; }        /* August */
    if (d > 90)  { *dom = 15; return 3; }
    *dom = d - 76; return 3; }                       /* September 1..14 */

void date_print(int y)                               /* 0000:6750 */
{   m = month_of_day(day_index(), &dom);
    sprintf(buf, "%s %d, 1963", MONTH_NAME[m] /*DS:5082*/, dom);
    with the newspaper text colours (DS:787E+0x41): text_draw(page, 0x61, y, buf); }

void calendar_show(int quick)                        /* 0000:65af */
{   summer_over_check();
    if (!quick && end_flags) return;
    d = day_index(); clock_hm(&h, &mi);              /* h, mi unused */
    m = month_of_day(d, &dom);
    row = (d + 0x15) / 7 - CAL_ROW0[m] /*DS:5052*/;
    x = ((d + 0x15) % 7) * 0x12 + DS_5048 /*33*/;
    y = row * 7 + CAL_Y0[m] /*DS:504A*/;
    present_flag = 1;
    pic = load(m < 2 ? 0x4C3 /*LIB2 #219 June/July*/ : 0x4C5 /*#221 Aug/Sep*/);
    overlay(pic, 0x4C4 /*#220 day marker*/, x & 0xFF, y & 0xFF);
    save = alloc_rect(...); copy_rect(0x50, 4, ..., save); blit(pic at 0x50, 4); free;
    if (!quick) { text_draw(-1, DS_505A, 0); wait_ticks_or_click(500); restore_rect(save, 0x50, 4); }
    free_buffers(); }
```

Game-clock costs (`0x222` = 1 hour): buying a part 1 h, selling a part / buying a car / changing
tyres 2 h, fitting a transmission or engine 3 h (4 h if it doesn't fit), manifold 1.5 h (2 h),
carburettor 45 min (1 h), bolt click ≈ 6 min (`0x36`), connector ≈ 12 min (`0x6D`), selling a car 4 h,
paint 4 h, stickers 1 h, bumper 4 h, roof 16 h (`0x2220`), tuning = 3 × elapsed ticks (max 2 h),
each drive/visit in `game_loop` 1–2 h. A day is 12 hours (`0x1998`); the summer ends after day 90
(June 16 → September 14).

### 4.16 Top speed (0000:e218, race range) — "how parts affect performance"

```c
int top_speed(int model)     /* model >= 0: a stock car of that model built on the stack
                                (parts from MODELS[model].spec, wear from INIT_WEAR, flags 0);
                                model < 0: cur_car */
{
    c = model < 0 ? cur_car : &tmp;
    eg = grade(c->engine); mg = grade(c->manifold); cg = grade(c->carb[0]);
    tg = grade(c->trans);  /* tyres read but unused */
    P = &MODEL_PICS[c->model];
    double f = 0.09 * ((c->flags & 0x2000) || P->roof == 0)
             + 0.05 * ((c->flags & 0x8000) || P->front_bumper == 0)
             + 0.01 * ((c->flags & 0x4000) || P->rear_bumper == 0)
             + CLASS_PTS[MODELS[c->model].klass] /*DS:556E*/ * 0.1 / 88.0
             + CARB_PTS[cg*5 + mg]               /*DS:552E*/ * 0.15 / 100.0
             + ENGINE_PTS[eg]                    /*DS:554C*/ * 0.25 / 100.0
             + TRANS_PTS[tg]                     /*DS:5552*/ * 0.15 / 100.0
             + (1.0 - fabs(c->ignition / 8.0)) * 0.2;
    f = f * (10000.0 - c->engine->wear) / 10000.0;
    f = f * (tg == 0 ? 0.8 : 1.0) * 0.3 + 0.7;
    v = f * 130.0; if (v > 130.0) v = 130.0;
    return ftol(v);
}
```

`CLASS_PTS = 40 44 48 60 64 68 80 84 88`, `CARB_PTS[carb][manifold] = {0 0 100 0 0}, {0 50 0 100 75},
{0 0 0 50 100}`, `ENGINE_PTS = 0 50 100`, `TRANS_PTS = 0 33 66 100`. The constants are doubles at
`DS:6988`–`6A00`. The maximum is 130 mph; `tools/srtables.py` prints the stock values (98–127 mph at
ignition 0). The integer is shown in Car info ("Max speed %3i mph") and stored by the race start
(`cdaf` → `DS:6CAC`); acceleration, gears and damage are race's.

### 4.17 Car pictures (0f38:8e48, 8866)

`car_draw(mode, model, flags, sticker, driver, y, smoke, tyre_pics)`:

| mode | use |
|---|---|
| 2 | leaves the gas station (drives right) |
| 3 | garage, drawn in place (after a trip) |
| 4 | drives in (garage, gas station); location 3 also loads the pump pictures |
| 6 | drives out (sold / switched car) |
| 7 | "See it" in the classifieds (static) |
| 8 | redraw after customising (static) |

The car is centred: `x = ((320 − w)/2 + 4) & ~7`, bottom at `y`; `car_x/car_y` (`DS:825E/8260`) are
set for the hot spots. `car_compose` (`8866`) builds the picture: base `MODELS[m].picture`; overlays
from `MODEL_PICS` at `MODEL_LAYOUT` offsets — flag 2 rear bumper stripped (`+6`), flag 1 front
bumper stripped (`+8`), flag 4 chopped roof (`+2`), flag 8 hood scoop/blower (`+4`, not for model
25); the sticker `STICKERS[n]` at layout `(+10,+11) + (dx,dy)`; the driver picture; the wheels
`tyre_pics[0..3]` (4 frames when animated) at layout x `+0`, `+1`; the smoke picture 0x49D. Colour
comes from the palette (registers 6/7), not from the picture. Drawing and animation are video.

### 4.18 Car info (0000:79eb, 78f1)

```c
void car_info(void)
{   if (!cur_car) { message(0xEFA); return; }
    car_info_list(xoff, wear);                 /* engine, manifold, carbs 1-3, trans, tyres if present;
                                                  x offsets DS:50CE (0,5,5,5,5,0,0) indent manifold/carbs;
                                                  then 4 rows with ids -1..-4 (texts via DS:511E):
                                                  "", "Fuel  NN gallons", "", "Max speed NNN mph" */
    v = car_runnable(cur_car, 1) ? top_speed(-1) : 0;
    sprintf(DS:50FE, " %3i mph", v);           /* into "Max speed      mph" */
    sprintf(DS:50E2, " %2i gallons", cur_car->gas / 10);
    r = list_box(0x26, 1, xoff, list_buf, wear, DS:511E);   /* "Car parts", OK = -3 */
    if (r == 0x3EA) message(0xF4E); }
```

### 4.19 List box (0f38:4f4f, 500e, 4fc1)

`list_box(screen, sel, xoff, list, wear, extra)`: returns `0x3EA` if `list[0] == 0`; otherwise saves
the screen rectangle, runs `list_box_run`, restores. `list_box_run` shows `list[1..]` (text id ≥ 0 →
`TEXTS+id`; negative → `extra[-id-1]`) in the rows of the screen (10 rows in screens 4/0xB, 11 in
0x26, 15 in 3/5), each optionally followed by `wear_text(wear[i])` right-aligned unless `wear[i] ==
0x80`. A click on a row highlights it and sets `list_selected()` (it does not return); −7/−8/−9/−10
scroll one line up / a page up / a page down / a line down; −6 edits the entry (save names, screen
5, game_flow); any other negative code returns. `wear_text(p)`: `p == 0` → `"(`new`)"`, else
`"(" + itoa(p) + "% worn)"` built in place at `DS:07AA` (the `` ` `` is a narrow space in the font).

### 4.20 Gas station (0000:b8a1, race range)

```c
void gas_station(void)       /* reached from game_loop code 15 after: car runnable (full),
                                gas < 140 ("You've got a full tank!"), money > 0 ("Gas ain't free!"),
                                drive (location 3, 8d26), game_clock += 0x222 */
{   text_draw(-1, DS_53C2, 0); location = 3;
    pic_draw_screen(0x442 /*LIB2 #66?*/, -1); money_draw(DS_49DC);
    present_flag = 1; driver_hook_78C2(); mouse_cursor(1);
    car_draw(4, cur_car->model, car_flags(cur_car), sticker, 0, 0xA9, 1, &TYRE_PICS[tyre_grade]);
    ... save the pump / nozzle / cap rectangles ...
    price = 1; wait = 40; units = (190 - cur_car->gas) / 30;   /* 3-gallon units that fit */
    nozzle = pump; pumping = 0; paid = 0;
    set spots: hose on the pump, tank cap (b4de); hotspots_push(0x17);
    deadline = ticks + 0x14;
    loop on ui_wait(3000):
      3  attendant       -> message(0x10F6 "Get out of my face, dummy!")
      1  rag/windshield  -> toggle washing (state 3, picture 0x473) when DS:6CA6
      2  tank cap        -> cap off / on (money and "pumping" checks) when DS:6CA8
      4  nozzle / filler -> hose to the car: if units == 0 or money < price
                             -> message(0x10B5 "Pay attention, dummy!"); else start pumping,
                             deadline = ticks + 0x4C; back to the pump -> "It's not full, speedy!" (0x18DD)
                             when units remain and money suffices
      1000 time-out:
         not pumping: if (--wait <= 0) { full service: price <<= 1; sfx_play_wait(3);
                         message(0x19E1 "Yo, you need full service, dummy!"); pumping starts by itself }
         pumping:     units--; cur_car->gas += 30;  money_add(-(money >= price ? price : money));
                      if (units == 0 || money < price) stop the pump; leave if the attendant served
      leaving (cap back / code 2 with no money or after pumping) -> gas_leave()
}

void gas_leave(int *st)      /* 0000:b717 */
{   if (st[6] != 1) mouse_cursor(1);
    if (cap was off) { put the cap back; if (cap lost) money_add(-min(10, money)); }
    if (hose not back on the pump) { restore; if (flag) money_add(-min(10, money)); }
    if (!served) attendant animation (0x4D1 = LIB2 #209, 0f38:975b);
    if (cur_car->gas > 170) cur_car->gas = 170;
    car_draw(2, ...);  hotspots_pop(1); free_buffers(); location = 1;
    drive_back_8d26();       /* race: back through town */
}
```

The gas-station state machine uses 13 locals (hose position, cap, windshield, served, paid…); the
outline above follows the decompile of `b8a1`/`b717`, which is readable (no floating point). The
exact bookkeeping of the locals must be ported from the decompile line by line; see §8.

---

## 5. Data tables

All values below are printed by `python tools/srtables.py` (original `Game/HOT_DATA`); the Street Rod
SE data disk (`Game/datadisk/HOT_DATA`) has the same layout with 25 other cars.

### 5.1 Part catalogue `DS:4806` (in the EXE), 8-byte records

`{i16 price, u8 grade, u8 make_mask (1 GM, 2 Ford, 4 Chrysler, 7 all), u8 category (0 engine, 1
transmission, 2 carburettor, 3 manifold, 4 tyres), u8 0, i16 text_id}`; 43 used records (index 43 is
zero; the Auto Parts loop runs to 0x2C).

| idx | price | grade | make | category | text |
|---:|---:|---:|---|---|---|
| 0–2 | 19 | 0 | GM / Ford / Chrysler | engine | V-6 engine (not sold; `part_value` = 100) |
| 3 | 350 | 1 | GM | engine | GM V-8 engine. 283 cu.in. A clean machine. $350 |
| 4 | 340 | 1 | Ford | engine | Ford V-8 engine. 292 cu.in. Never raced. $340 |
| 5 | 360 | 1 | Chrysler | engine | Chrysler V-8 eng 285 cu.in. Like new. $360 |
| 6 | 500 | 2 | GM | engine | GM V-8 engine. 327 cu.in. Low mileage. $500 |
| 7 | 475 | 2 | Ford | engine | Ford V-8 eng 351 cu.in. $475 |
| 8 | 525 | 2 | Chrysler | engine | Chrysler V-8 eng 340 cu.in. Just rebuilt. $525 |
| 9–11 | 130 / 120 / 125 | 0 | GM / Ford / Chrysler | trans | Auto Trnsmsn |
| 12–14 | 175 / 160 / 170 | 1 | GM / Ford / Chrysler | trans | Trnsmsn 3-spd |
| 15–17 | 250 / 260 / 245 | 2 | GM / Ford / Chrysler | trans | Trnsmsn 4-spd. |
| 18 | 275 | 2 | all | trans | Trnsmsn 4-spd. Fits all. |
| 19–21 | 400 / 405 / 410 | 3 | GM / Ford / Chrysler | trans | Racing 4-spd. |
| 22 | 25 | 0 | all | carb | 2-brl. carb. Fits all. |
| 23 | 40 | 1 | all | carb | 4-brl. carb. Fits all. |
| 24 | 60 | 2 | all | carb | 4-brl racing carb. Fits all models. Chrome. |
| 25–27 | 20 | 0 | GM / Ford / Chrysler | manifold | 2-brl manifold |
| 28–30 | 25 | 1 | GM / Ford / Chrysler | manifold | 4-brl manifold |
| 31–33 | 35 | 2 | GM / Ford / Chrysler | manifold | manifold for 3 × 2-brl carbs |
| 34–36 | 35 | 3 | GM / Ford / Chrysler | manifold | manifold for 2 × 4-brl carbs |
| 37–39 | 60 | 4 | GM / Ford / Chrysler | manifold | racing manifold |
| 40 | 50 | 0 | all | tyres | Tires. Four for $50 |
| 41 | 80 | 1 | all | tyres | Tires. Brand name. All sizes. Four for only $80 |
| 42 | 150 | 2 | all | tyres | Racing slicks. All sizes. Four for $150 |

Index formulas used by `car_new` / `top_speed`: engine `grade*3 + make`, transmission `9 + make +
grade*3 + (grade == 3)` (never picks 18, "fits all"), manifold `0x19 + grade*3 + make`, carb
`0x16 + grade`, tyres `0x28 + grade`, with `make = (spec & 0xF) >> 1` (0 GM, 1 Ford, 2 Chrysler).

Compatibility (`engine_bay`, `change_transmission`): make mask `MODELS[m].spec & PARTS[t].make &
0xF ≠ 0` for everything except tyres (not checked); **`MANIFOLD_FITS[engine grade][manifold grade]`**
(`DS:4978`): V-6 `1 0 1 0 0`, 283-class `1 1 0 1 1`, 327-class `1 1 0 1 1` — the 3×2-brl manifold
fits only a V-6; **`CARB_FITS[manifold grade][carb grade]`** (`DS:4988`): 2-brl manifold `1 0 0`,
4-brl `0 1 1`, 3×2 `1 0 0`, 2×4 `0 1 1`, racing `0 1 1`. `CARBS_PER_MANIFOLD` = 1, 1, 3, 2, 2.
Wear on creation: engines, transmissions and tyres 0; carbs and manifolds −128 (no wear).

### 5.2 Car models `DS:7D86` (HOT_DATA block 1, 26 × 10 bytes)

`{i16 price, u8 class, i8 next_for_sale, u16 spec, i16 ad_text, i16 picture}` — spec bits 0–3 make
mask, 4–5 tyre grade, 6–7 transmission grade, 8–10 engine grade, 11–13 manifold grade, 14–15 carb
grade. `class` 0–8 sets the opponent pool (<3, 3–5, ≥6), the race level `(class>>1)+1` and
`CLASS_PTS`. Pictures are ids (`1000 + LIB2 index`).

| m | price | cls | make | eng | trn | man | carb | tyre | LIB2 | ad (first line) |
|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|---|
| 0 | 400 | 0 | GM | 0 | 0 | 0 | 0 | 0 | 39 | 1940 Chevrolet 2-dr coupe. Auto. Runs good. |
| 1 | 2865 | 7 | GM | 2 | 3 | 1 | 1 | 1 | 40 | 1940 Chevrolet Roadster. Hot V-8. Racing transmission. |
| 2 | 1755 | 5 | GM | 2 | 2 | 3 | 1 | 1 | 47 | 1938 Chevrolet Master Deluxe 2-dr. |
| 3 | 1450 | 3 | GM | 1 | 2 | 1 | 1 | 0 | 48 | 1956 Corvette. Rebuilt eng. 4-spd. |
| 4 | 2250 | 5 | GM | 2 | 2 | 1 | 1 | 1 | 53 | 1961 Corvette. BIG engine. 4-spd. |
| 5 | 3770 | 8 | GM | 2 | 3 | 4 | 2 | 2 | 54 | 1963 Corvette. Car of your dreams. |
| 6 | 625 | 0 | Chrysler | 1 | 0 | 1 | 1 | 0 | 60 | 1955 Dodge Custom Royal Lancer. |
| 7 | 1700 | 4 | Ford | 1 | 2 | 1 | 1 | 1 | 61 | 1932 Ford. Customized deuce coupe. |
| 8 | 2050 | 6 | Chrysler | 1 | 1 | 0 | 0 | 0 | 112 | 1962 Dodge Polara. A real mover. |
| 9 | 1200 | 2 | Chrysler | 1 | 0 | 0 | 0 | 0 | 113 | 1962 Plymouth Savoy 2-dr. Auto. |
| 10 | 2680 | 7 | Ford | 2 | 2 | 3 | 1 | 1 | 118 | 1932 Ford Sedan. V-8. 4-spd. 2-4 brl carbs. |
| 11 | 2450 | 6 | Ford | 1 | 2 | 1 | 1 | 1 | 119 | 1940 Ford Deluxe 2-dr. |
| 12 | 1950 | 5 | Ford | 1 | 2 | 1 | 1 | 1 | 126 | 1955 Ford Fairlane Victoria. Hot. |
| 13 | 640 | 1 | Ford | 1 | 1 | 0 | 0 | 0 | 127 | 1957 Ford Fairlane 500 2-dr. V-8 engine. |
| 14 | 900 | 1 | Ford | 1 | 1 | 1 | 1 | 1 | 131 | 1954 Mercury Monterey. Customized. |
| 15 | 455 | 0 | Ford | 0 | 0 | 0 | 0 | 0 | 135 | 1956 Mercury Custom 2-dr. |
| 16 | 475 | 1 | GM | 1 | 0 | 0 | 0 | 0 | 145 | 1949 Chevrolet 2dr Styleline. |
| 17 | 1260 | 2 | GM | 1 | 0 | 0 | 0 | 1 | 149 | 1958 Chevrolet Impala. |
| 18 | 1850 | 4 | GM | 1 | 0 | 1 | 1 | 1 | 156 | 1949 Olds 88 2dr. Customized. |
| 19 | 2055 | 6 | GM | 2 | 2 | 1 | 1 | 1 | 157 | 1952 Olds 88. Customized. 4-spd. |
| 20 | 3100 | 7 | GM | 2 | 2 | 1 | 1 | 2 | 164 | 1950 Pontiac Silver Streak. Racing tires. |
| 21 | 3350 | 8 | Ford | 2 | 3 | 3 | 1 | 1 | 165 | 1957 T-Bird. Racing trnsmsn. 2-4brl carbs. |
| 22 | 1350 | 3 | Chrysler | 0 | 1 | 0 | 0 | 0 | 171 | 1961 Valiant V-200. |
| 23 | 1575 | 4 | Chrysler | 2 | 1 | 1 | 1 | 0 | 172 | 1963 Valiant V-100 2-dr. Big engine. |
| 24 | 950 | 2 | GM | 1 | 1 | 1 | 1 | 0 | 179 | 1955 Chevrolet Bel-Air Sport coupe. |
| 25 | 0 | 8 | GM | 2 | 3 | 4 | 2 | 2 | 201 | 1963 Corvette (The King's car) |

Used Cars ad order (`DS:7D16` = 2, then `next_for_sale`): 2 0 1 16 24 17 3 4 5 6 8 7 10 11 12 13 14
15 18 19 9 20 21 22 23 (model 25 is never for sale). The list is static (`next_for_sale` is never
written): every car stays for sale after it is bought.

### 5.3 Model overlay pictures `DS:8DF0` (HOT_DATA block 2, 26 × 10)

`{u8 b0, u8 _, i16 roof_chopped, i16 scoop, i16 rear_bumper_stripped, i16 front_bumper_stripped}`
(ids; 0 = not possible). `b0` (1–4) is read by the race (`DS:7648*10 − 0x7210`), not here. The
Corvettes (3–5) cannot be chopped; the 1932 Fords (7, 10) have no bumpers; model 25 has nothing.

### 5.4 Per-model layout blocks (HOT_DATA)

* **block 3** `DS:7682`, 12 bytes: `[0],[1]` rear / front wheel x, `[2],[3]` driver x / y, `[4]` roof
  x, `[5],[6]` scoop x / y, `[7]` rear bumper x, `[8],[9]` front bumper x / y, `[10],[11]` sticker x /
  y (`DS:8276/8272` split `[10]` by the page mask `DS:823E`).
* **block 5** `DS:818C`, 6 bytes: filler point (`DS:826E/8270`), windshield points (`DS:826A/826C`,
  `DS:8266/8268`, `DS:8264` = second one present) for the gas station.
* **block 4** `DS:70E4`, 20 bytes: 5 × `{dx, dy, w, h}` garage hot spots relative to the car: rear
  bumper (11), roof (13), transmission (9), hood (10), front bumper (12).

### 5.5 Opponents `DS:7FF8` (HOT_DATA block 0, 22 × 18)

Names (ids into TEXTS): Mike, Ralph, Kirk, Biff (group 0), Butch, Wally, Chip, Eddie, Ernie, Dick,
Steve (group 1), John, Jim, Charlie, George, Clyde, Tom, Erica, Spike, Bill, Vinnie (group 2), The
King (model 25, flags 8). Records 0–6 get cheap models, 7–13 middle, 14–20 expensive
(`opponents_init`); the group byte in the file does not follow that split (see race).

### 5.6 Small tables

| DS | contents |
|---|---|
| `4966` | initial wear per category: 0, 0, −128, −128, 0 |
| `4F4E` | headline pictures 1022, 1023, 1025, 1024, 1026 |
| `5030` | wheel pictures `[grade][4 frames]`: LIB2 #65–68 (plain), #28–31 (brand), #106–109 (slicks) |
| `50C2` | paint `{reg6, reg7}`: 0 red (4,12), 1 cyan (3,11), 2 green (2,10), 3 magenta (5,13), 4 blue (1,9), 5 grey (7,15) |
| `5082` | month names June, July, August, September |
| `5048`, `504A`, `5052` | calendar: x0 = 33; per month y0 and first row |
| `508A` | car hot-spot codes 11, 13, 9, 10, 12 |
| `50CE` | Car info x offsets 0, 5, 5, 5, 5, 0, 0 |
| `7844` | stickers 1–8: LIB2 #193, 192, 191, 190, 189, 188, 186, 187 with (dx, dy) (2,2) (1,5) (4,2) (0,6) (0,2) (8,1) (0,0) (1,1) |
| `4998` | engine bay pictures `[slot][grade]`: engine #76–78; manifold #81, 82, 79, 83, 104; carb #84–86 |
| `49CA` | bolt pictures by tightness: none, #87, #88, #89 |
| `49D2` | transmission pictures by grade: #99–102 |
| `5126`, `5130`, `513C` | carbs per manifold 1 1 3 2 2; bolts per layer 1 2 2 2 2; category per layer 0 3 2 2 2 |
| `5148` | bay layout per manifold grade (6 words each): `{4/5,4/5,0,0,0,0}`, `{6,6,0,0,0,0}`, `{7,7,8,8,9,9}`, `{10,10,11,11,0,0}`, `{12,12,13,13,0,0}` (the first two words rewritten 4 = V-6 / 5 = V-8) |
| `5184`/`5198` | per bolt 1–9 (index ×2): context 0,1,1,5,5,5,5,5,5… / side 0,1,0,1,0,1… |
| `556E`, `552E`, `554C`, `5552` | top-speed points (§4.16) |
| `6988`–`6A00` | doubles 0.09 0.05 0.01 0.1 88 0.15 100 0.25 8 1 0.2 10000 0.8 0.3 0.7 130 |
| `691E`, `6926`, `692E`, `6936`, `693E`, `6946` | doubles 100, 0.9, 75, 1.15, 85, 91 (selling) |

### 5.7 Pictures (LIB2 index = id − 1000)

Garage #32; newspaper #21 + headlines #22–26; classified page from `0000:08b4`; calendar #219 /
#221 + marker #220; transmission screen #46; timing light #47; sticker sheet #200 + mask #194; gas
station 0x442; car bodies see §5.2; overlays §5.3; wheels §5.6.

### 5.8 Messages used (id → `DS:2C02+id`)

| id | text |
|---|---|
| 0xD30 | You've got no room for all these cars! / You're gonna hafta sell one. |
| 0xD76 | You've got too much junk in your garage! / You're gonna hafta sell a part. |
| 0xDC7 | You've got too much junk in your garage! / Must sell 12 parts to make enough room. |
| 0xE27 / 0xE3F | Sell some cars first! / Sell some parts first! |
| 0xE58 | No go, hot shot. You're $12345 short. |
| 0xE7F | Your car's not running, speedy! |
| 0xEA1 | Can't tune that mess, klutz! |
| 0xEC0 | You must be kidding! |
| 0xED7 / 0xEE9 | All parts gone! / All cars gone! |
| 0xEFA / 0xF16 | You've got no car, dummy! / Pick a car first! |
| 0xF2A, 0xF4E, 0xF72, 0xF97, 0xFB5, 0xFD7 | You've got no spare parts / tires / transmissions / engine / carburator / manifold |
| 0xFF7, 0x100A | How about $1234 ? / Will you take $1234 ? |
| 0x1021 | I'll take it off your hands for nothing. |
| 0x104B, 0x106E, 0x1078, 0x1084, 0x1091 | You must be crazy. I'll take it! / No way! / Get lost! / No thanks! / I'll take it! |
| 0x10A1, 0x10B5, 0x10CD, 0x10F6 | Paint ain't free! / Pay attention, dummy! / …That won't fit! / Get out of my face, dummy! |
| 0xD22 | It's yours! |
| 0x172E | You don't got enough gas to cruise down to Bob's. |
| 0x17FF | You're outta dough. |
| 0x1867, 0x1875 | Here it is ! / Where will you put it, speedy? |
| 0x18AD | You need a better graphics card to paint! |
| 0x18DD, 0x18F6 | It's not full, speedy! / Don't drink and drive, bub! |
| 0x1914, 0x1936, 0x1959, 0x1975 | Ya can't chop this roof, dummy! / You've already lost your bumper! / Customization ain't free! / Stickers ain't free! |
| 0x199F, 0x19B1, 0x19E1 | Gas ain't free! / You've got a full tank! / Yo, you need full service, dummy! |

### 5.9 Screens (hot-spot sets) used here

Records `{type, style, label, code, x0, y0, x1, y1, next}` at `DS:09F4 + 18·r`, screen `n` starts at
record `DS:2BAD[n]` (format: platform/video).

| screen | contents (code: label) |
|---|---|
| 4 | spare-part list, 10 rows; −7…−10 scroll; −3 Change (relabelled Sell it); −4 Forget it |
| 7 | newspaper: 2 Auto Parts, 1 Used Cars, 4 Go to the garage |
| 8 | classified page (`0000:09cb`): −1 previous page, −2 next page, −3 garage, ads = item numbers |
| 9 | buy: −25 See it, −20 Yeah, −21 Forget it; "You want to buy it for " |
| 10 | garage: records 0x74–0x85: 10 Pop the hood, 9 Change transmission, 11 Rear bumper, 12 Front bumper, 13 The roof (moved onto the car); 2 Hit the street (0,28)-(39,128), 1 newspaper (14,154)-(49,178), 17 drink (3,159)-(13,186), 6 Cars (69,33)-(269,47), 5 Sell parts (97,69)-(118,88), 8 Change tires (134,154)-(161,186), 4 Time to quit (156,52)-(184,74), 14 Stickers (214,52)-(234,61), 15 tank (284,159)-(312,188), 16 Car info (253,92)-(283,100), −40 Squelch (255,49)-(285,61), 7 paint (224,158)-(260,182), 3 Calendar (296,31)-(318,91) |
| 0xB | Your cars: 10 rows, scroll, −1 Switch it, −2 Sell it, −3 Forget it |
| 0xC | haggle: "I'll take $" edit, −24 Offer, −22 Never mind |
| 0xD | How about: −20 OK, −21 No |
| 0xE | paint: 1 Previous color, 2 Next color, 3 Go ahead, −23 Forget it |
| 0x10 | Will you take: −20 OK, −21 No thanks |
| 0x14 | transmission: bolts 1, 2 (records 200/201), −2 parts, −1 done |
| 0x15 | engine bay: bolts 1–9, 13 connector, −2 parts, −3 tune, −1 done |
| 0x17 | gas station (dynamic spots 1–6) |
| 0x18 | click anywhere (See it) |
| 0x1C | timing light: −7 Retard, −10 Advance, −1 done |
| 0x22 | customise: −20 OK, −22 Forget it |
| 0x23 | stickers −0x1F…−0x26, −22 Forget it |
| 0x26 | Car parts (Car info), 11 rows, −3 OK |

---

## 6. Hardware / DOS dependencies

| where | what | SDL3 port |
|---|---|---|
| `0f38:1f4b` (called by `set_paint_palette`, `paint_job`) | EGA **palette register** write: waits for vertical retrace (`in 3DAh` bit 3), then INT 10h AX=1000h (BL=reg, BH=value, value ≥ 8 gets `|0x10`); shadow at `DS:0440+reg` | video `pal_set_reg`: set palette entry `reg` to EGA colour `value` (6-bit rgbRGB); the paint colour lives in entries 6 and 7, the sample in 0x0C |
| `0000:7fb1` | PC speaker: `in/out 61h` (bits 0–1 on/off), PIT channel 2 divisor on port 42h (`0x7530 + |ign|·0x400`), only when `DS:47CE` (sound on) | host square-wave tone at `1193182 / div` Hz while the screen is open |
| everywhere | `ftol` (`1e16:2de9`) and emulated x87 arithmetic in doubles | C `double`, truncation `(long)x` |
| `3c8d`, `49ed` | `fild dword` of a 16-bit value with `dx = 0` = **unsigned** extension | cast to `u32` before `double` |

No file access here (HOT_DATA is loaded by `0f38:6b79`, platform; saves by game_flow).

## 7. Timing

* No frame loop: every screen waits in `ui_wait` (`0000:1417`), which pumps input and timers
  (`host_pump()` per iteration in the port). The timeouts passed (3000, 32000, 0) are tick limits of
  the poll; 1000 is returned on the deadline set in `DS:05CC/05CE` (gas station).
* `message()` waits `DS:05DC` ticks or a click; `calendar_show(0)` waits 500 ticks.
* Animations (`car_draw` modes 4/6, stickers 2 ticks per 8 px, tyre and part animations) are
  tick-paced by video / sound helpers.
* Game time is not real time: each action adds a fixed amount to `game_clock` (§4.15); only
  `ignition_tune` and the gas station depend on real ticks (`tick_clock`, `DS:05F8`).

## 8. Open questions

1. `DS:7EAE` (= 10 at a new game) has no reader in the index — maybe read through a pointer (save
   file?). Check with game_flow.
2. Car `+0x21` and the low byte of `+0x24` (cleared by `car_new`) are not used by the garage; race?
3. `bay_slot_pic[i] = (&cur_car->engine)[i]->type` in `garage_screen` dereferences null slots when
   parts are missing (reads DS:0004); harmless in DOS, must be guarded (same value) in the port.
4. `message(-0xE58)` / `message(-0xD22)`: negative ids — `msgbox_open` uses `abs(id)`; meaning of the
   sign (no wait? different box?) to be documented by video/platform.
5. `hotspot_find` returns the code itself when not found (would index a wrong record).
6. Gas station (`b8a1`/`b717`): the roles of the locals (`local_c`, `local_a`, `local_e`, `local_8`,
   `local_6`) are named by behaviour only; its hot-spot helpers `b425/b456/b49c` build dynamic spots.
   Race may want to own this screen since it is reached by driving.
7. `DS:57A7` points to a byte set to 0x1F/0x2E around the sell list (a header glyph?).
8. `car_draw` mode 2/4 at the gas station use `location == 3` to load extra pictures (0x449) —
   video/race.
9. The paint colour is an EGA palette effect (registers 6/7, sample in 0x0C; video §4.10
   `pal_set_reg`), so the port's VGA path must implement palette registers; platform notes that
   `ui_wait` also rotates registers 6/7/8 — check that this does not clash with the paint colour.
10. Helper for FP disassembly: patch `CD 34..3B xx` → `9B D8..DF xx`, `CD 3D` → `90 9B`,
    `CD 3C` (segment override) → `9B` + prefix, then capstone. Worth adding to `tools/x86dis.py`
    (not done: other files were not to be edited).
