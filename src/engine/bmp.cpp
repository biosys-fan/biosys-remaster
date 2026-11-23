#include "engine/bmp.h"
#include <algorithm>

namespace biosys {

Bitmap8 readBmp8(const std::string& path) {
    std::vector<u8> d = readFile(path);
    ByteReader r(d);
    try {
        if (r.u8At(0) != 'B' || r.u8At(1) != 'M') throw Error("not a BMP");
        u32 off = r.u32At(10), hdr = r.u32At(14);
        if (hdr < 40) throw Error("unsupported BMP header");
        i32 w = r.i32At(18), h = r.i32At(22);
        u16 bpp = r.u16At(28);
        u32 comp = r.u32At(30), colors = r.u32At(46);
        if (bpp != 8 || comp != 0) throw Error("not an uncompressed 8-bit BMP");
        bool topDown = h < 0;
        if (topDown) h = -h;
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096) throw Error("bad BMP size");
        if (colors == 0 || colors > 256) colors = 256;
        Bitmap8 out;
        for (u32 i = 0; i < colors; i++) {
            size_t q = 14 + hdr + 4 * i;
            out.palette.c[i].b = r.u8At(q); out.palette.c[i].g = r.u8At(q + 1); out.palette.c[i].r = r.u8At(q + 2);
        }
        size_t stride = (static_cast<size_t>(w) + 3) & ~size_t(3);
        out.image.w = w; out.image.h = h;
        out.image.px.resize(static_cast<size_t>(w) * h);
        for (int y = 0; y < h; y++) {
            int srcRow = topDown ? y : h - 1 - y;
            const u8* s = r.bytesAt(off + stride * srcRow, w);
            std::copy(s, s + w, out.image.px.begin() + static_cast<size_t>(y) * w);
        }
        return out;
    } catch (const Error& e) {
        throw Error(path + ": " + e.what());
    }
}

} // namespace biosys
