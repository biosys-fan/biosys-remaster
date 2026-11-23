// Text bookkeeping for an optional high-resolution presentation of the GDI text (--text sharp, src/app/text_sharp.*).
//
// The game's text is drawn by Canvas into the 8-bit surfaces of the windows (page surface, top-bar / popup layers, the sim-status
// window, menu pages, the terminal's pages). A TextPlane is attached to such a Surface (Surface::text) while tracking is on and
// remembers, per surface pixel, the last text pixel that was drawn there: its ink index, the pixel (and layer mask) it replaced, and
// the run (TextRun) it belongs to.
//
// THE RULE. A record says "text pixel with ink I over background U". It is LIVE while the surface still holds I at that pixel (and,
// on a layer, its mask is set): whatever overwrites the pixel with another value (a RedrawPage, a DisplayImage, a sprite, a fill of
// the engine, a later text) ends it without any hook, and whatever puts the old value back (a sprite's save-under) revives it.
// Canvas itself ends the record of every pixel it draws with something other than text (fills, lines, ellipses; an opaque text
// background). A later text pixel over a live record keeps the old background U (the plane remembers what lies under ALL the text).
// Whole surfaces start again (reset) when the page is rebuilt or the layer is cleared. Known blind spot: a non-Canvas write of exactly
// the ink value over a live text pixel keeps the record live (the text-free picture then shows the old background there).
//
// Stripping puts U back into the surface at every live record (and clears the layer mask where the layer had nothing); restoring
// puts I back. Rendering the frame with the text stripped and as it is gives the text-free picture and, by difference, the text
// pixels that are visible in the final frame, in whatever order the engine composes its layers.
#pragma once
#include "engine/common.h"
#include "engine/font.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace biosys {

struct Surface;

struct TextRun {
    u32 id = 0;                         // global, increasing: the order the text was drawn in
    std::shared_ptr<const Font> font;
    std::string text;                   // the bytes as drawn (one line; DrawText draws a run per line)
    int x = 0, y = 0;                   // top-left of the text cell on the surface, after TA_* alignment
    int clipL = 0, clipT = 0, clipR = 0, clipB = 0;   // surface pixels that could be drawn (DrawText rectangle; right/bottom exclusive)
    u8 ink = 0;                         // palette index of the text colour
    u32 colorRef = 0;                   // the COLORREF as the game set it
    unsigned align = 0;                 // SetTextAlign state (x already includes it)
    bool opaque = false;                // SetBkMode(OPAQUE): the cell was filled with the background colour first
    u8 bkIndex = 0;
};

class TextPlane {
public:
    // Process-wide switch (Engine::setTextTracking): Canvas attaches planes only while it is on.
    static bool tracking();
    static void setTracking(bool on);

    // The surface (re)initialised or its content replaced: no text any more.
    void reset();
    // Begins a run on `s`: the dead records inside the cell are dropped (the cell is being drawn over). Returns the run id.
    u32 beginRun(const Surface& s, TextRun run, std::vector<u8>* layerMask);
    // One glyph pixel of the run about to be written at `at` (index into the surface); call before the write.
    void putInk(const Surface& s, size_t at, u8 ink, u32 runId, const std::vector<u8>* layerMask);
    // Canvas drew something else at `at` (call before the write): the record ends.
    void kill(size_t at);
    void endRun(u32 runId);              // runs that own no pixel are dropped

    // Strip / restore (see above). `s` and `layerMask` must be the surface and mask the plane belongs to.
    void strip(Surface& s, std::vector<u8>* layerMask);
    void restore(Surface& s, std::vector<u8>* layerMask);

    // The live runs: for each, the surface positions of the pixels it owns that are live (index into the surface).
    struct LiveRun {
        const TextRun* run = nullptr;
        std::vector<u32> owned;
    };
    void live(const Surface& s, const std::vector<u8>* layerMask, std::vector<LiveRun>& out) const;
    size_t recordCount() const { return touched_.size(); }
    size_t runCount() const { return runs_.size(); }
    bool empty() const { return runs_.empty(); }

private:
    struct Slot { TextRun run; int owned = 0; };
    void ensure(const Surface& s);
    bool isLive(const Surface& s, size_t at, const std::vector<u8>* mask) const;
    void release(size_t at);
    int w_ = 0, h_ = 0;
    std::vector<u16> ink_;               // 0xffff: no record
    std::vector<u8> under_, underMask_;
    std::vector<u32> owner_;
    std::vector<u32> touched_;           // surface indices that have (or had) a record
    std::vector<u32> stripped_;
    std::map<u32, Slot> runs_;
    u32 cur_ = 0;                        // the run being drawn
    size_t records_ = 0;
};

// The plane of a surface, made on first use.
TextPlane& planeOf(Surface& s);

} // namespace biosys
