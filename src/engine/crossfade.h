// Page cross-fades (TCMainWnd::DoCrossFadeAndPageJump exe:00444ca0 -> CrossFade exe:00413d10): the cross-fade
// definitions of a page record (formats.md 2.7, field roles read from the exe) and the colour blend of one frame.
#pragma once
#include "engine/common.h"
#include "engine/datafiles.h"
#include <vector>

namespace biosys {

// One 0x58-byte record of a page's cross-fade table (read straight from PAGES.DAT: PageFile only keeps
// steps and name). Layout (ints): [0] number of intermediate pictures n (0..8), [1..9] number of steps of
// segment 0..n (a segment blends two pictures in steps+1 frames, at least 2), [10] index in the page's file
// list of the .TAN holding the intermediate pictures (-1 = none), [11..18] their frame numbers, +0x4c name.
struct CrossFadeDef {
    u32 intermediates = 0;
    u32 steps[9] = {};
    i32 tanFile = -1;
    u32 frames[8] = {};
    std::string name;
};

// All definitions of page `pageNumber` (1-based); empty if it has none. Throws Error on a damaged record.
std::vector<CrossFadeDef> loadCrossFadeDefs(const DataFiles& files, int pageNumber);

constexpr int kCrossFadeW = 640, kCrossFadeH = 368;

// One frame of the blend (CrossFade_BuildBlendTable exe:004421d0 + CrossFade_MapPixels exe:0040d8a0).
// from/to: w*h palette indices, pal: 256 x 4 bytes (R,G,B,flags), xft: the 262144-byte lookup, level 0x400 =
// all `from` .. 0 = all `to`. Per pixel pair: channel = from*level + to*(0x400-level) (10-bit fixed point),
// out = xft[(S0 & ~0xfff) + ((S1 >> 12) << 6) + (S2 >> 12)] with S0,S1,S2 = the R,G,B sums.
void crossFadeBlend(const u8* from, const u8* to, size_t pixels, const u8* pal, const u8* xft, u32 level, u8* out);

// Level of frame k of a segment of n frames (n >= 2): 0x400 - (k * 0x400) / (n - 1).
inline u32 crossFadeLevel(u32 k, u32 n) { return 0x400 - (k * 0x400) / (n - 1); }

} // namespace biosys
