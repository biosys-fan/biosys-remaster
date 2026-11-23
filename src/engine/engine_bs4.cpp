// Engine glue of the BioSys 4 terminal (src/engine/bs4*.cpp): the Bs4Host the terminal talks to, entering (exe:004104e0), leaving
// (exe:004105c0) and the engine's side of the "display suspended" state (exe:004136a0). The entry points of mouse, keys, timer, idle and
// the frame are in engine.cpp (they test bs4_ at the places the original tests DAT_00498b84), the save / load part in engine_save.cpp.
// Spec: analysis/notes/bs4.md 1.2, 1.4, 1.8, 1.9, 1.12.
#include "engine/engine.h"
#include <algorithm>

namespace biosys {

namespace {
// exe:00480b00: the click sounds 1..14 (files bs4sound\<name>.wav) and the loops 1..7 (bs4sound\loops\<name>.wav), exe:00480b90
const char* const kClicks[14] = {"Synth7", "MousClk5", "TekBip41", "Synth10", "Blip9", "Stab3", "blip20",
                                 "Synth8", "TekBip12", "TekBip23", "TekBip77", "TekBip59", "TekBip7", "TekBip70"};
const char* const kLoops[7] = {"airhiss", "cfan", "hum", "power", "water", "wind", "rain"};
} // namespace

class Engine::Bs4HostImpl : public Bs4Host {
public:
    explicit Bs4HostImpl(Engine& e) : e_(e) {
        std::fill(clicks_, clicks_ + 15, -1);
        std::fill(loops_, loops_ + 8, -1);
    }
    ~Bs4HostImpl() override { closeSounds(); }

    // ---- game logic
    bool onBS4Query(int queryId, void* buffer) override { return e_.logic_->onBS4Query(queryId, buffer); }
    bool onSetFeatureParameter(const std::string& f, int id, int a, int b, int c) override {
        return e_.logic_->onSetFeatureParameter(f, id, a, b, c);
    }
    bool onGetFeatureParameter(const std::string& f, int id, u32& i, double& d) override { return e_.logic_->onGetFeatureParameter(f, id, i, d); }
    void onKeyDown(int vkey, int repeat, int flags) override {      // exe:00405ce0: the DLL's pDoDefault is ignored
        int doDefault = 1;
        e_.logic_->onKeyDown(vkey, repeat, flags, doDefault);
    }
    void onKeyUp(int vkey, int repeat, int flags) override {
        int doDefault = 1;
        e_.logic_->onKeyUp(vkey, repeat, flags, doDefault);
    }
    // ---- screen
    void setPalette(const Palette& pal) override {                  // IDirectDrawPalette::SetEntries: at once
        e_.bs4Palette_ = pal;
        e_.view_.forcePalette(&e_.bs4Palette_);
    }
    void present(const Frame& frame) override {
        e_.bs4Frame_ = frame;
        e_.bs4Presents_++;
    }
    void clipPointer(const Bs4Rect* rect) override {                // ClipCursor(rect) / ClipCursor(NULL)
        e_.bs4Clip_ = rect != nullptr;
        if (rect) e_.bs4ClipRect_ = *rect;
    }
    void requestMouseMove() override { e_.bs4MouseMovePending_ = true; }   // exe:00451c50 / exe:00451d50
    // ---- time and state
    u32 tickCount() override { return e_.now_; }
    bool appActive() override { return !e_.mediaPaused_; }
    void leave() override { e_.bs4LeaveRequested_ = true; }         // exe:004105c0(-1), done by the owner after the call returned
    void requestClose() override {                                  // PostCloseToMainWnd
        e_.quit_ = true;
    }
    void log(const std::string& message) override { e_.report("", message); }
    // ---- sounds (exe:0040c1e0 PreloadClicks, exe:0040c310 PlayClick, exe:0040c4b0 StartLoop, exe:0040c660 StopLoop, exe:0040c6a0)
    void playClick(int slot) override {
        if (slot < 1 || slot > 14) return;
        trace("bs4click " + std::to_string(slot));
        int& h = clicks_[slot];
        if (!usable(h)) h = open(std::string("bs4sound\\") + kClicks[slot - 1] + ".wav", false, false);
        if (h >= 0) e_.sounds_.play(h, true);                       // Sound__Play(sound, 1)
    }
    void startLoop(int loop) override {
        if (loop < 1 || loop > 7) return;
        int& h = loops_[loop];
        if (!usable(h)) h = open(std::string("bs4sound\\loops\\") + kLoops[loop - 1] + ".wav", true, false);
        if (h >= 0 && !e_.sounds_.isPlaying(h)) {
            trace("bs4loop " + std::to_string(loop) + " start");
            e_.sounds_.play(h, true);
        }
    }
    void stopLoop(int loop) override {
        if (loop < 1 || loop > 7) return;
        int h = loops_[loop];
        if (usable(h)) {
            trace("bs4loop " + std::to_string(loop) + " stop");
            e_.sounds_.stop(h);
        }
    }
    void playWelcome(std::function<void()>) override {              // exe:0040bb30: bs4sound\welcome.wav once; exe:0040c6a0 forgets the handle when it is destroyed
        trace("bs4welcome");
        if (!usable(welcome_)) {
            welcome_ = open("bs4sound\\welcome.wav", false, true);
        } else if (!e_.sounds_.isPlaying(welcome_)) {
            e_.sounds_.play(welcome_, true);
        }
    }
    // the engine freed every sound (a load game): the handles are gone
    void forgetSounds() {
        std::fill(clicks_, clicks_ + 15, -1);
        std::fill(loops_, loops_ + 8, -1);
        welcome_ = -1;
    }
    // exe:00404b90: all the terminal's sounds are stopped and closed
    void closeSounds() {
        auto close = [&](int& h) {
            if (usable(h)) {
                e_.sounds_.stop(h);
                e_.sounds_.close(h);
            }
            h = -1;
        };
        for (int& h : clicks_) close(h);
        for (int& h : loops_) close(h);
        close(welcome_);
    }

private:
    void trace(const std::string& s) { if (e_.bs4Trace) e_.bs4Trace(s); }
    bool usable(int h) const { return h >= 0 && e_.sounds_.valid(h); }
    int open(const std::string& file, bool loop, bool autoplay) {
        SoundOpenParams p;
        p.file = file;
        p.loop = loop;
        p.autoplay = autoplay;
        p.notify = false;                                           // the original passes no callback (the welcome one is internal)
        p.lock = 1;
        int h = -1;
        if (e_.sounds_.open(p, h) != kOk) return -1;
        return h;
    }
    Engine& e_;
    int clicks_[15], loops_[8], welcome_ = -1;
};

// ---------------------------------------------------------------------------------------------
// entering and leaving

// exe:004136a0(1): "display suspended". The status bar's saved surface goes; the page is unloaded and OnExitPage (event 3) is sent if one was loaded.
void Engine::bs4Unload() {
    if (bs4Suspended_) return;
    bs4Suspended_ = true;
    bs4ReturnPage_ = view_.pageNumber();                            // DAT_0049b0bc, the scroll position of the main window
    bs4ReturnScroll_[0] = bs4ReturnPage_ ? view_.scrollX() : 0;
    bs4ReturnScroll_[1] = bs4ReturnPage_ ? view_.scrollY() : 0;
    if (bs4ReturnPage_ != 0 && view_.onLeave) {
        view_.onLeave();                                            // event 3 and what the engine drops with the page
        pageGone_ = true;                                           // the next page jump finds no page to leave
    }
}

// exe:004104e0
bool Engine::bs4Enter(const u8* section, size_t size) {
    if (bs4_) return true;
    if (!bs4Host_) bs4Host_.reset(new Bs4HostImpl(*this));
    // the prelude (the terminal object is built after it here: its constructor already loads the first page)
    simStatusHide(false);                                           // exe:004102d0(0)
    stopAllMedia();                                                 // exe:00457860, exe:0045a150
    static_cast<Bs4HostImpl&>(*bs4Host_).forgetSounds();
    bs4SavedPalette_ = view_.palette();                             // GetPalette -> DAT_00495bd8
    barMode_ = false;                                               // exe:00444890(main, 0)
    lastBarSlot_ = -2;
    if (scrollMode_ || pointerCaptured()) {                         // exe:00443e80: the drag-scroll state ends (event 16)
        scrollMode_ = false;
        logic_->onEvent16(0, 0);
    }
    pendDx_ = pendDy_ = 0;
    if (textIn_.active) endTextInput(true);                         // exe:004148f0(1)
    bs4LeaveRequested_ = bs4MouseMovePending_ = bs4LeftDown_ = bs4Clip_ = false;
    if (bs4Trace) bs4Trace("bs4screen 0");
    logic_->onBS4Screen(0);                                         // event 18 (0)
    bs4Unload();
    try {
        Bs4Options opts;
        opts.fixes = bs4Fixes_;
        bs4_.reset(new Bs4(*bs4Host_, files_, bs4Session_, section, size, opts));
    } catch (const Error& e) {
        report("", e.what());
        bs4Leave(-1);
        return false;
    }
    bs4Pump();
    return true;
}

// after every entry into the terminal: a requested leave (exe:004105c0(-1) of the click handlers), then the faked mouse move (exe:00451d50)
void Engine::bs4Pump() {
    for (int i = 0; i < 4; i++) {
        if (bs4LeaveRequested_) {
            bs4LeaveRequested_ = false;
            bs4Leave(-1);
            return;
        }
        if (!bs4_ || !bs4MouseMovePending_) return;
        bs4MouseMovePending_ = false;
        bs4_->onMouseMove(bs4LeftDown_ ? 1u : 0u, ptrX_, ptrY_);    // the faked WM_MOUSEMOVE of exe:00451d50
    }
}

// exe:004136a0(0): JumpToPage(DAT_0049b0bc, scroll) (events 7, 8, 22, 2; not 3: the page was unloaded when the terminal came up)
void Engine::bs4Reload(const int* scroll) {
    if (!bs4Suspended_) return;
    bs4Suspended_ = false;
    try {
        if (bs4ReturnPage_ != 0) view_.goToPage(bs4ReturnPage_, false, scroll);
    } catch (const Error& e) {
        pageGone_ = false;
        quit_ = true;                                               // PostCloseToMainWnd
        report("", e.what());
    }
    pageGone_ = false;
}

// exe:004105c0
void Engine::bs4Leave(int fade) {
    if (!bs4_ && !bs4Suspended_) return;
    bs4_.reset();                                                   // exe:00404b90: the loops stop, the sounds are closed
    if (bs4Host_) static_cast<Bs4HostImpl&>(*bs4Host_).closeSounds();
    bs4Clip_ = bs4LeaveRequested_ = bs4MouseMovePending_ = bs4LeftDown_ = false;
    view_.forcePalette(nullptr);
    const Palette saved = bs4SavedPalette_;
    stashPalette_.reset(new Palette(saved));                        // exe:00412ab0(saved palette)
    view_.setKeyPalette(stashPalette_.get());
    const int scroll[2] = {bs4ReturnScroll_[0], bs4ReturnScroll_[1]};
    bs4Reload(scroll);
    stashPalette_.reset();                                          // exe:00412ab0(NULL)
    view_.setKeyPalette(nullptr);
    if (fade >= 0 && fade < 4) apiFadeToPalette(saved, 0, fade, 100);
    apiSetPalette(1, 0, saved);                                     // SetPalette(1, 0, DAT_00495bd8): event 13
    updateHover();
    if (bs4Trace) bs4Trace("bs4leave");
    logic_->onBS4Screen(1);                                         // event 18 (1)
}

// the engine's side of a restart (exe:00453da0): the terminal is destroyed without going back to the game page
void Engine::bs4Restart() {
    const bool wasUp = bs4_ != nullptr;
    bs4_.reset();
    if (bs4Host_) static_cast<Bs4HostImpl&>(*bs4Host_).closeSounds();
    bs4Clip_ = bs4LeaveRequested_ = bs4MouseMovePending_ = bs4LeftDown_ = false;
    if (wasUp) view_.forcePalette(nullptr);
    bs4Suspended_ = false;                                          // DAT_004990ec = 0; the page stays unloaded until the next jump
    bs4Session_ = Bs4Session();                                     // DAT_00499080, DAT_00497a20, DAT_004990e8, DAT_004979f8, DAT_00497970 = 0
}

void Engine::bs4ForgetSounds() {
    if (bs4Host_) static_cast<Bs4HostImpl&>(*bs4Host_).forgetSounds();
}

std::string Engine::bs4Dump() const {
    std::string out;
    if (!bs4_) return "bs4 off sounds " + std::to_string(sounds_.count()) + "\n";
    Bs4& b = *bs4_;
    const Bs4Page& m = b.mainPage();
    std::string panel = "-";
    int n = 0;
    if (b.panelActive() && b.panelPage().getPageNumber(n)) panel = b.panelPage().name();
    char buf[256];
    std::snprintf(buf, sizeof buf, "bs4 page %s panel %s opener '%s' mask 0x%08x pmask 0x%08x cursor %s\n", m.name().c_str(), panel.c_str(),
                  b.panelOpener().c_str(), b.mainPage().visibleMask(), b.panelActive() ? b.panelPage().visibleMask() : 0u,
                  b.cursorKind() == 2 ? "hand" : "arrow");
    out += buf;
    std::snprintf(buf, sizeof buf, "bs4 session initial=%d email=%d synth=%d mail=%u stamp '%s'\n", b.session().initialPassword ? 1 : 0,
                  b.session().emailPassword ? 1 : 0, b.session().synthPassword ? 1 : 0, b.session().mailState, b.session().mailStamp.c_str());
    out += buf;
    out += "bs4 panelfeature '" + (b.panelActive() ? b.panelPage().feature : std::string()) + "' highlight '" + b.highlightName() + "'\n";
    out += "bs4 clock '" + b.clockLeft() + "' '" + b.clockRight() + "'\n";
    out += std::string("bs4 textentry ") + (b.textEntry ? "1" : "0") + " '" + b.textBuffer + "'\n";
    out += "bs4 presents " + std::to_string(bs4Presents_) + " sounds " + std::to_string(sounds_.count()) + "\n";
    out += "bs4 return page " + view_.pageNameOf(bs4ReturnPage_) + " scroll " + std::to_string(bs4ReturnScroll_[0]) + "," +
           std::to_string(bs4ReturnScroll_[1]) + "\n";
    return out;
}

} // namespace biosys
