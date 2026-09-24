# Street Rod — SDL3 port plan

Same approach as TestDrive1987 / TestDrive2 / TestDrive3: first a **faithful C port** here
(reverse-engineer → document → port function by function against the original data, read at
runtime), then, if wanted, a separate **Street Rod Enhanced** repo built on it.

Street Rod (1989, P.Z.Y. / California Dreams, published by Logical Design Works) looks like the
TD1/TD2 engine (cockpit view, road drawn in bands, 320×200), but the code is not shared: matching
every function against the TD1, TD2 and TD3 indexes (`tools/tdmatch.py`) finds only the Microsoft C
5.1 runtime (`port/sr_td2_matches.csv`). The earlier repos are useful for workflow, tooling and the
port skeleton (loader, `mem.h`, host, code pointers), not for names.

## What we have (recon, 2026-09-24)

| File | Size | Notes |
|---|---|---|
| `SR.EXE` | 259 785 | EXEPACK; `tools/unexepack.py` → 284 048-byte image, entry 1e16:001e, 2218 relocs, DGROUP 3E96. "MS Run-Time Library (c) 1988" → MSC 5.1, 31 code segments, 764 functions indexed |
| `LIB1` | 76 662 | image library, 9 entries (full screens 320×200 and smaller), 4 bits per pixel, compressed |
| `LIB2` | 338 997 | image library, 268 entries (cars, parts, people, dashboard …) |
| `LIB1C`, `LIB2C` | 23 K / 334 K | the same libraries at 2 bits per pixel (CGA) |
| `HOT_DATA` | 4 116 | fixed-size records: probably the clickable hot spots of the screens, or the opponents' cars |
| `datadisk/` | | *Street Rod SE* (2014 fan edition): `SRSE.EXE` = SR.EXE with the protection check jumped over and CGA/Hercules disabled (41 bytes); its `LIB1`, `LIB2`, `HOT_DATA` carry 25 replacement cars |

Library directory (first pass): `u16 count`, then `count` × 30-byte records:
`u16 width, u16 height, u16 raw_size (= w×h/2, /4 for CGA), u8 ?, u8 colour_map[16], u8 0,
u32 offset, u16 packed_size`; the first record has no offset/size prefix (see `FORMATS.md` once decoded).

Video modes in the exe menu: CGA 4 colours, EGA 16, **VGA 320×200×16**, Hercules, Tandy 16.
Sound (to confirm): PC speaker, probably Tandy / AdLib — see the sound spec.

## Phases

### 0. Repository — done (2026-09-24)
- `C:\Coding\StreetRod`, git `master`, no remote (GitHub later, on request).
- `Game/` original data (git-ignored; the port reads it at runtime); `StrtRods/` is the untouched
  copy as received.
- `_tools` → junction to `TestDrive1987/_tools` (Ghidra 12.1.3, JDK 21).
- Tools carried over from TD3: `unexepack.py`, `x86dis.py`, `srindex.py` (was `td3index.py`),
  `tdmatch.py` (generalised `td2match.py`), `merge_symbols.py`, `gen_symbols.py`, Ghidra scripts.

### 1. Executable map � done (2026-09-24, see `port/RE_GUIDE.md`)
- Unpack, index (`tools/srindex.py`), match against TD1/TD2/TD3 (runtime only).
- Ghidra project in `_ghidra/`, `SetDS` + `DecompileAll` → `port/decomp/` (ignored).
- Segment map, main state machine, graphics library dispatch, interrupt handlers.
- Output: `port/RE_GUIDE.md`, `port/symbols.csv`.

### 2. File formats � started (LIB1/LIB2 decoded, `FORMATS.md`)
Each format gets a decoder that dumps to `work/` plus an image / contact sheet:
1. `LIB1` / `LIB2` directory and decompressor → every picture as PNG (EGA/VGA 16-colour set first).
2. Palettes / colour maps (the 16-byte map per picture; VGA DAC values in the exe).
3. `HOT_DATA`.
4. Tables inside the exe: car list, parts catalogue (prices, fits), opponents, map of the town,
   race tracks, texts.
5. Save games (`hall_dat`?) and the high-score / "King of the County" file.

### 3. Specs (`port/spec/`)
Split by call tree once the map is known; expected:
- `platform` — startup, video-mode menu, timer/IRQ, keyboard, mouse, joystick, file I/O, memory
- `video` — the graphics library (VGA 16-colour path first), blitters, text, palette
- `game_flow` — title, copy protection (dropped), save/load, the summer calendar, money, endings
- `garage` — the garage, used cars, auto parts, newspaper, painting/customising, tuning
- `race` — the town drive (Bob's drive-in, finding opponents), drag race and road race: road
  renderer, cockpit/dashboard, car physics, gear box, damage, police, opponent AI
- `sound` — the sound driver(s) and music data

### 4. `srport/` skeleton � done (2026-09-24)
A placeholder `game_main` shows LIB1 #0 (the title) through the C unpacker and the planar model.
CMake + SDL3, adapted from `td3port`: EXE loader (EXEPACK in C), `mem.h` memory model, host
(timer, retrace, XT scancodes, mouse, gamepad, audio), video model for the chosen mode, code
pointers, generated `symbols.h`, `PORTING.md`. Headless test path (`SDL_VIDEO_DRIVER=dummy`,
snapshot dir, scripted keys).

### 5. Port, subsystem by subsystem
platform/video → title and garage screens (the mouse-driven menus: a visible milestone) → shops,
newspaper, save/load → drag race → road race / town → sound. Each step checked against DOSBox
captures of the original (`DOSBOX/`, ignored) and headless snapshots.

### 6. Release, then (optionally) Street Rod Enhanced
Release like TD2/TD3 (zip + launcher). Enhanced ideas later.

## Decisions (2026-09-24, provisional — the user can overrule)

1. **Graphics**: the VGA 16-colour mode first (the best the game offers). EGA / CGA / Tandy /
   Hercules → TODO.
2. **Target executable**: `SR.EXE` with the original `LIB1`/`LIB2`/`HOT_DATA`. The data-disk cars
   (`datadisk/`) should work by pointing `--game-dir` at that folder, since `SRSE.EXE` differs only
   in the protection and the menu text.
3. **Copy protection** (the car-key colour question): bypassed in the port with a `/* PORT: */` note.
4. **Launcher**: wxWidgets like TD2 Enhanced / TD3, once the game runs.

## TODO (parked)

- [ ] EGA, CGA, Tandy, Hercules builds
- [ ] sound devices other than the first one ported
