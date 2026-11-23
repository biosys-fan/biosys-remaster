// The right-click return page of the main window: a jump from a 2560 x 1024 page to a smaller one (zoom-in jump) saves the page being left;
// the right button on a feature page (F, A, N, U) returns to it. exe:004434d0 WM_RBUTTONDOWN, exe:004449f0 DoZoomInAndPageJump,
// exe:00445cc0 (the zoom-in animation and the return data), exe:004456b0 / exe:004457a0 (save / drop, PageView), exe:00445f80 (return).
// The zoom-in on the hotspot (exe:00445cc0), the arrival zoom (exe:00445490) and the zoom-out of the return (exe:00445f80) are in
// analysis/notes/projection.md (sections 7 and 9).
#include "engine/engine.h"
#include <cmath>
#include <cstring>

namespace biosys {

namespace {

bool featurePageName(const std::string& n) {
    if (n.empty()) return false;
    const char c = n[0];
    return c == 'F' || c == 'f' || c == 'A' || c == 'a' || c == 'N' || c == 'n' || c == 'U' || c == 'u';
}

u32 floatBits(float f) {
    u32 v;
    std::memcpy(&v, &f, 4);
    return v;
}

float bitsFloat(u32 v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

// the host-turn loops of the animations run with input ignored (spec 2.8)
struct BlockFlag {
    bool& f;
    bool old;
    explicit BlockFlag(bool& x) : f(x), old(x) { f = true; }
    ~BlockFlag() { f = old; }
};

// __ftol of the original: truncation toward zero
int ftolInt(double v) { return static_cast<int>(v); }

} // namespace

// exe:004434d0
void Engine::rightButton() {
    if (menu_ || textIn_.active) return;                       // DAT_00498a60, DAT_00498b84
    if (view_.pageNumber() == 0) return;                       // FUN_00402800: no page loaded
    if (simStatusUp()) return;                                 // DAT_0049a39c == 1
    if (held_.pendingFlag == 1 && !barMode_ && featurePageName(view_.page().name)) {   // DAT_00497968, main window +0x304
        if (logic_->onEvent44()) {
            bool useSaved = false;
            if (view_.hasSavedPage()) {                        // main window +0x31c
                const std::string name = view_.pageNameOf(view_.findPage(held_.pendingPage));
                if (logic_->onEvent35(name)) useSaved = true;
            }
            returnToSavedPage(useSaved);
            held_.pendingFlag = 0;
            return;
        }
    }
    logic_->onRightClick();
}

// exe:00445a70 + exe:00443f30 + exe:00411810 of one animation frame. The flip is IDirectDrawSurface::Flip(0, DDFLIP_WAIT) (vtable +0x2c, flags 1,
// exe:00411810; the display is the exclusive-mode primary surface with one back buffer, exe:00414ca0): it returns when the retrace has swapped
// the buffers, which is the only wait between two frames (no Sleep, no timer): one host turn per frame, paced by the host's vertical sync.
bool Engine::zoomFrame(u32& t, double fov, int yawOffset) {
    Projection& p = view_.projection();
    p.setFov(fov);
    p.setYawOffset(yawOffset);
    p.track(view_.scrollX(), view_.scrollY());
    return pumpTurn(t);
}

// exe:0045a190(counter, 1, 0) after (zoom-in, arrival) or before (zoom-out) a frame, while the last call returned 1 (an animation is still
// running); the clock of the sprite animations is the host's.
void Engine::zoomTick(u32 t, bool& live) {
    if (!live) return;
    sprites_.update(t, 2);
    drainEvents();
    live = sprites_.anyPlaying();
}

// exe:004449f0 (called for verb 2 and verb 0x10 with the transition number 9, "zoomtohotspot", from a hotspot): what the zoom-in does
// to the return data; the jump itself is the caller's.
void Engine::zoomJumpPrepare(const Hotspot& hs, bool haveRect) {
    const Action& a = hs.action;
    if ((a.verb != VERB_JUMP && a.verb != VERB_JUMP_SCROLL) || a.arg[1] != 9) return;
    if (a.verb == VERB_JUMP_SCROLL && a.arg[4] != 0 && !crossFadeWantsZoom_) return;   // exe:004432f0: a cross-fade number sends it to DoCrossFadeAndPageJump
    if (!haveRect || view_.pageNumber() == 0) return;          // *param_1[2] == -1: no hotspot rectangle
    const int target = a.arg[0];
    if (target < 1 || target > view_.pageCount()) return;
    const bool big = view_.storedBigPage(target);
    const bool curBig = view_.page().pixelWidth() == 0xa00 && view_.page().pixelHeight() == 0x400;
    if (!big && !curBig) return;
    if (curBig) {
        // exe:00445cc0: the data the return needs: the scroll position, and per step of the 12-step zoom the shift towards the hotspot's
        // centre (x also the short way round the wrap)
        const int l = hs.left, t = hs.top, r = hs.right, b = hs.bottom;
        const int cx = ((r - l) / 2 - 320) + l;
        int cy = ((b - t) / 2 - 184) + t;
        if (cy < 0) cy = 0;
        if (cy > 1024 - 368) cy = 1024 - 368;
        const int sx = view_.scrollX(), sy = view_.scrollY();
        float dx = static_cast<float>(cx - sx) * 0.083333336f;
        if (2560 - 640 < sx) {
            const float alt = static_cast<float>((2560 - sx) + cx) * 0.083333336f;
            if (std::abs(alt) < std::abs(dx)) dx = alt;
        }
        const float dy = static_cast<float>(cy - sy) * 0.083333336f;
        // exe:00445cc0: the main window's sprites go (exe:0045a4d0), its movies are stopped or freed (exe:00457a60(0)), a pending flash of the
        // objects bar ends (exe:0045b0c0, DAT_00497a0c); then per step i = 1..12 the scroll is start + trunc(i * delta) (x < 0 wraps), the field
        // of view 72 - 3 (i - 1) with the yaw offset of fov 75 kept: render, flip (one host turn), tick of the other sprite animations. Nothing else
        // is drawn or faded; the field of view goes back to 75 without a render, so the remembered yaw / pitch stay those of the last frame.
        sprites_.freeWindow(-1);
        drainEvents();
        movies_.suspendWindow(-1, now_);
        drainEvents();
        bar_.flash(0);
        const int off = view_.projection().yawOffset();
        {
            BlockFlag guard(blocking_);
            u32 t = now_;
            bool live = true;
            for (int i = 1; i <= 12; i++) {
                int x = sx + ftolInt(static_cast<double>(dx) * i);
                const int y = sy + ftolInt(static_cast<double>(dy) * i);
                if (x < 0) x += 2560;
                view_.setScroll(x, y, false);
                if (!zoomFrame(t, 72 - 3 * (i - 1), off)) break;
                zoomTick(t, live);
            }
            lastStep_ = t;
        }
        view_.projection().setFov(75.0);
        held_.pending488[0] = static_cast<u32>(sx);
        held_.pending488[1] = static_cast<u32>(sy);
        held_.pending488[2] = 0x48;
        held_.pending488[3] = 3;
        held_.pending488[4] = 0xc;
        held_.pending488[5] = floatBits(dx);
        held_.pending488[6] = floatBits(dy);
        held_.pending488[7] = static_cast<u32>(off);               // this+0x2f0 of the projection
        held_.pending488[8] = static_cast<u32>(view_.pageNumber());   // DAT_00497948 = DAT_0049b0bc
        held_.pending488[9] = static_cast<u32>(target);               // DAT_0049794c
        held_.pendingPage = view_.page().name;
        held_.pendingHotspot = view_.pageNameOf(target);
        held_.pendingFlag = 1;                                 // DAT_00497968
    }
    if (big) {                                                 // a zoom into a panorama: no return from it
        held_.pendingFlag = 0;
        // exe:004449f0: the view is filled with palette index of RGB (0, 0, 0) (exe:00412530 finds the first one; none: nothing is done)
        // and flipped: one frame of black before the new page is loaded
        const int black = view_.palette().find(Color{0, 0, 0});
        if (black >= 0 && view_.pageNumber() != 0) {
            fillScreen(0, view_.viewTop(), Frame::W, PageView::ViewH, static_cast<u8>(black));
            BlockFlag guard(blocking_);
            u32 t = now_;
            pumpTurn(t);
            lastStep_ = t;
        }
        return;
    }
    view_.dropSavedPage();                                     // an older saved page goes (event 38)
    view_.requestSaveOnJump();                                 // DAT_004990fc = 1: the jump saves this one (event 37)
}

// exe:004449f0: does the jump of this hotspot end with the arrival zoom (exe:00445490)?
bool Engine::zoomArrives(const Hotspot& hs, bool haveRect) const {
    const Action& a = hs.action;
    if ((a.verb != VERB_JUMP && a.verb != VERB_JUMP_SCROLL) || a.arg[1] != 9) return false;
    if (a.verb == VERB_JUMP_SCROLL && a.arg[4] != 0 && !crossFadeWantsZoom_) return false;
    if (!haveRect || view_.pageNumber() == 0) return false;
    const int target = a.arg[0];
    if (target < 1 || target > view_.pageCount()) return false;
    return const_cast<PageView&>(view_).storedBigPage(target);
}

// exe:00445490 start: fov 75 for its yaw offset, then fov 128.25 with that offset kept (the field of view of the first picture of the new page)
void Engine::zoomArrivalBegin() {
    Projection& p = view_.projection();
    p.setFov(75.0);
    const int off = p.yawOffset();
    p.setFov(128.25);
    p.setYawOffset(off);
}

void Engine::zoomAbort() {
    view_.projection().setFov(75.0);
    view_.projection().track(view_.scrollX(), view_.scrollY());
}

// exe:00445490 after JumpToPage: the picture of fov 128 is on screen (its flip is the jump's); then 18 frames of fov 126, 123 .. 75 (yaw offset
// kept; render, flip, sprite tick), fov 75 (set, not rendered), the shake of a6 / a7 (verb 0x10), OnEnterPage. One frame = one host turn.
void Engine::zoomArrivalRun(const Action& a) {
    Projection& p = view_.projection();
    const int off = p.yawOffset();
    {
        BlockFlag guard(blocking_);
        u32 t = now_;
        bool live = true;
        p.track(view_.scrollX(), view_.scrollY());
        bool alive = pumpTurn(t);
        for (int fov = 126; fov >= 75 && alive; fov -= 3) {
            alive = zoomFrame(t, fov, off);
            if (alive) zoomTick(t, live);
        }
        lastStep_ = t;
    }
    zoomAbort();
    if (a.verb == VERB_JUMP_SCROLL) postJumpShake(a, false);
}

// exe:00445f80: back to the page of the return data (the data of exe:00445cc0: scroll x0, y0, field of view 72, step 3, 12 steps, deltas dx, dy,
// yaw offset, the page number). The view starts where the zoom-in ended (x0 + trunc(12 dx), y0 + trunc(12 dy); field of view 72 - 11 * 3 = 39),
// the page is shown again at that position (useSaved: the page object that was saved comes back as it was, event 36 instead of the page-load
// events; otherwise it is loaded again like any jump), and the zoom runs backwards: frames 39 (the picture shown by the page change), then
// 42, 45 .. 72 with the scroll start + trunc(k dx) for k = 11 .. 1 (a sprite tick before each), then field of view 75 at (x0, y0).
void Engine::returnToSavedPage(bool useSaved) {
    const int x = static_cast<int>(held_.pending488[0]), y = static_cast<int>(held_.pending488[1]);
    const int fov0 = static_cast<int>(held_.pending488[2]), step = static_cast<int>(held_.pending488[3]);
    const int steps = static_cast<int>(held_.pending488[4]);
    const float dx = bitsFloat(held_.pending488[5]), dy = bitsFloat(held_.pending488[6]);
    const int off = static_cast<int>(held_.pending488[7]);
    const int src = view_.findPage(held_.pendingPage);
    // The record normally comes from exe:00445cc0 (72, 3, 12); a record of a damaged save file that makes no sense is not animated.
    const bool animate = steps >= 1 && steps <= 64 && step >= 0 && step <= 16 && fov0 >= 1 && fov0 <= 120 && std::isfinite(dx) &&
                         std::isfinite(dy) && std::abs(dx) < 4096.0f && std::abs(dy) < 4096.0f;
    const double startFov = static_cast<double>((1 - steps) * step + fov0);     // 39
    int sx = x, sy = y;
    if (animate) {
        sx = x + ftolInt(static_cast<double>(dx) * steps);
        sy = y + ftolInt(static_cast<double>(dy) * steps);
        if (sx < 0) sx += 2560;
        view_.projection().setFov(startFov);
        view_.projection().setYawOffset(off);
    }
    if (useSaved && view_.hasSavedPage()) {
        view_.restoreSavedPage();                              // OnExitPage, the page object back
        pageChangeMedia();
        resetPageGlobals();
        view_.setScroll(sx, sy, false);
        objs_.pageBaked();
        logic_->onReturnToSavedPage(view_.pageNameOf(src ? src : view_.pageNumber()));   // event 36
        resetPageTimers();
        compass_ = -1;
        fireCompass(true);                                     // exe:004038f0(page, 1): before the first render, so with the last render's yaw
        view_.releaseTrack();
    } else {
        if (!src) {
            if (animate) zoomAbort();
            return;
        }
        const int scroll[2] = {sx, sy};
        suppressEnterPage_ = true;
        try {
            view_.goToPage(src, true, scroll);
        } catch (const Error&) {
            suppressEnterPage_ = false;
            if (animate) zoomAbort();
            return;
        }
        suppressEnterPage_ = false;
    }
    if (animate && view_.projected()) {
        movies_.suspendWindow(-1, now_);                       // exe:00457a60(1) of the restore / the page load
        drainEvents();
        BlockFlag guard(blocking_);
        u32 t = now_;
        bool live = true;
        bool alive = pumpTurn(t);                              // the picture of the page change, field of view 39
        double fov = startFov + step;
        for (int k = steps - 1; k >= 1 && alive; k--, fov += step) {
            zoomTick(t, live);
            int px = x + ftolInt(static_cast<double>(dx) * k);
            const int py = y + ftolInt(static_cast<double>(dy) * k);
            if (px < 0) px += 2560;
            view_.setScroll(px, py, false);
            alive = zoomFrame(t, fov, off);
        }
        view_.setScroll(x, y, false);
        view_.projection().setFov(75.0);
        view_.projection().track(view_.scrollX(), view_.scrollY());
        if (alive) pumpTurn(t);                                // the last picture: field of view 75 at the old position
        lastStep_ = t;
    } else if (animate) {
        zoomAbort();
    }
    barMode_ = false;
    lastBarSlot_ = -2;
    updateHover();
    logic_->onEnterPage(view_.page().name);
    movies_.resumeWindow(-1, now_);                            // exe:00457af0
}

} // namespace biosys
