// BioSys 4 device panels A (packet P4): the parts every Init, handler and setter of pages 54..69 has in common. See bs4panels_a.h.
#include "engine/bs4panels_a.h"
#include <cstdio>
#include <cstring>

namespace biosys::bs4a {

namespace {

// the three variables the original keeps in the page object (+0x5a8, +0x5e0, +0x618, +0x4d4); one panel page exists at a time and every
// Init sets them
RangeState g_heatDay, g_heatNight, g_mist;
u32 g_waterSource = 0;

bool setP(Bs4Page& p, int id, const char* paramName, int a) {
    if (p.owner().host().onSetFeatureParameter(p.feature, id, a, 0, 0)) return true;
    p.owner().log(std::string("BS4: Failed to set value of parameter ") + paramName + " for BiosFear feature '" + p.feature + "'.");
    return false;
}

} // namespace

const Variant* findVariant(const Variant* table, size_t n, int variant) {
    for (size_t i = 0; i < n; i++)
        if (table[i].variant == variant) return &table[i];
    return nullptr;
}

// exe:00421de0 (the first lines of every Init)
void begin(Bs4Page& p, const std::string& feature, const std::string& reload, const std::string& highlight, int weight) {
    p.reloadName = reload;                                   // +0x188
    p.owner().setHighlight(highlight, weight);               // +0x1bec, +0x1bf8
    p.feature = feature;                                     // +0xa4
}

// exe:00421de0
void begin(Bs4Page& p, const Variant& v, int weight) { begin(p, v.feature, v.reload, v.highlight, weight); }

// exe:004417b0 OnGetFeatureParameter + the failure message of exe:00421de0
bool get(Bs4Page& p, int id, const char* paramName, u32& i, double& d) {
    if (p.owner().host().onGetFeatureParameter(p.feature, id, i, d)) return true;
    p.owner().log(std::string("BS4: Failed to get the value of parameter ") + paramName + " for BiosFear feature '" + p.feature + "'.");
    return false;
}

// exe:00421de0 (and the same block in exe:00422590, 00423e60, 00426f20, 00428e30, 00429d50): a time of day above 86400 is logged with its
// value and then clamped to 86400. (The Ghidra decompile shows neither the clamp store nor the right stack word of the message; the
// disassembly does: `mov [esp+0x18], 0x15180` writes the checked variable, e.g. 0x4223b6.)
void logTooLarge(Bs4Page& p, const char* paramName, u32 value) {
    p.owner().log(std::string("BS4: Value of parameter ") + paramName + " for BiosFear feature '" + p.feature +
                  "' is greater than the maximum.  (Value = " + std::to_string(value) + ", Maximum = 86400 (= 24hours)).");
}

// exe:004246c0 (the same clamp is in exe:00422e10, exe:00423710, exe:00424ed0, exe:00425950, exe:00426220, exe:0042a280)
bool getClamped(Bs4Page& p, int id, const char* paramName, double hi, double& value) {
    u32 i = 0;
    double v = 0;
    if (!get(p, id, paramName, i, v)) return false;
    if (v < 0.0 || hi < v) {
        char b[64];
        std::snprintf(b, sizeof b, "%f", v);
        p.owner().log(std::string("BS4: Value of parameter ") + paramName + " for BiosFear feature '" + p.feature +
                      "' is outside its valid range.  (Value = " + b + ").");
        if (!(0.0 <= v)) v = 0.0;
        if (!(v <= hi)) v = hi;
    }
    value = v;
    return true;
}

// exe:00421de0 (the same block opens every Init)
bool broken(Bs4Page& p, std::initializer_list<int> hideItems) {
    u32 i = 0;
    double d = 0;
    bool isBroken = false;
    if (get(p, 7, "FEATURE_MALFUNCTION", i, d)) {
        if (i == 0) {
            p.setOrnamentVisible("broken", false);           // the first type-1 ornament "broken": flag bit 0 set
        } else {
            p.setOrnamentVisible("broken", true);
            p.handler = nullptr;                             // +0xd8
            p.owner().host().playClick(7);                   // exe:0040c310
            isBroken = true;
        }
    }
    if (isBroken)
        for (int n : hideItems) p.showItem(n, false);
    return isBroken;
}

// exe:00421de0
bool onState(Bs4Page& p) {
    u32 i = 0;
    double d = 0;
    if (!get(p, 1, "FEATURE_ON", i, d)) return false;
    p.showItem(1, false);
    p.showItem(2, false);
    p.showItem(0xf, false);
    if (i == 0) {
        p.showItem(2, true);
        p.showItem(0xf, true);
        p.devOn = false;                                     // +0x15c
    } else {
        p.showItem(1, true);
        p.devOn = true;
    }
    return true;
}

// exe:00421de0 (also exe:00423e60, exe:00426f20)
bool schedule(Bs4Page& p) {
    u32 i = 0;
    double d = 0;
    if (!get(p, 3, "FEATURE_TIME_ACTIVATE", i, d)) return false;
    if (i > 0x15180u) { logTooLarge(p, "FEATURE_TIME_ACTIVATE", i); i = 0x15180u; }
    const u32 on = i;
    p.schedule.onHours = on / 0xe10;
    p.schedule.onMinutes = (on % 0xe10) / 0x3c;
    if (!get(p, 6, "FEATURE_DAILY_TIME_LIMIT", i, d)) return false;
    if (i > 0x15180u) { logTooLarge(p, "FEATURE_DAILY_TIME_LIMIT", i); i = 0x15180u; }
    u32 off = i + on;                                        // end of the daily run: wraps at 24 h
    if (off > 0x15180u) off -= 0x15180u;
    p.schedule.offHours = off / 0xe10;
    p.schedule.offMinutes = (off % 0xe10) / 0x3c;
    p.sliderY = 0x51;                                        // +0x124
    p.schedule.onText = Bs4Rect{0x4d, 0x73, 0x7a, 0x7d};
    p.schedule.offText = Bs4Rect{0x4d, 0x85, 0x7a, 0x8f};
    return true;
}

// exe:00423e60 (also exe:00423710)
bool waterSource(Bs4Page& p) {
    u32 i = 0;
    double d = 0;
    if (!get(p, 0x5f, "FEATURE_WATER_SOURCE", i, d)) return false;
    if (i == 0x60) {
        p.showItem(0x11, false);
        p.showItem(0x12, true);
        g_waterSource = 0x60;                                // +0x4d4
    } else if (i == 0x61) {
        p.showItem(0x11, true);
        p.showItem(0x12, false);
        g_waterSource = 0x61;
    } else {
        p.owner().log("BS4: Invalid value (" + std::to_string(i) + ") of parameter FEATURE_WATER_SOURCE for BiosFear feature '" + p.feature + "'.");
        return false;
    }
    return true;
}

// exe:00425950 (osm, uv), exe:00426b40 / exe:0042a280 / exe:0042a7d0 (sprk, tide, wave)
bool activeItems(Bs4Page& p, bool inactiveShowsItem1) {
    u32 i = 0;
    double d = 0;
    if (!get(p, 2, "FEATURE_ACTIVE", i, d)) return false;
    p.showItem(1, false);
    p.showItem(2, false);
    const bool active = i != 0;
    p.showItem(active ? (inactiveShowsItem1 ? 2 : 1) : (inactiveShowsItem1 ? 1 : 2), true);
    p.devActive = active;                                    // +0x160
    return true;
}

// the ornament search + SetRect of exe:00422590 / exe:004246c0 / exe:00424ed0 / exe:00425950
bool findBar(Bs4Page& p, Bs4GaugeSlot& g, const char* name) {
    for (const Ornament& o : p.rec.ornaments) {
        if (o.type == 1 && iequals(o.name, name)) {
            g.image = static_cast<int>(o.image);
            g.bar = Bs4Rect{o.x, o.y, o.x + 0x97, o.y + 4};
            return true;
        }
    }
    p.owner().log(std::string("BS4: Can't find image '") + name + "' on panel page that represents BiosFear feature '" + p.feature + "'.");
    g.image = -1;
    return false;
}

// the slot stores of exe:00424ed0 (+0x1e0.. / +0x228..) and exe:00422590 (+0x198..)
void setupGauge(Bs4GaugeSlot& g, double value, double min, double max, int paramId, int tl, int tt, int tr, int tb) {
    g.last = value;
    g.text = Bs4Rect{tl, tt, tr, tb};
    g.min = min;
    g.max = max;
    g.paramId = paramId;
}

// the `+0x678 = n; StartLoop / StopLoop` tail of the Inits (exe:0040c4b0 / exe:0040c660)
void setLoop(Bs4Page& p, int loop, bool start) {
    p.loopSound = loop;                                      // +0x678
    if (start) p.owner().loopStart(loop);                    // exe:0040c4b0
    else p.owner().loopStop(loop);                           // exe:0040c660
}

// exe:00433740 (wave), exe:004336b0 (sprk), exe:004342a0 (tide), exe:004331d0 / exe:00433440 (osm, uv)
void pollActiveReload(Bs4Page& p) {
    u32 i = 0;
    double d = 0;
    if (!p.owner().host().onGetFeatureParameter(p.feature, 2, i, d)) return;
    if (i != 0) i = 1;
    // the original zeroes +0x4ac first and tests it against 1 (then comparing +0x15c instead of +0x160): never true
    if ((p.devActive ? 1u : 0u) != i) p.reload = true;       // +0x184
}

// exe:00434370 (rain), exe:00434920 (mist)
void pollWaterSourceReload(Bs4Page& p) {
    u32 i = 0;
    double d = 0;
    if (p.owner().host().onGetFeatureParameter(p.feature, 0x5f, i, d) && (i == 0x60 || i == 0x61) && g_waterSource != i) p.reload = true;
}

// exe:004323c0
void setOnFromMask(Bs4Page& p, u32 now, u32 old) {
    if ((now & 4) && !(old & 4)) {
        if (!setP(p, 1, "FEATURE_ON", 0)) return;
        p.devOn = false;
    }
    if ((now & 2) && !(old & 2)) {
        if (!setP(p, 1, "FEATURE_ON", 1)) return;
        p.devOn = true;
    }
}

namespace {
void setActive(Bs4Page& p, bool on) {
    if (!setP(p, 2, "FEATURE_ACTIVE", on ? 1 : 0)) return;
    p.devActive = on;
    if (p.loopSound != -1) {
        if (on) p.owner().loopStart(p.loopSound);
        else p.owner().loopStop(p.loopSound);
    }
}
} // namespace

// exe:00431f00
void setActiveFromMask(Bs4Page& p, u32 now, u32 old) {
    if ((now & 4) && !(old & 4)) {
        if (!setP(p, 2, "FEATURE_ACTIVE", 1)) return;
        p.devActive = true;
        if (p.loopSound != -1) p.owner().loopStart(p.loopSound);
    }
    if ((now & 2) && !(old & 2)) {
        if (!setP(p, 2, "FEATURE_ACTIVE", 0)) return;
        p.devActive = false;
        if (p.loopSound != -1) p.owner().loopStop(p.loopSound);
    }
}

// exe:00432180
void setActiveFromMaskB(Bs4Page& p, u32 now, u32 old) {
    if ((now & 4) && !(old & 4)) {
        if (!setP(p, 2, "FEATURE_ACTIVE", 0)) return;
        p.devActive = false;
        if (p.loopSound != -1) p.owner().loopStop(p.loopSound);
    }
    if ((now & 2) && !(old & 2)) {
        setActive(p, true);
    }
}

// exe:004324c0 (rain), exe:004325d0 (mist)
void setOnAndWaterSource(Bs4Page& p, u32 now, u32 old) {
    setOnFromMask(p, now, old);
    if ((now & 0x20000) && !(old & 0x20000)) {
        if (!setP(p, 0x5f, "FEATURE_WATER_SOURCE", 0x61)) return;
        g_waterSource = 0x61;
    }
    if ((now & 0x40000) && !(old & 0x40000)) {
        if (!setP(p, 0x5f, "FEATURE_WATER_SOURCE", 0x60)) return;
        g_waterSource = 0x60;
    }
}

RangeState& heatDay() { return g_heatDay; }
RangeState& heatNight() { return g_heatNight; }
RangeState& mistRange() { return g_mist; }
u32& waterSourceState() { return g_waterSource; }

// the copy-in / call / copy-out of exe:00434610 (heat, twice) and exe:00434920 (mist)
void runRange(Bs4Page& p, RangeState& s, int phase, const Bs4Rect& textA, const Bs4Rect& textB, int y, int paramA, int paramB, double scale) {
    Bs4RangeSlider& r = p.range;
    r.valueA = s.a;
    r.valueB = s.b;
    r.textA = textA;
    r.textB = textB;
    r.dragA = s.dragA;
    r.dragB = s.dragB;
    r.y = y;
    r.markerA = s.markerA;
    r.paramA = paramA;
    r.paramB = paramB;
    r.markerB = s.markerB;
    r.scale = scale;
    bs4w::valueRangeSlider(p, phase);
    s.a = r.valueA;
    s.b = r.valueB;
    s.dragA = r.dragA;
    s.dragB = r.dragB;
    s.markerA = r.markerA;
    s.markerB = r.markerB;
}

std::string debugState(Bs4Page& p) {
    auto rect = [](const Bs4Rect& r) {
        char b[64];
        std::snprintf(b, sizeof b, "%d,%d,%d,%d", r.left, r.top, r.right, r.bottom);
        return std::string(b);
    };
    int brokenShown = 0;                                      // the ornament "broken" is visible
    for (const Ornament& o : p.rec.ornaments)
        if (o.type == 1 && iequals(o.name, "broken")) { brokenShown = (o.flags & 1) ? 0 : 1; break; }
    char b[2048];
    int n = std::snprintf(b, sizeof b,
        "p4 page=%s feature=%s on=%d active=%d loop=%d reload=%d reloadname=%s alarm=0x%x tier=%s ws=0x%x sliderY=%d mask=0x%08x hdl=%d brokenorn=%d",
        p.name().c_str(), p.feature.c_str(), p.devOn ? 1 : 0, p.devActive ? 1 : 0, p.loopSound, p.reload ? 1 : 0,
        p.reloadName.empty() ? "-" : p.reloadName.c_str(), p.alarmBits, p.tierName.empty() ? "-" : p.tierName.c_str(), g_waterSource, p.sliderY,
        p.visibleMask(), p.handler ? 1 : 0, brokenShown);
    std::string s(b, static_cast<size_t>(n));
    const Bs4TimeSlider& t = p.timeSlider;
    std::snprintf(b, sizeof b, " time=%u:%u marker=%s drag=%d", t.hours, t.minutes, rect(t.marker).c_str(), t.dragging ? 1 : 0);
    s += b;
    const Bs4ScheduleSlider& c = p.schedule;
    std::snprintf(b, sizeof b, " sched=%u:%02u-%u:%02u markOn=%s markOff=%s", c.onHours, c.onMinutes, c.offHours, c.offMinutes,
                  rect(c.markerOn).c_str(), rect(c.markerOff).c_str());
    s += b;
    for (int g = 0; g < 2; g++) {
        const Bs4GaugeSlot& q = p.gauge[g];
        std::snprintf(b, sizeof b, " g%d=%.4f/%d/%d/%d", g, q.last, q.percent, q.paramId, q.image);
        s += b;
    }
    auto rs = [&](const char* name, const RangeState& r) {
        std::snprintf(b, sizeof b, " %s=%.4f,%.4f,%s,%s", name, r.a, r.b, rect(r.markerA).c_str(), rect(r.markerB).c_str());
        s += b;
    };
    rs("heatday", g_heatDay);
    rs("heatnight", g_heatNight);
    rs("mist", g_mist);
    std::snprintf(b, sizeof b, " range=%.4f,%.4f,%s,%s scale=%.6f", p.range.valueA, p.range.valueB, rect(p.range.markerA).c_str(),
                  rect(p.range.markerB).c_str(), p.range.scale);
    s += b;
    return s;
}

} // namespace biosys::bs4a
