// BioSys 4 panel `panelah` (air hatch): the worked example of how a panel is added (see the recipe at the top of bs4page.h).
// Init exe:00424ca0 (the smallest packet-P4 init), handler exe:00434220 (an empty function: `ret 4`), no setter (panelah is not in the
// state-button chain of exe:0041d0a0). The variant (the item 3 / 6 / 9 / 12 of the panel's visible mask, which the opening hotspot
// selects) picks the hatch feature, the highlight ornament of the main page and the name of the hotspot that re-opens the panel.
#include "engine/bs4.h"

namespace biosys {
namespace {

// exe:00424ca0
void initAh(Bs4Page& p, int variant) {
    const char* feature;
    const char* reload;
    const char* highlight;
    switch (variant) {
    case 3:  feature = "fs6htch";  reload = "5_ahs2"; highlight = "hiahs1"; break;
    case 6:  feature = "fo1htch";  reload = "5_aho1"; highlight = "hiaho1"; break;
    case 9:  feature = "fdb1htch"; reload = "5_ahe2"; highlight = "hiahe1"; break;
    case 12: feature = "fr10htch"; reload = "5_ahr2"; highlight = "hiahr1"; break;
    default: return;
    }
    p.reloadName = reload;                                   // +0x188
    p.owner().setHighlight(highlight, 5);                    // +0x1bec, +0x1bf8 = 5
    p.feature = feature;                                     // +0xa4
    u32 open = 0;
    double d = 0;
    if (!p.getParam(feature, 10, open, d)) return;           // FEATURE_OPEN; the original returns here without setting the handler
    p.showItem(0x11, open != 0);                             // "open" picture
    p.showItem(0x12, open == 0);                             // "closed" picture
    p.handler = [](int) {};                                  // exe:00434220: does nothing
}

Bs4PanelRegistrar registrar("panelah", Bs4PanelDef{true, initAh, nullptr, nullptr});

} // namespace
} // namespace biosys
