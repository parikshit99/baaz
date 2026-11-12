#include "HudWindow.hpp"
#include "BaazAddon.hpp"
#include "CommandExecutor.hpp"
#include "CommandSource.hpp"
#include "NavigatorIndexer.hpp"
#include <windowsx.h>
#include <gdiplus.h>
#include <memory>

#ifdef ACAPI_VERSION
#include "APIEnvir.h"
#include "ACAPinc.h"
#include "DGDLLExport.h"  // DGColorCatalog.hpp in the AC 29 DevKit does not include it itself
#include "AppearanceManager/DGColorCatalog.hpp"
#endif

using namespace Gdiplus;

namespace Baaz {

HWND HudWindow::s_hWnd = NULL;
HWND HudWindow::s_hParentWnd = NULL;
bool HudWindow::s_visible = false;
std::wstring HudWindow::s_query;
std::vector<SearchItem> HudWindow::s_results;
size_t HudWindow::s_selectedIndex = 0;
SearchItem HudWindow::s_pendingItem;
// Ready until the first index refresh arrives, so a fresh HUD does not flash a hint
CommandState HudWindow::s_commandState = CommandState::Ready;
size_t HudWindow::s_notSearchable = 0;
SearchItem HudWindow::s_workEnvironment;
bool HudWindow::s_runRefused = false;
HintRow HudWindow::s_hint;
bool HudWindow::s_hintSelected = false;
RECT HudWindow::s_rect = {};
float HudWindow::s_scale = 1.0f;

static const wchar_t* HUD_CLASS_NAME = L"BaazHUDWindowClass";
static const UINT WM_BAAZ_EXECUTE = WM_APP + 1;
static const UINT WM_BAAZ_REFRESH_INDEX = WM_APP + 2;

// Layout in 96-DPI pixels, sized to sit alongside Archicad's own palettes
static const float kWidth = 480.0f;
static const float kSearchH = 34.0f;
static const float kRowH = 26.0f;
static const float kGuideH = 18.0f;
static const float kFooterH = 22.0f;
static const float kRadius = 4.0f;
static const float kPadX = 12.0f;

// Colors follow Archicad's light/dark theme via DG's color catalog, falling
// back to Windows system colors when built without the API (tests).
struct HudPalette {
    Color background, border, divider, text, mutedText, selectedBackground, selectedText;
};

// Segoe UI 9pt is Archicad's UI font on Windows; rebuilt only when the DPI changes
struct HudFonts {
    float scale = 0.0f;
    std::unique_ptr<Font> query, row, hint;
};

static ULONG_PTR s_gdiplusToken = 0;
static HudPalette s_palette;
static HudFonts s_fonts;

// 32-bit premultiplied DIB that UpdateLayeredWindow blits from
static HDC s_memDC = NULL;
static HBITMAP s_hBitmap = NULL;
static HGDIOBJ s_hOldBitmap = NULL;
static void* s_pBits = nullptr;
static SIZE s_bitmapSize = {};

static Color WithAlpha(const Color& c, BYTE a) {
    return Color(a, c.GetR(), c.GetG(), c.GetB());
}

static Color Mix(const Color& a, const Color& b, float t) {
    auto lerp = [t](BYTE x, BYTE y) { return static_cast<BYTE>(x + (y - x) * t + 0.5f); };
    return Color(255, lerp(a.GetR(), b.GetR()), lerp(a.GetG(), b.GetG()), lerp(a.GetB(), b.GetB()));
}

#ifdef ACAPI_VERSION
static Color CatalogColor(DG::ColorId id) {
    Gfx::Color c = DG::ColorCatalog::GetColor(id);
    return Color(255, c.GetRed(), c.GetGreen(), c.GetBlue());
}
#else
static Color SysColor(int index) {
    COLORREF c = GetSysColor(index);
    return Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}
#endif

static HudPalette LoadPalette() {
    HudPalette p;
#ifdef ACAPI_VERSION
#if ACAPI_VERSION >= 30
    p.background = CatalogColor(DG::ColorId::DialogBackgroundColor);
#else
    p.background = CatalogColor(DG::ColorId::WindowBackgroundColor);  // AC 29 has no DialogBackgroundColor
#endif
    p.text = CatalogColor(DG::ColorId::StandardTextColor);
    p.mutedText = CatalogColor(DG::ColorId::DisabledControlTextColor);
    p.selectedBackground = CatalogColor(DG::ColorId::SelectedContentBackgroundColor);
    p.selectedText = CatalogColor(DG::ColorId::SelectedTextColor);
#else
    p.background = SysColor(COLOR_BTNFACE);
    p.text = SysColor(COLOR_WINDOWTEXT);
    p.mutedText = SysColor(COLOR_GRAYTEXT);
    p.selectedBackground = SysColor(COLOR_HIGHLIGHT);
    p.selectedText = SysColor(COLOR_HIGHLIGHTTEXT);
#endif
    p.border = Mix(p.background, p.text, 0.28f);
    p.divider = Mix(p.background, p.text, 0.14f);
    return p;
}

static const HudFonts& FontsForScale(float scale) {
    if (s_fonts.scale != scale) {
        FontFamily family(L"Segoe UI");
        s_fonts.scale = scale;
        s_fonts.query = std::make_unique<Font>(&family, 13.0f * scale, FontStyleRegular, UnitPixel);
        s_fonts.row = std::make_unique<Font>(&family, 12.0f * scale, FontStyleRegular, UnitPixel);
        s_fonts.hint = std::make_unique<Font>(&family, 11.0f * scale, FontStyleRegular, UnitPixel);
    }
    return s_fonts;
}

static void ReleaseSurface() {
    if (s_memDC) {
        if (s_hOldBitmap) SelectObject(s_memDC, s_hOldBitmap);
        DeleteDC(s_memDC);
    }
    if (s_hBitmap) DeleteObject(s_hBitmap);
    s_memDC = NULL;
    s_hBitmap = NULL;
    s_hOldBitmap = NULL;
    s_pBits = nullptr;
    s_bitmapSize = {};
}

static bool EnsureSurface(int width, int height) {
    if (s_hBitmap && s_bitmapSize.cx == width && s_bitmapSize.cy == height) return true;
    ReleaseSurface();

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    s_memDC = CreateCompatibleDC(NULL);
    s_hBitmap = CreateDIBSection(s_memDC, &bmi, DIB_RGB_COLORS, &s_pBits, NULL, 0);
    if (!s_hBitmap || !s_pBits) {
        ReleaseSurface();
        return false;
    }
    s_hOldBitmap = SelectObject(s_memDC, s_hBitmap);
    s_bitmapSize = { width, height };
    return true;
}

static void FillRoundedRect(Graphics& g, const Brush& brush, const Pen* pen, RectF rect, float radius) {
    GraphicsPath path;
    float diameter = radius * 2.0f;
    float r = rect.X + rect.Width;
    float b = rect.Y + rect.Height;

    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(r - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(r - diameter, b - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.X, b - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();

    g.FillPath(&brush, &path);
    if (pen) g.DrawPath(pen, &path);
}

// The hint row is a title row like a result, followed by its guide lines
static float HintBlockHeight(const HintRow& hint) {
    if (hint.title.empty()) return 0.0f;
    return kRowH + hint.guide.size() * kGuideH;
}

// The title is selectable like a result; the guide below it is plain muted text
static void DrawHint(Graphics& g, const HintRow& hint, bool selected, float y, float width, float scale,
                     const StringFormat& format) {
    const HudFonts& fonts = FontsForScale(scale);
    const float padX = kPadX * scale;
    const float rowH = kRowH * scale;
    SolidBrush textBrush(s_palette.text);
    SolidBrush mutedBrush(s_palette.mutedText);
    SolidBrush selTextBrush(s_palette.selectedText);
    SolidBrush selBrush(s_palette.selectedBackground);
    if (selected) {
        FillRoundedRect(g, selBrush, nullptr, RectF(4.0f * scale, y + 1.0f * scale, width - 8.0f * scale, rowH - 2.0f * scale), 3.0f * scale);
    }
    g.DrawString(hint.title.c_str(), -1, fonts.row.get(), RectF(padX, y, width - 2 * padX, rowH), &format,
                 selected ? &selTextBrush : &textBrush);
    for (size_t i = 0; i < hint.guide.size(); ++i) {
        g.DrawString(hint.guide[i].c_str(), -1, fonts.hint.get(),
                     RectF(padX, y + rowH + i * kGuideH * scale, width - 2 * padX, kGuideH * scale), &format, &mutedBrush);
    }
    Pen dividerPen(s_palette.divider, 1.0f);
    const float bottom = y + HintBlockHeight(hint) * scale;
    g.DrawLine(&dividerPen, 1.0f, bottom - 0.5f, width - 1.0f, bottom - 0.5f);
}

bool HudWindow::Initialize(HINSTANCE hInstance) {
    GdiplusStartupInput gdiplusStartupInput;
    if (GdiplusStartup(&s_gdiplusToken, &gdiplusStartupInput, NULL) != Ok) {
        return false;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = HUD_CLASS_NAME;
    RegisterClassExW(&wc);

    s_hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        HUD_CLASS_NAME, L"Baaz", WS_POPUP,
        0, 0, 100, 100,
        NULL, NULL, hInstance, NULL
    );
    return (s_hWnd != NULL);
}

void HudWindow::Shutdown() {
    if (s_hWnd) {
        DestroyWindow(s_hWnd);
        s_hWnd = NULL;
    }
    ReleaseSurface();
    s_fonts = HudFonts(); // GDI+ objects must go before GdiplusShutdown
    if (s_gdiplusToken) {
        GdiplusShutdown(s_gdiplusToken);
        s_gdiplusToken = 0;
    }
}

void HudWindow::Reposition() {
    RECT parentRc = {};
    if (s_hParentWnd && IsWindow(s_hParentWnd)) {
        GetClientRect(s_hParentWnd, &parentRc);
        MapWindowPoints(s_hParentWnd, NULL, reinterpret_cast<POINT*>(&parentRc), 2);
        s_scale = GetDpiForWindow(s_hParentWnd) / 96.0f;
    } else {
        parentRc = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        s_scale = 1.0f;
    }

    int width = static_cast<int>(kWidth * s_scale + 0.5f);
    int height = static_cast<int>((kSearchH + HintBlockHeight(s_hint) + s_results.size() * kRowH + kFooterH) * s_scale + 0.5f);
    int x = parentRc.left + (parentRc.right - parentRc.left - width) / 2;
    int y = parentRc.top + static_cast<int>((parentRc.bottom - parentRc.top) * 0.22);
    s_rect = { x, y, x + width, y + height };
}

void HudWindow::SetQuery(std::wstring query) {
    s_query = std::move(query);
    s_results = SearchEngine::Query(s_query);
    s_selectedIndex = 0;
    s_hintSelected = false;
    Reposition();
    Render();
}

void HudWindow::RebuildHint() {
    s_hint = MakeHintRow(s_commandState, s_notSearchable, s_workEnvironment.liveId != 0, s_runRefused);
}

void HudWindow::ApplyCommandIndex(const CommandIndex& index) {
    s_commandState = index.state;
    s_notSearchable = index.notSearchable;
    s_workEnvironment = index.workEnvironment;
    RebuildHint();
    if (s_visible) SetQuery(s_query);
}

void HudWindow::Show(HWND hParentWnd) {
    s_hParentWnd = hParentWnd;
    s_palette = LoadPalette();
    s_visible = true;
    s_runRefused = CommandExecutor::TakeRefusedRun();
    RebuildHint();
    SetQuery(L"");
    ShowWindow(s_hWnd, SW_SHOWNOACTIVATE);
    // Views, sheets and layer combinations change while working, so re-read
    // them on every open, from our own message rather than inside the hook.
    PostMessageW(s_hWnd, WM_BAAZ_REFRESH_INDEX, 0, 0);
}

void HudWindow::Hide() {
    s_visible = false;
    if (s_hWnd) ShowWindow(s_hWnd, SW_HIDE);
}

void HudWindow::ExecuteSelection() {
    // A hint that cannot open Work Environment has nothing to run; the HUD stays open
    if (s_hintSelected && !s_hint.canOpenWorkEnvironment) return;
    const SearchItem* chosen = nullptr;
    if (s_hintSelected) chosen = &s_workEnvironment;
    else if (s_selectedIndex < s_results.size()) chosen = &s_results[s_selectedIndex];
    if (chosen != nullptr) {
        // Run the command from our window's message, not from inside the keyboard
        // hook: Archicad refuses window and view changes made from within a hook.
        s_pendingItem = *chosen;
        PostMessageW(s_hWnd, WM_BAAZ_EXECUTE, 0, 0);
    }
    Hide();
}

void HudWindow::ClickAt(float y) {
    // What a hidden HUD last offered may no longer be what the user sees
    if (!s_visible) return;
    const float searchH = kSearchH * s_scale;
    const float hintH = HintBlockHeight(s_hint) * s_scale;
    if (y < searchH) return;
    if (y < searchH + hintH) {
        if (s_hint.canOpenWorkEnvironment) {
            s_hintSelected = true;
            ExecuteSelection();
        }
        return;
    }
    const size_t idx = static_cast<size_t>((y - searchH - hintH) / (kRowH * s_scale));
    if (idx < s_results.size()) {
        s_hintSelected = false;
        s_selectedIndex = idx;
        ExecuteSelection();
    }
}

// Up from the first result reaches the hint row above it
void HudWindow::MoveUp() {
    if (s_hintSelected) return;
    if (s_selectedIndex > 0) {
        --s_selectedIndex;
        Render();
    } else if (!s_hint.title.empty()) {
        s_hintSelected = true;
        Render();
    }
}

// Down and Tab on the hint row go back to the results, if there are any; true if the hint row was selected
bool HudWindow::LeaveHint() {
    if (!s_hintSelected) return false;
    if (!s_results.empty()) {
        s_hintSelected = false;
        Render();
    }
    return true;
}

void HudWindow::HandleKey(WPARAM vk, WCHAR ch) {
    switch (vk) {
    case VK_ESCAPE:
        Hide();
        break;
    case VK_RETURN:
        ExecuteSelection();
        break;
    case VK_SPACE:
        // Space on an empty search closes the HUD (tap to open, tap to close).
        // Otherwise it executes like Enter; Shift+Space, Space while nothing
        // matches, or Space on the hint row (which never runs on Space) types a
        // literal space for multi-word searches.
        if (s_query.empty()) {
            Hide();
        } else if ((GetKeyState(VK_SHIFT) & 0x8000) || s_results.empty() || s_hintSelected) {
            SetQuery(s_query + L' ');
        } else {
            ExecuteSelection();
        }
        break;
    case VK_UP:
        MoveUp();
        break;
    case VK_DOWN:
        if (LeaveHint()) break;
        if (s_selectedIndex + 1 < s_results.size()) {
            ++s_selectedIndex;
            Render();
        }
        break;
    case VK_TAB:
        if (LeaveHint()) break;
        if (!s_results.empty()) {
            s_selectedIndex = (s_selectedIndex + 1) % s_results.size();
            Render();
        }
        break;
    case VK_BACK:
        if (!s_query.empty()) SetQuery(s_query.substr(0, s_query.size() - 1));
        break;
    default:
        if (ch != 0) SetQuery(s_query + ch);
        break;
    }
}

void HudWindow::Render() {
    const int width = s_rect.right - s_rect.left;
    const int height = s_rect.bottom - s_rect.top;
    if (!s_hWnd || width <= 0 || height <= 0 || !EnsureSurface(width, height)) return;

    {
        // GDI+ draws straight into the DIB as premultiplied ARGB, the format
        // UpdateLayeredWindow wants
        Bitmap canvas(width, height, width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(s_pBits));
        Graphics g(&canvas);
        g.Clear(Color(0, 0, 0, 0));
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

        const HudPalette& pal = s_palette;
        const HudFonts& fonts = FontsForScale(s_scale);
        const float searchH = kSearchH * s_scale;
        const float rowH = kRowH * s_scale;
        const float padX = kPadX * s_scale;
        const float tagW = 90.0f * s_scale;

        // 1. Panel
        SolidBrush bgBrush(pal.background);
        Pen borderPen(pal.border, 1.0f);
        FillRoundedRect(g, bgBrush, &borderPen, RectF(0.5f, 0.5f, width - 1.0f, height - 1.0f), kRadius * s_scale);

        StringFormat formatLeft;
        formatLeft.SetAlignment(StringAlignmentNear);
        formatLeft.SetLineAlignment(StringAlignmentCenter);
        formatLeft.SetTrimming(StringTrimmingEllipsisCharacter);
        formatLeft.SetFormatFlags(StringFormatFlagsNoWrap);

        StringFormat formatRight;
        formatRight.SetAlignment(StringAlignmentFar);
        formatRight.SetLineAlignment(StringAlignmentCenter);
        formatRight.SetFormatFlags(StringFormatFlagsNoWrap);

        SolidBrush textBrush(pal.text);
        SolidBrush mutedBrush(pal.mutedText);
        SolidBrush selTextBrush(pal.selectedText);
        SolidBrush selTagBrush(WithAlpha(pal.selectedText, 200));
        SolidBrush selBrush(pal.selectedBackground);

        // 2. Search field
        const RectF searchRect(padX, 0.0f, width - 2 * padX, searchH);
        if (s_query.empty()) {
            g.DrawString(L"Search tools, views, sheets\x2026", -1, fonts.query.get(), searchRect, &formatLeft, &mutedBrush);
            g.DrawString(L"? Help   :settings", -1, fonts.hint.get(), searchRect, &formatRight, &mutedBrush);
        } else {
            g.DrawString(s_query.c_str(), -1, fonts.query.get(), searchRect, &formatLeft, &textBrush);
        }

        Pen dividerPen(pal.divider, 1.0f);
        g.DrawLine(&dividerPen, 1.0f, searchH - 0.5f, width - 1.0f, searchH - 0.5f);

        // 3. Hint row, pinned above the results
        const float hintH = HintBlockHeight(s_hint) * s_scale;
        if (hintH > 0.0f) DrawHint(g, s_hint, s_hintSelected, searchH, static_cast<float>(width), s_scale, formatLeft);

        // 4. Result rows: title on the left, chord or item kind right-aligned
        // in muted text, like shortcuts in Archicad's menus
        const float listY = searchH + hintH;
        for (size_t i = 0; i < s_results.size(); ++i) {
            const float y = listY + i * rowH;
            const bool isSelected = (i == s_selectedIndex) && !s_hintSelected;

            if (isSelected) {
                FillRoundedRect(g, selBrush, nullptr, RectF(4.0f * s_scale, y + 1.0f * s_scale, width - 8.0f * s_scale, rowH - 2.0f * s_scale), 3.0f * s_scale);
            }
            g.DrawString(s_results[i].title.c_str(), -1, fonts.row.get(),
                         RectF(padX, y, width - padX - tagW, rowH), &formatLeft, isSelected ? &selTextBrush : &textBrush);
            g.DrawString(s_results[i].tag.c_str(), -1, fonts.hint.get(),
                         RectF(width - tagW, y, tagW - padX, rowH), &formatRight, isSelected ? &selTagBrush : &mutedBrush);
        }

        // 5. Footer
        const float footerY = listY + s_results.size() * rowH;
        g.DrawLine(&dividerPen, 1.0f, footerY + 0.5f, width - 1.0f, footerY + 0.5f);
        g.DrawString(L"Space or Enter to run  \x2022  Shift+Space types a space  \x2022  Esc to close", -1, fonts.hint.get(),
                     RectF(padX, footerY, width - 2 * padX, kFooterH * s_scale), &formatLeft, &mutedBrush);
    }

    // Moves, resizes and repaints the window in one call
    POINT ptSrc = { 0, 0 };
    POINT ptDst = { s_rect.left, s_rect.top };
    SIZE size = { width, height };
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(s_hWnd, NULL, &ptDst, &size, s_memDC, &ptSrc, 0, &blend, ULW_ALPHA);
}

LRESULT CALLBACK HudWindow::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_BAAZ_EXECUTE: {
        SearchItem item = std::move(s_pendingItem);
        s_pendingItem = SearchItem();
        CommandExecutor::Execute(item);
        return 0;
    }

    case WM_BAAZ_REFRESH_INDEX: {
        std::vector<SearchItem> items = NavigatorIndexer::BuildProjectIndex();
        // An exception must not leave the window procedure; the project items are still offered,
        // and the commands count as unreadable
        CommandIndex commands;
        try {
            commands = CommandSource::BuildCommandIndex(MainWindow(), {});
            items.insert(items.end(), commands.commands.begin(), commands.commands.end());
        } catch (...) {
            commands = CommandIndex();
        }
        SearchEngine::SetProjectItems(std::move(items));
        ApplyCommandIndex(commands);
        return 0;
    }

    case WM_LBUTTONDOWN:
        // MessageHook keeps the matching button-up away from the canvas
        ClickAt(static_cast<float>(GET_Y_LPARAM(lParam)));
        return 0;

    default:
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}

} // namespace Baaz
