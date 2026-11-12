#pragma once

#include <string>
#include <vector>

namespace Baaz {

// Larger input files (exports, schemes, profiles) are rejected
constexpr size_t kMaxInputBytes = 8 * 1024 * 1024;
// Menus can reference each other, so a few KB of XML can expand into billions of nodes.
// A real scheme builds about 1300.
constexpr size_t kMaxMenuNodes = 100000;
// Linking copies menu paths, so long titles would multiply memory; real names are far shorter.
constexpr size_t kMaxNameLength = 512;

enum class MenuNodeType { Item, Separator, Popup };

struct MenuNode {
    MenuNodeType type = MenuNodeType::Item;
    std::wstring name;               // comment or title; access keys removed
    unsigned commandNumber = 0;      // first part of command="<n>-<mdid1>-<mdid2>"
    unsigned mdid1 = 0;              // second part, Item only
    unsigned mdid2 = 0;              // third part, Item only
    std::vector<MenuNode> children;  // Popup only
};

// Menu text without access keys: "&&" is a literal ampersand, a single "&" marks the access key.
std::wstring CleanName(const std::wstring& raw);

// Reads an exported Archicad command-layout scheme (Work Environment) into a menu tree.
class SchemeReader {
public:
    // Top-level menus of the "Nicht-Text" menu bar; empty if missing or malformed.
    static std::vector<MenuNode> ReadFloorPlanMenuBar(const std::string& utf8Xml);
};

} // namespace Baaz
