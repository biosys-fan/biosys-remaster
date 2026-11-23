#include "engine/engine.h"
#include "engine/savefile.h"
#include <algorithm>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>

namespace biosys {

namespace {

bool fileExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// NewGame.sav sits in the root of a data location (next to Biodata/), not inside it: search the same
// roots DataFiles uses (BIOSYS_DATA, else the installed copy then the CD).
std::string locateNewGame() {
    std::vector<std::string> roots = DataFiles::environmentRoots();
    if (roots.empty()) roots = DataFiles::developmentRoots();
    for (const std::string& root : roots) {
        if (fileExists(root + "/NewGame.sav")) return root + "/NewGame.sav";
        if (DIR* d = opendir(root.c_str())) {
            std::string found;
            while (const dirent* e = readdir(d))
                if (iequals(e->d_name, "newgame.sav")) { found = root + "/" + e->d_name; break; }
            closedir(d);
            if (!found.empty()) return found;
        }
    }
    return "";
}

} // namespace

std::string Engine::locateNewGameFile() const { return newGamePath_.empty() ? locateNewGame() : newGamePath_; }

// The engine's own blocking animation player (spec 1.10, exe:0044f920).
struct Engine::OwnPlayer : BlockingAnimPlayer {
    Engine& e;
    explicit OwnPlayer(Engine& eng) : e(eng) {}
    bool play(const ObjectAnim& rec, bool hideHeld, bool priority) override { return e.playBlocking(rec, hideHeld, priority); }
};

Engine::Engine(const DataFiles& files, const std::string& dataDir, GameLogic* logic, BlockingAnimPlayer* anim)
    : files_(files),
      sounds_(mixer_, files_),
      sprites_(sounds_, files_),
      movies_(mixer_, files_),
      view_(files_, dataDir),
      touch_(files_.requireData("touchinf.dat")),
      objs_(files_.requireData("objects.dat"), touch_, view_, bar_) {
    dataDir_ = dataDir;
    setLogic(logic);
    api_ = makeApi(*this);
    ownPlayer_.reset(new OwnPlayer(*this));
    objs_.setAnimPlayer(anim ? anim : ownPlayer_.get());
    movies_.setPageValidator([this](const std::string& n) { return view_.findPage(n) != 0; });
    view_.onPopupOpened = [this](int w, int h) {
        sprites_.setWindowRect(kPopupWindow, Rect{0, view_.viewTop(), w, h});
        popupOpened();
    };
    // exe:00450bd0: OnPageLoaded / OnPageDrawn (exe:00450360 loads and paints the popup) then OnPopupOpened, window 0
    view_.onTopbarOpened = [this]() {
        const std::string name = view_.topbarPage()->name;
        logic_->onPageLoaded(name, kTopBarWindow);
        logic_->onPageDrawn(name, kTopBarWindow);
        logic_->onPopupOpened(name, kTopBarWindow);
    };
    view_.onPopupClosed = [this]() {
        sprites_.freeWindow(kPopupWindow);
        movies_.freeWindow(kPopupWindow);
        logic_->onPopupClosed(kPopupWindow);
        updateHover();
    };
    view_.hotspotQuery = [this](int pn, int idx, const Hotspot& h) { return objs_.zoneOverride(pn, idx, h); };
    view_.onLeave = [this]() {
        if (pageGone_) {                 // the page was unloaded when the terminal came up (event 3 went then): nothing is left
            pageGone_ = false;
            return;
        }
        objs_.pageLeaving();
        logic_->onExitPage();            // exe:00411350: the old page is still current here
        hoverPage_ = nullptr;            // exe:004027c0 (page unload): the pointer left its hotspot
        if (!hoverName_.empty()) logic_->onHotspotHover(hoverName_, hoverWindow_, false);
        hoverName_.clear();
        hoverIndex_ = -1;
    };
    view_.onReturnSaved = [this]() { logic_->onEvent37(); };     // exe:004456b0
    view_.onReturnDropped = [this]() { logic_->onEvent38(); };   // exe:004457a0
    view_.eraseObjects = [this]() { objs_.eraseAllPainted(); };
    view_.repaintObjects = [this]() { objs_.repaintErased(); };
    view_.redrawObjects = [this]() { objs_.redrawPainted(); };
    // exe:00401050: the page record is read (surface allocated), OnPageLoaded fires, then the ornaments are painted
    view_.onLoading = [this]() {
        pageChangeMedia();
        resetPageGlobals();
        logic_->onPageLoaded(view_.page().name, kMainWindow);
    };
    view_.onBaked = [this](bool newPage) {
        if (!newPage) {
            sprites_.onSurfaceRebuilt();
            movies_.onSurfaceRebuilt();
        }
        syncMedia();
        if (newPage) {
            // exe:00401050: OnPageDrawn (page image rendered), then the page timers start (due = now + delay *
            // 250 ms) and the compass sector is announced on 360-degree pages
            logic_->onPageDrawn(view_.page().name, kMainWindow);
            resetPageTimers();
            compass_ = -1;
            fireCompass(true);
            view_.releaseTrack();     // the first render of the page (exe:00445490 / JumpToPage) comes after the compass event
        }
        objs_.pageBaked();
        // exe:00411350: after the objects were painted (a cross-fade jump fires it itself after the fade)
        if (newPage && !suppressEnterPage_) logic_->onEnterPage(view_.page().name);
    };
    bar_.onChanged = [this]() { notifyBar(true); };
}

// exe:00401050 start (after OnExitPage, before OnPageLoaded; also exe:00445f80 on a return to the saved page): FUN_0045a4d0 sprites,
// FUN_00457a60 movies, FUN_0043d2c0 sounds; each one reports its stopped / freed callbacks (3, 4) synchronously, in that order, before
// the game sees the new page and before any handle can be reused. spec 2.7 / 3.5 / 4.6: the animations and page-scoped sounds are freed
// and the movies told; the surface was replaced, so nothing is erased.
void Engine::pageChangeMedia() {
    const int pw = view_.surface().w, ph = view_.surface().h;
    if (textIn_.active) endTextInput(true);   // exe:00401050: a primary page load ends the text entry (event 28, cancelled)
    sprites_.freeAllOnPageChange();
    for (const MediaEvent& ev : sprites_.takeEvents()) logic_->onSpriteEvent(ev.handle, ev.code, ev.arg);
    sprites_.setPageSize(pw, ph);
    movies_.setPageSize(pw, ph);
    movies_.onPageChange(view_.page().name);
    for (const MediaEvent& ev : movies_.takeEvents()) logic_->onMovieEvent(ev.handle, ev.code, ev.arg);
    sounds_.onPageChange();
    for (const MediaEvent& ev : sounds_.takeEvents()) logic_->onSoundEvent(ev.handle, ev.code);
    syncMedia();
}

// exe:00401050 (main window) / exe:00445f80: the engine globals a primary page load resets
void Engine::resetPageGlobals() {
    simStatusHide(true);         // FUN_004102d0(1): a primary page load puts the sim-status window away
    scrollMode_ = view_.scrollable();
    pendDx_ = pendDy_ = 0;
    objs_.setInputEnabled(true); // DAT_00498a78 = 0: user input is on again, before OnPageLoaded
    filter_ = Filter();          // DAT_0049a398 = 0 at every primary page load
    limitsOn_ = vLock_ = false;  // DAT_004990f8 = DAT_00497d50 = 0, DAT_00499070 = 1, DAT_004979e0 = 0
    cursorShown_ = true;
    cursorOverride_ = false;
    fill_.on = false;
}

Engine::~Engine() {
    for (OpenDc& d : dcs_) CanvasRegistry::remove(d.handle);
}

void Engine::setLogic(GameLogic* l) {
    cp_.setTarget(l);
    logic_ = &cp_;
    objs_.setLogic(logic_);
}

int Engine::startup() {
    cp_.setClock(now_);
    int r = cp_.onStartup();
    return r;
}

void Engine::shutdown() {
    if (!cp_.shutdownSent()) cp_.onShutdown();
}

int Engine::apiCalls(int code) const { return code >= 0 && code < 0x70 ? apiCalls_[static_cast<size_t>(code)] : 0; }
int Engine::apiNotImplementedCalls(int code) const { return code >= 0 && code < 0x70 ? apiNotImpl_[static_cast<size_t>(code)] : 0; }

const Page* Engine::pageOfWindow(int window) const {
    if (window == kMainWindow) return view_.pageNumber() ? &view_.page() : nullptr;
    if (window == kTopBarWindow) return view_.topbarPage();
    if (window == kPopupWindow) return view_.popupPage();
    return nullptr;
}

int Engine::windowOfPage(const Page* p) const {
    if (p && p == view_.topbarPage()) return kTopBarWindow;
    if (p && p == view_.popupPage()) return kPopupWindow;
    return kMainWindow;
}

void Engine::popupOpened() {
    const Page* p = view_.popupPage();
    if (!p) return;
    std::string name = p->name;
    logic_->onPageLoaded(name, kPopupWindow);
    logic_->onPageDrawn(name, kPopupWindow);
    logic_->onPopupOpened(name, kPopupWindow);
}

void Engine::setAnimPlayer(BlockingAnimPlayer* p) { objs_.setAnimPlayer(p ? p : ownPlayer_.get()); }

// ---------------------------------------------------------------------------------------------
// start / navigation

void Engine::startNewGame(bool withObjects) {
    std::string path = newGamePath_.empty() ? locateNewGame() : newGamePath_;
    if (path.empty()) throw Error("NewGame.sav not found");
    SaveFile sf(path);
    bar_.setShown(false);
    // The original reaches the new game through the intro page, whose hotspot xTopBar (verb 9, page "topbar") opens the
    // top bar once; this front end skips the intro, so the top bar is opened here instead.
    view_.openTopbar();
    if (withObjects) objs_.loadSave(sf);
    else objs_.resetAllRemoved();
    int page = view_.findPage(sf.primaryPage);
    if (!page) throw Error("NewGame.sav: unknown page " + sf.primaryPage);
    view_.clearHistory();
    view_.goToPage(page, false);
    view_.setScroll(sf.scrollX, sf.scrollY);
    barMode_ = false;
    lastBarSlot_ = -2;
}

void Engine::goToPage(int number, bool push) { view_.goToPage(number, push); }
bool Engine::goBack() { return view_.goBack(); }

void Engine::setScrollMode(bool on) { scrollMode_ = on && view_.scrollable(); }

// ---------------------------------------------------------------------------------------------
// pointer, cursor

bool Engine::overBar() const { return view_.pageNumber() != 0 && !view_.fullScreen() && ptrY_ >= StatusBar::ScreenY; }

bool Engine::pointerCaptured() const {
    if (bs4_) return false;                      // the terminal draws its own pointer and owns the mouse (exe:00443110 and friends)
    if (!captureAllowed_ || menu_ || simStatusUp()) return false;   // exe:00445630(main, 1): the pointer is the player's again
    if (barMode_) return true;
    return scrollMode_ && !ctrlHeld_ && !view_.popupOpen() && !view_.fullScreen();
}

void Engine::pointerPos(int& x, int& y) const {
    if (pointerCaptured() && barMode_) { x = 320; y = StatusBar::ScreenY + StatusBar::Height / 2; }
    else if (pointerCaptured()) { x = 320; y = vRelease_ ? ptrY_ : centreY(); }
    else { x = ptrX_; y = ptrY_; }
}

void Engine::enterBarMode() {
    barMode_ = true;
    bar_.setShown(true);
    ptrX_ = 320;
    ptrY_ = StatusBar::ScreenY + StatusBar::Height / 2;
    notifyBar(false);
}

// exe:00445b70 (spec 5.2): a pointer delta over 7 px per event is doubled (the OS mouse acceleration
// the original configured at start-up); it replaces any pending scroll delta.
static int accelerate(int d) { return std::abs(d) > 7 ? d * 2 : d; }

void Engine::mouseMove(int x, int y, int dx, int dy) {
    if (blocking_) return;   // input is ignored while a blocking animation plays (spec 2.8)
    if (x != lastMoveX_ || y != lastMoveY_ || dx || dy) idleRestart_ = true;   // MFC IsIdleMessage: a redundant WM_MOUSEMOVE is not one
    lastMoveX_ = x; lastMoveY_ = y;
    if (menu_) {                              // exe:004178e0: the menu follows the free pointer
        ptrX_ = std::clamp(x, 0, Frame::W - 1);
        ptrY_ = std::clamp(y, 0, Frame::H - 1);
        menuMove(ptrX_, ptrY_);
        return;
    }
    if (bs4_) {                               // exe:00442d87: after the menu and the sim-status window, before everything of the page
        ptrX_ = std::clamp(x, 0, Frame::W - 1);
        ptrY_ = std::clamp(y, 0, Frame::H - 1);
        bs4_->onMouseMove(bs4LeftDown_ ? 1u : 0u, ptrX_, ptrY_);
        bs4Pump();
        return;
    }
    if (view_.pageNumber() == 0) return;
    if (captureAllowed_ && barMode_) {
        // relative scrolling of the bar (spec 1.8): the pointer stays at the bar centre
        if (dx) { bar_.scrollBy(accelerate(dx)); notifyBar(false); }
        return;
    }
    if (pointerCaptured()) {
        if (vRelease_) {
            // exe:00445b70 with DAT_004990cc == 1: only the x delta is taken, the pointer keeps its real y
            int ddx = accelerate(dx);
            if (mouseDeltaEvent_) { int ody = 0; logic_->onMouseDelta(ddx, ody); }
            pendDx_ = accumDelta_ ? pendDx_ + ddx : ddx;
            ptrX_ = 320;
            ptrY_ = std::clamp(y, 0, Frame::H - 1);
            // exe:00442de8..00442e2a (WM_MOUSEMOVE handler, after the call of 00445b70; the decompiler listing does not show these
            // instructions): the release ends when the pointer is back at the centre row of the view: released at the top
            // (unclamped y <= 0) and the pointer at or below the centre, or released at the bottom and the pointer at or above it.
            // Without this the view could never be turned up or down again after one overshoot.
            if (unclampedY_ <= 0 ? y >= centreY() : y <= centreY()) {
                vRelease_ = false;
                ptrY_ = centreY();                   // (exe:00446450 puts the cursor back at the centre on the next idle step)
            }
            updateHover();
            return;
        }
        int ddx = accelerate(dx), ddy = accelerate(dy);
        if (mouseDeltaEvent_) logic_->onMouseDelta(ddx, ddy);   // API 0x5c; the DLL may change the delta
        pendDx_ = accumDelta_ ? pendDx_ + ddx : ddx;
        pendDy_ = accumDelta_ ? pendDy_ + ddy : ddy;
        ptrX_ = 320; ptrY_ = centreY();
        updateHover();
        return;
    }
    ptrX_ = std::clamp(x, 0, Frame::W - 1);
    ptrY_ = std::clamp(y, 0, Frame::H - 1);
    if (overBar()) {
        bar_.setShown(true);                  // first time the bar is entered (spec 1.8)
        if (captureAllowed_) enterBarMode();  // entering the bar warps the pointer to its centre
        notifyBar(false);
    }
    updateHover();
}

// exe:00445b70 for a posted message (see engine.h)
void Engine::postMouseMove(int x, int y) {
    if (blocking_ || menu_ || view_.pageNumber() == 0 || !pointerCaptured() || barMode_) return;
    idleRestart_ = true;
    int dx = x - 320;
    if (vRelease_) {                                 // DAT_004990cc == 1: only the x delta
        if (dx == 0) return;
        int ody = 0;
        if (mouseDeltaEvent_) logic_->onMouseDelta(dx, ody);
        pendDx_ = dx;
        return;
    }
    int dy = y - centreY();
    if (dx == 0 && dy == 0) return;
    if (mouseDeltaEvent_) logic_->onMouseDelta(dx, dy);
    pendDx_ = dx;
    pendDy_ = dy;
}

// exe:004020e0: OnHotspotHover(old, 0) then OnHotspotHover(new, 1) when the hotspot under the pointer changes.
void Engine::updateHover() {
    if (bs4_ || bs4Suspended_) return;   // no page under the pointer
    if (simStatusUp()) return;       // exe:00440c20 / exe:004020e0: the page under the window hears nothing (guess for event 33)
    HitInfo h;
    if (view_.pageNumber() != 0 && !blocking_ && !overBar()) h = view_.hit(ptrX_, ptrY_);
    const Page* np = h ? h.page : nullptr;
    int ni = h ? h.index : -1;
    if (np == hoverPage_ && ni == hoverIndex_) return;
    if (!hoverName_.empty()) logic_->onHotspotHover(hoverName_, hoverWindow_, false);
    hoverPage_ = np;
    hoverIndex_ = ni;
    hoverName_ = h ? h.hotspot->name : std::string();
    hoverWindow_ = windowOfPage(np);
    if (h) logic_->onHotspotHover(hoverName_, hoverWindow_, true);
}

int Engine::buttonCode(u32 button) const {
    if (button == 0 || button > touch_.buttons.size()) return 0;
    return static_cast<int>(touch_.buttons[button - 1].w[1]);
}

// spec 5.1: STATUS over the bar, else the button type of the hotspot under the pointer.
int Engine::cursorCode() const {
    if (menu_) return menu_->over ? 2 : 0;       // exe:004020e0 end: the hand over any hotspot while the menu is up
    if (simStatusUp()) return sim_->hotspots && sim_->view->hit(ptrX_, ptrY_) ? 2 : 0;   // exe:0040fdb0 / exe:0040fdd0
    if (view_.pageNumber() == 0 || cursorOverride_) return 0;   // exe:004516d0: API 0x66 forces the default cursor
    if (overBar()) return 0x1a;
    HitInfo h = view_.hit(ptrX_, ptrY_);
    if (!h) return 0;
    return buttonCode(h.hotspot->button);
}

std::string Engine::cursorFile() const {
    int code = cursorCode();
    if (menu_) return code == 2 ? "MENUHAND" : "MNUARROW";
    if (simStatusUp()) return code == 2 ? "SIMHAND" : "SIMARROW";                 // exe:0040f1b0: CURSORS\SIMHAND.BMP, SIMARROW.BMP
    if (view_.pageNumber() != 0) {
        const std::string& pn = view_.page().name;
        if (view_.fullScreen()) return code == 2 ? "MENUHAND" : "MNUARROW";   // menu pages (guess: all full-screen pages)
        if (code == 0 && !pn.empty() && (pn[0] == 'U' || pn[0] == 'u')) return "CURSE";   // exe:00411d50
    }
    switch (code) {
    case 2: return "CURSHI";
    case 0x11: return "LONG";
    case 0x12: return "SHORT";
    case 0x13: return "PLANT";
    case 0x14: return "FRUIT";
    case 0x16: return "CURSHI";
    case 0x17: return "FEAT";
    case 0x18: return "FEAT2";
    case 0x19: return "OBJECT";
    case 0x1a: return "STATUS";
    case 0x1b: return "BLOCKED";
    case 0x22: return "EXTRA1";
    case 0x23: return "EXTRA2";
    case 0x24: return "EXTRA3";
    case 0x25: return "EXTRA4";
    default: return "CURSOR";
    }
}

bool Engine::cursorVisible() const {
    if (bs4_) return false;           // exe:00408b90: the terminal draws its own cursor (BS4ARROW / BS4HAND) into the frame
    // in scroll mode the default cursor sprite is not drawn (the pointer sits at the view centre)
    if (blocking_) return false;   // exe:00411d50(0): cursor hidden during a blocking animation
    if (!cursorShown_) return false;   // API 0x40: DAT_00499070 == 0 hides the sprite
    return !(pointerCaptured() && !barMode_ && cursorCode() == 0);
}

Bitmap8 Engine::loadCursor(const std::string& stem) const {
    return readBmp8(files_.requireData("cursors\\" + stem + ".bmp"));
}

// ---------------------------------------------------------------------------------------------
// mouse buttons and keys

bool Engine::statusBarObjectUnderCursor(std::string& name) const {
    name.clear();
    if (!overBar()) return false;
    int obj = bar_.objectAt(ptrX_);
    if (obj >= 0) name = objs_.objectName(obj);
    return true;
}

// event 43: the slot under the screen-centre pointer
void Engine::notifyBar(bool force) {
    int slot = bar_.slotAt(320);
    if (!force && slot == lastBarSlot_) return;
    lastBarSlot_ = slot;
    int obj = bar_.firstObjectInSlot(slot);
    if (obj >= 0) logic_->onStatusBarObject(obj, objs_.objectName(obj), 0);   // exe:0045aa40: only for an object
}

void Engine::mouseDown(Button b) {
    if (blocking_) return;   // input is ignored while a blocking animation plays (spec 2.8)
    idleRestart_ = true;
    if (menu_) {             // exe:00417c70 -> TCPage::OnLButtonDown
        if (b == Button::Left) {
            menu_->press = menu_->view->hit(ptrX_, ptrY_);
            menu_->pressValid = true;
        }
        return;
    }
    if (bs4_) {              // exe:00443110: the terminal takes the left button, the right one does nothing (exe:00406510)
        if (b == Button::Left) {
            bs4LeftDown_ = true;
            bs4_->onLButtonDown(1, ptrX_, ptrY_);
        }
        bs4Pump();
        return;
    }
    if (simStatusUp()) {     // exe:00443110 / exe:0040fd30: the window's hotspots take the left button when they are on
        if (b == Button::Left && sim_->hotspots) {
            sim_->press = sim_->view->hit(ptrX_, ptrY_);
            sim_->pressValid = true;
        }
        return;
    }
    if (b == Button::Right) {
        rightButton();
        return;
    }
    if (b != Button::Left || view_.pageNumber() == 0) return;
    press_ = overBar() ? HitInfo() : view_.hit(ptrX_, ptrY_);
    pressValid_ = true;
}

// The action fires on button release over the hotspot that got the press (guess, spec 1.5); on the
// bar a release alone acts (spec 1.6.8). The right button has no function in the spec.
void Engine::mouseUp(Button b) {
    if (blocking_) return;   // input is ignored while a blocking animation plays (spec 2.8)
    idleRestart_ = true;
    if (menu_) {             // exe:00417bd0 -> TCPage::OnLButtonUp: the hotspot that got the press and is still under the pointer
        if (b != Button::Left) return;
        const bool had = menu_->pressValid;
        menu_->pressValid = false;
        HitInfo h = menu_->view->hit(ptrX_, ptrY_);
        if (h && had && h.sameAs(menu_->press)) {
            const std::string name = h.hotspot->name;
            menuHotspot(name);
        }
        return;
    }
    if (bs4_) {              // exe:004432f0
        if (b == Button::Left) {
            bs4LeftDown_ = false;
            bs4_->onLButtonUp(0, ptrX_, ptrY_);
        }
        bs4Pump();
        return;
    }
    if (simStatusUp()) {     // exe:004432f0 / exe:0040fc60: press and release on the same hotspot: event 34 (not OnHotspot, no verb)
        if (b != Button::Left || !sim_->hotspots) return;
        const bool had = sim_->pressValid;
        sim_->pressValid = false;
        HitInfo h = sim_->view->hit(ptrX_, ptrY_);
        if (h && had && h.sameAs(sim_->press)) {
            const std::string name = h.hotspot->name;
            logic_->onSimStatusHotspot(name);
        }
        return;
    }
    if (b != Button::Left || view_.pageNumber() == 0) return;
    bool had = pressValid_;
    pressValid_ = false;
    if (overBar()) {
        objs_.barClick(bar_.objectAt(ptrX_));
        return;
    }
    HitInfo h = view_.hit(ptrX_, ptrY_);
    if (h) {
        if (had && h.sameAs(press_)) activate(h);
    } else if (had && !press_ && view_.popupOpen() && ptrY_ >= view_.viewTop() && ptrY_ < view_.viewTop() + PageView::ViewH) {
        view_.closePopup();
        message_ = "popup closed";
    }
}

void Engine::activate(const HitInfo& h) {
    const Hotspot& hs = *h.hotspot;
    // DLL event 5 (spec 1.5): the engine performs the action only if the handler returns exactly 1
    if (logic_->onHotspot(hs.name, windowOfPage(h.page), h.page->name) != 1) return;
    runAction(*h.page, h.index, hs);
}

// The verb dispatch of a hotspot / timer action (exe:0040d270 "Dispatcher" and the page viewer's verbs).
// Returns 0, or kApiActionFailed (0xb) like the API commands 6 and 7.
int Engine::debugVerb(int verb, const int* args, int count) {
    if (view_.pageNumber() == 0) return kApiActionFailed;
    Hotspot h;
    h.action.verb = verb;
    for (int i = 0; i < count && i < 8; i++) h.action.arg[i] = args[i];
    h.name = "(debug)";
    return runAction(view_.page(), -1, h);
}

int Engine::runAction(const Page& page, int index, const Hotspot& hotspot) {
    if (simStatusUp()) return kApiActionFailed;   // exe:0040d270: the Dispatcher does nothing while the sim-status window is up
    const Hotspot hs = hotspot;   // the page may be replaced by the action
    // verb 0x10 (Action_JumpToPageAt exe:0040d4f0) with a cross-fade number in a5: DoCrossFadeAndPageJump exe:00444ca0
    if (hs.action.verb == VERB_JUMP_SCROLL && hs.action.arg[4] != 0 && view_.pageNumber() != 0 && &page == &view_.page() &&
        crossFadeJump(page, hs.action))
        return 0;
    switch (hs.action.verb) {
    case VERB_ALLOWOBJECTDROP:
        return objs_.activateZone(page.number, index) ? 0 : kApiActionFailed;
    case 14:
        objs_.useOnHotspot(hs);
        return 0;
    case VERB_BS4:   // verb 15 (exe:0040d270 case 0xf): EnterBioSys4Mode(0, 0)
        return bs4Enter(nullptr, 0) ? 0 : kApiActionFailed;
    case 12:   // verb 12: stash the held object
        objs_.keyStash();
        return 0;
    case VERB_DISPLAYIMAGE:
        activateDisplayImage(page, hs);
        return 0;
    case VERB_PLAYWAVE:
        activateWave(page, hs);
        return 0;
    case VERB_PLAYANIM:
        activatePlayAnim(page, hs);
        return 0;
    case VERB_JUMP_SCROLL: {
        struct ResumeMovies { Engine& e; ~ResumeMovies() { e.movies_.resumeWindow(-1, e.now_); } } resumeMovies{*this};   // exe:00457af0, last
        zoomJumpPrepare(hs, index >= 0);
        const bool arrive = zoomArrives(hs, index >= 0);
        if (arrive) zoomArrivalBegin();
        // exe:0040d4f0 Action_JumpToPageAt: with a6 / a7 set, JumpToPage does not send OnEnterPage; after the jump the
        // page shakes (same routine as the cross-fade path), then OnEnterPage
        const bool shake = hs.action.arg[5] != 0 || hs.action.arg[6] != 0;
        suppressEnterPage_ = shake || arrive;
        ClickResult r = view_.perform(hs);
        suppressEnterPage_ = false;
        if (!r.text.empty()) message_ = r.text;
        const bool ok = r.implemented && r.text.compare(0, 6, "error:") != 0;
        crossFadeWantsZoom_ = false;
        if (arrive) {
            if (ok && view_.pageNumber() != 0) {
                zoomArrivalRun(hs.action);
                logic_->onEnterPage(view_.page().name);
            } else {
                zoomAbort();
            }
            return ok ? 0 : kApiActionFailed;
        }
        if (ok && shake && view_.pageNumber() != 0) {
            postJumpShake(hs.action, true);
            logic_->onEnterPage(view_.page().name);
        }
        return ok ? 0 : kApiActionFailed;
    }
    default: {
        struct ResumeMovies { Engine& e; ~ResumeMovies() { e.movies_.resumeWindow(-1, e.now_); } } resumeMovies{*this};   // exe:00457af0, last
        const bool arrive = hs.action.verb == VERB_JUMP && zoomArrives(hs, index >= 0);
        if (hs.action.verb == VERB_JUMP) zoomJumpPrepare(hs, index >= 0);
        if (arrive) zoomArrivalBegin();
        suppressEnterPage_ = arrive;
        ClickResult r = view_.perform(hs);
        suppressEnterPage_ = false;
        if (!r.text.empty()) message_ = r.text;
        if (arrive) {
            if (r.implemented && r.text.compare(0, 6, "error:") != 0 && view_.pageNumber() != 0) {
                zoomArrivalRun(hs.action);
                logic_->onEnterPage(view_.page().name);
                return 0;
            }
            zoomAbort();
        }
        return r.implemented && r.text.compare(0, 6, "error:") != 0 ? 0 : kApiActionFailed;
    }
    }
}

void Engine::keyDown(Key k, int vkey) {
    if (blocking_) return;   // input is ignored while a blocking animation plays (spec 2.8)
    idleRestart_ = true;
    if (menu_) { menuKey(vkey ? vkey : vkOfKey(k)); return; }                 // exe:00443930: the menu takes every key
    if (bs4_) {                                                               // then the terminal (exe:00405ce0): text entry or event 14
        bs4_->onKeyDown(vkey ? vkey : vkOfKey(k), 1, 0);
        bs4Pump();
        return;
    }
    if (textIn_.active) { textInputKey(vkey ? vkey : vkOfKey(k)); return; }   // then the text entry (no OnKeyDown)
    if (k == Key::Ctrl) ctrlHeld_ = true;
    else if (k == Key::Shift && view_.pageNumber() != 0 && !view_.fullScreen() && !simStatusUp()) {
        // Shift toggles the bar mode: scroll mode off, pointer moved to the bar (spec 5.2)
        if (barMode_) {
            barMode_ = false;
            ptrX_ = 320; ptrY_ = 240;
        } else {
            enterBarMode();
        }
    }
    int doDefault = 1;
    logic_->onKeyDown(vkey ? vkey : vkOfKey(k), 1, 0, doDefault);   // exe:00443930: after the engine's own Shift / Ctrl work
}

void Engine::keyUp(Key k, int vkey) {
    if (blocking_) return;   // input is ignored while a blocking animation plays (spec 2.8)
    idleRestart_ = true;
    if (menu_) return;                                                        // exe:00443a70
    if (bs4_) {                                                               // exe:00405e90: event 15 unless a password is typed
        bs4_->onKeyUp(vkey ? vkey : vkOfKey(k), 1, static_cast<int>(0xC0000001u));
        bs4Pump();
        return;
    }
    if (textIn_.active) return;
    if (k == Key::Ctrl) ctrlHeld_ = false;
    int doDefault = 1;
    if (view_.pageNumber() != 0) logic_->onKeyUp(vkey ? vkey : vkOfKey(k), 1, static_cast<int>(0xC0000001u), doDefault);
    if (k == Key::Ctrl) return;
    // exe:00443a70: if the handler left doDefault == 1 the game-object key handler runs
    if (view_.pageNumber() == 0 || !objs_.usable() || doDefault != 1) return;
    bool bar = overBar();
    int barObj = bar ? bar_.objectAt(ptrX_) : -1;
    switch (k) {
    case Key::S: objs_.keyStash(); break;
    case Key::D: objs_.keyDrop(bar, barObj); break;
    case Key::Space: {
        HitInfo h = bar ? HitInfo() : view_.hit(ptrX_, ptrY_);
        objs_.keySpace(bar, barObj, h ? h.hotspot : nullptr);
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------------------------------------
// time, scrolling, drawing

// exe:00444560 (spec 5.2): x += dx, y += dy (x wraps on panoramas, y clamps), then dx/=2, dy/=2.
void Engine::scrollStep() {
    if (!pointerCaptured() || barMode_ || (!pendDx_ && !pendDy_)) return;
    unclampedY_ = view_.scrollY() + pendDy_;
    scrollStepTime_ = lastStep_;
    view_.setScroll(view_.scrollX() + pendDx_, view_.scrollY() + pendDy_);
    applyScrollLimits();
    // exe:00444560 end: on a 2560 x 1024 page the pointer is released vertically when the y overshoots the top / bottom
    // and the matching API 0x6d option is on
    if ((allowTop_ == 1 || allowBottom_ == 1) && !vRelease_ && view_.page().pixelWidth() == 2560 &&
        view_.page().pixelHeight() == 1024) {
        if (unclampedY_ < 0) { if (allowTop_ == 1) vRelease_ = true; }
        else if (allowBottom_ == 1 && unclampedY_ > 1024 - PageView::ViewH) vRelease_ = true;
    }
    pendDx_ /= 2;
    pendDy_ /= 2;
    fireCompass(false);
    updateHover();
}

// exe:00444560 after the wrap / clamp: the API 0x5a horizontal limits, then the vertical lock.
void Engine::applyScrollLimits() {
    if (view_.pageNumber() == 0 || !view_.scrollable()) return;
    int x = view_.scrollX(), y = view_.scrollY();
    if (limitsOn_) {
        if (static_cast<u32>(x) < limitMin_) x = static_cast<int>(limitMin_);
        if (limitMax_ < static_cast<u32>(x)) x = static_cast<int>(limitMax_);
    }
    if (vLock_) y = (view_.page().pixelHeight() - PageView::ViewH) >> 1;
    view_.setScroll(x, y);
}

// exe:00454120
bool Engine::setPanoramaScrollLimits(u32 minX, u32 maxX, int verticalLock) {
    if (maxX < minX) return false;
    if (view_.pageNumber() == 0) return false;
    const bool scrollable = view_.scrollable();
    const u32 w = static_cast<u32>(view_.page().pixelWidth());
    if (minX == 0xffffffffu && maxX == 0xffffffffu) {
        limitsOn_ = false;
    } else {
        if (!scrollable) return false;
        if (w <= minX || w <= maxX) return false;
        limitMin_ = minX;
        limitMax_ = maxX;
        limitsOn_ = true;
        int x = view_.scrollX();
        if (static_cast<u32>(x) < minX) x = static_cast<int>(minX);
        if (maxX < static_cast<u32>(x)) x = static_cast<int>(maxX);
        view_.setScroll(x, view_.scrollY());
    }
    if (verticalLock == 0) {
        vLock_ = false;
    } else {
        if (!scrollable) return false;
        view_.setScroll(view_.scrollX(), (view_.page().pixelHeight() - PageView::ViewH) >> 1);
        vLock_ = true;
    }
    if (limitsOn_ || vLock_) {      // forced scroll step with no delta (only runs in scroll mode or bar mode)
        if (scrollMode_ || barMode_) {
            applyScrollLimits();
            fireCompass(false);
            updateHover();
        }
    }
    return true;
}

Engine::ScrollState Engine::scrollState() const {
    ScrollState s;
    if (scrollMode_ && !ctrlHeld_) {
        s.active = 1;
        s.mouseDeltaEvent = mouseDeltaEvent_ ? 1 : 0;
        s.released = vRelease_ ? 1 : 0;
        if (vRelease_) s.top = unclampedY_ < 1 ? 1 : 0;
    }
    return s;
}

void Engine::setScrollOptions(int allowTop, int allowBottom) {
    allowTop_ = allowTop;
    allowBottom_ = allowBottom;
    if (scrollMode_ && !ctrlHeld_ && vRelease_) {
        bool clear = unclampedY_ < 1 ? allowTop_ == 0 : allowBottom_ == 0;
        if (clear) {
            vRelease_ = false;
            ptrX_ = 320; ptrY_ = centreY();      // SetCursorPos(view centre)
            updateHover();
        }
    }
}

// exe:00445810
bool Engine::moveMainWindowView(int x, int y) {
    if (x < 0 || y < 0 || x + Frame::W > Frame::W || y + PageView::ViewH > Frame::H) return false;
    view_.setViewTop(y);
    ptrX_ = 320; ptrY_ = centreY();
    if (view_.pageNumber() != 0) updateHover();
    return true;
}

// exe:0043da90 case 0x68
void Engine::setCursorClip(bool manual, const int* r) {
    clipManual_ = manual;
    if (!manual) return;
    for (int i = 0; i < 4; i++) clipRect_[i] = r ? r[i] : -1;
}

// exe:00452020 (manual / none) and exe:00411860 (automatic)
bool Engine::cursorClipRect(Rect& out) const {
    if (bs4_) {                       // exe:004105c0 / exe:00404d10: ClipCursor(NULL), then the panel rectangle while a slider marker is dragged
        if (!bs4Clip_) return false;
        out = Rect{bs4ClipRect_.left, bs4ClipRect_.top, bs4ClipRect_.right - bs4ClipRect_.left, bs4ClipRect_.bottom - bs4ClipRect_.top};
        return true;
    }
    if (clipManual_) {
        if (clipRect_[0] == -1 && clipRect_[1] == -1 && clipRect_[2] == -1 && clipRect_[3] == -1) return false;
        out = Rect{clipRect_[0], clipRect_[1], clipRect_[2] - clipRect_[0], clipRect_[3] - clipRect_[1]};
        return true;
    }
    if (view_.pageNumber() == 0) return false;
    int l = 0, t = view_.viewTop(), r = Frame::W, b = t + (view_.fullScreen() ? Frame::H : PageView::ViewH);
    auto unite = [&](const Page* p, int pl, int pt, int pr, int pb) {
        if (!p || p->hotspots.empty()) return;
        l = std::min(l, pl); t = std::min(t, pt); r = std::max(r, pr); b = std::max(b, pb);
    };
    unite(view_.topbarPage(), 0, 0, Frame::W, PageView::TopBarH);
    if (const Page* p = view_.popupPage()) unite(p, 0, view_.viewTop(), p->pixelWidth(), view_.viewTop() + p->pixelHeight());
    out = Rect{l, t, r - l, b - t};
    return true;
}

void Engine::syncMedia() {
    sprites_.setViewScroll(view_.scrollX(), view_.scrollY());
    if (view_.projected()) {
        sprites_.setViewMapper([this](int x, int y, int& px, int& py) { view_.projection().inverse(x, y, px, py); });
    } else {
        sprites_.setViewMapper(nullptr);
    }
    sounds_.setView(view_.scrollX(), view_.surface().w);
}

// The page surface is a Surface, the media systems paint into an Image8 of the same layout: swap the
// pixel vector in and out (no copy).
void Engine::paintMedia() {
    Surface& sf = view_.surface();
    if (sf.w <= 0) return;
    Image8 page;
    page.w = sf.w; page.h = sf.h;
    page.px.swap(sf.px);
    movies_.paintPageRelative(page, view_.palette());
    sprites_.paintPageRelative(page, view_.palette());
    page.px.swap(sf.px);
}

void Engine::drainEvents() {
    for (const MediaEvent& ev : sounds_.takeEvents()) {
        logic_->onSoundEvent(ev.handle, ev.code);
    }
    for (const MediaEvent& ev : sprites_.takeEvents()) logic_->onSpriteEvent(ev.handle, ev.code, ev.arg);
    for (const MediaEvent& ev : movies_.takeEvents()) logic_->onMovieEvent(ev.handle, ev.code, ev.arg);
}

// One idle pass in the order of spec 2.4: movies and page-relative animations (exe:00453ce0) -> scroll
// step (exe:00444560) -> fixed animations (exe:0045a190) -> held-object animation (exe:00448eb0) ->
// sounds (exe:0043d3c0).
void Engine::passes(u32 now, bool movieSprites, bool scroll) {
    syncMedia();
    if (movieSprites) {
        movies_.update(now);
        sprites_.update(now, 1);
        paintMedia();
    }
    if (scroll) {
        for (int i = 0; i < 20 && now - lastStep_ >= passMs_; i++) {   // one idle pass per passMs_ (16 ms by default)
            lastStep_ += passMs_;
            scrollStep();
        }
        if (now - lastStep_ >= passMs_ * 20) lastStep_ = now;
        syncMedia();
    }
    sprites_.update(now, 2);
    objs_.update(now);
    sounds_.update();
    drainEvents();
}

void Engine::update(u32 nowMs) {
    if (blocking_) return;
    if (haveClock_ && static_cast<int32_t>(nowMs - now_) < 0) nowMs = now_;   // the clock never runs backwards
    now_ = nowMs;
    cp_.setClock(nowMs);
    if (!haveClock_) { haveClock_ = true; lastStep_ = nowMs; lastTick_ = nowMs; if (titleUp_) titleSince_ = nowMs; }
    if (titleUp_ && static_cast<u32>(nowMs - titleSince_) >= 1000) hideTitle();   // shown for one second at least
    if (mediaPaused_) {
        lastStep_ = nowMs;
        lastTick_ = nowMs;
        objs_.update(nowMs);
        return;
    }
    // WM_TIMER every 500 ms (exe:004437d0; missed ticks are coalesced like the OS does)
    if (static_cast<u32>(nowMs - lastTick_) >= 500) {
        lastTick_ = nowMs - (nowMs - lastTick_) % 500;
        idleRestart_ = true;     // WM_TIMER is an ordinary message for MFC's idle logic
        tick500(nowMs);
    }
    passes(nowMs, true, true);
    if (bs4_ && !menu_) {   // exe:00416840: the terminal's idle pass (a recompose when something changed) before the game's event 17
        bs4_->onIdle();
        bs4Pump();
    }
    // CWinApp::OnIdle override (exe:00416840), one pass per update. MFC's loop restarts its idle count at 0 after every
    // handled message except WM_PAINT / WM_SYSTIMER / a redundant WM_MOUSEMOVE; with count 0 the override sets
    // DAT_00483e88 = 1 (the game wants idle calls) and DAT_00483e84 = 0 (the game's own count). The game is called
    // with its own count (0, 1, 2 ...) only while its last result was exactly 1; a result of 0 silences it until the
    // next restart. The override's return value (more idle passes wanted) only decides whether MFC calls again
    // without waiting for a message; update() is that loop, so it needs no use here.
    if (idleRestart_) {
        idleRestart_ = false;
        gameIdleWants_ = 1;
        gameIdleCount_ = 0;
    }
    if (gameIdleWants_ == 1) {
        int c = gameIdleCount_++;
        gameIdleWants_ = logic_->onIdle(c);
    }
}

// exe:004437d0 (the part after the app-active test): OnTimer, then the page timers (exe:00402600).
void Engine::tick500(u32 now) {
    cp_.onTimer();
    if (menu_) return;       // exe:004437d0: no palette step, no page timers while the menu is up
    if (bs4_) {              // exe:00406520: the clock fields, the 1 Hz tick of the pages, a panel that wants to be reloaded
        bs4_->onTimer();
        bs4Pump();
        return;
    }
    if (fade_.active && !blocking_ && now >= fade_.due && !simStatusUp()) fadeStep(now);   // exe:004437d0: after OnTimer, before the page timers
    if (view_.pageNumber() == 0 || blocking_) return;
    const Page& pg = view_.page();
    for (size_t i = 0; i < timers_.size() && i < pg.timers.size(); i++) {
        PageTimerState& st = timers_[i];
        if (st.done || now < st.due) continue;
        st.done = true;          // no repeat information is parsed from the file (the only timer is one-shot)
        if (!st.enabled) continue;
        const PageTimer& t = pg.timers[i];
        if (simStatusUp()) continue;   // exe:004437d0: a timer that comes due while the window is up is used up without its action
        if (logic_->onPageTimer(t.name, kMainWindow, pg.name) != 0) {
            Hotspot h;
            h.action = t.action;
            h.name = t.name;
            runAction(pg, -1, h);
            return;              // one timer per tick; the action may have replaced the page
        }
    }
}

// exe:00401050 after OnPageDrawn: due = now + delay * 250 ms for every timer of the page.
void Engine::resetPageTimers() {
    timers_.clear();
    for (const PageTimer& t : view_.page().timers) {
        PageTimerState st;
        st.due = now_ + t.delay * 250u;
        timers_.push_back(st);
    }
}

// exe:004038f0 / exe:00401050: on 360-degree pages (panorama flag, wider than the view) announce the
// compass sector 1..8 of the view centre; `force` also when it did not change.
void Engine::fireCompass(bool force) {
    if (view_.pageNumber() == 0 || !view_.panorama() || view_.fullScreen() || view_.page().pixelWidth() == Frame::W) return;
    int px = 0, py = 0;
    if (!view_.viewToPage(Frame::W / 2, view_.viewTop(), px, py)) return;
    const double W = view_.page().pixelWidth(), start = view_.page().initialScrollX;
    double f = ((px - (start - W * 0.0625)) / W) * 8.0;
    if (f < 0.0) f += 8.0;
    if (f >= 8.0) f -= 8.0;
    int sector = static_cast<int>(f) + 1;
    if (force || sector != compass_) {
        compass_ = sector;
        logic_->onCompassHeading(sector);
    }
}

void Engine::render(Frame& f) {
    pano_.active = false;
    layers_ = ViewLayers();
    if (titleUp_) {          // exe:00413010: the title bitmap over the whole screen
        std::copy(titleImage_.px.begin(), titleImage_.px.end(), f.px.begin());
        return;
    }
    if (menu_) { menu_->view->render(f, false); return; }
    if (frozen_) { f.px = frozen_->px; return; }
    if (bs4_) { f.px = bs4Frame_.px; return; }      // exe:00411810: the last frame the terminal flipped
    syncMedia();
    view_.render(f, outlines_);
    if (view_.pageNumber() == 0 || view_.fullScreen()) { applyFill(f); return; }
    const int top = view_.viewTop();
    // exe:00443f30: the sim-status window up and blitted opaque takes the place of the page blit and everything drawn after it
    // (filter, synth movies, held object, sprite animations); the bars stay
    const bool simUp = simStatusUp();
    if (simUp && sim_->key >= 0x100) {
        simStatusRender(f, false);
        bar_.render(f, view_.palette(), bar_.visible(now_));
        applyFill(f);
        return;
    }
    // exe:00443f30: the page filter (OnBlitPage, API 0x32) works on the view right after the page blit
    if (filter_.active && !textFreePass_)   // (the text-free render of renderText must not advance the filter: its drops move once per frame)
        logic_->onBlitPage(&f.px[static_cast<size_t>(top) * Frame::W], &f.px[static_cast<size_t>(top + PageView::ViewH) * Frame::W],
                           filter_.buf, filter_.len, filter_.a, filter_.c, filter_.b);
    // back-buffer layers over the view (spec 1.3 / 2.4): synth movies, held object, fixed sprite
    // animations (priority first), wearing overlays
    const PageView::ProjectedView& pv = view_.lastProjectedView();
    if (pv.valid && !cross_) {
        pano_.active = true;
        pano_.view = pv.rect;
        pano_.camera = pv.camera;
        pano_.page = &view_.surface();
        pano_.palette = &view_.palette();
        pano_.bare = pv.bare;
        pano_.pageSerial = pv.pageSerial;
        pano_.scrollStepTime = scrollStepTime_;
        pano_.now = now_;
        pano_.limitsOn = limitsOn_;
        pano_.limitYawMin = limitMin_ + view_.projection().yawOffset();
        pano_.limitYawMax = limitMax_ + view_.projection().yawOffset();
    }
    movies_.drawSynth(f, 0, top, view_.palette());
    if (splitLayers_ && pano_.active && !cross_ && !simUp && !fillWillApply()) {    // (a ClearWindow fill is applied over the view after the layers)
        drawViewSplit(f, top);
    } else {
        objs_.drawHeld(f, top);
        sprites_.drawFixed(f, 0, top, view_.palette());
        objs_.drawWearing(f, top);
    }
    if (simUp) simStatusRender(f, true);        // exe:0040fa50: the page was blitted first, the window goes over it
    if (cross_) {   // exe:00413d10: the blended picture replaces the view, the held / worn objects go on top again
        std::copy(cross_->begin(), cross_->end(), f.px.begin() + static_cast<std::ptrdiff_t>(top) * Frame::W);
        objs_.drawHeld(f, top);
        objs_.drawWearing(f, top);
    }
    bar_.render(f, view_.palette(), bar_.visible(now_));
    applyFill(f);
}

// The draw order of render() (held, fixed sprites, wearing) with the held image taken out when the frame without them,
// plus it, is the frame the normal order makes (viewLayers(), setSplitLayers).
bool Engine::fillWillApply() const {      // what applyFill() would do to this frame
    return fill_.on && fill_.page == view_.pageNumber() && fill_.sx == view_.scrollX() && fill_.sy == view_.scrollY() && fill_.popup == view_.popupOpen();
}

void Engine::drawViewSplit(Frame& f, int top) {
    const Palette& pal = view_.palette();
    ViewLayer held;
    const bool haveHeld = objs_.heldLayer(held);
    layers_.viewTop = top;
    if (!haveHeld) {
        sprites_.drawFixed(f, 0, top, pal);
        objs_.drawWearing(f, top);
        return;
    }
    layerA_.px = f.px;                                  // the normal order
    objs_.drawHeld(layerA_, top);
    sprites_.drawFixed(layerA_, 0, top, pal);
    objs_.drawWearing(layerA_, top);
    sprites_.drawFixed(f, 0, top, pal);                 // without the layer
    objs_.drawWearing(f, top);
    held.shown = true;
    ViewLayers test;
    test.viewTop = top;
    test.held = held;
    layerB_.px = f.px;
    test.blitInto(layerB_);
    const size_t a = static_cast<size_t>(top) * Frame::W, b = static_cast<size_t>(top + PageView::ViewH) * Frame::W;
    if (std::equal(layerA_.px.begin() + static_cast<std::ptrdiff_t>(a), layerA_.px.begin() + static_cast<std::ptrdiff_t>(b), layerB_.px.begin() + static_cast<std::ptrdiff_t>(a))) {
        layers_ = std::move(test);
    } else {
        f.px.swap(layerA_.px);                          // something is drawn over the image: not separable, the frame stays as it was
    }
}

void Engine::cleanInfo(CleanInfo& ci) const {
    ci = CleanInfo();
    if (bs4_) { ci.kind = CleanInfo::Terminal; return; }
    if (titleUp_ || menu_ || frozen_ || view_.pageNumber() == 0) return;
    ci.pageSerial = view_.pageSerial();
    ci.day = view_.dayPalette();
    ci.surface = &view_.surface();
    ci.projected = pano_.active;
    std::vector<MovieSystem::ShownRect> rs;
    movies_.shownRects(rs);
    const int top = view_.viewTop();
    for (const auto& r : rs) {
        Rect fr;
        if (r.synth) fr = Rect{r.x, top + r.y, r.w, r.h};
        else if (view_.projected()) continue;
        else fr = Rect{r.x - view_.scrollX(), r.y - view_.scrollY() + top, r.w, r.h};
        fr = clipRect(fr, Frame::W, Frame::H);
        if (fr.w > 0 && fr.h > 0) ci.movies.push_back(fr);
    }
}

// ---------------------------------------------------------------------------------------------
// media: pause, blocking animations, hotspot verbs, debug triggers

void Engine::setMediaPaused(bool paused) {
    logic_->onActivateApp(!paused);          // exe:00442830: every WM_ACTIVATEAPP
    if (paused == mediaPaused_) return;
    mediaPaused_ = paused;
    if (paused) {
        sounds_.pause();
        movies_.pause(now_);
        sprites_.setActive(false);
        movies_.setActive(false);
    } else {
        sprites_.setActive(true);
        movies_.setActive(true);
        sounds_.resume(true);
        movies_.resume(now_);
    }
    logic_->onAppActiveChanged(!paused);     // only when the state really changed
}

void Engine::stopAllMedia() {
    sprites_.freeAllOnPageChange();
    movies_.closeAll();
    sounds_.closeAll();
    bs4ForgetSounds();
    drainEvents();
}

// exe:0044f920 with the open parameters of spec 1.10: window -1, loop 0, keep 0, width -1, no flip.
bool Engine::playBlocking(const ObjectAnim& rec, bool hideHeld, bool priority) {
    if (animTrace) animTrace(rec, hideHeld, priority);
    if (rec.file.empty()) return true;
    SpriteOpenParams p;
    p.file = rec.file; p.wav = rec.wav;
    p.x = rec.x; p.y = rec.y; p.mode = rec.unk0c; p.delayMs = rec.delayMs;
    p.notify = false;
    int h = -1;
    if (sprites_.open(p, h) != kOk) return false;
    sprites_.play(h, true, now_, priority);
    blocking_ = true;
    u32 t = now_;
    sprites_.update(t, 2);
    for (int turn = 0; turn < 20000 && sprites_.isPlaying(h); turn++) {
        u32 nt = t;
        if (pump_) {
            if (!pump_(nt)) { sprites_.stop(h); break; }
        } else {
            nt = t + 16;
        }
        if (static_cast<int32_t>(nt - t) > 0) t = nt;
        now_ = t;
        passes(t, turn % 4 == 0, false);   // every 4th turn also the movies and page-relative animations
    }
    sprites_.close(h);
    blocking_ = false;
    lastStep_ = t;                          // no catch-up of scroll steps for the time spent here
    return true;
}

// Verb 4 PLAYWAVE (spec 4.4): a1 = file index of the page owning the hotspot, a2 = flags.
void Engine::activateWave(const Page& page, const Hotspot& hs) {
    const Action& a = hs.action;
    const auto& files = page.files;
    if (a.arg[0] < 0 || static_cast<size_t>(a.arg[0]) >= files.size()) {
        message_ = "playwave: bad file index " + std::to_string(a.arg[0]);
        return;
    }
    int hd = sounds_.playWave(files[static_cast<size_t>(a.arg[0])], a.arg[1] & 7);
    message_ = "playwave " + files[static_cast<size_t>(a.arg[0])] + (hd < 0 ? " (failed)" : "");
}

// Verb 11 (spec 2.8): a1 file index, a2,a3 = x,y, a4 play mode, a5 delay ms; fixed, autoplay, once,
// refcount 0. Window: the main window (the action's window byte at +0x30 is not in Action: guess).
void Engine::activatePlayAnim(const Page& page, const Hotspot& hs) {
    const Action& a = hs.action;
    const auto& files = page.files;
    if (a.arg[0] < 0 || static_cast<size_t>(a.arg[0]) >= files.size()) {
        message_ = "playanim: bad file index " + std::to_string(a.arg[0]);
        return;
    }
    SpriteOpenParams p;
    p.file = files[static_cast<size_t>(a.arg[0])];
    p.x = a.arg[1]; p.y = a.arg[2]; p.mode = a.arg[3]; p.delayMs = a.arg[4];
    p.window = -1; p.autoplay = true; p.autoFree = true; p.notify = false;
    int hd = -1;
    int rc = sprites_.open(p, hd);
    message_ = "playanim " + p.file + (rc == kOk ? "" : " (failed)");
}

int Engine::debugPlayMovie(const std::string& file, const std::string& paletteFile) {
    if (view_.pageNumber() == 0) return -1;
    std::string path = files_.findTime(file);
    if (path.empty()) return -1;
    int mw = 0, mh = 0;
    try { AviFile avi(path); mw = avi.width(); mh = avi.height(); }
    catch (const Error&) { return -1; }
    MovieOpenParams p;
    p.file = file;
    const int vh = view_.fullScreen() ? Frame::H : PageView::ViewH;
    const int pw = view_.surface().w, ph = view_.surface().h;
    p.x = std::clamp(view_.scrollX() + (Frame::W - mw) / 2, 0, std::max(0, pw - mw));
    p.y = std::clamp(view_.scrollY() + (vh - mh) / 2, 0, std::max(0, ph - mh));
    if (!paletteFile.empty()) {
        std::string pp = files_.findData(paletteFile);
        if (pp.empty()) return -1;
        try {
            p.palette = Palette::load(pp);
            p.hasPalette = true;
            view_.forcePalette(&p.palette);
        } catch (const Error&) { return -1; }
    }
    int h = -1;
    if (movies_.open(p, h) != kOk) return -1;
    if (movies_.play(h, true, now_) != kOk) return -1;
    return h;
}

int Engine::debugPlaySprite(const std::string& file, int sx, int sy, bool pageRelative) {
    if (view_.pageNumber() == 0) return -1;
    SpriteOpenParams p;
    p.file = file;
    p.mode = 3; p.delayMs = 100; p.loop = true;
    p.pageRelative = pageRelative;
    int px = 0, py = 0;
    if (pageRelative) {
        if (!view_.viewToPage(sx, sy, px, py)) return -1;
        p.x = px; p.y = py;
    } else {
        p.x = sx; p.y = sy - view_.viewTop();
    }
    int h = -1;
    if (sprites_.open(p, h) != kOk) return -1;
    sprites_.play(h, true, now_);
    return h;
}

// ---------------------------------------------------------------------------------------------
// text dumps

std::string Engine::dump(const std::string& what) const {
    std::string out;
    if (what == "objects") {
        int counts[8] = {};
        for (int i = 0; i < objs_.objectCount(); i++) counts[objs_.status(i)]++;
        out += "objects " + std::to_string(objs_.objectCount()) + "\n";
        for (int s = 1; s <= 6; s++) out += "statuscount " + std::to_string(s) + " " + std::to_string(counts[s]) + "\n";
        for (int i = 0; i < objs_.objectCount(); i++) out += objs_.describe(i) + "\n";
    } else if (what == "bar") {
        out += "bar objects=" + std::to_string(bar_.objectCount()) + " slots=" + std::to_string(bar_.slotCount()) +
               " scroll=" + std::to_string(bar_.scrollPos()) + " shown=" + (bar_.shown() ? "1" : "0") + "\n";
        for (int s = 0; s < bar_.slotCount(); s++) {
            int o = bar_.firstObjectInSlot(s);
            out += "slot " + std::to_string(s) + " " + objs_.objectName(o) + " count=" + std::to_string(bar_.slotMemberCount(s)) +
                   " status=" + std::to_string(objs_.status(o)) + "\n";
        }
    } else if (what == "state") {
        int h = objs_.heldObject();
        out += "held " + (h >= 0 ? objs_.objectName(h) : std::string("-")) + "\n";
        std::string w;
        for (int o : objs_.wornObjects()) w += (w.empty() ? "" : ",") + objs_.objectName(o);
        out += "worn " + (w.empty() ? std::string("-") : w) + "\n";
        out += "page " + (view_.pageNumber() ? view_.page().name : std::string("-")) + " scroll " +
               std::to_string(view_.scrollX()) + "," + std::to_string(view_.scrollY()) + "\n";
        out += "cursor " + std::to_string(cursorCode()) + " " + cursorFile() + "\n";
        out += "pointer " + std::to_string(ptrX_) + "," + std::to_string(ptrY_) + "\n";
        out += "scrollmode " + std::string(scrollMode_ ? "1" : "0") + " barmode " + (barMode_ ? "1" : "0") + "\n";
        out += std::string("input ") + (objs_.inputEnabled() ? "1" : "0") + " simstatus " + (simStatusUp() ? "1" : "0") + "\n";
    } else if (what == "menu") {
        out += std::string("menu ") + (menu_ ? "open" : "closed") + "\n";
        if (menu_) {
            out += "page " + menuPageName() + " kind " + std::to_string(menu_->kind) + " flags " + std::to_string(menu_->flags) +
                   " credits " + std::to_string(menu_->cred) + " help " + std::to_string(menu_->help) + " saveconfirm " +
                   std::to_string(menu_->saveConfirm) + " quitconfirm " + std::to_string(menu_->quitConfirm) + " hover " +
                   (menu_->hover >= 0 ? menu_->view->page().hotspots[static_cast<size_t>(menu_->hover)].name : std::string("-")) + "\n";
            if (slotPages_ > 1) out += "slotpage " + std::to_string(menuSlotPage()) + "/" + std::to_string(slotPages_) + "\n";   // port: 0 = not on a slot list
            const Page& pg = menu_->view->page();
            for (size_t i = 0; i < pg.hotspots.size(); i++)
                out += "hotspot " + pg.hotspots[i].name + (menu_->view->hotspotEnabled(pg, static_cast<int>(i)) ? " on\n" : " off\n");
        }
    } else if (what == "text") {
        out += std::string("textinput ") + (textIn_.active ? "on" : "off") + " text '" + textIn_.text + "'\n";
    } else if (what == "simstatus") {
        out += std::string("simstatus created=") + (sim_ ? "1" : "0") + " up=" + (simStatusUp() ? "1" : "0");
        if (sim_) {
            out += " hotspots=" + std::to_string(sim_->hotspots ? 1 : 0) + " ownpalette=" + (sim_->ownPalette ? "1" : "0") +
                   " key=" + std::to_string(sim_->key) + " cyan=" + std::to_string(sim_->cyan);
        }
        out += "\n";
        if (sim_ && sim_->view) {
            const Page& pg = sim_->view->page();
            for (size_t i = 0; i < pg.hotspots.size(); i++)
                out += "simhotspot " + pg.hotspots[i].name + (sim_->view->hotspotEnabled(pg, static_cast<int>(i)) ? " on\n" : " off\n");
        }
    } else if (what == "bs4") {
        out += bs4Dump();
    } else if (what == "redraws") {
        out += "redraws " + std::to_string(redrawCount_) + "\n";
    } else if (what == "returnpage") {
        out += std::string("returnpage valid=") + std::to_string(held_.pendingFlag) + " from=" + held_.pendingPage + " to=" + held_.pendingHotspot +
               " saved=" + (view_.hasSavedPage() ? view_.savedPageName() : std::string("-")) + " x=" + std::to_string(static_cast<int>(held_.pending488[0])) +
               " y=" + std::to_string(static_cast<int>(held_.pending488[1])) + " destination=" + view_.destinationPageName() + "\n";
    } else if (what == "keyed") {
        out += view_.keyedReport();
        // the frame as shown: how many pixels of the view carry the palette index of (0, 255, 255)
        Frame f;
        const_cast<Engine*>(this)->render(f);
        const int ci = view_.palette().find(Color{0, 255, 255});
        long n = 0;
        if (ci >= 0 && view_.pageNumber() != 0)
            for (int y = view_.viewTop(); y < view_.viewTop() + PageView::ViewH; y++)
                for (int x = 0; x < Frame::W; x++) n += f.px[static_cast<size_t>(y) * Frame::W + static_cast<size_t>(x)] == ci;
        out += "cyanpixels " + std::to_string(ci) + " " + std::to_string(n) + "\n";
    } else if (what == "zones") {
        if (view_.pageNumber() != 0)
            for (int z : objs_.zonesOfPage(view_.pageNumber())) {
                Rect r = objs_.zoneRect(z);
                int o = objs_.zoneObject(z);
                out += "zone " + std::to_string(z) + " rect=" + std::to_string(r.x) + "," + std::to_string(r.y) + "," +
                       std::to_string(r.w) + "," + std::to_string(r.h) + " enabled=" + (objs_.zoneEnabled(z) ? "1" : "0") +
                       " object=" + (o >= 0 ? objs_.objectName(o) : std::string("-")) + "\n";
            }
    }
    return out;
}

} // namespace biosys
