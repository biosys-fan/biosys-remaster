// The window side of the overlay interface (app/overlaycompose.h has the state and the CPU composition, app/overlaylayout.h the arithmetic): the
// sprites (status words, message line, inventory strip, sharp view text) as textures, the dark gradient behind the strip, and the ambient fill.
// Everything is drawn in render pixels = window pixels (the renderer's logical presentation is off while it draws).
#pragma once
#include "app/overlaycompose.h"
#include <SDL3/SDL.h>
#include <vector>

namespace biosys {

class OverlayRenderer {
public:
    explicit OverlayRenderer(SDL_Renderer* ren) : ren_(ren) {}
    ~OverlayRenderer();
    OverlayRenderer(const OverlayRenderer&) = delete;
    OverlayRenderer& operator=(const OverlayRenderer&) = delete;

    // The ambient fill beside the picture [pL, pR) (window x): the scene's ambient picture covering the window, at kAmbientBrightness, brighter
    // towards the picture over a gradient of g pixels. Not called when the picture fills the width.
    void drawAmbient(const OverlayScene& sc, const ovl::Layout& L, double pL, double pR);
    // The dark gradient, the strip, the message line and the status words at their fades.
    void drawOverlays(const OverlayScene& sc, const ovl::Layout& L);
    // A sprite in texture slot `slot` (re-uploaded when its revision changed).
    void drawSprite(int slot, const Sprite& s, double alpha);
    // The sharp text of the view: a bitmap of outW x outH ARGB at window position (x, y), re-uploaded only when `sig` changed.
    void drawBitmap(int slot, uint64_t sig, const std::vector<uint32_t>& argb, int w, int h, int x, int y);

private:
    struct Slot {
        SDL_Texture* tex = nullptr;
        uint64_t rev = 0;
        int w = 0, h = 0;
    };
    Slot& slot(int i);
    void upload(Slot& s, uint64_t rev, const uint32_t* px, int w, int h);
    SDL_Renderer* ren_;
    std::vector<Slot> slots_;
    SDL_Texture* amb_ = nullptr;
    long ambBuild_ = -1;
    int ambW_ = 0, ambH_ = 0;
};

} // namespace biosys
