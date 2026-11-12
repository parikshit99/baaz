#pragma once

#include "CommandSource.hpp"
#include <string>
#include <vector>

namespace Baaz {

// The pinned line above the results: why Archicad's commands are missing, with a short guide, and, once,
// that the last command did not run
struct HintRow {
    std::wstring title;                // empty: no row
    std::vector<std::wstring> guide;   // numbered steps, one per line below the title
    bool canOpenWorkEnvironment = false;   // Enter or a click on the row opens Options > Work Environment
};

// lastRunRefused: the last command Baaz was asked to run did not run (shown once, by the caller)
HintRow MakeHintRow(CommandState state, size_t notSearchable, bool workEnvironmentFound, bool lastRunRefused);

} // namespace Baaz
