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
main.c        args, mem_load_exe(), host_init(), ega_init(), game_main()
mem.h/.c      real-mode memory: SR.EXE image at segment 0x1000, DGROUP 0x4E96, heap above
host.h/.c     SDL3: window/present, PIT-rate tick (72.8 Hz default), XT scan codes, mouse,
              gamepad, PC speaker, files, fatal errors
codeptr.h/.c  far code pointers stored in game data -> C functions
platform/ega.*  four 64 KB planes, CRTC start and line compare, scan-out with the EGA palette
platform/pic.*  picture libraries: the unpacker 0e92:0006
game/placeholder.c  skeleton stand-in: shows LIB1 #0, Esc quits (replaced by game_flow)
```

Only `host.c`, `mem.c`, `main.c` and file-loading code include SDL. Game and platform code talks to
the host through `host.h`.

## Memory model (`mem.h`)

* The original's data stays **in `mem[]` at its original address**: `DSW(DS_x)`, `ds_far(DS_x)`,
  `SEGW(0x389b, off)`. Never shadow game state in C variables that outlive a function.
* Near data pointers are DGROUP offsets (`u16`), far pointers are `FarPtr {off, seg}`.
* Video memory is not in `mem[]` (planar, `platform/ega.h`); segment `A000h` in a far pointer means
  the EGA planes and must go through the graphics module.
* Fixed-width arithmetic exactly as the original's registers; divisions through `div32_16` & co.

## Timing

* The host calls the tick handler at the PIT rate the game programs (`host_set_pit_divisor`,
  4000h = 72.8 Hz from `0000:1111`; the sound code reprograms it).
* **Every busy-wait loop of the original calls `host_pump()` once per iteration.**

## Developer aids

`SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy SR_SNAPSHOT_DIR=dir SR_KEYS="2:01"` runs headless,
saves frames and presses Esc after 2 s (see `host.h`).
