// BioSys 4 terminal: the page object (`BS4Page`, 0x680 bytes in the original) and the registries through which later packets
// add page-specific code. Specification: analysis/notes/bs4.md sections 1.5, 1.6, 1.8, 1.13, 2 and 5 (packet P2).
//
// A Bs4Page is one of the three layers of the terminal (main page, embedded page, panel page; kind 1/2/3). It owns the parsed page
// record (ornaments, hotspots with their live flag bits), a lazily decoded image cache and its 8-bit page surface, which is filled
// with the transparent colour and painted from the visible type-1 ornaments in file order. The terminal (`Bs4`, bs4.h) composes
// the surfaces. The original addresses members by byte offset; here they are named, with the original offset in a comment.
//
// HOW A PANEL IS ADDED (packets P4..P7). One new source file `src/engine/bs4panel_<name>.cpp` (the Makefile picks it up); the worked,
// complete example that is also in the build is bs4panel_ah.cpp (exe:00424ca0). The shape:
//
//     #include "engine/bs4.h"
//     namespace biosys { namespace {
//     // The panel's Init (exe:00424ca0). `variant` = the lowest set bit among 3..14 of the panel's visible mask, else 24 (spec 1.13
//     // InitPanel); 0 for a panel of the second group (Bs4PanelDef::variants == false). The Init sets what the original's Init sets:
//     void initAh(Bs4Page& p, int variant) {
//         p.feature = "fs6htch";                       // +0xa4: the feature the widgets and getParam/setParam use
//         p.reloadName = "5_ahs2";                     // +0x188: hotspot name the 1 Hz timer re-opens the panel with when p.reload is set
//         p.owner().setHighlight("hiahs1", 5);         // +0x1bec / +0x1bf8: ornament painted on the main page while the panel is up, and the item bit that must be visible
//         u32 i; double d;
//         if (!p.getParam(p.feature, 10, i, d)) return;        // FEATURE_OPEN (event 27); logs "Failed to get the value ..." itself
//         p.showItem(0x11, i != 0); p.showItem(0x12, i == 0);   // exe:0041cab0 (items = hotspots and ornaments named "<n>_...")
//         p.handler = [&p](int phase) { /* 1 draw, 2 poll, 3 mouse: use the widgets of bs4widgets.h */ };    // +0xd8, optional
//     }
//     // The setter (exe:004323c0 style): called by InitPanel when a state-button bit of the visible mask changed, i.e.
//     // (newMask ^ oldMask) & 0xfeff8007; it sends the parameter (event 26) and keeps p.devOn / p.devActive:
//     void setOn(Bs4Page& p, u32 now, u32 old) { if ((now & 2) && !(old & 2)) p.setParam(p.feature, 1, 1); ... }
//     Bs4PanelRegistrar registrar("panelah", Bs4PanelDef{/*variants*/ true, initAh, /*setter*/ nullptr, /*onLoad*/ nullptr});
//     } }
//
// READING THE ORIGINAL: the decompile loses the stack arguments of exe:0041cab0 ShowItemsByIndex(n, show, &changed) (it shows only `n`); recover
// them with `python3 -I tools/bs4_calls.py <start> <length> 41cab0` (static disassembly) before porting an init or a handler.
//
// What is not obvious: (1) InitPanel runs after the page is loaded and again whenever verb 0x17 changed the panel's items, the first time
// as Init, later as setter. (2) Which variant bit is set at the first call is decided by the kind-3 half of exe:00409720 (packet P1b: the
// hotspot that opened the panel, Bs4::panelOpener(), selects the variant by showing one item of the panel); without it a variant
// panel logs "Failed to find a group". (3) A panel's 1 Hz poll is the handler with phase 2; a widget that sees the device state change
// sets p.reload = true and Bs4::onTimer re-opens the panel with a synthetic verb 0x1a (hotspot name p.reloadName). (4) Everything a
// handler draws goes onto p.surf (the panel's own surface); it must call p.repaintScreen() (or paint()) so that the terminal recomposes.
// (5) Feature name "" is the NULL feature of the original (world parameters). (6) In tests, Bs4Registry::setPageSpecific(false) turns all
// page-specific code off (static picture); bs4test registers test-only panels through Bs4Registry::addPanel / removePanel.
//
// The handler protocol (spec 2): `handler(phase)`; 0 = init (not used by the data), 1 = draw (called by every Paint, after the
// ornaments), 2 = poll (once per second from Bs4Page::tick of a panel page; read the parameter and redraw if it changed), 3 = mouse
// event: the page fields mouseEvent (1 move, 2 button up, 3 button down, 4 leave), mouseFlags (MK_*), mouseX/mouseY (page relative)
// are set and `mayContinue` is 1; the handler writes mayContinue = 0 to swallow the event (a hotspot is then not hit). A handler that
// changes a surface calls p.repaintScreen() (= +0x1b74 "needs repaint"). Widgets live in bs4widgets.h and work on the page fields.
// A page handler is a plain std::function: capture `&p` only if the page object outlives the handler (it does: the handler is reset
// by Bs4Page::unload and by every load).
//
// Page-specific code of the MAIN pages (P3, "main-page dynamics") registers a Bs4MainExt (below). Page hooks, passwords and
// sounds (P1b) register Bs4Hooks. Both through static registrars in their own source files, like panels.
#pragma once
#include "engine/common.h"
#include "engine/framebuffer.h"
#include "engine/image.h"
#include "engine/pagefile.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace biosys {

class Bs4;
class Bs4Page;

// A GDI RECT: -1,-1,-1,-1 = none. Fills use [left,right) x [top,bottom); hit tests (ornament / hotspot rects) are inclusive.
struct Bs4Rect {
    int left = -1, top = -1, right = -1, bottom = -1;
    bool none() const { return left == -1; }
    bool operator==(const Bs4Rect& o) const { return left == o.left && top == o.top && right == o.right && bottom == o.bottom; }
};

// The 0x44-byte action record DispatchAction receives (exe:0041c290 fills it from the hotspot; spec 1.6).
struct Bs4Action {
    Bs4Rect rect;                  // +0x00 hotspot rect (page relative)
    int button = 0;                // +0x10 button index (hotspot +0x14), 1..14 = click sound slot; only filled by MouseUp
    int verb = 0;                  // +0x14
    int arg[8] = {};               // +0x18..: a1 = arg[0]
    std::string name;              // +0x38 hotspot name
};

// One bar gauge (exe:00438910, spec 2): a text field and a bar image painted width-clipped. The main page has three (reservebar
// 0x3f3, powconbar 0x3f4, powgenbar 0x3f2: slots +0x1e0, +0x228, +0x270); panels keep extra slots.
struct Bs4GaugeSlot {
    double last = 0;               // +0x00 last value shown
    Bs4Rect text;                  // +0x08 text rect (-1: none)
    double min = 0, max = 0;       // +0x18, +0x20
    int image = -1;                // +0x28 image index of the bar ornament
    Bs4Rect bar;                   // +0x2c bar rect: the image is painted at its left/top
    int percent = 0;               // +0x3c pixel width of the bar (the original calls it percent: ftol(frac * 151 * 0.01 * 100))
    int paramId = 0;               // +0x40 feature parameter id
};

struct Bs4TimeSlider {             // exe:00436d60: FEATURE_TIME_LIMIT (5), one marker
    u32 hours = 0, minutes = 0;    // +0xd0, +0xd4
    Bs4Rect marker;                // +0x108
    bool dragging = false;         // +0x118
    Bs4Rect hoursText, minutesText;    // +0x128, +0x138
};
struct Bs4ScheduleSlider {         // exe:00437470: TIME_ACTIVATE (3) + DAILY_TIME_LIMIT (6), two markers
    u32 onHours = 0, onMinutes = 0, offHours = 0, offMinutes = 0;      // +0x4d8, +0x4dc, +0x4e0, +0x4e4
    Bs4Rect onText, offText;       // +0x4e8, +0x4f8
    bool dragOn = false, dragOff = false;                              // +0x508, +0x50c
    Bs4Rect markerOn, markerOff;   // +0x510, +0x520
};
struct Bs4RangeSlider {            // exe:00437e90: two values (param ids paramA / paramB)
    double valueA = 0, valueB = 0;     // +0x538, +0x540
    Bs4Rect textA, textB;          // +0x548, +0x558
    bool dragA = false, dragB = false; // +0x568, +0x56c
    int y = 0;                     // +0x570
    Bs4Rect markerA, markerB;      // +0x574, +0x584
    double scale = 1;              // +0x598 units per pixel
    int paramA = 0, paramB = 0;    // +0x5a0, +0x5a4
};

// ---- registries -------------------------------------------------------------------------------------------------------

// Per panel page name (case-insensitive). exe:0041d0a0 InitPanel selects by name; the first call after a load runs `init`, later
// calls with a changed mask run `setter`. variants = true: the init of the first group (spec 1.13: the panel's mask has a variant
// bit among 3..14 / 24); false: the second group (hint pages, one-variant panels), init(page, 0).
struct Bs4PanelDef {
    bool variants = true;
    std::function<void(Bs4Page&, int variant)> init;
    std::function<void(Bs4Page&, u32 newMask, u32 oldMask)> setter;
    std::function<void(Bs4Page&)> onLoad;      // called by Bs4Page::load for kind 3 after InitPanel (the `term*` pages: exe:0041e5f0)
};

// Main-page dynamics (P3): called at the places where exe:0041a580 / 0041baf0 / 0041c690 / 0041ddf0 call the P3 functions.
// `page` empty = every main page, else the page name (case-insensitive). All optional.
struct Bs4MainExt {
    std::string page;
    std::function<void(Bs4Page&)> afterLoad;   // exe:0041a580 after the gauge slots / world alarms (before painting): 00439890, 00439b60, 0043a5f0(0), 0043a920(0), 0043bc70(0), 0043ad00(0)
    std::function<void(Bs4Page&)> loaded;      // exe:0041a580 last step before the page becomes active: 0043b990(0)
    std::function<void(Bs4Page&)> paint;       // exe:0041baf0 steps (d) overlays, 004399b0, 00439d70, (e) mail ornaments, (f) emailin stamp
    std::function<void(Bs4Page&)> tick;        // exe:0041c690 (1 Hz): 004396a0, 00439a90, 0043b990(1), 0043a110, 0043a5f0(1), 0043a920(1), 0043bc70(1), 0043ad00(1)
    std::function<void(Bs4Page&)> shown;       // exe:0041ddf0 OnMainPageShown
    std::function<void(Bs4Page&, const Bs4Action&)> hotspotAction;   // exe:0041deb0, called by AfterAction (bs4hooks.cpp, exe:0040a760) after every successful verb unless the hotspot name starts with "tech"
};

// P1b: page hooks, passwords. All optional; `Bs4` calls them where the original does.
struct Bs4Hooks {
    std::function<void(Bs4&, const std::string& pageName)> onMainPageLoaded;                 // exe:004090a0
    std::function<void(Bs4&, const std::string& pageName, int kind)> onPageLoaded;           // exe:00409720 (the kind-1 saved-mask restore is done by Bs4 itself)
    std::function<void(Bs4&, const Bs4Action&)> afterAction;                                 // exe:0040a760
    std::function<void(Bs4&)> updateHover;                                                   // exe:0040acf0
    std::function<void(Bs4&)> submitPassword;                                                // exe:0040bb30 (Enter in text entry mode)
    std::function<void(Bs4&)> drawPasswordText;                                              // exe:0040c0f0
};

class Bs4Registry {
public:
    static void addPanel(const std::string& page, Bs4PanelDef def);     // replaces an earlier definition of the same name
    static void removePanel(const std::string& page);
    static const Bs4PanelDef* findPanel(const std::string& page);
    static void addMainExt(Bs4MainExt ext);
    static const std::vector<Bs4MainExt>& mainExts();
    static Bs4Hooks& hooks();
    // false: nothing page-specific runs (no panel init / setter / onLoad, no main extension, no hooks): the static picture of the
    // page records (tests). Default true.
    static void setPageSpecific(bool on);
    static bool pageSpecific();
};

struct Bs4PanelRegistrar {                    // static registrar: `Bs4PanelRegistrar reg("panelah", def);`
    Bs4PanelRegistrar(const std::string& page, Bs4PanelDef def) { Bs4Registry::addPanel(page, std::move(def)); }
};
struct Bs4MainExtRegistrar {
    explicit Bs4MainExtRegistrar(Bs4MainExt e) { Bs4Registry::addMainExt(std::move(e)); }
};

// ---- the page object --------------------------------------------------------------------------------------------------

class Bs4Page {
public:
    enum Kind { kMain = 1, kEmbedded = 2, kPanel = 3 };
    Bs4Page(Bs4& owner, Kind kind);
    Bs4& owner() { return owner_; }
    const Bs4& owner() const { return owner_; }

    // ---- loading (exe:0041a580 Load, 0041c200 Unload, 0041c280 IsActive, 0041cbc0 GetPageNumber)
    // Reads page record `pageNo` of the terminal's BIOSYS4.DAT, runs the loaded-hook, InitPanel (kind 3), the main-page gauge setup
    // and the P3 extensions, paints the page surface. Throws Error if the page cannot be used (a main page must be 640x480).
    void load(int pageNo);
    void unload();
    bool isActive() const { return active_; }                       // +0x10
    bool getPageNumber(int& n) const { if (loaded_) n = number_; return loaded_; }   // 0041cbc0: 1 while a record is loaded
    int number() const { return number_; }
    bool loaded() const { return loaded_; }
    const std::string& name() const { return name_; }
    int width() const { return width_; }
    int height() const { return height_; }
    Kind kind() const { return kind_; }

    // The painted surface (page-sized, transparent colour where nothing was painted).
    Surface surf;

    // ---- painting (exe:0041baf0 Paint, 0041e040 PaintImage)
    bool paint();
    // PaintImage: image `imgIdx` of this page at (x,y) on the surface. transparent: key colour `key` (B,G,R of the ornament) is looked
    // up in the terminal palette (first exact match; if there is none the image is painted opaque). widthLimit < 0: all; otherwise
    // only that many columns (0 paints nothing and succeeds). outRect = the full image rectangle. Returns false if the page is not
    // loaded or the index is out of range.
    bool paintImage(int imgIdx, int x, int y, bool transparent, Color key, int widthLimit, Bs4Rect* outRect);
    // exe:0041cd00: paint the (even hidden) type-1 ornament `name` at its own position (pos null) or at *pos, opaque or with its own
    // key (flags bit4), width-clipped (widthLimit < 0: all). false if there is no such ornament.
    bool paintOrnamentByName(const std::string& name, const int* pos, int widthLimit);
    const Image8& image(int imgIdx);           // decoded image (cached)

    // ---- items by index (spec 1.6): names beginning "<n>_" are item n (0..31)
    bool showItems(int n, bool show, bool* changed = nullptr);   // exe:0041cab0: hotspots and type-1 ornaments; *changed set when an ornament flipped
    bool showItem(int n, bool show) { bool c = false; return showItems(n, show, &c); }
    bool visibleMask(u32& mask) const;                           // exe:0041ce00 (false if not loaded)
    u32 visibleMask() const { u32 m = 0; visibleMask(m); return m; }
    bool applyMask(u32 mask, bool* changed = nullptr);           // exe:0041cf40: item n shown if bit n set, else hidden, n = 0..31
    bool setOrnamentVisible(const std::string& name, bool show); // exe:0041dcd0 (first type-1 ornament of that name)
    bool setHotspotEnabled(const std::string& name, bool enabled);   // exe:0041dd60
    bool hotspotRect(const std::string& name, Bs4Rect& r, bool& enabled) const;   // exe:0041cf90
    bool hoverName(std::string& out) const;                      // exe:0041cc90 (empty when nothing is hovered)
    bool hitVisibleOrnament(int x, int y) const;                 // exe:0041cbe0: a visible painted ornament covers the point
    const Bs4Rect& ornamentRect(size_t i) const { return ornRect_.at(i); }   // +0x38: where ornament i was painted (-1 = not painted)
    int hoverIndex() const { return hover_; }                    // +0x2c

    // ---- input (exe:0041c290 MouseMove, 0041c470 MouseUp, 0041c580 MouseDown, 0041d070 MouseLeave). Coordinates are page relative.
    // Each returns true when `act` was filled and must be dispatched (up: the hovered hotspot; move/down: only an auto-repeat verb
    // 0x19). move: hit = a hotspot was hit; sets the cursor kind of the terminal (0 arrow, 2 hand).
    bool mouseMove(u32 flags, int x, int y, Bs4Action& act, bool& hit);
    bool mouseUp(u32 flags, int x, int y, Bs4Action& act);
    bool mouseDown(u32 flags, int x, int y, Bs4Action& act);
    void mouseLeave();
    void tick();                       // exe:0041c690: once per second (the caller calls it every 500 ms; it checks the clock)
    bool pendingReload(std::string& hotspotName) const;          // exe:0041dc70 (kind 3): reload flag +0x184 and its name +0x188
    // exe:0041d0a0: panel initialisation / state-button setters (kind 3); called after load and by verb 0x17.
    void initPanel();

    // ---- feature access (events 27 / 26 of the game logic). The name "" is NULL in the original (world parameters).
    bool getParam(const std::string& featureName, int id, u32& outInt, double& outValue);   // logs a failure like the original
    bool setParam(const std::string& featureName, int id, int a, int b = 0, int c = 0);
    bool setParamValue(const std::string& featureName, int id, double value);   // event 26 with (0, low dword, high dword) of the double
    void repaintScreen();              // owner.needRepaint(): +0x1b74 = 1
    // Text and fills on the page surface (exe:004124e0 colour fill; GDI DrawTextA with the terminal font, transparent background).
    void fill(const Bs4Rect& r, int paletteIndex);
    void drawText(const Bs4Rect& r, const std::string& text, int red, int green, int blue, unsigned dtFlags = 0x25);

    // ---- state (named after the original offsets)
    std::string feature;               // +0xa4: feature name of the panel's device (21 chars)
    std::string feature2;              // +0xb9: second feature name
    std::function<void(int phase)> handler;      // +0xd8
    // mouse event for the handler (phase 3)
    u32 mouseFlags = 0;                // +0x148
    int mouseX = 0, mouseY = 0;        // +0x14c, +0x150
    int mouseEvent = 0;                // +0x154: 1 move, 2 button up, 3 button down, 4 leave
    int mayContinue = 1;               // +0x158: the handler writes 0 to swallow the event
    bool cached = false;               // +0x9c InitPanel has run
    u32 cachedMask = 0;                // +0xa0 visible mask at the last InitPanel
    bool devOn = false;                // +0x15c FEATURE_ON as last seen
    bool devActive = false;            // +0x160 FEATURE_ACTIVE as last seen
    bool devOpen = false;              // +0x164 FEATURE_OPEN as last seen (written by the lock panel, exe:00428b80 / 004326e0)
    u32 alarmBits = 0;                 // +0x174 cached FEATURE_ALARMS bits (AlarmBitIcon)
    bool reload = false;               // +0x184 "state changed, reload me"
    std::string reloadName;            // +0x188 hotspot name that re-opens the panel
    int loopSound = -1;                // +0x678 loop sound 1..7, -1 none
    // images registered by name at load (exe:0041a580): ornament image index, -1 = absent
    int markOn = -1, markOff = -1, dangerLev = -1, litLev = -1, broke = -1, litBroke = -1;   // +0xdc, +0xe0, +0xe4, +0xec, +0xf8, +0xfc
    int levImage = -1; int levPos[2] = {0, 0};      // +0xe8 / +0xf0: the ornament named "lev" (image index and position)
    int brokePos[2] = {0, 0};          // +0x100
    int ewaitImage = -1, ewaitOffImage = -1, ewaitPos[2] = {0, 0};   // +0x80, +0x84, +0x88
    int numailImage = -1, numailPos[2] = {0, 0};                      // +0x90, +0x94
    // main page
    int deviceRow = -1;                // +0x54 row of the device-highlight table (P3)
    int alarmRow = -1;                 // +0x58 row of the alarm-probe table (P3)
    int probeSensor = -1;              // +0x5c current probe sensor (saved)
    int probeRow = -1;                 // +0x60 probe-page row (saved)
    double probeLast = 0;              // +0x68
    int phPos[2] = {0, 0};             // +0x70 position of ornament phbar
    Surface phSurface;                 // +0x78 pH bar surface (P3)
    int counter = 0;                   // +0x4c second counter
    u32 lastTick = 0;                  // +0x50 tick time of the last once-per-second tick
    bool gaugePresent[3] = {false, false, false};    // +0x40..+0x48
    Bs4GaugeSlot gauge[3];             // +0x1e0, +0x228, +0x270
    // panels
    Bs4GaugeSlot extraGauge[4];        // +0x3d8 .. (BarGaugeSlotB/C/D: exe:00438ca0, 00438d90, 00438e80) - further slots as the panels need
    std::string tierName;              // +0x4c4 current tier ornament name
    int tierImage = -1; int tierPos[2] = {0, 0};     // +0x4b0, +0x4b4
    Bs4TimeSlider timeSlider;
    Bs4ScheduleSlider schedule;
    Bs4RangeSlider range;
    int dragOffset = 0;                // +0x11c
    int dragStartX = 0;                // +0x120
    int sliderY = 0;                   // +0x124 y of the marker row

    // The parsed record; flags of ornaments (bit0 hidden) and hotspots (bit0 disabled) are the live state.
    Page rec;

private:
    friend class Bs4;
    void releaseAll();
    void initMainGauges();
    void paintMainExtras();
    int findOrnament(const std::string& name) const;     // first type-1 ornament, case-insensitive; -1
    bool inited_ = false;
    Bs4& owner_;
    Kind kind_;
    bool loaded_ = false, active_ = false;
    int number_ = 0, width_ = 640, height_ = 480;
    std::string name_;
    std::vector<Bs4Rect> ornRect_;
    int hover_ = -1;
    std::vector<Image8> images_;
    std::vector<char> imageLoaded_;
};

// Case-insensitive helpers used by the page code and the panels.
bool bs4Iequals(const std::string& a, const std::string& b);
bool bs4IstartsWith(const std::string& s, const char* prefix);

} // namespace biosys
