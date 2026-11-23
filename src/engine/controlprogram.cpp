#include "engine/controlprogram.h"

namespace biosys {

int vkOfKey(Key k) {
    switch (k) {
    case Key::S: return kVkS;
    case Key::D: return kVkD;
    case Key::Space: return kVkSpace;
    case Key::Shift: return kVkShift;
    case Key::Ctrl: return kVkCtrl;
    default: return 0;
    }
}

// exe:0043da90: `if (mask & bit) return 10` per case.
bool apiMaskRefuses(unsigned mask, int code) {
    if (mask & kMaskAll) return true;
    auto in = [code](std::initializer_list<int> l) {
        for (int c : l) if (c == code) return true;
        return false;
    };
    if ((mask & kMaskPageContent) &&
        in({0x03, 0x04, 0x05, 0x06, 0x07, 0x0a, 0x0b, 0x11, 0x28, 0x35, 0x3a, 0x3b, 0x53, 0x56, 0x5e, 0x6a, 0x6b, 0x6e}))
        return true;
    if ((mask & kMaskNoJumpEtc) && in({0x05, 0x06, 0x07, 0x28, 0x35, 0x53, 0x60})) return true;
    if ((mask & kMaskNoJump) && in({0x05, 0x35, 0x53})) return true;
    return false;
}

// Wrapper prologue: obj+0x1d8 != 0x40 -> push the old mask; epilogue pops.
void ControlProgram::push(unsigned m) {
    if (depth_ != 64) stack_[static_cast<size_t>(depth_++)] = mask_;
    mask_ = m;
}

void ControlProgram::pop() {
    if (depth_ != 0) mask_ = stack_[static_cast<size_t>(--depth_)];
}

void ControlProgram::setTimerInterval(int seconds) {
    if (seconds == 0) {
        timerEnabled_ = false;
        return;
    }
    timerPeriod_ = static_cast<u32>(seconds) * 1000u;
    if (!timerEnabled_) {
        timerLast_ = now_;
        timerEnabled_ = true;
    }
}

int ControlProgram::onStartup() {
    timerPeriod_ = 5000;
    timerLast_ = now_;
    timerEnabled_ = true;
    startupSent_ = true;
    Scope s(*this, 0);
    return t().onStartup();
}

void ControlProgram::onShutdown() {
    shutdownSent_ = true;
    Scope s(*this, 1);
    t().onShutdown();
}

void ControlProgram::onEnterPage(const std::string& pageName) {
    Scope s(*this, 0);
    t().onEnterPage(pageName);
}

void ControlProgram::onExitPage() {
    Scope s(*this, kMaskPageContent | kMaskNoJumpEtc);
    t().onExitPage();
}

// exe:00440b90: fires when period <= now - last.
void ControlProgram::onTimer() {
    if (!timerEnabled_ || static_cast<u32>(now_ - timerLast_) < timerPeriod_) return;
    timerLast_ = now_;
    Scope s(*this, 0);
    t().onTimer();
}

int ControlProgram::onHotspot(const std::string& hotspot, int windowId, const std::string& pageName) {
    Scope s(*this, kMaskNoJump);
    return t().onHotspot(hotspot, windowId, pageName);
}

int ControlProgram::onPageTimer(const std::string& timer, int windowId, const std::string& pageName) {
    Scope s(*this, kMaskNoJump);
    return t().onPageTimer(timer, windowId, pageName);
}

void ControlProgram::onPageLoaded(const std::string& pageName, int windowId) {
    if (windowId == -5 || windowId == -6) return;
    Scope s(*this, kMaskNoJumpEtc);
    t().onPageLoaded(pageName, windowId);
}

void ControlProgram::onPageDrawn(const std::string& pageName, int windowId) {
    if (windowId == -5 || windowId == -6) return;
    Scope s(*this, kMaskNoJumpEtc);
    t().onPageDrawn(pageName, windowId);
}

void ControlProgram::onPopupOpened(const std::string& pageName, int popupIndex) {
    Scope s(*this, 0);
    t().onPopupOpened(pageName, popupIndex);
}

void ControlProgram::onPopupClosed(int popupIndex) {
    Scope s(*this, kMaskPageContent | kMaskNoJumpEtc);
    t().onPopupClosed(popupIndex);
}

// exe:00440f50: at most 5 nested events; `extra` only for codes 6 and 7.
void ControlProgram::onGameObject(const std::string& objectName, int eventCode, const std::string& extra) {
    if (objectDepth_ == 5) return;
    objectDepth_++;
    {
        Scope s(*this, 0);
        t().onGameObject(objectName, eventCode, (eventCode == 6 || eventCode == 7) ? extra : std::string());
    }
    objectDepth_--;
}

void ControlProgram::onActivateApp(bool active) {
    Scope s(*this, 0);
    t().onActivateApp(active);
}

void ControlProgram::onPaletteChanged() {
    Scope s(*this, 0);
    t().onPaletteChanged();
}

void ControlProgram::onKeyDown(int vkey, int repeatCount, int flags, int& doDefault) {
    doDefault = 1;
    Scope s(*this, 0);
    t().onKeyDown(vkey, repeatCount, flags, doDefault);
}

void ControlProgram::onKeyUp(int vkey, int repeatCount, int flags, int& doDefault) {
    doDefault = 1;
    Scope s(*this, 0);
    t().onKeyUp(vkey, repeatCount, flags, doDefault);
}

void ControlProgram::onEvent16(int unk0, int unk1) {
    Scope s(*this, 0);
    t().onEvent16(unk0, unk1);
}

// exe:004412c0: 0 until the startup event was sent, and after the shutdown event.
int ControlProgram::onIdle(int idleCount) {
    if (!startupSent_ || shutdownSent_) return 0;
    Scope s(*this, 0);
    return t().onIdle(idleCount);
}

void ControlProgram::onBS4Screen(int leaving) {
    Scope s(*this, 1);
    t().onBS4Screen(leaving);
}

bool ControlProgram::onBS4Query(int queryId, void* buffer) {
    Scope s(*this, 0);
    return t().onBS4Query(queryId, buffer);
}

void ControlProgram::onAppActiveChanged(bool active) {
    Scope s(*this, 0);
    t().onAppActiveChanged(active);
}

void ControlProgram::onSystemPaletteChanged() {
    Scope s(*this, 0);
    t().onSystemPaletteChanged();
}

void ControlProgram::onCompassHeading(int sector) {
    Scope s(*this, 0);
    t().onCompassHeading(sector);
}

// Save and blit run with the caller's mask (no push).
bool ControlProgram::onSaveGame(const std::string& fileName) { return t().onSaveGame(fileName); }

bool ControlProgram::onLoadGame(const std::string& fileName, long offset) {
    Scope s(*this, 1);
    return t().onLoadGame(fileName, offset);
}

bool ControlProgram::onGetCrossFade(const std::string& srcPage, const std::string& destPage, u32& out1, int& out2, u32& out3,
                                    std::string& outCrossFade) {
    Scope s(*this, kMaskNoJumpEtc);
    return t().onGetCrossFade(srcPage, destPage, out1, out2, out3, outCrossFade);
}

bool ControlProgram::onGetCrossFadeEx(const std::string& defName, const std::string& destPage, CrossFadeSpec& spec) {
    Scope s(*this, kMaskNoJumpEtc);
    return t().onGetCrossFadeEx(defName, destPage, spec);
}

bool ControlProgram::onSetFeatureParameter(const std::string& featureName, int paramId, int a, int b, int c) {
    Scope s(*this, 0);
    return t().onSetFeatureParameter(featureName, paramId, a, b, c);
}

bool ControlProgram::onGetFeatureParameter(const std::string& featureName, int paramId, u32& outInt, double& outValue) {
    Scope s(*this, 0);
    return t().onGetFeatureParameter(featureName, paramId, outInt, outValue);
}

void ControlProgram::onTextInputDone(bool cancelled, const std::string& text) {
    Scope s(*this, 0);
    t().onTextInputDone(cancelled, text);
}

bool ControlProgram::onGetNameOfFruitObjectFromPageImageName(const std::string& pageImageName, std::string& outObjectName) {
    Scope s(*this, 1);
    return t().onGetNameOfFruitObjectFromPageImageName(pageImageName, outObjectName);
}

// exe:00441940: the timer restarts with the default period.
int ControlProgram::onRestart() {
    timerPeriod_ = 5000;
    timerEnabled_ = true;
    timerLast_ = now_;
    Scope s(*this, 0);
    return t().onRestart();
}

void ControlProgram::onMenuCommand(int code, int arg) {
    Scope s(*this, 0);
    t().onMenuCommand(code, arg);
}

void ControlProgram::onMenuQuerySavedGame(int slot, int& outExists, std::string& outDescription, int unk) {
    Scope s(*this, 0);
    t().onMenuQuerySavedGame(slot, outExists, outDescription, unk);
}

void ControlProgram::onHotspotHover(const std::string& hotspot, int windowId, bool entered) {
    Scope s(*this, kMaskNoJump);
    t().onHotspotHover(hotspot, windowId, entered);
}

void ControlProgram::onSimStatusHotspot(const std::string& hotspot) {
    Scope s(*this, 0);
    t().onSimStatusHotspot(hotspot);
}

bool ControlProgram::onEvent35(const std::string& savedPageName) {
    Scope s(*this, 1);
    return t().onEvent35(savedPageName);
}

void ControlProgram::onReturnToSavedPage(const std::string& pageName) {
    Scope s(*this, kMaskNoJumpEtc);
    t().onReturnToSavedPage(pageName);
}

void ControlProgram::onEvent37() {
    Scope s(*this, 1);
    t().onEvent37();
}

void ControlProgram::onEvent38() {
    Scope s(*this, 1);
    t().onEvent38();
}

void ControlProgram::onRightClick() {
    Scope s(*this, 0);
    t().onRightClick();
}

void ControlProgram::onMouseDelta(int& dx, int& dy) {
    Scope s(*this, 0);
    t().onMouseDelta(dx, dy);
}

void ControlProgram::onEvent41() { t().onEvent41(); }

bool ControlProgram::onBlitPage(u8* dst, u8* dstEnd, void* buf, int bufLen, u8 a, u8 b, u8 c) {
    return t().onBlitPage(dst, dstEnd, buf, bufLen, a, b, c);
}

void ControlProgram::onStatusBarObject(int objectIndex, const std::string& objectName, int arg) {
    Scope s(*this, kMaskNoJumpEtc);
    t().onStatusBarObject(objectIndex, objectName, arg);
}

bool ControlProgram::onEvent44() {
    Scope s(*this, 1);
    return t().onEvent44();
}

void ControlProgram::onEnumPageHotspots(u32 nHotspots, const void* hotspotStates, u32 n2, const void* states2, int windowId,
                                        const std::string& pageName) {
    t().onEnumPageHotspots(nHotspots, hotspotStates, n2, states2, windowId, pageName);
}

void ControlProgram::onSoundEvent(int handle, int code) { t().onSoundEvent(handle, code); }
void ControlProgram::onSpriteEvent(int handle, int code, int arg) { t().onSpriteEvent(handle, code, arg); }
void ControlProgram::onMovieEvent(int handle, int code, int arg) { t().onMovieEvent(handle, code, arg); }

} // namespace biosys
