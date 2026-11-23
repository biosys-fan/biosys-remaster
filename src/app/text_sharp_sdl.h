// The window side of --text sharp (app/text_sharp.h has the composition): the sharp glyphs as one transparent texture of the output's
// resolution, drawn over the enlarged frame. It is rebuilt only when the runs, their visible pixels or their colours change.
#pragma once
#include "app/text_sharp.h"
#include <SDL3/SDL.h>

namespace biosys {

class SharpTextLayer {
public:
    explicit SharpTextLayer(SDL_Renderer* ren) : ren_(ren) {}
    ~SharpTextLayer();
    SharpTextLayer(const SharpTextLayer&) = delete;
    SharpTextLayer& operator=(const SharpTextLayer&) = delete;
    // `tf.free` is what the caller drew as the frame. `scale`: output pixels per frame pixel (a whole number).
    // `offsetCols`: the frame sits this many frame columns right of the canvas's origin (--widescreen: the logical canvas is wider than 640)
    void draw(const Engine::TextFrame& tf, int scale, int offsetCols = 0);
    long rebuilds() const { return rebuilds_; }

private:
    SDL_Renderer* ren_;
    SDL_Texture* tex_ = nullptr;
    int scale_ = 0;
    uint64_t sig_ = 0;
    bool any_ = false;
    long rebuilds_ = 0;
    std::vector<uint32_t> argb_;
};

} // namespace biosys
