// "Widescreen" presentation of the projected panoramas (optional, off by default; src/app/README.md): the mathematics and the decisions,
// with no SDL. On a window wider than the 4:3 picture the panorama view is drawn across the full width at the same vertical extent and the
// same focal length, so the extra columns left and right show more of the cylinder; everything else stays 4:3 (centred, black bars).
//
// Geometry: the 640 x 480 frame keeps its whole-number scale s. The 640 * s centre columns of the view are pixel for pixel the smooth
// picture of the 4:3 path (same mesh cells, same rasteriser, panomesh.h buildPanoMeshRange / rasterizePanoRegion); the extra columns are
// the same projection continued, i.e. the horizontal field of view grows (a 16:9 window: 75 degrees -> about 91). The top band and the
// inventory band stay 640 * s wide and centred; the side areas of those rows are black.
#pragma once
#include "engine/panomesh.h"
#include <string>
#include <vector>

namespace biosys {

// ---------------------------------------------------------------------------------------------------------------------------------
// limits (decisions of this file, not of the original)

// The widest horizontal field of view drawn. 110 degrees: the plane-to-cylinder projection stretches the page by 1 / cos^2(55) = 3.0
// at the edge columns there (2.9 at 108 degrees, 2.0 at 91, 1.6 at the original 75), so the edges are soft, but a 21:9 screen (3440 x
// 1440: 108 degrees) is filled from side to side. The first cap was 100 degrees, which left black strips at the sides of a 21:9 screen
// on top of the black of the game's own bars above and below: black on all four sides, which the player rejected. Wider screens
// (32:9) still get black at the far sides.
constexpr double kWideMaxFovDeg = 110.0;

// Fall back to the 4:3 picture when more than this fraction of the outermost kWideEdgeCols columns of the view, at either edge, is covered
// by overlay pixels that are not rain (goggles worn, the opaque parts of status windows, a full-view sprite): such an overlay exists for the
// centre 640 columns only and cannot be extended. 30 percent: a held object or a fixed sprite of the game covers well under 10 percent of
// such a strip (measured in tests/test_widescreen.sh), the goggles frame and the status windows over 60 percent.
constexpr int kWideEdgeCols = 16;
constexpr double kWideEnterCover = 0.30;
// Back to the wide picture only after the cover has stayed below kWideExitCover for kWideExitFrames frames in a row (no flicker when
// an overlay comes and goes at the edge of the threshold; leaving the wide picture is immediate, the overlay is in that very frame).
constexpr double kWideExitCover = 0.10;
constexpr int kWideExitFrames = 8;

// The status screen (Engine::simStatusOpaque) fills the centre 640 columns; the wide panorama beside it stays, dimmed to this brightness (a black
// layer of alpha 1 - kWideStatusSideBrightness over the side columns of the view rows) so that the status panel is the focus.
constexpr double kWideStatusSideBrightness = 0.4;

// ---------------------------------------------------------------------------------------------------------------------------------
// the window

// Where the wide picture goes in a window of outW x outH pixels: the 4:3 picture keeps the whole-number scale of the window's height (and of
// its width when that is smaller), `extra` whole frame columns fit beside it on each side (the window shows 640 + 2 * extra frame columns;
// the window draws them with a logical size of logicalW x 480, so that the scale stays the same).
struct WideWindow {
    int scale = 1, extra = 0, logicalW = 640;
};
WideWindow wideWindow(int outW, int outH);

// The pointer is for the centre only (the engine's hit testing knows 640 columns): a window x in logical wide coordinates -> frame x,
// clamped into 0..639; and the engine's pointer clip rectangle (frame coordinates) -> wide logical coordinates, never wider than the
// centre picture.
int widePointerFrameX(int logicalX, int extra);
Rect wideClipToLogical(const Rect& clip, int extra);

// ---------------------------------------------------------------------------------------------------------------------------------
// what to draw

// The API 0x5a limits as camera yaws; on: the game restricts the turn (Engine::PanoramaFrame::limitsOn).
struct WideLimits {
    bool on = false;
    double yawMin = 0, yawMax = 0;
};

// The extra view columns (view pixels, from the edge) each side may show: `cap` limits the horizontal field of view to kWideMaxFovDeg, and
// `limit` keeps what is shown within the limits' range plus the original view's own half width (the extent the original shows when the
// view centre stands at the limit, row by row at the camera's pitch). Both are exact (closed form), infinity when nothing limits.
struct WideExtra {
    double capLeft, capRight, limitLeft, limitRight;
};
WideExtra wideExtra(const PanoCamera& cam, const WideLimits& limits);

struct WideInput {
    const Frame* frame = nullptr;       // the frame of this render
    const Frame* noFilter = nullptr;    // the same render without the page filter (the rain); null: there is none
    Rect view;                          // Engine::PanoramaFrame::view
    const u8* bare = nullptr;           // Engine::PanoramaFrame::bare
    PanoCamera cam;                     // the camera that is drawn
    WideLimits limits;
    int scale = 1;                      // output pixels per frame pixel
    int availLeft = 0, availRight = 0;  // output columns free beside the 640 * scale picture
    int quantum = 1;                    // the extent is a multiple of this (the window: whole frame pixels = scale)
    // The personal status screen is up with a palette of its own (Engine::simStatusOpaque): it covers the whole view opaque by design, so the cover
    // is not looked at and the controller keeps its state (wide stays wide, a fallback stays a fallback); the extent follows the camera as usual.
    bool keep = false;
};

struct WideDecision {
    bool wide = false;                  // the picture is wider than 4:3 (showLeft + showRight > 0)
    int showLeft = 0, showRight = 0;    // output columns beyond the 640 * scale picture that show the view
    double fovDeg = 0;                  // the horizontal field of view drawn
    double cover = 0;                   // the fraction of the edge columns covered by overlay pixels that are not rain
    bool held = false;                  // the 4:3 fallback is active (overlay at an edge)
    bool capped = false, limited = false;   // the extent is smaller than the window allows: by the field of view cap / by the scroll limits
};

// Per frame: the fallback with its hysteresis, then the extent. reset() when a frame shows no projected panorama.
class WideController {
public:
    WideDecision decide(const WideInput& in);
    void reset() { fallback_ = false; clear_ = 0; }
    bool fallback() const { return fallback_; }
private:
    bool fallback_ = false;
    int clear_ = 0;
};

// The fraction of the kWideEdgeCols outermost columns of the view (all 368 rows) where `ref` differs from the bare projection.
void wideEdgeCover(const Frame& ref, const Rect& view, const u8* bare, double& left, double& right);

// ---------------------------------------------------------------------------------------------------------------------------------
// rain

// The page filter's pixels of the centre: palette index where `frame` differs from `noFilter` (and from the bare projection), else -1.
// 640 x 368.
void panoRainMap(const Frame& frame, const Frame& noFilter, const Rect& view, const u8* bare, std::vector<int16_t>& rain);
// The rain continued into the side area: frame column x (< 0 left of the view, >= 640 right of it), row y of the view. The centre pattern
// is mirrored about interior columns (so neither a gap nor a doubled column at the boundary, and no empty strip: the original draws no thin
// drops left of column 10) and turned vertically by a different amount at each side, so the two sides do not repeat each other.
int wideSideRain(const std::vector<int16_t>& rain, int x, int y);
// `cols` columns of the side area as palette indices (-1 none): out[y * cols + i]; left side: x = -cols + i,
// right side: x = 640 + i. 368 rows.
void wideSideRainBlock(const std::vector<int16_t>& rain, bool right, int cols, std::vector<int16_t>& out);

// ---------------------------------------------------------------------------------------------------------------------------------
// the headless capture

// The canvas of a capture: outW columns, the 640 * scale picture starting at column `left`; the view is drawn from column left - showLeft to
// left + 640 * scale + showRight.
struct WideCanvas {
    int outW = 0, left = 0, showLeft = 0, showRight = 0;
};

// The wide picture: outW x 480 * scale (0x00RRGGBB). The centre columns are exactly composeSmooth()'s picture; the frame's own bands stay
// 640 * scale wide, black beside them; the view rows beside the picture show the continued projection (and the continued rain, `rain` may be
// null). Returns the number of side view pixels no triangle painted (none expected).
// `statusScreen`: the centre columns of the view are the frame opaque (no bare-projection transparency) and the side columns are dimmed (kWideStatusSideBrightness).
int composeSmoothWide(const Frame& frame, const Palette& pal, const Rect& view, const u8* bare, const std::vector<u32>& tex, const PanoCamera& cam,
                      int scale, bool linear, const WideCanvas& cv, const std::vector<int16_t>* rain, std::vector<u32>& out, bool statusScreen = false);
// Multiplies the side columns (outside the 640 * scale centre at column `left`, inside the shown extent) of the view rows by kWideStatusSideBrightness.
void dimWideSides(std::vector<u32>& out, int outW, const WideCanvas& cv, int scale, const Rect& view);

// A frame that is not a projected panorama (or the 4:3 fallback without smooth view): the 640 x 480 frame scaled by `scale`, centred in
// outW columns, black beside it.
void composeCentred(const Frame& frame, const Palette& pal, int scale, int outW, std::vector<u32>& out);

} // namespace biosys
