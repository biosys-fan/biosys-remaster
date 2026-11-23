#include "engine/canvas.h"
#include "engine/engineapi.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace biosys {

// ---------------------------------------------------------------------------------------------- registry

namespace {
std::map<int, Canvas*>& table() {
    static std::map<int, Canvas*> t;
    return t;
}
int gNext = 0x4001;     // looks like a GDI handle; 0 stays "no DC"
} // namespace

int CanvasRegistry::add(Canvas* c) {
    const int h = gNext++;
    table()[h] = c;
    return h;
}
Canvas* CanvasRegistry::get(int handle) {
    auto it = table().find(handle);
    return it == table().end() ? nullptr : it->second;
}
void CanvasRegistry::remove(int handle) { table().erase(handle); }

Canvas* EngineApi::canvas(int dcHandle) const { return CanvasRegistry::get(dcHandle); }

// ---------------------------------------------------------------------------------------------- Canvas

Canvas::Canvas(Surface& target, PaletteFn palette, const FontLibrary* fonts, std::vector<u8>* mask)
    : t_(target), track_(TextPlane::tracking()), pal_(std::move(palette)), fonts_(fonts), mask_(mask) {}

u8 Canvas::mapColor(u32 c) const {
    if ((c >> 24) == 1) return static_cast<u8>(c & 0xff);    // PALETTEINDEX
    const int r = static_cast<int>(c & 255), g = static_cast<int>((c >> 8) & 255), b = static_cast<int>((c >> 16) & 255);
    const Palette& p = pal_();
    int best = 0;
    long bestD = -1;
    for (int i = 0; i < 256; i++) {
        long dr = p.c[i].r - r, dg = p.c[i].g - g, db = p.c[i].b - b;
        long d = dr * dr + dg * dg + db * db;
        if (bestD < 0 || d < bestD) { bestD = d; best = i; }
    }
    return static_cast<u8>(best);
}

const Font* Canvas::fontPtr() const { return fonts_ && font_ ? fonts_->font(font_) : nullptr; }

int Canvas::selectFont(int h) {
    const int old = font_;
    font_ = (fonts_ && fonts_->font(h)) ? h : 0;
    return old;
}
u32 Canvas::setTextColor(u32 c) { u32 o = textColor_; textColor_ = c; return o; }
u32 Canvas::setBkColor(u32 c) { u32 o = bkColor_; bkColor_ = c; return o; }
int Canvas::setBkMode(int m) {
    const int o = bkMode_;
    if (m == kTransparent || m == kOpaque) bkMode_ = m;
    return o;
}
unsigned Canvas::setTextAlign(unsigned f) { unsigned o = align_; align_ = f & (TA_CENTER | TA_BASELINE); return o; }
Pen Canvas::selectPen(const Pen& p) { Pen o = pen_; pen_ = p; return o; }
Brush Canvas::selectBrush(const Brush& b) { Brush o = brush_; brush_ = b; return o; }

void Canvas::plot(int x, int y, u8 index) {
    int l = 0, t = 0, r = t_.w, b = t_.h;
    if (clipOn_) { l = std::max(l, clip_.left); t = std::max(t, clip_.top); r = std::min(r, clip_.right); b = std::min(b, clip_.bottom); }
    if (x < l || x >= r || y < t || y >= b) return;
    const size_t at = static_cast<size_t>(y) * static_cast<size_t>(t_.w) + static_cast<size_t>(x);
    if (track_) {
        if (inText_) planeOf(t_).putInk(t_, at, index, run_, mask_);
        else if (t_.text) t_.text->kill(at);
    }
    t_.px[at] = index;
    if (mask_) (*mask_)[at] = 1;
}

void Canvas::fillIdx(int l, int t, int r, int b, u8 index) {
    l = std::max(l, 0); t = std::max(t, 0); r = std::min(r, t_.w); b = std::min(b, t_.h);
    for (int y = t; y < b; y++)
        for (int x = l; x < r; x++) plot(x, y, index);
}

// ---- text

void Canvas::drawRun(int x, int y, const Font& f, const char* s, int n) {
    const int baseline = y + f.ascent();
    const u8 ink = mapColor(textColor_);
    if (track_) {       // the run for a high-resolution presentation of the text (engine/textplane.h)
        TextRun r;
        r.font = fonts_ ? fonts_->fontShared(font_) : nullptr;
        r.text.assign(s, static_cast<size_t>(n));
        r.x = x;
        r.y = y;
        r.clipL = clipOn_ ? clip_.left : 0;
        r.clipT = clipOn_ ? clip_.top : 0;
        r.clipR = clipOn_ ? clip_.right : t_.w;
        r.clipB = clipOn_ ? clip_.bottom : t_.h;
        r.ink = ink;
        r.colorRef = textColor_;
        r.align = align_;
        r.opaque = bkMode_ == kOpaque;
        r.bkIndex = r.opaque ? mapColor(bkColor_) : 0;
        run_ = planeOf(t_).beginRun(t_, std::move(r), mask_);
    }
    if (bkMode_ == kOpaque) fillIdx(x, y, x + f.textWidth(s, n), y + f.height(), mapColor(bkColor_));
    inText_ = track_;
    int pen = x;
    for (int i = 0; i < n; i++) {
        const GlyphBitmap& g = f.glyph(static_cast<unsigned char>(s[i]));
        for (int gy = 0; gy < g.h; gy++)
            for (int gx = 0; gx < g.w; gx++)
                if (g.px[static_cast<size_t>(gy) * static_cast<size_t>(g.w) + static_cast<size_t>(gx)])
                    plot(pen + g.left + gx, baseline - g.top + gy, ink);
        pen += g.advance;
    }
    inText_ = false;
    if (track_ && t_.text) t_.text->endRun(run_);
}

static int cstrLen(const char* s, int n) { return n >= 0 ? n : static_cast<int>(std::strlen(s)); }

bool Canvas::textOut(int x, int y, const char* s, int n) {
    const Font* f = fontPtr();
    if (!f || !f->hasFace()) return false;
    n = cstrLen(s, n);
    const int w = f->textWidth(s, n);
    switch (align_ & TA_CENTER) {
    case TA_RIGHT: x -= w; break;
    case TA_CENTER: x -= w / 2; break;
    default: break;
    }
    switch (align_ & TA_BASELINE) {
    case TA_BOTTOM: y -= f->height(); break;
    case TA_BASELINE: y -= f->ascent(); break;
    default: break;
    }
    drawRun(x, y, *f, s, n);
    return true;
}

GdiSize Canvas::textExtent(const char* s, int n) const {
    const Font* f = fontPtr();
    if (!f || !f->hasFace()) return {};
    return {f->textWidth(s, cstrLen(s, n)), f->height()};
}

// The lines DrawText would print: prefix characters removed, paragraphs split at "\r\n" / '\n' / '\r', and with DT_WORDBREAK
// each paragraph broken so that a line fits `width`: greedy, at the last blank of the line, blanks hanging over the edge,
// a word wider than the line broken inside; the blanks at a break are dropped.
std::vector<std::string> Canvas::layout(const char* s, int n, int width, unsigned fmt) const {
    std::vector<std::string> lines;
    const Font* f = fontPtr();
    if (!f || !f->hasFace()) return lines;
    n = cstrLen(s, n);
    std::string text;
    if (fmt & DT_NOPREFIX) text.assign(s, static_cast<size_t>(n));
    else
        for (int i = 0; i < n; i++) {     // '&&' = '&', '&x' = x (the mnemonic underline is not drawn)
            if (s[i] == '&' && i + 1 < n) { if (s[i + 1] == '&') text += '&', i++; }
            else if (s[i] != '&') text += s[i];
        }
    if (text.empty()) return lines;
    const bool wrap = (fmt & DT_WORDBREAK) && !(fmt & DT_SINGLELINE);
    auto adv = [&](char c) { return f->advance(static_cast<unsigned char>(c)); };
    size_t pos = 0;
    while (true) {
        size_t pe = pos;
        if (!(fmt & DT_SINGLELINE)) while (pe < text.size() && text[pe] != '\n' && text[pe] != '\r') pe++;
        else pe = text.size();
        size_t p = pos;
        if (!wrap || pe == p) {
            lines.push_back(text.substr(p, pe - p));
        } else {
            while (p < pe) {
                size_t q = p, lastSpace = std::string::npos;
                int w = 0;
                while (q < pe) {
                    const int cw = adv(text[q]);
                    if (text[q] != ' ' && w + cw > width && q > p) break;
                    w += cw;
                    if (text[q] == ' ') lastSpace = q;
                    q++;
                }
                size_t le, next;
                if (q >= pe) { le = pe; next = pe; }
                else if (lastSpace != std::string::npos && lastSpace > p) {     // break after the last blank run
                    size_t sb = lastSpace;
                    while (sb > p && text[sb - 1] == ' ') sb--;
                    le = sb;
                    next = lastSpace + 1;
                    while (next < pe && text[next] == ' ') next++;
                } else { le = q; next = q; }                                    // a single word wider than the rect
                lines.push_back(text.substr(p, le - p));
                p = next;
            }
        }
        if (pe >= text.size()) break;
        pos = pe + ((text[pe] == '\r' && pe + 1 < text.size() && text[pe + 1] == '\n') ? 2 : 1);
        if (pos >= text.size()) break;     // a final line break does not start another (empty) line
    }
    return lines;
}

int Canvas::drawText(const char* s, int n, GdiRect* rect, unsigned fmt) {
    const Font* f = fontPtr();
    if (!f || !f->hasFace() || !rect) return 0;
    const int width = rect->right - rect->left;
    const bool wrap = (fmt & DT_WORDBREAK) && !(fmt & DT_SINGLELINE);
    const std::vector<std::string> lines = layout(s, n, width, fmt);
    if (lines.empty()) return 0;
    const int lh = f->height(), total = static_cast<int>(lines.size()) * lh;
    auto lineWidth = [&](const std::string& l) { return f->textWidth(l.data(), static_cast<int>(l.size())); };
    if (fmt & DT_CALCRECT) {
        if (!wrap) {
            int mw = 0;
            for (const std::string& l : lines) mw = std::max(mw, lineWidth(l));
            rect->right = rect->left + mw;
        }
        rect->bottom = rect->top + total;
        return total;
    }
    int y = rect->top;
    if (fmt & DT_SINGLELINE) {
        if ((fmt & DT_BOTTOM)) y = rect->bottom - lh;
        else if (fmt & DT_VCENTER) y = rect->top + (rect->bottom - rect->top - lh) / 2;
    }
    const GdiRect savedClip = clip_;
    const bool savedOn = clipOn_;
    if (!(fmt & DT_NOCLIP)) { clip_ = *rect; clipOn_ = true; }
    for (const std::string& l : lines) {
        const int lw = lineWidth(l);
        int x = rect->left;
        if ((fmt & 3) == DT_CENTER) x += (width - lw) / 2;
        else if ((fmt & 3) == DT_RIGHT) x = rect->right - lw;
        if (!l.empty()) drawRun(x, y, *f, l.data(), static_cast<int>(l.size()));
        y += lh;
    }
    clip_ = savedClip;
    clipOn_ = savedOn;
    return total;
}

// ---- lines and areas

GdiPoint Canvas::moveTo(int x, int y) {
    GdiPoint o = pos_;
    pos_ = {x, y};
    return o;
}

void Canvas::stamp(int x, int y) {
    const int w = pen_.width, o = w / 2;
    const u8 c = mapColor(pen_.color);
    for (int j = 0; j < w; j++)
        for (int i = 0; i < w; i++) plot(x - o + i, y - o + j, c);
}

void Canvas::line(int x0, int y0, int x1, int y1, bool includeEnd) {
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    const u8 c = mapColor(pen_.color);
    while (true) {
        const bool last = x0 == x1 && y0 == y1;
        if (!last || includeEnd) {
            if (pen_.width > 1) stamp(x0, y0);
            else plot(x0, y0, c);
        }
        if (last) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

bool Canvas::lineTo(int x, int y) {
    if (pen_.style != 5) line(pos_.x, pos_.y, x, y, pen_.width > 1);
    pos_ = {x, y};
    return true;
}

bool Canvas::fillRect(const GdiRect& r, u32 c) {
    fillIdx(r.left, r.top, r.right, r.bottom, mapColor(c));
    return true;
}
bool Canvas::fillSolidRect(int x, int y, int cx, int cy, u32 c) {
    bkColor_ = c;
    fillIdx(x, y, x + cx, y + cy, mapColor(c));
    return true;
}
bool Canvas::fillSolidRect(const GdiRect& r, u32 c) { return fillSolidRect(r.left, r.top, r.right - r.left, r.bottom - r.top, c); }

bool Canvas::rectangle(int l, int t, int r, int b) {
    if (r <= l || b <= t) return false;
    if (!brush_.null) fillIdx(l + 1, t + 1, r - 1, b - 1, mapColor(brush_.color));
    if (pen_.style != 5) {
        const u8 c = mapColor(pen_.color);
        for (int x = l; x < r; x++) { plot(x, t, c); plot(x, b - 1, c); }
        for (int y = t; y < b; y++) { plot(l, y, c); plot(r - 1, y, c); }
    }
    return true;
}

// Ellipse (wedge = false) or pie sector: pixels whose centre is inside; interior in the brush colour, boundary pixels
// (a 4-neighbour outside the shape) in the pen colour.
bool Canvas::shape(int l, int t, int r, int b, bool wedge, int xs, int ys, int xe, int ye) {
    if (r <= l || b <= t) return false;
    const int w = r - l, h = b - t;
    const double cx = l + w / 2.0, cy = t + h / 2.0, a = w / 2.0, bb = h / 2.0;
    const double pi2 = 6.283185307179586;
    double a0 = 0, sweep = pi2;
    if (wedge && !(xs == xe && ys == ye)) {
        a0 = std::atan2(-(ys - cy), xs - cx);
        const double a1 = std::atan2(-(ye - cy), xe - cx);
        sweep = std::fmod(a1 - a0 + 2 * pi2, pi2);
    }
    std::vector<u8> in(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const double dx = (l + x + 0.5 - cx), dy = (t + y + 0.5 - cy);
            if ((dx / a) * (dx / a) + (dy / bb) * (dy / bb) > 1.0) continue;
            if (wedge && sweep < pi2 - 1e-9) {
                const double ang = std::fmod(std::atan2(-dy, dx) - a0 + 2 * pi2, pi2);
                if (ang > sweep) continue;
            }
            in[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = 1;
        }
    auto at = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && in[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]; };
    const u8 fillC = mapColor(brush_.color), penC = mapColor(pen_.color);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (!at(x, y)) continue;
            const bool edge = !at(x - 1, y) || !at(x + 1, y) || !at(x, y - 1) || !at(x, y + 1);
            if (edge && pen_.style != 5) plot(l + x, t + y, penC);
            else if (!brush_.null) plot(l + x, t + y, fillC);
        }
    return true;
}

bool Canvas::ellipse(int l, int t, int r, int b) { return shape(l, t, r, b, false, 0, 0, 0, 0); }
bool Canvas::pie(int l, int t, int r, int b, int xs, int ys, int xe, int ye) { return shape(l, t, r, b, true, xs, ys, xe, ye); }

bool Canvas::setPixel(int x, int y, u32 c) {
    if (x < 0 || y < 0 || x >= t_.w || y >= t_.h) return false;
    plot(x, y, mapColor(c));
    return true;
}

int Canvas::pixel(int x, int y) const {
    if (x < 0 || y < 0 || x >= t_.w || y >= t_.h) return -1;
    return t_.px[static_cast<size_t>(y) * static_cast<size_t>(t_.w) + static_cast<size_t>(x)];
}

} // namespace biosys
