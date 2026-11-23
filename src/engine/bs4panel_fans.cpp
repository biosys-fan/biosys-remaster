// BioSys 4 air panels: `panelsfan` (page 55, soil fans: time-limit slider + air-flow bar) and `panelvent` (page 60, vents: air-flow bar
// only). Inits exe:00422590 / exe:004246c0, handlers exe:00433150 / exe:00433170. Both read FEATURE_AIR_FLOW (0x50, 0..10) into the first
// bar slot and take the bar image "airbar".
#include "engine/bs4panels_a.h"

namespace biosys {
namespace {

using namespace bs4a;

const Variant kSfan[] = {
    {3, "fs1fan", "2_soils1", "hisoils1"},     {4, "fs4fan", "2_soils2", "hisoils2"},     {5, "fs7fan", "2_soils3", "hisoils3"},
    {9, "fdb6fan", "2_soile1", "hisoile1"},    {10, "fdb3fan", "2_soile2", "hisoile2"},   {11, "fdbfan3", "2_soile3", "hisoile3"},
    {12, "fr8fan", "2_soilr1", "hisoilr1"},    {13, "fr3fan", "2_soilr2", "hisoilr2"},    {14, "fr10fan", "2_soilr3", "hisoilr3"},
};
const Variant kVent[] = {
    {3, "fs5vent", "1_vents1", "hivents1"},    {4, "fs8vent", "1_vents2", "hivents2"},    {6, "fovent", "1_vento1", "hivento1"},
    {9, "fdbvent1", "1_vente1", "hivente1"},   {10, "fdbvent2", "1_vente2", "hivente2"},  {12, "fr7vent", "1_ventr1", "hiventr1"},
    {13, "fr6vent", "1_ventr2", "hiventr2"},
};

// the air-flow bar both Inits set up: false = the Init ends (read failed, or no "airbar" ornament)
bool setupAirBar(Bs4Page& p) {
    double flow = 0;
    if (!getClamped(p, 0x50, "FEATURE_AIR_FLOW", 10.0, flow)) return false;
    Bs4GaugeSlot& g = p.gauge[0];
    setupGauge(g, flow, 0.0, 10.0, 0x50, 0x23, 0x48, 0x41, 0x52);
    if (!findBar(p, g, "airbar")) return false;
    g.percent = bs4w::gaugePercent(g.last, g.min, g.max);
    return true;
}

// exe:00422590
void initSfan(Bs4Page& p, int variant) {
    const Variant* v = findVariant(kSfan, sizeof kSfan / sizeof *kSfan, variant);
    if (!v) return;
    begin(p, *v, 2);
    if (broken(p, {1, 2, 0x1f})) return;
    u32 i = 0;
    double d = 0;
    if (!get(p, 5, "FEATURE_TIME_LIMIT", i, d)) return;
    // exe:00422590: above 86400 the value is logged and clamped (disassembly 0x4229ef; see logTooLarge in bs4panels_a.cpp)
    if (i > 0x15180u) {
        p.owner().log("BS4: Value of parameter FEATURE_TIME_LIMIT for BiosFear feature '" + p.feature +
                      "' is greater than the maximum.  (Value = " + std::to_string(i) + ", Maximum = 86400 (= 24hours)).");
        i = 0x15180u;
    }
    p.timeSlider.hours = i / 0xe10;
    p.timeSlider.minutes = (i % 0xe10) / 0x3c;
    if (!get(p, 2, "FEATURE_ACTIVE", i, d)) return;
    p.showItem(1, false);
    p.showItem(2, false);
    p.showItem(0x1f, false);
    if (i == 0) {
        p.showItem(p.timeSlider.hours == 0 && p.timeSlider.minutes == 0 ? 0x1f : 1, true);
        p.devActive = false;
    } else {
        p.showItem(2, true);
        p.devActive = true;
    }
    p.sliderY = 0x7e;
    p.timeSlider.hoursText = Bs4Rect{0x51, 0x9c, 0x66, 0xa6};
    p.timeSlider.minutesText = Bs4Rect{0x78, 0x9c, 0x8d, 0xa6};
    if (!setupAirBar(p)) return;
    setLoop(p, 2, p.devActive);
    p.handler = [&p](int phase) {                            // exe:00433150
        bs4w::barGauge(p, p.gauge[0], phase);
        bs4w::timeLimitSlider(p, phase);
    };
}

// exe:004246c0
void initVent(Bs4Page& p, int variant) {
    const Variant* v = findVariant(kVent, sizeof kVent / sizeof *kVent, variant);
    if (!v) return;
    begin(p, *v, 1);
    if (broken(p, {})) return;
    if (!setupAirBar(p)) return;
    setLoop(p, 6, p.gauge[0].last > 0.0);
    p.handler = [&p](int phase) {                            // exe:00433170
        bs4w::barGauge(p, p.gauge[0], phase);
        if (phase == 2 && p.loopSound != -1) {
            if (p.gauge[0].last <= 0.0) p.owner().loopStop(p.loopSound);
            else p.owner().loopStart(p.loopSound);
        }
    };
}

Bs4PanelRegistrar regSfan("panelsfan", Bs4PanelDef{true, initSfan, setActiveFromMask, nullptr});
Bs4PanelRegistrar regVent("panelvent", Bs4PanelDef{true, initVent, nullptr, nullptr});

} // namespace
} // namespace biosys
