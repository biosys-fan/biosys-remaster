// Engine: palette changes and fades (API 0x1a 0x1d 0x24), the cross-fade page jump (exe:00444ca0), the view
// jiggle (API 0x3e), screen fills (API 0x3c 0x67) and the page-image page action. All time comes from the
// injected clock / the host pump, like the blocking animations in engine.cpp.
#include "engine/crossfade.h"
#include "engine/engine.h"
#include "engine/spriteanim.h"
#include <algorithm>
#include <cmath>

namespace biosys {

namespace {

// Keeps the engine in the "blocking" state (input ignored, update() inert) for the duration of a loop.
struct BlockGuard {
    bool& flag;
    bool old;
    explicit BlockGuard(bool& f) : flag(f), old(f) { flag = true; }
    ~BlockGuard() { flag = old; }
};

// pal[i][c] = (char)__ftol(delta * (float)k + (float)start): float operands, x87 arithmetic, truncation toward zero.
u8 fadeValue(float delta, int k, u8 start) {
    const double v = static_cast<double>(delta) * static_cast<double>(static_cast<float>(k)) + static_cast<double>(start);
    return static_cast<u8>(static_cast<int64_t>(v));
}

float fadeDelta(int target, int start, int divisor) {
    return static_cast<float>(static_cast<double>(target - start) / static_cast<double>(divisor));
}

} // namespace

void Engine::showPalette(const Palette& p) {
    view_.setApiPalette(p);
    if (paletteTrace) paletteTrace(p, now_);
}

// ---------------------------------------------------------------------------------------------
// timed SetPalette (exe:00412630) and its step (exe:004128e0)

bool Engine::apiSetPalette(u32 steps, int seconds, const Palette& target) {
    for (;;) {
        if (seconds == 0) {
            showPalette(target);
            logic_->onPaletteChanged();     // event 13 first ...
            fade_.active = false;           // ... then the running fade is cancelled (DAT_00497a24 = 0)
            return true;
        }
        if (steps == 0) return false;
        fade_.active = false;
        const u32 maxSteps = static_cast<u32>(seconds) * 1000u / 500u;   // steps happen at most once per 500 ms tick
        if (maxSteps < steps) steps = maxSteps;
        if (steps != 0) {
            fade_.start = view_.palette();
            for (int i = 0; i < 256; i++) {
                fade_.delta[i][0] = fadeDelta(target.c[i].r, fade_.start.c[i].r, static_cast<int>(steps));
                fade_.delta[i][1] = fadeDelta(target.c[i].g, fade_.start.c[i].g, static_cast<int>(steps));
                fade_.delta[i][2] = fadeDelta(target.c[i].b, fade_.start.c[i].b, static_cast<int>(steps));
            }
            if (!view_.apiPaletteActive()) view_.setApiPalette(fade_.start);   // the logic's palette is in charge from here on
            fade_.step = 0;
            fade_.steps = steps;
            fade_.interval = static_cast<u32>(seconds) * 1000u / steps;
            fade_.due = now_ + fade_.interval;
            fade_.active = true;
            return true;
        }
        seconds = 0;
        steps = 1;
    }
}

void Engine::fadeStep(u32 now) {
    if (!view_.apiPaletteActive()) {   // a debug key took the palette back
        fade_.active = false;
        return;
    }
    fade_.step++;
    Palette p;
    const int k = static_cast<int>(fade_.step);
    for (int i = 0; i < 256; i++) {
        p.c[i].r = fadeValue(fade_.delta[i][0], k, fade_.start.c[i].r);
        p.c[i].g = fadeValue(fade_.delta[i][1], k, fade_.start.c[i].g);
        p.c[i].b = fadeValue(fade_.delta[i][2], k, fade_.start.c[i].b);
    }
    showPalette(p);
    if (fade_.steps <= fade_.step) {
        fade_.active = false;
        logic_->onPaletteChanged();
        return;
    }
    fade_.due = now + fade_.interval;
}

// ---------------------------------------------------------------------------------------------
// blocking fades (exe:00412ae0 FadeToColour, exe:00413140 FadeToPalette)

bool Engine::pumpTurn(u32& t, u32 maxMs) {
    u32 nt = t;
    if (pump_) {
        pumpMaxMs_ = maxMs;
        const bool alive = pump_(nt);
        pumpMaxMs_ = 0;
        if (!alive) return false;
    } else {
        nt = t + 16;
    }
    if (static_cast<int32_t>(nt - t) > 0) t = nt;
    now_ = t;
    return true;
}

// Both fades: delta = (target - start) / N per channel (N = 30 / 60 / 100 / 100 for speed 0..3), steps k = 1 ..
// N * percent / 100 show start + delta * k; speed > 0 waits 1 / 16 / 32 ms (host turns, with the movie / animation
// passes) before every palette. back: then k = steps-1 .. 0 returns to the start colours.
bool Engine::blockingFade(const Palette& start, const Palette& target, int steps, int delayMs, int speed, int keep, bool back,
                          bool snapTarget, bool snapStart) {
    const int N = speed == 1 ? 60 : speed >= 2 ? 100 : 30;
    float delta[256][3];
    for (int i = 0; i < 256; i++) {
        delta[i][0] = fadeDelta(target.c[i].r, start.c[i].r, N);
        delta[i][1] = fadeDelta(target.c[i].g, start.c[i].g, N);
        delta[i][2] = fadeDelta(target.c[i].b, start.c[i].b, N);
    }
    auto frame = [&](int k) {
        Palette p;
        for (int i = 0; i < 256; i++) {
            p.c[i].r = fadeValue(delta[i][0], k, start.c[i].r);
            p.c[i].g = fadeValue(delta[i][1], k, start.c[i].g);
            p.c[i].b = fadeValue(delta[i][2], k, start.c[i].b);
        }
        if (keep >= 0 && keep < 256) p.c[keep] = start.c[keep];
        return p;
    };
    BlockGuard guard(blocking_);
    u32 t = now_, lastSet = t;
    auto show = [&](const Palette& p) {
        if (speed > 0) {
            const u32 deadline = lastSet + static_cast<u32>(delayMs);
            do {
                if (!pumpTurn(t, t < deadline ? deadline - t : 1)) return false;   // exe:00412ae0 spins on GetTickCount: a clock wait
                passes(t, true, false);
            } while (t < deadline);
        }
        showPalette(p);
        lastSet = t;
        return true;
    };
    for (int k = 1; k <= steps; k++)
        if (!show(frame(k))) return true;
    if (back) {
        lastSet = t;
        for (int k = steps - 1; k >= 0; k--)
            if (!show(frame(k))) return true;
        if (snapStart) showPalette(start);
    } else {
        if (snapTarget) showPalette(target);
        fade_.active = false;
    }
    lastStep_ = t;
    return true;
}

// the busy wait of the original (FUN_10007a40: GetTickCount spin); the host's turns present frames meanwhile
void Engine::waitBlocking(u32 ms) {
    if (!pump_ || ms == 0) return;
    BlockGuard guard(blocking_);
    u32 t = now_;
    const u32 deadline = t + ms;
    while (static_cast<int32_t>(deadline - t) > 0)
        if (!pumpTurn(t, deadline - t)) break;
    lastStep_ = t;
}

bool Engine::apiFadeToColour(int r, int g, int b, int back, int speed, int keep, int percent) {
    speed = std::clamp(speed, 0, 3);
    percent = std::clamp(percent, 1, 100);
    const int N = speed == 1 ? 60 : speed >= 2 ? 100 : 30;
    const int delay = speed == 1 ? 1 : speed == 2 ? 16 : speed == 3 ? 32 : 0;
    Palette target;
    for (Color& c : target.c) c = Color{static_cast<u8>(r), static_cast<u8>(g), static_cast<u8>(b)};
    const Palette start = view_.palette();
    // FadeToColour: palette index `keep` (when < 256) keeps its colour; no snap to the target at the end
    return blockingFade(start, target, percent * N / 100, delay, speed, keep >= 0 && keep < 256 ? keep : -1, back == 1, false,
                        percent == 100);
}

bool Engine::apiFadeToPalette(const Palette& target, int back, int speed, int percent) {
    speed = std::clamp(speed, 0, 3);
    percent = std::clamp(percent, 1, 100);
    const int N = speed == 1 ? 60 : speed >= 2 ? 100 : 30;
    const int delay = speed == 1 ? 1 : speed == 2 ? 16 : speed == 3 ? 32 : 0;
    const Palette start = view_.palette();
    return blockingFade(start, target, N * percent / 100, delay, speed, -1, back == 1, percent == 100, percent == 100);
}

// ---------------------------------------------------------------------------------------------
// view jiggle (exe:004139f0): the main view is scrolled in decaying steps

void Engine::jiggle(const i32* rec, u32 first, u32 last, int repeats) {
    BlockGuard guard(blocking_);
    u32 t = now_;
    bool alive = true;
    auto turn = [&](int vx, int vy) {
        passes(t, true, false);                                  // movies / page-relative animations
        if (scrollMode_ && view_.pageNumber() != 0) {            // the scroll step: x += vx, y += vy
            view_.setScroll(view_.scrollX() + vx, view_.scrollY() + vy);
            fireCompass(false);
            updateHover();
        }
        if (!pumpTurn(t)) alive = false;
        passes(t, false, false);
    };
    for (int rep = 0; rep < repeats + 1 && alive; rep++) {
        for (u32 i = first; i <= last && alive; i++) {
            i32 dx = rec[4 * i], dy = rec[4 * i + 1];
            u32 ax = static_cast<u32>(rec[4 * i + 2]), ay = static_cast<u32>(rec[4 * i + 3]);
            if (dx == 0 && dy == 0) continue;
            if (static_cast<u32>(std::abs(dx)) <= ax) ax = 0;
            if (static_cast<u32>(std::abs(dy)) <= ay) ay = 0;
            if (ax == 0 && ay == 0) {
                turn(dx, dy);
                continue;
            }
            const i32 stepX = ax == 0 ? dx : dx < 0 ? -static_cast<i32>(ax) : static_cast<i32>(ax);
            const i32 stepY = ay == 0 ? dy : dy < 0 ? -static_cast<i32>(ay) : static_cast<i32>(ay);
            i32 remX = dx, remY = dy;
            for (;;) {
                const i32 vx = std::abs(stepX) < std::abs(remX) ? stepX : remX;
                const i32 vy = std::abs(stepY) < std::abs(remY) ? stepY : remY;
                remX -= vx;
                remY -= vy;
                const bool done = remX == 0 && remY == 0;
                const int ox = view_.scrollX(), oy = view_.scrollY();
                turn(vx, vy);
                if (done || !alive) break;
                if (view_.scrollX() == ox && view_.scrollY() == oy) break;   // the view hit an edge
            }
        }
    }
    lastStep_ = t;
}

// The post-jump scroll animation of verb 16 (a6, a7): one jiggle record {a6, a7, |a6| / 32, |a7| / 32}. The cross-fade path
// (exe:00444ca0 end) runs it whenever a6 or a7 is set; the plain path (Action_JumpToPageAt exe:0040d4f0) only on a scrollable page.
void Engine::postJumpShake(const Action& a, bool plainPath) {
    if (a.arg[5] == 0 && a.arg[6] == 0) return;
    if (plainPath && !view_.scrollable()) return;
    const i32 rec[4] = {a.arg[5], a.arg[6], std::abs(a.arg[5]) >> 5, std::abs(a.arg[6]) >> 5};
    jiggle(rec, 0, 0, 0);
}

int Engine::apiJiggle(const i32* records, int count, int repeats, u32 first, u32 last) {
    if (view_.pageNumber() == 0) return kApiNotAllowed;
    if (!view_.scrollable()) return kApiOk;
    if (!records || count <= 0 || last < first) return kApiBadParameter;
    if (first >= static_cast<u32>(count)) first = static_cast<u32>(count) - 1;
    if (last >= static_cast<u32>(count)) last = static_cast<u32>(count) - 1;
    jiggle(records, first, last, repeats);
    return kApiOk;
}

// ---------------------------------------------------------------------------------------------
// screen fills (exe:004138b0 ClearWindow, exe:00411cb0 FillScreen): they paint the back buffer and present it, so
// they last until the next page blit; here until the page, the scroll position or the popup changes (or a redraw
// command runs).

void Engine::fillScreen(int x, int y, int w, int h, u8 colour) {
    fill_.on = true;
    fill_.x = x; fill_.y = y; fill_.w = w; fill_.h = h;
    fill_.colour = colour;
    fill_.page = view_.pageNumber();
    fill_.sx = view_.scrollX();
    fill_.sy = view_.scrollY();
    fill_.popup = view_.popupOpen();
}

void Engine::applyFill(Frame& f) {
    if (!fill_.on) return;
    if (fill_.page != view_.pageNumber() || fill_.sx != view_.scrollX() || fill_.sy != view_.scrollY() ||
        fill_.popup != view_.popupOpen()) {
        fill_.on = false;
        return;
    }
    const Rect r = clipRect({fill_.x, fill_.y, fill_.w, fill_.h}, Frame::W, Frame::H);
    for (int y = 0; y < r.h; y++)
        std::fill_n(&f.px[static_cast<size_t>(r.y + y) * Frame::W + r.x], r.w, fill_.colour);
}

// ---------------------------------------------------------------------------------------------
// page action DisplayImage (verb 7, Dispatcher exe:0040d270): ornament number a1, position a2,a3 (-1,-1 = its own)

void Engine::activateDisplayImage(const Page& page, const Hotspot& hs) {
    const Action& a = hs.action;
    int pos[2] = {a.arg[1], a.arg[2]};
    const bool own = a.arg[1] == -1 && a.arg[2] == -1;
    int rc = view_.displayImageNumber(windowOfPage(&page), static_cast<u32>(a.arg[0]), own ? nullptr : pos);
    message_ = rc == 0 ? "displayimage " + std::to_string(a.arg[0]) : "displayimage " + std::to_string(a.arg[0]) + " (failed)";
    dropFill();
}

// ---------------------------------------------------------------------------------------------
// cross-fade page jump (TCMainWnd::DoCrossFadeAndPageJump exe:00444ca0 -> CrossFade exe:00413d10)

namespace {
void cutView(const Frame& f, int top, std::vector<u8>& out) {
    out.assign(f.px.begin() + static_cast<std::ptrdiff_t>(top) * Frame::W,
               f.px.begin() + static_cast<std::ptrdiff_t>(top + kCrossFadeH) * Frame::W);
}
} // namespace

// Returns false (nothing done) whenever the original falls back to the plain page jump: no definition, the view
// is not 640x368, the logic supplies no cross-fade, the alternative definition does not exist.
bool Engine::crossFadeJump(const Page& page, const Action& a) {
    if (view_.fullScreen()) {
        message_ = "Can't do a page jump with cross-fade unless the view is 640x368";
        return false;
    }
    std::vector<CrossFadeDef> defs;
    try {
        defs = loadCrossFadeDefs(files_, view_.pageNumber());
    } catch (const Error& e) {
        message_ = e.what();
        return false;
    }
    if (a.arg[4] < 1 || static_cast<size_t>(a.arg[4]) > defs.size()) return false;
    CrossFadeDef def = defs[static_cast<size_t>(a.arg[4] - 1)];
    const int target = a.arg[0];
    if (target < 1 || target > view_.pageCount()) return false;
    CrossFadeSpec spec;
    if (!logic_->onGetCrossFadeEx(def.name, view_.pageNameOf(target), spec)) {
        crossFadeWantsZoom_ = a.arg[1] == 9;   // exe:00444ca0: a2 == 9 and no cross-fade from the logic: DoZoomInAndPageJump
        return false;
    }
    if (!spec.altName.empty()) {
        const CrossFadeDef* alt = nullptr;
        for (const CrossFadeDef& d : defs)
            if (iequals(d.name, spec.altName)) { alt = &d; break; }
        if (!alt) {
            message_ = "Cross-fade '" + spec.altName + "', specified by Control Program, doesn't exist on page " + page.name;
            return false;
        }
        def = *alt;
    }
    std::string tanName;
    if (def.intermediates > 0) {
        if (def.tanFile < 0 || static_cast<size_t>(def.tanFile) >= page.files.size()) {
            message_ = "Cross-fade '" + def.name + "': bad file number";
            return false;
        }
        tanName = page.files[static_cast<size_t>(def.tanFile)];
    }

    // the picture on screen now
    Frame f;
    render(f);
    std::vector<u8> from, to;
    cutView(f, view_.viewTop(), from);

    // the jump itself; OnEnterPage waits until the fade is over
    suppressEnterPage_ = true;
    try {
        view_.goToPage(target, true);
    } catch (const Error& e) {
        suppressEnterPage_ = false;
        message_ = e.what();
        return true;
    }
    suppressEnterPage_ = false;
    if (a.arg[2] > 0 || a.arg[3] > 0)
        view_.setScroll(a.arg[2] > 0 ? a.arg[2] : view_.scrollX(), a.arg[3] > 0 ? a.arg[3] : view_.scrollY());
    render(f);
    cutView(f, view_.viewTop(), to);
    message_ = "crossfade to " + view_.page().name;

    // CrossFade exe:00413d10: fails (no fade) when the lookup / palette are missing or a frame has the wrong size
    std::vector<u8> xft = spec.xftTable;
    if (xft.empty() && !spec.xftFile.empty()) {
        std::string path = files_.findData(spec.xftFile);
        if (!path.empty()) {
            try { xft = readFile(path); } catch (const Error&) { xft.clear(); }
        }
    }
    bool ok = xft.size() == 0x40000 && spec.palette.size() == 1024;
    std::vector<std::vector<u8>> seq;
    seq.push_back(from);
    if (ok) {
        try {
            if (def.intermediates > 0) {
                TanFile tan(files_.requireTime(tanName));
                for (u32 i = 0; i < def.intermediates; i++) {
                    Image8 im = tan.frame(static_cast<int>(def.frames[i]));
                    if (im.w != kCrossFadeW || im.h != kCrossFadeH) throw Error("size of frames within xfade intermediate animation is not 640x368");
                    seq.push_back(im.px);
                }
            }
        } catch (const Error& e) {
            message_ = e.what();
            ok = false;
        }
    }
    seq.push_back(to);
    if (ok) {
        BlockGuard guard(blocking_);
        u32 t = now_;
        std::vector<u8> out(seq[0].size());
        for (u32 seg = 0; seg <= def.intermediates && ok; seg++) {
            const u32 n = std::max<u32>(def.steps[seg] + 1, 2);
            for (u32 k = 0; k < n; k++) {
                crossFadeBlend(seq[seg].data(), seq[seg + 1].data(), out.size(), spec.palette.data(), xft.data(), crossFadeLevel(k, n),
                               out.data());
                cross_ = &out;
                if (!pumpTurn(t)) { ok = false; break; }
            }
        }
        cross_ = nullptr;
        lastStep_ = t;
    }

    // exe:00444ca0 end: the page-relative shake of the new view, then OnEnterPage
    postJumpShake(a, false);
    logic_->onEnterPage(view_.page().name);
    return true;
}

} // namespace biosys
