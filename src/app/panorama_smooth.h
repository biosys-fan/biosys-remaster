// "Smooth panoramas": the optional high-resolution presentation of the projected panorama view (SDL side; the mathematics are in
// engine/panomesh.h). The faithful 640 x 368 projection stays the default; this draws the same cylinder at the window's resolution.
//
// Per frame (PanoramaLayer::draw): the page surface through the palette is kept as an RGBA texture (re-uploaded only for the rows or the
// palette that changed), the view is drawn as a textured mesh with SDL_RenderGeometry (exact texture coordinates, linear filtering,
// the x seam handled by the mesh and the texture border), clipped to the view rectangle. The caller then draws the usual 640 x 480 frame
// over it; draw() has made the view pixels that show the bare projection transparent in that frame's ARGB image, so bars, cursor, held
// object, sprites, wearing overlays, rain and text appear exactly as in the original presentation.
#pragma once
#include "app/clean.h"
#include "engine/engine.h"
#include "engine/panomesh.h"
#include "engine/panowide.h"
#include <SDL3/SDL.h>
#include <memory>
#include <vector>

namespace biosys {

// --widescreen (engine/panowide.h): what the window offers the layer for one frame.
struct WideRequest {
    WideController* ctl = nullptr;
    const Frame* cover = nullptr;       // the frame whose edge columns and rain are looked at (the render that panorama() describes, with its text)
    const Frame* noFilter = nullptr;    // the same render without the page filter; null: no rain
    bool keep = false;                  // the status screen with its own palette is up: the decision keeps its state (WideInput::keep), no rain
    int extra = 0;                      // whole frame columns beside the 640 columns, each side (the logical canvas is 640 + 2 * extra wide)
};

class PanoramaLayer {
public:
    explicit PanoramaLayer(SDL_Renderer* ren) : ren_(ren) {}
    ~PanoramaLayer();
    PanoramaLayer(const PanoramaLayer&) = delete;
    PanoramaLayer& operator=(const PanoramaLayer&) = delete;

    void setLinear(bool on);
    bool linear() const { return linear_; }
    // false: show the engine's camera as it is (the headless capture); true: follow it continuously (PanoCameraSmoother)
    void setFollow(bool on) { follow_ = on; }
    // Draws the panorama under the 640 x 480 frame texture. `argb` is the frame through the palette (opaque); its view pixels that
    // show the bare projection get alpha 0 (the caller uploads it as a texture with SDL_BLENDMODE_BLEND). `nowMs` is the display clock
    // (the engine's clock domain); `scale` the output pixels per frame pixel (sets the mesh density). Returns the number of view pixels
    // the mesh left unpainted (none expected).
    void draw(const Engine::PanoramaFrame& pf, const Frame& frame, std::vector<Uint32>& argb, u32 nowMs, double scale);
    // draw() in two steps, so that the window can set its logical size from the decision before anything is drawn: plan() chooses the camera
    // (and, with `wide`, the wide extent, the fallback and the rain), drawPlanned() draws. With a wide plan the mesh sits `extra` columns
    // right of the origin (the canvas is wider than the frame); the caller draws the frame texture there too.
    void plan(const Engine::PanoramaFrame& pf, const Frame& frame, u32 nowMs, double scale, const WideRequest* wide);
    void drawPlanned(const Engine::PanoramaFrame& pf, const Frame& frame, std::vector<Uint32>& argb, double scale);
    // The overlay interface's drawing (--interface overlay): like drawPlanned, but the view is at a fractional scale `k` with its top-left corner at window pixel
    // (ox, oy); the renderer's scale is 1 and its logical presentation off (render pixels = window pixels). The decision of plan() (called with scale 1: the
    // extent is in whole frame columns) says how many columns beside the 640 are drawn. Returns nothing; the frame's argb gets its transparent pixels as in drawPlanned.
    void drawPlannedAffine(const Engine::PanoramaFrame& pf, const Frame& frame, std::vector<Uint32>& argb, double k, double ox, double oy);
    // The personal status screen with a palette of its own is up (Engine::simStatusOpaque): the panorama texture is frozen (no upload with the status
    // palette: the textures keep the scene's colours, the camera still follows) and the view's centre columns of the frame are opaque (no bare-projection
    // transparency). Call before plan().
    void setStatusScreen(bool on) { statusScreen_ = on; }
    const WideDecision& decision() const { return dec_; }       // of the last plan(); wide = false without a request
    const std::vector<int16_t>& rain() const { return rain_; }  // the centre's rain pixels (640 x 368, -1 none) when the plan is wide and a filter runs, else empty
    // the glide lasts one engine pass (--pass-ms) and runs through the frames of a zoom too
    void setPassMs(u32 ms) { smoother_.setStepMs(ms); smoother_.setZoomGlide(true); }
    void reset() { smoother_.reset(); }            // the last frame had no panorama
    // --picture clean: the page texture is the cleaned one (app/clean.h) as soon as all of it for the current page has arrived (until then the plain
    // page texture is drawn). `texScale` 1 or 2: the cleaned texture is the page times that. `bands` (CleanFront::popUploads) are uploaded here;
    // call before plan(). `serial`: the page the frame shows.
    void setClean(int texScale) { cleanScale_ = texScale; }
    void applyCleanBands(std::vector<CleanBand>& bands, u32 serial);
    bool cleanShown() const { return useClean_; }
    const PanoCamera& shownCamera() const { return shown_; }
    bool lastSnapped() const { return smoother_.snapped(); }
    long uploads() const { return uploads_; }       // page texture updates so far (tests, measurements)

private:
    void upload(const Surface& page, const Palette& pal);
    void clearBarePixels(const Engine::PanoramaFrame& pf, const Frame& frame, std::vector<Uint32>& argb) const;
    SDL_Renderer* ren_;
    SDL_Texture* page_ = nullptr;
    bool linear_ = true, follow_ = true, havePal_ = false;
    std::vector<u32> texCpu_;
    std::vector<u8> idx_;
    Palette pal_;
    PanoMesh mesh_;
    std::vector<SDL_Vertex> sv_;
    PanoCameraSmoother smoother_;
    PanoCamera shown_;
    long uploads_ = 0;
    // cleaned page texture
    int cleanScale_ = 0;                   // 0: off
    SDL_Texture* cleanTex_ = nullptr;
    u32 cleanSerial_ = 0;
    bool useClean_ = false, haveCleanSerial_ = false;
    std::vector<char> cleanCover_;         // per texture row: uploaded for cleanSerial_
    int cleanCovered_ = 0;
    bool statusScreen_ = false;
    WideDecision dec_;
    std::vector<int16_t> rain_;
    int planExtra_ = 0;
};

// The same drawing into an off-screen surface of 640 * scale x 480 * scale through SDL's software renderer (no window, no GPU), as
// 0x00RRGGBB. NOTE: SDL's software renderer samples textures with nearest-neighbour only (SDL_RenderGeometry works, linear
// filtering is ignored), so this checks the mesh and the compositing through SDL, not the filtering; the filtered headless capture is
// panomesh.h's CPU rasteriser. Returns false (and `err`) when SDL cannot do it.
bool captureSdlSoftware(const Engine::PanoramaFrame& pf, const Frame& frame, int scale, std::vector<Uint32>& rgb, std::string& err);

// The same, kept across calls like the window keeps its PanoramaLayer: one off-screen surface, renderer and layer, so the layer's change
// detection (which rows of the page texture, or the palette, to upload) is exercised frame after frame (tests/test_clickanim.sh).
class SdlSmoothSession {
public:
    SdlSmoothSession();
    ~SdlSmoothSession();
    SdlSmoothSession(const SdlSmoothSession&) = delete;
    SdlSmoothSession& operator=(const SdlSmoothSession&) = delete;
    bool capture(const Engine::PanoramaFrame& pf, const Frame& frame, int scale, std::vector<Uint32>& rgb, std::string& err);
    long uploads() const;       // page texture updates so far
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace biosys
