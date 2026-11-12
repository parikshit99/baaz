// Keep assert() active in Release builds too
#undef NDEBUG
#include "CommandHint.hpp"
#include <cassert>
#include <iostream>
#include <string>

using namespace Baaz;

static bool Contains(const std::wstring& text, const wchar_t* part) {
    return text.find(part) != std::wstring::npos;
}

static bool AnyLineContains(const HintRow& row, const wchar_t* part) {
    if (Contains(row.title, part)) return true;
    for (const std::wstring& line : row.guide) {
        if (Contains(line, part)) return true;
    }
    return false;
}

static const wchar_t* const kRefused = L"The last command did not run: its menu item changed or is disabled.";

void TestNoHintWhenReady() {
    const HintRow row = MakeHintRow(CommandState::Ready, 0, true, false);
    assert(row.title.empty() && row.guide.empty() && !row.canOpenWorkEnvironment);
    std::cout << "[PASS] TestNoHintWhenReady\n";
}

void TestNoExportHintHasGuide() {
    const HintRow row = MakeHintRow(CommandState::NoExport, 0, true, false);
    assert(!row.title.empty() && row.canOpenWorkEnvironment);
    assert(row.guide.size() == 3);
    assert(Contains(row.guide[0], L"Enter") && Contains(row.guide[0], L"Work Environment"));
    assert(Contains(row.guide[1], L"export") || Contains(row.guide[1], L"Export"));
    assert(Contains(row.guide[1], L"\x2026\\.baaz\\commands"));
    assert(Contains(row.guide[2], L"HUD"));
    std::cout << "[PASS] TestNoExportHintHasGuide\n";
}

void TestNewCommandsHintCounts() {
    const HintRow row = MakeHintRow(CommandState::NewCommands, 3, true, false);
    assert(row.title == L"Commands not searchable yet: 3");
    assert(row.guide == MakeHintRow(CommandState::NoExport, 0, true, false).guide);
    assert(row.canOpenWorkEnvironment);
    assert(Contains(MakeHintRow(CommandState::NewCommands, 17, true, false).title, L"17"));
    std::cout << "[PASS] TestNewCommandsHintCounts\n";
}

void TestUnreadableHintHasNoGuide() {
    const HintRow row = MakeHintRow(CommandState::Unreadable, 5, true, false);
    assert(!row.title.empty() && row.guide.empty() && !row.canOpenWorkEnvironment);
    assert(!Contains(row.title, L"version"));   // the profile, the scheme file or the menu may be the cause
    std::cout << "[PASS] TestUnreadableHintHasNoGuide\n";
}

void TestGuideNamesMenuPathWithoutCommand() {
    for (CommandState state : { CommandState::NoExport, CommandState::NewCommands }) {
        const HintRow row = MakeHintRow(state, 2, false, false);
        assert(!row.title.empty() && !row.canOpenWorkEnvironment);
        assert(row.guide.size() == 3);
        assert(Contains(row.guide[0], L"Options \x203A Work Environment"));
        assert(!AnyLineContains(row, L"Enter"));
    }
    std::cout << "[PASS] TestGuideNamesMenuPathWithoutCommand\n";
}

void TestRefusedRunIsShownOnce() {
    // Ready has no hint of its own: the refusal is the row
    const HintRow refused = MakeHintRow(CommandState::Ready, 0, true, true);
    assert(refused.title == kRefused && refused.guide.empty() && !refused.canOpenWorkEnvironment);
    assert(MakeHintRow(CommandState::Ready, 0, true, false).title.empty());
    // Next to a missing-commands hint it is one more line, and the hint keeps its action
    const HintRow both = MakeHintRow(CommandState::NoExport, 0, true, true);
    assert(both.canOpenWorkEnvironment && both.guide.size() == 4 && both.guide.back() == kRefused);
    assert(!AnyLineContains(MakeHintRow(CommandState::NoExport, 0, true, false), kRefused));
    assert(AnyLineContains(MakeHintRow(CommandState::Unreadable, 0, true, true), kRefused));
    std::cout << "[PASS] TestRefusedRunIsShownOnce\n";
}

int main() {
    TestNoHintWhenReady();
    TestNoExportHintHasGuide();
    TestNewCommandsHintCounts();
    TestUnreadableHintHasNoGuide();
    TestGuideNamesMenuPathWithoutCommand();
    TestRefusedRunIsShownOnce();
    std::cout << "ALL COMMAND HINT TESTS PASSED\n";
    return 0;
}
