// Interfaces through which the engine talks to the game logic (the original BiosGame.dll) and to the
// blocking object-animation player. Both are implemented outside the engine core.
//
// GameLogic is the engine -> DLL direction of the original interface: the 46 event slots of the table the
// DLL fills in `GetEventFunctionTable` (dll:100044b0), plus the three media callbacks. One virtual method
// per slot, named as in analysis/notes/interface.md section 2; the comment above each gives
//   slot N, engine wrapper (exe:xxxxxxxx) that calls it, DLL handler (dll:xxxxxxxx) of Biosys' DLL.
// The default bodies do what the engine's wrapper does when the DLL has no handler for the slot (the
// "default" column of the return values below), so a logic only overrides what it needs.
// The engine never calls these directly: it goes through ControlProgram (controlprogram.h), which adds the
// API restriction mask, the OnTimer bookkeeping and the "startup / shutdown sent" gating of the wrappers.
// Window ids: -1 main window, 0..19 popup window, -3 objects status bar.
#pragma once
#include "engine/common.h"
#include "engine/objectfile.h"
#include <string>

namespace biosys {

// Keys the engine reacts to (key release, spec 1.6.6; Shift toggles the bar mode on key press).
enum class Key { Other, S, D, Space, Shift, Ctrl };

// Windows virtual-key codes of the keys above (what the DLL's OnKeyDown / OnKeyUp receive). Key::Other has
// no code of its own: the front end passes the real one.
constexpr int kVkS = 0x53, kVkD = 0x44, kVkSpace = 0x20, kVkShift = 0x10, kVkCtrl = 0x11;
int vkOfKey(Key k);

// Outputs of event 25 (OnGetCrossFade), see GameLogic::onGetCrossFadeEx.
struct CrossFadeSpec {
    std::string xftFile;            // lookup file relative to BIODATA ("" = use xftTable)
    std::vector<u8> xftTable;       // the 64x64x64 table itself (262144 bytes), optional
    std::vector<u8> palette;        // 256 x {R,G,B,flags} (1024 bytes): required, else no fade is shown
    std::string altName;            // name of another cross-fade definition of the page ("" = keep)
};

class GameLogic {
public:
    virtual ~GameLogic() = default;

    // slot 0, exe:004408d0, dll:1001d7b0. Sent once after the engine started, before the first page. Return
    // 0: the engine itself jumps to the start page. (Biosys' DLL builds its whole simulation here, returns 0.)
    virtual int onStartup() { return 0; }
    // slot 1, exe:004409d0, dll:1001d8c0. Sent once at shutdown (mask 1).
    virtual void onShutdown() {}
    // slot 2, exe:00440a40, dll:1001d8e0. A page jump finished (page displayed, objects painted). The
    // original also passes a copy of the 46-slot table and copies slots 5 and 6 back; with virtual methods
    // that has no equivalent (Biosys' DLL ignores the argument).
    virtual void onEnterPage(const std::string& pageName) {}
    // slot 3, exe:00440af0, dll:1001d950. The main page is about to be replaced. No arguments in the
    // original (the table copy of slot 2 aside); the old page is still the engine's current page (mask 6).
    virtual void onExitPage() {}
    // slot 4, exe:00440b90, dll:1001d970. The simulation tick: once the period set by API 1 (default 5000 ms)
    // has elapsed, checked every 500 ms while the app is active.
    virtual void onTimer() {}
    // slot 5, exe:00440c20, dll:1001db50. A hotspot was activated (mask 8). Return 1: the engine performs the
    // hotspot's own action; any other value suppresses it. Not sent while the menu or the sim-status window
    // is up. Default (no handler): 1.
    virtual int onHotspot(const std::string& hotspot, int windowId, const std::string& pageName) { return 1; }
    // slot 6, exe:00440cd0, dll:1001e2f0 (returns 1). A page timer fired (mask 8). Nonzero: run its action.
    virtual int onPageTimer(const std::string& timer, int windowId, const std::string& pageName) { return 1; }
    // slot 7, exe:00440d60, dll:1001e300. A page was read, nothing drawn yet (mask 4). Not sent for window ids -5/-6.
    virtual void onPageLoaded(const std::string& pageName, int windowId) {}
    // slot 8, exe:00440de0, dll:1001e340. The page image was rendered to its surface (mask 4).
    virtual void onPageDrawn(const std::string& pageName, int windowId) {}
    // slot 9, exe:00440e60, dll:1001e350. A popup page was displayed (also the "topbar" page).
    virtual void onPopupOpened(const std::string& pageName, int popupIndex) {}
    // slot 10, exe:00440ee0, dll:1001e370 (empty). A popup was closed (mask 6).
    virtual void onPopupClosed(int popupIndex) {}
    // slot 11, exe:00440f50, dll:1001e380. A game-object state transition (spec 1.13, codes 1..0x10);
    // `extra` is the hotspot name for codes 6 and 7, "" for the others. Nested at most 5 deep. Mask 0.
    virtual void onGameObject(const std::string& objectName, int eventCode, const std::string& extra) {}
    // slot 12, exe:00441030, dll:1001e650 (empty). Every WM_ACTIVATEAPP.
    virtual void onActivateApp(bool active) {}
    // slot 13, exe:004410b0, dll:10022910. A palette change finished (SetPalette / fade step); guess at the exact meaning.
    virtual void onPaletteChanged() {}
    // slot 14, exe:00441120, dll:1001e6b0. A key was pressed (WM_KEYDOWN: vkey, repeat count, flags). The
    // wrapper presets doDefault = 1 before the call; the handler may clear it.
    virtual void onKeyDown(int vkey, int repeatCount, int flags, int& doDefault) {}
    // slot 15, exe:004411b0, dll:10021790. A key was released. doDefault preset to 1; if it is still 1 the
    // engine runs the game-object key handler (S / D / Space, spec 1.6.6).
    virtual void onKeyUp(int vkey, int repeatCount, int flags, int& doDefault) {}
    // slot 16, exe:00441240, dll:10021880 (empty). Called with (0, 0) when the main window leaves its
    // high-priority drag-scroll state (guess at the meaning; not understood further).
    virtual void onEvent16(int unk0, int unk1) {}
    // slot 17, exe:004412c0, dll:10021890. Idle pass of the message loop; return nonzero while more idle
    // work is wanted. Default (and before startup / after shutdown was sent): 0.
    virtual int onIdle(int idleCount) { return 0; }
    // slot 18, exe:00441360, dll:10021ae0. The built-in "BS4" terminal screen is entered (0) / left (1). Mask 1.
    virtual void onBS4Screen(int leaving) {}
    // slot 19, exe:004413d0, dll:10021b50. The BS4 screen asks for data (query id 5, 6, 7, 0xb ...; the
    // buffer layout depends on the id). Default 0.
    virtual bool onBS4Query(int queryId, void* buffer) { return false; }
    // slot 20, exe:00441450, dll:1001e660. Only when the activation state really changes.
    virtual void onAppActiveChanged(bool active) {}
    // slot 21, exe:004414d0, dll:10021ca0 (empty). WM_PALETTECHANGED.
    virtual void onSystemPaletteChanged() {}
    // slot 22, exe:00441540, dll:10021cb0. The view direction on a 360-degree page entered compass sector 1..8.
    virtual void onCompassHeading(int sector) {}
    // slot 23, exe:004415b0, dll:10021cc0. Save game: the DLL appends its state to the file. Default 1.
    virtual bool onSaveGame(const std::string& fileName) { return true; }
    // slot 24, exe:004415f0, dll:10021d00. Load game: the DLL reads its state at `offset` (mask 1). Default 1.
    virtual bool onLoadGame(const std::string& fileName, long offset) { return true; }
    // slot 25, exe:00441680, dll:10021d60. Cross-fade page jump: the DLL may supply a different cross-fade
    // definition (mask 4). Argument roles are a guess: srcPage = the page name buffer the engine fills,
    // destPage = name of the target page, out1..out3 = unknown outputs, outCrossFade = name of the
    // cross-fade definition to use (dll writes a name into it, and a second string pointer through a stack
    // slot Ghidra could not type). Default 0.
    virtual bool onGetCrossFade(const std::string& srcPage, const std::string& destPage, u32& out1, int& out2, u32& out3,
                                std::string& outCrossFade) { return false; }
    // slot 25, exact reading of the same call (exe:00444ca0 asm; the roles above are wrong): the engine passes the
    // cross-fade definition's name and the destination page name and receives, through four output pointers,
    // the lookup (.XFT) the blend maps its colours through - either the file name (a4, BIODATA-relative, the DLL
    // sends "%s%s" of two strings) or the 262144-byte table itself (a3, the DLL sends 0) -, the 256 x 4 byte
    // palette the two images are in (a5) and optionally the name of another cross-fade definition of the
    // current page to use instead (a6, the DLL leaves it 0). Returns false = no cross-fade (plain jump).
    // The engine calls this one; the default body forwards nothing and returns false.
    virtual bool onGetCrossFadeEx(const std::string& defName, const std::string& destPage, CrossFadeSpec& spec) { return false; }
    // slot 26, exe:00441720, dll:10021db0. BS4 screen sets a value of a "BiosFear feature". Default 0.
    virtual bool onSetFeatureParameter(const std::string& featureName, int paramId, int a, int b, int c) { return false; }
    // slot 27, exe:004417b0, dll:10021df0. BS4 screen reads a feature parameter (outInt and/or outValue). Default 0.
    virtual bool onGetFeatureParameter(const std::string& featureName, int paramId, u32& outInt, double& outValue) { return false; }
    // slot 28, exe:00441840, dll:10022380. The text-entry mode started by API 0x50 ended (cancelled / text).
    virtual void onTextInputDone(bool cancelled, const std::string& text) {}
    // slot 29, exe:004418c0, dll:10022350. Which fruit object does the page ornament with this name show?
    // (spec 1.9; mask 1). Return false if none. The name is written to outObjectName.
    virtual bool onGetNameOfFruitObjectFromPageImageName(const std::string& pageImageName, std::string& outObjectName) { return false; }
    // slot 30, exe:00441940, dll:100223b0 (returns 0). API 0x53 restarted the engine state. Default 0.
    virtual int onRestart() { return 0; }
    // slot 31, exe:004419e0, dll:10008010. Menu command (0 opened, 1 new game, 2 save slot arg, 3 load ...).
    virtual void onMenuCommand(int code, int arg) {}
    // slot 32, exe:00441a60, dll:10008350. The menu asks about a saved game slot: DLL fills exists + description.
    virtual void onMenuQuerySavedGame(int slot, int& outExists, std::string& outDescription, int unk) {}
    // slot 33, exe:00441af0, dll:100223c0. The pointer entered (1) / left (0) a hotspot (mask 8).
    virtual void onHotspotHover(const std::string& hotspot, int windowId, bool entered) {}
    // slot 34, exe:00441b70, dll:10022450. A hotspot of the modal sim-status window was clicked.
    virtual void onSimStatusHotspot(const std::string& hotspot) {}
    // slot 35, exe:00441bf0, dll:100224b0. Right click on a main page that can return to the saved page: use
    // the transition? (guess at the name; mask 1). Default 1.
    virtual bool onEvent35(const std::string& savedPageName) { return true; }
    // slot 36, exe:00441c80, dll:100224c0. The right-click return to the saved page happened (guess at name; mask 4).
    virtual void onReturnToSavedPage(const std::string& pageName) {}
    // slot 37, exe:00441d00, dll:100228d0 (empty). The current page was saved as the return page (guess; mask 1).
    virtual void onEvent37() {}
    // slot 38, exe:00441d70, dll:100228e0 (empty). The saved return page was discarded (guess; mask 1).
    virtual void onEvent38() {}
    // slot 39, exe:00441de0, dll:100224d0. Right button pressed and no return-to-saved-page happened.
    virtual void onRightClick() {}
    // slot 40, exe:00441e50, dll:10022510. Scroll delta of a mouse move; the DLL may change it. Only sent
    // after API 0x5c enabled it.
    virtual void onMouseDelta(int& dx, int& dy) {}
    // slot 41: not set by Biosys' DLL, no caller in the engine. Declared so that all 46 slots exist.
    virtual void onEvent41() {}
    // slot 42, exe:00441ed0, dll:10022530. Per-pixel page filter between API 0x32 and 0x33 (mask unchanged);
    // dst..dstEnd = destination bytes, buf/bufLen from API 0x5b, a/b/c = the three bytes of API 0x32. Default 0.
    virtual bool onBlitPage(u8* dst, u8* dstEnd, void* buf, int bufLen, u8 a, u8 b, u8 c) { return false; }
    // slot 43, exe:00441f20, dll:10022860. The objects status bar changed (insert / delete / change image /
    // scroll): objectIndex into the object table or the slot under the centre, objectName, arg (0: show the
    // object's description). Guess at the exact meaning. Mask 4.
    virtual void onStatusBarObject(int objectIndex, const std::string& objectName, int arg) {}
    // slot 44, exe:00441fa0, dll:100228f0. May the player right-click back to the saved page now? Mask 1. Default 1.
    virtual bool onEvent44() { return true; }
    // slot 45: callback of API 0x6a (EnumPageHotspots), not a table entry the DLL fills; unused by Biosys.
    virtual void onEnumPageHotspots(u32 nHotspots, const void* hotspotStates, u32 n2, const void* states2, int windowId,
                                    const std::string& pageName) {}

    // --- media callbacks (interface.md section 4.1; passed by the DLL as C pointers in the parameter block) ---
    // dll:10007600 SoundCallback. Sound: 1 created, 2 played, 3 stopped/ended, 4 destroyed.
    virtual void onSoundEvent(int handle, int code) {}
    // dll:100077c0 SpriteAnimationCallback. Sprite: 1 created, 2 play, 3 stopped/finished, 4 freed, 5 per frame
    // (window -4 only; arg = frame).
    virtual void onSpriteEvent(int handle, int code, int arg) {}
    // dll OnMovieEvent. Movie: 2 played, 3 stopped/finished, 4 destroyed, 5 per presented frame (arg = frame index).
    virtual void onMovieEvent(int handle, int code, int arg) {}
};

// Allows everything, knows no fruit.
class NullGameLogic : public GameLogic {};

// Plays one object animation record (spec 1.10, codes 1..7) and returns when it has finished. While it
// plays the engine renders without the held object if hideHeld is set. Engine implements this with its
// sprite system (the default player); returns false if the animation could not be opened (a record with
// an empty file name counts as success: nothing to play).
class BlockingAnimPlayer {
public:
    virtual ~BlockingAnimPlayer() = default;
    virtual bool play(const ObjectAnim& rec, bool hideHeld, bool priority) { return true; }
};

} // namespace biosys
