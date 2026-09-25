// launcher.h -- the launcher window: choose the game folder and the cars (the original ones or the
// Street Rod SE data disk's), the game's own switches and the display options, then Play.
#pragma once

#include <wx/dialog.h>

#include "game.h"

class wxButton;
class wxCheckBox;
class wxChoice;
class wxStaticBitmap;
class wxStaticText;
class wxTextCtrl;

extern const char* const APP_TITLE;

class LauncherDialog : public wxDialog {
public:
    LauncherDialog();

private:
    // Checks the folder and the program again and enables Play and the controls to match.
    void UpdateState();
    void BrowseFolder();
    void BrowseProgram();
    void Play();
    void About();
    void Save();
    // The folder the game runs in: the chosen folder, or its datadisk folder for the SE cars.
    wxString PlayFolder() const;

    wxTextCtrl* folder_ = nullptr;
    wxTextCtrl* program_ = nullptr;
    wxStaticBitmap* statusIcon_ = nullptr;
    wxStaticText* statusNote_ = nullptr;
    wxChoice* cars_ = nullptr;     // the original cars / the data disk's
    wxStaticText* carsNote_ = nullptr;
    wxCheckBox* demo_ = nullptr;
    wxCheckBox* autoDrive_ = nullptr;
    wxCheckBox* noMouse_ = nullptr;
    wxChoice* scale_ = nullptr;
    wxCheckBox* fullscreen_ = nullptr;
    wxButton* play_ = nullptr;

    GameFolder game_;
    bool loading_ = true;  // no edits are recorded while the window is built
};
