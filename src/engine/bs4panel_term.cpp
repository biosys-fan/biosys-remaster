// BioSys 4 power terminals `termsav termoc termevo termrf termhouse` (pages 74..78: Init exe:0041e5f0, AddPowerUsage exe:00420ef0,
// handler exe:00435a90). One routine and a table per page: the original has five blocks (termsav and termoc inline, the others through
// AddPowerUsage) whose logic is the same, checked block by block:
//   * every row (a group of features of one kind: mixers, lung, revos, UV, scrubbers, ... heater) starts at 0.0 and adds FEATURE_POWER_USAGE
//     (0x8e, a double) of each of its features; a failed read is logged and leaves the row ok = false, a value outside [0, 10] is logged
//     ("outside its valid range") and clamped into [0, 10] (the inline blocks of termsav / termoc clamp the same way as AddPowerUsage);
//   * all features of a row are read, but the next row is only started when every read of this row succeeded; when one fails the
//     function ends silently (no handler is set, the page stays blank; nothing is repainted);
//   * when all rows are done: Init(0) (page load) clears the highlight bit, repaints the main page and requests a screen repaint;
//     in every case the page handler is set.
// The handler (phase 1 draw, 2 poll, 3 nothing): phase 2 runs Init(1) first; then for each of the page's rows (9 / 10 / 9 / 10 / 2) the rectangle
// (2, 0x56 + 16 i, 0x30, 0x64 + 16 i) is filled white and the sum drawn as "%.1f" centred in (76,74,74); phase 2 ends by requesting a
// screen repaint. The panels have no control that sets a parameter: their hotspots (verb 0x1a) switch to the other terminals / to specterm.
#include "engine/bs4.h"
#include "engine/bs4panel_term.h"
#include <cstdio>
#include <memory>

namespace biosys {
namespace {

struct TermDef {
    const char* page;
    const char* highlight;                         // +0x1bec: ornament painted on the main page (bit 0 of its mask)
    std::vector<std::vector<const char*>> rows;    // the features of each row, in the order of the original's calls
};

const TermDef kDefs[] = {
    {"termsav", "hiterms1", {{"fs9mixer", "fs3mixer", "fs7mixer"}, {"fbs2lung"}, {"fbs4revos"}, {"fbs4UV"},
                             {"fbs5scrub", "fbs3scrub", "fbs2scrub"}, {"fbs3watpmp"}, {"fs2rain", "fs5rain", "fs8rain"},
                             {"fs1fan", "fs4fan", "fs7fan"}, {"fsheater"}}},
    {"termoc", "hitermo1", {{"fomixer1", "fomixer2", "fomixer3"}, {"fbo1lung"}, {"fbo4revos"}, {"fbo3UV"}, {"fbo3algae"}, {"fbo2watpmp"},
                            {"fo1rain"}, {"fbo1tide"}, {"foheater"}, {"fbo1wave"}}},
    {"termevo", "hiterme1", {{"fdbmixer1", "fdbmixer2", "fdbmixer3"}, {"fbd5lung"}, {"fbd3revos"}, {"fbd2UV"},
                             {"fbd1scrub", "fbd4scrub", "fbd5scrub"}, {"fbd3watpmp"}, {"fdbrain1", "fdbrain2", "fdbrain3"},
                             {"fdb6fan", "fdb3fan", "fdbfan3"}, {"fdbheater"}}},
    {"termrf", "hitermr1", {{"fr9mixer", "fr5mixer", "fr2mixer"}, {"fbr2lung"}, {"fbr4revos"}, {"fbr3UV"},
                            {"fbr5scrub", "fbr2scrub", "fbr1scrub"}, {"fbr3watpmp"}, {"fr9rain", "fr6rain", "fr1rain"},
                            {"fr8fan", "fr3fan", "fr10fan"}, {"frfheater"}, {"fr9mist", "fr5mist", "fr1mist"}}},
    {"termhouse", "hitermh1", {{"fphheater"}, {"fbp1UV"}}},
};

constexpr int kPowerUsage = 0x8e;                  // FEATURE_POWER_USAGE

struct State {
    const TermDef* def = nullptr;
    std::vector<double> sum;                       // +0x1e0 + 0x48 * i
    std::vector<Bs4Rect> rect;                     // +0x1e8 + 0x48 * i (set by Init(0))
    std::vector<std::string> text;                 // what the handler drew last
};
std::weak_ptr<State> gLast;

std::string fmt1(double v) {
    char b[64];
    std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}

// exe:00420ef0 (and the same code inline in exe:0041e5f0 for termsav / termoc). false: the read failed (logged).
bool addPowerUsage(Bs4Page& p, const char* feature, double& sum) {
    u32 i = 0;
    double v = 0;
    if (!p.owner().host().onGetFeatureParameter(feature, kPowerUsage, i, v)) {
        p.owner().log(std::string("BS4: Failed to get the value of parameter FEATURE_POWER_USAGE for BiosFear feature '") + feature + "'.");
        return false;
    }
    if (v < 0.0 || 10.0 < v) {
        p.owner().log(std::string("BS4: Value of parameter FEATURE_POWER_USAGE for BiosFear feature '") + feature +
                      "' is outside its valid range.  (Value = " + std::to_string(v) + ").");
        if (v < 0.0) v = 0.0;
        if (10.0 < v) v = 10.0;
    }
    sum += v;
    return true;
}

// exe:0041e5f0 (phase 0 = page load, 1 = poll). true: the original reaches its end and (re)sets the handler.
bool init(Bs4Page& p, State& s, int phase) {
    Bs4& o = p.owner();
    if (phase == 0) {
        o.setHighlight("", o.highlightBit());                          // +0x1bec = 0
        s.rect.assign(s.def->rows.size(), Bs4Rect());
        for (size_t i = 0; i < s.rect.size(); i++) s.rect[i] = Bs4Rect{2, 0x56 + 16 * static_cast<int>(i), 0x30, 0x64 + 16 * static_cast<int>(i)};
    }
    o.setHighlight(s.def->highlight, o.highlightBit());                // strcpy to +0x1bec
    for (size_t r = 0; r < s.def->rows.size(); r++) {
        s.sum[r] = 0;
        bool ok = true;
        for (const char* f : s.def->rows[r]) ok = addPowerUsage(p, f, s.sum[r]) && ok;
        if (!ok) return false;                                         // the original falls out of its nested ifs: no handler, no repaint
    }
    if (phase == 0) {
        o.setHighlight(s.def->highlight, 0);                           // +0x1bf8 = 0
        o.mainPage().paint();                                          // exe:0041baf0 on the main page (exe:00408ff0 is a dead remnant)
        o.needRepaint();
    }
    return true;
}

// exe:00435a90
void handler(Bs4Page& p, State& s, int phase) {
    if (phase == 2) init(p, s, 1);
    if (phase == 3) return;
    for (size_t i = 0; i < s.def->rows.size(); i++) {
        p.fill(s.rect[i], p.owner().colours().white);
        s.text[i] = fmt1(s.sum[i]);
        p.drawText(s.rect[i], s.text[i], 76, 74, 74, 0x25);
    }
    if (phase == 2) p.repaintScreen();
}

void onLoad(Bs4Page& p, const TermDef& def) {
    auto s = std::make_shared<State>();
    s->def = &def;
    s->sum.assign(def.rows.size(), 0.0);
    s->text.assign(def.rows.size(), std::string());
    s->rect.assign(def.rows.size(), Bs4Rect());
    gLast = s;
    p.handler = nullptr;
    if (init(p, *s, 0)) p.handler = [&p, s](int phase) { handler(p, *s, phase); };
}

struct Registrar {
    Registrar() {
        for (const TermDef& d : kDefs) {
            Bs4PanelDef def;
            def.variants = false;
            const TermDef* dp = &d;
            def.onLoad = [dp](Bs4Page& p) { onLoad(p, *dp); };
            Bs4Registry::addPanel(d.page, std::move(def));
        }
    }
} registrar;

} // namespace

namespace bs4term {
std::vector<Row> rows() {
    std::vector<Row> out;
    if (auto s = gLast.lock())
        for (size_t i = 0; i < s->sum.size(); i++) {
            Row r;
            r.sum = s->sum[i];
            r.text = s->text[i];
            r.left = s->rect[i].left; r.top = s->rect[i].top; r.right = s->rect[i].right; r.bottom = s->rect[i].bottom;
            out.push_back(r);
        }
    return out;
}
} // namespace bs4term
} // namespace biosys
