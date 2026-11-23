#include "app/script.h"
#include "app/clean.h"
#include "app/pacing.h"
#include "app/panorama_smooth.h"
#include "app/overlaycompose.h"
#include "app/text_sharp.h"
#include "engine/panomesh.h"
#include "engine/panowide.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <vector>

namespace biosys {

namespace {

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

void parseXY(const std::string& s, int& x, int& y) {
    if (std::sscanf(s.c_str(), "%d,%d", &x, &y) != 2) throw Error("script: bad coordinates '" + s + "'");
}

int parseSlotName(const std::string& s) {
    for (int i = 0; i < SLOT_COUNT; i++)
        if (iequals(s, timeSlotName(i))) return i;
    static const char* names[] = {"night", "dawn", "day", "dusk", "cloud", "goggles"};
    for (int i = 0; i < SLOT_COUNT; i++)
        if (iequals(s, names[i])) return i;
    throw Error("script: bad slot '" + s + "'");
}

Key parseKey(const std::string& s) {
    if (iequals(s, "S")) return Key::S;
    if (iequals(s, "D")) return Key::D;
    if (iequals(s, "SPACE")) return Key::Space;
    if (iequals(s, "SHIFT")) return Key::Shift;
    if (iequals(s, "CTRL")) return Key::Ctrl;
    return Key::Other;
}

// Windows virtual-key code of a script key name: a letter or digit, ENTER, ESC, TAB, BACKSPACE, F1..F12, 0xNN or vkNN.
int vkOfName(const std::string& s) {
    if (s.size() == 1 && std::isalnum(static_cast<unsigned char>(s[0]))) return std::toupper(static_cast<unsigned char>(s[0]));
    if (iequals(s, "ENTER") || iequals(s, "RETURN")) return 0x0d;
    if (iequals(s, "ESC") || iequals(s, "ESCAPE")) return 0x1b;
    if (iequals(s, "TAB")) return 0x09;
    if (iequals(s, "BACKSPACE")) return 0x08;
    if (iequals(s, "CAPS")) return 0x14;
    if (iequals(s, "PAGEUP") || iequals(s, "PRIOR")) return 0x21;
    if (iequals(s, "PAGEDOWN") || iequals(s, "NEXT")) return 0x22;
    if (s.size() >= 2 && (s[0] == 'F' || s[0] == 'f') && std::isdigit(static_cast<unsigned char>(s[1]))) {
        int n = std::atoi(s.c_str() + 1);
        if (n >= 1 && n <= 12) return 0x6f + n;
    }
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return static_cast<int>(std::strtol(s.c_str(), nullptr, 16));
    return 0;
}

// one real key press + release: S, D and Space go through the engine's own Key (its default action), the rest by code
void pressKey(Engine& e, const std::string& name, int half = 0) {   // half: 1 only the press, 2 only the release
    Key k = parseKey(name);
    int vk = k == Key::Other ? vkOfName(name) : 0;
    if (k == Key::Other && !vk) throw Error("script: bad key '" + name + "'");
    if (half != 2) e.keyDown(k, vk);
    if (half != 1) e.keyUp(k, vk);
}

void writeWav(const std::string& path, const std::vector<int16_t>& pcm, int rate) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw Error("script: cannot write " + path);
    auto w32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) std::fputc(static_cast<int>((v >> (8 * i)) & 255), f); };
    auto w16 = [&](uint32_t v) { for (int i = 0; i < 2; i++) std::fputc(static_cast<int>((v >> (8 * i)) & 255), f); };
    uint32_t bytes = static_cast<uint32_t>(pcm.size() * 2);
    std::fputs("RIFF", f); w32(36 + bytes); std::fputs("WAVEfmt ", f);
    w32(16); w16(1); w16(2); w32(static_cast<uint32_t>(rate)); w32(static_cast<uint32_t>(rate) * 4); w16(4); w16(16);
    std::fputs("data", f); w32(bytes);
    for (int16_t v : pcm) w16(static_cast<uint16_t>(v));
    std::fclose(f);
}


Engine* gEngine = nullptr;   // the engine of the running script (palette files, hex dumps)
int gLastDc = 0, gLastFont = 0;   // the newest device context / logical font the script made: the tokens lastdc / lastfont stand for them

int numberArg(const std::string& s) {
    if (s == "lastdc") return gLastDc;
    if (s == "lastfont") return gLastFont;
    return static_cast<int>(std::strtol(s.c_str(), nullptr, 0));
}

std::string hexOf(const u8* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

std::string paletteHex(const Palette& p) {
    std::vector<u8> b;
    for (const Color& c : p.c) { b.push_back(c.r); b.push_back(c.g); b.push_back(c.b); }
    return hexOf(b.data(), b.size());
}

// 256 x {R,G,B,0}: a .PAL file of BIODATA, or rgb:R,G,B for a uniform palette
std::vector<u8> paletteArg(const std::string& a) {
    std::vector<u8> out(1024, 0);
    if (a.compare(0, 4, "rgb:") == 0) {
        int r = 0, g = 0, b = 0;
        if (std::sscanf(a.c_str() + 4, "%d,%d,%d", &r, &g, &b) != 3) throw Error("script: bad palette '" + a + "'");
        for (size_t i = 0; i < 256; i++) { out[4 * i] = static_cast<u8>(r); out[4 * i + 1] = static_cast<u8>(g); out[4 * i + 2] = static_cast<u8>(b); }
        return out;
    }
    Palette p = Palette::load(gEngine->files().requireData(a));
    for (size_t i = 0; i < 256; i++) { out[4 * i] = p.c[i].r; out[4 * i + 1] = p.c[i].g; out[4 * i + 2] = p.c[i].b; }
    return out;
}

// ---- `api <Name> args...`: one EngineApi command, printed as `api <Name> rc=<code> [out=value ...]` ----
struct ApiArgs {
    std::vector<std::string> v;
    int i(size_t k, int d = 0) const { return k < v.size() ? numberArg(v[k]) : d; }
    bool b(size_t k, bool d = false) const { return k < v.size() ? std::strtol(v[k].c_str(), nullptr, 0) != 0 : d; }
    std::string s(size_t k) const { return k < v.size() && v[k] != "-" ? v[k] : std::string(); }   // "-" = empty string
    std::string rest(size_t k) const {
        std::string r;
        for (size_t j = k; j < v.size(); j++) r += (j > k ? " " : "") + v[j];
        return r;
    }
};
using ApiFn = std::string (*)(EngineApi&, const ApiArgs&);

std::string res(int rc) { return "rc=" + std::to_string(rc); }
template <class T>
std::string res(int rc, const char* key, const T& val) {
    std::string s = res(rc);
    if (rc == 0) {
        std::ostringstream o;
        o << val;
        s += std::string(" ") + key + "=" + (o.str().empty() ? "-" : o.str());
    }
    return s;
}

#define A(name, ...) {#name, [](EngineApi& a, const ApiArgs& g) -> std::string { __VA_ARGS__ }}
const std::map<std::string, ApiFn>& apiTable() {
    static const std::map<std::string, ApiFn> t = {
        A(SetTimerInterval, return res(a.SetTimerInterval(g.i(0)));),
        A(QuitGame, return res(a.QuitGame());),
        // EnableHotspot WINDOW NAME ENABLE [WILDCARD]
        A(EnableHotspot, int prev = -1; int rc = a.EnableHotspot(g.i(0, -1), g.s(1), g.b(2), g.b(3), &prev); return res(rc, "previous", prev);),
        A(EnablePageTimer, return res(a.EnablePageTimer(g.i(0, -1), g.s(1), g.b(2)));),
        // JumpToPage PAGE TRANSITION PUSH
        A(JumpToPage, return res(a.JumpToPage(g.s(0), g.s(1).empty() ? "none" : g.s(1), g.b(2, true), 0, 0, 0, 0));),
        A(DoHotspotAction, return res(a.DoHotspotAction(g.i(0, -1), g.s(1)));),
        A(DoPageTimerAction, return res(a.DoPageTimerAction(g.i(0, -1), g.s(1)));),
        A(ShowMessage, return res(a.ShowMessage(g.rest(0)));),
        A(WriteLog, return res(a.WriteLog(g.rest(0)));),
        A(GetGameObjectCount, int n = 0; int rc = a.GetGameObjectCount(n); return res(rc, "count", n);),
        A(GetGameObjectName, std::string n; int rc = a.GetGameObjectName(g.i(0), n); return res(rc, "name", n);),
        A(GetGameObjectInfo, std::array<int, 12> in{}; int rc = a.GetGameObjectInfo(g.s(0), in);
          std::string s; for (int k = 0; k < 12; k++) s += (k ? "," : "") + std::to_string(in[static_cast<size_t>(k)]);
          return res(rc, "info", s);),
        A(GetDropZoneCount, int n = 0; int rc = a.GetDropZoneCount(n); return res(rc, "count", n);),
        A(GetDropZoneInfo, std::string p, o; int hi = -1; int rc = a.GetDropZoneInfo(g.i(0), p, hi, o);
          return rc ? res(rc) : res(rc) + " page=" + p + " hotspot=" + std::to_string(hi) + " object=" + (o.empty() ? "-" : o);),
        A(GetHotspotNameByIndex, std::string n; int rc = a.GetHotspotNameByIndex(g.i(0, -1), g.i(1), n); return res(rc, "hotspot", n);),
        A(SetGameObjectProperty, return res(a.SetGameObjectProperty(g.s(0), g.s(1), g.i(2)));),
        // OpenSound FILE LOOP NOTIFY FREQ PAN VOLUME
        A(OpenSound, int h = -1; int rc = a.OpenSound(g.s(0), g.i(1), g.b(2, true), g.i(3), g.i(4), g.i(5), h); return res(rc, "handle", h);),
        A(PlaySound, return res(a.PlaySound(g.i(0), g.b(1)));),
        A(StopSound, return res(a.StopSound(g.i(0)));),
        A(IsSoundPlaying, int p = -1; int rc = a.IsSoundPlaying(g.i(0), p); return res(rc, "playing", p);),
        A(SetSoundProperty, return res(a.SetSoundProperty(g.i(0), g.s(1), g.i(2)));),
        A(CloseSound, return res(a.CloseSound(g.i(0)));),
        A(GetPalette, std::vector<u8> pal(1024); int rc = a.GetPalette(pal.data());
          return rc ? res(rc) : res(rc) + " c0=" + std::to_string(pal[0]) + "," + std::to_string(pal[1]) + "," + std::to_string(pal[2]) + " hex=" + hexOf(pal.data(), 1024);),
        // OpenSpriteAnimation FILE WINDOW X Y MODE DELAY NOTIFY LOOP KEEP WAV MAXPLAY SOUNDMODE PAGEREL VISWIDTH ERASE
        A(OpenSpriteAnimation, int h = -1;
          int rc = a.OpenSpriteAnimation(g.s(0), g.i(1, -1), g.i(2), g.i(3), g.i(4, 1), g.i(5), g.b(6, true), g.b(7), g.b(8), g.s(9),
                                         g.i(10), g.i(11), g.b(12), g.i(13, -1), g.b(14), h);
          return res(rc, "handle", h);),
        A(PlaySpriteAnimation, return res(a.PlaySpriteAnimation(g.i(0), g.b(1), g.b(2)));),
        A(StopSpriteAnimation, return res(a.StopSpriteAnimation(g.i(0)));),
        A(IsSpriteAnimationPlaying, int p = -1; int rc = a.IsSpriteAnimationPlaying(g.i(0), p); return res(rc, "playing", p);),
        A(CloseSpriteAnimation, return res(a.CloseSpriteAnimation(g.i(0)));),
        A(GetSpriteAnimationSound, int h = -1; int rc = a.GetSpriteAnimationSound(g.i(0), h); return res(rc, "sound", h);),
        A(GetDropZoneForHotspot, int z = -1; int rc = a.GetDropZoneForHotspot(g.s(0), z); return res(rc, "zone", z);),
        A(RemoveGameObject, return res(a.RemoveGameObject(g.s(0)));),
        A(SetSoundPanToPagePosition, return res(a.SetSoundPanToPagePosition(g.i(0), g.b(1), g.i(2), g.s(3)));),
        A(RedrawPage, return res(a.RedrawPage());),
        A(RedrawScreen, return res(a.RedrawScreen());),
        // OpenAVIMovie FILE X Y [LOOP]
        A(OpenAVIMovie, int h = -1; int rc = a.OpenAVIMovie(g.s(0), -1, g.i(1), g.i(2), true, g.b(3), 0, nullptr, h); return res(rc, "handle", h);),
        A(PlayAVIMovie, return res(a.PlayAVIMovie(g.i(0), g.b(1)));),
        A(StopAVIMovie, return res(a.StopAVIMovie(g.i(0)));),
        A(IsAVIMoviePlaying, int p = -1; int rc = a.IsAVIMoviePlaying(g.i(0), p); return res(rc, "playing", p);),
        A(CloseAVIMovie, return res(a.CloseAVIMovie(g.i(0)));),
        A(SetAVIMovieVolume, return res(a.SetAVIMovieVolume(g.i(0), g.i(1)));),
        A(SetAVIMoviePage, return res(a.SetAVIMoviePage(g.i(0), g.s(1)));),
        A(AviOption41, return res(a.AviOption41(g.i(0)));),
        A(AviOption42, return res(a.AviOption42(g.i(0)));),
        A(PrepareSynthAVIMovie, return res(a.PrepareSynthAVIMovie(g.i(0)));),
        A(AviOption4f, return res(a.AviOption4f(g.i(0), g.i(1), g.i(2)));),
        A(EnableUserInput, return res(a.EnableUserInput(g.b(0), 0));),
        A(GetPanoramaInfo, int w = 0, s = 0, c = 0; int rc = a.GetPanoramaInfo(w, s, c);
          return rc ? res(rc) : res(rc) + " width=" + std::to_string(w) + " start=" + std::to_string(s) + " centre=" + std::to_string(c);),
        A(ChangeGameObjectHoldingImagesGroup, return res(a.ChangeGameObjectHoldingImagesGroup(g.s(0), g.i(1)));),
        // PlaceGameObject SRC EXTRA DST KIND HOLDINDEX GROUP X Y
        A(PlaceGameObject, return res(a.PlaceGameObject(g.s(0), g.s(1), g.s(2), g.s(3), g.i(4), g.i(5), g.i(6), g.i(7)));),
        A(ReturnGameObjectHidden, return res(a.ReturnGameObjectHidden(g.s(0)));),
        A(ChangeGameObjectVariation, return res(a.ChangeGameObjectVariation(g.s(0), g.s(1), g.i(2, -1)));),
        A(ChangeGameObjectVariation1, return res(a.ChangeGameObjectVariation1(g.s(0), g.i(1, -1)));),
        // PlayObjectUseAnimation TAN WAV X Y MODE DELAY HIDEHELD REMOVEHELD [VARIATION [GROUP]]
        A(PlayObjectUseAnimation, return res(a.PlayObjectUseAnimation(g.s(0), g.s(1), g.b(6), g.b(7), g.i(2), g.i(3), g.i(4, 1), g.i(5, 40), g.s(8), g.i(9)));),
        A(DropHeldObject, std::string h = g.s(0); return res(a.DropHeldObject(h.empty() ? nullptr : &h));),
        A(ReturnGameObject, return res(a.ReturnGameObject(g.s(0)));),
        A(DisplayPopupPage, return res(a.DisplayPopupPage(g.s(0)));),
        A(ClosePopupWindow, return res(a.ClosePopupWindow(g.i(0)));),
        A(GetStatusBarObjectUnderCursor, int o = -1; std::string n; int rc = a.GetStatusBarObjectUnderCursor(o, n);
          return rc ? res(rc) : res(rc) + " overbar=" + std::to_string(o) + " object=" + (n.empty() ? "-" : n);),
        A(GetReturnPageName, std::string n; int rc = a.GetReturnPageName(n); return res(rc, "page", n);),
        A(EnableMouseDeltaEvent, return res(a.EnableMouseDeltaEvent(g.b(0)));),
        A(SetScrollMode, return res(a.SetScrollMode(g.b(0)));),
        A(NoOp13, return res(a.NoOp13(g.s(0), g.i(1)));),
        A(NoOp44, return res(a.NoOp44());),
        A(NoOp59, return res(a.NoOp59(g.s(0)));),
        A(Disabled62, return res(a.Disabled62());),
        A(Disabled63, return res(a.Disabled63());),
        // SetPalette PAL STEPS SECONDS
        A(SetPalette, std::vector<u8> p = paletteArg(g.s(0)); return res(a.SetPalette(p.data(), g.i(1), g.i(2)));),
        // FadeToPalette PAL BACK SPEED PERCENT
        A(FadeToPalette, std::vector<u8> p = paletteArg(g.s(0)); return res(a.FadeToPalette(p.data(), g.i(1), g.i(2), g.i(3, 100)));),
        // FadeToColour R G B BACK SPEED KEEP PERCENT
        A(FadeToColour, return res(a.FadeToColour(g.i(0) | g.i(1) << 8 | g.i(2) << 16, g.i(3), g.i(4), g.i(5, -1), g.i(6, 100)));),
        A(ClearWindow, return res(a.ClearWindow(g.i(0, -1), g.i(1)));),
        A(FillScreen, return res(a.FillScreen(g.i(0)));),
        // DisplayImage WIN NAME [X Y REDRAW KEYED WIDTH slot]
        A(DisplayImage, int slot = -2; int rc = a.DisplayImage(g.i(0, -1), g.s(1), g.i(2, -1), g.i(3, -1), g.i(4, 0), g.i(5, 0), g.i(6, -1),
                                                               g.s(7) == "slot" ? &slot : nullptr);
          return slot == -2 ? res(rc) : res(rc, "slot", slot);),
        A(SetImageVisibility, return res(a.SetImageVisibility(g.i(0, -1), g.s(1), g.b(2)));),
        A(ReleasePageImageSlot, return res(a.ReleasePageImageSlot(g.i(0, -1), 0, g.i(1)));),
        A(RaisePageImage, return res(a.RaisePageImage(g.i(0, -1), g.s(1)));),
        // JigglePage REPEATS FIRST LAST DX DY AX AY [DX DY AX AY ...]
        A(JigglePage, std::vector<i32> rec; for (size_t k = 3; k < g.v.size(); k++) rec.push_back(g.i(k));
          return res(a.JigglePage(rec.empty() ? nullptr : rec.data(), static_cast<int>(rec.size() / 4), g.i(0), g.i(1), g.i(2)));),
        A(BeginPageFilter, return res(a.BeginPageFilter(g.i(0), g.i(1), g.i(2)));),
        A(EndPageFilter, return res(a.EndPageFilter());),
        A(SetPageFilterBuffer, static std::vector<u8> buf; buf.assign(static_cast<size_t>(std::max(0, g.i(0))), 0x5a);
          return res(a.SetPageFilterBuffer(buf.empty() ? nullptr : buf.data(), static_cast<int>(buf.size())));),
        // AddRemoveFontResource FILE ADD(1|0): FILE as the DLL sends it (data directory + \fonts\NAME) or relative to BIODATA (fonts\NAME)
        A(AddRemoveFontResource, return res(a.AddRemoveFontResource(g.s(0), g.b(1, true)));),
        // CreateDeleteLogicalFont 1 HEIGHT FACE [WEIGHT [QUALITY]]  (LOGFONT as CEngineFX::CreateLogicalFont fills it) | CreateDeleteLogicalFont 0 FONT
        A(CreateDeleteLogicalFont, int font = g.i(1); int rc;
          if (g.b(0)) {
              u8 lf[60] = {};
              auto put32 = [&](size_t off, int v) { for (size_t k = 0; k < 4; k++) lf[off + k] = static_cast<u8>((static_cast<unsigned>(v) >> (8 * k)) & 255); };
              put32(0, g.i(1)); put32(16, g.i(3, 400));
              lf[23] = 1; lf[26] = static_cast<u8>(g.i(4, 2));
              const std::string face = g.s(2);
              for (size_t k = 0; k < face.size() && k < 31; k++) lf[28 + k] = static_cast<u8>(face[k]);
              font = 0;
              rc = a.CreateDeleteLogicalFont(true, lf, font);
          } else rc = a.CreateDeleteLogicalFont(false, nullptr, font);
          return g.b(0) ? res(rc, "font", font) : res(rc);),
        A(GetWindowDC, int dc = 0, w = 0, h = 0; int rc = a.GetWindowDC(g.i(0, -1), dc, w, h);
          return rc ? res(rc) : res(rc) + " dc=" + std::to_string(dc) + " width=" + std::to_string(w) + " height=" + std::to_string(h);),
        // ReleaseWindowDC WINDOW DC [UNK04 UNK08]
        A(ReleaseWindowDC, return res(a.ReleaseWindowDC(g.i(0, -1), g.i(1), g.i(2, 1), g.i(3, 1)));),
        A(ShowCursor, return res(a.ShowCursor(g.b(0)));),
        // SaveGame NAME... | LoadGame MODE NAME... (MODE 0..3 = fade-in speed, -1 none) | OpenMenu NAME|- [FLAGS [COLOUR]] (COLOUR: the DLL's 73 52 00)
        A(SaveGame, return res(a.SaveGame(g.rest(0)));),
        A(LoadGame, return res(a.LoadGame(g.rest(1), g.i(0, -1)));),
        A(OpenMenu, static const u8 colour[4] = {0x73, 0x52, 0, 0}; return res(a.OpenMenu(g.s(0), g.b(2) ? colour : nullptr, g.i(1)));),
        A(RestartGame, return res(a.RestartGame());),
        A(GetSaveDirectory, std::string d; int rc = a.GetSaveDirectory(d); return res(rc, "directory", d);),
        // BeginTextInput L,T,R,B MAXLEN FILL [R,G,B] | EndTextInput [CANCEL]
        A(BeginTextInput, int r[4] = {0, 0, 0, 0}; u8 c[4] = {0, 0, 0, 0};
          if (std::sscanf(g.s(0).c_str(), "%d,%d,%d,%d", &r[0], &r[1], &r[2], &r[3]) != 4) throw Error("script: BeginTextInput wants L,T,R,B");
          int cr = 0, cg = 0, cb = 0; bool col = g.v.size() > 3 && std::sscanf(g.s(3).c_str(), "%d,%d,%d", &cr, &cg, &cb) == 3;
          c[0] = static_cast<u8>(cr); c[1] = static_cast<u8>(cg); c[2] = static_cast<u8>(cb);
          return res(a.BeginTextInput(r, g.i(1), g.i(2), col ? c : nullptr));),
        A(EndTextInput, return res(a.EndTextInput(g.b(0)));),
    };
    return t;
}
#undef A

// gdi HANDLE OP args...: the Canvas of a DC from `api GetWindowDC` (colours R,G,B = PALETTERGB, i:N = PALETTEINDEX, rgb:R,G,B = RGB)
u32 colourArg(const std::string& a) {
    int r = 0, g = 0, b = 0, i = 0;
    if (std::sscanf(a.c_str(), "i:%d", &i) == 1) return paletteIndex(i);
    if (std::sscanf(a.c_str(), "rgb:%d,%d,%d", &r, &g, &b) == 3) return rgb(r, g, b);
    if (std::sscanf(a.c_str(), "%d,%d,%d", &r, &g, &b) == 3) return paletteRgb(r, g, b);
    throw Error("script: bad colour '" + a + "'");
}

GdiRect rectArg(const std::string& a) {
    GdiRect r;
    if (std::sscanf(a.c_str(), "%d,%d,%d,%d", &r.left, &r.top, &r.right, &r.bottom) != 4) throw Error("script: bad rectangle '" + a + "'");
    return r;
}

// "\n", "\r" and "\\" in the text arguments of the gdi commands (a script line cannot hold a real line break)
std::string unescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == 'n' || s[i + 1] == 'r' || s[i + 1] == '\\')) {
            o += s[i + 1] == 'n' ? '\n' : s[i + 1] == 'r' ? '\r' : '\\';
            i++;
        } else o += s[i];
    }
    return o;
}

void gdiCommand(Engine& e, const std::string& arg) {
    std::istringstream in(arg);
    std::string hs, op;
    in >> hs >> op;
    Canvas* c = e.api().canvas(numberArg(hs));
    if (!c) throw Error("script: gdi: " + hs + " is not an open device context");
    std::string rest;
    std::getline(in, rest);
    rest = trim(rest);
    std::istringstream a(rest);
    std::string t1, t2, t3, t4;
    int x = 0, y = 0;
    std::string out;
    auto num = [](const std::string& s) { return numberArg(s); };
    if (op == "font") { a >> t1; out = "prev=" + std::to_string(c->selectFont(num(t1))); }
    else if (op == "textcolor") { a >> t1; c->setTextColor(colourArg(t1)); out = "index=" + std::to_string(c->mapColor(colourArg(t1))); }
    else if (op == "bkcolor") { a >> t1; c->setBkColor(colourArg(t1)); out = "index=" + std::to_string(c->mapColor(colourArg(t1))); }
    else if (op == "bkmode") { a >> t1; out = "prev=" + std::to_string(c->setBkMode(num(t1))); }
    else if (op == "textalign") { a >> t1; out = "prev=" + std::to_string(c->setTextAlign(static_cast<unsigned>(num(t1)))); }
    else if (op == "pen") { a >> t1 >> t2; Pen p; p.width = num(t1); p.color = colourArg(t2); if (a >> t3) p.style = num(t3); c->selectPen(p); }
    else if (op == "brush") { a >> t1; Brush b; if (t1 == "null") b.null = true; else b.color = colourArg(t1); c->selectBrush(b); }
    else if (op == "textout") {
        a >> x >> y;
        std::string text;
        std::getline(a, text);
        if (!text.empty() && text[0] == ' ') text.erase(0, 1);
        out = std::string(c->textOut(x, y, unescape(text)) ? "ok" : "nofont");
    } else if (op == "extent") {
        GdiSize z = c->textExtent(unescape(rest));
        out = "cx=" + std::to_string(z.cx) + " cy=" + std::to_string(z.cy);
    } else if (op == "drawtext") {       // drawtext L,T,R,B FLAGS TEXT...
        a >> t1 >> t2;
        std::string text;
        std::getline(a, text);
        if (!text.empty() && text[0] == ' ') text.erase(0, 1);
        GdiRect r = rectArg(t1);
        int h = c->drawText(unescape(text), &r, static_cast<unsigned>(num(t2)));
        out = "height=" + std::to_string(h) + " rect=" + std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right) + "," + std::to_string(r.bottom);
    } else if (op == "layout") {         // layout WIDTH FLAGS TEXT...: the lines drawText would print, '|' separated
        a >> t1 >> t2;
        std::string text;
        std::getline(a, text);
        if (!text.empty() && text[0] == ' ') text.erase(0, 1);
        std::string joined;
        text = unescape(text);
        for (const std::string& l : c->layout(text.data(), static_cast<int>(text.size()), num(t1), static_cast<unsigned>(num(t2)))) joined += (joined.empty() ? "" : "|") + l;
        out = "lines=" + joined;
    } else if (op == "moveto") { parseXY(rest, x, y); c->moveTo(x, y); }
    else if (op == "lineto") { parseXY(rest, x, y); c->lineTo(x, y); }
    else if (op == "fillrect") { a >> t1 >> t2; c->fillRect(rectArg(t1), colourArg(t2)); }
    else if (op == "fillsolid") { a >> t1 >> t2; c->fillSolidRect(rectArg(t1), colourArg(t2)); }
    else if (op == "rectangle") { GdiRect r = rectArg(rest); c->rectangle(r.left, r.top, r.right, r.bottom); }
    else if (op == "ellipse") { GdiRect r = rectArg(rest); c->ellipse(r.left, r.top, r.right, r.bottom); }
    else if (op == "pie") {              // pie L,T,R,B XS,YS XE,YE
        a >> t1 >> t2 >> t3;
        GdiRect r = rectArg(t1);
        int xs = 0, ys = 0, xe = 0, ye = 0;
        parseXY(t2, xs, ys);
        parseXY(t3, xe, ye);
        c->pie(r.left, r.top, r.right, r.bottom, xs, ys, xe, ye);
    } else if (op == "pixel") { a >> t1 >> t2; parseXY(t1, x, y); c->setPixel(x, y, colourArg(t2)); }
    else if (op == "map") { a >> t1; out = "index=" + std::to_string(c->mapColor(colourArg(t1))); }
    else throw Error("script: gdi: unknown operation '" + op + "'");
    std::printf("gdi %s %s\n", op.c_str(), out.c_str());
    std::fflush(stdout);
}

} // namespace

void runScript(Engine& e, const std::string& script, ScriptHooks* hooks) {
    gEngine = &e;
    struct Reset { ~Reset() { gEngine = nullptr; } } resetEngine;
    u32 now = 1000;
    int px = 320, py = 240;
    u32 pumpStep = 40;
    std::string shotPrefix;
    int shotN = 0;
    std::vector<int16_t> pending;   // audio mixed during blocking animations, not yet dumped
    const int rate = e.mixer().outRate();
    auto mixFrames = [&](size_t frames) {
        size_t at = pending.size();
        pending.resize(at + frames * 2);
        e.mixer().mix(pending.data() + at, static_cast<int>(frames));
    };
    // host side of the blocking animation loop: one turn = one fixed clock step, audio mixed, frame recorded
    struct PumpGuard {
        Engine& e;
        ~PumpGuard() { e.setPump(nullptr); }
    } guard{e};
    bool pumpTrace = false;
    u32 traceDt = 0;                  // smoothtrace: the display clock step (0 = off)
    PanoCameraSmoother smoother;
    std::vector<u32> panoTex;
    // --widescreen without a window: `wideshot` / `wideanim` run the window's decisions (engine/panowide.h) on the script's frames
    WideController wideCtl;
    std::string wideAnimPrefix;
    int wideAnimScale = 2, wideAnimW = 1280, wideAnimN = 0;
    // One wide capture of the engine's current frame: the picture, its description line. `plainFile`: also the 4:3 smooth capture of the same
    // frame (for the centre comparison). Returns the description.
    CleanOptions cleanOpt;                 // --picture clean in the headless captures: wideshot ... clean, cleanshot, cleansim
    cleanOpt.async = false;
    bool wideClean = false;
    auto wideCapture = [&](const std::string& file, int scale, int outW, bool linear, const std::string& plainFile, const std::string& colsFile = std::string()) -> std::string {
        Frame nf, f;
        const bool statusOp = e.simStatusOpaque();
        const bool filter = e.pageFilterActive() && !statusOp;
        e.setSplitLayers(true);         // the held hand is a layer: the cover is measured without it, it is drawn back at its own place
        if (filter) e.renderNoFilter(nf);
        e.render(f);
        e.setSplitLayers(false);
        const ViewLayers layers = e.viewLayers();
        const Engine::PanoramaFrame& pf = e.panorama();
        const int availL = std::max(0, (outW - Frame::W * scale) / 2), availR = std::max(0, outW - Frame::W * scale - availL);
        std::vector<u32> out;
        char buf[384];
        std::string mode;
        if (!pf.active) {
            wideCtl.reset();
            layers.blitInto(f);
            composeCentred(f, e.view().palette(), scale, outW, out);
            mode = "flat";
            std::snprintf(buf, sizeof buf, "mode=flat left=0 right=0");
        } else {
            WideInput in;
            in.frame = &f;
            in.noFilter = filter ? &nf : nullptr;
            in.view = pf.view;
            in.bare = pf.bare;
            in.cam = pf.camera;
            in.limits.on = pf.limitsOn;
            in.limits.yawMin = pf.limitYawMin;
            in.limits.yawMax = pf.limitYawMax;
            in.scale = scale;
            in.availLeft = availL;
            in.availRight = availR;
            in.keep = statusOp;
            const WideDecision d = wideCtl.decide(in);
            const Frame cover = f;      // without the held image: what the rain is measured on
            layers.blitInto(f);         // the held hand goes back at its own place (the cover was measured without it)
            if (!statusOp || panoTex.empty()) panoTextureAll(*pf.page, *pf.palette, panoTex);     // the status screen up: the textures keep the scene's colours
            std::vector<int16_t> rain;
            long nrain = 0;
            if (filter && d.wide) {
                panoRainMap(cover, nf, pf.view, pf.bare, rain);
                for (int16_t r : rain) nrain += r >= 0;
            }
            WideCanvas cv;
            cv.outW = outW;
            cv.left = availL;
            cv.showLeft = d.wide ? d.showLeft : 0;
            cv.showRight = d.wide ? d.showRight : 0;
            const int unc = wideClean ? composeCleanWideCpu(e, f, *pf.palette, scale, cleanOpt, linear, cv, nrain ? &rain : nullptr, out)
                                      : composeSmoothWide(f, *pf.palette, pf.view, pf.bare, panoTex, pf.camera, scale, linear, cv, nrain ? &rain : nullptr, out, statusOp);
            if (wideClean && statusOp) dimWideSides(out, outW, cv, scale, pf.view);
            mode = d.wide ? "wide" : "4:3";
            // the rain pixels (frame pixels) of the centre and of the two side areas as they are drawn; cols=FILE appends one line per call:
            // the count per frame column from -showLeft/scale to 640 + showRight/scale
            long rainL = 0, rainR = 0;
            std::string cols;
            if (nrain) {
                const int sl = cv.showLeft / scale, sr = cv.showRight / scale;
                for (int x = -sl; x < kViewW + sr; x++) {
                    long n = 0;
                    for (int y = 0; y < kViewH; y++) n += (x >= 0 && x < kViewW ? rain[static_cast<size_t>(y) * kViewW + x] : wideSideRain(rain, x, y)) >= 0;
                    if (x < 0) rainL += n;
                    else if (x >= kViewW) rainR += n;
                    cols += std::to_string(n) + ' ';
                }
            }
            if (!colsFile.empty()) {
                if (std::FILE* fp = std::fopen(colsFile.c_str(), "ab")) {
                    std::fprintf(fp, "%d %s\n", -cv.showLeft / scale, cols.c_str());
                    std::fclose(fp);
                }
            }
            std::snprintf(buf, sizeof buf, "mode=%s left=%d right=%d fov=%.3f cover=%.4f held=%d capped=%d limited=%d rain=%ld rainL=%ld rainR=%ld uncovered=%d yaw=%.6f pitch=%.9f focal=%.9f cfov=%g layer=%d",
                          mode.c_str(), cv.showLeft, cv.showRight, d.fovDeg, d.cover, d.held ? 1 : 0, d.capped ? 1 : 0, d.limited ? 1 : 0, nrain, rainL, rainR, unc,
                          pf.camera.yaw, pf.camera.pitchDeg, pf.camera.focal, pf.camera.fovDeg,
                          layers.held.shown ? 1 : 0);
            if (!plainFile.empty()) {
                std::vector<u32> plain;
                composeSmooth(f, *pf.palette, pf.view, pf.bare, panoTex, pf.camera, scale, linear, plain);
                writeBmp24(plainFile, Frame::W * scale, Frame::H * scale, plain);
            }
        }
        writeBmp24(file, outW, Frame::H * scale, out);
        return buf;
    };
    auto smoothTrace = [&]() {
        Frame tf;
        e.render(tf);
        const Engine::PanoramaFrame& pf = e.panorama();
        if (!pf.active) {
            smoother.reset();
            std::printf("smooth t=%u active=0\n", static_cast<unsigned>(now));
        } else {
            const PanoCamera d = smoother.update(pf.camera, pf.pageSerial, pf.scrollStepTime, now);
            std::printf("smooth t=%u active=1 yaw=%.4f pitch=%.4f ty=%.4f tp=%.4f fov=%g snap=%d serial=%u\n", static_cast<unsigned>(now), d.yaw, d.pitchDeg,
                        smoother.target().yaw, smoother.target().pitchDeg, pf.camera.fovDeg, smoother.snapped() ? 1 : 0, static_cast<unsigned>(pf.pageSerial));
        }
        std::fflush(stdout);
    };
    auto scriptPump = [&](u32& t) {
        if (pumpTrace) {
            std::printf("pump t=%u\n", static_cast<unsigned>(now));
            std::fflush(stdout);
        }
        if (traceDt) smoothTrace();
        if (!wideAnimPrefix.empty()) {
            char suffix[48];
            std::snprintf(suffix, sizeof suffix, "%03d", wideAnimN);
            const std::string line = wideCapture(wideAnimPrefix + "_w" + suffix + ".bmp", wideAnimScale, wideAnimW, true, wideAnimPrefix + "_n" + suffix + ".bmp");
            std::printf("wideanim %d %s\n", wideAnimN++, line.c_str());
            std::fflush(stdout);
        }
        if (!shotPrefix.empty()) {
            Frame f;
            e.render(f);
            char suffix[32];
            std::snprintf(suffix, sizeof suffix, "%03d.bmp", shotN++);
            writeBmp(shotPrefix + suffix, f.toImage(), e.view().palette());
        }
        if (overlayPumpActive()) overlayPumpFrame(e);
        now += pumpStep;
        mixFrames(static_cast<size_t>(pumpStep) * static_cast<size_t>(rate) / 1000);
        t = now;
        return true;
    };
    e.setPump(scriptPump);
    e.update(now);

    // cleansim: the window's --picture clean on the fake clock (see the command)
    std::unique_ptr<CleanFront> cleanSim;
    int cleanSimScale = 2;
    std::vector<u32> simTex;
    u32 simSerial = 0;
    CleanFrameOut simFrame;
    struct SimStats { long frames = 0; double stepMsMax = 0, stepMsSum = 0; } simStats;
    auto cleanSimStep = [&](const Frame* given) {
        Frame own;
        if (!given) e.render(own);
        const Frame& f = given ? *given : own;
        const Engine::PanoramaFrame& pf = e.panorama();
        const auto t0 = std::chrono::steady_clock::now();
        cleanSim->step(e, f, e.view().palette(), pf.active && !e.simStatusOpaque() ? &pf : nullptr, cleanSimScale);     // the status screen up: the page is not given to the cleaner
        // what the window does with the results: apply the bands of the current page to its texture, keep the newest frame
        const int tw = cleanTexW(cleanSim->svc.options().texScale);
        std::vector<CleanBand> up;
        cleanSim->popUploads(e.view().pageSerial(), CleanFront::kUploadBytes, up);
        for (const CleanBand& b : up) {
            if (b.serial != simSerial || simTex.empty()) { simTex.assign(static_cast<size_t>(tw) * cleanTexH(cleanSim->svc.options().texScale), 0); simSerial = b.serial; }
            std::memcpy(&simTex[static_cast<size_t>(b.ty0) * tw], b.px.data(), b.px.size() * 4);
        }
        if (cleanSim->gotFrame) std::swap(simFrame, cleanSim->out);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        simStats.frames++;
        simStats.stepMsSum += ms;
        simStats.stepMsMax = std::max(simStats.stepMsMax, ms);
    };
    // The window's pacing without a window (`window HZ [P]`, tests/test_pacing.sh): the same Pacer as main.cpp's loop and pump, on a
    // fake real clock (the script's `now`) and a fake display of HZ refreshes per second (0: it does not wait for the vertical sync).
    struct WinSim {
        double t = 0, periodMs = 0;
        std::function<void()> onDraw;
        long presents = 0, passes = 0;
        u32 t0 = 0;
        u32 now() const { return static_cast<u32>(t); }
        void idle(u32 ms) { t += ms; }
        bool events() { return true; }
        void draw() {
            onDraw();
            presents++;
            if (periodMs > 0) t = (std::floor(t / periodMs + 1e-6) + 1.0) * periodMs;   // the swap happens at the next vertical sync
        }
    };
    WinSim win;
    std::unique_ptr<Pacer<WinSim>> pacer;
    PanoCameraSmoother winSmoother;
    bool winTrace = false;
    // wshots PREFIX [SCALE]: in window mode every presented frame whose content changed is written as PREFIXnnn.bmp (the 8-bit frame) and,
    // with SCALE, a projected panorama also as PREFIXnnns.bmp through a persistent SDL smooth session (what the window's PanoramaLayer shows
    // after all its incremental page-texture updates); `wshot` lines say when, and whether the persistent picture equals a fresh one.
    std::string wshotPrefix;
    int wshotScale = 0, wshotN = 0;
    uint64_t wshotLast = 0;
    std::unique_ptr<SdlSmoothSession> wshotSession;
    int wregX = 0, wregY = 0, wregW = 0, wregH = 0;   // wregion X,Y,W,H: a rectangle of the page surface, written with every wshot as PREFIXnnn.reg
    u32 tickedAt = now;      // the autosave's play time: the injected time since the last hook call
    auto playTick = [&]() {
        if (hooks && hooks->tick && now > tickedAt) hooks->tick(now - tickedAt);
        tickedAt = now;
    };
    auto advance = [&](u32 ms) {
        if (pacer) {
            const u32 end = now + ms;
            win.t = std::max(win.t, static_cast<double>(now));
            while (win.now() < end) {
                pacer->step([&](u32 t) { win.passes++; e.update(t); }, [] {});
                now = win.now();
                playTick();
            }
            return;
        }
        u32 end = now + ms;
        while (now < end) {
            now = std::min(end, now + (traceDt ? traceDt : 16));
            e.update(now);
            if (traceDt) smoothTrace();
            playTick();
        }
    };
    auto move = [&](int x, int y) {
        e.mouseMove(x, y, x - px, y - py);
        px = x; py = y;
    };
    for (const std::string& raw : split(script, ';')) {
        std::string cmd = trim(raw);
        if (cmd.empty()) continue;
        std::istringstream in(cmd);
        std::string op, arg;
        in >> op;
        std::getline(in, arg);
        arg = trim(arg);
        int x = 0, y = 0;
        if (scriptTextCommand(e, op, arg)) {
        } else if (scriptOverlayCommand(e, op, arg)) {
        } else if (op == "page") {
            char* end = nullptr;
            long n = std::strtol(arg.c_str(), &end, 10);
            int page = (!arg.empty() && !*end) ? static_cast<int>(n) : e.view().findPage(arg);
            if (!page) throw Error("script: unknown page '" + arg + "'");
            e.goToPage(page, true);
        } else if (op == "scroll") {
            parseXY(arg, x, y);
            e.setScroll(x, y);
        } else if (op == "move") {
            parseXY(arg, x, y);
            move(x, y);
        } else if (op == "click" || op == "rclick") {
            parseXY(arg, x, y);
            move(x, y);
            Button b = op == "click" ? Button::Left : Button::Right;
            e.mouseDown(b);
            e.mouseUp(b);
        } else if (op == "key") {
            pressKey(e, arg);
        } else if (op == "postmouse") {    // postmouse X,Y: a WM_MOUSEMOVE posted to the main window (client position; the view centre is 320,240)
            parseXY(arg, x, y);
            e.postMouseMove(x, y);
        } else if (op == "verb") {         // verb V A0 A1 ...: a hotspot action of verb V on the current page, prints `verb rc=<code>`
            std::istringstream a(arg);
            int v = 0, n = 0, args[8] = {};
            a >> v;
            while (n < 8 && a >> args[n]) n++;
            std::printf("verb rc=%d\n", e.debugVerb(v, args, n));
            std::fflush(stdout);
        } else if (op == "keydown" || op == "keyup") {   // keydown KEY / keyup KEY: one half of `key` (a key held while other input goes on)
            pressKey(e, arg, op == "keydown" ? 1 : 2);
        } else if (op == "type") {         // type TEXT: one key press + release per character ("\n" = Enter)
            for (size_t i = 0; i < arg.size(); i++) {
                std::string name(1, arg[i]);
                if (arg[i] == '\\' && i + 1 < arg.size() && arg[i + 1] == 'n') { name = "ENTER"; i++; }
                else if (arg[i] == ' ') name = "SPACE";
                pressKey(e, name);
            }
        } else if (op == "advance") {
            advance(static_cast<u32>(std::strtoul(arg.c_str(), nullptr, 10)));
        } else if (op == "shot") {
            Frame f;
            e.render(f);
            writeBmp(arg, f.toImage(), e.view().palette());
        } else if (op == "framehash") {    // framehash: FNV-1a of the rendered frame (indices) and the palette, to find frames that change
            Frame f;
            e.render(f);
            uint64_t h = 1469598103934665603ull;
            for (u8 v : f.px) h = (h ^ v) * 1099511628211ull;
            for (const Color& c : e.view().palette().c) h = (((h ^ c.r) * 1099511628211ull) ^ c.g) * 1099511628211ull ^ c.b;
            std::printf("framehash t=%u %016llx\n", static_cast<unsigned>(pacer ? win.now() : now), static_cast<unsigned long long>(h));
            std::fflush(stdout);
        } else if (op == "heldlayer") {    // heldlayer: the held image as a layer (Engine::viewLayers): the frame without it plus the layer against the normal frame
            Frame normal, split;
            e.render(normal);
            e.setSplitLayers(true);
            e.render(split);
            e.setSplitLayers(false);
            const ViewLayers L = e.viewLayers();
            Frame back = split;
            L.blitInto(back);
            std::printf("heldlayer shown=%d equal=%d x=%d y=%d w=%d h=%d key=%d differs=%d opaque=%ld\n", L.held.shown ? 1 : 0, back.px == normal.px ? 1 : 0, L.held.x, L.held.y,
                        L.held.image.w, L.held.image.h, L.held.transparent, split.px != normal.px ? 1 : 0,
                        static_cast<long>(std::count_if(L.held.image.px.begin(), L.held.image.px.end(), [&](u8 v) { return static_cast<int>(v) != L.held.transparent; })));
            std::fflush(stdout);
        } else if (op == "panoinfo") {
            Frame f;
            e.render(f);
            const Engine::PanoramaFrame& pf = e.panorama();
            std::printf("panoinfo active=%d view=%d,%d,%d,%d yaw=%.6f pitch=%.9f fov=%g focal=%.9f serial=%u step=%u now=%u limits=%d,%g,%g\n", pf.active ? 1 : 0, pf.view.x, pf.view.y,
                        pf.view.w, pf.view.h, pf.camera.yaw, pf.camera.pitchDeg, pf.camera.fovDeg, pf.camera.focal, static_cast<unsigned>(pf.pageSerial),
                        static_cast<unsigned>(pf.scrollStepTime), static_cast<unsigned>(pf.now), pf.limitsOn ? 1 : 0, pf.limitYawMin, pf.limitYawMax);
            std::fflush(stdout);
        } else if (op == "smoothshot") {
            std::istringstream a(arg);
            std::string file, mode, tok, frameFile, bareFile;
            int scale = 2;
            a >> file >> scale;
            while (a >> tok) {   // nearest | sdl | frame=FILE.bmp | bare=FILE.bmp (the 8-bit frame / bare projection of this very render)
                if (tok.rfind("frame=", 0) == 0) frameFile = tok.substr(6);
                else if (tok.rfind("bare=", 0) == 0) bareFile = tok.substr(5);
                else mode = tok;
            }
            if (file.empty() || scale < 1 || scale > 8 || (!mode.empty() && mode != "nearest" && mode != "sdl"))
                throw Error("script: smoothshot FILE SCALE [nearest|sdl] [frame=FILE.bmp] [bare=FILE.bmp]");
            Frame f;
            e.render(f);
            const Engine::PanoramaFrame& pf = e.panorama();
            if (!pf.active) throw Error("script: smoothshot: the frame shows no projected panorama");
            std::vector<u32> out;
            int uncovered = 0;
            if (mode == "sdl") {
                std::string err;
                if (!hooks || !hooks->smoothCaptureSdl || !hooks->smoothCaptureSdl(pf, f, scale, out, err)) throw Error("script: smoothshot sdl: " + err);
            } else {
                panoTextureAll(*pf.page, *pf.palette, panoTex);
                uncovered = composeSmooth(f, *pf.palette, pf.view, pf.bare, panoTex, pf.camera, scale, mode != "nearest", out);
            }
            writeBmp24(file, Frame::W * scale, Frame::H * scale, out);
            if (!frameFile.empty()) writeBmp(frameFile, f.toImage(), *pf.palette);
            if (!bareFile.empty()) {
                Image8 img;
                img.w = kViewW;
                img.h = kViewH;
                img.px.assign(pf.bare, pf.bare + static_cast<size_t>(kViewW) * kViewH);
                writeBmp(bareFile, img, *pf.palette);
            }
            std::printf("smoothshot %s %d%s uncovered=%d\n", file.c_str(), scale, mode.empty() ? "" : (" " + mode).c_str(), uncovered);
            std::fflush(stdout);
        } else if (op == "wideshot") {     // wideshot FILE SCALE WIDTH [nearest] [plain=FILE.bmp] [cols=FILE]: the --widescreen picture of this frame (one call = one frame of the hysteresis)
            std::istringstream a(arg);
            std::string file, tok, plain, cols;
            int scale = 2, width = 0;
            bool nearest = false;
            wideClean = false;
            a >> file >> scale >> width;
            while (a >> tok) {
                if (tok == "nearest") nearest = true;
                else if (tok == "clean") wideClean = true;
                else if (tok.rfind("tex=", 0) == 0) cleanOpt.texScale = std::atoi(tok.c_str() + 4);
                else if (tok.rfind("slew=", 0) == 0) cleanOpt.slew = std::atoi(tok.c_str() + 5);
                else if (tok.rfind("plain=", 0) == 0) plain = tok.substr(6);
                else if (tok.rfind("cols=", 0) == 0) cols = tok.substr(5);
            }
            if (file.empty() || scale < 1 || scale > 8 || width < Frame::W * scale || width > 8192)
                throw Error("script: wideshot FILE SCALE WIDTH [nearest] [plain=FILE.bmp] (WIDTH >= 640 * SCALE)");
            const std::string line = wideCapture(file, scale, width, !nearest, plain, cols);
            wideClean = false;
            std::printf("wideshot %s %d %d %s\n", file.c_str(), scale, width, line.c_str());
            std::fflush(stdout);
        } else if (op == "wideanim") {     // wideanim PREFIX SCALE WIDTH | off: in every frame of the blocking animations that follow, PREFIX_wNNN.bmp (wide) and PREFIX_nNNN.bmp (4:3 smooth)
            if (arg == "off") { wideAnimPrefix.clear(); }
            else {
                std::istringstream a(arg);
                a >> wideAnimPrefix >> wideAnimScale >> wideAnimW;
                if (wideAnimPrefix.empty() || wideAnimScale < 1 || wideAnimScale > 8 || wideAnimW < Frame::W * wideAnimScale)
                    throw Error("script: wideanim PREFIX SCALE WIDTH | off");
                wideAnimN = 0;
            }
        } else if (op == "barebmp") {
            Frame f;
            e.render(f);
            const Engine::PanoramaFrame& pf = e.panorama();
            if (!pf.active) throw Error("script: barebmp: the frame shows no projected panorama");
            Image8 img;
            img.w = kViewW;
            img.h = kViewH;
            img.px.assign(pf.bare, pf.bare + static_cast<size_t>(kViewW) * kViewH);
            writeBmp(arg, img, *pf.palette);
        } else if (op == "smoothtrace") {
            traceDt = arg == "off" ? 0 : static_cast<u32>(std::strtoul(arg.c_str(), nullptr, 10));
            smoother.reset();
        } else if (op == "surfdump") {     // surfdump FILE: the raw page surface (indices, row-major) the view is cut / projected from
            const Surface& s = e.view().surface();
            std::FILE* fp = std::fopen(arg.c_str(), "wb");
            if (!fp) throw Error("script: cannot write " + arg);
            std::fwrite(s.px.data(), 1, s.px.size(), fp);
            std::fclose(fp);
            std::printf("surfdump %d %d\n", s.w, s.h);
            std::fflush(stdout);
        } else if (op == "dumppal" || op == "daypal") {   // dumppal FILE: the display palette (768 bytes R,G,B); daypal FILE: the page's day palette (768 bytes; an empty file without one)
            Palette pl = e.view().palette();
            if (op == "daypal") {
                Engine::CleanInfo ci;
                e.cleanInfo(ci);
                if (ci.day) pl = *ci.day;
                else pl = Palette();
            }
            std::vector<u8> raw(768);
            for (int i = 0; i < 256; i++) { raw[3 * i] = pl.c[i].r; raw[3 * i + 1] = pl.c[i].g; raw[3 * i + 2] = pl.c[i].b; }
            Engine::CleanInfo noday;
            e.cleanInfo(noday);
            if (op == "daypal" && !noday.day) raw.clear();   // (an empty file: this page has no day palette)
            std::FILE* fp = std::fopen(arg.c_str(), "wb");
            if (!fp) throw Error("script: cannot write " + arg);
            std::fwrite(raw.data(), 1, raw.size(), fp);
            std::fclose(fp);
        } else if (op == "cleanshot") {    // cleanshot FILE SCALE [tex=1|2] [slew=N] [nearest] [plain=FILE]: the --picture clean frame at SCALE (1..4), 24-bit BMP; the sharp text on top when `sharptext on`
            std::istringstream a(arg);
            std::string file, tok, plain, smoothPlain;
            int scale = 2;
            bool linear = true;
            CleanOptions co = cleanOpt;
            a >> file >> scale;
            while (a >> tok) {
                if (tok == "nearest") linear = false;
                else if (tok.rfind("tex=", 0) == 0) co.texScale = std::atoi(tok.c_str() + 4);
                else if (tok.rfind("slew=", 0) == 0) co.slew = std::atoi(tok.c_str() + 5);
                else if (tok.rfind("plain=", 0) == 0) plain = tok.substr(6);
                else if (tok.rfind("smooth=", 0) == 0) smoothPlain = tok.substr(7);
            }
            if (file.empty() || scale < 1 || scale > 4 || (co.texScale != 1 && co.texScale != 2)) throw Error("script: cleanshot FILE SCALE(1..4) [tex=1|2] [slew=N] [nearest]");
            Engine::TextFrame tf;
            e.renderText(tf);
            std::vector<u32> rgb, withText;
            std::string notes;
            composeCleanCpu(e, tf.free, tf.palette, scale, co, linear, rgb, &notes);
            if (e.textTracking()) {
                composeSharpText(tf, scale, withText, &rgb);
                rgb.swap(withText);
            }
            writeBmp24(file, Frame::W * scale, Frame::H * scale, rgb);
            if (!plain.empty()) {   // the same frame as the default window draws it: the 8-bit frame enlarged in blocks (text and all)
                std::vector<u32> pl(static_cast<size_t>(Frame::W) * scale * Frame::H * scale);
                for (int Y = 0; Y < Frame::H * scale; Y++)
                    for (int X = 0; X < Frame::W * scale; X++) {
                        const Color& c = tf.palette.c[tf.full.px[static_cast<size_t>(Y / scale) * Frame::W + X / scale]];
                        pl[static_cast<size_t>(Y) * Frame::W * scale + X] = (static_cast<u32>(c.r) << 16) | (static_cast<u32>(c.g) << 8) | c.b;
                    }
                writeBmp24(plain, Frame::W * scale, Frame::H * scale, pl);
            }
            if (!smoothPlain.empty()) {   // --panorama smooth --text sharp without --picture clean
                std::vector<u32> pl;
                if (e.panorama().active) {
                    const Engine::PanoramaFrame& pf = e.panorama();
                    panoTextureAll(*pf.page, *pf.palette, panoTex);
                    composeSmooth(tf.free, *pf.palette, pf.view, pf.bare, panoTex, pf.camera, scale, linear, pl);
                } else {
                    pl.assign(static_cast<size_t>(Frame::W) * scale * Frame::H * scale, 0);
                    for (int Y = 0; Y < Frame::H * scale; Y++)
                        for (int X = 0; X < Frame::W * scale; X++) {
                            const Color& c = tf.palette.c[tf.free.px[static_cast<size_t>(Y / scale) * Frame::W + X / scale]];
                            pl[static_cast<size_t>(Y) * Frame::W * scale + X] = (static_cast<u32>(c.r) << 16) | (static_cast<u32>(c.g) << 8) | c.b;
                        }
                }
                std::vector<u32> pt;
                if (e.textTracking()) { composeSharpText(tf, scale, pt, &pl); pl.swap(pt); }
                writeBmp24(smoothPlain, Frame::W * scale, Frame::H * scale, pl);
            }
            std::printf("cleanshot %s %d tex=%d slew=%d %s\n", file.c_str(), scale, co.texScale, co.slew, notes.c_str());
            std::fflush(stdout);
        } else if (op == "cleansim") {     // cleansim on [tex=N] [slew=N] [scale=N] | off | wait | stats: the window's cleaner (worker threads) in the fake-clock `window` loop
            std::istringstream a(arg);
            std::string first, tok;
            a >> first;
            if (first == "on") {
                CleanOptions co = cleanOpt;
                co.async = true;
                cleanSimScale = 2;
                while (a >> tok) {
                    if (tok.rfind("tex=", 0) == 0) co.texScale = std::atoi(tok.c_str() + 4);
                    else if (tok.rfind("slew=", 0) == 0) co.slew = std::atoi(tok.c_str() + 5);
                    else if (tok.rfind("scale=", 0) == 0) cleanSimScale = std::atoi(tok.c_str() + 6);
                }
                cleanSim.reset(new CleanFront(co));
                simTex.clear();
                simSerial = 0;
                simStats = SimStats();
            } else if (first == "off") {
                cleanSim.reset();
            } else if (first == "wait") {
                if (!cleanSim) throw Error("script: cleansim is off");
                cleanSim->svc.waitIdle();
                for (int i = 0; i < 16; i++) cleanSimStep(nullptr);     // takes what finished (the uploads are budgeted per step)
                simStats.stepMsMax = 0;
            } else if (first == "stats") {
                if (!cleanSim) throw Error("script: cleansim is off");
                const CleanService::Stats st = cleanSim->svc.stats();
                std::printf("cleansim frames=%ld stepMsMax=%.3f stepMsSum=%.3f serviceMainMsMax=%.3f pageJobs=%ld pageMsMax=%.1f frameJobs=%ld frameMsMax=%.1f framesDropped=%ld bands=%ld\n", simStats.frames,
                            simStats.stepMsMax, simStats.stepMsSum, st.mainMsMax, st.pageJobs, st.pageMsMax, st.frameJobs, st.frameMsMax, st.framesDropped, st.pageBands);
                std::fflush(stdout);
                simStats.stepMsMax = 0;
            } else if (first == "check") {   // cleansim check: the texture and the frame the cleaner delivered against a fresh computation (slew=0 makes them equal)
                if (!cleanSim) throw Error("script: cleansim is off");
                cleanSim->svc.waitIdle();
                for (int i = 0; i < 16; i++) cleanSimStep(nullptr);
                Frame f;
                e.render(f);
                const Engine::PanoramaFrame& pf = e.panorama();
                long texDiff = -1, frameDiff = -1;
                if (pf.active) {
                    std::vector<u32> fresh;
                    Engine::CleanInfo ci;
                    e.cleanInfo(ci);
                    const CleanOptions& co = cleanSim->svc.options();
                    cleanPageTexture(*pf.page, *pf.palette, ci.day, co.texScale, co.slew, fresh);
                    texDiff = simTex.size() != fresh.size() ? static_cast<long>(fresh.size()) : 0;
                    if (!texDiff) for (size_t i = 0; i < fresh.size(); i++) texDiff += simTex[i] != fresh[i];
                }
                CleanOptions so = cleanSim->svc.options();
                so.async = false;
                CleanFront ref(so);
                ref.step(e, f, e.view().palette(), pf.active ? &pf : nullptr, cleanSimScale);
                if (ref.gotFrame) {
                    frameDiff = simFrame.argb.size() != ref.out.argb.size() ? static_cast<long>(ref.out.argb.size()) : 0;
                    if (!frameDiff) for (size_t i = 0; i < ref.out.argb.size(); i++) frameDiff += simFrame.argb[i] != ref.out.argb[i];
                }
                std::printf("cleansim check texdiff=%ld framediff=%ld\n", texDiff, frameDiff);
                std::fflush(stdout);
            } else {
                throw Error("script: cleansim on|off|wait|stats|check");
            }
        } else if (op == "inv") {          // inv SX,SY [SX,SY ...]: screen point -> page point (the projection's inverse on 2560 x 1024 pages)
            std::istringstream a(arg);
            std::string tok;
            while (a >> tok) {
                parseXY(tok, x, y);
                int pxx = -1, pyy = -1;
                bool ok = e.view().viewToPage(x, y, pxx, pyy);
                std::printf("inv %d,%d -> %d,%d%s\n", x, y, pxx, pyy, ok ? "" : " (outside)");
            }
            std::fflush(stdout);
        } else if (op == "clickp" || op == "movep" || op == "rclickp") {   // clickp PX,PY / movep PX,PY: the view point that shows page point (PX,PY) (nearest by the projection's inverse)
            parseXY(arg, x, y);
            const int top = e.view().viewTop();
            int bx = -1, by = -1;
            long bd = -1;
            for (int yy = top; yy < top + PageView::ViewH; yy++)
                for (int xx = 0; xx < 640; xx++) {
                    int qx, qy;
                    if (!e.view().viewToPage(xx, yy, qx, qy)) continue;
                    int dx = qx - x;
                    if (e.view().panorama()) dx = ((dx % 2560) + 3840) % 2560 - 1280;
                    const long d = static_cast<long>(dx) * dx + static_cast<long>(qy - y) * (qy - y);
                    if (bd < 0 || d < bd) { bd = d; bx = xx; by = yy; }
                }
            if (bd < 0) throw Error("script: " + op + ": no view");
            move(bx, by);
            if (op == "clickp") { e.mouseDown(Button::Left); e.mouseUp(Button::Left); }
            if (op == "rclickp") { e.mouseDown(Button::Right); e.mouseUp(Button::Right); }
            std::printf("%s %d,%d -> screen %d,%d (page distance^2 %ld)\n", op.c_str(), x, y, bx, by, bd);
            std::fflush(stdout);
        } else if (op == "clickhs") {      // clickhs NAME: click a screen point that shows the (enabled) hotspot NAME of the main page
            bool found = false;
            const int top = e.view().viewTop();
            for (int yy = top; yy < top + PageView::ViewH && !found; yy += 2)
                for (int xx = 0; xx < 640 && !found; xx += 2) {
                    HitInfo h = e.view().hit(xx, yy);
                    if (h && iequals(h.hotspot->name, arg)) {
                        move(xx, yy);
                        e.mouseDown(Button::Left);
                        e.mouseUp(Button::Left);
                        std::printf("clickhs %s at %d,%d\n", arg.c_str(), xx, yy);
                        found = true;
                    }
                }
            if (!found) {                  // why: no hotspot of that name, switched off, or switched on but not under any point of the view
                const char* why = "no such hotspot";
                if (e.view().pageNumber()) {
                    const Page& pg = e.view().page();
                    for (size_t i = 0; i < pg.hotspots.size(); i++)
                        if (iequals(pg.hotspots[i].name, arg)) {
                            const bool on = e.view().hit((pg.hotspots[i].left + pg.hotspots[i].right) / 2 - e.view().scrollX(),
                                                         (pg.hotspots[i].top + pg.hotspots[i].bottom) / 2 - e.view().scrollY() + top).hotspot != nullptr;
                            why = e.view().hotspotEnabled(pg, static_cast<int>(i)) ? (on ? "covered" : "enabled, not in view") : "disabled";
                            if (e.view().hotspotEnabled(pg, static_cast<int>(i))) break;
                        }
                }
                std::printf("clickhs %s not found (%s)\n", arg.c_str(), why);
            }
            std::fflush(stdout);
        } else if (op == "bs4hs" || op == "bs4move") {   // bs4hs NAME: click the (enabled) hotspot NAME of the BioSys 4 terminal's main or panel page; bs4move NAME: only move there
            Bs4* b = e.bs4();
            if (!b) throw Error("script: " + op + ": the BioSys 4 terminal is not up");
            // the point where the hit test of exe:0041c580 (main page first, then the panel; last hotspot on top) finds the hotspot `arg`
            auto topmost = [](const Bs4Page& pg, int px, int py) -> const Hotspot* {
                for (size_t i = pg.rec.hotspots.size(); i-- > 0;) {
                    const Hotspot& h = pg.rec.hotspots[i];
                    if (!(h.flags & 1) && px >= h.left && px <= h.right && py >= h.top && py <= h.bottom) return &h;
                }
                return nullptr;
            };
            bool found = false;
            for (int layer = 0; layer < 2 && !found; layer++) {
                const Bs4Page& pg = layer == 0 ? b->mainPage() : b->panelPage();
                if (layer == 1 && !b->panelActive()) break;
                const int ox = layer == 0 ? 0 : b->panelRect().left, oy = layer == 0 ? 0 : b->panelRect().top;
                for (const Hotspot& h : pg.rec.hotspots) {
                    if ((h.flags & 1) || !iequals(h.name, arg)) continue;
                    for (int yy = h.top; yy <= h.bottom && !found; yy++)
                        for (int xx = h.left; xx <= h.right && !found; xx++) {
                            const Hotspot* t = topmost(pg, xx, yy);
                            if (!t || !iequals(t->name, arg)) continue;
                            if (layer == 1 && topmost(b->mainPage(), xx + ox, yy + oy)) continue;   // the main page takes the point first
                            move(xx + ox, yy + oy);
                            if (op == "bs4hs") { e.mouseDown(Button::Left); e.mouseUp(Button::Left); }
                            std::printf("%s %s at %d,%d (%s)\n", op.c_str(), arg.c_str(), xx + ox, yy + oy, layer == 0 ? "main" : "panel");
                            found = true;
                        }
                }
            }
            if (!found) std::printf("%s %s not found\n", op.c_str(), arg.c_str());
            std::fflush(stdout);
        } else if (op == "bs4loop") {      // bs4loop N on|off: the terminal's loop sound N (1..7: exe:0040c4b0 StartLoop / exe:0040c660 StopLoop)
            Bs4* b = e.bs4();
            if (!b) throw Error("script: bs4loop: the BioSys 4 terminal is not up");
            std::istringstream a(arg);
            int n = 0;
            std::string state;
            a >> n >> state;
            if (state == "on") b->loopStart(n); else b->loopStop(n);
        } else if (op == "bs4trace") {     // bs4trace on|off: print what the terminal asks of the engine (sounds, enter / leave) as `event bs4...` lines
            if (arg == "on")
                e.bs4Trace = [](const std::string& s) { std::printf("event %s\n", s.c_str()); std::fflush(stdout); };
            else
                e.bs4Trace = nullptr;
        } else if (op == "dump") {
            std::fputs(e.dump(arg).c_str(), stdout);
            std::fflush(stdout);
        } else if (op == "slot") {
            e.setTimeSlot(parseSlotName(arg));
        } else if (op == "hotspots") {
            e.setOutlines(arg == "on");
        } else if (op == "hidden") {
            e.setShowHidden(arg == "on");
        } else if (op == "newgame") {
            e.startNewGame();
        } else if (op == "capture") {      // pointer capture on/off (what the live front end enables)
            e.setPointerCapture(arg == "on");
        } else if (op == "vrelease") {     // vrelease: `vrelease released=0|1 sy=<scroll y> py=<the pointer's row>` (the vertical release of the captured pointer, DAT_004990cc)
            int px = 0, py = 0;
            e.pointerPos(px, py);
            std::printf("vrelease released=%d sy=%d py=%d\n", e.verticalRelease() ? 1 : 0, e.view().scrollY(), py);
        } else if (op == "swapped") {      // swapped on|off: GetSystemMetrics(SM_SWAPBUTTON), the left-handed help page of the menu
            e.setMouseButtonsSwapped(arg == "on");
        } else if (op == "var") {          // var OBJECT VARIATION: API 0x46
            std::istringstream a(arg);
            std::string obj, var;
            a >> obj >> var;
            std::printf("api %d\n", e.objects().apiChangeVariation(obj, var, 0xffffffffu));
            std::fflush(stdout);
        } else if (op == "remove") {       // remove OBJECT: API 0x26
            std::printf("api %d\n", e.objects().apiRemoveObject(arg));
            std::fflush(stdout);
        } else if (op == "hold") {         // hold OBJECT: the object into the hand (tests; GameObjects::debugHold)
            std::printf("hold %s %d\n", arg.c_str(), e.objects().debugHold(arg));
            std::fflush(stdout);
        } else if (op == "returnobj") {    // returnobj OBJECT: API 0x52
            std::printf("api %d\n", e.objects().apiReturnObject(arg));
            std::fflush(stdout);
        } else if (op == "sound") {        // sound FILE [FLAGS]
            std::istringstream a(arg);
            std::string file;
            int flags = 0;
            a >> file >> flags;
            std::printf("sound handle %d\n", e.sounds().playWave(file, flags));
            std::fflush(stdout);
        } else if (op == "sprite") {       // sprite TAN X Y MODE DELAYMS [fixed|page] [loop]
            std::istringstream a(arg);
            SpriteOpenParams p;
            std::string kind, opt;
            a >> p.file >> p.x >> p.y >> p.mode >> p.delayMs >> kind >> opt;
            p.pageRelative = iequals(kind, "page");
            p.loop = iequals(kind, "loop") || iequals(opt, "loop");
            int h = -1;
            int rc = e.sprites().open(p, h);
            if (rc == kOk) { e.sprites().play(h, true, e.now()); e.update(now); }
            std::printf("sprite handle %d rc %d\n", h, rc);
            std::fflush(stdout);
        } else if (op == "movie") {        // movie AVI X Y [PALETTE]
            std::istringstream a(arg);
            MovieOpenParams p;
            std::string pal;
            a >> p.file >> p.x >> p.y >> pal;
            if (!pal.empty()) {
                p.palette = Palette::load(e.files().requireData(pal));
                p.hasPalette = true;
            }
            int h = -1;
            int rc = e.movies().open(p, h);
            if (rc == kOk) rc = e.movies().play(h, true, e.now());
            if (rc == kOk) e.update(now);
            std::printf("movie handle %d rc %d\n", h, rc);
            std::fflush(stdout);
        } else if (op == "dbgmovie") {     // dbgmovie AVI [PALETTE]: same path as --movie and the M key
            std::istringstream a(arg);
            std::string file, pal;
            a >> file >> pal;
            int h = e.debugPlayMovie(file, pal);
            e.update(now);
            std::printf("dbgmovie handle %d\n", h);
            std::fflush(stdout);
        } else if (op == "useanim") {      // useanim TAN WAV|- X Y MODE DELAYMS HIDEHELD REMOVEHELD: API 0x47
            std::istringstream a(arg);
            std::string tan, wav;
            int mode = 1, delay = 40, hide = 0, rem = 0;
            a >> tan >> wav >> x >> y >> mode >> delay >> hide >> rem;
            if (wav == "-") wav.clear();
            std::printf("api %d\n", e.objects().apiPlayUseAnimation(tan, wav, x, y, mode, delay, hide != 0, rem != 0, "", 0));
            std::fflush(stdout);
        } else if (op == "api") {          // api NAME args...: a command through the EngineApi interface
            std::istringstream a(arg);
            ApiArgs ar;
            std::string name, tok;
            a >> name;
            while (a >> tok) ar.v.push_back(tok);
            auto it = apiTable().find(name);
            if (it == apiTable().end()) throw Error("script: no scripted API command '" + name + "'");
            std::string out = it->second(e.api(), ar);
            for (const char* key : {" dc=", " font="}) {
                size_t at = out.find(key);
                if (at != std::string::npos) (key[1] == 'd' ? gLastDc : gLastFont) = std::atoi(out.c_str() + at + std::strlen(key));
            }
            e.update(now);
            std::printf("api %s %s\n", name.c_str(), out.c_str());
            std::fflush(stdout);
        } else if (op == "gdi") {          // gdi HANDLE OP args...: draw on the DC of `api GetWindowDC` (see gdiCommand)
            gdiCommand(e, arg);
        } else if (op == "fontinfo") {     // fontinfo FONT: what API 0x39 made (ppem, GDI cell metrics, matched face)
            const Font* f = e.fonts().font(numberArg(arg));
            if (!f) throw Error("script: fontinfo: unknown font " + arg);
            std::printf("fontinfo family=%s ppem=%d ascent=%d descent=%d height=%d\n", f->family().c_str(), f->ppem(), f->ascent(), f->descent(), f->height());
            std::fflush(stdout);
        } else if (op == "faces") {        // faces: the registered font files
            for (const FaceInfo& fi : e.fonts().faces())
                std::printf("face family=%s style=%s upm=%d winascent=%d windescent=%d\n", fi.family.c_str(), fi.style.c_str(), fi.unitsPerEm, fi.winAscent, fi.winDescent);
            std::fflush(stdout);
        } else if (op == "apistats") {     // apistats CODE...: calls / not-implemented calls of the commands
            std::istringstream a(arg);
            std::string tok;
            while (a >> tok) {
                int code = static_cast<int>(std::strtol(tok.c_str(), nullptr, 0));
                std::printf("apistats 0x%02x calls=%d notimpl=%d\n", code, e.apiCalls(code), e.apiNotImplementedCalls(code));
            }
            std::fflush(stdout);
        } else if (op == "activate") {     // activate on|off: the app gains / loses the focus
            e.setMediaPaused(arg != "on");
        } else if (op == "stopall") {
            e.stopAllMedia();
        } else if (op == "pumpstep") {
            pumpStep = static_cast<u32>(std::max(1L, std::strtol(arg.c_str(), nullptr, 10)));
        } else if (op == "animshots") {
            shotPrefix = arg;
            shotN = 0;
        } else if (op == "mixdump") {      // mixdump OUT.wav MS
            std::istringstream a(arg);
            std::string out;
            long ms = 0;
            a >> out >> ms;
            u32 end = now + static_cast<u32>(std::max(0L, ms));
            size_t done = 0, total = static_cast<size_t>(std::max(0L, ms)) * static_cast<size_t>(rate) / 1000;
            while (now < end) {
                now = std::min(end, now + 16);
                size_t target = total * (now - (end - static_cast<u32>(ms))) / static_cast<size_t>(ms);
                mixFrames(target - done);
                done = target;
                e.update(now);
            }
            writeWav(out, pending, rate);
            pending.clear();
        } else if (op == "pumptrace") {
            pumpTrace = arg == "on";
        } else if (op == "gamelogic") {
            e.setGameLogicAttached(arg == "on");
        } else if (op == "palettetrace") {
            if (arg == "on")
                e.paletteTrace = [](const Palette& p, u32 t) {
                    std::printf("palette t=%u %s\n", static_cast<unsigned>(t), paletteHex(p).c_str());
                    std::fflush(stdout);
                };
            else
                e.paletteTrace = nullptr;
        } else if (op == "pal") {
            std::printf("pal %s\n", paletteHex(e.view().palette()).c_str());
            std::fflush(stdout);
        } else if (op == "xfade") {         // xfade LOOKUP|- PALETTE|current|- [ALT]
            std::istringstream a(arg);
            std::string look, pal, alt;
            a >> look >> pal >> alt;
            CrossFadeSpec sp;
            if (look != "-") sp.xftFile = look;
            if (pal == "current") {
                for (const Color& c : e.view().palette().c) { sp.palette.push_back(c.r); sp.palette.push_back(c.g); sp.palette.push_back(c.b); sp.palette.push_back(0); }
            } else if (pal != "-" && !pal.empty()) {
                sp.palette = paletteArg(pal);
            }
            if (!alt.empty() && alt != "-") sp.altName = alt;
            if (hooks && hooks->setCrossFade) hooks->setCrossFade(sp);
        } else if (op == "filtertest") {
            if (hooks && hooks->setFilterTest) hooks->setFilterTest(arg == "on");
        } else if (op == "game") {         // game ARGS: the debug commands of the game logic (--game)
            if (!hooks || !hooks->game) throw Error("script: 'game' needs --game");
            std::fputs(hooks->game(arg).c_str(), stdout);
            std::fflush(stdout);
        } else if (op == "autosave") {     // autosave ARGS: see ScriptHooks::autosave
            if (!hooks || !hooks->autosave) throw Error("script: 'autosave' needs --game");
            std::fputs(hooks->autosave(arg).c_str(), stdout);
            std::fflush(stdout);
        } else if (op == "menu") {         // menu ARGS: see ScriptHooks::menu
            if (!hooks || !hooks->menu) throw Error("script: 'menu' is not available here");
            std::fputs(hooks->menu(arg).c_str(), stdout);
            std::fflush(stdout);
        } else if (op == "wheel") {        // wheel up|down: the mouse wheel in the save and load menus (Page Up / Page Down)
            e.keyDown(Key::Other, arg == "up" ? 0x21 : 0x22);
            e.keyUp(Key::Other, arg == "up" ? 0x21 : 0x22);
        } else if (op == "window") {       // window HZ [PASS_MS] | window off: the window's pacing on a fake real clock and display (see WinSim)
            std::istringstream a(arg);
            std::string first;
            a >> first;
            if (first == "off") {
                pacer.reset();
                if (hooks && hooks->realtimeWaits) hooks->realtimeWaits(false);
                e.setPassMs(16);
                e.setScrollAccumulate(false);
                e.setPump(scriptPump);
            } else {
                double hz = std::strtod(first.c_str(), nullptr);
                long pm = kDefaultPassMs;
                a >> pm;
                const u32 p = clampPassMs(pm);
                win = WinSim();
                win.t = now;
                win.t0 = now;
                win.periodMs = hz > 0 ? 1000.0 / hz : 0.0;
                win.onDraw = [&] {
                    Frame f;
                    e.render(f);
                    const Engine::PanoramaFrame& pf = e.panorama();
                    if (cleanSim) cleanSimStep(&f);
                    if (pf.active) {
                        const PanoCamera d = winSmoother.update(pf.camera, pf.pageSerial, pf.scrollStepTime, win.now());
                        if (winTrace)
                            std::printf("wframe t=%u sx=%d sy=%d active=1 yaw=%.4f pitch=%.4f fov=%.4f snap=%d\n", static_cast<unsigned>(win.now()),
                                        e.view().scrollX(), e.view().scrollY(), d.yaw, d.pitchDeg, d.fovDeg, winSmoother.snapped() ? 1 : 0);
                    } else {
                        winSmoother.reset();
                        if (winTrace)
                            std::printf("wframe t=%u sx=%d sy=%d active=0\n", static_cast<unsigned>(win.now()), e.view().scrollX(), e.view().scrollY());
                    }
                    if (winTrace) std::fflush(stdout);
                    if (!wshotPrefix.empty()) {
                        uint64_t h = 1469598103934665603ull;
                        for (u8 v : f.px) h = (h ^ v) * 1099511628211ull;
                        for (const Color& c : e.view().palette().c) h = (((h ^ c.r) * 1099511628211ull) ^ c.g) * 1099511628211ull ^ c.b;
                        std::vector<u8> region;
                        if (wregW > 0 && wregH > 0) {
                            const Surface& sf = e.view().surface();
                            for (int yy = wregY; yy < wregY + wregH; yy++)
                                for (int xx = wregX; xx < wregX + wregW; xx++)
                                    region.push_back(xx >= 0 && yy >= 0 && xx < sf.w && yy < sf.h ? sf.row(yy)[xx] : 0);
                            for (u8 v : region) h = (h ^ v) * 1099511628211ull;
                        }
                        if (wshotN == 0 || h != wshotLast) {
                            wshotLast = h;
                            char suffix[32];
                            std::snprintf(suffix, sizeof suffix, "%03d", wshotN);
                            writeBmp(wshotPrefix + suffix + ".bmp", f.toImage(), e.view().palette());
                            std::string extra;
                            if (!region.empty()) {
                                if (FILE* rf = std::fopen((wshotPrefix + suffix + ".reg").c_str(), "wb")) {
                                    std::fwrite(region.data(), 1, region.size(), rf);
                                    std::fclose(rf);
                                }
                            }
                            if (wshotScale > 0 && pf.active) {
                                std::vector<uint32_t> kept, fresh;
                                std::string err;
                                if (!wshotSession) wshotSession.reset(new SdlSmoothSession);
                                if (!wshotSession->capture(pf, f, wshotScale, kept, err)) throw Error("script: wshots: " + err);
                                if (!captureSdlSoftware(pf, f, wshotScale, fresh, err)) throw Error("script: wshots: " + err);
                                writeBmp24(wshotPrefix + suffix + "s.bmp", Frame::W * wshotScale, Frame::H * wshotScale, kept);
                                extra = std::string(" smooth_same_as_fresh=") + (kept == fresh ? "1" : "0") + " uploads=" + std::to_string(wshotSession->uploads());
                            }
                            std::printf("wshot t=%u n=%d active=%d%s\n", static_cast<unsigned>(win.now()), wshotN, pf.active ? 1 : 0, extra.c_str());
                            std::fflush(stdout);
                            wshotN++;
                        }
                    }
                };
                winSmoother = PanoCameraSmoother();
                winSmoother.setStepMs(p);
                winSmoother.setZoomGlide(true);
                pacer.reset(new Pacer<WinSim>(win, p));
                e.setPassMs(p);
                e.setScrollAccumulate(true);
                if (hooks && hooks->realtimeWaits) hooks->realtimeWaits(true);
                e.setPump([&](u32& t) {
                    if (pumpTrace) {
                        std::printf("pump t=%u\n", static_cast<unsigned>(win.now()));
                        std::fflush(stdout);
                    }
                    const bool alive = pacer->pumpTurn(t, e.pumpMaxMs());
                    now = win.now();
                    return alive;
                });
            }
        } else if (op == "wreset") {
            win.presents = win.passes = 0;
            win.t0 = pacer ? win.now() : now;
        } else if (op == "wstats") {       // wstats: passes and presented frames since `window` / `wreset`
            std::printf("wstats ms=%u passes=%ld presents=%ld\n", static_cast<unsigned>((pacer ? win.now() : now) - win.t0), win.passes, win.presents);
            std::fflush(stdout);
        } else if (op == "wtime") {
            std::printf("wtime t=%u\n", static_cast<unsigned>(pacer ? win.now() : now));
            std::fflush(stdout);
        } else if (op == "wtrace") {
            winTrace = arg == "on";
        } else if (op == "soundtrace") {   // soundtrace on|off: print `soundplay t=ENGINE_MS file=... restart=N` at every start of a sound
            if (arg.find("on") != std::string::npos)
                e.sounds().playTrace = [&e](const std::string& file, bool restart) {
                    std::printf("soundplay t=%u file=%s restart=%d\n", static_cast<unsigned>(e.now()), file.c_str(), restart ? 1 : 0);
                    std::fflush(stdout);
                };
            else
                e.sounds().playTrace = nullptr;
        } else if (op == "wregion") {      // wregion X,Y,W,H | wregion off
            int rx = 0, ry = 0, rw = 0, rh = 0;
            if (std::sscanf(arg.c_str(), " %d,%d,%d,%d", &rx, &ry, &rw, &rh) == 4) { wregX = rx; wregY = ry; wregW = rw; wregH = rh; }
            else wregW = wregH = 0;
        } else if (op == "wshots") {       // wshots PREFIX [SCALE] | wshots off
            std::istringstream a(arg);
            std::string pre;
            a >> pre;
            wshotScale = 0;
            a >> wshotScale;
            wshotPrefix = pre == "off" ? std::string() : pre;
            wshotN = 0;
            wshotLast = 0;
            wshotSession.reset();
        } else if (op == "echo") {
            std::printf("%s\n", arg.c_str());
            std::fflush(stdout);
        } else {
            throw Error("script: unknown command '" + op + "'");
        }
    }
}

} // namespace biosys
