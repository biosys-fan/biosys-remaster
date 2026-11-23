// biosys-bs4test: headless driver of the BioSys 4 terminal (src/engine/bs4*.cpp) with a scripted host.
//   biosys-bs4test [--static] [--game [file.sav]] [--fixes LIST] [--gamefixes LIST] [--clock TEXT] [script]      script = a file or stdin
// --fixes    the terminal's remaster switch (bs4fixes.h words); --gamefixes: game::Fixes::parse text for the --game world
// --static   no page-specific code (no panel inits, hooks or main extensions): the static picture of the page records
// --game     the feature parameters come from the real game logic (NewGame.sav by default) instead of the table set with `set`
// Script commands, one per line (answers on stdout, `#` starts a comment):
//   load <page|number>           main page (verb 0x12)           panel <page> [x y [opener]]   verb 0x1a (default 442 72)
//   act <verb> [a1..a8]          raw action (name=<hotspot> sets the action name)
//   move x y | down x y | up x y | click x y | clickh <hotspot>   mouse (click = move, down, up; clickh = the centre of the first enabled hotspot of that name)
//   key <vk> | keyup <vk> | type <text>      keyboard                advance <ms>   virtual clock, onTimer every 500 ms
//   set <feature|-> <id> <int> <double> | get <feature|-> <id>      the fake feature table / a read
//   shot <file> (frame as presented) | shotbb <file> (back buffer, no cursor) | state | mask main|panel | events | logs | hotspots
//   save <file> | restore <file>             the 0x480-byte section  clock <text>   text of the two clock fields
//   static <dir>                 write the static picture of every page (main_NNN.bmp, panel_NNN.bmp + .txt, masks.txt)
//   selftest                     the built-in checks (PASS / FAIL lines)
// P3 (main-page dynamics, tests/test_bs4main.sh):
//   p3                           the dynamics state of the main page (device / alarm row, probe sensor / row / value / text, pod, mail)
//   overlays | alarms            the non-zero overlay / alarm ornament states     orn                   every type-1 ornament of the main page: index name flags
//   setp <feature|-> <id> <a> [b c]   event 26 (the real game logic with --game, else the fake table)     session mail <n> | session stamp <text>
//   story <0|1>                  --game: bit 29 of the story milestones (the game's "mail has arrived" flag, parameter 0x438)
//   shotpage <file>              the main page surface (transparent colour where nothing is painted)      afteraction <hotspot>   exe:0041deb0
#include "engine/devroot.h"
#include "engine/bs4.h"
#include "engine/bs4main.h"
#include "engine/bs4widgets.h"
#include "engine/bs4panels_a.h"
#include "engine/bs4page.h"
#include "engine/bs4panel_term.h"
#include "engine/bs4panel_aw.h"
#include "engine/datafiles.h"
#include "game/featenv.h"
#include "game/featparam.h"
#include "game/savegame.h"
#include "game/sim.h"
#include "game/watertank.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

using namespace biosys;

namespace {

struct TestHost : Bs4Host {
    u32 now = 100000;
    bool active = true;
    std::map<std::pair<std::string, int>, std::pair<u32, double>> fake;
    game::World* world = nullptr;
    std::string clock;
    std::vector<std::string> events, logs;
    int presents = 0, leaves = 0, closes = 0, moves = 0;
    Frame last;
    std::vector<int> clicks;

    bool onBS4Query(int id, void* buffer) override {
        if ((id == 7 || id == 0xb) && !clock.empty()) {
            std::snprintf(static_cast<char*>(buffer), 512, "%s", clock.c_str());
            return true;
        }
        return false;
    }
    bool onSetFeatureParameter(const std::string& f, int id, int a, int b, int c) override {
        events.push_back("set " + (f.empty() ? "-" : f) + " " + std::to_string(id) + " " + std::to_string(a) + " " + std::to_string(b) + " " +
                         std::to_string(c));
        if (world) return game::featureSetParameter(*world, f, id, a, b, c);
        fake[{f, id}] = {static_cast<u32>(a), 0};
        return true;
    }
    bool onGetFeatureParameter(const std::string& f, int id, u32& i, double& d) override {
        if (world) return game::featureGetParameter(*world, f, id, i, d);
        auto it = fake.find({f, id});
        if (it == fake.end()) return false;
        i = it->second.first;
        d = it->second.second;
        return true;
    }
    void present(const Frame& f) override { last = f; presents++; }
    void clipPointer(const Bs4Rect* r) override { events.push_back(r ? "clip" : "unclip"); }
    void requestMouseMove() override { moves++; }
    u32 tickCount() override { return now; }
    bool appActive() override { return active; }
    void leave() override { leaves++; }
    void requestClose() override { closes++; }
    void log(const std::string& m) override { logs.push_back(m); }
    void playClick(int slot) override { clicks.push_back(slot); }
    std::vector<std::string> loops;                    // P6: "<n> start" / "<n> stop" (the command `loops`)
    void startLoop(int n) override { loops.push_back(std::to_string(n) + " start"); }
    void stopLoop(int n) override { loops.push_back(std::to_string(n) + " stop"); }
};

struct Rig {
    DataFiles files = DataFiles::fromEnvironment();
    TestHost host;
    Bs4Session session;
    std::unique_ptr<Bs4> bs;
    std::unique_ptr<game::SaveGame> sg;
    std::unique_ptr<game::Sim> sim;
    int mx = 0, my = 0;
    Bs4Fixes fixes;                      // --fixes: the terminal's remaster switch
    std::string gameFixes;               // --gamefixes: game::Fixes list for the world

    void startGame(const std::string& sav) {
        game::SaveGame::Options o;
        if (!gameFixes.empty()) o.fixes = game::Fixes::parse(gameFixes);
        sg = std::make_unique<game::SaveGame>(o);
        sg->load(game::SaveGame::blobOf(readFile(sav)));
        sim = std::make_unique<game::Sim>(*sg);
        game::bindFeatureHooks(*sim);
        host.world = &sg->world;
    }
    void open(const u8* section = nullptr, size_t size = 0) {
        bs.reset();
        Bs4Options opts;
        opts.fixes = fixes;
        bs = std::make_unique<Bs4>(host, files, session, section, size, opts);
    }
    Bs4& b() { return *bs; }
};

std::string num(double v) { char b[40]; std::snprintf(b, sizeof b, "%.17g", v); return b; }

Bs4Action makeAction(int verb, std::initializer_list<int> args = {}, const std::string& name = "") {
    Bs4Action a;
    a.rect = Bs4Rect{0, 0, 10, 10};
    a.verb = verb;
    int i = 0;
    for (int v : args) a.arg[i++] = v;
    a.name = name;
    return a;
}

int pageByArg(Bs4& b, const std::string& s) {
    if (!s.empty() && std::isdigit(static_cast<unsigned char>(s[0]))) return std::stoi(s);
    return b.pageNumber(s);
}

void writeShot(Rig& r, const std::string& path, bool backbuffer) {
    Image8 img;
    img.w = 640; img.h = 480;
    img.px = backbuffer ? r.b().backBuffer().px : r.b().frame().px;
    writeBmp(path, img, r.b().palette());
}

std::string hex(u32 v) { char b[16]; std::snprintf(b, sizeof b, "0x%08x", v); return b; }

void printState(Rig& r) {
    Bs4& b = r.b();
    std::string hov;
    b.mainPage().hoverName(hov);
    std::string pn = "-";
    int n = 0;
    if (b.panelActive() && b.panelPage().getPageNumber(n)) pn = b.panelPage().name();
    std::string gauges;
    for (int g = 0; g < 3; g++)
        if (b.mainPage().gaugePresent[g]) {
            char t[64];
            std::snprintf(t, sizeof t, "%s%d:%.1f/%d", gauges.empty() ? "" : ",", g, b.mainPage().gauge[g].last, b.mainPage().gauge[g].percent);
            gauges += t;
        }
    std::printf("state gauges=%s page=%s no=%d panel=%s opener=%s mask=%s panelmask=%s hover=%s cursor=%d highlight=%s/%d last=%s needs=%d\n",
                gauges.empty() ? "-" : gauges.c_str(), b.mainPage().name().c_str(), b.mainPage().number(), pn.c_str(), b.panelOpener().c_str(), hex(b.mainPage().visibleMask()).c_str(),
                b.panelActive() ? hex(b.panelPage().visibleMask()).c_str() : "-", hov.empty() ? "-" : hov.c_str(), b.cursorKind(),
                b.highlightName().c_str(), b.highlightBit(), b.lastHotspot().c_str(), b.needsRepaint() ? 1 : 0);
}

bool findHotspot(Bs4Page& p, const std::string& name, int& cx, int& cy) {
    for (const Hotspot& h : p.rec.hotspots)
        if (!(h.flags & 1) && iequals(h.name, name)) {
            cx = (h.left + h.right) / 2;
            cy = (h.top + h.bottom) / 2;
            return true;
        }
    return false;
}

void mouseClick(Rig& r, int x, int y) {
    r.b().onMouseMove(0, x, y);
    r.b().onLButtonDown(1, x, y);
    r.b().onLButtonUp(0, x, y);
}

// the opener of a panel page for the static pictures: the first verb-0x1a hotspot (page order 1..45, hotspot order) that names it
struct Opener { int main = 2; int x = 442, y = 72; std::string name; };
Opener openerFor(Bs4& b, int panelNo) {
    Opener o;
    o.main = b.pageNumber("tech");
    for (int n = 1; n <= 45; n++) {
        Page p = b.pageFile().loadPage(n);
        for (const Hotspot& h : p.hotspots)
            if (h.action.verb == 0x1a && h.action.arg[0] == panelNo) return Opener{n, h.action.arg[1], h.action.arg[2], h.name};
    }
    return o;
}

int cmdStatic(Rig& r, const std::string& dir) {
    Bs4& b = r.b();
    std::ofstream masks(dir + "/masks.txt");
    for (int n = 1; n <= b.pageFile().pageCount(); n++) {
        char nm[64];
        if (n <= 45) {
            b.loadMainPage(n);
            std::snprintf(nm, sizeof nm, "/main_%03d.bmp", n);
            writeShot(r, dir + nm, true);
            masks << b.mainPage().name() << " " << hex(b.mainPage().visibleMask()) << "\n";
        } else {
            Opener o = openerFor(b, n);
            b.loadMainPage(o.main);
            Bs4Action a = makeAction(0x1a, {n, o.x, o.y}, o.name);
            b.dispatchAction(a);
            std::snprintf(nm, sizeof nm, "/panel_%03d.bmp", n);
            writeShot(r, dir + nm, true);
            std::snprintf(nm, sizeof nm, "/panel_%03d.txt", n);
            std::ofstream t(dir + nm);
            t << "main=" << o.main << " x=" << o.x << " y=" << o.y << " name=" << o.name << "\n";
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------- selftest

int gFail = 0, gPass = 0;
void check(bool ok, const std::string& what, const std::string& detail = "") {
    if (ok) { gPass++; std::printf("PASS %s\n", what.c_str()); }
    else { gFail++; std::printf("FAIL %s%s%s\n", what.c_str(), detail.empty() ? "" : ": ", detail.c_str()); }
}

// item numbers present in a page record, written independently of the page code (sscanf of the "<n>_" prefix)
u32 existingItems(const Page& p) {
    u32 m = 0;
    auto note = [&](const std::string& s) {
        unsigned n = 0;
        int used = 0;
        if (std::sscanf(s.c_str(), "%u_%n", &n, &used) == 1 && used > 0 && s[static_cast<size_t>(used) - 1] == '_' && n < 32) {
            char back[16];
            std::snprintf(back, sizeof back, "%u_", n);
            if (s.compare(0, std::strlen(back), back) == 0) m |= 1u << n;
        }
    };
    for (const Hotspot& h : p.hotspots) note(h.name);
    for (const Ornament& o : p.ornaments) if (o.type == 1) note(o.name);
    return m;
}

void selfVerbTable(Rig& r) {
    Bs4& b = r.b();
    int n12 = 0, n17 = 0, n1a = 0, bad = 0;
    std::string firstBad;
    for (int pg = 1; pg <= 45; pg++) {
        Page rec = b.pageFile().loadPage(pg);
        for (size_t hi = 0; hi < rec.hotspots.size(); hi++) {
            const Hotspot& h = rec.hotspots[hi];
            Bs4Action a = makeAction(h.action.verb, {}, h.name);
            for (int i = 0; i < 8; i++) a.arg[i] = h.action.arg[i];
            r.host.logs.clear();
            b.loadMainPage(pg);
            if (h.action.verb == 0x12) {
                n12++;
                int want = h.action.arg[0];
                if (!r.session.emailPassword && want == b.pageEmail1()) want = b.pageEmailPw();
                if (!r.session.synthPassword && want == b.pageSynth1()) want = b.pageSynthPw();
                const bool ok = b.dispatchAction(a) && b.mainPage().number() == want;
                if (!ok && !bad++) firstBad = "0x12 " + rec.name + "/" + h.name;
            } else if (h.action.verb == 0x17) {
                n17++;
                const u32 before = b.mainPage().visibleMask(), exist = existingItems(rec);
                const u32 a1 = static_cast<u32>(h.action.arg[0]), a4 = static_cast<u32>(h.action.arg[3]);
                const u32 want = ((before & ~exist) | (((before | (a1 & exist)) & ~a4) & exist));
                const bool ok = b.dispatchAction(a) && b.mainPage().visibleMask() == want;
                if (!ok && !bad++) firstBad = "0x17 " + rec.name + "/" + h.name + " mask " + hex(b.mainPage().visibleMask()) + " want " + hex(want);
            } else if (h.action.verb == 0x1a) {
                n1a++;
                const bool ok = b.dispatchAction(a) && b.panelActive() && b.panelPage().number() == h.action.arg[0] &&
                                b.panelRect().left == h.action.arg[1] && b.panelRect().top == h.action.arg[2] &&
                                b.panelOpener() == h.name;
                if (!ok && !bad++) firstBad = "0x1a " + rec.name + "/" + h.name;
                Bs4Action c = makeAction(0x1b);
                if (!b.dispatchAction(c) || b.panelActive()) { if (!bad++) firstBad = "0x1b after " + h.name; }
            } else if (h.action.verb == 0x18) {
                const bool ok = b.dispatchAction(a);
                if (!ok && !bad++) firstBad = "0x18 " + rec.name;
            }
        }
    }
    check(bad == 0 && n12 > 700 && n17 > 300 && n1a > 300, "verb table on the real pages (" + std::to_string(n12) + " x 0x12, " +
              std::to_string(n17) + " x 0x17, " + std::to_string(n1a) + " x 0x1a/0x1b)", firstBad);
    bool threw[3] = {false, false, false};
    int k = 0;
    for (int v : {0x14, 0x15, 0x19}) {
        Bs4Action a = makeAction(v);
        try { b.dispatchAction(a); } catch (const Error&) { threw[k] = true; }
        k++;
    }
    check(threw[0] && threw[1] && threw[2], "verbs 0x14 0x15 0x19 fail loudly with biosys::Error");
    Bs4Action u = makeAction(0x1c), nop = makeAction(0x11), fail = makeAction(0x16), leave = makeAction(0x18);
    check(!b.dispatchAction(u) && b.dispatchAction(nop) && !b.dispatchAction(fail) && b.dispatchAction(leave),
          "verbs 0x11 / 0x13 no-op, 0x16 fails, 0x18 returns 1, an unknown verb fails");
    // a click on a hotspot with verb 0x18 asks the host to leave
    b.loadMainPage(b.pageNumber("tech"));
    int cx = 0, cy = 0;
    int before = r.host.leaves;
    Page rec = b.pageFile().loadPage(b.pageNumber("tech"));
    for (const Hotspot& h : rec.hotspots)
        if (h.action.verb == 0x18) { cx = (h.left + h.right) / 2; cy = (h.top + h.bottom) / 2; mouseClick(r, cx, cy); break; }
    check(r.host.leaves == before + 1, "a click on a verb-0x18 hotspot calls Bs4Host::leave");
}

void selfCompose(Rig& r) {
    Bs4& b = r.b();
    b.loadMainPage(b.pageNumber("tech"));
    Bs4Page& m = b.mainPage();
    const u8 tr = static_cast<u8>(b.colours().transparent), wh = static_cast<u8>(b.colours().white);
    std::fill(m.surf.px.begin(), m.surf.px.end(), tr);
    m.surf.row(300)[10] = 17;
    m.surf.row(301)[10] = tr;
    b.compose();
    const Surface& bb = b.backBuffer();
    check(bb.px[300 * 640 + 10] == 17 && bb.px[301 * 640 + 10] == wh && bb.px[0] == wh,
          "compose: white fill, main surface blitted with the transparent colour key");
    Bs4Action a = makeAction(0x1a, {b.pageNumber("panelah"), 442, 72}, "5_ahs2");
    b.dispatchAction(a);
    Bs4Page& p = b.panelPage();
    std::fill(p.surf.px.begin(), p.surf.px.end(), tr);
    p.surf.row(5)[5] = 33;
    m.surf.row(80)[447 + 1] = 21;                      // under the panel: shows through where the panel is transparent
    m.surf.row(77)[447] = 22;                          // under the panel's opaque pixel: hidden
    b.compose();
    check(bb.px[77 * 640 + 447] == 33 && bb.px[80 * 640 + 448] == 21 && bb.px[10 * 640 + 10] == wh,
          "compose: the panel surface goes over the main surface at its rectangle with the same key");
    b.setHoverFrame(Bs4Rect{100, 200, 150, 220});
    b.compose();
    const u8 frameIdx = bb.px[200 * 640 + 120];
    check(frameIdx == bb.px[200 * 640 + 130] && frameIdx == bb.px[210 * 640 + 100] && frameIdx != wh && bb.px[210 * 640 + 120] == wh,
          "compose: the hover frame is a one pixel box (top and left sides sampled, interior untouched)");
    b.setHoverFrame(Bs4Rect());
}

void selfMasks(Rig& r) {
    Bs4& b = r.b();
    // the visible mask after load, for every main page, against an independent scan of the record (hotspot first, else ornament)
    int bad = 0, nonzero = 0;
    std::string where;
    for (int pg = 1; pg <= 45; pg++) {
        b.loadMainPage(pg);
        Page rec = b.pageFile().loadPage(pg);
        u32 want = 0;
        for (unsigned n = 0; n < 32; n++) {
            char pre[16];
            std::snprintf(pre, sizeof pre, "%u_", n);
            bool done = false;
            for (const Hotspot& h : rec.hotspots)
                if (h.name.compare(0, std::strlen(pre), pre) == 0) { if (!(h.flags & 1)) want |= 1u << n; done = true; break; }
            if (done) continue;
            for (const Ornament& o : rec.ornaments)
                if (o.type == 1 && o.name.compare(0, std::strlen(pre), pre) == 0) { if (!(o.flags & 1)) want |= 1u << n; break; }
        }
        if (want) nonzero++;
        if (b.mainPage().visibleMask() != want && !bad++) where = rec.name;
    }
    check(bad == 0 && nonzero > 10, "visible mask of every main page after load = bits of its enabled N_* items (" + std::to_string(nonzero) +
                                         " pages with a non-zero mask)", where);
    int pg1 = 0;                                    // a page that has both an item 1 and an item 11
    for (int pg = 1; pg <= 45 && !pg1; pg++) {
        const u32 e = existingItems(b.pageFile().loadPage(pg));
        if ((e & 2u) && (e & (1u << 11)) && (e & 1u)) pg1 = pg;
    }
    b.loadMainPage(pg1);
    Page rec = b.pageFile().loadPage(pg1);
    Bs4Page& m = b.mainPage();
    bool ch = false;
    m.showItems(11, true, &ch);
    const u32 afterShow = m.visibleMask();
    ch = false;
    m.showItems(11, false, &ch);
    check((afterShow & (1u << 11)) && !(m.visibleMask() & (1u << 11)), "ShowItemsByIndex shows and hides an item, the mask follows (page " + rec.name + ")");
    m.applyMask(0x5, &ch);
    check(m.visibleMask() == (0x5u & existingItems(rec)), "ShowAllItems(mask) shows exactly the masked items and hides all others");
    m.applyMask(0xffffffffu, &ch);
    m.showItems(1, false, &ch);
    check((m.visibleMask() & (1u << 11)) != 0 && !(m.visibleMask() & 2u), "item 1 does not touch item 11 (prefix `1_` vs `11_`)");
}

void selfSaveRoundTrip(Rig& r) {
    Bs4& b = r.b();
    b.loadMainPage(b.pageNumber("watpur"));
    Bs4Action a = makeAction(0x1a, {b.pageNumber("panelsump"), 442, 72}, "0_sumps1");
    b.dispatchAction(a);
    Bs4Action x = makeAction(0x17, {0, 0, 0, 0x6, 0, 0});
    b.dispatchAction(x);
    std::vector<u8> s1 = b.saveSection();
    check(s1.size() == 0x480, "save section is 0x480 bytes");
    const u32 mask = b.mainPage().visibleMask();
    const std::string pn = b.panelPage().name();
    Rig r2;
    r2.host.world = r.host.world;
    r2.host.fake = r.host.fake;
    r2.open(s1.data(), s1.size());
    std::vector<u8> s2 = r2.b().saveSection();
    const bool same = s1 == s2;
    size_t diff = 0;
    for (size_t i = 0; i < s1.size() && i < s2.size(); i++) if (s1[i] != s2[i]) { diff = i; break; }
    check(same, "save -> load -> save gives the same 0x480 bytes", same ? "" : "first difference at " + hex(static_cast<u32>(diff)));
    check(r2.b().mainPage().name() == "watpur" && r2.b().mainPage().visibleMask() == mask && r2.b().panelActive() &&
              r2.b().panelPage().name() == pn && r2.b().panelOpener() == "0_sumps1" && r2.b().panelRect() == b.panelRect(),
          "the restored terminal has the same page, item mask, panel, opener and rectangle");
    check(r2.b().backBuffer().px == b.backBuffer().px, "the restored terminal composes the same picture");
    std::vector<u8> bad(0x47f, 0);
    bool threw = false;
    try { Rig r3; r3.open(bad.data(), bad.size()); } catch (const Error&) { threw = true; }
    check(threw, "a section of the wrong size is refused");
}

void selfSlider(Rig& r) {
    check(bs4w::sliderSeconds(24) == 0 && bs4w::sliderSeconds(24 + 6) == 3600 && bs4w::sliderSeconds(24 + 6 * 24) == 86400 &&
              bs4w::sliderSeconds(25) == 600,
          "schedule slider: seconds = x * 600 - 0x3840, x = 24 is 0:00, 6 px = 1 h, 1 px = 10 min");
    bool ok = true;
    for (u32 h = 0; h < 24 && ok; h++)
        for (u32 m = 0; m < 60 && ok; m += 10) {
            const int x = bs4w::sliderX(h, m);
            const u32 secs = bs4w::sliderSeconds(x);
            ok = secs / 3600 == h && (secs % 3600) / 60 == m && x >= 24 && x <= 168;
        }
    check(ok && bs4w::sliderX(0, 4) == 24 && bs4w::sliderX(0, 5) == 25 && bs4w::sliderX(1, 0) == 30, "marker x <-> time round trip, minutes round to 10");
    // a drag on a real panel page: the markers are the images of `panelah` (17 wide); the page code is driven through its handler protocol
    Bs4& b = r.b();
    b.loadMainPage(b.pageNumber("pollut"));
    Bs4Action a = makeAction(0x1a, {b.pageNumber("panelah"), 442, 72}, "5_ahs2");
    r.host.logs.clear();
    b.dispatchAction(a);
    Bs4Page& p = b.panelPage();
    p.feature = "fakesched";
    r.host.fake[{"fakesched", 1}] = {1, 0};
    p.devOn = true;
    p.markOn = 7; p.markOff = 8;                      // the 3_tabhi / 6_tabhi images stand in for the marker images
    p.sliderY = 100;
    p.schedule = Bs4ScheduleSlider();
    p.schedule.onText = Bs4Rect{2, 2, 30, 14}; p.schedule.offText = Bs4Rect{40, 2, 70, 14};
    p.schedule.onHours = 6; p.schedule.onMinutes = 0; p.schedule.offHours = 18; p.schedule.offMinutes = 30;
    bs4w::scheduleSlider(p, 1);
    const int x1 = p.schedule.markerOn.left, x2 = p.schedule.markerOff.left;
    check(x1 == 24 + 36 && x2 == 24 + 6 * 18 + 3 && p.schedule.markerOn.top == 100, "schedule slider phase 1 puts the markers at x = 60 (6:00) and 135 (18:30)",
          std::to_string(x1) + " " + std::to_string(x2));
    r.host.events.clear();
    auto mouse = [&](int ev, int x, int y) {
        p.mouseEvent = ev; p.mouseX = x; p.mouseY = y; p.mayContinue = 1;
        bs4w::scheduleSlider(p, 3);
    };
    mouse(3, x1 + 2, 103);                            // press on the first marker
    const bool dragging = p.schedule.dragOn && p.mayContinue == 0;
    mouse(1, x1 + 2 + 12, 103);                       // move 12 px = 2 hours
    mouse(2, x1 + 2 + 12, 103);                       // release: both parameters are written
    std::string e3, e6;
    for (const std::string& e : r.host.events) {
        if (e.rfind("set fakesched 3 ", 0) == 0) e3 = e;
        if (e.rfind("set fakesched 6 ", 0) == 0) e6 = e;
    }
    const u32 on = 8 * 3600, off = 18 * 3600 + 30 * 60, dur = off - on;
    check(dragging && e3 == "set fakesched 3 " + std::to_string(on) + " 0 0" && e6 == "set fakesched 6 " + std::to_string(dur) + " 0 0",
          "schedule slider drag: TIME_ACTIVATE = 8:00 and DAILY_TIME_LIMIT = 10.5 h in seconds", e3 + " | " + e6);
}

void selfPanels(Rig& r) {
    Bs4& b = r.b();
    // panelah (the shipped example): items 0x11 / 0x12 follow FEATURE_OPEN of the variant's feature
    for (int open = 0; open <= 1; open++) {
        r.host.fake[{"fs6htch", 10}] = {static_cast<u32>(open), 0};
        b.loadMainPage(b.pageNumber("pollut"));
        int cx = 0, cy = 0;
        bool found = findHotspot(b.mainPage(), "techah", cx, cy);        // enables the group of items 5 on the main page ...
        if (found) mouseClick(r, cx, cy);
        found = found && findHotspot(b.mainPage(), "5_ahs2", cx, cy);    // ... which contains the hotspot that opens the panel
        if (found) mouseClick(r, cx, cy);
        const u32 pm = b.panelActive() ? b.panelPage().visibleMask() : 0;
        check(found && b.panelActive() && b.panelPage().name() == "panelah" && b.panelPage().feature == "fs6htch" &&
                  ((pm >> 17) & 1u) == static_cast<u32>(open) && ((pm >> 18) & 1u) == static_cast<u32>(!open) && b.highlightName() == "hiahs1",
              std::string("panelah: variant 3 reads FEATURE_OPEN = ") + std::to_string(open) + " of fs6htch and shows the open / closed item");
    }
    // a panel with a real control: the click on 2_turnon / 1_turnoff sends Event 26 FEATURE_ON through the setter
    // the real panelalgs (packet P5, bs4panel_algs.cpp) reads these through the fake table: ON, MALFUNCTION, schedule start / length
    r.host.fake[{"fbo3algae", 1}] = {0, 0};
    r.host.fake[{"fbo3algae", 7}] = {0, 0};
    r.host.fake[{"fbo3algae", 3}] = {6 * 3600, 0};
    r.host.fake[{"fbo3algae", 6}] = {3600, 0};
    r.host.fake[{"fbo3algae", 2}] = {0, 0};
    b.loadMainPage(b.pageNumber("watpur"));
    int cx = 0, cy = 0;
    bool f0 = findHotspot(b.mainPage(), "techalg", cx, cy);
    if (f0) mouseClick(r, cx, cy);
    f0 = f0 && findHotspot(b.mainPage(), "7_algo1", cx, cy);
    if (f0) mouseClick(r, cx, cy);
    const bool opened = f0 && b.panelActive() && b.panelPage().name() == "panelalgs";
    r.host.events.clear();
    int px = 0, py = 0;
    const bool f = opened && findHotspot(b.panelPage(), "2_turnon", px, py);
    if (f) mouseClick(r, b.panelRect().left + px, b.panelRect().top + py);
    check(opened && f && !r.host.events.empty() && r.host.events.back() == "set fbo3algae 1 1 0 0",
          "a real click on 2_turnon of panelalgs sends set FEATURE_ON = 1 (event 26)", r.host.events.empty() ? "no event" : r.host.events.back());
    const u32 pm = b.panelActive() ? b.panelPage().visibleMask() : 0;
    check((pm & 2) && !(pm & 4), "the panel shows the 'on' item afterwards (the verb 0x17 of the hotspot changed the mask)");
    r.host.events.clear();
    if (findHotspot(b.panelPage(), "1_turnoff", px, py)) mouseClick(r, b.panelRect().left + px, b.panelRect().top + py);
    check(!r.host.events.empty() && r.host.events.back() == "set fbo3algae 1 0 0 0", "a click on 1_turnoff sends FEATURE_ON = 0");
}

int cmdSelftest(Rig& r) {
    selfVerbTable(r);
    selfCompose(r);
    selfMasks(r);
    selfSaveRoundTrip(r);
    selfSlider(r);
    selfPanels(r);
    std::printf("selftest: %d passed, %d failed\n", gPass, gFail);
    return gFail ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        Rig r;
        std::string script, sav;
        bool game = false;
        for (int i = 1; i < argc; i++) {
            std::string a = argv[i];
            if (a == "--static") Bs4Registry::setPageSpecific(false);
            else if (a == "--game") {
                game = true;
                sav = BIOSYS_DEV_ROOT "/setup-cd/installed/DATA1/NewGame.sav";
                if (i + 1 < argc && std::string(argv[i + 1]).find(".sav") != std::string::npos) sav = argv[++i];
            } else if (a == "--clock" && i + 1 < argc) r.host.clock = argv[++i];
            else if (a == "--fixes" && i + 1 < argc) {          // --fixes a,-b,restored: Bs4Fixes words
                std::istringstream ws(argv[++i]);
                std::string w;
                while (std::getline(ws, w, ','))
                    if (!w.empty() && !r.fixes.applyWord(w)) throw Error("unknown terminal fix: " + w);
            } else if (a == "--gamefixes" && i + 1 < argc) r.gameFixes = argv[++i];
            else script = a;
        }
        if (game) r.startGame(sav);
        r.session.initialPassword = true;                   // start on menuscreen like a session after the first password
        r.open();
        std::ifstream f;
        if (!script.empty()) f.open(script);
        std::istream& in = script.empty() ? std::cin : f;
        std::string line;
        int rc = 0;
        while (std::getline(in, line)) {
            std::istringstream is(line);
            std::string cmd;
            if (!(is >> cmd) || cmd[0] == '#') continue;
            if (cmd == "load") {
                std::string p; is >> p;
                const bool ok = r.b().loadMainPage(pageByArg(r.b(), p));
                std::printf("load %s = %s\n", p.c_str(), ok ? "ok" : "failed");
            } else if (cmd == "panel") {
                std::string p, opener; int x = 442, y = 72; is >> p >> x >> y >> opener;
                if (!is) { x = 442; y = 72; }
                Bs4Action a = makeAction(0x1a, {pageByArg(r.b(), p), x, y}, opener);
                std::printf("panel %s = %s\n", p.c_str(), r.b().dispatchAction(a) ? "ok" : "failed");
            } else if (cmd == "act") {
                int verb = 0; is >> std::hex >> verb >> std::dec;
                Bs4Action a = makeAction(verb);
                std::string tok; int k = 0;
                while (is >> tok) {
                    if (tok.rfind("name=", 0) == 0) a.name = tok.substr(5);
                    else if (k < 8) a.arg[k++] = static_cast<int>(std::stol(tok, nullptr, 0));
                }
                std::printf("act %x = %s\n", verb, r.b().dispatchAction(a) ? "ok" : "failed");
            } else if (cmd == "move" || cmd == "down" || cmd == "up" || cmd == "click") {
                int x, y; is >> x >> y;
                if (cmd == "move") r.b().onMouseMove(0, x, y);
                else if (cmd == "down") r.b().onLButtonDown(1, x, y);
                else if (cmd == "up") r.b().onLButtonUp(0, x, y);
                else mouseClick(r, x, y);
            } else if (cmd == "clickh") {
                std::string n; is >> n;
                int cx = 0, cy = 0;
                if (findHotspot(r.b().mainPage(), n, cx, cy)) { mouseClick(r, cx, cy); std::printf("clickh %s main %d %d\n", n.c_str(), cx, cy); }
                else if (r.b().panelActive() && findHotspot(r.b().panelPage(), n, cx, cy)) {
                    mouseClick(r, r.b().panelRect().left + cx, r.b().panelRect().top + cy);
                    std::printf("clickh %s panel %d %d\n", n.c_str(), cx, cy);
                } else std::printf("clickh %s not found\n", n.c_str());
            } else if (cmd == "key") { int v; is >> v; r.b().onKeyDown(v, 1, 0); }
            else if (cmd == "keyup") { int v; is >> v; r.b().onKeyUp(v, 1, 0); }
            else if (cmd == "type") { std::string t; is >> t; for (char c : t) r.b().onKeyDown(static_cast<unsigned char>(c), 1, 0); }
            else if (cmd == "advance") {
                int ms; is >> ms;
                while (ms > 0) { const int s = std::min(ms, 500); r.host.now += static_cast<u32>(s); ms -= s; r.b().onTimer(); r.b().onIdle(); }
            } else if (cmd == "set") {
                std::string f2, ids; long iv; double dv; is >> f2 >> ids >> iv >> dv;
                const int id = static_cast<int>(std::stol(ids, nullptr, 0));
                r.host.fake[{f2 == "-" ? "" : f2, id}] = {static_cast<u32>(iv), dv};
            } else if (cmd == "get") {
                std::string f2, ids; is >> f2 >> ids;
                const int id = static_cast<int>(std::stol(ids, nullptr, 0));
                u32 i = 0; double d = 0;
                const bool ok = r.host.onGetFeatureParameter(f2 == "-" ? "" : f2, id, i, d);
                std::printf("get %s %d = %s int=%d dbl=%s\n", f2.c_str(), id, ok ? "ok" : "no", static_cast<i32>(i), num(d).c_str());
            } else if (cmd == "shot" || cmd == "shotbb") { std::string p; is >> p; writeShot(r, p, cmd == "shotbb"); }
            else if (cmd == "term") {       // term: the rows of the power terminal panel (bs4panel_term.cpp): row sum, drawn text, rectangle
                int n = 0;
                for (const bs4term::Row& w : bs4term::rows())
                    std::printf("term row %d sum %.17g text '%s' rect %d,%d,%d,%d\n", n++, w.sum, w.text.c_str(), w.left, w.top, w.right, w.bottom);
                std::printf("term rows %d\n", n);
            } else if (cmd == "state") printState(r);
            else if (cmd == "textentry") { r.b().textEntry = true; r.b().textBuffer.clear(); }
            else if (cmd == "text") std::printf("text [%s] entry=%d\n", r.b().textBuffer.c_str(), r.b().textEntry ? 1 : 0);
            else if (cmd == "hook-ah") {}      // (the variant hook is the real one of bs4hooks.cpp now)
            else if (cmd == "mask") {
                std::string w; is >> w;
                const Bs4Page& p = w == "panel" ? r.b().panelPage() : r.b().mainPage();
                std::printf("mask %s %s\n", w.c_str(), hex(p.visibleMask()).c_str());
            } else if (cmd == "events") { for (auto& e : r.host.events) std::printf("%s\n", e.c_str()); r.host.events.clear(); }
            else if (cmd == "logs") { for (auto& e : r.host.logs) std::printf("log %s\n", e.c_str()); r.host.logs.clear(); }
            else if (cmd == "clock") { std::getline(is, r.host.clock); if (!r.host.clock.empty() && r.host.clock[0] == ' ') r.host.clock.erase(0, 1); }
            else if (cmd == "panelrect") { const Bs4Rect q = r.b().panelRect(); std::printf("panelrect %d %d %d %d\n", q.left, q.top, q.right, q.bottom); }
            else if (cmd == "hotspots") {         // hotspots [main|panel]
                std::string w; is >> w;
                const Bs4Page& p = w == "panel" ? r.b().panelPage() : r.b().mainPage();
                for (size_t i = 0; i < p.rec.hotspots.size(); i++) {
                    const Hotspot& h = p.rec.hotspots[i];
                    std::printf("hot %zu %s %d %d %d %d flags=%u verb=%x arg0=%d\n", i, h.name.c_str(), h.left, h.top, h.right, h.bottom, h.flags, h.action.verb, h.action.arg[0]);
                }
            } else if (cmd == "save") {
                std::string p; is >> p;
                std::vector<u8> s = r.b().saveSection();
                std::ofstream o(p, std::ios::binary);
                o.write(reinterpret_cast<const char*>(s.data()), static_cast<std::streamsize>(s.size()));
            } else if (cmd == "restore") {
                std::string p; is >> p;
                std::vector<u8> s = readFile(p);
                r.open(s.data(), s.size());
            } else if (cmd == "static") { std::string d; is >> d; rc |= cmdStatic(r, d); }
            else if (cmd == "selftest") rc |= cmdSelftest(r);
            else if (cmd == "echo") { std::string rest; std::getline(is, rest); std::printf("%s\n", rest.c_str() + (rest.empty() ? 0 : 1)); }
            else if (cmd == "p4") std::printf("%s\n", bs4a::debugState(r.b().panelPage()).c_str());     // P4: the panel page widget state
            else if (cmd == "p6") std::printf("%s\n", bs4aw::debugState(r.b().panelPage()).c_str());     // P6: the air / water panel state (bs4panel_aw.h)
            else if (cmd == "p6route") {         // p6route pump|lung FEATURE: exe:004319d0 / exe:00431c30
                std::string kind, f2; is >> kind >> f2;
                int out = -1;
                const bool ok = kind == "pump" ? bs4aw::checkPumpRoute(r.b(), f2, out) : bs4aw::checkLungRoute(r.b(), f2, out);
                std::printf("p6route %s %s %s out=%d\n", kind.c_str(), f2.c_str(), ok ? "ok" : "fail", out);
            } else if (cmd == "phot") {          // the hotspots of the panel page: index name rect flags verb a1..a6
                const Bs4Page& p = r.b().panelPage();
                for (size_t i = 0; i < p.rec.hotspots.size(); i++) {
                    const Hotspot& h = p.rec.hotspots[i];
                    std::printf("phot %zu %s %d %d %d %d flags=%u verb=%x args=%d,%d,%d,%d,%d,%d\n", i, h.name.c_str(), h.left, h.top, h.right, h.bottom, h.flags,
                                h.action.verb, h.action.arg[0], h.action.arg[1], h.action.arg[2], h.action.arg[3], h.action.arg[4], h.action.arg[5]);
                }
            } else if (cmd == "loops") { for (auto& e : r.host.loops) std::printf("loop %s\n", e.c_str()); r.host.loops.clear(); }
            else if (cmd == "power") {           // power 0|1 [hexmask]: --game: clear / set bits of the world's power flags (default bit 30: CWorld::HasPower is 0 for every fuse group without it, so every feature reports a malfunction; 0x8000000 = the dead biome's group)
                unsigned on = 1; unsigned long bits = 0x40000000ul; is >> on;
                if (is >> std::hex >> bits) {}
                if (r.sg) { u32& f = game::cworld::header(r.sg->world).m_dwPowerFlags; f = on ? (f | static_cast<u32>(bits)) : (f & ~static_cast<u32>(bits)); }
            }
            else if (cmd == "tank") {            // tank NAME PERCENT: --game: the water volume of the tank (STANK OTANK DBTANK RFTANK) as a percent of its capacity
                std::string n; double pc = 0; is >> n >> pc;
                game::CWaterTank* t = r.sg ? r.sg->world.findWaterTank(n.c_str()) : nullptr;
                if (t) t->setWaterVol(t->volume() * pc / 100.0); else std::printf("tank %s not found\n", n.c_str());
            }
            else if (cmd == "clicks") { std::printf("clicks"); for (int c : r.host.clicks) std::printf(" %d", c); std::printf("\n"); r.host.clicks.clear(); }
            else if (cmd == "p3") {
                const Bs4Page& p = r.b().mainPage();
                std::printf("p3 page=%s dev=%d alarm=%d sensor=%d row=%d last=%s ph=%d,%d phs=%dx%d text=[%s] mail=%u img=%d pod=%d counter=%d stamp=[%s]\n",
                            p.name().c_str(), p.deviceRow, p.alarmRow, p.probeSensor, p.probeRow, num(p.probeLast).c_str(), p.phPos[0], p.phPos[1],
                            p.phSurface.w, p.phSurface.h, bs4main_debug::probeText().c_str(), r.session.mailState, bs4main_debug::mailImage(),
                            bs4main_debug::podPosition(), p.counter, r.session.mailStamp.c_str());
            } else if (cmd == "overlays" || cmd == "alarms") {
                const std::vector<int> v = cmd == "overlays" ? bs4main_debug::overlayStates() : bs4main_debug::alarmStates();
                std::printf("%s", cmd.c_str());
                for (size_t i = 0; i < v.size(); i++) if (v[i]) std::printf(" %zu=%d", i, v[i]);
                std::printf("\n");
            } else if (cmd == "orn") {
                const Bs4Page& p = r.b().mainPage();
                for (size_t i = 0; i < p.rec.ornaments.size(); i++)
                    if (p.rec.ornaments[i].type == 1) std::printf("orn %zu %s %u\n", i, p.rec.ornaments[i].name.c_str(), p.rec.ornaments[i].flags);
            } else if (cmd == "setp") {
                std::string f2, ids; long a = 0, b = 0, c = 0; is >> f2 >> ids >> a >> b >> c;
                try {
                    const bool ok = r.host.onSetFeatureParameter(f2 == "-" ? "" : f2, static_cast<int>(std::stol(ids, nullptr, 0)), static_cast<int>(a), static_cast<int>(b), static_cast<int>(c));
                    std::printf("setp %s %s = %s\n", f2.c_str(), ids.c_str(), ok ? "ok" : "no");
                } catch (const std::exception& e) {      // the game logic has features whose parameters are not ported
                    std::printf("setp %s %s = throws\n", f2.c_str(), ids.c_str());
                }
            } else if (cmd == "session") {
                std::string what; is >> what;
                if (what == "mail") { unsigned n = 0; is >> n; r.session.mailState = n; }
                else if (what == "stamp") { std::getline(is, r.session.mailStamp); if (!r.session.mailStamp.empty() && r.session.mailStamp[0] == ' ') r.session.mailStamp.erase(0, 1); }
            } else if (cmd == "story") {
                int on = 0; is >> on;
                if (r.sg && r.sg->world.state().sim.story) {
                    u32& m = r.sg->world.state().sim.story->milestones;
                    m = on ? (m | 0x20000000u) : (m & ~0x20000000u);
                } else std::printf("story: no game\n");
            } else if (cmd == "shotpage") {
                std::string path; is >> path;
                Image8 img;
                img.w = r.b().mainPage().surf.w; img.h = r.b().mainPage().surf.h; img.px = r.b().mainPage().surf.px;
                writeBmp(path, img, r.b().palette());
            } else if (cmd == "afteraction") {
                std::string n; is >> n;
                Bs4Action a = makeAction(0x11, {}, n);
                bs4OnEmailListPage(r.b().mainPage(), a);
            }
            else { std::printf("unknown command %s\n", cmd.c_str()); rc = 2; }
        }
        for (auto& l : r.host.logs) std::fprintf(stderr, "bs4 log: %s\n", l.c_str());
        return rc;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bs4test: %s\n", e.what());
        return 1;
    }
}
