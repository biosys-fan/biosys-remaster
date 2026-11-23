// BioSys 4 panels `panelcond` (water condenser) and `panelsub` (sub-surface tank): packet P5. Both are panels of the first group of
// exe:0041d0a0 (the variant 3..14 picks the feature) with the same three bar gauges (water level, temperature -10..40, purity); the
// condenser also shows the alarm icon of the purity bar.
//   panelcond: Init exe:00427630, handler exe:00433b00     panelsub: Init exe:00428080, handler exe:00433850
// Neither has a setter (no state buttons): the controls are verb-0x17 / 0x1a hotspots only.
#include "engine/bs4panels_b.h"

namespace biosys {
namespace {

using namespace bs4b;

// the three bars (the part of both inits after the malfunction test); false: a read or an ornament failed (the handler stays unset)
bool initThree(Bs4Page& p) {
    const std::string f = p.feature;
    const auto pre = [&](const char* what) {
        return "BS4: Value of parameter " + std::string(what) + " for BiosFear feature '" + f + "' is outside its valid range.  (Value = ";
    };
    const char* post = ").";
    if (!readGauge(p, p.gauge[0], f, 0x2b, 0, 100, failGet("FEATURE_WATER_LEVEL", f), pre("FEATURE_WATER_LEVEL"), post)) return false;
    if (!readGauge(p, p.gauge[1], f, 0x28, -10, 40, failGet("FEATURE_WATER_TEMPERATURE", f), pre("FEATURE_WATER_TEMPERATURE"), post)) return false;
    if (!readGauge(p, p.gauge[2], f, 0x2c, 0, 100, failGet("FEATURE_WATER_PURITY", f), pre("FEATURE_WATER_PURITY"), post)) return false;
    if (!setupBar(p, p.gauge[0], Bs4Rect{0x23, 0x48, 0x41, 0x52}, 0x2b, 0, 100, "waterbar", noImage("waterbar", f))) return false;
    if (!setupBar(p, p.gauge[1], Bs4Rect{0x23, 0x70, 0x41, 0x7a}, 0x28, -10, 40, "tempbar", noImage("tempbar", f))) return false;
    if (!setupBar(p, p.gauge[2], Bs4Rect{0x23, 0x98, 0x41, 0xa2}, 0x2c, 0, 100, "puritybar", noImage("puritybar", f))) return false;
    return true;
}

// exe:00427630
void initCond(Bs4Page& p, int variant) {
    const char* feature;
    const char* reload;
    const char* highlight;
    switch (variant) {
    case 3:   feature = "fbs4cond"; reload = "13_conds1"; highlight = "hiconds1"; break;
    case 6:   feature = "fbo3cond"; reload = "13_condo1"; highlight = "hicondo1"; break;
    case 9:   feature = "fbd3cond"; reload = "13_conde1"; highlight = "hiconde1"; break;
    case 0xc: feature = "fbr3cond"; reload = "13_condr1"; highlight = "hicondr1"; break;
    default: return;
    }
    p.reloadName = reload;
    p.owner().setHighlight(highlight, 0xd);
    p.feature = feature;
    if (malfunction(p, p.feature)) return;
    if (!initThree(p)) return;
    u32 alarms = 0;
    double d = 0;
    if (!read(p, p.feature, 0x32, alarms, d, "BS4: Failed to get the value of parameter FEATURE_ALARMS for feature '" + p.feature + "'.")) return;
    p.alarmBits = alarms;                                                // +0x174
    p.handler = [&p](int phase) {                                        // exe:00433b00
        threeGauges(p, phase);
        if (phase != 3) {
            const int pos[2] = {p.gauge[2].bar.left, p.gauge[2].bar.top};   // +0x29c: the purity bar's rect
            bs4w::alarmBitIcon(p, phase, pos, p.owner().colours().white, 0, 0x8000);
        }
    };
}

// exe:00428080
void initSub(Bs4Page& p, int variant) {
    const char* feature;
    const char* reload;
    const char* highlight;
    switch (variant) {
    case 3:   feature = "fssub1";  reload = "4_subs1"; highlight = "hisubs1"; break;
    case 4:   feature = "fssub2";  reload = "4_subs2"; highlight = "hisubs2"; break;
    case 5:   feature = "fssub3";  reload = "4_subs3"; highlight = "hisubs3"; break;
    case 9:   feature = "fdbsub1"; reload = "4_sube1"; highlight = "hisube1"; break;
    case 10:  feature = "fdbsub2"; reload = "4_sube2"; highlight = "hisube2"; break;
    case 0xb: feature = "fdbsub3"; reload = "4_sube3"; highlight = "hisube3"; break;
    case 0xc: feature = "frfsub1"; reload = "4_subr1"; highlight = "hisubr1"; break;
    case 0xd: feature = "frfsub2"; reload = "4_subr2"; highlight = "hisubr2"; break;
    case 0xe: feature = "frfsub3"; reload = "4_subr3"; highlight = "hisubr3"; break;
    default: return;
    }
    p.reloadName = reload;
    p.owner().setHighlight(highlight, 4);
    p.feature = feature;
    if (malfunction(p, p.feature)) return;
    if (!initThree(p)) return;
    p.handler = [&p](int phase) { threeGauges(p, phase); };              // exe:00433850
}

Bs4PanelRegistrar regCond("panelcond", Bs4PanelDef{true, initCond, nullptr, nullptr});
Bs4PanelRegistrar regSub("panelsub", Bs4PanelDef{true, initSub, nullptr, nullptr});

} // namespace
} // namespace biosys
