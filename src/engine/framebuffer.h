// 640x480 8-bit (palette index) frame and clipped blits. No SDL.
#pragma once
#include "engine/common.h"
#include "engine/image.h"
#include <memory>

namespace biosys {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

struct Frame {
    static constexpr int W = 640, H = 480;
    std::vector<u8> px = std::vector<u8>(static_cast<size_t>(W) * H, 0);
    void clear(u8 index) { px.assign(px.size(), index); }
    Image8 toImage() const;
};

// Off-screen 8-bit surface of any size (the "page surface": a whole page, 2560x1024 on panoramas).
class TextPlane;   // engine/textplane.h

struct Surface {
    int w = 0, h = 0;
    std::vector<u8> px;
    // GDI text drawn into this surface, only while text tracking is on (engine/textplane.h); a copy of the surface shares it
    std::shared_ptr<TextPlane> text;
    void init(int w_, int h_, u8 fill = 0) { w = w_; h = h_; px.assign(static_cast<size_t>(w_) * h_, fill); text.reset(); }
    u8* row(int y) { return &px[static_cast<size_t>(y) * w]; }
    const u8* row(int y) const { return &px[static_cast<size_t>(y) * w]; }
};

// Copies the (sx,sy,w,h) part of src to dst at (dx,dy), clipped to dst. Pixels equal to `transparent`
// (0..255) are skipped; pass -1 for an opaque copy.
void blitRegion(Surface& dst, const Image8& src, int dx, int dy, int sx, int sy, int w, int h, int transparent);
// Rectangle clipped to the surface (w/h may become 0).
Rect clipRect(const Rect& r, int surfaceW, int surfaceH);
// Save-under helpers: the (clipped) rect's pixels out of / back into a surface.
std::vector<u8> copyOut(const Surface& s, const Rect& clippedRect);
void copyIn(Surface& s, const Rect& clippedRect, const std::vector<u8>& px);

// Copies src to dst at (dx,dy), clipped to clip (and to the frame). Pixels equal to `transparent`
// (0..255) are skipped; pass -1 for an opaque blit.
void blit(Frame& dst, const Image8& src, int dx, int dy, const Rect& clip, int transparent = -1);

// 1-pixel rectangle outline, clipped.
void outlineRect(Frame& dst, const Rect& r, const Rect& clip, u8 index);

} // namespace biosys
