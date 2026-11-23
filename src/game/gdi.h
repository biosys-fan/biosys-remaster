// GdiSession: one Eng_GetWindowDC .. Eng_ReleaseWindowDC pair of the original's display code (dll:10075ce0 / 10075d60) with the
// device context's Canvas (engine/canvas.h) and the system font ("systemfont", 18, CEngineFX +0x610) already selected, the way
// every Display method of the DLL opens it. Text goes through textOut / textOutRight, which draw it AND hand it to the
// recorder: every string drawn is also recorded (the recording is the tests' oracle, `game ui`, `--log-events`).
// A session that failed to open (no page, DC busy: the engine answered an error) has ok() == false; the original's Display
// methods then skip the drawing, so do callers.
#pragma once
#include "engine/canvas.h"
#include "game/eng.h"
#include <functional>
#include <string>

namespace biosys::game {

class GdiSession {
public:
    using Recorder = std::function<void(int x, int y, const std::string& text)>;
    // window: -1 main window, 0 top bar. font: the logical font handle to select (0 = none). redrawOnRelease: the `unk04` flag
    // of Eng_ReleaseWindowDC (flip the display now).
    GdiSession(Eng& eng, int window, int font, Recorder rec, bool redrawOnRelease = false);
    ~GdiSession();
    GdiSession(const GdiSession&) = delete;
    GdiSession& operator=(const GdiSession&) = delete;

    bool ok() const { return canvas_ != nullptr; }
    Canvas& dc() { return *canvas_; }
    int window() const { return window_; }

    // TextOutA(dc, x, y, text, strlen): top-left of the text cell.
    void textOut(int x, int y, const std::string& text);
    // The original's right alignment: GetTextExtentPointA, then TextOutA(dc, right - cx, y, ...).
    void textOutRight(int right, int y, const std::string& text);
    // Centred the way ShowMessage does it: TextOutA(dc, centre - cx / 2, y, ...).
    void textOutCentred(int centre, int y, const std::string& text);
    int textWidth(const std::string& text) { return canvas_ ? canvas_->textExtent(text).cx : 0; }

private:
    Eng& eng_;
    int window_, dcHandle_ = 0;
    Canvas* canvas_ = nullptr;
    Recorder rec_;
    bool redraw_;
};

} // namespace biosys::game
