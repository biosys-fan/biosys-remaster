// BioSys 4 panels `paneloxy` (ocean oxygen store), `panelpod` (the escape pod's charging) and `panellock` (airlock seals): packet P5.
//   paneloxy:  Init exe:00429700, handler exe:00433fc0, setter exe:00431f00 (FEATURE_ACTIVE buttons)
//   panelpod:  Init exe:0042aac0, handler exe:00434230, setter exe:004322b0 (FEATURE_START_CHARGING_POD buttons 0x8f / 0x90)
//   panellock: Init exe:00428b80 (first group: the variant picks the seal), handler exe:00434b10 (empty), setter exe:004326e0 (FEATURE_OPEN)
#include "engine/bs4panels_b.h"

namespace biosys {
namespace {

using namespace bs4b;

// ---- paneloxy ---------------------------------------------------------------------------------------------------------

// exe:00429700
void initOxy(Bs4Page& p, int) {
    p.owner().setHighlight("hioxy", 3);
    p.reloadName = "";
    p.feature = "fbo4oxygen";
    const std::string f = p.feature;
    u32 active = 0;
    double d = 0;
    if (!read(p, f, 2, active, d, failGet("FEATURE_ACTIVE", f))) return;
    p.showItem(1, false); p.showItem(2, false);
    if (active == 0) {
        p.showItem(1, true);
        p.devActive = false;                                             // +0x160
    } else {
        p.showItem(2, true);
        p.devActive = true;
    }
    const auto pre = [&](const char* what) {
        return "BS4: Value of parameter " + std::string(what) + " for BiosFear feature '" + f + "' is outside its valid range.  (Value = ";
    };
    if (!readGauge(p, p.gauge[0], f, 0x22, 0, 100, failGet("FEATURE_OXYGEN", f), pre("FEATURE_OXYGEN"), ").")) return;
    if (!readGauge(p, p.gauge[1], f, 0x78, 0, 100, failGet("FEATURE_AMOUNT_OXYGEN_STORED", f), pre("FEATURE_AMOUNT_OXYGEN_STORED"), ").")) return;
    if (!setupBar(p, p.gauge[0], Bs4Rect{0x23, 0x48, 0x41, 0x52}, 0x22, 0, 100, "oceanbar", noImage("oceanbar", f))) return;
    if (!setupBar(p, p.gauge[1], Bs4Rect{0x23, 0x70, 0x41, 0x7a}, 0x78, 0, 100, "oxybar", noImage("oxybar", f))) return;
    p.loopSound = 3;                                                     // +0x678
    if (p.devActive) p.owner().loopStart(3);
    else p.owner().loopStop(3);
    p.handler = [&p](int phase) {                                        // exe:00433fc0
        if (phase != 3) {
            bs4w::barGauge(p, p.gauge[0], phase);
            bs4w::barGauge(p, p.gauge[1], phase);
        }
        if (phase == 2) {
            // ORIGINAL BUG: the handler stores 0 into +0x4ac and tests it for 1 right after the get, so the branch that compares the
            // FEATURE_ON mirror (+0x15c) is dead; only the FEATURE_ACTIVE mirror (+0x160) is ever compared.
            const int mode = 0;                                          // +0x4ac
            u32 now = 0;
            double dv = 0;
            if (p.owner().host().onGetFeatureParameter(p.feature, 2, now, dv)) {
                if (now != 0) now = 1;
                if (mode == 1) {
                    if (static_cast<u32>(p.devOn) != now) { p.reload = true; return; }
                } else if (static_cast<u32>(p.devActive) != now) p.reload = true;
            }
        }
    };
}

// ---- panelpod ---------------------------------------------------------------------------------------------------------

// exe:0042aac0
void initPod(Bs4Page& p, int) {
    p.reloadName = "";
    p.feature = "fPod";
    const std::string f = p.feature;
    u32 charging = 0;
    double d = 0;
    if (!read(p, f, 0x86, charging, d, failGet("FEATURE_START_CHARGING_POD", f))) return;
    p.showItem(1, false); p.showItem(2, false);
    if (charging == 0) {
        p.showItem(1, true);
        p.devActive = false;
    } else {
        p.showItem(2, true);
        p.devActive = true;
    }
    Bs4GaugeSlot& g = p.gauge[0];                                        // the working slot +0x198 itself
    if (!readGauge(p, g, f, 0x8d, 0, 100, failGet("FEATURE_POWER_AVAILABLE", f),
                   "BS4: Value of parameter FEATURE_POWER_AVAILABLE for BiosFear feature '" + f + "' is outside its valid range.  (Value = ", ").")) return;
    if (!setupBar(p, g, Bs4Rect{0x23, 0x48, 0x41, 0x52}, 0x8d, 0, 100, "powerbar", noImage("powerbar", f))) return;
    p.handler = [&p](int phase) {                                        // exe:00434230
        bs4w::barGauge(p, p.gauge[0], phase);
        if (phase == 2) {
            u32 v = 0;
            double dv = 0;
            if (p.owner().host().onGetFeatureParameter(p.feature, 0x86, v, dv)) {
                if (v != 0) v = 1;
                if (static_cast<u32>(p.devActive) != v) p.reload = true;
            }
        }
    };
}

// exe:004322b0
void setPod(Bs4Page& p, u32 now, u32 old) {
    if ((now & 4) && !(old & 4)) {
        if (!p.owner().host().onSetFeatureParameter(p.feature, 0x8f, 1, 0, 0)) {
            p.owner().log(failSet("FEATURE_START_CHARGING_POD", p.feature));
            return;
        }
        p.devActive = true;
    }
    if ((now & 2) && !(old & 2)) {
        if (!p.owner().host().onSetFeatureParameter(p.feature, 0x90, 1, 0, 0)) {
            p.owner().log(failSet("FEATURE_START_CHARGING_POD", p.feature));
            return;
        }
        p.devActive = false;
    }
}

// ---- panellock --------------------------------------------------------------------------------------------------------

// exe:00428b80
void initLock(Bs4Page& p, int variant) {
    const char* feature;
    const char* reload;
    const char* highlight;
    switch (variant) {
    case 3:   feature = "fspseal"; reload = "18_locks1"; highlight = "hiseals1"; break;
    case 4:   feature = "fsoseal"; reload = "18_locks2"; highlight = "hiseals2"; break;
    case 6:   feature = "fopseal"; reload = "18_locko1"; highlight = "hisealo1"; break;
    case 9:   feature = "fdpseal"; reload = "18_locke1"; highlight = "hiseale1"; break;
    case 10:  feature = "ft3seal"; reload = "18_locke2"; highlight = "hiseale2"; break;
    case 0xc: feature = "frpseal"; reload = "18_lockr1"; highlight = "hisealr1"; break;
    default: return;
    }
    p.reloadName = reload;
    p.owner().setHighlight(highlight, 0x12);
    p.feature = feature;
    u32 open = 0;
    double d = 0;
    if (!read(p, p.feature, 10, open, d, failGet("FEATURE_OPEN", p.feature))) return;
    if (open == 0) {
        p.showItem(1, false);
        p.showItem(2, true);
        p.devOpen = false;                                               // +0x164
    } else {
        p.showItem(1, true);
        p.showItem(2, false);
        p.devOpen = true;
    }
    p.handler = [](int) {};                                              // exe:00434b10: `ret`
}

// exe:004326e0
void setLock(Bs4Page& p, u32 now, u32 old) {
    if ((now & 4) && !(old & 4)) {
        if (!p.owner().host().onSetFeatureParameter(p.feature, 10, 0, 0, 0)) {
            p.owner().log(failSet("FEATURE_OPEN", p.feature));
            return;
        }
        p.devOpen = false;
    }
    if ((now & 2) && !(old & 2)) {
        if (!p.owner().host().onSetFeatureParameter(p.feature, 10, 1, 0, 0)) {
            p.owner().log(failSet("FEATURE_OPEN", p.feature));
            return;
        }
        p.devOpen = true;
    }
}

Bs4PanelRegistrar regOxy("paneloxy", Bs4PanelDef{false, initOxy, setActiveFromMask, nullptr});
Bs4PanelRegistrar regPod("panelpod", Bs4PanelDef{false, initPod, setPod, nullptr});
Bs4PanelRegistrar regLock("panellock", Bs4PanelDef{true, initLock, setLock, nullptr});

} // namespace
} // namespace biosys
