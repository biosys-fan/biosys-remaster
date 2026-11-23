// Unit tests of the widescreen arithmetic (src/engine/panowide.*). No game data needed.
//   biosys-widetest window    the window geometry (logical size keeps the scale), pointer confinement to the centre
//   biosys-widetest fov       the field of view of the wide picture and its cap
//   biosys-widetest limits    the scroll limits (API 0x5a): nothing beyond the limit plus the original half width is shown
//   biosys-widetest fallback  the 4:3 fallback and its hysteresis
//   biosys-widetest rain      the continued rain
//   biosys-widetest compose   the centre of the wide capture equals the 4:3 capture, pixel for pixel
#include "engine/panowide.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace biosys;

namespace {

int failed = 0;
int checks = 0;
void check(const char* name, bool ok, const std::string& detail = "") {
    checks++;
    std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : " -- ", detail.c_str());
    if (!ok) failed++;
}
std::string fmt(const char* f, double a = 0, double b = 0, double c = 0) {
    char buf[200];
    std::snprintf(buf, sizeof buf, f, a, b, c);
    return buf;
}

PanoCamera camera(double yaw, double pitch, double fov) {
    const double f1 = 320.0 / std::tan(fov * 0.00872664625), f2 = 184.0 / std::tan(0.8984954979);
    return PanoCamera{yaw, pitch, fov, f1 > f2 ? f1 : f2};
}

void windowMode() {
    struct Case { int w, h, scale, extra; };
    const Case cases[] = {{1280, 960, 2, 0}, {1707, 960, 2, 106}, {1920, 1080, 2, 160}, {2560, 1080, 2, 320}, {3440, 1440, 3, 253},
                          {800, 600, 1, 80}, {1024, 768, 1, 192}, {639, 480, 1, 0}, {640, 480, 1, 0}, {1000, 1440, 1, 180}, {1300, 2000, 2, 5},
                          {3840, 2160, 4, 160}};
    for (const Case& c : cases) {
        const WideWindow w = wideWindow(c.w, c.h);
        check(fmt("wideWindow(%.0f x %.0f)", c.w, c.h).c_str(), w.scale == c.scale && w.extra == c.extra && w.logicalW == 640 + 2 * c.extra,
              fmt("scale %.0f extra %.0f logical %.0f", w.scale, w.extra, w.logicalW));
    }
    // property: SDL's integer-scale logical presentation of (logicalW x 480) in the window has the same scale as the 4:3 picture, so the centre
    // 640 columns are exactly as big as without --widescreen, and the 4:3 picture's position is centred (the black rest is less than a scale step)
    long bad = 0, total = 0;
    for (int h : {480, 600, 720, 768, 900, 960, 1080, 1200, 1440, 2160})
        for (int w = 640; w <= 5200; w++) {
            const WideWindow ww = wideWindow(w, h);
            const int sdlScale = std::min(w / ww.logicalW, h / Frame::H);
            const int base = std::min(w / Frame::W, h / Frame::H);
            total++;
            if (sdlScale != base || ww.scale != base || ww.logicalW > w || (ww.logicalW - 640) % 2) bad++;
        }
    check("the logical size of the wide canvas keeps the window's whole-number scale (10 heights x 4561 widths)", bad == 0, fmt("%.0f of %.0f differ", bad, total));
    // pointer: never leaves the centre
    bool ok = true;
    for (int extra : {0, 1, 106, 160, 320})
        for (int x = -50; x < 640 + 2 * extra + 50; x++) {
            const int f = widePointerFrameX(x, extra);
            if (f < 0 || f > 639) ok = false;
            if (x >= extra && x < extra + 640 && f != x - extra) ok = false;
            if (x < extra && f != 0) ok = false;
            if (x >= extra + 640 && f != 639) ok = false;
        }
    check("widePointerFrameX: inside the centre it is the frame x, outside it is clamped to column 0 / 639", ok);
    {
        const Rect clip{0, 56, 640, 424};
        const Rect r = wideClipToLogical(clip, 106);
        check("wideClipToLogical: the engine's pointer rectangle (0,56,640,424) is the centre picture in the wide canvas (106,56,640,424)",
              r.x == 106 && r.y == 56 && r.w == 640 && r.h == 424);
        const Rect q = wideClipToLogical(Rect{-30, 0, 800, 480}, 106);
        check("wideClipToLogical: a rectangle wider than the picture is cut to the centre (106,0,640,480)", q.x == 106 && q.w == 640 && q.y == 0 && q.h == 480);
        const Rect z = wideClipToLogical(Rect{100, 10, 50, 20}, 106);
        check("wideClipToLogical: an inner rectangle moves by the extra columns (206,10,50,20)", z.x == 206 && z.w == 50 && z.y == 10 && z.h == 20);
        // in window pixels at scale 2 the confined area is exactly [extra*2, (extra+640)*2)
        const WideWindow w = wideWindow(1707, 960);
        const Rect c = wideClipToLogical(Rect{0, 0, 640, 480}, w.extra);
        check("pointer confinement in window pixels: columns 212 .. 1491 of 1707 (the centre 640 * 2)", c.x * w.scale == 212 && (c.x + c.w) * w.scale == 1492);
    }
}

void fovMode() {
    // the wide field of view at the original focal length for common windows (frame columns beside the picture at scale 2: (W - 1280) / 4)
    struct Case { int w, h, scale; double fov; bool capped; };
    const Case cases[] = {{1707, 960, 2, 91.3, false}, {1920, 1080, 2, 98.0, false}, {3440, 1440, 3, 107.9, false}, {2560, 1440, 3, 91.2, false}, {2560, 1080, 2, 110.0, true}, {1280, 960, 2, 75.0, false}};
    for (const Case& c : cases) {
        const WideWindow w = wideWindow(c.w, c.h);
        Frame fr;
        std::vector<u8> bare(static_cast<size_t>(kViewW) * kViewH, 0);
        WideInput in;
        in.frame = &fr;
        in.view = Rect{0, 56, 640, 368};
        in.bare = bare.data();
        in.cam = camera(100, 0, 75);
        in.scale = w.scale;
        in.availLeft = in.availRight = w.extra * w.scale;
        in.quantum = w.scale;
        WideController ctl;
        const WideDecision d = ctl.decide(in);
        check(fmt("field of view in a %.0f x %.0f window", c.w, c.h).c_str(), std::fabs(d.fovDeg - c.fov) < 0.25 && d.capped == c.capped && d.fovDeg <= kWideMaxFovDeg + 1e-9,
              fmt("fov %.3f (want %.1f)", d.fovDeg, c.fov));
    }
    // the cap is exact: at any focal length the field of view drawn never exceeds it, and is the cap when the window is wider
    bool ok = true;
    double worst = 0;
    for (double fov = 30; fov <= 110; fov += 0.5)
        for (int avail : {0, 40, 213, 400, 800, 3000}) {
            Frame fr;
            std::vector<u8> bare(static_cast<size_t>(kViewW) * kViewH, 0);
            WideInput in;
            in.frame = &fr;
            in.view = Rect{0, 56, 640, 368};
            in.bare = bare.data();
            in.cam = camera(100, 0, fov);
            in.scale = 2;
            in.availLeft = in.availRight = avail;
            WideController ctl;
            const WideDecision d = ctl.decide(in);
            const double f = in.cam.focal;
            const double fovShown = 2.0 * std::atan((320.0 + (d.showLeft + d.showRight) / 4.0) / f) * 57.29577951308232;
            worst = std::max(worst, fovShown);
            if (d.showLeft > avail || d.showRight > avail) ok = false;
            if (fovShown > kWideMaxFovDeg + 1e-6 && d.wide) ok = false;
            if (avail >= 3000 && d.wide && fovShown < kWideMaxFovDeg - 0.2) ok = false;       // wide windows reach the cap
            if (!d.wide && avail > 0 && f * std::tan(kWideMaxFovDeg * 0.00872664625) > 321) ok = false;   // not wide although there is room
        }
    check("the field of view drawn never exceeds the cap (110 degrees) (fov 30..110 x 6 window sizes) and reaches it in wide windows", ok, fmt("worst %.4f", worst));
}

// the leftmost / rightmost page u (relative to the camera yaw) of the view at view x, over all rows (the extreme of the two edge rows)
double uExtreme(const PanoCamera& cam, double x, bool leftmost) {
    double best = leftmost ? 1e9 : -1e9;
    for (int y = 0; y <= kViewH; y += 1) {
        double u, v;
        panoMap(cam, x, y, u, v);
        const double r = u - cam.yaw;
        best = leftmost ? std::min(best, r) : std::max(best, r);
    }
    return best;
}

void limitsMode() {
    const WideLimits L{true, 781, 1271};      // the transit tubes T1 T6 T7 T12: scroll 469..959 + the yaw offset 312
    bool ok = true;
    double worstGap = 0;
    long cases = 0, tight = 0;
    for (double pitch : {-12.0, -2.35, 0.0, 6.0, 20.0})
        for (double fov : {75.0, 60.0, 90.0})
            for (double yaw = 700; yaw <= 1350; yaw += 7) {
                PanoCamera cam = camera(yaw, pitch, fov);
                const WideExtra e = wideExtra(cam, L);
                cases++;
                // original view at the limit yaws, same pitch and focal: the extreme u offsets of its edge columns
                const double origL = uExtreme(cam, 0, true), origR = uExtreme(cam, 640, false);
                const double allowedL = L.yawMin + origL, allowedR = L.yawMax + origR;
                const double dl = yaw - L.yawMin, dr = L.yawMax - yaw;
                if (dl < 0 || dr < 0) {   // outside the limits (the script's `scroll` bypasses them): no extra at all
                    if (e.limitLeft != 0 && dl < 0) ok = false;
                    if (e.limitRight != 0 && dr < 0) ok = false;
                    continue;
                }
                if (e.limitLeft < 1e9) {
                    const double got = yaw + uExtreme(cam, -e.limitLeft, true);
                    if (got < allowedL - 1e-6) ok = false;                 // never beyond
                    worstGap = std::max(worstGap, std::fabs(got - allowedL));
                    tight += std::fabs(got - allowedL) < 1e-5;
                }
                if (e.limitRight < 1e9) {
                    const double got = yaw + uExtreme(cam, 640 + e.limitRight, false);
                    if (got > allowedR + 1e-6) ok = false;
                    worstGap = std::max(worstGap, std::fabs(got - allowedR));
                    tight += std::fabs(got - allowedR) < 1e-5;
                }
            }
    check("limits: the leftmost / rightmost page position of the wide view never passes (limit + the original view's own extent), and is tight",
          ok && tight > 0 && worstGap < 1e-5, fmt("%.0f cases, %.0f tight, worst |gap| %.2g page px", cases, tight, worstGap));
    // at a limit the picture is the 4:3 picture, in the middle of the range it is the full wide picture
    Frame fr;
    std::vector<u8> bare(static_cast<size_t>(kViewW) * kViewH, 0);
    auto showAt = [&](double yaw) {
        WideInput in;
        in.frame = &fr;
        in.view = Rect{0, 56, 640, 368};
        in.bare = bare.data();
        in.cam = camera(yaw, 0, 75);
        in.limits = L;
        in.scale = 2;
        in.availLeft = in.availRight = 213;
        WideController c;
        return c.decide(in);
    };
    const WideDecision a = showAt(781), b = showAt(1026), c = showAt(1271), d = showAt(800);
    check("limits: at the left limit 0 extra columns, at the right limit 0, in the middle all 213; limited flag set only where it narrows",
          a.showLeft == 0 && a.showRight == 0 && !a.wide && a.limited && c.showLeft == 0 && c.showRight == 0 && b.showLeft == 213 && b.showRight == 213 && !b.limited &&
              d.showLeft > 0 && d.showLeft < 213 && d.limited,
          fmt("at 800: %.0f", d.showLeft));
    // the narrowing is symmetric and continuous: no jump of more than 3 output columns per page pixel of turn
    double prev = showAt(781).showLeft;
    bool sm = true;
    for (double y = 782; y <= 1271; y += 1) {
        const WideDecision q = showAt(y);
        if (q.showLeft != q.showRight || std::fabs(q.showLeft - prev) > 6) sm = false;
        prev = q.showLeft;
    }
    check("limits: the extent is the same left and right and changes by at most 6 output columns per page pixel (the edge columns show 2.4 view pixels per page pixel)", sm);
    // no limits: nothing narrows
    WideInput in;
    in.frame = &fr;
    in.view = Rect{0, 56, 640, 368};
    in.bare = bare.data();
    in.cam = camera(781, 0, 75);
    in.scale = 2;
    in.availLeft = in.availRight = 213;
    WideController ctl;
    check("no limits: the full extent everywhere", ctl.decide(in).showLeft == 213);
}

void fallbackMode() {
    Frame fr;
    std::vector<u8> bare(static_cast<size_t>(kViewW) * kViewH, 0);
    const Rect view{0, 56, 640, 368};
    auto setCover = [&](double frac) {   // columns 0..15: the first `frac` of the 16 * 368 strip pixels differ from the bare projection
        std::fill(fr.px.begin(), fr.px.end(), 0);
        const long n = static_cast<long>(frac * 16 * 368 + 0.5);
        long k = 0;
        for (int y = 0; y < 368 && k < n; y++)
            for (int x = 0; x < 16 && k < n; x++, k++) fr.px[static_cast<size_t>(56 + y) * 640 + x] = 7;
    };
    WideController ctl;
    auto step = [&](double cover) {
        setCover(cover);
        WideInput in;
        in.frame = &fr;
        in.view = view;
        in.bare = bare.data();
        in.cam = camera(100, 0, 75);
        in.scale = 2;
        in.availLeft = in.availRight = 213;
        return ctl.decide(in);
    };
    std::string seq;
    auto run = [&](double cover) { const WideDecision d = step(cover); seq += d.wide ? 'W' : '-'; return d; };
    // a status window / goggles frame (cover 1.0) comes and goes
    for (int i = 0; i < 3; i++) run(0);
    for (int i = 0; i < 5; i++) run(1.0);
    for (int i = 0; i < 12; i++) run(0);
    check("fallback: wide 3 frames, 4:3 at once with an overlay (5 frames), then 4:3 for 7 clear frames, wide again at the 8th (no flicker)", seq == "WWW------------WWWWW", seq);
    // an overlay that flickers around the thresholds does not flicker the picture
    ctl.reset();
    seq.clear();
    for (int i = 0; i < 4; i++) run(0);
    for (int i = 0; i < 20; i++) run(i % 2 ? 0.20 : 0.05);          // between the two thresholds: no change from wide
    check("hysteresis: cover 0.05 / 0.20 alternating (below the enter threshold 0.30) stays wide", seq.find('-') == std::string::npos, seq);
    seq.clear();
    run(0.35);
    for (int i = 0; i < 20; i++) run(i % 2 ? 0.2 : 0.05);          // once in the fallback, cover that does not stay below 0.10 keeps it
    check("hysteresis: once in the fallback, cover 0.05 / 0.20 alternating (never 8 frames below 0.10) stays 4:3", seq.find('W') == std::string::npos, seq);
    seq.clear();
    run(0.35);
    for (int i = 0; i < 7; i++) run(0.0);
    run(0.2);                                                         // the 8-frame run is broken
    for (int i = 0; i < 7; i++) run(0.0);
    check("hysteresis: 7 clear frames, one in between, 7 clear frames: still 4:3", seq.find('W') == std::string::npos, seq);
    run(0.0);
    check("hysteresis: the 8th clear frame in a row returns to wide", seq.back() == 'W', seq);
    // thresholds on the exact numbers: 29 percent does not trigger, 30 percent does
    ctl.reset();
    check("threshold: cover 0.29 stays wide", step(0.29).wide);
    ctl.reset();
    check("threshold: cover 0.31 falls back", !step(0.31).wide && ctl.fallback());
    // the right edge counts too
    ctl.reset();
    std::fill(fr.px.begin(), fr.px.end(), 0);
    for (int y = 0; y < 368; y++)
        for (int x = 640 - 16; x < 640; x++) fr.px[static_cast<size_t>(56 + y) * 640 + x] = 7;
    WideInput in;
    in.frame = &fr;
    in.view = view;
    in.bare = bare.data();
    in.cam = camera(100, 0, 75);
    in.scale = 2;
    in.availLeft = in.availRight = 213;
    check("the right edge counts as well", !ctl.decide(in).wide);
    // rain pixels do not count: the cover is measured on the render without the page filter
    ctl.reset();
    Frame noFilter;
    in.noFilter = &noFilter;
    check("rain does not count: the cover is measured on the render without the page filter (edge pixels that only the filter drew do not trigger)", ctl.decide(in).wide);
}

void rainMode() {
    // a centre pattern like the game's: thin drops at the columns 10..637 (about every third), at various rows
    std::vector<int16_t> rain(static_cast<size_t>(kViewW) * kViewH, -1);
    unsigned seed = 12345;
    auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
    int drops = 0;
    for (int x = 10; x <= 637; x += 1 + static_cast<int>(rnd() % 5)) {
        const int y0 = static_cast<int>(rnd() % 360);
        for (int k = 0; k < 5; k++) rain[static_cast<size_t>((y0 + k) % kViewH) * kViewW + x] = 118;
        drops++;
    }
    auto colCount = [&](int x) { int n = 0; for (int y = 0; y < kViewH; y++) n += wideSideRain(rain, x, y) >= 0; return n; };
    auto centreCount = [&](int x) { int n = 0; for (int y = 0; y < kViewH; y++) n += rain[static_cast<size_t>(y) * kViewW + x] >= 0; return n; };
    long cl = 0, cr = 0, cc = 0;
    for (int x = 1; x <= 177; x++) { cl += colCount(-x); cr += colCount(640 + x - 1); }
    for (int x = 10; x < 640; x++) cc += centreCount(x);
    const double perCol = static_cast<double>(cc) / 630.0;
    check("rain: the side areas have the density of the centre (177 columns each)", std::fabs(cl / 177.0 - perCol) < 0.2 * perCol && std::fabs(cr / 177.0 - perCol) < 0.2 * perCol,
          fmt("centre %.2f, left %.2f, right %.2f per column", perCol, cl / 177.0, cr / 177.0));
    // no gap at the boundary: the longest run of drop-free columns in the 40 columns on each side of the seam is not longer than in the interior
    auto longestGap = [&](int from, int to, bool side) {
        int best = 0, run = 0;
        for (int x = from; x < to; x++) {
            const int n = (x < 0 || x >= 640) ? colCount(x) : centreCount(x);
            if (n == 0) best = std::max(best, ++run); else run = 0;
            (void)side;
        }
        return best;
    };
    const int interior = longestGap(20, 600, false);
    check("rain: no empty strip at the right boundary (columns 600..680)", longestGap(600, 680, true) <= interior + 1, fmt("%.0f vs interior %.0f", longestGap(600, 680, true), interior));
    check("rain: no empty strip at the left boundary (columns -60..30)", longestGap(-60, 30, true) <= std::max(interior, 11) + 1, fmt("%.0f vs interior %.0f", longestGap(-60, 30, true), interior));
    // the two sides are not mirror images of each other, and a side is not a copy of the centre at the same rows
    long same = 0, tot = 0, copy = 0;
    for (int i = 0; i < 150; i++)
        for (int y = 0; y < kViewH; y++) {
            const bool a = wideSideRain(rain, -1 - i, y) >= 0, b = wideSideRain(rain, 640 + i, y) >= 0;
            const bool c = rain[static_cast<size_t>(y) * kViewW + 20 + i] >= 0;
            if (a || b) { tot++; same += a && b; }
            if (a) copy += c;
        }
    check("rain: the left and right side areas do not repeat each other", same < tot / 20, fmt("%.0f of %.0f", same, tot));
    check("rain: the side is not the centre pattern at the same rows", copy < 30, fmt("%.0f", copy));
    // far beyond the interior (super wide windows) the pattern keeps going
    bool far = true;
    long n = 0;
    for (int x = 640; x < 2400; x++) n += colCount(x);
    check("rain: 1760 columns right of the view (a 32:9 window) still have rain", n > 0 && far);
    // block = the same values
    std::vector<int16_t> blk;
    wideSideRainBlock(rain, true, 100, blk);
    bool eq = true;
    for (int y = 0; y < kViewH; y += 17)
        for (int i = 0; i < 100; i++) eq = eq && blk[static_cast<size_t>(y) * 100 + i] == wideSideRain(rain, 640 + i, y);
    wideSideRainBlock(rain, false, 100, blk);
    for (int y = 0; y < kViewH; y += 17)
        for (int i = 0; i < 100; i++) eq = eq && blk[static_cast<size_t>(y) * 100 + i] == wideSideRain(rain, -100 + i, y);
    check("rain: wideSideRainBlock is wideSideRain for the columns of the side", eq);
    (void)drops;
}

void composeMode() {
    Surface page;
    page.init(kPanoW, kPanoH, 0);
    unsigned seed = 99;
    for (u8& v : page.px) { seed = seed * 1664525u + 1013904223u; v = static_cast<u8>(seed >> 24); }
    Palette pal;
    for (int i = 0; i < 256; i++) pal.c[i] = Color{static_cast<u8>(i), static_cast<u8>((i * 7) & 255), static_cast<u8>(255 - i)};
    std::vector<u32> tex;
    panoTextureAll(page, pal, tex);
    Frame fr;
    std::vector<u8> bare(static_cast<size_t>(kViewW) * kViewH, 0);
    for (size_t i = 0; i < bare.size(); i++) bare[i] = static_cast<u8>(i % 5);
    const Rect view{0, 56, 640, 368};
    for (int y = 0; y < 368; y++)
        for (int x = 0; x < 640; x++) fr.px[static_cast<size_t>(56 + y) * 640 + x] = (x > 300 && x < 340) ? 77 : bare[static_cast<size_t>(y) * 640 + x];   // an overlay stripe
    long diffs = 0, centreCells = 0, sideUnc = 0, sideZero = 0;
    for (int scale : {1, 2, 3})
        for (int extraOut : {0, 1, 7, 100, 213, 400}) {
            for (double yaw : {-1270.0, 10.0, 1279.0}) {
                const PanoCamera cam = camera(yaw, -3.0, 75);
                std::vector<u32> plain, wide;
                composeSmooth(fr, pal, view, bare.data(), tex, cam, scale, true, plain);
                WideCanvas cv;
                const int total = 640 * scale + extraOut;
                cv.outW = total;
                cv.left = extraOut / 2;
                cv.showLeft = cv.left;
                cv.showRight = extraOut - cv.left;
                sideUnc += composeSmoothWide(fr, pal, view, bare.data(), tex, cam, scale, true, cv, nullptr, wide);
                for (int Y = 0; Y < 480 * scale; Y++)
                    for (int X = 0; X < 640 * scale; X++) {
                        centreCells++;
                        diffs += plain[static_cast<size_t>(Y) * 640 * scale + X] != wide[static_cast<size_t>(Y) * total + cv.left + X];
                    }
                for (int Y = 56 * scale; Y < (56 + 368) * scale; Y++) {
                    for (int X = 0; X < cv.left; X++) sideZero += wide[static_cast<size_t>(Y) * total + X] == 0;
                    for (int X = cv.left + 640 * scale; X < total; X++) sideZero += wide[static_cast<size_t>(Y) * total + X] == 0;
                }
                for (int Y = 0; Y < 56 * scale; Y++)       // the top band's sides stay black
                    for (int X = 0; X < cv.left; X++) diffs += wide[static_cast<size_t>(Y) * total + X] != 0;
            }
        }
    check("composeSmoothWide: the centre 640 * scale columns equal composeSmooth pixel for pixel (3 scales x 6 widths x 3 yaws incl. the seam)", diffs == 0,
          fmt("%.0f of %.0f differ", diffs, centreCells));
    check("composeSmoothWide: every side pixel of the view is painted by a triangle (none left over)", sideUnc == 0, fmt("%.0f", sideUnc));
    check("composeSmoothWide: the side areas of the view rows are not black (random page: all painted)", sideZero < 2000, fmt("%.0f black", sideZero));
}

} // namespace

int main(int argc, char** argv) {
    const std::string m = argc > 1 ? argv[1] : "";
    if (m == "window") windowMode();
    else if (m == "fov") fovMode();
    else if (m == "limits") limitsMode();
    else if (m == "fallback") fallbackMode();
    else if (m == "rain") rainMode();
    else if (m == "compose") composeMode();
    else { std::fprintf(stderr, "usage: biosys-widetest window|fov|limits|fallback|rain|compose\n"); return 2; }
    std::printf(failed ? "FAILED %d of %d\n" : "ALL OK (%d checks)\n", failed ? failed : checks, checks);
    return failed ? 1 : 0;
}
