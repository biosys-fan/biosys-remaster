// Pure helpers of the game window that need no window: the key translation, the "hold" of the status screens and the scaling
// arithmetic. Header only, no SDL calls (the tool build/biosys-windowtest includes it without SDL).
#pragma once
#include <SDL3/SDL_keycode.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace biosys {

constexpr int kVkCapsLock = 0x14;

// Windows virtual-key code of an SDL key (0 = none we know). Caps Lock and the backquote key are not here: they are the
// status-screen hold (StatusHold), not ordinary keys.
inline int vkOfSdl(SDL_Keycode k) {
    if (k >= SDLK_A && k <= SDLK_Z) return 'A' + static_cast<int>(k - SDLK_A);
    if (k >= SDLK_0 && k <= SDLK_9) return '0' + static_cast<int>(k - SDLK_0);
    if (k >= SDLK_F1 && k <= SDLK_F12) return 0x70 + static_cast<int>(k - SDLK_F1);
    switch (k) {
    case SDLK_SPACE: return 0x20;
    case SDLK_RETURN: return 0x0d;
    case SDLK_ESCAPE: return 0x1b;
    case SDLK_BACKSPACE: return 0x08;
    case SDLK_TAB: return 0x09;
    case SDLK_LEFT: return 0x25;
    case SDLK_UP: return 0x26;
    case SDLK_RIGHT: return 0x27;
    case SDLK_DOWN: return 0x28;
    default: return 0;
    }
}

// The game shows the plant and environment status while Caps Lock (VK 0x14) is DOWN and closes it when the key is released
// (Event_OnKeyDown dll:1001e6b0 / Event_OnKeyUp dll:10021790). A Mac reports Caps Lock as a toggle (press and release arrive
// together when the lock changes), so the key cannot be held there: the lock STATE is the key (on = down, off = up). The
// backquote key (the game uses no other function for it) is an ordinary hold key for the same screen. `blocked`: a menu or a
// text entry is up, which must not receive a new hold; a release is always delivered.
class StatusHold {
public:
    enum class Action { None, Down, Up };
    static bool isHoldKey(SDL_Keycode k) { return k == SDLK_GRAVE; }
    void graveKey(bool down) { grave_ = down; }
    // call on every key event and on focus gain (and, cheaply, once per frame)
    Action update(bool capsLockOn, bool blocked) {
        const bool want = capsLockOn || grave_;
        if (want == held_) return Action::None;
        if (want && blocked) return Action::None;
        held_ = want;
        return want ? Action::Down : Action::Up;
    }
    bool held() const { return held_; }

private:
    bool grave_ = false, held_ = false;
};

// Where a 640 x 480 picture goes in a window: whole-number scale (the largest that fits, at least 1) centred, the rest black.
// A window smaller than the picture shrinks it to fit instead (letterboxed, scale below 1). Same rule as SDL's
// SDL_LOGICAL_PRESENTATION_INTEGER_SCALE, which draws the real window.
struct Letterbox {
    float scale;
    int x, y, w, h;
};
inline Letterbox fitPicture(int winW, int winH, int picW, int picH) {
    Letterbox l{1.0f, 0, 0, picW, picH};
    if (winW <= 0 || winH <= 0 || picW <= 0 || picH <= 0) return l;
    const int whole = std::min(winW / picW, winH / picH);
    if (whole >= 1) {
        l.scale = static_cast<float>(whole);
    } else {
        l.scale = std::min(static_cast<float>(winW) / static_cast<float>(picW), static_cast<float>(winH) / static_cast<float>(picH));
    }
    l.w = static_cast<int>(static_cast<float>(picW) * l.scale);
    l.h = static_cast<int>(static_cast<float>(picH) * l.scale);
    l.x = (winW - l.w) / 2;
    l.y = (winH - l.h) / 2;
    return l;
}

// Master volume keys (run-time, both audio modes). Minus / keypad minus: quieter; Equals / Plus / keypad plus: louder; Backslash: mute.
// None of them reaches the game (vkOfSdl is 0 for all of them) and none is a developer key. `blocked`: a menu or a text entry is
// up (or the developer page-number entry): the key is then text, not a volume key.
enum class VolumeKey { None, Down, Up, Mute };
constexpr int kVolumeStep = 10;   // percent per key press
inline VolumeKey volumeKeyOf(SDL_Keycode k, bool blocked) {
    if (blocked) return VolumeKey::None;
    switch (k) {
    case SDLK_MINUS: case SDLK_KP_MINUS: return VolumeKey::Down;
    case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS: return VolumeKey::Up;
    case SDLK_BACKSLASH: return VolumeKey::Mute;
    default: return VolumeKey::None;
    }
}
// New master volume (percent, 0..100) after Down / Up; Mute leaves it (the mute is a separate switch).
inline int stepVolume(int percent, VolumeKey k) {
    if (k == VolumeKey::Down) percent -= kVolumeStep;
    else if (k == VolumeKey::Up) percent += kVolumeStep;
    return std::min(100, std::max(0, percent));
}

// ---- save-slot pages (the remaster; Engine::setSlotPages): Page Up / Page Down and the mouse wheel in the save and load menus. The engine
// takes them as the virtual keys 0x21 / 0x22 (the menu is the only place that reads them: the game never sees them, see keyOfSdlForMenu).
constexpr int kVkPageUp = 0x21, kVkPageDown = 0x22;
inline int pageKeyOfSdl(SDL_Keycode k) { return k == SDLK_PAGEUP ? kVkPageUp : k == SDLK_PAGEDOWN ? kVkPageDown : 0; }
// wheel y > 0 = scrolled up / away from the user = the previous page; 0 = no page change
inline int pageKeyOfWheel(float y) { return y > 0 ? kVkPageUp : y < 0 ? kVkPageDown : 0; }

// ---- F9: load the newest autosave after a second press within 3 seconds
class DoublePress {
public:
    static constexpr uint32_t kWindowMs = 3000;
    enum class Result { Armed, Confirmed };
    Result press(uint32_t nowMs) {
        if (armed_ && static_cast<uint32_t>(nowMs - at_) <= kWindowMs) {
            armed_ = false;
            return Result::Confirmed;
        }
        armed_ = true;
        at_ = nowMs;
        return Result::Armed;
    }
    bool armed(uint32_t nowMs) const { return armed_ && static_cast<uint32_t>(nowMs - at_) <= kWindowMs; }
    void cancel() { armed_ = false; }

private:
    bool armed_ = false;
    uint32_t at_ = 0;
};

// ---- pause while the window is minimised or has lost the focus, the mouse is released or the settings menu is up
// The original treats an application that is not active or is iconic alike (inactive(): DAT_0049a06c == 0 || IsIconic): sounds, movies and
// animations stop, API 0x29 / 0x2a answer 0x15 and OnIdle does nothing (engine_runtime.md 3.5; Engine::setMediaPaused does all that). With
// `pauseOnFocusLoss` off a window that merely lost the focus keeps running (as the player may have it beside another program); a minimised
// window is paused either way, because the original's idle pass does nothing then.
class PauseState {
public:
    enum class Edge { None, Pause, Resume };
    explicit PauseState(bool pauseOnFocusLoss = true) : pauseOnLoss_(pauseOnFocusLoss) {}
    Edge focus(bool gained) { focused_ = gained; return settle(); }
    Edge minimised(bool m) { minimised_ = m; return settle(); }
    Edge released(bool r) { released_ = r; return settle(); }      // the player let go of the mouse (Option+M, or came back beside the picture)
    Edge menu(bool up) { menu_ = up; return settle(); }            // the settings menu or the new-game screen is up (F10): the game waits
    Edge setPauseOnFocusLoss(bool on) { pauseOnLoss_ = on; return settle(); }
    bool pausesOnFocusLoss() const { return pauseOnLoss_; }
    bool isMenu() const { return menu_; }
    bool paused() const { return shown_; }
    bool isMinimised() const { return minimised_; }
    bool isReleased() const { return released_; }
    bool hasFocus() const { return focused_; }

private:
    Edge settle() {
        const bool p = minimised_ || released_ || menu_ || (pauseOnLoss_ && !focused_);
        if (p == shown_) return Edge::None;
        shown_ = p;
        return p ? Edge::Pause : Edge::Resume;
    }
    bool pauseOnLoss_, focused_ = true, minimised_ = false, released_ = false, menu_ = false, shown_ = false;
};

// ---- which screen: "--display" is a number (1 = the main screen, in the order of `--display list`) or a part of the screen's name,
// upper or lower case ("dell"). Returns the 1-based number, 0 when nothing matches (the caller then uses the main screen).
inline int pickDisplay(const std::string& spec, const std::vector<std::string>& names) {
    if (spec.empty()) return 0;
    if (spec.find_first_not_of("0123456789") == std::string::npos) {
        if (spec.size() > 3) return 0;
        const int n = std::atoi(spec.c_str());
        return n >= 1 && n <= static_cast<int>(names.size()) ? n : 0;
    }
    auto lower = [](std::string t) { for (char& c : t) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return t; };
    const std::string want = lower(spec);
    for (size_t i = 0; i < names.size(); i++)
        if (lower(names[i]).find(want) != std::string::npos) return static_cast<int>(i) + 1;
    return 0;
}
// The screen after `current` (1-based), wrapping; with one screen it is that screen.
inline int nextDisplay(int current, int count) { return count <= 0 ? 0 : (current < 1 || current >= count ? 1 : current + 1); }
// What goes into the preferences file for the screen the window was on at exit: the player's own word while it still names that screen
// (so "dell" stays "dell"), else the number; the main screen with no word stays empty.
inline std::string displayPrefFor(const std::string& startSpec, int current, const std::vector<std::string>& names) {
    const int was = pickDisplay(startSpec, names);
    if (was == current || (was == 0 && current <= 1)) return startSpec;
    return std::to_string(current);
}

// ---- mouse-look sensitivity: a factor 0.25 .. 4 on the motion deltas of the captured pointer. The remainder of a rounded delta is carried
// over, so a slow movement at 0.25 still scrolls (every fourth pixel) and the sum of the deltas is right to within one unit. Factor 1 is the
// identity (nothing is carried).
constexpr double kMinMouseSensitivity = 0.25, kMaxMouseSensitivity = 4.0;
inline bool parseMouseSensitivity(const std::string& text, double& out) {
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (text.empty() || *end || !(v >= kMinMouseSensitivity && v <= kMaxMouseSensitivity)) return false;
    out = v;
    return true;
}
class MotionScaler {
public:
    void setFactor(double f) { f_ = std::min(kMaxMouseSensitivity, std::max(kMinMouseSensitivity, f)); }
    double factor() const { return f_; }
    void reset() { rx_ = ry_ = 0; }
    void apply(int dx, int dy, int& ox, int& oy) {
        if (f_ == 1.0) { ox = dx; oy = dy; return; }
        rx_ += dx * f_;
        ry_ += dy * f_;
        ox = static_cast<int>(std::trunc(rx_));
        oy = static_cast<int>(std::trunc(ry_));
        rx_ -= ox;
        ry_ -= oy;
    }

private:
    double f_ = 1.0, rx_ = 0, ry_ = 0;
};

} // namespace biosys
