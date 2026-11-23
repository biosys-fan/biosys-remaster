// BioSys 4 panels `panelasav panelaoc panelaevo panelarf` (pages 46..49, the air lungs of the four biomes): packet P6.
//   Init exe:0042ff10 (one function for the four pages; the page name picks the lung, the highlight ornament and the own-biome item),
//   handler exe:00436350, setter exe:00432b50 (the on / off / route / draw / push buttons), CheckLungRoute exe:00431c30.
// The lung moves air between its own biome and another: FEATURE_DESTINATION (0x14) = 0x15..0x18 sends it to the biome of that code
// (0x15 rainforest, 0x16 savannah, 0x17 evolved, 0x18 ocean), 0x1a pushes it into the own biome, FEATURE_DRAW_AIR_FROM_BIOME (0x11) draws
// air from the biome. The panel shows the biome's air (temperature, oxygen, CO2, pollution, volume) and the destination's volume.
//   items:  1 / 2 off / on button, 0x13..0x16 the route buttons (lung off), 0xf..0x12 the destination shown (s o e r),
//           0x1c 0x1d 0x17 0x1b the push / draw states, 23 / 27 / 28 / 29 the draw and push buttons (see the setter), "overlay" the
//           biome's air is being received by another lung
#include "engine/bs4.h"
#include "engine/bs4panel_aw.h"
#include "engine/bs4page.h"

namespace biosys {
namespace bs4aw {

namespace {

constexpr const char* kLungs[4] = {"fbs2lung", "fbo1lung", "fbd5lung", "fbr2lung"};
constexpr int kCodes[4] = {0x16, 0x18, 0x17, 0x15};

}

// exe:00431c30
bool checkLungRoute(Bs4& o, const std::string& lung, int& out) {
    int want = 0;
    bool known = false;
    for (int i = 0; i < 4 && !known; i++)
        if (iequals(lung, kLungs[i])) { want = kCodes[i]; known = true; }
    if (!known) {
        o.log("BS4: 'isAirTankReceiving' : unrecognised feature name '" + lung + "'.");
        return false;
    }
    for (int i = 0; i < 4; i++) {
        const std::string name = kLungs[i];
        u32 v = 0;
        double d = 0;
        if (!o.host().onGetFeatureParameter(name, 2, v, d)) {
            o.log(failGet("FEATURE_ACTIVE", name));
            return false;
        }
        if (v == 0) continue;
        if (!o.host().onGetFeatureParameter(name, 0x11, v, d)) {
            o.log(failGet("FEATURE_DRAW_AIR_FROM_BIOME", name));
            return false;
        }
        if (v != 0) continue;                                  // drawing from its own biome sends nothing anywhere
        if (!o.host().onGetFeatureParameter(name, 0x14, v, d)) {
            o.log(failGet("FEATURE_DESTINATION", name));
            return false;
        }
        if (v < 0x15 || (0x18 < v && v != 0x1a)) {
            o.log("BS4: Unknown value (" + std::to_string(v) + ") of parameter FEATURE_DESTINATION for BiosFear feature '" + name + "'.");
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

std::string debugState(const Bs4Page& p) {
    const Wire& w = wire(p);
    char b[256];
    std::string s = "p6 page=" + p.name() + " feature=" + p.feature + " feature2=" + p.feature2;
    std::snprintf(b, sizeof b, " active=%d reload=%d loop=%d alarm=0x%x recv=%d draw=%d dest=%d mask=0x%08x", p.devActive ? 1 : 0, p.reload ? 1 : 0,
                  p.loopSound, p.alarmBits, w.receiving ? 1 : 0, w.draw ? 1 : 0, w.dest, p.visibleMask());
    s += b;
    for (const char* n : {"overlay", "broken"})
        for (const Ornament& o : p.rec.ornaments)
            if (o.type == 1 && iequals(o.name, n)) {
                std::snprintf(b, sizeof b, " %s=%u", n, o.flags & 1u);
                s += b;
                break;
            }
    const Bs4GaugeSlot* g[6] = {&p.gauge[0], &p.gauge[1], &p.gauge[2], &p.extraGauge[0], &p.extraGauge[1], &p.extraGauge[2]};
    for (int i = 0; i < 6; i++) {
        std::snprintf(b, sizeof b, " g%d=%.17g/%d/0x%x/%d", i, g[i]->last, g[i]->percent, g[i]->paramId, g[i]->image);
        s += b;
    }
    return s;
}

} // namespace bs4aw

namespace {

using namespace bs4aw;

struct AirBiome {
    const char* page;
    const char* lung;
    const char* highlight;
    int item;                                                 // the item of the own biome among 0xf..0x12
    int dest;                                                 // the own biome's FEATURE_DESTINATION code
};
constexpr AirBiome kAir[4] = {{"panelasav", "fbs2lung", "hiairts1", 0xf, 0x16},
                              {"panelaoc", "fbo1lung", "hiairto1", 0x10, 0x18},
                              {"panelaevo", "fbd5lung", "hiairte1", 0x11, 0x17},
                              {"panelarf", "fbr2lung", "hiairtr1", 0x12, 0x15}};

const AirBiome* airBiome(const Bs4Page& p) {
    for (const AirBiome& b : kAir)
        if (iequals(p.name(), b.page)) return &b;
    return nullptr;
}

// exe:0042ff10
void initAir(Bs4Page& p, int) {
    Bs4& o = p.owner();
    const AirBiome* b = airBiome(p);
    if (!b) {
        o.log("BS4: Unrecognised name of air-related panel page: '" + p.name() + "'.");
        return;
    }
    const std::string lung = b->lung;
    o.setHighlight(b->highlight, 0);                          // +0x1bec, +0x1bf8 = 0
    p.feature = lung;                                         // +0xa4
    p.reloadName = "";                                        // +0x188
    Wire& w = wire(p);
    p.devActive = false;                                      // +0x160
    w.receiving = false;                                      // +0x16c
    w.draw = false;                                           // +0x170
    w.dest = -1;                                              // +0x530
    for (int it : {1, 2, 0x1c, 0x1d, 0x13, 0x14, 0x15, 0x16, 0xf, 0x10, 0x11, 0x12, 0x17, 0x1b}) p.showItem(it, false);
    if (malfunction(p, lung)) return;
    int out = 0;
    if (!checkLungRoute(o, lung, out)) return;
    u32 v = 0;
    double d = 0;
    if (out == 1) {
        w.receiving = true;
        p.setOrnamentVisible("overlay", true);
    } else {
        if (!getp(p, lung, 2, "FEATURE_ACTIVE", v, d)) return;
        if (v == 0) {
            for (int it : {0x13, 0x14, 0x15, 0x16}) p.showItem(it, true);
        } else {
            p.devActive = true;
            if (!getp(p, lung, 0x11, "FEATURE_DRAW_AIR_FROM_BIOME", v, d)) return;
            if (v != 1) {
                if (!getp(p, lung, 0x14, "FEATURE_DESTINATION", v, d)) return;
                switch (v) {
                case 0x15: p.showItem(0x12, true); p.showItem(2, true); break;
                case 0x16: p.showItem(0xf, true); p.showItem(2, true); break;
                case 0x17: p.showItem(0x11, true); p.showItem(2, true); break;
                case 0x18: p.showItem(0x10, true); p.showItem(2, true); break;
                case 0x1a: p.showItem(b->item, true); p.showItem(0x1d, true); break;
                default:
                    o.log("BS4: Unknown value (" + std::to_string(v) + ") of parameter FEATURE_DESTINATION for BiosFear feature '" + lung + "'.");
                    return;
                }
                w.dest = static_cast<int>(v);
            } else {
                w.draw = true;
                p.showItem(b->item, true);
                p.showItem(0x1b, true);
            }
        }
    }
    Bs4GaugeSlot* g[6] = {&p.gauge[0], &p.gauge[1], &p.gauge[2], &p.extraGauge[0], &p.extraGauge[1], &p.extraGauge[2]};
    if (!readGauge(p, *g[0], lung, 0x1e, "FEATURE_AIR_TEMPERATURE", -10, 40)) return;
    if (!readGauge(p, *g[1], lung, 0x22, "FEATURE_OXYGEN", 0, 100)) return;
    if (!readGauge(p, *g[2], lung, 0x23, "FEATURE_CO2", 0, 100)) return;
    if (!readGauge(p, *g[3], lung, 0x26, "FEATURE_POLLUTION", 0, 100)) return;
    if (!readGauge(p, *g[4], lung, 0x1c, "FEATURE_AIR_VOLUME", 0, 100)) return;
    if (!setupBar(p, *g[0], Bs4Rect{0x8f, 0x55, 0xad, 0x5f}, 0x1e, -10, 40, "tempbar", noImage("tempbar", lung))) return;
    if (!setupBar(p, *g[1], Bs4Rect{0x8f, 0x6f, 0xad, 0x79}, 0x22, 0, 100, "02bar", noImage("02bar", lung))) return;
    if (!setupBar(p, *g[2], Bs4Rect{0x8f, 0x89, 0xad, 0x93}, 0x23, 0, 100, "c02bar", noImage("c02bar", lung))) return;
    if (!setupBar(p, *g[3], Bs4Rect{0x8f, 0xa3, 0xad, 0xad}, 0x26, 0, 100, "polbar", noImage("polbar", lung))) return;
    if (!setupBar(p, *g[4], Bs4Rect{0x8f, 0xbd, 0xad, 199}, 0x1c, 0, 100, "airvolbar", noImage("airvolbar", lung))) return;
    if (w.dest == -1 || w.dest == 0x1a) g[5]->last = 0;
    else if (!readGauge(p, *g[5], lung, 0x1b, "FEATURE_DESTINATION_AIR_VOLUME", 0, 100)) return;
    if (!setupBar(p, *g[5], Bs4Rect{0x8f, 0x11b, 0xad, 0x125}, 0x1b, 0, 100, "dlevelbar", "BS4: Can't find image 'dlevelbar' on air tank panel page.")) return;
    if (!getp(p, p.feature, 0x32, "FEATURE_ALARMS", v, d)) return;
    p.alarmBits = v;                                          // +0x174
    p.loopSound = 1;                                          // +0x678
    if (p.devActive) o.loopStart(1);
    else o.loopStop(1);
    p.handler = [&p](int phase) {                             // exe:00436350
        if (phase == 3) return;
        Bs4& ob = p.owner();
        Wire& wr = wire(p);
        bs4w::barGauge(p, p.gauge[0], phase);
        bs4w::barGauge(p, p.gauge[1], phase);
        bs4w::barGauge(p, p.gauge[2], phase);
        bs4w::barGauge(p, p.extraGauge[0], phase);
        bs4w::barGauge(p, p.extraGauge[1], phase);
        alarmIcon(p, phase, p.gauge[0], ob.colours().white, 1, 2);
        alarmIcon(p, phase, p.gauge[1], ob.colours().lightGrey, 0x400, 0x800);
        alarmIcon(p, phase, p.gauge[2], ob.colours().white, 0x100, 0x200);
        alarmIcon(p, phase, p.extraGauge[0], ob.colours().lightGrey, 0x1000, 0);
        if (wr.dest != -1 && wr.dest != 0x1a) bs4w::barGauge(p, p.extraGauge[2], phase);
        if (p.devActive && phase == 2) {
            u32 vv = 0;
            double dv = 0;
            if (ob.host().onGetFeatureParameter(p.feature, 2, vv, dv)) {
                if (vv != 0) vv = 1;
                if (static_cast<u32>(p.devActive) != vv) {
                    p.reload = true;                          // +0x184
                    return;
                }
            }
        }
        if (wr.receiving && phase == 2) {
            int r = 0;
            if (checkLungRoute(ob, p.feature, r) && r == 0) p.reload = true;
        }
    };
}

// exe:00432b50 (now = the new item mask, old = the mask at the last call): a button bit that became set is a command
void setLung(Bs4Page& p, u32 now, u32 old) {
    Bs4& o = p.owner();
    Wire& w = wire(p);
    const std::string lung = p.feature;
    const auto up = [&](u32 bit) { return (now & bit) && !(old & bit); };
    if (up(2) || up(0x10000000) || up(0x800000)) {            // items 1, 28, 23: off
        if (!setp(p, lung, 2, "FEATURE_ACTIVE", false, 0)) return;
        p.devActive = false;
        if (p.loopSound != -1) o.loopStop(p.loopSound);
        if (!up(0x800000)) return;
        w.draw = false;                                       // +0x170
        w.dest = 0x1a;
        setp(p, lung, 0x14, "FEATURE_DESTINATION", true, 0x1a);
        return;
    }
    if (up(4) || up(0x20000000) || up(0x8000000)) {           // items 2, 29, 27: on
        if (up(0x8000000)) {                                  // 27: draw air from the biome
            if (!setp(p, lung, 0x11, "FEATURE_DRAW_AIR_FROM_BIOME", true, 1)) return;
            w.draw = true;
        }
        if (!setp(p, lung, 2, "FEATURE_ACTIVE", false, 1)) return;
        p.devActive = true;
        if (p.loopSound != -1) o.loopStart(p.loopSound);
        return;
    }
    int dest;
    if (up(0x8000)) dest = 0x16;
    else if (up(0x10000)) dest = 0x18;
    else if (up(0x20000)) dest = 0x17;
    else if (up(0x40000)) dest = 0x15;
    else return;
    const AirBiome* b = airBiome(p);                          // a route to the own biome is the push into it
    if (b && dest == b->dest) dest = 0x1a;
    if (!setp(p, lung, 0x14, "FEATURE_DESTINATION", true, dest)) return;
    w.dest = dest;
    if (dest != 0x1a) {
        Bs4GaugeSlot& g = p.extraGauge[2];                    // +0x348
        if (!readGauge(p, g, lung, 0x1b, "FEATURE_DESTINATION_AIR_VOLUME", 0, 100)) return;
        g.percent = gaugeWidth(g); // +0x384
    }
    if (w.dest == 0x1a) {
        p.showItem(1, false); p.showItem(2, false); p.showItem(0x1c, true); p.showItem(0x1d, false); p.showItem(0x17, true); p.showItem(0x1b, false);
    } else {
        p.showItem(1, true); p.showItem(2, false); p.showItem(0x1c, false); p.showItem(0x1d, false); p.showItem(0x17, false); p.showItem(0x1b, false);
    }
}

Bs4PanelRegistrar regSav("panelasav", Bs4PanelDef{false, initAir, setLung, nullptr});
Bs4PanelRegistrar regOc("panelaoc", Bs4PanelDef{false, initAir, setLung, nullptr});
Bs4PanelRegistrar regEvo("panelaevo", Bs4PanelDef{false, initAir, setLung, nullptr});
Bs4PanelRegistrar regRf("panelarf", Bs4PanelDef{false, initAir, setLung, nullptr});

} // namespace
} // namespace biosys
