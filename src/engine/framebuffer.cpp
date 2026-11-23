#include "engine/framebuffer.h"
#include <algorithm>

namespace biosys {

Image8 Frame::toImage() const {
    Image8 img;
    img.w = W; img.h = H; img.px = px;
    return img;
}

static Rect clipToFrame(const Rect& c) {
    int x0 = std::max(c.x, 0), y0 = std::max(c.y, 0);
    int x1 = std::min(c.x + c.w, Frame::W), y1 = std::min(c.y + c.h, Frame::H);
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

void blit(Frame& dst, const Image8& src, int dx, int dy, const Rect& clipIn, int transparent) {
    if (src.w <= 0 || src.h <= 0 || src.px.size() < static_cast<size_t>(src.w) * src.h) return;
    Rect c = clipToFrame(clipIn);
    int x0 = std::max(dx, c.x), x1 = std::min(dx + src.w, c.x + c.w);
    int y0 = std::max(dy, c.y), y1 = std::min(dy + src.h, c.y + c.h);
    if (x0 >= x1 || y0 >= y1) return;
    for (int y = y0; y < y1; y++) {
        const u8* s = &src.px[static_cast<size_t>(y - dy) * src.w + (x0 - dx)];
        u8* d = &dst.px[static_cast<size_t>(y) * Frame::W + x0];
        int n = x1 - x0;
        if (transparent < 0) {
            std::copy(s, s + n, d);
        } else {
            for (int i = 0; i < n; i++)
                if (s[i] != transparent) d[i] = s[i];
        }
    }
}

void blitRegion(Surface& dst, const Image8& src, int dx, int dy, int sx, int sy, int w, int h, int transparent) {
    if (src.w <= 0 || src.h <= 0 || src.px.size() < static_cast<size_t>(src.w) * src.h) return;
    // clip the source window to the image, then the destination to the surface
    if (sx < 0) { dx -= sx; w += sx; sx = 0; }
    if (sy < 0) { dy -= sy; h += sy; sy = 0; }
    w = std::min(w, src.w - sx); h = std::min(h, src.h - sy);
    if (dx < 0) { sx -= dx; w += dx; dx = 0; }
    if (dy < 0) { sy -= dy; h += dy; dy = 0; }
    w = std::min(w, dst.w - dx); h = std::min(h, dst.h - dy);
    if (w <= 0 || h <= 0) return;
    for (int y = 0; y < h; y++) {
        const u8* s = &src.px[static_cast<size_t>(sy + y) * src.w + sx];
        u8* d = dst.row(dy + y) + dx;
        if (transparent < 0) std::copy(s, s + w, d);
        else for (int i = 0; i < w; i++) if (s[i] != transparent) d[i] = s[i];
    }
}

Rect clipRect(const Rect& r, int sw, int sh) {
    int x0 = std::max(r.x, 0), y0 = std::max(r.y, 0);
    int x1 = std::min(r.x + r.w, sw), y1 = std::min(r.y + r.h, sh);
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

std::vector<u8> copyOut(const Surface& s, const Rect& r) {
    std::vector<u8> out;
    out.reserve(static_cast<size_t>(r.w) * r.h);
    for (int y = 0; y < r.h; y++) out.insert(out.end(), s.row(r.y + y) + r.x, s.row(r.y + y) + r.x + r.w);
    return out;
}

void copyIn(Surface& s, const Rect& r, const std::vector<u8>& px) {
    if (px.size() < static_cast<size_t>(r.w) * r.h) return;
    for (int y = 0; y < r.h; y++) std::copy(px.begin() + static_cast<size_t>(y) * r.w, px.begin() + static_cast<size_t>(y + 1) * r.w, s.row(r.y + y) + r.x);
}

void outlineRect(Frame& dst, const Rect& r, const Rect& clipIn, u8 index) {
    if (r.w <= 0 || r.h <= 0) return;
    Rect c = clipToFrame(clipIn);
    auto put = [&](int x, int y) {
        if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h)
            dst.px[static_cast<size_t>(y) * Frame::W + x] = index;
    };
    for (int x = r.x; x < r.x + r.w; x++) { put(x, r.y); put(x, r.y + r.h - 1); }
    for (int y = r.y; y < r.y + r.h; y++) { put(r.x, y); put(r.x + r.w - 1, y); }
}

} // namespace biosys
