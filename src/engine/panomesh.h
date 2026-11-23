// Optional high-resolution presentation of the panorama view ("smooth panoramas"): the mathematics, with no SDL.
// The faithful 640 x 368 projection (projection.h) stays the reference; this file draws the SAME cylinder (the camera of
// Engine::panorama()) at the output resolution: exact per-vertex texture coordinates (no 32-pixel grid, no seam quirk, no
// truncations), bilinear filtering, and a camera that follows the engine's camera continuously.
//
// Conventions (a decision of this file, not of the original): the original shows at view pixel n the page point of the view
// coordinate c = n - 320 (r = line - 184), i.e. it treats the pixel as a point sample at its centre. The smooth view keeps that
// identification: the continuous view point (x, y) (pixel n covers [n, n+1)) has c = x - 320.5, r = y - 184.5, so the smooth picture
// is the original picture with more samples in between, not shifted against it, the hit test or the overlays.
#pragma once
#include "engine/common.h"
#include "engine/framebuffer.h"
#include "engine/image.h"
#include "engine/projection.h"
#include <vector>

namespace biosys {

// The page texture has a one-texel border (column -1 = page column 2559, column 2560 = page column 0; row -1 = row 0, row 1024 =
// row 1023), so linear filtering wraps correctly at the x seam without any texture address mode.
constexpr int kPanoTexW = kPanoW + 2, kPanoTexH = kPanoH + 2;

struct MeshVertex {
    float x, y;     // view pixels (0..640, 0..368)
    float u, v;     // page pixels (0..2560, 0..1024; pixel i covers [i, i+1))
};
struct PanoMesh {
    std::vector<MeshVertex> verts;
    std::vector<int> idx;   // triangles, three indices each
};

// The page point (page pixels, u unwrapped: continuous around the camera yaw) shown at the continuous view point (x, y).
void panoMap(const PanoCamera& cam, double x, double y, double& u, double& v);

// Mesh cell size (view pixels) for an output scale (output pixels per view pixel): fine enough that the interpolation error of the
// texture coordinates stays under a quarter of an output pixel (tools: biosys-panotest measures it).
int panoCellFor(double outputScale);

// The view as triangles, vertex (x, y) at every `cell` view pixels (cell divides 16), u shifted by whole turns so the view centre is in
// [0, 2560); triangles that cross u = 0 or u = 2560 are cut there and the pieces shifted back, so every vertex has u in [0, 2560].
void buildPanoMesh(const PanoCamera& cam, int cell, PanoMesh& out);
// The same grid extended to view x in [xLo, xHi] (xLo <= 0 and xHi >= 640, both multiples of `cell`; vertex x is negative left of the view):
// the cells inside the view are the cells of buildPanoMesh, vertex for vertex and triangle for triangle (--widescreen, engine/panowide.h).
void buildPanoMeshRange(const PanoCamera& cam, int cell, int xLo, int xHi, PanoMesh& out);

// Page surface through the palette as 0xAARRGGBB (alpha 255), kPanoTexW x kPanoTexH with the border. The Rows variant rewrites page rows
// [y0, y1) only (and the border rows they feed).
void panoTextureAll(const Surface& page, const Palette& pal, std::vector<u32>& tex);
void panoTextureRows(const Surface& page, const Palette& pal, int y0, int y1, std::vector<u32>& tex);

// CPU rasteriser of the mesh (the headless capture and the test oracle of the mesh; the window uses SDL_RenderGeometry): pixel (X, Y) of
// the output (640 * scale x 368 * scale, 0x00RRGGBB) samples the texture at its centre. linear = bilinear, else nearest.
// Pixels no triangle covers keep their value (the caller clears).
void rasterizePano(const PanoMesh& mesh, const std::vector<u32>& tex, int scale, bool linear, std::vector<u32>& out, int texScale = 1);

// The rasteriser into a canvas of W x H output pixels (0x00RRGGBB) with the view's x origin at canvas column `offX` (vertex x = 0 is column
// offX); only columns [clipX0, clipX1) are written. With offX = 0 and the clip [0, W) it is rasterizePano, pixel for pixel; a pixel inside the
// view gets the same value whatever offX is (the edge functions shift by whole pixels).
void rasterizePanoRegion(const PanoMesh& mesh, const std::vector<u32>& tex, int scale, bool linear, int W, int H, int offX, int clipX0, int clipX1,
                         std::vector<u32>& out, int texScale = 1);
// The same with a fractional scale and an origin that is a fraction of an output pixel (the overlay interface: scale k = H / 368): vertex (x, y) lands at
// output (x * scale + offX, y * scale + offY); only the rectangle [clipX0, clipX1) x [clipY0, clipY1) is written. With integer scale and offsets and offY = 0
// it is rasterizePanoRegion, pixel for pixel (that function calls it).
void rasterizePanoAffine(const PanoMesh& mesh, const std::vector<u32>& tex, double scale, bool linear, int W, int H, double offX, double offY, int clipX0,
                         int clipX1, int clipY0, int clipY1, std::vector<u32>& out, int texScale = 1);
// (texScale: the texture is the page enlarged by that whole number, (2560 * texScale + 2) x (1024 * texScale + 2) with the one-texel border; the
// --picture clean panorama texture. 1 = kPanoTexW x kPanoTexH as always.)

// The whole picture of the smooth path, 640 * scale x 480 * scale (0x00RRGGBB): the frame scaled by whole numbers; in the view
// rectangle the pixels that were not drawn over the panorama (frame == bare) come from the smooth rendering instead.
// Returns the number of non-overlay view pixels no triangle painted (the scaled frame stays there; none expected).
int composeSmooth(const Frame& frame, const Palette& pal, const Rect& view, const u8* bare, const std::vector<u32>& tex,
                   const PanoCamera& cam, int scale, bool linear, std::vector<u32>& out);

// Which pixels of the view the engine drew over the panorama (1) and which show the bare projection (0), 640 x 368.
void panoOverlayMask(const Frame& frame, const Rect& view, const u8* bare, std::vector<u8>& mask);

// The camera the high-resolution view displays: it follows the engine's camera (which moves in whole pixels, at its 16 ms idle passes,
// exe:00444560) by a linear glide that lasts one engine step (kStepMs) from the engine's own time of the step, so it never lags more
// than one step, never overshoots, is monotonic between two steps and sits exactly on the engine's camera one step after it stopped.
// It snaps (no glide) when the page changes, in every frame of a zoom animation (field of view other than 75, or different from the
// last frame's), at the first frame, and when the camera jumps by more than kSnapYaw pixels or kSnapPitchDeg degrees in one step.
class PanoCameraSmoother {
public:
    static constexpr u32 kStepMs = 16;                         // the default glide: one idle pass of 16 ms
    // The glide lasts one engine pass (the window's --pass-ms). Default kStepMs.
    void setStepMs(u32 ms) { stepMs_ = ms < 1 ? 1 : ms; }
    u32 stepMs() const { return stepMs_; }
    // true: the frames of a zoom animation glide too (position, field of view and focal length run linearly from the camera that was
    // shown to the new frame's, over one pass) instead of snapping. Default false (the headless traces).
    void setZoomGlide(bool on) { zoomGlide_ = on; }
    static constexpr double kSnapYaw = 160.0;                  // page pixels: a normal scroll step is a pointer delta (x2 when fast) < ~100
    static constexpr double kSnapPitchDeg = 160.0 * 86.0 / 1024.0;   // the same number of scroll rows
    // target: the engine's camera of this frame; stepTime: Engine::PanoramaFrame::scrollStepTime (a change of it dates the new
    // target, else the target is dated `now`); now: the display clock (the same clock as the engine's, ms).
    PanoCamera update(const PanoCamera& target, u32 pageSerial, u32 stepTime, u32 now);
    void reset() { have_ = false; }                            // the frame had no panorama (menu, other page kind)
    bool snapped() const { return snapped_; }                  // the last update() did not glide
    // the engine's camera with its yaw unwrapped (continuous over the seam), as followed now
    const PanoCamera& target() const { return to_; }
private:
    PanoCamera at(u32 t) const;
    bool have_ = false, snapped_ = false, zoomGlide_ = false;
    u32 stepMs_ = kStepMs;
    PanoCamera from_, to_;
    double engineYaw_ = 0;
    u32 t0_ = 0, serial_ = 0, stepTime_ = 0, lastNow_ = 0;
    double lastFov_ = 75;
};

// 24-bit BMP of 0x00RRGGBB pixels (the comparison images and the captures of the tests).
void writeBmp24(const std::string& path, int w, int h, const std::vector<u32>& rgb);

} // namespace biosys
