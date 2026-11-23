// InstallShield 5 cabinet (single volume) reader. Layout follows analysis/scripts/is5cab.py.
#include "install/internal.h"
#include <algorithm>
#include <cstring>
#include <zlib.h>

namespace biosys::install {

namespace {
std::string damagedCab() {
    return "DATA1.CAB on the disc is damaged or incomplete. " + std::string(kRedownload);
}
} // namespace

Cabinet::Cabinet(const ByteSource& src) : src_(src) {
    if (src.size() < 0x200) throw Error(damagedCab());
    uint8_t hd[20];
    src.read(0, hd, sizeof hd);
    ByteReader h(hd, sizeof hd);
    if (h.u32At(0) != 0x28635349) throw Error("DATA1.CAB is not an InstallShield cabinet. Is this the Biosys Setup CD?");
    const uint32_t cdo = h.u32At(12), cds = h.u32At(16);
    if (cds < 0x30 || cds > (1u << 20) || uint64_t(cdo) + cds > src.size()) throw Error(damagedCab());
    std::vector<uint8_t> cd(cds);
    src.read(cdo, cd.data(), cd.size());
    ByteReader c(cd);
    const uint32_t fto = c.u32At(0x0c);
    const uint32_t fts = std::max(c.u32At(0x14), c.u32At(0x18));
    const uint32_t ndirs = c.u32At(0x1c), nfiles = c.u32At(0x28);
    if (ndirs > 20000 || nfiles > 50000) throw Error(damagedCab());
    const uint64_t base = uint64_t(cdo) + fto;
    if (base >= src.size()) throw Error(damagedCab());
    const uint64_t ftLen = std::min<uint64_t>({uint64_t(fts) + 0x10000, src.size() - base, 32u << 20});
    if ((uint64_t(ndirs) + nfiles) * 4 > ftLen) throw Error(damagedCab());
    std::vector<uint8_t> ft(ftLen);
    src.read(base, ft.data(), ft.size());
    ByteReader t(ft);
    auto cstr = [&](uint32_t o) {
        if (o >= ft.size()) throw Error(damagedCab());
        const void* z = std::memchr(&ft[o], 0, ft.size() - o);
        if (!z) throw Error(damagedCab());
        return std::string(reinterpret_cast<const char*>(&ft[o]), static_cast<const uint8_t*>(z) - &ft[o]);
    };
    std::vector<std::string> dirs;
    for (uint32_t i = 0; i < ndirs; i++) dirs.push_back(cstr(t.u32At(size_t(i) * 4)));
    uint64_t total = 0;
    for (uint32_t i = 0; i < nfiles; i++) {
        const uint32_t o = t.u32At((size_t(ndirs) + i) * 4);
        if (o > ft.size() || ft.size() - o < 42) throw Error(damagedCab());
        const uint32_t nameOff = t.u32At(o), diri = t.u32At(o + 4);
        CabFile f;
        f.flags = t.u16At(o + 8);
        f.esize = t.u32At(o + 10);
        f.csize = t.u32At(o + 14);
        f.dataOff = t.u32At(o + 38);
        if (f.flags & 8) continue;
        if (diri >= ndirs) throw Error(damagedCab());
        const std::string name = cstr(nameOff);
        f.path = dirs[diri].empty() ? name : dirs[diri] + "\\" + name;
        const uint64_t span = (f.flags & 4) ? f.csize : f.esize;
        if (f.dataOff > src.size() || span > src.size() - f.dataOff) throw Error(damagedCab());
        total += f.esize;
        if (total > kMaxDiscBytes) throw Error("The cabinet on the disc claims more than 2 GB of files, so it is refused. " + std::string(kRedownload));
        files_.push_back(std::move(f));
    }
}

void Cabinet::extract(const CabFile& f, const std::function<void(const uint8_t*, size_t)>& sink) const {
    std::vector<uint8_t> out(1 << 18);
    if (!(f.flags & 4)) {
        uint64_t pos = 0;
        while (pos < f.esize) {
            const size_t n = size_t(std::min<uint64_t>(out.size(), f.esize - pos));
            src_.read(f.dataOff + pos, out.data(), n);
            sink(out.data(), n);
            pos += n;
        }
        return;
    }
    z_stream zs;
    std::memset(&zs, 0, sizeof zs);
    if (inflateInit2(&zs, -15) != Z_OK) throw Error("Internal error: cannot start the decompressor.");
    struct Ender { z_stream* z; ~Ender() { inflateEnd(z); } } ender{&zs};
    uint64_t pos = f.dataOff, left = f.csize, got = 0;
    std::vector<uint8_t> in;
    while (left > 0) {
        uint8_t lb[2];
        if (left < 2) throw Error(damagedCab());
        src_.read(pos, lb, 2);
        const size_t n = size_t(lb[0] | lb[1] << 8);
        pos += 2;
        left -= 2;
        if (n == 0 || n > left) throw Error(damagedCab());
        in.resize(n);
        src_.read(pos, in.data(), n);
        pos += n;
        left -= n;
        inflateReset(&zs);
        zs.next_in = in.data();
        zs.avail_in = static_cast<uInt>(n);
        int r = Z_OK;
        while (r != Z_STREAM_END) {
            zs.next_out = out.data();
            zs.avail_out = static_cast<uInt>(out.size());
            r = inflate(&zs, Z_NO_FLUSH);
            const size_t made = out.size() - zs.avail_out;
            if (made) {
                if (got + made > f.esize) throw Error(damagedCab());
                sink(out.data(), made);
                got += made;
            }
            if (r != Z_OK && r != Z_STREAM_END) throw Error(damagedCab());
            if (r == Z_OK && zs.avail_in == 0 && zs.avail_out != 0) throw Error(damagedCab());
        }
    }
    if (got != f.esize) throw Error(damagedCab());
}

} // namespace biosys::install
