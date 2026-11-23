// StoryLogic: the input handling, travel and story of BiosGame.dll on top of BiosGame (biosgame.h). It is a friend of
// BiosGame: the translated functions use the DLL globals (`g`), the engine wrappers (`eng_`), the CEngineFX / CSite / CPage
// members and the world exactly as the original's code does. Member functions are named after the original's
// (`CPath_BeginJourney`) and carry the `// dll:ADDR` of the function they translate.
//
// Files:
//   story_hotspot.cpp   Event_OnHotspot (dll:1001db50), hover, right click, mouse delta, status bar, the page hotspots
//   story_path.cpp      CPath (dll:100014e0..10002680): journeys, energy cost, vines
//   story_events.cpp    CGame::TriggerEvent (dll:100092d0), CStory::CalcScore, CPage::TriggerMilestone, regrowth
//   story_keys.cpp      Event_OnKeyDown / OnKeyUp (dll:1001e6b0 / 10021790): cheat words, pod password entry
//   story_handlers.cpp  CSubTechHandler, CSynthHandler (the helicopter and the synth stalker)
//   story_pages.cpp     CIntroPage, CDeathPage, CGameEndPage
//   story_player.cpp    the calls into the player (CSelf) and the objects: thin forwards
//   feats_*.cpp         the story / house feature classes
#pragma once
#include "game/biosgame.h"
#include <deque>
#include <string>
#include <vector>

namespace biosys::game {

// What the text drawing would show. The original draws with GDI (TextOutA on the window DC) or through
// CSelf::ShowMessage (status-bar text); the text API is not in the engine yet, so every such call goes through this
// interface, which records one line per call (`game ui` prints and clears the record; BiosGame::Config::uiSink is
// called with each line as it is made, `--log-events` prints them as `event ui ...`).
struct StoryUi {
    std::vector<std::string> lines;
    std::function<void(const std::string&)> sink;
    void record(const std::string& line) {
        lines.push_back(line);
        if (sink) sink(line);
    }
};

class StoryLogic {
public:
    explicit StoryLogic(BiosGame& game);

    // ---- events (story_hotspot.cpp) -------------------------------------------------------------------------------
    int onHotspot(const std::string& name, int window, const std::string& page);          // dll:1001db50
    void onHotspotHover(const std::string& name, int window, bool entered);               // dll:100223c0
    void onRightClick();                                                                   // dll:100224d0
    void onMouseDelta(int& dx, int& dy);                                                   // dll:10022510
    void onStatusBarObject(int index, const std::string& name, int arg);                   // dll:10022860
    bool onEvent35(const std::string& page);                                               // dll:100224b0
    bool onEvent44();                                                                      // dll:100228f0
    void onReturnToSavedPage(const std::string& page);                                     // dll:100224c0
    void onSimStatusHotspot(const std::string& name);                                      // dll:10022450
    void onBS4Screen(int leaving);                                                         // dll:10021ae0
    bool onBS4Query(int id, void* buffer);                                                 // dll:10021b50
    void onTextInputDone(bool cancelled, const std::string& text);                         // dll:10022380
    void featureTextInput(CFeature& f, bool cancelled, const std::string& text);           // feature vtable +0x90

    // ---- keys (story_keys.cpp) ------------------------------------------------------------------------------------
    void onKeyDown(int vkey, int repeat, int flags, int& doDefault);                       // dll:1001e6b0
    void onKeyUp(int vkey, int repeat, int flags, int& doDefault);                         // dll:10021790
    void typeWord(const std::string& text, bool enter);                                    // port: a typed word (WM_CHAR / WM_KEYDOWN stream)

    // ---- story (story_events.cpp) ---------------------------------------------------------------------------------
    void CGame_TriggerEvent(uint32_t milestone);                                           // dll:100092d0
    void CStory_CalcScore();                                                               // dll:10009a10
    void CPage_TriggerMilestone(CPage& p);                                                 // dll:1000f480
    void pageOnEntered(CPage& p);                                                          // page vtable +0x24
    uint32_t milestones() const { return b.save_->header.milestones; }

    // ---- travel (story_path.cpp) ----------------------------------------------------------------------------------
    CPath* CSite_FindLink(CSite& site, const std::string& name);                           // dll:1000c960
    double CPath_GetEnergyCost(const CPath& p) const;                                      // dll:10002680
    bool CPath_OnHandAction(CPath& p);                                                     // dll:10001230
    void CPath_Clear(CPath& p, int alsoReverse);                                           // dll:10002540
    bool CPath_Arrive(CPath& p);                                                           // dll:100014e0
    bool CPath_BeginJourney(CPath& p, const std::string& hotspot);                         // dll:10001ac0
    bool CPath_TravelStep(CPath& p);                                                       // dll:100023b0
    void CPath_End(CPath& p);                                                              // dll:10002330
    bool CPath_PageLeft();                                                                 // dll:10002380
    bool CSelf_Journey(char letter);                                                       // dll:1006e220 (hotspot 'J')

    // ---- handlers (story_handlers.cpp) ----------------------------------------------------------------------------
    struct Handler {                  // the layout both handlers share (+0x04 state, +0x08 x, +0x10 timer, +0x18 sound, ...)
        int state = 0;                // +0x04 (1 idle, 3 following/chase, 4 attack, 6 busy)
        int x = 0;                    // +0x08
        double timer = 0;             // +0x10
        int sound = -1;               // +0x18
        int volume = 0, frequency = 0, pan = 0;   // +0x1c, +0x20, +0x24
    };
    Handler subtech, synth;
    uint32_t subtechLastUpdate = 0;   // _DAT_100a2a80
    uint32_t subtechLastChase = 0;    // DAT_100a2a78
    uint32_t subtechChase2 = 0;       // _DAT_100a2a7c
    int subtechGunSound = -1;         // DAT_100a2a74
    uint32_t synthLastUpdate = 0;     // _DAT_1009f984
    uint32_t synthLastFollow = 0;     // _DAT_1009f97c
    uint32_t synthAttackStart = 0;    // _DAT_1009f980
    int synthMovie = -1;              // DAT_1009f978
    void CSubTechHandler_Update();                                                         // dll:10022930
    bool CSubTechHandler_OnMediaEnd(int handle, int kind);                                 // dll:10022c60
    void CSubTechHandler_Shoot();                                                          // dll:10022e00
    void CSubTechHandler_OnNewDay();                                                       // dll:10022f50
    void CSubTechHandler_OnEnterPage();                                                    // dll:10022c00
    void CSubTechHandler_CloseSound();                                                     // dll:10022c10
    void CSynthHandler_Update();                                                           // dll:100084c0
    void CSynthHandler_OnEnterPage(const std::string& page);                               // dll:10008830
    void CSynthHandler_CloseSound();                                                       // dll:10008890
    bool CSynthHandler_OnMediaEnd(int handle, int kind);                                   // dll:100088e0
    bool CSynthHandler_OnHotspot(const std::string& upperName);                            // dll:100089a0
    void CSynthHandler_Attack();                                                           // dll:10008a30
    void closeHandlerSound(Handler& h);                                                    // dll:10022c10 / 10008890
    bool startAttackMovie(const std::string& file, int& handle);                           // the common end of the attacks
    void handlerSoundParams(Handler& h);                                                   // dll:10022e90 / 10008c10

    // ---- pages (story_pages.cpp) ----------------------------------------------------------------------------------
    void CIntroPage_OnEnter(CIntroPage& p);                                                // dll:10061d80
    void CIntroPage_OnEnterPage(CIntroPage& p);                                            // dll:10061db0
    int CIntroPage_OnIdle(CIntroPage& p);                                                  // dll:10061ec0
    int CIntroPage_OnKey(CIntroPage& p, int vkey);                                         // dll:10062460
    int CIntroPage_OnMovieEnd(CIntroPage& p, int handle, int kind);                        // dll:10062650
    int CIntroPage_OnHotspot(CIntroPage& p, const std::string& name);                      // dll:100626f0
    void CDeathPage_OnEnter(CDeathPage& p);                                                // dll:1002b0b0
    int CDeathPage_OnMovieEnd(CDeathPage& p, int handle, int kind);                        // dll:1002b220
    int CDeathPage_OnIdle(CDeathPage& p);                                                  // dll:1002b240
    int CDeathPage_OnKey(CDeathPage& p, int vkey);                                         // dll:1002b4e0
    bool CGameEndPage_MatchesName(const CGameEndPage& p, const std::string& name);         // dll:1006fe50
    void CGameEndPage_OnEnter(CGameEndPage& p);                                            // dll:1006fed0
    void CGameEndPage_OnEntered(CGameEndPage& p);                                          // dll:1006ff60 (vtable +0x20)
    int CGameEndPage_OnMovieEnd(int handle, int kind);                                     // dll:1006ffd0
    int CGameEndPage_OnIdle(CGameEndPage& p);                                              // dll:10070040
    int CGameEndPage_OnHotspot(CGameEndPage& p, const std::string& name);                  // dll:10070140
    int pageOnHotspot(CPage& p, const std::string& name);                                  // page vtable +0x34
    int pageOnKey(CPage& p, int vkey);                                                     // page vtable +0x30
    bool pageOnMediaEnd(CPage& p, int handle, int kind);                                   // the special pages' media end hooks (vtable +0x3c)
    int idleSpecialPage(int which);                                                        // the idle event of eIntro (0), eGameEnd (1), eDeath (2)
    int idleTravel();                                                                      // the idle event while a journey is on (dll:100023b0)
    void pageCancelFx(CPage& p);                                                           // dll:1000f940 (stop every FX of the page)
    void pageStopFxByHotspot(CPage& p, const std::string& hotspot, int slot);              // dll:1000fa60 / 1000fb30
    void pageDisableFx(CPage& p, int id);                                                  // dll:1000f9c0

    // ---- the player and the objects (story_player.cpp): the other worker's code, stubs ---------------------------
    void Self_ShowMessage(int id, int a = 0xffff, int b = 0);                              // dll:1006bc40 CSelf::ShowMessage(&CSelf, 1, id, a, b)
    void Self_Consume(int id);                                                             // dll:1006cf40
    int Self_Drink(int amount);                                                            // dll:1006ccd0
    int Self_CanPickUp(double weight);                                                     // dll:1006def0
    void Self_LoseEnergy(double v);                                                        // dll:CSelf::LoseEnergy
    void Self_LoseNutrition(double v);                                                     // dll:CSelf::LoseNutrition
    void Self_CloseStatusScreen();                                                         // dll:1006c790 FUN_1006c790 (clears the status window)
    void Self_RefreshPage();                                                               // dll:1006f100 FUN_1006f100
    void Self_OnSynthAttack();                                                             // dll:1006efd0
    void Self_ShowStatus(int show);                                                        // dll:CSelf::ShowStatus
    void Self_Rest();                                                                      // dll:CSelf::Rest
    bool Self_CanSleep();                                                                  // dll:CSelf::CanSleep
    void Self_Sleep(int a, int b, int c);                                                  // dll:CSelf::Sleep
    void Self_ReturnToSavedPage(const std::string& page);                                  // dll:1006ede0
    void Self_Die();                                                                       // dll:CSelf::Die
    int heldHotspot(const std::string& name);                                              // held object vtable +0x70 (base: 0)
    void heldDropOn(const std::string& name);                                              // held object vtable +0x90 (base: nothing)
    void heldRightClick();                                                                 // held object vtable +0x74
    void heldStatusBar();                                                                  // object vtable +0x28 (Event_Slot43)
    bool objectDescribeHint = false;

    void Self_QueueMessage(int id);                                                        // dll:1006f330
    void Self_ShowEnvironmentStatus(int show);                                             // dll:1006aa80
    void Self_Eat();                                                                       // CSelf::Eat (cheat BANANA)
    void Self_CatchMalaria();                                                              // CSelf::CatchMalaria
    void Self_SetGogglesPower(int percent);                                                // dll:1001d780
    void plantPickFruit(CPlant& p, const std::string& hotspot);                            // dll:100731b0
    void plantRefreshFruitImages(CPlant& p);                                               // dll:10073090
    void setOceanHealth(double v);

    // ---- display interface ----------------------------------------------------------------------------------------
    StoryUi ui;
    void uiMessage(int id);                                                                // CSelf::ShowMessage text
    void uiText(int x, int y, const std::string& text);                                    // CEngineFX::DrawText: draws and records
    void uiRecordText(int x, int y, const std::string& text);                              // the record only (what GdiSession's recorder calls)
    void uiTextRight(int x, int y, const std::string& text);                               // TextOutA at x - text width
    void uiClearHint();                                                                    // FUN_1006c700

    // ---- the cursor filter (CEngineFX +0x6c8..+0x8a4, story_handlers.cpp) -------------------------------------------
    struct MouseFilter {
        double gain = 0.33, feedback = -2.0;          // +0x6c8, +0x6d0 (set by dll:1005fdc0: FUN_100617d0(0.33, -2.0, 20))
        int window = 20;                              // +0x6d8
        int idxA = 20, idxB = 0;                      // +0x86c, +0x870
        double sumX = 0, sumY = 0, prevSumX = 0, prevSumY = 0;
        int lastX = 0, lastY = 0, active = 0;
        int ringX[0x32] = {}, ringY[0x32] = {};
        bool skipNext = false;                        // DAT_100bc0f0
        uint32_t lastStep = 0;                        // _DAT_100bc484
    } mf;
    void mouseFilterSet(double gain, double feedback, int window);                        // dll:100617d0
    void mouseFilterReset();                                                               // dll:10061820
    void synthMouseDelta(int& dx, int& dy);                                                // dll:100618a0
    int synthMouseIdle();                                                                  // dll:10061a30

    // ---- journey statics ------------------------------------------------------------------------------------------
    int journeySound = -1;                                                                 // DAT_1009f110
    int journeyDelayMs = 0;                                                                // the busy wait of TravelStep (performed only by the window, Config::busyWait)
    uint32_t hoverTick = 0;                                                                // DAT_1009f834
    std::string lastCheat;
    void cheatWord();                                                                      // the words of dll:1001e6b0
    int gameEndPageStage();                                                                // port: CGameEndPage +0x6c
    int podState(CFeature& f);                                                             // CPodTransport +0x58 (or -1)
    double podCharge(CFeature& f);                                                         // CPodTransport +0x60 (or 0)

    // ---- port state ----------------------------------------------------------------------------------------------
    std::vector<std::string> trace;       // one line per hotspot handled: `NAME kind` (the handler the dispatch chose)
    std::string lastKind;
    bool hotspotHandled = false;          // DAT_1009f838
    uint32_t cheatFlags = 0;              // DAT_100c2718: bit 0 IMMORTAL, bit 1 cheats unlocked
    double score = 0;                     // story +0x18
    double biosphereHealth = 0;           // story +0x20
    std::string debugCommand(const std::string& args);                                     // `game ...` commands of this file set

    // ---- helpers --------------------------------------------------------------------------------------------------
    BiosGame& b;
    Self& me() { return b.self(); }
    World& world() { return b.world(); }
    std::string loc() const { return std::string(reinterpret_cast<const char*>(b.save_->self.m_szLocation.data())); }
    char locChar() const { return static_cast<char>(b.save_->self.m_szLocation[0]); }
    CSite* site() { return b.curSite(); }
    CPage* page() { return b.curPage(); }
    GameObj* held() { return dynamic_cast<GameObj*>(b.self().m_pObjHolding.item); }
    CFeature* feature() { return dynamic_cast<CFeature*>(b.self().m_pFeature.item); }
    CPath* path() { return dynamic_cast<CPath*>(b.self().m_pPath); }
    uint32_t& worldDword(int offset, uint32_t& scratch);   // CWorld dwords beyond the blob's members (+0x298 / +0x29c / +0x2a0)

private:
    // cheat state
    std::string typed_;                   // DAT_100c4328..: the typed characters
    uint32_t typedTick_ = 0;
    CIntroPage* intro_ = nullptr;         // DAT_100a180c / DAT_100a1f1c (cached CWorld::FindPage("eIntro"))
    CGameEndPage* gameEnd_ = nullptr;     // DAT_100a1f20
    CDeathPage* death_ = nullptr;         // DAT_100a1f24
    uint32_t introTick_ = 0;
    // ending / death page statics
    uint32_t gameEndDeadline_ = 0;        // DAT_100bd894
    int gameEndMovie_ = -1;               // DAT_100bd898
    int gameEndLoop_ = -1;                // DAT_100bd8a8
    bool gameEndJump_ = false;            // DAT_100bd8a0
    bool gameEndMovieDone_ = false;       // _DAT_100bd89c
    std::string gameEndNext_;             // DAT_100c4318
    int deathMovie_ = -1;                 // DAT_100a3588
    friend class BiosGame;
};

} // namespace biosys::game
