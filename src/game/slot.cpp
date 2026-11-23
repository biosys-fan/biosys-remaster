#include "game/slot.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace biosys::game {

std::string pyRepr(double v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
    // shortest digit string that round-trips, then Python's repr layout
    char buf[40];
    int prec = 0;
    for (; prec <= 16; prec++) {
        std::snprintf(buf, sizeof buf, "%.*e", prec, v);
        if (std::strtod(buf, nullptr) == v) break;
    }
    std::string s(buf);                       // d.ddde[+-]XX
    bool neg = s[0] == '-';
    if (neg) s.erase(0, 1);
    size_t e = s.find('e');
    int exp10 = std::atoi(s.c_str() + e + 1);
    std::string digits;
    for (size_t i = 0; i < e; i++) if (s[i] != '.') digits += s[i];
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    std::string out;
    if (exp10 >= -4 && exp10 < 16) {
        if (exp10 >= 0) {
            std::string ip = digits.substr(0, std::min<size_t>(digits.size(), exp10 + 1));
            while (ip.size() < static_cast<size_t>(exp10) + 1) ip += '0';
            std::string fp = digits.size() > static_cast<size_t>(exp10) + 1 ? digits.substr(exp10 + 1) : "0";
            out = ip + "." + fp;
        } else {
            out = "0." + std::string(static_cast<size_t>(-exp10 - 1), '0') + digits;
        }
    } else {
        out = digits.substr(0, 1);
        if (digits.size() > 1) out += "." + digits.substr(1);
        char eb[16];
        std::snprintf(eb, sizeof eb, "e%c%02d", exp10 < 0 ? '-' : '+', std::abs(exp10));
        out += eb;
    }
    return neg ? "-" + out : out;
}

std::string pyRepr(const std::string& s) {
    char q = (s.find('\'') != std::string::npos && s.find('"') == std::string::npos) ? '"' : '\'';
    std::string out(1, q);
    for (unsigned char c : s) {
        if (c == static_cast<unsigned char>(q) || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20 || c == 0x7f) { char b[8]; std::snprintf(b, sizeof b, "\\x%02x", c); out += b; }
        else out += static_cast<char>(c);
    }
    out += q;
    return out;
}

std::string bytesText(const u8* p, size_t n) {
    std::string out = std::to_string(n) + " bytes ";
    bool any = false;
    for (size_t i = 0; i < n; i++) if (p[i]) any = true;
    if (!any) return out + "all zero";
    char b[4];
    for (size_t i = 0; i < n; i++) { std::snprintf(b, sizeof b, "%02x", p[i]); out += b; }
    return out;
}

// ---------------------------------------------------------------------------------------------

SlotReader::SlotReader(const u8* data, size_t size, std::string what, std::vector<FieldLog>* log)
    : d_(data), n_(size), what_(std::move(what)), log_(log) {}

const u8* SlotReader::take(size_t n) {
    if (n > n_ - pos_)
        throw Error(what_ + ": member of " + std::to_string(n) + " bytes at offset " + std::to_string(pos_) +
                    " does not fit in " + std::to_string(n_) + " bytes");
    const u8* p = d_ + pos_;
    pos_ += n;
    return p;
}

void SlotReader::log(const char* name, const char* kind, size_t off, size_t n, std::string text) {
    if (log_) log_->push_back(FieldLog{name, kind, off, n, std::move(text)});
}

static u32 le32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

i32 SlotReader::readI32(const char* name) {
    size_t off = pos_;
    i32 v = static_cast<i32>(le32(take(4)));
    log(name, "i32", off, 4, std::to_string(v));
    return v;
}

u32 SlotReader::readU32(const char* name) {
    size_t off = pos_;
    u32 v = le32(take(4));
    log(name, "u32", off, 4, std::to_string(v));
    return v;
}

u32 SlotReader::readHex32(const char* name) {
    size_t off = pos_;
    u32 v = le32(take(4));
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", v);
    log(name, "hex32", off, 4, b);
    return v;
}

u16 SlotReader::readU16(const char* name) {
    size_t off = pos_;
    const u8* p = take(2);
    u16 v = static_cast<u16>(p[0] | (p[1] << 8));
    log(name, "u16", off, 2, std::to_string(v));
    return v;
}

u8 SlotReader::readU8(const char* name) {
    size_t off = pos_;
    u8 v = *take(1);
    log(name, "u8", off, 1, std::to_string(v));
    return v;
}

double SlotReader::readF64(const char* name) {
    size_t off = pos_;
    const u8* p = take(8);
    uint64_t bits = static_cast<uint64_t>(le32(p)) | (static_cast<uint64_t>(le32(p + 4)) << 32);
    double v;
    std::memcpy(&v, &bits, 8);
    log(name, "f64", off, 8, pyRepr(v));
    return v;
}

std::string SlotReader::readStr(const char* name, size_t n) {
    size_t off = pos_;
    const u8* p = take(n);
    size_t len = 0;
    while (len < n && p[len]) len++;
    std::string s(reinterpret_cast<const char*>(p), len);
    log(name, "str", off, n, pyRepr(s));
    return s;
}

std::vector<u8> SlotReader::readStrRaw(const char* name, size_t n) {
    size_t off = pos_;
    const u8* p = take(n);
    size_t len = 0;
    while (len < n && p[len]) len++;
    log(name, "str", off, n, pyRepr(std::string(reinterpret_cast<const char*>(p), len)));
    return std::vector<u8>(p, p + n);
}

std::vector<u8> SlotReader::readBytes(const char* name, size_t n) {
    size_t off = pos_;
    const u8* p = take(n);
    log(name, "bytes", off, n, bytesText(p, n));
    return std::vector<u8>(p, p + n);
}

void SlotReader::readZeros(const char* name, size_t n) {
    size_t off = pos_;
    const u8* p = take(n);
    for (size_t i = 0; i < n; i++)
        if (p[i]) throw Error(what_ + ": member " + name + " at offset " + std::to_string(off + i) + " is not zero");
    log(name, "zero", off, n, bytesText(p, n));
}

void SlotReader::skipPad(size_t n) {
    size_t off = pos_;
    const u8* p = take(n);
    for (size_t i = 0; i < n; i++)
        if (p[i]) throw Error(what_ + ": padding at offset " + std::to_string(off + i) + " is not zero");
    char nm[32];
    std::snprintf(nm, sizeof nm, "_pad@0x%zx", off);
    log(nm, "zero", off, n, bytesText(p, n));
}

void SlotReader::finishPad() {
    if (remaining()) skipPad(remaining());
}

std::vector<u8> SlotReader::takeRest(const char* name) {
    size_t off = pos_;
    size_t n = remaining();
    const u8* p = take(n);
    log(name, "bytes", off, n, bytesText(p, n));
    return std::vector<u8>(p, p + n);
}

// ---------------------------------------------------------------------------------------------

SlotWriter::SlotWriter(std::vector<u8>& out, size_t size, std::string what)
    : out_(out), base_(out.size()), n_(size), what_(std::move(what)) {
    out_.resize(base_ + size, 0);
}

u8* SlotWriter::put(size_t n) {
    if (n > n_ - pos_)
        throw Error(what_ + ": slot overflow: " + std::to_string(pos_ + n) + " bytes written, slot size is " +
                    std::to_string(n_));
    u8* p = out_.data() + base_ + pos_;
    pos_ += n;
    return p;
}

void SlotWriter::writeU32(u32 v) {
    u8* p = put(4);
    for (int i = 0; i < 4; i++) p[i] = static_cast<u8>(v >> (8 * i));
}

void SlotWriter::writeI32(i32 v) { writeU32(static_cast<u32>(v)); }

void SlotWriter::writeU16(u16 v) {
    u8* p = put(2);
    p[0] = static_cast<u8>(v);
    p[1] = static_cast<u8>(v >> 8);
}

void SlotWriter::writeU8(u8 v) { *put(1) = v; }

void SlotWriter::writeF64(double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    u8* p = put(8);
    for (int i = 0; i < 8; i++) p[i] = static_cast<u8>(bits >> (8 * i));
}

void SlotWriter::writeStr(const std::string& s, size_t n) {
    if (s.size() > n) throw Error(what_ + ": string '" + s + "' does not fit in char[" + std::to_string(n) + "]");
    u8* p = put(n);
    std::memcpy(p, s.data(), s.size());
}

void SlotWriter::writeBytes(const u8* src, size_t n) {
    u8* p = put(n);
    if (n) std::memcpy(p, src, n);
}

void SlotWriter::skip(size_t n) { put(n); }

} // namespace biosys::game
