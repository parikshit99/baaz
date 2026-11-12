#include "BinSchemeReader.hpp"
#include <map>
#include <string_view>
#include <utility>

namespace Baaz {

// Real schemes nest menus about six deep; the limit stops reference cycles and hostile files.
static const int kMaxMenuDepth = 64;

// Floor plan bar 2085E4D3-5694-4BB9-ACAC-3AED5BE50091 in the layout of a Windows GUID
static const std::string_view kFloorPlanBarGuid("\xD3\xE4\x85\x20\x94\x56\xB9\x4B\xAC\xAC\x3A\xED\x5B\xE5\x00\x91", 16);
static const std::string_view kMagic("\x57\x57\x78\x56\x34\x12\x05\x00", 8);
static const std::string_view kHeaderSeparator("\x03\x00\x57\x57", 4);
static const std::string_view kVersion1("\x01\x00", 2);
static const std::string_view kVersion2("\x02\x00", 2);
static const std::string_view kGlobalPrefix("\x01\x00\x01\x00\x02\x00", 6);
static const std::string_view kBlockTag("\x02\x01", 2);
static const char* const kComponentType = "6FCD168F-A26C-4547-AF9D-ED9A5FC54DD7";
static const size_t kGuidLength = 36;
static const size_t kAddOnLength = 21;

namespace {

// Reads little-endian values from a bounded range; every read checks the bound first.
class ByteReader {
public:
    ByteReader() = default;
    explicit ByteReader(std::string_view data) : m_data(data) {}

    bool AtEnd() const { return m_pos == m_data.size(); }

    bool Take(size_t count, std::string_view& bytes) {
        if (count > m_data.size() - m_pos) return false;
        bytes = m_data.substr(m_pos, count);
        m_pos += count;
        return true;
    }

    bool Expect(std::string_view expected) {
        std::string_view bytes;
        return Take(expected.size(), bytes) && bytes == expected;
    }

    bool U8(unsigned& value) {
        std::string_view bytes;
        if (!Take(1, bytes)) return false;
        value = static_cast<unsigned char>(bytes[0]);
        return true;
    }

    bool U32(unsigned& value) {
        std::string_view bytes;
        if (!Take(4, bytes)) return false;
        value = 0;
        for (size_t i = 4; i > 0; --i) value = (value << 8) | static_cast<unsigned char>(bytes[i - 1]);
        return true;
    }

    // 01 00 u32:n, then n UTF-16LE code units
    bool UStr(std::wstring& text) {
        unsigned count = 0;
        std::string_view bytes;
        if (!Expect(kVersion1) || !U32(count) || count > (m_data.size() - m_pos) / 2 || !Take(size_t{ count } * 2, bytes)) {
            return false;
        }
        text.clear();
        for (size_t i = 0; i < bytes.size(); i += 2) {
            text += static_cast<wchar_t>(static_cast<unsigned char>(bytes[i]) | (static_cast<unsigned char>(bytes[i + 1]) << 8));
        }
        return true;
    }

    // u32:n, then n bytes
    bool AStr(std::string& text) {
        unsigned count = 0;
        std::string_view bytes;
        if (!U32(count) || !Take(count, bytes)) return false;
        text.assign(bytes);
        return true;
    }

    // u32:size, then exactly size bytes inside this range
    bool Sized(ByteReader& inner) {
        unsigned size = 0;
        std::string_view bytes;
        if (!U32(size) || !Take(size, bytes)) return false;
        inner = ByteReader(bytes);
        return true;
    }

    bool Block(ByteReader& inner) { return Expect(kBlockTag) && Sized(inner); }

private:
    std::string_view m_data;
    size_t m_pos = 0;
};

struct MenuEntry {
    MenuNodeType type = MenuNodeType::Item;
    unsigned number = 0;
    unsigned mdid1 = 0;
    unsigned mdid2 = 0;
    std::wstring popupId;
};

struct MenuDefinition {
    std::wstring title;
    std::vector<MenuEntry> entries;
};

struct MenuReference {
    std::wstring id;
    bool inlineDefinition = false;
};

struct MenuBar {
    std::string guid;
    std::vector<std::wstring> menuIds;
};

struct ParsedScheme {
    std::string guid;
    std::map<std::wstring, MenuDefinition> definitions;
    std::vector<MenuBar> bars;
    size_t entries = 0;
};

struct BuildContext {
    const std::map<std::wstring, MenuDefinition>& definitions;
    size_t nodes = 0;
};

} // namespace

static bool IsHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool IsHexRun(const std::string& text, size_t first, size_t last) {
    for (size_t i = first; i < last; ++i) {
        if (!IsHexDigit(text[i])) return false;
    }
    return true;
}

// 8-4-4-4-12 hexadecimal digits
static bool IsGuidText(const std::string& text) {
    return text.size() == kGuidLength && IsHexRun(text, 0, 8) && IsHexRun(text, 9, 13) && IsHexRun(text, 14, 18) &&
           IsHexRun(text, 19, 23) && IsHexRun(text, 24, 36) && text[8] == '-' && text[13] == '-' && text[18] == '-' &&
           text[23] == '-';
}

// "0x%08x-0x%08x"
static bool IsAddOnText(const std::string& text) {
    return text.size() == kAddOnLength && text.compare(0, 2, "0x") == 0 && IsHexRun(text, 2, 10) && text[10] == '-' &&
           text.compare(11, 2, "0x") == 0 && IsHexRun(text, 13, 21);
}

static bool ReadMenuRef(ByteReader& reader, ParsedScheme& scheme, int depth, MenuReference& reference);

static bool ReadItem(ByteReader& reader, MenuEntry& entry) {
    unsigned dynamic = 0;
    entry.type = MenuNodeType::Item;
    return reader.Expect(kVersion1) && reader.U8(dynamic) && dynamic <= 1 && reader.U32(entry.number) &&
           reader.U32(entry.mdid1) && reader.U32(entry.mdid2);
}

static bool ReadPopupItem(ByteReader& reader, ParsedScheme& scheme, int depth, MenuEntry& entry) {
    MenuReference reference;
    entry.type = MenuNodeType::Popup;
    if (!reader.Expect(kVersion2) || !ReadMenuRef(reader, scheme, depth, reference)) return false;
    entry.popupId = std::move(reference.id);
    return true;
}

static bool ReadEntry(ByteReader& reader, ParsedScheme& scheme, int depth, MenuEntry& entry) {
    std::wstring tag;
    if (++scheme.entries > kMaxMenuNodes || !reader.UStr(tag)) return false;
    if (tag == L"Item") return ReadItem(reader, entry);
    if (tag == L"Separator") {
        entry.type = MenuNodeType::Separator;
        return reader.Expect(kVersion1);
    }
    // An unknown entry would shift the positions the linking relies on
    return tag == L"PopupItem" && ReadPopupItem(reader, scheme, depth, entry);
}

// The id of the body is the id of the reference, or empty
static bool ReadMenuHeader(ByteReader& body, const std::wstring& id, MenuDefinition& menu, unsigned& count) {
    std::wstring bodyId;
    unsigned icon = 0;
    return body.UStr(bodyId) && (bodyId.empty() || bodyId == id) && body.UStr(menu.title) && body.U32(icon) &&
           body.U32(count);
}

static bool ReadMenuBody(ByteReader& body, const std::wstring& id, ParsedScheme& scheme, int depth) {
    MenuDefinition menu;
    unsigned count = 0;
    if (depth > kMaxMenuDepth || !ReadMenuHeader(body, id, menu, count)) return false;
    for (unsigned i = 0; i < count; ++i) {
        MenuEntry entry;
        if (!ReadEntry(body, scheme, depth + 1, entry)) return false;
        menu.entries.push_back(std::move(entry));
    }
    // A second definition under one id: which one is meant?
    return body.AtEnd() && scheme.definitions.emplace(id, std::move(menu)).second;
}

static bool ReadMenuRef(ByteReader& reader, ParsedScheme& scheme, int depth, MenuReference& reference) {
    std::wstring key;
    if (!reader.UStr(key) || !reader.UStr(reference.id)) return false;
    if (key == L"IR") return true;
    ByteReader body;
    reference.inlineDefinition = true;
    return key == L"ID" && reader.Block(body) && ReadMenuBody(body, reference.id, scheme, depth);
}

static bool ReadBar(ByteReader& reader, ParsedScheme& scheme) {
    ByteReader content;
    std::wstring name, barId;
    std::string_view guid;
    unsigned count = 0;
    if (!reader.Block(content) || !content.UStr(name) || !content.Take(16, guid) || !content.UStr(barId) ||
        !content.U32(count)) {
        return false;
    }
    MenuBar bar;
    bar.guid.assign(guid);
    for (unsigned i = 0; i < count; ++i) {
        MenuReference reference;
        if (!ReadMenuRef(content, scheme, 1, reference)) return false;
        bar.menuIds.push_back(std::move(reference.id));
    }
    scheme.bars.push_back(std::move(bar));
    return content.AtEnd();
}

static bool ReadDefinitions(ByteReader& global, ParsedScheme& scheme) {
    unsigned count = 0;
    if (!global.U32(count)) return false;
    for (unsigned i = 0; i < count; ++i) {
        MenuReference reference;
        if (!ReadMenuRef(global, scheme, 1, reference) || !reference.inlineDefinition) return false;
    }
    return true;
}

static bool ReadBars(ByteReader& global, ParsedScheme& scheme) {
    unsigned count = 0;
    if (!global.U32(count)) return false;
    for (unsigned i = 0; i < count; ++i) {
        if (!ReadBar(global, scheme)) return false;
    }
    return true;
}

// Only the size of a toolbar is checked; Baaz offers menu commands, not toolbar buttons
static bool SkipToolbars(ByteReader& global) {
    unsigned count = 0;
    if (!global.U32(count)) return false;
    for (unsigned i = 0; i < count; ++i) {
        ByteReader toolbar;
        if (!global.Block(toolbar)) return false;
    }
    return true;
}

static bool ReadAddOns(ByteReader& global) {
    unsigned count = 0;
    if (!global.U32(count)) return false;
    for (unsigned i = 0; i < count; ++i) {
        std::string addOn;
        if (!global.AStr(addOn) || !IsAddOnText(addOn)) return false;
    }
    return true;
}

static bool ReadGlobal(ByteReader& global, ParsedScheme& scheme) {
    unsigned reserved = 1;
    return global.Expect(kGlobalPrefix) && ReadDefinitions(global, scheme) && global.U32(reserved) && reserved == 0 &&
           ReadBars(global, scheme) && SkipToolbars(global) && ReadAddOns(global) && global.AtEnd();
}

static bool ReadHeader(ByteReader& reader, ParsedScheme& scheme) {
    std::string name, componentType;
    return reader.Expect(kMagic) && reader.AStr(scheme.guid) && IsGuidText(scheme.guid) &&
           reader.Expect(kHeaderSeparator) && reader.AStr(name) && reader.AStr(componentType) &&
           componentType == kComponentType && reader.Expect(kVersion1);
}

// 02 00 AStr(GUID) u32:0, up to the end of the file
static bool ReadContextSettings(ByteReader& reader) {
    while (!reader.AtEnd()) {
        std::string guid;
        unsigned value = 1;
        if (!reader.Expect(kVersion2) || !reader.AStr(guid) || !IsGuidText(guid) || !reader.U32(value) || value != 0) {
            return false;
        }
    }
    return true;
}

static bool ParseScheme(const std::string& bytes, ParsedScheme& scheme) {
    ByteReader reader(bytes);
    ByteReader global;
    return ReadHeader(reader, scheme) && reader.Sized(global) && ReadGlobal(global, scheme) && ReadContextSettings(reader);
}

static bool BuildTitle(const std::wstring& raw, std::wstring& name) {
    // The Apple menu is stored as one control character; the XML export spells it out
    name = raw == L"\x14" ? L"Apple" : CleanName(raw);
    return name.size() <= kMaxNameLength;
}

static bool BuildPopup(const std::wstring& id, BuildContext& context, int depth, MenuNode& node);

static bool BuildEntry(const MenuEntry& entry, BuildContext& context, int depth, MenuNode& node) {
    if (++context.nodes > kMaxMenuNodes) return false;
    node.type = entry.type;
    node.commandNumber = entry.number;
    node.mdid1 = entry.mdid1;
    node.mdid2 = entry.mdid2;
    return entry.type != MenuNodeType::Popup || BuildPopup(entry.popupId, context, depth, node);
}

// A reference without definition ends here, which makes the whole scheme unreadable
static bool BuildPopup(const std::wstring& id, BuildContext& context, int depth, MenuNode& node) {
    auto found = context.definitions.find(id);
    if (depth > kMaxMenuDepth || found == context.definitions.end()) return false;
    node.type = MenuNodeType::Popup;
    if (!BuildTitle(found->second.title, node.name)) return false;
    for (const MenuEntry& entry : found->second.entries) {
        MenuNode child;
        if (!BuildEntry(entry, context, depth + 1, child)) return false;
        node.children.push_back(std::move(child));
    }
    return true;
}

// Fails if there is no floor plan bar or a second one: there is no telling which one is in use
static const MenuBar* FindFloorPlanBar(const ParsedScheme& scheme) {
    const MenuBar* found = nullptr;
    for (const MenuBar& bar : scheme.bars) {
        if (bar.guid != kFloorPlanBarGuid) continue;
        if (found != nullptr) return nullptr;
        found = &bar;
    }
    return found;
}

static bool BuildMenuBar(const MenuBar& bar, BuildContext& context, std::vector<MenuNode>& menus) {
    for (const std::wstring& id : bar.menuIds) {
        MenuNode menu;
        if (++context.nodes > kMaxMenuNodes || !BuildPopup(id, context, 1, menu)) return false;
        menus.push_back(std::move(menu));
    }
    return true;
}

bool BinSchemeReader::Read(const std::string& bytes, BinScheme& out) {
    ParsedScheme scheme;
    if (bytes.size() > kMaxInputBytes || !ParseScheme(bytes, scheme)) return false;
    const MenuBar* bar = FindFloorPlanBar(scheme);
    BuildContext context{ scheme.definitions };
    std::vector<MenuNode> menus;
    if (bar == nullptr || !BuildMenuBar(*bar, context, menus)) return false;
    out.guid = std::move(scheme.guid);
    out.floorPlanBar = std::move(menus);
    return true;
}

} // namespace Baaz
