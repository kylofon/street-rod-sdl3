# Street Rod — SDL3 port

A faithful C reimplementation of *Street Rod* (1989, California Dreams / Logical Design Works), running natively
on SDL3, in the same way as the [Test Drive (1987)](https://github.com/kylofon/test-drive-sdl3),
[Test Drive II](https://github.com/kylofon/test-drive-2-sdl3) and Test Drive III ports. It is not an emulator:
the original program was reverse-engineered, documented and rewritten in C, and the port reads the original
game's files at run time. The original data is not redistributed, and you need to get it yourself.

## How to play (Windows)

You need the files of the original DOS *Street Rod* (`SR.EXE`, `LIB1`, `LIB2`, `HOT_DATA`), and for the data
disk cars *Street Rod SE* (`SRSE.EXE` with its own `LIB1`, `LIB2`, `HOT_DATA`) in a folder named `datadisk`
inside it, as in the usual GOG and eXoDOS copies. They are not included.

1. Open the [latest release](https://github.com/kylofon/street-rod-sdl3/releases/latest) and download
   `srport-…-win64.zip`.
2. Put your original game files in a folder named `Game` (with `datadisk` inside it, if you have it).
3. Open the zip. Copy all of its files into the folder that holds `Game`, so that `Street Rod.exe` sits next to
   `Game`:

   ```text
   Street Rod\
   ├── Game\                 <- your original game files (SR.EXE, LIB1, LIB2, HOT_DATA, ...)
   │   └── datadisk\         <- Street Rod SE (SRSE.EXE, LIB1, LIB2, HOT_DATA), optional
   ├── Street Rod.exe        <- the launcher
   ├── srport.exe            <- the game
   ├── SDL3.dll
   └── (the other files from the zip)
   ```

4. Double-click `Street Rod.exe`, choose the cars and options, and press **Play**. (`srport.exe` also starts on
   its own, with the original cars in `Game`.)

Keep the folder somewhere you can save files, such as Documents or the Desktop, not Program Files: the game saves
your games (`HOTROD1.SAV` … `HOTROD15.SAV`) and the hall of fame (`hall_dat`) in the folder it runs in. If Windows
says "Windows protected your PC", click **More info**, then **Run anyway**. Press Alt+Enter for full screen. The
keys are listed under [Controls](#controls) and in the launcher.

## Status

* The whole VGA version (320×200, 16 colours) with the PC speaker is ported: the title and credits, the driver's
  licence, the newspaper and its ads, used cars and auto parts, haggling and selling, the garage with the engine
  bay, gearbox, tyres, tune-up, paint and customising, the gas station, Bob's Drive-In with the opponents, bets
  and pink slips, drag and road races, the police and jail, breakdowns and repairs, the calendar of the summer of
  1963, saved games, the hall of fame and the ending.
* It was checked against the original's code and with scripted test runs (title to garage, the ads and buying a
  car, the drive to the gas station, drag and road races with crashes and results, saving and loading), not yet
  frame by frame against the original in DOSBox, and not every screen has been played through. This is a first
  release: please report any difference from the original in the issues.
* The CGA, EGA, Tandy and Hercules graphics are not ported (see `PLAN.md`).

## The launcher

`Street Rod.exe` starts the game with the options chosen in its window (`srport/launcher/README.md`):

* **Game files**: the game folder and `srport.exe` (by default both beside the launcher), with a check that the
  game's files are there and how many saved games the folder has.
* **Cars**: the 25 cars of the Street Rod car data disks (Street Rod SE, the default when the `datadisk` folder
  is there) or the original cars. Each keeps its own saved games, in its own folder.
* **The game's own switches**: *Demo* (the game plays itself; Esc ends it), *Races drive themselves* (the hidden
  `auto` switch of `SR.EXE`) and *No mouse*.
* **Window size** and **full screen**.

Settings are remembered in `%APPDATA%\Street Rod\settings.ini`.

## Requirements

* Your game files in a folder (by default `Game` beside the program): `SR.EXE` (packed or unpacked) or `SRSE.EXE`,
  `LIB1`, `LIB2` and `HOT_DATA`. `LIB1C`, `LIB2C` (the CGA pictures) are not used.
* The folder must be writable (saved games, the hall of fame).
* To build: CMake 3.24+, a C11 compiler and SDL 3; for the launcher a C++17 compiler and wxWidgets 3.2.

## Build

From the repository root, in Git Bash or an MSYS2 MinGW64 shell:

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cmake -S srport -B srport/build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -DSR_LAUNCHER=ON
cmake --build srport/build
```

The build copies `SDL3.dll` with the libraries it loads (`libiconv-2.dll`) and the launcher's wxWidgets DLLs next
to the programs, so they also start outside the MSYS2 shell. Leave out `-DSR_LAUNCHER=ON` to build only the game.

## Run

```bash
./srport/build/srport.exe --game-dir Game
```

| Option | Meaning |
|---|---|
| `--game-dir DIR` | Folder with the original game files (default `Game`); `Game/datadisk` for Street Rod SE |
| `--scale N` | Initial window size as a multiple of 320×240 (default 3) |
| `--fullscreen` | Start in full screen |
| `--check` | Verify that `SR.EXE` / `SRSE.EXE` loads, then exit without opening a window |
| `demo`, `auto`, `nomouse`, `nouemem`, `1` | The original's own switches, passed on as they are: self-running demo, races that drive themselves, no mouse, no pictures cached in video memory, one floppy drive (no effect) |

Alt+Enter toggles full screen. The window keeps the 4:3 aspect of the original monitor. The game captures the
mouse and draws its own pointer; Alt+Tab leaves the window.

## Controls

* **Mouse**: point and click. The arrow keys and the keypad move the pointer too; Space, Ins or keypad 0 click.
* **Races**: Up gas, Down brake, Left / Right steer. Space or Ins shifts (down when braking or off the gas at low
  revs, otherwise up). **A / Z shift up / down** (added by the port). With the mouse: right button gas, moving it
  down brakes, sideways steers, left button shifts. A gamepad works as the joystick.
* **Esc** skips the drive through town and ends the demo. **M** music on / off, **Ctrl** sound on / off.

## Changes from original

* **Added:** A and Z shift up and down in races. The original's single shift control (Space, joystick button,
  mouse button) still works as it did.
* **Removed:** the copy protection (the game runs as after a correct answer), the video mode menu and all disk
  handling (drive letters, disk prompts). Files are found in the game folder whatever their case. `SRSE.EXE` is
  accepted in place of `SR.EXE`.
* **Graphics:** VGA only (BIOS mode 0Dh with the game's palette and the dashboard split screen).
* **Speed:** the game already paces itself by its 72.8 Hz timer (races draw a frame every 9 ticks, about 8 a
  second, and run the car physics every 12th tick), so it plays at its original speed. The start-up CPU speed test
  is replaced by a fixed "fast PC" result.
* **Sound:** the PC speaker is emulated, including the bit-banged noise effects, whose length on the original
  depended on the CPU; the port uses DOSBox timings.
* **Mouse and joystick:** the mouse driver and the analogue joystick are emulated (a gamepad acts as the joystick).
* **Robustness:** a few places where the original read uninitialised memory or could overflow a table are given
  defined values (marked `PORT` in the source).
* Everything else, including the original's quirks, is kept on purpose.

## Layout

* `Game/` — your game files (not in the repository).
* `tools/` — reverse-engineering tools and data decoders (pictures, tables, sound), and `srrun.py`, the headless
  test runner (see `srport/PORTING.md`, "Developer aids").
* `port/` — function index, specs (`port/spec/`) and the guide for writing them (`port/RE_GUIDE.md`).
* `srport/` — the SDL3 port (`srport/PORTING.md`: architecture and rules) and its launcher (`srport/launcher/`).
* `FORMATS.md` — decoded file formats. `PLAN.md` — the plan, decisions and what is parked.

## License

The port, launcher and tools are MIT licensed (see `LICENSE`). *Street Rod* is © California Dreams / Logical
Design Works. Its files are not part of this repository or the releases.

## Support

https://buymeacoffee.com/krzysztofkania
