// BioSys 4 panels with the 24 h schedule slider (exe:00437470): `panelcfan` (page 54, mixer fans), `panelrain` (page 57, rain machines),
// `panelfilt` (page 67, scrubbers). Their Inits (exe:00421de0, exe:00423e60, exe:00426f20) differ in the variant table, the highlight bit
// (14 / 6 / 9), the loop sound (2 / 7 / 3) and the water-source buttons of the rain machine; the handlers (exe:00433120, exe:00434370,
// exe:00434340) run the slider and, once per second, the active indicator.
#include "engine/bs4panels_a.h"

namespace biosys {
namespace {

using namespace bs4a;

const Variant kCfan[] = {
    {3, "fs9mixer", "14_cfans1", "hicfans1"},   {4, "fs3mixer", "14_cfans2", "hicfans2"},   {5, "fs7mixer", "14_cfans3", "hicfans3"},
    {6, "fomixer1", "14_cfano1", "hicfano1"},   {7, "fomixer2", "14_cfano2", "hicfano2"},   {8, "fomixer3", "14_cfano3", "hicfano3"},
    {9, "fdbmixer1", "14_cfane1", "hicfane1"},  {10, "fdbmixer2", "14_cfane2", "hicfane2"}, {11, "fdbmixer3", "14_cfane3", "hicfane3"},
    {12, "fr9mixer", "14_cfanr1", "hicfanr1"},  {13, "fr5mixer", "14_cfanr2", "hicfanr2"},  {14, "fr2mixer", "14_cfanr3", "hicfanr3"},
};
const Variant kRain[] = {
    {3, "fs2rain", "6_rains1", "hirains1"},     {4, "fs5rain", "6_rains2", "hirains2"},     {5, "fs8rain", "6_rains3", "hirains3"},
    {6, "fo1rain", "6_raino1", "hiraino1"},     {9, "fdbrain1", "6_raine1", "hiraine1"},    {10, "fdbrain2", "6_raine2", "hiraine2"},
    {11, "fdbrain3", "6_raine3", "hiraine3"},   {12, "fr9rain", "6_rainr1", "hirainr1"},    {13, "fr6rain", "6_rainr2", "hirainr2"},
    {14, "fr1rain", "6_rainr3", "hirainr3"},
};
const Variant kFilt[] = {
    {3, "fbs5scrub", "9_fils1", "hifilts1"},    {4, "fbs3scrub", "9_fils2", "hifilts2"},    {5, "fbs2scrub", "9_fils3", "hifilts3"},
    {9, "fbd1scrub", "9_file1", "hifilte1"},    {10, "fbd4scrub", "9_file2", "hifilte2"},   {11, "fbd5scrub", "9_file3", "hifilte3"},
    {12, "fbr5scrub", "9_filr1", "hifiltr1"},   {13, "fbr2scrub", "9_filr2", "hifiltr2"},   {14, "fbr1scrub", "9_filr3", "hifiltr3"},
};

// exe:00433120 (cfan) and exe:00434340 (filt): the same handler
void runScheduleHandler(Bs4Page& p) {
    p.handler = [&p](int phase) {
        bs4w::scheduleSlider(p, phase);
        if (phase == 2) bs4w::activeIndicator(p, 0);
    };
}

// exe:00421de0
void initCfan(Bs4Page& p, int variant) {
    const Variant* v = findVariant(kCfan, sizeof kCfan / sizeof *kCfan, variant);
    if (!v) return;
    begin(p, *v, 14);
    if (broken(p, {1, 2, 0xf})) return;
    if (!onState(p)) return;
    if (!schedule(p)) return;
    bs4w::activeIndicator(p, 1);
    setLoop(p, 2, p.devActive);
    runScheduleHandler(p);
}

// exe:00426f20
void initFilt(Bs4Page& p, int variant) {
    const Variant* v = findVariant(kFilt, sizeof kFilt / sizeof *kFilt, variant);
    if (!v) return;
    begin(p, *v, 9);
    if (broken(p, {1, 2, 0xf})) return;
    if (!onState(p)) return;
    if (!schedule(p)) return;
    bs4w::activeIndicator(p, 1);
    setLoop(p, 3, p.devActive);
    runScheduleHandler(p);
}

// exe:00423e60
void initRain(Bs4Page& p, int variant) {
    const Variant* v = findVariant(kRain, sizeof kRain / sizeof *kRain, variant);
    if (!v) return;
    begin(p, *v, 6);
    if (broken(p, {1, 2, 0xf, 0x11, 0x12})) return;
    if (!onState(p)) return;
    if (!schedule(p)) return;
    if (!waterSource(p)) return;
    bs4w::activeIndicator(p, 1);
    setLoop(p, 7, p.devActive);
    p.handler = [&p](int phase) {                            // exe:00434370
        bs4w::scheduleSlider(p, phase);
        if (phase == 2) {
            pollWaterSourceReload(p);
            bs4w::activeIndicator(p, 0);
        }
    };
}

Bs4PanelRegistrar regCfan("panelcfan", Bs4PanelDef{true, initCfan, setOnFromMask, nullptr});
Bs4PanelRegistrar regFilt("panelfilt", Bs4PanelDef{true, initFilt, setOnFromMask, nullptr});
Bs4PanelRegistrar regRain("panelrain", Bs4PanelDef{true, initRain, setOnAndWaterSource, nullptr});

} // namespace
} // namespace biosys
