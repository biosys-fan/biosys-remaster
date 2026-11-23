// BioSys 4 biome-score hint boxes `hsav`, `hoc`, `hevo`, `hrf` (pages 108..111, 241x101): packet P5. One init (exe:004315c0) for all four,
// selected by the page's own name; the single bar gauge shows the biome health score (world parameter 0x834 .. 0x837, NULL feature).
// Handler exe:004369b0 = the bar gauge. No setter, no variant.
#include "engine/bs4panels_b.h"

namespace biosys {
namespace {

using namespace bs4b;

// exe:004315c0
void initHint(Bs4Page& p, int) {
    const char* bar;
    const char* highlight;
    int id;
    // The names pick the parameter: hsav 0x835, hoc 0x836, hevo 0x837, hrf 0x834 (the spec lists them in the order 0x834..0x837).
    if (bs4Iequals(p.name(), "hsav")) { bar = "savbar"; highlight = "healthsav"; id = 0x835; }
    else if (bs4Iequals(p.name(), "hoc")) { bar = "ocbar"; highlight = "healthoc"; id = 0x836; }
    else if (bs4Iequals(p.name(), "hevo")) { bar = "evobar"; highlight = "healthevo"; id = 0x837; }
    else if (bs4Iequals(p.name(), "hrf")) { bar = "rfbar"; highlight = "healthrf"; id = 0x834; }
    else {
        p.owner().log("BS4: Unrecognised name of biome-score panel page: '" + p.name() + "'.");
        return;
    }
    p.owner().setHighlight(highlight, 0);
    p.reloadName = "";
    Bs4GaugeSlot& g = p.gauge[0];                                        // the working slot +0x198 itself
    g.paramId = id;                                                      // +0x1d8
    if (!readGauge(p, g, "", id, 0, 100, "BS4: Failed to get the value of feature parameter " + std::to_string(id),
                   "BS4: Value of feature parameter " + std::to_string(id) + " is outside its valid range.  (Value = ", ").")) return;
    if (!setupBar(p, g, Bs4Rect{0xb4, 0x4a, 0xd2, 0x54}, id, 0, 100, bar,
                  "BS4: Can't find image '" + std::string(bar) + "' on panel page '" + p.name() + "'")) return;
    p.handler = [&p](int phase) { bs4w::barGauge(p, p.gauge[0], phase); };   // exe:004369b0
}

Bs4PanelRegistrar regSav("hsav", Bs4PanelDef{false, initHint, nullptr, nullptr});
Bs4PanelRegistrar regOc("hoc", Bs4PanelDef{false, initHint, nullptr, nullptr});
Bs4PanelRegistrar regEvo("hevo", Bs4PanelDef{false, initHint, nullptr, nullptr});
Bs4PanelRegistrar regRf("hrf", Bs4PanelDef{false, initHint, nullptr, nullptr});

} // namespace
} // namespace biosys
