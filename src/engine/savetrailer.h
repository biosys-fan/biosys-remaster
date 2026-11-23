// The game-mode trailer of a saved game (the port's addition; SDL-free, header only).
//
// A .sav is the engine's part (header, game objects) followed by the game's blob (BiosGame.dll's part, a fixed size). The port appends
// one more block after the blob that records the settings the game was played under (realism, difficulty, fix list): the original's
// own bytes stay where they are, so a reader that knows only the original's layout simply stops at the end of the blob.
//
//   offset 0   8 bytes   magic "BSYSMODE"
//   offset 8   u32       version (1)
//   offset 12  u32       length N of the text
//   offset 16  N bytes   the settings text (see app/savesettings.h: "biosys-settings 1" and one key=value line each)
//   offset 16+N u32      FNV-1a (32 bit) of the text, so that a bit-damaged block is noticed
//
// The block is found from where the blob ends, not by searching: whoever reads the blob (the game logic's loader) knows its size.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace biosys {
namespace savetrailer {

constexpr char kMagic[8] = {'B', 'S', 'Y', 'S', 'M', 'O', 'D', 'E'};
constexpr uint32_t kVersion = 1;
constexpr size_t kMaxText = 4096;       // the settings text is a few dozen bytes; anything longer is not ours

inline uint32_t sum(const char* p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= static_cast<uint8_t>(p[i]); h *= 16777619u; }
    return h;
}
inline void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; i++) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
inline uint32_t get32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }

inline std::vector<uint8_t> make(const std::string& text) {
    std::vector<uint8_t> v(kMagic, kMagic + 8);
    put32(v, kVersion);
    put32(v, static_cast<uint32_t>(text.size()));
    v.insert(v.end(), text.begin(), text.end());
    put32(v, sum(text.data(), text.size()));
    return v;
}

enum class Status { Absent, Valid, Damaged };

// Does the file have a trailer (or the beginning of one) at `at`, the end of the blob? Valid: `text` is its text and `end` the end of
// the block (the file's size when nothing follows it). Damaged: the bytes at `at` start like a trailer (or are the first bytes of one:
// a file cut short) but are not a whole good one; `why` says what is wrong. Absent: nothing there, or something that is not ours.
inline Status inspect(const std::vector<uint8_t>& f, size_t at, std::string& text, std::string& why, size_t* end = nullptr) {
    if (at >= f.size()) return Status::Absent;
    const size_t have = f.size() - at;
    const size_t cmp = have < 8 ? have : 8;
    if (std::memcmp(f.data() + at, kMagic, cmp) != 0) return Status::Absent;
    if (have < 16) { why = "the settings block is cut short"; return Status::Damaged; }
    const uint32_t version = get32(f.data() + at + 8), n = get32(f.data() + at + 12);
    if (version != kVersion) { why = "the settings block is of version " + std::to_string(version); return Status::Damaged; }
    if (n > kMaxText || have < 16 + static_cast<size_t>(n) + 4) { why = "the settings block is cut short"; return Status::Damaged; }
    const char* t = reinterpret_cast<const char*>(f.data() + at + 16);
    if (get32(f.data() + at + 16 + n) != sum(t, n)) { why = "the settings block fails its check sum"; return Status::Damaged; }
    text.assign(t, n);
    if (end) *end = at + 16 + n + 4;
    return Status::Valid;
}

// Where the game's own part of a save ends: `blobStart` + `blobSize`, unless the file is longer and what follows looks like our block
// (good or damaged), in which case the blob still ends there. A file with other extra bytes is returned whole (the caller refuses it).
inline size_t blobEnd(const std::vector<uint8_t>& f, size_t blobStart, size_t blobSize) {
    const size_t at = blobStart + blobSize;
    if (at >= f.size()) return f.size();
    std::string text, why;
    return inspect(f, at, text, why) == Status::Absent ? f.size() : at;
}

} // namespace savetrailer
} // namespace biosys
