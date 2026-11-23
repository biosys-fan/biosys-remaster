// Unit tests of the overlay interface's arithmetic (src/app/overlaylayout.h) and of the ambient fill / text sprite helpers (src/app/overlaycompose.*, the
// SDL-free part). No game data needed.
//   biosys-overlaytest layout    k, u, the view and the strip at 3440 x 1440, 1920 x 1080, 1728 x 1084, 1280 x 960 and more
//   biosys-overlaytest pointer   the pointer maps: round trips, the centre, the bar, the clamp, the dock edge rule
//   biosys-overlaytest motion    the motion carry (deltas divided by the scale, the rest carried)
//   biosys-overlaytest fade      the faders, the strip's wish, the ambient brightness curve
#include "app/overlaylayout.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace biosys;

namespace {

int failed = 0, checks = 0;
void check(const std::string& name, bool ok, const std::string& detail = "") {
    checks++;
    std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : " -- ", detail.c_str());
    if (!ok) failed++;
}
std::string fmt(const char* f, double a = 0, double b = 0, double c = 0, double d = 0) {
    char buf[300];
    std::snprintf(buf, sizeof buf, f, a, b, c, d);
    return buf;
}
bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

struct Case { int w, h; double k; int u; bool fourThree; int extra; };
const Case kCases[] = {
    {3440, 1440, 1440.0 / 368, 2, false, 119},      // 21:9: the height decides, 119 frame columns fit beside the 640
    {1920, 1080, 1080.0 / 368, 2, false, 7},        // 16:9
    {1728, 1084, 2.7, 2, true, 0},                  // narrower than the view's 640:368 shape: the width decides, black above and below
    {1280, 960, 2.0, 2, true, 0},                   // 4:3
    {640, 480, 1.0, 1, true, 0},
    {2560, 1440, 1440.0 / 368, 2, false, 7},
    {1000, 368, 368.0 / 368, 1, false, 180},
};

void layout() {
    for (const Case& c : kCases) {
        const ovl::Layout L = ovl::layoutFor(c.w, c.h);
        const std::string tag = fmt("%g x %g", c.w, c.h);
        check(tag + ": k = min(H / 368, W / 640)", near(L.k, std::min(c.h / 368.0, c.w / 640.0)) && near(L.k, c.k, 1e-9), fmt("k=%.6f", L.k));
        check(tag + ": u = max(1, round(0.75 H / 480)) = " + std::to_string(c.u), L.u == c.u, fmt("u=%g", L.u));
        check(tag + ": the view is centred and 640 k x 368 k", near(L.viewW, 640 * L.k) && near(L.viewH, 368 * L.k) && near(L.viewX * 2 + L.viewW, c.w) && near(L.viewY * 2 + L.viewH, c.h),
              fmt("view=%.3f,%.3f,%.3f,%.3f", L.viewX, L.viewY, L.viewW, L.viewH));
        check(tag + ": four-three layout (black above and below) = " + std::to_string(c.fourThree), L.fourThree == c.fourThree);
        check(tag + ": the view touches the window's sides in the 4:3 layout, its top and bottom otherwise",
              c.fourThree ? (near(L.viewX, 0) && L.viewY >= 0) : (near(L.viewY, 0) && L.viewX >= 0));
        check(tag + ": the strip is 640 u x 56 u at the bottom, centred", L.stripW == 640 * L.u && L.stripH == 56 * L.u && L.stripY + L.stripH == c.h && L.stripX == (c.w - L.stripW) / 2,
              fmt("strip=%g,%g", L.stripX, L.stripY));
        check(tag + ": whole frame columns beside the 640 that fit: " + std::to_string(c.extra), L.extraCols == c.extra, fmt("extra=%g", L.extraCols));
        check(tag + ": u is a whole number of pixels; the strip fits when the window is as wide as 640 u", c.w < 640 * L.u || L.stripX >= 0);
    }
    check("k is whole at 1280 x 736 (2) and not at 1920 x 1080", ovl::layoutFor(1280, 736).whole() && !ovl::layoutFor(1920, 1080).whole());
}

void pointer() {
    for (const Case& c : kCases) {
        const ovl::Layout L = ovl::layoutFor(c.w, c.h);
        const std::string tag = fmt("%g x %g", c.w, c.h);
        double wx, wy;
        ovl::engineToWindow(L, 320, 240, wx, wy);
        check(tag + ": the held pointer (320, 240) is at the window centre", near(wx, c.w / 2.0) && near(wy, c.h / 2.0), fmt("%.3f,%.3f", wx, wy));
        ovl::engineToWindow(L, 320, 452, wx, wy);
        check(tag + ": the pointer of bar mode (320, 452) is at the centre of the strip", near(wx, c.w / 2.0) && near(wy, c.h - 28.0 * L.u), fmt("%.3f,%.3f", wx, wy));
        ovl::engineToWindow(L, 100, 10, wx, wy);
        check(tag + ": rows above 56 go through the top: y = py u, x = W/2 + (px - 320) u", near(wx, c.w / 2.0 + (100 - 320) * L.u) && near(wy, 10.0 * L.u));
        ovl::engineToWindow(L, 100, 430, wx, wy);
        check(tag + ": rows from 424 go through the bottom strip: y = H - (480 - py) u", near(wx, c.w / 2.0 + (100 - 320) * L.u) && near(wy, c.h - 50.0 * L.u));
        // round trip of every view pixel centre (a sample of them)
        int bad = 0, n = 0;
        for (int py = 56; py < 424; py += 7)
            for (int px = 0; px < 640; px += 9) {
                ovl::engineToWindow(L, px + 0.5, py + 0.5, wx, wy);
                const ovl::EnginePoint p = ovl::windowToEngine(L, wx, wy);
                n++;
                if (p.bar || static_cast<int>(p.x) != px || static_cast<int>(p.y) != py) bad++;
            }
        check(tag + ": window -> engine undoes engine -> window for the view's pixels (" + std::to_string(n) + " points)", bad == 0, std::to_string(bad) + " wrong");
        // the clamp: confined to the view
        ovl::EnginePoint p = ovl::windowToEngine(L, -50, -50);
        check(tag + ": a free pointer above and left of the window is held at the view's corner", p.x == 0 && near(p.y, 56) && !p.bar);
        p = ovl::windowToEngine(L, c.w + 50.0, c.h * 0.5);
        check(tag + ": ... beyond the right edge at the view's last column", static_cast<int>(p.x) == 639 && !p.bar);
        p = ovl::windowToEngine(L, c.w * 0.5, c.h - ovl::kDockPx - 0.01);
        check(tag + ": just above the dock the pointer is in the view (last row 423)", !p.bar && static_cast<int>(p.y) <= 423, fmt("y=%.3f", p.y));
        p = ovl::windowToEngine(L, c.w * 0.5, c.h - ovl::kDockPx);
        check(tag + ": at the dock (the last pixel rows) it enters the bar rows, x through the strip", p.bar && static_cast<int>(p.y) >= 424 && static_cast<int>(p.y) <= 479 && near(p.x, 320, 1.0), fmt("%.3f,%.3f", p.x, p.y));
        p = ovl::windowToEngine(L, c.w * 0.5 + 10.0 * L.u, c.h - 0.5);
        check(tag + ": the dock's x is the strip's: 10 u right of the centre = 330", p.bar && near(p.x, 330, 1e-6));
        p = ovl::windowToEngine(L, c.w * 0.5, 0.0);
        check(tag + ": no dock at the top edge (the top bar has no hotspots): view row 56", !p.bar && near(p.y, 56));
    }
    // a free pointer in the 4:3 layout: the black above and below the view holds it at the view's edge rows
    const ovl::Layout L43 = ovl::layoutFor(1280, 960);
    ovl::EnginePoint p = ovl::windowToEngine(L43, 640, 20);
    check("1280 x 960: the black above the view holds the free pointer at row 56", near(p.y, 56) && !p.bar);
    p = ovl::windowToEngine(L43, 640, 960 - 30);
    check("1280 x 960: the black below the view holds it at row 423", !p.bar && static_cast<int>(p.y) == 423);
    const ovl::PixelRect r = ovl::freePointerRect(L43);
    check("the free pointer may use the whole window (the dock needs the last row)", r.x == 0 && r.y == 0 && r.w == 1280 && r.h == 960);
}

void motion() {
    ovl::MotionCarry m;
    int sum = 0;
    for (int i = 0; i < 10; i++) sum += m.apply(1.0, 3.913043, 0);      // ten window pixels at k = 3.91 = 2.56 engine pixels
    check("ten 1-pixel moves at k = 3.913 give 2 engine pixels, 0.56 carried", sum == 2, "sum=" + std::to_string(sum));
    sum += m.apply(2.0, 3.913043, 0);
    check("the carry completes a pixel later (the 11th and 12th window pixel)", sum == 3, "sum=" + std::to_string(sum));
    ovl::MotionCarry n;
    int a = 0;
    for (int i = 0; i < 7; i++) a += n.apply(-1.0, 3.0, 1);
    check("negative motion truncates toward zero and carries (7 x -1 / 3 = -2, -1/3 left)", a == -2, "a=" + std::to_string(a));
    ovl::MotionCarry z;
    check("the two axes are separate", z.apply(5, 2, 0) == 2 && z.apply(5, 2, 1) == 2 && z.apply(1, 2, 0) == 1 && z.apply(1, 2, 1) == 1);
    z.reset();
    check("reset drops the remainder", z.apply(1, 4, 0) == 0);
    ovl::MotionCarry w;
    int s2 = 0;
    for (int i = 0; i < 1000; i++) s2 += w.apply(0.37, 1.7, 0);
    check("no motion is lost over a long drag: 370 window pixels / 1.7 = 217.6", s2 == 217, "s2=" + std::to_string(s2));
}

void fade() {
    ovl::Fader f;
    f.step(true, 60, ovl::kMessageFadeInMs, ovl::kMessageFadeOutMs);
    check("a message fades in over 120 ms: half at 60 ms", near(f.v, 0.5));
    f.step(true, 200, ovl::kMessageFadeInMs, ovl::kMessageFadeOutMs);
    check("... and stays at 1", near(f.v, 1.0));
    f.step(false, 125, ovl::kMessageFadeInMs, ovl::kMessageFadeOutMs);
    check("it fades out over 250 ms: half at 125 ms", near(f.v, 0.5));
    f.step(false, 500, ovl::kMessageFadeInMs, ovl::kMessageFadeOutMs);
    check("... and is gone", near(f.v, 0.0));
    check("the strip is wanted in bar mode, during the flash, with the pointer in the bar rows, else not",
          ovl::stripWanted(true, false, false) && ovl::stripWanted(false, true, false) && ovl::stripWanted(false, false, true) && !ovl::stripWanted(false, false, false));
    check("ambient brightness: 1.0 at the picture, 0.35 from the gradient's width on, linear between",
          near(ovl::ambientBrightness(0, 48), 1.0) && near(ovl::ambientBrightness(48, 48), 0.35) && near(ovl::ambientBrightness(500, 48), 0.35) && near(ovl::ambientBrightness(24, 48), 0.675));
    check("the strip's gradient: 1.4 strip heights, 0.6 opaque at the bottom edge", near(ovl::kStripGradientHeights, 1.4) && near(ovl::kStripGradientAlpha, 0.6));
}

} // namespace

int main(int argc, char** argv) {
    const std::string what = argc > 1 ? argv[1] : "all";
    if (what == "layout" || what == "all") layout();
    if (what == "pointer" || what == "all") pointer();
    if (what == "motion" || what == "all") motion();
    if (what == "fade" || what == "all") fade();
    std::printf("overlaytest: %d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
