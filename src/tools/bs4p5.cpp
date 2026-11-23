// biosys-bs4p5: headless driver of the BS4 device panels of packet P5 (tests/test_bs4p5.py). The real game logic (NewGame.sav) answers the
// feature parameters; the commands open pages with real clicks and dump what a panel shows.
//   biosys-bs4p5 [--sav file.sav] [script]       one command per line, `#` starts a comment
// load <page> | clickh <hotspot> | click x y | move x y | down x y | up x y | advance <ms> | shot <file>
// get <feature|-> <id> | setp <feature|-> <id> <a> [b c]            event 27 / 26 through the game logic (or the override table)
// ovr <feature|-> <id> <int> <double> | unovr                         force a parameter answer (for state the game has no lever for)
// hdr sun|wind|gen|demand <v>                                           the world header (sunlight, wind speed, power generation / demand)
// tank <feature> temp|purity|water <v>                                  the tank a tank sensor watches, committed, sensor updated
// upd <feature>                                                         CFeature::update(game time)
// openers <panelPage>                                                   every verb-0x1a hotspot of the main pages that opens it
// phot | mask main|panel | events | logs | echo ...
// pstate                       the panel's widget state, one line     textchk <l> <t> <r> <b> <grey|green|red> <R> <G> <B> <text>
#include "engine/devroot.h"
#include "engine/bs4.h"
#include "engine/bs4page.h"
#include "engine/bs4widgets.h"
#include "engine/datafiles.h"
#include "game/feat_sensors.h"
#include "game/featenv.h"
#include "game/featparam.h"
#include "game/feature.h"
#include "game/savegame.h"
#include "game/sim.h"
#include "game/watertank.h"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

using namespace biosys;

namespace {

struct Host : Bs4Host {
    u32 now = 100000;
    game::World* world = nullptr;
    std::map<std::pair<std::string, int>, std::pair<u32, double>> ovr;
    std::vector<std::string> events, logs;
    std::vector<int> clicks, loopsOn, loopsOff;
    int presents = 0;
    bool fake = false;      // `fake on`: event 26 only recorded and kept in the override table (the game is not called)
    bool onSetFeatureParameter(const std::string& f, int id, int a, int b, int c) override {
        events.push_back("set " + (f.empty() ? "-" : f) + " " + std::to_string(id) + " " + std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(c));
        if (fake) { ovr[{f, id}] = {static_cast<u32>(a), 0}; return true; }
        try {
            return game::featureSetParameter(*world, f, id, a, b, c);
        } catch (const Error& e) {                  // a parameter the game logic does not implement: the panel logs the failed set
            logs.push_back(std::string("game threw: ") + e.what());
            return false;
        }
    }
    bool onGetFeatureParameter(const std::string& f, int id, u32& i, double& d) override {
        auto it = ovr.find({f, id});
        if (it != ovr.end()) { i = it->second.first; d = it->second.second; return true; }
        return game::featureGetParameter(*world, f, id, i, d);
    }
    void present(const Frame&) override { presents++; }
    u32 tickCount() override { return now; }
    void log(const std::string& m) override { logs.push_back(m); }
    void playClick(int s) override { clicks.push_back(s); }
    void startLoop(int l) override { loopsOn.push_back(l); }
    void stopLoop(int l) override { loopsOff.push_back(l); }
};

std::string num(double v) { char b[40]; std::snprintf(b, sizeof b, "%.17g", v); return b; }
std::string hex(u32 v) { char b[16]; std::snprintf(b, sizeof b, "0x%08x", v); return b; }

Bs4Action makeAction(int verb) {
    Bs4Action a;
    a.rect = Bs4Rect{0, 0, 10, 10};
    a.verb = verb;
    return a;
}

bool findHotspot(Bs4Page& p, const std::string& name, int& cx, int& cy) {
    for (const Hotspot& h : p.rec.hotspots)
        if (!(h.flags & 1) && iequals(h.name, name)) { cx = (h.left + h.right) / 2; cy = (h.top + h.bottom) / 2; return true; }
    return false;
}

std::string rectStr(const Bs4Rect& r) { return std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right) + "," + std::to_string(r.bottom); }

// 1 when the bar of a slot is drawn as the original does: `percent` columns of the bar image, the rest of the 151 columns the grey fill
int barOk(Bs4& b, Bs4Page& p, const Bs4GaugeSlot& g) {
    if (g.image < 0 || g.bar.none()) return -1;
    const Image8& img = p.image(g.image);
    const int rows = g.bar.bottom - g.bar.top;
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < g.bar.right - g.bar.left; x++) {
            const u8 got = p.surf.row(g.bar.top + y)[g.bar.left + x];
            const u8 want = x < g.percent ? img.px[static_cast<size_t>(y * img.w + x)] : static_cast<u8>(b.colours().grey);
            if (got != want) return 0;
        }
    return 1;
}

void pstate(Bs4& b) {
    if (!b.panelActive()) { std::printf("pstate none\n"); return; }
    Bs4Page& p = b.panelPage();
    std::printf("pstate panel=%s feature=[%s] mask=%s opener=%s hl=%s/%d reload=%d reloadName=[%s] handler=%d loop=%d devOn=%d devActive=%d devOpen=%d alarm=%s",
                p.name().c_str(), p.feature.c_str(), hex(p.visibleMask()).c_str(), b.panelOpener().c_str(), b.highlightName().c_str(), b.highlightBit(),
                p.reload ? 1 : 0, p.reloadName.c_str(), p.handler ? 1 : 0, p.loopSound, p.devOn ? 1 : 0, p.devActive ? 1 : 0, p.devOpen ? 1 : 0,
                hex(p.alarmBits).c_str());
    for (int i = 0; i < 3; i++) {
        const Bs4GaugeSlot& g = p.gauge[i];
        std::printf(" g%d=%s|%d|%d|%s|%s|%d|%s|%s|%d", i, num(g.last).c_str(), g.percent, g.paramId, num(g.min).c_str(), num(g.max).c_str(), g.image,
                    rectStr(g.text).c_str(), rectStr(g.bar).c_str(), barOk(b, p, g));
    }
    const Bs4ScheduleSlider& s = p.schedule;
    std::printf(" sched=%u:%02u-%u:%02u texts=%s;%s markers=%s;%s sliderY=%d marks=%d,%d", s.onHours, s.onMinutes, s.offHours, s.offMinutes,
                rectStr(s.onText).c_str(), rectStr(s.offText).c_str(), rectStr(s.markerOn).c_str(), rectStr(s.markerOff).c_str(), p.sliderY, p.markOn, p.markOff);
    int broken = -1;                                  // the "broken" ornament: 1 shown, 0 hidden, -1 none
    for (const Ornament& o : p.rec.ornaments)
        if (o.type == 1 && iequals(o.name, "broken")) { broken = (o.flags & 1) ? 0 : 1; break; }
    std::printf(" broken=%d\n", broken);
}

// is the text of `s` drawn in the rectangle of the panel surface? Renders the reference on a copy of the surface.
int textCheck(Bs4& b, const Bs4Rect& r, const std::string& fill, int R, int G, int B, const std::string& text) {
    Bs4Page& p = b.panelPage();
    Surface ref = p.surf;
    const int idx = fill == "grey" ? b.colours().grey : fill == "green" ? b.colours().green : b.colours().red;
    b.fillRect(ref, r, idx);
    b.drawText(ref, r, text, R, G, B, 0x25);
    for (int y = r.top; y < r.bottom; y++)
        for (int x = r.left; x < r.right; x++)
            if (ref.row(y)[x] != p.surf.row(y)[x]) return 0;
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::string script, sav = BIOSYS_DEV_ROOT "/setup-cd/installed/DATA1/NewGame.sav";
        for (int i = 1; i < argc; i++) {
            const std::string a = argv[i];
            if (a == "--sav" && i + 1 < argc) sav = argv[++i];
            else script = a;
        }
        DataFiles files = DataFiles::fromEnvironment();
        Host host;
        Bs4Session session;
        game::SaveGame::Options o;
        game::SaveGame sg(o);
        sg.load(game::SaveGame::blobOf(readFile(sav)));
        game::Sim sim(sg);
        game::bindFeatureHooks(sim);
        host.world = &sg.world;
        session.initialPassword = true;
        std::unique_ptr<Bs4> bs = std::make_unique<Bs4>(host, files, session);
        auto click = [&](int x, int y) { bs->onMouseMove(0, x, y); bs->onLButtonDown(1, x, y); bs->onLButtonUp(0, x, y); };
        std::ifstream f;
        if (!script.empty()) f.open(script);
        std::istream& in = script.empty() ? std::cin : f;
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream is(line);
            std::string cmd;
            if (!(is >> cmd) || cmd[0] == '#') continue;
            Bs4& b = *bs;
            if (cmd == "load") {
                std::string pg; is >> pg;
                std::printf("load %s = %s\n", pg.c_str(), b.loadMainPage(b.pageNumber(pg)) ? "ok" : "failed");
            } else if (cmd == "clickh") {
                std::string n; is >> n;
                int cx = 0, cy = 0;
                if (b.panelActive() && findHotspot(b.panelPage(), n, cx, cy)) {
                    click(b.panelRect().left + cx, b.panelRect().top + cy);
                    std::printf("clickh %s panel %d %d\n", n.c_str(), cx, cy);
                } else if (findHotspot(b.mainPage(), n, cx, cy)) {
                    click(cx, cy);
                    std::printf("clickh %s main %d %d\n", n.c_str(), cx, cy);
                } else std::printf("clickh %s not found\n", n.c_str());
            } else if (cmd == "click" || cmd == "move" || cmd == "down" || cmd == "up") {
                int x, y; is >> x >> y;
                if (cmd == "click") click(x, y);
                else if (cmd == "move") b.onMouseMove(0, x, y);
                else if (cmd == "down") b.onLButtonDown(1, x, y);
                else b.onLButtonUp(0, x, y);
            } else if (cmd == "advance") {
                int ms; is >> ms;
                while (ms > 0) { const int s = std::min(ms, 500); host.now += static_cast<u32>(s); ms -= s; b.onTimer(); b.onIdle(); }
            } else if (cmd == "shot") {
                std::string path; is >> path;
                Image8 img;
                img.w = 640; img.h = 480; img.px = b.frame().px;
                writeBmp(path, img, b.palette());
            } else if (cmd == "get") {
                std::string fe, ids; is >> fe >> ids;
                u32 i = 0; double d = 0;
                const bool ok = host.onGetFeatureParameter(fe == "-" ? "" : fe, static_cast<int>(std::stol(ids, nullptr, 0)), i, d);
                std::printf("get %s %d = %s int=%d dbl=%s\n", fe.c_str(), static_cast<int>(std::stol(ids, nullptr, 0)), ok ? "ok" : "no", static_cast<i32>(i), num(d).c_str());
            } else if (cmd == "setp") {
                std::string fe, ids, sa = "0", sb = "0", sc = "0"; is >> fe >> ids >> sa >> sb >> sc;
                const long a = std::stol(sa, nullptr, 0), bb = std::stol(sb, nullptr, 0), c = std::stol(sc, nullptr, 0);
                const bool ok = host.onSetFeatureParameter(fe == "-" ? "" : fe, static_cast<int>(std::stol(ids, nullptr, 0)), static_cast<int>(a), static_cast<int>(bb), static_cast<int>(c));
                std::printf("setp %s %s = %s\n", fe.c_str(), ids.c_str(), ok ? "ok" : "no");
            } else if (cmd == "ovr") {
                std::string fe, ids, siv = "0"; double dv = 0; is >> fe >> ids >> siv >> dv;
                const long iv = std::stol(siv, nullptr, 0);
                host.ovr[{fe == "-" ? "" : fe, static_cast<int>(std::stol(ids, nullptr, 0))}] = {static_cast<u32>(iv), dv};
            } else if (cmd == "unovr") host.ovr.clear();
            else if (cmd == "fake") { std::string w; is >> w; host.fake = w == "on"; }
            else if (cmd == "hdr") {
                std::string w; double v = 0; is >> w >> v;
                auto& h = game::cworld::header(sg.world);
                if (w == "sun") h.m_dSunlight = v;
                else if (w == "wind") h.m_dWindSpeed = v;
                else if (w == "gen") h.m_dPowerGeneration = v / 3600.0;
                else if (w == "demand") h.m_dPowerDemand = v / 3600.0;
                else std::printf("hdr: unknown field\n");
            } else if (cmd == "upd") {
                std::string fe; is >> fe;
                game::CFeature* cf = sg.world.findFeature(fe.c_str());
                if (cf) cf->update(sim.time());
                else std::printf("upd %s: no such feature\n", fe.c_str());
            } else if (cmd == "tank") {
                std::string fe, what; double v = 0; is >> fe >> what >> v;
                game::CFeature* cf = sg.world.findFeature(fe.c_str());
                auto* ts = dynamic_cast<game::CTankSensor*>(cf);
                if (!ts) { std::printf("tank %s: not a tank sensor\n", fe.c_str()); continue; }
                game::CWaterTank* t = ts->tank();
                if (what == "temp") t->setNewTemp(v);
                else if (what == "purity") t->setNewPurity(v);
                else if (what == "water") t->setWaterVol(v);
                t->commit();
                ts->update(sim.time());
            } else if (cmd == "openers") {
                std::string panel; is >> panel;
                const int pn = b.pageNumber(panel);
                for (int n = 1; n <= 45; n++) {
                    Page p = b.pageFile().loadPage(n);
                    for (const Hotspot& h : p.hotspots)
                        if (h.action.verb == 0x1a && h.action.arg[0] == pn)
                            std::printf("opener %s %s %d %d %d %d %d %d\n", p.name.c_str(), h.name.c_str(), h.left, h.top, h.right, h.bottom, h.action.arg[1], h.action.arg[2]);
                }
            } else if (cmd == "openpanel") {
                // load the main page; when the opener hotspot is hidden click the enabled verb-0x17 hotspot that shows its item group first
                std::string pg, op; int idx = 0; is >> pg >> op >> idx;
                b.loadMainPage(b.pageNumber(pg));
                int cx = 0, cy = 0;
                unsigned item = 0;
                std::sscanf(op.c_str(), "%u_", &item);
                auto nth = [&](int& x, int& y) {
                    int k = 0;
                    for (const Hotspot& h : b.mainPage().rec.hotspots)
                        if (!(h.flags & 1) && iequals(h.name, op) && k++ == idx) { x = (h.left + h.right) / 2; y = (h.top + h.bottom) / 2; return true; }
                    return false;
                };
                if (!nth(cx, cy)) {
                    for (const Hotspot& h : b.mainPage().rec.hotspots)
                        if (!(h.flags & 1) && h.action.verb == 0x17 && ((static_cast<u32>(h.action.arg[0]) >> item) & 1)) {
                            click((h.left + h.right) / 2, (h.top + h.bottom) / 2);
                            if (nth(cx, cy)) break;
                        }
                }
                if (nth(cx, cy)) click(cx, cy);
                std::printf("openpanel %s %s -> panel=%s opener=%s\n", pg.c_str(), op.c_str(), b.panelActive() ? b.panelPage().name().c_str() : "-", b.panelOpener().c_str());
            } else if (cmd == "phot") {
                if (!b.panelActive()) { std::printf("phot none\n"); continue; }
                const Bs4Page& p = b.panelPage();
                for (const Hotspot& h : p.rec.hotspots)
                    std::printf("phot %s %d %d %d %d flags=%u verb=%x a=%d,%d,%d,%d,%d,%d,%d,%d\n", h.name.c_str(), h.left, h.top, h.right, h.bottom, h.flags, h.action.verb,
                                h.action.arg[0], h.action.arg[1], h.action.arg[2], h.action.arg[3], h.action.arg[4], h.action.arg[5], h.action.arg[6], h.action.arg[7]);
                std::printf("prect %s items=%s\n", rectStr(b.panelRect()).c_str(), hex(p.visibleMask()).c_str());
            } else if (cmd == "mask") {
                std::string w; is >> w;
                const Bs4Page& p = w == "panel" ? b.panelPage() : b.mainPage();
                std::printf("mask %s %s\n", w.c_str(), hex(p.visibleMask()).c_str());
            } else if (cmd == "pstate") pstate(b);
            else if (cmd == "textchk") {
                // textchk g0|g1|g2|on|off <text>: the gauge text (grey fill, grey text) or the schedule text (green / red) of the panel
                // `@ <feature|-> <id> <min> <max>` (gauge) / `@ <feature>` (schedule): the text is taken from the game's own answer
                std::string what, text;
                is >> what;
                std::getline(is, text);
                if (!text.empty() && text[0] == ' ') text.erase(0, 1);
                if (!text.empty() && text[0] == '@') {
                    std::istringstream ts(text.substr(1));
                    std::string fe, ids;
                    ts >> fe >> ids;
                    if (fe == "-") fe.clear();
                    u32 iv = 0; double dv = 0;
                    if (what == "on" || what == "off") {
                        u32 on = 0, len = 0;
                        host.onGetFeatureParameter(fe, 3, on, dv);
                        host.onGetFeatureParameter(fe, 6, len, dv);
                        u32 t = what == "on" ? on : len + on;
                        if (what == "off" && t > 0x15180u) t -= 0x15180u;
                        char tb[32];
                        std::snprintf(tb, sizeof tb, "%u:%02u", t / 0xe10, (t % 0xe10) / 0x3c);
                        text = tb;
                    } else {
                        double mn = 0, mx = 0;
                        ts >> mn >> mx;
                        host.onGetFeatureParameter(fe, static_cast<int>(std::stol(ids, nullptr, 0)), iv, dv);
                        if (dv < mn) dv = mn;
                        if (mx < dv) dv = mx;
                        char tb[64];
                        std::snprintf(tb, sizeof tb, "%.1f", dv);
                        text = tb;
                        if (text == "100.0") text = "100";
                    }
                }
                int res = -1;
                if (b.panelActive()) {
                    const Bs4Page& p = b.panelPage();
                    if (what.size() == 2 && what[0] == 'g') res = textCheck(b, p.gauge[what[1] - '0'].text, "grey", 76, 74, 74, text);
                    else if (what == "on") res = textCheck(b, p.schedule.onText, "green", 0, 58, 8, text);
                    else if (what == "off") res = textCheck(b, p.schedule.offText, "red", 99, 0, 0, text);
                }
                std::printf("textchk %s %d\n", what.c_str(), res);
            } else if (cmd == "sounds") {
                std::printf("sounds clicks=");
                for (int c : host.clicks) std::printf("%d,", c);
                std::printf(" on=");
                for (int c : host.loopsOn) std::printf("%d,", c);
                std::printf(" off=");
                for (int c : host.loopsOff) std::printf("%d,", c);
                std::printf("\n");
                host.clicks.clear(); host.loopsOn.clear(); host.loopsOff.clear();
            } else if (cmd == "events") { for (auto& e : host.events) std::printf("%s\n", e.c_str()); host.events.clear(); }
            else if (cmd == "logs") { for (auto& e : host.logs) std::printf("log %s\n", e.c_str()); host.logs.clear(); }
            else if (cmd == "state") {
                std::printf("state page=%s panel=%s mask=%s\n", b.mainPage().name().c_str(), b.panelActive() ? b.panelPage().name().c_str() : "-",
                            hex(b.mainPage().visibleMask()).c_str());
            } else if (cmd == "act") {
                int verb = 0; is >> std::hex >> verb >> std::dec;
                Bs4Action a = makeAction(verb);
                int k = 0, v;
                while (k < 8 && is >> v) a.arg[k++] = v;
                std::printf("act %x = %s\n", verb, b.dispatchAction(a) ? "ok" : "failed");
            } else if (cmd == "close") {
                Bs4Action a = makeAction(0x1b);
                std::printf("close = %s\n", b.dispatchAction(a) ? "ok" : "failed");
            } else if (cmd == "echo") { std::string rest; std::getline(is, rest); std::printf("%s\n", rest.c_str() + (rest.empty() ? 0 : 1)); }
            else std::printf("unknown command %s\n", cmd.c_str());
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bs4p5: %s\n", e.what());
        return 1;
    }
}
