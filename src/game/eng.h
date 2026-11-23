// The DLL's thin API wrappers (dll:10073fc0..10076930, "Eng_*"), over the typed EngineApi. Each wrapper stores the
// reply in the DLL global DAT_100be424 (Eng_GetLastError dll:10073fb0); the wrappers that check a name length answer
// error 1 without sending (the limits are in data/dll_api_wrappers.tsv). Return conventions of the original:
// "bool" = rc == 0, "int" = 1 on success / 0 on failure with the out-parameters written only on success.
// The argument order is the DLL's, not EngineApi's (the table maps one to the other).
#pragma once
#include "engine/engineapi.h"
#include <string>

namespace biosys::game {

class Eng {
public:
    explicit Eng(EngineApi& api) : api_(api) {}
    EngineApi& api() { return api_; }

    int lastError() const { return lastError_; }   // Eng_GetLastError dll:10073fb0

    void SetTimerInterval(int seconds) { api_.SetTimerInterval(seconds); lastError_ = 0; }                 // dll:10073fc0
    void QuitGame() { api_.QuitGame(); lastError_ = 0; }                                                     // dll:10073ff0
    // dll:10074010
    bool EnableHotspot(const std::string& name, int window, int enable, int* previousState, int prefixWildcard) {
        if (!lenOk(name, 10)) return false;
        return rc(api_.EnableHotspot(window, name, enable != 0, prefixWildcard != 0, previousState));
    }
    // dll:100740d0. xy == nullptr: (0, 0)
    bool JumpToPage(const std::string& page, const std::string& transition, int pushHistory, const int* xy,
                    int bigPageMode, int noFlip) {
        if (!lenOk(page, 10) || !lenOk(transition, 14)) return false;
        return rc(api_.JumpToPage(page, transition, pushHistory != 0, xy ? xy[0] : 0, xy ? xy[1] : 0, bigPageMode, noFlip));
    }
    bool DoHotspotAction(const std::string& hotspot, int window) {                                           // dll:10074200
        if (!lenOk(hotspot, 10)) return false;
        return rc(api_.DoHotspotAction(window, hotspot));
    }
    void WriteLog(const std::string& text) { api_.WriteLog(text); lastError_ = 0; }                          // dll:100742a0
    // dll:100742d0. xy == nullptr: (-1, -1)
    bool DisplayImage(const std::string& image, int window, const int* xy, int unk04, int unk20, int* unkOut, int unk08) {
        if (!lenOk(image, 10)) return false;
        return rc(api_.DisplayImage(window, image, xy ? xy[0] : -1, xy ? xy[1] : -1, unk04, unk08, unk20, unkOut));
    }
    bool SetImageVisibility(const std::string& image, int window, int visible) {                             // dll:10074440
        if (!lenOk(image, 10)) return false;
        return rc(api_.SetImageVisibility(window, image, visible != 0));
    }
    int GetGameObjectInfo(const std::string& name, std::array<int, 12>& info) {                              // dll:100744f0
        if (!lenOk(name, 10)) return 0;
        return ok(api_.GetGameObjectInfo(name, info));
    }
    int GetDropZoneInfo(int index, std::string& page, int& hotspotIndex, std::string& object) {              // dll:100745b0
        return ok(api_.GetDropZoneInfo(index, page, hotspotIndex, object));
    }
    // dll:100747d0 (the callback pointer is always passed by the DLL: notify = true)
    int OpenSound(const std::string& file, int loop, bool notify, int frequency, int pan, int volume, int& handle) {
        return ok(api_.OpenSound(file, loop, notify, frequency, pan, volume, handle));
    }
    bool PlaySound(int handle, int restart) { return rc(api_.PlaySound(handle, restart != 0)); }             // dll:10074880
    bool StopSound(int handle) { return rc(api_.StopSound(handle)); }                                        // dll:100748e0
    int IsSoundPlaying(int handle, int& playing) { return ok(api_.IsSoundPlaying(handle, playing)); }        // dll:10074930
    bool SetSoundProperty(int handle, const std::string& property, int value) {                              // dll:100749a0
        if (!lenOk(property, 12)) return false;
        return rc(api_.SetSoundProperty(handle, property, value));
    }
    bool CloseSound(int handle) { return rc(api_.CloseSound(handle)); }                                      // dll:10074b50
    bool SetPalette(const u8* palette, int steps, int duration) {                                            // dll:10074ba0
        return rc(api_.SetPalette(palette, steps, duration));
    }
    void GetDataDirectory(std::string& directory) { api_.GetDataDirectory(directory); lastError_ = 0; }     // dll:10074c10
    // dll:10074c40: r, g, b are bytes packed r | g << 8 | b << 16
    bool FadeToColour(int r, int g, int b, int unk04, int speed, int unk1c, int percent) {
        return rc(api_.FadeToColour((r & 0xff) | ((g & 0xff) << 8) | ((b & 0xff) << 16), unk04, speed, unk1c, percent));
    }
    // dll:10074ce0
    int OpenSpriteAnimation(const std::string& file, int window, int x, int y, int playMode, int frameDelayMs,
                            bool notify, int loop, int keepPainted, const std::string& wav, int maxPlayMs, int soundMode,
                            int pageRelative, int& handle, int visibleWidth, int eraseOnStop) {
        return ok(api_.OpenSpriteAnimation(file, window, x, y, playMode, frameDelayMs, notify, loop != 0, keepPainted != 0, wav,
                                           maxPlayMs, soundMode, pageRelative != 0, visibleWidth, eraseOnStop != 0, handle));
    }
    bool PlaySpriteAnimation(int handle, int restart, int priority) {                                        // dll:10074e20
        return rc(api_.PlaySpriteAnimation(handle, restart != 0, priority != 0));
    }
    bool StopSpriteAnimation(int handle) { return rc(api_.StopSpriteAnimation(handle)); }                    // dll:10074e90
    bool CloseSpriteAnimation(int handle) { return rc(api_.CloseSpriteAnimation(handle)); }                  // dll:10074f50
    bool FadeToPalette(const u8* palette, int unk04, int speed, int percent) {                               // dll:10074fa0
        return rc(api_.FadeToPalette(palette, unk04, speed, percent));
    }
    bool ChangeGameObjectHoldingImagesGroup(const std::string& object, int group) {                         // dll:10075830
        if (!lenOk(object, 10)) return false;
        return rc(api_.ChangeGameObjectHoldingImagesGroup(object, group));
    }
    bool RemoveGameObject(const std::string& object) {                                                       // dll:100750c0
        if (!lenOk(object, 10)) return false;
        return rc(api_.RemoveGameObject(object));
    }
    // dll:10074a50: hotspot == nullptr: x is used
    bool SetSoundPanToPagePosition(int handle, int enable, int x, const char* hotspot) {
        if (enable != 0 && hotspot) {
            if (!*hotspot) { lastError_ = 4; return false; }
            if (!lenOk(hotspot, 10)) return false;
        }
        return rc(api_.SetSoundPanToPagePosition(handle, enable != 0, enable != 0 && !hotspot ? x : 0,
                                                 enable != 0 && hotspot ? hotspot : ""));
    }
    // dll:100752d0 (callback pointer always passed: notify = true)
    int OpenAVIMovie(const std::string& file, int window, int x, int y, bool notify, int loop, int unk04,
                     const u8* palette, int& handle) {
        return ok(api_.OpenAVIMovie(file, window, x, y, notify, loop != 0, unk04, palette, handle));
    }
    bool PlayAVIMovie(int handle, int restart) { return rc(api_.PlayAVIMovie(handle, restart != 0)); }       // dll:10075650
    bool StopAVIMovie(int handle) { return rc(api_.StopAVIMovie(handle)); }                                  // dll:100756b0
    bool CloseAVIMovie(int handle) { return rc(api_.CloseAVIMovie(handle)); }                                // dll:10075700
    bool EnableUserInput(int enable, int unk08) { return rc(api_.EnableUserInput(enable != 0, unk08)); }     // dll:10075750
    int GetPanoramaInfo(int& pageWidth, int& startX, int& centreX) {                                         // dll:100757b0
        return ok(api_.GetPanoramaInfo(pageWidth, startX, centreX));
    }
    bool SaveGame(const std::string& fileName) { return rc(api_.SaveGame(fileName)); }                       // dll:10075b40
    bool LoadGame(const std::string& fileName, int mode) { return rc(api_.LoadGame(fileName, mode)); }       // dll:10075b90
    bool BeginPageFilter(int a, int b, int c) { return rc(api_.BeginPageFilter(a, b, c)); }                  // dll:10075ab0
    void EndPageFilter() { api_.EndPageFilter(); lastError_ = 0; }                                           // dll:10075b20
    bool SetAVIMovieParam(int handle, const u8* colourKey) { return rc(api_.SetAVIMovieParam(handle, colourKey)); }   // dll:10075390
    bool AddRemoveFontResource(const std::string& file, int add) { return rc(api_.AddRemoveFontResource(file, add != 0)); }   // dll:10075bf0
    int CreateDeleteLogicalFont(int create, const void* logFont, int& font) {                                // dll:10075c50
        return ok(api_.CreateDeleteLogicalFont(create != 0, logFont, font));
    }
    int GetWindowDC(int window, int& dc, int& size20, int& size14) { return ok(api_.GetWindowDC(window, dc, size20, size14)); }   // dll:10075ce0
    bool ReleaseWindowDC(int window, int dc, int unk04, int unk08) { return rc(api_.ReleaseWindowDC(window, dc, unk04, unk08)); }   // dll:10075d60
    // the sim-status window (dll:10076240 .. 10076490): "bool" = rc == 0; GetRelease... "int" = 1 on success, the DC in `dc`
    bool CreateSimStatusWindow(const std::string& page) {                                                    // dll:10076240
        if (!lenOk(page, 10)) return false;
        return rc(api_.CreateSimStatusWindow(page));
    }
    bool ShowSimStatusWindow(int show, const void* palette, int hotspots, int key) {                         // dll:100762e0
        return rc(api_.ShowSimStatusWindow(show == 1, palette, hotspots, key));
    }
    int GetSimStatusDC(int& dc) { return ok(api_.GetReleaseSimStatusDC(true, dc)); }                         // dll:10076350 (1)
    int ReleaseSimStatusDC(int dc) { return ok(api_.GetReleaseSimStatusDC(false, dc)); }                     // dll:10076350 (0)
    bool DisplaySimStatusImage(const std::string& image, const int* xy) {                                    // dll:100763d0
        if (!lenOk(image, 10)) return false;
        return rc(api_.DisplaySimStatusImage(image, xy ? xy[0] : -1, xy ? xy[1] : -1));
    }
    bool EnableSimStatusHotspot(const std::string& hotspot, int enable) {                                    // dll:10076490
        if (!lenOk(hotspot, 10)) return false;
        return rc(api_.EnableSimStatusHotspot(hotspot, enable != 0));
    }
    bool ClearWindow(int window, int colour) { return rc(api_.ClearWindow(window, colour)); }                // dll:10075dd0
    bool SetAVIMovieVolume(int handle, int volume) { return rc(api_.SetAVIMovieVolume(handle, volume)); }    // dll:100753f0
    bool SetAVIMoviePage(int handle, const std::string& page) {                                              // dll:10075450
        if (!lenOk(page, 10)) return false;
        return rc(api_.SetAVIMoviePage(handle, page));
    }
    void ShowCursor(int show) { api_.ShowCursor(show != 0); lastError_ = 0; }                                // dll:10075eb0
    bool AviOption41(int handle) { return rc(api_.AviOption41(handle)); }                                    // dll:100754f0
    bool AviOption42(int handle) { return rc(api_.AviOption42(handle)); }                                    // dll:100755b0
    bool PrepareSynthAVIMovie(int handle) { return rc(api_.PrepareSynthAVIMovie(handle)); }                  // dll:10075600
    bool AviOption4f(int handle, int restoreOnStop, int restoreOnClose) {                                    // dll:10075540
        return rc(api_.AviOption4f(handle, restoreOnStop, restoreOnClose));
    }
    bool ReturnGameObject(const std::string& object) {                                                       // dll:10075200
        if (!lenOk(object, 10)) return false;
        return rc(api_.ReturnGameObject(object));
    }
    void RedrawScreen() { api_.RedrawScreen(); lastError_ = 0; }                                             // dll:10076220
    bool EnableMouseDeltaEvent(int enable) { return rc(api_.EnableMouseDeltaEvent(enable != 0)); }           // dll:100767c0
    bool SetPanoramaScrollLimits(int minX, int maxX, int unk04) { return rc(api_.SetPanoramaScrollLimits(minX, maxX, unk04)); }   // dll:10076750
    int GetScrollState(int& active, int& unk08, int& unkAc, int& unk14) {                                    // dll:100768a0
        return ok(api_.GetScrollState(active, unk08, unkAc, unk14));
    }

private:
    bool lenOk(const std::string& s, size_t max) {
        if (s.size() > max) { lastError_ = 1; return false; }
        return true;
    }
    bool rc(int code) { lastError_ = code; return code == 0; }
    int ok(int code) { lastError_ = code; return code == 0 ? 1 : 0; }

    EngineApi& api_;
    int lastError_ = 0;   // DAT_100be424
};

} // namespace biosys::game
