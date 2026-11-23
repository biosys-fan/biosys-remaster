// What the machine classes need that is not part of the data model:
//  * FeatureHost: the calls into the shell (sounds, FX switches, the player's messages, the current feature) that the
//    original makes through DLL globals and wrapper functions. BiosGame implements it; tests run without (null host:
//    every call is skipped, as if the engine did nothing).
//  * the CWorld functions the features call (power, fuse boxes, flows, the feature update loop), as free functions on
//    a World (namespace cworld). They are translations of the CWorld members of the same name.
#pragma once
#include "game/gdi.h"
#include "game/world.h"
#include <memory>

namespace biosys::game {

class Eng;
class CFeature;
class CFuseBox;
class CBiomeSensor;
class CAirFlow;
class GameObj;
class COcean;
class CSite;
class CPage;
class Sim;

class FeatureHost {
public:
    virtual ~FeatureHost() = default;
    virtual Eng* eng() { return nullptr; }                                   // the Eng_* wrappers; null without an engine
    virtual void playSound(const char* file) {}                              // CEngineFX::PlaySound(&CEngineFX, file, 0, 0) dll:10060700
    virtual void showMessage(int id) {}                                      // CSelf::ShowMessage(&CSelf, 1, id, 0xffff, 0) dll:1006bc40
    virtual void startPageFX(int id) {}                                      // CWorld::StartPageFX dll:10014280
    virtual void stopPageFX(int id) {}                                       // CWorld::StopPageFX dll:100142d0
    virtual void setFXState(int id, int on) {}                               // CWorld::SetFXState dll:100138d0
    virtual bool isCurrentFeature(const CFeature*) const { return false; }   // DAT_100c1f9c == this (the player's feature)
    virtual const CFeature* currentFeature() const { return nullptr; }
    virtual CSite* currentSite() const { return nullptr; }                   // DAT_100c1f84 (the player's site)
    virtual bool helpTextsOn() const { return false; }                       // FUN_1006f320: CSelf+0x118 >> 31
    virtual void forcePalette() {}                                           // DAT_100c3630 = 1
    virtual void resetDiurnal() {}                                           // DAT_100c1c44 = 0; CTimeline::UpdateDiurnal(1, 1, 0, 1)
    virtual bool isAtPage(const char* page) const { return false; }          // CSelf::IsAtPage dll:1006ef40
    // The tail of CWorld::OnCircuitPowerOn / Off (dll:10014bb0 / 10014d50): lights = DAT_100c3224, then
    // FUN_1000d990 / 1000d9f0 / 1000e3a0 on the current site and CTimeline::UpdateDiurnal(0, 1, 0, 1)
    virtual void siteLightsChanged(int lightsOn) {}
    virtual void updatePlantHealthAvg(CBiomeCel&) {}                         // CBiomeCel::UpdatePlantHealthAvg dll:10018860
    virtual void triggerEvent(u32 milestone) {}                              // CGame::TriggerEvent dll:100092d0
    virtual void onPollutionAlarm(CBiomeCel* cel) {}                         // CBiomeSensor::Update: the pollution alarm starts; when the player is in `cel` and CSelf+0x80 > 20 two CSelf members are set (DAT_100c2058 = 1, DAT_100c1e48 = 6)
    virtual void openSensorPanel() {}                                        // CBiomeSensor::EnterPage on a panel page: Eng_ShowCursor(0) done; CEngineFX::ClearWindow, FUN_1006c790(&CSelf), LoadPalette(sensor.pal, slot 5)
    virtual void closeSensorPanel() {}                                       // CBiomeSensor::OnLeavePage: UnlockPalette(5), ClearWindow, DAT_100c3630 = 1
    virtual void flagHotspotHandled() {}                                     // DAT_1009f838 = 1 (CHatch hotspot)
    virtual bool fxStart(Item*) { return false; }                            // a CMovieFX's vtable +0x1c (CSpotFX start; true = started)
    virtual bool fxPlay(Item*) { return false; }                             // +0x20 (play)
    virtual bool fxOnStarted(Item*, int handle, unsigned mask) { return false; }   // +0x2c
    virtual bool fxOnClosed(Item*, int handle, unsigned mask) { return false; }    // +0x38 (the media was closed by the engine)
    virtual void fireHurts() {}                                              // CFire::OnHotspot dll:10006a90: FadeToColour(red), pain sound, CSelf::LoseHealth(1) when CSelf+0x68 > 5
    virtual void dieOfElectrocution() {}                                     // DAT_100c249c = 1; DAT_100c24a0 = 0x13 (CFuseBox::OnApply with a water extinguisher)
    virtual void drawFeaturePage(CFeature&, int redraw) {}                   // the GDI text of the panel pages (UNRESOLVED, shell side)
    virtual void drawText(int x, int y, const std::string& text) {}          // CEngineFX::DrawText dll:10061500 (own DC on the main window, the colours of setTextColours); records the text
    virtual void setTextColours(u32 colour, int transparent, u32 background) {}   // FUN_100611b0(&CEngineFX, colour, transparent, background): +0x614 / +0x61c / +0x618
    // Eng_GetWindowDC(window) + SelectObject(systemfont): the display methods' DC (gdi.h). Null without an engine, or when the
    // engine gave no DC; every string drawn through it is recorded. The session releases the DC when it is destroyed.
    virtual std::unique_ptr<GdiSession> openDc(int window) { return nullptr; }
    virtual bool playLoopedSoundInto(const char* file, int* handle) { return false; }   // CEngineFX::PlayLoopedSoundInto dll:10060780(file, handle, 0, 0)
    virtual bool playAvi(const char* file, int x, int y, int volume, int colourKey, const char* page, int pageMode, int opt42, int synth, int restart) { return false; }   // CEngineFX::PlayAvi dll:10060e10
    virtual GameObj* heldObject() const { return nullptr; }                 // DAT_100c1fa0: the object in the player's hand
    virtual bool startAttackMovie(const char* file, int* handle) { return false; }   // the synth attack movie (story_handlers.cpp): open, key, prepare, play
    virtual void killPlayer(int cause) {}                                    // DAT_100c249c = 1; DAT_100c24a0 = cause
    virtual void setSynthState(int s) {}                                     // DAT_100c27e4
    virtual int synthState() const { return 0; }
    virtual u32 tick() const { return 0; }                                   // GetTickCount()
    virtual CPage* currentPage() const { return nullptr; }                   // DAT_100c1f80
    virtual void stopPageFx(CPage*) {}                                       // FUN_1000f940: stop every FX of the page
    virtual void playRandomSound(const char* stem, unsigned n) {}            // CEngineFX::PlayRandomSound dll:10060820
    virtual void fadeToColour(int r, int g, int b, int back, int speed, int percent) {}   // FUN_100605f0 / 10060620
    virtual void closeStatusScreen() {}                                      // FUN_1006c790(&CSelf)
    virtual bool playLoopedAvi(const char* file, int x, int y, int volume, int colourKey, const char* page, int pageMode, int opt42, int synth) { return false; }   // dll:10060f40
    virtual void stopLoopedAvi() {}                                          // dll:10061110
    virtual int& moviePageHandle() { static int none = -1; return none; }    // DAT_100c18f0: the movie of the video player
    virtual bool startTapeMovie(const char* file, int* handle) { return false; }   // CVideoPlayer::Play: open, key, option, play on page ph1
    virtual void openMenu(const char* menu) {}
    virtual std::string currentLocation() const { return std::string(); }    // CSelf+0xec: the page the player is on (upper case)                               // Eng_OpenMenu
};

// Connects the simulation core's calls into the machines (power on / off of a feature, vents, lungs; sim.h Hooks) to the
// feature classes. BiosGame and the test tools call it once after constructing the Sim.
void bindFeatureHooks(Sim& sim);

namespace cworld {

// the simulation core of the world (GameState::simCore; throws when there is none)
Sim& sim(World& w);
// CWorld::HasPower dll:10014910
u32 hasPower(World& w, u32 mask);
// dll:100149f0 / 10014a20 (clamped 0..100), 10014a50 / 10014a70
void addPowerDemand(World& w, double d);
void removePowerDemand(World& w, double d);
void addPowerGeneration(World& w, double d);
void removePowerGeneration(World& w, double d);
// dll:10014a90 / 10014b20 / 10014ae0 / 10014b60 / 10014bb0 / 10014d50
void powerFailure(World& w);
void powerRestored(World& w);
void powerOffFeatures(World& w, u32 mask);
void powerOnFeatures(World& w, u32 mask);
void onCircuitPowerOn(World& w, u32 mask);
void onCircuitPowerOff(World& w, u32 mask);
// CWorld::BlowFuse dll:10014f70: false when the mask names no circuit or the fuse is already blown
bool blowFuse(World& w, u32 mask);
// dll:100148b0 / 10014860 (the cursor is GameState::featureCursor)
bool updateAllFeatures(World& w, i32 now);
bool updateNextFeature(World& w, i32 now);
// The flow switches the machines use (the loops over world+0x168 / world+0x1a4)
void switchAirFlows(World& w, i16 id, int on);               // dll:10013e60
void setAirFlowsReversed(World& w, i16 id, int reversed);    // dll:10013eb0
void setAirFlowsDest(World& w, i16 id, CBiomeCel* dest);     // dll:10013f00
void switchWaterFlows(World& w, i16 id, int on);             // dll:10014110
void toggleWaterFlowsReversed(World& w, i16 id);             // dll:10014160
void setWaterFlowsPurity(World& w, i16 id, double purity);   // dll:100141b0
// CWorld::StartPageFX / StopPageFX / SetFXState: the host's FX lists (nothing without a host)
void startPageFX(World& w, i32 id);
void stopPageFX(World& w, i32 id);
void setFXState(World& w, i32 id, int on);

// Short forms of the world records
WorldHeader& header(World& w);        // CWorld scalars
Timeline& timeline(World& w);         // DAT_100c1c18
u32 timeOfDay(World& w);              // DAT_100c1c28
i32 gameTime(World& w);               // DAT_100c1c1c
COcean& ocean(World& w);              // DAT_100c1cc8 (throws when the world has none)

} // namespace cworld

// CBiome functions (dll:1000a100 / 1000a160 / 1000a1c0) on the world's biome objects
double biomeAvgProbeAirTemp(const Biome& b);
double biomeAvgPlantHealth(const Biome& b);
int biomeHealthTrend(const Biome& b);

} // namespace biosys::game
