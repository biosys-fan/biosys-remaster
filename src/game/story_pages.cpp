// The special pages: CIntroPage "eIntro" (dll:10061c70, the intro movies and menu screens), CDeathPage "eDeath" (dll:1002af10)
// and CGameEndPage "eGameEnd" (dll:1006fd40, the ending). Each has the page's virtual slots (+0x1c enter, +0x24 entered, +0x30
// key, +0x34 hotspot) and an idle step that the idle event calls (dll:10021890) while the page is the game's state.
#include "game/story.h"

// DAT_1009f844: the menu slot text colour passed as the second argument of Eng_OpenMenu (dll:10076600).
static const unsigned char kMenuTextColour[4] = {0x73, 0x52, 0x00, 0x00};


namespace biosys::game {

namespace {
const int kVkEscape = 0x1b;
}

// ---------------------------------------------------------------------------------------------------------------
// CIntroPage. Stage (+0x6c): 0x14 take2 about to play, 0x1e take2 playing, 0x23 jumpstrt about to play, 0x28 jumpstrt playing,
// 0x2a pause, 0x2d intro1 about to play, 0x32 intro1 playing, 0x35 intro2 about to play, 0x39 intro2 playing, 0x3c title,
// 0x46 title shown, 0x50 / 0x5a start screen, 0x5d / 0x61 controls screen, 100 / 0x69 menu, 0x6e skip, 0x78 start game,
// 0x82 loading

// dll:10061d80 (page vtable +0x1c)
void StoryLogic::CIntroPage_OnEnter(CIntroPage&) {
    b.CEngineFX_LockPalette(0x10);
    b.CEngineFX_LockSounds(3);
    b.eng_.ShowCursor(0);
    b.eng_.EnableUserInput(0, 0);
}

// dll:10061db0 (page vtable +0x24): the intro movies are opened
void StoryLogic::CIntroPage_OnEnterPage(CIntroPage& p) {
    b.eng_.DoHotspotAction("xTopBar", -1);
    int h = -1;
    if (!b.eng_.OpenAVIMovie("movies\\intro\\take2.avi", -1, 0x46, 0, true, 0, 0, b.fx.palette.data(), h))
        b.eng_.WriteLog("CIntroPage::OnEnterPage  OpenAVIMovie 'movies\\intro\\take2.avi' failed");
    p.setUnk74(h);
    h = -1;
    if (!b.eng_.OpenAVIMovie("movies\\intro\\jumpstrt.avi", -1, 0, 0, true, 0, 0, b.fx.palette.data(), h))
        b.eng_.WriteLog("CIntroPage::OnEnterPage  OpenAVIMovie 'movies\\intro\\jumpstrt.avi' failed");
    p.setUnk78(h);
    h = -1;
    if (!b.eng_.OpenAVIMovie("movies\\intro\\intro1.avi", -1, 0, 0, true, 0, 0, b.fx.palette.data(), h))
        b.eng_.WriteLog("CIntroPage::OnEnterPage  OpenAVIMovie 'movies\\intro\\intro.avi' failed");
    p.setUnk7c(h);
    h = -1;
    if (!b.eng_.OpenAVIMovie("movies\\intro\\intro2.avi", -1, 0, 0, true, 0, 0, b.fx.palette.data(), h))
        b.eng_.WriteLog("CIntroPage::OnEnterPage  OpenAVIMovie 'movies\\intro\\intro.avi' failed");
    p.setUnk80(h);
    p.setStage(0x14);
    p.setUnk70(static_cast<int32_t>(b.tick() + 500));
}

// dll:10061ec0 CIntroPage::OnIdle (the idle event while no game runs)
int StoryLogic::CIntroPage_OnIdle(CIntroPage& p) {
    auto clearAndPalette = [&](const char* pal) { b.CEngineFX_ClearWindow(); b.CEngineFX_LoadPalette(pal, nullptr, 0, 0, 0x10, 0); };
    switch (p.stage()) {
    case 0x14:
        if (static_cast<uint32_t>(p.unk70()) < b.tick()) {
            clearAndPalette("palettes\\movies\\intro\\take2.pal");
            b.eng_.PlayAVIMovie(p.unk74(), 1);
            p.setUnk70(-1);
            p.setStage(0x1e);
        }
        return 1;
    case 0x23: {
        b.CEngineFX_ClearWindow();
        bool ok = b.CEngineFX_LoadPalette("palettes\\movies\\intro\\jumpstrt.pal", nullptr, 0, 0, 0x10, 0);
        if (!ok) b.eng_.WriteLog("jumpstrt.pal duff");
        if (!b.eng_.PlayAVIMovie(p.unk78(), 1)) { p.setStage(0x3c); return 1; }
        p.setStage(0x28);
        return 1;
    }
    case 0x2a:
        if (static_cast<uint32_t>(p.unk70()) < b.tick()) {
            p.setUnk70(-1);
            p.setStage(0x2d);
        }
        return 1;
    case 0x2d:
        clearAndPalette("palettes\\movies\\intro\\intro1.pal");
        if (!b.eng_.PlayAVIMovie(p.unk7c(), 1)) { p.setStage(0x3c); return 1; }
        p.setStage(0x32);
        return 1;
    case 0x35:
        clearAndPalette("palettes\\movies\\intro\\intro2.pal");
        if (!b.eng_.PlayAVIMovie(p.unk80(), 1)) { p.setStage(0x3c); return 1; }
        p.setStage(0x39);
        return 1;
    case 0x3c:
        b.CEngineFX_FadeToColour(0, 0, 0, 0, 100, 0x36, 0);
        b.CEngineFX_ClearWindow();
        if (b.g.fruitSoundHandle == -1) b.CEngineFX_PlayLoopedWav("sounds\\misc\\title.wav", 0, &b.g.fruitSoundHandle, nullptr, 3);
        b.CEngineFX_LoadPalette("palettes\\intro\\title.pal", nullptr, 0, 0, 0x10, 0);
        b.eng_.DisplayImage("title", -1, nullptr, 1, -1, nullptr, 0);
        p.setStage(0x46);
        p.setUnk70(static_cast<int32_t>(b.tick() + 4000));
        return 1;
    case 0x46:
        if (static_cast<uint32_t>(p.unk70()) < b.tick()) p.setStage(0x50);
        return 1;
    case 0x50:
        b.CEngineFX_FadeToColour(0, 0, 0, 0, 100, 0x36, 0);
        b.CEngineFX_ClearWindow();
        b.eng_.DisplayImage("info", -1, nullptr, 1, -1, nullptr, 0);
        b.CEngineFX_FadeToCurrent(0);
        p.setStage(0x5a);
        b.CEngineFX_HotspotEnable("controls", 1, -1);
        b.CEngineFX_HotspotEnable("intro", 0, -1);
        return 1;
    case 0x5d:
        b.CEngineFX_FadeToColour(0, 0, 0, 0, 100, 0x36, 0);
        b.CEngineFX_ClearWindow();
        b.eng_.DisplayImage("controls", -1, nullptr, 1, -1, nullptr, 0);
        b.CEngineFX_FadeToCurrent(0);
        b.CEngineFX_HotspotEnable("controls", 0, -1);
        b.CEngineFX_HotspotEnable("intro", 1, -1);
        p.setStage(0x61);
        return 1;
    case 100:
        p.setStage(0x69);
        b.eng_.api().OpenMenu("", kMenuTextColour, 1);
        return 1;
    case 0x69:
        p.setStage(0x5a);
        return 1;
    case 0x6e:
        b.CEngineFX_FadeToColour(0, 0, 0, 0, 100, 0x36, 0);
        b.CEngineFX_ClearWindow();
        if (b.g.fruitSoundHandle == -1) b.CEngineFX_PlayLoopedWav("sounds\\misc\\title.wav", 0, &b.g.fruitSoundHandle, nullptr, 3);
        b.CEngineFX_LoadPalette("palettes\\intro\\title.pal", nullptr, 0, 0, 0x10, 0);
        b.eng_.DisplayImage("info", -1, nullptr, 0, -1, nullptr, 0);
        p.setStage(0x5a);
        b.CEngineFX_HotspotEnable("controls", 1, -1);
        b.CEngineFX_HotspotEnable("intro", 0, -1);
        b.eng_.api().OpenMenu("", kMenuTextColour, 1);
        return 1;
    case 0x78:
        b.g.startupPhase = 2;                  // DAT_100c3a38
        b.save_->engineFx.m_wState = 0;        // DAT_100c1860 = 0
        b.fx.soundLock = 0;                    // _DAT_100c1874 = 0
        b.g.gameRunning = true;                // DAT_1009f824
        b.g.forcePalette = true;               // DAT_100c3630
        p.setStage(0x82);
        b.eng_.DisplayImage("infoload", -1, nullptr, 1, -1, nullptr, 0);
        b.g.redrawPending = true;              // DAT_100c3220
        b.g.silent = true;                     // DAT_1009f83c
        b.CWorld_StartPageFX(0x65);
        b.g.silent = false;
        b.CEngineFX_JumpToPage("R4", 0x50a, 0x134, 1, 0);
        return 1;
    default:
        return 1;
    }
}

// dll:10062460 (page vtable +0x30): a key was pressed (Escape skips)
int StoryLogic::CIntroPage_OnKey(CIntroPage& p, int vkey) {
    auto skip = [&](int next) { p.setStage(static_cast<uint16_t>(vkey == kVkEscape ? 0x6e : next)); return 1; };
    switch (p.stage()) {
    case 10: case 0x14:
        return skip(0x3c);
    case 0x1e:
        b.eng_.StopAVIMovie(p.unk74());
        return skip(0x23);
    case 0x28:
        b.eng_.StopAVIMovie(p.unk78());
        return skip(0x2d);
    case 0x2a:
        return skip(0x2d);
    case 0x32:
        b.eng_.StopAVIMovie(p.unk7c());
        return skip(0x3c);
    case 0x39:
        b.eng_.StopAVIMovie(p.unk80());
        return skip(0x3c);
    case 0x46:
        return skip(0x50);
    case 0x5a: case 0x61:
        p.setStage(vkey == kVkEscape ? 100 : 0x78);
        return 1;
    default:
        return 1;
    }
}

// dll:10062650: a movie of the intro ended (kind 1)
int StoryLogic::CIntroPage_OnMovieEnd(CIntroPage& p, int, int kind) {
    if (kind != 1) return 0;
    switch (p.stage()) {
    case 0x1e: p.setStage(0x23); return 0;
    case 0x28:
        p.setStage(0x2a);
        p.setUnk70(static_cast<int32_t>(b.tick() + 3000));
        return 0;
    case 0x32: p.setStage(0x35); return 0;
    case 0x39: p.setStage(0x3c); return 0;
    default: return 0;
    }
}

// dll:100626f0 (page vtable +0x34): the hotspots of the title screens
int StoryLogic::CIntroPage_OnHotspot(CIntroPage& p, const std::string& name) {
    if (iequals(name, "controls")) { p.setStage(0x5d); return 1; }
    if (iequals(name, "startgame")) { p.setStage(0x78); return 1; }
    p.setStage(0x50);
    return 1;
}

// ---------------------------------------------------------------------------------------------------------------
// CDeathPage. Stage (+0x6c): 1 movie plays, 2 movie ended (the screen is built), 3 shown, 4 menu opened

// dll:1002b0b0 (page vtable +0x1c)
void StoryLogic::CDeathPage_OnEnter(CDeathPage& p) {
    p.setStage(1);
    b.CEngineFX_ImageVisibility("image", 0, -1);
    b.save_->engineFx.m_wState = 0;                                   // DAT_100c1860 = 0 (CEngineFX +0x568, the palette owner)
    Self_CloseStatusScreen();
    Self_RefreshPage();
    if (me().m_hBreathingLoop != -1) {                                // DAT_100c246c (CSelf+0x734)
        b.CEngineFX_CloseSound(&me().m_hBreathingLoop);
        me().m_hBreathingLoop = -1;
    }
    if (me().m_hHeartBeatLoop != -1) {                                // DAT_100c24b0 (CSelf+0x778)
        b.CEngineFX_CloseSound(&me().m_hHeartBeatLoop);
        me().m_hHeartBeatLoop = -1;
    }
    b.CEngineFX_ClearWindow();
    Self_CloseStatusScreen();
    Self_RefreshPage();
    const char* movie;
    if (me().m_wCauseOfDeath == 0x14) {
        b.CEngineFX_LoadPalette("palettes\\death\\subtech.pal", nullptr, 0, 0, 0xc, 0);
        movie = "movies\\death\\subtech.avi";
    } else {
        b.CEngineFX_LoadPalette("palettes\\death\\death.pal", nullptr, 0, 0, 0xc, 0);
        movie = "movies\\death\\death.avi";
    }
    bool ok = b.eng_.OpenAVIMovie(movie, -1, 0, 0, true, 0, 0, b.fx.palette.data(), deathMovie_);
    CStory_CalcScore();
    b.eng_.EnableUserInput(0, 0);
    b.eng_.ShowCursor(0);
    if (!ok) {
        p.setStage(2);
        return;
    }
    b.fx.aviHandle = deathMovie_;                                     // DAT_100c18f0
    b.eng_.PrepareSynthAVIMovie(deathMovie_);
    b.eng_.PlayAVIMovie(deathMovie_, 1);
}

// dll:1002b220: the death movie ended
int StoryLogic::CDeathPage_OnMovieEnd(CDeathPage& p, int handle, int kind) {
    if (deathMovie_ == handle && kind == 1) p.setStage(2);
    return 0;
}

// dll:1002b240 CDeathPage::OnIdle: the screen of the death (the cause as an image, "badend" when the biosphere is below 80 %)
int StoryLogic::CDeathPage_OnIdle(CDeathPage& p) {
    switch (p.stage()) {
    case 1: case 3: return 1;
    case 4: p.setStage(3); return 1;
    case 2: break;
    default: return 0;
    }
    b.eng_.DisplayImage("image", -1, nullptr, 0, -1, nullptr, 0);
    b.eng_.DisplayImage("20april", -1, nullptr, 0, -1, nullptr, 0);
    if (biosphereHealth < 80.0) b.eng_.DisplayImage("badend", -1, nullptr, 0, -1, nullptr, 0);
    const char* img = nullptr;
    switch (me().m_wCauseOfDeath) {
    case 1: img = "exposure"; break;
    case 2: img = "malaria"; break;
    case 3: img = "heart"; break;
    case 4: img = "malnu"; break;
    case 5: img = "manioc"; break;
    case 6: img = "sleep"; break;
    case 7: img = "smoke"; break;
    case 8: img = "synth"; break;
    case 9: img = "bees"; break;
    case 0xc: img = "dehydrtn"; break;
    case 0xd: img = "heatstrk"; break;
    case 0xe: img = "yams"; break;
    case 0xf: img = "stimlnts"; break;
    case 0x11: img = "spider"; break;
    case 0x12: img = "asphyx"; break;
    case 0x13: img = "electrctn"; break;
    case 0x14: img = "subtech"; break;
    case 0x15: img = "blownup"; break;
    default: break;
    }
    if (img) b.eng_.DisplayImage(img, -1, nullptr, 0, -1, nullptr, 0);
    b.eng_.RedrawScreen();
    p.setStage(3);
    return 1;
}

// dll:1002b4e0 (page vtable +0x30): Escape on the shown screen opens the menu
int StoryLogic::CDeathPage_OnKey(CDeathPage& p, int vkey) {
    if (vkey == kVkEscape && p.stage() == 3) {
        p.setStage(4);
        b.eng_.api().OpenMenu("", kMenuTextColour, 1);
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------------------------
// CGameEndPage. Stage (+0x6c): 0 walking (5 s), 1..4 the static screens, 5 the last one

// dll:1006fe50 (page vtable +0x18): the page answers to "eGameEnd_<n>"
bool StoryLogic::CGameEndPage_MatchesName(const CGameEndPage& p, const std::string& name) {
    std::string n = name;
    size_t u = n.find('_');
    if (u != std::string::npos) n.resize(u);
    return iequals(p.name(), n);
}

// dll:1006fed0 (page vtable +0x1c): the first entry ends the game and works out the score, the others count the screens
void StoryLogic::CGameEndPage_OnEnter(CGameEndPage& p) {
    if (!b.g.gameEnd) {
        p.setStage(0);
        b.CWorld_LocalFxStop();                          // FUN_10014390
        b.save_->engineFx.m_wState = 0;                  // DAT_100c1860 = 0
        b.CEngineFX_ClearWindow();
        Self_CloseStatusScreen();
        Self_RefreshPage();
        b.g.gameEnd = true;                              // DAT_100c2724
        CStory_CalcScore();
        gameEndMovieDone_ = false;
    } else {
        p.setStage(p.stage() + 1);
    }
    b.eng_.EnableUserInput(0, 0);
    if (p.stage() == 0 || p.stage() == 5) b.eng_.ShowCursor(0);
    gameEndJump_ = false;                                // DAT_100bd8a0
}

// dll:1006ff60 (page vtable +0x20, after the page is drawn): the walking sound of the first screen
void StoryLogic::CGameEndPage_OnEntered(CGameEndPage& p) {
    if (p.stage() != 0) return;
    gameEndLoop_ = -1;
    b.CEngineFX_PlayLoopedWav("sounds\\gameend\\walking.wav", 0, &gameEndLoop_, nullptr, 0);
    b.CEngineFX_LoadPalette("palettes\\misc\\gameend.pal", "lookups\\misc\\gameend.xft", 0, 0, 0xd, 0);
    gameEndDeadline_ = b.tick() + 5000;
}

// dll:1006ffd0: the ending movie ended
int StoryLogic::CGameEndPage_OnMovieEnd(int handle, int kind) {
    if (kind == 1 && gameEndMovie_ == handle) {
        gameEndMovieDone_ = true;
        gameEndJump_ = true;
        gameEndNext_ = "eGameEnd_5";
        b.CEngineFX_SetVolume(gameEndLoop_, 0);
    }
    return 0;
}

// dll:10070040 CGameEndPage::OnIdle: after 5 s of walking the sound changes and the page goes on to "eGameEnd_1"; a pending
// jump (from a hotspot or the movie) is done here
int StoryLogic::CGameEndPage_OnIdle(CGameEndPage& p) {
    gameEndMovieDone_ = false;                           // _DAT_100bd8a4 = 0
    if (gameEndJump_) {
        gameEndJump_ = false;
        b.CEngineFX_JumpToPage(gameEndNext_, 0, 0, 1, 0);
        b.CEngineFX_LoadPalette("palettes\\misc\\gameend.pal", "lookups\\misc\\gameend.xft", 0, 1, 0xd, 0);
        return 0;
    }
    if (p.stage() == 0 && gameEndDeadline_ < b.tick()) {
        b.CEngineFX_FadeToColour(0, 0, 0, 1, 100, 0x36, 0);
        b.CEngineFX_ClearWindow();
        b.CEngineFX_CloseSound(&gameEndLoop_);
        b.CEngineFX_PlayLoopedWav("sounds\\gameend\\muzak.wav", 0, &gameEndLoop_, nullptr, 0);
        gameEndJump_ = true;
        gameEndNext_ = "eGameEnd_1";
    }
    return 0;
}

// dll:10070140 (page vtable +0x34): "eMovie" plays the ending movie, every other hotspot goes to the page of that name
int StoryLogic::CGameEndPage_OnHotspot(CGameEndPage&, const std::string& name) {
    if (iequals(name, "eMovie")) {
        b.eng_.ShowCursor(0);
        b.CEngineFX_FadeToColour(0, 0, 0, 1, 100, 0x36, 0);
        b.CEngineFX_ClearWindow();
        b.CEngineFX_LoadPalette("palettes\\movies\\end\\end.pal", nullptr, 0, 0, 0xd, 0);
        if (b.eng_.OpenAVIMovie("movies\\end\\end.avi", -1, 0, 0, true, 0, 0, b.fx.palette.data(), gameEndMovie_)) {
            b.CEngineFX_SetVolume(gameEndLoop_, -10000);
            b.eng_.PlayAVIMovie(gameEndMovie_, 1);
            return 1;
        }
        return 1;
    }
    b.CEngineFX_FadeToColour(0, 0, 0, 1, 100, 0x36, 0);
    b.CEngineFX_ClearWindow();
    gameEndJump_ = true;
    gameEndNext_ = name;
    return 1;
}

// the media end of the special pages (vtable +0x3c of the three pages): kind 1 = movie
bool StoryLogic::pageOnMediaEnd(CPage& p, int handle, int kind) {
    if (auto* ip = dynamic_cast<CIntroPage*>(&p)) return CIntroPage_OnMovieEnd(*ip, handle, kind) != 0;
    if (auto* dp = dynamic_cast<CDeathPage*>(&p)) return CDeathPage_OnMovieEnd(*dp, handle, kind) != 0;
    if (dynamic_cast<CGameEndPage*>(&p)) return CGameEndPage_OnMovieEnd(handle, kind) != 0;
    return false;
}

// dll:10021890: the idle event's three special cases (the pages are looked up once: DAT_100a1f1c / 1f20 / 1f24)
int StoryLogic::idleSpecialPage(int which) {
    if (which == 0) {
        if (!intro_) intro_ = dynamic_cast<CIntroPage*>(world().findPage("eIntro"));
        return intro_ ? CIntroPage_OnIdle(*intro_) : 0;
    }
    if (which == 1) {
        if (!gameEnd_) gameEnd_ = dynamic_cast<CGameEndPage*>(world().findPage("eGameEnd"));
        return gameEnd_ ? CGameEndPage_OnIdle(*gameEnd_) : 0;
    }
    if (!death_) death_ = dynamic_cast<CDeathPage*>(world().findPage("eDeath"));
    return death_ ? CDeathPage_OnIdle(*death_) : 0;
}

// page vtable +0x30
int StoryLogic::pageOnKey(CPage& p, int vkey) {
    if (auto* ip = dynamic_cast<CIntroPage*>(&p)) return CIntroPage_OnKey(*ip, vkey);
    if (auto* dp = dynamic_cast<CDeathPage*>(&p)) return CDeathPage_OnKey(*dp, vkey);
    if (auto* gp = dynamic_cast<CGameEndPage*>(&p)) return gp->stage() == 5 ? 0 : 1;      // dll:10070130
    return 0;
}

} // namespace biosys::game
