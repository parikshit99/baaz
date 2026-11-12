#include "MenuLink.hpp"
#include <algorithm>
#include <array>
#include <iterator>
#include <map>
#include <set>
#include <utility>

namespace Baaz {

// Built-in commands (MDID 0) of the AC29 standard scheme whose item Archicad replaces at run time or
// whose meaning depends on a modifier key. Found by the German names of the built-in items that
// start with "Keine ", "Kein ", "Letzte" or "Add-On" (which also covers "Add-On-"), and by the items
// flagged dynamic="yes" in the scheme (exactly two: 514 and 2563). This includes graphic override
// combinations (23040) and model view options (9472). The numbers do not depend on the language.
// 1027 (repeat last command) and 20754 (reopen last output) are in this list for their MDID-0 item
// only. Add-On Manager (1293) matched "Add-On" by name only and is a static command, so it is not
// listed. Sorted for binary search.
static constexpr unsigned kBuiltInPlaceholders[] = {
    514, 522, 542, 550, 551, 778, 787, 794, 1027, 1549, 1551, 1802,
    1803, 2055, 2563, 2578, 2817, 2825, 2826, 2832, 2833, 2834, 2835, 2836,
    2841, 2848, 2851, 3331, 3332, 3333, 3356, 5901, 6409, 6658, 6660, 8196,
    8449, 8450, 8706, 8962, 9218, 9472, 9986, 9987, 10008, 10018, 10019, 10020,
    10027, 10028, 10034, 10497, 10754, 10755, 11010, 11011, 11266, 11267, 11522, 11523,
    11778, 11779, 12034, 12035, 14341, 15105, 16129, 16641, 16642, 17923, 17924, 20754,
    20994, 20995, 21250, 21251, 21506, 21507, 21768, 22018, 22019, 22530, 22785, 23040
};

static_assert(std::is_sorted(std::begin(kBuiltInPlaceholders), std::end(kBuiltInPlaceholders)),
              "kBuiltInPlaceholders is searched with binary_search");

// A list that an add-on puts into the menu: the variant combinations of the add-on that also has the
// static command 1027-0x47246d64-0x3bbe5ae9, which is offered like any other command.
struct AddOnPlaceholder {
    unsigned number;
    unsigned mdid1;
    unsigned mdid2;
};

static constexpr AddOnPlaceholder kAddOnPlaceholders[] = { { 1027, 0x47246d64, 0xbca36043 } };

static const wchar_t* const kPathSeparator = L" \u203a ";

static bool IsPlaceholder(const MenuNode& node) {
    if (node.type != MenuNodeType::Item) return false;
    if (node.mdid1 == 0 && node.mdid2 == 0) {
        return std::binary_search(std::begin(kBuiltInPlaceholders), std::end(kBuiltInPlaceholders), node.commandNumber);
    }
    return std::any_of(std::begin(kAddOnPlaceholders), std::end(kAddOnPlaceholders), [&node](const AddOnPlaceholder& placeholder) {
        return placeholder.number == node.commandNumber && placeholder.mdid1 == node.mdid1 && placeholder.mdid2 == node.mdid2;
    });
}

namespace {

// Entries [begin, end) between two separators
struct Segment {
    size_t begin = 0;
    size_t end = 0;
    size_t size() const { return end - begin; }
};

// Indices into the catalogue entries and the live entries of one menu
struct EntryPair {
    size_t catalogue = 0;
    size_t live = 0;
};

// Offsets of the placeholders within a segment
struct PlaceholderSpan {
    size_t count = 0;
    size_t first = 0;
    size_t last = 0;
};

struct LinkRun {
    MenuSource& source;
    const std::vector<DynamicList>& lists;
    std::vector<LinkedCommand> commands;
    size_t unnamedItems = 0;     // linked Items of the scheme that have no name
};

} // namespace

template <typename Entry>
static std::vector<Segment> SplitSegments(const std::vector<Entry>& entries) {
    std::vector<Segment> segments;
    size_t begin = 0;
    for (size_t i = 0; i <= entries.size(); ++i) {
        if (i == entries.size() || entries[i].type == MenuNodeType::Separator) {
            segments.push_back({ begin, i });
            begin = i + 1;
        }
    }
    return segments;
}

// The part may have shifted since the export: it holds an add-on item, whose add-on may have been unloaded
// since, or it lost an entry: to DropAbsentAddOnItems, whose add-on may have been loaded since, or to
// ReduceToShownMenu, whose "(Empty)" item may be shown after all. Next to a placeholder the item count
// cannot show such a shift.
static bool IsUncertainPart(const std::vector<MenuNode>& catalogue, const Segment& segment) {
    for (size_t i = segment.begin; i < segment.end; ++i) {
        const MenuNode& node = catalogue[i];
        if (node.partLostEntries || (node.type == MenuNodeType::Item && (node.mdid1 != 0 || node.mdid2 != 0))) return true;
    }
    return false;
}

static void AddFromStart(std::vector<EntryPair>& pairs, const Segment& catalogue, const Segment& live, size_t count) {
    for (size_t i = 0; i < count; ++i) pairs.push_back({ catalogue.begin + i, live.begin + i });
}

static void AddFromEnd(std::vector<EntryPair>& pairs, const Segment& catalogue, const Segment& live, size_t count) {
    for (size_t i = 1; i <= count; ++i) pairs.push_back({ catalogue.end - i, live.end - i });
}

namespace {

// One menu: pairs its segments, then its entries, and descends into the popups it could pair
class MenuLevel {
public:
    MenuLevel(LinkRun& run, const std::vector<MenuNode>& catalogue, std::vector<int> path, std::wstring menuPath)
        : m_run(run), m_catalogue(catalogue), m_path(std::move(path)), m_menuPath(std::move(menuPath)),
          m_live(run.source.Entries(m_path)), m_catalogueSegments(SplitSegments(catalogue)),
          m_liveSegments(SplitSegments(m_live)) {}

    void Link() {
        LinkSegments();
        EmitPairs();
    }

private:
    PlaceholderSpan FindPlaceholders(const Segment& segment) const {
        PlaceholderSpan span;
        for (size_t i = segment.begin; i < segment.end; ++i) {
            if (!IsPlaceholder(m_catalogue[i])) continue;
            if (span.count == 0) span.first = i - segment.begin;
            span.last = i - segment.begin;
            ++span.count;
        }
        return span;
    }

    bool FindPlaceholderSegments(size_t& first, size_t& last) const {
        bool found = false;
        for (size_t i = 0; i < m_catalogueSegments.size(); ++i) {
            if (FindPlaceholders(m_catalogueSegments[i]).count == 0) continue;
            if (!found) first = i;
            last = i;
            found = true;
        }
        return found;
    }

    // Aligns from both ends up to the segments with placeholders. What Archicad builds in their
    // place (a list, or nothing) is the region in between.
    void LinkSegments() {
        const size_t count = m_catalogueSegments.size();
        size_t first = 0;
        size_t last = 0;
        if (!FindPlaceholderSegments(first, last)) {
            // Without a placeholder, a different number of segments means something else changed
            if (count == m_liveSegments.size()) PairRun(0, 0, count);
            return;
        }
        const size_t head = first;
        const size_t tail = count - 1 - last;
        // A region of placeholders alone vanishes with its separators when its lists are empty. A static
        // entry cannot vanish, and with fewer live segments the two ends overlap.
        const bool regionVanished = m_liveSegments.size() == head + tail && HoldsOnlyPlaceholders(first, last);
        if (m_liveSegments.size() < head + tail + 1 && !regionVanished) return;
        PairRun(0, 0, head);
        PairRun(count - tail, m_liveSegments.size() - tail, tail);
        if (!regionVanished) LinkRegion(first, last, head, m_liveSegments.size() - head - tail);
    }

    bool HoldsOnlyPlaceholders(size_t first, size_t last) const {
        for (size_t i = first; i <= last; ++i) {
            if (FindPlaceholders(m_catalogueSegments[i]).count != m_catalogueSegments[i].size()) return false;
        }
        return true;
    }

    void LinkRegion(size_t first, size_t last, size_t liveFirst, size_t liveCount) {
        // The first live segment is the list: the real graphic override popup holds a segment
        // after it (566) that must stay unlinked. This relies on the scheme listing every
        // static segment.
        if (first == last && IsLonePlaceholder(m_catalogueSegments[first])) {
            LinkList(m_catalogueSegments[first], m_liveSegments[liveFirst]);
            return;
        }
        // With several catalogue segments an equal live count cannot show which is which
        if (first == last && liveCount == 1) PairRun(first, liveFirst, 1);
    }

    bool IsLonePlaceholder(const Segment& segment) const {
        return segment.size() == 1 && IsPlaceholder(m_catalogue[segment.begin]);
    }

    void PairRun(size_t catalogueFirst, size_t liveFirst, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            PairSegment(m_catalogueSegments[catalogueFirst + i], m_liveSegments[liveFirst + i]);
        }
    }

    void PairSegment(const Segment& catalogue, const Segment& live) {
        std::vector<EntryPair> pairs = SegmentPairs(catalogue, live);
        for (const EntryPair& pair : pairs) {
            if (m_catalogue[pair.catalogue].type != m_live[pair.live].type) return;
        }
        m_pairs.insert(m_pairs.end(), pairs.begin(), pairs.end());
    }

    std::vector<EntryPair> SegmentPairs(const Segment& catalogue, const Segment& live) const {
        std::vector<EntryPair> pairs;
        const PlaceholderSpan span = FindPlaceholders(catalogue);
        // A skipped uncertain part links nothing, whatever the live count
        if (span.count > 0 && IsUncertainPart(m_catalogue, catalogue)) return pairs;
        // One placeholder cannot change the count unless it became exactly one entry. With two,
        // one may have grown while the other vanished, so only the entries at the ends are sure.
        if (catalogue.size() == live.size() && span.count <= 1) {
            AddFromStart(pairs, catalogue, live, catalogue.size());
            return pairs;
        }
        if (span.count == 0) return pairs;
        const size_t before = span.first;
        const size_t after = catalogue.size() - 1 - span.last;
        if (live.size() < before + after) return pairs;
        AddFromStart(pairs, catalogue, live, before);
        AddFromEnd(pairs, catalogue, live, after);
        return pairs;
    }

    const DynamicList* FindList(unsigned placeholder) const {
        const DynamicList* found = nullptr;
        for (const DynamicList& list : m_run.lists) {
            if (list.placeholder != placeholder) continue;
            // Two lists for one placeholder: which one is meant?
            if (found != nullptr) return nullptr;
            found = &list;
        }
        return found;
    }

    void LinkList(const Segment& placeholder, const Segment& entries) {
        if (IsUncertainPart(m_catalogue, placeholder)) return;
        const DynamicList* list = FindList(m_catalogue[placeholder.begin].commandNumber);
        if (list == nullptr || entries.size() == 0 || list->names.size() < entries.size()) return;
        for (size_t i = entries.begin; i < entries.end; ++i) {
            if (m_live[i].type != MenuNodeType::Item) return;
        }
        for (size_t i = entries.begin; i < entries.end; ++i) {
            m_run.commands.push_back(MakeCommand(list->names[i - entries.begin], i));
        }
    }

    std::vector<int> Position(size_t liveIndex) const {
        std::vector<int> position = m_path;
        position.push_back(static_cast<int>(liveIndex));
        return position;
    }

    LinkedCommand MakeCommand(const std::wstring& name, size_t liveIndex) const {
        LinkedCommand command;
        command.name = name;
        command.menuPath = m_menuPath;
        command.position = Position(liveIndex);
        command.liveId = m_live[liveIndex].liveId;
        command.enabled = m_live[liveIndex].enabled;
        return command;
    }

    void EmitPairs() {
        for (const EntryPair& pair : m_pairs) {
            const MenuNode& node = m_catalogue[pair.catalogue];
            if (node.type == MenuNodeType::Popup) {
                MenuLevel(m_run, node.children, Position(pair.live), m_menuPath + kPathSeparator + node.name).Link();
            } else if (!IsPlaceholder(node)) {
                if (node.name.empty()) ++m_run.unnamedItems;
                LinkedCommand command = MakeCommand(node.name, pair.live);
                command.commandNumber = node.commandNumber;
                command.mdid1 = node.mdid1;
                command.mdid2 = node.mdid2;
                m_run.commands.push_back(std::move(command));
            }
        }
    }

    LinkRun& m_run;
    const std::vector<MenuNode>& m_catalogue;
    std::vector<int> m_path;
    std::wstring m_menuPath;
    std::vector<LiveEntry> m_live;
    std::vector<Segment> m_catalogueSegments;
    std::vector<Segment> m_liveSegments;
    std::vector<EntryPair> m_pairs;
};

} // namespace

namespace {

// A top-level menu of the catalogue and the live menu bar slot with the same title
struct MatchedMenu {
    size_t catalogue = 0;
    size_t live = 0;
};

} // namespace

static bool IsUniqueIn(const std::vector<std::wstring>& titles, const std::wstring& title) {
    return std::count(titles.begin(), titles.end(), title) == 1;
}

// Pairs the top-level menus that have a title in common, in catalogue order. A title counts only if it
// occurs once on each side and is not empty. Menus without a partner (add-on menus, Teamwork, empty
// slots) are left out, because the live bar changes with the loaded add-ons and the project. false if
// there is no pair, or if the pairs are not in the same order on both sides.
static bool MatchMenus(const std::vector<MenuNode>& bar, const std::vector<std::wstring>& liveTitles,
                       std::vector<MatchedMenu>& matched) {
    std::vector<std::wstring> titles;
    for (const MenuNode& menu : bar) {
        if (menu.type != MenuNodeType::Popup) return false;
        titles.push_back(menu.name);
    }
    for (size_t i = 0; i < titles.size(); ++i) {
        if (titles[i].empty() || !IsUniqueIn(titles, titles[i]) || !IsUniqueIn(liveTitles, titles[i])) continue;
        const size_t live = static_cast<size_t>(std::find(liveTitles.begin(), liveTitles.end(), titles[i]) - liveTitles.begin());
        if (!matched.empty() && live <= matched.back().live) return false;
        matched.push_back({ i, live });
    }
    return !matched.empty();
}

// false if the live menu bar has no menu in common with the catalogue, or in another order
static bool LinkBar(const std::vector<MenuNode>& bar, MenuSource& live, const std::vector<DynamicList>& lists,
                    std::vector<LinkedCommand>& linked, size_t& notSearchable) {
    std::vector<MatchedMenu> matched;
    if (!MatchMenus(bar, live.TopLevelTitles(), matched)) return false;
    LinkRun run{ live, lists, {} };
    for (const MatchedMenu& menu : matched) {
        MenuLevel(run, bar[menu.catalogue].children, { static_cast<int>(menu.live) }, bar[menu.catalogue].name).Link();
    }
    std::sort(run.commands.begin(), run.commands.end(),
              [](const LinkedCommand& a, const LinkedCommand& b) { return a.position < b.position; });
    linked = std::move(run.commands);
    notSearchable = run.unnamedItems;
    return true;
}

std::vector<LinkedCommand> MenuLink::Link(const std::vector<MenuNode>& bar, MenuSource& live,
                                          const std::vector<DynamicList>& lists, size_t* notSearchable) {
    std::vector<LinkedCommand> linked;
    size_t count = 0;
    LinkBar(bar, live, lists, linked, count);
    if (notSearchable != nullptr) *notSearchable = count;
    return linked;
}

int MenuLink::FitScore(const std::vector<MenuNode>& bar, MenuSource& live) {
    std::vector<LinkedCommand> linked;
    size_t notSearchable = 0;
    if (!LinkBar(bar, live, {}, linked, notSearchable)) return -1;
    return static_cast<int>(linked.size());
}

using MdidPair = std::pair<unsigned, unsigned>;

static void CollectMdids(const std::vector<MenuNode>& nodes, std::set<MdidPair>& pairs) {
    for (const MenuNode& node : nodes) {
        if (node.type == MenuNodeType::Item) pairs.insert({ node.mdid1, node.mdid2 });
        else CollectMdids(node.children, pairs);
    }
}

static bool IsAbsentAddOnItem(const MenuNode& node, const std::set<MdidPair>& present) {
    return node.type == MenuNodeType::Item && (node.mdid1 != 0 || node.mdid2 != 0) &&
           present.count({ node.mdid1, node.mdid2 }) == 0;
}

static bool HoldsItemOrPopup(const std::vector<MenuNode>& nodes) {
    return std::any_of(nodes.begin(), nodes.end(), [](const MenuNode& node) { return node.type != MenuNodeType::Separator; });
}

// Marks the entries kept since the start of the part, if the part lost an entry
static void MarkPart(std::vector<MenuNode>& kept, size_t partBegin, bool lost) {
    if (!lost) return;
    for (size_t i = partBegin; i < kept.size(); ++i) kept[i].partLostEntries = true;
}

static void DropAbsent(std::vector<MenuNode>& nodes, const std::set<MdidPair>& present) {
    std::vector<MenuNode> kept;
    size_t partBegin = 0;
    bool partLost = false;
    for (MenuNode& node : nodes) {
        if (node.type == MenuNodeType::Separator) {
            MarkPart(kept, partBegin, partLost);
            partBegin = kept.size() + 1;
            partLost = false;
        }
        const bool hadContent = node.type == MenuNodeType::Popup && HoldsItemOrPopup(node.children);
        if (node.type == MenuNodeType::Popup) DropAbsent(node.children, present);
        const bool emptied = hadContent && !HoldsItemOrPopup(node.children);
        if (!emptied && !IsAbsentAddOnItem(node, present)) kept.push_back(std::move(node));
        else partLost = true;
    }
    MarkPart(kept, partBegin, partLost);
    nodes = std::move(kept);
}

void DropAbsentAddOnItems(std::vector<MenuNode>& bar, const std::vector<MenuNode>& exportBar) {
    std::set<MdidPair> present;
    CollectMdids(exportBar, present);
    DropAbsent(bar, present);
}

// number, mdid1, mdid2
using CommandId = std::array<unsigned, 3>;

static CommandId IdOf(const MenuNode& node) { return { node.commandNumber, node.mdid1, node.mdid2 }; }

// The same command can sit in several menus; any of its names is a name of that command
static void CollectNames(const std::vector<MenuNode>& nodes, std::map<CommandId, std::wstring>& names) {
    for (const MenuNode& node : nodes) {
        if (node.type == MenuNodeType::Item && !node.name.empty()) names.emplace(IdOf(node), node.name);
        else CollectNames(node.children, names);
    }
}

static void SetNames(std::vector<MenuNode>& nodes, const std::map<CommandId, std::wstring>& names) {
    for (MenuNode& node : nodes) {
        const auto found = node.type == MenuNodeType::Item ? names.find(IdOf(node)) : names.end();
        if (found != names.end()) node.name = found->second;
        SetNames(node.children, names);
    }
}

void AttachNames(std::vector<MenuNode>& bar, const std::vector<MenuNode>& exportBar) {
    std::map<CommandId, std::wstring> names;
    CollectNames(exportBar, names);
    SetNames(bar, names);
}

// Not translated: the export writes this text for a command this Archicad no longer offers
static const wchar_t* const kNotOfferedName = L"(Empty)";

static bool IsNotOfferedItem(const MenuNode& node) {
    return node.type == MenuNodeType::Item && node.name == kNotOfferedName && !IsPlaceholder(node);
}

static bool IsNotShown(const MenuNode& node) {
    if (node.type == MenuNodeType::Popup) return !HoldsItemOrPopup(node.children);
    return IsNotOfferedItem(node);
}

// The parts are those between the separators of the scheme as it was; a part that lost an Item is marked
// before surplus separators go, so a mark stays in its own part. A popup removed alone marks nothing.
static void ReduceEntries(std::vector<MenuNode>& nodes) {
    std::vector<MenuNode> kept;
    size_t partBegin = 0;
    bool partLost = false;
    for (MenuNode& node : nodes) {
        ReduceEntries(node.children);
        if (IsNotShown(node)) {
            partLost = partLost || IsNotOfferedItem(node);
            continue;
        }
        const bool isSeparator = node.type == MenuNodeType::Separator;
        if (isSeparator) {
            MarkPart(kept, partBegin, partLost);
            partLost = false;
        }
        const bool surplusSeparator = isSeparator && (kept.empty() || kept.back().type == MenuNodeType::Separator);
        if (!surplusSeparator) kept.push_back(std::move(node));
        if (isSeparator) partBegin = kept.size();
    }
    MarkPart(kept, partBegin, partLost);
    if (!kept.empty() && kept.back().type == MenuNodeType::Separator) kept.pop_back();
    nodes = std::move(kept);
}

void ReduceToShownMenu(std::vector<MenuNode>& bar) {
    for (MenuNode& menu : bar) ReduceEntries(menu.children);
}

} // namespace Baaz
