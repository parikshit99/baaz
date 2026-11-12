#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

// ---- Builder for synthetic scheme bytes; follows the grammar in BinSchemeReader.hpp ----

inline const char* const kFloorGuid = "2085E4D3-5694-4BB9-ACAC-3AED5BE50091";
inline const char* const kTextGuid = "2CAC215F-221C-420F-AAEB-8462A659B8D9";
inline const char* const kComponentType = "6FCD168F-A26C-4547-AF9D-ED9A5FC54DD7";

inline std::string Bytes(std::initializer_list<unsigned> values) {
    std::string out;
    for (unsigned value : values) out += static_cast<char>(value & 0xFF);
    return out;
}

inline std::string U16(unsigned value) { return Bytes({ value, value >> 8 }); }

inline std::string U32(size_t value) {
    unsigned number = static_cast<unsigned>(value);
    return Bytes({ number, number >> 8, number >> 16, number >> 24 });
}

inline std::string UStr(const std::wstring& text) {
    std::string out = Bytes({ 1, 0 }) + U32(text.size());
    for (wchar_t unit : text) out += U16(static_cast<unsigned>(unit));
    return out;
}

inline std::string AStr(const std::string& text) { return U32(text.size()) + text; }

inline std::string Block(const std::string& content) { return Bytes({ 2, 1 }) + U32(content.size()) + content; }

// "01234567-89AB-CDEF-0123-456789ABCDEF" in the layout of a Windows GUID (u32, u16, u16, 8 bytes)
inline std::string GuidBytes(const std::string& text) {
    std::string digits;
    for (char c : text) {
        if (c != '-') digits += c;
    }
    std::string big;
    for (size_t i = 0; i < 32; i += 2) big += static_cast<char>(std::stoi(digits.substr(i, 2), nullptr, 16));
    return std::string{ big[3], big[2], big[1], big[0], big[5], big[4], big[7], big[6] } + big.substr(8);
}

inline std::string Item(unsigned number, unsigned mdid1, unsigned mdid2, unsigned dynamic = 0) {
    return UStr(L"Item") + Bytes({ 1, 0, dynamic }) + U32(number) + U32(mdid1) + U32(mdid2);
}

inline std::string Separator() { return UStr(L"Separator") + Bytes({ 1, 0 }); }

inline std::string Popup(const std::string& menuRef) { return UStr(L"PopupItem") + Bytes({ 2, 0 }) + menuRef; }

inline std::string MenuRef(const std::wstring& key, const std::wstring& id) { return UStr(key) + UStr(id); }

inline std::string ReferenceTo(const std::wstring& id) { return MenuRef(L"IR", id); }

inline std::string MenuWithBodyId(const std::wstring& id, const std::wstring& bodyId, const std::wstring& title,
                                  const std::vector<std::string>& entries) {
    std::string body = UStr(bodyId) + UStr(title) + U32(0xFFFFFFFFu) + U32(entries.size());
    for (const std::string& entry : entries) body += entry;
    return MenuRef(L"ID", id) + Block(body);
}

inline std::string Menu(const std::wstring& id, const std::wstring& title, const std::vector<std::string>& entries) {
    return MenuWithBodyId(id, id, title, entries);
}

inline std::string Bar(const std::string& guid, const std::vector<std::string>& menus) {
    std::string content = UStr(L"Bar") + GuidBytes(guid) + UStr(L"BarId") + U32(menus.size());
    for (const std::string& menu : menus) content += menu;
    return Block(content);
}

inline std::string Context(const std::string& guid) { return Bytes({ 2, 0 }) + AStr(guid) + U32(0); }

struct FileParts {
    std::string guid = "01234567-89AB-CDEF-0123-456789ABCDEF";
    std::string componentType = kComponentType;
    std::vector<std::string> definitions;
    size_t unknownCount = 0;
    std::vector<std::string> bars;
    std::vector<std::string> toolbarContents;
    std::vector<std::string> addons;
    std::vector<std::string> contexts;
    std::string trailing;
    std::string insideGlobal;  // after the add-ons, counted in the global size
    int globalSizeDelta = 0;
};

inline std::string Count(const std::vector<std::string>& records) {
    std::string out = U32(records.size());
    for (const std::string& record : records) out += record;
    return out;
}

inline std::string Serialize(const FileParts& parts) {
    std::vector<std::string> toolbars;
    for (const std::string& content : parts.toolbarContents) toolbars.push_back(Block(content));
    std::vector<std::string> addons;
    for (const std::string& addon : parts.addons) addons.push_back(AStr(addon));
    std::string global = Bytes({ 1, 0, 1, 0, 2, 0 }) + Count(parts.definitions) + U32(parts.unknownCount) +
                         Count(parts.bars) + Count(toolbars) + Count(addons) + parts.insideGlobal;
    std::string file = Bytes({ 0x57, 0x57, 0x78, 0x56, 0x34, 0x12, 0x05, 0x00 }) + AStr(parts.guid) +
                       Bytes({ 3, 0, 0x57, 0x57 }) + AStr("Scheme") + AStr(parts.componentType) + Bytes({ 1, 0 }) +
                       U32(static_cast<size_t>(static_cast<int>(global.size()) + parts.globalSizeDelta)) + global;
    for (const std::string& context : parts.contexts) file += context;
    return file + parts.trailing;
}
