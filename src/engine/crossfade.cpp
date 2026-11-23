#include "engine/crossfade.h"
#include "engine/pagefile.h"
#include <cstdio>
#include <memory>

namespace biosys {

std::vector<CrossFadeDef> loadCrossFadeDefs(const DataFiles& files, int pageNumber) {
    PageFile pf(files.requireData("pages.dat"));
    const PageTableEntry& te = pf.entry(pageNumber);
    std::unique_ptr<FILE, int (*)(FILE*)> f(std::fopen(files.requireData("pages.dat").c_str(), "rb"), std::fclose);
    if (!f) throw Error("cannot open pages.dat");
    if (te.staticSize < 0x58 || te.staticSize > (16u << 20)) throw Error("page record has a bad size");
    std::vector<u8> rec(te.staticSize);
    if (std::fseek(f.get(), static_cast<long>(te.offset), SEEK_SET) != 0 || std::fread(rec.data(), 1, rec.size(), f.get()) != rec.size())
        throw Error("cannot read page record of " + te.name);
    ByteReader r(rec);
    const u32 n = r.u32At(0x24), off = r.u32At(0x48);
    std::vector<CrossFadeDef> out;
    for (u32 i = 0; i < n; i++) {
        const size_t o = static_cast<size_t>(off) + static_cast<size_t>(i) * 0x58;
        if (o + 0x58 > rec.size()) throw Error("cross-fade table of " + te.name + " out of range");
        CrossFadeDef d;
        d.intermediates = r.u32At(o);
        if (d.intermediates > 8) throw Error("cross-fade of " + te.name + " has too many intermediate pictures");
        for (int k = 0; k < 9; k++) d.steps[k] = r.u32At(o + 4 + 4 * static_cast<size_t>(k));
        d.tanFile = r.i32At(o + 0x28);
        for (int k = 0; k < 8; k++) d.frames[k] = r.u32At(o + 0x2c + 4 * static_cast<size_t>(k));
        d.name = r.cstrAt(o + 0x4c, 12);
        out.push_back(d);
    }
    return out;
}

void crossFadeBlend(const u8* from, const u8* to, size_t pixels, const u8* pal, const u8* xft, u32 level, u8* out) {
    u32 wFrom[256], wTo[256];
    for (u32 i = 0; i < 256; i++) { wFrom[i] = i * level; wTo[i] = i * (0x400 - level); }
    std::vector<i16> cache(65536, -1);   // pair (from << 8 | to) -> blended index, filled on first use
    for (size_t p = 0; p < pixels; p++) {
        const u32 key = static_cast<u32>(from[p]) << 8 | to[p];
        i16& c = cache[key];
        if (c < 0) {
            const u8* a = pal + 4 * static_cast<size_t>(from[p]);
            const u8* b = pal + 4 * static_cast<size_t>(to[p]);
            const u32 s0 = wFrom[a[0]] + wTo[b[0]], s1 = wFrom[a[1]] + wTo[b[1]], s2 = wFrom[a[2]] + wTo[b[2]];
            const u32 idx = ((s1 & 0xfffff03fu) >> 6) + (s2 >> 12) + (s0 & 0xfffff000u);
            c = xft[idx];
        }
        out[p] = static_cast<u8>(c);
    }
}

} // namespace biosys
