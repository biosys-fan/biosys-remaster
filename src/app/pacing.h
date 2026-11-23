// The pace of the game window, without a window: the engine runs its idle passes (and the steps of the blocking animations) at a fixed
// interval P on the real clock, whatever the display's refresh rate; the window presents at the display's rate in between.
// Header only, no SDL. The window (main.cpp) and the headless test of the pacing (script command `window`, tests/test_pacing.sh)
// run the same Pacer; only the Host differs (the real SDL clock and display, or a fake clock and display).
//
// Why: the original ran one idle pass (exe:00416840: scroll step, sprites, sounds, event 17, one flip) per turn of its message loop and
// one flip per frame of a zoom, cross-fade or jiggle (exe:00411810, DDFLIP_WAIT), so those things ran at the pace of its machine and
// monitor. A port that steps once per display frame runs twice as fast on a 120 Hz screen as on a 60 Hz one.
#pragma once
#include "engine/common.h"
#include <algorithm>

namespace biosys {

constexpr u32 kDefaultPassMs = 33;   // see MANUAL.md "Remaster settings": the evidence for this default
constexpr u32 kMinPassMs = 10;
constexpr u32 kMaxPassMs = 100;

inline u32 clampPassMs(long ms) { return static_cast<u32>(std::min<long>(kMaxPassMs, std::max<long>(kMinPassMs, ms))); }

// The schedule of the passes: one every `passMs` on the real clock. A pass that is more than a whole interval late does not make up
// for the lost ones (no burst): the schedule restarts from the time it ran.
class PassClock {
public:
    explicit PassClock(u32 passMs = kDefaultPassMs) : p_(passMs < 1 ? 1 : passMs) {}
    u32 passMs() const { return p_; }
    bool due(u32 now) const { return !started_ || static_cast<int32_t>(now - next_) >= 0; }
    u32 nextDue(u32 now) const { return started_ ? next_ : now; }
    // Consumes one pass: true when one is due at `now`.
    bool take(u32 now) {
        if (!due(now)) return false;
        if (!started_ || static_cast<int32_t>(now - next_) > static_cast<int32_t>(p_)) next_ = now;
        started_ = true;
        next_ += p_;
        return true;
    }

private:
    u32 p_;
    u32 next_ = 0;
    bool started_ = false;
};

// Host: now() real clock in ms; idle(ms) give up the CPU for about ms (a throttled display needs none); events() handle the pending
// input, false = quit; draw() render and present one frame at the display's rate (it may block for the vertical sync).
template <class Host>
class Pacer {
public:
    static constexpr u32 kMinPresentGap = 4;   // a short wait presents a frame only when this long since the last one (ms)
    Pacer(Host& h, u32 passMs) : h_(h), clock_(passMs) {}
    PassClock& clock() { return clock_; }

    // One iteration of the window's main loop, after its input events: the pass when it is due (`pass(now)`: the engine's update),
    // then `beforeDraw()` and the frame. A display that does not wait for the vertical sync does not make the loop spin.
    template <class PassFn, class DrawFn>
    void step(PassFn&& pass, DrawFn&& beforeDraw) {
        const u32 t0 = h_.now();
        if (clock_.take(t0)) pass(t0);
        beforeDraw();
        h_.draw();
        lastPresent_ = h_.now();
        if (static_cast<int32_t>(lastPresent_ - t0) < 2) h_.idle(1);
    }

    // The pump of the blocking operations (Engine::setPump): one turn. `t` in: the engine's clock; out: the real clock when the turn
    // ends. maxMs == 0: a whole pass: the new state is presented at once and kept on screen until the next pass is due (the frame is
    // shown for one pass interval, whatever the display); maxMs != 0 (a clock wait of the original, Engine::pumpMaxMs): until then,
    // at most one pass. Input is not handled beyond quit.
    bool pumpTurn(u32& t, u32 maxMs) {
        bool alive = h_.events();
        const bool shortTurn = maxMs != 0 && maxMs < clock_.passMs();
        u32 now = h_.now();
        const u32 due = shortTurn ? t + maxMs : clock_.nextDue(now);
        if (alive && (!shortTurn || static_cast<int32_t>(now - lastPresent_) >= static_cast<int32_t>(kMinPresentGap))) {
            h_.draw();
            lastPresent_ = h_.now();
        }
        while (alive && static_cast<int32_t>(h_.now() - due) < 0) {
            alive = h_.events();
            now = h_.now();
            if (!alive || static_cast<int32_t>(now - due) >= 0) break;
            if (static_cast<int32_t>(now - lastPresent_) >= static_cast<int32_t>(kMinPresentGap) && due - now > kMinPresentGap) {
                h_.draw();                 // the same state again: only the displayed camera moves (smooth panoramas)
                lastPresent_ = h_.now();
            } else {
                h_.idle(1);
            }
        }
        now = h_.now();
        if (!shortTurn) clock_.take(now);
        t = now;
        return alive;
    }

private:
    Host& h_;
    PassClock clock_;
    u32 lastPresent_ = 0;
};

} // namespace biosys
