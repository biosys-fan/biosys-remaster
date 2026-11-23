// "Light picture cleaner" (--picture clean): palette-aware de-dither of an 8-bit picture and Catmull-Rom enlargement. SDL-free.
// The algorithm is defined by upscale-trial/light_spec.md and its reference implementation (scripts/light/reference.py); the output of
// every function below is that reference's, byte for byte (tests/test_picclean.sh: the five vectors and more inputs).
//
// One generalisation of the reference, with the same output where the two overlap: a picture's "skip" mask (1 byte per pixel) marks
// pixels that are not filtered and do not take part in their neighbours' averages (the reference's key index and protect mask). A
// colour-keyed picture is the skip mask (index == key); a frame whose transparent view pixels belong to a panorama is the skip mask too.
#pragma once
#include "engine/image.h"
#include <cstdint>
#include <vector>

namespace biosys {
namespace picclean {

constexpr int kT0 = 14;            // threshold at full contrast
constexpr int kTMin = 2, kTMax = 16;
constexpr int kSoftOne = 32;       // the full weight of a soft partner table (weights are 0..kSoftOne)
constexpr int kGMin = 50;          // smallest threshold scale in percent

enum class Kernel { Bilinear, Catmull, Mitchell, Lanczos3 };
bool parseKernel(const std::string& name, Kernel& k);

// (2|dR| + 4|dG| + 3|dB|) / 9
int dist(const Color& a, const Color& b);

// standard deviation of luma (1000 x grey level) of the picture under `pal`, pixel weighted by hist[256]; 0 for an empty histogram
int lumaStd(const Color* pal, const uint32_t* hist);
// T = clamp(t0 * g, 2, 16), g = clamp(lumaStd(pal) / lumaStd(ref), 50 %, 100 %); ref null (or flat): clamp(t0, 2, 16)
int threshold(const Color* pal, const Color* ref, const uint32_t* hist, int t0 = kT0);
// index histogram of the pixels not skipped (skip may be null)
void histogram(const uint8_t* ix, int w, int h, const uint8_t* skip, uint32_t* hist);

// cl[a * 256 + b] = 1 iff a == b, or neither is `key` and dist(pal[a], pal[b]) <= T (key < 0: none). 65536 bytes.
void partnerTable(const Color* pal, int T, int key, uint8_t* cl);

// The soft variant of the partner test (an option of this port; the reference is the hard test): instead of 0 / 1 an entry is a weight 0..kSoftOne, kSoftOne for
// dist <= T, falling linearly to 0 at T + ramp, so that the cleaned colour is a continuous function of the palette (no pixel flips from dithered to
// smooth in one palette step). T100 = T in 1/100 units (clampT100); distances are the exact (2|dR| + 4|dG| + 3|dB|) / 9. ramp = 0: 16 for
// dist <= T, else 0, which is the reference's test (to the rounding of the integer division, which the reference floors; use partnerTable then).
int thresholdT100(const Color* pal, const Color* ref, const uint32_t* hist, int t0 = kT0);   // clamp(t0 * g, 200, 1600): T x 100, g in percent
void softTable(const Color* pal, int T100, int ramp100, int key, uint8_t* w);

// A partner table that remembers its previous state (the remedy for the flicker of discrete decisions while a palette is mixed, light_spec.md 7):
// an entry turns on only when dist <= T - hyst and off only when dist > T + hyst, in between it keeps its value. hyst = 0 is exactly partnerTable
// (the reference). reset() forgets (another picture).
class PartnerState {
public:
    void reset() { have_ = false; haveSlew_ = false; }
    const uint8_t* update(const Color* pal, int T, int key, int hyst);
    const uint8_t* table() const { return cl_.data(); }
    // The other remedy (what the window uses): weights 0..16 (a softTable) that follow the reference's decision (16 where dist <= T, else 0) by at
    // most `slew` per palette change, so a pair of colours that crosses the threshold while the light is mixed fades from dithered to smooth (or
    // back) over 16 / slew palette steps instead of flipping in one: no pixel's cleaned colour moves by more than about slew / 16 of its dither
    // amplitude per step. The table is taken exactly (no lag) for a new picture (reset()), for the first call and when the palette moved by more
    // than kSlewJump in any channel since the previous call (lights, goggles, a new location: not a time-of-day step). Calls with the same
    // palette and T do not advance it. slew <= 0: the reference's table at once (values 0 / 1 as partnerTable).
    const uint8_t* updateSlew(const Color* pal, int T, int key, int slew, bool& soft);
    static constexpr int kSlewJump = 4;
private:
    bool have_ = false;
    bool haveSlew_ = false;
    int lastT_ = -1;
    Color lastPal_[256];
    std::vector<uint8_t> cl_ = std::vector<uint8_t>(65536, 0);
};

// Stage 1, rows [y0, y1) of the picture: the four 2x2 boxes around each pixel. rgb: interleaved, w * h * 3 (only the rows written).
// skip: null or w * h bytes. wrap: horizontal wrap-around (panoramas), else replicated edges; vertical edges replicate.
// soft: null, or a softTable: the weights replace cl (cl is ignored); box weight = the smallest weight of its three other pixels, and
//   out = c + sum_b(B_b * (mean_b - c)) / max(kSoftOne, sum_b B_b) with the weights in 1/kSoftOne (all weights 0 / kSoftOne: exactly the reference, same rounding).
void dedither(const uint8_t* ix, int w, int h, const Color* pal, const uint8_t* cl, const uint8_t* skip, bool wrap, int y0, int y1, uint8_t* rgb,
              const uint8_t* soft = nullptr);
// The same without any filtering (the palette colours), rows [y0, y1).
void paletteRows(const uint8_t* ix, int w, const Color* pal, int y0, int y1, uint8_t* rgb);

// Stage 2: skipped pixels (skip != 0) take the colour of the nearest pixel that is not (4-neighbour dilation, test order W, E, N, S, state at the
// start of each pass; the reference). maxPasses > 0 stops after that many passes (the colour of the farther skipped pixels is then left as it is:
// they sit more than maxPasses pixels from any visible one, where an enlarged alpha of 0 hides them).
void keyFill(uint8_t* rgb, const uint8_t* skip, int w, int h, int maxPasses = 0);

// Stage 3: separable enlargement by s = 1..4 (integer weights of the spec, sum 256 per phase). For the source rows [y0, y1) writes the output
// rows [y0 * s, y1 * s); `out` points at the first of them: output pixel (X, Y) goes to out[(Y - y0 * s) * stride + X] (packed 0xFFRRGGBB: rgb in,
// alpha 255), stride in pixels.
void enlargeRgb(const uint8_t* rgb, int w, int h, int s, Kernel k, bool wrap, int y0, int y1, uint32_t* out, size_t stride);
// One plane (the alpha of a keyed picture): out[Y * stride + X] = clamp(...)
void enlargePlane(const uint8_t* plane, int w, int h, int s, Kernel k, bool wrap, int y0, int y1, uint8_t* out, size_t stride);
// The integer weights: tab[phase][tap], nt = 2 * r taps starting at x - (r - 1); returns r
int weightTable(Kernel k, int s, int (&tab)[4][6]);

// The reference's whole pipeline (picture + palette -> RGB or RGBA out of `process`): used by the vector test and the bench tool.
struct Params {
    int w = 0, h = 0;
    const uint8_t* ix = nullptr;
    const Color* pal = nullptr;
    const Color* ref = nullptr;        // day palette (threshold scale) or null
    int key = -1;
    const uint8_t* prot = nullptr;     // protect mask or null (block-overwritten after the enlargement)
    bool wrap = false;
    int scale = 2;
    Kernel kernel = Kernel::Catmull;
    int t0 = kT0;
    bool dedither = true;
};
struct Result {
    int T = 0;
    std::vector<uint8_t> dd;           // w * h * 3: after stage 1 (before key fill)
    std::vector<uint8_t> out;          // (w * s) * (h * s) * (3 or 4 with a key)
};
void process(const Params& p, Result& r);

} // namespace picclean
} // namespace biosys
