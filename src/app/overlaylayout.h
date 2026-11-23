// The layout arithmetic and the pointer mappings of the overlay interface (--interface overlay). SDL-free, header only; unit-tested by
// build/biosys-overlaytest (src/tools/overlaytest.cpp) and used by the window (main.cpp), the headless composition (overlaycompose.*) and
// the script commands (overlay_script.cpp).
//
// The original's 640 x 480 screen is: top bar rows 0..55, the view rows 56..423 (640 x 368), the inventory bar rows 424..479. Here the view
// fills the window and the two bars are overlays that exist only while they have something to show:
//   * a page that has the 368-row view (the engine's `viewFrame()`): the view is scaled by k = min(H / 368, W / 640) (not a whole number)
//     and centred. Where that leaves empty sides, the window draws an ambient fill; where it leaves black above and below (a 4:3 window:
//     k = W / 640 < H / 368), the overlays are anchored to the window's top and bottom edges and so look like the original layout.
//   * the overlays use the interface scale u = max(1, round(0.75 * H / 480)), a whole number: status words at the top (the original's x
//     positions on a 640 grid centred in the window), the message line above the place of the strip, the inventory strip at the bottom.
//   * every other frame (menus, title, terminal, frozen frames, films, full-screen pages) is drawn as always.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace biosys {
namespace ovl {

constexpr int kGridW = 640, kGridH = 480;     // the original's screen
constexpr int kTopH = 56, kViewRows = 368;    // top bar rows 0..55, view rows 56..423
constexpr int kBarY = 424, kBarH = 56;        // inventory bar rows 424..479
constexpr int kStatusWordY = 0x12, kMessageY = 0x24;   // the rows the game draws status words / the message line at (src/game/bg_display.cpp)
constexpr int kBarCentreY = kBarY + kBarH / 2;          // 452: where the engine holds the pointer in bar mode
constexpr double kDockPx = 2.0;                         // the pointer this close to the bottom edge of the window (render pixels) is in the dock

constexpr double kStripGradientHeights = 1.4;     // the dark gradient behind the strip rises 1.4 strip heights
constexpr double kStripGradientAlpha = 0.6;       // ... to this opacity at the bottom edge
constexpr double kAmbientBrightness = 0.35;       // the ambient fill's brightness, away from the picture
constexpr int kAmbientWidth = 64;                 // the picture is reduced to this many columns before it is enlarged again
constexpr double kMessageFadeInMs = 120, kMessageFadeOutMs = 250;
constexpr double kStripFadeInMs = 120, kStripFadeOutMs = 300;

struct Layout {
    int W = 640, H = 480;
    double k = 1;                                  // window pixels per view pixel
    double viewX = 0, viewY = 56;                  // the view's top-left corner in the window
    double viewW = 640, viewH = 368;
    int u = 1;                                     // the interface scale (a whole number)
    int stripX = 0, stripY = 424, stripW = 640, stripH = 56;    // the inventory strip, window pixels
    bool fourThree = false;                        // k = W / 640 < H / 368: black above and below, overlays at the window's edges
    int extraCols = 0;                             // whole frame columns beside the 640 that fit in the window at scale k, each side
    bool whole() const { return std::fabs(k - std::floor(k + 0.5)) < 1e-9; }     // k is a whole number: the picture is enlarged without filtering
    double centreX() const { return W * 0.5; }
};

inline Layout layoutFor(int W, int H) {
    Layout L;
    L.W = std::max(1, W);
    L.H = std::max(1, H);
    const double kh = static_cast<double>(L.H) / kViewRows, kw = static_cast<double>(L.W) / kGridW;
    L.k = std::min(kh, kw);
    L.fourThree = kw < kh;
    L.viewW = kGridW * L.k;
    L.viewH = kViewRows * L.k;
    L.viewX = (L.W - L.viewW) * 0.5;
    L.viewY = (L.H - L.viewH) * 0.5;
    L.u = std::max(1, static_cast<int>(std::lround(0.75 * L.H / kGridH)));
    L.stripW = kGridW * L.u;
    L.stripH = kBarH * L.u;
    L.stripX = (L.W - L.stripW) / 2;
    L.stripY = L.H - L.stripH;
    L.extraCols = std::max(0, static_cast<int>(std::floor((L.viewX + 1e-9) / L.k)));
    return L;
}

// ------------------------------------------------------------------------------------------------------------------ pointer mappings

// The engine's pointer position (640 x 480 grid) -> where it is shown in the window (render pixels): view rows through the view mapping,
// the bottom rows through the bottom strip, the top rows through the top.
inline void engineToWindow(const Layout& L, double px, double py, double& wx, double& wy) {
    if (py >= kBarY) {
        wy = L.H - (kGridH - py) * L.u;
        wx = L.centreX() + (px - kGridW / 2) * L.u;
    } else if (py < kTopH) {
        wy = py * L.u;
        wx = L.centreX() + (px - kGridW / 2) * L.u;
    } else {
        wx = L.viewX + px * L.k;
        wy = L.viewY + (py - kTopH) * L.k;
    }
}

struct EnginePoint {
    double x = 0, y = 0;
    bool bar = false;        // the point is in the bar rows (the dock: the engine enters bar mode by itself)
};

// A free pointer (Ctrl held, or a page with nothing to scroll): window position -> engine position. The view maps linearly to the window and
// the pointer is confined to the view rows; pushing it against the bottom edge of the window (the last kDockPx pixel rows) is the dock: it enters the bar
// rows. (The top bar has no hotspots, so the top edge has no dock.)
inline EnginePoint windowToEngine(const Layout& L, double wx, double wy) {
    EnginePoint p;
    if (wy >= L.H - kDockPx) {
        p.bar = true;
        p.x = std::min(std::max(kGridW / 2 + (wx - L.centreX()) / L.u, 0.0), kGridW - 1e-6);
        p.y = kBarCentreY;
        return p;
    }
    p.x = std::min(std::max((wx - L.viewX) / L.k, 0.0), kGridW - 1e-6);
    p.y = kTopH + std::min(std::max((wy - L.viewY) / L.k, 0.0), kViewRows - 1e-6);
    return p;
}

// The window rectangle the free pointer may move in, for the OS: the whole window (the dock needs the last row).
struct PixelRect { int x = 0, y = 0, w = 0, h = 0; };
inline PixelRect freePointerRect(const Layout& L) { return PixelRect{0, 0, L.W, L.H}; }

// Motion deltas of the window (render pixels) -> engine deltas: whole engine pixels, the rest carried to the next motion. The captured pointer
// is warped back to the centre after every frame, so without the carry the slow movements (less than k window pixels) would be lost.
// The vertical release (Engine::verticalRelease) uses the same: its pointer row is the previous row plus the delta, not an inverse mapping.
class MotionCarry {
public:
    int apply(double windowDelta, double scale, int axis) {
        double& r = rem_[axis];
        const double v = r + windowDelta / std::max(scale, 1e-6);
        const double whole = std::trunc(v + (v < 0 ? -1e-9 : 1e-9));      // (the bias keeps n * scale / scale from truncating to n - 1 by rounding noise)
        r = v - whole;
        return static_cast<int>(whole);
    }
    void reset() { rem_[0] = rem_[1] = 0; }
private:
    double rem_[2] = {0, 0};
};

// ------------------------------------------------------------------------------------------------------------------ fades

// A value between 0 and 1 that moves linearly to its target: up in inMs, down in outMs.
struct Fader {
    double v = 0;
    void step(bool target, double dtMs, double inMs, double outMs) {
        if (dtMs < 0) dtMs = 0;
        if (target) v = std::min(1.0, v + dtMs / std::max(inMs, 1.0));
        else v = std::max(0.0, v - dtMs / std::max(outMs, 1.0));
    }
};

// The wish to see the inventory strip: bar mode, the pick-up flash, or the pointer in the bar rows.
inline bool stripWanted(bool barMode, bool flashing, bool pointerInBarRows) { return barMode || flashing || pointerInBarRows; }

// The ambient fill's brightness at `d` window pixels beside the picture (g = the width of the soft gradient where it meets the picture):
// the picture's own edge colour at the seam (1.0), kAmbientBrightness from g on, linear in between (the window draws it as a gradient quad).
inline double ambientBrightness(double d, double g) {
    if (d <= 0) return 1.0;
    if (d >= g) return kAmbientBrightness;
    return kAmbientBrightness + (1.0 - kAmbientBrightness) * (1.0 - d / g);
}

} // namespace ovl
} // namespace biosys
