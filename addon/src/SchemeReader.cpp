#include "SchemeReader.hpp"
#include <windows.h>
#include <climits>
#include <cwchar>
#include <map>
#include <utility>

namespace Baaz {

// "Nicht-Text" menu bar: used by every window except the text editors. The name is
// localized, so the GUID identifies it.
static const wchar_t* const kFloorPlanBarGuid = L"2085E4D3-5694-4BB9-ACAC-3AED5BE50091";
// Real schemes nest menus about six deep; the limit stops reference cycles and hostile files.
static const int kMaxMenuDepth = 16;
static const size_t kMaxXmlDepth = 64;

namespace {

struct XmlAttribute {
    std::wstring name;
    std::wstring value;
};

struct XmlElement {
    std::wstring name;
    std::vector<XmlAttribute> attributes;
    std::vector<XmlElement> children;
};

using MenuDefinitions = std::map<std::wstring, const XmlElement*>;

struct BuildContext {
    const MenuDefinitions& definitions;
    size_t nodes = 0;
};

} // namespace

static bool ToWide(const std::string& utf8, std::wstring& out) {
    size_t start = utf8.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    size_t length = utf8.size() - start;
    if (length == 0) return true;
    if (length > static_cast<size_t>(INT_MAX)) return false;
    const char* bytes = utf8.data() + start;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, static_cast<int>(length), nullptr, 0);
    if (count <= 0) return false;
    out.resize(static_cast<size_t>(count));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, static_cast<int>(length), out.data(), count) == count;
}

// Value of a decimal digit, or of a hexadecimal one when hex is set; -1 for anything else
static int DigitValue(wchar_t c, bool hex) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (!hex) return -1;
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

static bool ParseCodePoint(const std::wstring& body, unsigned& codePoint) {
    bool hex = body.size() > 1 && body[1] == L'x';
    size_t first = hex ? 2 : 1;
    if (first >= body.size()) return false;
    unsigned base = hex ? 16 : 10;
    codePoint = 0;
    for (size_t i = first; i < body.size(); ++i) {
        int digit = DigitValue(body[i], hex);
        if (digit < 0) return false;
        codePoint = codePoint * base + static_cast<unsigned>(digit);
        if (codePoint > 0x10FFFF) return false;
    }
    return true;
}

static void AppendCodePoint(unsigned codePoint, std::wstring& out) {
    if (codePoint < 0x10000) {
        out += static_cast<wchar_t>(codePoint);
        return;
    }
    codePoint -= 0x10000;
    out += static_cast<wchar_t>(0xD800 + (codePoint >> 10));
    out += static_cast<wchar_t>(0xDC00 + (codePoint & 0x3FF));
}

// body is the text between '&' and ';'
static bool DecodeEntity(const std::wstring& body, std::wstring& out) {
    if (body == L"amp") out += L'&';
    else if (body == L"lt") out += L'<';
    else if (body == L"gt") out += L'>';
    else if (body == L"quot") out += L'"';
    else if (body == L"apos") out += L'\'';
    else if (!body.empty() && body[0] == L'#') {
        unsigned codePoint = 0;
        // 0 and the surrogate range are not characters
        if (!ParseCodePoint(body, codePoint) || codePoint == 0 || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) return false;
        AppendCodePoint(codePoint, out);
    } else {
        return false;
    }
    return true;
}

static bool DecodeEntities(const std::wstring& raw, std::wstring& out) {
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != L'&') {
            out += raw[i];
            continue;
        }
        size_t end = raw.find(L';', i);
        if (end == std::wstring::npos || !DecodeEntity(raw.substr(i + 1, end - i - 1), out)) return false;
        i = end;
    }
    return true;
}

static const std::wstring* Attribute(const XmlElement& element, const wchar_t* name) {
    for (const XmlAttribute& attribute : element.attributes) {
        if (attribute.name == name) return &attribute.value;
    }
    return nullptr;
}

namespace {

// Reads elements and attributes; character data and the XML declaration are skipped.
class XmlParser {
public:
    explicit XmlParser(const std::wstring& text) : m_text(text) {}

    bool Parse(XmlElement& root) {
        std::vector<XmlElement*> open;
        bool haveRoot = false;
        while (true) {
            m_pos = m_text.find(L'<', m_pos);
            if (m_pos == std::wstring::npos) break;
            if (!ParseMarkup(root, open, haveRoot)) return false;
        }
        return haveRoot && open.empty();
    }

private:
    const std::wstring& m_text;
    size_t m_pos = 0;

    bool StartsWith(const wchar_t* prefix) const { return m_text.compare(m_pos, wcslen(prefix), prefix) == 0; }

    bool SkipPast(const wchar_t* terminator) {
        size_t end = m_text.find(terminator, m_pos);
        if (end == std::wstring::npos) return false;
        m_pos = end + wcslen(terminator);
        return true;
    }

    void SkipSpace() {
        while (m_pos < m_text.size() && wcschr(L" \t\r\n", m_text[m_pos]) != nullptr) ++m_pos;
    }

    bool ReadName(std::wstring& name) {
        size_t start = m_pos;
        while (m_pos < m_text.size() && wcschr(L" \t\r\n/>=<\"", m_text[m_pos]) == nullptr) ++m_pos;
        name = m_text.substr(start, m_pos - start);
        return !name.empty();
    }

    bool ParseMarkup(XmlElement& root, std::vector<XmlElement*>& open, bool& haveRoot) {
        if (StartsWith(L"<!--")) {
            m_pos += 4;
            return SkipPast(L"-->");
        }
        if (StartsWith(L"<?")) {
            m_pos += 2;
            return SkipPast(L"?>");
        }
        if (StartsWith(L"<!")) return false;
        if (StartsWith(L"</")) return ParseEndTag(open);
        return ParseStartTag(root, open, haveRoot);
    }

    bool ParseEndTag(std::vector<XmlElement*>& open) {
        m_pos += 2;
        std::wstring name;
        if (!ReadName(name)) return false;
        SkipSpace();
        if (m_pos >= m_text.size() || m_text[m_pos] != L'>' || open.empty() || open.back()->name != name) return false;
        ++m_pos;
        open.pop_back();
        return true;
    }

    bool ParseStartTag(XmlElement& root, std::vector<XmlElement*>& open, bool& haveRoot) {
        ++m_pos;
        XmlElement element;
        bool selfClosing = false;
        if (!ReadName(element.name) || !ParseAttributes(element, selfClosing)) return false;

        XmlElement* placed = nullptr;
        if (open.empty()) {
            if (haveRoot) return false;
            haveRoot = true;
            root = std::move(element);
            placed = &root;
        } else {
            open.back()->children.push_back(std::move(element));
            placed = &open.back()->children.back();
        }
        if (selfClosing) return true;
        open.push_back(placed);
        return open.size() <= kMaxXmlDepth;
    }

    bool ParseAttributes(XmlElement& element, bool& selfClosing) {
        while (true) {
            SkipSpace();
            if (m_pos >= m_text.size()) return false;
            if (m_text[m_pos] == L'>') {
                ++m_pos;
                return true;
            }
            if (StartsWith(L"/>")) {
                m_pos += 2;
                selfClosing = true;
                return true;
            }
            XmlAttribute attribute;
            // Two values for one name: which one is meant?
            if (!ParseAttribute(attribute) || Attribute(element, attribute.name.c_str()) != nullptr) return false;
            element.attributes.push_back(std::move(attribute));
        }
    }

    bool ParseAttribute(XmlAttribute& attribute) {
        if (!ReadName(attribute.name)) return false;
        SkipSpace();
        if (m_pos >= m_text.size() || m_text[m_pos] != L'=') return false;
        ++m_pos;
        SkipSpace();
        if (m_pos >= m_text.size() || m_text[m_pos] != L'"') return false;
        ++m_pos;
        // '<' is not allowed in a value; hitting it first means the closing quote is missing
        size_t end = m_text.find_first_of(L"\"<", m_pos);
        if (end == std::wstring::npos || m_text[end] != L'"') return false;
        std::wstring raw = m_text.substr(m_pos, end - m_pos);
        m_pos = end + 1;
        return DecodeEntities(raw, attribute.value);
    }
};

} // namespace

static bool CollectDefinitions(const XmlElement& element, MenuDefinitions& definitions) {
    const std::wstring* id = Attribute(element, L"ID");
    if (element.name == L"Menu" && id != nullptr && !definitions.emplace(*id, &element).second) return false;
    for (const XmlElement& child : element.children) {
        if (!CollectDefinitions(child, definitions)) return false;
    }
    return true;
}

// Fails if a second bar carries the GUID: there is no telling which one is in use
static bool FindFloorPlanBar(const XmlElement& element, const XmlElement*& bar) {
    const std::wstring* guid = Attribute(element, L"menubarguid");
    if (element.name == L"MenuBar" && guid != nullptr && *guid == kFloorPlanBarGuid) {
        if (bar != nullptr) return false;
        bar = &element;
    }
    for (const XmlElement& child : element.children) {
        if (!FindFloorPlanBar(child, bar)) return false;
    }
    return true;
}

std::wstring CleanName(const std::wstring& raw) {
    std::wstring name;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != L'&') {
            name += raw[i];
        } else if (i + 1 < raw.size() && raw[i + 1] == L'&') {
            name += L'&';
            ++i;
        }
    }
    return name;
}

static bool ReadName(const std::wstring& raw, std::wstring& name) {
    name = CleanName(raw);
    return name.size() <= kMaxNameLength;
}

// "0x" followed by at least one hexadecimal digit
static bool IsHexLiteral(const std::wstring& part) {
    return part.size() > 2 && part.compare(0, 2, L"0x") == 0 &&
           part.find_first_not_of(L"0123456789abcdefABCDEF", 2) == std::wstring::npos;
}

static bool ParseDecimal(const std::wstring& digits, unsigned& number) {
    if (digits.empty() || digits.find_first_not_of(L"0123456789") != std::wstring::npos) return false;
    unsigned long long value = 0;
    for (wchar_t digit : digits) {
        value = value * 10 + static_cast<unsigned long long>(digit - L'0');
        if (value > UINT_MAX) return false;
    }
    number = static_cast<unsigned>(value);
    return true;
}

// "0x" followed by at least one hexadecimal digit that fits 32 bits
static bool ParseHex32(const std::wstring& literal, unsigned& value) {
    if (!IsHexLiteral(literal)) return false;
    unsigned long long result = 0;
    for (size_t i = 2; i < literal.size(); ++i) {
        result = result * 16 + static_cast<unsigned long long>(DigitValue(literal[i], true));
        if (result > UINT_MAX) return false;
    }
    value = static_cast<unsigned>(result);
    return true;
}

// command="<n>-<mdid1>-<mdid2>" with decimal n and two hexadecimal MDID parts
static bool ParseCommand(const std::wstring& command, MenuNode& node) {
    size_t first = command.find(L'-');
    size_t second = first == std::wstring::npos ? first : command.find(L'-', first + 1);
    if (second == std::wstring::npos || command.find(L'-', second + 1) != std::wstring::npos) return false;
    return ParseHex32(command.substr(first + 1, second - first - 1), node.mdid1) &&
           ParseHex32(command.substr(second + 1), node.mdid2) &&
           ParseDecimal(command.substr(0, first), node.commandNumber);
}

static bool BuildItem(const XmlElement& element, MenuNode& node) {
    const std::wstring* command = Attribute(element, L"command");
    const std::wstring* comment = Attribute(element, L"comment");
    if (command == nullptr || comment == nullptr) return false;
    node.type = MenuNodeType::Item;
    return ReadName(*comment, node.name) && ParseCommand(*command, node);
}

static bool BuildPopup(const XmlElement& menu, BuildContext& context, int depth, MenuNode& node);

// <PopupItem> holds exactly one <Menu>, defined inline or by IDREF
static bool BuildPopupItem(const XmlElement& element, BuildContext& context, int depth, MenuNode& node) {
    if (element.children.size() != 1 || element.children[0].name != L"Menu") return false;
    return BuildPopup(element.children[0], context, depth, node);
}

static bool CountNode(BuildContext& context) {
    return ++context.nodes <= kMaxMenuNodes;
}

static bool BuildEntry(const XmlElement& element, BuildContext& context, int depth, MenuNode& node) {
    // Only a popup has children; on an item or separator they would be dropped silently
    if (!CountNode(context) || (element.name != L"PopupItem" && !element.children.empty())) return false;
    if (element.name == L"Item") return BuildItem(element, node);
    if (element.name == L"Separator") {
        node.type = MenuNodeType::Separator;
        return true;
    }
    if (element.name == L"PopupItem") return BuildPopupItem(element, context, depth, node);
    // An unknown entry would shift the positions the linking relies on
    return false;
}

static const XmlElement* ResolveMenu(const XmlElement& menu, const MenuDefinitions& definitions) {
    const std::wstring* reference = Attribute(menu, L"IDREF");
    if (reference == nullptr) return &menu;
    // A reference with content of its own would hide that content
    if (!menu.children.empty()) return nullptr;
    auto found = definitions.find(*reference);
    return found == definitions.end() ? nullptr : found->second;
}

static bool BuildPopup(const XmlElement& menu, BuildContext& context, int depth, MenuNode& node) {
    if (depth > kMaxMenuDepth) return false;
    const XmlElement* definition = ResolveMenu(menu, context.definitions);
    const std::wstring* title = definition == nullptr ? nullptr : Attribute(*definition, L"title");
    if (title == nullptr) return false;
    node.type = MenuNodeType::Popup;
    if (!ReadName(*title, node.name)) return false;
    for (const XmlElement& child : definition->children) {
        MenuNode entry;
        if (!BuildEntry(child, context, depth + 1, entry)) return false;
        node.children.push_back(std::move(entry));
    }
    return true;
}

static bool BuildMenuBar(const XmlElement& bar, BuildContext& context, std::vector<MenuNode>& menus) {
    for (const XmlElement& child : bar.children) {
        MenuNode menu;
        if (!CountNode(context) || child.name != L"Menu" || !BuildPopup(child, context, 1, menu)) return false;
        menus.push_back(std::move(menu));
    }
    return true;
}

static bool ParseDocument(const std::string& utf8Xml, XmlElement& root, MenuDefinitions& definitions) {
    std::wstring text;
    return utf8Xml.size() <= kMaxInputBytes && ToWide(utf8Xml, text) && XmlParser(text).Parse(root) &&
           CollectDefinitions(root, definitions);
}

std::vector<MenuNode> SchemeReader::ReadFloorPlanMenuBar(const std::string& utf8Xml) {
    XmlElement root;
    MenuDefinitions definitions;
    const XmlElement* bar = nullptr;
    std::vector<MenuNode> menus;
    if (!ParseDocument(utf8Xml, root, definitions) || !FindFloorPlanBar(root, bar) || bar == nullptr) return {};
    BuildContext context{ definitions };
    if (!BuildMenuBar(*bar, context, menus)) return {};
    return menus;
}

} // namespace Baaz
