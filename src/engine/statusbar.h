// The objects status bar ("bar", spec 1.8): a horizontal strip of 72-px slots with 70x56 icons, shown
// in the 640x56 window at the bottom of the screen (y 424..479). No SDL.
#pragma once
#include "engine/common.h"
#include "engine/framebuffer.h"
#include "engine/image.h"
#include <functional>

namespace biosys {

class StatusBar {
public:
    static constexpr int ScreenY = 424, Height = 56, Width = 640, SlotW = 72, IconW = 70;

    // exe:0045aaf0. position -1 appends (a key != -1 goes after the last node with the same key, so a
    // group stays contiguous); otherwise inserts at that list index. Scrolls the new slot to the centre.
    bool insert(int position, const Image8& icon, int obj, int key);
    bool remove(int obj);                                  // exe:0045ae50; false if not on the bar
    bool changeImage(int obj, const Image8& icon);         // exe:0045b540
    void clear();                                          // exe:0045b870

    bool contains(int obj) const;
    int objectCount() const { return static_cast<int>(nodes_.size()); }
    int slotCount() const { return static_cast<int>(slots_.size()); }
    std::vector<int> objects() const;                      // all objects in bar (list) order
    int firstObjectInSlot(int slot) const;                 // -1 if out of range (clicks act on the first member)
    int slotMemberCount(int slot) const;
    int slotOfObject(int obj) const;                       // -1 if not on the bar

    // exe:0045bd90: slot under bar coordinate x (0..639), -1 outside the strip or when empty.
    int slotAt(int barX) const;
    int objectAt(int barX) const { return firstObjectInSlot(slotAt(barX)); }

    int scrollPos() const { return scroll_; }
    void scrollBy(int dx);                                 // exe:0045b310, clamped to [35, slots*72-37]
    void centreOnSlot(int slot);                           // exe:0045be10
    // The strip is only drawn once the bar was entered (or while flashing, spec 1.8 "Shown/hidden").
    bool shown() const { return shown_; }
    void setShown(bool s) { shown_ = s; }
    void flash(u32 untilMs) { flashUntil_ = untilMs; }
    bool visible(u32 nowMs) const { return shown_ || nowMs < flashUntil_; }
    bool flashing(u32 nowMs) const { return nowMs < flashUntil_; }   // the pick-up flash (read-only, for the overlay interface)

    // Draws the bar window into f (black background, then the strip if visible).
    void render(Frame& f, const Palette& pal, bool visible) const;

    std::function<void()> onChanged;                       // after insert / delete / change image

private:
    struct Node { Image8 icon; int obj; int key; };
    void rebuild();
    static Image8 normalise(const Image8& src);
    std::vector<Node> nodes_;
    std::vector<std::vector<int>> slots_;                  // node indices per slot
    int scroll_ = 35;
    bool shown_ = false;
    u32 flashUntil_ = 0;
};

} // namespace biosys
