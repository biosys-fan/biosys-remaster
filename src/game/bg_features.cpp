// The shell side of the machine classes: BiosGame's implementation of FeatureHost (what the features call back for sounds,
// FX, the player's messages, the current site), the binding of the simulation core's feature hooks, and the two feature
// parameter events (dll:10021db0 / 10021df0). The machines themselves are in feature.cpp and feat_*.cpp.
#include "game/biosgame.h"
#include "game/story.h"
#include "game/feat_sensors.h"
#include "game/feat_climate.h"
#include "game/featparam.h"
#include "game/feat_hostext.h"

// DAT_1009f844: the menu slot text colour passed as the second argument of Eng_OpenMenu (dll:10076600).
static const unsigned char kMenuTextColour[4] = {0x73, 0x52, 0x00, 0x00};


namespace biosys::game {

class BiosFeatureHost final : public FeatureHost, public FeatureHostExt {
public:
    explicit BiosFeatureHost(BiosGame& g) : g_(g) {}
    Eng* eng() override { return &g_.eng_; }
    void playSound(const char* file) override { g_.CEngineFX_PlaySound(file, 0, 0); }          // FUN_10060700(&CEngineFX, file, 0, 0)
    void showMessage(int id) override { g_.story_->Self_ShowMessage(id); }   // dll:1006bc40 CSelf::ShowMessage(&CSelf, 1, id, 0xffff, 0)
    void startPageFX(int id) override { g_.CWorld_StartPageFX(id); }                            // dll:10014280
    void stopPageFX(int id) override { g_.CWorld_StopPageFX(id); }                              // dll:100142d0
    void setFXState(int id, int on) override { g_.CWorld_SetFXState(id, on); }                  // dll:100138d0
    bool isCurrentFeature(const CFeature* f) const override { return f && g_.self().m_pFeature.item == f; }   // DAT_100c1f9c
    const CFeature* currentFeature() const override { return dynamic_cast<const CFeature*>(g_.self().m_pFeature.item); }
    CSite* currentSite() const override { return g_.curSite(); }                                // DAT_100c1f84
    bool helpTextsOn() const override { return (g_.self().m_dwDreamsSeen & 0x80000000u) != 0; }   // FUN_1006f320
    void forcePalette() override { g_.g.forcePalette = true; }                                  // DAT_100c3630 = 1
    void resetDiurnal() override {                                                              // DAT_100c1c44 = 0; UpdateDiurnal(1, 1, 0, 1)
        g_.tl.diurnal = 0;
        g_.gs().sim.diurnal = 0;
        g_.CTimeline_UpdateDiurnal(1, 1, 0, 1);
    }
    bool isAtPage(const char* page) const override { return g_.isAtPage(page); }                 // dll:1006ef40
    // the tail of CWorld::OnCircuitPowerOn / Off: FUN_1000d990 / 1000d9f0 / 1000e3a0 on the current site, UpdateDiurnal(0, 1, 0, 1)
    void siteLightsChanged(int) override {
        if (CSite* s = g_.curSite()) {      // the original dereferences DAT_100c1f84 unchecked
            g_.CSite_PaletteGroup(*s);
            g_.CSite_LoadPalettes(*s);
            g_.CSite_BuildDayPalette(*s);
        }
        g_.CTimeline_UpdateDiurnal(0, 1, 0, 1);
    }
    void updatePlantHealthAvg(CBiomeCel& c) override { g_.CBiomeCel_UpdatePlantHealthAvg(c); }   // dll:10018860
    void triggerEvent(u32 m) override { g_.CGame_TriggerEvent(m); }                              // dll:100092d0
    void onPollutionAlarm(CBiomeCel* cel) override {                                             // CBiomeSensor::Update dll:1005e0a0
        Self& me = g_.self();
        if (cel == me.m_pCel && 20.0 < me.m_dEnergy) { me.m_dwUnk320 = 1; me.m_wWakeReason = 6; }
    }
    void openSensorPanel() override {                                                            // dll:1005e000
        g_.CEngineFX_ClearWindow();
        g_.eng_.ClearWindow(g_.self().m_lTopBarHandle, 0);   // dll:1006c790 FUN_1006c790(&CSelf): the player's status screen closes
        g_.CEngineFX_LoadPalette("palettes\\sensor\\sensor.pal", nullptr, 0, 0, 5, 0);
    }
    void closeSensorPanel() override {                                                           // dll:1005e970
        g_.CEngineFX_UnlockPalette(5);
        g_.CEngineFX_ClearWindow();
        g_.g.forcePalette = true;
    }
    bool fxStart(Item* fx) override { auto* f = dynamic_cast<CSpotFX*>(fx); return f && g_.fxStart(*f); }          // a CMovieFX's vtable +0x1c
    bool fxPlay(Item* fx) override { auto* f = dynamic_cast<CSpotFX*>(fx); return f && g_.fxPlay(*f); }            // +0x20
    bool fxOnStarted(Item* fx, int handle, unsigned mask) override {                                              // +0x2c
        auto* f = dynamic_cast<CSpotFX*>(fx);
        return f && g_.CSpotFX_OnStarted(*f, handle, mask);
    }
    bool fxOnClosed(Item* fx, int handle, unsigned mask) override {                                               // +0x38
        auto* f = dynamic_cast<CSpotFX*>(fx);
        return f && g_.fxOnClosed(*f, handle, mask);
    }
    void flagHotspotHandled() override { g_.story_->hotspotHandled = true; }   // DAT_1009f838 = 1 (read by event 35, story_hotspot.cpp)
    void drawText(int x, int y, const std::string& text) override { g_.story_->uiText(x, y, text); }   // CEngineFX::DrawText
    void setTextColours(u32 colour, int transparent, u32 background) override { g_.CEngineFX_SetTextColours(colour, transparent, background); }
    std::unique_ptr<GdiSession> openDc(int window) override { return g_.openDc(window); }
    bool playLoopedSoundInto(const char* file, int* handle) override { return g_.CEngineFX_PlayLoopedSoundInto(file, handle, 0, 0); }   // dll:10060780
    std::string currentLocation() const override { return std::string(reinterpret_cast<const char*>(g_.self().m_szLocation.data())); }
    bool playAvi(const char* file, int x, int y, int volume, int colourKey, const char* page, int pageMode, int opt42, int synth, int restart) override {
        return g_.CEngineFX_PlayAvi(file, x, y, volume, colourKey, page, pageMode, opt42, synth, restart);   // dll:10060e10
    }
    GameObj* heldObject() const override { return dynamic_cast<GameObj*>(g_.self().m_pObjHolding.item); }   // DAT_100c1fa0
    bool startAttackMovie(const char* file, int* handle) override { return g_.story_->startAttackMovie(file, *handle); }
    void killPlayer(int cause) override { g_.g.dieOnUpdate = true; g_.self().m_wCauseOfDeath = static_cast<u16>(cause); }   // DAT_100c249c = 1; DAT_100c24a0
    void setSynthState(int s) override { g_.g.synthState = s; }
    int synthState() const override { return g_.g.synthState; }
    u32 tick() const override { return g_.tick(); }
    CPage* currentPage() const override { return g_.curPage(); }
    void stopPageFx(CPage* p) override { if (p) g_.story_->pageCancelFx(*p); }
    void playRandomSound(const char* stem, unsigned n) override { g_.CEngineFX_PlayRandomSound(stem, n); }
    void fadeToColour(int r, int gr, int b, int back, int speed, int percent) override { g_.CEngineFX_FadeToColour(r, gr, b, back, speed, percent, 0); }
    void closeStatusScreen() override { g_.story_->Self_CloseStatusScreen(); }
    bool playLoopedAvi(const char* file, int x, int y, int volume, int colourKey, const char* page, int pageMode, int opt42, int synth) override {
        return g_.CEngineFX_PlayLoopedAvi(file, x, y, volume, colourKey, page, pageMode, opt42, synth);
    }
    void stopLoopedAvi() override { g_.CEngineFX_StopLoopedAvi(); }
    int& moviePageHandle() override { return g_.fx.aviHandle; }       // DAT_100c18f0 = CEngineFX +0x5f8
    bool startTapeMovie(const char* file, int* handle) override {
        int h = -1;
        const bool ok = g_.eng_.OpenAVIMovie(file, -1, 0x91a, 0x19b, true, 0, 0, g_.fx.palette.data(), h);
        *handle = h;
        g_.fx.aviHandle = h;
        if (!ok) return false;
        const u8 key[4] = {0, 0xff, 0xff, 0};
        g_.eng_.SetAVIMovieParam(h, key);
        g_.eng_.AviOption42(h);
        g_.eng_.AviOption4f(h, 1, 0);
        g_.fx.loopedAvi = false;                                       // _DAT_100c1878 = 0 (CEngineFX +0x580)
        g_.eng_.SetAVIMoviePage(h, "ph1");
        g_.eng_.PlayAVIMovie(h, 1);
        return true;
    }
    void openMenu(const char* menu) override { g_.eng_.api().OpenMenu(menu ? menu : "", kMenuTextColour, 0); }

    // FeatureHostExt (feat_hostext.h): the CEngineFX calls of the story features
    Self* player() override { return &g_.self(); }
    void clearWindow() override { g_.CEngineFX_ClearWindow(); }                                  // dll:10060df0
    bool lockPalette(int t) override { return g_.CEngineFX_LockPalette(t); }                     // dll:10060ce0
    bool unlockPalette(int t) override { return g_.CEngineFX_UnlockPalette(t); }                 // dll:10060cb0
    bool loadPalette(const char* f, const char* x, int s, int i, int t, int u) override {       // dll:10060170
        return g_.CEngineFX_LoadPalette(f, x, s, i, t, u);
    }
    int jumpToPage(const char* p, int a, int b, int c, int d) override { return g_.CEngineFX_JumpToPage(p, a, b, c, d); }   // dll:10060d10
    bool stopSound(int handle) override { return g_.CEngineFX_StopSound(handle); }               // dll:100609d0
    void loopedAviClosed() override { g_.CEngineFX_LoopedAviClosed(); }                          // dll:10061120
    bool silent() const override { return g_.g.silent; }                                         // DAT_1009f83c
    bool playVideoMessage(const char* movie, int x, int y, int volume) override {               // dll:1001ae40
        int h = -1;
        if (!g_.eng_.OpenAVIMovie(movie, -1, x, y, true, 0, 0, g_.fx.palette.data(), h)) return false;
        g_.fx.aviHandle = h;                                                                     // DAT_100c18f0
        if (volume < 0) g_.eng_.SetAVIMovieVolume(h, volume);
        g_.eng_.AviOption4f(h, 1, 0);
        const u8 key[4] = {0, 0xff, 0xff, 0};
        g_.eng_.SetAVIMovieParam(h, key);
        g_.eng_.AviOption42(h);
        g_.fx.loopedAvi = false;                                                                 // _DAT_100c1878 = 0
        g_.eng_.PlayAVIMovie(h, 0);
        return true;
    }
    void fireHurts() override {                                                                    // CFire::OnHotspot dll:10006a90
        g_.CEngineFX_FadeToColour(0xff, 0, 0, 1, 0x14, 0x36, 0);
        g_.CEngineFX_PlayRandomSound("sounds\\russell\\pain\\pain", 5);
        if (5.0 < g_.self().m_dHealth) g_.self().LoseHealth(1.0, 0, 0);   // CSelf+0x68
    }
    void dieOfElectrocution() override {                                                         // dll:100272a0
        g_.g.dieOnUpdate = true;     // DAT_100c249c = 1
        g_.self().m_wCauseOfDeath = 0x13;    // DAT_100c24a0 = CSelf+0x768
        g_.self().m_bDieOnUpdate = 1;        // DAT_100c249c = CSelf+0x764
    }

private:
    BiosGame& g_;
};

// port: the machines' connection to the shell (GameState::host) and the simulation core's calls into the machines
void BiosGame::attachFeatureHost() {
    featureHost_ = std::make_unique<BiosFeatureHost>(*this);
    gs().host = featureHost_.get();
    bindFeatureHooks(*sim_);     // the simulation core's calls into the machines (power on / off, vents, lungs)
}

// dll:10021db0 Event_OnSetFeatureParameter
bool BiosGame::onSetFeatureParameter(const std::string& name, int id, int a, int b, int c) {
    return featureSetParameter(world(), name, id, a, b, c);
}
// dll:10021df0 Event_OnGetFeatureParameter
bool BiosGame::onGetFeatureParameter(const std::string& name, int id, u32& outInt, double& outValue) {
    return featureGetParameter(world(), name, id, outInt, outValue);
}

} // namespace biosys::game
