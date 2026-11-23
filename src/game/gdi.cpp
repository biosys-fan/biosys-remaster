#include "game/gdi.h"

namespace biosys::game {

// dll:10075ce0 Eng_GetWindowDC; the display code then does SelectObject(dc, systemfont)
GdiSession::GdiSession(Eng& eng, int window, int font, Recorder rec, bool redrawOnRelease)
    : eng_(eng), window_(window), rec_(std::move(rec)), redraw_(redrawOnRelease) {
    int s20 = 0, s14 = 0;
    if (!eng_.GetWindowDC(window, dcHandle_, s20, s14)) { dcHandle_ = 0; return; }
    canvas_ = eng_.api().canvas(dcHandle_);
    if (!canvas_) { eng_.ReleaseWindowDC(window, dcHandle_, 0, 0); dcHandle_ = 0; return; }
    if (font) canvas_->selectFont(font);
}

// dll:10075d60 Eng_ReleaseWindowDC(window, dc, redraw, 0)
GdiSession::~GdiSession() {
    if (canvas_) eng_.ReleaseWindowDC(window_, dcHandle_, redraw_ ? 1 : 0, 0);
}

void GdiSession::textOut(int x, int y, const std::string& text) {
    if (rec_) rec_(x, y, text);
    if (canvas_) canvas_->textOut(x, y, text);
}

void GdiSession::textOutRight(int right, int y, const std::string& text) {
    textOut(right - textWidth(text), y, text);
}

void GdiSession::textOutCentred(int centre, int y, const std::string& text) {
    textOut(centre - textWidth(text) / 2, y, text);
}

} // namespace biosys::game
