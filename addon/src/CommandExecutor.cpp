#include "CommandExecutor.hpp"
#include "CommandSource.hpp"
#include "SettingsDialog.hpp"
#include "BaazAddon.hpp"

#ifdef ACAPI_VERSION
#include "APIEnvir.h"
#include "ACAPinc.h"
#else
#include <cwctype>
#endif

namespace Baaz {

#ifdef ACAPI_VERSION

static bool SetTool(const SearchItem& item) {
    API_ToolBoxItem toolBoxItem = {};
    toolBoxItem.type = API_ElemType(static_cast<API_ElemTypeID>(item.toolType),
                                    static_cast<API_ElemVariationID>(item.toolVariation));
    return (ACAPI_Toolbox_SetToolBoxMode(&toolBoxItem) == NoError);
}

// Same as double-clicking the item in the Navigator
static bool GoToView(const SearchItem& item) {
    return (ACAPI_View_GoToView(item.guid.c_str()) == NoError);
}

static bool GoToLayout(const SearchItem& item) {
    API_Guid navGuid = APIGuidFromString(item.guid.c_str());
    API_NavigatorItem navItem = {};
    if (ACAPI_Navigator_GetNavigatorItem(&navGuid, &navItem) == NoError) {
        API_WindowInfo winInfo = navItem.db;
        winInfo.typeID = APIWind_LayoutID;
        if (ACAPI_Window_ChangeWindow(&winInfo) == NoError) return true;
    }
    return GoToView(item);
}

// Looked up by GUID: attribute indices aren't contiguous once combinations are deleted
static bool SetLayerCombo(const SearchItem& item) {
    API_Attribute attr = {};
    attr.header.typeID = API_LayerCombID;
    attr.header.guid = APIGuidFromString(item.guid.c_str());
    if (ACAPI_Attribute_Get(&attr) != NoError) return false;
    API_AttributeIndex layerCombIndex = attr.header.index;
    return (ACAPI_Navigator_ChangeCurrLayerComb(&layerCombIndex) == NoError);
}

#else

// Without Archicad (tests): "select" a tool by typing its chord
static bool SetTool(const SearchItem& item) {
    for (wchar_t ch : item.tag) {
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = static_cast<WORD>(towupper(ch));
        inputs[1] = inputs[0];
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));
    }
    return true;
}

static bool GoToView(const SearchItem&) { return true; }
static bool GoToLayout(const SearchItem&) { return true; }
static bool SetLayerCombo(const SearchItem&) { return true; }

#endif

bool CommandExecutor::s_refusedRun = false;

bool CommandExecutor::TakeRefusedRun() {
    const bool refused = s_refusedRun;
    s_refusedRun = false;
    return refused;
}

bool CommandExecutor::Execute(const SearchItem& item) {
    switch (item.kind) {
    case ItemKind::Tool:       return SetTool(item);
    case ItemKind::View:       return GoToView(item);
    case ItemKind::Sheet:      return GoToLayout(item);
    case ItemKind::LayerCombo: return SetLayerCombo(item);
    case ItemKind::Setting:
        SettingsDialog::Show(MainWindow());
        return true;
    case ItemKind::Command:
        s_refusedRun = !CommandSource::Run(item, MainWindow());
        return !s_refusedRun;
    case ItemKind::Help:
        return true;
    }
    return false;
}

} // namespace Baaz
