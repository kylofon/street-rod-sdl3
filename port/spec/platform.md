# Platform layer: startup, timer/IRQ, keyboard, mouse, joystick, memory, files, picture libraries

Conventions: `port/RE_GUIDE.md`. Addresses `SSSS:OOOO` as stored in `SR.EXE` (Ghidra adds 1000h to the segment).
`DS:xxxx` = DGROUP `3E96` (image `0x3E960 + xxxx`). All game code is Microsoft C 5.1 medium model (far calls between
segments, near calls inside one, arguments pushed right to left, first argument at `[bp+6]` for far and `[bp+4]` for
near functions, result in `AX` or `DX:AX`). The ISRs (`0000:1fe2`, `238c`, `2c42`, `2df4`) are hand-written assembly.
Every table quoted here was dumped from `work/SR_unp.exe`; helper scripts stayed in the session scratchpad.

**Corrections to RE_GUIDE.md / FORMATS.md / PLAN.md found here**

* **There is palette code.** `0f38:1fa4` (`ega_set_palette`) loads all 16 EGA attribute-controller palette registers
  plus the overscan register through `int86x(10h)` AX=1002h (the tool missed it because `int86x` builds its
  `INT n` at run time). `main` calls it with the table **`DS:0440` = `00 00 08 07 0F 07 09 0B 03 02 04 0C 05 0A 06 0E`**
  on EGA/VGA and Tandy; values ≥ 8 are OR-ed with 10h (the 200-line "intensity" bit). So picture colour index →
  EGA colour is **not** the identity: 0→0 (black), 1→0 (black), 2→8 (dark grey), 3→7, 4→15, 5→7, 6→9, 7→11, 8→3,
  9→2, 10→4, 11→12, 12→5, 13→10, 14→6, 15→14. Driver vector 7 `0f38:0b2d` loads the all-zero table `DS:0452`
  (black screen, `DS:8249`=0), vector 8 `0f38:0b3f` reloads `DS:0440` (`DS:8249`=1). `0f38:1f4b(reg, colour)`
  changes one register after waiting for vertical retrace (3DAh bit 3); `0000:1417` uses it to rotate registers 6/7/8
  (`DS:0698`–`069A`) every 16 ticks while music plays. `tools/srlib.py` PNGs therefore show wrong colours. See video.
* **The timer ISR runs at 72.8 Hz and chains the BIOS every 4th tick** (18.2 Hz, `DS:0600`). There are **two** INT 8
  handlers: `0000:1fe2` (normal: tick counter, music sequencer, key/joystick cursor, demo) and `0000:238c`, which is
  installed by `0000:1aad` for **driving** and restored by `0000:1b27`. `238c` is not only sound: besides the engine
  sound it samples keyboard/joystick/mouse controls and runs a car-control step every 12th tick (6.07 Hz). See race
  and sound.
* **The key "queue" is one byte** (`DS:47D0`, last translated key, read-and-clear by `0000:2db6`). The keyboard ISR
  does not chain to the BIOS and has its own translation table `DS:4766` (scan code → key code), which the game patches
  per screen (`0000:12de`, table `DS:579F`) and for text entry (`0000:2c04`/`2c2b`).
* **Mouse = relative motion only.** INT 33h is reached through `int86` (`1e16:1b9a`), functions 0 (reset), 3 (buttons
  only; the position in CX/DX is ignored) and 0Bh (mickey counters). The game never sets or reads an absolute mouse
  position; the cursor `DS:472A/472C` moves by `mickeys >> 1` per poll (`>> 6` vertically while driving).
* **The joystick exists** (port 201h, `0000:2df4`), detected and calibrated once in `0000:1111`; it moves the cursor
  1 px per tick and steers while driving. Not used in demo mode.
* **Copy protection in SR.EXE is already patched out.** At `0000:3a1a` (in `0000:39c0`) the `lcall 0000:2fc8` of the
  original is replaced by `jmp 3a28` (`EB 0C`); SRSE.EXE keeps the call and patches `2fc8` itself at `0000:30b5`.
  See game_flow.
* **`Game/HOT_DATA` is 4116 bytes but only 4064 are read** (13 blocks, section 5.4); the last 52 bytes of the retail
  file repeat the end of the file. The data-disk `HOT_DATA` is exactly 4064 bytes.
* **CPU speed calibration.** `0000:05c6` counts loop iterations during one BIOS tick (`DS:8ACC`); `0000:0659` sets the
  "slow machine" flag `DS:82C6 = (DS:8ACC < 4)`. Race and video code reduce detail when `DS:8ACC < 4` / `DS:82C6 = 1`.
* The driver vector table is **`DS:78A2`** (15 far pointers). The EGA/VGA driver init `21a0:1128` copies it from
  `DS:5BD0`; `1c11:01a8` (CGA/Hercules) from `DS:5B04`, `21a0:0952` (Tandy) from `DS:5B8A`. The segment at `DS:68E2`
  (= `2e3e`, a data segment) holds three 48-byte **bitmap descriptors** (pages), not vectors.
* `2460:0002` is a far `memmem` (used to find "Tandy"/"TANDY" in the BIOS ROM), not a graphics routine.

## 1. Overview

The platform layer is the part of SR.EXE that talks to DOS, the BIOS and the PC hardware:

1. **Startup** (`0000:0000`–`066f`, `0000:e56a`–`e6c0`): adapter detection, the text-mode "Video Options" menu,
   driver selection (fills the driver vector table `DS:78A2`), BIOS mode set, CPU speed calibration, and the
   initialisation part of `main` (`0000:066f`).
2. **Interrupts and input** (`0000:0d58`–`2e93`): timer install (PIT channel 0 = 4000h), the two INT 8 handlers, the
   INT 9 keyboard handler with its translation tables, the joystick reader, the INT 24h critical-error handler, the
   UI event/wait loop `0000:1417` with its tick-timer queue, and exit/cleanup `0000:0fea`.
3. **Mouse** (`0f38:799a`–`7a47`): INT 33h through `int86`.
4. **Memory** (`0000:3614`–`3967`, `0f38:9ee9`–`a0b8`): far allocation with "Not enough memory" exit, bitmap
   descriptor allocation, the off-screen EGA memory pool, the 65000-byte work arena.
5. **Files and picture libraries** (`0f38:6016`–`6f0a`, `0000:39c0`): LIB1/LIB2 open with floppy-disk prompts, the
   in-memory picture directory in segment `389b`, picture load (to RAM or parked in off-screen EGA memory), decode
   into a bitmap, `HOT_DATA` and `hall_dat`.
6. **C runtime** `1e16` (identification only, section 2.9), and a Wichmann–Hill random generator `0f38:5eb6`.

```
_astart 1e16:001e → main 0000:066f
 ├─ cpu_speed_calibrate 0000:05c6 → DS:8ACC
 ├─ argv: "1" (single floppy), nomouse, demo, auto, nouemem
 ├─ video_detect_menu 0000:0226
 │    ├─ ega_vga_detect 0000:e62f (INT 10h AH=0Fh/12h, CRTC probe) ─ equip_is_mono 0000:e5c3 (INT 11h)
 │    ├─ herc_detect 0000:e584 (3BAh, e56a) ─ memmem 2460:0002 ("Tandy" in F000:0000)
 │    ├─ vga_detect 0000:018b (INT 10h AX=1A00h)
 │    └─ video_menu 0000:0082 (INT 10h mode 3, DOS print 0000:0000, kbhit/getch)
 ├─ mem_pools_init 0000:36be (get_ds 36a8, pools_reset 3614)
 ├─ arena_init 0f38:9ee9 (far_alloc 65000)
 ├─ driver_select 0000:0316 → 21a0:1128 (copies DS:5BD0 → DS:78A2)            [VGA: DS:0254=1, DS:8236=-2]
 ├─ video_mode_set 0000:04f4 → bios_set_mode 0000:01d1(0Dh); DS:0048=1
 ├─ gfx_init 0f38:1859(2e3e:0000, 2e3e:0030)                                   [video]
 ├─ platform_install 0000:1111
 │    ├─ joystick detect/calibrate (port 201h, joy_read 0000:2df4)
 │    ├─ mouse_reset 0f38:799a (INT 33h AX=0)  → DS:05F2
 │    ├─ _harderr(0000:105b)  ─ PIT ch0 = 4000h  ─ INT 8 → timer_isr 0000:1fe2
 │    ├─ kbd_install 0000:2dc3 (INT 9 → kbd_isr 0000:2c42)  ─ PIT ch2 mode 3
 │    └─ input_reset 0000:0e24
 ├─ ega_set_palette 0f38:1fa4(DS:0440)                                         [video]
 ├─ lib_open_all 0f38:6943 ─ slow_flag 0000:0659 ─ lib_read_dir 0f38:61ab(1)
 ├─ cursor_init 0f38:2b28 [video] ─ snd_init 0f38:7101 [sound] ─ 0f38:0dbd(0) [video]
 ├─ DS:7564 = far_alloc(18000) (packed-picture buffer)
 ├─ pic_load_list 0f38:68ca({3,1,4}) == 3 ?  (enough memory for the big pictures)
 │    yes: DS:70E2=1, lib_read_dir(2), hot_data_load 0f38:6b79 (+ hall_load 6d09),
 │         EGA: pic_park_list 0f38:6887(DS:4C28), pic_load_list(DS:4C28, 1)
 │    no:  DS:70E2=0, pic_free_list 0f38:6915({3,1,4})       (done later by lib_phase2 0000:39c0)
 └─ game_loop 0000:503f [game_flow] ─ platform_exit 0000:0fea

INT 8  → timer_isr 0000:1fe2: tick++ ─ every 4th: BIOS INT 8 ─ speaker-off guard ─ demo step 1bae [game_flow]
                               ─ music sequencer [sound] ─ key/joystick cursor (1 px/tick)
         race_isr 0000:238c (while driving): tick++ ─ every 4th: BIOS ─ engine sound [sound]
                               ─ every 12th: control sampling + car step [race]
INT 9  → kbd_isr 0000:2c42: DS:47D0 (key), DS:472E/472F (held directions), DS:4736/4738/473A (click)
INT 24h→ crit_err 0000:105b (message box, retry/abort)
UI loop: ui_wait 0000:1417 ─ key_read 2db6 ─ mouse_poll 0f38:7a47 ─ timer queue 0f0b/1843
         ─ hotspot test 0f38:4dad [video/UI] ─ cursor draw 0f38:2e8b [video] ─ status text 0f38:20a1 [video/UI]
pictures: pic_draw 0f38:683e ─ pic_info 67ff ─ bitmap_alloc_arena 0f38:9fce [video]
          ─ pic_decode 0f38:65ae ─ pic_load 62ba ─ lib_select 6089 ─ lib_seek 6016
          ─ unpack 0e92:0006 [video] ─ mask 2634:00c1 [video]
exit:  platform_exit 0000:0fea → timer_restore 0fb7 (PIT 0, INT 8, INT 9) ─ driver vector 8 (palette)
          ─ video_restore e610 (BIOS mode, equipment byte) ─ speaker off ─ exit(5)
```

## 2. Function table

### 2.1 Startup (`0000:0000`–`066f`, `0000:e56a`–`e6c0`)

| address | proposed name | signature | one-line purpose | confidence |
|---|---|---|---|---|
| 0000:0000 | dos_print_line | `near void (char *s)` | Copies `s` + `"\n\r$"` to the stack and prints it with INT 21h AH=09h (via `int86`) | verified |
| 0000:0082 | video_menu | `near int (int recommended)` | Text-mode "STREET ROD: Video Options" menu; returns 1–5, or 0 = keep the recommendation; Esc exits | verified |
| 0000:018b | vga_detect | `near int (void)` | INT 10h AX=1A00h: 1 if AL=1Ah and BL=08h (VGA with analog colour display) | verified |
| 0000:01d1 | bios_set_mode | `far void (int mode)` | INT 10h AH=00h, AL=mode (called near with `push cs`) | verified |
| 0000:01f2 | cga_set_palette | `far void (int pal)` | INT 10h AH=0Bh BH=1 BL=pal, skipped on Hercules (`DS:8236 = -4`) | verified |
| 0000:0226 | video_detect_menu | `near int (void)` | Adapter detection, recommendation, menu; returns the mode code 1–6; sets `DS:0254=1` for VGA | verified |
| 0000:0316 | driver_select | `near void (int mode)` | Installs the graphics driver (vector table `DS:78A2`) and, for CGA/Tandy/Hercules, the page descriptors | verified |
| 0000:04f4 | video_mode_set | `near void (int mode)` | Sets the BIOS graphics mode for the chosen driver; `DS:0048 = 1` | verified |
| 0000:05c6 | cpu_speed_calibrate | `near int (void)` | Counts polls of the BIOS tick `0040:006C` during one tick, /95/10, min 1 | verified |
| 0000:0659 | slow_flag_set | `near void (void)` | `DS:82C6 = (DS:8ACC < 4)` | verified |
| 0000:066f | main | `far void (int argc, char **argv)` | Switches, detection, init (this spec), then `game_loop 0000:503f` (game_flow), exit | verified |
| 0000:e56a | herc_mem_alias | `far bool(ZF) (ES=B000h)` | Increments `ES:3FFF` and compares with `ES:7FFF` (restores it): ZF=1 if the two addresses alias | verified |
| 0000:e584 | herc_detect | `far bool (void)` | 3BAh bit 7 must toggle within 256×64 reads, then 64 K memory test at B000h with 3BFh=1 | verified |
| 0000:e5c3 | equip_is_mono | `far int (void)` | INT 11h: returns 30h if the initial video mode bits are 80×25 mono, else 0 | verified |
| 0000:e5d2 | herc_graphics_on | `near void (void)` | Copies 30 bytes `DS:5AD0` → `0040:0049`, 3BFh=1, 3B8h=0, 10 CRTC words from `DS:5ABC`, 3B8h=`DS:5AEC` | verified |
| 0000:e610 | video_restore | `far void (void)` | Restores the equipment byte `0040:0010` from `DS:5AF0` and the BIOS mode `DS:5AEE` (INT 10h AH=0) | verified |
| 0000:e62f | ega_vga_detect | `near void (void)` | Saves the BIOS mode and equipment byte; INT 10h AH=12h BL=10h EGA info; CRTC 3D4h probe; sets `DS:0250/0252/5AF1` | verified |
| 2460:0002 | far_memmem | `far int (char *pat, void far *buf, int len)` | Searches `pat` in `buf[len]`; returns the remaining count (≠ 0 = found) | verified |

### 2.2 Timer, keyboard, joystick, exit (`0000:0d58`–`2e93`)

| address | proposed name | signature | one-line purpose | confidence |
|---|---|---|---|---|
| 0000:0d58 | hotspots_reset | `far void (void)` | `0f38:a054`, clears `DS:051A/051C/051E`, `0f38:4ba1(1)` (game_flow/UI) | likely |
| 0000:0d78 | click_clear | `far void (void)` | `DS:6C4A=1`, `DS:8B92=-1`, `DS:8BAA=7D00h`, `DS:4736=DS:4737=0`, discards `DS:47D0` | verified |
| 0000:0d99 | music_stop | `far void (void)` | Sound: stops the tune of channel `DS:58D2`, speaker off (see sound) | likely |
| 0000:0dd8 | music_resume | `far void (void)` | Sound: restarts the tune if enabled (see sound) | likely |
| 0000:0e24 | input_reset | `far void (void)` | Timer queue reset, keyboard drained (10 quiet ticks), mouse counters and click state cleared | verified |
| 0000:0f0b | tq_remove | `far void (int i)` | Unlinks entry `i` from the tick-timer queue (`DS:0572`, 5 × 18 bytes) and returns it to the free list | verified |
| 0000:0fb7 | timer_restore | `far void (void)` | PIT ch0 divisor 0 (18.2 Hz), old INT 8, old INT 9; `DS:05E0 = DS:0044 = 0` | verified |
| 0000:0fea | platform_exit | `far noreturn (void)` | Demo "Quit demo" box, restores timer/keyboard/video/palette, speaker off, `exit(5)` | verified |
| 0000:105b | crit_err_handler | `far void (u16 deverror, u16 errcode, void far *devhdr)` | INT 24h handler (`_harderr`): message box, then `_hardresume(1 retry / 2 abort)` | verified |
| 0000:1111 | platform_install | `far void (void)` | Joystick detection and calibration, mouse reset, `_harderr`, PIT 4000h, INT 8, INT 9, PIT ch2 mode, input reset | verified |
| 0000:12de | kbd_remap_screen | `far void (int screen)` | `DS:6C4C = screen`; patches the translation table with the per-screen hot keys `DS:579F[screen]` plus `DS:579C` ('M') | verified |
| 0000:1391 | kbd_remap_restore | `far int (int keep_low)` | Clears direction/click state; undoes `12de` (all entries, or only those whose original code > 1Fh) | verified |
| 0000:1417 | ui_wait | `far int (int ticks)` | UI event loop: keys, mouse, clicks, hotspot hover, tick timers; returns an event code or 0 after `ticks` | verified |
| 0000:1843 | tq_insert | `far int (int delay, int prio)` | Takes a free timer entry, deadline = now + delay, inserts it sorted; returns its index or −1 | verified |
| 0000:198a | tq_schedule_call | `far void (int arg, int delay)` | `i = tq_insert(delay, DS:8EF6)`; entry type 1 (call `0f38:1e41(arg)` when due) | verified |
| 0000:19bc | tq_cancel_call | `far void (int arg)` | Sets type 0 on every queued type-1 entry with this `arg` | verified |
| 0000:1a05 | tune_start | `far void (int tune, int repeat)` | Sound (see sound) | likely |
| 0000:1a7c | tune_rewind | `far void (void)` | Sound (see sound), no direct callers | likely |
| 0000:1aad | race_isr_enter | `far void (void)` | INT 8 → `0000:238c`; mouse y-shift 6; clears click state and steering; speaker on (engine) | verified |
| 0000:1b27 | race_isr_leave | `far void (void)` | INT 8 → `0000:1fe2`; speaker off; mouse shifts 1/1; clears click state | verified |
| 0000:1b91 | sign | `far int (int v)` | −1 / 0 / 1 | verified |
| 0000:1bae | demo_step | `far void (void)` | Called from the timer ISR in demo mode: scripted input (see game_flow) | likely |
| 0000:1fe2 | timer_isr | `interrupt` | Normal INT 8 handler (section 4.3) | verified |
| 0000:2374 | race_ctrl_reset | `far void (void)` | Clears `DS:05D6`, `DS:78E2/78E4`, `DS:78F4/78F6`, `DS:799E = DS:79A0` (see race) | verified |
| 0000:238c | race_isr | `interrupt` | INT 8 handler while driving (interface in 4.3, internals race/sound) | verified |
| 0000:2b9a | bios_mode_a_set | `far void (u8 equip, u8 mode)` | Stores `DS:47EA/47EB` (no callers) | verified |
| 0000:2bad | bios_mode_b_set | `far void (u8 equip, u8 mode)` | Stores `DS:47EC/47ED` (no callers) | verified |
| 0000:2bc0 | bios_mode_a_apply | `far void (void)` | Equipment bits 4–5 := `DS:47EA`, INT 10h AH=0 AL=`DS:47EB` (no callers, dead) | verified |
| 0000:2be2 | bios_mode_b_apply | `far void (void)` | Same with `DS:47EC/47ED` (no callers, dead) | verified |
| 0000:2c04 | kbd_text_mode_on | `far void (int keep_space)` | Backspace→08h, ','→2Ch, '.'→2Eh, Space→20h (unless `keep_space`), clears keyboard click bit | verified |
| 0000:2c2b | kbd_text_mode_off | `far void (void)` | Backspace, ',', '.' → 0; Space → 1Fh (click) | verified |
| 0000:2c42 | kbd_isr | `interrupt` | INT 9 handler (section 4.4) | verified |
| 0000:2db6 | key_read | `far int (void)` | `cli; al = DS:47D0; DS:47D0 = 0; sti`; returns AL zero-extended (callers use it as `char`) | verified |
| 0000:2dc3 | kbd_install | `far void (void)` | Saves INT 9 in `DS:47D4/47D6`, sets it to `0000:2c42` | verified |
| 0000:2de2 | kbd_restore | `far void (void)` | Restores INT 9 | verified |
| 0000:2df4 | joy_read | `far void (void)` | Port 201h: axis counts relative to the calibration centre → `DS:4718/471A`, buttons → `DS:471C`, click | verified |

### 2.3 Mouse (`0f38`)

| address | proposed name | signature | one-line purpose | confidence |
|---|---|---|---|---|
| 0f38:799a | mouse_reset | `far int (void)` | 0 if the INT 33h vector `0000:00CC` is null, else INT 33h AX=0 → AX (FFFFh = present) | verified |
| 0f38:79d7 | mouse_buttons | `far void (void)` | INT 33h AX=3: `DS:05EE = BX & 1`, `DS:05F0 = BX & 2` | verified |
| 0f38:7a14 | mouse_motion | `far void (int *dx, int *dy)` | INT 33h AX=0Bh: mickeys since the last call (CX, DX) | verified |
| 0f38:7a47 | mouse_poll | `far void (void)` | Accumulates mickeys, `DS:05E2/05E4` = scaled delta, left button → click event | verified |
| 0f38:0b51 | wait_click_or_key | `far void (int ticks)` | Waits until `ticks` elapse, a click, mouse/joystick button, Enter or Esc; 'M' toggles music | verified |
| 0f38:2d7e | cursor_ctl | `far void (int op)` | op ≥ 0: cursor shape `op`; −1/−2: show (++`DS:4733`); −3/−4: hide (−−`DS:4733`); draws via `2e8b` (video) | verified |

### 2.4 Memory (`0000:3614`–`3967`, `0f38:9ee9`–`a0b8`)

| address | proposed name | signature | one-line purpose | confidence |
|---|---|---|---|---|
| 0000:3614 | pools_reset | `near void (int full)` | Free lists: 16 × 28h bytes at `DS:83B0` (head `DS:7EB4`), 8Ch × 8 bytes at `DS:866A` (head `DS:7EBA`); game object pools (see game_flow) | verified |
| 0000:36a8 | get_ds | `far u16 (void)` | `segread()` → DS | verified |
| 0000:36be | mem_pools_init | `near void (void)` | `DS:64A6 = 800h`, `DS:6C60 = DS`, `pools_reset(1)` | verified |
| 0000:36d6 | far_free | `far void (void far *p)` | `_ffree(p)` | verified |
| 0000:36e9 | out_of_memory | `far noreturn (void)` | Message box "Not enough memory to run Street Rod !" (`DS:4BEE`), `platform_exit` | verified |
| 0000:3709 | far_alloc | `far void far *(u16 size, int fatal)` | `_fmalloc`; rejects blocks in DGROUP (and ≥ 8000h:0 on Tandy); on failure exits if `fatal`, else NULL | verified |
| 0000:376b | bitmap_alloc | `far Bitmap far *(int w, int h, int flags, int drv, Bitmap far *d)` | Allocates/initialises a 48-byte bitmap descriptor + planes (+ mask) | verified |
| 0000:3880 | bitmap_make | `far Bitmap far *(void far *data, void far *mask, int w, int h, int drv)` | Allocates a bare descriptor over existing data (no callers) | verified |
| 0000:3904 | bitmap_init | `far void (Bitmap far *d, void far *data, void far *mask, int w, int h, int drv)` | Fills an existing descriptor | verified |
| 0000:3967 | vram_pool_alloc | `far void far *(u16 size, u8 *plane)` | First-fit in 4 off-screen EGA pools (one per plane) from offset 7D00h; NULL, `*plane = FFh` if full | verified |
| 0f38:9ee9 | arena_init | `far void (void)` | `DS:6CD0` = `far_alloc(65000, fatal)` (Tandy: `8000:0000`), then `0f38:9ebc` | verified |
| 0f38:9f2c | arena_alloc | `far void far *(int size, int mode)` | Stack-style allocations inside the arena (4 modes, 4.5.9) | verified |
| 0f38:a0b8 | arena_pop | `far void (int n)` | Releases `n` top (mode 2) allocations; `n = 0` resets the top stack | verified |

### 2.5 Files and picture libraries (`0f38:6016`–`6f0a`, `0000:39c0`)

| address | proposed name | signature | one-line purpose | confidence |
|---|---|---|---|---|
| 0f38:6016 | lib_seek | `far int (int handle, long pos)` | `intdos` AX=4200h; 0 or the error code (`DS:6318` → `DS:630D`) | verified |
| 0f38:6057 | pic_index | `near int (int id)` | id ≥ 1000 → id − 991 (LIB2 after the 9 LIB1 pictures), else id − 1; CGA/Hercules remap via `DS:07C6[]` | verified |
| 0f38:6089 | lib_select | `near void (int id)` | Makes the library holding `id` the open one (only one handle open at a time), disk prompt, count check | verified |
| 0f38:61ab | lib_read_dir | `far int (int lib)` | Reads the 30-byte directory records of LIB1/LIB2 into segment `389b` (48-byte entries); returns the largest packed size | verified |
| 0f38:62ba | pic_load | `near int (int id, int to_vram, void far *dst)` | Loads a packed picture: 0 no memory, 1 already loaded, 2 loaded into RAM, 3 parked in EGA memory | verified |
| 0f38:655e | pic_free | `far void (int id)` | `_ffree` the packed copy, entry +20h = NULL | verified |
| 0f38:65ae | pic_decode | `far Bitmap far *(int id, Bitmap far *dst)` | Loads (into `DS:7564`) and unpacks a picture into a bitmap (EGA: from parked plane via read map select) | verified |
| 0f38:67ff | pic_info | `far void (int id, int *w, int *h, int *masked)` | Width, height, masked flag from the directory | verified |
| 0f38:683e | pic_get | `far Bitmap far *(int id, int arena_mode)` | `bitmap_alloc_arena 0f38:9fce(w, h, masked, mode)` then `pic_decode` | verified |
| 0f38:6887 | pic_park_list | `far int (int *ids)` | `pic_load(id, 1, NULL)` for a 0-terminated list (EGA memory), skipped with `nouemem` | verified |
| 0f38:68ca | pic_load_list | `far int (int *ids, int keep_going)` | `pic_load(id, 0, NULL)` for each; counts successes; stops at the first failure unless `keep_going` | verified |
| 0f38:6915 | pic_free_list | `far void (int *ids)` | `pic_free` for each | verified |
| 0f38:6943 | lib_open_all | `far void (int disks, int mode)` | Current drive, one/two floppy drives, opens LIB1(C) and LIB2(C), reads counts, checks 9+268 = 115h | verified |
| 0f38:6b79 | hot_data_load | `far void (void)` | Reads `?:hot_data` (13 blocks into DGROUP), then `hall_load` | verified |
| 0f38:6c3d | data_disk_check | `far int (void)` | Floppy: waits until `hot_data` can be opened (disk prompt), re-selects LIB2; returns the data drive letter | verified |
| 0f38:6ccf | hall_scramble | `near void (void)` | Sets bit 7 of the 230 bytes at `DS:82CA` | verified |
| 0f38:6cec | hall_unscramble | `near void (void)` | Clears bit 7 of the 230 bytes at `DS:82CA` | verified |
| 0f38:6d09 | hall_load | `far void (void)` | Reads `?:hall_dat` (count + n × 23 bytes) or sets `DS:7646 = 0` | verified |
| 0f38:6dd2 | hall_save | `far void (void)` | Writes `?:hall_dat` (scrambled) | verified |
| 0000:39c0 | lib_phase2 | `near int (void)` | After the title (game loop): LIB2 directory, HOT_DATA and pictures if `main` could not load them; returns −1 | verified |
| 0000:3a83 | wait_ticks_or_input | `far void (int ticks)` | `DS:05CC:05CE = now + ticks`, `DS:05D0 = 1`, `ui_wait(30000)` (returns on timeout, key or click) | verified |

### 2.6 Utilities in `0f38`

| address | proposed name | signature | one-line purpose | confidence |
|---|---|---|---|---|
| 0f38:5eb6 | rnd | `far int (int n)` | n ≤ 0: seed from `time()`, returns −1; else Wichmann–Hill combined generator, result in 0…n−1 | verified |
| 0f38:1fa4 | ega_set_palette | `far void (u8 *pal16)` | INT 10h AX=1002h with 16 registers (≥8 → \|10h) + overscan `DS:8248` (see video) | verified |
| 0f38:1f4b | ega_set_palreg | `far void (int reg, int colour)` | Stores `DS:0440[reg]`, waits for vertical retrace, INT 10h AX=1000h (see video) | verified |
| 0f38:0b2d | pal_black | `far void (void)` | Driver vector 7: `ega_set_palette(DS:0452)` (all 0), `DS:8249 = 0` (not indexed) | verified |
| 0f38:0b3f | pal_normal | `far void (void)` | Driver vector 8: `ega_set_palette(DS:0440)`, `DS:8249 = 1` (not indexed) | verified |

### 2.7 Belongs to other specs, listed for the call graph

| address | note |
|---|---|
| 0f38:1859 | `gfx_init(Bitmap far *screen, Bitmap far *back)`: `DS:8236` from `screen->drv`, `DS:8238/823A/823C/823E` (planes, pixel shift…), CRTC start 0; EGA: page descriptors 2 (`2e3e:0060`, A000:4000) and 3 (`2e68:0000`, A000:6000). See video |
| 0f38:2b28, 0f38:2e8b | cursor init (save-under at `A000:1F40` on EGA) and cursor draw. See video |
| 0f38:0dbd | page/split-screen setup (`0`: remember screen, `1`: restore + CRTC). See video |
| 0f38:23ce, 0f38:231a | message box, screen-rectangle restore. See video (UI) |
| 0f38:20a1, 0f38:4dad, 0f38:4a66/4ba1/4bff, 0f38:1e41 | status line text, hotspot hit test and list, timer callback. See video (UI) / game_flow |
| 0f38:7101, 0f38:70d5, 0f38:703e, 0f38:6f0a… | PIT ch2 / sound effects. See sound |
| 0f38:9fce, 0f38:9ebc, 0f38:a054 | arena bitmap allocation and arena reset. See video |
| 0e92:0006, 2634:00c1, 2462:* | unpacker, mask builder, Tandy blits. See video |
| 0000:1bae, 0000:2e93 | demo script step and demo input injection. See game_flow |
| 0000:e0e2, 0000:e218 | not platform: use HOT_DATA table `DS:7D86` and the FP emulator. See race/garage |
| 0000:503f | game loop. See game_flow |

### 2.8 Driver vector table `DS:78A2` (filled by `driver_select`)

15 far pointers. VGA/EGA source `DS:5BD0` (copied by `21a0:1128`):

| slot | DS | EGA/VGA target | CGA/Herc (`DS:5B04`) | Tandy (`DS:5B8A`) | known use |
|---|---|---|---|---|---|
| 0 | 78A2 | 21a0:1862 | 1c11:12b8 | 21a0:04ab | see video |
| 1 | 78A6 | 21a0:223b | 1c11:1631 | 21a0:07ac | see video |
| 2 | 78AA | 21a0:1cd5 | 1c11:1152 | 21a0:0358 | see video |
| 3 | 78AE | 21a0:113a | 1c11:01ba | 21a0:0964 | see video |
| 4 | 78B2 | 0e6c:0175 | 0e6c:00c1 | 0e6c:0002 | see video |
| 5 | 78B6 | 21a0:16dc | 1c11:0dd7 | 21a0:0c09 | see video |
| 6 | 78BA | 21a0:1508 | 1c11:06f3 | 21a0:0a4f | see video |
| 7 | 78BE | **0f38:0b2d** pal_black | 1c11:0f70 | 0f38:0b2d | screen off (called by `lib_phase2`) |
| 8 | 78C2 | **0f38:0b3f** pal_normal | 1c11:0f94 | 0f38:0b3f | screen on (`platform_exit`, `gfx_init`) |
| 9 | 78C6 | 21a0:25d9 | 1c11:1815 | 21a0:0d62 | see video |
| 10 | 78CA | 21a0:1722 | 1c11:0e30 | 21a0:0c26 | see video |
| 11 | 78CE | 21a0:28ec | 0f38:a87e | 0f38:a87e | see video |
| 12 | 78D2 | 21a0:23bc | 1c11:1c12 | 2487:000a | see video |
| 13 | 78D6 | 2645:0034 | 1c11:1ec2 | 2487:0257 | see video |
| 14 | 78DA | 2645:00e7 | 1c11:1fee | 2487:0309 | see video |

### 2.9 C runtime `1e16` used by the platform (identification only)

Besides the seed list (`port/spec/seed_symbols.csv`): `1b9a` int86, `1ce8` int86x, `1d7e` intdos (REGS in/out),
`1b6e` kbhit, `1b82` getch, `1dc8` movedata(srcseg, srcoff, dstseg, dstoff, n) (`rep movsb`), `1de6` segread,
`1dfc` time, `157e` _ffree, `1593` _fmalloc, `22de` remove (AH=41h), `22ec` _bios_equiplist (INT 11h),
`22f0` _dos_close, `2305` _dos_creat, `231e` _dos_getvect, `2330` _dos_open(path, mode, &handle),
`2348` _dos_read(handle, buf far, n, &nread), `234f` _dos_write, `236c` _dos_setvect, `2382` _dos_getdrive,
`2396` _harderr, `23e3` _hardresume, `23ec` _aFldiv, `256a` _aFaldiv (`*pl /= l`), `255e` _aFlshr, `2488` _aFlmul,
`2130` stricmp, `01a8` exit. All return 0 on success in the `_dos_*` convention.

## 3. Globals

| DS offset | proposed name | type/size | meaning | written by | read by |
|---|---|---|---|---|---|
| 0042 | herc_graphics | u16 | Hercules graphics page active | 04f4 | Hercules driver |
| 0044 | timer_installed | u16 | 1 after `platform_install`; `platform_exit` restores the timer | 1111, 0fb7 | 0fea |
| 0046 | herc_mode | u16 | Hercules mode set | 04f4 | 1abd |
| 0048 | gfx_mode_set | u16 | BIOS graphics mode was set; exit restores palette and text mode | 04f4 | 0fea |
| 0242 | menu_lines | u16[7] | `[0]` unused, `[1..6]` = pointers to the menu lines 1–5 and "Esc - Back to DOS" | data | 0082 |
| 0244 | (same, from index 1) | u16[6] | printed in order by `video_menu` | data | 0082 |
| 0250 | ega_present | u16 | 0 none, 1 EGA/VGA, 2 no colour CRTC at 3D4h | e62f, 0226 | 0226, 066f, 7b22 |
| 0252 | ega_lowres_sw | u16 | EGA switch setting 1 or 0Eh | e62f | 0226 |
| 0254 | vga_split | u16 | 1 = VGA chosen (menu 3): split screen uses doubled scan lines | 0226, 0316 | 21a0:0014, 7b22, c1d8 |
| 0256, 025C | "Tandy", "TANDY" | char[] | BIOS ROM signatures | data | 0226 |
| 0262 | mouse_allowed | u16 | 1, 0 with `nomouse` | 066f | 1111 |
| 0264 | "1" | char[2] | single-floppy switch | data | 066f |
| 0280 | no_ega_park | u16 | `nouemem`: do not park pictures in EGA memory | 066f | 6887 |
| 0440 | ega_palette | u8[16] | Current palette register values (colour index → EGA colour, before the \|10h) | data, 1f4b | 1fa4 |
| 0452 | black_palette | u8[16] | All 0 | data | 0b2d |
| 0566 | tq_head | i16 | Timer queue: entry with the earliest deadline (−1 empty) | 0e24, 0f0b, 1843 | 1417 |
| 0568 | tq_tail | i16 | Timer queue: latest entry | 0e24, 0f0b, 1843 | 1843 |
| 056A | tq_free | i16 | Free-list head | 0e24, 0f0b, 1843 | 1843 |
| 056C | tq_next_due | u32 | Deadline of `tq_head` (7FFFFFFFh = none) | 0e24, 0f0b, 1843 | 1417 |
| 0570/0571 | tq_busy | u8 ×2 | 0571 = 1 while `tq_insert` runs | 0e24, 1843 | – |
| 0572 | tq_entries | 5 × 18 bytes | `{i16 next(earlier), i16 prev(later), u32 deadline, i16 prio, i16 type, i16 arg, 4 bytes}`; entry 4 at `05BA` is the sentinel (next = −1) | 0e24, 0f0b, 1843, 198a, 19bc | 1417 |
| 05CC | wait_deadline | u32 | `ui_wait` returns 1000 when the tick passes it (0 = off) | 3a83, game code | 1417 |
| 05D0 | wait_any_key | u16 | `ui_wait` returns 3EBh on any key or click | 3a83, 5910 | 1417 |
| 05D2 | wait_key | u16 | Key that ended the wait (0 = click) | 1417 | 5910, callers |
| 05DE | click_returns | u16 | `ui_wait` returns 3EBh on a click | 0f38:28a4… | 1417 |
| 05E0 | timer_on | u16 | Same as 0044 | 1111, 0fb7 | 23ce |
| 05E2/05E4 | mouse_dx/dy | i16 | Scaled mouse delta of the last `mouse_poll` | 7a47 | 1417, 238c |
| 05E6/05E8 | mouse_acc_x/y | i16 | Mickey accumulators | 7a47, 0e24, 1111, 1aad, 1b27 | 7a47 |
| 05EA/05EC | mouse_shift_x/y | u16 | 1/1 (UI), 1/6 (driving) | 1111, 1aad, 1b27 | 7a47 |
| 05EE | mouse_left | u16 | Left button held | 79d7 | 7a47, 0b51, 8d26, 8ea8, ab8b |
| 05F0 | mouse_right | u16 | Right button held (value 2) | 79d7 | 238c |
| 05F2 | mouse_present | u8 | Driver found and not `nomouse` | 1111 | 0e24, 1417, 1aad, 1b27, 0b51, 238c |
| 05F8 | ticks | u32 | 72.8 Hz tick counter | 1fe2, 238c | everywhere |
| 0600 | bios_ticks | u32 | 18.2 Hz counter (incremented when the BIOS is chained) | 1fe2, 238c | save/load, 7fb1 |
| 0608 | chain_count | u8 | Countdown 3…0 to the next BIOS chain | 1fe2, 238c | – |
| 0609 | race_step_count | u8 | Countdown to the next race control step (every 3rd chain) | 238c, 1aad | – |
| 060C/060E | crit_msg_ptrs | u16 ×2 | "cannot read drive ?", "disk ? is write protected" | data | 105b |
| 0610 | demo_active | u16 | ≠ 0 while the demo runs; 63h = Esc pressed in the demo | 39c0, 2c42, 0fea… | many |
| 0612 | auto_drive | u16 | `auto` switch | 066f | 238c, 2645 |
| 0614 | "Quit demo" | char[] | | data | 0fea |
| 0698–069A | pal_cycle | u8[3] | Colours rotated through palette registers 6, 7, 8 | 1417 | 1417 |
| 06A4 | demo_quit_done | u16 | Guard so the Esc-in-demo exit runs once | 1417 | 1417 |
| 06A6 | demo_fast | u8 | Demo step every 16 (else 128) ticks | 1bae | 1fe2 |
| 06AE/06B0 | demo_cursor_target | i16 ×2 | Cursor glides there 1 px per tick in the demo | 1bae | 1fe2 |
| 06E0 | isr_cursor_busy | u8 | Re-entrancy guard of the cursor part of `timer_isr` | 1fe2 | 1fe2 |
| 06E8 | isr_race_busy | u8 | Re-entrancy guard of the race step | 238c | 238c |
| 4718/471A | joy_x/joy_y | i16 | Axis count − centre | 2df4 | 1fe2, 238c, 1111 |
| 471C | joy_buttons | u8 | Bit 0 = button 1, bit 1 = button 2 (pressed) | 2df4 | 0b51, 8d26, 8ea8, ab8b |
| 471D | joy_present | u8 | Joystick detected (not in demo) | 1111 | 1fe2, 238c, 0b51, 8ea8 |
| 471E/4720 | joy_centre_x/y | i16 | Mean of 3 reads at start | 1111 | 2df4 |
| 4722/4724 | joy_thr_x | i16 ×2 | −centre_x/2, +centre_x/2 | 1111 | 1fe2, 238c |
| 4726/4728 | joy_thr_y | i16 ×2 | −centre_y/2, +centre_y/2 | 1111 | 1fe2, 238c |
| 472A/472C | cursor_x/y | i16 | Pointer position (320×200) | 1417, 1fe2, 238c, 1bae, … | UI, race |
| 472E/472F | kbd_dir_x/y | i8 | Sum of the held direction keys (−1…+1 per key) | 2c42, 0e24, 1391 | 1fe2, 238c |
| 4730/4731 | kbd_last_dir | i8 ×2 | Last pressed direction | 2c42, 0e24, 1391 | 1aad, 1b27, cdaf |
| 4732 | click_held | u8 | Bit 0 keyboard (Space…), bit 1 joystick, bit 2 mouse | 2c42, 2df4, 7a47, 2c04 | ISRs, UI |
| 4733 | cursor_shown | i8 | Show counter (> 0 visible) | 2d7e, 6b06 | 1417, 1fe2 |
| 4736 | click_code | u8 | Click event: 1Fh key, 1/2/3 joystick buttons, 1 mouse (0 = none) | 2c42, 2df4, 7a47 | 1417, 0b51, … |
| 4737 | click2_code | u8 | Second click event (code 1Eh, unused by the default table) | 2c42 | 1417 |
| 4738/473A | click_x/y | i16 | Cursor position at the click | 2c42, 2df4, 7a47 | 1417 |
| 473C/473E | click2_x/y | i16 | Same for `click2` | 2c42 | 2e93 |
| 4740 | kbd_dir_lock | i16 | FFFFh while the keyboard ISR updates 472E/472F | 2c42 | 1fe2, 238c |
| 4742/4744 | cursor_hot | i16 ×2 | Hot spot of the current cursor shape | 2d7e | video |
| 474A/4750 | step_x[3], step_y[3] | i16 | {−1,0,1}; indexed with −1/0/1 from `474C`/`4752` | data | 1fe2 |
| 4756–4765 | dir_deltas | (i8,i8)[8] | (dx,dy) of key codes −16…−2 at `4766 + code` (section 5.2) | data | 2c42 |
| 4766 | kbd_xlat | u8[57h] | Scan code → key code (section 5.2), patched at run time | data, 12de, 1391, 2c04, 2c2b | 2c42 |
| 47CA–47E7 | dir_held | u8[] (indexed −30…−1 from 47E8) | Direction key held flags | 2c42 | 2c42 |
| 47CC | kbd_remap_active | u8 | | 12de, 1bae, 2e93 | – |
| 47CD | click_edge | u8 | Set on Space make (race "any key") | 2c42 | 238c, c613, cdaf |
| 47CE | sound_on | u8 | Initially 1; toggled by Ctrl | 2c42 | ISRs, sound |
| 47CF | speaker_on | u8 | Speaker gate currently open | sound, ISRs | ISRs, 1417 |
| 47D0 | key | u8 | Last translated key (single slot) | 2c42, 2e93 | 2db6 |
| 47D4/47D6 | old_int9 | far ptr (seg, off) | | 2dc3 | 2de2 |
| 47D8 | (cleared) | u16[8] | Cleared with the direction state | 0e24, 1391 | ? |
| 47E8 | joy_saved_sp | u16 | SP for the timeout exit of `joy_read` | 2df4 | 2df4 |
| 47EE | click_seen | u16 | Set on Space make | 2c42 | 8d26, 8ea8 |
| 4BC8 | pal_cycle_on | u16 | Enables the palette rotation in `ui_wait` | game_flow | 1417 |
| 4BEE | msg_no_memory | char[] | "Not enough memory to run Street Rod !" | data | 36e9 |
| 4C14 | vram_pool_base | far ptr | A000:0000 | data | 3967 |
| 4C18 | vram_pool_used | u16[4] | Bytes used in each plane pool (from 7D00h) | 3967 | 3967 |
| 4C20 | big_pics | u16[] | {3, 1, 4, 0}: LIB1 pictures that must fit in RAM | data | 066f |
| 4C28 | resident_pics | u16[], 0-terminated | ~190 LIB2 ids to preload/park | data | 066f, 39c0 |
| 4E0A | "hot_data" | char[] | | data | 6b79 (via `DS:4EA2`) |
| 4ED8/4EDA | lib_handle[2] | u16 | DOS handles of LIB1/LIB2 (0 = closed) | 6943, 6089 | 6089, 61ab, 62ba |
| 4EDC–4EE2 | lib_names | char *[4] | "?:lib1", "?:lib1c", "?:lib2", "?:lib2c" | data | 6943 |
| 4EE4–4EEC | lib_msgs | char *[5] | "insert disk ? into drive ?", "copy all files to current directory", "bad file format", "write error", "file not found: ????" | data | 6089, 62ba, 6943, 6c3d, 6dd2 |
| 4EA2 | hot_data_name | char * | → 4E0A | data | 6b79 |
| 4EA4/4EBE | hot_blocks | u16[13] ×2 | HOT_DATA destinations and sizes (5.4) | data | 6b79 |
| 4EEE/4EFE | RUN_COUNT/RUN_VALUE | u8[16] ×2 | Unpacker tables (FORMATS.md) | data | video |
| 4F0E | decode_alloc | u16 | 1: `pic_decode` (re)initialises `dst` with `bitmap_alloc` | data, 872e | 65ae |
| 4F10, 4F25 | msgs | char[] | "Can't open data file", "Data disc read fail" | data | 6b79 |
| 4F3A | hall_name | char[] | "?:hall_dat" | 6d09 | 6d09, 6dd2 |
| 5AEE | saved_bios_mode | u8 | Mode at start | e62f | e610 |
| 5AEF/5AF1/5AF2 | ega_info | u8 ×3 | INT 10h AH=12h BH (mono), BL (memory 0–3 = 64–256 K), CL (switches) | e62f | 0226 |
| 5AF0 | saved_equip | u8 | `0040:0010` at start | e62f | e610 |
| 5BD0, 5B04, 5B8A | drv_tables | far ptr[15] ×3 | Driver vector sources (2.8) | data | 21a0:1128, 1c11:01a8, 21a0:0952 |
| 6318/630D | dos_err | u16 | C runtime error code | runtime | 6016 |
| 64A6 | – | u16 | 800h (set once) | 36be | ? |
| 68E2 | pages_seg | u16 | Segment `2e3e`: page descriptors | data | 0316 |
| 691C | picdir_seg | u16 | `389b` | data | 655e, 67ff |
| 6C46/6C48 | cursor_drawn_x/y | i16 | Position of the last cursor draw | 1111, 1417 | 1417 |
| 6C4A | cursor_moved | u8 | Cursor must be redrawn | ISRs, 1417, 0d78 | 1417 |
| 6C4C | screen_id | u16 | Current screen (for hot keys and status text) | 12de | 1417, 1bae |
| 6C4E | last_click_code | u8 | | 1417 | callers |
| 6C60 | dgroup_seg | u16 | DS | 36be | 3709 |
| 6C62/6C64/6C66 | rnd_s1/s2/s3 | i16 | Wichmann–Hill state | 5eb6 | 5eb6 |
| 6C68 | cur_drive | u16 | 1 = A:, 2 = B:, 3 = C: … (floppy if < 3) | 6943 | 62ba, 6c3d, 6dd2 |
| 6C6A | cur_lib_count | u16 * | → 8BC8 or 8DD2 | 6089 | 62ba |
| 6C6C | cur_lib_handle | u16 * | → 4ED8 or 4EDA | 6089 | 61ab, 62ba |
| 6C6E | cur_lib_name | char * | | 6089 | 62ba |
| 6C70 | other_lib_handle | u16 * | | 6089 | 6089 |
| 6C72 | hot_data_path | char[] | "X:" + "hot_data" | 6b79 | 6c3d |
| 6CD0 | arena | far ptr | 65000-byte work arena | 9ee9 | 9f2c |
| 70E2 | libs_preloaded | u16 | `main` loaded LIB2 dir/HOT_DATA/pictures | 066f | 39c0 |
| 7564 | pack_buf | far ptr | 18000-byte buffer for packed pictures (16000 CGA) | 066f | 65ae |
| 7646 | hall_count | u16 | Number of hall-of-fame records | 6d09 | 6dd2, 32f4, 339f, 1859 |
| 78A2 | drv_vec | far ptr[15] | Driver vector table (2.8) | 21a0:1128 | everywhere |
| 7AAA/7AAC | old_int8 | far ptr | | 1111 | ISRs, 0fb7 |
| 82C6 | slow_machine | u16 | 1 if `cpu_speed < 4` | 0659 | race, 2645 |
| 82CA | hall_records | 10 × 23 bytes | Hall of fame (230 bytes) | 6d09 | game_flow |
| 8236 | driver_id | i16 | −2 EGA/VGA, −3 CGA, −4 Hercules, −6 Tandy | 0316, 1859 | everywhere |
| 8238/823A | planes, pixel_shift | u16 | 4/3 on EGA | 1859 | 376b, video |
| 8ACC | cpu_speed | u16 | Calibration result (≥ 1) | 066f | 0659, race, 2645 |
| 8BC8 | lib1_count | u16 | 9 | 6943, 6089 | 6057, 6089, 61ab |
| 8BCE | demo_switch | u16 | `demo` | 066f | 39c0, … |
| 8DD2 | lib2_count | u16 | 268 | 6943, 6089 | 61ab |
| 8DD4/8DD6 | lib1_name/lib2_name | char * | Chosen library names (drive letter patched) | 6943 | 6089, 6b79, 6c3d, 6d09 |
| 8EF6 | tq_default_prio | u16 | 10 | 1111 | 198a |
| 8EF8/8EFA/8EFC | – | u16 | 2, 5, 5 (set by `platform_install`, meaning unknown) | 1111 | ? |

Segment `389b` (48 bytes × 277 entries, 13296 bytes): picture directory (section 5.3). Segment `2e3e`: page
descriptors (5.5).

## 4. Pseudocode

Types: `u8/i8/u16/i16/u32/i32`. `far_ptr` = {off, seg}. `ticks()` = `DS:05F8` as `u32`. Comparisons against
`DS:05F8` are 32-bit signed on the high word and unsigned on the low word, exactly as written below (`deadline > now`
means `(i16)dh > (i16)nh || (dh == nh && dl > nl)`).

### 4.1 Startup

```c
/* 0000:0000 */ static void dos_print_line(const char *s)
{
    char buf[0x54];
    strcpy(buf, s); strcat(buf, "\n\r$");
    int86(0x21, AH=0x09, DX=buf);
}

/* 0000:018b */ static int vga_detect(void)
{
    regs = int86(0x10, AX=0x1A00);
    return (regs.al != 0 && regs.bl == 8);     /* AL is 1Ah on VGA; only "nonzero" is tested */
}

/* 0000:e62f */ static void ega_vga_detect(void)
{
    DS_5AEE = int10(AH=0x0F).al;                /* current BIOS mode, restored at exit */
    DS_5AF0 = *(u8 far *)0x00400010;            /* equipment byte */
    r = int10(AH=0x12, BL=0x10);                /* EGA information */
    DS_5AEF = r.bh; DS_5AF2 = r.cl;
    if (r.bl != 0x10) {                         /* EGA BIOS answered */
        DS_0250 = 1; DS_5AF1 = r.bl;            /* 0..3 = 64..256 K */
        if (r.cl >= 0x0F || r.bh > 1 || r.bl > 3) goto crtc_probe;
        if (r.cl == 1 || r.cl == 0x0E) { DS_0252 = 1; return; }
        if (r.cl <= 8) goto crtc_probe;
        DS_5AF1 = r.bl; DS_0250 = 1; return;
    }
crtc_probe:                                     /* colour CRTC at 3D4h? register 0Fh (cursor low) */
    out(0x3D4, 0x0F); old = in(0x3D5); out(0x3D5, 0x5A);
    v = in(0x3D5); out(0x3D5, old);
    if (v != 0x5A) DS_0250 = 2;                 /* keeps 1 or 0 otherwise */
}

/* 0000:e5c3 */ static int equip_is_mono(void) { u16 e = int11() & 0x30; return e == 0x30 ? 0x30 : 0; }

/* 0000:e584 */ static bool herc_detect(void)
{
    for (bx = 0x100; bx; bx--) {                /* wait for 3BAh bit 7 (vsync) to change */
        u8 first = in(0x3BA) & 0x80;
        for (cx = 0x40; cx; cx--) if ((in(0x3BA) & 0x80) != first) goto toggles;
    }
    return 0;
toggles:
    ES = 0xB000;
    if (!herc_mem_alias()) return 1;            /* B000:3FFF and 7FFF differ: 64 K present */
    out(0x3BF, 1);                              /* enable graphics page */
    return !herc_mem_alias();
}

/* 0000:0226 */ static int video_detect_menu(void)
{
    int herc = 0, rec;
    ega_vga_detect();
    if (DS_0252) DS_0250 = 1;
    if (equip_is_mono()) {
        if (DS_0250 == 1 || herc_detect()) { DS_0250 = 0; herc = 1; }
    }
    rec = DS_0250 ? (DS_5AF1 == 3 ? 2 : 6) : 1;
    if (herc) rec = 4;
    if (far_memmem("Tandy", MK_FP(0xF000,0), 0xFFFF) || far_memmem("TANDY", MK_FP(0xF000,0), 0xFFFF)) rec = 5;
    if (vga_detect()) rec = 3;
    int choice = video_menu(rec);
    if (choice) rec = choice;
    if (rec == 1 && DS_0250) return 6;          /* CGA mode on an EGA/VGA card */
    if (rec == 3) { DS_0254 = 1; return 2; }    /* VGA = EGA driver + VGA split screen */
    return rec;                                 /* 1 CGA, 2 EGA, 4 Hercules, 5 Tandy, 6 (EGA 64 K → CGA) */
}

/* 0000:0082 */ static int video_menu(int rec)
{
    if (rec < 1 || rec > 5) rec = 1;            /* 6 is shown as 1 */
    int86(0x10, AX=0x0003);                     /* text mode 80x25 */
    char line[0x20]; memcpy(line, "\n           Recommended mode: #", 0x20);
    strcat(line, menu_lines[rec] + 0x14);       /* DS:0242[rec] without its 20 leading spaces */
    dos_print_line(title_DS_019B); dos_print_line(DS_01D1); dos_print_line(DS_0208);
    for (i = 0; i < 6; i++) dos_print_line(DS_0244[i]);   /* lines 1..5 and "Esc - Back to DOS" */
    dos_print_line(line);
    while (kbhit()) getch();                    /* flush */
    while (!kbhit()) ;                          /* busy wait */
    int k = getch() - '0';
    if (k == 0x1B - '0') platform_exit();       /* Esc */
    return (k > 0 && k <= 5) ? k : 0;
}

/* 0000:0316 */ static void driver_select(int mode)
{
    Bitmap far *pg = MK_FP(DS_68E2, 0);         /* segment 2e3e: page descriptors 0,1,2 at +0,+30h,+60h */
    switch (mode) {
    case 3: DS_0254 = 1;                        /* falls into 2 (not reached: 0226 returns 2) */
    case 2: DS_8236 = -2;
            *(u8 far *)0x00400410 = (*(u8 far *)0x00400410 & 0xCF) | 0x20;   /* equipment: 80x25 colour */
            drv_ega_init();                     /* 21a0:1128: memcpy(DS:78A2, DS:5BD0, 60) */
            return;
    case 5: pg[0].drv = 0xFA; DS_8236 = -6; drv_tandy_init();  /* 21a0:0952 */
            /* page 0 at B800:0000 (B000 if Hercules), 32 K pages, rows A0h, alloc 32 K back page … */
            ...; return;
    case 4: pg[0].drv = 0xFC; DS_8236 = -4; goto cga_herc;
    case 1: pg[0].drv = 0xFD; goto cga_herc;
    case 6: pg[0].drv = 0xFD; equipment = (equipment & 0xCF) | 0x20; goto cga_herc;
    default: cga_herc:
            drv_cgaherc_init();                 /* 1c11:01a8: memcpy(DS:78A2, DS:5B04, 60) */
            /* page 0 at B000/B800:0000, 16 K pages, rows 50h, two far_alloc(4000h) pages … */
            return;
    }
}

/* 0000:04f4 */ static void video_mode_set(int mode)
{
    switch (mode) {
    case 2: case 3: bios_set_mode(0x0D); break;            /* 320x200x16 planar */
    case 5: bios_set_mode(0x09); out(0x3DF, 0xF6); break;  /* Tandy */
    case 4: herc_graphics_on(); clear B000:0000..7FFF; out(0x3B8, 0x2A); DS_0046 = DS_0042 = 1; break;
    case 1: case 6: bios_set_mode(0x04); cga_set_palette(1); break;
    }
    DS_0048 = 1;
}

/* 0000:05c6 */ static int cpu_speed_calibrate(void)
{
    volatile u32 far *bios_tick = MK_FP(0, 0x046C);
    i32 n = 0; u32 t = *bios_tick;
    while (*bios_tick == t) ;                   /* align to a tick edge */
    t = *bios_tick;
    while (*bios_tick == t) n++;                /* one 55 ms tick */
    n /= 0x5F; n /= 10;                         /* _aFaldiv twice */
    return (n > 1) ? (int)n : 1;                /* signed 32-bit compare */
}

/* 0000:0659 */ static void slow_flag_set(void) { DS_82C6 = (i16)DS_8ACC < 4 ? 1 : 0; }

/* 0000:066f (initialisation part) */
void main(int argc, char **argv)
{
    int disks = 2, mode;
    DS_8247 = 0;
    DS_8ACC = cpu_speed_calibrate();
    DS_0262 = 1; DS_8BCE = 0; DS_0612 = 0;
    for (i = 1; i < argc; i++) {
        if (memcmp(argv[i], "1", 2) == 0) disks = 1;          /* exact, case-sensitive */
        else if (!stricmp(argv[i], "nomouse")) DS_0262 = 0;
        else if (!stricmp(argv[i], "demo"))    DS_8BCE = 1;
        else if (!stricmp(argv[i], "auto"))    DS_0612 = 1;
        else if (!stricmp(argv[i], "nouemem")) DS_0280 = 1;
    }
    mode = video_detect_menu();
    mem_pools_init();
    arena_init();
    driver_select(mode);
    video_mode_set(mode);
    gfx_init(MK_FP(0x2e3e, 0), MK_FP(0x2e3e, 0x30));           /* video */
    platform_install();
    if (DS_8236 == -2 || DS_8236 == -6) ega_set_palette(DS_0440);
    lib_open_all(disks, mode);
    slow_flag_set();
    lib_read_dir(1);
    cursor_init();                                            /* 0f38:2b28, video */
    snd_init();                                               /* 0f38:7101, sound */
    page_setup(0);                                            /* 0f38:0dbd, video */
    if (DS_8236 == -3 && DS_0250) DS_755C = bitmap_alloc(320, 100, 0, -1, NULL);
    DS_7564 = far_alloc((DS_8236 == -2 || DS_8236 == -6) ? 0x4650 : 0x3E80, 1);
    if (pic_load_list(DS_4C20 /* {3,1,4} */, 0) == 3) {
        DS_70E2 = 1;
        lib_read_dir(2);
        hot_data_load();
        if (DS_8236 == -2) pic_park_list(DS_4C28);
        pic_load_list(DS_4C28, 1);
    } else {
        DS_70E2 = 0;
        pic_free_list(DS_4C20);
    }
    game_loop();                                              /* 0000:503f, game_flow */
    platform_exit();
}
```

### 4.2 Timer install, exit, critical errors

```c
/* 0000:1111 */ void platform_install(void)
{
    i16 sx = 0, sy = 0;
    DS_8EF6 = 10; DS_8EF8 = 2; DS_8EFA = 5; DS_8EFC = 5;
    /* joystick detection: trigger the one-shots, wait until both A axes and all buttons read 0/released */
    out(0x201, 0); u8 first = in(0x201);
    u16 n = 1; u8 v;
    for (;;) {
        v = in(0x201);
        if ((v & 0xF3) == 0xF0) break;          /* A axes timed out, buttons up */
        if (++n == 0) break;                    /* 65535 polls: nothing there (port reads FFh) */
    }
    if (DS_0610 == 0 && n != 0 && v != first) {
        DS_471D = 1;                            /* joystick present: calibrate (centre starts at 0) */
        joy_read(); sx  = DS_4718; sy  = DS_471A;
        joy_read(); sx += DS_4718; sy += DS_471A;
        joy_read(); sx += DS_4718; sy += DS_471A;
        DS_471E = sx / 3; DS_4720 = sy / 3;     /* signed idiv */
        DS_4722 = -(DS_471E >> 1); DS_4724 = DS_471E >> 1;   /* sar */
        DS_4726 = -(DS_4720 >> 1); DS_4728 = DS_4720 >> 1;
    }
    if (DS_0610 == 0 && DS_0262 && mouse_reset() == -1) {
        DS_05F2 = 1; DS_05E6 = DS_05E8 = 0; DS_05EA = 1; DS_05EC = 1;
    } else DS_05F2 = 0;
    DS_6C46 = DS_6C48 = -1;
    /* sound channel defaults (see sound): */
    DS_79B0 = 0; DS_79B2 = 0; DS_79B4 = 0x2e6b; DS_79B6/79B8 = DS_79BA/79BC = 2e6b:0000;
    DS_79BE = DS_79C0 = 0x5858; DS_79C2 = DS_79C4 = 0x5882; DS_79CC = 0; DS_79DE = DS_79E0 = 0x58CC;
    _harderr(crit_err_handler);
    cli();
    out(0x43, 0x36); out(0x40, 0x00); out(0x40, 0x40);        /* PIT ch0 mode 3, divisor 4000h */
    DS_7AAA = _dos_getvect(8);
    _dos_setvect(8, timer_isr);
    sti();
    kbd_install();
    DS_05E0 = 1;
    out(0x43, 0xB6);                                          /* PIT ch2 lo/hi mode 3 (speaker) */
    input_reset();
    DS_0044 = 1;
}

/* 0000:0fb7 */ void timer_restore(void)
{
    cli(); out(0x43, 0x36); out(0x40, 0); out(0x40, 0);       /* 18.2 Hz */
    _dos_setvect(8, DS_7AAA); sti();
    kbd_restore();
    DS_05E0 = 0; DS_0044 = 0;
}

/* 0000:0fea */ void platform_exit(void)
{
    if (DS_0610) {
        DS_0610 = (DS_0610 == 0x62) ? 0 : 1;
        message_box("Quit demo");                             /* 0f38:23ce */
        rect_restore(0, 0, 320, 200, 0);                      /* 0f38:231a */
    }
    if (DS_0044) timer_restore();
    if (DS_0048) { drv_vec[8](); video_restore(); }           /* pal_normal, BIOS mode + equipment */
    out(0x61, in(0x61) & 0xFC); DS_47CF = 0;                  /* speaker off */
    exit(5);                                                  /* always exit code 5 */
}

/* 0000:105b */ void far crit_err_handler(u16 deverror, u16 errcode, void far *devhdr)
{
    int disk = (deverror & 0x8000) == 0;
    int action = (disk || (deverror & 0x1000)) ? 1 /*retry*/ : 2 /*abort*/;
    switch ((u8)errcode) {
    case 0:  DS_060E_str[5] = 'A' + (u8)deverror;             /* "disk ? is write protected" */
             message_box(DS_060E_str); break;
    case 2:  s = DS_060C_str; s[strlen(s) - 1] = 'A' + (u8)deverror;   /* "cannot read drive ?" */
             message_box(s); break;
    case 9:  message_box("printer out of paper"); break;
    default: if (disk) message_box("disk error");
             else { message_box("hardware error"); action = 2; }
    }
    if (action == 2) platform_exit();
    _hardresume(action);
}
```

### 4.3 Timer ISRs

```c
/* 0000:1fe2 — INT 8, 72.8 Hz */
void interrupt timer_isr(void)
{
    DS_05F8++;                                                /* u32 */
    if (DS_0608-- != 0) { out(0x20, 0x20); sti(); }           /* old value tested, then decremented */
    else { old_int8(); DS_0608 = 3; DS_0600++; }              /* BIOS every 4th tick (18.2 Hz); BIOS sends EOI */

    if (DS_47CE == 0 && DS_47CF != 0) {                       /* sound switched off: close speaker gate */
        out(0x61, in(0x61) & 0xFC); DS_47CF = 0;
    }
    if (DS_0610) {                                            /* demo */
        if ((DS_05F8 & (DS_06A6 ? 0x0F : 0x7F)) == 0) demo_step();          /* 0000:1bae, game_flow */
        if ((DS_06AE || DS_06B0) && (DS_472A != DS_06AE || DS_472C != DS_06B0)) {
            DS_472A += sign(DS_06AE - DS_472A);
            DS_472C += sign(DS_06B0 - DS_472C);
            DS_6C4A = 1;
        }
    }
    music_sequencer_tick();                                   /* 0000:20b8..2294, see sound */

    if ((i8)DS_4733 > 0) {                                    /* cursor visible */
        cli(); if (DS_06E0) goto out; DS_06E0 = 1; sti();
        int dx = 0, dy = 0;
        if (DS_4740 == 0) {                                   /* keyboard ISR not mid-update */
            if ((i8)DS_472E) dx = (i8)DS_472E > 0 ? 1 : -1;
            if ((i8)DS_472F) dy = (i8)DS_472F > 0 ? 1 : -1;
        }
        if (DS_0610 == 0 && DS_471D) {                        /* joystick overrides per axis */
            joy_read();
            if (DS_4718 < DS_4722) dx = -1; else if (DS_4718 > DS_4724) dx = 1;
            if (DS_471A < DS_4726) dy = -1; else if (DS_471A > DS_4728) dy = 1;
        }
        if (dx | dy) { DS_472A += step_x[dx]; DS_472C += step_y[dy]; DS_6C4A = 1; }  /* ±1 px */
        DS_06E0 = 0;
    }
out: ;
}
```

`0000:238c` (race_isr, installed by `race_isr_enter`) has the same tick/BIOS-chain head (`DS:05F8++`, `DS:0608`
countdown, `DS:0600++`, old INT 8 every 4th tick). On the other three ticks it drives the engine sound through PIT
ch2 (alternating on `DS:0608 & 1`, see sound). On chain ticks, if `DS:8ACA` (driving) and not `DS:0286` (paused), it
decrements `DS:0609` and every 3rd time (= every 12 ticks, 6.07 Hz), guarded by `DS:06E8`, it samples the controls
and runs one car-control step (see race):

* steering: held keys `DS:472E` → sets `DS:472A` = ±1; joystick X beyond ±centre/2 → `DS:472A` ∓/± 1; mouse
  (`mouse_poll`, x-shift 1, y-shift 6) → `DS:472A += sign(dx)`; up/down from `DS:472F`, joystick Y, mouse dy > 0,
  right button `DS:05F0`;
* "any click" edge from `DS:4732` / `DS:47CD`; automatic driving when `DS:0610` or `DS:0612` (`auto`);
* calls `rnd`, `2645:206b` (collision?) and `race_ctrl_reset 0000:2374` on crashes; decays the steering
  `DS:78E2/78E4`.

The port must run it at exactly the same place in the tick sequence as the original: once per 72.8 Hz tick of the
host timer, with the 1-in-4 / 1-in-12 counters above.

### 4.4 Keyboard

```c
/* 0000:2c42 — INT 9 */
void interrupt kbd_isr(void)
{
    u8 sc = in(0x60);
    u8 p = in(0x61); out(0x61, p | 0x80); out(0x61, p);       /* XT acknowledge */
    if (DS_0610 && sc != 0x1D && sc != 0x9D && sc != 0x32) {  /* demo: only Ctrl and 'M' pass */
        if (sc == 0x01) DS_0610 = 0x63;                       /* Esc aborts the demo */
        goto eoi;
    }
    if (sc == 0xFF || sc >= 0xE0) goto eoi;                   /* E0/E1 prefixes ignored */
    bool make = !(sc & 0x80);
    u8 i = sc & 0x7F;
    if (i > 0x55) { i = 0x56; make = true; }                  /* table[56h] = 0: ignored */
    i8 k = (i8)DS_4766[i];
    if (k == 0) goto eoi;
    if (k < 0 && k >= -30) {                                  /* direction key: k = -2,-4,..,-16 */
        i8 *dxy = (i8 *)(0x4766 + k);                         /* (dx, dy) at DS:4756..4765 */
        u8 *held = (u8 *)(0x47E8 + k);
        if (make) {
            if (!*held) { *held = 1; DS_4740 = -1;
                DS_472E += dxy[0]; DS_472F += dxy[1]; *(u16 *)&DS_4730 = *(u16 *)dxy; DS_4740 = 0; }
        } else if (*held) { DS_4740 = -1; *held = 0;
                DS_472E -= dxy[0]; DS_472F -= dxy[1]; DS_4740 = 0; }
        goto eoi;
    }
    if (make) {
        if (k == 0x1F) {                                      /* primary click (Space, Ins, keypad 0) */
            if (DS_4732 == 0) { DS_4740 = -1; DS_4736 = 0x1F; DS_4738 = DS_472A; DS_473A = DS_472C; DS_4740 = 0; }
            DS_4732 |= 1; DS_47CD = 1; DS_47EE = 1;
        } else if (k == 0x1E) {                               /* secondary click (no default key) */
            if (DS_4732 == 0) { DS_4740 = -1; DS_4737 = 0x1E; DS_473C = DS_472A; DS_473E = DS_472C; DS_4740 = 0; }
        } else if (k == 0x1D) DS_47CE ^= 1;                   /* Ctrl: sound on/off */
        else DS_47D0 = (u8)k;                                 /* the single key slot (overwrites) */
    } else {
        if (k == 0x1F) { DS_4740 = -1; DS_4732 &= ~1; DS_4740 = 0; }
    }
eoi:
    out(0x20, 0x20);
}

/* 0000:12de */ void kbd_remap_screen(int screen)
{
    if (DS_0610) DS_47CC = 0;
    DS_6C4C = screen;
    kbd_remap_restore(0);
    DS_064E = (u8)screen;
    const u8 *src = (const u8 *)DS_579F[screen];               /* pairs {scan code, new key}, 0-terminated */
    u8 *save = DS_0650;
    for (n = 1; *src && n < 30; n++, src += 2, save += 2) {
        save[0] = src[0]; save[1] = DS_4766[src[0]]; DS_4766[src[0]] = src[1];
    }
    src = (const u8 *)DS_57F3;                                /* → DS:579C = {32h 'M', A6h}: music toggle */
    save[0] = src[0]; save[1] = DS_4766[src[0]]; DS_4766[src[0]] = src[1];
    save[2] = 0;
}

/* 0000:1391 */ int kbd_remap_restore(int keep_low)
{
    memset(DS_47D8, 0, 16);
    DS_4731 = DS_4730 = DS_472F = DS_472E = 0;
    int was = (i8)DS_064E;
    if (was) {
        for (u8 *p = DS_0650; *p; p += 2)
            if (!keep_low || (i8)p[1] > 0x1F) DS_4766[p[0]] = p[1];
        if (!keep_low) DS_064E = 0;
    }
    return was;
}
```

`kbd_text_mode_on/off` (`2c04`/`2c2b`) are direct table stores (section 2.2); both run with interrupts off.

### 4.5 UI event loop, tick timers, mouse

```c
/* 0000:0e24 */ void input_reset(void)
{
    DS_0566 = DS_0568 = -1; DS_056A = 0; DS_056C = 0xFFFF; DS_056E = 0x7FFF;
    DS_0570 = DS_0571 = 0;
    for (i = 0; i < 4; i++) tq[i].next = i + 1;               /* tq = DS:0572, 18-byte entries */
    tq[4].next = -1;                                          /* DS:05BA */
    u32 until = ticks() + 10;
    while (until > ticks()) {                                 /* busy wait: 10 ticks without a key */
        int n = 0;
        while (key_read()) n++;
        if (n) until = ticks() + 10;
    }
    DS_05CC = 0;                                              /* u32 */
    if (DS_0610 == 0 && DS_05F2) { mouse_poll(); DS_05E6 = DS_05E8 = 0; }
    DS_4736 = 0;
    memset(DS_47D8, 0, 16);
    DS_4731 = DS_4730 = DS_472F = DS_472E = DS_4732 = 0;
}

/* 0000:1843 */ int tq_insert(int delay, int prio)
{
    DS_0571 = 1;
    int e = DS_056A;
    if (e < 0) { DS_0571 = 0; return -1; }
    u32 due = ticks() + (i32)delay;
    int nx = DS_0568, pv = -1;                                /* walk from the latest towards the earliest */
    if (delay != 0) {
        while (nx >= 0 && tq[nx].deadline > due) { pv = nx; nx = tq[nx].next; }
        if (nx >= 0 && tq[nx].deadline == due) {             /* same deadline: higher prio goes first */
            while (tq[nx].prio < prio) {
                pv = nx; nx = tq[nx].next;
                if (nx < 0 || tq[nx].deadline != tq[pv].deadline) break;
            }
        }
    }                                                         /* delay 0: nx = tail, pv = -1 (inserted at the tail) */
    tq[e].deadline = due; tq[e].prio = prio;
    DS_056A = tq[e].next;
    tq[e].next = nx;
    if (nx < 0) { DS_0566 = e; DS_056C = due; } else tq[nx].prev = e;
    tq[e].prev = pv;
    if (pv < 0) DS_0568 = e; else tq[pv].next = e;
    DS_0571 = 0;
    return e;
}

/* 0000:0f0b */ void tq_remove(int e)
{
    int nx = tq[e].next, pv = tq[e].prev;
    if (nx >= 0) tq[nx].prev = pv;
    else { DS_0566 = pv; if (pv < 0) DS_056C = 0x7FFFFFFF; else DS_056C = tq[pv].deadline; }
    if (pv >= 0) tq[pv].next = nx; else DS_0568 = nx;
    tq[e].next = DS_056A; DS_056A = e;
}

/* 0000:198a */ void tq_schedule_call(int arg, int delay)
{ int e = tq_insert(delay, DS_8EF6); tq[e].type = 1; tq[e].arg = arg; }   /* no check for -1 */

/* 0000:19bc */ void tq_cancel_call(int arg)
{ for (e = DS_0566; e >= 0; e = tq[e].next) if (tq[e].type == 1 && tq[e].arg == arg) tq[e].type = 0; }
```

`tq_cancel_call` walks `next` from the head: since `next` points to *earlier* entries, it only ever visits the head
(the head's `next` is −1). Faithful port: keep the loop exactly as written.

```c
/* 0000:1417 */ int ui_wait(int ticks_max)
{
    bool label_shown = false;
    if (DS_6C4C == 0x16) {                                     /* screen 16h: clear status texts */
        DS_8256 = 1; status_text(-1, DS_068C, 0);
        DS_8256 = 1; status_text(DS_51EE, DS_068D, DS_8BC0);
        DS_7F32 = 0;
    }
    bool timed = (DS_05CC != 0);                               /* u32 != 0 */
    u32 until = ticks() + (i32)ticks_max;
    do {
        if (DS_0610 == 0x63 && DS_06A4 != 0x63) { DS_06A4 = 0x63; platform_exit(); }
        if (DS_4BC8 && (DS_8236 == -2 || DS_8236 == -6) && (DS_05F8 & 0x0F) == 0
            && DS_58D0 && DS_47CF) {                           /* palette rotation, see 7. timing */
            u8 c = DS_069A; DS_069A = DS_0699; DS_0699 = DS_0698; DS_0698 = c;
            ega_set_palreg(6, (i8)DS_0698); ega_set_palreg(7, (i8)DS_0699); ega_set_palreg(8, (i8)DS_069A);
        }
        int k = (i8)key_read();
        if (k > 0) {
            if (DS_05D0) { DS_05D2 = k; return 0x3EB; }
            if (k == 0x7F) platform_exit();
        } else if (k < 0) {
            k += 0x32;
            if (k != -0x28) return k;                          /* per-screen hot key: 1..10, -20.. etc. */
            if (DS_58D4 == 1) {                                /* 'M' (A6h): music on/off */
                if (DS_58D0) { music_stop(); DS_58D0 = 0; } else { DS_58D0 = 1; music_resume(); }
            }
        }
        if (DS_056C <= ticks()) {                              /* tick timer due */
            int e = DS_0566;
            if (tq[e].type == 1) { int a = tq[e].arg; tq_remove(e); timer_callback(a); /* 0f38:1e41 */ }
            else tq_remove(e);
        }
        if (DS_0610 == 0 && DS_05F2) {
            mouse_poll();
            DS_6C4A |= (DS_05E2 || DS_05E4);
            if (DS_6C4A) { DS_472A += DS_05E2; DS_472C += DS_05E4; }
        }
        if (DS_4736 || DS_4737) {                              /* click event */
            cli(); DS_6C4E = DS_4736; int cx = DS_4738, cy = DS_473A; DS_4736 = DS_4737 = 0; sti();
            if (DS_05DE) { DS_05D2 = 0; return 0x3EB; }
            if (DS_8B8E) {                                     /* hotspots active */
                int id = (cx >= DS_8B90 && cx < DS_8B92 && cy >= DS_8B94 && cy < DS_8B96)
                         ? DS_8BAA : hotspot_at(cx, cy);       /* 0f38:4dad */
                if (id) return id;
            }
            if (DS_05D0) { DS_05D2 = 0; return 0x3EB; }
        } else if (timed && DS_05CC < ticks()) {               /* strict: fires one tick after the deadline */
            DS_05CC = 0; return 1000;
        }
        if ((i8)DS_4733 > 0 && DS_6C4A) {                     /* move the drawn cursor */
            cli();
            DS_472A = clamp(DS_472A, 0, 0x13A); DS_472C = clamp(DS_472C, 0, 0xC2);
            DS_6C46 = DS_472A; DS_6C48 = DS_472C; DS_6C4A = 0;
            sti();
            cursor_draw(DS_6C46, DS_6C48);                     /* 0f38:2e8b */
            if (DS_8B8E && !(inside current hotspot rect DS:8B90..8B96)) {
                int prev = DS_8BAA, id = hotspot_at(DS_6C46, DS_6C48);
                if (id) {
                    if (id != prev) {
                        if (DS_6C4C == 0x25) { DS_8256 = 1; status_text(-1, DS_068E, 0);
                                               DS_8256 = 1; status_text(DS_51EE, DS_068F, DS_8BC0); }
                        status_text(-1, 0x239E + ((int *)DS_8BA8)[2], 0);   /* hover label (HOT_DATA) */
                        label_shown = true;
                    }
                } else {
                    if (label_shown) { status_text(-1, DS_0690, 0); label_shown = false; }
                    if (DS_6C4C == 0x25) status_text(DS_51EE, DS_7F32, DS_8BC0);
                    else if (prev) status_text(-1, DS_0691, 0);
                }
            }
        }
    } while (until > ticks());
    return 0;
}

/* 0f38:7a47 */ void mouse_poll(void)
{
    int mx, my; mouse_motion(&mx, &my);                        /* INT 33h AX=0Bh */
    DS_05E6 += mx; DS_05E8 += my;
    int a = abs(DS_05E6) >> DS_05EA;                           /* shift of the magnitude */
    DS_05E2 = a;
    if (a) { if (DS_05E6 < 0) DS_05E2 = -a; DS_05E6 = 0; }     /* remainder is dropped */
    a = abs(DS_05E8) >> DS_05EC;
    DS_05E4 = a;
    if (a) { if (DS_05E8 < 0) DS_05E4 = -a; DS_05E8 = 0; }
    mouse_buttons();                                           /* INT 33h AX=3 */
    if (DS_05EE) {
        if (DS_4732 == 0) { DS_4736 = 1; DS_4738 = DS_472A; DS_473A = DS_472C; }
        DS_4732 |= 4;
    } else DS_4732 &= ~4;
}

/* 0f38:0b51 */ void wait_click_or_key(int t)
{
    u32 until = ticks() + (i32)t;
    while (until > ticks()) {
        if (DS_4736) { DS_4736 = 0; return; }
        if (DS_05F2) { mouse_buttons(); if (DS_05EE) return; }
        if (DS_471D) { joy_read(); if (DS_471C) return; }
        char k = key_read();
        if (k == 'M') { if (DS_58D0) { music_stop(); DS_58D0 = 0; } else { DS_58D0 = 1; music_resume(); } }
        else if (k == '\r' || k == 0x1B) return;
    }
}
```

### 4.6 Joystick

```c
/* 0000:2df4 — far, saves all registers, runs with interrupts off during the timing loop */
void joy_read(void)
{
    DS_47E8 = SP;
    cli();
    u16 cx = 0x400; u8 pending = 3, hist[2]; u16 when[2]; int n = 0;
    out(0x201, al); out(0x201, al);                           /* trigger (twice) */
    while (pending) {
        u8 v; do { v = in(0x201) & pending; } while (v == pending && --cx);
        if (cx == 0) { sti(); SP = DS_47E8; return; }        /* timeout: nothing updated */
        u8 fell = v ^ pending;                                /* axes that finished now */
        hist[n] = fell; when[n] = cx; n++;
        cx--; pending ^= fell;
    }
    sti();
    for each recorded (fell, cx) in reverse order:            /* two axes */
        count = 0x400 - cx;
        if (fell & 1) DS_4718 = count - DS_471E;              /* X */
        if (fell & 2) DS_471A = count - DS_4720;              /* Y */
    u8 b = (~in(0x201) >> 4) & 3;                             /* buttons 1, 2 */
    DS_471C = b;
    if (b) { if (DS_4732 == 0) { DS_4736 = b; DS_4738 = DS_472A; DS_473A = DS_472C; } DS_4732 |= 2; }
    else DS_4732 &= ~2;
}
```

### 4.7 Mouse (INT 33h via `int86`)

```c
/* 0f38:799a */ int mouse_reset(void)
{ if (*(u32 far *)MK_FP(0, 0xCC) == 0) return 0; return int86(0x33, AX=0).ax; }
/* 0f38:79d7 */ void mouse_buttons(void)
{ u8 b = int86(0x33, AX=3).bl; DS_05EE = b & 1; DS_05F0 = b & 2; }
/* 0f38:7a14 */ void mouse_motion(int *dx, int *dy)
{ r = int86(0x33, AX=0x0B); *dx = r.cx; *dy = r.dx; }
```

### 4.8 Race ISR switch

```c
/* 0000:1aad */ void race_isr_enter(void)
{
    DS_060A = 0;
    _dos_setvect(8, race_isr);
    if (DS_0610 == 0 && DS_05F2) { mouse_poll(); DS_05E8 = DS_05E6 = 0; DS_05EA = 1; DS_05EC = 6; }
    DS_4736 = DS_4732 = DS_4731 = DS_4730 = 0;
    DS_78E4 = DS_78E2 = 0; DS_0609 = 0;
    out(0x42, 0x1E); out(0x42, 0x00);                         /* ch2 divisor 1Eh */
    if (DS_47CE) { out(0x61, in(0x61) | 3); DS_47CF = 1; }    /* engine sound on */
}
/* 0000:1b27 */ void race_isr_leave(void)
{
    _dos_setvect(8, timer_isr);
    DS_58DC = DS_58DA = DS_5E0E = 0;
    out(0x61, in(0x61) & 0xFC); DS_47CF = 0;
    if (DS_0610 == 0 && DS_05F2) { mouse_poll(); DS_05E8 = DS_05E6 = 0; DS_05EA = 1; DS_05EC = 1; }
    DS_4736 = DS_4732 = DS_4731 = DS_4730 = 0;
}
```

### 4.9 Memory

```c
/* 0000:3709 */ void far *far_alloc(u16 size, int fatal)
{
    void far *p = _fmalloc(size);
    if (p) {
        if (FP_SEG(p) != DS_6C60 &&                          /* near-heap block: rejected */
            !(DS_8236 == -6 && FP_SEG(p) >= 0x8000))         /* Tandy: not in the top 512 K (video RAM) */
            return p;
        _ffree(p);
    }
    if (fatal) out_of_memory();
    return NULL;
}

/* Bitmap descriptor (48 bytes): +00 w, +02 h, +04 size, +24 data far, +28 mask far, +2C bytes/row, +2E drv (see 5.5) */
/* 0000:376b */ Bitmap far *bitmap_alloc(int w, int h, int flags, int drv, Bitmap far *d)
{
    int bpr   = ((w - 1) >> DS_823A) + 1;                    /* sar */
    u16 plane = (u16)(bpr * h);                               /* imul, low word */
    u16 total = plane * DS_8238;                              /* mul, low word (EGA: 4 planes) */
    u16 hdr = 0x30, maskb = flags ? plane : 0;
    if (!(flags & 2)) hdr += total;
    if (d == NULL) {
        d = far_alloc(hdr + maskb, 0);
        if (!d) return NULL;
        d->data = MK_FP(FP_SEG(d), FP_OFF(d) + 0x30);
    }
    if (flags & 2) {
        if (d->data) { d->mask = d->data; d->data = NULL; }
        else d->mask = MK_FP(FP_SEG(d), FP_OFF(d) + 0x30);
    } else
        d->mask = flags ? MK_FP(FP_SEG(d->data), FP_OFF(d->data) + total) : NULL;
    d->size = total; d->w = w; d->h = h; d->bpr = bpr; d->drv = (u8)drv;
    return d;
}

/* 0000:3904 */ void bitmap_init(Bitmap far *d, void far *data, void far *mask, int w, int h, int drv)
{
    int bpr = ((w - 1) >> DS_823A) + 1;
    d->data = data; d->mask = mask; d->size = bpr * h * DS_8238;   /* imul */
    d->w = w; d->h = h; d->bpr = bpr; d->drv = (u8)drv;
}
/* 0000:3880 bitmap_make: d = far_alloc(0x30, 0); if (!d) return NULL; bitmap_init(d, data, mask, w, h, drv) */

/* 0000:3967 */ void far *vram_pool_alloc(u16 size, u8 *plane)
{
    for (int i = 0; i < 4; i++) {
        if ((u16)(DS_4C18[i] + size) < 0x7D00) {              /* unsigned 16-bit, may wrap */
            void far *p = MK_FP(FP_SEG(DS_4C14), FP_OFF(DS_4C14) + DS_4C18[i] + 0x7D00);  /* A000:7D00+used */
            DS_4C18[i] += size;
            *plane = (u8)i;
            return p;
        }
    }
    *plane = 0xFF;
    return NULL;
}
```

Note the capacity test: `used + size < 7D00h` although the pool starts at 7D00h, so each plane pool may hold
up to 7CFFh bytes and extends to offset `0xF9FE` of the 64 K plane. The four pools are the four EGA planes at the
same offsets (selected through the map mask on write and the read map select on read, 4.10).

```c
/* 0f38:9f2c */ far_ptr arena_alloc(int n, int mode)         /* W = DS:8B02 (u16 array), arena = DS:6CD0 */
{
    u16 off = FP_OFF(DS_6CD0);
    switch (mode) {
    case 0: off += DS_8F00; DS_8F00 += n; W[0] = DS_8F00; break;          /* permanent bump */
    case 1: off += W[DS_7B14]; W[DS_7B14 + 1] = W[DS_7B14] + n; DS_7B14++; break;  /* low stack */
    case 2: W[DS_7D80] -= n; off += W[DS_7D80]; W[DS_7D80 - 1] = W[DS_7D80] - 1; DS_7D80--; break; /* top stack */
    case 3: n = DS_78DE - n; DS_78DE = n - 1; off += n; DS_8B7A = DS_78DE; break;
    }
    return MK_FP(FP_SEG(DS_6CD0), off);                       /* other modes: arena start */
}
/* 0f38:a0b8 */ void arena_pop(int k)
{
    if (k == 0) DS_7D80 = 0x3C;
    else { DS_7D80 += k; if (DS_7D80 != 0x3C) { W[DS_7D80] = W[DS_7D80 + 1] - 1; return; } }
    W[0x3C] = DS_78DE;
}
```

### 4.10 Picture libraries

```c
/* 0f38:6057 */ static int pic_index(int id)
{
    id = (id >= 1000) ? id - 0x3DF : id - 1;                 /* 1000 → 9 */
    if (DS_8236 != -2 && DS_8236 != -6) id = DS_07C6[id];    /* CGA/Hercules library order */
    return id;
}

/* 0f38:6089 */ static void lib_select(int id)
{
    int lib2 = pic_index(id) >= DS_8BC8;
    int *h     = lib2 ? &DS_4EDA : &DS_4ED8;
    int *other = lib2 ? &DS_4ED8 : &DS_4EDA;
    DS_6C6E = lib2 ? DS_8DD6 : DS_8DD4;  DS_6C6C = h; DS_6C70 = other;
    DS_6C6A = lib2 ? &DS_8DD2 : &DS_8BC8;
    if (*h) return;
    _dos_close(*other); *other = 0;                           /* only one library open at a time */
    char *m = DS_4EE4;                                        /* "insert disk ? into drive ?" */
    m[0x0C] = !lib2 ? '1' : (DS_8236 == -2 || DS_8236 == -6) ? '2' : '3';
    m[0x19] = DS_6C6E[0];
    for (int n = 0; _dos_open(DS_6C6E, 0, h) != 0; ) {
        if (n++ == 3) platform_exit();
        message_box(m);
    }
    int got;
    if (_dos_read(*h, DS_6C6A, 2, &got) || got != 2 || DS_8BC8 + DS_8DD2 != 0x115) {
        message_box(DS_4EE8 /* "bad file format" */); platform_exit();
    }
}

/* 0f38:61ab */ int lib_read_dir(int lib)
{
    int base = (lib == 1) ? 0 : DS_8BC8, maxsz = 0;
    lib_select(lib == 1 ? 1 : 1000);
    int count = (lib == 1) ? DS_8BC8 : DS_8DD2;
    for (int i = 0; i < count; i++) {
        PicEntry far *e = MK_FP(0x389B, (base + i) * 0x30);
        int got;
        if (lib_seek(*DS_6C6C, 2 + (i32)i * 0x1E) ||
            _dos_read(*DS_6C6C, e, 0x1E, &got) || got != 0x1E) {
            message_box(DS_4EE8); platform_exit();
        }
        e->plane = 0xFF; e->packed = NULL; e->data = NULL; e->mask = NULL;
        e->bpr = -1; e->drv = 1;
        if (e->size > maxsz) maxsz = e->size;                /* unsigned compare */
    }
    return maxsz;
}

/* 0f38:62ba */ static int pic_load(int id, int to_vram, void far *dst)
{
    PicEntry far *e = MK_FP(0x389B, pic_index(id) * 0x30);
    void far *vram = NULL; u8 plane = 0xFF;
    if (e->packed) return 1;
    if (to_vram) {
        vram = vram_pool_alloc(e->size, &plane);
        e->packed = vram ? arena_alloc(e->size + 1, 2) : NULL; /* temporary buffer in the arena */
        e->plane  = plane;
    } else {
        e->packed = dst ? dst : far_alloc(e->size + 1, 0);
        e->plane  = 0xFF;
    }
    if (!e->packed) return 0;
    lib_select(id);
    i32 pos = (i32)(*DS_6C6A * 0x1E + 2) + e->offset;
    if (lib_seek(*DS_6C6C, pos)) goto bad;
    if (e->plane != 0xFF) { out(0x3C4, 2); out(0x3C5, 1 << e->plane); }  /* map mask: one plane */
    int tries = 0;                                            /* [bp-16h], kept across attempts */
    for (;;) {
        u16 n = e->size + 1, got;                             /* includes the next picture's first byte */
        if (_dos_read(*DS_6C6C, e->packed, n, &got) || got != n) goto bad;
        u8 far *b = e->packed;
        if (b[0] == 0xBC && b[n - 1] == 0xBC) break;
        if (DS_6C68 > 2) goto bad;                            /* hard disk: no retry */
        do {                                                  /* floppy: ask for the disk again */
            if (++tries > 3) platform_exit();
            DS_4EE4[0x0C] = (*DS_6C6C == DS_4ED8) ? 1 /* sic: byte 01h, not '1' */
                            : (DS_8236 == -2 || DS_8236 == -6) ? '2' : '3';
            DS_4EE4[0x19] = DS_6C6E[0];
            message_box(DS_4EE4);
            _dos_close(*DS_6C6C);
        } while (_dos_open(DS_6C6E, 0, DS_6C6C) != 0);
        if (lib_seek(*DS_6C6C, pos)) goto bad;
    }
    if (e->plane == 0xFF) return 2;
    movedata(FP_SEG(e->packed), FP_OFF(e->packed), FP_SEG(vram), FP_OFF(vram), e->size);  /* to one plane */
    e->packed = vram;
    arena_pop(1);
    out(0x3C4, 2); out(0x3C5, 0x0F);
    return 3;
bad:
    message_box(DS_4EE8); platform_exit();
}
```

The bug marked *sic*: for LIB1 the retry prompt gets the byte 01h instead of `'1'` (`mov al,1` at `0f38:64c6`); only
reachable on floppies. One counter (`si`) serves the prompt/close/re-open loop and all later read attempts of the
same call; a failed re-open repeats prompt, close (of the failed handle) and open.

```c
/* 0f38:65ae */ Bitmap far *pic_decode(int id, Bitmap far *dst)
{
    cursor_ctl(-4);                                          /* hide */
    int r = pic_load(id, 0, DS_7564);                        /* packed data → shared buffer (if not resident) */
    PicEntry far *e = MK_FP(0x389B, pic_index(id) * 0x30);
    if (DS_4F0E) dst = bitmap_alloc(e->w, e->h, e->masked, 1, dst);
    u8 tokens[16]; memcpy(tokens, e->tokens, 16);
    if (e->plane != 0xFF) { out(0x3CE, 4); out(0x3CF, e->plane); }   /* read map select: parked plane */
    u8 far *src = (u8 far *)e->packed + 1;                   /* skip the leading BCh */
    u16 n = e->size - 1;
    if (DS_8236 == -2) {
        if (!dst->data) unpack(src, dst->mask, n, tokens, 16);          /* 0e92:0006 */
        else {
            unpack(src, dst->data, n, tokens, 16);
            if (dst->mask) make_mask(dst->data, dst->mask, dst->size);  /* 2634:00c1 */
        }
    } else if (DS_8236 == -6) { /* Tandy: 2462:0120 / 005a / 0229 */ }
    else unpack(src, dst->data ? dst->data : dst->mask, n, tokens, 16);
    if (e->plane != 0xFF) { out(0x3C4, 2); out(0x3C5, 0x0F); }         /* map mask back (read map not reset) */
    if (r == 2) e->packed = NULL;                            /* the shared buffer is reused next time */
    cursor_ctl(-2);                                          /* show */
    return dst;
}

/* 0f38:683e */ Bitmap far *pic_get(int id, int mode)
{ int w, h, m; pic_info(id, &w, &h, &m); Bitmap far *b = bitmap_alloc_arena(w, h, m, mode); pic_decode(id, b); return b; }

/* 0f38:6887 */ int pic_park_list(int *ids)
{ if (DS_0280) return 1; for (; *ids; ids++) pic_load(*ids, 1, NULL); return 1; }

/* 0f38:68ca */ int pic_load_list(int *ids, int keep_going)
{
    int n = 0;
    for (; *ids; ids++) {
        if (pic_load(*ids, 0, NULL)) n++;
        else if (!keep_going) return n;
    }
    return n;
}

/* 0f38:655e */ void pic_free(int id)
{ PicEntry far *e = MK_FP(DS_691C, pic_index(id) * 0x30); if (e->packed) { far_free(e->packed); e->packed = NULL; } }

/* 0f38:6943 */ void lib_open_all(int disks, int mode)
{
    _dos_getdrive(&DS_6C68);                                 /* 1 = A: */
    u16 eq;
    disks = (DS_6C68 < 3 && disks > 1 && ((eq = _bios_equiplist()) & 1) && (eq & 0xC0)) ? 2 : 1;
    char *n1 = (mode == 2 || mode == 5) ? "?:lib1" : "?:lib1c";
    DS_8DD4 = n1; n1[0] = '@' + DS_6C68;
    if (_dos_open(n1, 0, &DS_4ED8) || DS_4ED8 == 0) {
        if (DS_6C68 > 2) message_box("copy all files to current directory");
        else { strcpy(DS_4EEC + 0x10, n1 + 2); message_box(DS_4EEC); }   /* "file not found: lib1" */
        platform_exit();
    }
    int got;
    if (_dos_read(DS_4ED8, &DS_8BC8, 2, &got) || got != 2) { message_box("bad file format"); platform_exit(); }
    char *n2 = (mode == 2 || mode == 5) ? "?:lib2" : "?:lib2c";
    DS_8DD6 = n2; n2[0] = n1[0];
    if (_dos_open(n2, 0, &DS_4EDA) || DS_4EDA == 0) {
        if (DS_6C68 > 2) { message_box("copy all files to current directory"); platform_exit(); }
        if (disks == 1) DS_4EDA = 0;                          /* single drive: asked for later */
        else {
            n2[0] = (DS_6C68 == 1) ? 'B' : 'A';               /* the other floppy drive */
            for (int t = 0; _dos_open(n2, 0, &DS_4EDA) || DS_4EDA == 0; t++) {
                if (t == 3) platform_exit();
                DS_4EE4[0x0C] = (mode == 2 || mode == 5) ? '2' : '3';
                DS_4EE4[0x19] = n2[0];
                message_box(DS_4EE4);
            }
        }
    }
    if (DS_4EDA && (_dos_read(DS_4EDA, &DS_8DD2, 2, &got) || got != 2 || DS_8BC8 + DS_8DD2 != 0x115)) {
        message_box("bad file format"); platform_exit();
    }
}

/* 0f38:6b79 */ void hot_data_load(void)
{
    DS_6C72[0] = DS_8DD6[0]; DS_6C72[1] = DS_8DD6[1];         /* "X:" of LIB2 */
    strcpy(DS_6C72 + 2, "hot_data");
    int h, got;
    if (_dos_open(DS_6C72, 0, &h)) { message_box("Can't open data file"); platform_exit(); }
    for (int i = 0; i < 13; i++)
        if (_dos_read(h, MK_FP(DS, DS_4EA4[i]), DS_4EBE[i], &got) || got != DS_4EBE[i]) {
            message_box("Data disc read fail"); platform_exit();
        }
    _dos_close(h);
    hall_load();
}

/* 0f38:6c3d */ int data_disk_check(void)
{
    if (DS_6C68 >= 3) return '@' + DS_6C68;
    int h;
    for (int t = 0; _dos_open(DS_6C72, 0, &h); ) {
        if (++t > 3) platform_exit();
        DS_4EE4[0x0C] = (DS_8236 == -2 || DS_8236 == -6) ? '2' : '3';
        DS_4EE4[0x19] = DS_8DD6[0];
        message_box(DS_4EE4);
    }
    _dos_close(h);
    lib_select(1000);
    return DS_6C72[0];
}

/* 0f38:6d09 */ void hall_load(void)
{
    DS_4F3A[0] = DS_8DD6[0];                                  /* "X:hall_dat" on the LIB2 drive */
    data_disk_check();
    int h, got;
    if (_dos_open(DS_4F3A, 0, &h) == 0 && h != 0) {
        if (_dos_read(h, &DS_7646, 2, &got) || got != 2) { message_box("bad file format"); platform_exit(); }
        for (int i = 0; i < DS_7646; i++)                     /* no upper bound check (10 fit) */
            if (_dos_read(h, DS_82CA + i * 0x17, 0x17, &got) || got != 0x17) { message_box("bad file format"); platform_exit(); }
        _dos_close(h);
        hall_unscramble();                                    /* & 7Fh over all 230 bytes */
    } else DS_7646 = 0;
}

/* 0f38:6dd2 */ void hall_save(void)
{
    hall_scramble();                                          /* | 80h over all 230 bytes */
    data_disk_check();
    int h, err = _dos_creat(DS_4F3A, 0, &h), got;
    if (DS_6C68 > 2) {
        if (h == 0 || err) { message_box("write error"); platform_exit(); }
    } else {
        DS_4EE4[0x0C] = '1'; DS_4EE4[0x19] = DS_4F3A[0];      /* prepared but not shown (bug) */
        for (int t = 0; h == 0 || err; t++) {
            if (t == 3) platform_exit();
            message_box("bad file format");                    /* DS_4EE8, sic */
            err = _dos_creat(DS_4F3A, 0, &h);
        }
    }
    if (_dos_write(h, &DS_7646, 2, &got) || got != 2) goto fail;
    for (int i = 0; i < DS_7646; i++)
        if (_dos_write(h, DS_82CA + i * 0x17, 0x17, &got) || got != 0x17) goto fail;
    if (_dos_close(h) == 0) goto done;
fail:
    message_box("write error"); remove(DS_4F3A); message_box("write error");
done:
    hall_unscramble();
}

/* 0000:39c0 */ static int lib_phase2(void)                   /* called by the game loop after the title */
{
    page_setup(0);                                            /* 0f38:0dbd, video */
    tune_start(2, 10);
    title_free();                                             /* 0f38:0bf0 (game_flow/video) */
    drv_vec[7]();                                             /* pal_black */
    page_setup(1);
    rnd(-1);                                                  /* seed */
    drv_vec[7]();
    if (!DS_70E2) lib_read_dir(2);
    if (!DS_8BCE) {
        /* original: if (!copy_protection()) platform_exit();   — patched to a jmp in SR.EXE (3a1a) */
        cursor_ctl(-4);
    }
    video_2645_0d48();                                        /* see video */
    DS_7E9A = 0;
    DS_0610 = DS_8BCE;                                        /* the demo starts here */
    if (!DS_70E2) { hot_data_load(); if (DS_8236 == -2) pic_park_list(DS_4C28); }
    pic_load_list(DS_4C28, 1);
    cursor_ctl(-2);
    return -1;
}

/* 0000:3a83 */ void wait_ticks_or_input(int t)
{
    DS_05CC = ticks() + (i32)t;  DS_05D0 = 1;
    ui_wait(30000);
    DS_05CC = 0; DS_05D0 = 0;
}
```

### 4.11 Random numbers

```c
/* 0f38:5eb6 — Wichmann–Hill with Schrage steps, all 16-bit signed (idiv) */
static i16 wh1(i16 s) { s = (s % 177) * 171 - (s / 177) * 2;  return s < 0 ? s + 30269 : s; }
static i16 wh2(i16 s) { s = (s % 176) * 172 - (s / 176) * 35; return s < 0 ? s + 30307 : s; }
static i16 wh3(i16 s) { s = (s % 178) * 170 - (s / 178) * 63; return s < 0 ? s + 30323 : s; }
int rnd(int n)
{
    if (n <= 0) {
        i16 t = (i16)time(NULL);                              /* low word, signed */
        i16 s = t % 30000 + 1;                                /* may be negative */
        DS_6C62 = DS_6C64 = DS_6C66 = s;
        do DS_6C64 = wh1(DS_6C64); while (DS_6C64 > 30000);   /* sic: wh1 for s2 */
        do DS_6C66 = wh3(DS_6C66); while (DS_6C66 > 30000);
        return -1;
    }
    DS_6C62 = wh1(DS_6C62); DS_6C64 = wh2(DS_6C64); DS_6C66 = wh3(DS_6C66);
    return (DS_6C62 % n + DS_6C64 % n + DS_6C66 % n) % n;
}
```

## 5. File formats and in-exe tables

### 5.1 Command line

`SR [1] [nomouse] [demo] [auto] [nouemem]`, any order. `1` (exactly, case-sensitive) = one floppy drive; the
others are case-insensitive. `demo` runs the demo (`DS:0610` from `lib_phase2`); `auto` = automatic driving
(`DS:0612`, read by the race ISR and `2645`); `nouemem` = no pictures parked in EGA memory.

### 5.2 Keyboard translation table `DS:4766` (scan code set 1 → key code)

| scan codes | key code | meaning |
|---|---|---|
| 01 | 1Bh | Esc |
| 02–0B | '1'…'9','0' | digits |
| 0C | '-' | |
| 10–19 | Q W E R T Y U I O P | letters are upper case |
| 1C | 0Dh | Enter |
| 1D | 1Dh | Ctrl: toggles `DS:47CE` (sound) |
| 1E–26 | A S D F G H J K L | |
| 2C–32 | Z X C V B N M | |
| 39, 52, 55 | 1Fh | Space, Ins/keypad 0, 55h: **click** |
| 29 (`) | −4 | up |
| 2B (\\) | −8 | left |
| 47, 48, 49 | −2, −4, −6 | keypad 7/8/9: up-left, up, up-right |
| 4B, 4D, 4E (+) | −8, −10, −10 | left, right, right |
| 4A (−) | −14 | down |
| 4F, 50, 51 | −12, −14, −16 | down-left, down, down-right |
| everything else | 0 | ignored (Backspace, ',', '.' only in text mode) |

Direction deltas (dx, dy) at `DS:4766 + code`: −2 (−1,−1), −4 (0,−1), −6 (1,−1), −8 (−1,0), −10 (1,0),
−12 (−1,1), −14 (0,1), −16 (1,1). The arrow keys of an AT keyboard send E0 + the keypad codes; E0 is ignored,
so they act like the keypad. F-keys (3B–3F) and others become keys only through the per-screen remap
(`DS:579F[screen]`, 41 lists of {scan code, i8 code} pairs; the returned value is `code + 32h`, e.g. −49 → 1).
`DS:579F[0]` is not a valid list (value 002Ah); game_flow must never call `kbd_remap_screen(0)`.

### 5.3 Picture directory (segment `389b`, 48 bytes per picture, 277 entries: LIB1 0–8, LIB2 9–276)

| off | size | from | meaning |
|---|---|---|---|
| +00 | u16 | file | width (multiple of 8) |
| +02 | u16 | file | height |
| +04 | u16 | file | raw size (w×h/2) |
| +06 | u8 | file | masked (colour 0 transparent) |
| +07 | u8[16] | file | run tokens |
| +17 | u8 | file | 0 |
| +18 | u32 | file | offset from the end of the directory |
| +1C | u16 | file | packed size (without the trailing BCh) |
| +1E | u8 | 61ab | plane of the parked copy in EGA memory, FFh = none |
| +1F | u8 | – | unused |
| +20 | far ptr | 62ba | packed data (RAM, `DS:7564`, or `A000:7D00+`), NULL = not loaded |
| +24 | far ptr | 61ab | NULL (bitmap-descriptor layout: data) |
| +28 | far ptr | 61ab | NULL (mask) |
| +2C | i16 | 61ab | FFFFh (bytes per row) |
| +2E | u8 | 61ab | 1 (driver byte) |

Library file layout: FORMATS.md. The loader reads `packed size + 1` bytes and requires both the first and the last
byte to be BCh.

### 5.4 `HOT_DATA` (4064 bytes read, 13 blocks, straight into DGROUP)

Table `DS:4EA4` (destinations) / `DS:4EBE` (sizes). Record structures are guesses from the data; the owners
(garage/race/game_flow) must confirm.

| # | file offset | size | DS dest | first look |
|---|---|---|---|---|
| 0 | 0000 | 396 | 7FF8 | 22 × 18 bytes; words 04ACh.. (LIB2 picture ids) and 0593h.. |
| 1 | 018C | 260 | 7D86 | 26 × 10 bytes (`0000:e0e2` indexes `7D86 + i*10`) |
| 2 | 0290 | 260 | 8DF0 | 26 × 10 bytes: {u16 n, u16 pic ids ×4} |
| 3 | 0394 | 312 | 7682 | 26 × 12 bytes |
| 4 | 04CC | 520 | 70E4 | 26 × 20 bytes |
| 5 | 06D4 | 156 | 818C | 26 × 6 bytes |
| 6 | 0770 | 1850 | 2C02 | text pool: car advertisements ("1940 Chevrolet 2-dr coupe.", "+ Auto.", "Runs good. $400") |
| 7 | 0EAA | 2 | 7D16 | u16 (2) |
| 8 | 0EAC | 200 | 239E | text pool: hover labels ("'Hot Lips'", "'Ranger'"…); `ui_wait` shows `0x239E + hotspot[+4]` |
| 9 | 0F74 | 18 | 7D68 | 9 × u16 (D2h B4h 96h AFh 96h 7Dh 8Ch 78h 64h) |
| 10 | 0F86 | 18 | 8BAE | 9 × u16 |
| 11 | 0F98 | 24 | 8DD8 | 12 × u16 |
| 12 | 0FB0 | 48 | 784A | 8 × {u16 picture id 04A2h–04A9h, u16, u16} |

The retail file has 52 extra bytes (a copy of its last 52 bytes) that are never read.

### 5.5 Bitmap / page descriptors (48 bytes)

`+00 w`, `+02 h`, `+04 size` (per plane for VRAM pages, all planes for RAM bitmaps), `+1C` FFFFh on page 0,
`+24 data far ptr`, `+28 mask far ptr`, `+2C bytes per row`, `+2E driver byte` (FEh EGA, FDh CGA, FCh Hercules,
FAh Tandy). Segment `2e3e`: page 0 = A000:0000, page 1 = A200:0000 (= A000:2000), 320×200, 40 bytes/row,
8000 bytes; the third (CGA) descriptor is overwritten by `gfx_init` on EGA with A000:4000, and a fourth page
`2e68:0000` = A000:6000. The rest is video's.

### 5.6 `hall_dat`

`u16 count` (plain) followed by `count × 23` bytes with bit 7 set on every byte (cleared after loading; the
in-memory copy `DS:82CA` holds 10 records = 230 bytes). Written to the LIB2 drive. Record contents: see game_flow.

### 5.7 Messages

`DS:4BEE` "Not enough memory to run Street Rod !", `DS:4E31` "insert disk ? into drive ?" (disk digit at +0Ch,
drive letter at +19h), `DS:4E4C` "copy all files to current directory", `DS:4E70` "bad file format",
`DS:4E80` "write error", `DS:4E8C` "file not found: ????" (name copied at +10h), `DS:4F10` "Can't open data file",
`DS:4F25` "Data disc read fail", INT 24h: `DS:0538` "cannot read drive ?", `DS:054C` "disk ? is write protected",
`DS:061E` "printer out of paper", `DS:0633` "disk error", `DS:063E` "hardware error", `DS:0614` "Quit demo".
Menu: `DS:004E`…`DS:0208` (the five mode lines, title, instructions).

## 6. Hardware/DOS dependencies and their SDL3 replacements

| Original | Where | SDL3 port |
|---|---|---|
| Video detection (INT 10h 0Fh/12h/1A00h, INT 11h, ports 3BAh/3BFh/3D4h, BIOS ROM search) and the text menu | 0226, 0082, e5xx–e6xx | Skipped: the port is the VGA path. Set `DS:0254 = 1`, `DS:8236 = −2`, `DS:0250 = 1`, run `drv_ega_init` (copy `DS:5BD0` → `DS:78A2`), `DS:0048 = 1`. `/* PORT: */` note. Optional later: show the menu text in the window |
| INT 10h mode 0Dh, equipment byte | 04f4, 0316, e610, 2bc0/2be2 | `ega_init()`; no restore at exit |
| INT 10h AX=1002h/1000h palette, wait for 3DAh bit 3 | 1fa4, 1f4b, 0b2d, 0b3f | Keep `DS:0440` in memory, give the 16 registers to the EGA model (6-bit rgbRGB interpreted with the 200-line rule: bit 4 = intensity, bits 0–2 = BGR); the retrace wait = the host's frame timing (see video) |
| PIT ch0 4000h + INT 8 → `timer_isr`/`race_isr`, chain every 4th | 1111, 0fb7, 1aad, 1b27 | `host_set_pit_divisor(0x4000)`; the host calls the current handler (a C function pointer switched by `_dos_setvect(8, …)`) once per 1193182/16384 = 72.8236 Hz tick, catching up missed ticks. Old INT 8 = nothing (the BIOS clock is not needed); keep the `DS:0608`/`DS:0600` counters |
| INT 9 + port 60h/61h, scan code set 1 | 2c42, 2dc3, 2de2 | SDL key events → XT make/break codes (`host_xt_scancode`), fed one by one to a C `kbd_isr(sc)` that works on `mem[]` (the table is patched by the game). Key repeat: pass SDL repeats as make codes (the ISR ignores repeated direction makes via `held`). Arrows: send 48/4B/4D/50 (E0 is dropped anyway) |
| BIOS `kbhit/getch` | 0082 | only in the skipped menu |
| INT 33h reset / buttons / mickeys | 799a, 79d7, 7a14 | Reset returns −1 if a mouse is allowed (`nomouse` → 0). Buttons from SDL mouse state (left = bit 0, right = bit 1). Mickeys: sum of SDL relative motion since the last `AX=0Bh` call, scaled so 2 mickeys = 1 game pixel (`mickeys = rel_window_px · 2 · 320 / window_width`, y with 200 / window_height); in relative mode (`SDL_SetWindowRelativeMouseMode`) during the game. There is no absolute position to map |
| Joystick port 201h | 1111, 2df4 | SDL gamepad. Presence: a gamepad connected at `platform_install` → the calibration path with a fixed centre. `joy_read` replacement: raw count = 100h + axis·FFh/32767 (so the centre is 100h and the ±80h dead zone = half deflection), minus `DS:471E/4720`; buttons A/B → bits 0/1 of `DS:471C`, plus the click logic. d-pad may also feed the keypad codes |
| PC speaker (61h, ch2 42h/43h) | 0d99, 1a05, 1aad, 1b27, ISRs | host PC-speaker model (see sound); `DS:47CE/47CF` stay in memory |
| INT 24h (`_harderr`) | 105b | not installed; file errors come back as return codes and use the same messages |
| DOS files: `?:lib1`, `?:lib2`, `?:hot_data`, `?:hall_dat` (lower case, drive letter from `_dos_getdrive`) | 6943, 6089, 6b79, 6d09, 6dd2 | Report drive 3 (C:) so every floppy path is dead; strip `X:` and open in `--game-dir` with a case-insensitive lookup (`HOT_DATA`, `LIB1`…). `hall_dat` is written next to the game data (or a user dir) |
| `_fmalloc` / `_ffree` | 3709, 36d6 | host heap in `mem[]` (segments above the image), never in DGROUP |
| EGA planes as storage (map mask 3C4h/2, read map 3CEh/4) | 3967, 62ba, 65ae | `platform/ega` keeps four 64 K planes: `movedata` into A000 with map mask = one plane writes that plane; reads with read-map-select read it back. Implement `pic_park` faithfully (cheap, and keeps the page-3 overlap behaviour, see 8) |
| `cpu_speed_calibrate` busy loop on `0040:006C` | 05c6 | Return a constant ≥ 4 (e.g. 20) so `DS:82C6 = 0` (full detail); `/* PORT: */`. Optionally a launcher option to force the slow-machine path |
| `time()` for the random seed | 5eb6 | `time(NULL)`; test builds may fix the seed |
| Text-mode exit | 0fea | nothing; exit code 5 is irrelevant |

## 7. Timing

**Tick = 1193182 / 16384 = 72.8236 Hz (13.73 ms).** Programmed once by `platform_install` and restored only at exit;
nothing else touches PIT channel 0 (the sound code only uses channel 2).

Per tick, `timer_isr` (outside driving):

1. `DS:05F8++` (the game clock of everything: waits, animations, timers, `ui_wait` timeouts).
2. Every 4th tick (first on the very first tick): old INT 8 (BIOS) and `DS:0600++` (18.2 Hz).
3. Speaker gate closed if sound is off.
4. Demo: `demo_step` every 16 or 128 ticks; cursor glides 1 px/tick towards the demo target.
5. Music sequencer step (sound spec).
6. While the cursor is shown: held keypad keys and the joystick move the cursor **1 px per tick** in x and y
   (72.8 px/s). The mouse is *not* read here; it is polled by `ui_wait` on every loop iteration.

While driving, `race_isr` instead: tick counter, BIOS chain every 4th tick, engine sound on the other ticks, and every
12th tick (**6.07 Hz**) control sampling + car step (race spec). The race frame loop itself runs in the foreground.

Speed-dependent code (each busy loop must call `host_pump()` once per iteration):

* `ui_wait` (`0000:1417`) is a busy loop: key slot, tick-timer queue, `mouse_poll` (INT 33h) and hotspot tests per
  iteration. Its **palette rotation** runs on *every iteration* while `(DS:05F8 & 0Fh) == 0`, i.e. an arbitrary
  number of times during that one tick (then 15 ticks of pause): the resulting phase depends on CPU speed. Port:
  rotate once per qualifying tick (remember the last tick that rotated) — `/* PORT: */` deviation, documented.
* `mouse_poll` drops the remainder of `|acc| >> 1` when the result is non-zero, so the cursor speed depends on the
  poll rate. With `host_pump` per iteration and relative motion summed by the host, poll the INT 33h emulation as
  the game does; fine as long as the host pumps SDL events at ≥ the frame rate.
* `input_reset`: waits until 10 ticks pass without a key (≥ 137 ms, longer while keys arrive).
* `wait_click_or_key`, `wait_ticks_or_input`, `video_menu` (kbhit), `joy_read` (count loop up to 400h, timed by the
  one-shot hardware), joystick detection (≤ 65535 port reads), `herc_detect`: busy loops; only the tick-based ones
  matter for the port.
* `cpu_speed_calibrate` → `DS:8ACC`; consumers only test `< 4` (race: `0000:d29d`, `d544`, `d67d`; video `2645:3f8d`)
  and `DS:82C6` (animation step `% (DS:82C6*4 + 4)` in race/video). With the port constant ≥ 4 the fast-machine
  behaviour is used.
* Tick timers (`tq_*`) and `ui_wait` timeouts are counted in 72.8 Hz ticks; `wait_ticks_or_input(n)` waits n ticks.

## 8. Open questions

1. **EGA page 3 vs. the parked pictures.** `gfx_init` creates a fourth page at A000:6000 (8000 bytes → up to
   A000:7F3F), but `vram_pool_alloc` parks packed pictures from A000:7D00 in every plane. Rows 185–199 of page 3
   overlap the parked data. Either page 3 is never used at full height, or the original shows/corrupts data there.
   Video must check how page 3 (`2e68:0000`) is used.
2. `tq_cancel_call` only looks at the head of the timer queue (walks `next` = earlier). Intended or a bug that
   matters (a cancelled label timer that still fires)? Faithful port keeps it; game_flow/video should check the
   callers of `198a`/`19bc` (`0f38:1abd`, `0f38:19ec`).
3. `DS:8EF8/8EFA/8EFC` (2, 5, 5) and `DS:64A6` (800h) are set at start; readers not found in the index.
4. `DS:47D8[8]` is cleared with the direction state but no reader was found (maybe the demo recorder).
5. Key code 1Eh (secondary click, `DS:4737`) has no key in the default table; perhaps set by a per-screen remap
   (none of the 41 lists uses it) or by the demo injector `0000:2e93`.
6. The palette rotation in `ui_wait` needs `DS:4BC8` — which screen sets it (juke box / neon sign?) is game_flow's.
7. In `race_isr` the mouse Y shift of 6 means vertical mouse motion hardly counts while driving; which control
   (gas/brake?) it maps to is race's.
8. `DS:0252` (EGA switch settings 1/0Eh) path in `ega_vga_detect` returns before setting `DS:5AF1`; harmless for the
   port.

## Index additions

Code reached only through pointers or not found by `tools/srindex.py`:

| address | how it is reached |
|---|---|
| 0f38:0b2d | driver vector 7 (`DS:5BD0`/`5B8A` slot 7 → `DS:78BE`); called as `lcall [78BE]` |
| 0f38:0b3f | driver vector 8 (`DS:78C2`) |
| 1c11:0f70, 1c11:0f94 | CGA/Hercules driver vectors 7 and 8 (`DS:5B04`) |

New pointer tables (far pointers, 15 × {off, seg} each — `PTR_TABLES` currently only takes near offsets):
`DS:5BD0` (EGA/VGA), `DS:5B04` (CGA/Hercules), `DS:5B8A` (Tandy), all copied to `DS:78A2`. All other targets are
already indexed. The ISRs (`2c42`, `1fe2`, `238c`, `105b`) are already in `HANDLERS`. `0000:2df4` is indexed but
is also an assembly entry point without a caller in its own segment range.
