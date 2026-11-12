// Keep assert() active in Release builds too
#undef NDEBUG
#include "BinSchemeReader.hpp"
#include "bin_scheme_builder.hpp"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace Baaz;

static const char* const kContextGuid = "FEDCBA98-7654-3210-FEDC-BA9876543210";

// The tree of the task description plus everything else a file holds
static FileParts Sample() {
    FileParts parts;
    parts.definitions = { Menu(L"Shared", L"Shared", { Item(7, 3, 4) }) };
    parts.bars = {
        Bar(kFloorGuid, { Menu(L"M1", L"&Ansicht",
            { Item(21761, 0x47732f64, 0x38c147a6), Separator(), Popup(Menu(L"M2", L"X", { Item(1557, 1, 2) })) }) }),
        Bar(kTextGuid, { ReferenceTo(L"M1"), ReferenceTo(L"Shared") }),
    };
    parts.toolbarContents = { Bytes({ 9, 8, 7, 6, 5 }) };
    parts.addons = { "0x00000001-0x0000002a" };
    return parts;
}

// ---- Dump of a tree, to compare whole trees as one string ----

static std::wstring Dump(const MenuNode& node) {
    std::wostringstream out;
    if (node.type == MenuNodeType::Separator) return L"S";
    if (node.type == MenuNodeType::Item) {
        out << L"I" << node.name << L"#" << node.commandNumber << L":" << std::hex << node.mdid1 << L":" << node.mdid2;
        return out.str();
    }
    out << L"P:" << node.name << L"[";
    for (size_t i = 0; i < node.children.size(); ++i) out << (i == 0 ? L"" : L",") << Dump(node.children[i]);
    out << L"]";
    return out.str();
}

static std::wstring DumpAll(const std::vector<MenuNode>& nodes) {
    std::wstring out;
    for (const MenuNode& node : nodes) out += (out.empty() ? L"" : L";") + Dump(node);
    return out;
}

static bool Read(const FileParts& parts, BinScheme& out) { return BinSchemeReader::Read(Serialize(parts), out); }

// ---- Tests ----

void TestReadsMenusSeparatorsAndPopups() {
    BinScheme scheme;
    assert(Read(Sample(), scheme));
    assert(scheme.guid == "01234567-89AB-CDEF-0123-456789ABCDEF");
    assert(scheme.floorPlanBar.size() == 1);
    assert(DumpAll(scheme.floorPlanBar) == L"P:Ansicht[I#21761:47732f64:38c147a6,S,P:X[I#1557:1:2]]");
    assert(scheme.floorPlanBar[0].type == MenuNodeType::Popup);
    const MenuNode& submenu = scheme.floorPlanBar[0].children.at(2);
    assert(submenu.type == MenuNodeType::Popup && submenu.children.at(0).type == MenuNodeType::Item);
    std::cout << "[PASS] TestReadsMenusSeparatorsAndPopups\n";
}

void TestKeepsMdidParts() {
    FileParts parts = Sample();
    parts.bars = { Bar(kFloorGuid, { Menu(L"M", L"M", { Item(2129920001u, 0x47732f64, 0x38c147a6), Item(0, 0, 0xFFFFFFFFu) }) }) };
    BinScheme scheme;
    assert(Read(parts, scheme));
    const MenuNode& item = scheme.floorPlanBar.at(0).children.at(0);
    assert(item.commandNumber == 2129920001u && item.mdid1 == 0x47732f64u && item.mdid2 == 0x38c147a6u);
    assert(item.name.empty());
    const MenuNode& last = scheme.floorPlanBar.at(0).children.at(1);
    assert(last.commandNumber == 0u && last.mdid1 == 0u && last.mdid2 == 0xFFFFFFFFu);
    std::cout << "[PASS] TestKeepsMdidParts\n";
}

void TestIgnoresTextMenuBar() {
    FileParts parts = Sample();
    // The text bar is not part of the result, even if it refers to nothing that exists
    parts.bars.push_back(Bar(kTextGuid, { Menu(L"T", L"TextOnly", { Item(1, 0, 0) }), ReferenceTo(L"Missing") }));
    BinScheme scheme;
    assert(Read(parts, scheme));
    assert(DumpAll(scheme.floorPlanBar) == L"P:Ansicht[I#21761:47732f64:38c147a6,S,P:X[I#1557:1:2]]");
    std::cout << "[PASS] TestIgnoresTextMenuBar\n";
}

void TestResolvesReferencesAndEmptyBodyIds() {
    FileParts parts = Sample();
    // "Shared" is a top-level definition, "Late" is defined inline in a later bar, the menu "Anon"
    // has no body id and the Apple menu's title is the single character U+0014
    parts.bars = {
        Bar(kFloorGuid, { Menu(L"A", L"Neu && &Wiederherstellen",
            { Popup(ReferenceTo(L"Shared")), Popup(ReferenceTo(L"Late")), Popup(MenuWithBodyId(L"Anon", L"", L"&B", {})) }),
            Menu(L"Apple", L"\x14", { Item(5, 6, 7) }) }),
        Bar(kTextGuid, { Menu(L"Late", L"Late", { Item(8, 9, 10) }) }),
    };
    BinScheme scheme;
    assert(Read(parts, scheme));
    assert(DumpAll(scheme.floorPlanBar) == L"P:Neu & Wiederherstellen[P:Shared[I#7:3:4],P:Late[I#8:9:a],P:B[]];P:Apple[I#5:6:7]");
    std::cout << "[PASS] TestResolvesReferencesAndEmptyBodyIds\n";
}

void TestUnknownHeaderIsUnreadable() {
    BinScheme scheme;
    std::string file = Serialize(Sample());
    assert(BinSchemeReader::Read(file, scheme));
    // Bytes of the magic (0, 1, 2, 7) and of the separator after the GUID (48, 51) must be as expected
    for (size_t index : { 0u, 1u, 2u, 7u, 48u, 51u }) {
        std::string changed = file;
        changed[index] = static_cast<char>(changed[index] ^ 0x01);
        assert(!BinSchemeReader::Read(changed, scheme));
    }
    FileParts parts = Sample();
    parts.componentType = "6FCD168F-A26C-4547-AF9D-ED9A5FC54DD8";
    assert(!Read(parts, scheme));
    parts = Sample();
    parts.guid = "01234567-89AB-CDEF-0123-456789ABCDE";  // 35 characters
    assert(!Read(parts, scheme));
    parts.guid = "0123456789AB-CDEF-0123-456789ABCDEF0";  // dashes in the wrong places
    assert(!Read(parts, scheme));
    parts.guid = "0123456G-89AB-CDEF-0123-456789ABCDEF";  // not a hexadecimal digit
    assert(!Read(parts, scheme));
    std::cout << "[PASS] TestUnknownHeaderIsUnreadable\n";
}

void TestUnknownRecordIsUnreadable() {
    BinScheme scheme;
    FileParts parts = Sample();
    parts.bars = { Bar(kFloorGuid, { Menu(L"M", L"M", { Item(1, 0, 0), UStr(L"Bogus") + Bytes({ 1, 0 }) }) }) };
    assert(!Read(parts, scheme));

    // Version bytes, the dynamic flag and the key of a menu reference are part of the record
    const std::string wrongVersions[] = {
        UStr(L"Item") + Bytes({ 2, 0, 0 }) + U32(1) + U32(0) + U32(0),
        UStr(L"Item") + Bytes({ 1, 0, 2 }) + U32(1) + U32(0) + U32(0),
        UStr(L"Separator") + Bytes({ 2, 0 }),
        UStr(L"PopupItem") + Bytes({ 1, 0 }) + Menu(L"P", L"P", {}),
    };
    for (const std::string& entry : wrongVersions) {
        parts.bars = { Bar(kFloorGuid, { Menu(L"M", L"M", { entry }) }) };
        assert(!Read(parts, scheme));
    }
    parts.bars = { Bar(kFloorGuid, { MenuRef(L"XX", L"M") }) };
    assert(!Read(parts, scheme));
    std::cout << "[PASS] TestUnknownRecordIsUnreadable\n";
}

void TestTruncatedIsUnreadable() {
    FileParts parts = Sample();
    std::string file = Serialize(parts);
    BinScheme scheme;
    assert(BinSchemeReader::Read(file, scheme));
    for (size_t length = 0; length < file.size(); ++length) {
        assert(!BinSchemeReader::Read(file.substr(0, length), scheme));
    }
    std::cout << "[PASS] TestTruncatedIsUnreadable (" << file.size() << " prefixes)\n";
}

void TestContextSettingsMustParse() {
    FileParts parts = Sample();
    parts.contexts = { Context(kContextGuid), Context(kContextGuid) };
    BinScheme scheme;
    assert(Read(parts, scheme));
    std::string file = Serialize(parts);
    size_t contextSize = Context(kContextGuid).size();
    size_t globalEnd = file.size() - 2 * contextSize;
    // A prefix ends on a record boundary or inside a record; only the boundaries are files
    for (size_t length = globalEnd + 1; length < file.size(); ++length) {
        bool boundary = length == globalEnd + contextSize;
        assert(BinSchemeReader::Read(file.substr(0, length), scheme) == boundary);
    }
    FileParts broken = parts;
    broken.contexts = { Context(kContextGuid), Bytes({ 2, 0 }) + AStr(kContextGuid) + U32(1) };
    assert(!Read(broken, scheme));
    broken.contexts = { Context("FEDCBA98-7654-3210-FEDC-BA987654321") };
    assert(!Read(broken, scheme));
    std::cout << "[PASS] TestContextSettingsMustParse\n";
}

void TestOversizeIsUnreadable() {
    BinScheme scheme;
    scheme.guid = "keep";
    assert(!BinSchemeReader::Read(std::string(kMaxInputBytes + 1, '\0'), scheme));
    assert(scheme.guid == "keep");
    std::cout << "[PASS] TestOversizeIsUnreadable\n";
}

static std::vector<std::pair<const char*, FileParts>> BrokenVariants() {
    std::vector<std::pair<const char*, FileParts>> variants;
    auto add = [&variants](const char* name, const FileParts& parts) { variants.emplace_back(name, parts); };
    FileParts parts = Sample();

    parts.definitions.push_back(Menu(L"M1", L"Again", {}));
    add("duplicate ID across definition and bar", parts);
    parts = Sample();
    parts.bars[0] = Bar(kFloorGuid, { Menu(L"M1", L"A", {}), Menu(L"M1", L"B", {}) });
    add("duplicate ID inside one bar", parts);
    parts = Sample();
    parts.bars[0] = Bar(kFloorGuid, { Menu(L"M1", L"A", { Popup(ReferenceTo(L"Missing")) }) });
    add("unresolved reference in the floor plan bar", parts);
    parts = Sample();
    parts.bars[0] = Bar(kFloorGuid, { ReferenceTo(L"Missing") });
    add("unresolved reference as a top-level menu", parts);
    parts = Sample();
    parts.bars.push_back(Bar(kFloorGuid, { Menu(L"Other", L"Other", {}) }));
    add("two floor plan bars", parts);
    parts = Sample();
    parts.bars.erase(parts.bars.begin());
    add("no floor plan bar", parts);
    parts = Sample();
    parts.unknownCount = 1;
    add("non-zero count after the definitions", parts);
    parts = Sample();
    parts.definitions = { ReferenceTo(L"M1") };
    add("reference among the top-level definitions", parts);
    parts = Sample();
    parts.addons = { "0x00000001-0x0000002" };
    add("add-on entry of the wrong length", parts);
    parts = Sample();
    parts.addons = { "0x00000001+0x0000002a" };
    add("add-on entry with the wrong separator", parts);
    parts = Sample();
    parts.globalSizeDelta = 1;
    add("global size too large", parts);
    parts = Sample();
    parts.globalSizeDelta = -1;
    add("global size too small", parts);
    parts = Sample();
    parts.trailing = "x";
    add("trailing byte", parts);
    parts = Sample();
    parts.insideGlobal = "x";
    add("byte after the add-ons inside the global part", parts);
    parts = Sample();
    parts.bars[0] = Bar(kFloorGuid, { MenuWithBodyId(L"M1", L"Other", L"A", {}) });
    add("body id differs from the reference id", parts);
    parts = Sample();
    parts.bars[0] = Bar(kFloorGuid, { MenuRef(L"ID", L"M1") + Block(UStr(L"M1") + UStr(L"A") + U32(0) + U32(0) + "x") });
    add("byte after the entries inside a menu block", parts);
    parts = Sample();
    parts.bars[0] = Bar(kFloorGuid, { Menu(L"M1", std::wstring(kMaxNameLength + 1, L'a'), {}) });
    add("title longer than the limit", parts);
    return variants;
}

void TestBrokenStructureIsUnreadable() {
    BinScheme scheme;
    for (const auto& variant : BrokenVariants()) {
        scheme.guid = "keep";
        scheme.floorPlanBar.resize(3);
        bool rejected = !Read(variant.second, scheme);
        if (!rejected) std::cerr << "not rejected: " << variant.first << "\n";
        assert(rejected);
        // out stays as it was
        assert(scheme.guid == "keep" && scheme.floorPlanBar.size() == 3);
    }
    FileParts atLimit = Sample();
    atLimit.bars[0] = Bar(kFloorGuid, { Menu(L"M1", std::wstring(kMaxNameLength, L'a'), {}) });
    assert(Read(atLimit, scheme));
    std::cout << "[PASS] TestBrokenStructureIsUnreadable\n";
}

// Menu "L<level>" holding the menu below it, `levels` menus deep
static std::string Chain(int levels, int level = 1) {
    std::wstring id = L"L" + std::to_wstring(level);
    std::vector<std::string> entries;
    if (level < levels) entries.push_back(Popup(Chain(levels, level + 1)));
    return Menu(id, id, entries);
}

void TestDepthLimitAndCycles() {
    BinScheme scheme;
    FileParts parts = Sample();
    // kMaxMenuDepth is 64 in BinSchemeReader.cpp: a chain of 64 menus is allowed, 65 is not
    parts.bars[0] = Bar(kFloorGuid, { Chain(64) });
    assert(Read(parts, scheme));
    parts.bars[0] = Bar(kFloorGuid, { Chain(65) });
    assert(!Read(parts, scheme));

    // A menu that contains itself ends at the depth limit
    parts = Sample();
    parts.bars[0] = Bar(kFloorGuid, { Menu(L"Self", L"Self", { Popup(ReferenceTo(L"Self")) }) });
    assert(!Read(parts, scheme));
    std::cout << "[PASS] TestDepthLimitAndCycles\n";
}

void TestNodeBudget() {
    BinScheme scheme;
    FileParts parts;
    // The budget counts the menu itself: kMaxMenuNodes - 1 entries fit, one more does not
    std::vector<std::string> entries(kMaxMenuNodes - 1, Separator());
    parts.bars = { Bar(kFloorGuid, { Menu(L"M", L"M", entries) }) };
    assert(Read(parts, scheme));
    assert(scheme.floorPlanBar.at(0).children.size() == kMaxMenuNodes - 1);
    entries.push_back(Separator());
    parts.bars = { Bar(kFloorGuid, { Menu(L"M", L"M", entries) }) };
    assert(!Read(parts, scheme));

    // Entries of menus the floor plan bar does not use count while parsing: kMaxMenuNodes in the file
    entries.pop_back();
    parts.bars = { Bar(kFloorGuid, { Menu(L"M", L"M", entries) }) };
    parts.definitions = { Menu(L"Unused", L"Unused", { Separator() }) };
    assert(Read(parts, scheme));
    parts.definitions = { Menu(L"Unused", L"Unused", { Separator(), Separator() }) };
    assert(!Read(parts, scheme));
    parts.definitions.clear();

    // Ten references per level over six levels expand a few KB into over a million nodes
    std::vector<std::string> fanOut(10, Popup(ReferenceTo(L"D1")));
    parts.bars = { Bar(kFloorGuid, { Menu(L"D0", L"D0", fanOut) }) };
    for (int level = 1; level <= 6; ++level) {
        std::wstring id = L"D" + std::to_wstring(level);
        std::vector<std::string> below;
        if (level < 6) below.assign(10, Popup(ReferenceTo(L"D" + std::to_wstring(level + 1))));
        parts.definitions.push_back(Menu(id, id, below));
    }
    assert(!Read(parts, scheme));
    std::cout << "[PASS] TestNodeBudget\n";
}

static bool EnvValue(const char* name, std::string& value) {
    char* raw = nullptr;
    size_t length = 0;
    if (_dupenv_s(&raw, &length, name) != 0 || raw == nullptr) return false;
    value = raw;
    free(raw);
    return !value.empty();
}

static std::string ReadWholeFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    assert(file.is_open());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// Item names are not in the .bin, so only items' numbers and MDIDs and popups' titles are compared
static bool SameStructure(const std::vector<MenuNode>& a, const std::vector<MenuNode>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        bool popup = a[i].type == MenuNodeType::Popup;
        bool same = a[i].type == b[i].type && a[i].commandNumber == b[i].commandNumber && a[i].mdid1 == b[i].mdid1 &&
                    a[i].mdid2 == b[i].mdid2 && (!popup || a[i].name == b[i].name);
        if (!same || !SameStructure(a[i].children, b[i].children)) return false;
    }
    return true;
}

static std::vector<std::string> SplitList(const std::string& list) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t end = list.find(';', start);
        parts.push_back(list.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) return parts;
        start = end + 1;
    }
}

// The paths come only from the environment: ';'-separated lists of equal length, pairs of the same state
void TestRealFilesMatchTheirExport() {
    std::string binList, xmlList;
    if (!EnvValue("BAAZ_TEST_SCHEME_BIN", binList) || !EnvValue("BAAZ_TEST_SCHEME_XML", xmlList)) {
        std::cout << "[SKIP] real scheme files not configured\n";
        return;
    }
    std::vector<std::string> binPaths = SplitList(binList);
    std::vector<std::string> xmlPaths = SplitList(xmlList);
    assert(binPaths.size() == xmlPaths.size());
    for (size_t i = 0; i < binPaths.size(); ++i) {
        assert(!binPaths[i].empty() && !xmlPaths[i].empty());
        BinScheme scheme;
        assert(BinSchemeReader::Read(ReadWholeFile(binPaths[i]), scheme));
        std::vector<MenuNode> exported = SchemeReader::ReadFloorPlanMenuBar(ReadWholeFile(xmlPaths[i]));
        assert(!exported.empty());
        assert(SameStructure(scheme.floorPlanBar, exported));
        std::cout << "[PASS] TestRealFilesMatchTheirExport pair " << (i + 1) << " (" << scheme.floorPlanBar.size()
                  << " top-level menus)\n";
    }
}

int main() {
    TestReadsMenusSeparatorsAndPopups();
    TestKeepsMdidParts();
    TestIgnoresTextMenuBar();
    TestResolvesReferencesAndEmptyBodyIds();
    TestUnknownHeaderIsUnreadable();
    TestUnknownRecordIsUnreadable();
    TestTruncatedIsUnreadable();
    TestContextSettingsMustParse();
    TestOversizeIsUnreadable();
    TestBrokenStructureIsUnreadable();
    TestDepthLimitAndCycles();
    TestNodeBudget();
    TestRealFilesMatchTheirExport();
    std::cout << "All bin scheme tests completed.\n";
    return 0;
}
