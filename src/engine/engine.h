// Engine: the game engine core (no SDL). Owns the data files, the page view (page surface model), the
// game objects, the status bar, cursor selection and the scroll modes, and composes the 640x480 frame:
// page surface view (page-relative sprites and movies are painted into the surface) -> synth movies ->
// held object -> fixed sprite animations -> wearing overlays -> bar (spec 1.3 / 2.4 draw order). Also
// owns the media systems (mixer, sounds, sprite animations, movies) and the blocking object-animation
// player. All time comes in through update(nowMs): no wall-clock calls here.
#pragma once
#include "engine/bmp.h"
#include "engine/bs4.h"
#include "engine/canvas.h"
#include "engine/controlprogram.h"
#include "engine/datafiles.h"
#include "engine/engineapi.h"
#include "engine/gameobjects.h"
#include "engine/gamelogic.h"
#include "engine/media.h"
#include "engine/pageview.h"
#include "engine/savefile.h"
#include "engine/statusbar.h"
#include "engine/touchinfo.h"
#include <array>
#include <functional>
#include <memory>

namespace biosys {

enum class Button { Left, Right };

class Engine {
public:
    // dataDir: the port's data/ directory (palette tables). Loads pages, objects, touchinf. logic and
    // anim may be null (NullGameLogic / no animations).
    Engine(const DataFiles& files, const std::string& dataDir, GameLogic* logic = nullptr, BlockingAnimPlayer* anim = nullptr);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // The game logic receiving the 46 events (nullptr = none). It is wrapped in the engine's ControlProgram,
    // which keeps the API restriction mask, the OnTimer state and the default return values.
    void setLogic(GameLogic* l);
    // nullptr = the engine's own player (sprite system + pump loop, spec 1.10 / 2.8).
    void setAnimPlayer(BlockingAnimPlayer* p);

    // --- game-logic interface (gamelogic.h = engine -> logic, engineapi.h = logic -> engine) ---
    ControlProgram& control() { return cp_; }
    // The API the game logic calls (the original's SendMessage(hwnd, 0x8000, code, block)); refused with error
    // 10 while an event runs whose restriction mask forbids the command; commands the engine does not support
    // yet return kApiNotImplemented.
    EngineApi& api() { return *api_; }
    // Sends OnStartup (before the first page); returns its result: 0 = the front end should now start the
    // game itself (startNewGame), as the original jumps to its start page. The top bar is not opened here: in
    // the original it is a popup the game opens (API 0x60 or the intro page's xTopBar hotspot, verb 9); startNewGame()
    // opens it when the game is started without the intro page.
    int startup();
    void shutdown();                  // sends OnShutdown (once)
    bool quitRequested() const { return quit_; }   // API 0x02 was called
    // Calls of API command `code` so far (all of them, implemented or not), and how many of those returned
    // kApiNotImplemented.
    int apiCalls(int code) const;
    int apiNotImplementedCalls(int code) const;
    // Sink for API 0x09 WriteLog (the engine log file); default: lines are only kept in lastLogLine().
    std::function<void(const std::string&)> logSink;
    const std::string& lastLogLine() const { return lastLog_; }
    // Window ids of the API: -1 main window, 0 = top bar, 1 = the popup (the original numbers popups in
    // order of opening and the top bar is the first one; one popup at a time is supported here).
    static constexpr int kMainWindow = -1, kTopBarWindow = 0, kPopupWindow = 1, kSimStatusWindow = -5;
    const Page* pageOfWindow(int window) const;      // nullptr if there is no page in that window
    // GDI text and lines (API 0x38 .. 0x3b): the registered fonts, and the device contexts that are out. A DC is a Canvas on the
    // window's page surface; EngineApi::canvas(handle) finds it.
    FontLibrary& fonts() { return fonts_; }
    const FontLibrary& fonts() const { return fonts_; }

    // --- media (the media systems the API commands 0x14.. work on) ---
    Mixer& mixer() { return mixer_; }
    SoundSystem& sounds() { return sounds_; }
    SpriteSystem& sprites() { return sprites_; }
    MovieSystem& movies() { return movies_; }
    const DataFiles& files() const { return files_; }
    // Host callback of the blocking animation loop: render + present one frame, handle events (only
    // quit matters), and return the current time in nowMs. Returns false to abort the animation (quit).
    using PumpFn = std::function<bool(u32& nowMs)>;
    void setPump(PumpFn f) { pump_ = std::move(f); }
    // Length of one idle pass in ms (the scroll step of exe:00444560 and the mouse-delta decay run once per pass). Default 16, which
    // the headless scripts rely on; the window sets it to its pass interval (app/pacing.h, --pass-ms).
    void setPassMs(u32 ms) { passMs_ = ms < 1 ? 1 : ms; }
    u32 passMs() const { return passMs_; }
    // false (default): each mouse motion replaces the pending scroll delta (exe:00445b70). true: motions add up until the next scroll step
    // takes them, so a window that sees several motion events per pass (display rate above the pass rate) loses none of the distance.
    void setScrollAccumulate(bool on) { accumDelta_ = on; }
    // For the pump: when non-zero the turn that is being asked for is a clock wait of at most this many ms (the delay of a palette
    // fade step, the busy wait of a journey), not a whole pass. A pump may return earlier than a full pass interval then.
    u32 pumpMaxMs() const { return pumpMaxMs_; }
    // The original's busy wait on GetTickCount (e.g. the journey's FUN_10007a40): runs host turns until `ms` have passed on the pump's clock,
    // input ignored. Without a pump it returns at once (nothing to wait for).
    void waitBlocking(u32 ms);
    bool blocking() const { return blocking_; }          // a blocking animation is running: input ignored
    std::function<void(const ObjectAnim&, bool hideHeld, bool priority)> animTrace;   // called per blocking play
    // App (de)activation: silences / resumes sounds and movies, suspends the media passes (spec 2.7, 4.6).
    void setMediaPaused(bool paused);
    bool mediaPaused() const { return mediaPaused_; }
    void stopAllMedia();                                 // closes every sound, sprite animation and movie
    // Debug triggers of the front end. Movie: opened and played centred in the view (page coordinates,
    // clamped to the page); the palette file (BIODATA-relative, "" = keep the current one) is shown
    // until the next page change. Returns the movie handle or -1.
    int debugPlayMovie(const std::string& file, const std::string& paletteFile);
    // Sprite at a view point (screen coordinates); pageRelative: x,y become page coordinates. -1 on failure.
    int debugPlaySprite(const std::string& file, int screenX, int screenY, bool pageRelative);

    // --- saved games, restart, the menu, text input, the start-up flow (analysis/notes/menus_saveload.md) ---
    // The directory of the saved games ("Saved Game N.sav", written there; DAT_00497a28 = the original's current directory).
    // API 0x55 returns it. Empty: nothing can be saved (error 0x1c). NewGame.sav is found there, else through
    // setNewGamePath, else in the data roots; the data roots are never written.
    void setSaveDirectory(const std::string& dir) { saveDir_ = dir; }
    const std::string& saveDirectory() const { return saveDir_; }
    // API 0x34 / 0x35 / 0x53 bodies (exe:00452410 / exe:00452b70 / exe:00453da0): 0 = done, else the original's error code
    // (save 0x1c cannot open, 0x1d write failed, 0x1e other; load 0x1f cannot open, 0x20 read failed, 0x21 other, 0x22 invalid file;
    // restart 0x11). The messages the original logs / shows go to reportSink and lastReport().
    int saveGame(const std::string& fileName);
    int loadGame(const std::string& fileName, int mode);       // mode 0..3: fade-in speed, else the palette is set at once
    int restartGame();
    std::function<void(const std::string& userMessage, const std::string& logMessage)> reportSink;
    // port: a saved-game file is about to be overwritten, or was removed after a failed save; the argument is its path (the front end
    // keeps a settings file next to each save and drops it here).
    std::function<void(const std::string& savePath)> saveFileDropped;
    const std::string& lastReport() const { return lastReport_; }
    // API 0x54 body (exe:004468d0): kind 1 top, 2 load, 3 save; flags = the API's +04 (1: no game running: save is greyed);
    // textColour: 3 bytes (B, G, R) of the slot descriptions or nullptr. 0 = done / already open, 0x11 = failed.
    int openMenu(int kind, const u8* textColour, int flags);
    bool menuOpen() const { return menu_ != nullptr || bs4_ != nullptr; }   // (the terminal takes keys like the menu does: a front end leaves its own hot keys alone)
    bool gameMenuOpen() const { return menu_ != nullptr; }
    // port (remaster): pages of save slots. The save and load menus show the original's ten slots as page 1 (files "Saved Game 1..10.sav");
    // page p > 1 shows the slots (p-1)*10+1 .. p*10 on the same art (files "Saved Game 11.sav", ... which the original never reads), the page
    // number as small text of the menu's own font and colour at the top right. 1 = the original's menu, nothing drawn, no page keys.
    // Page Up / Page Down (virtual keys 0x21 / 0x22) change the page while a slot list is up (and no question is).
    void setSlotPages(int n) { slotPages_ = n < 1 ? 1 : n > 20 ? 20 : n; }
    int slotPages() const { return slotPages_; }
    int menuSlotPage() const;                                  // 1-based number of the page a save / load slot list shows now, 0 if none is up
    std::string menuPageName() const;                          // "" if the menu is closed
    // API 0x50 / 0x51 (exe:00414720 / exe:004148f0): the text box on the main page; rect = left, top, right, bottom.
    int beginTextInput(const int* rect, int maxLength, u8 fillColour, const u8* textColour);   // 0 or 0x11
    void endTextInput(bool cancel);
    bool textInputActive() const { return textIn_.active; }
    const std::string& textInputText() const { return textIn_.text; }
    // Start-up (exe:00414ca0): showTitleScreen() paints IMAGES\TITLE.BMP (PubInfo +0x2fe) before startup(); it stays up for at least
    // one second of engine time. After startup() returned 0: jumpToStartPage() = JumpToPage(PubInfo first page, the intro page).
    // menuNewGame() is what the menu's New Game does without the menu: the top bar, then OnMenuCommand(1, 0).
    bool showTitleScreen();
    bool titleScreenUp() const { return titleUp_; }
    void jumpToStartPage();
    void menuNewGame();
    // The system's swapped-mouse-buttons setting (GetSystemMetrics(SM_SWAPBUTTON)): the help page menuhelp1a is used.
    void setMouseButtonsSwapped(bool on) { buttonsSwapped_ = on; }

    // --- game start ---
    // Loads the object state of NewGame.sav (spec 1.11) and opens the saved page ("R4") with its scroll
    // position. withObjects = false: same page, but every object removed (viewer tests).
    void startNewGame(bool withObjects = true);
    void setNewGamePath(const std::string& path) { newGamePath_ = path; }

    // --- input (screen coordinates 640x480; dx,dy = relative motion for the captured pointer) ---
    void mouseMove(int x, int y, int dx, int dy);
    void mouseDown(Button b);
    void mouseUp(Button b);
    // A WM_MOUSEMOVE posted to the main window (the game's cursor filter, dll:10061a30): exe:00445b70 for client position (x, y). The centre
    // of the main view (320, 240) is where the captured pointer rests, so the move is the delta (x - 320, y - 240): it is not accelerated like an
    // OS pointer, goes through OnMouseDelta (event 40) when API 0x5c enabled it, and replaces the pending scroll delta. With the pointer free in
    // y (API 0x6d) only x counts. Ignored unless the pointer is captured (scroll mode). pointerY(): the engine's idea of the pointer's y.
    void postMouseMove(int x, int y);
    int pointerY() const { return ptrY_; }
    // Shift toggles the bar mode (spec 5.2), Ctrl suspends pointer capture. vkey: the Windows virtual-key code
    // passed to OnKeyDown / OnKeyUp (0 = derive it from k).
    void keyDown(Key k, int vkey = 0);
    // S stash, D drop, Space use/wear/take (spec 1.6.6) unless OnKeyUp cleared doDefault (exe:00443a70).
    void keyUp(Key k, int vkey = 0);
    void update(u32 nowMs);           // injected clock: holding animation, bar flash, scroll idle steps
    void render(Frame& f);

    // --- pointer capture (spec 5.2) ---
    // The front end sets this when it can warp the OS pointer; headless runs leave the pointer free.
    void setPointerCapture(bool allowed) { captureAllowed_ = allowed; }
    bool pointerCaptured() const;     // front end: warp the OS pointer to pointerPos() after each motion event
    void pointerPos(int& x, int& y) const;
    bool barMode() const { return barMode_; }
    bool scrollMode() const { return scrollMode_; }
    void setScrollMode(bool on);      // API 0x5f (on only if the page is larger than the view)
    // API 0x5a, exe:00454120: horizontal limits (both 0xffffffff = none; unsigned, max >= min) and the vertical lock
    // (y fixed at the page's vertical middle). False = the original's failure (API 0x11).
    bool setPanoramaScrollLimits(u32 minX, u32 maxX, int verticalLock);
    // API 0x6c, exe:0043da90 case 0x6c.
    struct ScrollState { int active = 0, mouseDeltaEvent = 0, released = 0, top = 0; };
    ScrollState scrollState() const;
    // API 0x6d, exe:0043da90 case 0x6d: may the pointer leave the view vertically at the top / bottom of a 2560x1024 page.
    void setScrollOptions(int allowTop, int allowBottom);
    bool verticalRelease() const { return vRelease_; }   // DAT_004990cc: pointer free in y (front end: warp x only)
    // API 0x5d, exe:00445810: moves the main view (640 x 368) inside 640 x 480; false if it would leave the screen.
    bool moveMainWindowView(int x, int y);
    // API 0x40 (DAT_00499070) / 0x66 (DAT_004979e0) / 0x68 (DAT_0049906c, DAT_00499898): both flags are reset by every
    // primary page load.
    void setCursorShown(bool on) { cursorShown_ = on; }
    void setCursorOverride(bool on) { cursorOverride_ = on; }
    // The rectangle the OS pointer is confined to (exe:00411860 / exe:00452020): the manual one, or the main view united
    // with the open popups that have hotspots. False = no clip.
    bool cursorClipRect(Rect& r) const;
    void setCursorClip(bool manual, const int* ltrb);   // ltrb: the RECT (4 ints) or nullptr

    // --- the sim-status window (API 0x4a .. 0x4e, 0x56; exe:00410130 .. 00410410, window class exe:0040f1b0 .. 0040fdd0) ---
    // A modal 640 x 368 window made from the page "status" (window id -5), created once by the game. While it is up (DAT_0049a39c == 1)
    // it replaces what is blitted into the main view: opaque (own palette, key >= 0x100) or over the page with palette index `key`
    // transparent. It has its own DC (API 0x4c), image display (0x4d) and, when asked, hotspots whose click fires event 34.
    bool simStatusUp() const { return sim_ && sim_->up; }
    bool simStatusCreated() const { return sim_ != nullptr; }

    // --- the BioSys 4 terminal (engine_bs4.cpp; analysis/notes/bs4.md 1.2 / 1.4 / 1.8 / 1.9 / 1.12) ---
    // While it is up (DAT_00498b84 != 0, the display is "suspended": DAT_004990ec == 1) the game page is unloaded and mouse, keys, the 500 ms
    // timer, idle and redraw go to it; render() shows its frame; the API commands other than 2 9 0x14..0x19 0x34 0x35 0x44 0x53 0x54 0x59 return
    // 0x17. Entered by verb 15 (exe:0040d270 -> exe:004104e0) and by loading a save whose header +0x60 is 1; left by its verb 0x18 hotspots
    // (exe:004105c0), a load of a save without terminal, restart and WM_DESTROY (shutdown).
    bool bs4Active() const { return bs4_ != nullptr; }
    Bs4* bs4() { return bs4_.get(); }
    Bs4Session& bs4Session() { return bs4Session_; }               // the three password flags, the mail state and stamp (header +0x4cc..)
    void setBs4Fixes(const Bs4Fixes& f) { bs4Fixes_ = f; }         // the terminal's remaster switch (bs4fixes.h), read when the terminal is built
    // exe:004104e0 EnterBioSys4Mode(buffer, size): false (the terminal is left again) if it could not be built. section = the 0x480 bytes of a save.
    bool bs4Enter(const u8* section, size_t size);
    // exe:004105c0 LeaveBioSys4Mode(fade): fade 0..3 fades the game palette in at that speed, else it is set at once. No-op if not in the terminal.
    void bs4Leave(int fade);
    // Trace of what the terminal asks of the engine, one line each ("bs4screen 0", "bs4click 5", "bs4loop 2 start", "bs4welcome", "bs4leave"), for tests.
    std::function<void(const std::string&)> bs4Trace;
    int bs4Presents() const { return bs4Presents_; }               // how many frames the terminal flipped (tests)

    // --- cursor (spec 5.1) ---
    int cursorCode() const;           // code of the table in spec 5.1 (0 = default, 2 = hand, 0x19 = object zone...)
    std::string cursorFile() const;   // BIODATA\CURSORS file stem to show ("CURSOR", "OBJECT", "STATUS"...)
    bool cursorVisible() const;       // false in scroll mode with the default cursor (spec 5.1)
    Bitmap8 loadCursor(const std::string& stem) const;   // throws Error

    // --- palettes and page images owned by the game logic (API 0x1a 0x1c 0x1d 0x24, 0x0a 0x0b 0x5e 0x6e ...) ---
    // Game logic attached: ornaments are painted exactly as the page flags say (the viewer's provisional "show the
    // healthy variant" rule is off). Set automatically by the first DisplayImage / SetImageVisibility / RaisePageImage
    // call; a front end that attaches a real game logic should call it before the first page is shown. The palette
    // of the logic (SetPalette) takes over by itself at its first call.
    void setGameLogicAttached(bool on) { view_.setGameLogicAttached(on); }
    bool gameLogicAttached() const { return view_.gameLogicAttached(); }
    // Called with every palette the API puts on the display (immediate set, each timed-fade step, each
    // blocking-fade step); for tests.
    std::function<void(const Palette&, u32 nowMs)> paletteTrace;
    bool paletteFadeActive() const { return fade_.active; }   // a timed SetPalette is running
    // True while a cross-fade page jump runs its frames.
    bool crossFading() const { return cross_ != nullptr; }

    // --- navigation / debug facilities ---
    void goToPage(int number, bool push = true);
    bool goBack();
    void setTimeSlot(int slot) { view_.setTimeSlot(slot); }                // also takes the palette back from the API
    void setPaletteSetOverride(int set) { view_.setPaletteSetOverride(set); }
    void setShowHidden(bool on) { view_.setShowHidden(on); }
    void setOutlines(bool on) { outlines_ = on; }
    bool outlines() const { return outlines_; }
    void scrollBy(int dx, int dy) { view_.scrollBy(dx, dy); }
    void setScroll(int x, int y) { view_.setScroll(x, y); }

    // --- state access ---
    PageView& view() { return view_; }
    const PageView& view() const { return view_; }
    GameObjects& objects() { return objs_; }
    const GameObjects& objects() const { return objs_; }
    StatusBar& bar() { return bar_; }
    const StatusBar& bar() const { return bar_; }
    void setInputEnabled(bool on) { objs_.setInputEnabled(on); }   // API 0x2e
    u32 now() const { return now_; }
    const std::string& lastMessage() const { return message_; }

    // --- high-resolution presentation (optional; the 8-bit frame and the game do not depend on it) ---
    // What the last render(Frame&) did, for a front end that draws the panorama view at the window's resolution (src/engine/panomesh.h,
    // src/app/panorama_smooth.cpp). `active`: that frame shows a projected 2560 x 1024 page through the normal path (not a menu, a
    // frozen or terminal frame, an opaque status window or a cross-fade). `page` / `palette` are valid until the next engine call; `bare`
    // is the 640 x 368 projection of the bare page: a view pixel of the final frame that differs from it was drawn over the panorama
    // (held object, sprites, wearing overlays, rain, text, outlines, the status window). `scrollStepTime` is the engine clock of the
    // last scroll step (exe:00444560, one per idle pass, 16 ms unless setPassMs), `now` the clock of the last update().
    struct PanoramaFrame {
        bool active = false;
        Rect view;
        PanoCamera camera;
        const Surface* page = nullptr;
        const Palette* palette = nullptr;
        const u8* bare = nullptr;
        u32 pageSerial = 0;
        u32 scrollStepTime = 0, now = 0;
        // API 0x5a horizontal scroll limits (exe:00454120) as camera yaws (limit + the projection's yaw offset, not wrapped): the game
        // keeps the view centre inside [limitYawMin, limitYawMax] because the art beyond is not meant to be seen (--widescreen respects it)
        bool limitsOn = false;
        double limitYawMin = 0, limitYawMax = 0;
    };
    const PanoramaFrame& panorama() const { return pano_; }
    // Does the frame render() makes have the usual layout: top bar rows 0..55, the 640 x 368 view, the inventory bar rows 424..479 (a page
    // that is not full screen, no title, menu, frozen frame or terminal)? Read-only; the overlay interface (--interface overlay) draws
    // the view over the whole window for exactly these frames.
    bool viewFrame() const { return !titleUp_ && !menu_ && !frozen_ && !bs4_ && view_.pageNumber() != 0 && !view_.fullScreen(); }
    // render() with the page filter (the rain, API 0x32) left out, without advancing it: the difference to render() is the filter's pixels
    // (--widescreen continues the rain into the side areas). Call it BEFORE render() when panorama() must describe the real frame.
    void renderNoFilter(Frame& f) { textFreePass_ = true; render(f); textFreePass_ = false; }
    bool pageFilterActive() const { return filter_.active; }
    // The held object as a layer (engine/viewlayer.h). With the split on, render() leaves the held image out of the frame whenever that is exactly
    // separable and says so in viewLayers().held.shown; the front end measures the wide picture's edge cover on that frame and draws the image back
    // at its own place (ViewLayers::blitInto). Separable = the frame is a projected panorama (panorama().active), not a cross-fade, no sim-status
    // window up, no ClearWindow fill, and nothing the engine draws after the image (fixed sprites, the wearing overlays) touches its pixels: checked by drawing the
    // layer over the frame without it and comparing with the frame the normal draw order makes (rows of the view). Otherwise the frame is the
    // normal one and `shown` is false. Draw order of render(): page,
    // page filter (rain), synth movies, HELD OBJECT, fixed sprites (the blocking animation first), wearing overlays, status window: the rain is
    // under the held image, so a layer drawn over the frame is over the rain too. viewLayers() describes the last render().
    void setSplitLayers(bool on) { splitLayers_ = on; }
    const ViewLayers& viewLayers() const { return layers_; }
    // The sim-status window is up with a palette of its own: it replaces the main view opaque (the status screen, Tab).
    bool simStatusOpaque() const { return sim_ && sim_->up && sim_->ownPalette; }

    // --- the picture cleaner (engine/picclean.h, src/app/clean_*; --picture clean) ---
    // What the last render() shows, as far as the cleaner needs it. `kind`: Page = a PAGES.DAT picture (panoramas, close-ups, flat pages, bars,
    // menus, the title) which is de-dithered; Terminal = the BioSys 4 terminal's frame (BIOSYS4.DAT art: enlargement only). `movies`: rectangles of
    // the frame (640 x 480) that show film frames (enlargement only there; page-relative movies on a projected panorama page are not listed).
    // `pageSerial` / `day` / `surface`: the page behind the frame (the threshold scale of the cleaner, whole-page histogram), null / 0 when the frame
    // is not made of the page (menu, title, terminal, frozen frame).
    struct CleanInfo {
        enum Kind { Page, Terminal } kind = Page;
        std::vector<Rect> movies;
        u32 pageSerial = 0;
        const Palette* day = nullptr;
        const Surface* surface = nullptr;
        bool projected = false;
    };
    void cleanInfo(CleanInfo& ci) const;

    // --- GDI text at the window's resolution (src/app/text_sharp.*; engine/textplane.h has the bookkeeping rule) ---
    // While tracking is on, Canvas keeps for every surface it draws text into the text runs and what lies under them. Off (the
    // default), nothing is recorded. Switch it on before the first frame is drawn: text drawn earlier is not known.
    void setTextTracking(bool on) { TextPlane::setTracking(on); }
    bool textTracking() const { return TextPlane::tracking(); }
    // One text run that shows in the frame, in 640 x 480 screen coordinates (text drawn on a surface that is shown through the
    // cylindrical projection, or in a frame the engine cannot take apart, is not listed: it stays in the 8-bit frame).
    struct TextRunOut {
        u32 id = 0;                         // drawing order
        std::string layer;                  // page | topbar | popup | status | menu | terminal | terminal panel | terminal clock
        std::shared_ptr<const Font> font;   // the face and size of the low-resolution text
        std::string text;
        int x = 0, y = 0;                   // top-left of the text cell; glyphs advance by font->advance()
        int clipL = 0, clipT = 0, clipR = 0, clipB = 0;   // right / bottom exclusive
        u8 ink = 0;                         // palette index of the text colour
        u8 r = 0, g = 0, b = 0;             // the colour under the palette on display
        u32 colorRef = 0;
        unsigned align = 0;
        bool opaque = false;
        u8 bkIndex = 0;
        std::vector<u32> px;                // the pixels (y * 640 + x) of the final frame that this run's text shows in
    };
    struct TextFrame {
        Frame full;                         // render(): the 8-bit frame as it is
        Frame free;                         // the same frame with the low-resolution text taken out (the pixels beneath)
        std::vector<u8> mask;               // 640 x 480: 1 where `full` shows text that `free` does not (= the pixels of `runs`)
        std::vector<TextRunOut> runs;       // in drawing order
        size_t orphans = 0;                 // pixels where the two renders differ without a listed run owning them (left in `free`)
        Palette palette;
    };
    // render() twice: without text (free) and as it is (full, the one panorama() describes). A text pixel the list cannot account for
    // is left in `free` (it stays low-resolution). With tracking off: full = free, no runs.
    void renderText(TextFrame& tf);

    // API 0x65: is the pointer over the bar, and the object under it ("" if none).
    bool statusBarObjectUnderCursor(std::string& objectName) const;

    // exe:004137e0 (API 0x3b): how many times a released DC asked for the screen to be shown again (window != top bar; the original
    // blits the page and flips, or flips at the end of the idle pass). Counts for tests.
    int redrawCount() const { return redrawCount_; }

    // Test aid: the verb dispatch of a hotspot action on the current page (exe:0040d270), as `api DoHotspotAction` runs a real hotspot's.
    // 0 or kApiActionFailed (0xb).
    int debugVerb(int verb, const int* args, int count);

    // Text dumps for headless tests: "objects", "bar", "state".
    std::string dump(const std::string& what) const;

private:
    struct ApiImpl;                               // the EngineApi adapter (engineapi_impl.cpp)
    static std::unique_ptr<EngineApi> makeApi(Engine& e);
    void activate(const HitInfo& h);
    void pageChangeMedia();                      // exe:00401050 start: sprites / movies / sounds of the old page are freed
    void resetPageGlobals();                     // exe:00401050 (main window): the globals a primary page load resets
    // engine_return.cpp: the right-click return page (analysis/notes/interface.md events 35 36 37 38 44 39)
    void rightButton();                          // exe:004434d0 WM_RBUTTONDOWN
    void zoomJumpPrepare(const Hotspot& hs, bool haveRect);   // exe:004449f0 DoZoomInAndPageJump: leaving a 2560 x 1024 page = exe:00445cc0
    // exe:004449f0 / exe:00445490: a zoom jump (verb 2, or verb 0x10 without cross-fade, with transition 9 from a hotspot) to a page whose header is
    // 2560 wide or 1024 high arrives with a zoom (field of view 128 -> 75). zoomArrivalBegin before the jump, zoomArrivalRun after it.
    bool zoomArrives(const Hotspot& hs, bool haveRect) const;
    void zoomArrivalBegin();
    void zoomArrivalRun(const Action& a);
    void zoomAbort();                                          // field of view back to 75 after a failed jump
    // One frame of a zoom animation (field of view fov with the yaw offset of fov 75 kept, remembered like a render, one flip = one host turn)
    // and the tick of the fixed sprite animations that follows it (exe:0045a190, only while the previous tick found one still running).
    bool zoomFrame(u32& t, double fov, int yawOffset);
    void zoomTick(u32 t, bool& live);
    bool crossFadeWantsZoom_ = false;                          // exe:00444ca0: no cross-fade from the logic and transition 9: DoZoomInAndPageJump
    void returnToSavedPage(bool useSaved);       // exe:00445f80
    int runAction(const Page& page, int index, const Hotspot& h);   // the verb dispatch (no OnHotspot); API code
    void tick500(u32 now);                       // WM_TIMER every 500 ms: OnTimer, page timers
    void resetPageTimers();
    void fireCompass(bool force);
    void updateHover();
    int windowOfPage(const Page* p) const;
    void popupOpened();
    bool overBar() const;
    int pointerBarX() const { return ptrX_; }
    void notifyBar(bool force);
    void enterBarMode();
    void scrollStep();
    int buttonCode(u32 button) const;
    void passes(u32 now, bool movieSprites, bool scroll);
    void syncMedia();
    void paintMedia();
    void drainEvents();
    bool playBlocking(const ObjectAnim& rec, bool hideHeld, bool priority);
    // enginefx.cpp: palette fades, cross-fade jump, jiggle, screen fills, page filter
    bool apiSetPalette(u32 steps, int seconds, const Palette& target);   // exe:00412630
    void fadeStep(u32 now);                                              // exe:004128e0
    bool apiFadeToColour(int r, int g, int b, int back, int speed, int keep, int percent);   // exe:00412ae0
    bool apiFadeToPalette(const Palette& target, int back, int speed, int percent);          // exe:00413140
    bool blockingFade(const Palette& start, const Palette& target, int steps, int delayMs, int speed, int keep, bool back,
                      bool snapTarget, bool snapStart);
    void showPalette(const Palette& p);
    int apiJiggle(const i32* records, int count, int repeats, u32 first, u32 last);   // exe:004139f0
    void jiggle(const i32* records, u32 first, u32 last, int repeats);
    bool pumpTurn(u32& t, u32 maxMs = 0);                                               // one turn of the host loop while blocking
    void postJumpShake(const Action& a, bool plainPath);
    bool crossFadeJump(const Page& page, const Action& a);               // exe:00444ca0
    void captureView(std::vector<u8>& out);
    void fillScreen(int x, int y, int w, int h, u8 colour);
    void dropFill() { fill_.on = false; }
    void applyFill(Frame& f);
    void activateDisplayImage(const Page& page, const Hotspot& hs);
    void activateWave(const Page& page, const Hotspot& hs);
    void activatePlayAnim(const Page& page, const Hotspot& hs);
    struct OwnPlayer;

    // saved games / menu / text input (engine_save.cpp, engine_menu.cpp)
    struct MenuState {                       // the original's menu object (exe:00416ac0, 0x648 bytes), see menus_saveload.md
        std::unique_ptr<PageView> view;      // +0x404 the page, +0x5e4 its 640 x 480 surface
        int kind = 1, flags = 0;             // type opened (1 top, 2 load, 3 save); +0x62c (1 = no game running)
        u8 bgr[3] = {0, 0, 0};               // +0x620 .. +0x622 text colour of the slot descriptions
        int pTop = 0, pSave = 0, pLoad = 0, pCred1 = 0, pHelp1 = 0;   // +0x608 .. +0x618
        int cur = 0;                         // +0x61c current page
        int slotPage = 0;                    // port: 0-based page of save slots (0 = the original's)
        int cred = 0, help = 0;              // +0x624 / +0x628 credits / help page number (0 = not there)
        int saveConfirm = 0, saveImg = -1, saveSlot = 0;   // +0x630 +0x634 +0x638 overwrite question
        int quitConfirm = 0, quitImg = -1;   // +0x63c +0x640 quit question
        int hover = -1;                      // +0x644 hotspot under the pointer
        bool over = false;                   // the pointer is over an enabled hotspot (the hand cursor)
        HitInfo press;
        bool pressValid = false;
        Palette prePalette;                  // DAT_0049bc80: the palette the game had when the menu opened
        bool preApi = false;                 // ... and whether the logic's palette was in charge
    };
    struct TextInput {
        bool active = false;
        int rect[4] = {0, 0, 0, 0};
        int maxLen = 0;
        u8 fill = 0;
        bool haveColour = false;
        u8 colour[3] = {0, 0, 0};
        std::string text;
    };
    std::string locateNewGameFile() const;                       // NewGame.sav of setNewGamePath or of the data roots, "" if none
    std::string savePathOf(const std::string& fileName) const;
    std::string findSaveFile(const std::string& fileName) const;
    void report(const std::string& userMessage, const std::string& logMessage);
    void closeMenu(bool redraw);                                 // exe:004469c0
    bool menuLoadPage(int number);                               // exe:00417d60
    void menuListSlots();                                        // exe:00419c40
    void menuKey(int vkey);                                      // exe:004172a0
    void menuHotspot(const std::string& name);                   // exe:004184e0
    void menuMove(int x, int y);                                 // exe:004178e0 (hover images) + exe:00418180
    void menuEndConfirm(bool saveKind);
    void menuPauseMedia(bool on);
    void textInputKey(int vkey);                                 // exe:00414950
    void textInputDraw();                                        // exe:00414a90
    void hideTitle();
    int uiFont();

    DataFiles files_;
    Mixer mixer_;
    SoundSystem sounds_;
    SpriteSystem sprites_;
    MovieSystem movies_;
    PageView view_;
    TouchInfo touch_;
    StatusBar bar_;
    GameObjects objs_;
    ControlProgram cp_;
    GameLogic* logic_ = nullptr;          // = &cp_: everything the engine fires goes through the ControlProgram
    std::unique_ptr<EngineApi> api_;
    std::array<int, 0x70> apiCalls_{}, apiNotImpl_{};
    bool quit_ = false, mouseDeltaEvent_ = false;
    std::string lastLog_;
    FontLibrary fonts_;
    struct SimStatus {
        std::unique_ptr<PageView> view;     // the page "status" with its own 640 x 368 surface (+0x04 TCPage, +0x1e4 surface)
        bool up = false;                    // DAT_0049a39c
        bool hotspots = false;              // +0x1ec: the hotspots take the mouse
        bool ownPalette = false;            // +0x1f0: the window put its own palette on display
        int key = -1;                       // +0x1e8: palette index that is transparent when the window is blitted (>= 0x100: none)
        int cyan = -1;                      // +0x214: index of (0, 255, 255), the transparent colour of the cursor pictures
        Palette pre;                        // DAT_004957c8: the palette before the window came up
        bool preApi = false;
        HitInfo press;
        bool pressValid = false;
    };
    std::unique_ptr<SimStatus> sim_;
    int simStatusCreate(const std::string& page);                          // exe:00410130
    int simStatusShow(const u8* palette, bool hotspots, int key);          // exe:0040f700 (1 = done / already up, 0 = failed)
    int simStatusHide(bool redraw);                                        // exe:0040f930
    void simStatusDestroy(bool redraw);                                    // exe:00410410
    void simStatusRender(Frame& f, bool keyedPass);
    bool fillWillApply() const;
    void drawViewSplit(Frame& f, int top);                                 // render(): held / fixed sprites / wearing with the layers taken out                        // exe:0040fa50
    int redrawCount_ = 0;
    struct OpenDc {
        int window = 0, handle = 0;
        std::shared_ptr<void> owner;          // keeps a popup / top bar layer alive while its DC is out
        std::unique_ptr<Canvas> canvas;
    };
    std::vector<OpenDc> dcs_;
    struct PageTimerState { bool enabled = true, done = false; u32 due = 0; };
    std::vector<PageTimerState> timers_;
    u32 lastTick_ = 0;
    // scroll / cursor state of the original (DAT_ names)
    bool limitsOn_ = false, vLock_ = false;           // DAT_004990f8, DAT_00497d50
    u32 limitMin_ = 0, limitMax_ = 0;                 // DAT_00497a1c, DAT_0049988c
    bool vRelease_ = false;                           // DAT_004990cc
    int allowTop_ = 0, allowBottom_ = 0;              // DAT_00499bdc, DAT_00497a10
    int unclampedY_ = 0;                              // this+0x32c of the main window: y before clamping at the last step
    bool cursorShown_ = true, cursorOverride_ = false;
    bool clipManual_ = false;
    int clipRect_[4] = {-1, -1, -1, -1};
    void applyScrollLimits();
    int centreY() const { return view_.viewTop() + (view_.fullScreen() ? Frame::H : PageView::ViewH) / 2; }
    bool idleRestart_ = true;               // a message was handled since the last idle pass (MFC lIdleCount = 0)
    int gameIdleCount_ = 0, gameIdleWants_ = 0;   // DAT_00483e84 / DAT_00483e88
    int lastMoveX_ = -1, lastMoveY_ = -1;
    int compass_ = -1;
    const Page* hoverPage_ = nullptr;
    int hoverIndex_ = -1, hoverWindow_ = -1;
    std::string hoverName_;
    std::string newGamePath_;
    std::string message_;
    bool outlines_ = false;

    u32 now_ = 0, lastStep_ = 0, passMs_ = 16, pumpMaxMs_ = 0;
    bool accumDelta_ = false;
    u32 scrollStepTime_ = 0;
    PanoramaFrame pano_;
    bool haveClock_ = false;
    int ptrX_ = 320, ptrY_ = 240;
    int pendDx_ = 0, pendDy_ = 0;
    bool captureAllowed_ = false, scrollMode_ = false, barMode_ = false, ctrlHeld_ = false;
    bool pressValid_ = false;
    HitInfo press_;
    int lastBarSlot_ = -2;
    std::unique_ptr<BlockingAnimPlayer> ownPlayer_;
    PumpFn pump_;
    bool blocking_ = false, mediaPaused_ = false;

    struct Fade {                                // timed SetPalette (DAT_00497a24 ...)
        bool active = false;
        Palette start;
        float delta[256][3] = {};
        u32 step = 0, steps = 0, interval = 0, due = 0;
    } fade_;
    struct Fill { bool on = false; int x = 0, y = 0, w = 0, h = 0; u8 colour = 0; int page = 0, sx = 0, sy = 0; bool popup = false; } fill_;
    struct Filter {                              // API 0x32 / 0x5b: OnBlitPage between the page blit and the overlays
        bool active = false;
        u8 a = 0, b = 0, c = 0;
        void* buf = nullptr;
        int len = 0;
    } filter_;
    const std::vector<u8>* cross_ = nullptr;     // the blended view of the cross-fade frame being shown (640 x 368)
    bool suppressEnterPage_ = false;
    bool textFreePass_ = false;                   // renderText: the second render, without the page filter
    bool splitLayers_ = false;                    // setSplitLayers
    ViewLayers layers_;
    Frame layerA_, layerB_;                       // scratch frames of the split check

    std::string dataDir_, saveDir_, lastReport_;
    std::unique_ptr<MenuState> menu_;
    int slotPages_ = 1;
    std::unique_ptr<Frame> frozen_;                // the last menu frame, shown while the screen fades out
    TextInput textIn_;
    std::unique_ptr<Palette> stashPalette_;        // exe:00412ab0: what GetPalette answers while a load is in progress
    int uiFont_ = 0;
    bool titleUp_ = false, buttonsSwapped_ = false, menuMediaPaused_ = false;
    u32 titleSince_ = 0;
    Image8 titleImage_;
    Palette titlePalette_;
    // header fields of the save file that the engine does not model (the pending return page): kept from the last load and written
    // back as they were. (+0x5c is the live "display suspended" state, +0x4cc.. the terminal's session: bs4Session_.)
    struct HeldHeader {
        u32 pending488[10] = {};
        std::string pendingPage, pendingHotspot;
        i32 pendingFlag = 0;
    } held_;

    // BioSys 4 (engine_bs4.cpp)
    class Bs4HostImpl;                             // the Bs4Host the terminal talks to
    Bs4Session bs4Session_;
    Bs4Fixes bs4Fixes_;
    std::unique_ptr<Bs4Host> bs4Host_;
    std::unique_ptr<Bs4> bs4_;                     // DAT_00498b84
    bool bs4Suspended_ = false;                    // DAT_004990ec == 1: the display is suspended (the terminal is up or being built)
    bool pageGone_ = false;                        // the game page was unloaded (event 3 sent): the next page jump has no page to leave
    int bs4ReturnPage_ = 0;                        // DAT_0049b0bc: the page the game was on, reloaded when the terminal is left
    int bs4ReturnScroll_[2] = {0, 0};              // main window +0x2c4 / +0x2c8
    Palette bs4SavedPalette_;                      // DAT_00495bd8: the game palette saved on entry
    Palette bs4Palette_;                           // what the terminal put on display
    Frame bs4Frame_;                               // the last frame the terminal flipped
    int bs4Presents_ = 0;
    bool bs4LeaveRequested_ = false, bs4MouseMovePending_ = false, bs4LeftDown_ = false, bs4Clip_ = false;
    Bs4Rect bs4ClipRect_;
    void bs4Pump();                                // after every entry into the terminal: a requested leave, a requested mouse move
    void bs4Unload();                              // exe:004136a0(1) (event 3)
    void bs4Reload(const int* scroll);             // exe:004136a0(0)
    void bs4Restart();                             // exe:00453da0: the terminal is destroyed, the session flags cleared
    void bs4ForgetSounds();                        // every sound was freed (stopAllMedia)
    std::string bs4Dump() const;
};

} // namespace biosys
