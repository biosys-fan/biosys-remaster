#include "engine/common.h"
#include <cstdio>
#include <memory>

namespace biosys {

static void oob(size_t off, size_t len, size_t n) {
    throw Error("read of " + std::to_string(len) + " bytes at offset " + std::to_string(off) +
                " is outside buffer of " + std::to_string(n) + " bytes");
}

const u8* ByteReader::bytesAt(size_t off, size_t len) const {
    if (off > n_ || len > n_ - off) oob(off, len, n_);
    return d_ + off;
}

u8 ByteReader::u8At(size_t off) const { return *bytesAt(off, 1); }

u16 ByteReader::u16At(size_t off) const {
    const u8* p = bytesAt(off, 2);
    return static_cast<u16>(p[0] | (p[1] << 8));
}

u32 ByteReader::u32At(size_t off) const {
    const u8* p = bytesAt(off, 4);
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}

std::string ByteReader::cstrAt(size_t off, size_t maxLen) const {
    const u8* p = bytesAt(off, maxLen);
    size_t n = 0;
    while (n < maxLen && p[n]) n++;
    return std::string(reinterpret_cast<const char*>(p), n);
}

std::vector<u8> readFile(const std::string& path) {
    std::unique_ptr<FILE, int (*)(FILE*)> f(std::fopen(path.c_str(), "rb"), std::fclose);
    if (!f) throw Error("cannot open " + path);
    std::vector<u8> out;
    u8 buf[65536];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof buf, f.get())) > 0) out.insert(out.end(), buf, buf + got);
    if (std::ferror(f.get())) throw Error("read error on " + path);
    return out;
}

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x + 32);
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y + 32);
        if (x != y) return false;
    }
    return true;
}

} // namespace biosys
