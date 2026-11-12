#pragma once

#include <windows.h>
#include <string>
#include "CommandHint.hpp"
#include "SearchEngine.hpp"

namespace Baaz {

// The search palette: a focusless, layered popup centered over Archicad.
// It never takes focus; MessageHook feeds it keystrokes.
class HudWindow {
public:
    static bool Initialize(HINSTANCE hInstance);
    static void Shutdown();
    static void Show(HWND hParentWnd);
    static void Hide();
    static bool IsVisible() { return s_visible; }
    static const std::wstring& GetQuery() { return s_query; }
    static bool IsQueryEmpty() { return s_query.empty(); }
    static void HandleKey(WPARAM vk, WCHAR ch);
    static RECT GetBoundingRectScreen() { return s_rect; }
    // What the command index says about missing commands; redraws the open HUD
    static void ApplyCommandIndex(const CommandIndex& index);
    static const HintRow& GetHint() { return s_hint; }
    static bool IsHintSelected() { return s_hintSelected; }

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static void SetQuery(std::wstring query);
    static void RebuildHint();
    static void Render();
    static void Reposition();
    static void ExecuteSelection();
    static void ClickAt(float y);
    static void MoveUp();
    static bool LeaveHint();

    static HWND s_hWnd;
    static HWND s_hParentWnd;
    static bool s_visible;
    static std::wstring s_query;
    static std::vector<SearchItem> s_results;
    static size_t s_selectedIndex;
    static SearchItem s_pendingItem;
    static CommandState s_commandState;
    static size_t s_notSearchable;
    static SearchItem s_workEnvironment;
    static bool s_runRefused;
    static HintRow s_hint;
    static bool s_hintSelected;
    static RECT s_rect;
    static float s_scale;
};

} // namespace Baaz
