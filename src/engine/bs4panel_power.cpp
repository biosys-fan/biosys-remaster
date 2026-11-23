// BioSys 4 panels `panelsol` (solar generator), `panelwind` (windmill) and `panelres` (world power reserve): packet P5. One-device panels
// of the second group of exe:0041d0a0 with plain bar gauges and a looping power sound, no controls.
//   panelsol:  Init exe:0042ae50, handler exe:00434410     panelwind: Init exe:0042b180, handler exe:00434420
//   panelres:  Init exe:0042b4b0 (NULL feature: the world parameters 0x3f2 / 0x3f4), handler exe:00434430
#include "engine/bs4panels_b.h"

namespace biosys {
namespace {

using namespace bs4b;

// exe:0042ae50 / exe:0042b180 share everything but the names
void initGenerator(Bs4Page& p, const char* highlight, int bit, const char* feature, const char* bar) {
    p.owner().setHighlight(highlight, bit);
    p.reloadName = "";
    p.feature = feature;
    const std::string f = p.feature;
    Bs4GaugeSlot& g = p.gauge[0];                                        // the working slot +0x198 itself
    if (!readGauge(p, g, f, 0x8c, 0, 100, failGet("FEATURE_POWER_GENERATION", f),
                   "BS4: Value of parameter FEATURE_POWER_GENERATION for BiosFear feature '" + f + "' is outside its valid range.  (Value = ", ").")) return;
    if (!setupBar(p, g, Bs4Rect{0x23, 0x48, 0x41, 0x52}, 0x8c, 0, 100, bar, noImage(bar, f))) return;
    p.loopSound = 4;                                                     // +0x678
    p.owner().loopStart(4);                                              // exe:0040c4b0(4)
    p.handler = [&p](int phase) { bs4w::barGauge(p, p.gauge[0], phase); };   // exe:00434410 / exe:00434420
}

void initSol(Bs4Page& p, int) { initGenerator(p, "hisol", 4, "fsolargen", "solbar"); }
void initWind(Bs4Page& p, int) { initGenerator(p, "hiwind", 3, "fwindmill", "windbar"); }

// exe:0042b4b0
void initRes(Bs4Page& p, int) {
    p.owner().setHighlight("hires", 1);
    p.reloadName = "";
    p.feature = "";                                                      // +0xa4 = 0: the NULL feature
    const std::string none;
    if (!readGauge(p, p.gauge[0], none, 0x3f2, 0, 200, "BS4: Failed to get the value of parameter WORLD_POWER_GENERATION",
                   "BS4: Value of parameter WORLD_POWER_GENERATION is outside its valid range.  (Value = ", ").")) return;
    if (!readGauge(p, p.gauge[1], none, 0x3f4, 0, 200, "BS4: Failed to get the value of parameter WORLD_POWER_USAGE",
                   "BS4: Value of parameter WORLD_POWER_USAGE is outside its valid range.  (Value = ", ").")) return;
    if (!setupBar(p, p.gauge[0], Bs4Rect{0x1c, 0x48, 0x43, 0x52}, 0x3f2, 0, 200, "genbar", "BS4: Can't find image 'genbar' on panel page 'panelres'.")) return;
    if (!setupBar(p, p.gauge[1], Bs4Rect{0x23, 0x7a, 0x41, 0x84}, 0x3f4, 0, 200, "usedbar", "BS4: Can't find image 'usedbar' on panel page 'panelres'.")) return;
    p.loopSound = 4;
    p.owner().loopStart(4);
    p.handler = [&p](int phase) {                                        // exe:00434430
        if (phase != 3) {
            bs4w::barGauge(p, p.gauge[0], phase);
            bs4w::barGauge(p, p.gauge[1], phase);
        }
    };
}

Bs4PanelRegistrar regSol("panelsol", Bs4PanelDef{false, initSol, nullptr, nullptr});
Bs4PanelRegistrar regWind("panelwind", Bs4PanelDef{false, initWind, nullptr, nullptr});
Bs4PanelRegistrar regRes("panelres", Bs4PanelDef{false, initRes, nullptr, nullptr});

} // namespace
} // namespace biosys
