// What the player (CSelf, self.h) and the game objects (gameobj.h) need from outside the data model.
//
//  * PlayerHost: the calls the DLL makes into the engine and into the shell (CEngineFX, CTimeline, CGame, the page
//    machinery) and the DLL globals that do not belong to CSelf. BiosGame implements it (bg_player.cpp); the test tool
//    (src/tools/selftest.cpp) implements it headlessly. Every virtual has a default that does what "no engine" means
//    (nothing), so a method may be added without touching the implementations. Each method names the original call.
//  * PlayerDisplay: the text drawing of CSelf::ShowMessage / ShowStatus (GDI in the original, which the engine cannot do
//    yet). The player code translates the logic up to the drawing and hands the finished text over.
//  * PlayerGlobals: the DLL globals only the player and the objects touch (names from analysis/notes/player.md).
//
// Sections are marked per author so that several people may add members side by side; add members to the end of your
// own section only.
#pragma once
#include "game/slot.h"
#include <array>
#include <string>

namespace biosys::game {

class Self;
class World;
class GameObj;
class Item;
struct Biome;

// ---- the text output of the player (CSelf::ShowMessage dll:1006bc40, ShowStatus dll:10068f90, ShowEnvironmentStatus
// dll:1006aa80). `stringId` is the id in the DLL's string table (analysis/out/dll_stringtable.txt, generated into
// self_text.gen.cpp as `dllString(id)`), `text` the resolved text. Positions and colours of the original's GDI drawing
// are not passed (the drawing code of the shell knows them); the semantic state is.
class PlayerDisplay {
public:
    virtual ~PlayerDisplay() = default;
    // The message line (y = 0x24, centred): the text of string `stringId` followed (strcat, no separator) by the
    // second line's text when CSelf+0x29c is set. id 0xffff (text empty) clears the line.
    virtual void showMessage(int stringId, const std::string& text) {}
    // The same line drawn again without a new message (ShowMessage(1, 0, 0, 0) refreshes the status bar and redraws the
    // current message). The default shows it again; the recording displays ignore it.
    virtual void redrawMessage(int stringId, const std::string& text) { showMessage(stringId, text); }
    // unused: the original appends the second line to the first (see showMessage)
    virtual void showSecondLine(int stringId, const std::string& text) {}
    // Status-bar words: which stat ("health", "energy", "nutrition", "hydration", "temperature"), its state (1 low,
    // 2 middle, 3 good; temperature 1 cold, 2 cool, 3 normal, 4 warm, 5 hot) and the word. An empty text clears the
    // word's place (health / temperature in state 3). For energy / nutrition / hydration in state 3 the original draws
    // the word in the background colour (invisible); the text is passed anyway.
    virtual void showStatusWord(const char* stat, int state, const std::string& text) {}
    // Anything else drawn by the status screens: one line of text with a tag naming the screen area (the tags are those of
    // GdiPlayerDisplay::showText, bg_display.cpp: its table gives the colour and the position of each)
    virtual void showText(const char* area, const std::string& text) {}

    // The status screens. screen = "status" (the player's own: ShowStatus) or "environment" (ShowEnvironmentStatus).
    // open: Eng_DisplayImage / Eng_DisplaySimStatusImage / Eng_ShowSimStatusWindow(1) and Eng_GetReleaseSimStatusDC(1);
    // false = no device context (the original then hides the window again and gives up).
    virtual bool statusScreenOpen(const char* screen, bool withPlant) { return true; }
    // SetTextColor(COLORREF) of the status screens where the colour depends on the state (or is set without a text following): the text
    // drawn next, and the ones after it that set no colour of their own, use it
    virtual void statusTextColour(unsigned colorRef) {}
    // A gauge: `value` is the stat, `pixels` the offset in pixels the original computes (ftol of value times a scale)
    virtual void statusScreenGauge(const char* name, double value, int pixels) {}
    // The time-of-day pie of the status screen (seconds of the day)
    virtual void statusScreenClock(int timeOfDay) {}
    // Eng_EnableSimStatusHotspot(name, 1)
    virtual void statusScreenHotspot(const char* name) {}
    // end of drawing: Eng_GetReleaseSimStatusDC(0), Eng_RedrawScreen (+ Eng_ShowSimStatusWindow(1) for "environment")
    virtual void statusScreenDone(const char* screen) {}
    // close: Eng_DisplayImage(blackbar, hide) + Eng_ShowSimStatusWindow(0)
    virtual void statusScreenClose(const char* screen) {}
};

// ---- the DLL globals that are not members of CSelf ------------------------------------------------------------------
struct PlayerGlobals {
    // --- player model (self_update.cpp, self_consume.cpp)
    i32 lastHeartBeatVol = -10000;       // DAT_100bd48c (UpdateHeartBeatSound)
    u16 lastBreathingState = 0;          // DAT_100bd4b4 (UpdateBreathingSound)
    u32 lastThirstMessageTick = 0;       // DAT_100bd308 (the thirst cue's help message, at most every 5 s)
    Item* s9Cel = nullptr;               // DAT_100bcff0 the biome cel "S9" (found on first use)

    // --- sleep, wake, dreams, idle, page (self_sleep.cpp, self_msg.cpp)
    // sleep state (self_sleep.cpp): the statics of CSelf::Sleep / OnIdle / WakeUp
    u32 sleepSeconds = 0;                   // DAT_100c42e0: seconds the sleep lasts (the drawn duration times the biome factor)
    i32 sleepAwayFromBed = 0;               // DAT_100c42e4: Sleep's first argument (nonzero = a nap: no bag warmth, no energy limit)
    double sleepEnergyCap = 0;              // DAT_100c42e8: sleep stops giving energy once energy - start energy reaches this (80.0 / 100.0)
    double sleepEnergyGain = 0;             // DAT_100c42f0: energy per 90 s step
    i32 sleepingPill = 0;                   // DAT_100c42f8: 1 when CSelf+0x220 > 1 (sleeping pill: nothing wakes the player)
    double sleepHealthGain = 0;             // DAT_100c4300: health per 90 s step
    i32 sleepWithDream = 0;                 // DAT_100c430c: Sleep's second argument (a dream follows)
    u32 hotspotMsgExpiry = 0;               // DAT_100bc7b0: GetTickCount() after which the hotspot message is cleared
    double sleepEnergyGained = 0;           // CSelf+0x108 (not a member of Self): energy gained by the last sleep
    i32 angelSequenceDone = 0;              // CSelf+0x760 = DAT_100c2498 (set by the story code): OnIdle returns to the saved page
    i32 panoramaX = 0;                      // CSelf+0x754: the panorama x offset of the page jump after sleep (FUN_1006efa0)

    // --- messages (self_msg.cpp)
    i32 lastShownMsg1 = 0;                  // DAT_100bd1d0: CSelf+0x298 as last drawn by ShowMessage (the message line is cleared when it changes)
    i32 lastShownMsg2 = 0;                  // DAT_100bd1d4: CSelf+0x29c as last drawn by ShowMessage
    i32 lastShownLine1 = 0;                 // DAT_100bd1d8: the same for ShowMessageLine (dll:1006c400)
    i32 lastShownLine2 = 0;                 // DAT_100bd1dc

    // --- objects (gameobj.cpp, obj_*.cpp)
    i32 describeSuppressed = 0;             // DAT_100c42d0: the next CGameObj::Describe prints nothing (set before a hand change)
    i32 dat1009f838 = 0;                    // DAT_1009f838: set when an object flagged 0x200 is picked up / dropped at the site N or A
    std::string fishObject;                 // DAT_100c4280 char[12]: name of the fish the fishing rod found ("" = none)
    i32 fishHeading = 0;                    // DAT_100c428c: compass heading when the rod was cast
    i32 fishBiteTime = -1;                  // DAT_100a1378: game time of the next bite (-1 = none)
    GameObj* blowTarget = nullptr;          // DAT_100bb90c: the thorn the blowpipe fires (FUN_1005a4c0)
    std::string paperReturnPage;            // DAT_100c4290 (PTR_DAT_100a1554): the page the open paper item returns to
};

// ---- the shell ---------------------------------------------------------------------------------------------------------
class PlayerHost {
public:
    virtual ~PlayerHost() = default;

    // --- clock and world (CTimeline / CWorld) ---
    // CTimeline::Advance(&CTimeline, seconds, updateSelf) dll:100154b0: runs the world updates and, when updateSelf,
    // CSelf::Update(0) in steps of 90 game seconds; the timeline advances by `seconds`.
    virtual void advanceTime(unsigned seconds, int updateSelf) {}

    // --- CEngineFX (the engine wrappers of bg_enginefx.cpp) ---
    virtual void playSound(const char* file) {}                              // FUN_10060700(&CEngineFX, file, 0, 0)

    // --- shell globals ---

    // --- metabolism (self_update.cpp) ---
    virtual u32 getTickCount() { return 0; }                                 // GetTickCount()
    virtual u32 lastHoverTick() { return 0; }                                // DAT_1009f834: GetTickCount() of the last Event_OnHotspotHover (0 = none yet)
    virtual u32 cheatFlags() { return 0; }                                   // DAT_100c2718: bit 0 IMMORTAL, bit 1 cheats unlocked
    virtual int modalFlag() { return 0; }                                    // DAT_100c275c (story +0x3c): Die is ignored while set
    virtual int dead() { return dead_; }                                     // DAT_100c2728 (story +8): the dead flag
    virtual void setDead(int v) { dead_ = v; }
    // FUN_100616f0(&CEngineFX, a, b, c, d) adds one record to the page jiggle list (10 at most); FUN_10061790(&CEngineFX, a, b, c)
    // = Eng_JigglePage(list, count, a, b, c) plays and empties it
    virtual void addJiggle(int a, int b, int c, int d) {}
    virtual void jigglePage(int a, int b, int c) {}
    // CEngineFX::PlayLoopedWav dll:10060860 (&CEngineFX, file, volume, &handle, NULL, 0): does nothing when sounds are off
    // (fast-forward flag DAT_1009f83c) or *handle != -1; stores the engine's handle (-1 on failure) in *handle
    virtual void playLoopedWav(const char* file, int volume, int* handle) {}
    // FUN_100609b0(&handle): Eng_CloseSound(*handle) and, when that succeeded, *handle = -1
    virtual void closeSoundHandle(int* handle) { *handle = -1; }
    virtual void setSoundVolume(int handle, int volume) {}                   // FUN_10060980: Eng_SetSoundProperty(handle, "VOLUME", volume), not for -1
    // Eng_CloseSound(handle) without the reset of the variable (FUN_10065150 resets it itself)
    virtual void closeSound(int handle) {}
    // FUN_1000c6e0(site): sets the sound handles of the four sound buffers of the site (CSite+0xc0..0xcc, 0x68 bytes per entry) to -1
    virtual void resetSiteSoundHandles(Item* site) {}
    // SendMessageA(DAT_100c4324 (the game window), WM_KEYUP, vk, lParam)
    virtual void sendKeyUp(int vk, unsigned lParam) {}

    // --- objects (gameobj.cpp, obj_*.cpp); the defaults say "the engine accepted" so that the object logic runs headless ---
    // Eng_RemoveGameObject dll:100750c0 (API 0x26)
    virtual bool Eng_RemoveGameObject(const char* object) { return true; }
    // Eng_ChangeGameObjectVariation1 dll:10075fe0 (API 0x57): the variation of the object of the event in progress
    virtual bool Eng_ChangeGameObjectVariation1(const char* variation, int group) { return true; }
    // Eng_Api52_ReturnGameObject dll:10075200 / Eng_Api45_ReturnGameObjectHidden dll:10075160: put the object back
    virtual bool Eng_Api52_ReturnGameObject(const char* object) { return true; }
    virtual bool Eng_Api45_ReturnGameObjectHidden(const char* object) { return true; }
    // Eng_GetGameObjectInfo dll:100744f0 (API 0x0e): info[9] is the usage count (see engineapi.h)
    virtual int Eng_GetGameObjectInfo(const char* name, std::array<int, 12>& info) { info.fill(0); return 1; }
    // Eng_SetGameObjectProperty dll:10074670 (API 0x12): "usagecount" (-1 = unlimited) or "weight"
    virtual bool Eng_SetGameObjectProperty(const char* object, const char* property, int value) { return true; }
    // Eng_Api48_DropHeldObject dll:10076170 (API 0x48): drops the held object into the zone of `hotspot` (null = anywhere)
    virtual bool Eng_Api48_DropHeldObject(const char* hotspot) { return true; }
    virtual void Eng_Api6D_SetScrollOptions(int a, int b) {}                 // dll:Eng_Api6D_SetScrollOptions
    virtual void Eng_StopSound(int handle) {}                                // Eng_StopSound(handle) (the wrapper under CEngineFX::StopSound)
    // Eng_DisplayImage dll:100742d0 (image, window, xy or null, unk04, unk20, unkOut, unk08)
    virtual void Eng_DisplayImage(const char* image, int window, const int* xy, int unk04, int unk20, int* unkOut, int unk08) {}
    virtual void CEngineFX_SetImageVisibility(const char* image, int visible, int window) {}   // CEngineFX::SetImageVisibility dll:10060c70
    virtual void CEngineFX_HotspotEnable(const char* hotspot, int enable, int window) {}       // FUN_10060c90
    // FUN_100609f0 CEngineFX::PlayAnim: *handle receives the engine's handle; returns 1 when the animation plays
    virtual int CEngineFX_PlayAnim(const char* file, int* handle, int window, int frameDelay, int x, int y, int pageRelative,
                                   int keepPainted, const char* wav) { return 0; }
    virtual void CEngineFX_PlaceWorn(const char* object, const char* extraImage, int x, int y) {}   // FUN_10060c30
    virtual int CEngineFX_ChangeHoldingImagesGroup(const char* object, int group) { return 1; }   // dll:100616a0
    virtual void CWorld_StopPageFX(int id) {}                                // dll:10014310
    virtual void CTimeline_ResetDiurnal() {}                                 // DAT_100c1c44 = 0; CTimeline::UpdateDiurnal(1, 1, 0, 1)
    virtual void triggerEvent(u32 milestone) {}                              // CGame::TriggerEvent dll:100092d0
    // The synth handler (DAT_100c27e0, SubTech/Synth worker): state word, the stalker's x position and distance
    virtual void setSynthState(int state) {}                                 // DAT_100c27e4 = state
    virtual int synthX() { return 0; }                                       // DAT_100c27e8
    virtual double synthDistance() { return 1.0e9; }                         // DAT_100c27f0
    virtual void CSynthHandler_Attack() {}                                   // CSynthHandler::Attack(&DAT_100c27e0)
    // hooks into the plant / path / feature code (BiosPlayerHost, bg_player.cpp, overrides all three)
    virtual void CPlant_UseObject(Item* plant, GameObj* object, const std::string& hotspot) {}   // FUN_10072a00 (plant, object, UPPER hotspot)
    virtual void CPath_UseObject(Item* path, GameObj* object) {}             // FUN_10001200 (path from the site's link list "L...")
    virtual void setFeatureObject(Item* feature, GameObj* object) {}         // *(feature + 0x58) = object (CVideoPlayer / CDataTerminal)
    // FUN_1001d600's CEngineFX::LoadPalette(site palette entry +500, +0x44c, 0, 1, 1, 0): the goggles' palette of the site
    virtual bool CEngineFX_LoadGogglesPalette(Item* site) { return true; }
    // The alarm clock's display (FUN_100165a0): Eng_DisplayImage / GDI text of the clock and the alarm time
    // FUN_100611b0(&CEngineFX, 0x2037703, 1, 0x2000000) + FUN_10061500 twice (the timeline's clock text at (0xeb, 0xb9) in
    // `fontClock`, `alarmText` at (0x11e, 0x7c) in `fontAlarm`) + Eng_RedrawScreen
    virtual void drawAlarmDisplay(const std::string& alarmText, int fontClock, int fontAlarm) {}
    virtual int CEngineFX_CreateLogicalFont(const char* face, int height, int* font) { *font = 0; return 1; }   // dll:100613a0
    virtual bool CEngineFX_CreateFontResource(const char* file) { return true; }   // dll:100611e0
    virtual void CEngineFX_RemoveFontResource(const char* file) {}           // dll:10061300
    virtual void CEngineFX_DeleteFont(int font) {}                           // FUN_100614e0
    // DAT_100c11fc (map 0, the paper items flagged 0x8000000) / DAT_100c1200 (map 1, flagged 0x10000000): bit n set = the
    // n-th route of the map is known (the 15 / 12 images "R1R2".. / "S1S2"..)
    virtual u32 mapRoutes(int map) { return 0; }
    // FUN_1000fb30(page, name): every FX of the page whose name contains `name` gets its vtable +0x28 called
    virtual void CPage_FxNamedStop(Item* page, const char* name) {}
    // CPlant::Display(plant, p) (the plant's page images) and Eng_Api5E_ReleasePageImageSlot(plant's image slot `slot`
    // (plant + 0x18 + slot * 4), a, b)
    virtual void CPlant_Display(Item* plant, int p) {}
    virtual void Eng_Api5E_ReleasePageImageSlot(Item* plant, int slot, int a, int b) {}

    // --- sleep, wake, dreams, idle, page (self_sleep.cpp, self_page.cpp); named after the original call ---
    virtual u32 fadeFromBlackUntil() { return 0; }                           // DAT_100c4310
    virtual bool gameEnded() { return false; }                               // DAT_100c2724
    virtual bool paletteOn() { return true; }                                // DAT_100c1864 (CEngineFX+0x56c)
    virtual bool rainFilter() { return false; }                              // DAT_1009f848
    virtual int paletteLockOwner() { return 0; }                             // DAT_100c1860 (CEngineFX+0x568, the palette lock token)
    virtual void setForcePalette(int v) {}                                   // DAT_100c3630 = v
    virtual void setStartupPhase(int v) {}                                   // DAT_100c3a38 = v
    virtual bool CEngineFX_FadeToColour(int r, int g, int b, int back, int speed, int percent) { return true; }   // FUN_100605f0
    virtual bool CEngineFX_LockPalette(int token) { return true; }           // FUN_10060ce0
    virtual bool CEngineFX_UnlockPalette(int token) { return true; }         // FUN_10060cb0
    virtual bool CEngineFX_LockSounds(int token) { return true; }            // FUN_100606a0
    virtual bool CEngineFX_UnlockSounds(int token) { return true; }          // FUN_100606d0
    virtual bool CEngineFX_LoadPalette(const char* file, const char* xft, int steps, int immediate, int token, int unk) { return true; }   // FUN_10060170
    virtual void CEngineFX_PlayRandomSound(const char* stem, unsigned n) {}  // FUN_10060820 ("<stem><1 + rand() % n>.wav")
    virtual void CEngineFX_ClearWindow() {}                                  // CEngineFX::ClearWindow dll:10060df0
    virtual bool CEngineFX_StopSound(int handle) { return true; }            // FUN_100609d0
    virtual void CEngineFX_StopLoopedAvi() {}                                // FUN_10061110
    virtual void CEngineFX_LoopedAviClosed() {}                              // FUN_10061120
    // Eng_OpenAVIMovie(file, window, x, y, OnMovieEvent, loop 0, unk04 0, palette = CEngineFX+0x104, &handle) dll:100752d0; returns success
    virtual int Eng_OpenAVIMovie(const char* file, int window, int x, int y, int& handle) { return 0; }
    virtual void setMovieHandle(int handle) {}                               // DAT_100c18f0 = handle
    virtual bool Eng_PlayAVIMovie(int handle, int restart) { return true; }  // dll:10075650
    virtual int Eng_GetLastError() { return 0; }                             // dll:10073fb0
    virtual void Eng_GetPanoramaInfo(int& a, int& b, int& x) { a = b = x = 0; }   // Eng_GetPanoramaInfo
    virtual void Eng_BeginPageFilter(int a, int b, int c) {}                 // Eng_BeginPageFilter
    virtual void Eng_DoHotspotAction(const char* hotspot, int p) {}          // Eng_DoHotspotAction
    virtual void CTimeline_SetTicksPerGameSec(int ticks) {}                  // dll:10015ab0
    virtual void CTimeline_UpdateDiurnal(int p1, int p2, int p3, int p4) {}  // dll:10015710
    virtual void CWorld_StartPageFX(int id) {}                               // dll:10014280
    virtual void CWorld_LocalFxUpdate() {}                                   // FUN_10014350
    virtual void CSite_CloseAllSets() {}                                     // FUN_1000e6b0
    virtual void CSite_PlayAmbientSounds(Item* site) {}                      // FUN_1000eaa0(site): day / night / dead ambient sounds
    virtual void CSite_RefreshFx(Item* site) {}                              // FUN_1000ca00(site)
    virtual void CSite_OnPageEntered(Item* site, const char* page) {}        // site vtable +0x24 = FUN_1000d940 (the argument is unused)
    virtual void CPage_Exit(Item* page) {}                                   // page vtable +0x28
    virtual void CPage_OnTrigger(Item* page) {}                              // page vtable +0x24 (CPage::TriggerMilestone dll:1000f480; CGameEndPage dll:1006ffc0)
    virtual void CPage_EnterAndUpdate(Item* page) {}                         // FUN_1000f540: DAT_100c2a14 = 1; vtable +0x1c; +0x20; DAT_100c2a14 = 0
    virtual void CPath_PageLeft() {}                                         // FUN_10002380
    virtual void CPlant_ReleasePageImages(Item* plant) {}                    // FUN_10073a60
    // the held / worn object's virtual `vtableOffset` (+0x80 / +0x84 / +0x88) called with the page name
    virtual void objectVirtual(Item* object, unsigned vtableOffset, const char* page) {}
    // FUN_10029440 core: Eng_ChangeGameObjectVariation(object name, variation, group); true = the engine accepted
    virtual bool ChangeGameObjectVariation(const char* object, const char* variation, int group) { return false; }

    // --- consume (self_consume.cpp) ---
    // FUN_10060780(&CEngineFX, file, &handle, 0, 0): starts the sound when sounds are on, *handle == -1 and the file opens;
    // stores the new handle in *handle and returns true; false when it did not start (the host keeps this logic).
    virtual bool playSoundHandle(const char* file, int* handle) { return false; }
    // Eng_FadeToColour (FUN_10060620 passes unk04 = 1, FUN_100605f0 passes 0)
    virtual void fadeToColour(int r, int g, int b, int unk04, int speed, int unk1c, int percent) {}
    // FUN_10060670(&CEngineFX, p) = Eng_FadeToPalette(&CEngineFX+0x104, 0, p, 100)
    virtual void fadeToPalette(int p) {}
    // FUN_10061820(&CEngineFX): zeroes the sway/blur state of the engine FX (+0x86c..0x8a4 and the two 50-dword tables)
    virtual void resetEngineFXSway() {}
    virtual void enableMouseDeltaEvent(int enable) {}                        // Eng_EnableMouseDeltaEvent dll:100767c0
    virtual void enableUserInput(int enable, int unk) {}                     // Eng_Api2E_EnableUserInput dll:10075750
    // Eng_PlayObjectUseAnimation dll:10076060 (variation may be null)
    virtual void playObjectUseAnimation(const char* tanFile, const char* wavFile, int hideHeld, int removeHeldAfter,
                                        int x, int y, int playMode, int frameDelayMs, const char* variation, int holdGroup) {}
    // CEngineFX::JumpToPage dll:10060d10 (&CEngineFX, page, p2, p3, p4, p5); returns the engine's success flag
    virtual int jumpToPage(const char* page, int p2, int p3, int p4, int p5) { return 0; }
    virtual void clearWindow(int handle, int colour) {}                      // FUN_1006c790: Eng_ClearWindow(CSelf+0x328, 0)
    virtual void blankTopBar(int handle) {}                                  // FUN_1006f100: fills the top bar rectangle (0,0,0x27f,0x37) black
    virtual void redrawPage() {}                                             // Eng_RedrawPage dll:100752a0
    virtual void redrawScreen() {}                                           // Eng_RedrawScreen dll:10076220
    virtual void refreshPage(Item* page) {}                                  // FUN_1000f730(page): the page's FX list update
    virtual void setFXState(int id, int on) {}                               // CWorld::SetFXState dll:100138d0
    virtual void writeLog(const std::string& text) {}                        // Eng_WriteLog dll:100742a0
    // --- messages and status screens (self_msg.cpp) ---
    virtual bool gameStarted() { return true; }                              // DAT_1009f824: 0 while the intro page is active
    virtual bool windowDC() { return true; }                                 // Eng_GetWindowDC(CSelf+0x328, ...) succeeded and returned a DC
    virtual int synthState() { return 0; }                                   // DAT_100c27e4 (synth handler +4)
    virtual int subTechState() { return 0; }                                 // DAT_100c24bc (CSubTechHandler +4)
    // FUN_1001d600(goggles): re-applies the page palette that the worn goggles replaced (objects worker, CGoggles)
    virtual void restoreGogglesPalette(Item* goggles) {}
    // FUN_10009b10(biome): mean plant health 0..1 of the biome's cels, or -1.0 for a biome without any (story / cels)
    virtual double biomeHealthFraction(const Biome& biome) { return -1.0; }
private:
    int dead_ = 0;                                                           // DAT_100c2728
};

} // namespace biosys::game
