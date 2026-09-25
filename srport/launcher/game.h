// game.h -- what a game folder holds (Street Rod, or the Street Rod SE data disk in its datadisk
// folder), where srport is, and starting it.
#pragma once

#include <wx/string.h>

// A game folder as the launcher sees it.
struct GameFolder {
    wxString missing;       // the first of the game's files that isn't there, empty if all are
    bool se = false;        // the program is SRSE.EXE (Street Rod SE, the data-disk edition)
    int saves = 0;          // saved games (HOTROD1..15.SAV)
    wxString dataDisk;      // <folder>/datadisk when it holds a complete Street Rod SE, else empty
};

GameFolder ReadGameFolder(const wxString& dir);

// The folder the launcher runs from.
wxString LauncherDir();

// Where things are by default: "Game" and srport(.exe) beside the launcher.
wxString DefaultGameDir();
wxString DefaultProgram();

struct GameOptions {
    wxString program;       // srport(.exe)
    wxString gameDir;       // --game-dir
    bool demo = false;      // the game's "demo" switch: it plays itself
    bool autoDrive = false; // the game's "auto" switch: races drive themselves
    bool noMouse = false;   // the game's "nomouse" switch
    int scale = 3;          // --scale: the window is 320x240 times this
    bool fullscreen = false;  // --fullscreen
};

// Starts the game. On failure returns false and says why in `error`.
bool LaunchGame(const GameOptions& options, wxString& error);
