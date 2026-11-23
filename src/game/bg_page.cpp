// Pages: CPage (dll:1000f370..1000fd20, the page's effect lists), the special pages' virtual slots, and the three event
// handlers that tie the engine's page jump to the world: Event_OnPageLoaded -> CSelf::OnEnterPage (dll:1006e390),
// Event_OnPageDrawn (dll:1006e900), Event_OnEnterPage (dll:1006eca0), Event_OnExitPage (dll:1006ec20).
#include "game/biosgame.h"
#include "game/story.h"
#include <cstring>

namespace biosys::game {

namespace {
std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
}

// The player's page name (+0xec) as text
static std::string locText(const Self& s) { return std::string(reinterpret_cast<const char*>(s.m_szLocation.data())); }

// ---------------------------------------------------------------------------------------------------------------
// CPage

// dll:1000f730 (FUN_1000f730): the page's FX: start those that may, then play all
void BiosGame::CPage_UpdateFx(CPage& p) {
    std::vector<CSpotFX*> list = spotFxOf(p.fx());
    for (CSpotFX* f : list)
        if (CSpotFX_CanStart(*f)) fxStart(*f);
    for (CSpotFX* f : list) fxPlay(*f);
}

// dll:1000f8e0 (FUN_1000f8e0): one random occasional looped FX, 80 % of the calls
void BiosGame::CPage_UpdateOccLoopedFx(CPage& p) {
    const std::vector<Item*>& l = p.occLoopedFx();
    if (l.empty()) return;
    if (!chancePercent(80.0)) return;
    int i = crand() % static_cast<int>(l.size());
    CSpotFX& f = as<CSpotFX>(l[static_cast<size_t>(i)]);
    if (CSpotFX_CanStart(f)) {
        if (fxStart(f)) fxPlay(f);
    }
}

// dll:1000f7f0 (FUN_1000f7f0, called from OnIdle at most once a second): one random occasional FX, at least 4 s apart,
// 20 % of the tries; only while no other occasional FX of the page plays (+0x48)
void BiosGame::CPage_UpdateOccFx(CPage& p) {
    const std::vector<Item*>& l = p.occFx();
    if (p.unk48() != 0 || l.empty()) return;
    u32 now = tick();
    if (now - g.lastOccFxTick < 4000) return;
    if (!chancePercent(20.0)) return;
    g.lastOccFxTick = now;
    int i = crand() % static_cast<int>(l.size());
    CSpotFX* f = &as<CSpotFX>(l[static_cast<size_t>(i)]);
    if (f == g.lastOccFx) return;
    if (!CSpotFX_CanStart(*f)) return;
    if (f->handle() == -1 && !fxStart(*f)) return;
    if (fxPlay(*f)) {
        g.lastOccFx = f;
        p.setUnk48(1);
    }
}

// dll:1000f460 (vtable +0x20)
void BiosGame::CPage_Update(CPage& p) {
    CPage_UpdateFx(p);
    CPage_UpdateOccLoopedFx(p);
}

// the Y<page>P_<n> / P<page>P_<n> hotspots a page disables on entry (inlined in dll:1000f370 and dll:1000f570)
static void disablePageHotspots(Eng& eng, const std::string& page) {
    int n = 0;
    for (;;) {
        std::string name;
        do {
            n++;
            name = "Y" + page + "P_" + std::to_string(n);
        } while (eng.EnableHotspot(name, -1, 0, nullptr, 0));
        name[0] = 'P';
        if (!eng.EnableHotspot(name, -1, 0, nullptr, 0)) break;
    }
}

// dll:1000f370 CPage::OnEnter (vtable +0x1c)
void BiosGame::CPage_OnEnter(CPage& p) {
    p.setUnk48(1);
    g.lastOccFx = nullptr;
    disablePageHotspots(eng_, p.name());
    for (CSpotFX* f : spotFxOf(p.fx())) {
        if ((f->flags() & 0x20000000) != 0) f->setOn(true);
        else if ((f->flags() & 0x40000000) != 0) f->setOn(false);
    }
    for (CSpotFX* f : spotFxOf(p.occFx()))
        if (CSpotFX_CanStart(*f)) fxStart(*f);
}

// dll:1000f4a0 CPage::OnExit (vtable +0x28)
void BiosGame::CPage_Exit(CPage& p) {
    for (CSpotFX* f : spotFxOf(p.fx())) {
        if ((f->flags() & 4) != 0) fxStop(*f);
        fxReset(*f);
    }
    for (CSpotFX* f : spotFxOf(p.occFx())) fxClose(*f);
    for (CSpotFX* f : spotFxOf(p.occLoopedFx())) fxReset(*f);
}

// dll:1000f570 (vtable +0x2c): after a game was loaded the page's effects start from scratch
void BiosGame::CPage_AfterLoad(CPage& p) {
    if (curPage() != &p) {
        for (CSpotFX* f : spotFxOf(p.occFx())) f->setHandle(-1);
        for (CSpotFX* f : spotFxOf(p.occLoopedFx())) f->setHandle(-1);
        for (CSpotFX* f : spotFxOf(p.fx())) f->setHandle(-1);
    }
    g.lastOccFx = nullptr;
    disablePageHotspots(eng_, p.name());
    for (CSpotFX* f : spotFxOf(p.fx())) {
        if ((f->flags() & 0x20000000) != 0) f->setOn(true);
        else if ((f->flags() & 0x40000000) != 0) f->setOn(false);
    }
    for (CSpotFX* f : spotFxOf(p.occFx()))
        if (CSpotFX_CanStart(*f)) fxStart(*f);
    CPage_UpdateFx(p);
    CPage_UpdateOccLoopedFx(p);
}

// dll:1000fc00 (vtable +0x38): the engine started media `handle` of type `mask`: which FX is it?
bool BiosGame::CPage_OnMediaStarted(CPage& p, int handle, unsigned mask) {
    for (CSpotFX* f : spotFxOf(p.occFx()))
        if (CSpotFX_OnStarted(*f, handle, mask)) return true;
    for (CSpotFX* f : spotFxOf(p.occLoopedFx()))
        if (CSpotFX_OnStarted(*f, handle, mask)) return true;
    for (CSpotFX* f : spotFxOf(p.fx()))
        if (CSpotFX_OnStarted(*f, handle, mask)) return true;
    return false;
}

// dll:1000fcc0 (vtable +0x3c): the engine reports that media ended: only the occasional FX are told; the page is free
// for the next one
bool BiosGame::CPage_OnMediaStopped(CPage& p, int handle, unsigned mask) {
    if (dynamic_cast<CIntroPage*>(&p) || dynamic_cast<CDeathPage*>(&p) || dynamic_cast<CGameEndPage*>(&p))
        return story_->pageOnMediaEnd(p, handle, mask);     // the special pages' own slot +0x3c (dll:10062650, 1002b220, 1006ffd0)
    for (CSpotFX* f : spotFxOf(p.occFx())) {
        if (CSpotFX_OnStopped(*f, handle, mask)) {
            p.setUnk48(0);
            return true;
        }
    }
    return false;
}

// dll:1000fd20 (vtable +0x40): media closed
bool BiosGame::CPage_OnMediaClosed(CPage& p, int handle, unsigned mask) {
    for (CSpotFX* f : spotFxOf(p.occFx())) {
        if (fxOnClosed(*f, handle, mask)) {
            p.setUnk48(0);
            return true;
        }
    }
    for (CSpotFX* f : spotFxOf(p.occLoopedFx()))
        if (fxOnClosed(*f, handle, mask)) return true;
    for (CSpotFX* f : spotFxOf(p.fx()))
        if (fxOnClosed(*f, handle, mask)) return true;
    return false;
}

// The special pages' vtable slot +0x1c (OnEnter). CIntroPage dll:10061d80 is translated; CDeathPage dll:1002b0b0 and
// CGameEndPage dll:1006fed0 belong to the story / ending code.
void BiosGame::pageOnEnter(CPage& p) {
    if (auto* ip = dynamic_cast<CIntroPage*>(&p)) {
        story_->CIntroPage_OnEnter(*ip);                    // dll:10061d80
        return;
    }
    if (auto* dp = dynamic_cast<CDeathPage*>(&p)) {
        story_->CDeathPage_OnEnter(*dp);                    // dll:1002b0b0
        return;
    }
    if (auto* gp = dynamic_cast<CGameEndPage*>(&p)) {
        story_->CGameEndPage_OnEnter(*gp);                  // dll:1006fed0
        return;
    }
    CPage_OnEnter(p);
}

// vtable +0x20 of the page (OnPageDrawn calls it)
void BiosGame::pageUpdate(CPage& p) {
    if (auto* gp = dynamic_cast<CGameEndPage*>(&p)) {
        story_->CGameEndPage_OnEntered(*gp);                // vtable 10093cc0 +0x20: dll:1006ff60 (the intro and death pages use dll:1000f460)
        return;
    }
    CPage_Update(p);
}

// vtable +0x28 of the page
void BiosGame::pageExit(CPage& p) {
    CPage_Exit(p);       // the special pages' vtable +0x28 is the generic one (dll:1000f4a0, dll:1006fec0 for the game end page is empty)
}

// ---------------------------------------------------------------------------------------------------------------
// the handlers

// dll:1006e390 CSelf::OnEnterPage: Event_OnPageLoaded. The page was read, nothing drawn yet. The first letter of the
// page name tells what kind of page it is (A plants, F / U features, L paths, M movies, N, Z sleep, the rest sites).
void BiosGame::CSelf_OnEnterPage(const std::string& pageName) {
    Self& me = self();
    me.m_dwHotspotExpiry = 0;     // +0x294
    me.m_dwUnk298 = 0;
    me.m_dwUnk2a0 = 0;            // (+0x29c, +0x2a0)
    std::string loc = upper(pageName);
    if (loc.size() > 10) loc.resize(10);
    me.m_szLocation.fill(0);
    std::memcpy(me.m_szLocation.data(), loc.data(), loc.size());
    gs().features.currentSiteName = loc;
    story_->CSynthHandler_OnEnterPage(loc);                 // dll:10008830
    story_->CSubTechHandler_OnEnterPage();                  // dll:10022c00 FUN_10022c00(&DAT_100c24b8): state 1
    bool tail = true;
    switch (loc.empty() ? '\0' : loc[0]) {
    case 'A': {
        me.m_pPage = nullptr;
        if (CFeature* cf = dynamic_cast<CFeature*>(me.m_pFeature.item)) cf->onLeave();   // vtable +0x4c of the current feature
        me.m_pFeature.clear();
        CPlant* pl = world().findPlant(loc.c_str());
        me.m_pPlant.set(pl);
        if (pl) {
            CPlant_OnEnter(*pl);                       // dll:10072930
        }
        break;
    }
    case 'B': case 'C': case 'D': case 'E': case 'O': case 'P': case 'R': case 'S': case 'T': case 'W': {
        me.m_pPlant.clear();
        CSite* site = world().findSite(loc.c_str());
        CSite* prev = dynamic_cast<CSite*>(me.m_pSite.item);
        if (site != prev && prev) CSite_Exit(*prev);
        if (site) {
            me.m_pSite.set(site);
            me.m_pCel = site->cel();
            me.m_pBiome = site->biome();
            CSite_UpdateDisplayState(*site, loc);
        }
        if ((me.m_dwChampagne != 0 && static_cast<double>(static_cast<uint32_t>(me.m_dwChampagne)) < 3600.0) ||
            (me.m_dwMescal != 0 && static_cast<double>(static_cast<uint32_t>(me.m_dwMescal)) < 3600.0))
            eng_.EnableMouseDeltaEvent(1);
        if (!g.alarm) g.alarm = world().findObject("alarm");
        if (auto* alarm = dynamic_cast<GameObj*>(g.alarm)) alarm->onPageEnter(me, pageName);   // vtable +0x80(page name)
        break;
    }
    case 'F': {
        me.m_pPlant.clear();
        CFeature* f = world().findFeature(loc.c_str());
        CFeature* old = dynamic_cast<CFeature*>(me.m_pFeature.item);
        if (old && f != old) old->onLeave();   // vtable +0x4c of the old feature
        me.m_pFeature.set(f);
        if (f) {
            f->onEnterPage(pageName);   // vtable +0x34(name): enter the feature's page
            if (f->unk54() != 0) {
                playerHost_->clearWindow(me.m_lTopBarHandle, 0);   // dll:1006c790
                eng_.EnableUserInput(0, 0);
                return;
            }
        }
        break;
    }
    case 'U': {
        me.m_pPlant.clear();
        CFeature* f = dynamic_cast<CFeature*>(me.m_pFeature.item);
        if (f) {
            f->onEnterPage(pageName);   // vtable +0x34(name) of the current feature
            if (f->unk54() != 0) {
                eng_.EnableUserInput(0, 0);
                return;
            }
        }
        break;
    }
    case 'L':
        me.m_pPage = nullptr;
        if (CPath* jp = dynamic_cast<CPath*>(me.m_pPath)) story_->CPath_Arrive(*jp);     // dll:100014e0 (the journey's page)
        return;
    case 'M':
        log("WARNING: demo page " + loc + " displayed using non-demo verion of dll");
        g.silent = true;                          // DAT_1009f83c
        CTimeline_SetTicksPerGameSec(-1);
        CEngineFX_ClearWindow();
        if (iequals(loc, "MXSSINTRO")) {
            if (CSite* cs = curSite()) {
                CSite_Exit(*cs);
                CSite_CloseAllAmbient(*cs);
            }
            if (CPage* cp = curPage()) pageExit(*cp);
            CWorld_LocalFxStop();
        }
        return;
    case 'N':
        if (CSite* cs = curSite()) {
            uint32_t f = cs->flags();
            CBiomeCel* cel = curCel();
            if ((f & 0x1000) == 0) {
                if ((f & 0x400) == 0) {
                    if ((f & 0x800) != 0) CEngineFX_ImageVisibility((save_->header.milestones & 0x10000000) == 0 ? "dead" : "healthy", 1, -1);
                } else {
                    CEngineFX_ImageVisibility(gs().lightsOn == 0 ? "lightsoff" : "lightson", 1, -1);
                }
            } else {
                CEngineFX_ImageVisibility(cel && cel->plantHealthAvg() <= 0.2 ? "dead" : "healthy", 1, -1);
            }
        }
        break;
    case 'X':
        break;
    case 'Z':
        me.m_pPage = nullptr;
        me.m_bAwake = 0;                          // +0x2ac
        return;
    default:
        log("A site (" + loc + ") is FLOUTING the Naming Convention. This is really bad");
        break;
    }
    (void)tail;
    // the common end: the page object
    CPage* pg = world().findPage(pageName.c_str());
    me.m_pPage = pg;
    if (pg) pageOnEnter(*pg);
    if (me.m_pObjHolding.item) {
        if (auto* o = dynamic_cast<GameObj*>(me.m_pObjHolding.item)) o->onPageEnter(me, pageName);   // vtable +0x80
    }
    if (me.m_pAirMask.item) {
        if (auto* o = dynamic_cast<GameObj*>(me.m_pAirMask.item)) o->onPageEnter(me, pageName);   // vtable +0x80
    }
    if (me.m_pGoggles.item) {
        if (auto* o = dynamic_cast<GameObj*>(me.m_pGoggles.item)) o->onPageEnter(me, pageName);   // vtable +0x80
    }
}

// dll:1006e900 Event_OnPageDrawn
void BiosGame::CSelf_OnPageDrawn(const std::string& pageName) {
    Self& me = self();
    g.fadeFromBlackUntil = 0;                     // DAT_100c4310 = 0
    if (me.m_dwUnk2a8 == 0) me.m_dwUnk2a8 = 1;
    else {
        me.RefreshForPage();                      // dll:1006f100
    }
    const std::string loc = locText(me);
    switch (loc.empty() ? '\0' : loc[0]) {
    case 'A':
        if (CPlant* pl = dynamic_cast<CPlant*>(me.m_pPlant.item)) CPlant_DisplayAll(*pl);   // dll:10072910
        break;
    case 'B': case 'C': case 'D': case 'E': case 'O': case 'P': case 'R': case 'S': case 'T': case 'W':
        if (CSite* cs = curSite()) CSite_OnPageDrawn(*cs, loc);
        break;
    case 'F':
        if (CFeature* cf = dynamic_cast<CFeature*>(me.m_pFeature.item)) cf->onPageDrawn(pageName);   // vtable +0x38(name)
        break;
    case 'L':
        if (CPath* jp = dynamic_cast<CPath*>(me.m_pPath)) story_->CPath_BeginJourney(*jp, pageName);   // dll:10001ac0
        return;
    case 'M': {
        eng_.EnableUserInput(0, 0);
        me.RefreshForPage();                      // dll:1006f100
        const char* pal = nullptr;
        switch (loc.size() > 1 ? loc[1] : '\0') {
        case 'C': pal = "palettes\\demo\\ce.pal"; break;
        case 'D': pal = "palettes\\demo\\db.pal"; break;
        case 'G': pal = "palettes\\demo\\gogs.pal"; break;
        case 'O': pal = "palettes\\demo\\o.pal"; break;
        case 'P': pal = "palettes\\demo\\ph.pal"; break;
        case 'R': pal = "palettes\\demo\\rf.pal"; break;
        case 'S': pal = "palettes\\demo\\s.pal"; break;
        case 'X': pal = "palettes\\demo\\x.pal"; break;
        default: break;
        }
        if (pal) CEngineFX_LoadPalette(pal, nullptr, 0, 1, 0x12, 0);
        return;
    }
    case 'Z':
        if (CSite* cs = curSite()) {
            CSite_CloseAllAmbient(*cs);
            CSite_StopFx(*cs);
        }
        CWorld_LocalFxStop();
        CEngineFX_ClearWindow();
        CEngineFX_LoadPalette("palettes\\misc\\sleep.pal", nullptr, 0, 1, 7, 0);
        me.ShowMessageLine(me.m_dwSynthAttack == 1 ? 0x163 : 0x152, 0xffff);   // dll:1006c400
        eng_.EnableUserInput(0, 0);
        CEngineFX_HideCursor();
        me.m_bRunSleepOnIdle = 1;                 // +0x75c
        return;
    default:
        break;
    }
    if (me.m_pObjHolding.item) {
        if (auto* o = dynamic_cast<GameObj*>(me.m_pObjHolding.item)) o->onPageLeave(me, pageName);   // vtable +0x84
    }
    if (me.m_pAirMask.item) {
        if (auto* o = dynamic_cast<GameObj*>(me.m_pAirMask.item)) o->onPageLeave(me, pageName);   // vtable +0x84
    }
    if (me.m_pGoggles.item) {
        if (auto* o = dynamic_cast<GameObj*>(me.m_pGoggles.item)) o->onPageLeave(me, pageName);   // vtable +0x84
    }
    if (CPage* pg = curPage()) pageUpdate(*pg);
    g.rainFilter = false;                         // DAT_1009f848
    CWorld_LocalFxUpdate();
    (void)pageName;
}

// dll:1006eca0 Event_OnEnterPage (the page jump is complete)
void BiosGame::CSelf_OnEnterPageEvent(const std::string& pageName) {
    Self& me = self();
    if (me.m_bAwake == 0) return;
    if (!g.gameEnd) {
        if (!g.dead && !pageName.empty() && pageName[0] != 'L') {
            me.m_wBreathing = static_cast<u16>(me.m_wBreathing & 0xfffb);
            if (CPage* pg = curPage()) story_->pageOnEntered(*pg);        // the page's vtable +0x24 (dll:1000f480 CPage::TriggerMilestone)
            if (CFeature* cf = dynamic_cast<CFeature*>(me.m_pFeature.item)) cf->onPageEntered(pageName);   // vtable +0x3c(name)
            if (me.m_pObjHolding.item) {
                if (auto* o = dynamic_cast<GameObj*>(me.m_pObjHolding.item)) o->onPageEntered(me, pageName);   // vtable +0x88
            }
            if (fx.paletteOn) {   // DAT_100c1864 (CEngineFX +0x56c)
                playerHost_->clearWindow(me.m_lTopBarHandle, 0);   // dll:1006c790
                me.ShowMessage(1, 0, 0, 0);          // dll:1006bc40
            }
            switch (locText(me).empty() ? '\0' : locText(me)[0]) {
            case 'A': case 'C': case 'D': case 'F': case 'O': case 'R': case 'S': {
                CFeature* feat = dynamic_cast<CFeature*>(me.m_pFeature.item);
                if (g.rainFilter && (!feat || feat->unk54() == 0)) {
                    eng_.BeginPageFilter(0x76, 0x56, 0x57);
                    return;
                }
                break;
            }
            case 'M':
                eng_.DoHotspotAction("xTopBar", -1);
                break;
            default:
                break;
            }
        }
    } else if (CPage* pg = curPage()) {
        story_->pageOnEntered(*pg);                           // the page's vtable +0x24 (CGameEndPage: dll:1006ffc0, empty)
        return;
    }
}

// dll:1006ec20 Event_OnExitPage
void BiosGame::CSelf_OnExitPageEvent() {
    Self& me = self();
    if (!me.m_pPath || locText(me).empty() || locText(me)[0] != 'L') {
        if (CPage* pg = curPage()) {
            pageExit(*pg);
            me.m_pPage = nullptr;
        }
    } else {
        story_->CPath_PageLeft();                             // dll:10002380 FUN_10002380() (the journey page is left)
    }
    if (me.m_bAwake != 0) {
        if (me.m_bStatusScreenVisible != 0) {
            me.CloseStatusScreen();               // dll:1006c7b0
        }
        if (CFeature* cf = dynamic_cast<CFeature*>(me.m_pFeature.item)) cf->onLeavePage();   // vtable +0x40 of the current feature
        me.m_dwUnk298 = 0;
        me.m_dwUnk2a0 = 0;
    }
}

} // namespace biosys::game
