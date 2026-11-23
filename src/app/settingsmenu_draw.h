// Drawing of the settings menu, the new-game screen and the "F10 settings" hint (SDL-free): an ARGB layer of 640 * k x 480 * k pixels
// (straight alpha) that the window puts over its picture as a texture and that `composeMenuPicture` puts over a CPU-scaled frame for the
// pictures of the tests. The text is the game's own typeface (BIODATA\FONTS\JON.TTF, "systemfont") through the engine's font code, drawn
// the way sharp text is (app/text_sharp.h): the layout is the 640 x 480 one (integer advances), the glyphs are anti-aliased at k times
// the size. Nothing here is ever drawn into the engine's 640 x 480 frame.
#pragma once
#include "app/settingsmenu.h"
#include "engine/datafiles.h"
#include "engine/font.h"
#include "engine/framebuffer.h"
#include "engine/image.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace biosys {

class MenuFonts {
public:
    explicit MenuFonts(const DataFiles& files);
    const Font* title() const { return title_; }
    const Font* body() const { return body_; }
    const Font* small() const { return small_; }
    bool ok() const { return body_ && body_->hasFace(); }

private:
    FontLibrary lib_;
    const Font *title_ = nullptr, *body_ = nullptr, *small_ = nullptr;
};

// The darkening of the picture behind a menu (black at this alpha out of 255; the window draws the same over its picture)
constexpr int kMenuDim = 150;

// The layer of the open menu or, when none is open and `hint` is set, the small "F10 settings" hint. Returns false when there is nothing
// to draw (the buffer is then left empty). `k`: output pixels per grid pixel (a whole number, 1..8).
bool renderMenuLayer(const smenu::Controller& c, const MenuFonts& fonts, int k, bool hint, std::vector<uint32_t>& argb);

// How a text wraps at `width` grid pixels (the lines of the help text; used by the tests to check that every text fits)
std::vector<std::string> wrapMenuText(const Font& f, const std::string& text, int width);

// The window's picture as the tests see it: the 640 x 480 frame enlarged by the whole-number scale that fits in winW x winH (as the
// window does), centred on black, darkened if `dim`, with the layer over it. rgb: 0x00RRGGBB, winW * winH.
void composeMenuPicture(const Frame& frame, const Palette& pal, int winW, int winH, bool dim, const std::vector<uint32_t>& layer, int k,
                        std::vector<uint32_t>& rgb);

} // namespace biosys
