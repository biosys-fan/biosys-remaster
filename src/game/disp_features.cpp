// The panel displays of the machines: CFeature vtable +0x5c (`display`) of CAlgaeScrubber, CTanksCabinet, COxygenStore,
// COceanProbe, CBiomeSensor, CLung, CHeater and the page drawn by CTideMachine. Translated from the decompiled functions
// with the arguments of the GDI calls read off the disassembly (the decompiler drops the arguments of the virtual CDC calls;
// the call inventory is analysis/notes/gdi_usage.md). They draw into the page surface of the main window (GetWindowDC(-1)),
// in the system font, in PALETTERGB colours, after the images the original shows with Eng_DisplayImage. Every number is
// right-aligned the way the original does it: GetTextExtentPointA, TextOutA(right - cx, y). Without a host (the headless
// machine tests) nothing is drawn.
#include "game/blobrecords.h"
#include "game/disp_util.h"
#include "game/biomecel.h"
#include "game/eng.h"
#include "game/feat_climate.h"
#include "game/feat_sensors.h"
#include "game/feat_water.h"
#include "game/self_text.gen.h"
#include "game/site.h"
#include "game/watertank.h"
#include <cstring>

namespace biosys::game {

using namespace disp;

namespace {

// Eng_DisplayImage(image, -1, 0, 0, -1, 0, 0) dll:100742d0; false = the engine refused (no such image)
bool showImage(FeatureHost* h, const char* image, int unk20 = -1) {
    Eng* e = h ? h->eng() : nullptr;
    return e && e->DisplayImage(image, -1, nullptr, 0, unk20, nullptr, 0);
}

// CEngineFX::DisplayImage(image, -1, 0, 0, -1, 0) dll:10060bf0 (with an explicit position 0,0)
void showImageAt0(FeatureHost* h, const char* image) {
    Eng* e = h ? h->eng() : nullptr;
    if (!e) return;
    const int xy[2] = {0, 0};
    e->DisplayImage(image, -1, xy, 0, -1, nullptr, 0);
}

// CEngineFX::PlayAnimEx dll:10060aa0 / StopAnim dll:10060b40 as the alarm icons use them: looping sprite animation of the
// panel page (window -1, 100 ms frames, play mode 1, sound mode 1). The handle variable is -1 while nothing plays.
void alarmAnim(FeatureHost* h, int& handle, bool on, const char* file, int x, int y) {
    Eng* e = h ? h->eng() : nullptr;
    if (!e) return;
    if (on) {
        if (handle != -1) return;
        int local = -1;
        const int ok = e->OpenSpriteAnimation(file, -1, x, y, 1, 0x64, true, 1, 0, "", 0, 1, 0, local, -1, 0);
        handle = local;
        if (ok) e->PlaySpriteAnimation(local, 1, 0);
    } else if (handle != -1) {
        e->StopSpriteAnimation(handle);
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// CAlgaeScrubber::Display dll:100037e0: the ocean's algae level (100 - algae) as one amber bar and its number

void CAlgaeScrubber::display(int redraw) {
    FeatureHost* h = host();
    if (!h) return;
    if (redraw != 0) showImageAt0(h, "background");           // FUN_10060bf0("background", -1, 0, 0, -1, 0)
    const bool stopped = m_bActive == 0;
    if (stopped) {
        hotspotEnable("bStart", 1);
    } else {
        showImage(h, "startin");
        hotspotEnable("bStart", 0);
    }
    hotspotEnable("bStop", stopped ? 0 : 1);
    if (cworld::hasPower(world(), m_dwPowerMask) == 0) return;
    Panel p(h);
    p.alignOwn = fixes().panelNumberAlign;
    if (!p.ok()) return;
    p.ink(kAmber);
    p.pen(3, kAmber);
    const double v = 100.0 - cworld::ocean(world()).algae();
    p.vbar(0x133, 0xa3, 0xa3 - ftol(v * 0.5));
    // ORIGINAL BUG: the number is right-aligned by the width of the "%d" string measured last, not by its own text
    p.right(0x15a, 0xa1, fmtD(v), fmtF("%0.1f", v));
}

// ---------------------------------------------------------------------------------------------------------------------
// CTideMachine::Display dll:10003f70: the two tanks' fill levels as filled boxes, the percentages and the state text

void CTideMachine::display(int redraw) {
    if (cworld::hasPower(world(), m_dwPowerMask) == 0) return;
    if (redraw != 0) redrawPage();
    FeatureHost* h = host();
    if (!h) return;
    if (m_bActive != 0) showImage(h, "on");
    Panel p(h);
    if (!p.ok()) return;
    p.ink(paletteRgb(0x31, 0x49, 0x7c));
    const u32 box = paletteRgb(15, 30, 95);
    auto level = [&](const CWaterTank& t, int l, int r, int numRight) {
        double f = t.waterVol() / t.volume();            // Clamp(&f, 0.0, 1.0)
        if (f < 0.0) f = 0.0; else if (f > 1.0) f = 1.0;
        GdiRect rc{l, 0x73 + ftol((1.0 - f) * 48.0), r, 0xa2};
        p.c->fillSolidRect(rc, box);
        f *= 100.0;
        p.right(numRight, 0xa2, fmtD(f));                // sprintf("%d", ftol(f)): measured and drawn
    };
    if (m_pTideTank) level(*m_pTideTank, 0x128, 0x157, 0x14a);
    level(cworld::ocean(world()), 0x16e, 0x1ca, 0x1ce);
    // the state text: string 0x143 (stopped), 0x142 / 0x141 by the direction flag
    const int id = m_bActive == 0 ? 0x143 : (m_dwDirection == 0 ? 0x142 : 0x141);
    p.g->textOut(0x11b, 0xda, dllString(id));
}

// ---------------------------------------------------------------------------------------------------------------------
// CTanksCabinet::Display dll:100047b0: bar graphs of the cabinet's tanks; which ones depends on the page (the last letter of
// the player's location: "fbptanks_1", "fbptanks_2", ...S)

void CTanksCabinet::display(int redraw) {
    if (redraw != 0) redrawPage();
    if (cworld::hasPower(world(), m_dwPowerMask) == 0) return;
    FeatureHost* h = host();
    if (!h) return;
    const std::string& loc = globals().currentSiteName;           // DAT_100c1e24
    const char last = loc.empty() ? '\0' : loc.back();
    Panel p(h);
    p.alignOwn = fixes().panelNumberAlign;
    if (!p.ok()) return;
    p.ink(kAmber);
    // a temperature bar in the thin pen: MoveTo(x, yFrom); LineTo(x, yBase - temp); the number "%0.2f" right-aligned
    auto tempBar = [&](const CWaterTank& t, int x, int yFrom, int yBase, int right, int ty) {
        p.vbar(x, yFrom, yBase - ftol(t.temp()));
        p.right(right, ty, fmtD(t.temp()), fmtF("%0.2f", t.temp()));      // ORIGINAL BUG: aligned by the "%d" width
    };
    // a percentage bar in the thick pen, half a pixel per percent: the number "%0.1f"
    auto pctBar = [&](double v, int x, int yBase, int right, int ty) {
        const int i = ftol(v);
        p.vbar(x, yBase, yBase - i / 2);
        p.right(right, ty, fmtD(v), fmtF("%0.1f", v));                   // ORIGINAL BUG: aligned by the "%d" width
    };
    auto fill = [&](const CWaterTank& t) { return t.waterVol() / t.volume() * 100.0; };
    auto purity = [&](const CWaterTank& t) { return t.purity() * 100.0; };
    CWaterTank* t0 = m_pTank[0]; CWaterTank* t1 = m_pTank[1]; CWaterTank* t2 = m_pTank[2]; CWaterTank* t3 = m_pTank[3];
    if (last == '1' && t0 && t1) {
        p.pen(1, kAmber);
        tempBar(*t0, 0xa6, 0x91, 0x87, 0xc4, 0x8f);
        tempBar(*t1, 0x1a0, 0x122, 0x118, 0x1be, 0x120);
        p.pen(3, kAmber);
        pctBar(fill(*t0), 0x19e, 0x91, 0x1c4, 0x8f);
        pctBar(fill(*t1), 0xa4, 0x122, 0xca, 0x120);
        pctBar(purity(*t0), 0x121, 0x91, 0x147, 0x8f);
        pctBar(purity(*t1), 0x121, 0x122, 0x147, 0x120);
    } else if (last == '2' && t2 && t3) {
        if (strcasecmp(loc.c_str(), "fbptanks_2") == 0) {
            p.pen(1, kAmber);
            tempBar(*t2, 0xad, 0x9f, 0x95, 0xcb, 0x9d);
            p.pen(3, kAmber);
            pctBar(fill(*t2), 0x18f, 0x9f, 0x1b6, 0x9d);
            pctBar(fill(*t3), 0x11f, 0x127, 0x146, 0x125);
            pctBar(purity(*t2), 0x11f, 0x9f, 0x146, 0x9d);
            pctBar(purity(*t3), 0xaa, 0x127, 0xe0, 0x125);
        } else {
            p.pen(1, kAmber);
            tempBar(*t2, 0x81, 0x96, 0x8c, 0x9f, 0x94);
            p.pen(3, kAmber);
            pctBar(fill(*t2), 0x163, 0x96, 0x18a, 0x94);
            pctBar(fill(*t3), 0xf3, 0x11e, 0x11a, 0x11c);
            pctBar(purity(*t2), 0xf3, 0x96, 0x11a, 0x94);
            pctBar(purity(*t3), 0x7e, 0x11e, 0xb4, 0x11c);
        }
    } else if (last == 'S' && t3) {
        // the last tank holds more than 1/36 of its capacity: the FX of id 0xfdc is on
        cworld::setFXState(world(), 0xfdc, t3->volume() * 0.027777777777777776 < t3->waterVol() ? 1 : 0);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// COxygenStore::Display dll:10025580: the siphon state images, the "low" / "empty" / "FULL" lamps, two bars

void COxygenStore::display(int redraw) {
    FeatureHost* h = host();
    if (!h) return;
    if (redraw != 0) showImage(h, "background");
    if (m_bActive != 0) {
        if (m_wMode == 1) {
            showImage(h, "siphin"); showImage(h, "res_in"); showImage(h, "chem_out");
            hotspotEnable("bSiphIn", 0);
            hotspotEnable("bSiphOut", 1);
        } else if (m_wMode == 2) {
            showImage(h, "siphout"); showImage(h, "res_out"); showImage(h, "chem_in");
            hotspotEnable("bSiphIn", 1);
            hotspotEnable("bSiphOut", 0);
        }
        // (any other mode: no images; the original goes straight on to the power test)
    } else {
        showImage(h, "SiphStop");
        hotspotEnable("bSiphIn", 1);
        hotspotEnable("bSiphOut", 1);
        hotspotEnable("bSiphStop", 0);
    }
    if (cworld::hasPower(world(), m_dwPowerMask) == 0) return;
    if (m_pCel && m_pCel->oxygen() < 15.0) showImage(h, "low");
    if (m_dStore == 0.0) showImage(h, "empty");                     // (the original tests the double's bits without the sign)
    if (m_dStore == 10.0) showImage(h, "FULL");
    Panel p(h);
    p.alignOwn = fixes().panelNumberAlign;
    if (!p.ok()) return;
    p.ink(kAmber);
    p.pen(3, kAmber);
    const double s = m_dStore * 10.0;
    p.hbar(0x69, 0x7f, 0x69 + ftol(s * 0.5));
    p.right(0xbb, 0x6e, fmtD(s), fmtF("%0.2f", s));                 // ORIGINAL BUG: aligned by the "%d" width
    if (!m_pCel) return;                                            // (the original dereferences it)
    double o = m_pCel->oxygen();
    const double shown = o < 25.0 ? o : 25.0;                       // min(oxygen, 25.0)
    p.hbar(0x69, 0xf6, 0x69 + ftol(shown * 2.0));
    p.right(0xbb, 0xe4, fmtD(o), fmtF("%0.2f", o));                 // ORIGINAL BUG: aligned by the "%d" width
}

// ---------------------------------------------------------------------------------------------------------------------
// COceanProbe::EnterPage dll:10027d90 and Display dll:10028090

void COceanProbe::onEnterPage(const std::string& page) {
    FeatureHost* h = host();
    if (h && h->eng()) h->eng()->EnableUserInput(0, 0);              // Eng_Api2E_EnableUserInput(0, 0)
    if (cworld::hasPower(world(), m_dwPowerMask) != 0 && h && h->eng())
        h->eng()->SetImageVisibility("background", -1, 1);           // CEngineFX::SetImageVisibility("background", 1, -1)
    CFeature::onEnterPage(page);
}

void COceanProbe::display(int) {
    FeatureHost* h = host();
    if (!h) return;
    if (cworld::hasPower(world(), m_dwPowerMask) == 0) return;
    showImage(h, "background");
    Panel p(h);
    if (!p.ok()) return;
    p.ink(kAmber);
    p.pen(1, kAmber);
    {   // temperature
        const double v = m_dTempReading;
        p.vbar(0xd0, 0xbf, 0xb5 - ftol(v));
        p.right(0xfa, 0xbe, fmtF("%0.1f", v));
    }
    p.pen(3, kAmber);
    {   // clarity
        const double v = m_dClarityReading;
        p.vbar(0x129, 0xbf, 0xbf - ftol(v * 0.5));
        p.right(0x151, 0xbe, fmtF("%0.1f", v));
    }
    {   // salinity
        const double v = m_dSalinityReading;
        p.vbar(0x17e, 0xbf, 0xbf - ftol(v * 5.0));
        p.right(0x1a5, 0xbe, fmtF("%0.2f", v));
    }
    {   // water level
        const double v = m_dLevelReading;
        p.vbar(0xd3, 0x11c, 0x11c - ftol(v * 0.5));
        p.right(0xfa, 0x11b, fmtF("%0.1f", v));
    }
    {   // oxygen
        const double v = m_dOxygenReading;
        p.vbar(0x129, 0x11c, 0x11c - ftol(v * 10.0));
        p.right(0x150, 0x11b, fmtF("%0.2f", v));
    }
    {   // pH
        const double v = m_dPHReading;
        p.vbar(0x17e, 0x11c, 0x11c - ftol((v - 3.0) * 5.0));
        p.right(0x1a4, 0x11b, fmtF("%0.2f", v));
    }
    p.g.reset();                                                     // Eng_ReleaseWindowDC
    // the alarm icons (anims\osensor\*.tan): on while the alarm bit is set, stopped otherwise
    struct Icon { u32 mask; const char* file; int x, y; };
    static const Icon icons[11] = {
        {0x10000, "anims\\osensor\\hot.tan", 0xe5, 0x8f},    {0x20000, "anims\\osensor\\cold.tan", 0xe5, 0xa9},
        {0x40000, "anims\\osensor\\murky.tan", 0x145, 0xb8}, {0x80000, "anims\\osensor\\salty.tan", 0x19a, 0x9e},
        {0x100000, "anims\\osensor\\fresh.tan", 0x19a, 0xb8}, {0x200000, "anims\\osensor\\high.tan", 0xec, 0xed},
        {0x400000, "anims\\osensor\\low.tan", 0xec, 0x115},  {0x400, "anims\\osensor\\high.tan", 0x14d, 0xef},
        {0x800, "anims\\osensor\\low.tan", 0x14d, 0xf8},     {0x10, "anims\\osensor\\alkali.tan", 0x19a, 0xf2},
        {0x20, "anims\\osensor\\acid.tan", 0x19a, 0x115}};
    for (int i = 0; i < 11; i++) alarmAnim(h, m_hAlarmAnim[i], (m_dwAlarms & icons[i].mask) != 0, icons[i].file, icons[i].x, icons[i].y);
}

// ---------------------------------------------------------------------------------------------------------------------
// CBiomeSensor::Display dll:1005e9a0. On the sensor's own panel (+0x54 set): the background images, eleven bars and numbers
// in white, the world's power bar in red, the alarm icons and a random beep; on any other page: only the lamp images.

void CBiomeSensor::display(int) {
    FeatureHost* h = host();
    if (!h) return;
    if (unk_54 == 0) {
        m_hAlarmAnim.fill(-1);                                       // the animation handles are forgotten (not stopped)
        imgVis("sens_alarm", 0);
        imgVis("sens_power", 0);
        if (cworld::hasPower(world(), m_dwPowerMask) != 0) {
            hotspotEnable("b_open", 1);
            imgVis("sens_power", 1);
            if (m_dwAlarms != 0) imgVis("sens_alarm", 1);
        } else {
            hotspotEnable("b_open", 0);
        }
        return;
    }
    if (cworld::hasPower(world(), m_dwPowerMask) == 0) return;
    for (const char* im : {"background", "name", "number", "reddot", "rh_levels", "sw_levels", "at_levels", "st_levels"}) showImage(h, im);
    if (m_dCO2Reading < 0.1) { showImage(h, "CO2_01_t"); showImage(h, "CO2_01_b"); }
    else if (m_dCO2Reading < 5.0) { showImage(h, "CO2_02_t"); showImage(h, "CO2_02_b"); }
    else { showImage(h, "CO2_03_t"); showImage(h, "CO2_03_b"); }
    Panel p(h);
    if (!p.ok()) return;
    const u32 red = paletteRgb(180, 12, 12);
    p.ink(kWhite);
    p.pen(1, kWhite);
    auto bar2 = [&](double v, int x, int y0, int y1, int right, int ty) {       // "%d" measured, "%0.2f" drawn
        p.vbar(x, y0, y1);
        p.right(right, ty, fmtD(v), fmtF("%0.2f", v));
    };
    bar2(m_dAirTempReading, 0x18e, 0x6c, 0x62 - ftol(m_dAirTempReading), 0x17b, 0x8b);
    bar2(m_dSoilTempReading, 0x221, 0x6c, 0x62 - ftol(m_dSoilTempReading), 0x1fe, 0x8b);
    p.pen(2, kWhite);
    {   // light: the number only
        const std::string t = fmtD(m_dLightReading);
        p.g->textOut(0x58 - p.g->textWidth(t), 0x135, t);
    }
    {   // wind speed ("%0d" measured), rain today
        p.right(0x10e, 0x135, fmtD(m_dWindSpeedReading), fmtF("%0.1f", m_dWindSpeedReading));
        p.right(0xab, 0x135, fmtD(m_dRainToday), fmtF("%0.1f", m_dRainToday));
    }
    {   // humidity
        const double v = m_dHumidityReading;
        p.vbar(0xd6, 0xed, 0xed - ftol(v * 0.5));
        p.right(0xf6, 0xa4, fmtD(v), fmtF("%0.1f", v));
    }
    {   // pressure: the "%d" text itself
        const double v = m_dPressureReading;
        p.vbar(0x12a, 0x6c, 0x6c - ftol((v - 800.0) * 0.125));
        const std::string t = fmtD(v);
        p.g->textOut(0x12c - p.g->textWidth(t), 0x7e, t);
    }
    {   // pollution
        const double v = m_dPollutionReading;
        const double shown = v < 20.0 ? v : 20.0;
        p.vbar(0xd6, 0x6c, 0x6c - ftol(shown * 2.5));
        p.right(0xd3, 0x7e, fmtD(v), fmtF("%0.1f", v));
    }
    {   // CO2: the scale steps with the level
        const double v = m_dCO2Reading;
        const double scale = v < 0.1 ? 500.0 : (v < 5.0 ? 10.0 : 0.5);
        p.vbar(0x12a, 0xed, 0xed - ftol(v * scale));
        p.right(0x13a, 0xa4, fmtD(v), fmtF("%0.3f", v));
    }
    {   // oxygen
        const double v = m_dOxygenReading;
        const double shown = v < 25.0 ? v : 25.0;
        p.vbar(0x184, 0xed, 0xed - ftol(shown * 2.0));
        p.right(0x19e, 0xa4, fmtD(v), fmtF("%0.2f", v));
    }
    {   // soil water
        const double v = m_dSoilWaterReading;
        const double shown = v < 50.0 ? v : 50.0;
        p.vbar(0x217, 0xed, 0xed - ftol(shown));
        p.right(0x22c, 0xa4, fmtD(v), fmtF("%0.1f", v));
    }
    p.pen(2, red);
    {   // the world's stored power (DAT_100c1130 = CWorld +0x1d0)
        const double v = cworld::header(world()).m_dPower;
        p.hbar(0x16a, 0x140, 0x16a + ftol(v));
        p.right(0x226, 0x120, fmtD(v));
    }
    p.g.reset();                                                     // Eng_ReleaseWindowDC
    struct Icon { u32 mask; const char* file; int x, y; };
    static const Icon icons[13] = {
        {0x1, "anims\\sensor\\hot.tan", 0x1a8, 0x35},       {0x2, "anims\\sensor\\cold.tan", 0x1a5, 0x60},
        {0x2000, "anims\\sensor\\high.tan", 0xf0, 0xbc},    {0x4000, "anims\\sensor\\low.tan", 0xf1, 0xea},
        {0x2000000, "anims\\sensor\\high.tan", 0x143, 0x3c}, {0x4000000, "anims\\sensor\\low.tan", 0x145, 0x66},
        {0x1000, "anims\\sensor\\pollute.tan", 0xf5, 0x3d}, {0x100, "anims\\sensor\\high.tan", 0x150, 0xba},
        {0x800, "anims\\sensor\\low.tan", 0x1ab, 0xd9},     {0x40, "anims\\sensor\\high.tan", 0x23c, 0xbd},
        {0x80, "anims\\sensor\\low.tan", 0x23e, 0xe7},      {0x4, "anims\\sensor\\hot.tan", 0x23b, 0x35},
        {0x8, "anims\\sensor\\cold.tan", 0x238, 0x60}};
    for (int i = 0; i < 13; i++) alarmAnim(h, m_hAlarmAnim[i], (m_dwAlarms & icons[i].mask) != 0, icons[i].file, icons[i].x, icons[i].y);
    // the beep: sounds\features\sensblp<n>.wav for n = ftol(RandRange(1.0, 6.0)) when n <= 3
    const int n = ftol(world().state().rng.randRange(1.0, 6.0));
    if (n <= 3) playSound(("sounds\\features\\sensblp" + std::to_string(n) + ".wav").c_str());
}

// ---------------------------------------------------------------------------------------------------------------------
// CLung::Display dll:100102f0 (a CBiomeSensor): five bars of the lung's cel

void CLung::display(int redraw) {
    if (redraw != 0) redrawPage();
    if (cworld::hasPower(world(), m_dwPowerMask) == 0) return;
    FeatureHost* h = host();
    Panel p(h);
    if (!p.ok()) return;
    p.ink(kAmber);
    p.pen(1, kAmber);
    {   // air temperature
        const double v = m_dAirTempReading;
        p.vbar(0x1a4, 0xb3, 0xa9 - ftol(v));
        p.right(0x1c2, 0xb1, fmtD(v), fmtF("%0.2f", v));
    }
    p.pen(3, kAmber);
    if (m_pCel) {   // the cel's air amount in percent
        const double v = m_pCel->airVolume() * 100.0;
        const int i = ftol(v);
        p.vbar(0x12a, 0xb3, 0xb3 - i);
        p.right(0x150, 0xb1, fmtD(v), fmtF("%0.1f", v));
    }
    {   // pollution
        const double v = m_dPollutionReading;
        const double shown = v < 20.0 ? v : 20.0;
        p.vbar(0xb1, 0xb3, 0xb3 - ftol(shown * 2.5));
        p.right(0xd8, 0xb1, fmtD(v), fmtF("%0.1f", v));
    }
    {   // CO2 (horizontal)
        const double v = m_dCO2Reading;
        const double scale = v < 0.1 ? 500.0 : (1.0 <= v ? 0.5 : 50.0);
        p.hbar(0xb0, 0x107, 0xb0 + ftol(v * scale));
        p.right(0xf8, 0xf5, fmtD(v), fmtF("%0.3f", v));
    }
    {   // oxygen (horizontal)
        const double v = m_dOxygenReading;
        const double shown = v < 25.0 ? v : 25.0;
        p.hbar(0x15e, 0x107, 0x15e + ftol(shown * 2.0));
        p.right(0x1b0, 0xf5, fmtD(v), fmtF("%0.2f", v));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// CHeater::Display dll:10016e70: the state image by the site's state, then the biome's probe temperature and the four set-points

void CHeater::display(int) {
    FeatureHost* h = host();
    if (!h) return;
    siteStateImages();                                               // FUN_10019b70
    const CSite* site = h->currentSite();
    const bool dead = site && site->state() == 1;
    if (m_bRunning != 0) {
        if (!showImage(h, "active")) showImage(h, dead ? "dactive" : "hactive");
    } else {
        if (!showImage(h, "blank")) showImage(h, dead ? "dblank" : "hblank");
    }
    if (cworld::hasPower(world(), m_dwPowerMask) == 0) return;
    Panel p(h);
    if (!p.ok()) return;
    p.ink(kAmber);
    if (m_pBiome) p.right(0x146, 0x30, fmtF("%0.1f", biomeAvgProbeAirTemp(*m_pBiome)));
    p.right(0x109, 0x93, fmtF("%0.1f", m_dDayOn));
    p.right(0x19a, 0x93, fmtF("%0.1f", m_dDayOff));
    p.right(0x109, 0x109, fmtF("%0.1f", m_dNightOn));
    p.right(0x19a, 0x109, fmtF("%0.1f", m_dNightOff));
}

// dll:1005fba0 (CBiomeSensor and CLung, vtable +0xa0) and dll:10028a30 (COceanProbe): the engine closed a sprite animation (mask 2).
// When it is one of the alarm icons its handle variable becomes -1 again, which is what lets the icon be started the next time
// (alarmAnim above starts only on -1; the original's variables are DLL globals, DAT_100bbd68.. and DAT_100a3128..).
// PORT: missing until 2026-11-22, so an alarm icon that had been stopped once, or whose page had been left, never played again.
bool CBiomeSensor::onMediaClosed(int handle, unsigned mask) {
    if (mask != 2) return false;
    for (int& a : m_hAlarmAnim)
        if (a == handle) { a = -1; return true; }
    return false;
}

bool COceanProbe::onMediaClosed(int handle, unsigned mask) {
    if (mask != 2) return false;
    for (int& a : m_hAlarmAnim)
        if (a == handle) { a = -1; return true; }
    return false;
}

} // namespace biosys::game
