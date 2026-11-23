// Engine: the menu (exe:00416ac0 .. exe:00419c40, opened by API 0x54) and the text entry (API 0x50 / 0x51, exe:00414720 ..
// exe:00414a90). The menu is the engine's own modal object: its pages come from PAGES.DAT (menutop, menuload, menusave,
// menucred1..11, menuhelp1, menuhelp1a), it draws into its own 640 x 480 surface (here: a second PageView), shows MENU.PAL,
// and reports the player's choices to the game logic through OnMenuCommand (31) and OnMenuQuerySavedGame (32).
// Spec: analysis/notes/menus_saveload.md.
#include "engine/engine.h"
#include <algorithm>
#include <cstdio>

namespace biosys {

namespace {

constexpr int kMenuWindow = -6;       // FUN_00401040(menu + 0x404, menu, 0xfffffffa)
constexpr int kVkEscape = 0x1b, kVkY = 0x59, kVkN = 0x4e, kVkBack = 0x08, kVkEnter = 0x0d, kVkPageUp = 0x21, kVkPageDown = 0x22;
constexpr int kSlotsPerPage = 10;     // the original's save and load pages have ten slots

// "savegame12" -> 12; 0 if the name is not prefix + a number 1 .. 20
int slotOf(const std::string& name, const char* prefix) {
    const size_t n = std::string(prefix).size();
    if (name.size() <= n || !iequals(name.substr(0, n), prefix)) return 0;
    int v = 0;
    for (size_t i = n; i < name.size(); i++) {
        if (name[i] < '0' || name[i] > '9') return 0;
        v = v * 10 + (name[i] - '0');
    }
    return v >= 1 && v <= 20 ? v : 0;
}

} // namespace

int Engine::menuSlotPage() const {
    if (!menu_ || (menu_->cur != menu_->pSave && menu_->cur != menu_->pLoad) || menu_->cred != 0 || menu_->help != 0) return 0;
    return menu_->slotPage + 1;
}

std::string Engine::menuPageName() const {
    if (!menu_ || menu_->cur == 0) return "";
    return menu_->view->pageNameOf(menu_->cur);
}

// Fonts: the engine's own GDI font is "systemfont", cell height 18 (exe:00416400 LOGFONT, loaded from FONTS\JON.TTF).
int Engine::uiFont() {
    if (uiFont_) return uiFont_;
    const std::string ttf = files_.findData("fonts\\jon.ttf");
    if (!ttf.empty()) fonts_.addFile(ttf);
    LogFont lf;
    lf.height = 18;
    lf.weight = 400;
    lf.charSet = 1;
    lf.quality = 2;
    lf.faceName = "systemfont";
    uiFont_ = fonts_.createFont(lf);
    return uiFont_;
}

// exe:00457c70 / exe:0043d730 (stop movies and sounds, remembering that they were playing) and exe:00457cf0 / exe:0043d780
void Engine::menuPauseMedia(bool on) {
    if (on == menuMediaPaused_) return;
    menuMediaPaused_ = on;
    if (mediaPaused_) return;           // the application is deactivated: the media are paused already
    if (on) {
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
}

// ---------------------------------------------------------------------------------------------
// opening and closing

// exe:004468d0 + exe:00416bd0
int Engine::openMenu(int kind, const u8* textColour, int flags) {
    if (menu_) return 0;
    if (kind == 3 && flags == 1) return 0x11;                       // exe:00416bd0: the save menu needs a running game
    if (!textIn_.active) simStatusHide(true);                       // exe:004468d0: FUN_004102d0(1)
    auto m = std::make_unique<MenuState>();
    m->view.reset(new PageView(files_, dataDir_));
    m->view->setGameLogicAttached(true, false);                     // ornaments exactly as the page flags say
    m->kind = kind;
    m->flags = flags;
    m->pTop = m->view->findPage("menutop");
    m->pSave = m->view->findPage("menusave");
    m->pLoad = m->view->findPage("menuload");
    m->pCred1 = m->view->findPage("menucred1");
    m->pHelp1 = m->view->findPage("menuhelp1");
    if (!m->pTop || !m->pSave || !m->pLoad) {
        report("", "Menu mode needs pages 'menutop', 'menusave' and 'menuload'.");
        return 0x11;
    }
    if (textColour) for (int i = 0; i < 3; i++) m->bgr[i] = textColour[i];
    Palette pal;
    try {
        pal = Palette::load(files_.requireData("menu.pal"));
    } catch (const Error& e) {
        report("", std::string("Failed to open the menu palette file MENU.PAL. ") + e.what());
        return 0x11;
    }
    if (pal.find(Color{0, 255, 255}) < 0) {
        report("", "There is no transparent colour (0, 255, 255) in the menu palette.  The Engine needs one.");
        return 0x11;
    }
    // exe:004468d0: what the menu stops or pauses, and the palette the game has now
    if (textIn_.active) endTextInput(true);                         // FUN_004148f0(1)
    m->prePalette = view_.palette();
    m->preApi = view_.apiPaletteActive();
    if (pointerCaptured()) logic_->onEvent16(0, 0);                 // FUN_00443e80: the captured pointer is let go
    barMode_ = false;
    menuPauseMedia(true);
    // exe:00416bd0: fade out the game, show the menu palette, load the first page
    apiFadeToColour(0, 0, 0, 0, 0, -1, 100);
    if (bs4_) view_.forcePalette(nullptr);                          // the terminal's palette gives way to the menu's (opened from the terminal: API 0x54 is accepted)
    showPalette(pal);
    m->view->setApiPalette(pal);
    menu_ = std::move(m);
    int first = kind == 2 ? menu_->pLoad : kind == 3 ? menu_->pSave : menu_->pTop;
    if (!menuLoadPage(first)) {
        closeMenu(true);
        return 0x11;
    }
    ptrX_ = std::clamp(ptrX_, 0, Frame::W - 1);
    ptrY_ = std::clamp(ptrY_, 0, Frame::H - 1);
    menuMove(ptrX_, ptrY_);
    return 0;
}

// exe:004469c0. redraw (parameter 0): the menu fades out, the game's palette returns and the page is drawn again;
// without it (a load follows, which does its own fade) the last menu frame stays up for that fade.
void Engine::closeMenu(bool redraw) {
    if (!menu_) return;
    std::unique_ptr<MenuState> m = std::move(menu_);
    if (redraw || !frozen_) {
        frozen_.reset(new Frame());
        m->view->render(*frozen_, false);
    }
    m->view.reset();                                // exe:00416b40: the page and its surface are freed
    if (!redraw) return;
    apiFadeToColour(0, 0, 0, 0, 0, -1, 100);
    frozen_.reset();
    if (m->preApi) view_.setApiPalette(m->prePalette);
    else view_.clearApiPalette();
    if (bs4_) view_.forcePalette(&bs4Palette_);                     // back to the terminal's palette
    menuPauseMedia(false);
    updateHover();
}

// ---------------------------------------------------------------------------------------------
// pages

// exe:00417d60: the page replaces the current one; nothing is sent to the game logic (windows -6 send no page events)
bool Engine::menuLoadPage(int number) {
    MenuState& m = *menu_;
    try {
        m.view->goToPage(number, false);
    } catch (const Error& e) {
        report("", std::string("Failed to get offset and length of page ") + std::to_string(number) + ". " + e.what());
        return false;
    }
    m.cur = number;
    m.saveConfirm = 0;
    m.quitConfirm = 0;
    menuListSlots();
    m.hover = -1;
    m.over = false;
    menuMove(ptrX_, ptrY_);                                         // exe:00451d50: a mouse move at the pointer
    return true;
}

// exe:00419c40: yes / no hidden; the main page greys "save" when no game runs; the save and load pages list the slots
void Engine::menuListSlots() {
    MenuState& m = *menu_;
    PageView& v = *m.view;
    const Page& pg = v.page();
    v.setHotspotEnabled(pg, "yes", false, false, nullptr);
    v.setHotspotEnabled(pg, "no", false, false, nullptr);
    if (m.cur == m.pTop) {
        if (m.flags == 1) {
            v.setHotspotEnabled(pg, "jumpsave", false, false, nullptr);
            v.displayImage(-1, "greyedsave", nullptr, 0xffffffffu, false, nullptr, false);
        }
        return;
    }
    if (m.cred != 0 || m.help != 0) return;
    int save;
    if (m.cur == m.pSave) save = 1;
    else if (m.cur == m.pLoad) save = 0;
    else return;
    Surface* sf = nullptr;
    std::vector<u8>* mask = nullptr;
    std::shared_ptr<void> owner;
    PageView* view = &v;
    std::unique_ptr<Canvas> cv;
    if (v.drawTarget(-1, sf, mask, owner)) {
        cv.reset(new Canvas(*sf, [view]() -> const Palette& { return view->palette(); }, &fonts_, nullptr));
        cv->selectFont(uiFont());
        cv->setBkMode(kTransparent);
        cv->setTextColor(paletteRgb(m.bgr[2], m.bgr[1], m.bgr[0]));   // exe:00419c40: (b621 | 0x20000) << 8 | b620 << 16 | b622
    }
    // page 1: slots 1..20 are asked for as ever (only 1..10 have a rectangle); page p > 1: the ten slots of that page
    const int slotBase = m.slotPage * kSlotsPerPage;
    const int slotLast = m.slotPage == 0 ? 20 : kSlotsPerPage;
    for (int slot = 1; slot <= slotLast; slot++) {
        const std::string name = std::string(save ? "savegame" : "loadgame") + std::to_string(slot);
        int idx = -1;
        for (size_t i = 0; i < pg.hotspots.size(); i++)
            if (iequals(pg.hotspots[i].name, name)) { idx = static_cast<int>(i); break; }
        GdiRect rc{-1, -1, -1, -1};
        if (idx >= 0) {
            const Hotspot& h = pg.hotspots[static_cast<size_t>(idx)];
            rc = GdiRect{h.left, h.top, h.right, h.bottom};
        }
        int exists = 0;
        std::string text;
        logic_->onMenuQuerySavedGame(slotBase + slot, exists, text, save);       // event 32
        if (!save && exists == 0) v.setHotspotEnabled(pg, name, false, false, nullptr);
        if (!text.empty() && rc.left != -1 && cv) cv->drawText(text, &rc, DT_SINGLELINE | DT_VCENTER);   // DrawText flags 0x24
    }
    if (slotPages_ > 1 && cv) {   // port: "page 2/4" in the free top strip right of the title (nothing else of the art is there)
        GdiRect rc{440, 26, 629, 48};
        cv->drawText("page " + std::to_string(m.slotPage + 1) + "/" + std::to_string(slotPages_), &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
    }
}

// ---------------------------------------------------------------------------------------------
// mouse and keys

// exe:004178e0: the hotspot under the pointer changed: the old one is drawn as "<name>0", the new one as "<name>1"
// (a name of 10 or more characters has its last character replaced: jumpcredit -> jumpcredi0 / jumpcredi1)
void Engine::menuMove(int x, int y) {
    MenuState& m = *menu_;
    PageView& v = *m.view;
    HitInfo h = v.hit(x, y);
    const int idx = h ? h.index : -1;
    m.over = idx >= 0;
    if (idx == m.hover) return;
    const Page& pg = v.page();
    auto stateImage = [&](const std::string& hotspot, char digit) {
        std::string n = hotspot;
        if (n.empty()) return;
        if (n.size() < 10) n += digit;
        else n[n.size() - 1] = digit;
        v.displayImage(-1, n, nullptr, 0xffffffffu, false, nullptr, false);
    };
    if (m.hover >= 0 && static_cast<size_t>(m.hover) < pg.hotspots.size()) {
        const std::string old = pg.hotspots[static_cast<size_t>(m.hover)].name;
        logic_->onHotspotHover(old, kMenuWindow, false);              // event 33 (exe:004020e0)
        stateImage(old, '0');
    }
    m.hover = idx;
    if (idx >= 0) {
        const std::string now = pg.hotspots[static_cast<size_t>(idx)].name;
        logic_->onHotspotHover(now, kMenuWindow, true);
        stateImage(now, '1');
    }
}

// the tail shared by the Y / N keys of both questions (exe:004172a0)
void Engine::menuEndConfirm(bool saveKind) {
    MenuState& m = *menu_;
    PageView& v = *m.view;
    const Page& pg = v.page();
    int slot;
    if (saveKind) { m.saveConfirm = 0; slot = m.saveImg; }
    else { m.quitConfirm = 0; slot = m.quitImg; }
    v.setAllHotspotsEnabled(pg, true);                              // exe:00402a50(page, 1)
    v.setHotspotEnabled(pg, "yes", false, false, nullptr);
    v.setHotspotEnabled(pg, "no", false, false, nullptr);
    if (slot == -1) {
        v.redrawPage();                                             // TCPage::ReRender
        menuListSlots();
    } else {
        v.releaseImageSlot(-1, slot);                               // exe:00403250
    }
    menuMove(ptrX_, ptrY_);
}

// exe:004172a0 (WM_KEYDOWN while the menu is up; auto-repeats are not delivered here)
void Engine::menuKey(int vkey) {
    MenuState& m = *menu_;
    if ((vkey == kVkPageUp || vkey == kVkPageDown) && slotPages_ > 1) {   // port: the next / previous page of save slots
        if (menuSlotPage() != 0 && m.saveConfirm == 0 && m.quitConfirm == 0) {
            const int np = std::clamp(m.slotPage + (vkey == kVkPageDown ? 1 : -1), 0, slotPages_ - 1);
            if (np != m.slotPage) {
                m.slotPage = np;
                menuLoadPage(m.cur);
            }
        }
        return;
    }
    if (vkey == kVkEscape) {
        if (m.cur == m.pTop) {
            closeMenu(true);                                        // exe:004469c0(0)
            logic_->onMenuCommand(0, 0);                            // event 31, code 0: the game goes on
            return;
        }
        m.cred = 0;
        m.help = 0;
        m.saveConfirm = 0;
        m.quitConfirm = 0;
        menuLoadPage(m.pTop);
        return;
    }
    if (m.cred != 0 && vkey == kVkSpace) {                          // the next credits page
        const int n = m.view->findPage("menucred" + std::to_string(m.cred + 1));
        if (n == 0 || !menuLoadPage(n)) {
            m.cred = 0;
            menuLoadPage(m.pTop);
            return;
        }
        m.cred++;
    }
    if (m.help != 0 && vkey == kVkSpace) {
        const int n = m.view->findPage("menuhelp" + std::to_string(m.help + 1));
        if (n == 0 || !menuLoadPage(n)) {
            m.help = 0;
            menuLoadPage(m.pTop);
            return;
        }
        m.help++;
    }
    if (m.saveConfirm == 1) {
        if (vkey == kVkY) {
            const int slot = m.saveSlot;
            menuEndConfirm(true);
            logic_->onMenuCommand(2, slot);                         // save into the slot
            return;
        }
        if (vkey == kVkN) menuEndConfirm(true);
    }
    if (m.quitConfirm == 1) {
        if (vkey == kVkY) {
            menuEndConfirm(false);
            logic_->onMenuCommand(4, 0);                            // quit
            return;
        }
        if (vkey == kVkN) menuEndConfirm(false);
    }
}

// exe:004184e0: a hotspot of a menu page was clicked
void Engine::menuHotspot(const std::string& name) {
    MenuState& m = *menu_;
    PageView& v = *m.view;
    const Page& pg = v.page();
    auto question = [&](const char* image, int& flag, int& slotOut) {
        int slot = -1;
        if (v.displayImage(-1, image, nullptr, 0xffffffffu, true, &slot, false) != 0) return false;
        slotOut = slot;
        flag = 1;
        v.setAllHotspotsEnabled(pg, false);                          // exe:00402a50(page, 0)
        v.setHotspotEnabled(pg, "no", true, false, nullptr);
        v.setHotspotEnabled(pg, "yes", true, false, nullptr);
        menuMove(ptrX_, ptrY_);
        return true;
    };
    if (iequals(name, "newgame")) { logic_->onMenuCommand(1, 0); return; }
    if (iequals(name, "jumpload")) { menuLoadPage(m.pLoad); return; }
    if (iequals(name, "jumpsave")) { menuLoadPage(m.pSave); return; }
    if (iequals(name, "jumpcredit")) {
        if (m.pCred1 != 0 && menuLoadPage(m.pCred1)) m.cred = 1;
        return;
    }
    if (iequals(name, "jumphelp")) {
        if (m.pHelp1 == 0) return;
        if (buttonsSwapped_) {                                       // GetSystemMetrics(SM_SWAPBUTTON): the left-handed help
            const int n = v.findPage("menuhelp1a");
            if (n != 0 && menuLoadPage(n)) { m.help = 1; return; }
        }
        if (menuLoadPage(m.pHelp1)) m.help = 1;
        return;
    }
    if (iequals(name, "exitgame")) {
        int slot = -1;
        if (question("quit", m.quitConfirm, slot)) { m.quitImg = slot; return; }
        logic_->onMenuCommand(4, 0);
        return;
    }
    if (iequals(name, "next")) {
        if (m.cred != 0 || m.help != 0) menuKey(kVkSpace);
        return;
    }
    if (iequals(name, "back")) {
        if (m.cred == 0) {
            if (m.help == 0) return;
            if (m.help == 1) { menuKey(kVkEscape); return; }
            const int n = v.findPage("menuhelp" + std::to_string(m.help - 1));
            if (n != 0 && menuLoadPage(n)) m.help--;
        } else {
            if (m.cred == 1) { menuKey(kVkEscape); return; }
            const int n = v.findPage("menucred" + std::to_string(m.cred - 1));
            if (n != 0 && menuLoadPage(n)) m.cred--;
        }
        return;
    }
    if (iequals(name, "main")) { if (m.pTop != m.cur) menuKey(kVkEscape); return; }
    if (iequals(name, "done")) { if (m.pTop == m.cur) menuKey(kVkEscape); return; }
    if (iequals(name, "yes")) { menuKey(kVkY); return; }
    if (iequals(name, "no")) { menuKey(kVkN); return; }
    if (const int slotOnPage = slotOf(name, "savegame")) {
        const int slot = m.slotPage * kSlotsPerPage + slotOnPage;
        int exists = 0;
        std::string text;
        logic_->onMenuQuerySavedGame(slot, exists, text, 1);         // event 32: is the slot taken?
        if (exists == 1) {
            int img = -1;
            if (question("overwrite", m.saveConfirm, img)) {
                m.saveImg = img;
                m.saveSlot = slot;
                return;
            }
        }
        logic_->onMenuCommand(2, slot);
        return;
    }
    if (const int slot = slotOf(name, "loadgame")) {
        logic_->onMenuCommand(3, m.slotPage * kSlotsPerPage + slot);
        return;
    }
}

// ---------------------------------------------------------------------------------------------
// text entry

// exe:00414720
int Engine::beginTextInput(const int* rect, int maxLength, u8 fillColour, const u8* textColour) {
    if (textIn_.active) return 0x11;
    if (maxLength < 1 || maxLength > 8) return 0x11;
    if (view_.pageNumber() == 0 || view_.scrollable()) return 0x11;       // a page bigger than the view has no text box
    if (!rect || rect[0] < 0 || rect[1] < 0 || rect[2] > Frame::W || rect[3] > PageView::ViewH || rect[2] <= rect[0] || rect[3] <= rect[1])
        return 0x11;
    TextInput t;
    t.active = true;
    for (int i = 0; i < 4; i++) t.rect[i] = rect[i];
    t.maxLen = maxLength;
    t.fill = fillColour;
    if (textColour) {
        t.haveColour = true;
        for (int i = 0; i < 3; i++) t.colour[i] = textColour[i];
    }
    textIn_ = t;
    barMode_ = false;                                                     // FUN_00444890(main, 0)
    textInputDraw();
    return 0;
}

// exe:00414a90: the box is filled again, then the whole text drawn in it
void Engine::textInputDraw() {
    Surface& sf = view_.surface();
    const int l = std::clamp(textIn_.rect[0], 0, sf.w), t = std::clamp(textIn_.rect[1], 0, sf.h);
    const int r = std::clamp(textIn_.rect[2], 0, sf.w), b = std::clamp(textIn_.rect[3], 0, sf.h);
    for (int y = t; y < b; y++) std::fill(sf.row(y) + l, sf.row(y) + r, textIn_.fill);
    if (textIn_.text.empty()) return;
    Canvas cv(sf, [this]() -> const Palette& { return view_.palette(); }, &fonts_, nullptr);
    cv.selectFont(uiFont());
    cv.setBkMode(kTransparent);
    cv.setTextColor(textIn_.haveColour ? paletteRgb(textIn_.colour[0], textIn_.colour[1], textIn_.colour[2]) : paletteRgb(0, 0, 0));
    GdiRect rc{textIn_.rect[0], textIn_.rect[1], textIn_.rect[2], textIn_.rect[3]};
    cv.drawText(textIn_.text, &rc, DT_SINGLELINE | DT_VCENTER);
}

// exe:00414950 (the virtual-key code of a WM_KEYDOWN while the text entry is on; no OnKeyDown goes to the game)
void Engine::textInputKey(int vkey) {
    if (!textIn_.active) return;
    const bool letter = vkey >= 0x41 && vkey <= 0x5a, digit = vkey >= 0x30 && vkey <= 0x39;
    if (letter || digit || vkey == kVkSpace) {
        if (static_cast<int>(textIn_.text.size()) != textIn_.maxLen) {
            textIn_.text += static_cast<char>(vkey);
            textInputDraw();
        }
    } else if (vkey == kVkBack) {
        if (!textIn_.text.empty()) {
            textIn_.text.pop_back();
            textInputDraw();
        }
    } else if (vkey == kVkEnter) {
        endTextInput(false);
    } else if (vkey == kVkEscape) {
        endTextInput(true);
    }
}

// exe:004148f0: event 28 (cancelled, text); a cancelled entry reports no text
void Engine::endTextInput(bool cancel) {
    if (!textIn_.active) return;
    textIn_.active = false;
    const std::string text = textIn_.text;
    if (cancel) logic_->onTextInputDone(true, "");
    else logic_->onTextInputDone(false, text);
}

} // namespace biosys
