// CONTRACT — shared basics for the engine core.
#pragma once
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace biosys {

using u8 = uint8_t;  using u16 = uint16_t;  using u32 = uint32_t;
using i16 = int16_t; using i32 = int32_t;

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Bounds-checked little-endian reader over a byte buffer. Every out-of-range access throws Error.
class ByteReader {
public:
    ByteReader(const u8* data, size_t size) : d_(data), n_(size) {}
    explicit ByteReader(const std::vector<u8>& v) : d_(v.data()), n_(v.size()) {}
    size_t size() const { return n_; }
    u8  u8At(size_t off) const;
    u16 u16At(size_t off) const;
    u32 u32At(size_t off) const;
    i32 i32At(size_t off) const { return static_cast<i32>(u32At(off)); }
    // NUL-terminated string inside a fixed-size field of maxLen bytes (junk after the NUL is ignored).
    std::string cstrAt(size_t off, size_t maxLen) const;
    // Pointer to len bytes at off (throws if out of range).
    const u8* bytesAt(size_t off, size_t len) const;
private:
    const u8* d_; size_t n_;
};

// Whole file into memory. Throws Error if it cannot be read.
std::vector<u8> readFile(const std::string& path);

// ASCII case-insensitive string equality.
bool iequals(const std::string& a, const std::string& b);

} // namespace biosys
