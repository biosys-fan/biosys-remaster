// The engine's implementation of EngineApi: one adapter class (Engine::ApiImpl) that routes every one of the
// 110 commands to the subsystems that already support it, with the original's parameter semantics and error
// codes (interface.md 3, engine_runtime.md 1.12 / 2.6 / 3.1 / 4.3), and answers the others with
// kApiNotImplemented. Every command first goes through gate(): it is counted and refused with error 10 when
// the restriction mask of the running event forbids it (exe:0043da90 top, see controlprogram.h).
#include "engine/engine.h"
#include <algorithm>
#include <cstdio>

namespace biosys {

namespace {

std::vector<ApiCommandInfo>& registry() {
    static std::vector<ApiCommandInfo> r;
    return r;
}

bool registerCommand(int code, const char* name, bool implemented, const char* category) {
    registry().push_back({code, name, implemented, category});
    return true;
}

// GetTransitionFromName (exe:00451e30)
bool knownTransition(const std::string& n) {
    static const char* names[] = {"none", "wipeleft", "wiperight", "wipeup", "wipedown", "pushwipeleft",
                                  "pushwiperight", "pushwipeup", "pushwipedown", "zoomtohotspot"};
    for (const char* t : names)
        if (iequals(n, t)) return true;
    return false;
}

} // namespace

const std::vector<ApiCommandInfo>& apiCommandTable() {
    static const std::vector<ApiCommandInfo> sorted = [] {
        std::vector<ApiCommandInfo> v = registry();
        std::sort(v.begin(), v.end(), [](const ApiCommandInfo& a, const ApiCommandInfo& b) { return a.code < b.code; });
        return v;
    }();
    return sorted;
}

#define API_REG(code, Name, impl, cat) static inline const bool reg_##Name = registerCommand(code, #Name, impl, cat)
#define STUB(code, Name, params, cat) \
    int Name params override { return stub(code, #Name); } \
    API_REG(code, Name, false, cat);
#define IMPL(code, Name, params) \
    API_REG(code, Name, true, ""); \
    int Name params override
#define GATE(code) do { if (int gateErr_ = gate(code)) return gateErr_; } while (0)

struct Engine::ApiImpl : EngineApi {
    Engine& e;
    std::array<bool, 0x70> warned{};

    explicit ApiImpl(Engine& eng) : e(eng) {}

    int gate(int code) {
        ++e.apiCalls_[static_cast<size_t>(code)];
        if (e.menu_) {      // exe:0043da90 top: while the menu is up only these commands are accepted
            switch (code) {
            case 0x02: case 0x09: case 0x34: case 0x35: case 0x53: case 0x55: case 0x59: case 0x6a: break;
            default: return kApiMenuOpen;
            }
        }
        if (e.bs4Suspended_) {   // exe:0043da90 top: DAT_004990ec == 1 (the terminal is up): only these commands are accepted
            switch (code) {
            case 0x02: case 0x09: case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x19:
            case 0x34: case 0x35: case 0x44: case 0x53: case 0x54: case 0x59: break;
            default: return kApiModalBlocked;
            }
        }
        return apiMaskRefuses(e.cp_.mask(), code) ? kApiNotAllowed : kApiOk;
    }
    int stub(int code, const char* name) {
        if (int err = gate(code)) return err;
        ++e.apiNotImpl_[static_cast<size_t>(code)];
        if (!warned[static_cast<size_t>(code)]) {
            warned[static_cast<size_t>(code)] = true;
            std::fprintf(stderr, "api not implemented: 0x%02x %s\n", code, name);
        }
        return kApiNotImplemented;
    }
    bool inactive() const { return e.mediaPaused_; }   // DAT_0049a06c == 0 || IsIconic

    // ---- helpers ----
    static int findHotspot(const Page& p, const std::string& name) {
        for (size_t i = 0; i < p.hotspots.size(); i++)
            if (iequals(p.hotspots[i].name, name)) return static_cast<int>(i);
        return -1;
    }
    static int findTimer(const Page& p, const std::string& name) {
        for (size_t i = 0; i < p.timers.size(); i++)
            if (iequals(p.timers[i].name, name)) return static_cast<int>(i);
        return -1;
    }
    int openSprite(const std::string& file, int window, int x, int y, int playMode, int frameDelayMs, bool notify, bool loop,
                   bool keepPainted, const std::string& wav, int maxPlayMs, int soundMode, bool pageRelative, int visibleWidth,
                   bool eraseOnStop, int& handle) {
        if (inactive()) return kApiAppInactive;
        SpriteOpenParams p;
        p.file = file; p.window = window; p.x = x; p.y = y; p.mode = playMode; p.delayMs = frameDelayMs;
        p.notify = notify; p.loop = loop; p.keepPainted = keepPainted; p.wav = wav; p.maxPlayMs = maxPlayMs;
        p.soundMode = soundMode; p.pageRelative = pageRelative; p.visibleWidth = visibleWidth; p.eraseOnStop = eraseOnStop;
        return e.sprites_.open(p, handle);
    }

    // ---- 0x01 .. 0x0f: timers, hotspots, jumps, log, object queries ----
    IMPL(0x01, SetTimerInterval, (int seconds)) {
        GATE(0x01);
        e.cp_.setTimerInterval(seconds);
        return kApiOk;
    }
    IMPL(0x02, QuitGame, ()) {
        GATE(0x02);
        e.quit_ = true;          // the front end ends its loop (the original fades to black and posts WM_CLOSE)
        return kApiOk;
    }
    // exe:00402860: a name ending in '*' with wildcard == 1 is a prefix; the match is case-insensitive.
    IMPL(0x03, EnableHotspot, (int window, const std::string& hotspot, bool enable, bool prefixWildcard, int* previousState)) {
        GATE(0x03);
        const Page* p = e.pageOfWindow(window);
        if (!p) return kApiBadWindow;
        bool prefix = false;
        std::string name = hotspot;
        if (prefixWildcard && name.size() > 1 && name.back() == '*') {
            prefix = true;
            name.pop_back();
        }
        if (prefix) {
            e.view_.setHotspotEnabled(*p, name, enable, true, nullptr);
            return kApiOk;
        }
        int prev = 0;
        if (!e.view_.setHotspotEnabled(*p, name, enable, false, &prev)) return kApiHotspotNotFound;
        if (previousState) *previousState = prev;
        return kApiOk;
    }
    IMPL(0x04, EnablePageTimer, (int window, const std::string& timer, bool enable)) {
        GATE(0x04);
        const Page* p = e.pageOfWindow(window);
        if (!p) return kApiBadWindow;
        int i = findTimer(*p, timer);
        if (i < 0) return kApiTimerNotFound;
        if (window == kMainWindow && static_cast<size_t>(i) < e.timers_.size()) e.timers_[static_cast<size_t>(i)].enabled = enable;
        return kApiOk;
    }
    // exe:0043da90 case 5: the checks run in this order; the jump itself is exe:00411350.
    IMPL(0x05, JumpToPage, (const std::string& page, const std::string& transition, bool pushHistory, int x, int y,
                            int bigPageMode, int noFlip)) {
        GATE(0x05);
        if (inactive()) return kApiAppInactive;
        int n = e.view_.findPage(page);
        if (!n) return kApiPageNotFound;
        if (!knownTransition(transition)) return kApiTransitionNotFound;
        try {
            e.view_.goToPage(n, pushHistory);
        } catch (const Error&) {
            return kApiJumpFailed;
        }
        return kApiOk;
    }
    IMPL(0x06, DoHotspotAction, (int window, const std::string& hotspot)) {
        GATE(0x06);
        if (inactive()) return kApiAppInactive;
        const Page* p = e.pageOfWindow(window);
        if (!p) return kApiBadWindow;
        int i = findHotspot(*p, hotspot);
        if (i < 0) return kApiHotspotNotFound;
        return e.runAction(*p, i, p->hotspots[static_cast<size_t>(i)]) == 0 ? kApiOk : kApiActionFailed;
    }
    IMPL(0x07, DoPageTimerAction, (int window, const std::string& timer)) {
        GATE(0x07);
        if (inactive()) return kApiAppInactive;
        const Page* p = e.pageOfWindow(window);
        if (!p) return kApiBadWindow;
        int i = findTimer(*p, timer);
        if (i < 0) return kApiTimerNotFound;
        Hotspot h;
        h.action = p->timers[static_cast<size_t>(i)].action;
        h.name = p->timers[static_cast<size_t>(i)].name;
        return e.runAction(*p, -1, h) == 0 ? kApiOk : kApiActionFailed;
    }
    IMPL(0x08, ShowMessage, (const std::string& text)) {
        GATE(0x08);
        e.message_ = text;       // the original opens a message box
        return kApiOk;
    }
    IMPL(0x09, WriteLog, (const std::string& text)) {
        GATE(0x09);
        e.lastLog_ = text;
        if (e.logSink) e.logSink(text);
        return kApiOk;
    }
    IMPL(0x0c, GetGameObjectCount, (int& count)) {
        GATE(0x0c);
        count = e.objs_.apiObjectCount();
        return kApiOk;
    }
    IMPL(0x0d, GetGameObjectName, (int index, std::string& name)) {
        GATE(0x0d);
        return e.objs_.apiObjectName(index, name);
    }
    IMPL(0x0e, GetGameObjectInfo, (const std::string& name, std::array<int, 12>& info)) {
        GATE(0x0e);
        ObjectInfo oi;
        int rc = e.objs_.apiObjectInfo(name, oi);
        if (rc) return rc;
        info = {oi.moveable, oi.wearable, oi.inZone, oi.fruitInZone, oi.held, oi.onBar, oi.removed, oi.worn,
                oi.weight, oi.usageCount, oi.fruit, oi.zone};
        return kApiOk;
    }
    IMPL(0x0f, GetDropZoneCount, (int& count)) {
        GATE(0x0f);
        count = e.objs_.apiZoneCount();
        return kApiOk;
    }
    IMPL(0x10, GetDropZoneInfo, (int index, std::string& page, int& hotspotIndex, std::string& object)) {
        GATE(0x10);
        ZoneInfo zi;
        int rc = e.objs_.apiZoneInfo(index, zi);
        if (rc) return rc;
        page = zi.page;
        hotspotIndex = zi.hotspotIndex;
        object = zi.object;
        return kApiOk;
    }
    IMPL(0x11, GetHotspotNameByIndex, (int window, int index, std::string& hotspot)) {
        GATE(0x11);
        const Page* p = e.pageOfWindow(window);
        if (!p) return kApiBadWindow;
        if (index < 0 || static_cast<size_t>(index) >= p->hotspots.size()) return kApiHotspotNotFound;
        hotspot = p->hotspots[static_cast<size_t>(index)].name;
        return kApiOk;
    }
    IMPL(0x12, SetGameObjectProperty, (const std::string& object, const std::string& property, int value)) {
        GATE(0x12);
        return e.objs_.apiSetObjectValue(object, property, value);
    }
    IMPL(0x13, NoOp13, (const std::string& text, int number)) {
        GATE(0x13);
        return kApiOk;
    }

    // ---- 0x14 .. 0x19: sounds ----
    IMPL(0x14, OpenSound, (const std::string& file, int loop, bool notify, int frequency, int pan, int volume, int& handle)) {
        GATE(0x14);
        SoundOpenParams p;
        p.file = file; p.loop = loop == 1; p.frequency = frequency; p.pan = pan; p.volume = volume; p.notify = notify;
        p.lock = 1;
        return e.sounds_.open(p, handle);
    }
    IMPL(0x15, PlaySound, (int handle, bool restart)) {
        GATE(0x15);
        return e.sounds_.play(handle, restart);
    }
    IMPL(0x16, StopSound, (int handle)) {
        GATE(0x16);
        return e.sounds_.stop(handle);
    }
    IMPL(0x17, IsSoundPlaying, (int handle, int& playing)) {
        GATE(0x17);
        if (!e.sounds_.valid(handle)) return kApiBadSoundHandle;
        playing = e.sounds_.isPlaying(handle) ? 1 : 0;
        return kApiOk;
    }
    IMPL(0x18, SetSoundProperty, (int handle, const std::string& property, int value)) {
        GATE(0x18);
        return e.sounds_.setProperty(handle, property, value);
    }
    IMPL(0x19, CloseSound, (int handle)) {
        GATE(0x19);
        return e.sounds_.close(handle);
    }
    // exe:00412a00: the 256 entries of the display palette as R,G,B,flags (flags 0).
    IMPL(0x1c, GetPalette, (u8* palette)) {
        GATE(0x1c);
        if (inactive()) return kApiAppInactive;
        if (!palette) return kApiFailure;
        const Palette& pal = e.stashPalette_ ? *e.stashPalette_ : e.view_.palette();   // exe:00412a00 with the exe:00412ab0 stash
        for (size_t i = 0; i < 256; i++) {
            palette[4 * i] = pal.c[i].r; palette[4 * i + 1] = pal.c[i].g; palette[4 * i + 2] = pal.c[i].b; palette[4 * i + 3] = 0;
        }
        return kApiOk;
    }

    // ---- 0x1e .. 0x23: sprite animations ----
    IMPL(0x1e, OpenSpriteAnimationVariant, (const std::string& file, int window, int x, int y, int playMode, int frameDelayMs,
                                            bool notify, bool loop, bool keepPainted, const std::string& wav, int maxPlayMs,
                                            int soundMode, bool pageRelative, int visibleWidth, bool eraseOnStop, int& handle)) {
        GATE(0x1e);
        return openSprite(file, window, x, y, playMode, frameDelayMs, notify, loop, keepPainted, wav, maxPlayMs, soundMode,
                          pageRelative, visibleWidth, eraseOnStop, handle);
    }
    IMPL(0x1f, OpenSpriteAnimation, (const std::string& file, int window, int x, int y, int playMode, int frameDelayMs,
                                     bool notify, bool loop, bool keepPainted, const std::string& wav, int maxPlayMs,
                                     int soundMode, bool pageRelative, int visibleWidth, bool eraseOnStop, int& handle)) {
        GATE(0x1f);
        return openSprite(file, window, x, y, playMode, frameDelayMs, notify, loop, keepPainted, wav, maxPlayMs, soundMode,
                          pageRelative, visibleWidth, eraseOnStop, handle);
    }
    IMPL(0x20, PlaySpriteAnimation, (int handle, bool restart, bool priority)) {
        GATE(0x20);
        return e.sprites_.play(handle, restart, e.now_, priority);
    }
    IMPL(0x21, StopSpriteAnimation, (int handle)) {
        GATE(0x21);
        return e.sprites_.stop(handle);
    }
    IMPL(0x22, IsSpriteAnimationPlaying, (int handle, int& playing)) {
        GATE(0x22);
        if (!e.sprites_.valid(handle)) return kApiBadAnimHandle;
        playing = e.sprites_.isPlaying(handle) ? 1 : 0;
        return kApiOk;
    }
    IMPL(0x23, CloseSpriteAnimation, (int handle)) {
        GATE(0x23);
        return e.sprites_.close(handle);
    }

    // ---- 0x25 .. 0x31: objects, sound pan, redraw, movies, input, panorama ----
    IMPL(0x25, GetDropZoneForHotspot, (const std::string& hotspot, int& zone)) {
        GATE(0x25);
        return e.objs_.apiZoneOfHotspot(hotspot, zone);
    }
    IMPL(0x26, RemoveGameObject, (const std::string& object)) {
        GATE(0x26);
        if (inactive()) return kApiAppInactive;
        return e.objs_.apiRemoveObject(object);
    }
    // exe:0043c6e0: x, or the centre of a hotspot of the current main page.
    IMPL(0x27, SetSoundPanToPagePosition, (int handle, bool enable, int x, const std::string& hotspot)) {
        GATE(0x27);
        if (!e.sounds_.valid(handle)) return kApiBadSoundHandle;
        if (enable && !hotspot.empty()) {
            if (!e.view_.pageNumber()) return kApiHotspotNotFound;
            int i = findHotspot(e.view_.page(), hotspot);
            if (i < 0) return kApiHotspotNotFound;
            const Hotspot& h = e.view_.page().hotspots[static_cast<size_t>(i)];
            x = (h.left + h.right) / 2;
        }
        return e.sounds_.panToPage(handle, enable, x, e.view_.panorama());
    }
    // exe:00411760: TCPage::ReRender (the visible ornaments painted over the surface without clearing it), then the
    // objects drawn again; the frame itself is composed anew on every render.
    IMPL(0x28, RedrawPage, ()) {
        GATE(0x28);
        if (inactive()) return kApiAppInactive;
        if (!e.view_.pageNumber()) return kApiRedrawFailed;
        e.view_.redrawPage();
        e.dropFill();
        return kApiOk;
    }
    IMPL(0x29, OpenAVIMovie, (const std::string& file, int window, int x, int y, bool notify, bool loop, int unk04,
                              const u8* palette, int& handle)) {
        GATE(0x29);
        if (inactive()) return kApiAppInactive;
        MovieOpenParams p;
        p.file = file; p.x = x; p.y = y; p.window = window; p.loop = loop;
        p.audio = e.view_.pageNumber() != 1;       // sound enabled and owning page number != 1 (engine_runtime 3.2)
        if (palette) {
            for (size_t i = 0; i < 256; i++) p.palette.c[i] = Color{palette[4 * i], palette[4 * i + 1], palette[4 * i + 2]};
            p.hasPalette = true;
        }
        return e.movies_.open(p, handle);
    }
    IMPL(0x2a, PlayAVIMovie, (int handle, bool restart)) {
        GATE(0x2a);
        if (inactive()) return kApiAppInactive;
        return e.movies_.play(handle, restart, e.now_);
    }
    IMPL(0x2b, StopAVIMovie, (int handle)) {
        GATE(0x2b);
        return e.movies_.stop(handle);
    }
    IMPL(0x2c, IsAVIMoviePlaying, (int handle, int& playing)) {
        GATE(0x2c);
        if (!e.movies_.valid(handle)) return kApiBadMovieHandle;
        playing = e.movies_.isPlaying(handle) ? 1 : 0;
        return kApiOk;
    }
    IMPL(0x2d, CloseAVIMovie, (int handle)) {
        GATE(0x2d);
        return e.movies_.close(handle);
    }
    IMPL(0x2e, EnableUserInput, (bool enable, int unk08)) {
        GATE(0x2e);
        e.objs_.setInputEnabled(enable);
        return kApiOk;
    }
    // exe:00403850
    IMPL(0x2f, GetPanoramaInfo, (int& pageWidth, int& startX, int& centreX)) {
        GATE(0x2f);
        if (!e.view_.pageNumber()) return kApiNotAllowed;
        const Page& p = e.view_.page();
        if (!e.view_.panorama() || e.view_.fullScreen() || p.pixelWidth() == Frame::W) return kApiNotScrollable;
        int px = 0, py = 0;
        e.view_.viewToPage(Frame::W / 2, e.view_.viewTop(), px, py);
        pageWidth = p.pixelWidth();
        startX = static_cast<int>(p.initialScrollX);
        centreX = px;
        return kApiOk;
    }
    IMPL(0x30, ChangeGameObjectHoldingImagesGroup, (const std::string& object, int group)) {
        GATE(0x30);
        return e.objs_.apiSetHoldingGroup(object, static_cast<u32>(group));
    }
    IMPL(0x31, PlaceGameObject, (const std::string& srcObject, const std::string& extraImage, const std::string& dstObject,
                                 const std::string& imageKind, int holdIndex, int group, int x, int y)) {
        GATE(0x31);
        return e.objs_.apiPlaceImage(srcObject, extraImage, dstObject, imageKind, holdIndex, static_cast<u32>(group), x, y);
    }
    IMPL(0x36, SetSpriteAnimationValue, (int handle, int value)) {
        GATE(0x36);
        return e.sprites_.setVisibleWidth(handle, value);
    }
    IMPL(0x37, SetAVIMovieParam, (int handle, const u8* colourKey)) {
        GATE(0x37);
        if (!e.movies_.valid(handle)) return kApiBadMovieHandle;
        if (!colourKey) return kApiBadParameter;
        return e.movies_.setColorKey(handle, colourKey[0], colourKey[1], colourKey[2]);
    }
    IMPL(0x3d, SetAVIMovieVolume, (int handle, int volume)) {
        GATE(0x3d);
        return e.movies_.setVolume(handle, volume);
    }
    IMPL(0x3f, SetAVIMoviePage, (int handle, const std::string& page)) {
        GATE(0x3f);
        return e.movies_.bindPage(handle, page);
    }
    IMPL(0x41, AviOption41, (int handle)) {
        GATE(0x41);
        return e.movies_.setLineDoubling(handle);
    }
    IMPL(0x42, AviOption42, (int handle)) {
        GATE(0x42);
        return e.movies_.setSaveBackground(handle);
    }
    IMPL(0x43, PrepareSynthAVIMovie, (int handle)) {
        GATE(0x43);
        return e.movies_.setSynth(handle);
    }
    IMPL(0x44, NoOp44, ()) {
        GATE(0x44);
        return kApiOk;
    }
    IMPL(0x45, ReturnGameObjectHidden, (const std::string& object)) {
        GATE(0x45);
        if (inactive()) return kApiAppInactive;
        return e.objs_.apiRestoreFruit(object);
    }
    IMPL(0x46, ChangeGameObjectVariation, (const std::string& object, const std::string& variation, int group)) {
        GATE(0x46);
        return e.objs_.apiChangeVariation(object, variation, static_cast<u32>(group));
    }
    IMPL(0x47, PlayObjectUseAnimation, (const std::string& tanFile, const std::string& wavFile, bool hideHeld,
                                        bool removeHeldAfter, int x, int y, int playMode, int frameDelayMs,
                                        const std::string& variation, int holdGroup)) {
        GATE(0x47);
        if (inactive()) return kApiAppInactive;
        return e.objs_.apiPlayUseAnimation(tanFile, wavFile, x, y, playMode, frameDelayMs, hideHeld, removeHeldAfter, variation,
                                           static_cast<u32>(holdGroup));
    }
    IMPL(0x48, DropHeldObject, (const std::string* hotspot)) {
        GATE(0x48);
        return e.objs_.apiDropHeld(hotspot);
    }
    IMPL(0x49, RedrawScreen, ()) {
        GATE(0x49);
        if (e.view_.pageNumber()) e.dropFill();
        return kApiOk;
    }
    IMPL(0x4f, AviOption4f, (int handle, int restoreOnStop, int restoreOnClose)) {
        GATE(0x4f);
        return e.movies_.setRestore(handle, restoreOnStop != 0, restoreOnClose != 0);
    }
    IMPL(0x52, ReturnGameObject, (const std::string& object)) {
        GATE(0x52);
        if (inactive()) return kApiAppInactive;
        return e.objs_.apiReturnObject(object);
    }
    IMPL(0x57, ChangeGameObjectVariation1, (const std::string& variation, int group)) {
        GATE(0x57);
        return e.objs_.apiChangeVariationDeferred(variation, static_cast<u32>(group));
    }
    IMPL(0x59, NoOp59, (const std::string& unkA8)) {
        GATE(0x59);
        return kApiOk;
    }
    // exe:0043da90 case 0x1b: copies DAT_00498f38 ("<install dir>\BIODATA"). The port hands out the real BIODATA directory;
    // DataFiles resolves names that start with it (the game appends "\fonts\jon.ttf" or a lookup file name to it).
    IMPL(0x1b, GetDataDirectory, (std::string& directory)) {
        GATE(0x1b);
        directory = e.files_.dataDirectory();
        return kApiOk;
    }
    // exe:0043da90 case 0x40: DAT_00499070 = (show == 1); the page is blitted again
    IMPL(0x40, ShowCursor, (bool show)) {
        GATE(0x40);
        e.setCursorShown(show);
        return kApiOk;
    }
    // exe:0043da90 case 0x5a -> exe:00454120: max < min (unsigned) is 0x12, any other refusal 0x11
    IMPL(0x5a, SetPanoramaScrollLimits, (int minX, int maxX, int unk04)) {
        GATE(0x5a);
        if (static_cast<u32>(maxX) < static_cast<u32>(minX)) return kApiBadParameter;
        return e.setPanoramaScrollLimits(static_cast<u32>(minX), static_cast<u32>(maxX), unk04) ? kApiOk : kApiFailure;
    }
    // exe:0043da90 case 0x5d -> exe:00445810
    IMPL(0x5d, MoveMainWindowView, (int x, int y)) {
        GATE(0x5d);
        return e.moveMainWindowView(x, y) ? kApiOk : kApiFailure;
    }
    // exe:0043da90 case 0x66: DAT_004979e0 = (enable != 0), then the cursor is refreshed
    IMPL(0x66, SetCursorOverride, (bool enable)) {
        GATE(0x66);
        e.setCursorOverride(enable);
        return kApiOk;
    }
    // exe:0043da90 case 0x68: rect is the RECT (left, top, right, bottom); a null rect = (-1, -1, -1, -1) = no clip
    IMPL(0x68, SetCursorClipRect, (bool manual, const void* rect)) {
        GATE(0x68);
        e.setCursorClip(manual, static_cast<const int*>(rect));
        return kApiOk;
    }
    // exe:0043da90 case 0x6c
    IMPL(0x6c, GetScrollState, (int& active, int& unk08, int& unkAc, int& unk14)) {
        GATE(0x6c);
        active = 0;
        Engine::ScrollState s = e.scrollState();
        active = s.active;
        if (s.active) {
            unk08 = s.mouseDeltaEvent;
            unkAc = s.released;
            if (s.released) unk14 = s.top;
        }
        return kApiOk;
    }
    // exe:0043da90 case 0x6d
    IMPL(0x6d, SetScrollOptions, (int unk04, int unk08)) {
        GATE(0x6d);
        e.setScrollOptions(unk04, unk08);
        return kApiOk;
    }
    IMPL(0x5c, EnableMouseDeltaEvent, (bool enable)) {
        GATE(0x5c);
        e.mouseDeltaEvent_ = enable;
        return kApiOk;
    }
    IMPL(0x5f, SetScrollMode, (bool enable)) {
        GATE(0x5f);
        e.setScrollMode(enable);
        return kApiOk;
    }
    // exe:00450bd0: the page must be a popup page.
    IMPL(0x60, DisplayPopupPage, (const std::string& page)) {
        GATE(0x60);
        int n = e.view_.findPage(page);
        if (!n) return kApiPageNotFound;
        try {
            return e.view_.displayPopupPage(n) ? kApiOk : kApiFailure;   // exe:00450bd0, the same routine as verb 9
        } catch (const Error&) {
            return kApiFailure;
        }
    }
    IMPL(0x61, ClosePopupWindow, (int window)) {
        GATE(0x61);
        if (window < 0 || window > 19) return kApiBadWindow;
        if (window != kPopupWindow || !e.view_.popupOpen()) return kApiPopupNotOpen;
        e.view_.closePopup();
        return kApiOk;
    }
    IMPL(0x62, Disabled62, ()) {
        GATE(0x62);
        return kApiNotAllowed;
    }
    IMPL(0x63, Disabled63, ()) {
        GATE(0x63);
        return kApiNotAllowed;
    }
    IMPL(0x64, GetSpriteAnimationSound, (int handle, int& soundHandle)) {
        GATE(0x64);
        if (!e.sprites_.valid(handle)) return kApiBadAnimHandle;
        soundHandle = e.sprites_.soundHandle(handle);
        return kApiOk;
    }
    IMPL(0x65, GetStatusBarObjectUnderCursor, (int& overBar, std::string& object)) {
        GATE(0x65);
        std::string n;
        overBar = e.statusBarObjectUnderCursor(n) ? 1 : 0;
        object = n;
        return kApiOk;
    }
    IMPL(0x69, GetReturnPageName, (std::string& page)) {
        GATE(0x69);
        page = e.view_.destinationPageName();   // DAT_00499bf8: set by the jump routine before OnExitPage (exe:00411350 / exe:00445f80)
        return kApiOk;
    }

    // ---- palettes and fades ----
    // exe:0043da90 case 0x1a: duration 0 = immediately, else steps must be nonzero (0x12); the work is exe:00412630.
    IMPL(0x1a, SetPalette, (const u8* palette, int steps, int duration)) {
        GATE(0x1a);
        if (inactive()) return kApiAppInactive;
        if (!(duration == 0 || steps != 0)) return kApiBadParameter;
        if (!palette) return kApiFailure;
        Palette p;
        for (size_t i = 0; i < 256; i++) p.c[i] = Color{palette[4 * i], palette[4 * i + 1], palette[4 * i + 2]};
        return e.apiSetPalette(static_cast<u32>(steps), duration, p) ? kApiOk : kApiFailure;
    }
    IMPL(0x1d, FadeToColour, (int rgb, int unk04, int speed, int unk1c, int percent)) {
        GATE(0x1d);
        if (inactive()) return kApiAppInactive;
        return e.apiFadeToColour(rgb & 0xff, (rgb >> 8) & 0xff, (rgb >> 16) & 0xff, unk04, speed, unk1c, percent) ? kApiOk : kApiFailure;
    }
    IMPL(0x24, FadeToPalette, (const u8* palette, int unk04, int speed, int percent)) {
        GATE(0x24);
        if (inactive()) return kApiAppInactive;
        if (!palette) return kApiFailure;
        Palette p;
        for (size_t i = 0; i < 256; i++) p.c[i] = Color{palette[4 * i], palette[4 * i + 1], palette[4 * i + 2]};
        return e.apiFadeToPalette(p, unk04, speed, percent) ? kApiOk : kApiFailure;
    }

    // ---- page images, fills, jiggle, page filter ----
    // exe:0043da90 case 0x0a -> TCPage::DisplayImage exe:00402e00. block: +04 redraw flag, +08 "keyed restore" flag
    // (slot restore only puts back the pixels the image changed), +20 width limit (-1 = all), +c0 out slot number.
    IMPL(0x0a, DisplayImage, (int window, const std::string& image, int x, int y, int unk04, int unk08, int unk20, int* unkOut)) {
        GATE(0x0a);
        e.view_.setGameLogicAttached(true, false);
        const int pos[2] = {x, y};
        int slot = -1;
        int rc = e.view_.displayImage(window, image, (x == -1 && y == -1) ? nullptr : pos, static_cast<u32>(unk20), unkOut != nullptr,
                                      &slot, unk08 == 1);
        if (rc != 0) return rc;
        if (unkOut) *unkOut = slot;
        if (unk04 != 0 && !inactive()) e.dropFill();
        return kApiOk;
    }
    IMPL(0x0b, SetImageVisibility, (int window, const std::string& image, bool visible)) {
        GATE(0x0b);
        e.view_.setGameLogicAttached(true, false);
        return e.view_.setImageVisibility(window, image, visible);
    }
    IMPL(0x5e, ReleasePageImageSlot, (int window, int unk04, int slot)) {
        GATE(0x5e);
        int rc = e.view_.releaseImageSlot(window, slot);
        if (rc == 0) e.dropFill();
        return rc;
    }
    IMPL(0x6e, RaisePageImage, (int window, const std::string& image)) {
        GATE(0x6e);
        e.view_.setGameLogicAttached(true, false);
        return e.view_.raiseImage(window, image);
    }
    // exe:004138b0: window rectangle on the screen filled with palette index `colour` (not on the page surface).
    // An inactive app returns 0 (the API answers OK), an unknown window 0x11.
    IMPL(0x3c, ClearWindow, (int window, int colour)) {
        GATE(0x3c);
        if (inactive()) return kApiOk;
        Rect r;
        const int top = e.view_.viewTop();
        if (window == kMainWindow) r = {0, top, Frame::W, e.view_.fullScreen() ? Frame::H : PageView::ViewH};
        else if (window == -3) r = {0, StatusBar::ScreenY, Frame::W, StatusBar::Height};
        else if (window == kTopBarWindow && e.view_.topbarPage()) r = {0, 0, Frame::W, PageView::TopBarH};
        else if (window == kPopupWindow && e.view_.popupPage())
            r = {0, top, e.view_.popupPage()->pixelWidth(), e.view_.popupPage()->pixelHeight()};
        else return kApiFailure;
        e.fillScreen(r.x, r.y, r.w, r.h, static_cast<u8>(colour));
        return kApiOk;
    }
    // exe:00411cb0: the whole back buffer
    IMPL(0x67, FillScreen, (int colour)) {
        GATE(0x67);
        e.fillScreen(0, 0, Frame::W, Frame::H, static_cast<u8>(colour));
        return kApiOk;
    }
    // exe:004139f0: records of 4 ints {dx, dy, stepX, stepY}
    IMPL(0x3e, JigglePage, (const void* records, int count, int unk14, int first, int last)) {
        GATE(0x3e);
        if (inactive()) return kApiOk;
        return e.apiJiggle(static_cast<const i32*>(records), count, unk14, static_cast<u32>(first), static_cast<u32>(last));
    }
    // exe:0043da90 cases 0x32 / 0x33 / 0x5b: OnBlitPage (event 42) is called on the view after every page blit.
    IMPL(0x32, BeginPageFilter, (int a, int b, int c)) {
        GATE(0x32);
        e.filter_ = Filter();
        e.filter_.active = true;
        e.filter_.a = static_cast<u8>(a);
        e.filter_.b = static_cast<u8>(b);
        e.filter_.c = static_cast<u8>(c);
        return kApiOk;
    }
    IMPL(0x33, EndPageFilter, ()) {
        GATE(0x33);
        if (!e.filter_.active) return kApiOk;
        e.filter_.active = false;
        if (!inactive()) e.dropFill();
        return kApiOk;
    }
    IMPL(0x5b, SetPageFilterBuffer, (void* buffer, int length)) {
        GATE(0x5b);
        if (!e.filter_.active) return kApiFailure;
        if (length == 0) {
            e.filter_.buf = nullptr;
            e.filter_.len = 0;
            return kApiOk;
        }
        if (!buffer) return kApiBadParameter;
        e.filter_.buf = buffer;
        e.filter_.len = length;
        return kApiOk;
    }

    // ---- GDI text and lines: fonts and device contexts (canvas.h, font.h; analysis/notes/gdi_usage.md) ----
    // exe:0043da90 case 0x38: AddFontResourceA / RemoveFontResourceA of the path the DLL built (data directory + "\\fonts\\" + name);
    // 0x11 if the call fails. The port resolves the path through DataFiles and keeps the font in the engine's FontLibrary.
    IMPL(0x38, AddRemoveFontResource, (const std::string& file, bool add)) {
        GATE(0x38);
        std::string real = e.files_.findData(file);
        if (real.empty()) real = file;
        return (add ? e.fonts_.addFile(real) : e.fonts_.removeFile(real)) ? kApiOk : kApiFailure;
    }
    // case 0x39: CreateFontIndirectA(+c0) -> +18, or DeleteObject(+18); 0x11 on failure.
    IMPL(0x39, CreateDeleteLogicalFont, (bool create, const void* logFont, int& font)) {
        GATE(0x39);
        if (!create) return e.fonts_.deleteFont(font) ? kApiOk : kApiFailure;
        if (!logFont) return kApiFailure;
        font = e.fonts_.createFont(LogFont::fromWin32(logFont));
        return kApiOk;
    }
    // case 0x3a, exe:00413770: the window's page (TCPage::GetSize -> +20 width, +14 height) and the DC of its page surface
    // (IDirectDrawSurface::GetDC, which fails while a DC is out); any failure is 0x11. Window -3 (status bar) has no page.
    IMPL(0x3a, GetWindowDC, (int window, int& dc, int& size20, int& size14)) {
        GATE(0x3a);
        const Page* page = e.pageOfWindow(window);
        if (!page) return kApiFailure;
        for (const Engine::OpenDc& d : e.dcs_)
            if (d.window == window) return kApiFailure;
        Engine::OpenDc d;
        Surface* surface = nullptr;
        std::vector<u8>* mask = nullptr;
        if (!e.view_.drawTarget(window, surface, mask, d.owner)) return kApiFailure;
        Engine* eng = &e;
        d.window = window;
        d.canvas = std::make_unique<Canvas>(*surface, [eng]() -> const Palette& { return eng->view_.palette(); }, &e.fonts_, mask);
        d.handle = CanvasRegistry::add(d.canvas.get());
        dc = d.handle;
        size20 = page->pixelWidth();
        size14 = page->pixelHeight();
        e.dcs_.push_back(std::move(d));
        return kApiOk;
    }
    // case 0x3b, exe:004137e0: IDirectDrawSurface::ReleaseDC, then (window != 0) the page is blitted to the back buffer again,
    // which also replaces a ClearWindow fill; the flip flags (+04, +08) have no counterpart here. 0x11 on failure.
    IMPL(0x3b, ReleaseWindowDC, (int window, int dc, int unk04, int unk08)) {
        GATE(0x3b);
        if (!e.pageOfWindow(window)) return kApiFailure;
        for (size_t i = 0; i < e.dcs_.size(); i++)
            if (e.dcs_[i].window == window && e.dcs_[i].handle == dc) {
                CanvasRegistry::remove(dc);
                e.dcs_.erase(e.dcs_.begin() + static_cast<ptrdiff_t>(i));
                if (window != Engine::kTopBarWindow) {
                    e.dropFill();
                    if (dc != 0) e.redrawCount_++;   // exe:004137e0: the page is blitted, then flipped for any non-null HDC (immediately, or
                                                     // once at the end of the idle pass: DAT_0049aa18); +04 / +08 are not read
                }
                return kApiOk;
            }
        return kApiFailure;
    }

    // ---- commands the engine has no feature for yet ----
    // case 0x34 / 0x35, exe:0043da90: SaveGame has no restriction mask entry of its own; LoadGame is refused by the masks 2, 4, 8.
    // The result is the error code the engine routine left (0x1c .. 0x22) when it failed. LoadGame's mode (the third argument of
    // exe:00452b70 on the stack): 0 .. 3 the speed of the fade-in, anything else no fade.
    IMPL(0x34, SaveGame, (const std::string& fileName)) {
        GATE(0x34);
        return e.saveGame(fileName);
    }
    IMPL(0x35, LoadGame, (const std::string& fileName, int mode)) {
        GATE(0x35);
        return e.loadGame(fileName, mode);
    }
    // ---- the sim-status window (engine_simstatus.cpp; exe:0043da90 cases 0x4a .. 0x4e, 0x56) ----
    // case 0x4a: exe:00410130, 0x11 when it cannot be made. 0x4b: +04 == 1 shows (palette at +c0 or null, +08 hotspots on, +1c the transparent
    // index of a window without palette), else hides with a redraw; 0x11 when the window does not exist or cannot be shown.
    IMPL(0x4a, CreateSimStatusWindow, (const std::string& page)) {
        GATE(0x4a);
        return e.simStatusCreate(page) ? kApiOk : kApiFailure;
    }
    IMPL(0x4b, ShowSimStatusWindow, (bool show, const void* unkC0, int unk08, int unk1c)) {
        GATE(0x4b);
        const int r = show ? e.simStatusShow(static_cast<const u8*>(unkC0), unk08 != 0, unk1c) : e.simStatusHide(true);
        return r ? kApiOk : kApiFailure;
    }
    // case 0x4c: +04 == 1 gets the DC of the window's surface (exe:004102f0, IDirectDrawSurface::GetDC: one at a time), else releases the
    // DC in +18 (exe:00410330; the window is blitted again when it is up). 0x11 on failure.
    IMPL(0x4c, GetReleaseSimStatusDC, (bool get, int& dc)) {
        GATE(0x4c);
        if (!e.sim_ || !e.sim_->view) return kApiFailure;
        if (get) {
            for (const Engine::OpenDc& d : e.dcs_)
                if (d.window == Engine::kSimStatusWindow) return kApiFailure;
            Engine::OpenDc d;
            Surface* surface = nullptr;
            std::vector<u8>* mask = nullptr;
            if (!e.sim_->view->drawTarget(Engine::kMainWindow, surface, mask, d.owner)) return kApiFailure;
            Engine* eng = &e;
            d.window = Engine::kSimStatusWindow;
            d.canvas = std::make_unique<Canvas>(*surface, [eng]() -> const Palette& { return eng->view_.palette(); }, &e.fonts_, mask);
            d.handle = CanvasRegistry::add(d.canvas.get());
            dc = d.handle;
            e.dcs_.push_back(std::move(d));
            return kApiOk;
        }
        for (size_t i = 0; i < e.dcs_.size(); i++)
            if (e.dcs_[i].window == Engine::kSimStatusWindow && e.dcs_[i].handle == dc) {
                CanvasRegistry::remove(dc);
                e.dcs_.erase(e.dcs_.begin() + static_cast<ptrdiff_t>(i));
                return kApiOk;
            }
        return kApiFailure;
    }
    // case 0x4d: exe:004103a0 TCPage::DisplayImage on the window's page, (x, y) = (-1, -1): the image's own position. 0x11 on failure.
    IMPL(0x4d, DisplaySimStatusImage, (const std::string& image, int x, int y)) {
        GATE(0x4d);
        if (!e.sim_ || !e.sim_->view) return kApiFailure;
        const int pos[2] = {x, y};
        e.sim_->view->setKeyPalette(&e.view_.palette());   // exe:00412530: the colour key is looked up in the palette on display
        const int rc = e.sim_->view->displayImage(Engine::kMainWindow, image, x == -1 && y == -1 ? nullptr : pos, 0xffffffffu, false, nullptr, false);
        e.sim_->view->setKeyPalette(nullptr);
        return rc == 0 ? kApiOk : kApiFailure;
    }
    IMPL(0x4e, DestroySimStatusWindow, ()) {
        GATE(0x4e);
        e.simStatusDestroy(true);
        return kApiOk;
    }
    // case 0x56: not while page content is refused (mask 2); 0x11 without the window; TCPage::EnableHotspot of its page: 4 for an unknown hotspot.
    IMPL(0x56, EnableSimStatusHotspot, (const std::string& hotspot, bool enable)) {
        GATE(0x56);
        if (!e.sim_ || !e.sim_->view) return kApiFailure;
        const Page& pg = e.sim_->view->page();
        return e.sim_->view->setHotspotEnabled(pg, hotspot, enable, false, nullptr) ? kApiOk : kApiHotspotNotFound;
    }
    // case 0x50 / 0x51, exe:0043da90: rect = 4 ints (left, top, right, bottom), +14 the length (1 .. 8), +18 the fill colour index,
    // +b0 a pointer to the text colour (R, G, B bytes) or null. 0x11 when the entry cannot start; 0x51 ends it (+04 = 1 cancels).
    IMPL(0x50, BeginTextInput, (const void* rect, int maxLength, int unk18, void* unkB0)) {
        GATE(0x50);
        return e.beginTextInput(static_cast<const int*>(rect), maxLength, static_cast<u8>(unk18), static_cast<const u8*>(unkB0));
    }
    IMPL(0x51, EndTextInput, (bool cancel)) {
        GATE(0x51);
        e.endTextInput(cancel);
        return kApiOk;
    }
    // case 0x53: masks 2, 4 and 8 refuse it (apiMaskRefuses); 0x11 when the restart failed
    IMPL(0x53, RestartGame, ()) {
        GATE(0x53);
        return e.restartGame();
    }
    // case 0x54: "" / NULL / "top" the main menu (1), "loadgame" (2), "savegame" (3), anything else 0x12; 0x11 if it cannot open
    IMPL(0x54, OpenMenu, (const std::string& menu, const void* unkC0, int unk04)) {
        GATE(0x54);
        int kind;
        if (menu.empty() || iequals(menu, "top")) kind = 1;
        else if (iequals(menu, "savegame")) kind = 3;
        else if (iequals(menu, "loadgame")) kind = 2;
        else return kApiBadParameter;
        return e.openMenu(kind, static_cast<const u8*>(unkC0), unk04);
    }
    // case 0x55: the save directory, "C:\\" when none is set
    IMPL(0x55, GetSaveDirectory, (std::string& directory)) {
        GATE(0x55);
        directory = e.saveDir_.empty() ? std::string("C:\\") : e.saveDir_;
        return kApiOk;
    }
    STUB(0x58, DumpFileStats, (const void* unkC0, int unk04), "misc")
    STUB(0x6a, EnumPageHotspots, (int window), "page images and text")
    STUB(0x6b, Unresolved6b, (int window, int count, void* names, int unk18, void* unkB0), "page images and text")
};

std::unique_ptr<EngineApi> Engine::makeApi(Engine& e) { return std::make_unique<ApiImpl>(e); }

} // namespace biosys
