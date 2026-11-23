// The overlay interface (--interface overlay; layout and pointer arithmetic in overlaylayout.h): SDL-free state and composition.
//
// OverlayScene is the state that lives from frame to frame: the status words and the message line (the text runs of the engine's "topbar"
// layer, drawn again with a dark outline and a soft dark halo and no bar behind them), the inventory strip (the bar rows of the frame with
// the black around the icons made transparent, over a dark gradient), their fades, and the ambient fill (the view's picture reduced to 64
// columns and blurred). The window draws these with SDL (main.cpp); composeOverlay() draws exactly the same on the CPU (the headless
// capture of tests/test_overlay.sh, script command `overlay shot`).
#pragma once
#include "app/overlaylayout.h"
#include "engine/engine.h"
#include "engine/panomesh.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace biosys {

// A straight-alpha ARGB bitmap (0xAARRGGBB) placed at a window pixel position (it may reach beyond the window).
struct Sprite {
    int x = 0, y = 0, w = 0, h = 0;
    uint64_t rev = 0;                       // a new number whenever the bitmap is made again (the window re-uploads its texture then)
    std::vector<uint32_t> argb;
    bool empty() const { return w <= 0 || h <= 0 || argb.empty(); }
};

// One line of the top bar's text with its outline and halo, placed for window layout `L`: the cell of the text starts at (cellX, cellY).
// `radiusOut` is how far the outline and the halo reach beyond the cell.
uint64_t nextSpriteRevision();
Sprite buildTextSprite(const Engine::TextRunOut& r, int u, int cellX, int cellY);

struct SceneInput {
    const Frame* frame = nullptr;          // the frame as the engine renders it (the bar rows of it are the strip, its view rows the ambient fill)
    const Palette* pal = nullptr;
    const std::vector<Engine::TextRunOut>* runs = nullptr;   // Engine::renderText's list (layer "topbar" is the words and the message)
    bool barMode = false, flashing = false, pointerInBar = false;
    int barObjects = 0;                    // objects on the bar (no object: no strip)
    uint32_t nowMs = 0;                    // the engine's clock (ms)
};

class OverlayScene {
public:
    // Once per drawn frame, with the layout of the window.
    void update(const ovl::Layout& L, const SceneInput& in);
    void reset();

    struct Word { Sprite sprite; };
    const std::vector<Word>& words() const { return words_; }
    const Sprite& message() const { return message_; }
    double messageAlpha() const { return msgFade_.v; }
    const Sprite& strip() const { return strip_; }
    double stripAlpha() const { return stripFade_.v; }
    bool stripWanted() const { return stripWant_; }
    // the ambient picture: aw x ah, opaque 0x00RRGGBB; changes() counts how many times it was rebuilt
    const std::vector<uint32_t>& ambient() const { return ambient_; }
    int ambientW() const { return aw_; }
    int ambientH() const { return ah_; }
    long ambientBuilds() const { return ambientBuilds_; }
    long stripBuilds() const { return stripBuilds_; }
    long textBuilds() const { return textBuilds_; }

private:
    void buildAmbient(const SceneInput& in);
    void buildStrip(const ovl::Layout& L, const SceneInput& in);
    std::vector<Word> words_;
    Sprite message_;
    uint64_t wordsSig_ = 0, msgSig_ = 0, stripSig_ = 0, ambientSig_ = 0;
    bool msgShown_ = false, stripWant_ = false;
    ovl::Fader msgFade_, stripFade_;
    Sprite strip_;
    std::vector<uint32_t> ambient_;
    int aw_ = 0, ah_ = 0;
    uint32_t lastNow_ = 0;
    bool haveNow_ = false;
    int lastU_ = 0, lastW_ = 0, lastH_ = 0;
    long ambientBuilds_ = 0, stripBuilds_ = 0, textBuilds_ = 0;
    int stripCount_ = 0;
};

// The part of the ambient picture that covers a window of W x H: normalised source rectangle (u0, v0, uspan, vspan) of the ambient picture.
struct AmbientCrop { double u0 = 0, v0 = 0, uspan = 1, vspan = 1; };
AmbientCrop ambientCrop(int W, int H);

// What the CPU composition draws the view from.
struct OverlayView {
    const Frame* frame = nullptr;          // the frame to show (the text-free one when the text is sharp)
    const Palette* pal = nullptr;
    const Engine::TextFrame* tf = nullptr; // sharp view text: the runs (null: the text stays in `frame`)
    bool sharp = false;
    bool pano = false;                     // a projected panorama: the smooth view, from `pf` and `tex`
    const Engine::PanoramaFrame* pf = nullptr;
    PanoCamera cam;                        // the camera that is drawn
    const std::vector<u32>* tex = nullptr; // panoTextureAll of the page
    bool linear = true;
    int texScale = 1;                      // the page texture is the page enlarged by this whole number (--picture clean: 2)
    int showL = 0, showR = 0;              // widescreen: view columns shown beside the 640 (frame columns)
    const std::vector<int16_t>* rain = nullptr;   // the rain of the centre (panoRainMap), continued into the sides
    bool statusScreen = false;             // the status screen with its own palette is up: the centre is opaque, the panorama beside it is dimmed (kWideStatusSideBrightness)
};

// Draws one window picture of W x H pixels (0x00RRGGBB): ambient fill, the view, the overlays of `scene` at their fades.
void composeOverlay(const ovl::Layout& L, const OverlayScene& scene, const OverlayView& v, std::vector<uint32_t>& rgb);

// The captured pointer's motion in the overlay interface (window and script share it): the window-pixel motion (mx, my) since the pointer was last put or seen
// becomes engine pixels (divided by k, in bar mode by u; the remainder is carried) and goes to Engine::mouseMove as a delta from the engine's pointer position
// (in the vertical release the pointer row is that position plus the delta: no inverse map). `adjust` (may be empty) changes the deltas first (the sensitivity
// factor; not called in bar mode). Returns false when the motion is less than a whole engine pixel.
bool overlayCapturedMove(Engine& e, const ovl::Layout& L, ovl::MotionCarry& carry, double mx, double my, const std::function<void(int&, int&)>& adjust);

// Script commands (app/overlay_script.cpp): `overlay on|off|size W H|frame [MS]|shot FILE|state|pointer ...`
bool scriptOverlayCommand(Engine& e, const std::string& op, const std::string& arg);
// `overlay pump on|off`: while on, every turn of the engine's blocking loops (the transition film of a walk, the cross-fade, an object animation) presents a frame
// like the window's pump does: `overlay frame` + `overlay diff` lines (script.cpp's pump calls overlayPumpFrame).
bool overlayPumpActive();
void overlayPumpFrame(Engine& e);

} // namespace biosys
