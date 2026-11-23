// BioSys 4 ("BS4"): the engine's built-in full-screen computer terminal over BIOSYS4.DAT / BIOSYS4.PAL. This is `TCBioSys4`
// (exe:00404a80 .. exe:004105c0, spec analysis/notes/bs4.md sections 1 and 5, packet P1a) without the engine: everything it needs from
// outside goes through Bs4Host, so that it runs headless (src/tools/bs4test.cpp).
//
// What Bs4 does: owns the three page layers (bs4page.h) and the 640x480 back buffer; loads main pages by number (verb 0x12) and panel
// pages (verb 0x1a); composes (exe:004067f0): white fill, main surface keyed with the transparent colour, hover frame, panel surface,
// the two clock fields; draws the software cursor; dispatches the verbs 0x11..0x1b (exe:00406bd0); turns mouse / key / timer / idle
// entry points into page hit tests; writes and restores the 0x480-byte save section. The page hooks, passwords and sounds (P1b) hang
// in through Bs4Hooks (bs4page.h) and the Bs4Host sound calls.
//
// ENGINE GLUE (what a later step has to connect; addresses of the original in brackets):
//   enter:    verb 15 of PAGES.DAT (exe:0040d270 -> exe:004104e0 EnterBioSys4Mode) and load game with header +0x60 == 1 (exe:00452b70):
//             unload the game page (event 3), host event 18(0), then `Bs4 b(host, files, session); b.initialise(section or null)`.
//   leave:    Bs4Host::leave() is called for verb 0x18 and from the click handlers (exe:004105c0 LeaveBioSys4Mode(-1)): destroy Bs4,
//             restore the saved palette and the game page (events 7, 8, 22, 2), host event 18(1). WM_DESTROY, restart (exe:00453da0)
//             and load of a save without a BS4 section do the same.
//   input:    while a Bs4 exists, WM_LBUTTONDOWN/UP -> onLButtonDown/Up (exe:00443110/004432f0), mouse move -> onMouseMove
//             (exe:00442d87), WM_KEYDOWN/UP -> onKeyDown/onKeyUp (exe:00443930/00443a70), the 500 ms WM_TIMER -> after event 4,
//             onTimer (exe:004437d0), TCApp::OnIdle -> onIdle then event 17 (exe:00416840), RedrawScreen -> redraw (exe:00411b00,
//             exe:00411a40); the right button is ignored.
//   save:     exe:00452410 SaveGame writes header +0x60 = 1 and u32 0x480 + saveSection() when a Bs4 exists; load: initialise(section).
//   palette:  the engine's API 0x0e/0x0f palette commands act on the *saved game palette* while Bs4 is up (exe:004106f0/00410730).
#pragma once
#include "engine/bs4fixes.h"
#include "engine/bs4page.h"
#include "engine/canvas.h"
#include "engine/datafiles.h"
#include "engine/font.h"
#include "engine/framebuffer.h"
#include "engine/pagefile.h"
#include <memory>

namespace biosys {

// Everything BS4 needs from the engine. Defaults do nothing (so a host overrides what it supports).
class Bs4Host {
public:
    virtual ~Bs4Host() = default;
    // ---- the game logic (gamelogic.h): the four events BS4 uses. A NULL feature name of the original arrives as "".
    virtual void onBS4Screen(int leaving) {}                                        // event 18 (the engine glue sends it)
    virtual bool onBS4Query(int queryId, void* buffer) { return false; }            // event 19: 7 / 0xb = clock text into buffer (char[512])
    virtual bool onSetFeatureParameter(const std::string& featureName, int paramId, int a, int b, int c) { return false; }   // event 26
    virtual bool onGetFeatureParameter(const std::string& featureName, int paramId, u32& outInt, double& outValue) { return false; }   // event 27
    virtual void onKeyDown(int vkey, int repeat, int flags) {}                      // event 14 (doDefault ignored by BS4)
    virtual void onKeyUp(int vkey, int repeat, int flags) {}                        // event 15
    // ---- screen
    virtual void setPalette(const Palette& pal) {}                                  // IDirectDrawPalette::SetEntries(0, 256) at once
    virtual void present(const Frame& frame) {}                                     // the composed 640x480 frame incl. the software cursor (flip)
    virtual void clipPointer(const Bs4Rect* rect) {}                                // ClipCursor(rect) while a slider marker is dragged; null releases
    virtual void requestMouseMove() {}                                              // exe:00451c50/00451d50: flush mouse messages and fake a move (the glue calls onMouseMove at the pointer)
    // ---- time and state
    virtual u32 tickCount() = 0;                                                    // GetTickCount (ms)
    virtual bool appActive() { return true; }                                       // DAT_0049a06c: DispatchAction does nothing while the app is not active
    virtual void leave() {}                                                         // LeaveBioSys4Mode(-1): verb 0x18 (the owner destroys the Bs4)
    virtual void requestClose() {}                                                  // PostCloseToMainWnd: a main page failed to load
    virtual void log(const std::string& message) {}                                 // ReportMessage
    // ---- sounds (P1b: exe:0040c1e0 PreloadClicks, 0040c310 PlayClick, 0040c4b0 StartLoop, 0040c660 StopLoop, 0040c6a0 WelcomeCallback).
    // Files: <data>\bs4sound\<name>.wav; click slots 1..14, loops 1..7 (spec 1.3). Bs4 calls playClick for a hotspot button index
    // 1..14 in DispatchAction and start/stopLoop from the active indicator and page unload.
    virtual void playClick(int slot) {}
    virtual void startLoop(int loop) {}
    virtual void stopLoop(int loop) {}
    virtual void playWelcome(std::function<void()> whenDone) { if (whenDone) whenDone(); }
};

// The state that outlives one visit to the terminal (the engine keeps it in the game header, spec 1.12): the three password flags
// (DAT_00499080, DAT_00497a20, DAT_004990e8), the mail-arrival state DAT_004979f8 and its text DAT_00497970.
struct Bs4Session {
    bool initialPassword = false;      // RUSSELL entered: the first page is menuscreen instead of initial
    bool emailPassword = false;        // CYBERSPHERES: email1 is reachable (verb 0x12 remaps it to emailpw otherwise)
    bool synthPassword = false;        // GREENKING: synth1 likewise (-> synthpw)
    u32 mailState = 0;                 // 0 none, 1 story mail arrived, 2 read
    std::string mailStamp;             // clock text at arrival
};

struct Bs4Options {
    std::string pageFile = "BIOSYS4.DAT";     // names resolved through DataFiles::requireData
    std::string paletteFile = "BIOSYS4.PAL";
    Bs4Fixes fixes;                           // the terminal's remaster switch (bs4fixes.h); all false = the original
};

class Bs4 {
public:
    static constexpr int kSaveSize = 0x480;

    // exe:00404a80 Construct + exe:00404d10 Initialise: loads palette, cursors, page file, font; with `section` (0x480 bytes, size
    // checked) restores a saved session, else opens `menuscreen` (session.initialPassword) or `initial`. Throws Error on failure.
    Bs4(Bs4Host& host, const DataFiles& files, Bs4Session& session, const u8* section = nullptr, size_t sectionSize = 0,
        const Bs4Options& options = Bs4Options());
    ~Bs4();                                                              // exe:00404b90 Destroy (stops the loops)
    Bs4(const Bs4&) = delete;
    Bs4& operator=(const Bs4&) = delete;

    // ---- pages and actions
    bool loadMainPage(int pageNo);                                       // exe:004065f0 (verb 0x12); false on failure (host.requestClose is the caller's choice)
    int pageNumber(const std::string& name) const;                       // 0 if there is none
    bool dispatchAction(Bs4Action& act);                                 // exe:00406bd0; false = failed verb
    Bs4Page& mainPage() { return main_; }
    Bs4Page& panelPage() { return panel_; }
    const Bs4Page& mainPage() const { return main_; }
    const Bs4Page& panelPage() const { return panel_; }
    Bs4Page& embeddedPage() { return embedded_; }     // never active (the layer is not ported: spec section 6)
    bool panelActive() const { return panel_.isActive(); }
    const std::string& currentMainName() const { return currentName_; }  // +0x18dc
    const std::string& lastHotspot() const { return lastHotspot_; }      // +0x18e7: name of the hotspot of the last dispatched action
    const std::string& panelOpener() const { return panelOpener_; }      // +0x1b9c: name of the hotspot that opened the panel
    const Bs4Rect& panelRect() const { return panelRect_; }              // +0x17b4

    // ---- input and timing (exe:00405ed0 / 00406210 / 00406390 / 00405ce0 / 00405e90 / 00406520 / 00406fe0 / 004065d0)
    void onMouseMove(u32 flags, int x, int y);
    void onLButtonUp(u32 flags, int x, int y);
    void onLButtonDown(u32 flags, int x, int y);
    void onKeyDown(int vkey, int repeat, int flags);
    void onKeyUp(int vkey, int repeat, int flags);
    void onTimer();                                                      // every 500 ms
    int onIdle();                                                        // recompose when needed; returns 0
    void redraw();                                                       // exe:004065d0: recompose and flip

    // ---- compose / flip (exe:004067f0, exe:00411810)
    bool compose();
    void flip();                                                         // back buffer + software cursor -> host.present
    const Surface& backBuffer() const { return back_; }
    Frame frame() const;                                                 // back buffer with the cursor, as flip() presents it
    // High-resolution text presentation (engine/textplane.h): the frame of the last flip() without the GDI text (kept only while text
    // tracking is on), and the surfaces that hold text with their screen offsets.
    const Frame& lastFlippedTextFree() const { return free_; }
    struct TextSurface { Surface* surface; int dx, dy; const char* name; };
    void textSurfaces(std::vector<TextSurface>& out);
    void needRepaint() { needsRepaint_ = true; }                         // +0x1b74
    bool needsRepaint() const { return needsRepaint_; }

    // ---- save section (exe:00407000 / exe:00407330); 0x480 bytes
    std::vector<u8> saveSection();
    bool loadSection(const u8* section, size_t size);
    bool loadingGame() const { return loadingGame_; }                    // +0x1b98
    // exe:00407720: the saved probe sensor index / probe-page row while loading a game
    bool loadedProbeState(u32& sensor, u32& row) const;
    u32 savedMask() const { return savedMask_; }                         // +0x1bfc

    // ---- shared state of the terminal that pages and panels use
    Bs4Host& host() { return host_; }
    const Palette& palette() const { return palette_; }
    const u8* paletteFlags() const { return paletteFlags_; }
    struct Colours { int white = 0, offWhite = 0, transparent = 0, grey = 0, red = 0, green = 0, lightGrey = 0; };
    const Colours& colours() const { return col_; }                      // +0x1898 .. +0x18b0
    Bs4Session& session() { return session_; }
    const Bs4Fixes& fixes() const { return fixes_; }
    void setHighlight(const std::string& ornamentName, int maskBit) { highlightName_ = ornamentName; highlightBit_ = maskBit; }   // +0x1bec / +0x1bf8
    const std::string& highlightName() const { return highlightName_; }
    int highlightBit() const { return highlightBit_; }
    bool hoverFramesOff() const { return hoverFramesOff_; }              // +0x1bac
    void setHoverFramesOff(bool v) { hoverFramesOff_ = v; }
    void setHoverFrame(const Bs4Rect& r) { hoverFrame_ = r; }            // +0x1b88: drawn as a 1-pixel box over the main surface (-1 none)
    const Bs4Rect& hoverFrame() const { return hoverFrame_; }
    std::string& hoverName() { return hoverName_; }                      // +0x1b78 cache for UpdateHover
    void setCursorKind(int k) { cursorKind_ = k; }                       // +0x18d4: 0 arrow, 2 hand
    int cursorKind() const { return cursorKind_; }
    void clipToPanelRect();                                              // ClipCursor(+0x17b4) while a slider marker is dragged
    void releasePointer();                                               // ClipCursor(NULL)
    void loopStart(int loop) { if (loop > 0) host_.startLoop(loop); }
    void loopStop(int loop) { if (loop > 0) host_.stopLoop(loop); }
    // page numbers looked up at initialise: email1, emailpw, synth1, synthpw (+0x1bdc..+0x1be8; 0 if missing)
    int pageEmail1() const { return pageEmail1_; }
    int pageEmailPw() const { return pageEmailPw_; }
    int pageSynth1() const { return pageSynth1_; }
    int pageSynthPw() const { return pageSynthPw_; }
    // world alarm bits (DAT_0049bc60 / DAT_0049bc5c), set at main-page load and by the 1 Hz tick; the alarm sound hook is a stub (exe:00439330)
    bool worldAlarm0 = false, worldAlarm1 = false;
    // text entry mode (+0x1bb0..+0x1bd8) for the password pages (P1b draws the field and submits)
    bool textEntry = false;
    std::string textBuffer;                                              // +0x1bb4, at most 16 characters
    Bs4Rect textRect;                                                    // +0x1bc8
    int textBackground = 0;                                              // +0x1bd8 palette index
    // clock fields
    const std::string& clockLeft() const { return clockLeft_; }
    const std::string& clockRight() const { return clockRight_; }
    void updateClockText(bool force, bool flipAfter);                    // exe:00408680

    // ---- text and fills
    int fontHandle() const { return fontHandle_; }
    void fillRect(Surface& s, const Bs4Rect& r, int index);              // exe:004124e0
    void drawText(Surface& s, const Bs4Rect& r, const std::string& text, int red, int green, int blue, unsigned dtFlags);
    Canvas canvasFor(Surface& s);
    const PageFile& pageFile() const { return *pf_; }
    void log(const std::string& m) { host_.log(m); }

private:
    friend class Bs4Page;
    bool displayPanel(const Bs4Action& a);                               // exe:004080b0
    bool showHideItems(const Bs4Action& a);                              // exe:00407c40
    void closePanelPage();                                               // verb 0x1b body
    void loadPalette(const DataFiles& files, const Bs4Options& o);       // exe:004084a0
    void findColours();
    void loadCursors(const DataFiles& files);
    bool loadSectionImpl(const u8* section, size_t size);

    Bs4Host& host_;
    Bs4Session& session_;
    Bs4Fixes fixes_;
    std::unique_ptr<PageFile> pf_;
    Palette palette_;
    u8 paletteFlags_[256] = {};
    Colours col_;
    Bs4Page main_, embedded_, panel_;
    Surface back_;
    Frame free_;
    void composeLayers(Surface& dst) const;
    Frame frameOf(const Surface& back) const;
    Frame frameTextFree();
    Bs4Rect panelRect_;
    std::string currentName_, lastHotspot_, panelOpener_;
    std::string clockLeft_, clockRight_;
    std::string highlightName_; int highlightBit_ = 0;
    bool hoverFramesOff_ = false;
    Bs4Rect hoverFrame_;
    std::string hoverName_;
    bool needsRepaint_ = false;
    bool loadingGame_ = false;
    u32 savedMask_ = 0, savedProbe_ = 0, savedProbeRow_ = 0;
    int pageEmail1_ = 0, pageEmailPw_ = 0, pageSynth1_ = 0, pageSynthPw_ = 0;
    // cursor
    Image8 arrow_, hand_;
    int cursorKind_ = 0;
    bool cursorDrawn_ = false;                                           // +0x18d0
    int cursorX_ = 0, cursorY_ = 0;
    // font
    FontLibrary fonts_;
    int fontHandle_ = 0;
    u32 lastMouseFlags_ = 0;
};

} // namespace biosys
