// BioSys 4 panels `panelwsav panelwoc panelwevo panelwrf` (pages 50..53, the water pumps of the four biomes): packet P6.
//   Init exe:0042ed70 (one function for the four pages; the page name picks the tank, the highlight ornament and the pump),
//   handler exe:00435e40, setter exe:004327e0 (the route / on / off buttons), CheckPumpRoute exe:004319d0.
// The panel shows the water tank of its biome (level, purity, temperature: feature +0xa4 = the tank) and the biome's pump (+0xb9 = the
// pump: its state, destination and the destination tank's level). The pump is routed to the tank of one of the four biomes.
//   items:  1 / 2  the pump's off / on button (1 "turn on" is shown after a destination was chosen, 2 "turn off" while it runs)
//           0x13..0x16 the four route buttons (pump off), 0xf..0x12 the destination shown (s o e r), "overlay" the tank is receiving
#include "engine/bs4.h"
#include "engine/bs4panel_aw.h"
#include "engine/bs4page.h"
#include <cstring>

namespace biosys {
namespace bs4aw {

namespace {

struct Biome {
    const char* tank;                              // the water tank of the biome
    const char* pump;                              // its pump
    int dest;                                      // FEATURE_DESTINATION code of the biome: 0x16 savannah, 0x18 ocean, 0x17 evolved, 0x15 rainforest
};
constexpr Biome kBiomes[4] = {{"fbs3tank", "fbs3watpmp", 0x16}, {"fbo2tank", "fbo2watpmp", 0x18}, {"fbd3tank", "fbd3watpmp", 0x17},
                              {"fbr3tank", "fbr3watpmp", 0x15}};

}

// exe:004319d0
bool checkPumpRoute(Bs4& o, const std::string& tank, int& out) {
    int want = 0;
    if (iequals(tank, kBiomes[0].tank)) want = 0x16;
    else if (iequals(tank, kBiomes[1].tank)) want = 0x18;
    else if (iequals(tank, kBiomes[2].tank)) want = 0x17;
    else if (iequals(tank, kBiomes[3].tank)) want = 0x15;
    else {
        o.log("BS4: 'isWaterTankReceiving' : unrecognised feature name '" + tank + "'.");
        return false;
    }
    for (int i = 0; i < 4; i++) {
        const std::string pump = kBiomes[i].pump;
        u32 v = 0;
        double d = 0;
        if (!o.host().onGetFeatureParameter(pump, 2, v, d)) {
            o.log(failGet("FEATURE_ACTIVE", pump));
            return false;
        }
        if (v == 0) continue;
        if (!o.host().onGetFeatureParameter(pump, 0x14, v, d)) {
            o.log(failGet("FEATURE_DESTINATION", pump));
            return false;
        }
        if (v < 0x15 || 0x18 < v) {
            o.log("BS4: Unknown value (" + std::to_string(v) + ") of parameter FEATURE_DESTINATION for BiosFear feature '" + pump + "'.");
            return false;
        }
        if (static_cast<u32>(want) == v) {
            out = 1;
            return true;
        }
    }
    out = 0;
    return true;
}

} // namespace bs4aw

namespace {

using namespace bs4aw;

// exe:0042ed70
void initWater(Bs4Page& p, int) {
    Bs4& o = p.owner();
    const Biome* b = nullptr;
    const char* highlight = nullptr;
    if (iequals(p.name(), "panelwsav")) { b = &kBiomes[0]; highlight = "hiutils1"; }
    else if (iequals(p.name(), "panelwoc")) { b = &kBiomes[1]; highlight = "hiutilo1"; }
    else if (iequals(p.name(), "panelwevo")) { b = &kBiomes[2]; highlight = "hiutile1"; }
    else if (iequals(p.name(), "panelwrf")) { b = &kBiomes[3]; highlight = "hiutilr1"; }
    else {
        o.log("BS4: Unrecognised name of water-related panel page: '" + p.name() + "'.");
        return;
    }
    const std::string tank = b->tank, pump = b->pump;
    o.setHighlight(highlight, 0xe);                           // +0x1bec, +0x1bf8
    p.feature = tank;                                         // +0xa4
    p.feature2 = pump;                                        // +0xb9
    p.reloadName = "";                                        // +0x188 (the empty string at exe:0048338c)
    Wire& w = wire(p);
    if (malfunction(p, tank)) {                               // FEATURE_MALFUNCTION of the tank
        for (int it : {1, 2, 0x13, 0x14, 0x15, 0x16, 0xf, 0x10, 0x11, 0x12}) p.showItem(it, false);
        return;
    }
    p.devActive = false;                                      // +0x160
    w.receiving = false;                                      // +0x168
    w.dest = -1;                                              // +0x530
    int out = 0;
    if (!checkPumpRoute(o, tank, out)) return;
    u32 v = 0;
    double d = 0;
    if (out == 1) {
        w.receiving = true;
        p.setOrnamentVisible("overlay", true);
        for (int it : {1, 2, 0x13, 0x14, 0x15, 0x16, 0xf, 0x10, 0x11, 0x12}) p.showItem(it, false);
    } else {
        if (!getp(p, pump, 2, "FEATURE_ACTIVE", v, d)) return;
        if (v != 0) {
            p.devActive = true;
            p.showItem(2, true);
            for (int it : {1, 0x13, 0x14, 0x15, 0x16, 0xf, 0x10, 0x11, 0x12}) p.showItem(it, false);
            if (!getp(p, pump, 0x14, "FEATURE_DESTINATION", v, d)) return;
            int item;
            switch (v) {
            case 0x15: item = 0x12; break;
            case 0x16: item = 0xf; break;
            case 0x17: item = 0x11; break;
            case 0x18: item = 0x10; break;
            default:
                o.log("BS4: Unknown value (" + std::to_string(v) + ") of parameter FEATURE_DESTINATION for BiosFear feature '" + pump + "'.");
                return;
            }
            p.showItem(item, true);
            w.dest = static_cast<int>(v);
        } else {
            for (int it : {2, 1}) p.showItem(it, false);
            for (int it : {0x13, 0x14, 0x15, 0x16}) p.showItem(it, true);
            for (int it : {0xf, 0x10, 0x11, 0x12}) p.showItem(it, false);
        }
    }
    Bs4GaugeSlot& level = p.gauge[0];                         // +0x1e0
    Bs4GaugeSlot& purity = p.gauge[1];                        // +0x228
    Bs4GaugeSlot& temp = p.gauge[2];                          // +0x270
    Bs4GaugeSlot& dlevel = p.extraGauge[0];                   // +0x2b8
    if (!readGauge(p, level, tank, 0x2b, "FEATURE_WATER_LEVEL", 0, 100)) return;
    if (!readGauge(p, purity, tank, 0x2c, "FEATURE_WATER_PURITY", 0, 100)) return;
    if (!readGauge(p, temp, tank, 0x28, "FEATURE_WATER_TEMPERATURE", -10, 40)) return;
    if (!setupBar(p, level, Bs4Rect{0x8f, 0x55, 0xad, 0x5f}, 0x2b, 0, 100, "levelbar", noImage("levelbar", tank))) return;
    if (!setupBar(p, purity, Bs4Rect{0x8f, 0x6f, 0xad, 0x79}, 0x2c, 0, 100, "puritybar", noImage("puritybar", tank))) return;
    if (!setupBar(p, temp, Bs4Rect{0x8f, 0x89, 0xad, 0x93}, 0x28, -10, 40, "tempbar", noImage("tempbar", tank))) return;
    if (w.dest == -1) dlevel.last = 0;
    else if (!readGauge(p, dlevel, pump, 0x2b, "FEATURE_WATER_LEVEL", 0, 100)) return;
    if (!setupBar(p, dlevel, Bs4Rect{0x8f, 0xbd, 0xad, 199}, 0x2b, 0, 100, "dlevelbar", "BS4: Can't find image 'dlevelbar' on water tank panel page.")) return;
    if (!getp(p, p.feature, 0x32, "FEATURE_ALARMS", v, d)) return;
    p.alarmBits = v;                                          // +0x174
    p.loopSound = 5;                                          // +0x678
    if (p.devActive) o.loopStart(5);
    else o.loopStop(5);
    p.handler = [&p](int phase) {                             // exe:00435e40
        if (phase == 3) return;
        Bs4& ob = p.owner();
        Wire& wr = wire(p);
        bs4w::barGauge(p, p.gauge[0], phase);
        bs4w::barGauge(p, p.gauge[1], phase);
        bs4w::barGauge(p, p.gauge[2], phase);
        alarmIcon(p, phase, p.gauge[0], ob.colours().white, 0x200000, 0x400000);
        alarmIcon(p, phase, p.gauge[2], ob.colours().white, 0x10000, 0x20000);
        if (wr.dest != -1) {                                  // the destination tank's level: the gauge reads the pump's feature
            const std::string saved = p.feature;
            p.feature = p.feature2;
            bs4w::barGauge(p, p.extraGauge[0], phase);
            p.feature = saved;
        }
        if (p.devActive && phase == 2) {
            u32 vv = 0;
            double dv = 0;
            if (ob.host().onGetFeatureParameter(p.feature2, 2, vv, dv)) {
                if (vv != 0) vv = 1;
                if (static_cast<u32>(p.devActive) != vv) {
                    p.reload = true;                          // +0x184
                    return;
                }
            }
        }
        if (wr.receiving && phase == 2) {
            int r = 0;
            if (checkPumpRoute(ob, p.feature, r) && r == 0) p.reload = true;
        }
    };
}

// exe:004327e0 (this = the page, now = the new item mask, old = the mask at the last call): a button bit that became set is a command
void setPump(Bs4Page& p, u32 now, u32 old) {
    Bs4& o = p.owner();
    const std::string pump = p.feature2;
    if ((now & 2) && !(old & 2)) {                            // item 1: turn off
        if (!setp(p, pump, 2, "FEATURE_ACTIVE", false, 0)) return;
        p.devActive = false;
        if (p.loopSound != -1) o.loopStop(p.loopSound);
        return;
    }
    if ((now & 4) && !(old & 4)) {                            // item 2: turn on
        if (!setp(p, pump, 2, "FEATURE_ACTIVE", false, 1)) return;
        p.devActive = true;
        if (p.loopSound != -1) o.loopStart(p.loopSound);
        return;
    }
    int dest;
    if ((now & 0x8000) && !(old & 0x8000)) dest = 0x16;
    else if ((now & 0x10000) && !(old & 0x10000)) dest = 0x18;
    else if ((now & 0x20000) && !(old & 0x20000)) dest = 0x17;
    else if ((now & 0x40000) && !(old & 0x40000)) dest = 0x15;
    else return;
    if (!setp(p, pump, 0x14, "FEATURE_DESTINATION", true, dest)) return;
    wire(p).dest = dest;
    Bs4GaugeSlot& g = p.extraGauge[0];
    if (!readGauge(p, g, pump, 0x2b, "FEATURE_WATER_LEVEL", 0, 100)) return;
    g.percent = gaugeWidth(g);     // +0x2f4
    p.showItem(1, true);
    p.showItem(2, false);
}

Bs4PanelRegistrar regSav("panelwsav", Bs4PanelDef{false, initWater, setPump, nullptr});
Bs4PanelRegistrar regOc("panelwoc", Bs4PanelDef{false, initWater, setPump, nullptr});
Bs4PanelRegistrar regEvo("panelwevo", Bs4PanelDef{false, initWater, setPump, nullptr});
Bs4PanelRegistrar regRf("panelwrf", Bs4PanelDef{false, initWater, setPump, nullptr});

} // namespace
} // namespace biosys
