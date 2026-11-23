// BiosGame's side of PlayerHost (self_env.h): the engine and shell calls the player model and the game objects make.
// Each override forwards to the BiosGame member / Eng wrapper that translates the original call. Virtuals not overridden
// keep their "nothing happens" default (none that the game reaches).
#include "game/biosgame.h"
#include "game/story.h"
#include "game/feat_story.h"
#include "game/obj_gear.h"
#include "game/watertank.h"
#include <algorithm>
#include "game/bg_display.h"
#include "game/self_display.h"
#include "game/story.h"

namespace biosys::game {

class BiosPlayerHost final : public PlayerHost {
public:
    explicit BiosPlayerHost(BiosGame& g) : g_(g) {}

    void advanceTime(unsigned seconds, int updateSelf) override { g_.CTimeline_Advance(seconds, updateSelf); }   // dll:100154b0
    void playSound(const char* file) override { g_.CEngineFX_PlaySound(file, 0, 0); }
    u32 getTickCount() override { return g_.tick(); }
    u32 cheatFlags() override { return g_.story_->cheatFlags; }                       // DAT_100c2718 (story_keys.cpp)
    int modalFlag() override { return g_.g.noPalette ? 1 : 0; }                         // DAT_100c275c
    int dead() override { return g_.g.dead ? 1 : 0; }                                   // DAT_100c2728
    void setDead(int v) override { g_.g.dead = v != 0; }
    bool gameStarted() override { return g_.g.gameRunning; }                            // DAT_1009f824
    bool gameEnded() override { return g_.g.gameEnd; }                                  // DAT_100c2724
    int synthState() override { return g_.g.synthState; }                               // DAT_100c27e4
    void setForcePalette(int v) override { g_.g.forcePalette = v != 0; }                // DAT_100c3630
    void setStartupPhase(int v) override { g_.g.startupPhase = v; }                     // DAT_100c3a38
    void triggerEvent(u32 m) override { g_.CGame_TriggerEvent(m); }
    void writeLog(const std::string& t) override { g_.log(t); }
    void setFXState(int id, int on) override { g_.CWorld_SetFXState(static_cast<i16>(id), on); }
    void CWorld_StartPageFX(int id) override { g_.CWorld_StartPageFX(id); }
    void CWorld_StopPageFX(int id) override { g_.CWorld_StopPageFX(id); }
    void CWorld_LocalFxUpdate() override { g_.CWorld_LocalFxUpdate(); }
    void CTimeline_SetTicksPerGameSec(int t) override { g_.CTimeline_SetTicksPerGameSec(t); }
    void CTimeline_UpdateDiurnal(int a, int b, int c, int d) override { g_.CTimeline_UpdateDiurnal(a, b, c, d); }
    void CTimeline_ResetDiurnal() override { g_.tl.diurnal = 0; g_.gs().sim.diurnal = 0; g_.CTimeline_UpdateDiurnal(1, 1, 0, 1); }
    void CSite_CloseAllSets() override { g_.CSite_CloseAllSets(); }
    void CSite_RefreshFx(Item* s) override { if (auto* site = dynamic_cast<CSite*>(s)) g_.CSite_RefreshFx(*site); }

    // CEngineFX
    bool CEngineFX_LockPalette(int t) override { return g_.CEngineFX_LockPalette(t); }
    bool CEngineFX_UnlockPalette(int t) override { return g_.CEngineFX_UnlockPalette(t); }
    bool CEngineFX_LockSounds(int t) override { return g_.CEngineFX_LockSounds(t); }
    bool CEngineFX_UnlockSounds(int t) override { return g_.CEngineFX_UnlockSounds(t); }
    bool CEngineFX_LoadPalette(const char* f, const char* x, int s, int i, int t, int u) override {
        return g_.CEngineFX_LoadPalette(f, x, s, i, t, u);
    }
    bool CEngineFX_FadeToColour(int r, int gr, int b, int back, int speed, int percent) override {
        return g_.CEngineFX_FadeToColour(r, gr, b, back, speed, percent, 0);
    }
    void fadeToColour(int r, int gr, int b, int back, int speed, int, int percent) override {
        g_.CEngineFX_FadeToColour(r, gr, b, back, speed, percent, 0);
    }
    void fadeToPalette(int) override { g_.CEngineFX_FadeToCurrent(0); }
    void CEngineFX_PlayRandomSound(const char* stem, unsigned n) override { g_.CEngineFX_PlayRandomSound(stem, n); }
    void CEngineFX_ClearWindow() override { g_.CEngineFX_ClearWindow(); }
    bool CEngineFX_StopSound(int h) override { return g_.CEngineFX_StopSound(h); }
    void CEngineFX_StopLoopedAvi() override { g_.CEngineFX_StopLoopedAvi(); }
    void CEngineFX_LoopedAviClosed() override { g_.CEngineFX_LoopedAviClosed(); }
    void CEngineFX_SetImageVisibility(const char* i, int v, int w) override { g_.CEngineFX_ImageVisibility(i, v, w); }
    void CEngineFX_HotspotEnable(const char* h, int e, int w) override { g_.CEngineFX_HotspotEnable(h, e, w); }
    void CEngineFX_PlaceWorn(const char* o, const char* x, int px, int py) override { g_.CEngineFX_PlaceWorn(o, x ? x : "", px, py); }
    int CEngineFX_ChangeHoldingImagesGroup(const char* o, int grp) override { return g_.CEngineFX_ChangeHoldingImagesGroup(o, grp); }
    void playLoopedWav(const char* f, int vol, int* h) override { g_.CEngineFX_PlayLoopedWav(f, vol, h, nullptr, 0); }
    void closeSoundHandle(int* h) override { g_.CEngineFX_CloseSound(h); }
    void setSoundVolume(int h, int v) override { g_.CEngineFX_SetVolume(h, v); }
    void closeSound(int h) override { g_.eng_.CloseSound(h); }
    int jumpToPage(const char* p, int a, int b, int c, int d) override { return g_.CEngineFX_JumpToPage(p, a, b, c, d); }
    void clearWindow(int handle, int colour) override { g_.eng_.ClearWindow(handle, colour); }   // FUN_1006c790: Eng_ClearWindow(CSelf+0x328, 0), the top bar

    // Eng_* object calls
    bool Eng_RemoveGameObject(const char* o) override { return g_.eng_.RemoveGameObject(o); }
    bool Eng_Api52_ReturnGameObject(const char* o) override { return g_.eng_.ReturnGameObject(o); }
    bool Eng_Api45_ReturnGameObjectHidden(const char* o) override { return g_.api_.ReturnGameObjectHidden(o) == 0; }
    int Eng_GetGameObjectInfo(const char* n, std::array<int, 12>& info) override { return g_.eng_.GetGameObjectInfo(n, info); }
    bool Eng_SetGameObjectProperty(const char* o, const char* p, int v) override { return g_.api_.SetGameObjectProperty(o, p, v) == 0; }
    bool ChangeGameObjectVariation(const char* o, const char* v, int grp) override { return g_.api_.ChangeGameObjectVariation(o, v, grp) == 0; }
    bool Eng_ChangeGameObjectVariation1(const char* v, int grp) override { return true; }
    bool Eng_Api48_DropHeldObject(const char* h) override { std::string s(h); return g_.api_.DropHeldObject(&s) == 0; }
    void Eng_DoHotspotAction(const char* h, int w) override { g_.eng_.DoHotspotAction(h, w); }
    void setFeatureObject(Item* feature, GameObj* object) override { storyFeatureSetObject(feature, object); }   // *(feature + 0x58) = object (CThermalCooker / CDataTerminal / CVideoPlayer)
    void enableMouseDeltaEvent(int e) override { g_.eng_.EnableMouseDeltaEvent(e); }
    void enableUserInput(int e, int u) override { g_.eng_.EnableUserInput(e, u); }
    void redrawScreen() override { g_.eng_.RedrawScreen(); }
    int Eng_GetLastError() override { return g_.eng_.lastError(); }

    // ---- the hooks that had no engine call behind them (self_env.h): each translates what the original's call site does ----
    u32 lastHoverTick() override { return g_.story_->hoverTick; }                       // DAT_1009f834
    // FUN_100616f0 / FUN_10061790 (CEngineFX +0x620 count, +0x624 ten records of four ints)
    void addJiggle(int a, int b, int c, int d) override {
        if (jiggle_.size() < 40) {
            jiggle_.insert(jiggle_.end(), {a, b, c, d});
            return;
        }
        g_.gs().message = "BiosGame.dll: Add Jiggle - too many jiggles";               // the text is at DAT_100bc434 (cut by the decompiler)
        g_.log(g_.gs().message);
    }
    void jigglePage(int a, int b, int c) override {                                       // dll:10061790: Eng_JigglePage(records, count, a, b, c), count = 0
        g_.api_.JigglePage(jiggle_.data(), static_cast<int>(jiggle_.size() / 4), a, b, c);
        jiggle_.clear();
    }
    void resetSiteSoundHandles(Item* site) override {                                    // dll:1000c6e0: the handles of the four sound buffers = -1
        if (auto* s = dynamic_cast<CSite*>(site))
            for (int i = 0; i < 4; i++)
                if (SoundSet* set = g_.soundSet(s->buffer(i))) std::fill(set->handles.begin(), set->handles.end(), -1);
    }
    void sendKeyUp(int vk, unsigned lParam) override { if (g_.config_.sendKeyUp) g_.config_.sendKeyUp(vk, lParam); }   // SendMessageA(window, WM_KEYUP, vk, lParam)
    void Eng_Api6D_SetScrollOptions(int a, int b) override { g_.api_.SetScrollOptions(a, b); }
    void Eng_StopSound(int handle) override { g_.eng_.StopSound(handle); }
    void Eng_DisplayImage(const char* image, int window, const int* xy, int unk04, int unk20, int* unkOut, int unk08) override {
        g_.eng_.DisplayImage(image, window, xy, unk04, unk20, unkOut, unk08);
    }
    int CEngineFX_PlayAnim(const char* file, int* handle, int window, int frameDelay, int x, int y, int pageRelative, int keepPainted,
                           const char* wav) override {
        return g_.CEngineFX_PlayAnim(file, handle, window, frameDelay, x, y, pageRelative, keepPainted, wav ? wav : "");
    }
    void setSynthState(int state) override { g_.g.synthState = state; }                  // DAT_100c27e4
    int synthX() override { return g_.g.synthX; }                                         // DAT_100c27e8
    double synthDistance() override { return g_.g.synthParam; }                           // DAT_100c27f0
    void CSynthHandler_Attack() override { g_.story_->CSynthHandler_Attack(); }           // dll:10008a30
    void CPlant_UseObject(Item* plant, GameObj* object, const std::string& hotspot) override {   // dll:10072a00
        if (auto* p = dynamic_cast<CPlant*>(plant)) g_.CPlant_UseObject(*p, *object, hotspot);
    }
    void CPath_UseObject(Item* path, GameObj* object) override {                          // dll:10001200
        if (auto* p = dynamic_cast<CPath*>(path)) {
            g_.story_->CPath_Clear(*p, 1);                                               // FUN_10002540(path, 1)
            if (object && (p->flags() & 0x80000) != 0) object->removeFromScene(&g_.self());   // path +0x1d & 8; object vtable +0x94
        }
    }
    bool CEngineFX_LoadGogglesPalette(Item* site) override {                              // FUN_1001d600's LoadPalette call
        auto* s = dynamic_cast<CSite*>(site);
        const PaletteSet* ps = s ? g_.paletteSetOf(*s) : nullptr;
        if (!ps) return false;
        return g_.CEngineFX_LoadPalette(ps->pal[0][5], ps->xft[0][5].c_str(), 0, 1, 1, 0);   // set + 500 / set + 0x44c, steps 0, immediate 1, token 1
    }
    void restoreGogglesPalette(Item* goggles) override {                                  // dll:1001d600
        if (auto* gg = dynamic_cast<ObjGoggles*>(goggles)) gg->showPalette(g_.self());
    }
    void drawAlarmDisplay(const std::string& alarmText, int fontClock, int fontAlarm) override {   // dll:100165a0 (its text part)
        g_.CEngineFX_SetTextColours(0x2037703, 1, 0x2000000);                             // FUN_100611b0
        g_.CEngineFX_DrawText(g_.tl.clock, 0xeb, 0xb9, fontClock);                       // &DAT_100c1c2c = the timeline's clock text
        g_.CEngineFX_DrawText(alarmText, 0x11e, 0x7c, fontAlarm);
        g_.eng_.RedrawScreen();
    }
    int CEngineFX_CreateLogicalFont(const char* face, int height, int* font) override { return g_.CEngineFX_CreateLogicalFont(face, height, *font); }
    bool CEngineFX_CreateFontResource(const char* file) override { return g_.CEngineFX_CreateFontResource(file); }
    void CEngineFX_RemoveFontResource(const char* file) override { g_.CEngineFX_RemoveFontResource(file); }
    void CEngineFX_DeleteFont(int font) override { if (font != 0) g_.eng_.CreateDeleteLogicalFont(0, nullptr, font); }   // dll:100614e0
    u32 mapRoutes(int map) override {                                                     // DAT_100c11fc / DAT_100c1200 (story_path.cpp CPath_End)
        const WorldHeader& wh = g_.save_->worldHeader;
        if (map == 0) {
            uint64_t bits;
            std::memcpy(&bits, &wh.unk_298, 8);
            return static_cast<u32>(bits >> 32);
        }
        return static_cast<u32>(wh.unk_2a0);
    }
    void CPage_FxNamedStop(Item* page, const char* name) override {                       // dll:1000fb30
        if (auto* p = dynamic_cast<CPage*>(page)) g_.story_->pageStopFxByHotspot(*p, name, 0x28);
    }
    void CPlant_Display(Item* plant, int p) override { if (auto* pl = dynamic_cast<CPlant*>(plant)) g_.CPlant_Display(*pl, p); }   // dll:10072aa0
    void Eng_Api5E_ReleasePageImageSlot(Item* plant, int slot, int a, int b) override {   // dll:100743d0 on the plant's slot (plant + 0x18 + 4 slot)
        if (auto* pl = dynamic_cast<CPlant*>(plant)) {
            g_.api_.ReleasePageImageSlot(a, b, pl->imageSlot(slot));
            pl->imageSlot(slot) = -1;
        }
    }
    u32 fadeFromBlackUntil() override { return g_.g.fadeFromBlackUntil; }                // DAT_100c4310
    bool paletteOn() override { return g_.fx.paletteOn; }                                 // DAT_100c1864
    // FUN_10009b10: mean plant health (0..1) over the cels of the biome that count for it (the CBiome flag +0x60 bit 4: rainforest,
    // savannah, ocean, dead biome); the ocean and the undersea biome count their water's health nine times; -1 for a biome with none
    double biomeHealthFraction(const Biome& biome) override {
        Sim* sim = g_.gs().simCore;
        double sum = 0.0, result = -1.0;
        unsigned n = 0;
        for (Item* it : biome.cels) {
            CBiomeCel& c = as<CBiomeCel>(it);
            if (c.biome() && sim && (sim->biomeParams(c.biome()).flags & 4) != 0) {
                n++;
                g_.CBiomeCel_UpdatePlantHealthAvg(c);
                sum += c.plantHealthAvg();
            }
        }
        if (biome.addr == 0x100c1280u || biome.addr == 0x100c2768u) {
            if (const COcean* oc = dynamic_cast<const COcean*>(g_.gs().ocean)) {
                n += 9;
                sum += oc->health() * 9.0;
            }
        }
        if (n != 0) result = sum / static_cast<double>(n);
        return result;
    }
    bool rainFilter() override { return g_.g.rainFilter; }                                // DAT_1009f848
    int paletteLockOwner() override { return g_.save_->engineFx.m_wState; }              // DAT_100c1860
    int Eng_OpenAVIMovie(const char* file, int window, int x, int y, int& handle) override {   // dll:100752d0 (OnMovieEvent, loop 0, unk04 0, CEngineFX +0x104)
        return g_.eng_.OpenAVIMovie(file, window, x, y, true, 0, 0, g_.fx.palette.data(), handle);
    }
    void setMovieHandle(int handle) override { g_.fx.aviHandle = handle; }               // DAT_100c18f0
    bool Eng_PlayAVIMovie(int handle, int restart) override { return g_.eng_.PlayAVIMovie(handle, restart); }
    void Eng_GetPanoramaInfo(int& a, int& b, int& x) override { g_.eng_.GetPanoramaInfo(a, b, x); }
    void Eng_BeginPageFilter(int a, int b, int c) override { g_.eng_.BeginPageFilter(a, b, c); }
    void CSite_PlayAmbientSounds(Item* site) override {                                   // dll:1000eaa0
        auto* s = dynamic_cast<CSite*>(site);
        if (!s) return;
        g_.CSite_PlayAmbient(*s, s->daySound());
        g_.g.ambDay = s->daySound();
        g_.CSite_PlayAmbient(*s, s->nightSound());
        g_.g.ambNight = s->nightSound();
        if ((s->flags() & 0x100) != 0) g_.CSite_PlayAmbient(*s, s->deadSound());         // +0x11 & 1
        g_.g.ambDead = s->deadSound();
    }
    void CSite_OnPageEntered(Item*, const char*) override {                               // site vtable +0x24 = dll:1000d940
        if (g_.g.forcePalette) {
            g_.g.forcePalette = false;
            g_.CTimeline_UpdateDiurnal(0, 1, 0, 1);
        }
        if (g_.g.rainFilter) g_.eng_.BeginPageFilter(0x76, 0x56, 0x57);
    }
    void CPage_Exit(Item* page) override { if (auto* p = dynamic_cast<CPage*>(page)) g_.pageExit(*p); }          // vtable +0x28
    void CPage_OnTrigger(Item* page) override { if (auto* p = dynamic_cast<CPage*>(page)) g_.story_->pageOnEntered(*p); }   // vtable +0x24
    void CPage_EnterAndUpdate(Item* page) override {                                      // dll:1000f540
        auto* p = dynamic_cast<CPage*>(page);
        if (!p) return;
        g_.g.linkedObjectLock = true;                                                    // DAT_100c2a14
        g_.pageOnEnter(*p);                                                              // vtable +0x1c
        g_.pageUpdate(*p);                                                               // vtable +0x20
        g_.g.linkedObjectLock = false;
    }
    void CPath_PageLeft() override { g_.story_->CPath_PageLeft(); }                      // dll:10002380
    void CPlant_ReleasePageImages(Item* plant) override { if (auto* pl = dynamic_cast<CPlant*>(plant)) g_.CPlant_ReleasePageImages(*pl); }   // dll:10073a60
    void objectVirtual(Item* object, unsigned off, const char* page) override {          // the held / worn object's +0x80 / +0x84 / +0x88
        auto* o = dynamic_cast<GameObj*>(object);
        if (!o) return;
        if (off == 0x80) o->onPageEnter(g_.self(), page);
        else if (off == 0x84) o->onPageLeave(g_.self(), page);
        else if (off == 0x88) o->onPageEntered(g_.self(), page);
    }
    bool playSoundHandle(const char* file, int* handle) override { return g_.CEngineFX_PlayLoopedSoundInto(file, handle, 0, 0); }   // dll:10060780
    void resetEngineFXSway() override { g_.story_->mouseFilterReset(); }                  // dll:10061820
    void playObjectUseAnimation(const char* tan, const char* wav, int hideHeld, int removeHeldAfter, int x, int y, int playMode,
                                int frameDelayMs, const char* variation, int holdGroup) override {   // dll:10076060
        g_.api_.PlayObjectUseAnimation(tan ? tan : "", wav ? wav : "", hideHeld != 0, removeHeldAfter != 0, x, y, playMode, frameDelayMs,
                                       variation ? variation : "", holdGroup);
    }
    void blankTopBar(int handle) override {                                               // dll:1006f100: FillSolidRect(0, 0, 0x27f, 0x37, black)
        GdiSession dc(g_.eng_, handle, 0, nullptr);
        if (dc.ok()) dc.dc().fillSolidRect(0, 0, 0x27f, 0x37, 0x2000000);
    }
    void redrawPage() override { g_.api_.RedrawPage(); }                                  // dll:100752a0
    void refreshPage(Item* page) override { if (auto* p = dynamic_cast<CPage*>(page)) g_.CPage_UpdateFx(*p); }   // dll:1000f730
    // Eng_GetWindowDC(CSelf+0x328) gives a DC once the top bar exists (+0x328 is 0x29a until OnPopupOpened("topbar"), dll:1001e350); the strip's own
    // drawing opens the real DC (PlayerDisplay), so no probe call is made here
    bool windowDC() override { return g_.self().m_lTopBarHandle != 0x29a; }
    int subTechState() override { return g_.story_->subtech.state; }                      // DAT_100c24bc
private:
    BiosGame& g_;
    std::vector<int> jiggle_;      // CEngineFX +0x624: records {a, b, c, d}
};

// port: binds the player to the shell (called once after the SaveGame exists)
void BiosGame::attachPlayerHost() {
    playerHost_ = std::make_unique<BiosPlayerHost>(*this);
    playerDisplay_ = std::make_unique<GdiPlayerDisplay>(*this, [this](const std::string& s) { log(s); });
    save_->self.bind(save_->world, playerHost_.get(), playerDisplay_.get());
}

} // namespace biosys::game
