// BiosGame: the events (dll:100044b0 GetEventFunctionTable lists the handlers), start-up, the new-game loader and the
// media callbacks (dll:10007600, 100077c0, 100078f0). See biosgame.h for the layout of the translation.
#include "game/biosgame.h"
#include "engine/savetrailer.h"
#include "game/self_text.gen.h"
#include "game/story.h"
#include "game/watertank.h"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sstream>
#include <sys/stat.h>

namespace biosys::game {

namespace {
std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
// where the port's own tables are when the front end names no folder: the development tree, or "data" beside the program
#ifdef BIOSYS_DEV_ROOT
const char* const kDefaultDataDir = BIOSYS_DEV_ROOT "/port/data";
#else
const char* const kDefaultDataDir = "data";
#endif
bool fileExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
} // namespace

// port constructor: the DLL's static initialisation (dll:10015070 CTimeline, dll:1005fdc0 CEngineFX) and the world builders (SaveGame)
BiosGame::BiosGame(EngineApi& api, const DataFiles& files, Config config)
    : api_(api), eng_(api), files_(files), config_(std::move(config)),
      palettes_(PaletteTable::load(config_.dataDir.empty() ? kDefaultDataDir : config_.dataDir)) {
    SaveGame::Options o;
    o.randSeed = config_.randSeed;
    o.fixes = config_.fixes;
    o.model = config_.model;
    o.tuning = config_.tuning;
    save_ = std::make_unique<SaveGame>(o);     // seeds the generator and replays the builders of Event_OnStartup
    sim_ = std::make_unique<Sim>(*save_);
    bindSimHooks();
    attachFeatureHost();
    attachPlayerHost();
    story_ = std::make_unique<StoryLogic>(*this);
    story_->ui.sink = config_.uiSink;
    CTimeline_Constructor();
    CEngineFX_Construct();
}

BiosGame::~BiosGame() = default;

bool BiosGame::chancePercent(double percent) {   // ChancePercent dll:10007570: rand() <= (int)(percent * 0.01 * 32767)
    return gs().rng.chance(percent * 0.01 * 32767.0);
}

// ---------------------------------------------------------------------------------------------------------------
// stubs of this slice (everything the translated code calls that belongs to the simulation, the player or the story)

void BiosGame::CWorld_UpdateWeather() { sim_->updateWeather(); }      // dll:10013420 (the translation is in sim.cpp)
void BiosGame::CWorld_UpdateAirFlows() { sim_->updateAirFlows(); }     // dll:10013e00
void BiosGame::CWorld_UpdateWaterFlows() { sim_->updateWaterFlows(); }   // dll:100140b0
void BiosGame::CWorld_UpdateAllObjects() { cworld::updateAllObjects(world(), self(), save_->timeline.m_dwTime); }   // dll:100146a0
void BiosGame::CWorld_UpdateAllFeatures() { cworld::updateAllFeatures(world(), save_->timeline.m_dwTime); }   // dll:100148b0
void BiosGame::CWorld_UpdateAllPlants() { sim_->updateAllPlants(); }    // dll:10014540

// dll:10029990 COcean::UpdateState: the state step is COcean::updateState (watertank.cpp); what follows in the original
// is the display of the ocean level on the page of the site on display.
void BiosGame::COcean_UpdateState() {
    sim_->updateOcean();
    COcean& oc = sim_->ocean();
    CSite* site = curSite();
    if (oc.levelState() != sim_->oceanLevelShown && site && (site->flags() & 0x2000000) != 0) {
        sim_->oceanLevelShown = oc.levelState();
        for (const char* img : {"h_high", "h_mid", "h_low", "d_high", "d_mid", "d_low"}) CEngineFX_ImageVisibility(img, 0, -1);
        const CBiomeCel* cel = curCel();                       // DAT_100c1f94: the player's cel
        const bool healthy = cel && 0.2 < cel->plantHealthAvg();
        if (oc.waterVol() <= 11.25) {
            if (10.75 <= oc.waterVol()) {
                CEngineFX_ImageVisibility(healthy ? "h_mid" : "d_mid", 1, -1);
                site->setState(6);
            } else {
                CEngineFX_ImageVisibility(healthy ? "h_low" : "d_low", 1, -1);
                site->setState(7);
            }
        } else {
            CEngineFX_ImageVisibility(healthy ? "h_high" : "d_high", 1, -1);
            site->setState(5);
        }
        eng_.api().RedrawPage();        // PORT: Eng_RedrawPage (dll:100752a0) has no wrapper in eng.h
    }
}

// port: connects the simulation core to the shell (the calls the translated simulation code makes outside the simulation)
void BiosGame::bindSimHooks() {
    Sim::Hooks& h = sim_->hooks;
    h.log = [this](const std::string& s) { log(s); };
    h.triggerEvent = [this](uint32_t m) { CGame_TriggerEvent(m); };
    h.buildDayPalette = [this] {                                // FUN_1000e3a0(DAT_100c1f84): the original dereferences the site unchecked
        if (CSite* s = curSite()) CSite_BuildDayPalette(*s);
    };
    h.realTimeTick = [this] { CTimeline_RealTimeTick(); };
    h.playSound = [this](const char* f) { CEngineFX_PlaySound(f, 0, 0); };
    h.removeFromScene = [this](GameObj& o) { if (eng_.RemoveGameObject(o.name())) o.removeFromScene(); };   // FUN_10029510
    // featurePowerOn / featurePowerOff (feature vtable +0x68 / +0x6c), lungSwitchOff (+0x74), lungSetMoving (lung+0x1dc) and
    // ventAdjustTarget (CVent+0x60) are bound by bindFeatureHooks (featb_world.cpp)
}
void BiosGame::CSelf_Update(bool full) { self().Update(full ? 1 : 0); }   // dll:10066aa0
void BiosGame::CSelf_Die() { self().Die(0); }   // dll:10068f10 (Event_OnTimer / OnIdle pass 0; the cause is the largest loss)
void BiosGame::CSelf_OnIdle() { self().OnIdle(); }   // dll:10065e80
void BiosGame::CGame_TriggerEvent(uint32_t m) { story_->CGame_TriggerEvent(m); }   // dll:100092d0 (story_events.cpp)

// ---------------------------------------------------------------------------------------------------------------
// start-up

// dll:1001d7b0 Event_OnStartup. The world was built by the constructor (SaveGame: buildWorld = the builders this
// handler calls); this does the rest in the original's order.
int BiosGame::onStartup() {
    // (srand(GetTickCount()) happened in the SaveGame constructor with Config::randSeed)
    log("**** normal version ****");
    log("");
    log("language: ENGLISH");
    log("");
    // FUN_10009230(&CGame, "calibrating...") / FUN_10009270: the benchmark log lines
    log("benchmark: calibrating...");
    u32 t0 = tick();
    log("benchmark: " + std::to_string(tick() - t0) + "ms");
    log("benchmark: initialising...");
    t0 = tick();
    CEngineFX_Init();
    // World__InitBiomeParams, FUN_1002b700 (pages), World__BuildCelsSitesTanks, FUN_10039130 (paths), World__BuildOcean,
    // World__BuildBasementTanks, World__BuildFeatures, World__BuildAirFlows, World__BuildWaterFlows, FUN_10051a10,
    // World__BuildPlants, FUN_10057f30 (objects), FUN_1005a150 / 1005a0c0 / 1005a130: done by buildWorld (SaveGame)
    CTimeline_Start(0x7080);                   // dll:1005a150 FUN_1005a150: CTimeline::Start(0x7080), Eng_SetTimerInterval(1)
    eng_.SetTimerInterval(1);
    g.synthState = 1;                          // dll:1005a130 FUN_1005a130: DAT_100c27e4 = 1 (the synth handler's idle state)
    // dll:1005a0c0: the sim-status window the two status screens are drawn on, and the palette they put on display
    eng_.CreateSimStatusWindow("status");
    g.haveStatusPalette = readPalette("palettes\\status\\status.pal", g.statusPalette);
    // dll:10008cf0 Game::InitNewGameState
    {
        cworld::powerOffFeatures(world(), 0x8000000);   // dll:10014ae0 CWorld::PowerOffFeatures
        if (CFeature* f = world().findFeature("fbo1Wave")) f->activate();   // the feature's vtable +0x78 (Activate)
        for (int i = 1; i < 8; i++) {
            if (GameObj* o = world().findObject(("baby" + std::to_string(i)).c_str()))
                if (eng_.RemoveGameObject(o->name())) o->removeFromScene();   // vtable +0x94, dll:10029510
        }
        for (int i = 1; i < 7; i++) {
            if (GameObj* o = world().findObject(("dbaby" + std::to_string(i)).c_str()))
                if (eng_.RemoveGameObject(o->name())) o->removeFromScene();
        }
        CBiomeCel& r4 = world().requireBiomeCel("R4");
        r4.setFlags(r4.flags() | 2);
    }
    // dll:10013cc0 CWorld::UpdateBiomeCels(world): the first state step of every cel. The timeline still has its constructor
    // time (1), the world stamp is 0x7080: dt wraps to 4294938497 s and every cel saturates its clamps (ORIGINAL BUG,
    // harmless: the new game overwrites the cels)
    sim_->updateBiomeCels();
    g.forcePalette = true;       // DAT_100c3630 = 1
    g.startupPhase = 2;          // DAT_100c3a38 = 2
    log("benchmark: " + std::to_string(tick() - t0) + "ms");
    started_ = true;
    return 0;
}

// dll:1001d8c0 Event_OnShutdown
void BiosGame::onShutdown() {
    CEngineFX_Shutdown();       // FUN_1005ff10
    CWorld_LocalFxReset();      // FUN_10014310
}

// ---------------------------------------------------------------------------------------------------------------
// saved games. The engine writes and reads its part of a .sav (header, game objects) and calls the three events below for
// the game's own part, the blob that follows it (SaveGame container). A new game is a menu command: dll:10008010 case 1 asks the
// engine to load "newgame.sav" (BiosGame::onMenuCommand); the menu is the engine's.
// ---------------------------------------------------------------------------------------------------------------

// dll:10021cc0 Event_OnSaveGame: the file is opened for appending (the engine wrote its part) and CGame::Save dll:10008db0 appends
// the blob: first the world is brought up to date, then the description for the menu is made
bool BiosGame::onSaveGame(const std::string& fileName) {
    CWorld_UpdateWeather();
    COcean_UpdateState();
    CWorld_UpdateAirFlows();
    CWorld_UpdateWaterFlows();
    CWorld_UpdateAllObjects();
    CWorld_UpdateAllFeatures();
    CWorld_UpdateAllPlants();
    CSelf_Update(false);                         // CSelf::Update(self, 0)
    story_->CStory_CalcScore();
    // "Saved Game: Day " (string 0x12f), "%d, %s, " (day, clock), the site's name (string +0xe0 of the site), ", %d%% %s" (score,
    // string 0x137); the field is the 100 bytes of DAT_100c0de8
    auto text = [](int id) { const char* t = dllString(id); return std::string(t ? t : ""); };
    std::string d = text(0x12f);
    d += std::to_string(save_->timeline.m_dwDay) + ", " + tl.clock + ", ";
    if (CSite* site = curSite())
        if (site->unk0e0() != 0) d += text(static_cast<int>(site->unk0e0()));
    d += ", " + std::to_string(static_cast<int>(story_->score)) + "% " + text(0x137);
    save_->header.description.assign(100, 0);
    std::memcpy(save_->header.description.data(), d.data(), std::min<size_t>(d.size(), 99));
    const std::vector<u8> blob = save_->save();
    std::FILE* f = std::fopen(fileName.c_str(), "ab");
    if (!f) return false;
    const bool ok = std::fwrite(blob.data(), blob.size(), 1, f) == 1;
    std::fclose(f);
    return ok;
}

// dll:10021d00 Event_OnLoadGame: the file is opened, positioned at the offset the engine stopped at, and CGame::Load
// dll:10009080 reads the blob (the container checks every marker and the version; the original returns false on a bad one)
bool BiosGame::onLoadGame(const std::string& fileName, long offset) {
    bool ok = false;
    try {
        std::vector<u8> sav = readFile(fileName);
        if (offset >= 0 && static_cast<size_t>(offset) <= sav.size()) {
            // the port's game-mode block after the blob (engine/savetrailer.h) is not part of it
            const size_t end = savetrailer::blobEnd(sav, static_cast<size_t>(offset), SaveGame::blobSize());
            save_->load(std::vector<u8>(sav.begin() + offset, sav.begin() + static_cast<long>(end)));
            // CTimeline::Load dll:10015250 after reading its record: stamps the load tick and makes the clock string
            tl.lastTick = tick();
            CTimeline_UpdateTimeOfDay();
            ok = true;
        }
    } catch (const Error& e) {
        log(std::string("BiosGame.dll: could not load the saved game: ") + e.what());
    }
    g.timerPhase = 0;                            // DAT_100c42a0 = 0 (set whether or not the load worked)
    return ok;
}

// dll:10008350 Event_OnMenuQuerySavedGame: the slot's file ("saved game N.sav" in the save directory) is looked at: absent:
// "-- empty --"; else its description (100 bytes at the start of the blob + 13), "Saved Game N" if it cannot be read, "Out of date
// file. Saved Game N" for another blob version
void BiosGame::onMenuQuerySavedGame(int slot, int& outExists, std::string& outDescription, int) {
    std::string dir;
    eng_.api().GetSaveDirectory(dir);
    while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    const std::string name = "Saved Game " + std::to_string(slot) + ".sav";
    std::string path = dir + "/" + name;
    if (!fileExists(path)) {                      // the original's file names are not case sensitive
        path.clear();
        if (DIR* d = opendir(dir.c_str())) {
            while (const dirent* e = readdir(d))
                if (iequals(e->d_name, name)) { path = dir + "/" + e->d_name; break; }
            closedir(d);
        }
    }
    if (path.empty()) {
        outExists = 0;
        outDescription = "-- empty --";
        return;
    }
    outExists = 1;
    outDescription = "Saved Game " + std::to_string(slot);
    try {
        const std::vector<u8> d = readFile(path);
        ByteReader r(d);
        const size_t at = r.u32At(8) + 13;
        const std::string desc = r.cstrAt(at, 100);
        if (r.u32At(at + 100) != 0x23) outDescription = "Out of date file: Saved Game " + std::to_string(slot) + ".sav";   // dll:10008350
        else outDescription = desc;
    } catch (const Error&) {
    }
}

// Event_OnMenuCommand dll:10008010, the part of code 1 after Eng_LoadGame returned true
void BiosGame::afterNewGameLoaded() {
    if (CSite* s = curSite()) CSite_AfterLoad(*s);             // FUN_1000c7c0
    if (CPage* p = curPage()) CPage_AfterLoad(*p);             // page vtable +0x2c
    if (CFeature* f = dynamic_cast<CFeature*>(self().m_pFeature.item)) f->afterLoad();   // vtable +0x28
    // dll:10072750 FUN_10072750() (the plant page redisplay) is empty in the original
    // FUN_10012f80(world): vtable +0x20 of every object of world+0x98, then FUN_10014350
    for (Item* it : world().list(Table::Objects)) as<GameObj>(it).afterLoad(self());   // dll:10012f80: vtable +0x20 of every object
    CWorld_LocalFxUpdate();
    self().UpdateBreathingSound(1);            // dll:1006dae0
    self().UpdateHeartBeatSound(1);            // dll:1006da60
    CTimeline_SetTicksPerGameSec(0x28);
    CTimeline_UpdateDiurnal(0, 0, 0, 1);
    eng_.SetTimerInterval(1);
}

// ---------------------------------------------------------------------------------------------------------------
// page events

// dll:1001d8e0 Event_OnEnterPage (a page jump completed)
void BiosGame::onEnterPage(const std::string& pageName) {
    CSelf_OnEnterPageEvent(upper(pageName));
    if (g.gameRunning && g.fruitSoundHandle != -1) {
        eng_.CloseSound(g.fruitSoundHandle);     // DAT_1009f830
        g.fruitSoundHandle = -1;
    }
}

// dll:1001d950 Event_OnExitPage
void BiosGame::onExitPage() {
    story_->CSynthHandler_CloseSound();       // dll:10008890 FUN_10008890(&DAT_100c27e0)
    story_->CSubTechHandler_CloseSound();     // dll:10022c10 FUN_10022c10(&DAT_100c24b8)
    CSelf_OnExitPageEvent();
}

// dll:1001e300 Event_OnPageLoaded
void BiosGame::onPageLoaded(const std::string& pageName, int windowId) {
    if (!iequals(pageName, "topbar")) {
        story_->mouseFilterReset();               // dll:10061820 FUN_10061820(&CEngineFX): resets the cursor filter
        CSelf_OnEnterPage(pageName);
    }
}

// dll:1001e340 Event_OnPageDrawn
void BiosGame::onPageDrawn(const std::string& pageName, int) {
    CSelf_OnPageDrawn(pageName);
}

// dll:1001e350 Event_OnPopupOpened: remembers the popup number of the top bar (self +0x328)
void BiosGame::onPopupOpened(const std::string& pageName, int popupIndex) {
    if (iequals(pageName, "topbar")) self().m_lTopBarHandle = popupIndex;
}

// dll:10022910 Event_OnPaletteChanged
void BiosGame::onPaletteChanged() { g.fadeFromBlack = false; }

// dll:10021cb0 Event_OnCompassHeading -> FUN_1006ef80 (CSelf)
void BiosGame::onCompassHeading(int sector) {
    self().m_ulCompassHeading = static_cast<u32>(sector);
    if (self().m_pObjHolding.item) {
        if (auto* o = dynamic_cast<GameObj*>(self().m_pObjHolding.item)) o->onSector(self(), sector);   // vtable +0x5c
    }
}

// dll:10022350 Event_OnGetNameOfFruitObjectFromPageImageName: the fruit object has the name of the page ornament
bool BiosGame::onGetNameOfFruitObjectFromPageImageName(const std::string& pageImageName, std::string& outObjectName) {
    outObjectName = pageImageName;
    return true;
}

// dll:10021d60 Event_OnGetCrossFade. The cross-fade of a page jump uses the lookup and the palette on display (the
// site must have a biome: `*(site + 4) != 0`). The DLL sends "%s%s" of the data directory and the lookup's name.
bool BiosGame::onGetCrossFadeEx(const std::string&, const std::string&, CrossFadeSpec& spec) {
    CSite* s = curSite();
    if (!s || s->biome() == nullptr) return false;
    spec.xftFile = fx.dataDir + "\\" + fx.xft;   // sprintf("%s\\%s", data directory, lookup name)
    spec.xftTable.clear();
    spec.palette.assign(fx.palette.begin(), fx.palette.end());
    spec.altName.clear();
    return true;
}

// dll:1001e660 Event_OnAppActiveChanged
void BiosGame::onAppActiveChanged(bool active) {
    CTimeline_SetPaused(active ? 1 : 0);          // FUN_100156e0
    CEngineFX_ResumeLoopedAvi(active ? 1 : 0);    // FUN_10061130
    if (active) CEngineFX_UnlockPalette(9);       // FUN_100092b0
    if (active) {
        if (CFeature* f = dynamic_cast<CFeature*>(self().m_pFeature.item)) f->onResume();   // vtable +0x54
    }
}

// dll:10008010 Event_OnMenuCommand
void BiosGame::onMenuCommand(int code, int arg) {
    switch (code) {
    case 0:
        CTimeline_SetTicksPerGameSec(0x28);
        return;
    case 1:
    case 3: {
        if (CSite* s = curSite()) {
            CSite_Exit(*s);
            CSite_CloseAllAmbient(*s);
        }
        if (CPage* p = curPage()) pageExit(*p);
        CWorld_LocalFxStop();
        self().ResetPlayer();                          // dll:10065150
        g.loading = true;
        g.soundsOff = true;
        bool ok = code == 1 ? eng_.LoadGame("newgame.sav", 2)
                            : eng_.LoadGame("saved game " + std::to_string(arg) + ".sav", -1);
        g.soundsOff = false;
        g.loading = false;
        if (ok) {
            if (!g.gameRunning) g.gameRunning = true;
            if (code == 1) {      // Tuning start values: a new game only, a loaded game continues with its own state
                const Tuning& t = world().state().tuning;
                self().applyStartValues(t.startHydration, t.startNutrition, t.startEnergy);
            }
            afterNewGameLoaded();
        }
        return;
    }
    case 2:
        if (!g.dead && !g.gameEnd && g.gameRunning) {
            g.soundsOff = true;
            eng_.SaveGame("saved game " + std::to_string(arg) + ".sav");
            g.soundsOff = false;
            CTimeline_SetTicksPerGameSec(0x28);
        }
        return;
    case 4:
        CEngineFX_FadeToColour(0, 0, 0, 1, 100, 0x36, 0);   // FUN_100605f0(0, 0, 0, 1, 100, 0x36)
        eng_.QuitGame();
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// the simulation tick

// dll:1001d970 Event_OnTimer: a five-phase round robin (the engine calls it every `period` ms, API 1: 1 s after a
// game was started).
void BiosGame::onTimer() {
    Self& me = self();
    int phase = g.timerPhase;
    if (g.gameRunning && !g.gameEnd && !g.dead && g.synthState != 6) {
        if (g.dieOnUpdate) {
            CSelf_Die();
            return;
        }
        if (!self().m_pPath && (me.m_bAwake != 0 || me.m_dwUnk324 == 0) && save_->timeline.m_dwTicksPerGameSec != -1) {
            g.timerPhase = g.timerPhase + 1;
            switch (phase) {
            case 0:
                CWorld_UpdateWeather();
                COcean_UpdateState();
                CTimeline_RealTimeTick();
                return;
            case 1:
                CWorld_UpdateAirFlows();
                return;
            case 2:
                CWorld_UpdateWaterFlows();
                return;
            case 3:
                break;
            default:
                CSelf_Update(me.m_bStatusScreenVisible == 0);
                g.timerPhase = 0;
                return;
            }
            // phase 3
            if (static_cast<uint32_t>(save_->timeline.m_dwTime) - g.lastHeavyUpdate > 0x5a) {
                CWorld_UpdateAllObjects();
                CWorld_UpdateAllFeatures();
                CWorld_UpdateAllPlants();
            }
            if (me.m_bStatusScreenVisible != 0) return;
            CFeature* feat = dynamic_cast<CFeature*>(me.m_pFeature.item);
            if (!feat || (feat->flags() & 1) == 0 || (me.m_szLocation[0] == 'X')) {
                if (CPlant* pl = dynamic_cast<CPlant*>(me.m_pPlant.item)) CPlant_Display(*pl, 1);   // dll:10072aa0
            } else {
                feat->display(1);                // the current feature's vtable +0x5c(1) (redraw)
            }
            if (me.m_pObjHolding.item) {
                auto* o = dynamic_cast<GameObj*>(me.m_pObjHolding.item);
                if (o && (o->flags() & 0x100) != 0) o->redraw(me, 1);   // vtable +0x6c (flag +0x15 & 1)
            }
            if (me.m_pAirMask.item) {
                if (auto* o = dynamic_cast<GameObj*>(me.m_pAirMask.item)) o->redraw(me, 1);   // vtable +0x6c
            }
            if (me.m_pGoggles.item) {
                if (auto* o = dynamic_cast<GameObj*>(me.m_pGoggles.item)) o->redraw(me, 1);   // vtable +0x6c
            }
        }
    }
}

// dll:10021890 Event_OnIdle. Returns nonzero while more idle work is wanted.
int BiosGame::onIdle(int idleCount) {
    Self& me = self();
    if (!g.gameRunning) return story_->idleSpecialPage(0);       // dll:10061ec0 CIntroPage::OnIdle
    if (g.gameEnd) return story_->idleSpecialPage(1);            // dll:10070040 CGameEndPage::OnIdle
    if (me.m_pPath) return story_->idleTravel();                 // dll:100023b0 CPath::TravelStep(path)
    if (g.dead) return story_->idleSpecialPage(2);               // dll:1002b240 CDeathPage::OnIdle
    if (g.dieOnUpdate) {
        CSelf_Die();
        return 0;
    }
    if (g.synthState == 6) return 0;
    story_->synthMouseIdle();                                    // dll:10061a30 FUN_10061a30(&CEngineFX)
    story_->CSynthHandler_Update();                              // dll:100084c0
    story_->CSubTechHandler_Update();                            // dll:10022930
    CSelf_OnIdle();
    if (CFeature* f = dynamic_cast<CFeature*>(me.m_pFeature.item)) f->onIdle();   // vtable +0xa4
    if (idleCount == 0) {
        u32 now = tick();
        if (now - g.lastPageFxTick > 1000 && curPage()) {
            CPage_UpdateOccFx(*curPage());
            g.lastPageFxTick = now;
        }
        return 1;
    }
    if (idleCount == 1) {
        CTimeline_RealTimeTick();
        if (g.idleSeenTime != static_cast<uint32_t>(save_->timeline.m_dwTime)) {
            g.idleSeenTime = static_cast<uint32_t>(save_->timeline.m_dwTime);
            g.idleObject = true;
            g.idleFeature = true;
            g.idlePlant = true;
            return 1;
        }
    } else {
        if (g.idlePlant) {
            g.idlePlant = sim_->updateNextPlant(save_->timeline.m_dwTime);   // dll:100144f0
        }
        if (g.idleFeature) {
            g.idleFeature = cworld::updateNextFeature(world(), save_->timeline.m_dwTime);   // dll:10014860
        }
        if (g.idleObject) {
            g.idleObject = cworld::updateNextObject(world(), self(), save_->timeline.m_dwTime);   // dll:10014650
            if (g.idleObject) return 1;
        }
        if (g.idleFeature || g.idlePlant) return 1;
        g.lastHeavyUpdate = static_cast<uint32_t>(save_->timeline.m_dwTime);
        return 1;
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------------------------
// game-object events (event 11) and the media callbacks

// dll:1001e380 Event_OnGameObject: dispatches the engine's object state transitions to the object's virtual methods.
void BiosGame::onGameObject(const std::string& objectName, int eventCode, const std::string& extra) {
    dispatchObjectEvent(world(), self(), objectName, eventCode, extra);
}

// dll:10007600 SoundCallback: 1 created, 2 played, 3 stopped / ended, 4 destroyed
void BiosGame::onSoundEvent(int handle, int code) {
    CPage* page = curPage();
    CSite* site = curSite();
    CFeature* feat = dynamic_cast<CFeature*>(self().m_pFeature.item);
    if (code == 2) {
        bool handled = page && CPage_OnMediaStarted(*page, handle, 4);
        if (!handled && feat) {
            feat->onMediaStarted(handle, 4);     // vtable +0x98
        }
        return;
    }
    if (code == 3) {
        if (g.pageSoundHandle == handle) g.pageSoundHandle = -1;
        if (site && CSite_OwnsSound(*site, handle)) return;
        if (page && CPage_OnMediaStopped(*page, handle, 4)) return;
        if (feat && feat->onMediaStopped(handle, 4)) return;   // vtable +0x9c: nonzero when it owns the sound
        if (story_->CSynthHandler_OnMediaEnd(handle, 4) || story_->CSubTechHandler_OnMediaEnd(handle, 4)) return;   // dll:100088e0 / 10022c60
        int h = handle;
        CEngineFX_CloseSound(&h);
        return;
    }
    if (code != 4) return;
    if (g.pageSoundHandle2 == handle) {
        g.pageSoundHandle2 = -1;
        return;
    }
    for (const std::string* f : {&g.ambDay, &g.ambNight, &g.ambDead})
        if (!f->empty() && ambientHandle(*f) == handle) setAmbientHandle(*f, -1);
    if (feat) feat->onMediaClosed(handle, 4);   // vtable +0xa0
}

// dll:100077c0 SpriteAnimationCallback: 1 created, 2 play, 3 stopped / finished, 4 freed, 5 per frame
void BiosGame::onSpriteEvent(int handle, int code, int) {
    CPage* page = curPage();
    CFeature* feat = dynamic_cast<CFeature*>(self().m_pFeature.item);
    if (code == 2) {
        bool handled = page && CPage_OnMediaStarted(*page, handle, 2);
        if (!handled && feat) {
            feat->onMediaStarted(handle, 2);     // vtable +0x98
        }
        return;
    }
    if (code == 3) {
        if (page && CPage_OnMediaStopped(*page, handle, 2)) return;
        if (!feat) {
            eng_.CloseSpriteAnimation(handle);
            return;
        }
        feat->onMediaStopped(handle, 2);     // vtable +0x9c
        if ((feat->flags() & 0x80) == 0) eng_.CloseSpriteAnimation(handle);
        return;
    }
    if (code != 4) return;
    bool handled = page && CPage_OnMediaClosed(*page, handle, 2);
    if (!handled && feat) {
        feat->onMediaClosed(handle, 2);      // vtable +0xa0
    }
}

// dll:100078f0 OnMovieEvent: 2 played, 3 stopped / finished, 4 destroyed
void BiosGame::onMovieEvent(int handle, int code, int) {
    CPage* page = curPage();
    CFeature* feat = dynamic_cast<CFeature*>(self().m_pFeature.item);
    if (code == 2) {
        bool handled = page && CPage_OnMediaStarted(*page, handle, 1);
        if (!handled && feat) {
            feat->onMediaStarted(handle, 1);     // vtable +0x98
        }
        return;
    }
    if (code == 3) {
        if (story_->CSynthHandler_OnMediaEnd(handle, 1) || story_->CSubTechHandler_OnMediaEnd(handle, 1)) return;   // dll:100088e0 / 10022c60
        if (page && CPage_OnMediaStopped(*page, handle, 1)) return;
        if (feat && feat->onMediaStopped(handle, 1)) return;   // vtable +0x9c
        CEngineFX_LoopedAviClosed();   // FUN_10061120: CloseAVIMovie(CEngineFX movie handle)
        return;
    }
    if (code != 4) return;
    if (fx.aviHandle == handle) fx.aviHandle = -1;
    if (self().m_bAwake == 0) {
        self().m_dwUnk324 = 1;           // DAT_100c205c = 1
        return;
    }
    bool handled = page && CPage_OnMediaClosed(*page, handle, 1);
    if (!handled && feat) {
        feat->onMediaClosed(handle, 1);      // vtable +0xa0
    }
}

// ---------------------------------------------------------------------------------------------------------------
// debugging

// port: the `game ...` script commands (no original)
std::string BiosGame::debugCommand(const std::string& args) {
    std::istringstream in(args);
    std::string cmd;
    in >> cmd;
    std::ostringstream out;
    const Timeline& t = save_->timeline;
    if (cmd == "dump" || cmd.empty()) {
        char dl[32];
        std::snprintf(dl, sizeof dl, "%.6f", tl.daylight);
        out << "game time=" << static_cast<uint32_t>(t.m_dwTime) << " day=" << t.m_dwDay << " tod=" << t.m_dwTimeOfDay
            << " clock=" << tl.clock << " phase=0x" << std::hex << tl.diurnal << std::dec << " daylight=" << dl
            << " ticks=" << t.m_dwTicksPerGameSec << " lasttick=" << tl.lastTick << " now=" << tick() << "\n";
        CSite* s = curSite();
        out << "game site=" << (s ? s->name() : "-") << " page=" << (self().m_pPage ? self().m_pPage->name() : "-")
            << " sitestate=" << (s ? s->state() : 0) << " lights=" << gs().lightsOn
            << " location=" << std::string(reinterpret_cast<const char*>(self().m_szLocation.data()));
        {
            char av[32];
            std::snprintf(av, sizeof av, "%.17g", curCel() ? curCel()->plantHealthAvg() : -1.0);
            out << " plantavg=" << av;
        }
        out << "\n";
        out << "game palette group=" << g.paletteGroup << " set=" << g.lastPaletteSetIndex << " xft=" << fx.xft
            << " dayxft=" << g.dayXftName << " owner=" << save_->engineFx.m_wState << "\n";
    } else if (cmd == "settime") {
        // debug: puts the clock at a game time (seconds) and applies the time of day to the site at once
        uint32_t secs = 0;
        in >> secs;
        save_->timeline.m_dwTime = static_cast<i32>(secs);
        self().m_dwTimeLastUpdate = static_cast<i32>(secs);   // port: a debug jump of the clock is not time the player lived through
        save_->timeline.m_dwDay = static_cast<i32>(secs / 0x15180 + 1);
        tl.lastTick = tick();
        CTimeline_UpdateTimeOfDay();
        CTimeline_UpdateDiurnal(1, 1, 0, 1);
        out << "game settime " << secs << "\n";
    } else if (cmd == "seed") {
        uint32_t s = 1;
        in >> s;
        gs().rng.seed(s);
        out << "game seed " << s << "\n";
    } else if (cmd == "settings") {
        out << "game settings " << save_->settings().toString() << "\n";
    } else if (cmd == "rain") {
        // the drops the next frame of the rain overlay draws: `rain started=N` then `rain thin|tall X Y SPEED` per drop (x or y -1: starts at its first position)
        out << "rain started=" << (rain_.started ? 1 : 0) << "\n";
        auto list = [&](const char* kind, const int16_t (*d)[6], int n) {
            for (int k = 0; k < n; k++) {
                const bool fresh = d[k][4] == -1;
                out << "rain " << kind << " " << (fresh ? d[k][0] : d[k][4]) << " " << (fresh ? d[k][1] : d[k][5]) << " " << d[k][3] << "\n";
            }
        };
        list("thin", rain_.thin, 217);
        list("tall", rain_.tall, 15);
    } else if (cmd == "fx") {
        // every FX of the current page: file, flags, on, playing, handle and what CanStart answers
        if (CPage* p = curPage()) {
            auto list = [&](const char* kind, const std::vector<Item*>& l) {
                for (Item* it : l) {
                    CSpotFX& f = as<CSpotFX>(it);
                    out << "fx " << kind << " " << f.file() << " flags=0x" << std::hex << f.flags() << std::dec
                        << " on=" << f.on() << " playing=" << f.playing() << " handle=" << f.handle()
                        << " canstart=" << (CSpotFX_CanStart(f) ? 1 : 0) << "\n";
                }
            };
            list("page", p->fx());
            list("occ", p->occFx());
            list("looped", p->occLoopedFx());
        }
    } else if (cmd == "sounds") {
        out << "sounds day=" << g.ambDay << "(" << (g.ambDay.empty() ? -1 : ambientHandle(g.ambDay)) << ") night=" << g.ambNight
            << "(" << (g.ambNight.empty() ? -1 : ambientHandle(g.ambNight)) << ") dead=" << g.ambDead << "("
            << (g.ambDead.empty() ? -1 : ambientHandle(g.ambDead)) << ")\n";
        auto set = [&](const char* n, SoundSet* s) {
            out << "sounds set " << n << " ";
            if (!s) { out << "-\n"; return; }
            out << std::hex << s->addr << std::dec << " open=";
            int open = 0;
            for (int h : s->handles) open += h != -1;
            out << open << "/" << s->handles.size() << "\n";
        };
        set("day", g.setDay);
        set("dusk", g.setDusk);
        set("night", g.setNight);
        set("dawn", g.setDawn);
    } else if (cmd == "pals") {
        // FNV-1a of the five site palettes and the one on display (the test compares them with its own blend)
        auto h = [](const Pal& p) {
            uint32_t x = 2166136261u;
            for (u8 b : p) x = (x ^ b) * 16777619u;
            char b[16];
            std::snprintf(b, sizeof b, "%08x", x);
            return std::string(b);
        };
        out << "pals night=" << h(g.sitePal[0]) << " dawn=" << h(g.sitePal[1]) << " day=" << h(g.sitePal[2])
            << " dusk=" << h(g.sitePal[3]) << " cloud=" << h(g.sitePal[4]) << " daymix=" << h(g.sitePal[5])
            << " shown=" << h(fx.palette) << "\n";
    } else if (cmd == "self") {      // port: every member of CSelf ("game self"), or the members whose name contains the argument
        std::string filter;
        in >> filter;
        for (auto& kv : self().dumpAll())
            if (filter.empty() || kv.first.find(filter) != std::string::npos) out << "self." << kv.first << " = " << kv.second << "\n";
    } else if (cmd == "obj") {       // port: the members of a game object ("game obj NAME")
        std::string name;
        in >> name;
        GameObj* o = world().findObject(name.c_str());
        if (!o) { out << "obj " << name << " not found\n"; return out.str(); }
        ObjMembers m;
        o->dumpMembers(m);
        for (auto& kv : m) out << "obj." << name << "." << kv.first << " = " << kv.second << "\n";
    } else if (cmd == "event") {     // port: Event_OnGameObject through the real dispatcher ("game event OBJECT CODE [EXTRA]")
        std::string name, extra;
        int code = 0;
        in >> name >> code >> extra;
        onGameObject(name, code, extra);
        out << "event " << name << " " << code << " done\n";
    } else if (cmd == "consume") {   // port: CSelf::Consume(code)
        int code = 0;
        in >> code;
        out << "consume " << code << " -> " << self().Consume(static_cast<u16>(code)) << "\n";
    } else if (cmd == "clock") {
        // advance the clock of the game logic alone is not possible: time comes from the engine
        out << "game clock tick=" << tick() << "\n";
    } else {
        return story_->debugCommand(args);       // the story code's commands (story_glue.cpp); throws for an unknown one
    }
    return out.str();
}

} // namespace biosys::game
