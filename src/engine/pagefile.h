// CONTRACT — PAGES.DAT / BIOSYS4.DAT reader.
// Format reference: analysis/notes/formats.md section 2 (field offsets there are
// authoritative). Python reference implementation: analysis/scripts/biosys_pages.py.
#pragma once
#include "engine/common.h"
#include "engine/image.h"
#include <memory>

namespace biosys {

struct PageTableEntry {
    u32 offset = 0;      // file offset of the page record (static part)
    u32 staticSize = 0;  // bytes of the static part
    u32 dynaSize = 0;    // bytes of the dyna-load area that follows it
    u32 flags = 0;
    std::string name;
};

// Hotspot/timer action: verb + 8 raw arguments (unused arguments contain junk in the file).
struct Action {
    i32 verb = 0;
    i32 arg[8] = {};     // arg[0] is "a1" in formats.md
};

enum ActionVerb : i32 {
    VERB_NONE = 0, VERB_BACK = 1, VERB_JUMP = 2, VERB_PLAYWAVE = 4, VERB_DISPLAYIMAGE = 7,
    VERB_POPUP = 9, VERB_CLOSEPOPUP = 10, VERB_PLAYANIM = 11, VERB_ALLOWOBJECTDROP = 13,
    VERB_BS4 = 15, VERB_JUMP_SCROLL = 16,
};

struct Ornament {
    std::string name;
    u32 type = 0;        // 1 = image
    u32 flags = 0;       // bit0 initially hidden, bit2 fruit image, bit4 paint with transparency
    i32 x = 0, y = 0;    // position on the page
    u32 image = 0;       // 0-based index into Page::images
    Color transparent;   // stored in the file as B,G,R
    bool hidden() const { return flags & 1; }
    bool fruit() const { return flags & 4; }
    bool transparentPaint() const { return flags & 0x10; }
};

struct Hotspot {
    i32 left = 0, top = 0, right = 0, bottom = 0;  // page coordinates
    u32 flags = 0;       // bit0 = don't draw button
    u32 button = 0;      // 1-based index into TOUCHINF buttons, 0 = none
    Action action;
    std::string name;
};

struct PageTimer {
    u32 delay = 0;       // in 250 ms units (record +0x04)
    Action action;       // record +0x10
    std::string name;    // record +0x34
};

struct CrossFade {
    u32 steps = 0;       // record +0x04
    std::string name;    // record +0x4c, destination page name
};

// Image descriptor (pixels are loaded on demand with PageFile::loadImage).
struct PageImage {
    u32 compressed = 0;  // 0 = raw bottom-up rows, 1 = zlib stream of top-down rows
    u32 width = 0, height = 0, stride = 0;
    u32 dataSize = 0;    // compressed size (0 for raw)
    i32 dynaOffset = -1; // -1 = pixel data inline after the descriptor
    u32 fileOffset = 0;  // absolute file offset of the pixel data (computed by the loader)
};

struct Page {
    int number = 0;      // 1-based
    std::string name;
    u32 flags = 0;       // bit0 scrolling wrap-around panorama, bit1 popup page
    u32 width = 0, height = 0;   // as stored; 0 means book size (640 x 368)
    u32 initialScrollX = 0;
    u32 background = 0;
    std::vector<std::string> files;
    std::vector<Ornament> ornaments;
    std::vector<Hotspot> hotspots;
    std::vector<PageTimer> timers;
    std::vector<CrossFade> crossFades;
    std::vector<PageImage> images;
    bool panorama() const { return flags & 1; }
    bool popup() const { return flags & 2; }
    int pixelWidth() const { return width ? static_cast<int>(width) : 640; }
    int pixelHeight() const { return height ? static_cast<int>(height) : 368; }
};

class PageFile {
public:
    // Opens the file and reads the page table. Throws Error on failure.
    explicit PageFile(const std::string& path);
    ~PageFile();
    PageFile(const PageFile&) = delete;
    PageFile& operator=(const PageFile&) = delete;

    int pageCount() const;
    const PageTableEntry& entry(int number) const;      // 1-based; throws Error if out of range
    int findPage(const std::string& name) const;        // case-insensitive; 0 if not found
    Page loadPage(int number) const;                    // parses the static part of a page record
    Image8 loadImage(const Page& page, int imageIndex) const;  // decodes one image (see Image8)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace biosys
