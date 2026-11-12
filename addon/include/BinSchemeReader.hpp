#pragma once

#include "SchemeReader.hpp"
#include <string>
#include <vector>

namespace Baaz {

// Reader for the binary command scheme (".bin") that Archicad keeps current in its settings
// folder. It holds the same menu structure as the XML export, but every part of a work
// environment is current there, while an export is only as new as the last time someone made one.
//
// The grammar below was derived from real files and checked against the XML export of the same
// state. All integers are little-endian. The reader accepts exactly this grammar, nothing more:
// an unknown header, tag, version, flag or length, a missing byte or a surplus byte makes
// Read return false.
//
//   File          := Magic SchemeGuid HeaderSep SchemeName ComponentType 01 00 u32:globalSize
//                    Global (exactly globalSize bytes)  ContextSetting* (to the end of the file)
//   Magic         := 57 57 78 56 34 12 05 00
//   SchemeGuid    := AStr, 36 characters
//   HeaderSep     := 03 00 57 57
//   SchemeName    := AStr, not used
//   ComponentType := AStr, "6FCD168F-A26C-4547-AF9D-ED9A5FC54DD7"
//
//   Global        := 01 00 01 00 02 00
//                    u32:nDefs   nDefs x MenuRef (each "ID": the top-level menu definitions)
//                    u32:0       (any other value is unreadable)
//                    u32:nBars   nBars x MenuBar
//                    u32:nTools  nTools x Block (toolbars; only the block size is checked,
//                                the contents are not read)
//                    u32:nAddons nAddons x AStr (built-in add-ons, "0x%08x-0x%08x")
//
//   MenuBar       := Block( UStr:name  16 bytes:menubarguid  UStr:barId  u32:n  n x MenuRef )
//                    menubarguid has the Windows GUID layout (u32, u16, u16, 8 bytes). The floor
//                    plan bar is 2085E4D3-5694-4BB9-ACAC-3AED5BE50091 (the localized name is no
//                    key); the text editors' bar, 2CAC215F-221C-420F-AAEB-8462A659B8D9, is ignored.
//
//   MenuRef       := UStr:key UStr:id, then for key "ID" Block(MenuBody), for key "IR" nothing.
//                    "ID" is the XML's <Menu ID=...>, "IR" its <Menu IDREF=...>. Any other key
//                    is unreadable.
//   MenuBody      := UStr:bodyId UStr:title i32:icon u32:n n x Entry
//                    ends exactly at the end of its Block. bodyId is the id of the MenuRef or
//                    empty (add-on menus are written without it); the icon is not used.
//   Entry         := UStr:tag, then
//                      "Item"      01 00 u8:dynamic(0|1) u32:number u32:mdid1 u32:mdid2
//                      "Separator" 01 00
//                      "PopupItem" 02 00 MenuRef
//
//   Block         := 02 01 u32:size, then exactly size bytes inside the enclosing bound
//   UStr          := 01 00 u32:n, then n UTF-16LE code units
//   AStr          := u32:n, then n bytes
//   ContextSetting:= 02 00 AStr(36-character GUID) u32:0
//
// An Item record is 29 bytes (tag included) and a Separator 26. Two Items "29 bytes apart" are
// Item after Item, "55 bytes apart" are Item then Separator; there is no 55-byte record type.
// Other spacings (32 and 58 bytes) occur only inside toolbar blocks, which the reader skips.
//
// An "IR" refers to an "ID" defined anywhere in the file: at the top level, or inline in any bar
// (the text bar refers to menus defined inline in the floor plan bar). References are resolved
// after the whole file is parsed. A duplicate "ID" is unreadable, a reference cycle ends at the
// depth limit, and a reference without definition is unreadable if the floor plan bar uses it.
//
// The file holds no names for items. Popup titles carry access keys ("&") like the XML titles.
// The Apple menu's title is the single character U+0014; the XML export writes "Apple", and the
// reader does too, so both readers give the same tree.

struct BinScheme {
    std::string guid;                    // scheme GUID of the header
    std::vector<MenuNode> floorPlanBar;  // top-level popups; titles set, item names empty
};

class BinSchemeReader {
public:
    // false = unreadable (unknown header or record, bad length, over budget, not exactly one
    // floor plan bar, unresolved reference in it); out is untouched then. Never a partial tree.
    static bool Read(const std::string& bytes, BinScheme& out);
};

} // namespace Baaz
