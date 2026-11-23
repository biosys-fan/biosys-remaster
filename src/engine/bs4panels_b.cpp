// BioSys 4 terminal, packet P5: shared pieces of the device-panel inits (see bs4panels_b.h).
#include "engine/bs4panels_b.h"
#include <cstdio>

namespace biosys::bs4b {

std::string failGet(const std::string& what, const std::string& feature) {
    return "BS4: Failed to get the value of parameter " + what + " for BiosFear feature '" + feature + "'.";
}
std::string failSet(const std::string& what, const std::string& feature) {
    return "BS4: Failed to set value of parameter " + what + " for BiosFear feature '" + feature + "'.";
}
std::string noImage(const std::string& image, const std::string& feature) {
    return "BS4: Can't find image '" + image + "' on panel page that represents BiosFear feature '" + feature + "'.";
}

namespace {
std::string fmtF(double v) {                          // "%f"
    char b[512];
    std::snprintf(b, sizeof b, "%f", v);
    return b;
}
} // namespace

bool read(Bs4Page& p, const std::string& feature, int id, u32& i, double& d, const std::string& failMessage) {
    if (p.owner().host().onGetFeatureParameter(feature, id, i, d)) return true;
    p.owner().log(failMessage);
    return false;
}

bool readGauge(Bs4Page& p, Bs4GaugeSlot& slot, const std::string& feature, int id, double min, double max, const std::string& failMessage,
               const std::string& rangePrefix, const std::string& rangeSuffix) {
    u32 i = 0;
    double v = 0;
    if (!read(p, feature, id, i, v, failMessage)) return false;
    if (v < min || max < v) {
        p.owner().log(rangePrefix + fmtF(v) + rangeSuffix);
        if (v < min) v = min;
        if (max < v) v = max;
    }
    slot.last = v;
    return true;
}

bool setupBar(Bs4Page& p, Bs4GaugeSlot& slot, const Bs4Rect& text, int id, double min, double max, const char* ornament,
              const std::string& missingMessage) {
    slot.text = text;
    slot.paramId = id;
    slot.min = min;
    slot.max = max;
    int found = -1;
    for (size_t k = 0; k < p.rec.ornaments.size(); k++)
        if (p.rec.ornaments[k].type == 1 && iequals(p.rec.ornaments[k].name, ornament)) { found = static_cast<int>(k); break; }
    if (found < 0) {
        p.owner().log(missingMessage);
        slot.image = -1;
        return false;
    }
    const Ornament& o = p.rec.ornaments[static_cast<size_t>(found)];
    slot.image = static_cast<int>(o.image);
    slot.bar = Bs4Rect{o.x, o.y, o.x + 0x97, o.y + 4};
    slot.percent = bs4w::gaugePercent(slot.last, slot.min, slot.max);
    return true;
}

bool malfunction(Bs4Page& p, const std::string& feature) {
    u32 broken = 0;
    double d = 0;
    if (!read(p, feature, 7, broken, d, failGet("FEATURE_MALFUNCTION", feature))) return false;
    if (broken == 0) {
        p.setOrnamentVisible("broken", false);
        return false;
    }
    p.setOrnamentVisible("broken", true);
    p.handler = nullptr;                                    // +0xd8 = 0
    p.owner().host().playClick(7);                          // exe:0040c310(7)
    return true;
}

bool readSchedule(Bs4Page& p, const std::string& feature) {
    u32 on = 0, len = 0;
    double d = 0;
    if (!read(p, feature, 3, on, d, failGet("FEATURE_TIME_ACTIVATE", feature))) return false;
    if (on > 0x15180u)
        p.owner().log("BS4: Value of parameter FEATURE_TIME_ACTIVATE for BiosFear feature '" + feature + "' is greater than the maximum.  (Value = " +
                      std::to_string(on) + ", Maximum = 86400 (= 24hours)).");
    if (on > 0x15180u) on = 0x15180u;                       // exe:00429d50 / 00428e30 clamp after logging (see logTooLarge in bs4panels_a.cpp)
    p.schedule.onHours = on / 0xe10;
    p.schedule.onMinutes = (on % 0xe10) / 0x3c;
    if (!read(p, feature, 6, len, d, failGet("FEATURE_DAILY_TIME_LIMIT", feature))) return false;
    if (len > 0x15180u)
        p.owner().log("BS4: Value of parameter FEATURE_DAILY_TIME_LIMIT for BiosFear feature '" + feature + "' is greater than the maximum.  (Value = " +
                      std::to_string(len) + ", Maximum = 86400 (= 24hours)).");
    if (len > 0x15180u) len = 0x15180u;
    u32 off = len + on;
    if (off > 0x15180u) off -= 0x15180u;
    p.schedule.offHours = off / 0xe10;
    p.schedule.offMinutes = (off % 0xe10) / 0x3c;
    return true;
}

// exe:00433850 (and the first three calls of exe:00433b00): the slot copies of the original only move the slot into the working area
void threeGauges(Bs4Page& p, int phase) {
    if (phase == 3) return;
    bs4w::barGauge(p, p.gauge[0], phase);
    bs4w::barGauge(p, p.gauge[1], phase);
    bs4w::barGauge(p, p.gauge[2], phase);
}

} // namespace biosys::bs4b
