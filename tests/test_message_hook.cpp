// Edge-case tests for the Spacebar hook (MessageHook) driven through a real
// Win32 message queue, without Archicad. Built without ACAPI_VERSION, so a
// project always counts as open and tools "execute" by synthesizing their
// chord keys, which the SendInput stub records.

#include "MessageHook.hpp"
#include "HudWindow.hpp"
#include "CommandExecutor.hpp"
#include "SearchEngine.hpp"
#include "BaazAddon.hpp"
#include <windows.h>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace Baaz;

namespace Baaz {
HINSTANCE AddonInstance() { return GetModuleHandleW(NULL); }
HWND g_frame = NULL;
HWND MainWindow() { return g_frame; }
}

static std::vector<INPUT> g_sent;
static std::vector<MSG> g_delivered;
static std::vector<unsigned> g_commands;
static int g_failures = 0;
static HWND g_main = NULL;
static HWND g_edit = NULL;
static HWND g_dgRichEdit = NULL;

UINT WINAPI TestSendInput(UINT cInputs, LPINPUT pInputs, int /*cbSize*/) {
    for (UINT i = 0; i < cInputs; ++i) g_sent.push_back(pInputs[i]);
    return cInputs;
}

#define CHECK(cond) do { if (!(cond)) { std::cout << "    FAILED: " #cond " (line " << __LINE__ << ")\n"; ++g_failures; } } while (0)

static LPARAM KeyLParam(WPARAM vk, bool up, bool repeat = false) {
    LPARAM lp = 1 | (static_cast<LPARAM>(MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC)) << 16);
    if (repeat || up) lp |= (1LL << 30);
    if (up) lp |= (1LL << 31);
    return lp;
}

// Archicad's event loop peeks at pending messages before removing them; the
// hook runs on both passes. peekFirst reproduces that.
static void Pump(bool peekFirst) {
    MSG m;
    for (;;) {
        if (peekFirst && !PeekMessageW(&m, NULL, 0, 0, PM_NOREMOVE)) break;
        if (!PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) break;
        if (m.message == WM_KEYDOWN || m.message == WM_KEYUP) g_delivered.push_back(m);
        if (m.message == WM_COMMAND && m.hwnd == g_main) g_commands.push_back(LOWORD(m.wParam));
        DispatchMessageW(&m);
    }
}

static void PumpFor(DWORD ms, bool peekFirst) {
    ULONGLONG end = GetTickCount64() + ms;
    while (GetTickCount64() < end) {
        Pump(peekFirst);
        Sleep(5);
    }
    Pump(peekFirst);
}

static void Key(UINT msg, WPARAM vk, bool peekFirst, bool repeat = false) {
    HWND target = GetFocus() ? GetFocus() : g_main;
    PostMessageW(target, msg, vk, KeyLParam(vk, msg == WM_KEYUP, repeat));
    Pump(peekFirst);
}

static void Tap(WPARAM vk, bool peekFirst) {
    Key(WM_KEYDOWN, vk, peekFirst);
    Key(WM_KEYUP, vk, peekFirst);
}

static void Type(const wchar_t* text, bool peekFirst) {
    for (const wchar_t* c = text; *c; ++c) Tap(static_cast<WPARAM>(towupper(*c)), peekFirst);
}

static int WandPresses() {
    int n = 0;
    for (const INPUT& in : g_sent)
        if (in.ki.wVk == VK_SPACE && in.ki.dwExtraInfo == SENTINEL_MAGIC_WAND && !(in.ki.dwFlags & KEYEVENTF_KEYUP)) ++n;
    return n;
}

static int WandReleases() {
    int n = 0;
    for (const INPUT& in : g_sent)
        if (in.ki.wVk == VK_SPACE && in.ki.dwExtraInfo == SENTINEL_MAGIC_WAND && (in.ki.dwFlags & KEYEVENTF_KEYUP)) ++n;
    return n;
}

static std::wstring ExecutedChord() {
    std::wstring keys;
    for (const INPUT& in : g_sent)
        if (in.ki.dwExtraInfo != SENTINEL_MAGIC_WAND && !(in.ki.dwFlags & KEYEVENTF_KEYUP)) keys.push_back(static_cast<wchar_t>(in.ki.wVk));
    return keys;
}

static int DeliveredSpaces() {
    int n = 0;
    for (const MSG& m : g_delivered) if (m.wParam == VK_SPACE) ++n;
    return n;
}

static void Reset() {
    if (HudWindow::IsVisible()) HudWindow::Hide();
    PumpFor(450, false);  // let any hold timer from the previous test expire
    BYTE keys[256] = {};
    SetKeyboardState(keys);
    SetFocus(g_main);
    EnableWindow(g_main, TRUE);
    g_sent.clear();
    g_delivered.clear();
    g_commands.clear();
}

static void Run(const char* name, void (*test)(bool), bool peekFirst) {
    Reset();
    int before = g_failures;
    test(peekFirst);
    std::cout << (g_failures == before ? "[PASS] " : "[FAIL] ") << name << (peekFirst ? " (peek+remove)" : " (remove only)") << "\n";
}

// --- HUD closed --------------------------------------------------------------

static void TapOpensHud(bool peek) {
    Tap(VK_SPACE, peek);
    CHECK(HudWindow::IsVisible());
    CHECK(DeliveredSpaces() == 0);
    PumpFor(600, peek);  // no leftover hold timer may fire later
    CHECK(WandPresses() == 0);
    CHECK(HudWindow::IsVisible());
}

static void TwentyTapsInARow(bool peek) {
    int opened = 0;
    for (int i = 0; i < 20; ++i) {
        Tap(VK_SPACE, peek);
        if (HudWindow::IsVisible()) ++opened;
        Tap(VK_ESCAPE, peek);
        PumpFor(120 + (i % 4) * 90, peek);  // vary the gap around the 350 ms hold time
    }
    CHECK(opened == 20);
    CHECK(WandPresses() == 0);
}

static void HoldEngagesMagicWand(bool peek) {
    Key(WM_KEYDOWN, VK_SPACE, peek);
    PumpFor(450, peek);
    CHECK(WandPresses() == 1);
    // Typematic repeats while holding are swallowed, not passed to Archicad
    Key(WM_KEYDOWN, VK_SPACE, peek, true);
    Key(WM_KEYDOWN, VK_SPACE, peek, true);
    CHECK(DeliveredSpaces() == 0);
    CHECK(WandPresses() == 1);
    Key(WM_KEYUP, VK_SPACE, peek);
    CHECK(WandReleases() == 1);
    CHECK(!HudWindow::IsVisible());
}

static void TapJustUnderHoldTime(bool peek) {
    Key(WM_KEYDOWN, VK_SPACE, peek);
    PumpFor(250, peek);
    Key(WM_KEYUP, VK_SPACE, peek);
    CHECK(HudWindow::IsVisible());
    CHECK(WandPresses() == 0);
}

static void ClickDuringHoldEngagesWandAtOnce(bool peek) {
    Key(WM_KEYDOWN, VK_SPACE, peek);
    PostMessageW(g_main, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
    Pump(peek);
    CHECK(WandPresses() == 1);
    PumpFor(450, peek);
    CHECK(WandPresses() == 1);  // the hold timer must not fire a second time
    Key(WM_KEYUP, VK_SPACE, peek);
    CHECK(WandReleases() == 1);
    CHECK(!HudWindow::IsVisible());
}

static void TextFieldGetsTheSpace(bool peek) {
    SetFocus(g_edit);
    Tap(VK_SPACE, peek);
    CHECK(!HudWindow::IsVisible());
    CHECK(DeliveredSpaces() == 2);
    PumpFor(450, peek);
    CHECK(WandPresses() == 0);
}

// Archicad's text editor on the plan is a DG rich edit, not a Win32 Edit
static void DgRichEditKeepsTheSpace(bool peek) {
    SetFocus(g_dgRichEdit);
    Tap(VK_SPACE, peek);
    CHECK(!HudWindow::IsVisible());
    CHECK(DeliveredSpaces() == 2);
    PumpFor(450, peek);
    CHECK(WandPresses() == 0);
}

static void ModalDialogGetsTheSpace(bool peek) {
    EnableWindow(g_main, FALSE);
    Tap(VK_SPACE, peek);
    CHECK(!HudWindow::IsVisible());
    CHECK(DeliveredSpaces() == 2);
    EnableWindow(g_main, TRUE);
}

static void LostKeyUpThenTap(bool peek) {
    // Space released while another app had focus: the keyup never arrives
    Key(WM_KEYDOWN, VK_SPACE, peek);
    Key(WM_KEYDOWN, VK_SPACE, peek);  // fresh press, not a repeat
    Key(WM_KEYUP, VK_SPACE, peek);
    CHECK(HudWindow::IsVisible());
    PumpFor(600, peek);
    CHECK(WandPresses() == 0);
}

// --- HUD open ----------------------------------------------------------------

static void TypingDoesNotDoubleLetters(bool peek) {
    Tap(VK_SPACE, peek);
    Type(L"wa", peek);
    CHECK(HudWindow::GetQuery() == L"wa");
    CHECK(g_delivered.empty() || DeliveredSpaces() == 0);
}

static void SpaceAfterChordRunsIt(bool peek) {
    Tap(VK_SPACE, peek);
    Type(L"wa", peek);
    g_delivered.clear();
    Tap(VK_SPACE, peek);
    Pump(peek);  // deferred execution message
    CHECK(!HudWindow::IsVisible());
    CHECK(ExecutedChord() == L"WA");
    CHECK(DeliveredSpaces() == 0);  // keyup swallowed too
}

static void EnterRunsSelection(bool peek) {
    Tap(VK_SPACE, peek);
    Type(L"sl", peek);
    g_delivered.clear();
    Tap(VK_RETURN, peek);
    Pump(peek);
    CHECK(!HudWindow::IsVisible());
    CHECK(ExecutedChord() == L"SL");
    CHECK(g_delivered.empty());
}

static void ShiftSpaceTypesSpace(bool peek) {
    Tap(VK_SPACE, peek);
    Type(L"wa", peek);
    BYTE keys[256] = {};
    keys[VK_SHIFT] = 0x80;
    SetKeyboardState(keys);
    Tap(VK_SPACE, peek);
    keys[VK_SHIFT] = 0;
    SetKeyboardState(keys);
    CHECK(HudWindow::IsVisible());
    CHECK(HudWindow::GetQuery() == L"wa ");
    CHECK(ExecutedChord().empty());
}

static void BackspaceAndArrows(bool peek) {
    Tap(VK_SPACE, peek);
    Type(L"wax", peek);
    Tap(VK_BACK, peek);
    CHECK(HudWindow::GetQuery() == L"wa");
    Tap(VK_DOWN, peek);
    Tap(VK_UP, peek);
    CHECK(HudWindow::IsVisible());
    CHECK(g_delivered.empty());
}

static void EscapeCloses(bool peek) {
    Tap(VK_SPACE, peek);
    Type(L"wa", peek);
    Tap(VK_ESCAPE, peek);
    CHECK(!HudWindow::IsVisible());
    CHECK(ExecutedChord().empty());
    // Reopening starts with an empty query
    Tap(VK_SPACE, peek);
    CHECK(HudWindow::IsVisible());
    CHECK(HudWindow::IsQueryEmpty());
}

static void SpaceOnEmptyQueryCloses(bool peek) {
    Tap(VK_SPACE, peek);
    Tap(VK_SPACE, peek);
    Pump(peek);
    CHECK(!HudWindow::IsVisible());
    CHECK(ExecutedChord().empty());
    CHECK(DeliveredSpaces() == 0);  // the closing tap doesn't reach Archicad
    PumpFor(450, peek);
    CHECK(WandPresses() == 0);
    // ...and the next tap opens it again
    Tap(VK_SPACE, peek);
    CHECK(HudWindow::IsVisible());
}

static void EnterOnEmptyQueryRunsTopResult(bool peek) {
    Tap(VK_SPACE, peek);
    Tap(VK_RETURN, peek);
    Pump(peek);
    CHECK(!HudWindow::IsVisible());
    CHECK(ExecutedChord() == L"WA");
}

static void HudReopensAfterRunningCommand(bool peek) {
    Tap(VK_SPACE, peek);
    Type(L"wa", peek);
    Tap(VK_SPACE, peek);
    Pump(peek);
    g_sent.clear();
    Tap(VK_SPACE, peek);
    CHECK(HudWindow::IsVisible());
    PumpFor(450, peek);
    CHECK(WandPresses() == 0);
}

// --- Hint row ----------------------------------------------------------------
// Without a frame window the commands cannot be read, so the HUD shows the hint for that state.

static const unsigned kWorkEnvironmentId = 7000;

// A frame menu bar with one Work Environment command at {0, 0}, as BuildCommandIndex would find it
struct WorkEnvironmentMenu {
    HMENU bar = CreateMenu();
    WorkEnvironmentMenu() {
        HMENU popup = CreatePopupMenu();
        AppendMenuW(popup, MF_STRING, kWorkEnvironmentId, L"Work Environment");
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(popup), L"Options");
        SetMenu(g_main, bar);
        g_frame = g_main;
    }
    ~WorkEnvironmentMenu() {
        g_frame = NULL;
        SetMenu(g_main, NULL);
        DestroyMenu(bar);
    }
    WorkEnvironmentMenu(const WorkEnvironmentMenu&) = delete;
    WorkEnvironmentMenu& operator=(const WorkEnvironmentMenu&) = delete;
};

static CommandIndex NoExportIndex() {
    CommandIndex index;
    index.state = CommandState::NoExport;
    index.workEnvironment.kind = ItemKind::Command;
    index.workEnvironment.title = L"Work Environment";
    index.workEnvironment.menuPosition = { 0, 0 };
    index.workEnvironment.liveId = kWorkEnvironmentId;
    return index;
}

// The index refresh of a fresh HUD is pumped first, so it cannot replace what a test applies afterwards
static void OpenHud(bool peek) {
    Tap(VK_SPACE, peek);
    Pump(peek);
}

static void HintIsNeverTheDefaultSelection(bool peek) {
    OpenHud(peek);
    CHECK(!HudWindow::GetHint().title.empty());
    CHECK(!HudWindow::IsHintSelected());
    Type(L"sl", peek);
    CHECK(!HudWindow::IsHintSelected());
    Tap(VK_RETURN, peek);
    Pump(peek);
    CHECK(!HudWindow::IsVisible());
    CHECK(ExecutedChord() == L"SL");
}

static void UpReachesHintAndDownLeavesIt(bool peek) {
    OpenHud(peek);
    Tap(VK_DOWN, peek);
    Tap(VK_UP, peek);
    CHECK(!HudWindow::IsHintSelected());   // back on the first result, not on the hint
    Tap(VK_UP, peek);
    CHECK(HudWindow::IsHintSelected());
    Tap(VK_UP, peek);
    CHECK(HudWindow::IsHintSelected());
    Tap(VK_DOWN, peek);
    CHECK(!HudWindow::IsHintSelected());
    Tap(VK_UP, peek);
    Tap(VK_TAB, peek);
    CHECK(!HudWindow::IsHintSelected());
}

static void EnterOnHintWithoutActionDoesNothing(bool peek) {
    OpenHud(peek);
    Tap(VK_UP, peek);
    Tap(VK_RETURN, peek);
    Pump(peek);
    CHECK(HudWindow::IsVisible());
    CHECK(HudWindow::IsHintSelected());
    CHECK(ExecutedChord().empty());
}

static void EnterOnHintOpensWorkEnvironment(bool peek) {
    OpenHud(peek);
    WorkEnvironmentMenu menu;
    HudWindow::ApplyCommandIndex(NoExportIndex());
    CHECK(HudWindow::GetHint().canOpenWorkEnvironment);
    Tap(VK_UP, peek);
    Tap(VK_RETURN, peek);
    Pump(peek);
    CHECK(!HudWindow::IsVisible());
    CHECK((g_commands == std::vector<unsigned>{ kWorkEnvironmentId }));
}

static void SpaceNeverRunsHint(bool peek) {
    OpenHud(peek);
    WorkEnvironmentMenu menu;
    HudWindow::ApplyCommandIndex(NoExportIndex());
    Type(L"sl", peek);
    Tap(VK_UP, peek);
    CHECK(HudWindow::IsHintSelected());
    Tap(VK_SPACE, peek);
    Pump(peek);
    CHECK(HudWindow::IsVisible());
    CHECK(HudWindow::GetQuery() == L"sl ");
    CHECK(ExecutedChord().empty());
    CHECK(g_commands.empty());
}

static BOOL CALLBACK FindHudOfThread(HWND window, LPARAM found) {
    wchar_t name[64] = {};
    if (GetClassNameW(window, name, static_cast<int>(std::size(name))) == 0 || wcscmp(name, L"BaazHUDWindowClass") != 0) return TRUE;
    *reinterpret_cast<HWND*>(found) = window;
    return FALSE;
}

// The HUD of this test only: an Archicad running Baaz on the same desktop has a window of the same class
static HWND OwnHudWindow() {
    HWND found = NULL;
    EnumThreadWindows(GetCurrentThreadId(), FindHudOfThread, reinterpret_cast<LPARAM>(&found));
    return found;
}

// 34 is the height of the search field in 96-DPI pixels; a few pixels below it is the hint row
static void ClickHintRow() {
    const float scale = GetDpiForWindow(g_main) / 96.0f;
    const int y = static_cast<int>((34.0f + 5.0f) * scale);
    HWND hud = OwnHudWindow();
    CHECK(hud != NULL);
    if (hud != NULL) SendMessageW(hud, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100, y));
}

static void ClickOnHintOpensWorkEnvironment(bool peek) {
    OpenHud(peek);
    WorkEnvironmentMenu menu;
    HudWindow::ApplyCommandIndex(NoExportIndex());
    ClickHintRow();
    Pump(peek);
    CHECK(!HudWindow::IsVisible());
    CHECK((g_commands == std::vector<unsigned>{ kWorkEnvironmentId }));
}

// A click that reaches the hidden window must not run what the HUD offered when it was last open
static void ClickOnHiddenHudRunsNothing(bool peek) {
    OpenHud(peek);
    WorkEnvironmentMenu menu;
    HudWindow::ApplyCommandIndex(NoExportIndex());
    HudWindow::Hide();
    ClickHintRow();
    Pump(peek);
    CHECK(!HudWindow::IsVisible());
    CHECK(g_commands.empty());
}

static bool HintMentionsRefusedRun() {
    const std::vector<std::wstring>& guide = HudWindow::GetHint().guide;
    return !guide.empty() && guide.back().find(L"did not run") != std::wstring::npos;
}

static void RefusedRunIsShownOnce(bool peek) {
    SearchItem item;
    item.kind = ItemKind::Command;   // nothing to run in: no frame window
    CHECK(!CommandExecutor::Execute(item));
    OpenHud(peek);
    CHECK(HintMentionsRefusedRun());
    Tap(VK_ESCAPE, peek);
    OpenHud(peek);
    CHECK(!HintMentionsRefusedRun());
}

static void SwitchingAppsClosesHud(bool peek) {
    Tap(VK_SPACE, peek);
    CHECK(HudWindow::IsVisible());
    SendMessageW(g_main, WM_ACTIVATEAPP, FALSE, 0);
    CHECK(!HudWindow::IsVisible());
}

static void SwitchingAppsDuringHoldSendsNothing(bool peek) {
    Key(WM_KEYDOWN, VK_SPACE, peek);
    SendMessageW(g_main, WM_ACTIVATEAPP, FALSE, 0);  // Alt+Tab with Space down
    PumpFor(600, peek);
    CHECK(WandPresses() == 0);
    SendMessageW(g_main, WM_ACTIVATEAPP, TRUE, 0);
    Tap(VK_SPACE, peek);  // back in Archicad: a tap still works
    CHECK(HudWindow::IsVisible());
}

int main() {
    HINSTANCE hInst = GetModuleHandleW(NULL);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hInst;
    wc.lpszClassName = L"BaazHookTestMain";
    RegisterClassW(&wc);
    g_main = CreateWindowExW(0, wc.lpszClassName, L"Archicad 30 (hook test)", WS_OVERLAPPEDWINDOW,
                             -2000, -2000, 400, 300, NULL, NULL, hInst, NULL);
    g_edit = CreateWindowExW(0, L"Edit", L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, 20, g_main, NULL, hInst, NULL);
    WNDCLASSW dgClass = {};
    dgClass.lpfnWndProc = DefWindowProcW;
    dgClass.hInstance = hInst;
    dgClass.lpszClassName = L"DGUniRichEditClass";
    RegisterClassW(&dgClass);
    g_dgRichEdit = CreateWindowExW(0, dgClass.lpszClassName, L"", WS_CHILD | WS_VISIBLE, 0, 30, 100, 20, g_main, NULL, hInst, NULL);
    ShowWindow(g_main, SW_SHOWNOACTIVATE);

    SearchEngine::Initialize();
    HudWindow::Initialize(hInst);
    if (!MessageHook::Install(g_main)) {
        std::cout << "Could not install hooks\n";
        return 1;
    }

    std::cout << "Running Spacebar hook edge-case tests...\n";
    for (bool peek : { false, true }) {
        Run("Tap opens HUD, no stray timer", TapOpensHud, peek);
        Run("20 taps in a row all open HUD", TwentyTapsInARow, peek);
        Run("Hold engages Magic Wand", HoldEngagesMagicWand, peek);
        Run("Tap just under hold time", TapJustUnderHoldTime, peek);
        Run("Click during hold engages wand at once", ClickDuringHoldEngagesWandAtOnce, peek);
        Run("Text field keeps the space", TextFieldGetsTheSpace, peek);
        Run("DG rich edit keeps the space", DgRichEditKeepsTheSpace, peek);
        Run("Modal dialog keeps the space", ModalDialogGetsTheSpace, peek);
        Run("Lost keyup, then tap", LostKeyUpThenTap, peek);
        Run("Typing does not double letters", TypingDoesNotDoubleLetters, peek);
        Run("Space after chord runs it", SpaceAfterChordRunsIt, peek);
        Run("Enter runs selection", EnterRunsSelection, peek);
        Run("Shift+Space types a space", ShiftSpaceTypesSpace, peek);
        Run("Backspace and arrows", BackspaceAndArrows, peek);
        Run("Escape closes and clears", EscapeCloses, peek);
        Run("Space on empty query closes HUD", SpaceOnEmptyQueryCloses, peek);
        Run("Enter on empty query runs top result", EnterOnEmptyQueryRunsTopResult, peek);
        Run("HUD reopens after running a command", HudReopensAfterRunningCommand, peek);
        Run("Hint is never the default selection", HintIsNeverTheDefaultSelection, peek);
        Run("Up reaches the hint, Down leaves it", UpReachesHintAndDownLeavesIt, peek);
        Run("Enter on a hint without action does nothing", EnterOnHintWithoutActionDoesNothing, peek);
        Run("Enter on the hint opens Work Environment", EnterOnHintOpensWorkEnvironment, peek);
        Run("Space never runs the hint", SpaceNeverRunsHint, peek);
        Run("Click on the hint opens Work Environment", ClickOnHintOpensWorkEnvironment, peek);
        Run("A click on the hidden HUD runs nothing", ClickOnHiddenHudRunsNothing, peek);
        Run("A refused run is shown once", RefusedRunIsShownOnce, peek);
        Run("Switching apps closes HUD", SwitchingAppsClosesHud, peek);
        Run("Switching apps during hold sends nothing", SwitchingAppsDuringHoldSendsNothing, peek);
    }

    MessageHook::Uninstall();
    HudWindow::Shutdown();
    DestroyWindow(g_main);

    std::cout << (g_failures == 0 ? "\nALL HOOK TESTS PASSED\n" : "\nHOOK TESTS FAILED\n");
    return g_failures == 0 ? 0 : 1;
}
