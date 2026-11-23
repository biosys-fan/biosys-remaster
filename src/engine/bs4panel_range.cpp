// BioSys 4 panels with value-range sliders (exe:00437e90): `panelheat` (page 56, heaters: two sliders, day and night temperatures) and
// `panelmist` (page 59, rain-forest misters: the humidity slider and the water-source buttons). Inits exe:00422e10 / exe:00423710,
// handlers exe:00434610 / exe:00434920. The original keeps a copy of the slider fields (values, drag flags, marker rectangles) per slider
// at +0x5a8 / +0x5e0 / +0x618 and copies it in and out of the one slider object around every call; RangeState is that copy.
#include "engine/bs4panels_a.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace biosys {
namespace {

using namespace bs4a;

double bitsToDouble(uint64_t b) {
    double d;
    std::memcpy(&d, &b, 8);
    return d;
}
const double kHeatScale = bitsToDouble(0x3fd1c71c71c71c72ULL);     // +0x598: degrees per pixel (40 / 144)
const double kMistScale = bitsToDouble(0x3fe638e38e38e38eULL);     // +0x670: percent per pixel (100 / 144)

const Variant kHeat[] = {
    {3, "fsheater", "4_heats1", "hiheats1"},
    {6, "foheater", "4_heato1", "hiheato1"},
    {9, "fdbheater", "4_heate1", "hiheate1"},
    {12, "frfheater", "4_heatr1", "hiheatr1"},
};
const Variant kMist[] = {
    {12, "fr9mist", "1_mistr1", "himistr1"},
    {13, "fr5mist", "1_mistr2", "himistr2"},
    {14, "fr1mist", "1_mistr3", "himistr3"},
};

// exe:00422e10
void initHeat(Bs4Page& p, int variant) {
    const Variant* v = findVariant(kHeat, sizeof kHeat / sizeof *kHeat, variant);
    if (!v) return;
    begin(p, *v, 4);
    heatDay() = RangeState();
    heatNight() = RangeState();
    if (broken(p, {1, 2, 0xf})) return;
    if (!onState(p)) return;
    if (!getClamped(p, 100, "FEATURE_DAY_ACTIVATE_TEMP", 40.0, heatDay().a)) return;
    if (!getClamped(p, 0x65, "FEATURE_DAY_DEACTIVATE_TEMP", 40.0, heatDay().b)) return;
    if (!getClamped(p, 0x66, "FEATURE_NIGHT_ACTIVATE_TEMP", 40.0, heatNight().a)) return;
    if (!getClamped(p, 0x67, "FEATURE_NIGHT_DEACTIVATE_TEMP", 40.0, heatNight().b)) return;
    if (heatDay().b < heatDay().a) {
        char buf[512];
        std::snprintf(buf, sizeof buf, "BS4: The day-time activate temperature (%.1f) is higher than the deactivate temperature (%.1f) for BiosFear feature '%s'.",
                      heatDay().a, heatDay().b, p.feature.c_str());
        p.owner().log(buf);
        return;
    }
    if (heatNight().b < heatNight().a) {
        char buf[512];
        std::snprintf(buf, sizeof buf, "BS4: The night-time activate temperature (%.1f) is higher than the deactivate temperature (%.1f) for BiosFear feature '%s'.",
                      heatNight().a, heatNight().b, p.feature.c_str());
        p.owner().log(buf);
        return;
    }
    p.range.scale = kHeatScale;                              // +0x598
    bs4w::activeIndicator(p, 1);
    setLoop(p, 3, p.devActive);
    p.handler = [&p](int phase) {                            // exe:00434610
        runRange(p, heatDay(), phase, Bs4Rect{0x47, 0x73, 0x65, 0x7d}, Bs4Rect{0x47, 0x85, 0x65, 0x8f}, 0x51, 100, 0x65, kHeatScale);
        if (p.reload) return;
        runRange(p, heatNight(), phase, Bs4Rect{0x47, 0xca, 0x65, 0xd4}, Bs4Rect{0x47, 0xdc, 0x65, 0xe6}, 0xa8, 0x66, 0x67, kHeatScale);
        if (!p.reload && phase == 2) bs4w::activeIndicator(p, 0);
    };
}

// exe:00423710
void initMist(Bs4Page& p, int variant) {
    const Variant* v = findVariant(kMist, sizeof kMist / sizeof *kMist, variant);
    if (!v) return;
    begin(p, *v, 1);
    mistRange() = RangeState();
    if (broken(p, {1, 2, 0xf, 0x11, 0x12})) return;
    if (!onState(p)) return;
    if (!getClamped(p, 0x6e, "FEATURE_ACTIVATE_HUMIDITY", 100.0, mistRange().a)) return;
    if (!getClamped(p, 0x6f, "FEATURE_DEACTIVATE_HUMIDITY", 100.0, mistRange().b)) return;
    if (mistRange().b < mistRange().a) {
        char buf[512];
        std::snprintf(buf, sizeof buf, "BS4: The activate humidity (%.1f) is higher than the deactivate humidity (%.1f) for BiosFear feature '%s'.",
                      mistRange().a, mistRange().b, p.feature.c_str());
        p.owner().log(buf);
        return;
    }
    if (!waterSource(p)) return;
    bs4w::activeIndicator(p, 1);
    setLoop(p, 1, p.devActive);
    p.handler = [&p](int phase) {                            // exe:00434920
        runRange(p, mistRange(), phase, Bs4Rect{0x47, 0x73, 0x65, 0x7d}, Bs4Rect{0x47, 0x85, 0x65, 0x8f}, 0x51, 0x6e, 0x6f, kMistScale);
        if (phase == 2) {
            pollWaterSourceReload(p);
            bs4w::activeIndicator(p, 0);
        }
    };
}

Bs4PanelRegistrar regHeat("panelheat", Bs4PanelDef{true, initHeat, setOnFromMask, nullptr});
Bs4PanelRegistrar regMist("panelmist", Bs4PanelDef{true, initMist, setOnAndWaterSource, nullptr});

} // namespace
} // namespace biosys
