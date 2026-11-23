// BioSys 4 panels with bar gauges and an on/off ("active") state: `panelsump` (page 64, drain sumps: flow and silt bars, silt tier icon,
// alarm icon), `panelosm` (65, reverse-osmosis units) and `paneluv` (66, UV sterilisers) (level + purity bars, alarm icon), `paneltide`
// (68) and `panelwave` (69) (single-variant machines, Inits without a variant) and the dead `panelsprk` (no page in BIOSYS4.DAT).
// Inits exe:00424ed0, exe:00425950, exe:00426220, exe:0042a280, exe:0042a7d0, exe:00426b40; handlers exe:00433dc0, exe:004331d0 (osm),
// exe:00433440 (uv, the same code), exe:004342a0, exe:00433740, exe:004336b0. Setters exe:00431f00 (osm, uv) and exe:00432180.
#include "engine/bs4panels_a.h"

namespace biosys {
namespace {

using namespace bs4a;

const Variant kSump[] = {
    {3, "fbsdrain1", "0_sumps1", "hisumps1"},  {4, "fbsdrain2", "0_sumps2", "hisumps2"},  {5, "fbsdrain3", "0_sumps3", "hisumps3"},
    {9, "fbddrain1", "0_sumpe1", "hisumpe1"},  {10, "fbddrain2", "0_sumpe2", "hisumpe2"}, {11, "fbddrain3", "0_sumpe3", "hisumpe3"},
    {12, "fbrdrain1", "0_sumpr1", "hisumpr1"}, {13, "fbrdrain2", "0_sumpr2", "hisumpr2"}, {14, "fbrdrain3", "0_sumpr3", "hisumpr3"},
};
const Variant kOsm[] = {
    {3, "fbs4revos", "2_oss1", "hiosms1"}, {6, "fbo4revos", "2_oso1", "hiosmo1"}, {9, "fbd3revos", "2_ose1", "hiosme1"},
    {12, "fbr4revos", "2_osr1", "hiosmr1"},
};
const Variant kUv[] = {
    {3, "fbs4UV", "3_uvs1", "hiuvs1"}, {6, "fbo3UV", "3_uvo1", "hiuvo1"}, {9, "fbd2UV", "3_uve1", "hiuve1"},
    {12, "fbr3UV", "3_uvr1", "hiuvr1"}, {24, "fbp1UV", "3_uvho1", "hiuvh1"},
};
const Variant kSprk[] = {
    {3, "fbs5sprink", "8_sprs1", "hisprs1"}, {6, "fbo2sprink", "8_spro1", "hispro1"}, {9, "fbd2sprink", "8_spre1", "hispre1"},
    {12, "fbr3sprink", "8_sprr1", "hisprr1"}, {24, "fbp1sprink", "8_sprho1", "hisprh1"},
};

// the alarm icon of the sump / osm / uv panels sits at the top left of the second bar
void alarmIcon(Bs4Page& p, int phase, u32 maskA, u32 maskB) {
    const int pos[2] = {p.gauge[1].bar.left, p.gauge[1].bar.top};
    bs4w::alarmBitIcon(p, phase, pos, p.owner().colours().white, maskA, maskB);
}

bool readAlarms(Bs4Page& p) {
    u32 bits = 0;
    double d = 0;
    if (!p.owner().host().onGetFeatureParameter(p.feature, 0x32, bits, d)) {
        p.owner().log("BS4: Failed to get the value of parameter FEATURE_ALARMS for feature '" + p.feature + "'.");
        return false;
    }
    p.alarmBits = bits;                                      // +0x174
    return true;
}

// exe:00424ed0
void initSump(Bs4Page& p, int variant) {
    const Variant* v = findVariant(kSump, sizeof kSump / sizeof *kSump, variant);
    if (!v) return;
    begin(p, *v, 0);
    if (broken(p, {})) return;
    double flow = 0, silt = 0;
    if (!getClamped(p, 0x51, "FEATURE_WATER_FLOW", 10.0, flow)) return;
    if (!getClamped(p, 0x52, "FEATURE_SILT_LEVEL", 100.0, silt)) return;
    Bs4GaugeSlot& g0 = p.gauge[0];
    Bs4GaugeSlot& g1 = p.gauge[1];
    setupGauge(g0, flow, 0.0, 10.0, 0x51, 0x23, 0x48, 0x41, 0x52);
    if (!findBar(p, g0, "waterbar")) return;
    g0.percent = bs4w::gaugePercent(g0.last, g0.min, g0.max);
    setupGauge(g1, silt, 0.0, 100.0, 0x52, 0x23, 0x70, 0x41, 0x7a);
    if (!findBar(p, g1, "siltbar")) return;
    g1.percent = bs4w::gaugePercent(g1.last, g1.min, g1.max);
    // the tier icon of the silt level (exe:00438f70 chooses the same way at every poll)
    const int tier = static_cast<int>(g1.last);
    const char* name = tier < 10 ? "0to9" : tier < 0x15 ? "10to20" : tier < 0x29 ? "21to40" : tier < 0x3d ? "41to60" : tier >= 0x51 ? "81plus" : "61to80";
    bool found = false;
    for (const Ornament& o : p.rec.ornaments) {
        if (o.type == 1 && iequals(o.name, name)) {
            p.tierImage = static_cast<int>(o.image);         // +0x4b0
            p.tierPos[0] = o.x;
            p.tierPos[1] = o.y;
            found = true;
            break;
        }
    }
    if (!found) {
        p.owner().log(std::string("BS4: Can't find image '") + name + "' on panel page that represents BiosFear feature '" + p.feature + "'.");
        p.tierImage = -1;
        return;
    }
    p.tierName = name;                                       // +0x4c4
    if (!readAlarms(p)) return;
    p.handler = [&p](int phase) {                            // exe:00433dc0
        if (phase == 3) return;
        bs4w::barGauge(p, p.gauge[0], phase);
        bs4w::barGauge(p, p.gauge[1], phase);
        bs4w::tierIcon(p, phase, static_cast<int>(p.gauge[1].last));
        alarmIcon(p, phase, 0x1000000, 0);
    };
}

// exe:00425950 (osm) and exe:00426220 (uv): the same except the variants, the highlight bit, the first bar image and the loop sound
void initOsmUv(Bs4Page& p, const Variant& v, int weight, const char* bar0, int loop) {
    begin(p, v, weight);
    if (broken(p, {1, 2})) return;
    if (!activeItems(p, true)) return;
    double level = 0, purity = 0;
    if (!getClamped(p, 0x2b, "FEATURE_WATER_LEVEL", 100.0, level)) return;
    if (!getClamped(p, 0x2c, "FEATURE_WATER_PURITY", 100.0, purity)) return;
    Bs4GaugeSlot& g0 = p.gauge[0];
    Bs4GaugeSlot& g1 = p.gauge[1];
    setupGauge(g0, level, 0.0, 100.0, 0x2b, 0x23, 0x48, 0x41, 0x52);
    if (!findBar(p, g0, bar0)) return;
    g0.percent = bs4w::gaugePercent(g0.last, g0.min, g0.max);
    setupGauge(g1, purity, 0.0, 100.0, 0x2c, 0x23, 0x70, 0x41, 0x7a);
    if (!findBar(p, g1, "waterbar")) return;
    g1.percent = bs4w::gaugePercent(g1.last, g1.min, g1.max);
    if (!readAlarms(p)) return;
    setLoop(p, loop, p.devActive);
    p.handler = [&p](int phase) {                            // exe:004331d0 = exe:00433440
        if (phase != 3) {
            bs4w::barGauge(p, p.gauge[0], phase);
            bs4w::barGauge(p, p.gauge[1], phase);
            alarmIcon(p, phase, 0, 0x8000);
        }
        if (phase == 2) pollActiveReload(p);
    };
}

void initOsm(Bs4Page& p, int variant) {
    if (const Variant* v = findVariant(kOsm, sizeof kOsm / sizeof *kOsm, variant)) initOsmUv(p, *v, 2, "osmbar", 5);
}
void initUv(Bs4Page& p, int variant) {
    if (const Variant* v = findVariant(kUv, sizeof kUv / sizeof *kUv, variant)) initOsmUv(p, *v, 3, "uvpbar", 3);
}

// exe:0042a280: the tide machine of the ocean biome (no variants)
void initTide(Bs4Page& p, int) {
    begin(p, "fbo1tide", "", "hitide", 10);
    if (broken(p, {1, 2})) return;
    if (!activeItems(p, false)) return;
    double level = 0;
    if (!getClamped(p, 0x2b, "FEATURE_WATER_LEVEL", 100.0, level)) return;
    Bs4GaugeSlot& g = p.gauge[0];
    setupGauge(g, level, 0.0, 100.0, 0x2b, 0x23, 0x48, 0x41, 0x52);
    if (!findBar(p, g, "waterbar")) return;
    g.percent = bs4w::gaugePercent(g.last, g.min, g.max);
    setLoop(p, 5, p.devActive);
    p.handler = [&p](int phase) {                            // exe:004342a0
        bs4w::barGauge(p, p.gauge[0], phase);
        if (phase == 2) pollActiveReload(p);
    };
}

// exe:0042a7d0: the wave machine of the ocean biome (no variants)
void initWave(Bs4Page& p, int) {
    begin(p, "fbo1wave", "", "hiwave", 0xb);
    if (broken(p, {1, 2})) return;
    if (!activeItems(p, false)) return;
    setLoop(p, 5, p.devActive);
    p.handler = [&p](int phase) {                            // exe:00433740
        if (phase == 2) pollActiveReload(p);
    };
}

// exe:00426b40: no page of BIOSYS4.DAT is named panelsprk, so this never runs (spec section 6)
void initSprk(Bs4Page& p, int variant) {
    const Variant* v = findVariant(kSprk, sizeof kSprk / sizeof *kSprk, variant);
    if (!v) return;
    begin(p, *v, 8);
    if (broken(p, {1, 2})) return;
    if (!activeItems(p, false)) return;
    p.handler = [&p](int phase) {                            // exe:004336b0
        if (phase == 2) pollActiveReload(p);
    };
}

Bs4PanelRegistrar regSump("panelsump", Bs4PanelDef{true, initSump, nullptr, nullptr});
Bs4PanelRegistrar regOsm("panelosm", Bs4PanelDef{true, initOsm, setActiveFromMask, nullptr});
Bs4PanelRegistrar regUv("paneluv", Bs4PanelDef{true, initUv, setActiveFromMask, nullptr});
Bs4PanelRegistrar regTide("paneltide", Bs4PanelDef{false, initTide, setActiveFromMaskB, nullptr});
Bs4PanelRegistrar regWave("panelwave", Bs4PanelDef{false, initWave, setActiveFromMaskB, nullptr});
Bs4PanelRegistrar regSprk("panelsprk", Bs4PanelDef{true, initSprk, setActiveFromMaskB, nullptr});

} // namespace
} // namespace biosys
