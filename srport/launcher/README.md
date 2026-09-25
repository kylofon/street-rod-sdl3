# Street Rod (the launcher)

`Street Rod.exe` starts `srport`, the SDL3 port of Street Rod (California Dreams, 1989), with the options chosen
in its window. It is built with [wxWidgets](https://www.wxwidgets.org/) 3.2 from the platform's own controls,
like the Test Drive III launcher it is modelled on.

## The window

* **Game files**
  * **Folder**: the folder with the original game's files (default `Game` beside the launcher).
  * **Program**: `srport.exe` (default: beside the launcher).
  * The line below says whether the folder holds Street Rod or Street Rod SE and how many saved games it has,
    or which file is missing (`SR.EXE` or `SRSE.EXE`, `LIB1`, `LIB2`, `HOT_DATA`). **Play** stays greyed out
    until the folder and the program are there.
* **Start**
  * **Cars**: the original cars, or the 25 cars of the Street Rod car data disks. The second needs a
    `datadisk` folder with Street Rod SE (`SRSE.EXE` and its `LIB1`, `LIB2`, `HOT_DATA`) inside the game folder,
    as in the usual GOG/eXoDOS copies; the game then runs in that folder and keeps its own saved games there.
  * The original's own command-line switches: **Demo** (`demo`: the game plays itself, Esc ends it), **Races
    drive themselves** (`auto`) and **No mouse** (`nomouse`).
* **Options**: **Window size** (`--scale`) and **Start in full screen** (`--fullscreen`; Alt+Enter switches).
* **Keys in the game**: a reminder of the game's keys, including the port's A / Z gear keys.
* **Play** starts the game; the launcher stays open. **About**: version, author and links.

Everything is remembered in `%APPDATA%\Street Rod\settings.ini` (`~/.config/street-rod` on Linux). A folder or
program left at its default is stored empty, so it follows the launcher if the whole folder moves. Delete the
file to go back to the defaults.

## Building

Needs CMake 3.24, a C++17 compiler and wxWidgets 3.2 (MSYS2 `mingw64`: `mingw-w64-x86_64-wxwidgets3.2-msw`;
Debian and Ubuntu: `libwxgtk3.2-dev`). To build it beside `srport.exe`, add `-DSR_LAUNCHER=ON` when configuring
the port:

```bash
cmake -S srport -B srport/build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -DSR_LAUNCHER=ON
cmake --build srport/build
```

It also builds on its own (`cmake -S launcher -B launcher/build -G Ninja`); then choose the program in the
window or copy the launcher next to it.

On Windows the build copies every DLL the launcher and the game need beside them (`copy_dlls.cmake`): the two
wxWidgets DLLs and the MSYS2 libraries they load, and `libiconv-2.dll` for `SDL3.dll`. Keep them with the
`.exe` files in a release. The C++ runtime of the launcher itself is linked in.

## Files

* `app.cpp`: the wxWidgets application.
* `launcher.h`, `launcher.cpp`: the window and the About box.
* `game.h`, `game.cpp`: the file checks, the data disk, saved games and starting the game.
* `settings.h`, `settings.cpp`: `settings.ini`.
* `icon.h`, `icon.cpp`: the app icon, Street Rod's night road drawn in code. `make_icon.py` (Pillow) writes the
  same drawing to `app.ico` for Explorer.
* `app.rc`, `app.manifest`, `app.ico`, `version.h`: icon, visual styles, DPI awareness, version info.
* `copy_dlls.cmake`: the post-build DLL copy (also used by the game's build).
* `CMakeLists.txt`: the build, standalone or from the main project.
