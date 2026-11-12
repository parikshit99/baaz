// Keep assert() active in Release builds too
#undef NDEBUG
#include "CommandSource.hpp"
#include "bin_scheme_builder.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace Baaz;

// --- A real window with a Win32 menu bar whose popup is filled on demand -----------------

// Normal: Alpha 255, separator, Beta 300. Shifted: one more active entry in front. ExtraItem: one more
// entry in each segment. Nested: a submenu "Sub" at index 3 that is filled in its own WM_INITMENUPOPUP.
// Listed: the ids in g_listed, 0 for a separator.
enum class Fill { Normal, Grayed, ChangedId, Shifted, ExtraItem, Nested, Listed };

static Fill g_fill = Fill::Normal;
static std::vector<unsigned> g_listed;
static HMENU g_popup = nullptr;
static HMENU g_sub = nullptr;
static std::vector<unsigned> g_commands;

static void ClearMenu(HMENU menu) {
    while (GetMenuItemCount(menu) > 0) RemoveMenu(menu, 0, MF_BYPOSITION);
}

static void FillListed(HMENU popup) {
    for (unsigned id : g_listed) {
        if (id == 0) AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
        else AppendMenuW(popup, MF_STRING, id, L"Entry");
    }
}

static void FillPopup(HMENU popup) {
    ClearMenu(popup);
    if (g_fill == Fill::Listed) return FillListed(popup);
    if (g_fill == Fill::Shifted) AppendMenuW(popup, MF_STRING, 400, L"Shifted");
    UINT firstFlags = g_fill == Fill::Grayed ? (MF_STRING | MF_GRAYED) : MF_STRING;
    UINT firstId = g_fill == Fill::ChangedId ? 256 : 255;
    AppendMenuW(popup, firstFlags, firstId, L"A&lpha\tCtrl+A");
    if (g_fill == Fill::ExtraItem) AppendMenuW(popup, MF_STRING, 401, L"Extra");
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_STRING, 300, L"Beta");
    if (g_fill == Fill::ExtraItem) AppendMenuW(popup, MF_STRING, 402, L"Extra");
    if (g_fill == Fill::Nested) {
        if (g_sub == nullptr) g_sub = CreatePopupMenu();
        AppendMenuW(popup, MF_POPUP, reinterpret_cast<UINT_PTR>(g_sub), L"Sub");
    }
}

static void FillSub(HMENU sub) {
    ClearMenu(sub);
    AppendMenuW(sub, MF_STRING, 500, L"S1");
    AppendMenuW(sub, MF_STRING, 501, L"S2");
}

static LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_INITMENUPOPUP && reinterpret_cast<HMENU>(wParam) == g_popup) {
        FillPopup(g_popup);
        return 0;
    }
    if (msg == WM_INITMENUPOPUP && g_sub != nullptr && reinterpret_cast<HMENU>(wParam) == g_sub) {
        FillSub(g_sub);
        return 0;
    }
    if (msg == WM_COMMAND) {
        g_commands.push_back(LOWORD(wParam));
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

class TestFrame {
public:
    // withOtherMenus: a menu "Extra" in front of "Test" and an untitled slot after it, as in a real bar
    explicit TestFrame(Fill fill, bool withOtherMenus = false) {
        g_fill = fill;
        g_sub = nullptr;
        g_commands.clear();
        static const wchar_t* const kClass = L"BaazLiveMenuTestFrame";
        WNDCLASSW windowClass = {};
        windowClass.lpfnWndProc = FrameProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = kClass;
        RegisterClassW(&windowClass);
        HMENU bar = CreateMenu();
        g_popup = CreatePopupMenu();
        if (withOtherMenus) {
            HMENU extra = CreatePopupMenu();
            AppendMenuW(extra, MF_STRING, 700, L"Other");
            AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(extra), L"&Extra");
        }
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(g_popup), L"&Test");
        if (withOtherMenus) AppendMenuW(bar, MF_STRING, 701, L"");
        m_hwnd = CreateWindowExW(0, kClass, L"frame", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, nullptr, bar,
                                 windowClass.hInstance, nullptr);
        assert(m_hwnd != nullptr);
    }
    ~TestFrame() {
        DestroyWindow(m_hwnd);
        if (g_sub != nullptr && IsMenu(g_sub)) DestroyMenu(g_sub);
        g_popup = nullptr;
        g_sub = nullptr;
    }
    TestFrame(const TestFrame&) = delete;
    TestFrame& operator=(const TestFrame&) = delete;

    HWND Hwnd() const { return m_hwnd; }
    void ChangeFill(Fill fill) { g_fill = fill; }
    void List(std::vector<unsigned> ids) {
        g_fill = Fill::Listed;
        g_listed = std::move(ids);
    }

private:
    HWND m_hwnd = nullptr;
};

static void Pump() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
}

static SearchItem CommandItem(unsigned liveId, std::vector<int> position) {
    SearchItem item;
    item.kind = ItemKind::Command;
    item.liveId = liveId;
    item.menuPosition = std::move(position);
    return item;
}
// --- Temporary commands folder and %APPDATA% root ------------------------------------------

static const char* const kBinGuid = "01234567-89AB-CDEF-0123-456789ABCDEF";
static const char* const kGuidA = "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA";
static const char* const kGuidB = "BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB";
static const wchar_t* const kArchicadFolder = L"Archicad 29 TST R1";

static std::string ExportXml(const std::string& menus) {
    return std::string("<Scheme componentType=\"") + kComponentType + "\"><GlobalSettings><CommandLayout>"
           "<MenuBar ID=\"B\" menubarguid=\"2085E4D3-5694-4BB9-ACAC-3AED5BE50091\" name=\"x\">" + menus +
           "</MenuBar></CommandLayout></GlobalSettings></Scheme>";
}

static std::string XmlItem(unsigned number, const char* name) {
    return "<Item command=\"" + std::to_string(number) + "-0x0-0x0\" comment=\"" + name + "\"/>";
}

static const std::string kExportXml = ExportXml("<Menu ID=\"M\" title=\"&amp;Test\">" + XmlItem(1, "Alpha") +
                                                 "<Separator/>" + XmlItem(2, "Beta") + "</Menu>");

static std::string Replaced(std::string text, const std::string& from, const std::string& to) {
    const size_t at = text.find(from);
    assert(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

// The export with the menu "Test" renamed (to a title of the same length), so it no longer fits the live menu bar
static std::string ExportForOtherMenuBar() { return Replaced(kExportXml, "&amp;Test", "&amp;Othr"); }

static std::vector<std::string> TestEntries() { return { Item(1, 0, 0), Separator(), Item(2, 0, 0) }; }

static std::string BinOf(const std::vector<std::string>& menus, const char* guid = kBinGuid) {
    FileParts parts;
    parts.guid = guid;
    parts.bars = { Bar(kFloorGuid, menus) };
    return Serialize(parts);
}

// The menu "Test" of the live window; the title carries an access key, as in a real file
static std::string TestBin(const std::vector<std::string>& entries = TestEntries(), const char* guid = kBinGuid) {
    return BinOf({ Menu(L"M", L"&Test", entries) }, guid);
}

// A file with the scheme marker that the reader does not understand
static std::string UnreadableBin() { return std::string("xx") + kComponentType; }

static FILETIME TimeAt(int secondsFromBase) {
    ULARGE_INTEGER ticks;
    ticks.QuadPart = 133500000000000000ull + static_cast<unsigned long long>(1000 + secondsFromBase) * 10000000ull;
    return { ticks.LowPart, ticks.HighPart };
}

static void SetWriteTime(const std::filesystem::path& path, const FILETIME& time) {
    HANDLE file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(file != INVALID_HANDLE_VALUE);
    assert(SetFileTime(file, nullptr, nullptr, &time));
    CloseHandle(file);
}

static void WriteTestFile(const std::filesystem::path& path, const std::string& contents, int secondsFromBase) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << contents;
    SetWriteTime(path, TimeAt(secondsFromBase));
}

static const char* const kProfileNamespace = " xmlns=\"http://www.graphisoft.com/Development/WorkEnvironment/Profile/2005/01\"";

// A profile naming the given command schemes next to a scheme of another component, laid out as Archicad writes it
static std::string ProfileXml(const std::vector<const char*>& commandSchemeGuids) {
    std::string xml = std::string("\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                  "<Profile guid=\"CCCCCCCC-CCCC-CCCC-CCCC-CCCCCCCCCCCC\" name=\"x\"") + kProfileNamespace + ">"
                      "<SchemeRef componentType=\"D3EC122F-0A7F-4B14-94D0-0A6B7FFA66C3\" guid=\"DDDDDDDD-DDDD-DDDD-DDDD-DDDDDDDDDDDD\" name=\"\"/>";
    for (const char* guid : commandSchemeGuids) {
        xml += std::string("<SchemeRef componentType=\"") + kComponentType + "\" guid=\"" + guid + "\" name=\"x\"/>";
    }
    return xml + "</Profile>";
}

class TestFolders {
public:
    TestFolders() : m_root(std::filesystem::temp_directory_path() / (L"baaz_live_menu_" + std::to_wstring(GetCurrentProcessId()))) {
        std::filesystem::remove_all(m_root);
        std::filesystem::create_directories(Commands());
        std::filesystem::create_directories(AppData());
        CommandSource::SetFoldersForTest(Commands().wstring(), AppData().wstring());
    }
    ~TestFolders() { std::filesystem::remove_all(m_root); }
    TestFolders(const TestFolders&) = delete;
    TestFolders& operator=(const TestFolders&) = delete;

    std::filesystem::path Root() const { return m_root; }
    std::filesystem::path Commands() const { return m_root / L"commands"; }
    std::filesystem::path AppData() const { return m_root / L"appdata"; }
    std::filesystem::path ArchicadFolder(const wchar_t* folder = kArchicadFolder) const {
        return AppData() / L"GRAPHISOFT" / folder;
    }
    void WriteExport(int secondsFromBase, const std::string& xml = kExportXml) const {
        WriteTestFile(Commands() / L"export.xml", xml, secondsFromBase);
    }
    // The scheme files of the running Archicad, two folders below its folder
    std::filesystem::path SchemePath(const wchar_t* name = L"layout.bin", const wchar_t* folder = kArchicadFolder) const {
        return ArchicadFolder(folder) / L"Workenv" / L"Commands" / name;
    }
    void WriteBin(const std::string& bytes, int secondsFromBase, const wchar_t* name = L"layout.bin",
                  const wchar_t* folder = kArchicadFolder) const {
        WriteTestFile(SchemePath(name, folder), bytes, secondsFromBase);
    }
    // A profile of the work environment folder `directory`, which holds no ProfileSet.xml
    void WriteProfile(const std::string& xml, int secondsFromBase = 0, const wchar_t* directory = L"Recent",
                      const wchar_t* file = L"Profile.xml") const {
        WriteTestFile(ArchicadFolder() / directory / L"Profile" / file, xml, secondsFromBase);
    }
    // The named profiles sit next to a ProfileSet.xml; they say nothing about the one in use
    void WriteNamedProfile(const std::string& xml) const {
        WriteTestFile(ArchicadFolder() / L"Named" / L"ProfileSet.xml", "<ProfileSet/>", 0);
        WriteTestFile(ArchicadFolder() / L"Named" / L"Profile" / L"Profile_1.xml", xml, 0);
    }
    // The profile in use names the command scheme with this GUID
    void UseScheme(const char* guid = kBinGuid, int secondsFromBase = 0) const {
        WriteProfile(ProfileXml({ guid }), secondsFromBase);
    }
    void WriteCurrent() const {
        WriteExport(0);
        WriteBin(TestBin(), 0);
        UseScheme();
    }

    void CreateArchicadFolder() const { std::filesystem::create_directories(ArchicadFolder()); }

private:
    std::filesystem::path m_root;
};

// Holds a file open without sharing, like a program that is writing to it
class FileLock {
public:
    explicit FileLock(const std::filesystem::path& path)
        : m_file(CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {
        assert(m_file != INVALID_HANDLE_VALUE);
    }
    ~FileLock() { CloseHandle(m_file); }
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

private:
    HANDLE m_file;
};

static CommandIndex IndexOf(HWND frame) { return CommandSource::BuildCommandIndex(frame, {}); }

static std::vector<std::wstring> Titles(const std::vector<SearchItem>& items) {
    std::vector<std::wstring> titles;
    for (const SearchItem& item : items) titles.push_back(item.title);
    return titles;
}

static const std::vector<std::wstring> kAlphaBeta = { L"Alpha", L"Beta" };

// --- Tests -----------------------------------------------------------------------------------

void TestFillsOnDemand() {
    TestFrame frame(Fill::Normal);
    assert(GetMenuItemCount(g_popup) == 0);
    LiveMenu live(frame.Hwnd());
    assert((live.TopLevelTitles() == std::vector<std::wstring>{ L"Test" }));
    std::vector<LiveEntry> entries = live.Entries({ 0 });
    assert(entries.size() == 3);
    assert(entries[0].type == MenuNodeType::Item && entries[0].liveId == 255 && entries[0].enabled);
    assert(entries[1].type == MenuNodeType::Separator);
    assert(entries[2].type == MenuNodeType::Item && entries[2].liveId == 300);
    assert(live.Entries({ 1 }).empty());
    assert(live.Entries({ 0, 0 }).empty());
    std::cout << "[PASS] TestFillsOnDemand\n";
}

void TestRunPostsCommand() {
    TestFrame frame(Fill::Normal);
    assert(CommandSource::Run(CommandItem(255, { 0, 0 }), frame.Hwnd()));
    Pump();
    assert((g_commands == std::vector<unsigned>{ 255 }));
    std::cout << "[PASS] TestRunPostsCommand\n";
}

void TestRunRefusesDisabled() {
    TestFrame frame(Fill::Grayed);
    assert(!CommandSource::Run(CommandItem(255, { 0, 0 }), frame.Hwnd()));
    Pump();
    assert(g_commands.empty());
    std::cout << "[PASS] TestRunRefusesDisabled\n";
}

void TestRunRefusesChangedId() {
    TestFrame frame(Fill::ChangedId);
    assert(!CommandSource::Run(CommandItem(255, { 0, 0 }), frame.Hwnd()));
    Pump();
    assert(g_commands.empty());
    std::cout << "[PASS] TestRunRefusesChangedId\n";
}

// WM_COMMAND carries 16 bits: a longer id would arrive as another command
void TestRunRefusesIdLongerThanACommand() {
    const unsigned tooLong = 0x10000 + 255;
    TestFrame frame(Fill::Normal);
    frame.List({ tooLong });
    assert(!CommandSource::Run(CommandItem(tooLong, { 0, 0 }), frame.Hwnd()));
    Pump();
    assert(g_commands.empty());
    std::cout << "[PASS] TestRunRefusesIdLongerThanACommand\n";
}

void TestRunRefusesWrongKindOrPosition() {
    TestFrame frame(Fill::Normal);
    SearchItem notACommand = CommandItem(255, { 0, 0 });
    notACommand.kind = ItemKind::Help;
    assert(!CommandSource::Run(notACommand, frame.Hwnd()));
    assert(!CommandSource::Run(CommandItem(255, { 0, 1 }), frame.Hwnd()));   // a separator
    assert(!CommandSource::Run(CommandItem(255, { 0, 7 }), frame.Hwnd()));   // beyond the menu
    assert(!CommandSource::Run(CommandItem(255, { 0 }), frame.Hwnd()));      // a menu, not an item
    assert(!CommandSource::Run(CommandItem(255, { 0, 0 }), nullptr));
    Pump();
    assert(g_commands.empty());
    std::cout << "[PASS] TestRunRefusesWrongKindOrPosition\n";
}

void TestRunRefusesDisabledFrame() {
    TestFrame frame(Fill::Normal);
    EnableWindow(frame.Hwnd(), FALSE);   // a modal dialog is open on top of it
    assert(!CommandSource::Run(CommandItem(255, { 0, 0 }), frame.Hwnd()));
    Pump();
    assert(g_commands.empty());
    EnableWindow(frame.Hwnd(), TRUE);
    assert(CommandSource::Run(CommandItem(255, { 0, 0 }), frame.Hwnd()));
    Pump();
    assert((g_commands == std::vector<unsigned>{ 255 }));
    std::cout << "[PASS] TestRunRefusesDisabledFrame\n";
}

void TestNestedPopups() {
    TestFrame frame(Fill::Nested);
    LiveMenu live(frame.Hwnd());
    std::vector<LiveEntry> top = live.Entries({ 0 });
    assert(top.size() == 4);
    assert(top[3].type == MenuNodeType::Popup && top[3].liveId == 0);
    std::vector<LiveEntry> sub = live.Entries({ 0, 3 });
    assert(sub.size() == 2 && sub[0].liveId == 500 && sub[1].liveId == 501);
    assert(CommandSource::Run(CommandItem(501, { 0, 3, 1 }), frame.Hwnd()));
    Pump();
    assert((g_commands == std::vector<unsigned>{ 501 }));
    assert(!CommandSource::Run(CommandItem(600, { 0, 3 }), frame.Hwnd()));   // the popup itself
    Pump();
    assert(g_commands.size() == 1);
    std::cout << "[PASS] TestNestedPopups\n";
}

void TestOffersCommandsWhenCurrent() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteCurrent();
    const CommandIndex index = IndexOf(frame.Hwnd());
    assert(index.state == CommandState::Ready && index.notSearchable == 0);
    const std::vector<SearchItem>& items = index.commands;
    assert(items.size() == 2);
    assert(items[0].kind == ItemKind::Command && items[0].title == L"Alpha" && items[0].tag == L"Test");
    assert(items[0].liveId == 255 && items[0].enabled && (items[0].menuPosition == std::vector<int>{ 0, 0 }));
    assert(items[1].title == L"Beta" && items[1].liveId == 300 && (items[1].menuPosition == std::vector<int>{ 0, 2 }));
    assert(index.workEnvironment.liveId == 0);   // 7682 is not in this menu
    assert(CommandSource::Run(items[1], frame.Hwnd()));
    Pump();
    assert((g_commands == std::vector<unsigned>{ 300 }));
    std::cout << "[PASS] TestOffersCommandsWhenCurrent\n";
}

void TestShiftedTopLevelPositionsAreFollowed() {
    TestFrame frame(Fill::Normal, true);   // live bar: Extra, Test, untitled slot; the scheme has Test only
    TestFolders folders;
    folders.WriteCurrent();
    std::vector<SearchItem> items = IndexOf(frame.Hwnd()).commands;
    assert(items.size() == 2);
    assert(items[0].title == L"Alpha" && items[0].liveId == 255 && (items[0].menuPosition == std::vector<int>{ 1, 0 }));
    assert(items[1].title == L"Beta" && (items[1].menuPosition == std::vector<int>{ 1, 2 }));
    assert(CommandSource::Run(items[0], frame.Hwnd()));
    Pump();
    assert((g_commands == std::vector<unsigned>{ 255 }));
    frame.ChangeFill(Fill::ChangedId);
    assert(!CommandSource::Run(items[0], frame.Hwnd()));
    Pump();
    assert(g_commands.size() == 1);
    std::cout << "[PASS] TestShiftedTopLevelPositionsAreFollowed\n";
}

void TestRunAfterMenuRebuiltSinceIndexing() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteCurrent();
    std::vector<SearchItem> items = IndexOf(frame.Hwnd()).commands;
    assert(items.size() == 2);
    frame.ChangeFill(Fill::ChangedId);
    assert(!CommandSource::Run(items[0], frame.Hwnd()));
    frame.ChangeFill(Fill::Grayed);
    assert(!CommandSource::Run(items[0], frame.Hwnd()));
    frame.ChangeFill(Fill::Shifted);
    assert(!CommandSource::Run(items[0], frame.Hwnd()));
    assert(!CommandSource::Run(items[1], frame.Hwnd()));
    Pump();
    assert(g_commands.empty());
    frame.ChangeFill(Fill::Normal);
    assert(CommandSource::Run(items[0], frame.Hwnd()));
    Pump();
    assert((g_commands == std::vector<unsigned>{ 255 }));
    std::cout << "[PASS] TestRunAfterMenuRebuiltSinceIndexing\n";
}

void TestIndexOffersGreyedItemDisabled() {
    TestFrame frame(Fill::Grayed);
    TestFolders folders;
    folders.WriteCurrent();
    std::vector<SearchItem> items = IndexOf(frame.Hwnd()).commands;
    assert(items.size() == 2);
    assert(items[0].kind == ItemKind::Command && items[0].title == L"Alpha" && !items[0].enabled);
    assert(items[1].enabled);
    assert(!CommandSource::Run(items[0], frame.Hwnd()));
    Pump();
    assert(g_commands.empty());
    std::cout << "[PASS] TestIndexOffersGreyedItemDisabled\n";
}

// --- Names: the export, found by its marker, the newest that fits ------------------------

void TestExportFoundBySchemeMarker() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteBin(TestBin(), 0);
    folders.UseScheme();
    // Folder and file names do not matter, the marker does
    WriteTestFile(folders.Commands() / L"Exported profile" / L"Commands" / L"x_123.xml", kExportXml, 10);
    // A newer XML without the marker is not an export, whatever names it holds
    const std::string notAnExport = Replaced(Replaced(kExportXml, kComponentType, "0FCD168F-A26C-4547-AF9D-ED9A5FC54DD7"), "Alpha", "Wrong");
    WriteTestFile(folders.Commands() / L"other.xml", notAnExport, 20);
    assert(Titles(IndexOf(frame.Hwnd()).commands) == kAlphaBeta);
    std::cout << "[PASS] TestExportFoundBySchemeMarker\n";
}

void TestNewestFittingExportGivesNames() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteBin(TestBin(), 0);
    folders.UseScheme();
    WriteTestFile(folders.Commands() / L"one" / L"old.xml", Replaced(kExportXml, "Alpha", "Old name"), 10);
    WriteTestFile(folders.Commands() / L"two" / L"new.xml", Replaced(kExportXml, "Alpha", "New name"), 20);
    // Newest of all, but its menu bar is another one: it must not give names
    WriteTestFile(folders.Commands() / L"three" / L"other_bar.xml", Replaced(ExportForOtherMenuBar(), "Alpha", "Wrong"), 30);
    assert((Titles(IndexOf(frame.Hwnd()).commands) == std::vector<std::wstring>{ L"New name", L"Beta" }));
    std::cout << "[PASS] TestNewestFittingExportGivesNames\n";
}

void TestUnnamedCommandIsCountedNotOffered() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteBin(TestBin(), 0);
    folders.UseScheme();
    folders.WriteExport(0, ExportXml("<Menu ID=\"M\" title=\"&amp;Test\">" + XmlItem(1, "Alpha") + "</Menu>"));   // no name for 2
    const CommandIndex index = IndexOf(frame.Hwnd());
    assert(index.state == CommandState::NewCommands && index.notSearchable == 1);
    assert((Titles(index.commands) == std::vector<std::wstring>{ L"Alpha" }));
    std::cout << "[PASS] TestUnnamedCommandIsCountedNotOffered\n";
}

void TestPlaceholderIsNeverCounted() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    // Archicad replaces 23040 and 9472 at run time; the export has no name for them
    folders.WriteBin(TestBin({ Item(1, 0, 0), Separator(), Item(2, 0, 0), Item(23040, 0, 0), Item(9472, 0, 0) }), 0);
    folders.WriteExport(0);
    folders.UseScheme();
    const CommandIndex index = IndexOf(frame.Hwnd());
    assert(index.state == CommandState::Ready && index.notSearchable == 0);
    assert(Titles(index.commands) == kAlphaBeta);
    std::cout << "[PASS] TestPlaceholderIsNeverCounted\n";
}

// The export lists Lineal (1557) in the Test menu; the scheme has moved it to the Extra menu
void TestMovedCommandKeepsItsName() {
    TestFrame frame(Fill::Normal, true);
    TestFolders folders;
    folders.WriteExport(0, ExportXml("<Menu ID=\"M\" title=\"&amp;Test\">" + XmlItem(1, "Alpha") + "<Separator/>" +
                                      XmlItem(2, "Beta") + XmlItem(1557, "Lineal") + "</Menu>"));
    folders.WriteBin(BinOf({ Menu(L"E", L"&Extra", { Item(1557, 0, 0) }), Menu(L"M", L"&Test", TestEntries()) }), 0);
    folders.UseScheme();
    const std::vector<SearchItem> items = IndexOf(frame.Hwnd()).commands;
    assert((Titles(items) == std::vector<std::wstring>{ L"Lineal", L"Alpha", L"Beta" }));
    assert(items[0].liveId == 700 && (items[0].menuPosition == std::vector<int>{ 0, 0 }) && items[0].tag == L"Extra");
    std::cout << "[PASS] TestMovedCommandKeepsItsName\n";
}

// The export is older than the scheme: a menu change since the export is no reason to stop offering
void TestMenuChangeWithoutExportStillOffers() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteExport(0);
    folders.UseScheme();
    folders.WriteBin(TestBin({ Item(2, 0, 0), Separator(), Item(1, 0, 0) }), 60);   // the two commands swapped
    const CommandIndex index = IndexOf(frame.Hwnd());
    assert(index.state == CommandState::Ready);
    assert((Titles(index.commands) == std::vector<std::wstring>{ L"Beta", L"Alpha" }));
    assert(index.commands[0].liveId == 255 && index.commands[1].liveId == 300);
    std::cout << "[PASS] TestMenuChangeWithoutExportStillOffers\n";
}

// The scheme still lists a menu of an add-on that is not loaded; the export (and the live menu) leave it out
void TestUnloadedAddOnItemsDoNotBlockLinking() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteExport(0);
    folders.UseScheme();
    std::vector<std::string> entries = TestEntries();
    entries.push_back(Popup(Menu(L"H", L"Hidden", { Item(9, 5, 6) })));
    folders.WriteBin(TestBin(entries), 0);
    const CommandIndex index = IndexOf(frame.Hwnd());
    assert(index.state == CommandState::Ready && Titles(index.commands) == kAlphaBeta);
    std::cout << "[PASS] TestUnloadedAddOnItemsDoNotBlockLinking\n";
}

// --- Add-on changes since the export: parts around a placeholder --------------------------------

static const unsigned kGraphicOverrides = 23040;

// An item of the add-on with the MDID pair 5:6
static std::string XmlAddOnItem(unsigned number, const char* name) {
    return "<Item command=\"" + std::to_string(number) + "-0x5-0x6\" comment=\"" + name + "\"/>";
}

static std::string TestMenuXml(const std::string& items) {
    return ExportXml("<Menu ID=\"M\" title=\"&amp;Test\">" + items + "</Menu>");
}

// The names A, B and C for the built-in commands 1, 2 and 3, as in [A, B | C]
static std::string ExportABC(const std::string& before = "") {
    return TestMenuXml(before + XmlItem(1, "A") + XmlItem(2, "B") + "<Separator/>" + XmlItem(3, "C"));
}

static std::vector<unsigned> LiveIds(const std::vector<SearchItem>& items) {
    std::vector<unsigned> ids;
    for (const SearchItem& item : items) ids.push_back(item.liveId);
    return ids;
}

// The add-on of X was loaded when the export was made and is not loaded now, so X stays in the structure.
// [X, A, P, B] against the live [A, e1, e2, B]: the counts agree, and pairing one by one would run A for "X".
void TestUnloadedAddOnNextToPlaceholderLinksNothing() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteExport(0, ExportABC(XmlAddOnItem(9, "X")));
    folders.WriteBin(TestBin({ Item(9, 5, 6), Item(1, 0, 0), Item(kGraphicOverrides, 0, 0), Item(2, 0, 0), Separator(), Item(3, 0, 0) }), 0);
    folders.UseScheme();
    frame.List({ 201, 801, 802, 202, 0, 203 });
    const CommandIndex index = IndexOf(frame.Hwnd());
    assert((Titles(index.commands) == std::vector<std::wstring>{ L"C" }) && (LiveIds(index.commands) == std::vector<unsigned>{ 203 }));
    // The part is skipped because an export cannot cure it, so it is not counted
    assert(index.state == CommandState::Ready && index.notSearchable == 0);
    std::cout << "[PASS] TestUnloadedAddOnNextToPlaceholderLinksNothing\n";
}

// The add-on of Y was installed after the export, so Y is left out of the structure but is in the live menu.
// [A, P, B] against the live [Y, A, e1, B]: anchoring at the ends would run Y for "A".
void TestAddOnInstalledSinceExportLinksNothing() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteExport(0, ExportABC());
    folders.WriteBin(TestBin({ Item(9, 5, 6), Item(1, 0, 0), Item(kGraphicOverrides, 0, 0), Item(2, 0, 0), Separator(), Item(3, 0, 0) }), 0);
    folders.UseScheme();
    frame.List({ 209, 201, 801, 202, 0, 203 });
    const CommandIndex index = IndexOf(frame.Hwnd());
    assert((Titles(index.commands) == std::vector<std::wstring>{ L"C" }) && (LiveIds(index.commands) == std::vector<unsigned>{ 203 }));
    assert(index.state == CommandState::Ready && index.notSearchable == 0);
    std::cout << "[PASS] TestAddOnInstalledSinceExportLinksNothing\n";
}

// [X, A, P, B] against the live [A, B] (X not loaded, P gave nothing): the count differs as well, but the part is
// uncertain, and uncertain wins: it is skipped and not counted
void TestUncertainPartWithCountMismatchIsNotCounted() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteExport(0, ExportABC(XmlAddOnItem(9, "X")));
    folders.WriteBin(TestBin({ Item(9, 5, 6), Item(1, 0, 0), Item(kGraphicOverrides, 0, 0), Item(2, 0, 0), Separator(), Item(3, 0, 0) }), 0);
    folders.UseScheme();
    frame.List({ 201, 202, 0, 203 });
    const CommandIndex index = IndexOf(frame.Hwnd());
    assert((Titles(index.commands) == std::vector<std::wstring>{ L"C" }) && (LiveIds(index.commands) == std::vector<unsigned>{ 203 }));
    assert(index.state == CommandState::Ready && index.notSearchable == 0);
    std::cout << "[PASS] TestUncertainPartWithCountMismatchIsNotCounted\n";
}

void TestBuiltInPartAroundPlaceholderStillLinks() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteExport(0, ExportABC());
    folders.WriteBin(TestBin({ Item(1, 0, 0), Item(kGraphicOverrides, 0, 0), Item(2, 0, 0), Separator(), Item(3, 0, 0) }), 0);
    folders.UseScheme();
    frame.List({ 201, 801, 802, 202, 0, 203 });
    const CommandIndex index = IndexOf(frame.Hwnd());
    assert((Titles(index.commands) == std::vector<std::wstring>{ L"A", L"B", L"C" }));
    assert((LiveIds(index.commands) == std::vector<unsigned>{ 201, 202, 203 }));
    assert(index.state == CommandState::Ready && index.notSearchable == 0);
    std::cout << "[PASS] TestBuiltInPartAroundPlaceholderStillLinks\n";
}

// Without an export every add-on item counts as uncertain. [X, Work Environment, P] against the live
// [W, e1, e2] (X not loaded): pairing one by one would open e1 for Work Environment.
void TestNoExportAddOnPartLinksNoWorkEnvironment() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteBin(TestBin({ Item(9, 5, 6), Item(7682, 0, 0), Item(kGraphicOverrides, 0, 0), Separator(), Item(3, 0, 0) }), 0);
    folders.UseScheme();
    frame.List({ 210, 801, 802, 0, 203 });
    const CommandIndex index = IndexOf(frame.Hwnd());
    assert(index.state == CommandState::NoExport && index.commands.empty());
    assert(index.workEnvironment.liveId == 0);
    std::cout << "[PASS] TestNoExportAddOnPartLinksNoWorkEnvironment\n";
}

// A part whose item count changed links nothing, and a new export cannot change that, so it is not counted
void TestCountMismatchPartIsNotCounted() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteCurrent();
    frame.List({ 255, 0, 300, 402 });
    const CommandIndex index = IndexOf(frame.Hwnd());
    assert((Titles(index.commands) == std::vector<std::wstring>{ L"Alpha" }));
    assert(index.state == CommandState::Ready && index.notSearchable == 0);
    std::cout << "[PASS] TestCountMismatchPartIsNotCounted\n";
}

// --- Entries of the scheme that Archicad does not show ------------------------------------------

static const std::string kA = Item(1, 0, 0);
static const std::string kB = Item(2, 0, 0);
static const std::string kC = Item(3, 0, 0);
static const std::string kNotOffered = Item(4, 0, 0);   // a command this Archicad no longer offers
static const std::string kCut = Separator();

static const std::string kNames = TestMenuXml(XmlItem(1, "A") + XmlItem(2, "B") + XmlItem(3, "C") + XmlItem(4, "(Empty)"));

// The files keep their path from test to test and may keep their size, so each gets a new time to be read again
static int NextTime() {
    static int time = 1000;
    return ++time;
}

// The scheme menu "Test" holding entries against a live menu holding the ids in live, 0 for a separator
static CommandIndex IndexFor(const std::vector<std::string>& entries, const std::vector<unsigned>& live) {
    const int time = NextTime();
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteExport(time, kNames);
    folders.WriteBin(TestBin(entries), time);
    folders.UseScheme();
    frame.List(live);
    return IndexOf(frame.Hwnd());
}

static void AssertLinksAll(const std::vector<std::string>& entries, const std::vector<unsigned>& live,
                           const std::vector<std::wstring>& titles, const std::vector<unsigned>& ids) {
    const CommandIndex index = IndexFor(entries, live);
    assert(Titles(index.commands) == titles && LiveIds(index.commands) == ids);
    assert(index.state == CommandState::Ready && index.notSearchable == 0);
}

static const std::vector<std::wstring> kTitlesAB = { L"A", L"B" };
static const std::vector<std::wstring> kTitlesABC = { L"A", L"B", L"C" };

void TestItemNotShownLeavesItsPartLinked() {
    AssertLinksAll({ kA, kNotOffered, kB }, { 201, 202 }, kTitlesAB, { 201, 202 });
    AssertLinksAll({ kA, kCut, kNotOffered, kCut, kB }, { 201, 0, 202 }, kTitlesAB, { 201, 202 });   // a whole part
    std::cout << "[PASS] TestItemNotShownLeavesItsPartLinked\n";
}

void TestPopupLeftEmptyLeavesItsPartLinked() {
    AssertLinksAll({ kA, Popup(Menu(L"H", L"Hidden", { kNotOffered })), kB }, { 201, 202 }, kTitlesAB, { 201, 202 });
    const std::string inner = Popup(Menu(L"I", L"Inner", { kNotOffered }));
    AssertLinksAll({ kA, Popup(Menu(L"H", L"Hidden", { inner, kCut })), kB }, { 201, 202 }, kTitlesAB, { 201, 202 });
    std::cout << "[PASS] TestPopupLeftEmptyLeavesItsPartLinked\n";
}

// The add-on is missing from the export, its popup empties and leaves two separators in a row: only the
// reduction after the drop makes the parts fit the live menu
void TestPopupOfAbsentAddOnLeavesTheMenuLinked() {
    const std::string absent = Item(9, 5, 6);
    AssertLinksAll({ kA, kCut, Popup(Menu(L"H", L"Hidden", { absent })), kCut, kB }, { 201, 0, 202 }, kTitlesAB, { 201, 202 });
    std::cout << "[PASS] TestPopupOfAbsentAddOnLeavesTheMenuLinked\n";
}

void TestSurplusSeparatorsLeaveTheMenuLinked() {
    AssertLinksAll({ kA, kCut, kCut, kB }, { 201, 0, 202 }, kTitlesAB, { 201, 202 });
    AssertLinksAll({ kCut, kA, kCut, kB, kCut }, { 201, 0, 202 }, kTitlesAB, { 201, 202 });
    std::cout << "[PASS] TestSurplusSeparatorsLeaveTheMenuLinked\n";
}

// [A, (Empty), B | C] against the live [A, Z, B | C]: the count differs, so A and B stay unlinked and,
// as no export can link them, uncounted
void TestItemShownAfterAllLeavesItsPartUnlinked() {
    const CommandIndex index = IndexFor({ kA, kNotOffered, kB, kCut, kC }, { 201, 209, 202, 0, 203 });
    assert((Titles(index.commands) == std::vector<std::wstring>{ L"C" }) && (LiveIds(index.commands) == std::vector<unsigned>{ 203 }));
    assert(index.state == CommandState::Ready && index.notSearchable == 0);
    std::cout << "[PASS] TestItemShownAfterAllLeavesItsPartUnlinked\n";
}

// A part with a placeholder that lost an "(Empty)" item is uncertain: the count cannot show whether that
// item is shown after all. [(Empty), A, P] against the live [Z, A, e] would run Z for A.
void TestItemNotShownNextToPlaceholderMakesItsPartUncertain() {
    const CommandIndex index = IndexFor({ kNotOffered, kA, Item(kGraphicOverrides, 0, 0), kCut, kC }, { 209, 201, 801, 0, 203 });
    assert((Titles(index.commands) == std::vector<std::wstring>{ L"C" }) && (LiveIds(index.commands) == std::vector<unsigned>{ 203 }));
    assert(index.state == CommandState::Ready && index.notSearchable == 0);
    // The same if the item sits between the others
    const CommandIndex inside = IndexFor({ kA, kNotOffered, Item(kGraphicOverrides, 0, 0), kB, kCut, kC }, { 201, 801, 802, 202, 0, 203 });
    assert((Titles(inside.commands) == std::vector<std::wstring>{ L"C" }) && (LiveIds(inside.commands) == std::vector<unsigned>{ 203 }));
    // Without a placeholder the count decides, so the part still links
    AssertLinksAll({ kNotOffered, kA, kB }, { 201, 202 }, kTitlesAB, { 201, 202 });
    // A popup left empty is no lost item
    AssertLinksAll({ Popup(Menu(L"H", L"Hidden", { kNotOffered })), kA, Item(kGraphicOverrides, 0, 0), kB, kCut, kC }, { 201, 801, 802, 202, 0, 203 },
                   kTitlesABC, { 201, 202, 203 });
    std::cout << "[PASS] TestItemNotShownNextToPlaceholderMakesItsPartUncertain\n";
}

// Popups and separators need no names: Work Environment is found before the first export
void TestSchemeIsReducedWithoutAnExport() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteBin(TestBin({ kA, kCut, kCut, Item(7682, 0, 0) }), NextTime());
    folders.UseScheme();
    frame.List({ 201, 0, 210 });
    const CommandIndex index = IndexOf(frame.Hwnd());
    assert(index.state == CommandState::NoExport && index.workEnvironment.liveId == 210);
    std::cout << "[PASS] TestSchemeIsReducedWithoutAnExport\n";
}

// --- A part of placeholders whose lists are empty vanishes with its separator ----------------------

static const std::string kList = Item(kGraphicOverrides, 0, 0);
static const std::string kSecondList = Item(9472, 0, 0);

void TestVanishedPlaceholderPartLeavesTheOthersLinked() {
    AssertLinksAll({ kA, kCut, kList }, { 201 }, { L"A" }, { 201 });
    AssertLinksAll({ kList, kCut, kA }, { 201 }, { L"A" }, { 201 });
    AssertLinksAll({ kA, kCut, kList, kCut, kB }, { 201, 0, 202 }, kTitlesAB, { 201, 202 });
    AssertLinksAll({ kA, kCut, kList, kCut, kSecondList, kCut, kB }, { 201, 0, 202 }, kTitlesAB, { 201, 202 });
    AssertLinksAll({ kA, kCut, kList, kSecondList, kCut, kB }, { 201, 0, 202 }, kTitlesAB, { 201, 202 });
    std::cout << "[PASS] TestVanishedPlaceholderPartLeavesTheOthersLinked\n";
}

// [A | P, C | B] against the live [A | B]: a static entry cannot vanish, so something else changed
void TestRegionWithStaticEntryLinksNothingAround() {
    const int time = NextTime();
    TestFrame frame(Fill::Normal, true);   // the menu Extra links, so that something links at all
    TestFolders folders;
    folders.WriteExport(time, TestMenuXml(XmlItem(1, "A") + XmlItem(2, "B") + XmlItem(3, "C") + XmlItem(5, "Other")));
    folders.WriteBin(BinOf({ Menu(L"E", L"&Extra", { Item(5, 0, 0) }), Menu(L"M", L"&Test", { kA, kCut, kList, kC, kCut, kB }) }), time);
    folders.UseScheme();
    frame.List({ 201, 0, 202 });
    const CommandIndex index = IndexOf(frame.Hwnd());
    assert((Titles(index.commands) == std::vector<std::wstring>{ L"Other" }) && (LiveIds(index.commands) == std::vector<unsigned>{ 700 }));
    assert(index.state == CommandState::Ready && index.notSearchable == 0);
    std::cout << "[PASS] TestRegionWithStaticEntryLinksNothingAround\n";
}

void TestPartNextToVanishedPartStillNeedsItsCount() {
    const CommandIndex index = IndexFor({ kA, kCut, kList, kCut, kB, kC }, { 201, 0, 202 });
    assert((Titles(index.commands) == std::vector<std::wstring>{ L"A" }) && (LiveIds(index.commands) == std::vector<unsigned>{ 201 }));
    assert(index.state == CommandState::Ready && index.notSearchable == 0);
    std::cout << "[PASS] TestPartNextToVanishedPartStillNeedsItsCount\n";
}

// --- Which scheme file is the active one: only the one the profile in use names ---------------

static void AssertNothingOffered(HWND frame) {
    const CommandIndex index = IndexOf(frame);
    assert(index.commands.empty() && index.state == CommandState::Unreadable && index.workEnvironment.liveId == 0);
}

// Scheme A (older) names Beta, scheme B (newer) names Gamma, both fit equally; the export names 1, 2 and 3
static void WriteSchemesAAndB(const TestFolders& folders) {
    folders.WriteExport(0, ExportXml("<Menu ID=\"M\" title=\"&amp;Test\">" + XmlItem(1, "Alpha") + "<Separator/>" +
                                      XmlItem(2, "Beta") + XmlItem(3, "Gamma") + "</Menu>"));
    folders.WriteBin(TestBin({ Item(1, 0, 0), Separator(), Item(2, 0, 0) }, kGuidA), 10, L"a.bin");
    folders.WriteBin(TestBin({ Item(1, 0, 0), Separator(), Item(3, 0, 0) }, kGuidB), 20, L"b.bin");
}

static const std::vector<std::wstring> kAlphaGamma = { L"Alpha", L"Gamma" };

void TestOnlyProfileSchemeIsUsed() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    WriteSchemesAAndB(folders);
    folders.UseScheme(kGuidA);   // the older file; the newer one is readable and fits as well
    assert(Titles(IndexOf(frame.Hwnd()).commands) == kAlphaBeta);
    folders.UseScheme(kGuidB, 1);   // the profile changes, and the scheme with it
    assert(Titles(IndexOf(frame.Hwnd()).commands) == kAlphaGamma);
    std::cout << "[PASS] TestOnlyProfileSchemeIsUsed\n";
}

// Folder and file names are localized: the profile in use is found by its content, the named profiles by
// the ProfileSet.xml of the folder they lie in
void TestProfileFoundByContent() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    WriteSchemesAAndB(folders);
    WriteTestFile(folders.ArchicadFolder() / L"Recently used" / L"Profil" / L"active.xml", ProfileXml({ kGuidA }), 0);
    WriteTestFile(folders.ArchicadFolder() / L"Named" / L"ProfileSet.xml", "<ProfileSet/>", 0);
    WriteTestFile(folders.ArchicadFolder() / L"Named" / L"Profil" / L"other.xml", ProfileXml({ kGuidB }), 0);
    // An XML that is not a profile says nothing, whatever it names
    WriteTestFile(folders.ArchicadFolder() / L"Reports" / L"report.xml", Replaced(ProfileXml({ kGuidB }), "<Profile ", "<ProfileSet "), 0);
    assert(Titles(IndexOf(frame.Hwnd()).commands) == kAlphaBeta);
    std::cout << "[PASS] TestProfileFoundByContent\n";
}

void TestNamedProfilesAreNotTheActiveProfile() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    WriteSchemesAAndB(folders);
    folders.UseScheme(kGuidA);
    folders.WriteNamedProfile(ProfileXml({ kGuidB }));   // next to a ProfileSet.xml: not the one in use
    assert(Titles(IndexOf(frame.Hwnd()).commands) == kAlphaBeta);
    std::cout << "[PASS] TestNamedProfilesAreNotTheActiveProfile\n";
}

// The given profile files sit in work environment folders without a ProfileSet.xml
static void AssertNothingOfferedForProfiles(const std::vector<std::string>& profiles) {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    WriteSchemesAAndB(folders);
    for (size_t i = 0; i < profiles.size(); ++i) folders.WriteProfile(profiles[i], 0, (L"Recent " + std::to_wstring(i)).c_str());
    AssertNothingOffered(frame.Hwnd());
}

void TestMissingOrAmbiguousProfileOffersNothing() {
    AssertNothingOfferedForProfiles({});                                              // no profile file
    AssertNothingOfferedForProfiles({ ProfileXml({ kGuidA }), ProfileXml({ kGuidA }) });   // two profile files
    AssertNothingOfferedForProfiles({ ProfileXml({ kGuidA, kGuidB }) });              // two command schemes
    AssertNothingOfferedForProfiles({ ProfileXml({}) });                              // no command scheme
    AssertNothingOfferedForProfiles({ "this is not a profile" });
    AssertNothingOfferedForProfiles({ Replaced(ProfileXml({ kGuidA }), kProfileNamespace, "") });   // not in the profile namespace
    {   // Only named profiles: which one is in use is unknown
        TestFrame frame(Fill::Normal);
        TestFolders folders;
        WriteSchemesAAndB(folders);
        folders.WriteNamedProfile(ProfileXml({ kGuidA }));
        AssertNothingOffered(frame.Hwnd());
    }
    std::cout << "[PASS] TestMissingOrAmbiguousProfileOffersNothing\n";
}

// The scheme named by the profile is missing or not understood, and a readable standard scheme is there
void TestProfileSchemeUnreadableOffersNothing() {
    TestFrame frame(Fill::Normal);
    {
        TestFolders folders;
        folders.WriteExport(0);
        folders.WriteBin(TestBin(TestEntries(), kGuidB), 20, L"standard.bin");
        folders.UseScheme(kGuidA);
        AssertNothingOffered(frame.Hwnd());
    }
    {
        TestFolders folders;
        folders.WriteExport(0);
        folders.WriteBin(UnreadableBin(), 5, L"named.bin");
        folders.WriteBin(TestBin(TestEntries(), kGuidB), 20, L"standard.bin");
        folders.UseScheme(kGuidA);
        AssertNothingOffered(frame.Hwnd());
    }
    std::cout << "[PASS] TestProfileSchemeUnreadableOffersNothing\n";
}

void TestSchemeGuidFoundTwiceOffersNothing() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteExport(0);
    folders.WriteBin(TestBin(), 10, L"layout.bin", L"Archicad 29 DEU R1");
    folders.WriteBin(TestBin(), 20, L"layout.bin", L"Archicad 29 INT R1");
    folders.UseScheme();
    AssertNothingOffered(frame.Hwnd());
    std::cout << "[PASS] TestSchemeGuidFoundTwiceOffersNothing\n";
}

void TestSchemeFoundInAnyArchicadFolderOfTheVersion() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteExport(0);
    folders.WriteBin(TestBin(), 10, L"layout.bin", L"Archicad 29 INT R1");
    folders.WriteBin(TestBin(), 20, L"layout.bin", L"Archicad 30 INT R1");   // another version: not a candidate
    folders.UseScheme();   // the profile sits in a folder without a scheme file
    assert(Titles(IndexOf(frame.Hwnd()).commands) == kAlphaBeta);
    std::cout << "[PASS] TestSchemeFoundInAnyArchicadFolderOfTheVersion\n";
}

void TestProfileSchemeNotFittingOffersNothing() {
    {   // None of its menus is in the live menu bar, although the other scheme would fit
        TestFrame frame(Fill::Normal);
        TestFolders folders;
        WriteSchemesAAndB(folders);
        folders.WriteBin(BinOf({ Menu(L"M", L"&Elsewhere", TestEntries()) }, kGuidA), 10, L"a.bin");
        folders.UseScheme(kGuidA);
        AssertNothingOffered(frame.Hwnd());
        std::filesystem::remove(folders.Commands() / L"export.xml");   // without names the answer is the same
        AssertNothingOffered(frame.Hwnd());
    }
    {   // Its menu is there, but the live menu has entries it does not know: no command links
        TestFrame frame(Fill::ExtraItem);
        TestFolders folders;
        folders.WriteCurrent();
        AssertNothingOffered(frame.Hwnd());
    }
    std::cout << "[PASS] TestProfileSchemeNotFittingOffersNothing\n";
}

void TestBinWithoutMarkerIsNotACandidate() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteCurrent();
    folders.WriteBin("a newer file that is not a command scheme", 120, L"other.bin");
    assert(Titles(IndexOf(frame.Hwnd()).commands) == kAlphaBeta);
    std::cout << "[PASS] TestBinWithoutMarkerIsNotACandidate\n";
}

// --- States without commands ------------------------------------------------------------------

void TestNoExportState() {
    {
        TestFrame frame(Fill::Normal);
        TestFolders folders;
        folders.WriteBin(TestBin({ Item(1, 0, 0), Separator(), Item(7682, 0, 0) }), 0);
        folders.UseScheme();
        const CommandIndex index = IndexOf(frame.Hwnd());
        assert(index.state == CommandState::NoExport && index.commands.empty() && index.notSearchable == 0);
        // Work Environment is found from the scheme alone and runs like any command
        assert(index.workEnvironment.kind == ItemKind::Command && index.workEnvironment.liveId == 300);
        assert((index.workEnvironment.menuPosition == std::vector<int>{ 0, 2 }));
        assert(CommandSource::Run(index.workEnvironment, frame.Hwnd()));
        Pump();
        assert((g_commands == std::vector<unsigned>{ 300 }));
        // An export whose menu bar is not the live one gives no names either
        folders.WriteExport(0, ExportForOtherMenuBar());
        assert(IndexOf(frame.Hwnd()).state == CommandState::NoExport);
    }
    {   // Nothing links (the live menu has entries the scheme does not know): still the missing export is the news
        TestFrame frame(Fill::ExtraItem);
        TestFolders folders;
        folders.WriteBin(TestBin({ Item(1, 0, 0), Separator(), Item(7682, 0, 0) }), 0);
        folders.UseScheme();
        const CommandIndex index = IndexOf(frame.Hwnd());
        assert(index.state == CommandState::NoExport && index.commands.empty() && index.workEnvironment.liveId == 0);
    }
    std::cout << "[PASS] TestNoExportState\n";
}

void TestWorkEnvironmentIsFoundWithAnExport() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteExport(0, ExportXml("<Menu ID=\"M\" title=\"&amp;Test\">" + XmlItem(1, "Alpha") + "<Separator/>" +
                                      XmlItem(7682, "Work Environment") + "</Menu>"));
    folders.WriteBin(TestBin({ Item(1, 0, 0), Separator(), Item(7682, 0, 0) }), 0);
    folders.UseScheme();
    const CommandIndex index = IndexOf(frame.Hwnd());
    assert(index.state == CommandState::Ready && index.workEnvironment.liveId == 300);
    assert(Titles(index.commands) == (std::vector<std::wstring>{ L"Alpha", L"Work Environment" }));
    std::cout << "[PASS] TestWorkEnvironmentIsFoundWithAnExport\n";
}

void TestUnreadableBinState() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteExport(0);
    folders.WriteBin(UnreadableBin(), 0);
    folders.UseScheme();
    CommandIndex index = IndexOf(frame.Hwnd());
    assert(index.state == CommandState::Unreadable && index.commands.empty() && index.workEnvironment.liveId == 0);
    std::cout << "[PASS] TestUnreadableBinState\n";
}

void TestNoSchemeFileIsUnreadable() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteExport(0);
    folders.CreateArchicadFolder();
    folders.UseScheme();
    assert(IndexOf(frame.Hwnd()).state == CommandState::Unreadable);
    assert(IndexOf(frame.Hwnd()).commands.empty());
    std::cout << "[PASS] TestNoSchemeFileIsUnreadable\n";
}

void TestBuildCommandIndexWithoutMenu() {
    TestFolders folders;
    folders.WriteCurrent();
    assert(IndexOf(nullptr).commands.empty());
    HWND noMenu = CreateWindowExW(0, L"STATIC", L"no menu", WS_OVERLAPPED, 0, 0, 50, 50, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    assert(noMenu != nullptr);
    assert(IndexOf(noMenu).commands.empty());
    DestroyWindow(noMenu);
    std::cout << "[PASS] TestBuildCommandIndexWithoutMenu\n";
}

// --- Reading the files --------------------------------------------------------------------------

void TestOversizeExportIsNotUsed() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteBin(TestBin(), 0);
    folders.UseScheme();
    folders.WriteExport(60, std::string(kComponentType) + std::string(kMaxInputBytes, ' '));
    assert(IndexOf(frame.Hwnd()).state == CommandState::NoExport);
    folders.WriteExport(61);
    assert(IndexOf(frame.Hwnd()).commands.size() == 2);
    std::cout << "[PASS] TestOversizeExportIsNotUsed\n";
}

void TestOversizeBinIsUnreadable() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteExport(0);
    folders.UseScheme();
    folders.WriteBin(std::string(kComponentType) + std::string(kMaxInputBytes, ' '), 0);
    assert(IndexOf(frame.Hwnd()).state == CommandState::Unreadable);
    std::cout << "[PASS] TestOversizeBinIsUnreadable\n";
}

void TestUnreadableExportIsRetried() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteCurrent();
    {
        FileLock lock(folders.Commands() / L"export.xml");
        assert(IndexOf(frame.Hwnd()).state == CommandState::NoExport);
    }
    assert(IndexOf(frame.Hwnd()).commands.size() == 2);   // same file time, read again
    std::cout << "[PASS] TestUnreadableExportIsRetried\n";
}

void TestLockedSchemeFileIsUnreadable() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteCurrent();
    {
        // Whether the locked file is the scheme in use cannot be told, so nothing is offered
        FileLock lock(folders.SchemePath());
        assert(IndexOf(frame.Hwnd()).state == CommandState::Unreadable);
    }
    assert(IndexOf(frame.Hwnd()).commands.size() == 2);
    std::cout << "[PASS] TestLockedSchemeFileIsUnreadable\n";
}

void TestDirectoryNamedLikeAnExportIsSkipped() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteCurrent();
    std::filesystem::create_directories(folders.Commands() / L"folder.xml");
    std::filesystem::create_directories(folders.SchemePath().parent_path() / L"folder.bin");
    assert(IndexOf(frame.Hwnd()).commands.size() == 2);
    std::cout << "[PASS] TestDirectoryNamedLikeAnExportIsSkipped\n";
}

// Without a user profile the commands folder is unknown; a ".baaz\commands" below the current folder is not it
void TestNoUserProfileOffersNothing() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    WriteTestFile(folders.Root() / L".baaz" / L"commands" / L"export.xml", kExportXml, 60);
    wchar_t savedProfile[2048] = {};
    const DWORD savedLength = GetEnvironmentVariableW(L"USERPROFILE", savedProfile, 2048);
    wchar_t savedDirectory[2048] = {};
    assert(GetCurrentDirectoryW(2048, savedDirectory) > 0);
    assert(SetCurrentDirectoryW(folders.Root().c_str()));
    assert(SetEnvironmentVariableW(L"USERPROFILE", nullptr));
    CommandSource::SetFoldersForTest(L"", folders.AppData().wstring());   // no test override: the real lookup
    const CommandIndex index = IndexOf(frame.Hwnd());
    SetCurrentDirectoryW(savedDirectory);
    if (savedLength > 0) SetEnvironmentVariableW(L"USERPROFILE", savedProfile);
    assert(index.commands.empty());
    std::cout << "[PASS] TestNoUserProfileOffersNothing\n";
}

void TestExportIsReReadOnlyWhenItChanges() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteBin(TestBin(), 0);
    folders.UseScheme();
    folders.WriteExport(60);
    assert(IndexOf(frame.Hwnd()).commands.size() == 2);
    folders.WriteExport(60, ExportForOtherMenuBar());   // same path, file time and size: the first parse is kept
    assert(IndexOf(frame.Hwnd()).commands.size() == 2);
    folders.WriteExport(61, ExportForOtherMenuBar());   // new file time: read again, and it no longer fits
    assert(IndexOf(frame.Hwnd()).state == CommandState::NoExport);
    folders.WriteExport(61, kExportXml + " ");          // same file time, new size: read again
    assert(IndexOf(frame.Hwnd()).commands.size() == 2);
    std::cout << "[PASS] TestExportIsReReadOnlyWhenItChanges\n";
}

void TestBinIsReReadOnlyWhenItChanges() {
    TestFrame frame(Fill::Normal);
    TestFolders folders;
    folders.WriteExport(0);
    folders.UseScheme();
    folders.WriteBin(TestBin(), 60);
    assert(Titles(IndexOf(frame.Hwnd()).commands) == kAlphaBeta);
    folders.WriteBin(TestBin({ Item(2, 0, 0), Separator(), Item(1, 0, 0) }), 60);   // same time, same size: kept
    assert(Titles(IndexOf(frame.Hwnd()).commands) == kAlphaBeta);
    folders.WriteBin(TestBin({ Item(2, 0, 0), Separator(), Item(1, 0, 0) }), 61);   // new time: read again
    assert((Titles(IndexOf(frame.Hwnd()).commands) == std::vector<std::wstring>{ L"Beta", L"Alpha" }));
    std::cout << "[PASS] TestBinIsReReadOnlyWhenItChanges\n";
}

int main() {
    TestFillsOnDemand();
    TestRunPostsCommand();
    TestRunRefusesDisabled();
    TestRunRefusesChangedId();
    TestRunRefusesIdLongerThanACommand();
    TestRunRefusesWrongKindOrPosition();
    TestRunRefusesDisabledFrame();
    TestNestedPopups();
    TestOffersCommandsWhenCurrent();
    TestShiftedTopLevelPositionsAreFollowed();
    TestRunAfterMenuRebuiltSinceIndexing();
    TestIndexOffersGreyedItemDisabled();
    TestExportFoundBySchemeMarker();
    TestNewestFittingExportGivesNames();
    TestUnnamedCommandIsCountedNotOffered();
    TestPlaceholderIsNeverCounted();
    TestMovedCommandKeepsItsName();
    TestMenuChangeWithoutExportStillOffers();
    TestUnloadedAddOnItemsDoNotBlockLinking();
    TestUnloadedAddOnNextToPlaceholderLinksNothing();
    TestAddOnInstalledSinceExportLinksNothing();
    TestUncertainPartWithCountMismatchIsNotCounted();
    TestBuiltInPartAroundPlaceholderStillLinks();
    TestNoExportAddOnPartLinksNoWorkEnvironment();
    TestCountMismatchPartIsNotCounted();
    TestItemNotShownLeavesItsPartLinked();
    TestPopupLeftEmptyLeavesItsPartLinked();
    TestPopupOfAbsentAddOnLeavesTheMenuLinked();
    TestSurplusSeparatorsLeaveTheMenuLinked();
    TestItemShownAfterAllLeavesItsPartUnlinked();
    TestItemNotShownNextToPlaceholderMakesItsPartUncertain();
    TestSchemeIsReducedWithoutAnExport();
    TestVanishedPlaceholderPartLeavesTheOthersLinked();
    TestRegionWithStaticEntryLinksNothingAround();
    TestPartNextToVanishedPartStillNeedsItsCount();
    TestOnlyProfileSchemeIsUsed();
    TestProfileFoundByContent();
    TestNamedProfilesAreNotTheActiveProfile();
    TestMissingOrAmbiguousProfileOffersNothing();
    TestProfileSchemeUnreadableOffersNothing();
    TestProfileSchemeNotFittingOffersNothing();
    TestSchemeFoundInAnyArchicadFolderOfTheVersion();
    TestSchemeGuidFoundTwiceOffersNothing();
    TestBinWithoutMarkerIsNotACandidate();
    TestNoExportState();
    TestWorkEnvironmentIsFoundWithAnExport();
    TestUnreadableBinState();
    TestNoSchemeFileIsUnreadable();
    TestBuildCommandIndexWithoutMenu();
    TestOversizeExportIsNotUsed();
    TestOversizeBinIsUnreadable();
    TestUnreadableExportIsRetried();
    TestLockedSchemeFileIsUnreadable();
    TestDirectoryNamedLikeAnExportIsSkipped();
    TestNoUserProfileOffersNothing();
    TestExportIsReReadOnlyWhenItChanges();
    TestBinIsReReadOnlyWhenItChanges();
    std::cout << "All live menu tests passed.\n";
    return 0;
}
