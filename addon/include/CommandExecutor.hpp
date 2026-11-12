#pragma once

#include "SearchEngine.hpp"

namespace Baaz {

class CommandExecutor {
public:
    static bool Execute(const SearchItem& item);
    // true once after a menu command was refused; the HUD says so the next time it opens
    static bool TakeRefusedRun();

private:
    static bool s_refusedRun;
};

} // namespace Baaz
