# Porting rules — Street Rod → SDL3

Faithful reimplementation of **SR.EXE**, VGA 16-colour path. Behaviour, timing, integer arithmetic
and visible quirks must match the original. Specs: `../port/spec/*.md` (read `../port/RE_GUIDE.md`
first) and `../FORMATS.md`. Symbols: `../port/symbols.csv` → generated `src/symbols.h`
(`python ../tools/gen_symbols.py` after `merge_symbols.py`). Ground truth when a spec is unclear:
`python ../tools/x86dis.py ../work/SR_unp.exe dis SSSS:OOOO LEN`. The skeleton comes from
`../../TestDrive3/td3port` (same memory model, host and code pointers); the TD2 port
(`../../TestDrive2/td2port`) is the model for EGA planar graphics.

## Architecture

```
main.c        port args (--game-dir ...) + the original's switches, mem_load_exe(), host_init(),
              ega_init(), modules_init(), platform_main_init() (main 0000:066f up to the game
              loop), game_main() (game loop 0000:503f + platform_exit)
mem.h/.c      real-mode memory: SR.EXE image at segment 0x1000, DGROUP 0x4E96, DOS memory above
host.h/.c     SDL3: window/present, PIT-rate tick, 70 Hz retrace clock, XT scan codes, mouse
              (mickeys, relative mode), gamepad, PC-speaker model, files, fatal errors
codeptr.h/.c  far code pointers stored in game data -> C functions
modules.h/.c  hooks from platform/video into subsystems ported separately (race step, demo step,
              UI: tick-timer callback, hotspot test, message box); stand-ins until they land
platform/     the platform and video layers (platform.md, video.md), public API in two headers:
  platform.h    start-up, timer/ISRs, keyboard, UI wait loop, mouse, joystick, memory (far heap,
                bitmaps, VRAM pools, arena), DOS files, picture libraries, HOT_DATA, hall_dat, rnd
  video.h       descriptors, driver slots (drv_*), the 21a0 driver, blit helpers, 0f38 primitives,
                status line, pictures, palette, pages, split screen, mouse pointer
  vga.h/.c      the VGA register model (see below): vga_out/vga_in, vrd/vwr, vmovedata
  ega.h/.c      plane memory, palette registers, scan-out (start address, line compare)
  startup.c     main 0000:066f init, driver_select 0316, video_mode_set 04f4 (no menu: VGA)
  timer.c       timer_isr 1fe2, race_isr 238c (platform part), install/restore/exit, tick timers
  kbd.c         INT 9 2c42, key slot, per-screen remaps, text mode
  input.c       ui_wait 1417, input_reset, wait helpers, INT 33h mouse, joystick 2df4
  dos.c         DOS memory (MCB chain) + MS C far heap (_fmalloc), DOS file handles
  memory.c      far_alloc 3709, bitmap descriptors 376b.., VRAM pools 3967, the arena 0f38:9ee9..
  lib.c         picture libraries 0f38:6016-6f0a, HOT_DATA, hall_dat, rnd 0f38:5eb6
  drv_ega.c     the EGA/VGA driver segment 21a0, the drv_* slot wrappers
  blit.c        helper segments 0e6c, 0e92 (unpacker), 24e7, 24f4, 2595, 2634, 2645 spans
  anim_rect.c   rect / sprite-animation bookkeeping 0f38:a0fa-aac2
  gfx.c         0f38 set-up, palette, lines/rects/frames, page copies, bitmap helpers
  screen.c      0f38 screen_* helpers, status line, full-screen pictures, mouse pointer
sound/        the PC-speaker driver (section Sound)
game/         game code; game/placeholder.c is the stand-in game_main until game_flow lands
```

Owners: `platform/`, `main.c`, `mem.*`, `codeptr.*`, `modules.*` = foundation; `sound/` = sound;
`game/` = game_flow, garage, race (one file set each). Game code uses only `platform/platform.h`,
`platform/video.h`, `sound/sound.h` (and `vga.h` for the few register-level routines outside the
driver, e.g. garage `0f38:8126`/`819a`). Cross-calls the other way go through `modules.h` or direct
includes once both sides exist; the coordinator wires them in `modules.c`.

Only `host.c`, `mem.c`, `main.c` and file-loading code include SDL. Game and platform code talks to
the host through `host.h`.

### API conventions

* Functions carry the names of `../port/symbols.csv` and a `/* SSSS:OOOO */` comment; signatures
  follow the original: `int` = `s16`/`u16`, far pointers `FarPtr`, near pointers to DGROUP data `u16`
  DS offsets (lists like `DS:4C28`, animation records, palette tables). Near pointers the original
  passes to the caller's **stack** (driver `Rect`s, token copies, out-parameters, strings built in a
  buffer) are C pointers; a DGROUP string is `ds_str(off)`.
* Bitmap / page descriptors are 48-byte records in `mem[]` addressed by `FarPtr` (accessors
  `desc_w/h/planes/mask/stride/type` in `video.h`); `g_front()` / `g_back()` are `DS:822A/822E`.

### Driver slots

The game calls the graphics driver only through the 15 far pointers at `DS:78A2`. As in the
original, `ega_install_vectors` (21a0:1128, from `driver_select`) copies the EGA template `DS:5BD0`
there, and `video_register_codeptrs()` registers every target address in `codeptr`. Game code calls
the typed wrappers `drv_blit` (slot 0), `drv_blit_shifted` (1), `drv_blit_masked` (2),
`drv_draw_text` (3), `drv_draw_cursor` (4), `drv_copy_page` (5), `drv_recolour_rect` (6),
`drv_pal_black` (7), `drv_pal_normal` (8), `drv_anim_step` (9), `drv_blinds` (10),
`drv_slide_sprite` (11), `drv_line` (12), `drv_hspan` (13), `drv_fill_rows` (14) wherever the
original has `lcall [DS:78A2+4k]`; each looks up the far pointer currently in the slot and calls its
C function. Direct calls of the original (`lcall 21a0:xxxx`) call the `ega_*` functions directly.

### VGA register model (`platform/vga.h`)

Mode 0Dh (320x200x16, 4 planes of 64 KB, 40 bytes per row) exactly as the card sees it. Every
routine that touches the card is transcribed with `out(port, v)` / `in(port)` and the video memory
accesses `vrd(seg, off)` / `vwr(seg, off, v)` (`vrd16`/`vwr16` for word accesses = two byte cycles,
`vmovedata` for `movedata 1e16:1dc8`). Segments A000h-AFFFh are video memory (linear offset
`(seg - A000h) * 16 + off` modulo 64 KB, so `A200:0000` = offset 2000h); any other segment is
`mem[]`, so the same code runs on RAM bitmaps. Modelled: sequencer map mask, graphics controller
set/reset, enable set/reset, colour compare, function (AND/OR/XOR), read map select, write modes
0/1/2, read modes 0/1, colour don't care, bit mask applied against the **latches**, the four latches
(loaded by every read), CRTC start address and line compare (VGA: 400 scan lines, first shown row
= ceil((L+1)/2)), input status 3DAh from the host's 70 Hz retrace clock. Registers persist between
routines exactly as on the card (several routines rely on values left by others). The attribute
palette (INT 10h AX=1000h/1002h via `int86x`) goes to `ega_set_palette_reg`; the scan-out shows each
pixel through it with the 200-line decoding (bits 0-2 BGR, bit 4 intensity). The picture cache
(`A000:7D00`+, one plane per packed picture) lives in the same plane arrays, so page 3's overlap
with it behaves as on the card.

## Memory model (`mem.h`)

* The original's data stays **in `mem[]` at its original address**: `DSW(DS_x)`, `ds_far(DS_x)`,
  `SEGW(0x389b, off)`. Never shadow game state in C variables that outlive a function.
* Near data pointers are DGROUP offsets (`u16`), far pointers are `FarPtr {off, seg}`.
* Video memory is not in `mem[]` (planar, `platform/ega.h`); segments `A000h`-`AFFFh` in a far pointer
  mean the planes and must go through `vrd`/`vwr` (`platform/vga.h`).
* Fixed-width arithmetic exactly as the original's registers; divisions through `div32_16` & co.
* **DOS memory is modelled in `mem[]`** (`platform/dos.c`): the PSP at `LOAD_SEG - 10h`, the program
  block up to `DGROUP + 1000h` as the MS C start-up leaves it (64 KB DGROUP), free memory up to
  `A000h`, an MCB chain as DOS keeps it. `far_alloc 0000:3709` runs the MS C 5.1 far heap
  (`_fmalloc 1e16:1593`: heap segments from INT 21h 48h, growth with 4Ah, block headers, rover), so
  far pointers (the arena, the packed-picture buffer, resident pictures, bitmaps) land at the same
  segments relative to the load address as in the original.
* Host-side state outside `mem[]`: the VGA registers and planes, DOS file handles (`FILE *`), the
  window. Everything the game reads or writes stays in `mem[]`.

## Timing

* The host calls the tick handler at the PIT rate the game programs (`host_set_pit_divisor`):
  `platform_install 0000:1111` sets 4000h = 72.8 Hz and the port's "INT 8 vector" (the tick handler)
  to `timer_isr 0000:1fe2`; `race_isr_enter/leave` switch it to `race_isr 0000:238c` and back. The
  handlers run inside `host_pump()` (catching up missed ticks), i.e. between two game-code steps
  that pump the host: that is where the game can observe an interrupt. The BIOS INT 8 chained every
  4th tick is modelled as the tick count at `0040:006C`.
* The keyboard ISR `kbd_isr 0000:2c42` receives the XT byte stream from the host at the same points;
  the key slot, direction and click state are in `mem[]`.
* **Every busy-wait loop of the original calls `host_pump()` once per iteration.** Port 3DAh polls map
  to `vga_wait_retrace_edge` / `vga_wait_in_retrace` / `vga_wait_blank_then_retrace`
  (`host_wait_vretrace`: next 70.086 Hz frame, presenting the picture), so page flips, the blinds and
  the sprite slides show their intermediate frames.
* `DS:8ACC` (CPU speed class, `0000:05c6`) is the constant `SR_CPU_SPEED` = 20 (fast machine, race.md
  §7); the slow-machine paths need a value 1..3.
* PORT timing deviations: the `ui_wait` palette rotation runs once per qualifying tick (the original
  repeats it on every loop pass during that tick, CPU dependent).

## Developer aids

`SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy SR_SNAPSHOT_DIR=dir SR_KEYS="2:01"` runs headless,
saves frames and presses Esc after 2 s (see `host.h`). The original's switches follow the port's
options: `srport --game-dir Game nomouse demo`.

## Sound

`src/sound/` ports the PC-speaker driver (`../port/spec/sound.md`; public API and "who calls what"
in `sound/sound.h`). Only the speaker exists (no AdLib/Tandy code in SR.EXE); PIT channel 0 stays at
4000h in both timer ISRs, the sound code only programs channel 2 and port 61h.

* **State in `mem[]`.** Song structures (`DS_bg_song`, `DS_fx_song`), vibrato records and tables,
  `DS_sound_on` (Ctrl), `DS_speaker_on` (gate mirror), `DS_music_*`, siren/squeal flags. Song data is
  read in place: background tune `2e6b:0000`, loop sounds `2f93:0000`–`00FB` (pass `SEG(0x2F93)`, the
  relocated segment, wherever the original pushes a song segment).
* **Hooks for the platform ISRs.** `timer_isr 0000:1fe2`: `snd_timer_gate_check()` after the BIOS
  chain, `snd_music_tick()` after the demo part (`snd_timer_tick()` = both). `race_isr 0000:238c`:
  `snd_race_tick()` on the three non-chain ticks (reads the decremented `DS_chain_count`).
  `race_isr_enter/leave 0000:1aad/1b27` call `snd_race_isr_enter/leave()` for their sound part;
  `platform_install 0000:1111` calls `snd_song_defaults()`; `main` calls `snd_init()` (0f38:7101);
  `platform_exit` calls `spk_off()`. `snd_set_rnd(rnd)` hands the effects the game's `rnd 0f38:5eb6`.
  The functions `0000:0d99 0dd8 1a05 1a7c` (`music_stop`, `music_resume`, `tune_start`,
  `tune_rewind`) live in the sound module although they sit in the platform address range.
* **Host speaker model** (`host.h`, PC-speaker section): `host_pit2_divisor()`, `host_port61()`,
  `host_port61_get()`, `host_busy_wait_ns/us()`. Writes are stamped on an emulated timeline (PIT
  clocks): tick-handler writes at the tick, other writes at the last tick + busy-wait time since.
  Channel 2 is a mode-3 square (new count at the next output transition, restart on a rising gate);
  level = bit 1 && (bit 0 ? OUT2 : 1), integrated exactly per 44.1 kHz sample, ~20 Hz DC blocker,
  ~10 kHz low-pass. `host_speaker(div, on)` remains as a wrapper.
* **Busy-loop effects** (`fx_*`, `delay 2196:0002`) block like the original and advance the
  timeline with `host_busy_wait_ns`; timer ticks that fall inside run at their place (paced to real
  time), so the sequencer / drive ISR change the speaker in the middle of an effect as on the PC.
  Durations: `SND_DELAY_SCALE`, `SND_DELAY_UNIT_NS` (4.47 µs, DOSBox), `SND_TONE_OVERHEAD_NS`,
  `SND_NOISE_OVERHEAD_NS` in `sound.h`; `delay()` keeps the original's loop-count arithmetic
  (including `delay(0)` = 65536 loops). The start-up calibration `0f38:703e` is not measured (PORT).
* **Verification.** `srsnd-dump GAME_DIR OUT_DIR` (target in `CMakeLists.txt`, source
  `tools/snd_dump.c`) runs the module offline through the host model: event lists, per-tick traces,
  `engine.csv` and WAVs of every song, effect, the siren / engine sweep and effects during the drive
  ISR. `python ../tools/snd_compare.py ../work/SR_unp.exe OUT_DIR` checks them against
  `../tools/srsnd.py`: all songs match event for event and tick for tick (vibrato divisor and gate
  each tick), the engine table is identical. `SR_AUDIO_DUMP=file.wav` records the live game's audio.
