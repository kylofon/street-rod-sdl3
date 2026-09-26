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
| `HOT_DATA` | 4 116 | 13 blocks: opponents (22 × 18 bytes, block 0) and more (game_flow spec); 52 trailing bytes never read |
| `datadisk/` | | *Street Rod SE* (2014 fan edition): `SRSE.EXE` = SR.EXE with the protection routine returning 1 and CGA/Hercules disabled (41 bytes); its `LIB1`, `LIB2`, `HOT_DATA` carry 25 replacement cars. The shipped `SR.EXE` is itself cracked another way (the call at 0000:3a1a jumped over, the check at 314e NOPed) |

Picture libraries: decoded, see `FORMATS.md` (`tools/srlib.py`). Data tables: `tools/srtables.py`.

Video modes in the exe menu: CGA 4 colours, EGA 16, **VGA 320×200×16**, Hercules, Tandy 16.
The VGA mode is BIOS mode 0Dh with the game's EGA palette registers (INT 10h AX=1002h, no DAC);
it differs from EGA only in the dashboard split screen. Sound: **PC speaker only**: a tick-driven
music sequencer, the engine note and bit-banged noise effects, no digitised sound
(`port/spec/sound.md`). Timer 72.8 Hz; the race runs a car step in the timer ISR every 12th tick
(6.07 Hz). Switches: `nomouse`, `demo` (self-playing attract mode), `auto` (races drive
themselves), `nouemem` (no picture parking in video memory).

## Phases

### 0. Repository — done (2026-09-24)
- `C:\Coding\StreetRod`, git `master`, no remote (GitHub later, on request).
- `Game/` original data (git-ignored; the port reads it at runtime); `StrtRods/` is the untouched
  copy as received.
- `_tools` → junction to `TestDrive1987/_tools` (Ghidra 12.1.3, JDK 21).
- Tools carried over from TD3: `unexepack.py`, `x86dis.py`, `srindex.py` (was `td3index.py`),
  `tdmatch.py` (generalised `td2match.py`), `merge_symbols.py`, `gen_symbols.py`, Ghidra scripts.

### 1. Executable map — done (2026-09-24, see `port/RE_GUIDE.md`)
- Unpack, index (`tools/srindex.py`), match against TD1/TD2/TD3 (runtime only).
- Ghidra project in `_ghidra/`, `SetDS` + `DecompileAll` → `port/decomp/` (ignored).
- Segment map, main state machine, graphics library dispatch, interrupt handlers.
- Output: `port/RE_GUIDE.md`, `port/symbols.csv`.

### 2. File formats — started (LIB1/LIB2 decoded, `FORMATS.md`)
Each format gets a decoder that dumps to `work/` plus an image / contact sheet:
1. `LIB1` / `LIB2` directory and decompressor → every picture as PNG (EGA/VGA 16-colour set first).
2. Palettes / colour maps (the 16-byte map per picture; VGA DAC values in the exe).
3. `HOT_DATA`.
4. Tables inside the exe: car list, parts catalogue (prices, fits), opponents, map of the town,
   race tracks, texts.
5. Save games (`hall_dat`?) and the high-score / "King of the County" file.

### 3. Specs (`port/spec/`) — done (2026-09-24)
`platform`, `video`, `sound`, `game_flow`, `garage`, `race` + `race_render`; symbols merged into
`port/symbols.csv` (978 names), `srport/src/symbols.h` generated.
Split by call tree once the map is known; expected:
- `platform` — startup, video-mode menu, timer/IRQ, keyboard, mouse, joystick, file I/O, memory
- `video` — the graphics library (VGA 16-colour path first), blitters, text, palette
- `game_flow` — title, copy protection (dropped), save/load, the summer calendar, money, endings
- `garage` — the garage, used cars, auto parts, newspaper, painting/customising, tuning
- `race` — the town drive (Bob's drive-in, finding opponents), drag race and road race: road
  renderer, cockpit/dashboard, car physics, gear box, damage, police, opponent AI
- `sound` — the sound driver(s) and music data

### 4. `srport/` skeleton — done (2026-09-24)
A placeholder `game_main` shows LIB1 #0 (the title) through the C unpacker and the planar model.
CMake + SDL3, adapted from `td3port`: EXE loader (EXEPACK in C), `mem.h` memory model, host
(timer, retrace, XT scancodes, mouse, gamepad, audio), video model for the chosen mode, code
pointers, generated `symbols.h`, `PORTING.md`. Headless test path (`SDL_VIDEO_DRIVER=dummy`,
snapshot dir, scripted keys).

### 5. Port, subsystem by subsystem — first full integration running (2026-09-25)
All subsystems ported (platform, video, sound, game_flow, garage + UI toolkit, race); the whole
game links warning-free. Headless demo mode (`srport --game-dir Game demo`) runs title → licence →
newspaper → used cars → garage → drive → gas station → garage → engine bay. Next: compare with
DOSBox captures, play-test every screen, fix differences.
platform/video → title and garage screens (the mouse-driven menus: a visible milestone) → shops,
newspaper, save/load → drag race → road race / town → sound. Each step checked against DOSBox
captures of the original (`DOSBOX/`, ignored) and headless snapshots.

### 6. Release, then (optionally) Street Rod Enhanced — v0.1.0 prepared (2026-09-26)
`release/` (ignored): `srport-v0.1.0-win64.zip` (programs, DLLs, README, LICENSE, `licenses/`),
`RELEASE_NOTES.md`, `SHA256SUMS.txt`. Not published: no remote yet.
Release like TD2/TD3 (zip + launcher). Enhanced ideas later.

## Decisions (2026-09-24, provisional — the user can overrule)

1. **Graphics**: the VGA 16-colour mode first (the best the game offers). EGA / CGA / Tandy /
   Hercules → TODO.
2. **Target executable**: `SR.EXE` with the original `LIB1`/`LIB2`/`HOT_DATA`. The data-disk cars
   (`datadisk/`) should work by pointing `--game-dir` at that folder, since `SRSE.EXE` differs only
   in the protection and the menu text.
3. **Copy protection** (the car-key colour question): bypassed in the port with a `/* PORT: */` note.
4. **Launcher**: wxWidgets like TD3 — done (2026-09-25, `srport/launcher`, `-DSR_LAUNCHER=ON`): game
   folder, original or data-disk cars, the game's switches, window options.
5. **A / Z gear keys** in races, on by default (port addition, `srport/PORTING.md`).

## TODO (parked)

- [ ] EGA, CGA, Tandy, Hercules builds
- [ ] sound devices other than the first one ported
