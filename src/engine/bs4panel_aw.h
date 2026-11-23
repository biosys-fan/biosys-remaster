// BioSys 4 panels `panelasav panelaoc panelaevo panelarf` (air lungs) and `panelwsav panelwoc panelwevo panelwrf` (water pumps), packet P6:
// what the two groups share. Every init of the original repeats these steps inline (the read of a parameter with its valid-range check,
// the look-up of the bar ornament, the malfunction picture); here they are written once. Messages are the exe's.
#pragma once
#include "engine/bs4.h"
#include "engine/bs4widgets.h"
#include <cstdio>
#include <map>
#include <string>

namespace biosys::bs4aw {

// The members of the panel page the original keeps outside the fields Bs4Page has: +0x168 (water) / +0x16c (air) "the tank is
// receiving: the overlay is shown", +0x170 (air) "the lung draws air from its biome", +0x530 the destination the panel shows (-1 none).
struct Wire {
    bool receiving = false;
    bool draw = false;
    int dest = -1;
};
inline Wire& wire(const Bs4Page& p) {
    static std::map<const Bs4Page*, Wire> m;
    return m[&p];
}

inline std::string failGet(const std::string& what, const std::string& f) {
    return "BS4: Failed to get the value of parameter " + what + " for BiosFear feature '" + f + "'.";
}
inline std::string failSetValue(const std::string& what, const std::string& f) {      // "Failed to set value of" (FEATURE_ACTIVE)
    return "BS4: Failed to set value of parameter " + what + " for BiosFear feature '" + f + "'.";
}
inline std::string failSetThe(const std::string& what, const std::string& f) {        // "Failed to set the value of" (DESTINATION, DRAW_AIR)
    return "BS4: Failed to set the value of parameter " + what + " for BiosFear feature '" + f + "'.";
}

// event 27 with the exe's own message on failure
inline bool getp(Bs4Page& p, const std::string& f, int id, const char* what, u32& i, double& d) {
    if (p.owner().host().onGetFeatureParameter(f, id, i, d)) return true;
    p.owner().log(failGet(what, f));
    return false;
}
// event 26 (a, 0, 0); the message is the exe's ("the value" for DESTINATION / DRAW_AIR_FROM_BIOME, "value" for ACTIVE)
inline bool setp(Bs4Page& p, const std::string& f, int id, const char* what, bool the, int a) {
    if (p.owner().host().onSetFeatureParameter(f, id, a, 0, 0)) return true;
    p.owner().log(the ? failSetThe(what, f) : failSetValue(what, f));
    return false;
}

// A gauge value read: get `id`; outside [min, max] it logs and clamps the value (the original clamps the value it has just read);
// the value goes to `slot.last`. False: the get failed (logged).
inline bool readGauge(Bs4Page& p, Bs4GaugeSlot& slot, const std::string& f, int id, const char* what, double mn, double mx) {
    u32 i = 0;
    double v = 0;
    if (!getp(p, f, id, what, i, v)) return false;
    if (v < mn || mx < v) {
        char b[64];
        std::snprintf(b, sizeof b, "%f", v);
        p.owner().log("BS4: Value of parameter " + std::string(what) + " for BiosFear feature '" + f + "' is outside its valid range.  (Value = " +
                      b + ").");
        if (v < mn) v = mn;
        if (mx < v) v = mx;
    }
    slot.last = v;
    return true;
}

// The pixel width of a gauge after its value changed (the setters recompute it; the original's __ftol of a quotient by (max - min) = 0, which a
// broken panel's never initialised destination slot gives, is the x87's "integer indefinite" 0x80000000 where a C++ cast is undefined)
inline int gaugeWidth(const Bs4GaugeSlot& g) {
    const double r = (((g.last - g.min) * 100.0) / (g.max - g.min)) * 151.0 * 0.01;
    if (!(r > -2147483649.0 && r < 2147483648.0)) return static_cast<int>(0x80000000u);
    return static_cast<int>(r);
}

// The second half of a gauge's set-up: text rect, [min, max], parameter id, the bar ornament `ornament` (first type-1 ornament of that
// name: image and position), the 0x97 x 4 bar rect at the ornament's position and the pixel width from slot.last. False (image = -1,
// `missing` logged): no such ornament.
inline bool setupBar(Bs4Page& p, Bs4GaugeSlot& slot, const Bs4Rect& text, int id, double mn, double mx, const char* ornament,
                     const std::string& missing) {
    slot.text = text;
    slot.min = mn;
    slot.max = mx;
    slot.paramId = id;
    for (const Ornament& o : p.rec.ornaments) {
        if (o.type != 1 || !iequals(o.name, ornament)) continue;
        slot.image = static_cast<int>(o.image);
        slot.bar = Bs4Rect{o.x, o.y, o.x + 0x97, o.y + 4};
        slot.percent = bs4w::gaugePercent(slot.last, mn, mx);
        return true;
    }
    p.owner().log(missing);
    slot.image = -1;
    return false;
}
inline std::string noImage(const std::string& image, const std::string& f) {
    return "BS4: Can't find image '" + image + "' on panel page that represents BiosFear feature '" + f + "'.";
}

// FEATURE_MALFUNCTION (7) of `f`: the ornament "broken" is shown while the device is broken; then the page handler is cleared and click
// sound 7 played. Returns true when broken. A failed read is logged and counts as not broken (and leaves the ornament alone).
inline bool malfunction(Bs4Page& p, const std::string& f) {
    u32 i = 0;
    double d = 0;
    if (!getp(p, f, 7, "FEATURE_MALFUNCTION", i, d)) return false;
    if (i == 0) {
        p.setOrnamentVisible("broken", false);
        return false;
    }
    p.setOrnamentVisible("broken", true);
    p.handler = nullptr;                          // +0xd8
    p.owner().host().playClick(7);                // exe:0040c310
    return true;
}

// The alarm icon of a gauge: at the bar rect's top-left corner (the original passes a pointer to the rect, +0x20c ...)
inline void alarmIcon(Bs4Page& p, int phase, const Bs4GaugeSlot& g, int clearIndex, u32 maskA, u32 maskB) {
    const int pos[2] = {g.bar.left, g.bar.top};
    bs4w::alarmBitIcon(p, phase, pos, clearIndex, maskA, maskB);
}

// exe:004319d0 / exe:00431c30: is the tank (resp. the lung's biome) the destination of some running pump (lung)? `tank` / `lung` is the
// feature name of the panel's tank (lung) of one of the four biomes. false = failure (logged), else out = 1 / 0.
bool checkPumpRoute(Bs4& o, const std::string& tank, int& out);
bool checkLungRoute(Bs4& o, const std::string& lung, int& out);

// driver support (biosys-bs4test `p6`): one line with the panel page's feature names, mirrors (active, reload, loop, alarm bits), the Wire,
// the item mask, the "overlay" / "broken" ornament flags and the six gauge slots (last value, pixel width, parameter id, image)
std::string debugState(const Bs4Page& p);

} // namespace biosys::bs4aw
