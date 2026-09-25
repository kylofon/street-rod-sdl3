// game.cpp -- the files the game needs, the data disk, saved games and starting the game.
#include "game.h"

#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <vector>

namespace {

// Whether `dir` holds the file `name` as written, in upper or in lower case (case matters outside
// Windows; the port itself finds files whatever their case).
bool FilePresent(const wxString& dir, const wxString& name) {
    if (dir.empty()) return false;
    for (const wxString& n : {name, name.Upper(), name.Lower()})
        if (wxFileName::FileExists(wxFileName(dir, n).GetFullPath())) return true;
    return false;
}

// The first file the port needs that `dir` lacks (SR.EXE or SRSE.EXE, LIB1, LIB2, HOT_DATA).
wxString Missing(const wxString& dir, bool* se) {
    *se = !FilePresent(dir, "SR.EXE") && FilePresent(dir, "SRSE.EXE");
    if (!*se && !FilePresent(dir, "SR.EXE")) return "SR.EXE";
    for (const char* name : {"LIB1", "LIB2", "HOT_DATA"})
        if (!FilePresent(dir, name)) return name;
    return wxString();
}

}  // namespace

GameFolder ReadGameFolder(const wxString& dir) {
    GameFolder f;
    if (dir.empty()) {
        f.missing = "SR.EXE";
        return f;
    }
    f.missing = Missing(dir, &f.se);
    for (int n = 1; n <= 15; ++n)
        if (FilePresent(dir, wxString::Format("HOTROD%d.SAV", n))) ++f.saves;
    const wxString disk = wxFileName(dir, "datadisk").GetFullPath();
    bool se = false;
    if (wxFileName::DirExists(disk) && Missing(disk, &se).empty()) f.dataDisk = disk;
    return f;
}

wxString LauncherDir() { return wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath(); }

wxString DefaultGameDir() { return wxFileName(LauncherDir(), "Game").GetFullPath(); }

wxString DefaultProgram() {
    wxFileName name(LauncherDir(), "srport");
#ifdef __WXMSW__
    name.SetExt("exe");
#endif
    return name.GetFullPath();
}

bool LaunchGame(const GameOptions& o, wxString& error) {
    if (!wxFileName::FileExists(o.program)) {
        error = wxString::Format("The game's program isn't there:\n\n%s", o.program);
        return false;
    }
    std::vector<wxString> args{o.program,
                               "--game-dir", o.gameDir,
                               "--scale", wxString::Format("%d", o.scale)};
    if (o.fullscreen) args.push_back("--fullscreen");
    // The original's own command-line switches, passed on as the port receives them.
    if (o.noMouse) args.push_back("nomouse");
    if (o.demo) args.push_back("demo");
    if (o.autoDrive) args.push_back("auto");

    std::vector<std::wstring> wide;
    for (const wxString& a : args) wide.push_back(a.ToStdWstring());
    std::vector<const wchar_t*> argv;
    for (const std::wstring& w : wide) argv.push_back(w.c_str());
    argv.push_back(nullptr);

    wxExecuteEnv env;  // an empty variable map: the game inherits the launcher's environment
    env.cwd = wxFileName(o.program).GetPath();
    long pid;
    {
        wxLogNull quiet;  // wxExecute would show its own error box
        pid = wxExecute(argv.data(), wxEXEC_ASYNC, nullptr, &env);
    }
    if (pid == 0) {
        error = wxString::Format("Couldn't start %s.\n\n%s", wxFileName(o.program).GetFullName(),
                                 wxSysErrorMsgStr(wxSysErrorCode()));
        return false;
    }
    return true;
}
