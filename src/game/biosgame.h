// BiosGame: the port of BiosGame.dll's behaviour, the first slice (analysis/notes/interface.md section 2: the event
// handlers; the clock; sites, pages and their ambient effects; CEngineFX; the spot FX family). It is a GameLogic the
// engine drives and talks back to through EngineApi only (through the DLL's thin wrappers, eng.h).
//
// Layout of the translation: one class holds what the original keeps in DLL globals and class members; its member
// functions are named after the original's (`CSite_OnTimeOfDayChange`) and carry the `// dll:ADDR` of the function they
// translate. Files:
//   biosgame.cpp      events (the handlers GetEventFunctionTable dll:100044b0 lists), start-up, new game, media callbacks
//   bg_timeline.cpp   CTimeline (dll:10015070..10015ab0)
//   bg_enginefx.cpp   CEngineFX (dll:1005fdc0..10061500) and the palette blend MakeIntermediatePalette
//   bg_site.cpp       CSite (dll:1000c7c0..1000e9e0)
//   bg_page.cpp       CPage (dll:1000f370..1000fd20), CSelf::OnEnterPage and the page-drawn / enter / exit handlers
//   bg_fx.cpp         the CSpotFX family and the local FX
// The simulation, the player model, the objects' use logic, the hotspot handlers, the story and the SubTech / Synth handlers are in
// sim.*, self*.cpp, obj_*.cpp and story_*.cpp; no stub marker is left (src/game/README.md lists what is not done).
#pragma once
#include "engine/datafiles.h"
#include "engine/engineapi.h"
#include "engine/gamelogic.h"
#include "engine/palettesets.h"
#include "game/eng.h"
#include "game/feature.h"
#include "game/gameobj.h"
#include "game/page.h"
#include "game/path.h"
#include "game/plant.h"
#include "game/savegame.h"
#include "game/sim.h"
#include "game/site.h"
#include "game/soundfx.h"
#include "game/soundsets.h"
#include "game/spotfx.h"
#include <array>
#include <functional>
#include <map>
#include <set>
#include <memory>

namespace biosys::game {

// A sound set at run time (soundsets.h): the files and the engine handles of the entries that are open.
struct SoundSet {
    uint32_t addr = 0;
    std::vector<std::string> files;
    std::vector<int> handles;     // -1 = not open
};

// One 256 x {r,g,b,flags} palette (the DLL's PTR_DAT_1009ff30..44 buffers and CEngineFX+0x104).
using Pal = std::array<u8, 1024>;

class StoryLogic;      // story.h: input, travel and story

class BiosGame final : public GameLogic {
public:
    struct Config {
        std::string newGamePath;                   // NewGame.sav ("" = search the data roots like the engine)
        uint32_t randSeed = 1;                     // srand(GetTickCount()) of Event_OnStartup
        std::function<u32()> tick;                 // GetTickCount(): the engine's injected clock
        std::string dataDir;                       // the port's data/ directory (palette_sets.tsv)
        std::function<void(int vk, unsigned lParam)> sendKeyUp;   // SendMessageA(window, WM_KEYUP, vk, lParam): the front end puts a key release into the engine (nullptr = ignored)
        // PostMessageA(window, WM_MOUSEMOVE, 0, (y << 16) | x) of the cursor filter (client coordinates; yFromCursor: y is the pointer's own
        // y, GetCursorPos): the front end puts a mouse move into the engine (nullptr = ignored)
        std::function<void(int x, int y, bool yFromCursor)> postMouseMove;
        // FUN_10007a40, the busy wait on GetTickCount of CPath::TravelStep (dll:100023b0): the window waits that many ms on the real clock
        // (presenting frames); nullptr = not waited (headless runs, whose clock is injected)
        std::function<void(u32 ms)> busyWait;
        std::function<void(const std::string&)> uiSink;   // StoryUi: each line of text the game would draw (nullptr = record only)
        Fixes fixes;                               // the remaster switch (fixes.h, `--fixes` of the window); default faithful
        Model model;                               // model corrections (settings.h, `--realism`); default faithful
        Tuning tuning;                             // difficulty numbers (settings.h, `--difficulty`); default original
        void set(const Settings& s) { fixes = s.fixes; model = s.model; tuning = s.tuning; }   // the front door
    };
    BiosGame(EngineApi& api, const DataFiles& files, Config config);
    ~BiosGame() override;
    // The remaster switch (fixes.h): changes the fixes of the running game; the front end calls it from `--fixes`.
    void setBusyWait(std::function<void(u32 ms)> f) { config_.busyWait = std::move(f); }   // Config::busyWait, set by the front end
    void setFixes(const Fixes& f) { save_->setFixes(f); }
    // The whole remaster layer (settings.h): fixes, model, tuning. Start values apply to games started afterwards only.
    void setSettings(const Settings& s) { save_->setSettings(s); }

    // The events (gamelogic.h)
    int onStartup() override;
    void onShutdown() override;
    void onEnterPage(const std::string& pageName) override;
    void onExitPage() override;
    void onTimer() override;
    int onPageTimer(const std::string&, int, const std::string&) override { return 1; }
    void onPageLoaded(const std::string& pageName, int windowId) override;
    void onPageDrawn(const std::string& pageName, int windowId) override;
    void onPopupOpened(const std::string& pageName, int popupIndex) override;
    void onPopupClosed(int popupIndex) override {}
    void onGameObject(const std::string& objectName, int eventCode, const std::string& extra) override;
    void onActivateApp(bool active) override {}
    void onPaletteChanged() override;
    int onIdle(int idleCount) override;
    void onAppActiveChanged(bool active) override;
    void onCompassHeading(int sector) override;
    bool onGetCrossFadeEx(const std::string& defName, const std::string& destPage, CrossFadeSpec& spec) override;
    bool onGetNameOfFruitObjectFromPageImageName(const std::string& pageImageName, std::string& outObjectName) override;
    int onRestart() override { return 0; }
    void onMenuCommand(int code, int arg) override;
    void onSoundEvent(int handle, int code) override;
    void onSpriteEvent(int handle, int code, int arg) override;
    void onMovieEvent(int handle, int code, int arg) override;
    // The events this slice does not translate: declared, doing nothing (the engine's defaults), each marked with its handler.
    int onHotspot(const std::string&, int, const std::string&) override;                  // dll:1001db50 (story_hotspot.cpp)
    void onKeyDown(int, int, int, int&) override;                                          // dll:1001e6b0 (story_keys.cpp)
    void onKeyUp(int, int, int, int&) override;                                            // dll:10021790 (story_keys.cpp)
    void onEvent16(int, int) override {}                                                   // dll:10021880 (empty)
    void onBS4Screen(int) override;                                                        // dll:10021ae0 (story_hotspot.cpp)
    bool onBS4Query(int, void*) override;                                                  // dll:10021b50 (story_hotspot.cpp)
    void onSystemPaletteChanged() override {}                                              // dll:10021ca0 (empty)
    bool onSaveGame(const std::string&) override;                                          // dll:10021cc0 Event_OnSaveGame (biosgame.cpp)
    bool onLoadGame(const std::string&, long) override;                                    // dll:10021d00 Event_OnLoadGame (biosgame.cpp)
    bool onSetFeatureParameter(const std::string&, int, int, int, int) override;           // dll:10021db0 (bg_features.cpp)
    bool onGetFeatureParameter(const std::string&, int, u32&, double&) override;           // dll:10021df0 (bg_features.cpp)
    void onTextInputDone(bool, const std::string&) override;                               // dll:10022380 (story_hotspot.cpp)
    void onMenuQuerySavedGame(int, int&, std::string&, int) override;                      // dll:10008350 Event_OnMenuQuerySavedGame (biosgame.cpp)
    void onHotspotHover(const std::string&, int, bool) override;                           // dll:100223c0 (story_hotspot.cpp)
    void onSimStatusHotspot(const std::string&) override;                                  // dll:10022450 (story_hotspot.cpp)
    bool onEvent35(const std::string&) override;                                           // dll:100224b0 (story_hotspot.cpp)
    void onReturnToSavedPage(const std::string&) override;                                 // dll:100224c0 (story_hotspot.cpp)
    void onEvent37() override {}                                                           // dll:100228d0 (empty)
    void onEvent38() override {}                                                           // dll:100228e0 (empty)
    void onRightClick() override;                                                          // dll:100224d0 (story_hotspot.cpp)
    void onMouseDelta(int&, int&) override;                                                // dll:10022510 (story_hotspot.cpp)
    void onEvent41() override {}                                                           // slot 41 is not set by the DLL
    bool onBlitPage(u8*, u8*, void*, int, u8, u8, u8) override;                            // dll:10022530 Event_OnBlitPage, the rain overlay (bg_rain.cpp)
    void onStatusBarObject(int, const std::string&, int) override;                         // dll:10022860 (story_hotspot.cpp)
    bool onEvent44() override;                                                             // dll:100228f0 (story_hotspot.cpp)
    void onEnumPageHotspots(u32, const void*, u32, const void*, int, const std::string&) override {}   // slot 45: not set by the DLL

    // --- the front end's saves without the menu (bg_autosave.cpp; port, no original) ------------------------------------
    // "" if the player could load a game now (F6 / the load menu need: awake, no angel / peyote / synth / sub-tech sequence, not on a
    // 'U' / 'X' site: dll:1001e6b0), else the reason. saveBlocker() adds what the save command of the menu needs: a game runs, the
    // player is neither dead nor at the end (dll:10008010 case 2).
    std::string loadBlocker();
    std::string saveBlocker();
    bool awake() { return self().m_bAwake != 0; }
    bool gameRunning() const { return g.gameRunning; }
    // Eng_SaveGame(fileName) as the menu's save slot does it (sounds off meanwhile), without the menu. false: refused or failed.
    bool saveToFile(const std::string& fileName);
    // Eng_LoadGame(fileName, -1) as the menu's load slot does it (dll:10008010 case 3), without the menu. false: failed (the game is then
    // in the state a failed load leaves it in, as with the original's F6).
    bool loadFromFile(const std::string& fileName);

    // --- debugging / tests -------------------------------------------------------------------------------------
    SaveGame& save() { return *save_; }
    // The state dump of the script command `game dump` and the other `game ...` commands (see script.cpp).
    std::string debugCommand(const std::string& args);

private:
    // ===== DLL globals (names from analysis/notes, original address in the comment) ============================
    struct Globals {
        bool gameRunning = false;           // DAT_1009f824 (0: the intro runs); set by the new-game path
        bool gameEnd = false;               // DAT_100c2724
        bool dead = false;                  // DAT_100c2728
        int synthState = 0;                 // DAT_100c27e4 (6: synth sequence in progress)
        bool dieOnUpdate = false;           // DAT_100c249c
        bool silent = false;                // DAT_1009f83c: set while CTimeline::Advance runs; no sounds, no images
        bool noPalette = false;             // DAT_100c275c
        bool loading = false;               // DAT_1009f840
        bool soundsOff = false;             // DAT_100c2760: set while a game is loading
        bool redrawPending = false;         // DAT_100c3220
        bool linkedObjectLock = false;      // DAT_100c2a14
        bool rainFilter = false;            // DAT_1009f848
        int timerPhase = 0;                 // DAT_100c42a0
        u32 lastHeavyUpdate = 0;            // DAT_100a17b8
        bool idlePlant = false, idleFeature = false, idleObject = false;   // DAT_100c42b0 / 42ac / 42b8
        u32 idleSeenTime = 0;               // DAT_100c42b4
        u32 lastPageFxTick = 0;             // DAT_100a1f28 (page idle: occasional FX)
        u32 lastOccFxTick = 0;              // DAT_100c4264
        u32 lastRealTick = 0;               // DAT_100c426c
        const CSpotFX* lastOccFx = nullptr; // DAT_100a0284 (the occasional FX started last)
        u32 movieThrottle = 0;              // DAT_1009f548 (game time of the next throttled movie)
        // palettes / time of day
        int paletteGroup = 0;               // DAT_1009ff54 (0 = group A, 1 = B)
        int paletteGroupPrev = 0;           // DAT_1009ff58
        int lastDiurnal = 0;                // DAT_1009ff5c
        int lastPaletteSetIndex = -1;       // DAT_100c425c: the palette set (the site's palette base pointer) shown last; -1 = null
        bool forcePalette = false;          // DAT_100c3630
        int startupPhase = 0;               // DAT_100c3a38
        bool paletteChanged = false;        // DAT_100c2a1c
        bool fadeFromBlack = false;         // _DAT_100c4308
        u32 fadeFromBlackUntil = 0;         // DAT_100c4310
        Pal statusPalette{};                // DAT_100c2064 = CSelf+0x32c: palettes\\status\\status.pal (read by FUN_1005a0c0)
        bool haveStatusPalette = false;
        std::array<Pal, 6> sitePal{};       // PTR_DAT_1009ff30 night, ff34 dawn, ff38 day, ff3c dusk, ff40 cloud, ff44 day (blend)
        const std::string* dayXft = nullptr;   // DAT_100c3a40 (points into the palette set)
        std::string dayXftName;
        // ambient sounds of the current site
        std::string ambDay, ambNight, ambDead;      // DAT_100c4258, DAT_100c3a3c, DAT_100c4250 (file names; "" = none)
        SoundSet* setDay = nullptr;         // DAT_100c3628
        SoundSet* setDusk = nullptr;        // DAT_100c4254
        SoundSet* setNight = nullptr;       // DAT_100c2a18
        SoundSet* setDawn = nullptr;        // DAT_100c3e48
        bool variantToggle = false;         // DAT_100a2d7c (CSiteVariant::OnTimeOfDayChange alternates night / day palette)
        int pageSoundHandle = -1;           // DAT_100c2464
        int pageSoundHandle2 = -1;          // DAT_100c2468
        int fruitSoundHandle = -1;          // DAT_1009f830
        Item* alarm = nullptr;              // DAT_100bd65c
        int synthX = 0;                     // DAT_100c27e8 (CSynthAnimFX::OnStarted: x + 100)
        double synthParam = 0.0;            // DAT_100c27f0
    } g;

    // CTimeline members that are not in the blob record (dll:10015070)
    struct TimelineState {
        u32 lastTick = 0;                   // +0x28
        double daylight = 0.0;              // +0x20 (0 = night .. 1 = day)
        int diurnal = 0x28;                 // +0x2c
        bool paused = false;                // +0x30
        std::string clock = "08:00";        // +0x14
    } tl;

    // CEngineFX members (global DAT_100c12f8): dll:1005fdc0
    struct EngineFxState {
        std::string dataDir;                // +0x000 (Eng_GetDataDirectory)
        Pal palette{};                      // +0x104 the palette on display
        std::string xft;                    // +0x504 the cross-fade lookup of the palette on display
        // +0x568 palette owner token: SaveGame::engineFx.m_wState
        bool paletteOn = true;              // +0x56c
        int tintR = 0, tintG = 0, tintB = 0;   // +0x570..+0x578
        int soundLock = 0;                  // +0x57c (u16)
        bool loopedAvi = false;             // +0x580
        std::string loopedAviFile;          // +0x584
        std::string loopedAviPage = "nowhere";   // +0x5e8
        int loopedAviUnk = 0;               // +0x5f4
        int aviHandle = -1;                 // +0x5f8
        int aviX = 0, aviY = 0, aviVolume = 0, aviKey = 0, aviOption = 0;   // +0x5fc..+0x60c
        int systemFont = 0;                 // +0x610
        u32 textColour = 0, textBackground = 0;   // +0x614, +0x618 (FUN_100611b0)
        int textTransparent = 0;            // +0x61c
    } fx;

    // the falling drops of Event_OnBlitPage (DAT_100a1f88 .. DAT_100a2a7c): per drop {x start, y start, unused, speed, x, y}, x = y = -1: not on screen
    struct RainDrops {
        bool started = false;
        int16_t thin[217][6] = {};
        int16_t tall[15][6] = {};
    } rain_;

    // ===== helpers ============================================================================================
    World& world() { return save_->world; }
    GameState& gs() { return save_->world.state(); }
    Self& self() { return save_->self; }
    u32 tick() const { return config_.tick ? config_.tick() : 0; }     // GetTickCount()
    bool chancePercent(double percent);                                 // ChancePercent dll:10007570
    double randRange(double lo, double hi) { return gs().rng.randRange(lo, hi); }   // dll:100075a0
    int crand() { return gs().rng.rand(); }                             // _rand dll:10079380
    void log(const std::string& s) { eng_.WriteLog(s); gs().log.push_back(s); }
    CSite* curSite() { return dynamic_cast<CSite*>(self().m_pSite.item); }
    CPage* curPage() { return dynamic_cast<CPage*>(self().m_pPage); }
    CBiomeCel* curCel() { return dynamic_cast<CBiomeCel*>(self().m_pCel); }
    static uint32_t biomePowerBit(const Biome* b);                      // CBiome +0x70

    // CSelf / CWorld / CGame: forwards to the simulation core and the player (biosgame.cpp)
    void CWorld_UpdateWeather();                  // dll:10013420
    void CWorld_UpdateAirFlows();                 // dll:10013e00
    void CWorld_UpdateWaterFlows();               // dll:100140b0
    void CWorld_UpdateAllObjects();               // dll:100146a0
    void CWorld_UpdateAllFeatures();              // dll:100148b0
    void CWorld_UpdateAllPlants();                // dll:10014540
    void COcean_UpdateState();                    // dll:10029990
    void bindSimHooks();                          // port: the simulation core's calls into the shell
    void CSelf_Update(bool flag);                 // dll:10066aa0
    void CSelf_Die();                             // dll:10068f10
    void CSelf_OnIdle();                          // dll:10065e80
    void CGame_TriggerEvent(uint32_t milestone);  // dll:100092d0
    void CSubTechHandler_OnNewDay();              // dll:10022f50 (calls CGame::TriggerEvent)
    void CWorld_OnNewDay();                       // dll:10014ee0

    // ----- bg_timeline.cpp -----
    void CTimeline_AdvanceRealTime();             // dll:100153b0
    void CTimeline_Start(uint32_t t);             // dll:10015410
    void CTimeline_RealTimeTick();                // dll:10015470
    void CTimeline_Advance(uint32_t seconds, int updateSelf);   // dll:100154b0
    void CTimeline_UpdateTimeOfDay();             // dll:100155e0
    void CTimeline_UpdateDiurnal(int p1, int p2, int p3, int p4);   // dll:10015710
    void CTimeline_SetTicksPerGameSec(int ticks); // dll:10015ab0
    void CTimeline_SetPaused(int active);         // dll:100156e0 (OnAppActiveChanged)
    void CTimeline_Constructor();                 // dll:10015070

    // ----- bg_enginefx.cpp -----
    void CEngineFX_Construct();                   // dll:1005fdc0
    void CEngineFX_Init();                        // dll:1005fe80
    void CEngineFX_Shutdown();                    // dll:1005ff10
    bool CEngineFX_CreateFontResource(const std::string& file);   // dll:100611e0
    void CEngineFX_RemoveFontResource(const std::string& file);   // dll:10061300
    int CEngineFX_CreateLogicalFont(const std::string& face, int height, int& font);   // dll:100613a0
    bool readPalette(const std::string& file, Pal& out);          // dll:100604d0 (FUN_100604d0)
    bool CEngineFX_LoadPalette(const std::string& file, const char* xft, int steps, int immediate, int token, int unk);   // dll:10060170
    bool CEngineFX_SetPalette(const Pal& pal, const char* xft, int steps, int immediate, int token, int unk);   // dll:10060280
    bool CEngineFX_Blend(const Pal& a, const Pal& b, double frac, const std::string& xftA, const std::string& xftB,
                         int steps, int immediate, int unk, int token);   // dll:10060370
    void MakeIntermediatePalette(const Pal& a, const Pal& b, Pal& out, double frac, int fixed);   // dll:10060020
    void CEngineFX_ApplyTint(Pal& p);             // dll:10060b50
    bool CEngineFX_FadeToColour(int r, int g, int b, int back, int speed, int percent, int unk);   // dll:100605f0 / 10060620
    void CEngineFX_ShowPalette();                 // dll:10060650
    void CEngineFX_FadeToCurrent(int speed);      // dll:10060670
    void CEngineFX_HideCursor();                  // dll:10060690
    bool CEngineFX_LockPalette(int token);        // dll:10060ce0
    bool CEngineFX_UnlockPalette(int token);      // dll:10060cb0
    bool CEngineFX_LockSounds(int token);         // dll:100606a0
    bool CEngineFX_UnlockSounds(int token);       // dll:100606d0
    bool CEngineFX_PlaySound(const std::string& file, int token, int volume);   // dll:10060700
    bool CEngineFX_PlayLoopedSoundInto(const std::string& file, int* handle, int token, int volume);   // dll:10060780
    void CEngineFX_PlayRandomSound(const std::string& stem, unsigned n);   // dll:10060820
    int CEngineFX_PlayLoopedWav(const std::string& file, int volume, int* handle, const char* panHotspot, int token);   // dll:10060860
    bool CEngineFX_SetVolume(int handle, int volume);   // dll:10060980
    void CEngineFX_CloseSound(int* handle);       // dll:100609b0
    bool CEngineFX_StopSound(int handle);         // dll:100609d0
    int CEngineFX_PlayAnim(const std::string& file, int* handle, int window, int frameDelay, int x, int y, int pageRelative,
                           int keepPainted, const std::string& wav);   // dll:100609f0
    bool CEngineFX_PlayAnimEx(const std::string& file, int* handle, int window, int frameDelay, int x, int y, int maxPlayMs,
                              int pageRelative, int keepPainted, const std::string& wav, int soundMode, int playMode,
                              int visibleWidth);   // dll:10060aa0
    void CEngineFX_StopAnim(int handle);          // dll:10060b40
    void CEngineFX_DisplayImage(const std::string& image, int window, int x, int y, int unk20, int unk04);   // dll:10060bf0
    void CEngineFX_PlaceWorn(const std::string& object, const std::string& extraImage, int x, int y);   // dll:10060c30
    int CEngineFX_ChangeHoldingImagesGroup(const std::string& object, int group);   // dll:100616a0
    void CEngineFX_ImageVisibility(const std::string& image, int visible, int window);   // dll:10060c70
    void CEngineFX_HotspotEnable(const std::string& hotspot, int enable, int window);    // dll:10060c90
    int CEngineFX_JumpToPage(const std::string& page, int x, int y, int paletteOn, int bigPageMode);   // dll:10060d10
    void CEngineFX_ClearWindow();                 // dll:10060df0
    bool CEngineFX_PlayAvi(const std::string& file, int x, int y, int volume, int colourKey, const std::string& page,
                           int pageMode, int opt42, int synth, int restart);   // dll:10060e10
    bool CEngineFX_PlayLoopedAvi(const std::string& file, int x, int y, int volume, int colourKey, const std::string& page,
                                 int pageMode, int opt42, int synth);   // dll:10060f40
    void CEngineFX_StopLoopedAvi();               // dll:10061110
    void CEngineFX_LoopedAviClosed();             // dll:10061120
    void CEngineFX_ResumeLoopedAvi(int active);   // dll:10061130
    void CEngineFX_SetTextColours(u32 colour, int transparent, u32 background);   // dll:100611b0
    bool CEngineFX_DrawText(const std::string& text, int x, int y, int font);     // dll:10061500 (font 0: the system font)
    std::unique_ptr<GdiSession> openDc(int window);   // port: Eng_GetWindowDC + the system font, the strings drawn are recorded

    // ----- bg_site.cpp -----
    void CSite_UpdateDisplayState(CSite& s, const std::string& pageName);   // dll:1000cab0 (vtable +0x14)
    void CSite_OnPageDrawn(CSite& s, const std::string& pageName);          // dll:1000d600 (vtable +0x18)
    void CSite_Exit(CSite& s);                                              // dll:1000d900 (vtable +0x1c)
    void CSite_OnTimeOfDayChange(CSite& s, int p1, int p2, int state, int steps, int immediate, double frac);   // dll:1000db70 (+0x20)
    void CSiteVariant_UpdateDisplayState(CSiteVariant& s);                  // dll:100250f0 (vtable +0x14 of CSiteVariant)
    void CSiteVariant_OnTimeOfDayChange(CSiteVariant& s, int steps, int immediate);   // dll:10025250 (vtable +0x20)
    void CSite_AfterLoad(CSite& s);                                         // dll:1000c7c0
    void CSite_RefreshFx(CSite& s);                                         // dll:1000ca00
    void CSite_StopFx(CSite& s);                                            // dll:1000ca70
    int CSite_PaletteGroup(CSite& s);                                       // dll:1000d990
    void CSite_LoadPalettes(CSite& s);                                      // dll:1000d9f0
    void CSite_BuildDayPalette(CSite& s);                                   // dll:1000e3a0
    void CSite_TintPalette(CSite& s, Pal& p);                               // dll:1000e470
    void CSite_OpenSounds(CSite& s, int slot);                              // dll:1000e5d0 (CSite::CloseSounds, sic)
    void CSite_CloseAllSets();                                              // dll:1000e6b0
    void CSite_CloseSet(SoundSet* set);                                     // dll:1000e710
    void CSite_RandomAmbient(CSite& s, int slot, double weight);            // dll:1000e750
    bool CSite_OwnsSound(CSite& s, int handle);                             // dll:1000e910
    void CSite_PlayAmbient(CSite& s, const std::string& file);              // dll:1000e9e0
    void CSite_CloseAmbient(const std::string& file);                       // dll:1000eb00
    void CSite_CloseAllAmbient(CSite& s);                                   // dll:1000eb20
    SoundSet* soundSet(DllPtr p);
    // the palette set (data/palette_sets.tsv) a site's palette base stands for: its 24 slots of 100 bytes
    const PaletteSet* paletteSetOf(const CSite& s);
    int ambientHandle(const std::string& file);
    void setAmbientHandle(const std::string& file, int h);
    void CBiomeCel_UpdatePlantHealthAvg(CBiomeCel& c);                      // dll:10018860
    uint32_t CWorld_HasPower(uint32_t mask);                                // dll:10014910
    void CPath_Display(CPath& p);                                           // dll:10001290

    // ----- bg_plant.cpp: the plants' page display and the player's dealings with a plant -----
    void CPlant_DisplayInDistance(CPlant& p);                               // dll:10072d70
    void CPlant_ShowFruitImages(CPlant& p);                                 // dll:10072fb0
    void CPlant_DisplayAll(CPlant& p);                                      // dll:10072910 (DisplayInDistance, then FUN_10072fb0 when flag 0x800)
    void CPlantPair_DisplayInDistance(CPlantPair& p, int redraw);           // dll:10009ce0
    void CPlant_Display(CPlant& p, int redraw);                             // dll:10072aa0
    void CPlant_OnEnter(CPlant& p);                                         // dll:10072930
    void CPlant_RefreshFruitImages(CPlant& p);                              // dll:10073090
    void CPlant_ReleasePageImages(CPlant& p);                               // dll:10073a60
    void CPlant_UseObject(CPlant& p, GameObj& o, const std::string& hotspot);   // dll:10072a00
    void CPlant_Interact(CPlant& p, GameObj* o, const std::string& hotspot);    // dll:100731b0
    void CWorld_RemovePlantFood(CPlant& p);                                 // dll:10014740

    // ----- bg_page.cpp -----
    void pageOnEnter(CPage& p);                                             // vtable +0x1c of the page classes
    void pageUpdate(CPage& p);                                              // vtable +0x20
    void pageExit(CPage& p);                                                // vtable +0x28
    void CSelf_OnEnterPage(const std::string& pageName);                    // dll:1006e390
    void CSelf_OnPageDrawn(const std::string& pageName);                    // dll:1006e900
    void CSelf_OnEnterPageEvent(const std::string& pageName);               // dll:1006eca0
    void CSelf_OnExitPageEvent();                                           // dll:1006ec20
    void CPage_OnEnter(CPage& p);                                           // dll:1000f370 (vtable +0x1c)
    void CPage_Update(CPage& p);                                            // dll:1000f460 (vtable +0x20)
    void CPage_UpdateFx(CPage& p);                                          // dll:1000f730
    void CPage_UpdateOccLoopedFx(CPage& p);                                 // dll:1000f8e0
    void CPage_UpdateOccFx(CPage& p);                                       // dll:1000f7f0
    void CPage_Exit(CPage& p);                                              // dll:1000f4a0 (vtable +0x28)
    void CPage_AfterLoad(CPage& p);                                         // dll:1000f570 (vtable +0x2c)
    bool CPage_OnMediaStarted(CPage& p, int handle, unsigned mask);         // dll:1000fc00 (vtable +0x38)
    bool CPage_OnMediaStopped(CPage& p, int handle, unsigned mask);         // dll:1000fcc0 (vtable +0x3c)
    bool CPage_OnMediaClosed(CPage& p, int handle, unsigned mask);          // dll:1000fd20 (vtable +0x40)
    // the local FX list of the world (world+0x5c: CSoundFX / CRainFX) and the FX switches
    void CWorld_LocalFxReset();                                             // dll:10014310 (vtable +0x28 of every local FX)
    void CWorld_LocalFxUpdate();                                            // dll:10014350 (vtable +0x30)
    void CWorld_LocalFxStop();                                              // dll:10014390 (vtable +0x34)
    void CWorld_StartPageFX(int id);                                        // dll:10014280 (vtable +0x24(id))
    void CWorld_StopPageFX(int id);                                         // dll:100142d0 (vtable +0x2c(id))
    void CWorld_SetFXState(int id, int on);                                 // dll:100138d0

    // ----- bg_fx.cpp: the CSpotFX family (vtable slots +0x18..+0x38) and the local FX -----
    bool CSpotFX_CanStart(CSpotFX& f);                                      // dll:1000a5b0 (+0x18)
    bool fxStart(CSpotFX& f);                                               // +0x1c: 100707e0 / 10003110 / 100179c0 (base: 0)
    bool fxPlay(CSpotFX& f);                                                // +0x20: 100708e0 / 10003340 / 10017a70
    bool fxStop(CSpotFX& f);                                                // +0x24: 10070920 / 10003390 / 10017ab0
    bool fxClose(CSpotFX& f);                                               // +0x28: 10070960 / 100033c0 / 10017ae0
    bool CSpotFX_OnStarted(CSpotFX& f, int handle, unsigned mask);          // +0x2c: 1000a850 (CSynthAnimFX: 10015dd0)
    bool CSpotFX_OnStopped(CSpotFX& f, int handle, unsigned mask);          // +0x30: 1000a930 (CSynthAnimFX: 10015e50)
    void fxReset(CSpotFX& f);                                               // +0x34: 1000aa60 (CMovieFX: 10003440)
    bool fxOnClosed(CSpotFX& f, int handle, unsigned mask);                 // +0x38: 1000aa20 (CMovieFX: 100033f0)
    void CSpotFX_ToggleLinkedHotspot(CSpotFX& f, bool enable, bool* found); // the 'Y'/'P' hotspot code of 1000a850 / 1000a930
    std::vector<CSpotFX*> spotFxOf(const std::vector<Item*>& list);
    bool localFxUpdate(CLocalFX& f);          // vtable +0x30: dll:10078230 (CSoundFX), 10073e20 (CRainFX)
    void localFxReset(CLocalFX& f);           // +0x28: dll:10078200 (CSoundFX), 10073db0 (CRainFX)
    bool localFxStop(CLocalFX& f);            // +0x34: dll:10078340 (CSoundFX), 10073f60 (CRainFX)
    void localFxSetOn(CLocalFX& f, int id);   // +0x24: dll:10006190
    void localFxSetOff(CLocalFX& f, int id);  // +0x2c: dll:100781c0 (CSoundFX), 10073d30 (CRainFX)
    bool isCurrentSite(const CSite* s) { return curSite() == s; }   // dll:1006ef60
    bool isAtPage(const std::string& page);                          // dll:1006ef40

    // ----- biosgame.cpp -----
    void buildSaveGame();
    void afterNewGameLoaded();                                              // Event_OnMenuCommand dll:10008010 case 1, after Eng_LoadGame

    EngineApi& api_;
    Eng eng_;
    const DataFiles& files_;
    Config config_;
    PaletteTable palettes_;
    std::unique_ptr<SaveGame> save_;
    std::unique_ptr<Sim> sim_;                 // the simulation core of the world (sim.h)
    friend class BiosFeatureHost;              // bg_features.cpp: the machines' view of the shell
    friend class BiosPlayerHost;               // bg_player.cpp: the player's and the objects' view of the shell
    friend class GdiPlayerDisplay;             // bg_display.cpp: the message strip
    std::unique_ptr<PlayerHost> playerHost_;   // Self::bind
    std::unique_ptr<PlayerDisplay> playerDisplay_;
    void attachPlayerHost();                   // bg_player.cpp
    friend class StoryLogic;                   // story.h: input, travel, story
    std::unique_ptr<StoryLogic> story_;
    std::unique_ptr<FeatureHost> featureHost_; // GameState::host
    void attachFeatureHost();                  // bg_features.cpp
    std::map<uint32_t, SoundSet> soundSets_;
    std::map<std::string, int> ambient_;       // handle slot of each ambient file entry (DLL string entries at +100)
    bool started_ = false;
    friend struct BiosGameTestAccess;
};

} // namespace biosys::game
