# Reverse-engineering guide for the SDL3 port

Shared conventions for everyone writing port specs. Target: **SR.EXE** (Street Rod, 1989), the
**VGA 16-colour path** (menu choice 3: EGA/VGA driver `21a0` with `DS:0254 = 1`). CGA, Tandy,
Hercules and plain EGA are parked (PLAN.md, TODO).

Street Rod shares no game or assembly code with Test Drive 1/2/3: only the Microsoft C 5.1 runtime
matches (`port/sr_td2_matches.csv`, `sr_td1_matches.csv`, `sr_td3_matches.csv`). The TD repos
(`../TestDrive1987`, `../TestDrive2`, `../TestDrive3`) are references for workflow, tooling and the
port skeleton (`../TestDrive3/td3port`), not for names.

## Files

| Path | What |
|---|---|
| `work/SR_unp.exe` | EXEPACK-unpacked MZ (`tools/unexepack.py Game/SR.EXE work/SR_unp.exe`); image offsets exclude the MZ header |
| `work/SRSE_unp.exe` | Street Rod SE (`Game/datadisk/SRSE.EXE`): same program, 41 bytes differ (protection skipped at `0000:30b5`, CGA/Hercules menu lines "Not Avail.") |
| `port/sr_functions.json` / `.csv` | Capstone index (`tools/srindex.py`): extent, near/far, callers/callees, DS reads/writes, strings, ints, ports, jump tables |
| `port/decomp/sr_ds.c` | Ghidra decompilation of every indexed function, names from `port/symbols.csv`, DGROUP globals renamed to DS offsets |
| `port/decomp/sr_globals_xref.txt` | For each DS global: which functions use it |
| `port/symbols.csv`, `port/symbol_conflicts.txt` | Merged symbols of all specs (`tools/merge_symbols.py`) |
| `tools/x86dis.py work/SR_unp.exe dis SSSS:OOOO LEN` | Ground-truth disassembly when the decompile looks wrong |
| `tools/srlib.py` | Picture libraries `LIB1`/`LIB2` → PNG (`FORMATS.md`) |
| `FORMATS.md` | Decoded file formats |
| `_ghidra/SR.gpr` | Ghidra project (`_tools/ghidra_12.1.3_PUBLIC/ghidraRun.bat`, JDK in `_tools/jdk-21*`) |
| `Game/` | Original game files (`Game/datadisk/` = Street Rod SE with the car data disks) |

## Addresses

* Microsoft C 5.1 medium model: 31 code segments (from the relocation table), far calls between
  them, near calls inside a segment.
* Write code addresses as **`SSSS:OOOO`** with the segment as stored in the file: `0f38:62ba`.
  Image offset = SSSS×16 + OOOO (`0x1563A`). Ghidra loads the image at segment `1000`, so the same
  function is `FUN_1f38_62ba` in Ghidra; `port/decomp/sr_ds.c` has names rewritten back.
* **DGROUP** is segment `0x3E96` (image `0x3E960`). `DS:xxxx` = image `0x3E960 + xxxx`.
* Segments `2fa6`–`389b` hold no code (data far segments: picture directory `389b`, etc.).

## Segment map (first pass, to be refined by the specs)

| Segment(s) | Size | Contents |
|---|---|---|
| `0000:0000`–`0659` | 1.6 K | startup: video-mode menu (`0000:0082`, "Recommended mode"), detection (`0000:0226`, `e569`–`e6c0`), driver selection `0000:0316` (menu 1 CGA, 2 EGA, 3 VGA, 4 Hercules, 5 Tandy) |
| `0000:066f` | | `main`: switches `nomouse` (DS:0262=0), `demo` (DS:8BCE), `auto` (DS:0612), `nouemem` (DS:0280); libraries `0f38:6943`, title, then the game loop `0000:503f` |
| `0000:0d58`–`2df4` | 8 K | timer, PC-speaker sound (ports 42h/43h/61h), keyboard, INT 24h handler (`0000:105b`), BIOS mode set `0000:2bc0`/`2be2` |
| `0000:2e93`–`3c8d` | | game flow helpers, **copy protection** `0000:2fc8` ("What is the color of the car key on page %d?"; skipped in SRSE at `0000:30b5`), memory (`0000:3709` far alloc, `36e9` "Not enough memory") |
| `0000:3c8d`–`8d26` | 20 K | the garage and the town screens: used cars, auto parts, selling/haggling ("How about $%d ?"), newspaper (`%s %d, 1963`), painting and customising, tuning, save/load (`:HOTROD.SAV`, `0000:59f6`/`5b4a`/`5eea`) |
| `0000:8d26`–`e569` | 22 K | driving: cruising, Bob's Drive-In, challenges, drag and road race (start `8ea8`, per-frame update `da25`), police and jail (`b08c`), gas station (`b8a1`), results and damage (`c613`), physics set-up and top speed (`e0e2`, `e218`); `79eb`/`7aa4`/`7fb1`/`81ab` are garage screens (race.md) |
| `0e6c`, `0e92`, `2462`–`2634` | small | blit helpers: picture unpacker `0e92:0006`, planar blits, masks |
| `0f38` | 52 K | UI library: text and fonts, buttons/menus, mouse and keyboard input, picture libraries (`6016`–`6f0a`), `hall_dat` (hall of fame?), sound effects (`6f0a`–`7700`), dialogs (`7b22`, `be93`) |
| `1c11` | 8 K | Hercules driver (menu 4, `DS:8236 = -4`) |
| `1e16` | 14 K | MS C 5.1 runtime (entry `1e16:001e` `_astart`) incl. floating-point emulator stubs |
| `21a0` | 11 K | **EGA/VGA driver** (menu 2/3, `DS:8236 = -2`; init `21a0:1128`), CRTC split screen `21a0:0014` |
| `2645` | 23 K | race: track builder, road/scenery/opponent/mirror renderer, movement along the road, collisions (`race_render.md`); also the EGA span routines used as driver slots 13/14 |
| `2beb` | 9 K | MS C floating-point emulator (runtime) |

Driver calls go through 15 far pointers at `DS:78A2` (copied from `DS:5BD0` for EGA/VGA; video.md
4.1). `DS:8236` = active driver: `-2` EGA/VGA, `-3` CGA?, `-4` Hercules, `-6` Tandy. `DS:0254` = 1 on VGA
(the split-screen code uses the doubled scan-line count). The palette is set with INT 10h AX=1002h
(`0f38:1fa4`, game palette DS:0440; FORMATS.md "Colours").

Functions with no direct callers (68% code coverage) are mostly reached through the driver vector
tables filled by `0000:0316` in the segment at `DS:68E2` (`es:[0x24]` …). Record new pointer
tables in `tools/srindex.py` (`PTR_TABLES`).

## Subsystem split (phase 3 specs)

| Spec file | Code | Scope |
|---|---|---|
| `platform` | `0000:0000`–`066f` startup, `0000:0d58`–`2df4`, `0000:3709`–`3ab8` memory, `0000:e569`–`e6c0`; `0f38` input / file / library loader (`6016`–`6f0a`); `1e16` (identify only) | startup, mode menu and detection, timer/IRQ, keyboard, mouse (INT 33h), joystick?, files, memory, the picture libraries |
| `video` | `21a0` (VGA path), `0e6c`, `0e92`, `2462`–`2634`, `0f38` drawing and text primitives | the driver interface (vector table), pages, blits, masks, fonts, text, split screen, anything that touches video memory |
| `sound` | `0000` speaker routines, `0f38:6f0a`–`7700` | PC-speaker effects and music, engine sound |
| `game_flow` | `0000:066f`, `0000:2e93`–`3c8d`, `0000:503f`, save/load `59f6`–`641e`, ending | main loop and screen dispatch, calendar / money / win and lose conditions, save games and `hall_dat`, copy protection (briefly: dropped), demo mode |
| `garage` | `0000:3c8d`–`8d26` except save/load | garage, used cars, auto parts, haggling, newspaper, paint/customise, tuning, parts wear, the car and parts data tables |
| `race` | `0000:8d26`–`e569`, `2645` | cruising in town, Bob's, opponents and challenges, drag and road race: road renderer, dashboard, physics, gears, damage, police, AI, results |

If a function clearly belongs to another subsystem, list it with a "see `<spec>`" note instead of
analysing it in depth.

## Deliverable format (one file per subsystem: `port/spec/<subsystem>.md`)

1. **Overview** – what the subsystem does, with a call graph of its main functions.
2. **Function table** – `address | proposed name | signature | one-line purpose | confidence`
   (`verified`, `likely`, `guess`).
3. **Globals table** – `DS offset | proposed name | type/size | meaning | written by | read by`.
4. **Pseudocode** – clean C for every non-trivial function, faithful to the original arithmetic
   (integer widths, signedness, shifts, overflow, long helpers). Original constants in hex.
5. **File formats** – every data file or in-exe table the subsystem reads or writes.
6. **Hardware/DOS dependencies** – each interrupt, port, BIOS call, direct video memory access, with
   the SDL3 replacement.
7. **Timing** – what runs per frame vs per timer tick, tick rates, anything speed dependent.
8. **Open questions**.

Also write `port/spec/<subsystem>_symbols.csv` with `kind,address,name,type,notes` (`kind` = `func`
or `global`; addresses as `0f38:62ba` and `DS:8236`).

## Regenerating

```
python tools/unexepack.py Game/SR.EXE work/SR_unp.exe
python tools/srindex.py work/SR_unp.exe port/sr
python tools/tdmatch.py ../TestDrive2 work/TD2EGA_unp.exe port/td2ega_functions.json td2
python tools/merge_symbols.py
tr -d '\r' < port/symbols_ghidra.txt > work/symbols_ghidra.txt
tr -d '\r' < port/sr_starts.txt > work/sr_starts.txt
analyzeHeadless _ghidra SR -import work/SR_unp.exe -overwrite -scriptPath tools/ghidra \
    -preScript SetDS.java 3E96 -postScript FixNearFlows.java 3E96 \
    -postScript ApplySymbols.java work/symbols_ghidra.txt 3E96 \
    -postScript DecompileAll.java work/sr_starts.txt port/decomp/sr.c 3E96 120 only
python tools/ghidra/postprocess.py port/decomp/sr.c 3E96
```

(`JAVA_HOME` = `_tools/jdk-21.0.12.1+1`; `analyzeHeadless` is in `_tools/ghidra_12.1.3_PUBLIC/support/`.)
