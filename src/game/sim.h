// The simulation core of the port of BiosGame.dll: the world update that Event_OnTimer (dll:1001d970) round-robins
// through (weather, biome cels, air flows, water flows, ocean, plants) and the CWorld helpers it uses.
// Notes: analysis/notes/simulation.md sections 2 to 7.
//
// The scalars of CWorld, CTimeline and the story object are the SaveGame's records (WorldHeader, Timeline,
// BlobHeader); everything the DLL keeps in function-local statics or in unsaved CWorld members and that the update
// functions need lives here: one Sim per SaveGame (no globals). The update methods of the simulated classes
// (CBiomeCel::updateState, CAirFlow::update, CWaterFlow::update, COcean::updateState, CPlant::update ...) take the Sim
// the way the original reads the globals theWorld / theTimeline.
//
// Calls into code that is not part of the simulation (shell, story, machines) go through `hooks`; an empty hook does nothing
// (BiosGame::bindSimHooks and bindFeatureHooks bind every one of them).
#pragma once
#include "game/savegame.h"
#include <functional>
#include <map>

namespace biosys::game {

class CBiomeCel;
class CWaterTank;
class COcean;
class CAirFlow;
class CWaterFlow;
class CPlant;
class CFeature;
class GameObj;

// Clamp dll:100074b0 (a NaN passes through unchanged)
inline void clampD(double& x, double lo, double hi) {
    if (x < lo) x = lo;
    else if (hi < x) x = hi;
}

// The members of the nine static CBiome objects the simulation reads (ctor dll:10009f60, World__InitBiomeParams
// dll:1002b560): the temperature gain (+0x50), the flags (+0x60: bit 0 = fixed temperature, bit 2), the fixed
// temperature (+0x68) and the target temperature CBiome::UpdateTargetTemp (dll:1000a0d0) writes (+0x58).
struct BiomeParams {
    double gain = 0.0;
    uint32_t flags = 0;
    double fixedTemp = 0.0;
    double target = 0.0;
};

class Sim {
public:
    struct Hooks {
        // Eng_WriteLog: when empty the text only goes to GameState::log
        std::function<void(const std::string&)> log;
        // CGame::TriggerEvent(&DAT_100c2720, id) dll:100092d0 (story milestones)
        std::function<void(uint32_t)> triggerEvent;
        // FUN_1000e3a0(current site) = CSite_BuildDayPalette: the sky palette of the site on display (reads BlueSky)
        std::function<void()> buildDayPalette;
        // the feature's virtual +0x68 / +0x6c (OnPowerOn / OnPowerOff), called by PowerRestored / PowerFailure
        std::function<void(CFeature&)> featurePowerOn, featurePowerOff;
        // CAirFlow::TurnOn: adds `delta` to the target airflow of the flow's vent feature (CVent+0x60)
        std::function<void(CFeature&, double)> ventAdjustTarget;
        // CLungAirFlow::Update: the lung's virtual +0x74 (switch off) and the store into lung+0x1dc (moving flag)
        std::function<void(CFeature&)> lungSwitchOff;
        std::function<void(CFeature&, int)> lungSetMoving;
        // FUN_10014740 -> the food object's virtual +0x94 (Eng_RemoveGameObject + FUN_10029510); empty: the state change only
        std::function<void(GameObj&)> removeFromScene;
        // CEngineFX::PlaySound(&CEngineFX, file, 0, 0) dll:10060700
        std::function<void(const char*)> playSound;
        // CTimeline::RealTimeTick dll:10015470 (the direct cel setters call it first)
        std::function<void()> realTimeTick;
    } hooks;

    explicit Sim(SaveGame& sg);

    // ----- what the original reads from the DLL globals ------------------------------------------------------
    World& world() { return sg_.world; }
    const Fixes& fixes() const { return sg_.world.state().fixes; }
    const Model& model() const { return sg_.world.state().model; }           // settings.h: the model corrections
    const Tuning& tuning() const { return sg_.world.state().tuning; }        // settings.h: the difficulty numbers
    SaveGame& save() { return sg_; }
    WorldHeader& header() { return sg_.worldHeader; }                        // theWorld scalars
    i32 time() const { return sg_.timeline.m_dwTime; }                       // DAT_100c1c1c
    uint32_t timeOfDay() const { return static_cast<uint32_t>(sg_.timeline.m_dwTimeOfDay); }   // DAT_100c1c28
    double daylight() const { return sg_.world.state().sim.daylight; }       // DAT_100c1c38
    int diurnal() const { return sg_.world.state().sim.diurnal; }            // DAT_100c1c44
    uint32_t milestones() const { return sg_.header.milestones; }            // DAT_100c272c (story +0x0c)
    double sunlight() const { return sg_.worldHeader.m_dSunlight; }          // DAT_100c11f0
    double extTemp() const { return sg_.worldHeader.m_dExtTemp; }            // DAT_100c1168
    COcean& ocean();                                                         // DAT_100c1cc8
    BiomeParams& biomeParams(const Biome* b);
    BiomeParams& biomeParams(uint32_t addr);
    void log(const std::string& text);        // sprintf into DAT_100c2650, then Eng_WriteLog

    // ----- weather state that is not in the save blob (CWorld ctor dll:10010f70) ---------------------------------
    double blueSkyTarget = 0;                 // +0x278
    double blueSkyLo = 0.9, blueSkyHi = 1.0;  // +0x280 / +0x288 (story events change them)

    // ----- function-local statics of the original -------------------------------------------------------------
    bool condensationFlowsOn = false;         // DAT_100a10d8 shared by all cels (CBiomeCel::UpdateState)
    // Statistics of the vapour model (measurement only): %RH put into the air per biome, (evaporation, transpiration)
    std::map<const Biome*, std::pair<double, double>> vapourIn;
    double statCondensed = 0, statRain = 0;    // %RH removed by the condensation sink / added by rain and mist (all cels)
    bool airTempReported = false;             // DAT_100a10dc
    uint16_t oceanLevelShown = 0;             // DAT_100a3428: the ocean level state last drawn on a page (BiosGame)
    bool lookUpReportEnabled = true;          // DAT_1009f84c: 1 until the first LookUpVal overflow has been reported

    // ----- CWorld ----------------------------------------------------------------------------------------------
    void updateWeather();                     // dll:10013420
    bool updateBiomeCels();                   // dll:10013cc0
    void commitBiomeCels();                   // dll:10013c10
    bool updateAirFlows();                    // dll:10013e00
    bool updateWaterFlows();                  // dll:100140b0
    void commitWaterTanks();                  // dll:10014040
    void updateOcean();                       // COcean::UpdateState(DAT_100c1cc8, DAT_100c1c1c) dll:10029990 (state part)
    bool updateAllPlants();                   // dll:10014540
    bool updateNextPlant(i32 now);            // dll:100144f0 (true while more plants wait)
    void resetPlantCursor() { plantCursor_ = kNoCursor; }

    void switchAirFlows(uint16_t id, int on);              // dll:10013e60
    void setAirFlowsReversed(uint16_t id, int reversed);   // dll:10013eb0
    void setAirFlowsDest(uint16_t id, CBiomeCel* dest);    // dll:10013f00
    void switchWaterFlows(uint16_t id, int on);            // dll:10014110
    void toggleWaterFlowsReversed(uint16_t id);            // dll:10014160
    void setWaterFlowsPurity(uint16_t id, double purity);  // dll:100141b0

    // power storage (the machines call these through the shell; HasPower itself is BiosGame::CWorld_HasPower)
    void addPowerDemand(double d);            // dll:100149f0
    void removePowerDemand(double d);         // dll:10014a20
    void addPowerGeneration(double d);        // dll:10014a50
    void removePowerGeneration(double d);     // dll:10014a70
    void powerFailure();                      // dll:10014a90
    void powerRestored();                     // dll:10014b20
    void powerOffFeatures(uint32_t mask);     // dll:10014ae0
    void powerOnFeatures(uint32_t mask);      // dll:10014b60

    // CBiome::UpdateTargetTemp dll:1000a0d0
    void updateTargetTemp(uint32_t biomeAddr);
    // FUN_10014740: removes the fruit of `plant` that grows on it
    void removePlantFruit(CPlant& plant);
    // LookUpVal dll:10007a60 (n = index of the last entry)
    double lookUpVal(uint32_t table, double x, int n);

    // the nine biomes in the order CWorld::UpdateWeather updates their target temperature
    static const uint32_t kBiomeAddrs[9];

private:
    static constexpr size_t kNoCursor = static_cast<size_t>(-1);
    SaveGame& sg_;
    std::map<uint32_t, BiomeParams> biomes_;
    size_t plantCursor_ = kNoCursor;      // CWorld+0x90: the next plant UpdateNextPlant updates (null = none)
};

} // namespace biosys::game
