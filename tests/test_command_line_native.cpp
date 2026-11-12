// Keep assert() active in Release builds too
#undef NDEBUG
#include "MenuLink.hpp"
#include "SchemeReader.hpp"
#include <cassert>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace Baaz;

static std::string ReadFixture() {
    std::ifstream file(std::string(BAAZ_TEST_FIXTURES) + "/command_scheme_ac29_de.xml", std::ios::binary);
    assert(file.is_open());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

static std::vector<std::wstring> Names(const std::vector<MenuNode>& nodes) {
    std::vector<std::wstring> names;
    for (const MenuNode& node : nodes) names.push_back(node.name);
    return names;
}

static const MenuNode* FindChild(const MenuNode& parent, const std::wstring& name) {
    for (const MenuNode& child : parent.children) {
        if (child.name == name) return &child;
    }
    return nullptr;
}

static bool SameTree(const std::vector<MenuNode>& a, const std::vector<MenuNode>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].type != b[i].type || a[i].name != b[i].name || a[i].commandNumber != b[i].commandNumber ||
            !SameTree(a[i].children, b[i].children)) {
            return false;
        }
    }
    return true;
}

static const char* const kFloorPlanGuid = "2085E4D3-5694-4BB9-ACAC-3AED5BE50091";
static const char* const kTextGuid = "2CAC215F-221C-420F-AAEB-8462A659B8D9";

static std::string Scheme(const std::string& contents) {
    return "<Scheme><GlobalSettings><CommandLayout>" + contents + "</CommandLayout></GlobalSettings></Scheme>";
}

static std::string BarWithGuid(const char* guid, const std::string& menus) {
    return std::string("<MenuBar ID=\"B\" menubarguid=\"") + guid + "\" name=\"x\">" + menus + "</MenuBar>";
}

// A complete scheme with one menu bar, so edge cases only spell out the menus
static std::string FloorPlanBar(const std::string& menus) {
    return Scheme(BarWithGuid(kFloorPlanGuid, menus));
}

void TestReadsFloorPlanBarOnly() {
    auto menus = SchemeReader::ReadFloorPlanMenuBar(ReadFixture());
    assert((Names(menus) == std::vector<std::wstring>{ L"Ansicht", L"Dokumentation" }));
    for (const MenuNode& menu : menus) assert(menu.type == MenuNodeType::Popup);
    std::cout << "[PASS] TestReadsFloorPlanBarOnly\n";
}

void TestRulerPosition() {
    auto menus = SchemeReader::ReadFloorPlanMenuBar(ReadFixture());
    assert(menus.size() == 2);
    const MenuNode& ruler = menus[0].children.at(9);
    assert(ruler.type == MenuNodeType::Item);
    assert(ruler.name == L"Lineal");
    assert(ruler.commandNumber == 1557);
    assert(menus[0].children.at(8).type == MenuNodeType::Separator);
    std::cout << "[PASS] TestRulerPosition\n";
}

void TestIdrefResolved() {
    auto menus = SchemeReader::ReadFloorPlanMenuBar(ReadFixture());
    assert(menus.size() == 2);
    const MenuNode& displayOptions = menus[0].children.at(0);
    assert(displayOptions.type == MenuNodeType::Popup);
    assert(displayOptions.name == L"Bildschirmdarstellungs-Optionen");
    assert(!displayOptions.children.empty());
    // Referenced from a menu that is itself defined inline: the second hop also resolves
    const MenuNode& gridOptions = menus[0].children.at(6);
    const MenuNode* projection = FindChild(gridOptions, L"Projektion auf Bearbeitungsebene");
    assert(projection != nullptr && !projection->children.empty());
    std::cout << "[PASS] TestIdrefResolved\n";
}

void TestPlaceholderNumbers() {
    auto menus = SchemeReader::ReadFloorPlanMenuBar(ReadFixture());
    assert(menus.size() == 2);
    const MenuNode* overrides = FindChild(menus[1], L"Grafische \u00dcberschreibung");
    assert(overrides != nullptr);
    assert(overrides->children.at(2).commandNumber == 23040);
    const MenuNode* viewOptions = FindChild(menus[1], L"Modelldarstellung");
    assert(viewOptions != nullptr);
    assert(viewOptions->children.at(2).commandNumber == 9472);
    std::cout << "[PASS] TestPlaceholderNumbers\n";
}

void TestNamesUnescaped() {
    auto menus = SchemeReader::ReadFloorPlanMenuBar(ReadFixture());
    assert(menus.size() == 2);
    assert(menus[0].children.at(6).name == L"Raster- & Bearbeitungsebenen-Optionen");
    assert(menus[0].children.at(13).name == L"Elementfang");

    // Entities, access keys and non-ASCII text in titles and comments
    auto parsed = SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar(
        "<Menu ID=\"M\" title=\"&amp;Ab&#108;age &#xE4;\">"
        "<Item command=\"7-0x0-0x0\" comment=\"a&lt;b&gt; &quot;c&quot; &apos;d&apos; Neu &amp;&amp; &amp;W\"/>"
        "<Item command=\"8-0x0-0x0\" comment=\"Gr\xC3\xB6\xC3\x9F" "e\"/>"
        "</Menu>"));
    assert(parsed.size() == 1);
    assert(parsed[0].name == L"Ablage \u00e4");
    assert(parsed[0].children.at(0).name == L"a<b> \"c\" 'd' Neu & W");
    assert(parsed[0].children.at(1).name == L"Gr\u00f6\u00dfe");
    std::cout << "[PASS] TestNamesUnescaped\n";
}

void TestBomCommentsAndCrlf() {
    std::string raw = ReadFixture();
    // The fixture really has what the test is about
    assert(raw.compare(0, 3, "\xEF\xBB\xBF") == 0);
    assert(raw.find("\r\n") != std::string::npos);
    assert(raw.find("<!--") != std::string::npos);

    std::string plain = raw.substr(3);
    size_t commentStart = plain.find("<!--");
    size_t commentEnd = plain.find("-->", commentStart) + 3;
    plain.erase(commentStart, commentEnd - commentStart);
    for (size_t pos = plain.find("\r\n"); pos != std::string::npos; pos = plain.find("\r\n", pos)) {
        plain.erase(pos, 1);
    }
    assert(plain.find('\r') == std::string::npos && plain.find("<!--") == std::string::npos);

    auto withExtras = SchemeReader::ReadFloorPlanMenuBar(raw);
    auto without = SchemeReader::ReadFloorPlanMenuBar(plain);
    assert(!without.empty());
    assert(SameTree(withExtras, without));

    // A comment may hold markup and a '>' without confusing the reader
    auto tricky = SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar(
        "<!-- <Menu ID=\"Ghost\" title=\"Ghost\"> -->"
        "<Menu ID=\"M\" title=\"Real\"/>"));
    assert((Names(tricky) == std::vector<std::wstring>{ L"Real" }));
    std::cout << "[PASS] TestBomCommentsAndCrlf\n";
}

void TestMalformedIsEmpty() {
    assert(SchemeReader::ReadFloorPlanMenuBar("").empty());
    assert(SchemeReader::ReadFloorPlanMenuBar("<Scheme>").empty());
    assert(SchemeReader::ReadFloorPlanMenuBar("not xml at all").empty());
    // Unclosed tag, mismatched end tag, unterminated attribute and comment
    assert(SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar("<Menu ID=\"M\" title=\"A\">")).empty());
    assert(SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar("<Menu ID=\"M\" title=\"A\"></Item>")).empty());
    assert(SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar("<Menu ID=\"M\" title=\"A/>")).empty());
    assert(SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar("<!-- never closed")).empty());
    // Unknown entity, invalid UTF-8, IDREF to nothing
    assert(SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar("<Menu ID=\"M\" title=\"&bogus;\"/>")).empty());
    assert(SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar("<Menu ID=\"M\" title=\"\xC3\"/>")).empty());
    assert(SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar("<Menu IDREF=\"Nowhere\"/>")).empty());
    // Only the text bar, or no CommandLayout at all
    assert(SchemeReader::ReadFloorPlanMenuBar(
        "<Scheme><GlobalSettings><CommandLayout>"
        "<MenuBar ID=\"B\" menubarguid=\"2CAC215F-221C-420F-AAEB-8462A659B8D9\" name=\"Text\">"
        "<Menu ID=\"M\" title=\"Text-Only\"/></MenuBar></CommandLayout></GlobalSettings></Scheme>").empty());
    assert(SchemeReader::ReadFloorPlanMenuBar("<Scheme/>").empty());
    // A menu bar with the right GUID in a document that is otherwise fine still works
    assert(SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar("<Menu ID=\"M\" title=\"A\"/>")).size() == 1);
    std::cout << "[PASS] TestMalformedIsEmpty\n";
}

void TestReferenceCycleIsEmpty() {
    auto loop = SchemeReader::ReadFloorPlanMenuBar(
        "<Scheme><GlobalSettings><CommandLayout>"
        "<Menu ID=\"A\" title=\"A\"><PopupItem><Menu IDREF=\"B\"/></PopupItem></Menu>"
        "<Menu ID=\"B\" title=\"B\"><PopupItem><Menu IDREF=\"A\"/></PopupItem></Menu>"
        "<MenuBar ID=\"M\" menubarguid=\"2085E4D3-5694-4BB9-ACAC-3AED5BE50091\" name=\"x\">"
        "<Menu IDREF=\"A\"/></MenuBar></CommandLayout></GlobalSettings></Scheme>");
    assert(loop.empty());
    std::cout << "[PASS] TestReferenceCycleIsEmpty\n";
}

static const std::string kValidItem = "<Item command=\"1-0x0-0x0\" comment=\"x\"/>";
static const std::string kValidMenuBefore = "<Menu ID=\"V1\" title=\"V1\">" + kValidItem + "</Menu>";
static const std::string kValidMenuAfter = "<Menu ID=\"V2\" title=\"V2\">" + kValidItem + "</Menu>";

// A popup nested in a menu, with the construct between valid items
static std::string InsideNestedPopup(const std::string& construct) {
    return "<Menu ID=\"P\" title=\"P\">" + kValidItem + "<PopupItem><Menu ID=\"N\" title=\"N\">" + kValidItem +
           construct + kValidItem + "</Menu></PopupItem></Menu>";
}

static std::wstring Dump(const MenuNode& node) {
    if (node.type == MenuNodeType::Separator) return L"S";
    if (node.type == MenuNodeType::Item) return L"I:" + node.name + L"#" + std::to_wstring(node.commandNumber);
    std::wstring text = L"P:" + node.name + L"[";
    for (size_t i = 0; i < node.children.size(); ++i) {
        if (i > 0) text += L",";
        text += Dump(node.children[i]);
    }
    return text + L"]";
}

// One bad construct as a child of the menu bar, and as an entry of a menu
struct BadConstruct {
    const char* asBarChild;
    const char* asEntry;
};

void TestRejectionNeverYieldsPartialTree() {
    const BadConstruct constructs[] = {
        { "<PopupItem/>", "<PopupItem/>" },
        { "<PopupItem><Menu ID=\"a\" title=\"a\"/><Menu ID=\"b\" title=\"b\"/></PopupItem>",
          "<PopupItem><Menu ID=\"a\" title=\"a\"/><Menu ID=\"b\" title=\"b\"/></PopupItem>" },
        { "<PopupItem><Item command=\"1-0x0-0x0\" comment=\"x\"/></PopupItem>",
          "<PopupItem><Item command=\"1-0x0-0x0\" comment=\"x\"/></PopupItem>" },
        { "<Item command=\"x-0x0-0x0\" comment=\"a\"/>", "<Item command=\"x-0x0-0x0\" comment=\"a\"/>" },
        { "<Item command=\"99999999999-0x0-0x0\" comment=\"a\"/>", "<Item command=\"99999999999-0x0-0x0\" comment=\"a\"/>" },
        { "<Menu ID=\"Z\"/>", "<PopupItem><Menu ID=\"Z\"/></PopupItem>" },
        { "<Separator/>", "<Bogus/>" },
        { "<Menu IDREF=\"Nowhere\"/>", "<PopupItem><Menu IDREF=\"Nowhere\"/></PopupItem>" },
    };
    // The same documents without a bad construct read fine, so only the construct can empty them
    assert(SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar(kValidMenuBefore + kValidMenuAfter)).size() == 2);
    auto control = SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar(kValidMenuBefore + InsideNestedPopup("") + kValidMenuAfter));
    assert(control.size() == 3);
    assert(Dump(control[1]) == L"P:P[I:x#1,P:N[I:x#1,I:x#1]]");

    for (const BadConstruct& construct : constructs) {
        assert(SchemeReader::ReadFloorPlanMenuBar(
            FloorPlanBar(kValidMenuBefore + construct.asBarChild + kValidMenuAfter)).empty());
        assert(SchemeReader::ReadFloorPlanMenuBar(
            FloorPlanBar(kValidMenuBefore + InsideNestedPopup(construct.asEntry) + kValidMenuAfter)).empty());
    }
    std::cout << "[PASS] TestRejectionNeverYieldsPartialTree\n";
}

// Menus M0..M(count-1), each holding the next one by IDREF; the last holds an item
static std::string MenuChain(int count) {
    std::string definitions;
    for (int i = 0; i < count; ++i) {
        std::string id = "M" + std::to_string(i);
        std::string inner = i + 1 < count ? "<PopupItem><Menu IDREF=\"M" + std::to_string(i + 1) + "\"/></PopupItem>" : kValidItem;
        definitions += "<Menu ID=\"" + id + "\" title=\"" + id + "\">" + inner + "</Menu>";
    }
    return Scheme(definitions + BarWithGuid(kFloorPlanGuid, "<Menu IDREF=\"M0\"/>"));
}

void TestDepthLimitNotCycle() {
    // kMaxMenuDepth is 16 in SchemeReader.cpp: a chain of 16 menus is allowed, 17 is not
    assert(SchemeReader::ReadFloorPlanMenuBar(MenuChain(1)).size() == 1);
    assert(SchemeReader::ReadFloorPlanMenuBar(MenuChain(16)).size() == 1);
    assert(SchemeReader::ReadFloorPlanMenuBar(MenuChain(17)).empty());
    std::cout << "[PASS] TestDepthLimitNotCycle\n";
}

static std::string MenuWithSeparators(size_t count) {
    std::string menu = "<Menu ID=\"M\" title=\"M\">";
    for (size_t i = 0; i < count; ++i) menu += "<Separator/>";
    return FloorPlanBar(menu + "</Menu>");
}

void TestNodeBudgetStopsExpansion() {
    // 15 menus with three links each to the next: 3^14 nodes if expanded, a few KB of XML
    std::string definitions;
    for (int i = 0; i < 15; ++i) {
        std::string id = "M" + std::to_string(i);
        std::string link = "<PopupItem><Menu IDREF=\"M" + std::to_string(i + 1) + "\"/></PopupItem>";
        definitions += "<Menu ID=\"" + id + "\" title=\"" + id + "\">" + (i < 14 ? link + link + link : kValidItem) + "</Menu>";
    }
    assert(SchemeReader::ReadFloorPlanMenuBar(
        Scheme(definitions + BarWithGuid(kFloorPlanGuid, "<Menu IDREF=\"M0\"/>"))).empty());

    // kMaxMenuNodes is 100000 in SchemeReader.hpp and counts the menu itself
    assert(SchemeReader::ReadFloorPlanMenuBar(MenuWithSeparators(99999)).size() == 1);
    assert(SchemeReader::ReadFloorPlanMenuBar(MenuWithSeparators(100000)).empty());
    std::cout << "[PASS] TestNodeBudgetStopsExpansion\n";
}

void TestInputSizeLimit() {
    // kMaxInputBytes is 8 MiB in SchemeReader.hpp; trailing white space is ignored by the reader
    std::string document = FloorPlanBar("<Menu ID=\"M\" title=\"M\"/>");
    const size_t limit = 8u * 1024u * 1024u;
    assert(SchemeReader::ReadFloorPlanMenuBar(document + std::string(limit - document.size(), ' ')).size() == 1);
    assert(SchemeReader::ReadFloorPlanMenuBar(document + std::string(limit - document.size() + 1, ' ')).empty());
    std::cout << "[PASS] TestInputSizeLimit\n";
}

void TestSchemeReaderKeepsMdidParts() {
    auto menus = SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar(
        "<Menu ID=\"M\" title=\"M\">"
        "<Item command=\"2129920001-0x47732f64-0x38c147a6\" comment=\"a\"/>"
        "<Item command=\"5-0x0-0xFFFFFFFF\" comment=\"b\"/>"
        "<Item command=\"6-0x00000000001-0x2\" comment=\"c\"/>"
        "</Menu>"));
    assert(menus.size() == 1 && menus[0].children.size() == 3);
    const MenuNode& first = menus[0].children[0];
    assert(first.commandNumber == 2129920001u && first.mdid1 == 0x47732f64u && first.mdid2 == 0x38c147a6u);
    assert(menus[0].children[1].mdid1 == 0u && menus[0].children[1].mdid2 == 0xFFFFFFFFu);
    assert(menus[0].children[2].mdid1 == 1u && menus[0].children[2].mdid2 == 2u);
    // A part that does not fit 32 bits makes the whole scheme unreadable
    const char* const tooBig[] = { "1-0x100000000-0x0", "1-0x0-0x100000000", "1-0x0-0x1ffffffff" };
    for (const char* command : tooBig) {
        std::string item = std::string("<Menu ID=\"M\" title=\"M\"><Item command=\"") + command + "\" comment=\"a\"/></Menu>";
        assert(SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar(item)).empty());
    }
    std::cout << "[PASS] TestSchemeReaderKeepsMdidParts\n";
}

void TestExactTreeShape() {
    auto menus = SchemeReader::ReadFloorPlanMenuBar(Scheme(
        "<Menu ID=\"C\" title=\"C\"><Item command=\"3-0x0-0x0\" comment=\"z\"/></Menu>" +
        BarWithGuid(kFloorPlanGuid,
            "<Menu ID=\"A\" title=\"A\">"
            "<Item command=\"1-0x0-0x0\" comment=\"x\"/>"
            "<Separator/>"
            "<PopupItem><Menu ID=\"B\" title=\"B\"><Item command=\"2-0x0-0x0\" comment=\"y\"/></Menu></PopupItem>"
            "<PopupItem><Menu IDREF=\"C\"/></PopupItem>"
            "<Item command=\"4-0x0-0x0\" comment=\"w\"/>"
            "<PopupItem><Menu ID=\"D\" title=\"D\"><Separator/></Menu></PopupItem>"
            "</Menu>"
            "<Menu ID=\"E\" title=\"E\"/>")));
    assert(menus.size() == 2);
    assert(Dump(menus[0]) == L"P:A[I:x#1,S,P:B[I:y#2],P:C[I:z#3],I:w#4,P:D[S]]");
    assert(Dump(menus[1]) == L"P:E[]");

    auto fixture = SchemeReader::ReadFloorPlanMenuBar(ReadFixture());
    assert(fixture.size() == 2);
    assert(fixture[0].children.size() == 14);
    assert(fixture[1].children.size() == 4);
    assert(Dump(fixture[1]) ==
           L"P:Dokumentation["
           L"P:Modelldarstellung[I:Modelldarstellung erstellen...#1550,S,I:Keine Modelldarstellungs-Kombinationen#9472],"
           L"P:Grafische \u00dcberschreibung[I:Grafische \u00dcberschreibungen...#1564,S,"
           L"I:Keine grafischen \u00dcberschreibungen#23040,S,I:Grafische \u00dcberschreibungsregel...#1563],"
           L"S,I:Grundriss-Schnittebene...#6407]");
    std::cout << "[PASS] TestExactTreeShape\n";
}

void TestPicksBarByGuidNotOrder() {
    auto menus = SchemeReader::ReadFloorPlanMenuBar(Scheme(
        BarWithGuid(kTextGuid, "<Menu ID=\"T\" title=\"Text-Only\"/>") +
        BarWithGuid(kFloorPlanGuid, "<Menu ID=\"F\" title=\"Floor\"/>")));
    assert((Names(menus) == std::vector<std::wstring>{ L"Floor" }));
    std::cout << "[PASS] TestPicksBarByGuidNotOrder\n";
}

void TestDuplicateBarGuidIsEmpty() {
    assert(SchemeReader::ReadFloorPlanMenuBar(Scheme(
        BarWithGuid(kFloorPlanGuid, "<Menu ID=\"F1\" title=\"One\"/>") +
        BarWithGuid(kFloorPlanGuid, "<Menu ID=\"F2\" title=\"Two\"/>"))).empty());
    std::cout << "[PASS] TestDuplicateBarGuidIsEmpty\n";
}

static std::string OneMenu(const std::string& entries) {
    return FloorPlanBar("<Menu ID=\"M\" title=\"M\">" + entries + "</Menu>");
}

void TestAmbiguousInputIsEmpty() {
    assert(SchemeReader::ReadFloorPlanMenuBar(OneMenu(kValidItem + "<Separator/>")).size() == 1);

    // A repeated attribute: which value is meant?
    assert(SchemeReader::ReadFloorPlanMenuBar(OneMenu("<Item command=\"1-0x0-0x0\" comment=\"A\" comment=\"B\"/>")).empty());
    assert(SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar("<Menu ID=\"M\" title=\"a\" title=\"b\"/>")).empty());

    // Children of a leaf entry would be dropped silently
    assert(SchemeReader::ReadFloorPlanMenuBar(OneMenu(
        "<Item command=\"1-0x0-0x0\" comment=\"A\"><Item command=\"2-0x0-0x0\" comment=\"B\"/></Item>")).empty());
    assert(SchemeReader::ReadFloorPlanMenuBar(OneMenu("<Separator><Separator/></Separator>")).empty());

    // A reference that also has content would hide that content
    assert(SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar("<Menu ID=\"M\" title=\"M\"/><Menu IDREF=\"M\"/>")).size() == 2);
    assert(SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar("<Menu ID=\"M\" title=\"M\"/><Menu IDREF=\"M\">" + kValidItem + "</Menu>")).empty());

    // command is <decimal>-0x<hex>-0x<hex> and nothing else
    const char* badCommands[] = { "12-garbage", "12-0x0", "12", "12-0x0-0x0-0x0", "12-0x-0x0",
                                  "12-0xZZ-0x0", "12-0x0-0x0 ", "-0x0-0x0", "12-x0-0x0" };
    for (const char* command : badCommands) {
        assert(SchemeReader::ReadFloorPlanMenuBar(
            OneMenu(std::string("<Item command=\"") + command + "\" comment=\"A\"/>")).empty());
    }
    auto good = SchemeReader::ReadFloorPlanMenuBar(OneMenu("<Item command=\"12-0x47732F64-0xd1c74938\" comment=\"A\"/>"));
    assert(good.size() == 1 && good[0].children.at(0).commandNumber == 12);
    std::cout << "[PASS] TestAmbiguousInputIsEmpty\n";
}

void TestNameLengthLimit() {
    const std::string atLimit(512, 'a');
    const std::string tooLong(513, 'a');
    const std::string item = "<Item command=\"1-0x0-0x0\" comment=\"";
    assert(SchemeReader::ReadFloorPlanMenuBar(OneMenu(item + atLimit + "\"/>")).size() == 1);
    assert(SchemeReader::ReadFloorPlanMenuBar(OneMenu(item + tooLong + "\"/>")).empty());
    // Access key marks are not part of the name
    assert(SchemeReader::ReadFloorPlanMenuBar(OneMenu(item + "&amp;" + atLimit + "\"/>")).size() == 1);

    const std::string title = "<Menu ID=\"M\" title=\"";
    assert(SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar(title + atLimit + "\"/>")).size() == 1);
    assert(SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar(title + tooLong + "\"/>")).empty());
    // One long title deeper down empties the whole result
    const std::string inner = "<Menu ID=\"M\" title=\"M\"><PopupItem><Menu ID=\"S\" title=\"";
    assert(SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar(inner + atLimit + "\"/></PopupItem></Menu>")).size() == 1);
    assert(SchemeReader::ReadFloorPlanMenuBar(FloorPlanBar(inner + tooLong + "\"/></PopupItem></Menu>")).empty());
    std::cout << "[PASS] TestNameLengthLimit\n";
}

static const int P = -1;  // popup
static const int S = -2;  // separator
static const unsigned kAddOnPlaceholder = 1803;
static const unsigned kSecondAddOnPlaceholder = 1802;
static const unsigned kModifierDependent = 514;
static const unsigned kGraphicOverridePlaceholder = 23040;
static const unsigned kModelViewPlaceholder = 9472;
static const wchar_t* const kSeparator = L" \u203a ";

// Live menu spec: a positive number is an item with that live id
static std::vector<LiveEntry> Live(const std::vector<int>& spec) {
    std::vector<LiveEntry> entries;
    for (int item : spec) {
        LiveEntry entry;
        entry.type = item == P ? MenuNodeType::Popup : item == S ? MenuNodeType::Separator : MenuNodeType::Item;
        entry.liveId = item > 0 ? static_cast<unsigned>(item) : 0;
        entries.push_back(entry);
    }
    return entries;
}

class FakeMenu : public MenuSource {
public:
    explicit FakeMenu(std::vector<std::wstring> titles) : m_titles(std::move(titles)) {}

    void Set(const std::vector<int>& path, const std::vector<int>& spec) { m_menus[path] = Live(spec); }
    void Disable(const std::vector<int>& path, size_t index) { m_menus.at(path).at(index).enabled = false; }

    std::vector<std::wstring> TopLevelTitles() override { return m_titles; }
    std::vector<LiveEntry> Entries(const std::vector<int>& path) override {
        auto found = m_menus.find(path);
        return found == m_menus.end() ? std::vector<LiveEntry>() : found->second;
    }

private:
    std::vector<std::wstring> m_titles;
    std::map<std::vector<int>, std::vector<LiveEntry>> m_menus;
};

static MenuNode ItemNode(const wchar_t* name, unsigned number) {
    MenuNode node;
    node.name = name;
    node.commandNumber = number;
    return node;
}

static MenuNode SeparatorNode() {
    MenuNode node;
    node.type = MenuNodeType::Separator;
    return node;
}

static MenuNode PopupNode(const wchar_t* name, std::vector<MenuNode> children) {
    MenuNode node;
    node.type = MenuNodeType::Popup;
    node.name = name;
    node.children = std::move(children);
    return node;
}

static const LinkedCommand* FindCommand(const std::vector<LinkedCommand>& commands, const std::wstring& name) {
    for (const LinkedCommand& command : commands) {
        if (command.name == name) return &command;
    }
    return nullptr;
}

static bool HasLiveId(const std::vector<LinkedCommand>& commands, unsigned liveId) {
    for (const LinkedCommand& command : commands) {
        if (command.liveId == liveId) return true;
    }
    return false;
}

static std::vector<std::wstring> LinkedNames(const std::vector<LinkedCommand>& commands) {
    std::vector<std::wstring> names;
    for (const LinkedCommand& command : commands) names.push_back(command.name);
    return names;
}

static std::vector<unsigned> LinkedIds(const std::vector<LinkedCommand>& commands) {
    std::vector<unsigned> ids;
    for (const LinkedCommand& command : commands) ids.push_back(command.liveId);
    return ids;
}

// The measured Ansicht (View) menu of Archicad 29, as ids and kinds in menu order (evidence E8)
static const std::vector<int> kMeasuredAnsicht = { P, S, 1233, 256, 391, P, P, 393, S, 255, 265, P, S, 415, 416, 371, 369,
                                                   P, 261, P, S, P, P, S, P, 1180, 1176, P, S, P };
static const int kAnsichtIndex = 1;

// Catalogue of that menu: the first segments as in the real export, then segments with the same
// shape and invented names. One add-on placeholder sits where the live menu dropped it.
static MenuNode AnsichtMenu() {
    return PopupNode(L"Ansicht", {
        PopupNode(L"Bildschirmdarstellungs-Optionen", { ItemNode(L"Reinzeichnung", 10241) }),
        SeparatorNode(),
        ItemNode(L"Transparentpause", 21761),
        ItemNode(L"Konstruktionsraster darstellen", 1540),
        ItemNode(L"Darstellung der Bearbeitungsebene", 1818),
        PopupNode(L"Transparentpausen-Optionen", { ItemNode(L"Transparentpause aktivieren", 21766) }),
        PopupNode(L"Raster-Optionen", { ItemNode(L"Raster-Einstellungen...", 1538) }),
        ItemNode(L"Anzeige Ursprung", 1830),
        SeparatorNode(),
        ItemNode(L"Lineal", 1557),
        ItemNode(L"Hilfslinien", 7937),
        PopupNode(L"Hilfslinien-Optionen", { ItemNode(L"Hilfslinien-Segment erstellen", 7942) }),
        SeparatorNode(),
        ItemNode(L"Entry 1", 9001), ItemNode(L"Entry 2", 9002), ItemNode(L"Entry 3", 9003), ItemNode(L"Entry 4", 9004),
        PopupNode(L"Submenu 1", { ItemNode(L"Sub 1", 9011) }),
        ItemNode(L"Entry 5", 9005),
        PopupNode(L"Submenu 2", { ItemNode(L"Sub 2", 9012) }),
        SeparatorNode(),
        PopupNode(L"Submenu 3", { ItemNode(L"Sub 3", 9013) }),
        PopupNode(L"Submenu 4", { ItemNode(L"Sub 4", 9014) }),
        SeparatorNode(),
        ItemNode(L"Add-On commands", kAddOnPlaceholder),
        PopupNode(L"Submenu 5", { ItemNode(L"Sub 5", 9015) }),
        ItemNode(L"Orbit", 1850),
        ItemNode(L"3D Rundgang", 1851),
        PopupNode(L"Submenu 6", { ItemNode(L"Sub 6", 9016) }),
        SeparatorNode(),
        PopupNode(L"Submenu 7", { ItemNode(L"Sub 7", 9017) }),
    });
}

static std::vector<MenuNode> AnsichtBar() {
    return { PopupNode(L"Ablage", { ItemNode(L"Neu", 1) }), AnsichtMenu() };
}

static FakeMenu AnsichtFake(const std::vector<int>& ansicht) {
    FakeMenu live({ L"Ablage", L"Ansicht" });
    live.Set({ 0 }, { 77 });
    live.Set({ kAnsichtIndex }, ansicht);
    return live;
}

static std::vector<MenuNode> OneMenu(std::vector<MenuNode> children) {
    return { PopupNode(L"M", std::move(children)) };
}

static FakeMenu OneLive(const std::vector<int>& spec) {
    FakeMenu live({ L"M" });
    live.Set({ 0 }, spec);
    return live;
}

static std::vector<LinkedCommand> LinkOne(std::vector<MenuNode> children, const std::vector<int>& spec,
                                          const std::vector<DynamicList>& lists = {}) {
    FakeMenu live = OneLive(spec);
    return MenuLink::Link(OneMenu(std::move(children)), live, lists);
}

static std::vector<std::wstring> Wide(std::initializer_list<const wchar_t*> names) {
    return std::vector<std::wstring>(names.begin(), names.end());
}

static std::vector<std::wstring> GraphicOverrideNames(size_t count) {
    std::vector<std::wstring> names = { L"No overrides", L"20 Vorentwurfsplanung" };
    for (size_t i = names.size(); i < count; ++i) names.push_back(L"Combination " + std::to_wstring(i));
    names.resize(count);
    return names;
}

// Documentation as in the fixture: model view options, graphic overrides, one command
static FakeMenu DocumentationFake() {
    FakeMenu live({ L"Ansicht", L"Dokumentation" });
    live.Set({ 1 }, { P, P, S, 6001 });
    return live;
}

static const std::vector<int> kGraphicOverrideLive = { 408, S, 1840, 1841, 1842, 1843, 1844, 1845, 1846, 1847, 1848, 1849,
                                                       S, 566, S, 407 };

void TestLinksRuler() {
    auto bar = AnsichtBar();
    FakeMenu live = AnsichtFake(kMeasuredAnsicht);
    auto commands = MenuLink::Link(bar, live, {});
    const LinkedCommand* ruler = FindCommand(commands, L"Lineal");
    assert(ruler != nullptr);
    assert(ruler->liveId == 255);
    assert(ruler->menuPath == L"Ansicht");
    assert((ruler->position == std::vector<int>{ kAnsichtIndex, 9 }));
    assert(ruler->enabled);
    const LinkedCommand* neu = FindCommand(commands, L"Neu");
    assert(neu != nullptr && neu->liveId == 77 && (neu->position == std::vector<int>{ 0, 0 }) && neu->menuPath == L"Ablage");
    std::cout << "[PASS] TestLinksRuler\n";
}

void TestLinksAreOrderedByPosition() {
    auto bar = AnsichtBar();
    FakeMenu live = AnsichtFake(kMeasuredAnsicht);
    live.Set({ kAnsichtIndex, 0 }, { 501 });
    auto commands = MenuLink::Link(bar, live, {});
    assert(commands.size() > 3);
    for (size_t i = 1; i < commands.size(); ++i) assert(commands[i - 1].position < commands[i].position);
    std::cout << "[PASS] TestLinksAreOrderedByPosition\n";
}

void TestAnchorsFromSegmentEnd() {
    auto bar = AnsichtBar();
    FakeMenu live = AnsichtFake(kMeasuredAnsicht);
    auto commands = MenuLink::Link(bar, live, {});
    const LinkedCommand* orbit = FindCommand(commands, L"Orbit");
    const LinkedCommand* walkthrough = FindCommand(commands, L"3D Rundgang");
    assert(orbit != nullptr && orbit->liveId == 1180);
    assert(walkthrough != nullptr && walkthrough->liveId == 1176);
    assert(FindCommand(commands, L"Add-On commands") == nullptr);
    std::cout << "[PASS] TestAnchorsFromSegmentEnd\n";
}

void TestCountMismatchSkipsSegment() {
    std::vector<int> withoutOne;
    for (int entry : kMeasuredAnsicht) {
        if (entry != 256) withoutOne.push_back(entry);
    }
    auto bar = AnsichtBar();
    FakeMenu live = AnsichtFake(withoutOne);
    // Were the two popups of the skipped segment entered, these would link wrongly
    live.Set({ kAnsichtIndex, 4 }, { 601 });
    live.Set({ kAnsichtIndex, 5 }, { 602 });
    auto commands = MenuLink::Link(bar, live, {});
    assert(FindCommand(commands, L"Transparentpause") == nullptr);
    assert(FindCommand(commands, L"Konstruktionsraster darstellen") == nullptr);
    assert(FindCommand(commands, L"Darstellung der Bearbeitungsebene") == nullptr);
    assert(FindCommand(commands, L"Anzeige Ursprung") == nullptr);
    assert(FindCommand(commands, L"Transparentpause aktivieren") == nullptr);
    assert(FindCommand(commands, L"Raster-Einstellungen...") == nullptr);
    assert(!HasLiveId(commands, 601) && !HasLiveId(commands, 602));
    const LinkedCommand* ruler = FindCommand(commands, L"Lineal");
    assert(ruler != nullptr && ruler->liveId == 255 && (ruler->position == std::vector<int>{ kAnsichtIndex, 8 }));
    std::cout << "[PASS] TestCountMismatchSkipsSegment\n";
}

void TestDisabledCarriedOver() {
    auto bar = AnsichtBar();
    FakeMenu live = AnsichtFake(kMeasuredAnsicht);
    live.Disable({ kAnsichtIndex }, 9);
    auto commands = MenuLink::Link(bar, live, {});
    const LinkedCommand* ruler = FindCommand(commands, L"Lineal");
    assert(ruler != nullptr && !ruler->enabled);
    const LinkedCommand* guideLines = FindCommand(commands, L"Hilfslinien");
    assert(guideLines != nullptr && guideLines->enabled);
    std::cout << "[PASS] TestDisabledCarriedOver\n";
}

void TestRecursesIntoLinkedPopups() {
    auto bar = AnsichtBar();
    FakeMenu live = AnsichtFake(kMeasuredAnsicht);
    live.Set({ kAnsichtIndex, 0 }, { 501 });
    auto commands = MenuLink::Link(bar, live, {});
    const LinkedCommand* inner = FindCommand(commands, L"Reinzeichnung");
    assert(inner != nullptr && inner->liveId == 501);
    assert(inner->menuPath == std::wstring(L"Ansicht") + kSeparator + L"Bildschirmdarstellungs-Optionen");
    assert((inner->position == std::vector<int>{ kAnsichtIndex, 0, 0 }));
    std::cout << "[PASS] TestRecursesIntoLinkedPopups\n";
}

void TestGraphicOverrideList() {
    auto bar = SchemeReader::ReadFloorPlanMenuBar(ReadFixture());
    FakeMenu live = DocumentationFake();
    live.Set({ 1, 1 }, kGraphicOverrideLive);
    auto commands = MenuLink::Link(bar, live, { { kGraphicOverridePlaceholder, GraphicOverrideNames(24) } });
    const std::wstring path = std::wstring(L"Dokumentation") + kSeparator + L"Grafische \u00dcberschreibung";

    const LinkedCommand* second = FindCommand(commands, L"20 Vorentwurfsplanung");
    assert(second != nullptr && second->liveId == 1841 && second->menuPath == path);
    assert((second->position == std::vector<int>{ 1, 1, 3 }));
    const LinkedCommand* rule = FindCommand(commands, L"Grafische \u00dcberschreibungsregel...");
    assert(rule != nullptr && rule->liveId == 407 && rule->menuPath == path);
    const LinkedCommand* dialog = FindCommand(commands, L"Grafische \u00dcberschreibungen...");
    assert(dialog != nullptr && dialog->liveId == 408);

    size_t listEntries = 0;
    for (const LinkedCommand& command : commands) {
        if (command.liveId >= 1840 && command.liveId <= 1849) ++listEntries;
    }
    assert(listEntries == 10);
    assert(!HasLiveId(commands, 566));
    assert(FindCommand(commands, L"Keine grafischen \u00dcberschreibungen") == nullptr);
    std::cout << "[PASS] TestGraphicOverrideList\n";
}

void TestModelViewOptionsList() {
    auto bar = SchemeReader::ReadFloorPlanMenuBar(ReadFixture());
    FakeMenu live = DocumentationFake();
    live.Set({ 1, 0 }, { 412, S, 3001, 3002 });
    auto commands = MenuLink::Link(bar, live, { { kModelViewPlaceholder, Wide({ L"View A", L"View B", L"View C" }) } });
    const LinkedCommand* second = FindCommand(commands, L"View B");
    assert(second != nullptr && second->liveId == 3002);
    assert(second->menuPath == std::wstring(L"Dokumentation") + kSeparator + L"Modelldarstellung");
    assert(FindCommand(commands, L"View A")->liveId == 3001);
    assert(FindCommand(commands, L"View C") == nullptr);
    assert(FindCommand(commands, L"Modelldarstellung erstellen...")->liveId == 412);
    std::cout << "[PASS] TestModelViewOptionsList\n";
}

void TestListTooShortNotOffered() {
    auto bar = SchemeReader::ReadFloorPlanMenuBar(ReadFixture());
    FakeMenu live = DocumentationFake();
    live.Set({ 1, 1 }, kGraphicOverrideLive);
    auto commands = MenuLink::Link(bar, live, { { kGraphicOverridePlaceholder, GraphicOverrideNames(3) } });
    for (unsigned id = 1840; id <= 1849; ++id) assert(!HasLiveId(commands, id));
    assert(FindCommand(commands, L"20 Vorentwurfsplanung") == nullptr);
    assert(FindCommand(commands, L"Grafische \u00dcberschreibungsregel...")->liveId == 407);
    std::cout << "[PASS] TestListTooShortNotOffered\n";
}

static std::vector<MenuNode> ListMenu() {
    return { ItemNode(L"A", 11), SeparatorNode(), ItemNode(L"Placeholder", kGraphicOverridePlaceholder),
             SeparatorNode(), ItemNode(L"B", 12) };
}

void TestListWithoutRegisteredPlaceholderStaysUnlinked() {
    const std::vector<int> spec = { 1, S, 10, 11, 12, S, 2 };
    const std::vector<DynamicList> notRegistered[] = {
        {},
        { { kModelViewPlaceholder, Wide({ L"X", L"Y", L"Z" }) } },
        // Two lists for one placeholder: which one is meant?
        { { kGraphicOverridePlaceholder, Wide({ L"X", L"Y", L"Z" }) },
          { kGraphicOverridePlaceholder, Wide({ L"X", L"Y", L"Z" }) } } };
    for (const auto& lists : notRegistered) {
        auto commands = LinkOne(ListMenu(), spec, lists);
        assert((LinkedNames(commands) == std::vector<std::wstring>{ L"A", L"B" }));
        assert((LinkedIds(commands) == std::vector<unsigned>{ 1, 2 }));
    }
    std::cout << "[PASS] TestListWithoutRegisteredPlaceholderStaysUnlinked\n";
}

void TestListNeedsOnlyItems() {
    const std::vector<DynamicList> lists = { { kGraphicOverridePlaceholder, Wide({ L"X", L"Y", L"Z" }) } };
    auto commands = LinkOne(ListMenu(), { 1, S, 10, P, 12, S, 2 }, lists);
    assert((LinkedNames(commands) == std::vector<std::wstring>{ L"A", L"B" }));
    // Two separators in a row leave an empty first segment, which is no list
    commands = LinkOne(ListMenu(), { 1, S, S, 10, 11, S, 2 }, lists);
    assert((LinkedNames(commands) == std::vector<std::wstring>{ L"A", L"B" }));
    std::cout << "[PASS] TestListNeedsOnlyItems\n";
}

// PINNED ACCEPTED RISK: this documents today's behaviour, it does not endorse it. Around a placeholder
// region whole parts are anchored by position, so a new live part the scheme does not hold (Y1, Y2 after
// the last separator) takes the place of the last scheme part (E, F), whose real entries sit in front of
// it. Not observed in Archicad; see MenuLink.hpp.
void TestNewLivePartTakesPlaceOfLastSchemePart() {
    const std::vector<MenuNode> menu = { ItemNode(L"S0", 11), SeparatorNode(), ItemNode(L"P", kGraphicOverridePlaceholder),
                                         SeparatorNode(), ItemNode(L"E", 12), ItemNode(L"F", 13) };
    // Live: S0 | the list | the real E and F | the new part Y1, Y2
    auto commands = LinkOne(menu, { 1, S, 10, 11, S, 2, 3, S, 20, 21 });
    assert((LinkedNames(commands) == std::vector<std::wstring>{ L"S0", L"E", L"F" }));
    assert((LinkedIds(commands) == std::vector<unsigned>{ 1, 20, 21 }));   // E and F run Y1 and Y2
    std::cout << "[PASS] TestNewLivePartTakesPlaceOfLastSchemePart\n";
}

// PINNED ACCEPTED RISK: this documents today's behaviour, it does not endorse it. If Archicad hides the
// static part B next to the region of placeholders while the list holds exactly as many entries as B,
// the live menu [A | e] looks like [A | B] with an empty list, and B links to the list entry e. Not
// observed in Archicad; the run-time re-check cannot catch it; see MenuLink.hpp.
void TestHiddenStaticPartNextToPlaceholderRegionLinksToTheList() {
    auto commands = LinkOne(ListMenu(), { 1, S, 10 });
    assert((LinkedNames(commands) == std::vector<std::wstring>{ L"A", L"B" }));
    assert((LinkedIds(commands) == std::vector<unsigned>{ 1, 10 }));   // B runs e
    std::cout << "[PASS] TestHiddenStaticPartNextToPlaceholderRegionLinksToTheList\n";
}

// N counts the linked Items without a name and nothing else: an unnamed Item in a part that does not link
// is not counted, since a new export cannot link it
void TestOnlyLinkedUnnamedItemsAreCounted() {
    std::vector<MenuNode> bar = OneMenu({ ItemNode(L"A", 11), ItemNode(L"", 12), SeparatorNode(), ItemNode(L"", 13), ItemNode(L"", 14) });
    FakeMenu live = OneLive({ 1, 2, S, 3 });   // the second part has one entry too few
    size_t notSearchable = 99;
    const auto commands = MenuLink::Link(bar, live, {}, &notSearchable);
    assert((LinkedIds(commands) == std::vector<unsigned>{ 1, 2 }));
    assert(notSearchable == 1);
    FakeMenu matching = OneLive({ 1, 2, S, 3, 4 });
    MenuLink::Link(bar, matching, {}, &notSearchable);
    assert(notSearchable == 3);
    std::cout << "[PASS] TestOnlyLinkedUnnamedItemsAreCounted\n";
}

void TestFitScoreRejectsOtherLanguage() {
    auto bar = SchemeReader::ReadFloorPlanMenuBar(ReadFixture());
    FakeMenu live({ L"View", L"Document" });
    live.Set({ 0 }, kMeasuredAnsicht);
    assert(MenuLink::FitScore(bar, live) == -1);
    assert(MenuLink::Link(bar, live, {}).empty());
    std::cout << "[PASS] TestFitScoreRejectsOtherLanguage\n";
}

void TestFitScoreCountsLinks() {
    auto bar = AnsichtBar();
    FakeMenu live = AnsichtFake(kMeasuredAnsicht);
    int score = MenuLink::FitScore(bar, live);
    assert(score > 0);
    assert(static_cast<size_t>(score) == MenuLink::Link(bar, live, {}).size());
    FakeMenu unreadable({ L"Ablage", L"Ansicht" });
    assert(MenuLink::FitScore(bar, unreadable) == 0);
    std::cout << "[PASS] TestFitScoreCountsLinks\n";
}

void TestTopLevelFitRules() {
    auto bar = AnsichtBar();
    FakeMenu none({});
    assert(MenuLink::FitScore(bar, none) == -1 && MenuLink::Link(bar, none, {}).empty());

    // Top-level menus are matched by title, so a missing or an extra live menu no longer rejects the
    // bar: the menus that are in both are linked, the others are ignored
    FakeMenu fewer({ L"Ablage" });
    fewer.Set({ 0 }, { 77 });
    assert(MenuLink::FitScore(bar, fewer) == 1);
    assert((LinkedNames(MenuLink::Link(bar, fewer, {})) == std::vector<std::wstring>{ L"Neu" }));

    FakeMenu more({ L"Ablage", L"Ansicht", L"Extras" });
    more.Set({ 0 }, { 77 });
    more.Set({ kAnsichtIndex }, kMeasuredAnsicht);
    more.Set({ 2 }, { 88 });
    auto withExtra = MenuLink::Link(bar, more, {});
    assert(MenuLink::FitScore(bar, more) == static_cast<int>(withExtra.size()));
    assert(FindCommand(withExtra, L"Neu") != nullptr && FindCommand(withExtra, L"Lineal") != nullptr);
    assert(!HasLiveId(withExtra, 88));

    // The matched menus must keep their order
    FakeMenu reordered({ L"Ansicht", L"Ablage" });
    reordered.Set({ 0 }, kMeasuredAnsicht);
    assert(MenuLink::FitScore(bar, reordered) == -1 && MenuLink::Link(bar, reordered, {}).empty());
    std::cout << "[PASS] TestTopLevelFitRules\n";
}

static MenuNode OneItemMenu(const wchar_t* title, const wchar_t* item, unsigned number) {
    return PopupNode(title, { ItemNode(item, number) });
}

// The pattern of a real Archicad bar, reduced: menus the export has and the live bar lacks (Apple,
// ArchiCARD), menus only the live bar has (Teamwork), empty slots, and shifted positions
void TestTopLevelMenusMatchedByTitle() {
    std::vector<MenuNode> bar = { OneItemMenu(L"Apple", L"Apple item", 101), OneItemMenu(L"A", L"A item", 102),
                                  OneItemMenu(L"B", L"B item", 103), OneItemMenu(L"ArchiCARD", L"Card item", 104),
                                  OneItemMenu(L"Tapir", L"Tapir item", 105) };
    FakeMenu live({ L"", L"A", L"B", L"Teamwork", L"Tapir", L"", L"" });
    live.Set({ 0 }, { 90 });
    live.Set({ 1 }, { 5 });
    live.Set({ 2 }, { 6 });
    live.Set({ 3 }, { 91 });
    live.Set({ 4 }, { 7 });
    auto commands = MenuLink::Link(bar, live, {});
    assert((LinkedNames(commands) == std::vector<std::wstring>{ L"A item", L"B item", L"Tapir item" }));
    assert((LinkedIds(commands) == std::vector<unsigned>{ 5, 6, 7 }));
    assert((commands[0].position == std::vector<int>{ 1, 0 }));
    assert((commands[1].position == std::vector<int>{ 2, 0 }));
    assert((commands[2].position == std::vector<int>{ 4, 0 }));
    assert(commands[0].menuPath == L"A" && commands[2].menuPath == L"Tapir");
    assert(MenuLink::FitScore(bar, live) == 3);
    std::cout << "[PASS] TestTopLevelMenusMatchedByTitle\n";
}

void TestDuplicateTopLevelTitlesAreNotMatched() {
    std::vector<MenuNode> bar = { OneItemMenu(L"A", L"A item", 102), OneItemMenu(L"B", L"B item", 103) };
    FakeMenu duplicateLive({ L"A", L"A", L"B" });
    duplicateLive.Set({ 0 }, { 5 });
    duplicateLive.Set({ 1 }, { 6 });
    duplicateLive.Set({ 2 }, { 7 });
    assert((LinkedNames(MenuLink::Link(bar, duplicateLive, {})) == std::vector<std::wstring>{ L"B item" }));

    std::vector<MenuNode> duplicateBar = { OneItemMenu(L"A", L"A item", 102), OneItemMenu(L"A", L"Other A item", 106),
                                           OneItemMenu(L"B", L"B item", 103) };
    FakeMenu live({ L"A", L"B" });
    live.Set({ 0 }, { 5 });
    live.Set({ 1 }, { 7 });
    assert((LinkedNames(MenuLink::Link(duplicateBar, live, {})) == std::vector<std::wstring>{ L"B item" }));

    // If only duplicated titles are left, nothing matches
    FakeMenu onlyDuplicates({ L"A", L"A" });
    assert(MenuLink::FitScore(std::vector<MenuNode>{ OneItemMenu(L"A", L"A item", 102) }, onlyDuplicates) == -1);
    std::cout << "[PASS] TestDuplicateTopLevelTitlesAreNotMatched\n";
}

void TestTopLevelOrderMustAgree() {
    std::vector<MenuNode> bar = { OneItemMenu(L"A", L"A item", 102), OneItemMenu(L"B", L"B item", 103) };
    FakeMenu swapped({ L"B", L"A" });
    swapped.Set({ 0 }, { 6 });
    swapped.Set({ 1 }, { 5 });
    assert(MenuLink::FitScore(bar, swapped) == -1 && MenuLink::Link(bar, swapped, {}).empty());
    std::cout << "[PASS] TestTopLevelOrderMustAgree\n";
}

void TestNoCommonTopLevelTitleDoesNotFit() {
    std::vector<MenuNode> bar = { OneItemMenu(L"A", L"A item", 102) };
    FakeMenu other({ L"X", L"Y" });
    other.Set({ 0 }, { 5 });
    other.Set({ 1 }, { 6 });
    assert(MenuLink::FitScore(bar, other) == -1 && MenuLink::Link(bar, other, {}).empty());
    std::cout << "[PASS] TestNoCommonTopLevelTitleDoesNotFit\n";
}

void TestEmptyTopLevelTitlesNeverMatch() {
    std::vector<MenuNode> bar = { OneItemMenu(L"", L"Nameless item", 101), OneItemMenu(L"A", L"A item", 102) };
    FakeMenu live({ L"", L"A" });
    live.Set({ 0 }, { 5 });
    live.Set({ 1 }, { 6 });
    assert((LinkedNames(MenuLink::Link(bar, live, {})) == std::vector<std::wstring>{ L"A item" }));

    FakeMenu onlyEmpty({ L"" });
    onlyEmpty.Set({ 0 }, { 5 });
    assert(MenuLink::FitScore(std::vector<MenuNode>{ OneItemMenu(L"", L"Nameless item", 101) }, onlyEmpty) == -1);
    std::cout << "[PASS] TestEmptyTopLevelTitlesNeverMatch\n";
}

void TestEmptyBarDoesNotFit() {
    FakeMenu none({});
    assert(MenuLink::FitScore({}, none) == -1);
    assert(MenuLink::Link({}, none, {}).empty());
    std::cout << "[PASS] TestEmptyBarDoesNotFit\n";
}

void TestTypeMismatchDropsSegment() {
    std::vector<MenuNode> bar = { ItemNode(L"A", 11), ItemNode(L"B", 12), SeparatorNode(), ItemNode(L"C", 13), ItemNode(L"D", 14) };
    auto commands = LinkOne(bar, { 1, P, S, 3, 4 });
    assert((LinkedNames(commands) == std::vector<std::wstring>{ L"C", L"D" }));

    // The reverse: a popup in the catalogue where the live menu has an item
    std::vector<MenuNode> withPopup = { PopupNode(L"Sub", { ItemNode(L"Inner", 20) }), ItemNode(L"B", 12) };
    FakeMenu live = OneLive({ 1, 2 });
    live.Set({ 0, 0 }, { 9 });
    assert(MenuLink::Link(OneMenu(withPopup), live, {}).empty());
    std::cout << "[PASS] TestTypeMismatchDropsSegment\n";
}

void TestTypeMismatchAtAnchorDropsSegment() {
    std::vector<MenuNode> bar = { ItemNode(L"A", 11), ItemNode(L"Placeholder", kAddOnPlaceholder), ItemNode(L"B", 12) };
    assert(LinkOne(bar, { 1, 5, 6, P }).empty());
    assert(LinkOne(bar, { P, 5, 6, 2 }).empty());
    std::cout << "[PASS] TestTypeMismatchAtAnchorDropsSegment\n";
}

void TestMismatchedMenuLinksNothing() {
    std::vector<MenuNode> bar = { ItemNode(L"A", 11), SeparatorNode(), ItemNode(L"B", 12) };
    assert(LinkOne(bar, { 1, 2 }).empty());           // separator missing
    assert(LinkOne(bar, { 1, S, 2, S, 3 }).empty());  // separator added
    assert(LinkOne(bar, {}).empty());                 // live menu unreadable
    assert(LinkOne(bar, { 1, S, 2 }).size() == 2);
    std::cout << "[PASS] TestMismatchedMenuLinksNothing\n";
}

void TestPlaceholderAtSegmentStart() {
    std::vector<MenuNode> bar = { ItemNode(L"Placeholder", kAddOnPlaceholder), ItemNode(L"A", 11), ItemNode(L"B", 12) };
    assert((LinkedIds(LinkOne(bar, { 2, 3 })) == std::vector<unsigned>{ 2, 3 }));             // placeholder dropped
    assert((LinkedIds(LinkOne(bar, { 7, 8, 9, 2, 3 })) == std::vector<unsigned>{ 2, 3 }));    // replaced by three entries
    assert((LinkedIds(LinkOne(bar, { 7, 2, 3 })) == std::vector<unsigned>{ 2, 3 }));          // replaced by one entry
    assert(LinkOne(bar, { 3 }).empty());                                                      // too short to anchor both
    std::cout << "[PASS] TestPlaceholderAtSegmentStart\n";
}

void TestPlaceholderAtSegmentEnd() {
    std::vector<MenuNode> bar = { ItemNode(L"A", 11), ItemNode(L"B", 12), ItemNode(L"Placeholder", kAddOnPlaceholder) };
    assert((LinkedIds(LinkOne(bar, { 1, 2 })) == std::vector<unsigned>{ 1, 2 }));
    assert((LinkedIds(LinkOne(bar, { 1, 2, 7, 8, 9 })) == std::vector<unsigned>{ 1, 2 }));
    assert(LinkOne(bar, { 1 }).empty());
    std::cout << "[PASS] TestPlaceholderAtSegmentEnd\n";
}

void TestOnePlaceholderEqualCount() {
    std::vector<MenuNode> bar = { ItemNode(L"A", 11), ItemNode(L"Placeholder", kAddOnPlaceholder), ItemNode(L"B", 12) };
    auto commands = LinkOne(bar, { 1, 2, 3 });
    assert((LinkedNames(commands) == std::vector<std::wstring>{ L"A", L"B" }));
    assert((LinkedIds(commands) == std::vector<unsigned>{ 1, 3 }));
    assert((LinkedIds(LinkOne(bar, { 1, 2, 3, 4 })) == std::vector<unsigned>{ 1, 4 }));
    assert(LinkOne(bar, { 1 }).empty());
    std::cout << "[PASS] TestOnePlaceholderEqualCount\n";
}

void TestTwoPlaceholdersInOneSegment() {
    std::vector<MenuNode> bar = { ItemNode(L"A", 11), ItemNode(L"Placeholder 1", kAddOnPlaceholder), ItemNode(L"B", 12),
                                  ItemNode(L"Placeholder 2", kSecondAddOnPlaceholder), ItemNode(L"C", 13) };
    // Equal count, but one placeholder may have grown while the other vanished: B is in doubt
    assert((LinkedIds(LinkOne(bar, { 1, 2, 3, 4, 5 })) == std::vector<unsigned>{ 1, 5 }));
    assert((LinkedIds(LinkOne(bar, { 1, 5 })) == std::vector<unsigned>{ 1, 5 }));
    assert((LinkedIds(LinkOne(bar, { 1, 2, 3, 4, 5, 6 })) == std::vector<unsigned>{ 1, 6 }));
    assert(LinkOne(bar, { 1 }).empty());
    std::cout << "[PASS] TestTwoPlaceholdersInOneSegment\n";
}

void TestPlaceholderIsNeverOffered() {
    std::vector<MenuNode> bar = { ItemNode(L"A", 11), ItemNode(L"Keine Stift-Sets", 1551), ItemNode(L"B", 12) };
    auto commands = LinkOne(bar, { 1, 2, 3 });
    assert(FindCommand(commands, L"Keine Stift-Sets") == nullptr && commands.size() == 2);
    std::cout << "[PASS] TestPlaceholderIsNeverOffered\n";
}

void TestPlaceholderSegmentVanishes() {
    // A list that is empty takes its segment and one separator with it
    assert((LinkedIds(LinkOne(ListMenu(), { 1, S, 2 })) == std::vector<unsigned>{ 1, 2 }));
    // With the other separator gone as well, the two ends cannot be told apart
    assert(LinkOne(ListMenu(), { 1, 2 }).empty());
    std::cout << "[PASS] TestPlaceholderSegmentVanishes\n";
}

void TestPlaceholderRegionSizeMismatch() {
    std::vector<MenuNode> bar = { ItemNode(L"A", 11), SeparatorNode(), ItemNode(L"Placeholder 1", kAddOnPlaceholder), SeparatorNode(),
                                  ItemNode(L"B", 12), SeparatorNode(), ItemNode(L"Placeholder 2", kSecondAddOnPlaceholder),
                                  SeparatorNode(), ItemNode(L"D", 14) };
    // Three catalogue segments between the outer ones, four live ones: B cannot be placed
    auto commands = LinkOne(bar, { 1, S, 5, S, 6, S, 7, S, 8, S, 4 });
    assert((LinkedNames(commands) == std::vector<std::wstring>{ L"A", L"D" }));
    // Same number of segments, but it cannot show which is which: 2 may be a list entry that
    // spilled over, with the second placeholder gone and B being 6
    commands = LinkOne(bar, { 1, S, 5, S, 2, S, 6, S, 4 });
    assert((LinkedNames(commands) == std::vector<std::wstring>{ L"A", L"D" }));
    assert((LinkedIds(commands) == std::vector<unsigned>{ 1, 4 }));
    std::cout << "[PASS] TestPlaceholderRegionSizeMismatch\n";
}

void TestDynamicItemIsNeverOffered() {
    std::vector<MenuNode> bar = { ItemNode(L"A", 11), ItemNode(L"Dynamic", kModifierDependent), ItemNode(L"B", 12) };
    auto commands = LinkOne(bar, { 1, 2, 3 });
    assert((LinkedNames(commands) == std::vector<std::wstring>{ L"A", L"B" }));
    assert((LinkedIds(commands) == std::vector<unsigned>{ 1, 3 }));
    commands = LinkOne(bar, { 1, 2, 3, 4 });
    assert((LinkedIds(commands) == std::vector<unsigned>{ 1, 4 }));
    assert(FindCommand(commands, L"Dynamic") == nullptr);
    std::cout << "[PASS] TestDynamicItemIsNeverOffered\n";
}

struct ExpectedLink {
    const wchar_t* name;
    const wchar_t* menuPath;
    unsigned liveId;
    std::vector<int> position;
};

// Nested popups are unreadable here, so only the items of the two menus are linked
void TestAnsichtExactLinkSet() {
    auto bar = AnsichtBar();
    FakeMenu live = AnsichtFake(kMeasuredAnsicht);
    auto commands = MenuLink::Link(bar, live, {});
    const ExpectedLink expected[] = {
        { L"Neu", L"Ablage", 77, { 0, 0 } },
        { L"Transparentpause", L"Ansicht", 1233, { 1, 2 } },
        { L"Konstruktionsraster darstellen", L"Ansicht", 256, { 1, 3 } },
        { L"Darstellung der Bearbeitungsebene", L"Ansicht", 391, { 1, 4 } },
        { L"Anzeige Ursprung", L"Ansicht", 393, { 1, 7 } },
        { L"Lineal", L"Ansicht", 255, { 1, 9 } },
        { L"Hilfslinien", L"Ansicht", 265, { 1, 10 } },
        { L"Entry 1", L"Ansicht", 415, { 1, 13 } },
        { L"Entry 2", L"Ansicht", 416, { 1, 14 } },
        { L"Entry 3", L"Ansicht", 371, { 1, 15 } },
        { L"Entry 4", L"Ansicht", 369, { 1, 16 } },
        { L"Entry 5", L"Ansicht", 261, { 1, 18 } },
        { L"Orbit", L"Ansicht", 1180, { 1, 25 } },
        { L"3D Rundgang", L"Ansicht", 1176, { 1, 26 } },
    };
    assert(commands.size() == std::size(expected));
    for (size_t i = 0; i < commands.size(); ++i) {
        assert(commands[i].name == expected[i].name);
        assert(commands[i].menuPath == expected[i].menuPath);
        assert(commands[i].liveId == expected[i].liveId);
        assert(commands[i].position == expected[i].position);
        assert(commands[i].enabled);
    }
    std::cout << "[PASS] TestAnsichtExactLinkSet\n";
}

void TestSameNameKeepsItsOwnPosition() {
    std::vector<MenuNode> bar = { PopupNode(L"First", { ItemNode(L"Settings", 11), ItemNode(L"Settings", 12) }),
                                  PopupNode(L"Second", { ItemNode(L"Settings", 13) }) };
    FakeMenu live({ L"First", L"Second" });
    live.Set({ 0 }, { 10, 20 });
    live.Set({ 1 }, { 30 });
    auto commands = MenuLink::Link(bar, live, {});
    assert(commands.size() == 3);
    assert((LinkedIds(commands) == std::vector<unsigned>{ 10, 20, 30 }));
    assert((commands[0].position == std::vector<int>{ 0, 0 }) && commands[0].menuPath == L"First");
    assert((commands[1].position == std::vector<int>{ 0, 1 }) && commands[1].menuPath == L"First");
    assert((commands[2].position == std::vector<int>{ 1, 0 }) && commands[2].menuPath == L"Second");
    std::cout << "[PASS] TestSameNameKeepsItsOwnPosition\n";
}

// The real graphic override popup is [408, S, 1840..1849, S, 566, S, 407] and 566 must stay
// unlinked, so the first live segment of the region is the list. This assumes the scheme lists
// every static segment.
void TestListRegionTakesFirstLiveSegment() {
    const std::vector<DynamicList> lists = { { kGraphicOverridePlaceholder, Wide({ L"X", L"Y", L"Z" }) } };
    auto commands = LinkOne(ListMenu(), { 1, S, 10, S, 11, 12, S, 2 }, lists);
    assert((LinkedNames(commands) == std::vector<std::wstring>{ L"A", L"X", L"B" }));
    assert((LinkedIds(commands) == std::vector<unsigned>{ 1, 10, 2 }));
    assert(!HasLiveId(commands, 11) && !HasLiveId(commands, 12));
    std::cout << "[PASS] TestListRegionTakesFirstLiveSegment\n";
}

static MenuNode AddOnNode(const wchar_t* name, unsigned number, unsigned mdid1, unsigned mdid2) {
    MenuNode node = ItemNode(name, number);
    node.mdid1 = mdid1;
    node.mdid2 = mdid2;
    return node;
}

static std::wstring DumpAll(const std::vector<MenuNode>& nodes) {
    std::wstring text;
    for (const MenuNode& node : nodes) text += (text.empty() ? L"" : L";") + Dump(node);
    return text;
}

// The export holds one command of the pair 7:8 and nothing else
static std::vector<MenuNode> ExportOfOneAddOn() {
    return { PopupNode(L"M", { AddOnNode(L"Kept", 20, 7, 8) }) };
}

void TestAbsentAddOnItemsAreDropped() {
    std::vector<MenuNode> bar = {
        PopupNode(L"M", { ItemNode(L"a", 10), SeparatorNode(),
                          PopupNode(L"Gone", { AddOnNode(L"b", 30, 1, 2), SeparatorNode(), AddOnNode(L"c", 31, 3, 4) }),
                          AddOnNode(L"d", 20, 7, 8), ItemNode(L"e", 11) }),
        PopupNode(L"Add-On menu", { PopupNode(L"Deep", { AddOnNode(L"f", 32, 1, 2) }) }),
        PopupNode(L"Last", { ItemNode(L"g", 12) }),
    };
    DropAbsentAddOnItems(bar, ExportOfOneAddOn());
    // Built-in a and e stay although the export does not list them; the popup that held only absent
    // items is gone, so d moves up; the add-on menu is gone with its only submenu
    assert(DumpAll(bar) == L"P:M[I:a#10,S,I:d#20,I:e#11];P:Last[I:g#12]");
    std::cout << "[PASS] TestAbsentAddOnItemsAreDropped\n";
}

void TestMixedPopupKeepsPresentItems() {
    std::vector<MenuNode> bar = { PopupNode(L"M", { PopupNode(L"Mixed", { AddOnNode(L"b", 30, 1, 2), AddOnNode(L"d", 20, 7, 8),
                                                                         SeparatorNode(), AddOnNode(L"c", 31, 3, 4) }) }) };
    DropAbsentAddOnItems(bar, ExportOfOneAddOn());
    assert(DumpAll(bar) == L"P:M[P:Mixed[I:d#20,S]]");
    // The pair decides, not the number: the same number with another pair is a different command
    bar = { PopupNode(L"M", { AddOnNode(L"x", 20, 7, 9), AddOnNode(L"y", 99, 7, 8) }) };
    DropAbsentAddOnItems(bar, ExportOfOneAddOn());
    assert(DumpAll(bar) == L"P:M[I:y#99]");
    std::cout << "[PASS] TestMixedPopupKeepsPresentItems\n";
}

void TestAlreadyEmptyPopupStays() {
    std::vector<MenuNode> bar = { PopupNode(L"M", { PopupNode(L"Empty", {}), PopupNode(L"Separators", { SeparatorNode() }),
                                                    PopupNode(L"Emptied", { AddOnNode(L"b", 30, 1, 2) }), ItemNode(L"a", 10) }),
                                  PopupNode(L"EmptyMenu", {}) };
    DropAbsentAddOnItems(bar, ExportOfOneAddOn());
    assert(DumpAll(bar) == L"P:M[P:Empty[],P:Separators[S],I:a#10];P:EmptyMenu[]");
    std::cout << "[PASS] TestAlreadyEmptyPopupStays\n";
}

void TestAttachNamesByFullId() {
    std::vector<MenuNode> bar = { PopupNode(L"M", { AddOnNode(L"", 1027, 5, 6), AddOnNode(L"", 1027, 7, 8), ItemNode(L"", 1027),
                                                    PopupNode(L"Sub", { ItemNode(L"", 2) }), ItemNode(L"", 3) }) };
    std::vector<MenuNode> exportBar = {
        PopupNode(L"Other", { AddOnNode(L"First add-on", 1027, 5, 6), ItemNode(L"Built-in", 1027) }),
        PopupNode(L"Elsewhere", { PopupNode(L"Deeper", { AddOnNode(L"Second add-on", 1027, 7, 8), ItemNode(L"Two", 2) }) }),
    };
    AttachNames(bar, exportBar);
    const std::vector<MenuNode>& items = bar[0].children;
    assert(items[0].name == L"First add-on" && items[1].name == L"Second add-on" && items[2].name == L"Built-in");
    assert(items[3].children[0].name == L"Two");
    assert(items[4].name.empty());   // no name in the export: stays empty
    assert(items[3].name == L"Sub" && bar[0].name == L"M");   // popups keep their own titles
    std::cout << "[PASS] TestAttachNamesByFullId\n";
}

void TestLinkedCommandCarriesItsFullId() {
    std::vector<MenuNode> bar = OneMenu({ AddOnNode(L"Add-on", 1027, 0x47246d64, 0x3bbe5ae9), ItemNode(L"", 12) });
    FakeMenu live = OneLive({ 1, 2 });
    auto commands = MenuLink::Link(bar, live, {});
    assert(commands.size() == 2);
    assert(commands[0].commandNumber == 1027 && commands[0].mdid1 == 0x47246d64u && commands[0].mdid2 == 0x3bbe5ae9u);
    // An item without a name is linked with an empty name; the position stays right
    assert(commands[1].name.empty() && commands[1].commandNumber == 12 && commands[1].liveId == 2);
    assert((commands[1].position == std::vector<int>{ 0, 1 }));
    std::cout << "[PASS] TestLinkedCommandCarriesItsFullId\n";
}

// Only these ids are replaced by Archicad at run time; the same number with another MDID is a normal command
void TestPlaceholdersAreKeyedByFullId() {
    const MenuNode notOffered[] = {
        AddOnNode(L"repeat last command", 1027, 0, 0), AddOnNode(L"reopen last output", 20754, 0, 0),
        AddOnNode(L"graphic override list", 23040, 0, 0),
    };
    for (const MenuNode& node : notOffered) {
        assert(LinkOne({ ItemNode(L"A", 11), node, ItemNode(L"B", 12) }, { 1, 2, 3 }).size() == 2);
    }
    // The list of an add-on is an add-on item too, so its part links nothing. In a part of its own it is
    // anchored around like any placeholder: an ordinary item would leave the extra live part unexplained.
    const MenuNode addOnList = AddOnNode(L"no variant combinations", 1027, 0x47246d64, 0xbca36043);
    assert(LinkOne({ ItemNode(L"A", 11), addOnList, ItemNode(L"B", 12) }, { 1, 2, 3 }).empty());
    assert((LinkedIds(LinkOne({ ItemNode(L"A", 11), SeparatorNode(), addOnList, SeparatorNode(), ItemNode(L"B", 12) },
                              { 1, S, 5, 6, S, 7, S, 3 })) == std::vector<unsigned>{ 1, 3 }));
    const MenuNode offered[] = {
        AddOnNode(L"select connected horizontal guides", 1027, 0x47246d64, 0x3bbe5ae9), AddOnNode(L"Add-On Manager", 1293, 0, 0),
        AddOnNode(L"same number, add-on MDID", 20754, 1, 2), AddOnNode(L"same number, add-on MDID", kAddOnPlaceholder, 1, 2),
    };
    for (const MenuNode& node : offered) {
        auto commands = LinkOne({ ItemNode(L"A", 11), node, ItemNode(L"B", 12) }, { 1, 2, 3 });
        assert(commands.size() == 3 && commands[1].liveId == 2 && commands[1].name == node.name);
    }
    std::cout << "[PASS] TestPlaceholdersAreKeyedByFullId\n";
}

void TestDroppedEntriesMarkTheirPart() {
    std::vector<MenuNode> bar = { PopupNode(L"M", { ItemNode(L"a", 10), SeparatorNode(), AddOnNode(L"b", 30, 1, 2), ItemNode(L"c", 11),
                                                    SeparatorNode(), PopupNode(L"Gone", { AddOnNode(L"d", 31, 1, 2) }), ItemNode(L"e", 12),
                                                    AddOnNode(L"f", 20, 7, 8), SeparatorNode(), ItemNode(L"g", 13) }) };
    DropAbsentAddOnItems(bar, ExportOfOneAddOn());
    const std::vector<MenuNode>& m = bar[0].children;
    assert(DumpAll(m) == L"I:a#10;S;I:c#11;S;I:e#12;I:f#20;S;I:g#13");
    // The part that lost an item and the one that lost a popup are marked, the others and the separators are not
    const bool expected[] = { false, false, true, false, true, true, false, false };
    for (size_t i = 0; i < m.size(); ++i) assert(m[i].partLostEntries == expected[i]);
    std::cout << "[PASS] TestDroppedEntriesMarkTheirPart\n";
}

// A part around a placeholder links nothing if it holds an add-on item or lost an entry to
// DropAbsentAddOnItems: the add-ons loaded now may not be the ones of the export
void TestUncertainPartAroundPlaceholderLinksNothing() {
    const MenuNode list = ItemNode(L"P", kGraphicOverridePlaceholder);
    assert(LinkOne({ AddOnNode(L"X", 9, 5, 6), ItemNode(L"A", 11), list, ItemNode(L"B", 12) }, { 1, 2, 3, 4 }).empty());
    assert(LinkOne({ ItemNode(L"A", 11), list, AddOnNode(L"X", 9, 5, 6) }, { 1, 2, 3, 4 }).empty());
    // Without a placeholder the exact count still decides
    assert(LinkOne({ AddOnNode(L"X", 9, 5, 6), ItemNode(L"A", 11) }, { 1, 2 }).size() == 2);
    // Y was dropped, but its add-on may be loaded by now
    std::vector<MenuNode> bar = OneMenu({ AddOnNode(L"Y", 9, 1, 2), ItemNode(L"A", 11), list, ItemNode(L"B", 12), SeparatorNode(),
                                          ItemNode(L"C", 13) });
    DropAbsentAddOnItems(bar, ExportOfOneAddOn());
    FakeMenu live = OneLive({ 9, 1, 5, 2, S, 3 });
    assert(LinkedNames(MenuLink::Link(bar, live, {})) == Wide({ L"C" }));
    // A lone list placeholder whose part lost an entry gives no names to the live part in its place
    bar = OneMenu({ ItemNode(L"A", 11), SeparatorNode(), AddOnNode(L"Y", 9, 1, 2), list, SeparatorNode(), ItemNode(L"B", 12) });
    DropAbsentAddOnItems(bar, ExportOfOneAddOn());
    FakeMenu listLive = OneLive({ 1, S, 9, 10, S, 2 });
    const std::vector<DynamicList> lists = { { kGraphicOverridePlaceholder, Wide({ L"X", L"Y", L"Z" }) } };
    assert(LinkedNames(MenuLink::Link(bar, listLive, lists)) == Wide({ L"A", L"B" }));
    std::cout << "[PASS] TestUncertainPartAroundPlaceholderLinksNothing\n";
}

// The add-on of Y is not loaded and the list is empty, so nothing of the part is left in the live menu;
// whether the add-on came back shows in the live part count. An add-on item that stays is a static entry.
void TestPlaceholderPartWithAddOnEntries() {
    std::vector<MenuNode> bar = OneMenu({ ItemNode(L"A", 11), SeparatorNode(), AddOnNode(L"Y", 9, 1, 2),
                                          ItemNode(L"P", kGraphicOverridePlaceholder), SeparatorNode(), ItemNode(L"B", 12) });
    DropAbsentAddOnItems(bar, ExportOfOneAddOn());
    FakeMenu live = OneLive({ 1, S, 2 });
    assert((LinkedIds(MenuLink::Link(bar, live, {})) == std::vector<unsigned>{ 1, 2 }));
    assert(LinkOne({ ItemNode(L"A", 11), SeparatorNode(), AddOnNode(L"Y", 9, 1, 2), ItemNode(L"P", kGraphicOverridePlaceholder),
                     SeparatorNode(), ItemNode(L"B", 12) }, { 1, S, 2 }).empty());
    std::cout << "[PASS] TestPlaceholderPartWithAddOnEntries\n";
}

void TestItemsNamedEmptyAreRemoved() {
    std::vector<MenuNode> bar = OneMenu({ ItemNode(L"a", 10), ItemNode(L"(Empty)", 20), AddOnNode(L"(Empty)", 21, 7, 8),
                                          PopupNode(L"Sub", { ItemNode(L"(Empty)", 22), ItemNode(L"b", 11) }) });
    ReduceToShownMenu(bar);
    assert(DumpAll(bar) == L"P:M[I:a#10,P:Sub[I:b#11]]");
    // Only the exact text counts, and an item without a name is a command the export does not know
    bar = OneMenu({ ItemNode(L"(empty)", 10), ItemNode(L"Empty", 11), ItemNode(L"(Empty) x", 12), ItemNode(L"", 13) });
    const std::wstring before = DumpAll(bar);
    ReduceToShownMenu(bar);
    assert(DumpAll(bar) == before);
    std::cout << "[PASS] TestItemsNamedEmptyAreRemoved\n";
}

void TestPlaceholdersNamedEmptyStay() {
    std::vector<MenuNode> bar = OneMenu({ ItemNode(L"(Empty)", kGraphicOverridePlaceholder), AddOnNode(L"(Empty)", 1027, 0x47246d64, 0xbca36043) });
    const std::wstring before = DumpAll(bar);
    ReduceToShownMenu(bar);
    assert(DumpAll(bar) == before);
    std::cout << "[PASS] TestPlaceholdersNamedEmptyStay\n";
}

void TestPopupsWithoutEntriesAreRemoved() {
    std::vector<MenuNode> bar = OneMenu({ PopupNode(L"Empty", {}), PopupNode(L"Separators", { SeparatorNode() }),
                                          PopupNode(L"Outer", { PopupNode(L"Inner", { ItemNode(L"(Empty)", 20) }), SeparatorNode() }),
                                          PopupNode(L"List", { ItemNode(L"P", kGraphicOverridePlaceholder) }), ItemNode(L"a", 10) });
    ReduceToShownMenu(bar);
    assert(DumpAll(bar) == L"P:M[P:List[I:P#23040],I:a#10]");
    std::cout << "[PASS] TestPopupsWithoutEntriesAreRemoved\n";
}

void TestSurplusSeparatorsAreRemoved() {
    std::vector<MenuNode> bar = OneMenu({ SeparatorNode(), SeparatorNode(), ItemNode(L"a", 10), SeparatorNode(), SeparatorNode(),
                                          ItemNode(L"b", 11), SeparatorNode(), ItemNode(L"(Empty)", 20), SeparatorNode(),
                                          PopupNode(L"Empty", {}), SeparatorNode(),
                                          PopupNode(L"Sub", { SeparatorNode(), ItemNode(L"c", 12), SeparatorNode() }), SeparatorNode() });
    ReduceToShownMenu(bar);
    assert(DumpAll(bar) == L"P:M[I:a#10,S,I:b#11,S,P:Sub[I:c#12]]");
    std::cout << "[PASS] TestSurplusSeparatorsAreRemoved\n";
}

void TestReductionKeepsTopLevelMenus() {
    std::vector<MenuNode> bar = { PopupNode(L"M", { ItemNode(L"(Empty)", 20) }), PopupNode(L"Empty", {}) };
    ReduceToShownMenu(bar);
    assert(DumpAll(bar) == L"P:M[];P:Empty[]");
    std::cout << "[PASS] TestReductionKeepsTopLevelMenus\n";
}

static std::wstring MarkedParts(const std::vector<MenuNode>& nodes) {
    std::wstring marks;
    for (const MenuNode& node : nodes) marks += node.partLostEntries ? L'x' : L'-';
    return marks;
}

// A part that lost an "(Empty)" Item may have shifted under the add-ons loaded now, like a part that lost an
// add-on item: its entries are marked, the separators and the other parts are not
void TestReductionMarksPartThatLostAnItem() {
    std::vector<MenuNode> bar = OneMenu({ ItemNode(L"a", 10), ItemNode(L"(Empty)", 20), ItemNode(L"b", 11), SeparatorNode(),
                                          ItemNode(L"c", 12), SeparatorNode(), ItemNode(L"d", 13), ItemNode(L"(Empty)", 21) });
    ReduceToShownMenu(bar);
    assert(DumpAll(bar) == L"P:M[I:a#10,I:b#11,S,I:c#12,S,I:d#13]");
    assert(MarkedParts(bar[0].children) == L"xx---x");
    std::cout << "[PASS] TestReductionMarksPartThatLostAnItem\n";
}

// Parts are the original ones: surplus separators go after the marking, so a mark never lands in the next part
void TestReductionMarksByOriginalParts() {
    std::vector<MenuNode> bar = OneMenu({ ItemNode(L"a", 10), SeparatorNode(), ItemNode(L"(Empty)", 20), SeparatorNode(), ItemNode(L"b", 11),
                                          SeparatorNode(), SeparatorNode(), ItemNode(L"c", 12), ItemNode(L"(Empty)", 21), SeparatorNode(),
                                          ItemNode(L"(Empty)", 22), SeparatorNode(), ItemNode(L"d", 13) });
    ReduceToShownMenu(bar);
    assert(DumpAll(bar) == L"P:M[I:a#10,S,I:b#11,S,I:c#12,S,I:d#13]");
    assert(MarkedParts(bar[0].children) == L"----x--");
    std::cout << "[PASS] TestReductionMarksByOriginalParts\n";
}

// Removing an empty popup, or a part that vanishes with its separators, marks nothing
void TestReductionMarksNothingForAnEmptyPopup() {
    std::vector<MenuNode> bar = OneMenu({ ItemNode(L"a", 10), PopupNode(L"Empty", {}), SeparatorNode(), ItemNode(L"b", 11) });
    ReduceToShownMenu(bar);
    assert(DumpAll(bar) == L"P:M[I:a#10,S,I:b#11]");
    assert(MarkedParts(bar[0].children) == L"---");
    std::cout << "[PASS] TestReductionMarksNothingForAnEmptyPopup\n";
}

// Each level has its own parts: the inner part that lost an item is marked, the popup and its neighbours are not
void TestReductionMarksPerLevel() {
    std::vector<MenuNode> bar = OneMenu({ ItemNode(L"a", 10),
                                          PopupNode(L"Sub", { ItemNode(L"x", 11), ItemNode(L"(Empty)", 20), SeparatorNode(), ItemNode(L"y", 12) }),
                                          ItemNode(L"b", 13) });
    ReduceToShownMenu(bar);
    assert(DumpAll(bar) == L"P:M[I:a#10,P:Sub[I:x#11,S,I:y#12],I:b#13]");
    assert(MarkedParts(bar[0].children) == L"---");
    assert(MarkedParts(bar[0].children[1].children) == L"x--");
    std::cout << "[PASS] TestReductionMarksPerLevel\n";
}

int main() {
    TestReadsFloorPlanBarOnly();
    TestRulerPosition();
    TestIdrefResolved();
    TestPlaceholderNumbers();
    TestNamesUnescaped();
    TestBomCommentsAndCrlf();
    TestMalformedIsEmpty();
    TestReferenceCycleIsEmpty();
    TestRejectionNeverYieldsPartialTree();
    TestDepthLimitNotCycle();
    TestNodeBudgetStopsExpansion();
    TestInputSizeLimit();
    TestSchemeReaderKeepsMdidParts();
    TestExactTreeShape();
    TestPicksBarByGuidNotOrder();
    TestDuplicateBarGuidIsEmpty();
    TestAmbiguousInputIsEmpty();
    TestNameLengthLimit();
    TestLinksRuler();
    TestLinksAreOrderedByPosition();
    TestAnchorsFromSegmentEnd();
    TestCountMismatchSkipsSegment();
    TestDisabledCarriedOver();
    TestRecursesIntoLinkedPopups();
    TestGraphicOverrideList();
    TestModelViewOptionsList();
    TestListTooShortNotOffered();
    TestListWithoutRegisteredPlaceholderStaysUnlinked();
    TestListNeedsOnlyItems();
    TestNewLivePartTakesPlaceOfLastSchemePart();
    TestHiddenStaticPartNextToPlaceholderRegionLinksToTheList();
    TestOnlyLinkedUnnamedItemsAreCounted();
    TestFitScoreRejectsOtherLanguage();
    TestFitScoreCountsLinks();
    TestTopLevelFitRules();
    TestTopLevelMenusMatchedByTitle();
    TestDuplicateTopLevelTitlesAreNotMatched();
    TestTopLevelOrderMustAgree();
    TestNoCommonTopLevelTitleDoesNotFit();
    TestEmptyTopLevelTitlesNeverMatch();
    TestEmptyBarDoesNotFit();
    TestTypeMismatchDropsSegment();
    TestTypeMismatchAtAnchorDropsSegment();
    TestMismatchedMenuLinksNothing();
    TestPlaceholderAtSegmentStart();
    TestPlaceholderAtSegmentEnd();
    TestOnePlaceholderEqualCount();
    TestTwoPlaceholdersInOneSegment();
    TestPlaceholderIsNeverOffered();
    TestPlaceholderSegmentVanishes();
    TestPlaceholderRegionSizeMismatch();
    TestDynamicItemIsNeverOffered();
    TestAnsichtExactLinkSet();
    TestSameNameKeepsItsOwnPosition();
    TestListRegionTakesFirstLiveSegment();
    TestAbsentAddOnItemsAreDropped();
    TestMixedPopupKeepsPresentItems();
    TestAlreadyEmptyPopupStays();
    TestAttachNamesByFullId();
    TestLinkedCommandCarriesItsFullId();
    TestPlaceholdersAreKeyedByFullId();
    TestDroppedEntriesMarkTheirPart();
    TestUncertainPartAroundPlaceholderLinksNothing();
    TestPlaceholderPartWithAddOnEntries();
    TestItemsNamedEmptyAreRemoved();
    TestPlaceholdersNamedEmptyStay();
    TestPopupsWithoutEntriesAreRemoved();
    TestSurplusSeparatorsAreRemoved();
    TestReductionKeepsTopLevelMenus();
    TestReductionMarksPartThatLostAnItem();
    TestReductionMarksByOriginalParts();
    TestReductionMarksNothingForAnEmptyPopup();
    TestReductionMarksPerLevel();
    std::cout << "All command line tests passed\n";
    return 0;
}
