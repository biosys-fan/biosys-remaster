// biosys-enginefix: drives the engine with a logging GameLogic for tests/test_enginefix.sh (needs the game data).
//   biosys-enginefix idle     the idle counts the game sees across updates, timer ticks, mouse moves and keys
//   biosys-enginefix media    page change with a playing sprite, movie and sound: callback order and handle reuse
//   biosys-enginefix topbar   when the top bar is opened (start-up, API 0x60, the xTopBar hotspot)
//   biosys-enginefix api      the commands 0x1b 0x40 0x5a 0x5d 0x66 0x68 0x6c 0x6d
//   biosys-enginefix jump     verb 16 with a start position and a post-jump scroll animation (plain path)
// Prints `key=value` lines; tests/test_enginefix.py checks them.
#include "engine/devroot.h"
#include <cstdio>
#include <sys/stat.h>
#include "engine/engine.h"

using namespace biosys;

namespace {

std::string dataDir(const char* argv0) {
    std::string a = argv0 ? argv0 : "";
    size_t slash = a.rfind('/');
    std::string cands[] = {slash == std::string::npos ? "../data" : a.substr(0, slash) + "/../data", "data",
                           BIOSYS_DEV_ROOT "/port/data"};
    for (const std::string& c : cands) {
        struct stat st;
        if (stat(c.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return c;
    }
    return cands[2];
}

struct Log : GameLogic {
    Engine* e = nullptr;
    int idleLimit = 4;          // onIdle(count) returns 1 while count < idleLimit - 1, else 0
    int idleStopAt = -1;        // return 0 at exactly this count (>= 0)
    bool reuseOnLoad = false;   // open a sprite as soon as the new page is loaded (handle reuse)
    int pumps = 0;
    bool quiet = false;

    void out(const std::string& s) { if (!quiet) std::printf("ev %s\n", s.c_str()); }
    int onStartup() override { out("startup"); return 0; }
    int onIdle(int count) override {
        if (!quiet) std::printf("idle %d\n", count);
        if (idleStopAt >= 0 && count == idleStopAt) return 0;
        return count < idleLimit - 1 ? 1 : 0;
    }
    void onExitPage() override { out("exit"); }
    void onPageLoaded(const std::string& p, int w) override {
        out("loaded " + p + " " + std::to_string(w));
        if (reuseOnLoad && w == -1) {
            int h = -1;
            int rc = e->api().OpenSpriteAnimation("anims\\OBJ_UP\\kukup.tan", -1, 100, 20, 1, 100, true, false, false, "", 0, 0, false, -1, false, h);
            out("reuse-sprite rc=" + std::to_string(rc) + " handle=" + std::to_string(h));
        }
    }
    void onPageDrawn(const std::string& p, int w) override { out("drawn " + p + " " + std::to_string(w)); }
    void onPopupOpened(const std::string& p, int i) override { out("popupopened " + p + " " + std::to_string(i)); }
    void onEnterPage(const std::string& p) override { out("enter " + p + " pumps=" + std::to_string(pumps)); }
    void onSpriteEvent(int h, int c, int) override { out("sprite " + std::to_string(h) + " " + std::to_string(c)); }
    void onMovieEvent(int h, int c, int) override { out("movie " + std::to_string(h) + " " + std::to_string(c)); }
    void onSoundEvent(int h, int c) override { out("sound " + std::to_string(h) + " " + std::to_string(c)); }
};

void kv(const char* key, long v) { std::printf("%s=%ld\n", key, v); }

void modeIdle(Engine& e, Log& l) {
    l.quiet = true;
    e.startup();
    e.startNewGame();
    l.quiet = false;
    // each marker line separates the steps; every update() is one idle pass of MFC's loop
    u32 t = 1000;
    auto upd = [&](const char* tag) { std::printf("-- %s\n", tag); t += 16; e.update(t); };
    e.update(t);                       // first pass: count 0 (the start restarts the count)
    for (int i = 0; i < 5; i++) upd("idle only");
    e.mouseMove(100, 100, 0, 0);
    for (int i = 0; i < 5; i++) upd("after a mouse move");
    e.mouseMove(100, 100, 0, 0);       // the same position again: MFC's IsIdleMessage says no
    upd("redundant mouse move");
    e.mouseMove(101, 100, 0, 0);
    upd("moved mouse");
    upd("moved mouse, next pass");
    e.keyDown(Key::Other, 0x41);
    upd("key down");
    e.keyUp(Key::Other, 0x41);
    upd("key up");
    e.mouseDown(Button::Left);
    upd("button down");
    e.mouseUp(Button::Left);
    upd("button up");
    // WM_TIMER at +500 ms from the first update
    t = 1000 + 500 - 32;
    upd("before the timer");
    t = 1500;
    std::printf("-- timer tick\n");
    e.update(t);
    upd("after the timer tick");
    // a result of 0 at count 1 silences the game until the next restart
    l.idleStopAt = 1;
    e.mouseMove(150, 100, 0, 0);
    for (int i = 0; i < 4; i++) upd("result 0 at count 1");
    e.mouseMove(160, 100, 0, 0);
    for (int i = 0; i < 4; i++) upd("restart after the silence");
}

void modeMedia(Engine& e, Log& l) {
    e.startup();
    e.startNewGame();
    e.goToPage(e.view().findPage("O1"), false);
    e.update(1000);
    EngineApi& a = e.api();
    int sp = -1, mv = -1, sd = -1;
    std::printf("open-sprite rc=%d\n", a.OpenSpriteAnimation("anims\\OBJ_UP\\kukup.tan", -1, 100, 20, 1, 100, true, true, false, "", 0, 0, false, -1, false, sp));
    std::printf("play-sprite rc=%d\n", a.PlaySpriteAnimation(sp, true, false));
    std::printf("open-movie rc=%d\n", a.OpenAVIMovie("movies\\intro\\jumpstrt.avi", -1, 100, 100, true, true, 0, nullptr, mv));
    std::printf("play-movie rc=%d\n", a.PlayAVIMovie(mv, true));
    std::printf("open-sound rc=%d\n", a.OpenSound("sounds\\house\\squelch3.wav", 1, true, 0, 0, 0, sd));
    std::printf("play-sound rc=%d\n", a.PlaySound(sd, true));
    std::printf("handles sprite=%d movie=%d sound=%d\n", sp, mv, sd);
    e.update(1016);
    e.update(1100);
    std::printf("-- page change\n");
    l.reuseOnLoad = true;
    e.goToPage(e.view().findPage("R4"), false);
    std::printf("-- after the page change\n");
    e.update(1200);
    e.update(1300);
    std::printf("-- end\n");
}

void modeTopbar(Engine& e, Log& l) {
    std::printf("-- startup\n");
    e.startup();
    kv("topbar-open-after-startup", e.view().topbarPage() != nullptr);
    kv("window0-after-startup", e.pageOfWindow(0) != nullptr);
    std::printf("-- DisplayPopupPage topbar\n");
    int rc = e.api().DisplayPopupPage("topbar");
    kv("rc-first", rc);
    kv("topbar-open", e.view().topbarPage() != nullptr);
    std::printf("-- DisplayPopupPage topbar again\n");
    rc = e.api().DisplayPopupPage("topbar");
    kv("rc-second", rc);
    std::printf("-- end\n");
}

void modeTopbarHotspot(Engine& e, Log& l) {
    e.startup();
    l.quiet = true;
    // the intro page has the hotspot xTopBar (verb 9, page "topbar")
    std::string found;
    for (int n = 1; n <= e.view().pageCount() && found.empty(); n++) {
        if (e.view().isPopupPage(n)) continue;
        e.goToPage(n, false);
        for (const Hotspot& h : e.view().page().hotspots)
            if (iequals(h.name, "xTopBar")) { found = e.view().page().name; kv("xtopbar-verb", h.action.verb); kv("xtopbar-target", h.action.arg[0]); break; }
    }
    std::printf("xtopbar-page=%s\n", found.c_str());
    kv("topbar-open-before", e.view().topbarPage() != nullptr);
    l.quiet = false;
    std::printf("-- DoHotspotAction xTopBar\n");
    kv("rc", e.api().DoHotspotAction(-1, "xTopBar"));
    kv("topbar-open-after", e.view().topbarPage() != nullptr);
    kv("topbar-window", e.view().topbarPage() ? e.pageOfWindow(0) == e.view().topbarPage() : -1);
    std::printf("-- end\n");
}

void modeApi(Engine& e, Log& l) {
    l.quiet = true;
    e.startup();
    e.startNewGame();
    EngineApi& a = e.api();
    e.update(1000);
    // 0x1b
    std::string dir;
    kv("getdatadir-rc", a.GetDataDirectory(dir));
    std::printf("datadir=%s\n", dir.c_str());
    kv("datadir-resolves", !e.files().findData(dir + "\\touchinf.dat").empty() && e.files().findData(dir + "\\touchinf.dat") == e.files().findData("touchinf.dat"));
    kv("datadir-fonts-sep", e.files().findData(dir + "\\palettes").empty() == e.files().findData("palettes").empty());
    // 0x66 SetCursorOverride: find a hotspot with a non-default cursor
    int found = 0, fx = 0, fy = 0, fpage = 0;
    for (int n = 1; n <= e.view().pageCount() && !found; n++) {
        if (e.view().isPopupPage(n)) continue;
        e.goToPage(n, false);
        if (e.view().fullScreen()) continue;
        for (int y = 60; y < 420 && !found; y += 6)
            for (int x = 4; x < 640 && !found; x += 6) {
                e.mouseMove(x, y, 0, 0);
                if (e.cursorCode() != 0) { found = 1; fx = x; fy = y; fpage = n; }
            }
    }
    const int code0 = e.cursorCode();
    kv("cursor-code", code0);
    kv("override-rc", a.SetCursorOverride(true));
    kv("cursor-code-override", e.cursorCode());
    kv("override-off-rc", a.SetCursorOverride(false));
    kv("cursor-code-off", e.cursorCode());
    a.SetCursorOverride(true);
    e.goToPage(fpage, false);
    e.mouseMove(fx, fy, 0, 0);
    kv("cursor-code-after-page-load", e.cursorCode());
    kv("cursor-code-expected", code0);
    // 0x40
    kv("visible", e.cursorVisible());
    kv("showcursor-rc", a.ShowCursor(false));
    kv("visible-hidden", e.cursorVisible());
    a.ShowCursor(true);
    kv("visible-shown", e.cursorVisible());
    a.ShowCursor(false);
    e.goToPage(e.view().findPage("R4"), false);
    kv("visible-after-page", e.cursorVisible());
    // 0x68
    Rect r;
    kv("clip-auto", e.cursorClipRect(r));
    std::printf("clip-auto-rect=%d,%d,%d,%d\n", r.x, r.y, r.w, r.h);
    const int box[4] = {10, 70, 200, 300};
    kv("clip-rc", a.SetCursorClipRect(true, box));
    kv("clip-manual", e.cursorClipRect(r));
    std::printf("clip-manual-rect=%d,%d,%d,%d\n", r.x, r.y, r.w, r.h);
    a.SetCursorClipRect(true, nullptr);
    kv("clip-null", e.cursorClipRect(r));
    a.SetCursorClipRect(false, nullptr);
    kv("clip-auto-again", e.cursorClipRect(r));
    // 0x5d
    kv("viewtop", e.view().viewTop());
    kv("move-0-0", a.MoveMainWindowView(0, 0));
    kv("viewtop-0", e.view().viewTop());
    kv("move-0-112", a.MoveMainWindowView(0, 112));
    kv("viewtop-112", e.view().viewTop());
    kv("move-0-113", a.MoveMainWindowView(0, 113));
    kv("viewtop-still-112", e.view().viewTop());
    kv("move-1-56", a.MoveMainWindowView(1, 56));
    kv("move-0--1", a.MoveMainWindowView(0, -1));
    kv("move-back", a.MoveMainWindowView(0, 56));
    kv("viewtop-back", e.view().viewTop());
    // 0x5a / 0x6c / 0x6d on the panorama O1 (2560 x 1024)
    e.goToPage(e.view().findPage("O1"), false);
    e.setPointerCapture(true);
    e.setScroll(0, 100);
    u32 t = 2000;
    auto pass = [&](int n) { for (int i = 0; i < n; i++) { t += 16; e.update(t); } };
    int act = -1, u8v = -1, ac = -1, u14 = -1;
    kv("state-rc", a.GetScrollState(act, u8v, ac, u14));
    kv("state-active", act);
    kv("limits-bad-order", a.SetPanoramaScrollLimits(300, 200, 0));
    kv("limits-too-wide", a.SetPanoramaScrollLimits(100, 2560, 0));
    kv("limits-rc", a.SetPanoramaScrollLimits(1000, 1200, 0));
    kv("scrollx-after-limits", e.view().scrollX());
    e.mouseMove(320, 240, 60, 0);
    pass(40);
    kv("scrollx-right", e.view().scrollX());
    e.mouseMove(320, 240, -60, 0);
    pass(10);
    e.mouseMove(320, 240, -60, 0);
    pass(10);
    e.mouseMove(320, 240, -60, 0);
    pass(40);
    kv("scrollx-left", e.view().scrollX());
    kv("limits-clear", a.SetPanoramaScrollLimits(-1, -1, 0));
    e.mouseMove(320, 240, -60, 0);
    pass(40);
    kv("scrollx-free", e.view().scrollX());
    kv("vlock-rc", a.SetPanoramaScrollLimits(-1, -1, 1));
    kv("scrolly-locked", e.view().scrollY());
    e.mouseMove(320, 240, 0, 40);
    pass(40);
    kv("scrolly-locked-after", e.view().scrollY());
    kv("vlock-off", a.SetPanoramaScrollLimits(-1, -1, 0));
    e.mouseMove(320, 240, 0, 40);
    pass(40);
    kv("scrolly-free", e.view().scrollY());
    // the limits are dropped by the next primary page load
    a.SetPanoramaScrollLimits(1000, 1200, 0);
    e.goToPage(e.view().findPage("O1"), false);
    e.setScroll(0, 100);
    e.mouseMove(320, 240, 20, 0);
    pass(10);
    kv("limits-after-reload", e.view().scrollX() < 1000);
    // 0x6c: mouse delta flag, Ctrl, scroll mode off
    a.EnableMouseDeltaEvent(true);
    a.GetScrollState(act, u8v, ac, u14);
    kv("state-active-2", act);
    kv("state-mousedelta", u8v);
    kv("state-released", ac);
    e.keyDown(Key::Ctrl);
    act = -1;
    a.GetScrollState(act, u8v, ac, u14);
    kv("state-active-ctrl", act);
    e.keyUp(Key::Ctrl);
    a.SetScrollMode(false);
    a.GetScrollState(act, u8v, ac, u14);
    kv("state-active-mode-off", act);
    a.SetScrollMode(true);
    // 0x6d: vertical release at the top
    e.setScroll(500, 0);
    kv("options-rc", a.SetScrollOptions(1, 1));
    e.mouseMove(320, 240, 0, -20);
    pass(3);
    a.GetScrollState(act, u8v, ac, u14);
    kv("release-active", act);
    kv("release-released", ac);
    kv("release-top", u14);
    e.mouseMove(320, 100, 5, 0);
    int px = 0, py = 0;
    e.pointerPos(px, py);
    kv("release-pointer-y", py);
    kv("options-top-off", a.SetScrollOptions(0, 1));
    ac = -1;
    a.GetScrollState(act, u8v, ac, u14);
    kv("release-cleared", ac);
    e.pointerPos(px, py);
    kv("pointer-y-back", py);
    // bottom
    e.setScroll(500, 1024 - 368);
    a.SetScrollOptions(0, 1);
    e.mouseMove(320, 240, 0, 40);
    pass(3);
    a.GetScrollState(act, u8v, ac, u14);
    kv("release-bottom", ac);
    kv("release-bottom-top", u14);
    e.update(t += 16);
}

void modeJump(Engine& e, Log& l) {
    e.startup();
    l.quiet = true;
    e.startNewGame();
    // No shipped hotspot sets a6 / a7, so a jump hotspot (verb 2) whose target is a scrollable page is turned into verb 16 in
    // memory: start position (500, 100), no cross-fade, post-jump scroll animation (96, 0).
    std::string page, hot;
    int target = 0;
    for (int n = 1; n <= e.view().pageCount() && page.empty(); n++) {
        if (e.view().isPopupPage(n)) continue;
        e.goToPage(n, false);
        for (const Hotspot& h : e.view().page().hotspots) {
            if (h.action.verb != VERB_JUMP || h.action.arg[0] < 1 || h.action.arg[0] > e.view().pageCount()) continue;
            if (e.view().isPopupPage(h.action.arg[0])) continue;
            Hotspot& m = const_cast<Hotspot&>(h);
            page = e.view().page().name;
            hot = m.name;
            target = m.action.arg[0];
            m.action.verb = VERB_JUMP_SCROLL;
            m.action.arg[2] = 500; m.action.arg[3] = 100; m.action.arg[4] = 0; m.action.arg[5] = 96; m.action.arg[6] = 0;
            break;
        }
        if (!page.empty()) {
            e.goToPage(target, false);
            if (!e.view().scrollable()) { page.clear(); }          // keep looking: the target must be scrollable
        }
    }
    if (page.empty()) { std::printf("no-hotspot\n"); return; }
    std::printf("hotspot page=%s name=%s target=%d\n", page.c_str(), hot.c_str(), target);
    e.setPump([&](u32& now) { l.pumps++; now += 16; return true; });
    e.goToPage(e.view().findPage(page), false);
    l.quiet = false;
    std::printf("-- action\n");
    kv("rc", e.api().DoHotspotAction(-1, hot));
    kv("pumps", l.pumps);
    kv("page", e.view().pageNumber() == target);
    kv("scrollx", e.view().scrollX());
    kv("scrolly", e.view().scrollY());
    std::printf("-- end\n");
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::string mode = argc > 1 ? argv[1] : "";
        Log logic;
        Engine e(DataFiles::fromEnvironment(), dataDir(argv[0]), &logic, nullptr);
        logic.e = &e;
        if (mode == "idle") modeIdle(e, logic);
        else if (mode == "media") modeMedia(e, logic);
        else if (mode == "topbar") modeTopbar(e, logic);
        else if (mode == "topbar-hotspot") modeTopbarHotspot(e, logic);
        else if (mode == "api") modeApi(e, logic);
        else if (mode == "jump") modeJump(e, logic);
        else {
            std::fprintf(stderr, "usage: biosys-enginefix idle|media|topbar|topbar-hotspot|api|jump\n");
            return 2;
        }
        return 0;
    } catch (const Error& err) {
        std::fprintf(stderr, "biosys-enginefix: %s\n", err.what());
        return 1;
    }
}
