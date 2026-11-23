// ControlProgram: the engine's "Control Program" object (exe:0049b0d0, class ControlProgram__*): sits between
// the engine and the GameLogic and does what the original's 46 wrapper methods (exe:004408d0..00441fa0) do
// around every call into the DLL:
//   * sets the API restriction mask for the duration of the event and restores the old one afterwards
//     (a 64-deep stack, obj+0xcc / +0xd8 / +0x1d8); EngineApi commands refused by the mask return error 10;
//   * keeps the OnTimer state (enabled, last tick, period; API 1) and the "startup / shutdown sent" flags
//     that gate OnIdle;
//   * supplies the default return value when no handler exists.
// It is itself a GameLogic, so subsystems can be given a ControlProgram* in place of the real logic.
// No wall clock: the engine tells it the time (setClock).
#pragma once
#include "engine/gamelogic.h"
#include <array>

namespace biosys {

// Restriction mask bits (exe:0043da90 top, interface.md 3.2).
enum ApiMask : unsigned {
    kMaskAll = 1,           // every command returns 10
    kMaskPageContent = 2,   // 3 4 5 6 7 0a 0b 11 28 35 3a 3b 53 56 5e 6a 6b 6e return 10
    kMaskNoJumpEtc = 4,     // 5 6 7 28 35 53 60 return 10
    kMaskNoJump = 8,        // 5 35 53 return 10
};

// True if `mask` makes API command `code` return 10 (the table above).
bool apiMaskRefuses(unsigned mask, int code);

class ControlProgram : public GameLogic {
public:
    explicit ControlProgram(GameLogic* target = nullptr) : target_(target) {}
    void setTarget(GameLogic* t) { target_ = t; }
    GameLogic* target() const { return target_; }

    void setClock(u32 nowMs) { now_ = nowMs; }
    unsigned mask() const { return mask_; }
    int depth() const { return depth_; }                      // events currently being handled
    bool startupSent() const { return startupSent_; }
    bool shutdownSent() const { return shutdownSent_; }

    // API 1: seconds 0 stops OnTimer, else the period becomes seconds * 1000 and the timer is started.
    void setTimerInterval(int seconds);
    bool timerEnabled() const { return timerEnabled_; }
    u32 timerPeriod() const { return timerPeriod_; }

    // The GameLogic interface: every method forwards to the target inside the mask scope of its slot.
    int onStartup() override;
    void onShutdown() override;
    void onEnterPage(const std::string& pageName) override;
    void onExitPage() override;
    void onTimer() override;
    int onHotspot(const std::string& hotspot, int windowId, const std::string& pageName) override;
    int onPageTimer(const std::string& timer, int windowId, const std::string& pageName) override;
    void onPageLoaded(const std::string& pageName, int windowId) override;
    void onPageDrawn(const std::string& pageName, int windowId) override;
    void onPopupOpened(const std::string& pageName, int popupIndex) override;
    void onPopupClosed(int popupIndex) override;
    void onGameObject(const std::string& objectName, int eventCode, const std::string& extra) override;
    void onActivateApp(bool active) override;
    void onPaletteChanged() override;
    void onKeyDown(int vkey, int repeatCount, int flags, int& doDefault) override;
    void onKeyUp(int vkey, int repeatCount, int flags, int& doDefault) override;
    void onEvent16(int unk0, int unk1) override;
    int onIdle(int idleCount) override;
    void onBS4Screen(int leaving) override;
    bool onBS4Query(int queryId, void* buffer) override;
    void onAppActiveChanged(bool active) override;
    void onSystemPaletteChanged() override;
    void onCompassHeading(int sector) override;
    bool onSaveGame(const std::string& fileName) override;
    bool onLoadGame(const std::string& fileName, long offset) override;
    bool onGetCrossFade(const std::string& srcPage, const std::string& destPage, u32& out1, int& out2, u32& out3,
                        std::string& outCrossFade) override;
    bool onGetCrossFadeEx(const std::string& defName, const std::string& destPage, CrossFadeSpec& spec) override;
    bool onSetFeatureParameter(const std::string& featureName, int paramId, int a, int b, int c) override;
    bool onGetFeatureParameter(const std::string& featureName, int paramId, u32& outInt, double& outValue) override;
    void onTextInputDone(bool cancelled, const std::string& text) override;
    bool onGetNameOfFruitObjectFromPageImageName(const std::string& pageImageName, std::string& outObjectName) override;
    int onRestart() override;
    void onMenuCommand(int code, int arg) override;
    void onMenuQuerySavedGame(int slot, int& outExists, std::string& outDescription, int unk) override;
    void onHotspotHover(const std::string& hotspot, int windowId, bool entered) override;
    void onSimStatusHotspot(const std::string& hotspot) override;
    bool onEvent35(const std::string& savedPageName) override;
    void onReturnToSavedPage(const std::string& pageName) override;
    void onEvent37() override;
    void onEvent38() override;
    void onRightClick() override;
    void onMouseDelta(int& dx, int& dy) override;
    void onEvent41() override;
    bool onBlitPage(u8* dst, u8* dstEnd, void* buf, int bufLen, u8 a, u8 b, u8 c) override;
    void onStatusBarObject(int objectIndex, const std::string& objectName, int arg) override;
    bool onEvent44() override;
    void onEnumPageHotspots(u32 nHotspots, const void* hotspotStates, u32 n2, const void* states2, int windowId,
                            const std::string& pageName) override;
    void onSoundEvent(int handle, int code) override;
    void onSpriteEvent(int handle, int code, int arg) override;
    void onMovieEvent(int handle, int code, int arg) override;

private:
    struct Scope {
        ControlProgram& c;
        Scope(ControlProgram& cp, unsigned m) : c(cp) { c.push(m); }
        ~Scope() { c.pop(); }
    };
    void push(unsigned m);
    void pop();
    GameLogic& t() { return target_ ? *target_ : null_; }

    GameLogic* target_;
    NullGameLogic null_;
    u32 now_ = 0, timerLast_ = 0, timerPeriod_ = 5000;
    bool timerEnabled_ = false, startupSent_ = false, shutdownSent_ = false;
    unsigned mask_ = 0;
    std::array<unsigned, 64> stack_{};
    int depth_ = 0;
    int objectDepth_ = 0;       // OnGameObject nesting (DAT_0049a388), events at depth 5 are dropped
};

} // namespace biosys
