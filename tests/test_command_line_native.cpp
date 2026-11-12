// Keep assert() active in Release builds too
#undef NDEBUG
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
    std::cout << "All command line tests passed\n";
    return 0;
}
