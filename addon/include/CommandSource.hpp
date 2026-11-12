#pragma once

#include "BinSchemeReader.hpp"
#include "MenuLink.hpp"
#include "SearchEngine.hpp"
#include <windows.h>
#include <set>
#include <string>
#include <vector>

namespace Baaz {

// The Win32 menu bar of a frame window. Menus are filled the way Windows does before showing
// them: WM_INITMENU once, then WM_INITMENUPOPUP for each level, top-down.
class LiveMenu : public MenuSource {
public:
    explicit LiveMenu(HWND frame);
    std::vector<std::wstring> TopLevelTitles() override;
    std::vector<LiveEntry> Entries(const std::vector<int>& path) override;

private:
    HMENU MenuBar();
    HMENU FilledMenu(const std::vector<int>& path);

    HWND m_frame;
    bool m_barInitialized = false;
    std::set<std::vector<int>> m_filled;
};

// What Baaz can say about Archicad's menu commands right now
enum class CommandState {
    Ready,         // every linked command has a name
    NoExport,      // the scheme of the profile in use is usable, but no export that fits gives names: no commands
    NewCommands,   // some linked menu commands have no name (added to the menus since the export), so they are not offered; a new export names them
    Unreadable     // no commands: the profile or its scheme file is missing, ambiguous or not understood,
                   // or the scheme does not fit the live menu bar (with a fitting export: or links nothing)
};

struct CommandIndex {
    std::vector<SearchItem> commands;   // named, linked commands only
    CommandState state = CommandState::Unreadable;
    size_t notSearchable = 0;           // NewCommands: linked items without a name; a new export can name them
    SearchItem workEnvironment;         // kind Command, position of 7682; liveId 0 if not found
};

// Archicad's menu commands. The menu structure comes from the command scheme Archicad keeps current
// (a .bin file), the names from the user's newest fitting export of the work environment.
class CommandSource {
public:
    // Re-reads the scheme files, the profile that names the scheme in use and the exports only when
    // their path, write time or size changed. The structure comes only from the scheme file whose GUID
    // the profile in use names; if there is not exactly one such readable file, or it fits no top-level
    // menu of the live menu bar, nothing is offered (never another scheme file). The names come from the
    // newest export whose top-level menus fit the live menu bar; commands of add-ons that this export
    // does not list are left out of the structure first.
    static CommandIndex BuildCommandIndex(HWND frame, const std::vector<DynamicList>& lists);
    // Fills the item's menu path again and posts WM_COMMAND only if the live item is still an
    // enabled item with the same id.
    static bool Run(const SearchItem& item, HWND frame);

    // A file read from disk, kept until its path, write time or size changes
    struct CachedFile {
        std::wstring path;
        FILETIME time = {};
        unsigned long long size = 0;
        bool readFailed = false;   // too large or unreadable: read again at the next refresh
    };

    struct CachedExport : CachedFile {
        std::vector<MenuNode> bar;
    };

    struct CachedScheme : CachedFile {
        bool readable = false;     // the reader accepted the file
        BinScheme scheme;
    };

    struct CachedProfile : CachedFile {
        std::string schemeGuid;    // command scheme the profile names; empty if it names none or several
    };

    // Where the exports, the scheme files Archicad keeps and the running program are
    struct Locations {
        std::wstring commands;
        std::wstring graphisoft;
        std::wstring modulePath;
    };

#ifndef ACAPI_VERSION
    // Replaces the commands folder, %APPDATA% and the path of the running program, and forgets
    // everything read so far. The default module path is that of an Archicad 29 installation.
    static void SetFoldersForTest(std::wstring commands, std::wstring appData,
                                  std::wstring modulePath = L"C:\\Test\\Archicad 29\\ARCHICAD.exe");
#endif

private:
    static Locations CurrentLocations();
    static std::vector<CachedExport> LoadExports(const std::wstring& folder);
    // false if the scheme files could not be listed completely; activeGuid is empty unless exactly
    // one profile of the work environment in use names a command scheme
    static bool LoadSchemes(const Locations& where, std::vector<CachedScheme>& schemes, std::string& activeGuid);

    static std::vector<CachedExport> s_exports;
    static std::vector<CachedScheme> s_schemes;
    static std::vector<CachedProfile> s_profiles;
#ifndef ACAPI_VERSION
    static Locations s_testLocations;
#endif
};

} // namespace Baaz
