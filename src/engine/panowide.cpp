#include "engine/panowide.h"
#include <algorithm>
#include <climits>
#include <cmath>

namespace biosys {

namespace {
constexpr double kRadius = 407.4366547808163;     // 2560 / 2 pi
constexpr double kRad = 0.017453292519943295;
constexpr double kInf = 1e18;

// d (page pixels) wrapped into (-1280, 1280]
double wrapPage(double d) {
    d = std::fmod(d, static_cast<double>(kPanoW));
    if (d > kPanoW / 2) d -= kPanoW;
    if (d <= -kPanoW / 2) d += kPanoW;
    return d;
}
} // namespace

WideWindow wideWindow(int outW, int outH) {
    WideWindow w;
    if (outW < Frame::W || outH < Frame::H) return w;
    w.scale = std::max(1, std::min(outW / Frame::W, outH / Frame::H));
    w.extra = std::max(0, (outW / w.scale - Frame::W) / 2);
    w.logicalW = Frame::W + 2 * w.extra;
    return w;
}

int widePointerFrameX(int logicalX, int extra) { return std::min(std::max(logicalX - extra, 0), Frame::W - 1); }

Rect wideClipToLogical(const Rect& c, int extra) {
    const int l = std::min(std::max(c.x, 0), Frame::W), r = std::min(std::max(c.x + c.w, l), Frame::W);
    return Rect{l + extra, c.y, r - l, c.h};
}

WideExtra wideExtra(const PanoCamera& cam, const WideLimits& limits) {
    WideExtra e{kInf, kInf, kInf, kInf};
    const double hw = cam.focal * std::tan(kWideMaxFovDeg * 0.5 * kRad);
    e.capLeft = std::max(0.0, hw - 320.5);
    e.capRight = std::max(0.0, hw - 319.5);
    if (!limits.on) return e;
    // the original view with its centre at the limit shows, at the row with the smallest D, the largest angle from the axis (D = F cos p - r sin p)
    const double pr = cam.pitchDeg * kRad, cs = std::cos(pr), sn = std::sin(pr);
    const double dmin = std::min(cam.focal * cs + 184.5 * sn, cam.focal * cs - 183.5 * sn);
    const double distL = wrapPage(cam.yaw - limits.yawMin), distR = wrapPage(limits.yawMax - cam.yaw);
    auto side = [&](double half, double dist) {
        if (dmin <= 1e-6 || dist < 0) return 0.0;
        const double a = std::atan2(half, dmin) + dist / kRadius;
        if (a >= 1.5707963267948966 - 1e-9) return kInf;
        return std::max(0.0, dmin * std::tan(a) - half);
    };
    e.limitLeft = side(320.5, distL);
    e.limitRight = side(319.5, distR);
    return e;
}

void wideEdgeCover(const Frame& ref, const Rect& view, const u8* bare, double& left, double& right) {
    long l = 0, r = 0;
    for (int y = 0; y < kViewH; y++) {
        const int fy = view.y + y;
        if (fy < 0 || fy >= Frame::H) continue;
        for (int i = 0; i < kWideEdgeCols; i++) {
            const int xl = i, xr = kViewW - 1 - i;
            if (ref.px[static_cast<size_t>(fy) * Frame::W + view.x + xl] != bare[static_cast<size_t>(y) * kViewW + xl]) l++;
            if (ref.px[static_cast<size_t>(fy) * Frame::W + view.x + xr] != bare[static_cast<size_t>(y) * kViewW + xr]) r++;
        }
    }
    const double n = static_cast<double>(kWideEdgeCols) * kViewH;
    left = static_cast<double>(l) / n;
    right = static_cast<double>(r) / n;
}

WideDecision WideController::decide(const WideInput& in) {
    WideDecision d;
    if (!in.keep) {
        const Frame& ref = in.noFilter ? *in.noFilter : *in.frame;
        double cl = 0, cr = 0;
        wideEdgeCover(ref, in.view, in.bare, cl, cr);
        d.cover = std::max(cl, cr);
        if (!fallback_) {
            if (d.cover >= kWideEnterCover) { fallback_ = true; clear_ = 0; }
        } else if (d.cover < kWideExitCover) {
            if (++clear_ >= kWideExitFrames) { fallback_ = false; clear_ = 0; }
        } else {
            clear_ = 0;
        }
    }
    d.held = fallback_;
    if (fallback_) return d;
    const WideExtra e = wideExtra(in.cam, in.limits);
    const double capV = std::min(e.capLeft, e.capRight), limV = std::min(e.limitLeft, e.limitRight);
    auto toOut = [&](double v) { return v >= kInf / 2 ? INT_MAX : static_cast<int>(std::floor(v * in.scale + 1e-9)); };
    const int capB = toOut(capV), limB = toOut(limV), base = std::min(capB, limB);
    const int q = std::max(1, in.quantum);
    int sl = std::min(in.availLeft, base), sr = std::min(in.availRight, base);
    sl = std::max(0, sl / q * q);
    sr = std::max(0, sr / q * q);
    d.showLeft = sl;
    d.showRight = sr;
    d.wide = sl + sr > 0;
    const int availMin = std::min(in.availLeft, in.availRight);
    d.capped = capB < availMin;
    d.limited = limB < std::min(availMin, capB);
    d.fovDeg = 2.0 * std::atan((320.0 + 0.5 * (sl + sr) / in.scale) / in.cam.focal) / kRad;
    return d;
}

// ---------------------------------------------------------------------------------------------------------------------------------

void panoRainMap(const Frame& frame, const Frame& noFilter, const Rect& view, const u8* bare, std::vector<int16_t>& rain) {
    rain.assign(static_cast<size_t>(kViewW) * kViewH, -1);
    for (int y = 0; y < kViewH; y++) {
        const int fy = view.y + y;
        if (fy < 0 || fy >= Frame::H) continue;
        for (int x = 0; x < kViewW; x++) {
            const size_t fi = static_cast<size_t>(fy) * Frame::W + view.x + x, vi = static_cast<size_t>(y) * kViewW + x;
            if (frame.px[fi] != noFilter.px[fi] && frame.px[fi] != bare[vi]) rain[vi] = frame.px[fi];
        }
    }
}

namespace {
// triangle wave over the columns [kLo, kHi] of the centre pattern
int triColumn(int t) {
    constexpr int kLo = 10, kHi = 637, m = kHi - kLo;
    int q = (t - kLo) % (2 * m);
    if (q < 0) q += 2 * m;
    if (q > m) q = 2 * m - q;
    return kLo + q;
}
}

int wideSideRain(const std::vector<int16_t>& rain, int x, int y) {
    if (x >= 0 && x < kViewW) return -1;
    int sx, dy;
    if (x < 0) { sx = triColumn(19 - x); dy = 131; }       // mirror about column 9.5: x = -1 shows column 20
    else { sx = triColumn(1277 - x); dy = 227; }           // mirror about column 638.5: x = 640 shows column 637
    const int sy = ((y - dy) % kViewH + kViewH) % kViewH;
    return rain[static_cast<size_t>(sy) * kViewW + sx];
}

void wideSideRainBlock(const std::vector<int16_t>& rain, bool right, int cols, std::vector<int16_t>& out) {
    out.assign(static_cast<size_t>(cols) * kViewH, -1);
    for (int y = 0; y < kViewH; y++)
        for (int i = 0; i < cols; i++) out[static_cast<size_t>(y) * cols + i] = static_cast<int16_t>(wideSideRain(rain, right ? kViewW + i : -cols + i, y));
}

// ---------------------------------------------------------------------------------------------------------------------------------

static inline u32 rgbOf(const Palette& pal, u8 i) {
    const Color& c = pal.c[i];
    return (static_cast<u32>(c.r) << 16) | (static_cast<u32>(c.g) << 8) | c.b;
}

void composeCentred(const Frame& frame, const Palette& pal, int scale, int outW, std::vector<u32>& out) {
    const int OH = Frame::H * scale, left = (outW - Frame::W * scale) / 2;
    out.assign(static_cast<size_t>(outW) * OH, 0);
    u32 lut[256];
    for (int i = 0; i < 256; i++) lut[i] = rgbOf(pal, static_cast<u8>(i));
    for (int Y = 0; Y < OH; Y++)
        for (int X = 0; X < Frame::W * scale; X++)
            out[static_cast<size_t>(Y) * outW + left + X] = lut[frame.px[static_cast<size_t>(Y / scale) * Frame::W + X / scale]];
}

void dimWideSides(std::vector<u32>& out, int outW, const WideCanvas& cv, int scale, const Rect& view) {
    const int centreW = Frame::W * scale;
    for (int y = 0; y < kViewH * scale; y++) {
        u32* row = &out[static_cast<size_t>(view.y * scale + y) * outW];
        for (int X = cv.left - cv.showLeft; X < cv.left + centreW + cv.showRight; X++) {
            if (X >= cv.left && X < cv.left + centreW) { X = cv.left + centreW - 1; continue; }
            if (X < 0 || X >= outW) continue;
            u32 p = row[X], q = 0;
            for (int sh = 0; sh <= 16; sh += 8) q |= static_cast<u32>(std::floor(((p >> sh) & 255) * kWideStatusSideBrightness + 0.5)) << sh;
            row[X] = q;
        }
    }
}

int composeSmoothWide(const Frame& frame, const Palette& pal, const Rect& view, const u8* bare, const std::vector<u32>& tex, const PanoCamera& cam,
                      int scale, bool linear, const WideCanvas& cv, const std::vector<int16_t>* rain, std::vector<u32>& out, bool statusScreen) {
    int uncovered = 0;
    composeCentred(frame, pal, scale, cv.outW, out);
    const int W = cv.outW, centreW = Frame::W * scale, left = cv.left;
    u32 lut[256];
    for (int i = 0; i < 256; i++) lut[i] = rgbOf(pal, static_cast<u8>(i));
    std::vector<u8> mask;
    panoOverlayMask(frame, view, bare, mask);
    const int cell = panoCellFor(scale);
    auto cells = [&](int outCols) { return (outCols + scale * cell - 1) / (scale * cell) * cell; };
    PanoMesh mesh;
    buildPanoMeshRange(cam, cell, -cells(cv.showLeft), kViewW + cells(cv.showRight), mesh);
    std::vector<u32> sm(static_cast<size_t>(W) * kViewH * scale, 0xFFFFFFFFu);
    rasterizePanoRegion(mesh, tex, scale, linear, W, kViewH * scale, left, left - cv.showLeft, left + centreW + cv.showRight, sm);
    for (int y = 0; y < kViewH * scale; y++) {
        u32* orow = &out[static_cast<size_t>(view.y * scale + y) * W];
        const u32* srow = &sm[static_cast<size_t>(y) * W];
        for (int X = left - cv.showLeft; X < left + centreW + cv.showRight; X++) {
            const bool centre = X >= left && X < left + centreW;
            if (centre && (statusScreen || mask[static_cast<size_t>(y / scale) * kViewW + (X - left) / scale])) continue;
            const u32 s = srow[X];
            if (s == 0xFFFFFFFFu) { uncovered++; continue; }
            orow[X] = s;
            if (!centre && rain) {
                const int fx = X < left ? -1 - (left - 1 - X) / scale : kViewW + (X - left - centreW) / scale;
                const int r = wideSideRain(*rain, fx, y / scale);
                if (r >= 0) orow[X] = lut[r & 255];
            }
        }
    }
    if (statusScreen) dimWideSides(out, W, cv, scale, view);
    return uncovered;
}

} // namespace biosys
