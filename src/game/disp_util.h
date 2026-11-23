// Helpers of the machines' panel displays (disp_*.cpp): what the original's Display methods do with the window DC, in the
// original's order. Every string goes through GdiSession::textOut*, which also records it.
#pragma once
#include "game/feature.h"
#include "game/gdi.h"
#include <cstdio>
#include <memory>
#include <string>

namespace biosys::game::disp {

constexpr u32 kAmber = paletteRgb(165, 132, 41);     // 0x022984a5: the pens and text of the panels
constexpr u32 kWhite = paletteRgb(255, 255, 255);    // 0x02ffffff

// __ftol: truncation toward zero
inline int ftol(double d) { return static_cast<int>(d); }

inline std::string fmtD(double v) { char b[64]; std::snprintf(b, sizeof b, "%d", ftol(v)); return b; }          // sprintf("%d", ftol(v))
inline std::string fmtF(const char* f, double v) { char b[96]; std::snprintf(b, sizeof b, f, v); return b; }   // sprintf("%0.nf", v)

// An open window DC with the panel idiom: SelectObject(systemfont) is done by openDc; SetTextColor(colour), SetBkMode(TRANSPARENT).
struct Panel {
    std::unique_ptr<GdiSession> g;
    Canvas* c = nullptr;
    explicit Panel(FeatureHost* h) {
        if (h) g = h->openDc(-1);        // Eng_GetWindowDC(-1, ...): the original skips the drawing when it fails
        if (g) c = &g->dc();
    }
    // Fixes::panelNumberAlign: right-align a number by the width of its own text (the original measures the "%d" text
    // that was formatted last: evident, every other panel number measures what it draws)
    bool alignOwn = false;
    const std::string& widthText(const std::string& measured, const std::string& text) const { return alignOwn ? text : measured; }
    bool ok() const { return c != nullptr; }
    void ink(u32 colour) { c->setTextColor(colour); c->setBkMode(kTransparent); }
    void pen(int width, u32 colour) { Pen p; p.style = 0; p.width = width; p.color = colour; c->selectPen(p); }
    // MoveTo(x, y0) then LineTo(x, y1) (a vertical bar) / LineTo(x1, y) (a horizontal one)
    void vbar(int x, int y0, int y1) { c->moveTo(x, y0); c->lineTo(x, y1); }
    void hbar(int x0, int y, int x1) { c->moveTo(x0, y); c->lineTo(x1, y); }
    // The right alignment of the panels: GetTextExtentPointA of `measured`, then TextOutA(right - cx, y, text)
    void right(int rightX, int y, const std::string& measured, const std::string& text) {
        g->textOut(rightX - g->textWidth(widthText(measured, text)), y, text);
    }
    void right(int rightX, int y, const std::string& text) { right(rightX, y, text, text); }
};

} // namespace biosys::game::disp
