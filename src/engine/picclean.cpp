#include "engine/picclean.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace biosys {
namespace picclean {

bool parseKernel(const std::string& n, Kernel& k) {
    if (n == "catmull") k = Kernel::Catmull;
    else if (n == "mitchell") k = Kernel::Mitchell;
    else if (n == "lanczos3") k = Kernel::Lanczos3;
    else if (n == "bilinear") k = Kernel::Bilinear;
    else return false;
    return true;
}

int dist(const Color& a, const Color& b) {
    return (2 * std::abs(a.r - b.r) + 4 * std::abs(a.g - b.g) + 3 * std::abs(a.b - b.b)) / 9;
}

static int64_t isqrt64(int64_t v) {
    if (v <= 0) return 0;
    int64_t r = static_cast<int64_t>(std::sqrt(static_cast<double>(v)));
    while (r * r > v) r--;
    while ((r + 1) * (r + 1) <= v) r++;
    return r;
}

int lumaStd(const Color* pal, const uint32_t* hist) {
    int64_t n = 0, l[256], s1 = 0;
    for (int i = 0; i < 256; i++) {
        n += hist[i];
        l[i] = 299 * pal[i].r + 587 * pal[i].g + 114 * pal[i].b;
        s1 += l[i] * hist[i];
    }
    if (n == 0) return 0;
    const int64_t m = (s1 + n / 2) / n;
    int64_t var = 0;
    for (int i = 0; i < 256; i++) var += static_cast<int64_t>(hist[i]) * (l[i] - m) * (l[i] - m);
    return static_cast<int>(isqrt64(var / n));
}

int threshold(const Color* pal, const Color* ref, const uint32_t* hist, int t0) {
    auto clampT = [](int v) { return std::max(kTMin, std::min(kTMax, v)); };
    if (!ref) return clampT(t0);
    const int sc = lumaStd(pal, hist), sr = lumaStd(ref, hist);
    if (sr == 0) return clampT(t0);
    const int g = std::min(100, std::max(kGMin, (100 * sc + sr / 2) / sr));
    return clampT((t0 * g + 50) / 100);
}

int thresholdT100(const Color* pal, const Color* ref, const uint32_t* hist, int t0) {
    auto clampT = [](int v) { return std::max(kTMin * 100, std::min(kTMax * 100, v)); };
    if (!ref) return clampT(t0 * 100);
    const int sc = lumaStd(pal, hist), sr = lumaStd(ref, hist);
    if (sr == 0) return clampT(t0 * 100);
    const int g = std::min(100, std::max(kGMin, (100 * sc + sr / 2) / sr));
    return clampT(t0 * g);
}

void softTable(const Color* pal, int T100, int ramp100, int key, uint8_t* wt) {
    std::memset(wt, 0, 65536);
    for (int a = 0; a < 256; a++) {
        wt[a * 256 + a] = kSoftOne;
        if (a == key) continue;
        for (int b = a + 1; b < 256; b++) {
            if (b == key) continue;
            const int num = 2 * std::abs(pal[a].r - pal[b].r) + 4 * std::abs(pal[a].g - pal[b].g) + 3 * std::abs(pal[a].b - pal[b].b);   // 9 x dist
            const int d100 = num * 100 / 9;
            int v;
            if (d100 <= T100) v = kSoftOne;
            else if (ramp100 <= 0 || d100 >= T100 + ramp100) v = 0;
            else v = kSoftOne * (T100 + ramp100 - d100) / ramp100;
            wt[a * 256 + b] = wt[b * 256 + a] = static_cast<uint8_t>(v);
        }
    }
}

void histogram(const uint8_t* ix, int w, int h, const uint8_t* skip, uint32_t* hist) {
    std::memset(hist, 0, 256 * sizeof(uint32_t));
    const size_t n = static_cast<size_t>(w) * h;
    if (!skip) {
        for (size_t i = 0; i < n; i++) hist[ix[i]]++;
    } else {
        for (size_t i = 0; i < n; i++) hist[ix[i]] += skip[i] ? 0u : 1u;
    }
}

void partnerTable(const Color* pal, int T, int key, uint8_t* cl) {
    std::memset(cl, 0, 65536);
    for (int a = 0; a < 256; a++) {
        cl[a * 256 + a] = 1;
        if (a == key) continue;
        for (int b = a + 1; b < 256; b++) {
            if (b == key) continue;
            if (dist(pal[a], pal[b]) <= T) cl[a * 256 + b] = cl[b * 256 + a] = 1;
        }
    }
}

const uint8_t* PartnerState::update(const Color* pal, int T, int key, int hyst) {
    if (hyst <= 0 || !have_) {
        partnerTable(pal, T, key, cl_.data());
        have_ = true;
        return cl_.data();
    }
    for (int a = 0; a < 256; a++) {
        cl_[a * 256 + a] = 1;
        if (a == key) continue;
        for (int b = a + 1; b < 256; b++) {
            if (b == key) { cl_[a * 256 + b] = cl_[b * 256 + a] = 0; continue; }
            const int d = dist(pal[a], pal[b]);
            uint8_t v = cl_[a * 256 + b];
            if (d <= T - hyst) v = 1;
            else if (d > T + hyst) v = 0;
            cl_[a * 256 + b] = cl_[b * 256 + a] = v;
        }
    }
    return cl_.data();
}

const uint8_t* PartnerState::updateSlew(const Color* pal, int T, int key, int slew, bool& soft) {
    if (slew <= 0) { soft = false; return update(pal, T, key, 0); }
    soft = true;
    int maxMove = 0;
    if (haveSlew_)
        for (int i = 0; i < 256; i++)
            maxMove = std::max(maxMove, std::max(std::abs(pal[i].r - lastPal_[i].r), std::max(std::abs(pal[i].g - lastPal_[i].g), std::abs(pal[i].b - lastPal_[i].b))));
    if (haveSlew_ && maxMove == 0 && T == lastT_) return cl_.data();
    const bool takeAsIs = !haveSlew_ || maxMove > kSlewJump;
    for (int a = 0; a < 256; a++) {
        cl_[a * 256 + a] = kSoftOne;
        if (a == key) continue;
        for (int b = a + 1; b < 256; b++) {
            int target = (b != key && dist(pal[a], pal[b]) <= T) ? kSoftOne : 0;
            int v = cl_[a * 256 + b];
            if (takeAsIs) v = target;
            else if (target > v) v = std::min(target, v + slew);
            else if (target < v) v = std::max(target, v - slew);
            cl_[a * 256 + b] = cl_[b * 256 + a] = static_cast<uint8_t>(v);
        }
    }
    haveSlew_ = true;
    have_ = false;
    lastT_ = T;
    for (int i = 0; i < 256; i++) lastPal_[i] = pal[i];
    return cl_.data();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// stage 1

void paletteRows(const uint8_t* ix, int w, const Color* pal, int y0, int y1, uint8_t* rgb) {
    for (int y = y0; y < y1; y++) {
        const uint8_t* s = ix + static_cast<size_t>(y) * w;
        uint8_t* o = rgb + static_cast<size_t>(y) * w * 3;
        for (int x = 0; x < w; x++) {
            const Color& c = pal[s[x]];
            o[3 * x] = c.r; o[3 * x + 1] = c.g; o[3 * x + 2] = c.b;
        }
    }
}

void dedither(const uint8_t* ix, int w, int h, const Color* pal, const uint8_t* cl, const uint8_t* skip, bool wrap, int y0, int y1, uint8_t* rgb,
              const uint8_t* soft) {
    if (w <= 0 || h <= 0) return;
    int PR[256], PG[256], PB[256];
    for (int i = 0; i < 256; i++) { PR[i] = pal[i].r; PG[i] = pal[i].g; PB[i] = pal[i].b; }
    static const int kRecip[17] = {0, 0, 0, 0, 16384, 0, 0, 0, 8192, 0, 0, 0, 5461, 0, 0, 0, 4096};
    std::vector<uint8_t> pad[3], spad[3];
    for (int k = 0; k < 3; k++) { pad[k].assign(static_cast<size_t>(w) + 2, 0); if (skip) spad[k].assign(static_cast<size_t>(w) + 2, 0); }
    auto fill = [&](std::vector<uint8_t>& d, const uint8_t* row) {
        std::memcpy(&d[1], row, static_cast<size_t>(w));
        d[0] = wrap ? row[w - 1] : row[0];
        d[static_cast<size_t>(w) + 1] = wrap ? row[0] : row[w - 1];
    };
    for (int y = y0; y < y1; y++) {
        const int ys[3] = {std::max(y - 1, 0), y, std::min(y + 1, h - 1)};
        for (int k = 0; k < 3; k++) {
            fill(pad[k], ix + static_cast<size_t>(ys[k]) * w);
            if (skip) fill(spad[k], skip + static_cast<size_t>(ys[k]) * w);
        }
        const uint8_t *p0 = pad[0].data(), *p1 = pad[1].data(), *p2 = pad[2].data();
        const uint8_t *q0 = skip ? spad[0].data() : nullptr, *q1 = skip ? spad[1].data() : nullptr, *q2 = skip ? spad[2].data() : nullptr;
        uint8_t* o = rgb + static_cast<size_t>(y) * w * 3;
        for (int x = 0; x < w; x++, o += 3) {
            const int c = p1[x + 1];
            if (skip && q1[x + 1]) { o[0] = static_cast<uint8_t>(PR[c]); o[1] = static_cast<uint8_t>(PG[c]); o[2] = static_cast<uint8_t>(PB[c]); continue; }
            const int a = p0[x], b = p0[x + 1], cq = p0[x + 2], d = p1[x], f = p1[x + 2], g = p2[x], hh = p2[x + 1], i = p2[x + 2];
            if (a == c && b == c && cq == c && d == c && f == c && g == c && hh == c && i == c) {   // flat: nothing to average
                o[0] = static_cast<uint8_t>(PR[c]); o[1] = static_cast<uint8_t>(PG[c]); o[2] = static_cast<uint8_t>(PB[c]);
                continue;
            }
            // neighbour order: 0 a(NW) 1 b(N) 2 c(NE) 3 d(W) 4 f(E) 5 g(SW) 6 h(S) 7 i(SE)
            const int nb[8] = {a, b, cq, d, f, g, hh, i};
            int ok[8];
            bool fractional = false;
            if (soft) {
                const uint8_t* row = soft + c * 256;
                for (int k = 0; k < 8; k++) { const int v = row[nb[k]]; ok[k] = v; fractional |= v != 0 && v != kSoftOne; }
            } else {
                const uint8_t* row = cl + c * 256;
                for (int k = 0; k < 8; k++) ok[k] = row[nb[k]];
            }
            if (skip) {
                if (q0[x]) ok[0] = 0;
                if (q0[x + 1]) ok[1] = 0;
                if (q0[x + 2]) ok[2] = 0;
                if (q1[x]) ok[3] = 0;
                if (q1[x + 2]) ok[4] = 0;
                if (q2[x]) ok[5] = 0;
                if (q2[x + 1]) ok[6] = 0;
                if (q2[x + 2]) ok[7] = 0;
            }
            int sr = 0, sg = 0, sb = 0, sw = 0;
            static const int boxes[4][3] = {{0, 1, 3}, {1, 2, 4}, {3, 5, 6}, {4, 6, 7}};
            if (soft && fractional) {
                // out = c + sum B_b (mean_b - c) / max(kSoftOne, sum B_b); mean_b = box sum / 4, B in 1/kSoftOne
                int num[3] = {0, 0, 0}, S = 0;
                for (const auto& bx : boxes) {
                    const int B = std::min(ok[bx[0]], std::min(ok[bx[1]], ok[bx[2]]));
                    if (B == 0) continue;
                    S += B;
                    int bs[3] = {PR[c], PG[c], PB[c]};
                    for (int k = 0; k < 3; k++) { const int n = nb[bx[k]]; bs[0] += PR[n]; bs[1] += PG[n]; bs[2] += PB[n]; }
                    const int cc[3] = {PR[c], PG[c], PB[c]};
                    for (int k = 0; k < 3; k++) num[k] += B * (bs[k] - 4 * cc[k]);   // 4 x (mean_b - c), in 1/16
                }
                if (S == 0) { o[0] = static_cast<uint8_t>(PR[c]); o[1] = static_cast<uint8_t>(PG[c]); o[2] = static_cast<uint8_t>(PB[c]); continue; }
                const int D = std::max(kSoftOne, S) * 4;
                const int cc[3] = {PR[c], PG[c], PB[c]};
                for (int k = 0; k < 3; k++) {
                    const int v = cc[k] * D + num[k];                 // value x D
                    o[k] = static_cast<uint8_t>(std::min(255, std::max(0, (2 * v + D) / (2 * D))));
                }
                continue;
            }
            for (const auto& bx : boxes) {
                if (!(ok[bx[0]] && ok[bx[1]] && ok[bx[2]])) continue;
                for (int k = 0; k < 3; k++) { const int n = nb[bx[k]]; sr += PR[n]; sg += PG[n]; sb += PB[n]; }
                sr += PR[c]; sg += PG[c]; sb += PB[c];
                sw += 4;
            }
            if (sw == 0) {
                o[0] = static_cast<uint8_t>(PR[c]); o[1] = static_cast<uint8_t>(PG[c]); o[2] = static_cast<uint8_t>(PB[c]);
            } else {
                const int m = kRecip[sw];
                o[0] = static_cast<uint8_t>((sr * m + 32768) >> 16);
                o[1] = static_cast<uint8_t>((sg * m + 32768) >> 16);
                o[2] = static_cast<uint8_t>((sb * m + 32768) >> 16);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------
// stage 2: nearest-visible fill, breadth first = the reference's passes

void keyFill(uint8_t* rgb, const uint8_t* skip, int w, int h, int maxPasses) {
    const size_t n = static_cast<size_t>(w) * h;
    std::vector<uint8_t> known(n);
    std::vector<uint32_t> front, next, fromOf;
    bool any = false;
    for (size_t i = 0; i < n; i++) { known[i] = skip[i] ? 0 : 1; any |= known[i] != 0; }
    if (!any) return;
    // candidates of the first pass: unknown pixels with a known 4-neighbour
    std::vector<uint8_t> queued(n, 0);
    auto consider = [&](size_t i, std::vector<uint32_t>& lst) {
        if (known[i] || queued[i]) return;
        const int x = static_cast<int>(i % w), y = static_cast<int>(i / w);
        if ((x > 0 && known[i - 1]) || (x < w - 1 && known[i + 1]) || (y > 0 && known[i - w]) || (y < h - 1 && known[i + w])) {
            queued[i] = 1;
            lst.push_back(static_cast<uint32_t>(i));
        }
    };
    for (size_t i = 0; i < n; i++) if (!known[i]) consider(i, front);
    std::vector<uint32_t> src;
    for (int pass = 1; !front.empty() && (maxPasses <= 0 || pass <= maxPasses); pass++) {
        src.assign(front.size(), 0);
        for (size_t k = 0; k < front.size(); k++) {   // the source is chosen against the state at the start of the pass
            const size_t i = front[k];
            const int x = static_cast<int>(i % w), y = static_cast<int>(i / w);
            size_t j;
            if (x > 0 && known[i - 1]) j = i - 1;
            else if (x < w - 1 && known[i + 1]) j = i + 1;
            else if (y > 0 && known[i - w]) j = i - w;
            else j = i + w;
            src[k] = static_cast<uint32_t>(j);
        }
        for (size_t k = 0; k < front.size(); k++) std::memcpy(rgb + static_cast<size_t>(front[k]) * 3, rgb + static_cast<size_t>(src[k]) * 3, 3);
        for (uint32_t i : front) known[i] = 1;
        next.clear();
        for (uint32_t i : front) {
            const int x = static_cast<int>(i % w), y = static_cast<int>(i / w);
            if (x > 0) consider(i - 1, next);
            if (x < w - 1) consider(i + 1, next);
            if (y > 0) consider(i - w, next);
            if (y < h - 1) consider(i + w, next);
        }
        front.swap(next);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------
// stage 3

namespace {
struct Tab { int r; int nt; int w[4][6]; };
// spec 3.6 (= reference.py weight_table); the taps start at x - (r - 1)
const Tab kBilinear[4] = {{1, 2, {{256, 0}}}, {1, 2, {{256, 0}, {192, 64}}}, {1, 2, {{256, 0}, {256, 0}, {171, 85}}}, {1, 2, {{256, 0}, {256, 0}, {224, 32}, {160, 96}}}};
const Tab kCatmull[4] = {{2, 4, {{0, 256, 0, 0}}},
                         {2, 4, {{57, 217, -18, 0}, {-18, 222, 58, -6}}},
                         {2, 4, {{82, 192, -18, 0}, {0, 256, 0, 0}, {-19, 199, 85, -9}}},
                         {2, 4, {{96, 178, -18, 0}, {23, 245, -12, 0}, {-12, 247, 23, -2}, {-19, 186, 100, -11}}}};
const Tab kMitchell[4] = {{2, 4, {{14, 228, 14, 0}}},
                          {2, 4, {{65, 197, -6, 0}, {-6, 200, 66, -4}}},
                          {2, 4, {{87, 177, -8, 0}, {14, 228, 14, 0}, {-8, 182, 88, -6}}},
                          {2, 4, {{98, 167, -9, 0}, {35, 220, 1, 0}, {1, 220, 36, -1}, {-9, 172, 100, -7}}}};
const Tab kLanczos[4] = {{3, 6, {{0, 0, 256, 0, 0, 0}}},
                         {3, 6, {{-18, 70, 230, -34, 8, 0}, {8, -34, 228, 69, -17, 2}}},
                         {3, 6, {{-24, 99, 211, -38, 8, 0}, {0, 0, 256, 0, 0, 0}, {8, -37, 208, 98, -24, 3}}},
                         {3, 6, {{-28, 114, 200, -38, 8, 0}, {-8, 31, 250, -22, 5, 0}, {5, -22, 250, 31, -8, 0}, {8, -38, 196, 113, -27, 4}}}};
const Tab& tabOf(Kernel k, int s) {
    if (s < 1 || s > 4) throw Error("picclean: scale must be 1..4");
    switch (k) {
    case Kernel::Bilinear: return kBilinear[s - 1];
    case Kernel::Mitchell: return kMitchell[s - 1];
    case Kernel::Lanczos3: return kLanczos[s - 1];
    default: return kCatmull[s - 1];
    }
}

struct PackRgb {   // 3 channels -> 0xFFRRGGBB
    static void put(uint32_t* o, const int* v) {
        auto cl = [](int q) { return static_cast<uint32_t>(q < 0 ? 0 : q > 255 ? 255 : q); };
        *o = 0xFF000000u | (cl(v[0]) << 16) | (cl(v[1]) << 8) | cl(v[2]);
    }
};
struct PackPlane {
    static void put(uint8_t* o, const int* v) { *o = static_cast<uint8_t>(v[0] < 0 ? 0 : v[0] > 255 ? 255 : v[0]); }
};

// Generic enlargement of an N-channel interleaved 8-bit picture. Horizontal pass: value x 16 in int16 (|v| <= 5300), then the vertical
// pass with a ring of the nt rows it needs.
template <int N, int NT, class Pack, class Out>
void enlargeImpl(const uint8_t* src, int w, int h, int s, Kernel kern, bool wrap, int y0, int y1, Out* out, size_t stride) {
    if (w <= 0 || h <= 0 || y1 <= y0) return;
    if (s == 1) {
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < w; x++) {
                int v[N];
                for (int c = 0; c < N; c++) v[c] = src[(static_cast<size_t>(y) * w + x) * N + c];
                Pack::put(&out[static_cast<size_t>(y - y0) * stride + x], v);
            }
        return;
    }
    const Tab& t = tabOf(kern, s);
    const int r = t.r, lo = r - 1, hi = r, W = w * s;
    constexpr int nt = NT;
    std::vector<uint8_t> pad(static_cast<size_t>(w + lo + hi) * N);
    std::vector<int16_t> ring[6];
    for (int k = 0; k < nt; k++) ring[k].assign(static_cast<size_t>(W) * N, 0);
    int ringRow[6];
    for (int k = 0; k < nt; k++) ringRow[k] = -1;
    auto hrow = [&](int y) -> const int16_t* {       // the horizontally enlarged source row y (cached in the ring by y % nt)
        const int slot = y % nt;
        if (ringRow[slot] == y) return ring[slot].data();
        const uint8_t* row = src + static_cast<size_t>(y) * w * N;
        std::memcpy(&pad[static_cast<size_t>(lo) * N], row, static_cast<size_t>(w) * N);
        for (int k = 0; k < lo; k++) std::memcpy(&pad[static_cast<size_t>(k) * N], row + static_cast<size_t>(wrap ? w - lo + k : 0) * N, N);
        for (int k = 0; k < hi; k++) std::memcpy(&pad[static_cast<size_t>(w + lo + k) * N], row + static_cast<size_t>(wrap ? k : w - 1) * N, N);
        int16_t* o = ring[slot].data();
        for (int x = 0; x < w; x++) {
            const uint8_t* p = &pad[static_cast<size_t>(x) * N];
            for (int k = 0; k < s; k++) {
                const int* wt = t.w[k];
                for (int c = 0; c < N; c++) {
                    int sum = 0;
                    for (int j = 0; j < nt; j++) sum += wt[j] * p[j * N + c];
                    o[(static_cast<size_t>(x) * s + k) * N + c] = static_cast<int16_t>(sum >> 4);
                }
            }
        }
        ringRow[slot] = y;
        return o;
    };
    for (int y = y0; y < y1; y++) {
        const int16_t* rows[6];
        for (int j = 0; j < nt; j++) rows[j] = hrow(std::min(std::max(y + j - lo, 0), h - 1));
        for (int k = 0; k < s; k++) {
            const int* wt = t.w[k];
            Out* o = out + static_cast<size_t>((y - y0) * s + k) * stride;
            for (int X = 0; X < W; X++) {
                int v[N];
                for (int c = 0; c < N; c++) {
                    int sum = 2048;
                    for (int j = 0; j < nt; j++) sum += wt[j] * rows[j][static_cast<size_t>(X) * N + c];
                    v[c] = sum >> 12;
                }
                Pack::put(&o[X], v);
            }
        }
    }
}
} // namespace

int weightTable(Kernel k, int s, int (&tab)[4][6]) {
    const Tab& t = tabOf(k, s);
    for (int p = 0; p < 4; p++) for (int j = 0; j < 6; j++) tab[p][j] = t.w[p][j];
    return t.r;
}

void enlargeRgb(const uint8_t* rgb, int w, int h, int s, Kernel k, bool wrap, int y0, int y1, uint32_t* out, size_t stride) {
    if (tabOf(k, std::max(1, s)).nt == 6) enlargeImpl<3, 6, PackRgb, uint32_t>(rgb, w, h, s, k, wrap, y0, y1, out, stride);
    else if (tabOf(k, std::max(1, s)).nt == 4) enlargeImpl<3, 4, PackRgb, uint32_t>(rgb, w, h, s, k, wrap, y0, y1, out, stride);
    else enlargeImpl<3, 2, PackRgb, uint32_t>(rgb, w, h, s, k, wrap, y0, y1, out, stride);
}
void enlargePlane(const uint8_t* plane, int w, int h, int s, Kernel k, bool wrap, int y0, int y1, uint8_t* out, size_t stride) {
    if (tabOf(k, std::max(1, s)).nt == 6) enlargeImpl<1, 6, PackPlane, uint8_t>(plane, w, h, s, k, wrap, y0, y1, out, stride);
    else if (tabOf(k, std::max(1, s)).nt == 4) enlargeImpl<1, 4, PackPlane, uint8_t>(plane, w, h, s, k, wrap, y0, y1, out, stride);
    else enlargeImpl<1, 2, PackPlane, uint8_t>(plane, w, h, s, k, wrap, y0, y1, out, stride);
}

// ---------------------------------------------------------------------------------------------------------------------------------

void process(const Params& p, Result& r) {
    const int w = p.w, h = p.h, s = p.scale;
    const size_t n = static_cast<size_t>(w) * h;
    std::vector<uint8_t> skip;           // key and protected pixels
    const bool haveSkip = p.key >= 0 || p.prot;
    if (haveSkip) {
        skip.assign(n, 0);
        for (size_t i = 0; i < n; i++) skip[i] = (p.key >= 0 && p.ix[i] == p.key) || (p.prot && p.prot[i]);
    }
    uint32_t hist[256];
    std::memset(hist, 0, sizeof hist);
    for (size_t i = 0; i < n; i++) hist[p.ix[i]]++;
    if (p.key >= 0) hist[p.key] = 0;     // (the reference subtracts protected pixels from the histogram of their index afterwards)
    if (p.prot) for (size_t i = 0; i < n; i++) if (p.prot[i]) hist[p.ix[i]]--;
    r.T = threshold(p.pal, p.ref, hist, p.t0);
    r.dd.assign(n * 3, 0);
    if (p.dedither) {
        std::vector<uint8_t> cl(65536);
        partnerTable(p.pal, r.T, p.key, cl.data());
        dedither(p.ix, w, h, p.pal, cl.data(), haveSkip ? skip.data() : nullptr, p.wrap, 0, h, r.dd.data());
    } else {
        paletteRows(p.ix, w, p.pal, 0, h, r.dd.data());
    }
    std::vector<uint8_t> src = r.dd;
    if (p.key >= 0) {
        std::vector<uint8_t> ks(n);
        for (size_t i = 0; i < n; i++) ks[i] = p.ix[i] == p.key;
        keyFill(src.data(), ks.data(), w, h, 0);
    }
    const size_t W = static_cast<size_t>(w) * s, H = static_cast<size_t>(h) * s;
    std::vector<uint32_t> big(W * H);
    enlargeRgb(src.data(), w, h, s, p.kernel, p.wrap, 0, h, big.data(), W);
    const int ch = p.key >= 0 ? 4 : 3;
    r.out.assign(W * H * ch, 0);
    for (size_t i = 0; i < W * H; i++) {
        r.out[i * ch] = static_cast<uint8_t>(big[i] >> 16);
        r.out[i * ch + 1] = static_cast<uint8_t>(big[i] >> 8);
        r.out[i * ch + 2] = static_cast<uint8_t>(big[i]);
    }
    if (p.prot) {   // protected source pixels: their exact colour over the s x s output block
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                if (!p.prot[static_cast<size_t>(y) * w + x]) continue;
                const uint8_t* c = &r.dd[(static_cast<size_t>(y) * w + x) * 3];
                for (int dy = 0; dy < s; dy++)
                    for (int dx = 0; dx < s; dx++) std::memcpy(&r.out[((static_cast<size_t>(y) * s + dy) * W + static_cast<size_t>(x) * s + dx) * ch], c, 3);
            }
    }
    if (p.key >= 0) {
        std::vector<uint8_t> mask(n), alpha(W * H);
        for (size_t i = 0; i < n; i++) mask[i] = p.ix[i] == p.key ? 0 : 255;
        if (s > 1) enlargePlane(mask.data(), w, h, s, p.kernel, false, 0, h, alpha.data(), W);
        else alpha = mask;
        for (size_t i = 0; i < W * H; i++) r.out[i * 4 + 3] = alpha[i];
    }
}

} // namespace picclean
} // namespace biosys
