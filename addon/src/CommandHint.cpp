#include "CommandHint.hpp"

namespace Baaz {

static const wchar_t* const kRefusedLine = L"The last command did not run: its menu item changed or is disabled.";

static std::wstring TitleFor(CommandState state, size_t notSearchable) {
    switch (state) {
    case CommandState::NoExport:    return L"Commands are not searchable yet: no export found.";
    case CommandState::NewCommands: return L"Commands not searchable yet: " + std::to_wstring(notSearchable);
    case CommandState::Unreadable:  return L"Commands cannot be read for this Archicad setup.";
    case CommandState::Ready:       break;
    }
    return std::wstring();
}

static std::vector<std::wstring> ExportGuide(bool workEnvironmentFound) {
    return {
        workEnvironmentFound ? L"1. Press Enter on this line to open Work Environment."
                             : L"1. Open Options \x203A Work Environment.",
        L"2. Export the profile to \x2026\\.baaz\\commands",
        L"3. Open the HUD again."
    };
}

HintRow MakeHintRow(CommandState state, size_t notSearchable, bool workEnvironmentFound, bool lastRunRefused) {
    HintRow row;
    row.title = TitleFor(state, notSearchable);
    const bool needsExport = (state == CommandState::NoExport || state == CommandState::NewCommands);
    if (needsExport) {
        row.guide = ExportGuide(workEnvironmentFound);
        row.canOpenWorkEnvironment = workEnvironmentFound;
    }
    if (lastRunRefused && row.title.empty()) row.title = kRefusedLine;
    else if (lastRunRefused) row.guide.push_back(kRefusedLine);
    return row;
}

} // namespace Baaz
