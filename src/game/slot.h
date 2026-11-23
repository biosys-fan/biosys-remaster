// Bounds-checked helpers for the packed members of one save slot or fixed record.
// Layout reference: analysis/notes/saveblob.md. All integers are little-endian, doubles are IEEE binary64,
// BOOLs are dwords, names are fixed char arrays. Members are packed (no alignment), the rest of a slot is zero.
#pragma once
#include "engine/common.h"
#include <cstring>
#include <string>
#include <vector>

namespace biosys::game {

// One decoded member, as recorded by a logging SlotReader (used by `biosys-gametest dump`).
struct FieldLog {
    std::string name;     // member name, "_pad" for unnamed zero padding
    std::string kind;     // i32 u32 hex32 u16 u8 f64 str bytes zero
    size_t offset = 0, size = 0;
    std::string text;     // value in the format of analysis/scripts/biosys_savblob.py dump
};

// Python-compatible text of a value (repr(float), repr(str)).
std::string pyRepr(double v);
std::string pyRepr(const std::string& latin1);
std::string bytesText(const u8* p, size_t n);   // "N bytes all zero" or "N bytes <hex>"

class SlotReader {
public:
    SlotReader(const u8* data, size_t size, std::string what, std::vector<FieldLog>* log = nullptr);

    size_t pos() const { return pos_; }
    size_t size() const { return n_; }
    size_t remaining() const { return n_ - pos_; }
    const std::string& what() const { return what_; }

    i32 readI32(const char* name);
    u32 readU32(const char* name);
    u32 readHex32(const char* name);                 // bit mask, dumped as 0x%08x
    u16 readU16(const char* name);
    u8 readU8(const char* name);
    double readF64(const char* name);
    // char[n] holding a NUL-terminated string (bytes after the NUL are dropped).
    std::string readStr(const char* name, size_t n);
    // Like readStr but returns all n bytes (the text is the part before the first NUL); for fields whose bytes
    // after the NUL must survive a round trip.
    std::vector<u8> readStrRaw(const char* name, size_t n);
    // The 11-character name copies of CSelf / CLocalFX slots are readStr(name, 11).
    std::vector<u8> readBytes(const char* name, size_t n);   // opaque bytes carried verbatim
    void readZeros(const char* name, size_t n);              // named member that the writer never fills; throws if not 0
    void skipPad(size_t n);                                  // unnamed zero padding; throws if not 0
    void finishPad();                                        // the rest of the slot must be zero
    // Everything not yet read, verbatim (used by RawItem).
    std::vector<u8> takeRest(const char* name = "_raw");

private:
    const u8* take(size_t n);
    void log(const char* name, const char* kind, size_t off, size_t n, std::string text);
    const u8* d_;
    size_t n_, pos_ = 0;
    std::string what_;
    std::vector<FieldLog>* log_;
};

class SlotWriter {
public:
    // `size` is the slot size; the buffer starts zeroed (the original memsets every slot first).
    SlotWriter(std::vector<u8>& out, size_t size, std::string what);
    ~SlotWriter() = default;

    size_t pos() const { return pos_; }
    size_t size() const { return n_; }
    void writeI32(i32 v);
    void writeU32(u32 v);
    void writeU16(u16 v);
    void writeU8(u8 v);
    void writeF64(double v);
    void writeStr(const std::string& s, size_t n);   // NUL padded to n; throws if longer than n
    void writeBytes(const u8* p, size_t n);
    void writeBytes(const std::vector<u8>& v) { writeBytes(v.data(), v.size()); }
    void skip(size_t n);                              // zero bytes

private:
    u8* put(size_t n);
    std::vector<u8>& out_;
    size_t base_, n_, pos_ = 0;
    std::string what_;
};

// Anything saved as one fixed-size slot or record.
class Record {
public:
    virtual ~Record() = default;
    virtual void saveSlot(SlotWriter& w) const = 0;
    virtual void loadSlot(SlotReader& r) = 0;
};

} // namespace biosys::game
