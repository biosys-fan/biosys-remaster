// The player's message strip and status words on the top bar (window CSelf+0x328 = the "topbar" popup, 0). Colours, positions and
// the erase / redraw order are those of CSelf::ShowMessage dll:1006bc40: the strip is OPAQUE text on a black background; a word whose
// state is "good" is erased with FillRect (health, temperature) or drawn in the background colour (energy, nutrition, hydration).
#include "game/bg_display.h"
#include "game/story.h"
#include <cmath>

namespace biosys::game {

namespace {
constexpr u32 kBlack = 0x02000000u;       // PALETTERGB(0, 0, 0): the background
constexpr u32 kRed = 0x021c19ffu;         // PALETTERGB(255, 25, 28)
constexpr u32 kGrey = 0x027f7f7fu;        // PALETTERGB(127, 127, 127)
constexpr u32 kMessage = 0x02d2dce1u;     // PALETTERGB(225, 220, 210)
}

// dll:1006bc40: the status words (the DC is opened per word here; the original keeps one DC for the whole call)
void GdiPlayerDisplay::showStatusWord(const char* stat, int state, const std::string& text) {
    const std::string s(stat);
    int x = 0;
    if (s == "health") x = 0x21a;
    else if (s == "energy") x = 0x121;
    else if (s == "nutrition") x = 0x22;
    else if (s == "hydration") x = 0xa2;
    else if (s == "temperature") x = 0x19e;
    else return;
    GdiSession dc(g_.eng_, g_.self().m_lTopBarHandle, g_.fx.systemFont, [this](int tx, int ty, const std::string& t) { g_.story_->uiRecordText(tx, ty, t); });
    if (!dc.ok()) return;
    Canvas& c = dc.dc();
    c.setBkMode(kOpaque);
    c.setBkColor(kBlack);
    if (s == "health" && state == 3) { c.fillRect({0x21a, 0x12, 0x27e, 0x26}, kBlack); return; }
    if (s == "temperature" && state == 3) { c.fillRect({0x19e, 0x12, 0x1d0, 0x26}, kBlack); return; }
    u32 colour = kBlack;
    if (s == "temperature") colour = (state == 1 || state == 5) ? kRed : kGrey;     // 1 and 5 are the extremes
    else if (state == 1) colour = kRed;
    else if (state == 2) colour = kGrey;
    c.setTextColor(colour);
    dc.textOut(x, 0x12, text);
}

// the message line: cleared first when it is another message than the one drawn last, centred at x = 0x140 - width / 2, y = 0x24
void GdiPlayerDisplay::drawLine(int stringId, const std::string& text) {
    GdiSession dc(g_.eng_, g_.self().m_lTopBarHandle, g_.fx.systemFont, [this](int tx, int ty, const std::string& t) { g_.story_->uiRecordText(tx, ty, t); });
    if (!dc.ok()) return;
    Canvas& c = dc.dc();
    c.setBkMode(kOpaque);
    c.setBkColor(kBlack);
    if (stringId == 0xffff) {                                // FillRect(0, 0x24, 0x27f, 0x37)
        c.fillRect({0, 0x24, 0x27f, 0x37}, kBlack);
        lastDrawn_.clear();
        return;
    }
    c.setTextColor(kMessage);
    if (text != lastDrawn_) {
        c.fillRect({0, 0x24, 0x27f, 0x37}, kBlack);
        lastDrawn_ = text;
    }
    dc.textOutCentred(0x140, 0x24, text);
}

void GdiPlayerDisplay::showMessage(int stringId, const std::string& text) {
    LoggingPlayerDisplay::showMessage(stringId, text);       // the record: "player-message <id> <text>"
    drawLine(stringId, text);
}

void GdiPlayerDisplay::redrawMessage(int stringId, const std::string& text) { drawLine(stringId, text); }

// ---------------------------------------------------------------------------------------------------------------------------------
// The two status screens (CSelf::ShowStatus dll:10068f90, CSelf::ShowEnvironmentStatus dll:1006aa80), drawn on the DC of the engine's sim-status
// window (API 0x4c). Colours are the COLORREFs of the DLL (PALETTERGB); positions are TextOutA / MoveTo / LineTo / Ellipse / Pie arguments of the
// decompiled code (the MoveTo / TextOut arguments the decompiler dropped were read from the disassembly).
namespace {

enum Align { kLeft, kRightAt, kCentre620 };
struct TextSpec {
    const char* area;
    u32 colour;       // 0: the DC's text colour as it is (set by an earlier text or statusTextColour)
    int x, y;
    Align align;      // kRightAt: TextOutA(x - cx, y); kCentre620: TextOutA(ftol(cx * -0.5 + 620.0), y)
};
const TextSpec kTexts[] = {
    // ShowStatus: the parts of the day, the buttons, the stat numbers, the sleep and journey columns
    {"daypartNight", 0x02293131u, 0x234, 0x00, kLeft},   {"daypartDay", 0x02293131u, 0x239, 0x3d, kLeft},
    {"daypartDawn", 0x02293131u, 0x25c, 0x1f, kLeft},    {"daypartDusk", 0x02293131u, 0x20d, 0x1f, kLeft},
    {"daypartNowDawn", 0x028cad9cu, 0x25c, 0x1f, kLeft}, {"daypartNowDay", 0x028cad9cu, 0x239, 0x3c, kLeft},
    {"daypartNowDusk", 0x028cad9cu, 0x20d, 0x1f, kLeft}, {"daypartNowNight", 0x028cad9cu, 0x234, 0x00, kLeft},
    {"sleepButton", 0x028cad9cu, 0x1bb, 0x03, kLeft},    {"restButton", 0x028cad9cu, 0x1be, 0x1d, kLeft},
    {"energyValue", 0x021818ffu, 0x1f, 0x47, kRightAt},  {"stimulus", 0x02fa6000u, 0x75, 0x34, kLeft},
    {"overloaded", 0x023163ffu, 0x8f, 0x86, kLeft},      {"healthValue", 0x02ffffffu, 0, 0x5f, kCentre620},
    {"lastDrank", 0x02ffce31u, 0x1bb, 0x155, kRightAt},  {"lastAte", 0x02319cceu, 0x22e, 0x155, kRightAt},
    {"wokeUp", 0x02636300u, 0xa8, 0xa4, kRightAt},       {"slept", 0x02636300u, 0xa8, 0xb9, kRightAt},
    {"wakeReason", 0x02636300u, 0x9e, 0xd0, kRightAt},   {"sleepEnergy", 0x02636300u, 0xe2, 0xe3, kRightAt},
    {"journeyAgo", 0x02636300u, 0xa8, 0x116, kRightAt},  {"journeyCost", 0x02636300u, 0xa8, 0x12b, kRightAt},
    {"journeyTime", 0x02636300u, 0xa8, 0x140, kRightAt}, {"journeyEnergy", 0x02636300u, 0xe2, 0x155, kRightAt},
    // ShowEnvironmentStatus
    {"plantName", 0x02cfcca1u, 6, 0x1e, kLeft},          {"plantState", 0, 0x32, 0x31, kLeft},
    {"plantFruit", 0x02908573u, 0x32, 0x9c, kLeft},      {"plantTemp", 0, 0x32, 0xe2, kLeft},
    {"daysSinceRain", 0x02fa6000u, 0x82, 0x103, kRightAt}, {"stomata", 0, 0x32, 0x13e, kLeft},
    {"cooling", 0x02fa6000u, 0xbb, 0x13d, kLeft},        {"biomePercent", 0x02cfcca1u, 0x27a, 0x31, kRightAt},
    {"biomeName", 0x02cfcca1u, 0x206, 0x31, kLeft},      {"airTemp", 0, 0x206, 0xe2, kLeft},
    {"soilWater", 0, 0x206, 0xf4, kLeft},                {"wind", 0, 0x206, 0x106, kLeft},
    {"pollution", 0, 0x206, 0x118, kLeft},               {"humidity", 0x02cfcca1u, 0x206, 0x12a, kLeft},
};

}  // namespace

// The sim-status window's DC for the length of a screen (Eng_GetReleaseSimStatusDC(1) .. (0)); the system font selected, background transparent.
bool GdiPlayerDisplay::statusScreenOpen(const char* screen, bool withPlant) {
    Eng& eng = g_.eng_;
    const std::string s(screen);
    const bool status = s == "status";
    const Pal* pal = g_.g.haveStatusPalette ? &g_.g.statusPalette : nullptr;
    if (status) {
        const int origin[2] = {0, 0};
        eng.DisplayImage("blackbar", g_.self().m_lTopBarHandle, origin, 0, -1, nullptr, 0);   // the strip of the top bar goes black
        eng.DisplaySimStatusImage("person_bk", origin);
        eng.ShowSimStatusWindow(1, pal ? pal->data() : nullptr, 1, -1);
    } else {
        eng.DisplaySimStatusImage("site_bk", nullptr);
        if (withPlant) eng.DisplaySimStatusImage("plant_bk", nullptr);
    }
    simDc_ = 0;
    if (!eng.GetSimStatusDC(simDc_)) {
        simDc_ = 0;
        if (status) eng.ShowSimStatusWindow(0, pal ? pal->data() : nullptr, 1, -1);
        return false;
    }
    sc_ = eng.api().canvas(simDc_);
    if (!sc_) {
        eng.ReleaseSimStatusDC(simDc_);
        simDc_ = 0;
        return false;
    }
    oldPen_ = sc_->pen();
    oldBrush_ = sc_->brush();
    oldFont_ = sc_->selectFont(g_.fx.systemFont);
    sc_->setBkMode(kTransparent);
    energyX_ = 0;
    symptomY_ = 0x85;
    problems_ = 0;
    if (status) sc_->setTextColor(0x02293131u);
    else if (withPlant) selectPenBrush(0x0259d1f7u);                 // the plant health gauge's colour
    return true;
}

void GdiPlayerDisplay::selectPenBrush(u32 colour) {
    Pen p;
    p.color = colour;
    Brush b;
    b.color = colour;
    sc_->selectPen(p);
    sc_->selectBrush(b);
}

void GdiPlayerDisplay::statusTextColour(unsigned colorRef) {
    if (sc_) sc_->setTextColor(colorRef);
}

void GdiPlayerDisplay::showText(const char* area, const std::string& text) {
    if (!sc_) return;
    const std::string a(area);
    const TextSpec* spec = nullptr;
    for (const TextSpec& t : kTexts)
        if (a == t.area) { spec = &t; break; }
    int x = 0, y = 0;
    if (a == "symptom") {                                            // x = 0x197, one line each from y = 0x85, 0x10 apart
        sc_->setTextColor(0x021414c8u);                              // SetTextColor(200, 20, 20) before the loop
        x = 0x197;
        y = symptomY_;
        symptomY_ += 0x10;
    } else if (a == "plantProblem") {                                // x = 0x32, from y = 0x54, 0x12 apart (the colour was set by statusTextColour)
        x = 0x32;
        y = 0x54 + problems_ * 0x12;
        problems_++;
    } else if (!spec) {
        return;
    } else {
        if (spec->colour) sc_->setTextColor(spec->colour);
        x = spec->x;
        y = spec->y;
    }
    auto draw = [&](int tx) {
        g_.story_->uiRecordText(tx, y, text);
        sc_->textOut(tx, y, text);
    };
    if (spec && spec->align == kRightAt) draw(x - sc_->textExtent(text).cx);
    else if (spec && spec->align == kCentre620) draw(static_cast<int>(sc_->textExtent(text).cx * -0.5 + 620.0));
    else draw(x);
}

// A gauge: pen and brush of its colour, MoveTo(start), LineTo(start + pixels) and a 5 x 5 dot at the end (Ellipse(x - 2, y - 2, x + 3, y + 3)).
void GdiPlayerDisplay::statusScreenGauge(const char* name, double, int pixels) {
    if (!sc_) return;
    const std::string n(name);
    auto dot = [&](int x, int y) { sc_->ellipse(x - 2, y - 2, x + 3, y + 3); };
    // horizontal bar from (x0, y) to (x0 + pixels, y) and the dot
    auto bar = [&](int x0, int y) {
        sc_->moveTo(x0, y);
        sc_->lineTo(x0 + pixels, y);
        dot(x0 + pixels, y);
    };
    // vertical bar from (x, base) up to (x, base - pixels) and the dot
    auto column = [&](int x, int base) {
        sc_->moveTo(x, base);
        sc_->lineTo(x, base - pixels);
        dot(x, base - pixels);
    };
    if (n == "energy") {
        selectPenBrush(0x023163ffu);
        bar(0x25, 0x51);
        energyX_ = 0x25 + pixels;
        selectPenBrush(0x0200006bu);                                 // the dark red line from the bar down to the weight bar
        sc_->moveTo(energyX_, 0x53);
        sc_->lineTo(energyX_, 0x68);
    } else if (n == "weight") {
        selectPenBrush(0x029c9c63u);
        bar(0x25, 0x68);
        selectPenBrush(0x0200006bu);
        dot(energyX_, 0x68);                                         // where the energy line ends
    } else if (n == "health") {
        selectPenBrush(0x0273efa5u);
        bar(0x192, 0x69);
    } else if (n == "temperature") {
        selectPenBrush(0x02ffffffu);
        const int y = 0x109 - pixels;
        sc_->moveTo(0x11e, y);
        dot(0x11e, y);
    } else if (n == "hydration") {
        selectPenBrush(0x02ffce31u);
        column(0x196, 0x165);
    } else if (n == "nutrition") {
        selectPenBrush(0x02319cceu);
        column(0x208, 0x165);
    } else if (n == "protein") {
        selectPenBrush(0x02ffceceu);
        column(0x27a, 0x165);
    } else if (n == "plantHealth") {
        bar(0x32, 0x4c);
    } else if (n == "leafTemp") {
        selectPenBrush(0x02d2dce1u);
        sc_->moveTo(0x32 + pixels, 0xdb);
        dot(0x32 + pixels, 0xdb);
    } else if (n == "waterAvailability") {
        bar(0x32, 0x132);
    } else if (n == "transpiration") {
        bar(0xbb, 0x132);
    } else if (n == "photosynthesis") {
        selectPenBrush(0x029ed1dcu);
        bar(0xbb, 0x167);
    } else if (n == "biomeHealth") {
        selectPenBrush(0x0259d1f7u);
        bar(0x206, 0x29);
    } else if (n == "oceanHealth" || n == "celPlantHealth") {
        bar(0x206, 0xda);
    }
}

// The time-of-day pie: centre (580, 40), radius 17; the sector between the angles t * 2 pi / 86400 +- pi / 6 (Pie from the first to the second).
void GdiPlayerDisplay::statusScreenClock(int timeOfDay) {
    if (!sc_) return;
    Pen p;
    p.color = 0x0246574e;
    Brush b;
    b.color = 0x028cad9c;
    sc_->selectPen(p);
    sc_->selectBrush(b);
    const double a = static_cast<double>(static_cast<uint32_t>(timeOfDay)) * 7.272205324074074e-05;
    const double k = 0.5235987833333333;
    const double a1 = k + a, a2 = a - k;
    const int x1 = static_cast<int>(std::sin(a1) * 17.0 + 580.0), y1 = static_cast<int>(std::cos(a1) * -17.0 + 40.0);
    const int x2 = static_cast<int>(std::sin(a2) * 17.0 + 580.0), y2 = static_cast<int>(std::cos(a2) * -17.0 + 40.0);
    sc_->pie(0x233, 0x17, 0x255, 0x39, x1, y1, x2, y2);
}

void GdiPlayerDisplay::statusScreenHotspot(const char* name) {
    g_.eng_.EnableSimStatusHotspot(name, 1);
}

// the end of the drawing: the DC's old font, pen and brush back, the DC released, the screen drawn; the environment screen is shown only now
void GdiPlayerDisplay::statusScreenDone(const char* screen) {
    Eng& eng = g_.eng_;
    if (sc_) {
        sc_->selectFont(oldFont_);
        sc_->selectPen(oldPen_);
        sc_->selectBrush(oldBrush_);
        eng.ReleaseSimStatusDC(simDc_);
        sc_ = nullptr;
        simDc_ = 0;
    }
    eng.RedrawScreen();
    if (std::string(screen) == "environment") eng.ShowSimStatusWindow(1, nullptr, 0, 0x36);
}

void GdiPlayerDisplay::statusScreenClose(const char* screen) {
    Eng& eng = g_.eng_;
    if (std::string(screen) == "status") {
        const int origin[2] = {0, 0};
        eng.DisplayImage("blackbar", g_.self().m_lTopBarHandle, origin, 1, -1, nullptr, 0);
        eng.ShowSimStatusWindow(0, g_.g.haveStatusPalette ? g_.g.statusPalette.data() : nullptr, 1, -1);
    } else {
        eng.ShowSimStatusWindow(0, nullptr, 0, -1);
    }
}

} // namespace biosys::game
