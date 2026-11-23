// The held object's image as a layer of the frame (optional; the 8-bit frame and the game do not depend on it). The wide presentations
// (--widescreen, --interface overlay) measure the 4:3 fallback's edge cover on the frame without it, so a held object (the hand reaches the
// right edge of the view) can never cause or hold the fallback; the image is then drawn back at its own place. See Engine::setSplitLayers.
#pragma once
#include "engine/framebuffer.h"
#include "engine/image.h"

namespace biosys {

struct ViewLayer {
    bool shown = false;        // true: render() left the image out of the frame and the front end draws it back; false: it is in the frame (or there is none)
    Image8 image;              // palette indices
    int x = 0, y = 0;          // top-left in the view (columns 0..639, rows 0..367); the view's first row is ViewLayers::viewTop of the frame
    int transparent = -1;      // palette index that is not drawn, -1: opaque
};

struct ViewLayers {
    int viewTop = 56;
    ViewLayer held;            // the held object's image (GameObjects::drawHeld)
    // The layer goes into `f` as the engine would have drawn it: the frame render() made plus this equals the frame render() makes without the
    // split, byte for byte.
    void blitInto(Frame& f) const {
        const Rect clip{0, viewTop, 640, 368};
        if (held.shown) blit(f, held.image, held.x, viewTop + held.y, clip, held.transparent);
    }
};

} // namespace biosys
