// PageView: the page viewer core (current page, scrolling, hotspots, popups, palette). No SDL.
#pragma once
#include "engine/common.h"
#include "engine/datafiles.h"
#include "engine/framebuffer.h"
#include "engine/image.h"
#include "engine/pagefile.h"
#include "engine/projection.h"
#include "engine/palettesets.h"
#include <functional>
#include <map>
#include <memory>

namespace biosys {

enum class PaletteSource { Fixed, Set, DayFallback, KeptOnFailure, Kept, Default };

struct ClickResult {
    bool hit = false;           // a hotspot (or popup dismissal) was triggered
    bool implemented = true;    // false: verb not implemented yet, nothing was done
    std::string text;           // human readable description of the event ("" if nothing hit)
};

// Run-time state of one hotspot that the object manager owns (zones): `disabled` = skipped by hit
// testing (flag bit0 in the file is only the initial state); a hit rectangle replaces the file
// rectangle. Rectangles are inclusive (right/bottom are the last pixel), page coordinates.
struct HotspotOverride {
    bool disabled = false;
    bool hasRect = false;
    int l = 0, t = 0, r = 0, b = 0;
};
using HotspotQuery = std::function<HotspotOverride(int pageNumber, int index, const Hotspot& hs)>;

struct HitInfo {
    const Page* page = nullptr;   // page owning the hotspot (main page, top bar or popup)
    int index = -1;               // index in page->hotspots
    const Hotspot* hotspot = nullptr;
    explicit operator bool() const { return hotspot != nullptr; }
    bool sameAs(const HitInfo& o) const { return page == o.page && index == o.index; }
};

class PageView {
public:
    static constexpr int TopBarH = 56, ViewH = 368;   // screen layout: top bar, view area (bottom bar 424..479)

    // dataDir: the port's data/ directory (palette tables). Loads pages.dat and the "topbar" page.
    PageView(const DataFiles& files, const std::string& dataDir);

    // --- navigation ---
    int pageCount() const;
    int findPage(const std::string& name) const;            // 0 if unknown
    std::string pageNameOf(int number) const;               // "" if out of range
    // throws Error on bad number/data. scroll (x, y): the starting position of the new page, set before its events fire
    // (exe:00411350 with a start position, as LoadGame passes it); nullptr = the page's own start.
    void goToPage(int number, bool pushHistory = true, const int* scroll = nullptr);
    bool goBack();                                          // false if history is empty
    void clearHistory() { history_.clear(); }
    int pageNumber() const;
    const Page& page() const;
    bool panorama() const;
    bool fullScreen() const;                                // stored height 480: no top bar
    bool scrollable() const;                                // page larger than the 640x368 view

    // --- scrolling (panoramas wrap in x; other large pages clamp; y always clamps) ---
    // track = false: the scroll position changes but the remembered render state of the projection (exe:0x499110 / 0x499114) does not,
    // as when the original sets +0x2c4 / +0x2c8 and has not drawn yet.
    void setScroll(int x, int y, bool track = true);
    // Remembers yaw / pitch of the present scroll position (= one render of the original) and ends the hold of a page load (see enter()).
    void releaseTrack();
    void scrollBy(int dx, int dy);
    int scrollX() const;
    // exe:00443f30 / exe:00403470: pages of 2560 x 1024 are shown through the cylindrical projection (projection.h)
    bool projected() const;
    Projection& projection() { return proj_; }
    const Projection& projection() const { return proj_; }
    int scrollY() const;
    // The last render() for a high-resolution presentation (src/engine/panomesh.h): `valid` when the view was cut through the projection;
    // `bare` = the 640 x 368 indices of the projection of the bare page, before the popup, outlines or anything of the Engine was drawn
    // over them (the same bytes cutView copied into the frame); `pageSerial` changes whenever another page (or the saved one) is entered.
    struct ProjectedView {
        bool valid = false;
        const u8* bare = nullptr;
        PanoCamera camera;
        Rect rect;                 // on the 640 x 480 screen
        u32 pageSerial = 0;
    };
    const ProjectedView& lastProjectedView() const { return projected_; }

    // --- interaction (screen coordinates, 640x480) ---
    HitInfo hit(int sx, int sy) const;                      // enabled hotspot under the point, last to first
    const Hotspot* hotspotAt(int sx, int sy) const { return hit(sx, sy).hotspot; }
    ClickResult perform(const Hotspot& h);                  // default action of the viewer verbs (jump, back, popup)
    ClickResult click(int sx, int sy);                      // hit + perform
    // Page position (x wrapped on panoramas) of a view point; false if (sx,sy) is not in the view.
    bool viewToPage(int sx, int sy, int& px, int& py) const;
    bool popupOpen() const;
    void closePopup();
    // Opens a popup page (verb 9 / API 0x60); throws Error on a bad number. The popup replaces an open one.
    void openPopup(int pageNumber);
    bool isPopupPage(int pageNumber);                       // header flag bit 1 (only "topbar" has it in PAGES.DAT)
    // exe:00450bd0 DisplayPopupPage, the one routine behind API 0x60 and verb 9: true if the page is open already or was opened; false
    // (nothing opened) for a page number that does not exist, a stored width or height of 0, or a header without the popup flag (bit 1).
    bool displayPopupPage(int pageNumber);
    const Page* topbarPage() const;                         // nullptr until the top bar was opened (it is a popup the game opens)
    // exe:00450bd0 for the "topbar" page (verb 9, API 0x60): the first popup, kept across page jumps (header flag bit 3).
    // Returns false if it is already open (the original then just returns 1) or the data has no such page.
    bool openTopbar();
    bool topbarOpen() const { return topbar_ != nullptr; }
    bool hasTopbarPage() const { return topbarPre_ != nullptr; }
    const Page* popupPage() const;                          // nullptr if no popup is open

    // --- run-time hotspot state (API 0x03; reset whenever the page is entered again) ---
    // Enables / disables hotspots of `page` (the main page, the top bar or the popup) by name
    // (case-insensitive); prefix: every hotspot whose name starts with `name` (exe:00402860). Returns
    // false if no hotspot matched. *previous receives the old enabled state of an exact match.
    bool setHotspotEnabled(const Page& page, const std::string& name, bool enable, bool prefix, int* previous);
    bool hotspotEnabled(const Page& page, int index) const;
    // exe:00402a50: every hotspot of `page` enabled / disabled (the file's own disabled flag is replaced by the run-time one).
    void setAllHotspotsEnabled(const Page& page, bool enable);

    // --- palette / options ---
    void setTimeSlot(int slot);                             // TimeSlot
    int timeSlot() const;
    void setPaletteSetOverride(int set);                    // -1 = none, else palette set index
    int paletteSetOverride() const;
    int paletteSetCount() const;
    void setShowHidden(bool on);
    bool showHidden() const;
    const Palette& palette() const;                         // what is shown: forced > API palette > the page's
    // The game logic's palette (API 0x1a / 0x1c / 0x1d / 0x24, exe:00412630): from the first call on it is what
    // is shown and the provisional choice above (page tables + time slot) no longer changes anything, until
    // setTimeSlot / setPaletteSetOverride (the debug keys and options) explicitly override it.
    void setApiPalette(const Palette& p);
    void setKeyPalette(const Palette* p) { keyOverride_ = p; }   // exe:00412ab0 stash (nullptr = none)
    // Back to the page / time-slot palette (the logic's palette is no longer in charge), no re-bake.
    void clearApiPalette() { apiActive_ = false; }
    bool apiPaletteActive() const { return apiActive_; }
    // True once game logic is attached: ornaments are painted exactly as the page flags say (the provisional
    // "show the HEALTHY variant when everything is hidden" rule is off).
    void setGameLogicAttached(bool on, bool rebake = true);
    bool gameLogicAttached() const { return attached_; }
    // Debug facility: show this palette instead of the page's until the next page change (nullptr = off).
    void forcePalette(const Palette* p);
    const std::string& paletteFile() const;                 // file name as in the tables; "" if none
    PaletteSource paletteSource() const;
    bool paletteFellBack() const;                           // wanted palette missing, or default used
    u32 pageSerial() const { return pageSerial_; }          // changes whenever another page (or the saved one) is entered
    // The page's day palette (palette set group A, time slot day): the reference of the picture cleaner's threshold scale (picclean.h).
    // nullptr for a page with a fixed palette (menus, ...) or none.
    const Palette* dayPalette() const;

    // Draws top bar, the view cut from the page surface and any popup into f (not the bar, held object
    // or other back-buffer layers: Engine does those). Outlines: yellow = clickable verb, green = object
    // zone, red = not implemented.
    void render(Frame& f, bool outlines);
    u8 nearestIndex(int r, int g, int b) const;

    // --- page surface (spec 1.3 "draw order"): the whole current page, ornaments baked at load ---
    // --- the right-click return page (exe:004456b0 save, exe:004457a0 drop, exe:00445f80 restore) ---
    // A jump from a 2560 x 1024 page to a smaller one (zoom-in jump, Engine::zoomJump) saves the page being left with its surface:
    // requestSaveOnJump() sets DAT_004990fc, the next page jump saves (event 37, an older saved page goes first: event 38). A jump to a
    // page whose name does not start with F, A, N or U drops it (event 38). restoreSavedPage() puts it back without reloading it.
    void requestSaveOnJump(bool on = true) { saveNext_ = on; }
    bool hasSavedPage() const { return saved_ != nullptr; }
    std::string savedPageName() const;
    void dropSavedPage();                                   // exe:004457a0
    bool restoreSavedPage();                                // false if there is none; fires onLeave, no load events
    bool storedBigPage(int number);                         // page header width == 0xa00 or height == 0x400 (DoZoomInAndPageJump)
    // DAT_00499bf8 (API 0x69): the name of the page the jump in progress goes to; set before OnExitPage
    const std::string& destinationPageName() const { return destName_; }
    std::function<void()> onReturnSaved, onReturnDropped;   // events 37 / 38
    // Test aid (`dump keyed`): one line per visible keyed ornament and keyed DisplayImage paint of the main page: the palette index the
    // colour key resolved to (exe:00412530), how many pixels of the image carry that index, and how many of them show on the surface.
    std::string keyedReport() const;
    Surface& surface() { return surface_; }
    const Surface& surface() const { return surface_; }
    int findOrnament(const std::string& name) const;         // type-1 ornament of the current page, -1 if none
    const Image8& ornamentPixels(int ornament);              // current page
    int ornamentKey(int ornament) const;                     // palette index of its transparent colour or -1
    int viewTop() const { return fullScreen() ? 0 : viewTop_; }
    void setViewTop(int y) { viewTop_ = y; }   // API 0x5d (exe:00445810); the view is always 640 x 368, so only y moves

    // --- page images (API 0x0a 0x0b 0x5e 0x6e 0x28; TCPage methods exe:00402e00 00403390 00403250 00402d20 00401cc0) ---
    // window: -1 main page, 0 top bar, 1 popup. The ornament flags (bit0 hidden) and the table order are run-time
    // state, reset from the file whenever the page is entered again (the original reloads the page record).
    // Returns the API error code: 0, 3 no page in that window, 7 no such image / cannot paint.
    // DisplayImage paints the named ornament NOW onto the page surface (hidden or not) at pos (nullptr = its own
    // position), at most widthLimit pixels wide; objects are lifted off the surface first and put back after
    // (hooks below). wantSlot: also saves what the image covers into a slot (*slotOut, -1 = none free); with
    // keyedRestore and a transparent image only the pixels the image changed are restored on release.
    int displayImage(int window, const std::string& name, const int* pos, u32 widthLimit, bool wantSlot, int* slotOut, bool keyedRestore);
    // The page action "DisplayImage" (verb 7, exe:004031e0): the ornament is given by its number in the table.
    int displayImageNumber(int window, u32 index, const int* pos);
    int setImageVisibility(int window, const std::string& name, bool visible);
    int raiseImage(int window, const std::string& name);                 // moves the ornament record to the end of the table
    int releaseImageSlot(int window, int slot);                          // 0 or 0x27 / 3
    void redrawPage();                                                   // ReRender: visible ornaments over the surface
    std::function<void()> eraseObjects;                                  // put the objects' save-under back
    std::function<void()> repaintObjects;                                // paint them again (exe:004478a0 mode 1)
    std::function<void()> redrawObjects;                                 // paint them without erasing first (RedrawPage)
    int imageSlotsInUse() const;

    // --- drawing surfaces of the windows for GDI text (API 0x3a; canvas.h). window -1: the page surface (text stays until
    // something paints over it); 0 / 1 (top bar / popup): an overlay layer of the window's page, put over its ornaments
    // when composing and cleared when the page is entered again or an image is displayed over a pixel. `mask` (nullptr
    // for -1) is set for every pixel drawn; `owner` keeps the layer alive (null for -1). False if there is no page in that window.
    bool drawTarget(int window, Surface*& surface, std::vector<u8>*& mask, std::shared_ptr<void>& owner);

    // The surfaces that can hold GDI text right now and where they show on the 640 x 480 screen (engine/textplane.h, the
    // high-resolution text presentation): screen = surface + (dx, dy). `mapped` is false when the surface is shown through the
    // cylindrical projection (its text stays in the 8-bit frame). window as in drawTarget.
    struct TextSurface {
        Surface* surface = nullptr;
        std::vector<u8>* mask = nullptr;
        int window = -1;
        bool mapped = true;
        int dx = 0, dy = 0;
        const char* name = "page";
    };
    void textSurfaces(std::vector<TextSurface>& out);

    // Called before the current page is replaced / after the page surface was (re)built
    // (newPage = false for a rebuild caused by a palette or option change).
    std::function<void()> onLeave;
    std::function<void()> onLoading;                  // new page: surface allocated, ornaments not yet painted (OnPageLoaded)
    std::function<void(bool newPage)> onBaked;
    std::function<void(int w, int h)> onPopupOpened;   // popup page size
    std::function<void()> onPopupClosed;
    std::function<void()> onTopbarOpened;
    HotspotQuery hotspotQuery;

private:
    struct PageData;
    std::shared_ptr<PageData> load(int number);
    const Image8& image(PageData& pd, size_t i);
    void enter(int number, bool push, const int* scroll = nullptr);
    void resolvePalette();
    void dropPopup();
    void setPopup(std::shared_ptr<PageData> p);
    std::vector<size_t> visibleOrnaments(const PageData& pd) const;
    void drawOrnaments(Frame& f, PageData& pd, int originX, int originY, const Rect& clip);
    void drawLayer(Frame& f, const PageData& pd, int originX, int originY, const Rect& clip);
    void bake(bool newPage);
    PageData* pageDataOf(int window) const;
    void resetImageState(PageData& pd);
    void paintOrnaments(PageData& pd);                       // visible ornaments over the surface, then the DisplayImage paints
    void paintRec(Surface& s, const Image8& img, int x, int y, u32 widthLimit, int key);
    const Palette& paintPalette() const { return apiActive_ ? apiPal_ : pal_; }
    // exe:00412530: the transparent colour is looked up (first exact match) in the stash while a load is in progress, else in the
    // palette on display (DirectDraw GetEntries).
    const Palette& keyPalette() const { return keyOverride_ ? *keyOverride_ : paintPalette(); }
    void cutView(Frame& f) const;
    void outlinePage(Frame& f, const PageData& pd, int scrollX, int scrollY, int originY, const Rect& clip, bool wrap);
    HitInfo hitPage(const PageData& pd, int px, int py, bool wrap) const;
    HotspotOverride query(const PageData& pd, int index) const;
    HotspotOverride baseQuery(const PageData& pd, int index) const;
    bool tryLoadPalette(const std::string& rel);

    struct SavedPage {
        std::shared_ptr<PageData> pd;
        Surface surface;
        int sx = 0, sy = 0;
    };
    void saveForReturn();                                   // exe:004456b0
    struct HistoryEntry { int page, sx, sy; };

    DataFiles files_;
    std::unique_ptr<PageFile> pf_;
    PaletteTable table_;
    std::map<int, std::shared_ptr<PageData>> cache_;
    int viewTop_ = TopBarH;
    std::shared_ptr<PageData> cur_, topbar_, popup_, topbarPre_;   // topbarPre_: loaded, not opened yet
    std::vector<HistoryEntry> history_;
    std::map<std::pair<const Page*, int>, bool> hsDisabled_;   // run-time disabled flag per hotspot (API 0x03)
    int sx_ = 0, sy_ = 0;
    mutable Projection proj_;
    int slot_ = SLOT_DAY, override_ = -1, activeSet_ = -1;
    std::string activeFixed_;
    bool showHidden_ = false;
    Palette pal_, apiPal_;
    bool apiActive_ = false, attached_ = false;
    const Palette* keyOverride_ = nullptr;
    std::unique_ptr<SavedPage> saved_;
    bool saveNext_ = false;
    bool holdTrack_ = false;   // a page load: exe:00401050 fires the compass before the first render of the new page
    std::string destName_;
    struct Slot {
        bool used = false;
        int key = -1;
        u32 paintId = 0;
        std::vector<std::pair<Rect, std::vector<u8>>> parts;   // saved rectangles (one per panorama copy)
    };
    Slot slots_[16];
    u32 nextPaintId_ = 1;
    std::shared_ptr<Palette> forced_;
    std::string palFile_;
    PaletteSource palSrc_ = PaletteSource::Default;
    bool havePal_ = false;
    Surface surface_;
    mutable std::vector<u8> bare_;
    mutable ProjectedView projected_;
    u32 pageSerial_ = 0;
    mutable std::map<std::string, Palette> dayCache_;
};

} // namespace biosys
