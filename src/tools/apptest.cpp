// build/biosys-apptest: the pure logic of the window's preferences, autosave, pause, mouse sensitivity and save-slot paging, without a
// window (src/app/prefs.h, autosave.h, windowlogic.h). Prints PASS / FAIL lines; exit 1 if any failed.
#include "app/autosave.h"
#include "app/prefs.h"
#include "app/windowlogic.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace biosys;

static int failed = 0;
static void check(const std::string& name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
    if (!ok) failed++;
}

// a small option set shaped like main.cpp's table
struct Mini {
    bool fullscreen = false, autosave = true;
    int volume = 100, minutes = 10;
    std::string panorama = "original";
    bool cliOnlyFlag = false;
};

static OptTable<Mini> miniTable() {
    OptTable<Mini> t;
    OptSpec<Mini> s;
    s.cli = "--fullscreen"; s.key = "fullscreen"; s.kind = OptKind::Switch; s.def = "no"; s.help = "Full screen.";
    s.set = [](Mini& o, const std::string& v) { o.fullscreen = v == "yes"; };
    s.get = [](const Mini& o) { return prefBoolText(o.fullscreen); };
    t.specs.push_back(s);
    s = OptSpec<Mini>();
    s.cli = "--volume"; s.key = "volume"; s.def = "100"; s.help = "Master volume.";
    s.set = [](Mini& o, const std::string& v) {
        char* end = nullptr;
        const long n = std::strtol(v.c_str(), &end, 10);
        if (v.empty() || *end || n < 0 || n > 100) throw Error("bad --volume (want 0..100): " + v);
        o.volume = static_cast<int>(n);
    };
    s.get = [](const Mini& o) { return std::to_string(o.volume); };
    t.specs.push_back(s);
    s = OptSpec<Mini>();
    s.cli = "--panorama"; s.key = "panorama"; s.def = "original"; s.help = "Panorama mode.";
    s.set = [](Mini& o, const std::string& v) {
        if (v != "original" && v != "smooth") throw Error("bad --panorama");
        o.panorama = v;
    };
    s.get = [](const Mini& o) { return o.panorama; };
    t.specs.push_back(s);
    s = OptSpec<Mini>();
    s.cli = "--script"; s.kind = OptKind::Value;      // command line only
    s.set = [](Mini& o, const std::string&) { o.cliOnlyFlag = true; };
    t.specs.push_back(s);
    return t;
}

static std::string tmpDir() {
    char tmpl[] = "/tmp/biosys-apptest-XXXXXX";
    const char* d = mkdtemp(tmpl);
    return d ? d : "/tmp";
}
static void put(const std::string& p, const std::string& t) {
    if (std::FILE* f = std::fopen(p.c_str(), "wb")) { std::fwrite(t.data(), 1, t.size(), f); std::fclose(f); }
}

int main() {
    // ---- preferences: parse
    {
        const PrefFile f = parsePrefs("# comment\n\n  Volume =  40  \n; also a comment\nfullscreen=YES\nbogus line\n = nokey\npanorama = \"smooth\"\nvolume = 50\n");
        check("parse: five entries, two problems", f.entries.size() == 4 && f.problems.size() == 2);
        check("parse: keys are lower case and trimmed, values trimmed, quotes dropped", f.entries[0].key == "volume" && f.entries[0].value == "40" &&
              f.entries[1].key == "fullscreen" && f.entries[1].value == "YES" && f.entries[2].value == "smooth");
        check("parse: line numbers", f.entries[0].line == 3 && f.entries[3].line == 9 && f.problems[0].find("line 6") == 0);
    }
    // ---- apply: precedence, unknown keys, bad values
    {
        const OptTable<Mini> t = miniTable();
        Mini m;
        std::vector<std::string> problems;
        t.apply(m, parsePrefs("volume = 40\nfullscreen = yes\npanorama = smooth\nzoom = 3\nzoom = 4\nscript = x\nvolume = 150\n"), problems);
        check("apply: the file's values are taken", m.volume == 40 && m.fullscreen && m.panorama == "smooth");
        check("apply: an unknown key is reported once, however often it appears", problems.size() == 3);
        check("apply: a key that is command line only is unknown in the file", !m.cliOnlyFlag);
        check("apply: a bad value is reported with its line and ignored (the earlier value stays)", m.volume == 40 &&
              problems[2].find("line 7") == 0 && problems[2].find("volume") != std::string::npos);
        Mini m2;
        problems.clear();
        t.apply(m2, parsePrefs("fullscreen = maybe\n"), problems);
        check("apply: a switch wants yes / no", !m2.fullscreen && problems.size() == 1);
        // the command line over the file: the caller applies the arguments after the file; the table's setters are the same for both
        Mini m3;
        problems.clear();
        t.apply(m3, parsePrefs("volume = 40\n"), problems);
        t.byCli("--volume")->set(m3, "70");
        check("precedence: command line over file over default", m3.volume == 70);
        check("table lookups: by option and by key, command-line-only options have no key", t.byCli("--volume") == t.byKey("volume") && t.byKey("script") == nullptr &&
              t.byCli("--script") != nullptr && t.keys().size() == 3);
    }
    // ---- default file and write-back round trip
    {
        const OptTable<Mini> t = miniTable();
        const std::string def = t.defaultFile();
        check("default file: every key with its default, commented, and its help line",
              def.find("# volume = 100") != std::string::npos && def.find("# fullscreen = no") != std::string::npos && def.find("# Master volume.") != std::string::npos &&
              def.find("script") == std::string::npos);
        check("default file: nothing is active", parsePrefs(def).entries.empty() && parsePrefs(def).problems.empty());
        Mini m;
        m.volume = 33;
        m.fullscreen = true;
        const std::string upd = updatePrefsText(def, t.currentValues(m));
        Mini back;
        std::vector<std::string> pr;
        t.apply(back, parsePrefs(upd), pr);
        check("write-back round trip: the file gives the same settings again", pr.empty() && back.volume == 33 && back.fullscreen && back.panorama == "original");
        check("write-back: a new line goes right after its commented default", upd.find("# volume = 100\nvolume = 33\n") != std::string::npos);
        check("write-back: the help text is kept", upd.find("# Master volume.") != std::string::npos);
        const std::string hand = "# my notes\nvolume = 10   \nmystery = 1\nfullscreen = no\n";
        const std::string upd2 = updatePrefsText(hand, {{"volume", "20"}, {"fullscreen", "yes"}, {"panorama", "smooth"}});
        check("update: active lines are replaced, unknown lines and comments kept, a missing key appended",
              upd2 == "# my notes\nvolume = 20\nmystery = 1\nfullscreen = yes\npanorama = smooth\n");
        check("update: idempotent", updatePrefsText(upd2, {{"volume", "20"}}) == upd2);
    }
    // ---- autosave planner
    {
        AutosavePlanner p;
        p.setMinutes(10);
        check("planner: nothing due at the start", p.due() == AutosavePlanner::Why::None);
        p.advance(9 * 60000 + 59999);
        check("planner: not due one millisecond before the interval", p.due() == AutosavePlanner::Why::None);
        p.advance(1);
        check("planner: due at the interval, and it stays due until an attempt", p.due() == AutosavePlanner::Why::Periodic);
        p.advance(5000);
        check("planner: still due later (waiting for an allowed moment)", p.due() == AutosavePlanner::Why::Periodic);
        p.attempted();
        check("planner: an attempt (made or failed) restarts the interval", p.due() == AutosavePlanner::Why::None && p.playedMs() == 0);
        p.observeAwake(true);
        p.observeAwake(false);
        check("planner: falling asleep is not a wake", p.due() == AutosavePlanner::Why::None);
        p.observeAwake(false);
        p.observeAwake(true);
        check("planner: waking up makes a wake autosave due", p.due() == AutosavePlanner::Why::Wake);
        p.attempted();
        check("planner: and it is used up", p.due() == AutosavePlanner::Why::None);
        p.setMinutes(0);
        p.advance(10 * 3600000u);
        check("planner: 0 minutes = no timed autosave", p.due() == AutosavePlanner::Why::None);
        p.observeAwake(false);
        p.observeAwake(true);
        check("planner: ... but waking up still counts", p.due() == AutosavePlanner::Why::Wake);
        p.setEnabled(false);
        check("planner: disabled: nothing is due", p.due() == AutosavePlanner::Why::None);
        p.restart();
        p.setEnabled(true);
        p.setMinutes(1);
        p.advance(60000);
        p.restart();
        check("planner: restart (a game was loaded) clears the interval", p.due() == AutosavePlanner::Why::None);
    }
    // ---- autosave files
    {
        const std::string d = tmpDir();
        auto make = [&](int n, const std::string& text, bool sidecar) {
            put(autosave::path(d, n), text);
            if (sidecar) put(autosave::sidecar(autosave::path(d, n)), "s" + text);
        };
        auto read = [&](const std::string& p) {
            std::string t;
            if (std::FILE* f = std::fopen(p.c_str(), "rb")) { char b[64]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) t.append(b, n); std::fclose(f); }
            return t;
        };
        check("files: none to begin with", autosave::newest(d) == 0);
        std::string err;
        make(0, "A", true);
        check("files: first rotation", autosave::rotate(d, err) && read(autosave::path(d, 1)) == "A" && read(autosave::sidecar(autosave::path(d, 1))) == "sA" &&
              !autosave::exists(autosave::path(d, 0)) && autosave::newest(d) == 1);
        make(0, "B", true);
        autosave::rotate(d, err);
        make(0, "C", false);
        autosave::rotate(d, err);
        check("files: newest is 1, then older ones in order, a missing settings file does not leave a stale one",
              read(autosave::path(d, 1)) == "C" && read(autosave::path(d, 2)) == "B" && read(autosave::path(d, 3)) == "A" &&
              !autosave::exists(autosave::sidecar(autosave::path(d, 1))) && read(autosave::sidecar(autosave::path(d, 2))) == "sB");
        make(0, "D", true);
        autosave::rotate(d, err);
        check("files: three kept, the oldest is dropped", read(autosave::path(d, 1)) == "D" && read(autosave::path(d, 2)) == "C" && read(autosave::path(d, 3)) == "B" &&
              !autosave::exists(autosave::path(d, 0)));
        check("files: names", autosave::fileName(2) == "autosave2.sav" && autosave::sidecar("/x/autosave2.sav") == "/x/autosave2.settings");
        make(0, "E", true);
        autosave::discardTemp(d);
        check("files: a discarded temporary leaves the rotation alone", !autosave::exists(autosave::path(d, 0)) && read(autosave::path(d, 1)) == "D");
        for (int n = 0; n <= 3; n++) { std::remove(autosave::path(d, n).c_str()); std::remove(autosave::sidecar(autosave::path(d, n)).c_str()); }
        rmdir(d.c_str());
    }
    // ---- F9 twice within 3 seconds
    {
        DoublePress f;
        using R = DoublePress::Result;
        check("F9: the first press only arms", f.press(1000) == R::Armed && f.armed(1000) && f.armed(3999));
        check("F9: the second press within 3 s confirms", f.press(3500) == R::Confirmed && !f.armed(3500));
        check("F9: a press after a confirmation arms again", f.press(3600) == R::Armed);
        check("F9: exactly 3 s is still in time", f.press(6600) == R::Confirmed);
        f.press(10000);
        check("F9: after more than 3 s the prompt is gone and the press arms anew", !f.armed(13001) && f.press(13001) == R::Armed);
        check("F9: and then a quick second press confirms", f.press(13500) == R::Confirmed);
        f.press(20000);
        f.cancel();
        check("F9: cancel", f.press(20100) == R::Armed);
        DoublePress g;   // the clock wraps
        g.press(0xfffffff0u);
        check("F9: the 32-bit tick counter wrapping is harmless", g.press(0x100u) == R::Confirmed);
    }
    // ---- pause
    {
        using E = PauseState::Edge;
        PauseState p;   // pause on focus loss
        check("pause: running at first", !p.paused() && p.hasFocus());
        check("pause: focus lost pauses", p.focus(false) == E::Pause && p.paused());
        check("pause: no second edge", p.focus(false) == E::None && p.minimised(true) == E::None && p.paused());
        check("pause: minimised, focus back: still paused", p.focus(true) == E::None && p.paused());
        check("pause: restored: resumes", p.minimised(false) == E::Resume && !p.paused());
        check("pause: minimise alone pauses, restore resumes", p.minimised(true) == E::Pause && p.isMinimised() && p.minimised(false) == E::Resume);
        PauseState q(false);   // pause-on-focus-loss = no
        check("pause off: focus loss does nothing", q.focus(false) == E::None && !q.paused() && q.focus(true) == E::None);
        check("pause off: a minimised window is paused all the same", q.minimised(true) == E::Pause && q.paused() && q.minimised(false) == E::Resume);
        check("pause off: lost focus + minimised, restored without focus: running", q.focus(false) == E::None && q.minimised(true) == E::Pause && q.minimised(false) == E::Resume);
    }
    // ---- mouse sensitivity
    {
        MotionScaler m;
        int ox = 0, oy = 0;
        m.apply(7, -3, ox, oy);
        check("sensitivity 1: the identity", ox == 7 && oy == -3);
        m.setFactor(2);
        m.apply(7, -3, ox, oy);
        check("sensitivity 2: doubled", ox == 14 && oy == -6);
        m.setFactor(0.25);
        int sumx = 0, sumy = 0;
        for (int i = 0; i < 16; i++) { m.apply(1, -1, ox, oy); sumx += ox; sumy += oy; }
        check("sensitivity 0.25: sixteen single steps give four, none is lost to rounding", sumx == 4 && sumy == -4);
        m.reset();
        m.setFactor(1.5);
        sumx = 0;
        for (int i = 0; i < 10; i++) { m.apply(3, 0, ox, oy); sumx += ox; }
        check("sensitivity 1.5: the sum is exact to one unit (45)", sumx == 45);
        m.setFactor(9);
        check("sensitivity: clamped to 4 and to 0.25", m.factor() == 4.0 && (m.setFactor(0.01), m.factor() == 0.25));
        double v = 0;
        check("parse: 0.25 .. 4 accepted, outside and junk refused", parseMouseSensitivity("0.25", v) && v == 0.25 && parseMouseSensitivity("4", v) && v == 4.0 && parseMouseSensitivity("1.5", v) &&
              !parseMouseSensitivity("0.2", v) && !parseMouseSensitivity("4.1", v) && !parseMouseSensitivity("fast", v) && !parseMouseSensitivity("", v));
    }
    // ---- slot page keys
    {
        check("page keys: Page Up / Page Down are 0x21 / 0x22, nothing else is a page key", pageKeyOfSdl(SDLK_PAGEUP) == 0x21 && pageKeyOfSdl(SDLK_PAGEDOWN) == 0x22 &&
              pageKeyOfSdl(SDLK_UP) == 0 && pageKeyOfSdl(SDLK_F9) == 0);
        check("page keys: wheel up = previous page, wheel down = next, no motion = none", pageKeyOfWheel(1) == 0x21 && pageKeyOfWheel(-2) == 0x22 && pageKeyOfWheel(0) == 0);
        check("the game never sees a page key (no virtual key for it in vkOfSdl)", vkOfSdl(SDLK_PAGEUP) == 0 && vkOfSdl(SDLK_PAGEDOWN) == 0 && vkOfSdl(SDLK_F9) == 0x78);
    }
    std::printf(failed ? "%d FAILED\n" : "all checks passed\n", failed);
    return failed ? 1 : 0;
}
