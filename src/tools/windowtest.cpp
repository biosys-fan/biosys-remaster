// build/biosys-windowtest: the window's pure helpers without a window (src/app/windowlogic.h, src/app/savesettings.h):
// key translation, the Caps Lock / backquote hold of the status screens, the scaling arithmetic, the settings sidecar text.
#include "app/pacing.h"
#include "app/savesettings.h"
#include "app/windowlogic.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace biosys;

static int failed = 0;
static void check(const std::string& name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
    if (!ok) failed++;
}

using A = StatusHold::Action;

int main() {
    // ---- key translation
    check("letters are their capitals, digits themselves", vkOfSdl(SDLK_A) == 'A' && vkOfSdl(SDLK_Z) == 'Z' && vkOfSdl(SDLK_0) == '0' && vkOfSdl(SDLK_9) == '9');
    check("F1..F12 are 0x70..0x7b", vkOfSdl(SDLK_F1) == 0x70 && vkOfSdl(SDLK_F5) == 0x74 && vkOfSdl(SDLK_F6) == 0x75 && vkOfSdl(SDLK_F12) == 0x7b);
    check("space, enter, escape, backspace, tab, arrows", vkOfSdl(SDLK_SPACE) == 0x20 && vkOfSdl(SDLK_RETURN) == 0x0d && vkOfSdl(SDLK_ESCAPE) == 0x1b &&
          vkOfSdl(SDLK_BACKSPACE) == 8 && vkOfSdl(SDLK_TAB) == 9 && vkOfSdl(SDLK_LEFT) == 0x25 && vkOfSdl(SDLK_DOWN) == 0x28);
    check("Caps Lock and backquote are not ordinary keys (they are the hold)", vkOfSdl(SDLK_CAPSLOCK) == 0 && vkOfSdl(SDLK_GRAVE) == 0 &&
          StatusHold::isHoldKey(SDLK_GRAVE) && !StatusHold::isHoldKey(SDLK_CAPSLOCK) && !StatusHold::isHoldKey(SDLK_A));
    check("the game's Caps Lock is virtual key 0x14", kVkCapsLock == 0x14);

    // ---- the hold, driven by the lock state
    {
        StatusHold h;
        check("lock off: nothing", h.update(false, false) == A::None && !h.held());
        check("lock switched on: key down", h.update(true, false) == A::Down && h.held());
        check("every later key event with the lock on: nothing more", h.update(true, false) == A::None && h.update(true, false) == A::None);
        check("lock switched off: key up", h.update(false, false) == A::Up && !h.held());
        check("and nothing again", h.update(false, false) == A::None);
    }
    {   // the macOS toggle: the press and release of the Caps Lock key come together, the state is checked at each event
        StatusHold h;
        bool lock = false;
        auto toggleKey = [&]() { lock = !lock; const A a = h.update(lock, false); const A b = h.update(lock, false); return std::pair<A, A>(a, b); };
        auto p = toggleKey();
        check("a toggle press+release turning the lock on: one Down, then nothing", p.first == A::Down && p.second == A::None && h.held());
        p = toggleKey();
        check("the next toggle turning it off: one Up", p.first == A::Up && p.second == A::None && !h.held());
    }
    {   // the backquote key is an ordinary hold
        StatusHold h;
        h.graveKey(true);
        check("backquote down: key down", h.update(false, false) == A::Down);
        check("backquote auto-repeat does not repeat it", h.update(false, false) == A::None);
        h.graveKey(false);
        check("backquote up: key up", h.update(false, false) == A::Up);
    }
    {   // both at once: the screen stays until both are released
        StatusHold h;
        h.graveKey(true);
        h.update(true, false);
        h.graveKey(false);
        check("lock on and backquote released: still held", h.update(true, false) == A::None && h.held());
        check("then the lock off: up", h.update(false, false) == A::Up);
        h.update(true, false);
        h.graveKey(true);
        check("lock off while backquote held: still held", h.update(false, false) == A::None && h.held());
        h.graveKey(false);
        check("backquote released: up", h.update(false, false) == A::Up);
    }
    {   // a menu / text entry blocks a new hold, never a release; the hold starts when it is unblocked
        StatusHold h;
        check("blocked: no new hold", h.update(true, true) == A::None && !h.held());
        check("unblocked with the lock still on: down", h.update(true, false) == A::Down);
        check("a release is delivered even when blocked", h.update(false, true) == A::Up);
        h.graveKey(true);
        check("backquote while blocked: no hold", h.update(false, true) == A::None);
        h.graveKey(false);
        check("backquote released while blocked: nothing happened", h.update(false, false) == A::None && !h.held());
    }

    // ---- the scaling arithmetic (640 x 480 picture)
    auto fit = [](int w, int h, float sc, int x, int y, int bw, int bh) {
        const Letterbox l = fitPicture(w, h, 640, 480);
        return l.scale == sc && l.x == x && l.y == y && l.w == bw && l.h == bh;
    };
    check("640x480: scale 1, no bars", fit(640, 480, 1, 0, 0, 640, 480));
    check("1280x960: scale 2", fit(1280, 960, 2, 0, 0, 1280, 960));
    check("1920x1080: scale 2 (1080/480 = 2.25), bars left and right", fit(1920, 1080, 2, 320, 60, 1280, 960));
    check("2560x1440: scale 3, bars left and right", fit(2560, 1440, 3, 320, 0, 1920, 1440));
    check("1440x900 (a laptop screen): scale 1, bars all round", fit(1440, 900, 1, 400, 210, 640, 480));
    check("1000x1000: scale 1 (not 1.5: whole numbers only)", fit(1000, 1000, 1, 180, 260, 640, 480));
    check("1279x960: width limits it to scale 1", fit(1279, 960, 1, 319, 240, 640, 480));
    {
        const Letterbox l = fitPicture(500, 400, 640, 480);
        check("a window smaller than the picture shrinks it to fit (500x400 -> 500x375)", l.scale == 0.78125f && l.w == 500 && l.h == 375 && l.x == 0 && l.y == 12);
        const Letterbox z = fitPicture(0, 0, 640, 480);
        check("a degenerate window gives scale 1", z.scale == 1.0f);
    }

    // ---- the settings text next to a save
    {
        SettingsText t{"corrected", "guided", "restored,-rainWashesPollution"};
        const std::string text = sidecarText(t, 0x0123456789abcdefull);
        SettingsText back;
        uint64_t h = 0;
        check("sidecar text round trip", parseSidecar(text, back, h) && back == t && h == 0x0123456789abcdefull);
        check("an empty fix list survives", [&] { SettingsText e, b2; uint64_t hh; return parseSidecar(sidecarText(e, 7), b2, hh) && b2 == e && hh == 7; }());
        check("text of another version is refused", !parseSidecar("biosys-settings 2\nrealism=a\ndifficulty=b\nfixes=\nsave=0123456789abcdef\n", back, h));
        check("text without a hash is refused", !parseSidecar("biosys-settings 1\nrealism=a\ndifficulty=b\nfixes=\n", back, h));
        check("a damaged hash is refused", !parseSidecar("biosys-settings 1\nrealism=a\ndifficulty=b\nfixes=\nsave=xyz\n", back, h));
        check("sidecar path of a save", sidecarPathOf("/d/Saved Game 3.sav") == "/d/Saved Game 3.settings" && sidecarPathOf("/d/Saved Game 3.SAV") == "/d/Saved Game 3.settings");
        check("NewGame.sav and other files have none", sidecarPathOf("/d/NewGame.sav").empty() && sidecarPathOf("/d/notes.txt").empty());
        check("the hash depends on every byte", saveHash({1, 2, 3}) != saveHash({1, 2, 4}) && saveHash({1, 2, 3}) == saveHash({1, 2, 3}));
        const ResolvedSettings r = resolveSettings(t);
        check("corrected resolves to the corrected settings and the terminal's fixes", r.settings.model.any() && r.bs4.any());
        const ResolvedSettings f = resolveSettings(SettingsText());
        check("the defaults resolve to the unchanged translation", f.settings.isDefault() && !f.bs4.any());
        bool threw = false;
        try { resolveSettings(SettingsText{"nonsense", "original", ""}); } catch (const Error&) { threw = true; }
        check("an unknown word throws", threw);
    }
    // ---- the pass schedule (app/pacing.h)
    {
        PassClock c(33);
        check("pass clock: the first pass is due at once, then one per interval", c.due(1000) && c.take(1000) && !c.due(1032) && !c.take(1032) && c.take(1033));
        PassClock d(33);
        int n = 0;
        for (u32 t = 5000; t < 5000 + 10000; t += 7) n += d.take(t) ? 1 : 0;      // a 143 Hz display polling the clock
        PassClock e(33);
        int m = 0;
        for (u32 t = 5000; t < 5000 + 10000; t += 17) m += e.take(t) ? 1 : 0;     // a 59 Hz one
        check("pass clock: the number of passes per 10 s does not depend on how often it is asked (303 +- 1)", std::abs(n - 303) <= 1 && std::abs(m - 303) <= 1);
        PassClock f(33);
        f.take(0);
        check("pass clock: a pass that is more than one interval late does not make up the passes it missed", f.take(1000) && !f.take(1001) && !f.take(1032) && f.take(1033));
        check("pass clock: wraps with the 32-bit clock", [] { PassClock g(33); u32 t = 0xFFFFFFF0u; int k = 0; for (int i = 0; i < 100; i++, t += 11) k += g.take(t) ? 1 : 0; return k >= 32 && k <= 35; }());
        check("pass interval range 10..100", clampPassMs(1) == 10 && clampPassMs(500) == 100 && clampPassMs(33) == 33 && kDefaultPassMs == 33);
        {   // the screen choice and the released mouse
            const std::vector<std::string> names{"Built-in Retina Display", "DELL U3824DW"};
            check("display: a number picks that screen, 1-based", pickDisplay("1", names) == 1 && pickDisplay("2", names) == 2);
            check("display: a number beyond the screens, 0 and nonsense pick none", pickDisplay("3", names) == 0 && pickDisplay("0", names) == 0 && pickDisplay("9999", names) == 0 && pickDisplay("", names) == 0);
            check("display: a part of the name picks the screen, any case", pickDisplay("dell", names) == 2 && pickDisplay("DELL U38", names) == 2 && pickDisplay("retina", names) == 1 && pickDisplay("lg", names) == 0);
            check("display: the next screen wraps", nextDisplay(1, 2) == 2 && nextDisplay(2, 2) == 1 && nextDisplay(1, 1) == 1 && nextDisplay(0, 3) == 1 && nextDisplay(5, 3) == 1 && nextDisplay(1, 0) == 0);
            check("display: the preference keeps the player's word while it names the screen", displayPrefFor("dell", 2, names) == "dell" && displayPrefFor("", 1, names) == "" && displayPrefFor("nosuch", 1, names) == "nosuch");
            check("display: the preference becomes the number when the window moved", displayPrefFor("dell", 1, names) == "1" && displayPrefFor("", 2, names) == "2" && displayPrefFor("1", 2, names) == "2");
            PauseState ps(true);
            check("released mouse: pauses, and gives back", ps.released(true) == PauseState::Edge::Pause && ps.paused() && ps.isReleased() && ps.released(false) == PauseState::Edge::Resume && !ps.paused());
            check("released mouse: stays paused through a focus change until it is taken again",
                  ps.released(true) == PauseState::Edge::Pause && ps.focus(false) == PauseState::Edge::None && ps.focus(true) == PauseState::Edge::None && ps.paused() && ps.released(false) == PauseState::Edge::Resume);
            PauseState pn(false);
            check("released mouse: pauses also with --pause-on-focus-loss no", pn.focus(false) == PauseState::Edge::None && pn.released(true) == PauseState::Edge::Pause);
        }
    }
    std::printf("%s\n", failed ? "SOME CHECKS FAILED" : "windowtest: all checks passed");
    return failed ? 1 : 0;
}
