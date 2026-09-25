// app.cpp -- the Street Rod launcher: the game folder, the cars, the game's switches and the
// display options, then srport.
#include <wx/app.h>

#include "launcher.h"

class LauncherApp : public wxApp {
public:
    bool OnInit() override {
        SetAppName("Street Rod");
        SetVendorName("Krzysztof Kania");
        if (!wxApp::OnInit()) return false;
        auto* dialog = new LauncherDialog;
        SetTopWindow(dialog);
        dialog->Show();
        return true;
    }
};

wxIMPLEMENT_APP(LauncherApp);
