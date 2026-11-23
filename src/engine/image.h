// CONTRACT — 8-bit images and palettes.
#pragma once
#include "engine/common.h"

namespace biosys {

struct Color {
    u8 r = 0, g = 0, b = 0;
    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b; }
};

// 256-colour palette. File format (.PAL): RIFF container of exactly 0x418 bytes —
// "RIFF" u32 "PAL " "data" u32, u16 0x0300, u16 256, then 256 x {R,G,B,flags}.
struct Palette {
    Color c[256];
    // Index of the first entry equal to col, or -1.
    int find(Color col) const;
    // Parse a RIFF .PAL file image. Throws Error if it is not a 0x418-byte RIFF palette.
    static Palette fromRiff(const std::vector<u8>& file);
    static Palette load(const std::string& path);
};

// Decoded picture, always normalised to: top-down rows, no row padding, one byte per pixel
// (palette index). px.size() == w * h.
struct Image8 {
    int w = 0, h = 0;
    std::vector<u8> px;
    u8 at(int x, int y) const { return px[static_cast<size_t>(y) * w + x]; }
};

// Write an 8-bit BMP (bottom-up, rows padded to 4 bytes, 256-entry colour table from pal).
void writeBmp(const std::string& path, const Image8& img, const Palette& pal);

} // namespace biosys
