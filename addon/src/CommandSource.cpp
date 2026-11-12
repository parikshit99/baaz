#include "CommandSource.hpp"
#include "Settings.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <utility>

namespace Baaz {

namespace fs = std::filesystem;

static const std::string_view kSchemeMarker = "6FCD168F-A26C-4547-AF9D-ED9A5FC54DD7";
static const size_t kMarkerBytes = 512;
// The root tag of a profile holds the profile's name, which the user chooses
static const size_t kProfileHeadBytes = 4096;
static const std::string_view kProfileNamespace = "http://www.graphisoft.com/Development/WorkEnvironment/Profile/2005/01";
// Levels below a searched folder that are searched
static const int kMaxSchemeDepth = 3;
// The id of WM_COMMAND is 16 bits wide
static const unsigned kMaxCommandId = 0xFFFF;
// Options > Work Environment, a built-in command
static const unsigned kWorkEnvironmentNumber = 7682;
// The named profiles lie in the folder that holds ProfileSet.xml; the profile in use does not
static const wchar_t* const kProfileSetFile = L"ProfileSet.xml";

std::vector<CommandSource::CachedExport> CommandSource::s_exports;
std::vector<CommandSource::CachedScheme> CommandSource::s_schemes;
std::vector<CommandSource::CachedProfile> CommandSource::s_profiles;

// --- LiveMenu ------------------------------------------------------------------------------

LiveMenu::LiveMenu(HWND frame) : m_frame(frame) {}

HMENU LiveMenu::MenuBar() {
    HMENU bar = GetMenu(m_frame);
    if (bar != nullptr && !m_barInitialized) {
        SendMessageW(m_frame, WM_INITMENU, reinterpret_cast<WPARAM>(bar), 0);
        m_barInitialized = true;
    }
    return bar;
}

// The popup at path, with every popup on the way filled once per LiveMenu
HMENU LiveMenu::FilledMenu(const std::vector<int>& path) {
    HMENU menu = MenuBar();
    std::vector<int> prefix;
    for (int index : path) {
        if (menu == nullptr) return nullptr;
        menu = GetSubMenu(menu, index);
        prefix.push_back(index);
        if (menu != nullptr && m_filled.insert(prefix).second) {
            SendMessageW(m_frame, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(menu), MAKELPARAM(index, 0));
        }
    }
    return menu;
}

static std::wstring MenuTitle(HMENU bar, int index) {
    const int length = GetMenuStringW(bar, static_cast<UINT>(index), nullptr, 0, MF_BYPOSITION);
    if (length <= 0) return L"";
    std::wstring title(static_cast<size_t>(length) + 1, wchar_t());
    const int copied = GetMenuStringW(bar, static_cast<UINT>(index), title.data(), length + 1, MF_BYPOSITION);
    title.resize(copied > 0 ? static_cast<size_t>(copied) : 0);
    return title;
}

std::vector<std::wstring> LiveMenu::TopLevelTitles() {
    std::vector<std::wstring> titles;
    HMENU bar = MenuBar();
    const int count = bar != nullptr ? GetMenuItemCount(bar) : 0;
    for (int i = 0; i < count; ++i) titles.push_back(CleanName(MenuTitle(bar, i)));
    return titles;
}

static MenuNodeType EntryType(const MENUITEMINFOW& info) {
    if ((info.fType & MFT_SEPARATOR) != 0) return MenuNodeType::Separator;
    return info.hSubMenu != nullptr ? MenuNodeType::Popup : MenuNodeType::Item;
}

static bool ReadEntry(HMENU menu, int index, LiveEntry& entry) {
    MENUITEMINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_SUBMENU;
    if (!GetMenuItemInfoW(menu, static_cast<UINT>(index), TRUE, &info)) return false;
    entry.type = EntryType(info);
    entry.liveId = entry.type == MenuNodeType::Item ? info.wID : 0;
    entry.enabled = (info.fState & (MFS_GRAYED | MFS_DISABLED)) == 0;
    return true;
}

std::vector<LiveEntry> LiveMenu::Entries(const std::vector<int>& path) {
    std::vector<LiveEntry> entries;
    HMENU menu = FilledMenu(path);
    const int count = menu != nullptr ? GetMenuItemCount(menu) : 0;
    for (int i = 0; i < count; ++i) {
        LiveEntry entry;
        // A menu that cannot be read completely is not linked at all
        if (!ReadEntry(menu, i, entry)) return {};
        entries.push_back(entry);
    }
    return entries;
}

// --- Files ---------------------------------------------------------------------------------

static std::wstring EnvironmentValue(const wchar_t* name) {
    wchar_t buffer[2048];
    const DWORD length = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    return (length > 0 && length < std::size(buffer)) ? std::wstring(buffer, length) : std::wstring();
}

static std::wstring RunningModulePath() {
    wchar_t buffer[2048];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    return (length > 0 && length < std::size(buffer)) ? std::wstring(buffer, length) : std::wstring();
}

CommandSource::Locations CommandSource::CurrentLocations() {
#ifndef ACAPI_VERSION
    if (!s_testLocations.commands.empty()) return s_testLocations;
#endif
    Locations locations;
    locations.modulePath = RunningModulePath();
    // Settings falls back to the current folder; commands are not read from there
    if (!EnvironmentValue(L"USERPROFILE").empty()) locations.commands = (fs::path(Settings::FolderPath()) / L"commands").wstring();
    const std::wstring appData = EnvironmentValue(L"APPDATA");
    if (!appData.empty()) locations.graphisoft = (fs::path(appData) / L"GRAPHISOFT").wstring();
    return locations;
}

#ifndef ACAPI_VERSION
CommandSource::Locations CommandSource::s_testLocations;

void CommandSource::SetFoldersForTest(std::wstring commands, std::wstring appData, std::wstring modulePath) {
    s_testLocations = { std::move(commands), (fs::path(appData) / L"GRAPHISOFT").wstring(), std::move(modulePath) };
    s_exports.clear();
    s_schemes.clear();
    s_profiles.clear();
}
#endif

static bool HasExtension(const fs::path& path, const wchar_t* extension) {
    return _wcsicmp(path.extension().c_str(), extension) == 0;
}

static bool ReadWriteTime(const fs::path& path, FILETIME& time, unsigned long long* size = nullptr) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    time = data.ftLastWriteTime;
    if (size != nullptr) *size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return true;
}

// Files told apart by the start of their content, not by their names
struct FileKind {
    const wchar_t* extension;
    size_t headBytes;
    bool (*recognise)(std::string_view head);
};

static bool HasSchemeMarker(std::string_view head) { return head.find(kSchemeMarker) != std::string_view::npos; }

static const FileKind kExportFile = { L".xml", kMarkerBytes, HasSchemeMarker };
static const FileKind kSchemeFile = { L".bin", kMarkerBytes, HasSchemeMarker };

// false if the file cannot be read
static bool IsOfKind(const fs::path& path, const FileKind& kind, bool& found) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::string head(kind.headBytes, '\0');
    file.read(head.data(), static_cast<std::streamsize>(head.size()));
    if (file.bad()) return false;
    head.resize(static_cast<size_t>(file.gcount()));
    found = kind.recognise(head);
    return true;
}

// Adds the file if it is a regular file of that kind; false if that cannot be told
template <typename Cached>
static bool AddIfOfKind(const fs::directory_entry& entry, const FileKind& kind, std::vector<Cached>& found) {
    std::error_code error;
    const bool regular = entry.is_regular_file(error);
    if (error) return false;
    if (!regular || !HasExtension(entry.path(), kind.extension)) return true;
    bool marked = false;
    if (!IsOfKind(entry.path(), kind, marked)) return false;
    if (!marked) return true;
    Cached file;
    file.path = entry.path().wstring();
    if (!ReadWriteTime(entry.path(), file.time, &file.size)) return false;
    found.push_back(std::move(file));
    return true;
}

// The files of that kind below the folder. false if the folder could not be listed completely: a file
// left out could be the one that counts.
template <typename Cached>
static bool ListFiles(const fs::path& folder, const FileKind& kind, std::vector<Cached>& found) {
    std::error_code error;
    fs::recursive_directory_iterator it(folder, error);
    for (; !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (it.depth() >= kMaxSchemeDepth) it.disable_recursion_pending();
        if (!AddIfOfKind(*it, kind, found)) return false;
    }
    return !error;
}

template <typename Cached>
static bool SameFiles(const std::vector<Cached>& a, const std::vector<Cached>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const Cached& x, const Cached& y) {
        return x.path == y.path && CompareFileTime(&x.time, &y.time) == 0 && x.size == y.size && !y.readFailed;
    });
}

// A copy: the menu messages sent while linking can run code that refreshes the caches
template <typename Cached, typename Read>
static std::vector<Cached> Refresh(std::vector<Cached>& cache, std::vector<Cached> listed, Read read) {
    std::sort(listed.begin(), listed.end(), [](const Cached& a, const Cached& b) { return a.path < b.path; });
    if (!SameFiles(listed, cache)) {
        for (Cached& file : listed) file.readFailed = !read(file);
        cache = std::move(listed);
    }
    return cache;
}

// false if the file is too large or cannot be read completely
static bool ReadWholeFile(const CommandSource::CachedFile& file, std::string& bytes) {
    if (file.size > kMaxInputBytes) return false;
    std::ifstream stream(fs::path(file.path), std::ios::binary);
    bytes.assign(static_cast<size_t>(file.size), ' ');
    return stream.is_open() && stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

static bool ReadExport(CommandSource::CachedExport& file) {
    std::string xml;
    if (!ReadWholeFile(file, xml)) return false;
    file.bar = SchemeReader::ReadFloorPlanMenuBar(xml);
    return true;
}

std::vector<CommandSource::CachedExport> CommandSource::LoadExports(const std::wstring& folder) {
    std::vector<CachedExport> listed;
    if (!ListFiles(fs::path(folder), kExportFile, listed)) listed.clear();
    return Refresh(s_exports, std::move(listed), ReadExport);
}

// --- Scheme files and the profile ------------------------------------------------------------

// "29" for ...\Archicad 29\ARCHICAD.exe; empty if the program does not sit in such a folder
static std::wstring ArchicadMajorVersion(const std::wstring& modulePath) {
    const std::wstring folder = fs::path(modulePath).parent_path().filename().wstring();
    const std::wstring prefix = L"Archicad ";
    if (folder.compare(0, prefix.size(), prefix) != 0) return L"";
    size_t end = prefix.size();
    while (end < folder.size() && iswdigit(folder[end])) ++end;
    return folder.substr(prefix.size(), end - prefix.size());
}

// "Archicad 29 DEU R1" belongs to version 29, "Archicad 290" does not
static bool IsFolderOfVersion(const std::wstring& name, const std::wstring& major) {
    const std::wstring prefix = L"Archicad " + major;
    if (name.compare(0, prefix.size(), prefix) != 0) return false;
    return name.size() == prefix.size() || !iswdigit(name[prefix.size()]);
}

// The settings folders of the running Archicad version; none if the program does not sit in an
// "Archicad <major>" folder. false if the folders could not be listed completely.
static bool ArchicadFolders(const CommandSource::Locations& where, std::vector<fs::path>& folders) {
    const std::wstring major = ArchicadMajorVersion(where.modulePath);
    if (major.empty()) return true;
    std::error_code error;
    for (fs::directory_iterator it(where.graphisoft, error), end; !error && it != end; it.increment(error)) {
        std::error_code entryError;
        const bool directory = it->is_directory(entryError);
        if (entryError) return false;
        if (directory && IsFolderOfVersion(it->path().filename().wstring(), major)) folders.push_back(it->path());
    }
    return !error;
}

static bool ReadScheme(CommandSource::CachedScheme& file) {
    std::string bytes;
    if (!ReadWholeFile(file, bytes)) return false;
    file.readable = BinSchemeReader::Read(bytes, file.scheme);
    return true;
}

// Value of the attribute in the text of one tag; empty if the tag has no such attribute
static std::string_view AttributeValue(std::string_view tag, std::string_view name) {
    const std::string key = std::string(name) + "=\"";
    for (size_t at = tag.find(key); at != std::string_view::npos; at = tag.find(key, at + 1)) {
        if (at == 0 || std::isspace(static_cast<unsigned char>(tag[at - 1])) == 0) continue;
        const size_t start = at + key.size();
        const size_t end = tag.find('"', start);
        return end == std::string_view::npos ? std::string_view() : tag.substr(start, end - start);
    }
    return {};
}

// The GUID of the command scheme a profile refers to; empty unless it has exactly one such reference
static std::string CommandSchemeGuid(const std::string& xml) {
    static const std::string_view kTag = "<SchemeRef";
    std::string guid;
    size_t count = 0;
    for (size_t at = xml.find(kTag); at != std::string::npos; at = xml.find(kTag, at + 1)) {
        const size_t end = xml.find('>', at);
        if (end == std::string::npos) return "";
        const std::string_view tag(xml.data() + at, end - at);
        if (AttributeValue(tag, "componentType") != kSchemeMarker) continue;
        ++count;
        guid = std::string(AttributeValue(tag, "guid"));
    }
    return count == 1 ? guid : "";
}

static bool ReadProfile(CommandSource::CachedProfile& file) {
    std::string xml;
    if (!ReadWholeFile(file, xml)) return false;
    file.schemeGuid = CommandSchemeGuid(xml);
    return true;
}

// The first element tag of the XML text, after the declaration and comments; empty if there is none
static std::string_view RootTag(std::string_view xml) {
    size_t at = xml.find('<');
    while (at != std::string_view::npos && at + 1 < xml.size() && (xml[at + 1] == '?' || xml[at + 1] == '!')) {
        const size_t end = xml.find('>', at);
        at = end == std::string_view::npos ? end : xml.find('<', end);
    }
    const size_t end = at == std::string_view::npos ? at : xml.find('>', at);
    return end == std::string_view::npos ? std::string_view() : xml.substr(at, end - at);
}

// A work environment profile: the root element is Profile in the profile namespace
static bool IsProfile(std::string_view head) {
    static const std::string_view kName = "<Profile";
    const std::string_view tag = RootTag(head);
    if (tag.size() <= kName.size() || tag.substr(0, kName.size()) != kName) return false;
    return std::isspace(static_cast<unsigned char>(tag[kName.size()])) != 0 && AttributeValue(tag, "xmlns") == kProfileNamespace;
}

static const FileKind kProfileFile = { L".xml", kProfileHeadBytes, IsProfile };

// Whether the file lies in the folder that holds ProfileSet.xml, or below it, up to the Archicad folder.
// false if that cannot be told.
static bool IsNamedProfile(const fs::path& file, const fs::path& archicadFolder, bool& named) {
    named = false;
    for (fs::path folder = file.parent_path(); !named; folder = folder.parent_path()) {
        std::error_code error;
        named = fs::exists(folder / kProfileSetFile, error);
        if (error) return false;
        if (folder == archicadFolder || folder == folder.parent_path()) break;
    }
    return true;
}

// The profiles below the Archicad folders that are not named profiles; false if the folders could not be
// read completely
static bool ListProfiles(const std::vector<fs::path>& archicadFolders, std::vector<CommandSource::CachedProfile>& profiles) {
    for (const fs::path& archicadFolder : archicadFolders) {
        std::vector<CommandSource::CachedProfile> found;
        if (!ListFiles(archicadFolder, kProfileFile, found)) return false;
        for (CommandSource::CachedProfile& file : found) {
            bool named = false;
            if (!IsNamedProfile(fs::path(file.path), archicadFolder, named)) return false;
            if (!named) profiles.push_back(std::move(file));
        }
    }
    return true;
}

// The command scheme the profile in use refers to; empty if there is not exactly one profile of that kind
static std::string ActiveSchemeGuid(std::vector<CommandSource::CachedProfile>& cache, const std::vector<fs::path>& archicadFolders) {
    std::vector<CommandSource::CachedProfile> listed;
    if (!ListProfiles(archicadFolders, listed)) return "";
    const std::vector<CommandSource::CachedProfile> profiles = Refresh(cache, std::move(listed), ReadProfile);
    return profiles.size() == 1 ? profiles[0].schemeGuid : "";
}

bool CommandSource::LoadSchemes(const Locations& where, std::vector<CachedScheme>& schemes, std::string& activeGuid) {
    std::vector<fs::path> folders;
    std::vector<CachedScheme> listed;
    if (!ArchicadFolders(where, folders)) return false;
    for (const fs::path& folder : folders) {
        if (!ListFiles(folder, kSchemeFile, listed)) return false;
    }
    schemes = Refresh(s_schemes, std::move(listed), ReadScheme);
    activeGuid = ActiveSchemeGuid(s_profiles, folders);
    return true;
}

// --- Choosing --------------------------------------------------------------------------------

// The newest of the exports whose top-level menus fit the live menu bar; none if none does
static const CommandSource::CachedExport* NewestFittingExport(const std::vector<CommandSource::CachedExport>& exports,
                                                              MenuSource& live) {
    const CommandSource::CachedExport* newest = nullptr;
    for (const CommandSource::CachedExport& candidate : exports) {
        if (MenuLink::FitScore(candidate.bar, live) < 0) continue;
        if (newest == nullptr || CompareFileTime(&candidate.time, &newest->time) > 0) newest = &candidate;
    }
    return newest;
}

// The one readable scheme file whose header GUID is the one the profile names. No other file is a
// candidate: a scheme that is not in use would link names to the wrong commands.
static const CommandSource::CachedScheme* ActiveScheme(const std::vector<CommandSource::CachedScheme>& schemes,
                                                       const std::string& activeGuid) {
    if (activeGuid.empty()) return nullptr;
    const CommandSource::CachedScheme* found = nullptr;
    for (const CommandSource::CachedScheme& scheme : schemes) {
        if (!scheme.readable || _stricmp(scheme.scheme.guid.c_str(), activeGuid.c_str()) != 0) continue;
        // The same scheme twice: which copy is current?
        if (found != nullptr) return nullptr;
        found = &scheme;
    }
    return found;
}

// --- CommandSource -------------------------------------------------------------------------

static SearchItem ToSearchItem(const LinkedCommand& command) {
    SearchItem item;
    item.title = command.name;
    item.tag = command.menuPath;
    item.kind = ItemKind::Command;
    item.menuPosition = command.position;
    item.liveId = command.liveId;
    item.enabled = command.enabled;
    return item;
}

static const LinkedCommand* FindWorkEnvironment(const std::vector<LinkedCommand>& linked) {
    for (const LinkedCommand& command : linked) {
        if (command.commandNumber == kWorkEnvironmentNumber && command.mdid1 == 0 && command.mdid2 == 0) return &command;
    }
    return nullptr;
}

// Names the structure from the export and links it. Without a fitting export that is all there is
// to say, even when nothing links; with one, nothing linking means the scheme does not describe the live menu.
static CommandIndex Offer(std::vector<MenuNode>& bar, const CommandSource::CachedExport* names, MenuSource& live,
                          const std::vector<DynamicList>& lists, CommandIndex index) {
    if (names != nullptr) AttachNames(bar, names->bar);
    ReduceToShownMenu(bar);
    size_t notSearchable = 0;
    const std::vector<LinkedCommand> linked = MenuLink::Link(bar, live, lists, &notSearchable);
    const LinkedCommand* workEnvironment = FindWorkEnvironment(linked);
    if (workEnvironment != nullptr) index.workEnvironment = ToSearchItem(*workEnvironment);
    if (names == nullptr) {
        index.state = CommandState::NoExport;
        return index;
    }
    if (linked.empty()) return index;
    for (const LinkedCommand& command : linked) {
        if (!command.name.empty()) index.commands.push_back(ToSearchItem(command));
    }
    index.notSearchable = notSearchable;
    index.state = notSearchable > 0 ? CommandState::NewCommands : CommandState::Ready;
    return index;
}

CommandIndex CommandSource::BuildCommandIndex(HWND frame, const std::vector<DynamicList>& lists) {
    CommandIndex index;
    index.workEnvironment.kind = ItemKind::Command;
    if (!IsWindow(frame) || GetMenu(frame) == nullptr) return index;
    const Locations where = CurrentLocations();
    std::vector<CachedScheme> schemes;
    std::string activeGuid;
    if (!LoadSchemes(where, schemes, activeGuid)) return index;
    const CachedScheme* scheme = ActiveScheme(schemes, activeGuid);
    if (scheme == nullptr) return index;
    const std::vector<CachedExport> exports = LoadExports(where.commands);
    LiveMenu live(frame);
    const CachedExport* names = NewestFittingExport(exports, live);
    std::vector<MenuNode> bar = scheme->scheme.floorPlanBar;
    if (names != nullptr) DropAbsentAddOnItems(bar, names->bar);
    if (MenuLink::FitScore(bar, live) < 0) return index;
    return Offer(bar, names, live, lists, std::move(index));
}

static bool HasRunnableShape(const SearchItem& item) {
    return item.kind == ItemKind::Command && item.menuPosition.size() >= 2 && item.liveId != 0 &&
           item.liveId <= kMaxCommandId && item.menuPosition.back() >= 0;
}

bool CommandSource::Run(const SearchItem& item, HWND frame) {
    // A disabled frame has a modal dialog open on top of it; nothing may run underneath that
    if (!HasRunnableShape(item) || !IsWindow(frame) || !IsWindowEnabled(frame)) return false;
    LiveMenu live(frame);
    const std::vector<int> menuPath(item.menuPosition.begin(), item.menuPosition.end() - 1);
    const std::vector<LiveEntry> entries = live.Entries(menuPath);
    const size_t index = static_cast<size_t>(item.menuPosition.back());
    if (index >= entries.size()) return false;
    const LiveEntry& entry = entries[index];
    if (entry.type != MenuNodeType::Item || !entry.enabled || entry.liveId != item.liveId) return false;
    return PostMessageW(frame, WM_COMMAND, MAKEWPARAM(item.liveId, 0), 0) != FALSE;
}

} // namespace Baaz
