// Engine: saved games (API 0x34 / 0x35: exe:00452410 / exe:00452b70), restart (API 0x53: exe:00453da0), the start-up flow
// (title bitmap exe:00413010, jump to the first page) and the error reports of these. Spec: analysis/notes/menus_saveload.md.
#include "engine/bmp.h"
#include "engine/engine.h"
#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

namespace biosys {

namespace {

std::string lowered(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return s;
}

// "saved game 3.sav" (what the DLL asks for, any case) is written as "Saved Game 3.sav"
std::string canonicalName(const std::string& n) {
    const std::string l = lowered(n);
    const std::string head = "saved game ", tail = ".sav";
    if (l.size() > head.size() + tail.size() && l.compare(0, head.size(), head) == 0 &&
        l.compare(l.size() - tail.size(), tail.size(), tail) == 0) {
        const std::string digits = l.substr(head.size(), l.size() - head.size() - tail.size());
        if (digits.find_first_not_of("0123456789") == std::string::npos) return "Saved Game " + digits + ".sav";
    }
    return n;
}

bool isFile(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string trimSlash(std::string d) {
    while (d.size() > 1 && (d.back() == '/' || d.back() == '\\')) d.pop_back();
    return d;
}

} // namespace

void Engine::report(const std::string& userMessage, const std::string& logMessage) {
    lastReport_ = logMessage;
    message_ = logMessage;
    if (reportSink) reportSink(userMessage, logMessage);
}

std::string Engine::savePathOf(const std::string& fileName) const {
    if (saveDir_.empty()) return "";
    return trimSlash(saveDir_) + "/" + canonicalName(fileName);
}

// The file of a LoadGame: the save directory first (names are matched without regard to case), NewGame.sav also where
// setNewGamePath / the data roots have it. "" if there is none.
std::string Engine::findSaveFile(const std::string& fileName) const {
    const bool isNew = iequals(fileName, "newgame.sav");
    if (isNew && !newGamePath_.empty()) return newGamePath_;
    if (!saveDir_.empty()) {
        const std::string dir = trimSlash(saveDir_);
        const std::string direct = dir + "/" + canonicalName(fileName);
        if (isFile(direct)) return direct;
        if (DIR* d = opendir(dir.c_str())) {
            std::string found;
            while (const dirent* e = readdir(d))
                if (iequals(e->d_name, fileName) || iequals(e->d_name, canonicalName(fileName))) { found = dir + "/" + e->d_name; break; }
            closedir(d);
            if (!found.empty() && isFile(found)) return found;
        }
    }
    if (isNew) return locateNewGameFile();
    return "";
}

// ---------------------------------------------------------------------------------------------
// save (exe:00452410)

int Engine::saveGame(const std::string& fileName) {
    const std::string path = savePathOf(fileName);
    const std::string shown = path.empty() ? fileName : path;
    FILE* f = path.empty() ? nullptr : std::fopen(path.c_str(), "wb");
    if (!f) {
        report("", "Failed to open the saved-game file " + shown + " for writing.");
        return 0x1c;
    }
    if (saveFileDropped) saveFileDropped(path);          // the old file is gone (truncated): so is what described it
    auto drop = [&]() {
        std::fclose(f);
        std::remove(path.c_str());
    };
    SaveFile sf;
    sf.title = touch_.pubInfo.title;
    sf.unk5c = bs4Suspended_ ? 1 : 0;                    // DAT_004990ec: the display is suspended while the terminal is up
    sf.bs4Active = bs4_ ? 1 : 0;                         // +0x60 (exe:00452410: DAT_00498b84 != 0)
    // while the terminal is up the game page is unloaded: the name comes from the page number DAT_0049b0bc (exe:00452410), the scroll
    // from the main window
    const int primary = bs4_ ? bs4ReturnPage_ : view_.pageNumber();
    if (primary == 0) {
        drop();
        report("", "Don't have a primary page name to write to saved-game file " + shown + ".");
        return 0x1e;
    }
    sf.primaryPage = bs4_ ? view_.pageNameOf(bs4ReturnPage_) : view_.page().name;
    sf.scrollX = bs4_ ? bs4ReturnScroll_[0] : view_.scrollX();
    sf.scrollY = bs4_ ? bs4ReturnScroll_[1] : view_.scrollY();
    // GetPalette (exe:00412a00) = what the display has; while the menu is up the menu's own palette is on display and
    // exe:00446b10 answers the one the game had when the menu opened
    // exe:004106f0 while the terminal is up: the game palette saved on entry
    const Palette pal = bs4_ ? bs4SavedPalette_ : menu_ ? menu_->prePalette : view_.palette();
    sf.paletteGrabResult = 1;
    for (size_t i = 0; i < 256; i++) {
        sf.palette[i][0] = pal.c[i].r;
        sf.palette[i][1] = pal.c[i].g;
        sf.palette[i][2] = pal.c[i].b;
        sf.palette[i][3] = 0;
    }
    sf.unk47c = objs_.inputEnabled() ? 0 : 1;            // DAT_00498a78 (API 0x2e with enable == 0)
    sf.unk480 = filter_.active ? 1 : 0;                  // DAT_0049a398
    sf.bytes484[0] = filter_.a;                          // DAT_00499074 / DAT_00499884 / DAT_00498f28
    sf.bytes484[1] = filter_.b;
    sf.bytes484[2] = filter_.c;
    for (size_t k = 0; k < 10; k++) sf.pending488[k] = held_.pending488[k];
    sf.pendingPage = held_.pendingPage;
    sf.pendingHotspot = held_.pendingHotspot;
    sf.pendingFlag = held_.pendingFlag;
    sf.flag4cc = bs4Session_.initialPassword ? 0x80b71f04u : 0;     // DAT_00499080 / DAT_00497a20 / DAT_004990e8: the magic dwords of exe:00452410
    sf.flag4d0 = bs4Session_.emailPassword ? 0xff89ce43u : 0;
    sf.flag4d4 = bs4Session_.synthPassword ? 0x180eb800u : 0;
    sf.unk4d8 = bs4Session_.mailState;                              // DAT_004979f8
    sf.string4dc = bs4Session_.mailStamp.substr(0, 99);            // DAT_00497970
    if (bs4_) sf.bs4Section = bs4_->saveSection();                  // exe:00407000: u32 0x480 + the section, before the objects
    sf.unk540 = static_cast<u32>(allowTop_);             // DAT_00499bdc / DAT_00497a10 (API 0x6d)
    sf.unk544 = static_cast<u32>(allowBottom_);
    objs_.saveTo(sf);
    std::vector<u8> bytes;
    try {
        bytes = sf.serialize();
    } catch (const Error& e) {
        drop();
        report("", std::string("Failed to write to saved-game file ") + shown + ". " + e.what());
        return 0x1e;
    }
    if (std::fwrite(bytes.data(), bytes.size(), 1, f) != 1) {
        drop();
        report("", "Failed to write to saved-game file " + shown + ".");
        return 0x1d;
    }
    std::fclose(f);
    if (!logic_->onSaveGame(path)) {      // event 23: the game logic appends its blob
        std::remove(path.c_str());
        return 0x1e;
    }
    closeMenu(true);                      // exe:004469c0(0)
    return 0;
}

// ---------------------------------------------------------------------------------------------
// load (exe:00452b70)

int Engine::loadGame(const std::string& fileName, int mode) {
    const std::string path = findSaveFile(fileName);
    const std::string shown = path.empty() ? (saveDir_.empty() ? fileName : trimSlash(saveDir_) + "/" + canonicalName(fileName)) : path;
    std::vector<u8> d;
    try {
        if (path.empty()) throw Error("no file");
        d = readFile(path);
    } catch (const Error&) {
        report("", "Failed to open the saved-game file " + shown + " for reading.");
        return 0x1f;
    }
    const std::string unreadable = "Failed to read from saved-game file " + shown + ".  This probably means it is not a valid file.";
    if (d.size() < 0x548) {
        report("", unreadable);
        return 0x20;
    }
    ByteReader r(d);
    if (r.u32At(0) != 0xff5e67c0u || r.u32At(4) != 0x12 || !iequals(r.cstrAt(0x0c, 80), touch_.pubInfo.title)) {
        report("", "Saved-game file " + shown +
                       " is invalid; incorrect magic number and/or incorrect version number and/or publication title string.");
        return 0x22;
    }
    const std::string badObjects = "Invalid game objects data in the saved-game file " + shown + ".";
    size_t objOff = 0x548;                                       // where the game-objects section starts
    const bool bs4Save = r.u32At(0x60) == 1;
    if (bs4Save) {                                               // exe:00452b70: u32 size, then the BioSys 4 data
        if (d.size() < 0x548 + 4) {
            report("", unreadable);
            return 0x20;
        }
        const u32 n = r.u32At(0x548);
        if (n > d.size() - (0x548 + 4)) {
            report("", "Failed to read BioSys 4 data from saved-game file " + shown + ".");
            return 0x20;
        }
        objOff = 0x548 + 4 + n;
    }
    if (d.size() < objOff + 4) {
        report("", "Failed to read game objects data from saved-game file " + shown + ".");
        return 0x20;
    }
    {
        const u32 nObjects = r.u32At(objOff);
        if (nObjects != static_cast<u32>(objs_.objectCount())) {
            report("", badObjects);
            return 0x22;
        }
        if (nObjects != 0) {
            if (d.size() < objOff + 16) {
                report("", "Failed to read game objects data from saved-game file " + shown + ".");
                return 0x20;
            }
            const u32 nMoveable = r.u32At(objOff + 4), nZones = r.u32At(objOff + 8), nBar = r.u32At(objOff + 12);
            SaveFile probe;
            objs_.saveTo(probe);
            if (nZones != probe.zoneCount || nMoveable != probe.moveableCount || nBar > nObjects) {
                report("", badObjects);
                return 0x22;
            }
            if (d.size() < objOff + 16 + (static_cast<size_t>(nBar) + static_cast<size_t>(nObjects) * 8) * 11) {
                report("", "Failed to read game objects data from saved-game file " + shown + ".");
                return 0x20;
            }
        }
    }
    SaveFile sf;
    try {
        sf.parse(d, shown, false);
    } catch (const Error& e) {
        report("", "Failed to read game objects data from saved-game file " + shown + ". " + e.what());
        return 0x20;
    }
    // event 24: the game logic reads its part from where the engine's part ends
    if (!logic_->onLoadGame(shown, static_cast<long>(sf.sectionsEnd))) return 0x21;
    // exe:004469c0(1), exe:004457a0 (the saved return page is dropped: event 38 if there was one), exe:00444890(main, 0)
    closeMenu(false);
    simStatusHide(false);                               // exe:00452b70: FUN_004102d0(0)
    view_.dropSavedPage();
    barMode_ = false;
    lastBarSlot_ = -2;
    fade_.active = false;
    Palette saved;
    for (size_t i = 0; i < 256; i++) saved.c[i] = Color{sf.palette[i][0], sf.palette[i][1], sf.palette[i][2]};
    if (sf.paletteGrabResult == 1) {
        apiFadeToColour(0, 0, 0, 0, 0, -1, 100);
        stashPalette_.reset(new Palette(saved));        // exe:00412ab0: GetPalette answers the saved palette from here on
        view_.setKeyPalette(stashPalette_.get());       // ... and so does the colour-key look-up exe:00412530
    }
    frozen_.reset();
    stopAllMedia();                                     // exe:0045a150, exe:0043d1f0, exe:00457860
    menuPauseMedia(false);
    auto fail = [&](const std::string& log) {
        stashPalette_.reset();
        view_.setKeyPalette(nullptr);
        report("", log);
        return 0x21;
    };
    try {
        objs_.loadSave(sf);                              // exe:0044b3b0
    } catch (const Error& e) {
        return fail(std::string(e.what()));
    }
    const int page = view_.findPage(sf.primaryPage);
    if (!page) return fail("Page name '" + sf.primaryPage + "' in saved-game file " + shown + " is unknown.");
    // the BioSys 4 globals of the header: "== magic" is the flag (exe:00452b70)
    bs4Session_.initialPassword = sf.flag4cc == 0x80b71f04u;
    bs4Session_.emailPassword = sf.flag4d0 == 0xff89ce43u;
    bs4Session_.synthPassword = sf.flag4d4 == 0x180eb800u;
    bs4Session_.mailState = sf.unk4d8;
    bs4Session_.mailStamp = sf.string4dc;
    bar_.setShown(false);
    const int scroll[2] = {sf.scrollX, sf.scrollY};
    const bool wasBs4 = bs4_ != nullptr, toBs4 = sf.bs4Active == 1;
    const u8* section = sf.bs4Section.data();
    const size_t sectionSize = sf.bs4Section.size();
    try {
        view_.clearHistory();
        if (!wasBs4 && !toBs4) {
            view_.goToPage(page, false, scroll);         // JumpToPage(page, 0, 0, scroll): events 3, 7, 8, 2
        } else if (!wasBs4) {                            // a save with the terminal, the game is on a page: enter the terminal
            if (sf.paletteGrabResult == 1) {
                stashPalette_.reset();                   // exe:00412ab0(NULL)
                view_.setKeyPalette(nullptr);
            }
            if (!bs4Enter(section, sectionSize)) {       // exe:004104e0(buffer, size); the failure posts WM_CLOSE
                quit_ = true;
                return 0x21;
            }
            if (sf.paletteGrabResult == 1) bs4SavedPalette_ = saved;   // exe:00410730
            bs4ReturnPage_ = page;                       // DAT_0049b0bc, exe:00443e10
            bs4ReturnScroll_[0] = scroll[0];
            bs4ReturnScroll_[1] = scroll[1];
        } else if (!toBs4) {                             // in the terminal, a save without one: leave it for the saved page
            if (sf.paletteGrabResult == 1) {
                bs4SavedPalette_ = saved;
                stashPalette_.reset();
                view_.setKeyPalette(nullptr);
            }
            bs4ReturnPage_ = page;
            bs4ReturnScroll_[0] = scroll[0];
            bs4ReturnScroll_[1] = scroll[1];
            bs4Leave(mode);                              // exe:004105c0(mode): the jump, the fade, SetPalette, event 18 (1)
        } else {                                         // in the terminal, a save with one: exe:00407330 on the open object
            if (sf.paletteGrabResult == 1) {
                stashPalette_.reset();
                view_.setKeyPalette(nullptr);
                bs4SavedPalette_ = saved;
            }
            if (!bs4_->loadSection(section, sectionSize)) {
                quit_ = true;
                return fail("BS4: the saved terminal state could not be restored");
            }
            bs4ReturnPage_ = page;
            bs4ReturnScroll_[0] = scroll[0];
            bs4ReturnScroll_[1] = scroll[1];
        }
    } catch (const Error& e) {
        return fail(e.what());
    }
    if (!wasBs4 && !toBs4 && sf.paletteGrabResult == 1) {
        stashPalette_.reset();
        view_.setKeyPalette(nullptr);
        if (mode >= 0 && mode < 4) apiFadeToPalette(saved, 0, mode, 100);   // exe:00413140 with the speed of the API's mode
        apiSetPalette(1, 0, saved);                                          // SetPalette(1, 0, palette): event 13
    }
    // exe:00452b70 end: the globals of the header
    objs_.setInputEnabled(sf.unk47c == 0);
    filter_.active = sf.unk480 == 1;
    filter_.a = sf.bytes484[0];
    filter_.b = sf.bytes484[1];
    filter_.c = sf.bytes484[2];
    for (size_t k = 0; k < 10; k++) held_.pending488[k] = sf.pending488[k];
    held_.pendingPage = sf.pendingPage;
    held_.pendingHotspot = sf.pendingHotspot;
    held_.pendingFlag = sf.pendingFlag;
    if (held_.pendingFlag == 1 && (!view_.findPage(held_.pendingPage) || !view_.findPage(held_.pendingHotspot)))
        held_.pendingFlag = 0;                          // exe:00452b70 end: a return page the file names wrongly is no return page
    allowTop_ = static_cast<int>(sf.unk540);
    allowBottom_ = static_cast<int>(sf.unk544);
    barMode_ = false;
    updateHover();
    return 0;
}

// ---------------------------------------------------------------------------------------------
// restart (exe:00453da0)

int Engine::restartGame() {
    bs4Restart();                                        // exe:00453da0: the terminal is destroyed, the flags and the mail state cleared
    fade_.active = false;
    apiFadeToColour(0, 0, 0, 0, 0, -1, 100);
    barMode_ = false;
    lastBarSlot_ = -2;
    closeMenu(false);
    frozen_.reset();
    textIn_.active = false;
    view_.closePopup();                                  // exe:00450f50
    stopAllMedia();
    menuPauseMedia(false);
    // the globals the original resets: page jumps on (DAT_0049907c), input on (DAT_00498a78), no page filter, no scroll limits,
    // cursor shown / not forced, the BioSys 4 flags and the saved return page cleared
    objs_.setInputEnabled(true);
    filter_ = Filter();
    limitsOn_ = vLock_ = false;
    cursorShown_ = true;
    cursorOverride_ = false;
    clipManual_ = false;
    fill_.on = false;
    stashPalette_.reset();
    view_.setKeyPalette(nullptr);
    held_ = HeldHeader();
    simStatusDestroy(false);                             // exe:00453da0: FUN_00410410(0): the window is gone (the game made it once)
    view_.dropSavedPage();                               // exe:004457a0; DAT_00497968 = DAT_004990fc = 0
    view_.requestSaveOnJump(false);
    objs_.resetAllRemoved();                             // exe:00447db0 / exe:00446cd0 / TCGameObjects::Initialise
    bar_.setShown(false);
    try {                                                // LoadPublicationPalette(..., 1): PUB.PAL on display again
        const std::string pub = files_.findData("pub.pal");
        if (!pub.empty()) showPalette(Palette::load(pub));
    } catch (const Error&) {
    }
    if (logic_->onRestart() == 0) {                      // event 30; 0: the engine jumps to the start page
        try {
            jumpToStartPage();
        } catch (const Error& e) {
            quit_ = true;                                // exe:00453da0: PostMessage(WM_CLOSE)
            report("", e.what());
            return 0x11;
        }
    }
    updateHover();
    return 0;
}

// ---------------------------------------------------------------------------------------------
// start-up

// exe:00413010: IMAGES\TITLE.BMP must be 640 x 480. It is painted over the screen and stays until the first frames of the game.
bool Engine::showTitleScreen() {
    const std::string rel = touch_.pubInfo.titleBitmap;
    if (rel.empty()) return false;
    try {
        Bitmap8 bm = readBmp8(files_.requireData(rel));
        if (bm.image.w != Frame::W || bm.image.h != Frame::H) {
            report("", "Make sure that the title screen bitmap (" + rel + ") is the same size as the screen, otherwise it won't be displayed.");
            return false;
        }
        titleImage_ = std::move(bm.image);
        titlePalette_ = bm.palette;
    } catch (const Error& e) {
        report("", e.what());
        return false;
    }
    titleUp_ = true;
    titleSince_ = now_;
    view_.forcePalette(&titlePalette_);
    return true;
}

void Engine::hideTitle() {
    if (!titleUp_) return;
    titleUp_ = false;
    view_.forcePalette(nullptr);
}

// exe:00414ca0: ControlProgram::OnStartup returned 0: JumpToPage(DAT_00499c1c) = the first page of PubInfo (+0x14), the intro
void Engine::jumpToStartPage() {
    const int n = static_cast<int>(touch_.pubInfo.firstPage);
    if (n < 1 || n > view_.pageCount()) throw Error("the start page of the publication is not in the page file");
    view_.clearHistory();
    view_.goToPage(n, false);
}

// What the menu's New Game does, without the menu (OnMenuCommand code 1 loads NewGame.sav). The intro page opens the top bar
// (its xTopBar hotspot, verb 9); without the intro that is done here, before the game is loaded.
void Engine::menuNewGame() {
    view_.openTopbar();
    logic_->onMenuCommand(1, 0);
}

} // namespace biosys
