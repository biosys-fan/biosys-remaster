// BioSys 4 panels `panelalgs` (algae tank) and `panelbubl` (bubbler): packet P5. Both are one-device panels of the second group of
// exe:0041d0a0 (no variant): FEATURE_ON buttons (setter exe:004323c0), a 24 h schedule slider (exe:00437470) and the active indicator;
// the bubbler adds the gas-source buttons (setter exe:00432030) and the oxygen-store gauge.
//   panelalgs: Init exe:00429d50, handler exe:004343e0, setter exe:004323c0
//   panelbubl: Init exe:00428e30, handler exe:004337d0, setter exe:00432030 (+ exe:004323c0)
#include "engine/bs4panels_b.h"

namespace biosys {
namespace {

using namespace bs4b;

// the common tail of both inits: loop sound `loop`, started when the device is active (exe:0040c4b0 / 0040c660)
void startLoopSound(Bs4Page& p, int loop) {
    p.loopSound = loop;                                                  // +0x678
    if (p.devActive) p.owner().loopStart(loop);
    else p.owner().loopStop(loop);
}

// ---- panelalgs --------------------------------------------------------------------------------------------------------

// exe:00429d50
void initAlgs(Bs4Page& p, int) {
    p.owner().setHighlight("hialg", 7);                                  // +0x1bec / +0x1bf8
    p.reloadName = "";                                                   // +0x188 (the exe's empty string)
    p.feature = "fbo3algae";                                             // +0xa4
    if (malfunction(p, p.feature)) {                                     // the broken picture: no controls
        p.showItem(1, false); p.showItem(2, false); p.showItem(0xf, false);
        return;
    }
    u32 on = 0;
    double d = 0;
    if (!read(p, p.feature, 1, on, d, failGet("FEATURE_ON", p.feature))) return;
    p.showItem(1, false); p.showItem(2, false); p.showItem(0xf, false);
    if (on == 0) {
        p.showItem(2, true);
        p.showItem(0xf, true);
        p.devOn = false;                                                 // +0x15c
    } else {
        p.showItem(1, true);
        p.devOn = true;
    }
    if (!readSchedule(p, p.feature)) return;
    p.sliderY = 0x51;                                                    // +0x124
    p.schedule.onText = Bs4Rect{0x4d, 0x73, 0x7a, 0x7d};                 // +0x4e8
    p.schedule.offText = Bs4Rect{0x4d, 0x85, 0x7a, 0x8f};                // +0x4f8
    bs4w::activeIndicator(p, 1);                                         // exe:00439340(1): FEATURE_ACTIVE, item 0x19
    startLoopSound(p, 3);
    p.handler = [&p](int phase) {                                        // exe:004343e0
        bs4w::scheduleSlider(p, phase);
        if (phase == 2) bs4w::activeIndicator(p, 0);
    };
}

// ---- panelbubl --------------------------------------------------------------------------------------------------------

int gGasSource = 0;                                                      // +0x4d0: the gas source (0x5b / 0x5c) the page shows

// exe:00428e30
void initBubl(Bs4Page& p, int) {
    p.owner().setHighlight("hibub", 6);
    p.reloadName = "";
    p.feature = "fw3bubl";
    const std::string& f = p.feature;
    if (malfunction(p, f)) {
        p.showItem(1, false); p.showItem(2, false); p.showItem(0xf, false); p.showItem(0x11, false); p.showItem(0x12, false);
        return;
    }
    u32 on = 0;
    double d = 0;
    if (!read(p, f, 1, on, d, failGet("FEATURE_ON", f))) return;
    p.showItem(1, false); p.showItem(2, false); p.showItem(0xf, false);
    if (on == 0) {
        p.showItem(2, true);
        p.showItem(0xf, true);
        p.devOn = false;
    } else {
        p.showItem(1, true);
        p.devOn = true;
    }
    if (!readSchedule(p, f)) return;
    p.sliderY = 0xd1;
    p.schedule.onText = Bs4Rect{0x53, 0xef, 0x80, 0xf9};
    p.schedule.offText = Bs4Rect{0x53, 0x101, 0x80, 0x10b};
    u32 gas = 0;
    if (!read(p, f, 0x5a, gas, d, failGet("FEATURE_GAS_SOURCE", f))) return;
    if (gas == 0x5b) {
        p.showItem(0x11, true); p.showItem(0x12, false);
        gGasSource = 0x5b;
    } else {
        if (gas != 0x5c) {
            p.owner().log("BS4: Invalid value (" + std::to_string(gas) + ") of parameter FEATURE_GAS_SOURCE for BiosFear feature '" + f + "'.");
            return;
        }
        p.showItem(0x11, false); p.showItem(0x12, true);
        gGasSource = 0x5c;
    }
    Bs4GaugeSlot& g = p.gauge[0];                                        // the working slot +0x198 itself
    if (!readGauge(p, g, f, 0x78, 0, 100, failGet("FEATURE_AMOUNT_OXYGEN_STORED", f),
                   "BS4: Value of parameter FEATURE_AMOUNT_OXYGEN_STORED for BiosFear feature '" + f + "' is outside its valid range.  (Value = ", ").")) return;
    if (!setupBar(p, g, Bs4Rect{0x23, 0x48, 0x41, 0x52}, 0x78, 0, 100, "oxybar", noImage("oxybar", f))) return;
    bs4w::activeIndicator(p, 1);
    startLoopSound(p, 5);
    p.handler = [&p](int phase) {                                        // exe:004337d0
        bs4w::barGauge(p, p.gauge[0], phase);
        bs4w::scheduleSlider(p, phase);
        if (phase == 2) {
            u32 gasNow = 0;
            double dv = 0;
            if (p.owner().host().onGetFeatureParameter(p.feature, 0x5a, gasNow, dv) && (gasNow == 0x5b || gasNow == 0x5c) &&
                gGasSource != static_cast<int>(gasNow))
                p.reload = true;
            bs4w::activeIndicator(p, 0);
        }
    };
}

// exe:00432030: the highlight again, the FEATURE_ON buttons, then the gas-source buttons (items 0x11 = bit 17, 0x12 = bit 18)
void setBubl(Bs4Page& p, u32 now, u32 old) {
    p.owner().setHighlight("hibub", 6);
    setOnFromMask(p, now, old);
    for (int k = 0; k < 2; k++) {
        const u32 bit = k == 0 ? 0x20000u : 0x40000u;
        const int value = k == 0 ? 0x5b : 0x5c;
        if ((now & bit) && !(old & bit)) {
            if (!p.owner().host().onSetFeatureParameter(p.feature, 0x5a, value, 0, 0)) {
                p.owner().log(failSet("FEATURE_GAS_SOURCE", p.feature));
                return;
            }
            gGasSource = value;
        }
    }
}

Bs4PanelRegistrar regAlgs("panelalgs", Bs4PanelDef{false, initAlgs, setOnFromMask, nullptr});
Bs4PanelRegistrar regBubl("panelbubl", Bs4PanelDef{false, initBubl, setBubl, nullptr});

} // namespace
} // namespace biosys
