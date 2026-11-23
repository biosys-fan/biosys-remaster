// BS4 main-page dynamics: the probe pages (exe:00439b60 InitProbePage, 00439cb0 SelectProbe, 0043a110 ReadProbeValue,
// 00439d70 DrawProbeValue, 0043a2f0 CreatePhBarSurface, 0043a4a0 ProbeBarPixels) and exe:0041ddf0 OnMainPageShown.
// Spec analysis/notes/bs4.md section 2 ("Probe page"). The page fields: probeSensor (+0x5c, index into the sensor list exe:00487c40),
// probeRow (+0x60, row of the probe-page table exe:00488048), probeLast (+0x68, the last value), phPos (+0x70) and phSurface (+0x78).
#include "engine/bs4main_priv.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace biosys {
namespace bs4main {

namespace {

std::string g_probeText;      // the text of the last exe:00439d70 (DAT_00499178)

const ProbePage* probeRow(const Bs4Page& p) {
    return p.probeRow >= 0 && static_cast<size_t>(p.probeRow) < kProbePageCount ? &kProbePages[p.probeRow] : nullptr;
}
const Sensor* probeSensor(const Bs4Page& p) {
    return p.probeSensor >= 0 && static_cast<size_t>(p.probeSensor) < kSensorCount ? &kSensors[p.probeSensor] : nullptr;
}
std::string fmt(const char* f, double v) {
    char b[64];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

// exe:0043a2f0 CreatePhBarSurface: a 3-pixel wide off-screen surface of the bar height holding the left columns of the bar ornament's
// image (PaintImage at (0,0), opaque, clipped); the position of the bar ornament goes to phPos.
void createPhBarSurface(Bs4Page& p) {
    p.phSurface = Surface();                                             // the old one is released
    const ProbePage* row = probeRow(p);
    if (!row || !row->bar[0]) return;
    for (const Ornament& o : p.rec.ornaments) {
        if (o.type != 1 || !bs4Iequals(o.name, row->bar)) continue;
        p.phPos[0] = o.x;
        p.phPos[1] = o.y;
        Surface s;
        s.init(3, row->barHeight, 0);                                    // (DDraw leaves the memory of a new surface unspecified)
        if (o.image < p.rec.images.size()) {
            const Image8& im = p.image(static_cast<int>(o.image));
            for (int y = 0; y < s.h && y < im.h; y++)
                for (int x = 0; x < s.w && x < im.w; x++) s.row(y)[x] = im.px[static_cast<size_t>(y) * im.w + x];
        }
        p.phSurface = std::move(s);
        return;
    }
}

// exe:0043a4a0 ProbeBarPixels: the height in pixels of the bar for the last value (false: no probe row / sensor selected). The `co2` page
// scales its bar against 0.1, 5 or 100 depending on the value.
bool probeBarPixels(const Bs4Page& p, u32& pixels) {
    if (p.probeSensor == -1 || p.probeRow == -1) return false;
    const ProbePage* row = probeRow(p);
    if (!row) return false;
    const double v = p.probeLast;
    double mn = row->min, mx = row->max;
    if (bs4Iequals(row->page, "co2")) {                                  // exe:0048d544
        if (v < 0.1) mx = 0.1;
        else if (v < 5.0) mx = 5.0;
        else mx = 100.0;
    }
    if (v <= mn) { pixels = 0; return true; }
    if (v >= mx) { pixels = static_cast<u32>(row->barHeight); return true; }
    pixels = static_cast<u32>(((v - mn) * 100.0 / (mx - mn)) * static_cast<double>(row->barHeight) * 0.01);
    return true;
}

} // namespace

// exe:00439b60
void initProbePage(Bs4Page& p) {
    p.probeSensor = -1;
    p.probeRow = -1;
    size_t r = 0;
    for (; r < kProbePageCount; r++)
        if (bs4Iequals(kProbePages[r].page, p.name())) break;
    if (r == kProbePageCount) return;
    p.probeRow = static_cast<int>(r);
    createPhBarSurface(p);
    if (!isProbeForecastPage(p)) {
        u32 sensor = 0, row = 0;
        if (!p.owner().loadedProbeState(sensor, row)) {                  // exe:00407720: not while a saved game is loaded
            if (kProbePages[r].defaultSensor == -1) return;
            p.probeSensor = kProbePages[r].defaultSensor;
            readProbeValue(p, 0);
            return;
        }
        if (sensor < 0x17) {
            p.probeSensor = static_cast<int>(sensor);
            readProbeValue(p, 0);
        }
        return;
    }
    for (size_t i = 0; i < kSensorCount; i++)                            // the three forecast pages: the sensor whose feature is "null"
        if (bs4Iequals(kSensors[i].feature, "null")) {                   // exe:0048d4dc
            p.probeSensor = static_cast<int>(i);
            readProbeValue(p, 0);
            return;
        }
}

// exe:00439cb0: the clicked hotspot `16_sens*` chooses the sensor
void selectProbe(Bs4Page& p, const Bs4Action& a) {
    if (p.probeRow == -1 || isProbeForecastPage(p)) return;
    for (size_t i = 0; i < kSensorCount; i++) {
        if (!bs4Iequals(kSensors[i].hotspot, a.name)) continue;
        p.probeSensor = static_cast<int>(i);
        readProbeValue(p, 0);
        repaint(p);
        return;
    }
}

// exe:0043a110. mode 0 (load / selection): read the value and keep it, a failure is logged; mode 1 (the 1 Hz tick): a failed read counts
// as 0, and the page is repainted when the text of the value changes.
void readProbeValue(Bs4Page& p, int mode) {
    const ProbePage* row = probeRow(p);
    const Sensor* s = probeSensor(p);
    if (!row || !s) return;
    const bool world = bs4Iequals(s->feature, "null");
    const std::string feature = world ? std::string() : std::string(s->feature);
    const u32 id = row->paramId;
    u32 i = 0;
    double v = 0;
    if (!getFeature(p, feature, static_cast<int>(id), i, v)) {
        if (mode != 1) {
            if (world) logf(p, "BS4: Failed to get the value of world parameter %lu.", static_cast<unsigned long>(id));
            else logf(p, "BS4: Failed to get the value of parameter %lu for BiosFear sensor '%s'.", static_cast<unsigned long>(id), s->feature);
            return;
        }
        v = 0;
    }
    if (mode != 1) {
        p.probeLast = v;
        return;
    }
    // (the format of the comparison, 0x22 -> %.2f and 0x23 -> %.3f, is not the one the value is drawn with, see drawProbeValue: it only makes
    // the tick repaint for changes in digits that are not shown)
    const char* f = id == 0x23 ? "%.3f" : id == 0x22 ? "%.2f" : "%.1f";   // exe:0048d550, 0048d548, 0048d29c
    if (fmt(f, v) != fmt(f, p.probeLast)) {
        p.probeLast = v;
        repaint(p);
    }
}

// exe:00439d70 (called by the page paint): the value text, the `co2` range mark, the bar, the chosen sensor's mark and the forecast marks
void drawProbeValue(Bs4Page& p) {
    const ProbePage* row = probeRow(p);
    const Sensor* s = probeSensor(p);
    if (!row || !s) return;
    const double v = p.probeLast;
    const char* f = "%.1f";
    if (row->paramId == 0x23) f = v < 10.0 ? "%.3f" : "%.2f";             // (exe:00439d70 picks its format apart from exe:0043a110)
    g_probeText = fmt(f, v);
    p.drawText(Bs4Rect{row->rect[0], row->rect[1], row->rect[2], row->rect[3]}, g_probeText, 0x4c, 0x4a, 0x4a, 0x25);   // exe:0x24a4a4c
    if (bs4Iequals(row->page, "co2")) {
        if (v < 0.1) p.paintOrnamentByName("rangea", nullptr, -1);
        else if (v < 5.0) p.paintOrnamentByName("rangeb", nullptr, -1);
        else p.paintOrnamentByName("rangec", nullptr, -1);
    }
    u32 n = 0;
    if (p.phSurface.w > 0 && probeBarPixels(p, n) && n > 0) {
        // Blt of the strip (0, H-n, 3, H) of the 3-pixel bar surface to the same rows at phPos
        const int h = row->barHeight, top = h - static_cast<int>(n);
        for (int y = top; y < h; y++) {
            const int dy = p.phPos[1] + y;
            if (dy < 0 || dy >= p.surf.h || y >= p.phSurface.h) continue;
            for (int x = 0; x < 3; x++) {
                const int dx = p.phPos[0] + x;
                if (dx >= 0 && dx < p.surf.w) p.surf.row(dy)[dx] = p.phSurface.row(y)[x];
            }
        }
    }
    if (!bs4Iequals(s->feature, "null")) p.paintOrnamentByName(s->ornament, nullptr, -1);     // the mark of the chosen sensor
    struct Marks { const char* page; double lo, hi; const char* a; const char* b; const char* c; };
    static const Marks kMarks[] = {{"xwind", 7.0, 14.0, "xwind1", "xwind2", "xwind3"},         // exe:0048919c ...
                                   {"xsun", 33.0, 66.0, "xsun1", "xsun2", "xsun3"},            // exe:00489194
                                   {"xtemp", 5.0, 18.0, "xtemp1", "xtemp2", "xtemp3"}};        // exe:004891a4
    for (const Marks& m : kMarks)
        if (pageIs(p, m.page)) p.paintOrnamentByName(v < m.lo ? m.a : v < m.hi ? m.b : m.c, nullptr, -1);
}

std::string probeTextNow() { return g_probeText; }

// exe:0041ddf0 OnMainPageShown: called by exe:00407c40 (verb 0x17) whenever an item of the main page changed (its `changed` argument, which the
// decompile does not show, is the flag that triggers it) or the panel was closed by bit 30, and after a saved game was loaded: the overlay
// states are built for the visible item mask. This is the only place that selects the row of the device-highlight table (exe:00439450 has no
// other caller); a page is loaded with no row item visible, so the overlays appear with the first click on one of its device tabs. A probe
// page that is not showing its probe item any more forgets its sensor.
void onMainPageShown(Bs4Page& p) {
    const u32 mask = p.visibleMask();
    buildOverlayStates(p, mask);
    if (p.probeSensor != -1 && p.probeRow != -1 && !isProbeForecastPage(p) && !(mask & 0x20000u)) {
        p.probeSensor = -1;
        p.probeRow = -1;
        p.phSurface = Surface();
    }
}

} // namespace bs4main
} // namespace biosys
