// The first-run screen without SDL: what the player has handed over, what is being imported, what the screen says, where its buttons are.
// The window (firstrun.cpp) feeds events in and draws what is asked here; tests/test_firstrun.sh checks this header through
// build/biosys-firstruntest. The layout is a fixed 960 x 600 design (kDesignW x kDesignH), scaled uniformly and centred in the window.
#pragma once
#include "install/importer.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <string>

namespace biosys::firstrun {

using install::DiscKind;

constexpr int kDesignW = 960, kDesignH = 600;

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(double px, double py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

// The design's fixed places (design pixels).
constexpr Rect kDownloadButton{580, 158, 280, 48};
constexpr Rect kChooseButton{580, 240, 280, 48};
constexpr Rect kSetupCard{100, 322, 370, 138};
constexpr Rect kGameCard{490, 322, 370, 138};
constexpr Rect kMessageArea{100, 478, 760, 72};

enum class Hit { None, Download, Choose };

inline Hit hitTest(double dx, double dy) {
    if (kDownloadButton.contains(dx, dy)) return Hit::Download;
    if (kChooseButton.contains(dx, dy)) return Hit::Choose;
    return Hit::None;
}

// The design placed in a window of pw x ph pixels: uniform scale, centred.
struct Fit {
    double scale = 1, offX = 0, offY = 0;
    double toDesignX(double px) const { return (px - offX) / scale; }
    double toDesignY(double py) const { return (py - offY) / scale; }
};
inline Fit fitDesign(int pw, int ph) {
    Fit f;
    f.scale = std::min(static_cast<double>(pw) / kDesignW, static_cast<double>(ph) / kDesignH);
    if (f.scale <= 0) f.scale = 1;
    f.offX = (pw - kDesignW * f.scale) / 2;
    f.offY = (ph - kDesignH * f.scale) / 2;
    return f;
}

enum class Slot { Missing, Queued, Unpack, Extract, Done };

struct DiscView {
    Slot slot = Slot::Missing;
    int pct = 0;
};

struct Job {
    DiscKind kind = DiscKind::Unknown;
    std::string path;
};

class Logic {
public:
    using Identify = std::function<DiscKind(const std::string&, std::string*)>;

    // initial: what an earlier run already imported (install::status of the data home).
    Logic(Identify identify, install::Status initial) : identify_(std::move(identify)) {
        if (initial.setup) setup_.slot = Slot::Done;
        if (initial.game) game_.slot = Slot::Done;
    }

    // A dropped (or chosen) file or folder. Unknown things leave a message and nothing else.
    void drop(const std::string& path) {
        if (allSet()) return;
        message_.clear();
        std::string why;
        const DiscKind k = identify_(path, &why);
        if (k == DiscKind::Unknown) {
            message_ = why.empty() ? "This is not one of the two Biosys CDs." : why;
            return;
        }
        DiscView& v = viewOf(k);
        const std::string name = install::discName(k);
        if (v.slot == Slot::Done) {
            message_ = "The " + name + " is already imported.";
        } else if (v.slot != Slot::Missing) {
            message_ = "The " + name + " is already on its way.";
        } else {
            v.slot = Slot::Queued;
            v.pct = 0;
            queue_.push_back(Job{k, path});
        }
    }

    // The next disc to import, if none is running. The caller starts it and then reports progress / finished / failed / cancelled.
    bool takeJob(Job& out) {
        if (active_ != DiscKind::Unknown || queue_.empty()) return false;
        out = queue_.front();
        queue_.pop_front();
        active_ = out.kind;
        DiscView& v = viewOf(out.kind);
        v.slot = Slot::Unpack;
        v.pct = 0;
        message_.clear();
        return true;
    }
    bool busy() const { return active_ != DiscKind::Unknown || !queue_.empty(); }
    DiscKind active() const { return active_; }

    void progress(DiscKind k, const std::string& step, uint64_t done, uint64_t total) {
        if (k != active_) return;
        DiscView& v = viewOf(k);
        int pct = total ? static_cast<int>(std::min<uint64_t>(100, done * 100 / total)) : 0;
        if (step == "unpack") v.slot = Slot::Unpack;
        else if (step == "extract") v.slot = Slot::Extract;
        else if (step == "finish") { v.slot = Slot::Extract; pct = 100; }
        v.pct = pct;
    }
    void finished(DiscKind k) {
        viewOf(k).slot = Slot::Done;
        viewOf(k).pct = 100;
        if (k == active_) active_ = DiscKind::Unknown;
    }
    void failed(DiscKind k, const std::string& what) {
        viewOf(k).slot = Slot::Missing;
        viewOf(k).pct = 0;
        if (k == active_) active_ = DiscKind::Unknown;
        message_ = "The " + std::string(install::discName(k)) + " could not be imported: " + what;
    }
    void cancelled(DiscKind k) {
        viewOf(k).slot = Slot::Missing;
        viewOf(k).pct = 0;
        if (k == active_) active_ = DiscKind::Unknown;
    }

    const DiscView& view(DiscKind k) const { return k == DiscKind::SetupCd ? setup_ : game_; }
    bool allSet() const { return setup_.slot == Slot::Done && game_.slot == Slot::Done; }

    // The line under the cards: the last error or hint; a gentle pointer to the other disc when one is done.
    std::string message() const {
        if (allSet()) return "All set. Starting the game\xE2\x80\xA6";
        if (!message_.empty()) return message_;
        if (!busy()) {
            if (setup_.slot == Slot::Done && game_.slot == Slot::Missing) return "The Setup CD is done. Now drop the Game CD.";
            if (game_.slot == Slot::Done && setup_.slot == Slot::Missing) return "The Game CD is done. Now drop the Setup CD.";
        }
        return "";
    }

    static std::string stateText(const DiscView& v) {
        switch (v.slot) {
        case Slot::Missing: return "Not here yet";
        case Slot::Queued: return "Waiting\xE2\x80\xA6";
        case Slot::Unpack: return "Unpacking\xE2\x80\xA6 " + std::to_string(v.pct) + " %";
        case Slot::Extract: return "Copying\xE2\x80\xA6 " + std::to_string(v.pct) + " %";
        case Slot::Done: return "Done";
        }
        return "";
    }

    // 0..1 length of the card's progress bar
    static double barFraction(const DiscView& v) {
        switch (v.slot) {
        case Slot::Unpack:
        case Slot::Extract: return v.pct / 100.0;
        case Slot::Done: return 1.0;
        default: return 0.0;
        }
    }

    // One line per state change, for --firstrun-log.
    std::string logLine() const {
        auto word = [](const DiscView& v) {
            switch (v.slot) {
            case Slot::Missing: return std::string("missing");
            case Slot::Queued: return std::string("queued");
            case Slot::Unpack: return "unpack " + std::to_string(v.pct);
            case Slot::Extract: return "extract " + std::to_string(v.pct);
            case Slot::Done: return std::string("done");
            }
            return std::string();
        };
        std::string m;
        for (char c : message()) {
            if (c == '"') m += '\'';
            else if (c == '\n' || c == '\r') m += ' ';
            else m += c;
        }
        return "firstrun: setup=" + word(setup_) + " game=" + word(game_) + " msg=\"" + m + "\"";
    }

    // A message of the window itself (the browser could not be opened, ...).
    void note(const std::string& text) { message_ = text; }

    void setDragging(bool on) { dragging_ = on; }
    bool dragging() const { return dragging_; }

private:
    DiscView& viewOf(DiscKind k) { return k == DiscKind::SetupCd ? setup_ : game_; }
    Identify identify_;
    DiscView setup_, game_;
    std::deque<Job> queue_;
    DiscKind active_ = DiscKind::Unknown;
    std::string message_;
    bool dragging_ = false;
};

} // namespace biosys::firstrun
