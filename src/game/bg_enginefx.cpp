// CEngineFX (dll:1005fdc0..10061500): the DLL's own layer over the engine API. It keeps the palette on display (+0x104)
// and the lookup (XFT) file that belongs to it, a "palette owner" token (+0x568: the page-specific code that put its
// palette up keeps others from replacing it), a sound lock (+0x57c), the looped movie of the current page and the
// fonts, and wraps the sound / animation / movie API calls with the checks and log messages of the original.
#include "game/biosgame.h"
#include "game/story.h"
#include <cstdio>
#include <cstring>

namespace biosys::game {

// dll:1005fdc0 (the static constructor)
void BiosGame::CEngineFX_Construct() {
    save_->engineFx.m_wState = 0;
    fx = EngineFxState();
    fx.loopedAviPage = "nowhere";   // s_nowhere_1009f54c
    fx.aviHandle = -1;
    fx.paletteOn = true;
    // FUN_100617d0 (dll:100617d0), the synth-stalker / cursor filter state of CEngineFX (+0x6c8..+0x8a4), is StoryLogic's (story.h)
}

// dll:1005fe80
void BiosGame::CEngineFX_Init() {
    eng_.GetDataDirectory(fx.dataDir);
    if (!CEngineFX_CreateFontResource("jon.ttf")) {
        int e = eng_.lastError();
        log("CEngineFX::Init - CreateFontResource(\"jon.ttf\") error: " + std::to_string(e));
    }
    if (!CEngineFX_CreateLogicalFont("systemfont", 0x12, fx.systemFont)) {
        int e = eng_.lastError();
        log("CEngineFX::Init - CreateLogicalFont(\"systemfont\", 18, m_hSystemFont) error: " + std::to_string(e));
    }
}

// dll:1005ff10
void BiosGame::CEngineFX_Shutdown() {
    if (fx.systemFont != 0) {                      // FUN_100614e0 dll:100614e0
        int font = fx.systemFont;
        eng_.CreateDeleteLogicalFont(0, nullptr, font);
    }
    CEngineFX_RemoveFontResource("jon.ttf");
}

// dll:100611e0. The path is the data directory + "\fonts\" + file (DAT_100bc398).
bool BiosGame::CEngineFX_CreateFontResource(const std::string& file) {
    gs().message = fx.dataDir + "\\fonts\\" + file;
    bool ok = eng_.AddRemoveFontResource(gs().message, 1);
    if (!ok) {
        gs().message = "BiosGame.dll - AddRemoveFontResource failed: " + file;
        log(gs().message);
    }
    return ok;
}

// dll:10061300
void BiosGame::CEngineFX_RemoveFontResource(const std::string& file) {
    gs().message = fx.dataDir + "\\fonts\\" + file;
    eng_.AddRemoveFontResource(gs().message, 0);
}

// dll:100613a0. The LOGFONT is the 60-byte Win32 structure.
int BiosGame::CEngineFX_CreateLogicalFont(const std::string& face, int height, int& font) {
    int dc = 0, s20 = 0, s14 = 0;
    eng_.GetWindowDC(-1, dc, s20, s14);
    // SetMapMode(dc, MM_TEXT): GDI call on the window DC, no engine command
    u8 lf[60] = {};
    auto put32 = [&](int off, int v) { std::memcpy(lf + off, &v, 4); };
    put32(0, height);
    put32(16, 400);                                  // FW_NORMAL
    lf[23] = 1;                                      // DEFAULT_CHARSET
    lf[26] = 2;                                      // quality
    std::strncpy(reinterpret_cast<char*>(lf + 28), face.c_str(), 31);
    int created = eng_.CreateDeleteLogicalFont(1, lf, font);
    if (!created) {
        font = 0;
        gs().message = "BiosGame.dll - CreateDeleteLogicalFont failed: " + face;
        log(gs().message);
    }
    eng_.ReleaseWindowDC(-1, dc, 0, 1);
    return created;
}

// dll:100604d0 (the path is built by FUN_100074f0 from the data directory). A palette file is a 0x418-byte RIFF PAL
// file: 24 bytes of header, then 256 x {r,g,b,flags}.
bool BiosGame::readPalette(const std::string& file, Pal& out) {
    std::string path = files_.findData(file);
    if (path.empty()) return false;
    std::vector<u8> d;
    try { d = readFile(path); } catch (const Error&) { return false; }
    if (d.size() != 0x418) return false;
    if (d[0] != 'R' || d[1] != 'I' || d[2] != 'F' || d[3] != 'F') return false;
    std::memcpy(out.data(), d.data() + 0x18, 0x400);
    return true;
}

// dll:10060b50: the tint of the palette (set by the intro / trip code, +0x570..+0x578): entries 62..255
void BiosGame::CEngineFX_ApplyTint(Pal& p) {
    if (fx.tintR == 0 && fx.tintG == 0 && fx.tintB == 0) return;
    for (int i = 0x3e; i < 0x100; i++) {
        u8* e = p.data() + i * 4;
        auto clamp = [](int v) { return v < 0 ? 0 : v > 255 ? 255 : v; };
        e[0] = static_cast<u8>(clamp(e[0] + fx.tintR));
        e[1] = static_cast<u8>(clamp(e[1] + fx.tintG));
        e[2] = static_cast<u8>(clamp(e[2] + fx.tintB));
    }
}

// the end shared by dll:10060170 / 10060280: puts the palette on display. Returns false when the engine refused.
// (dll:10074fa0 Eng_FadeToPalette / dll:10074ba0 Eng_SetPalette)
static bool showPaletteWith(Eng& eng, const Pal& pal, int steps, int immediate, int unk) {
    if (immediate == 0) return eng.FadeToPalette(pal.data(), unk, steps, 100);
    return eng.SetPalette(pal.data(), steps * 2 + 2, steps);
}

// dll:10060170: loads a palette file and shows it. `xft` (optional) is the lookup file that goes with it.
bool BiosGame::CEngineFX_LoadPalette(const std::string& file, const char* xft, int steps, int immediate, int token, int unk) {
    if (g.noPalette) return true;
    if (g.loading) return true;
    u16& state = save_->engineFx.m_wState;
    if (state != 0 && state != token) return false;
    if (!readPalette(file, fx.palette)) return false;
    if (xft) fx.xft = xft;
    state = static_cast<u16>(token);
    CEngineFX_ApplyTint(fx.palette);
    bool ok = true;
    if (fx.paletteOn) ok = showPaletteWith(eng_, fx.palette, steps, immediate, unk);
    return ok;
}

// dll:10060280: the same with a palette in memory
bool BiosGame::CEngineFX_SetPalette(const Pal& pal, const char* xft, int steps, int immediate, int token, int unk) {
    if (g.noPalette) return true;
    if (g.loading) return true;
    u16& state = save_->engineFx.m_wState;
    if (state != 0 && state != token) return false;
    fx.palette = pal;
    if (xft) fx.xft = xft;
    state = static_cast<u16>(token);
    CEngineFX_ApplyTint(fx.palette);
    bool ok = true;
    if (fx.paletteOn) ok = showPaletteWith(eng_, fx.palette, steps, immediate, unk);
    return ok;
}

// dll:10060020. out = a for the first `fixed` entries, then each channel a + trunc((b - a) * frac) (mod 256), flags of a.
void BiosGame::MakeIntermediatePalette(const Pal& a, const Pal& b, Pal& out, double frac, int fixed) {
    if (frac < 0.0) { log("biosgame.dll: dRatio < 0 in MakeIntermediatePalette"); frac = 0.0; }
    if (1.0 < frac) { log("biosgame.dll: dRatio > 1 in MakeIntermediatePalette"); frac = 1.0; }
    for (int i = 0; i < fixed; i++) std::memcpy(out.data() + i * 4, a.data() + i * 4, 4);
    for (int i = fixed; i < 0x100; i++) {
        const u8* pa = a.data() + i * 4;
        const u8* pb = b.data() + i * 4;
        u8* po = out.data() + i * 4;
        for (int c = 0; c < 3; c++)
            po[c] = static_cast<u8>(pa[c] + static_cast<u8>(static_cast<int>(static_cast<double>(pb[c] - pa[c]) * frac)));
        po[3] = pa[3];
    }
}

// dll:10060370: shows the blend of two palettes (frac 0 = a, 1 = b) and remembers the lookup of the nearer one.
bool BiosGame::CEngineFX_Blend(const Pal& a, const Pal& b, double frac, const std::string& xftA, const std::string& xftB,
                               int steps, int immediate, int unk, int token) {
    if (g.noPalette) return true;
    u16& state = save_->engineFx.m_wState;
    if (state != 0 && state != token) return false;
    CFeature* feat = dynamic_cast<CFeature*>(self().m_pFeature.item);
    // the condition of the original: no feature, or the feature's flag byte (+0x10) lacks 0x20, or the page is a B site
    bool plain = !feat || (feat->flags() & 0x20) == 0 || self().m_szLocation[0] == 'B';
    if (plain) {
        MakeIntermediatePalette(a, b, fx.palette, frac, 0x3e);
        if (CSite* s = curSite()) CSite_TintPalette(*s, fx.palette);
    } else if (0.5 <= frac) {
        fx.palette = b;
    } else {
        fx.palette = a;
    }
    bool ok = true;
    if (fx.paletteOn) {
        if (immediate == 0) ok = eng_.FadeToPalette(fx.palette.data(), unk, steps, 100);
        else ok = eng_.SetPalette(fx.palette.data(), steps * 2, steps);
    }
    if (ok) {
        const std::string& x = frac < 0.5 ? xftA : xftB;
        if (!x.empty()) fx.xft = x;
        state = static_cast<u16>(token);
        return true;
    }
    return false;
}

// dll:100605f0 (back = 0) / dll:10060620 (back = 1)
bool BiosGame::CEngineFX_FadeToColour(int r, int g_, int b, int back, int speed, int percent, int unk) {
    return eng_.FadeToColour(r, g_, b, back, speed, unk, percent);
}
// dll:10060650
void BiosGame::CEngineFX_ShowPalette() { eng_.SetPalette(fx.palette.data(), 1, 0); }
// dll:10060670
void BiosGame::CEngineFX_FadeToCurrent(int speed) { eng_.FadeToPalette(fx.palette.data(), 0, speed, 100); }
// dll:10060690
void BiosGame::CEngineFX_HideCursor() { eng_.ShowCursor(0); }

// dll:10060ce0 / dll:10060cb0: the palette owner token
bool BiosGame::CEngineFX_LockPalette(int token) {
    u16& s = save_->engineFx.m_wState;
    if (s == 0) { s = static_cast<u16>(token); return true; }
    return false;
}
// dll:10060cb0
bool BiosGame::CEngineFX_UnlockPalette(int token) {
    u16& s = save_->engineFx.m_wState;
    if (s == token) { s = 0; return true; }
    return false;
}
// dll:100606a0 / dll:100606d0: the sound lock
bool BiosGame::CEngineFX_LockSounds(int token) {
    if (fx.soundLock == 0) { fx.soundLock = token; return true; }
    return false;
}
// dll:100606d0
bool BiosGame::CEngineFX_UnlockSounds(int token) {
    if (fx.soundLock == token) { fx.soundLock = 0; return true; }
    return false;
}

// dll:10060700: plays a wav once (fire and forget; the engine closes it when it ends, SoundCallback code 3)
bool BiosGame::CEngineFX_PlaySound(const std::string& file, int token, int volume) {
    if (g.silent) return false;
    if (fx.soundLock != 0 && fx.soundLock != token) return false;
    if (file.empty()) return false;
    int h = -1;
    bool ok = false;
    if (eng_.OpenSound(file, 0, true, 0, 0, volume, h)) ok = eng_.PlaySound(h, 1);
    return ok;
}

// dll:10060780: the same, keeping the handle in *handle (which must be -1)
bool BiosGame::CEngineFX_PlayLoopedSoundInto(const std::string& file, int* handle, int token, int volume) {
    if (g.silent) return false;
    if (fx.soundLock != 0 && fx.soundLock != token) return false;
    if (file.empty()) return false;
    if (*handle != -1) return false;
    int h = -1;
    bool ok = false;
    if (eng_.OpenSound(file, 0, true, 0, 0, volume, h)) {
        *handle = h;
        ok = eng_.PlaySound(h, 1);
    }
    return ok;
}

// dll:10060820: plays the wav "<stem><1 + rand() % n>.wav"
void BiosGame::CEngineFX_PlayRandomSound(const std::string& stem, unsigned n) {
    unsigned r = static_cast<unsigned>(crand());
    char b[16];
    std::snprintf(b, sizeof b, "%u", r % n + 1);
    CEngineFX_PlaySound(stem + b + ".wav", 0, 0);   // "%s%d.wav"
}

// dll:10060860: opens a wav as a loop (volume in 1/100 dB), optionally panned to a hotspot, and plays it
int BiosGame::CEngineFX_PlayLoopedWav(const std::string& file, int volume, int* handle, const char* panHotspot, int token) {
    if (g.silent) return 0;
    if (fx.soundLock != 0 && fx.soundLock != token) return 0;
    if (file.empty()) return 0;
    if (*handle != -1) return 0;
    int h = -1;
    int ok = eng_.OpenSound(file, 1, true, 0, 0, volume, h);
    *handle = h;
    if (ok) {
        if (panHotspot) eng_.SetSoundPanToPagePosition(h, 1, 0, panHotspot);
        if (eng_.PlaySound(*handle, 1)) return 1;
    }
    gs().message = "BiosGame.dll: PlayLoopedWav failed - " + file;
    return 0;
}

// dll:10060980
bool BiosGame::CEngineFX_SetVolume(int handle, int volume) {
    if (handle == -1) return false;
    return eng_.SetSoundProperty(handle, "VOLUME", volume);
}
// dll:100609b0
void BiosGame::CEngineFX_CloseSound(int* handle) {
    if (eng_.CloseSound(*handle)) *handle = -1;
}
// dll:100609d0
bool BiosGame::CEngineFX_StopSound(int handle) {
    if (handle == -1) return false;
    return eng_.StopSound(handle);
}

// dll:100609f0: opens a sprite animation (looping off, mode 1) and plays it
int BiosGame::CEngineFX_PlayAnim(const std::string& file, int* handle, int window, int frameDelay, int x, int y,
                                 int pageRelative, int keepPainted, const std::string& wav) {
    if (g.silent) return 0;
    int local = -1;
    int ok = eng_.OpenSpriteAnimation(file, window, x, y, 1, frameDelay, true, 0, keepPainted, wav, 0, 0, pageRelative, local, -1, 0);
    *handle = local;
    if (!ok) return 0;
    if (!eng_.PlaySpriteAnimation(local, 1, 0)) {
        *handle = local;
        return 0;
    }
    return 1;
}

// dll:10060aa0: the same looping, with the play mode and the sound mode as arguments
bool BiosGame::CEngineFX_PlayAnimEx(const std::string& file, int* handle, int window, int frameDelay, int x, int y,
                                    int maxPlayMs, int pageRelative, int keepPainted, const std::string& wav, int soundMode,
                                    int playMode, int visibleWidth) {
    if (g.silent) return false;
    int local = -1;
    int ok = eng_.OpenSpriteAnimation(file, window, x, y, playMode, frameDelay, true, 1, keepPainted, wav, maxPlayMs, soundMode,
                                      pageRelative, local, visibleWidth, 0);
    *handle = local;
    if (!ok) return false;
    return eng_.PlaySpriteAnimation(local, 1, 0);
}

// dll:10060b40
void BiosGame::CEngineFX_StopAnim(int handle) { eng_.StopSpriteAnimation(handle); }

// dll:10060bf0
void BiosGame::CEngineFX_DisplayImage(const std::string& image, int window, int x, int y, int unk20, int unk04) {
    int xy[2] = {x, y};
    eng_.DisplayImage(image, window, xy, unk04, unk20, nullptr, 0);
}

// dll:10060c30: puts the wearing image of an object on the page (Eng_Api31_PlaceGameObject(object, extra, object, "wearing"))
void BiosGame::CEngineFX_PlaceWorn(const std::string& object, const std::string& extraImage, int x, int y) {
    api_.PlaceGameObject(object, extraImage, object, "wearing", 0, 0, x, y);
}

// dll:100616a0
int BiosGame::CEngineFX_ChangeHoldingImagesGroup(const std::string& object, int group) {
    bool ok = eng_.ChangeGameObjectHoldingImagesGroup(object, group);
    if (!ok) {
        gs().message = "BiosGame.dll: ChangeGameObjectHoldingImagesGroup (sheesh what a mouthfull) failed for " + object + ", group " + std::to_string(group);
        log(gs().message);
    }
    return ok ? 1 : 0;
}

// dll:10060c70 / dll:10060c90 (49 and 52 callers: argument order of the DLL: name, value, window)
void BiosGame::CEngineFX_ImageVisibility(const std::string& image, int visible, int window) {
    eng_.SetImageVisibility(image, window, visible);
}
// dll:10060c90
void BiosGame::CEngineFX_HotspotEnable(const std::string& hotspot, int enable, int window) {
    eng_.EnableHotspot(hotspot, window, enable, nullptr, 0);
}

// dll:10060d10: a page jump through the engine with the palette display switched off (paletteOn = 0) for its duration
int BiosGame::CEngineFX_JumpToPage(const std::string& page, int x, int y, int paletteOn, int bigPageMode) {
    fx.paletteOn = paletteOn != 0;
    int xy[2] = {x, y};
    bool ok = eng_.JumpToPage(page, "none", 0, xy, bigPageMode, 0);   // DAT_100a1244 = "none"
    fx.paletteOn = true;
    if (!ok) {
        gs().message = "BiosGame.dll: totally failed to jump to " + page;
        log(gs().message);
    }
    return ok ? 1 : 0;
}

// dll:10060df0
void BiosGame::CEngineFX_ClearWindow() {
    if (!eng_.ClearWindow(-1, 0)) log("ClearWindow failed");
}

// dll:10060e10 (restart = param_10, the second argument of Eng_PlayAVIMovie)
bool BiosGame::CEngineFX_PlayAvi(const std::string& file, int x, int y, int volume, int colourKey, const std::string& page,
                                 int pageMode, int opt42, int synth, int restart) {
    if (g.silent) return false;
    if (!eng_.OpenAVIMovie(file, -1, x, y, true, 0, 0, fx.palette.data(), fx.aviHandle)) {
        log("BiosGame.dll: PlayAvi - OpenAVIMovie failed, " + file);
        return false;
    }
    if (colourKey != 0) {
        const u8 key[4] = {0, 0xff, 0xff, 0};
        eng_.SetAVIMovieParam(fx.aviHandle, key);
    }
    if (volume < 0) eng_.SetAVIMovieVolume(fx.aviHandle, volume);
    if (pageMode != 0 && !iequals(page, "nowhere")) eng_.SetAVIMoviePage(fx.aviHandle, page);
    if (opt42 != 0) eng_.AviOption42(fx.aviHandle);
    if (synth != 0) eng_.PrepareSynthAVIMovie(fx.aviHandle);
    fx.loopedAvi = false;
    return eng_.PlayAVIMovie(fx.aviHandle, restart);
}

// dll:10060f40: a movie that plays in a loop and is reopened when the window is reactivated (FUN_10061130)
bool BiosGame::CEngineFX_PlayLoopedAvi(const std::string& file, int x, int y, int volume, int colourKey,
                                       const std::string& page, int pageMode, int opt42, int synth) {
    if (g.silent) return false;
    if (!eng_.OpenAVIMovie(file, -1, x, y, true, 1, 0, fx.palette.data(), fx.aviHandle)) {
        log("BiosGame.dll: PlayLoopedAvi - OpenAVIMovie failed, " + file);
        return false;
    }
    if (colourKey != 0) {
        const u8 key[4] = {0, 0xff, 0xff, 0};
        eng_.SetAVIMovieParam(fx.aviHandle, key);
    }
    if (volume < 0) eng_.SetAVIMovieVolume(fx.aviHandle, volume);
    if (pageMode != 0 && !iequals(page, "nowhere")) eng_.SetAVIMoviePage(fx.aviHandle, page);
    if (opt42 != 0) eng_.AviOption42(fx.aviHandle);
    if (synth != 0) eng_.PrepareSynthAVIMovie(fx.aviHandle);
    fx.loopedAvi = true;
    fx.loopedAviFile = file;
    fx.loopedAviPage = page;
    fx.loopedAviUnk = pageMode;
    fx.aviX = x;
    fx.aviY = y;
    fx.aviVolume = volume;
    fx.aviKey = colourKey;
    fx.aviOption = opt42;
    return eng_.PlayAVIMovie(fx.aviHandle, 1);
}

// dll:10061110 (StopAVIMovie of the CEngineFX movie handle) and dll:10061120 (CloseAVIMovie of it)
void BiosGame::CEngineFX_StopLoopedAvi() { eng_.StopAVIMovie(fx.aviHandle); }
void BiosGame::CEngineFX_LoopedAviClosed() { eng_.CloseAVIMovie(fx.aviHandle); }

// dll:10061130: the window was activated again: restart the looped movie if the player is on its page
void BiosGame::CEngineFX_ResumeLoopedAvi(int active) {
    if (active != 0 && fx.loopedAvi && fx.aviHandle == -1) {
        // CSelf::IsAtPage dll:? compares the player's page name with the movie's page
        if (iequals(std::string(reinterpret_cast<const char*>(self().m_szLocation.data())), fx.loopedAviPage))
            CEngineFX_PlayLoopedAvi(fx.loopedAviFile, fx.aviX, fx.aviY, fx.aviVolume, fx.aviKey, fx.loopedAviPage,
                                    fx.loopedAviUnk, fx.aviOption, 0);
    }
}

} // namespace biosys::game

namespace biosys::game {

// dll:100611b0
void BiosGame::CEngineFX_SetTextColours(u32 colour, int transparent, u32 background) {
    fx.textColour = colour;
    fx.textBackground = background;
    fx.textTransparent = transparent;
}

// dll:10061500: its own DC on the main window; the font (or the system font) selected; text colour, then OPAQUE with the
// background colour or TRANSPARENT; TextOutA at (x, y); the DC released with (0, 1).
bool BiosGame::CEngineFX_DrawText(const std::string& text, int x, int y, int font) {
    GdiSession dc(eng_, -1, font ? font : fx.systemFont, [this](int tx, int ty, const std::string& t) { story_->uiRecordText(tx, ty, t); });
    if (!dc.ok()) return false;
    dc.dc().setTextColor(fx.textColour);
    if (fx.textTransparent == 0) {
        dc.dc().setBkMode(kOpaque);
        dc.dc().setBkColor(fx.textBackground);
    } else {
        dc.dc().setBkMode(kTransparent);
    }
    dc.textOut(x, y, text);
    return true;
}

// port: the display methods' DC
std::unique_ptr<GdiSession> BiosGame::openDc(int window) {
    auto s = std::make_unique<GdiSession>(eng_, window, fx.systemFont,
                                          [this](int tx, int ty, const std::string& t) { story_->uiRecordText(tx, ty, t); });
    if (!s->ok()) return nullptr;
    return s;
}

} // namespace biosys::game
