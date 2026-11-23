// Autosave: when it is due and where the files go. No SDL, no engine; header only (build/biosys-apptest includes it, main.cpp and the
// script command `autosave` drive it).
//
// Rules (MANUAL.md, "Saving"):
//  * Three rotating saves `autosave1.sav` (newest), `autosave2.sav`, `autosave3.sav` in the save folder, each with its `.settings` file.
//    A new one is written as `autosave0.sav` first and rotated into place only when it was written whole, so a failed write costs nothing.
//  * Due: (a) after `minutes` of play (the time the game runs and the window is not paused; in a script the injected clock of `advance`),
//    (b) when the player wakes from sleep, (c) before the program quits with a game running (main.cpp). A due autosave waits for the next
//    moment a normal save is possible; it is never made while the game or the window refuses one (a menu or a text entry is up, an animation
//    or a sequence runs, the player sleeps, is dead, the game has ended: main.cpp's saveBlocker).
//  * Every attempt, successful or not, restarts the interval, so a folder that cannot be written is reported once per interval and not once per frame.
#pragma once
#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <string>

namespace biosys {

constexpr int kAutosaveKeep = 3;
constexpr int kDefaultAutosaveMinutes = 10;

class AutosavePlanner {
public:
    enum class Why { None, Periodic, Wake };
    void setMinutes(int m) { minutes_ = m < 0 ? 0 : m; }
    int minutes() const { return minutes_; }
    void setEnabled(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_; }
    // `dtMs` of play time have passed (the caller passes none while the game does not run or the window is paused)
    void advance(uint32_t dtMs) { if (enabled_ && minutes_ > 0) accMs_ += dtMs; }
    // the player's awake state, observed after every pass: asleep -> awake makes a wake autosave due
    void observeAwake(bool awake) {
        if (!wasAwake_ && awake && enabled_) wakePending_ = true;
        wasAwake_ = awake;
    }
    Why due() const {
        if (!enabled_) return Why::None;
        if (wakePending_) return Why::Wake;
        if (minutes_ > 0 && accMs_ >= static_cast<uint64_t>(minutes_) * 60000u) return Why::Periodic;
        return Why::None;
    }
    // an attempt was made (it worked or it did not): nothing is due until the next interval
    void attempted() { wakePending_ = false; accMs_ = 0; }
    // a game was loaded or started: the interval and the sleep observation begin again
    void restart() { wakePending_ = false; accMs_ = 0; wasAwake_ = true; }
    uint64_t playedMs() const { return accMs_; }

private:
    bool enabled_ = true, wakePending_ = false, wasAwake_ = true;
    int minutes_ = kDefaultAutosaveMinutes;
    uint64_t accMs_ = 0;
};

namespace autosave {

inline std::string trimSlashes(std::string d) {
    while (d.size() > 1 && (d.back() == '/' || d.back() == '\\')) d.pop_back();
    return d;
}
// n = 0: the file a new autosave is written to before it is rotated in; 1..3: the rotation
inline std::string fileName(int n) { return "autosave" + std::to_string(n) + ".sav"; }
inline std::string path(const std::string& dir, int n) { return trimSlashes(dir) + "/" + fileName(n); }
inline std::string sidecar(const std::string& savPath) { return savPath.substr(0, savPath.size() - 4) + ".settings"; }

inline bool exists(const std::string& p) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}

// the newest autosave that exists (1 .. 3), 0 if there is none
inline int newest(const std::string& dir) {
    for (int n = 1; n <= kAutosaveKeep; n++)
        if (exists(path(dir, n))) return n;
    return 0;
}

inline void discardTemp(const std::string& dir) {
    std::remove(path(dir, 0).c_str());
    std::remove(sidecar(path(dir, 0)).c_str());
}

// autosave0 becomes autosave1, 1 becomes 2, 2 becomes 3, the old 3 is gone (each with its .settings). false with `err` set when a file could not be moved.
inline bool rotate(const std::string& dir, std::string& err) {
    std::remove(path(dir, kAutosaveKeep).c_str());
    std::remove(sidecar(path(dir, kAutosaveKeep)).c_str());
    for (int n = kAutosaveKeep - 1; n >= 0; n--) {
        const std::string from = path(dir, n), to = path(dir, n + 1);
        if (!exists(from)) {
            std::remove(sidecar(to).c_str());
            continue;
        }
        if (std::rename(from.c_str(), to.c_str()) != 0) {
            err = "cannot move " + fileName(n) + " to " + fileName(n + 1);
            return false;
        }
        const std::string sf = sidecar(from), st = sidecar(to);
        if (exists(sf)) std::rename(sf.c_str(), st.c_str());
        else std::remove(st.c_str());
    }
    return true;
}

} // namespace autosave

} // namespace biosys
