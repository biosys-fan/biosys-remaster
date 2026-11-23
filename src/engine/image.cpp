#include "engine/image.h"
#include <cstdio>
#include <memory>

namespace biosys {

int Palette::find(Color col) const {
    for (int i = 0; i < 256; i++)
        if (c[i] == col) return i;
    return -1;
}

Palette Palette::fromRiff(const std::vector<u8>& file) {
    if (file.size() != 0x418) throw Error("palette file is " + std::to_string(file.size()) + " bytes, expected 1048");
    ByteReader r(file);
    if (r.cstrAt(0, 4) != "RIFF" || r.cstrAt(8, 8) != "PAL data") throw Error("palette is not a RIFF PAL file");
    if (r.u16At(20) != 0x0300 || r.u16At(22) != 256) throw Error("palette header has unexpected version/count");
    Palette p;
    for (int i = 0; i < 256; i++) {
        p.c[i].r = r.u8At(24 + 4 * i);
        p.c[i].g = r.u8At(25 + 4 * i);
        p.c[i].b = r.u8At(26 + 4 * i);
    }
    return p;
}

Palette Palette::load(const std::string& path) {
    try {
        return fromRiff(readFile(path));
    } catch (const Error& e) {
        throw Error(path + ": " + e.what());
    }
}

static void put32(std::vector<u8>& v, u32 x) {
    for (int i = 0; i < 4; i++) v.push_back(static_cast<u8>(x >> (8 * i)));
}
static void put16(std::vector<u8>& v, u32 x) {
    v.push_back(static_cast<u8>(x)); v.push_back(static_cast<u8>(x >> 8));
}

void writeBmp(const std::string& path, const Image8& img, const Palette& pal) {
    if (img.w <= 0 || img.h <= 0 || img.px.size() != static_cast<size_t>(img.w) * img.h)
        throw Error("writeBmp: invalid image");
    size_t bstride = (static_cast<size_t>(img.w) + 3) & ~size_t(3);
    size_t dataSize = bstride * img.h;
    u32 off = 14 + 40 + 1024;
    std::vector<u8> out;
    out.reserve(off + dataSize);
    out.push_back('B'); out.push_back('M');
    put32(out, static_cast<u32>(off + dataSize)); put32(out, 0); put32(out, off);
    put32(out, 40); put32(out, static_cast<u32>(img.w)); put32(out, static_cast<u32>(img.h));
    put16(out, 1); put16(out, 8); put32(out, 0); put32(out, static_cast<u32>(dataSize));
    put32(out, 2835); put32(out, 2835); put32(out, 256); put32(out, 0);
    for (int i = 0; i < 256; i++) {
        out.push_back(pal.c[i].b); out.push_back(pal.c[i].g); out.push_back(pal.c[i].r); out.push_back(0);
    }
    for (int y = img.h - 1; y >= 0; y--) {
        const u8* row = &img.px[static_cast<size_t>(y) * img.w];
        out.insert(out.end(), row, row + img.w);
        out.insert(out.end(), bstride - img.w, 0);
    }
    std::unique_ptr<FILE, int (*)(FILE*)> f(std::fopen(path.c_str(), "wb"), std::fclose);
    if (!f) throw Error("cannot create " + path);
    if (std::fwrite(out.data(), 1, out.size(), f.get()) != out.size()) throw Error("write error on " + path);
}

} // namespace biosys
