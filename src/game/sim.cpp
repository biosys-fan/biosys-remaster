// The simulation core: CWorld::UpdateWeather and the loops that drive the cels, flows, tanks and plants, the CWorld
// helpers they use, LookUpVal. Every function carries the address of the original it translates.
// Operation order and rounding follow the decompiled expressions; no fused multiply-add (see biomecel.cpp).
#pragma clang fp contract(off)
#include "game/sim.h"
#include "game/airflow.h"
#include "game/biomecel.h"
#include "game/feature.h"
#include "game/obj_food.h"
#include "game/plant.h"
#include "game/sim_tables.h"
#include "game/waterflow.h"
#include "game/watertank.h"
#include <cmath>
#include <cstdio>

namespace biosys::game {

const uint32_t Sim::kBiomeAddrs[9] = {0x100c1c50, 0x100c0ee8, 0x100c1280, 0x100c2768, 0x100c24e8,
                                      0x100c2560, 0x100c1208, 0x100c1ba0, 0x100c25d8};

Sim::Sim(SaveGame& sg) : sg_(sg) {
    // CBiome::CBiome dll:10009f60 leaves gain 0, flags 0, fixed temperature 0, target 0; World__InitBiomeParams
    // dll:1002b560 then sets (the nine static objects: rainforest, savannah, ocean, undersea, dead biome, Club Eden,
    // house, basements, tunnels)
    for (uint32_t a : kBiomeAddrs) biomes_[a] = BiomeParams();
    for (uint32_t a : {0x100c1c50u, 0x100c0ee8u, 0x100c1280u, 0x100c24e8u, 0x100c2560u}) biomes_[a].gain = 10.0;
    for (uint32_t a : {0x100c1ba0u, 0x100c2768u, 0x100c25d8u}) {       // basements, undersea, tunnels
        biomes_[a].flags |= 1;
        biomes_[a].fixedTemp = 15.0;
    }
    for (uint32_t a : {0x100c1c50u, 0x100c0ee8u, 0x100c1280u, 0x100c24e8u}) biomes_[a].flags |= 4;
    // The CWorld constructor dll:10010f70 runs at static-initialisation time: its four RandRange draws come from the
    // C runtime's initial seed 1 (before srand). The first and third (BlueSky, wind speed) are overwritten by the
    // loaded game; the second is the BlueSky target, which is not saved.
    Rng boot(1);
    boot.randRange(0.9, 1.0);                       // +0x270 BlueSky
    blueSkyTarget = boot.randRange(blueSkyLo, blueSkyHi);   // +0x278
    sg_.world.state().simCore = this;
}

COcean& Sim::ocean() {
    Item* o = sg_.world.state().ocean;
    if (!o) throw Error("the world has no ocean");
    return as<COcean>(o);
}

BiomeParams& Sim::biomeParams(uint32_t addr) {
    auto it = biomes_.find(addr);
    if (it == biomes_.end()) throw Error("unknown biome object");
    return it->second;
}

BiomeParams& Sim::biomeParams(const Biome* b) {
    if (!b) throw Error("cel without a biome (crash in the original)");
    return biomeParams(b->addr);
}

void Sim::log(const std::string& text) {
    sg_.world.state().message = text;
    if (hooks.log) hooks.log(text);
    else sg_.world.log(text);
}

// dll:1000a0d0
void Sim::updateTargetTemp(uint32_t addr) {
    BiomeParams& b = biomeParams(addr);
    if ((b.flags & 1) == 0) b.target = b.gain * sunlight() + extTemp();
    else b.target = b.fixedTemp;
}

// dll:10013420: wind, sky, outside temperature, then the cels, then the stored power
void Sim::updateWeather() {
    WorldHeader& w = header();
    const uint32_t dtU = static_cast<uint32_t>(time()) - static_cast<uint32_t>(w.m_dwStamp_1c8);
    if (dtU == 0) return;
    const double dt = static_cast<double>(dtU);
    w.m_dwStamp_1c8 = time();
    Rng& rng = sg_.world.state().rng;
    double step = dt * 2.777777777777778e-05;          // 0.1 per hour
    // sky clarity
    if (step < std::fabs(blueSkyTarget - w.m_dBlueSky)) {
        w.m_dBlueSky = (w.m_dBlueSky < blueSkyTarget) ? w.m_dBlueSky + step : w.m_dBlueSky - step;
    } else {
        blueSkyTarget = rng.randRange(blueSkyLo, blueSkyHi);
    }
    if (hooks.buildDayPalette) hooks.buildDayPalette();     // FUN_1000e3a0(DAT_100c1f84)
    w.m_dSunlight = (w.m_dBlueSky * 0.7 + 0.3) * daylight();
    // daily maximum and minimum of the outside temperature
    if (step < std::fabs(w.m_dDayMaxExtTempTarget - w.m_dDayMaxExtTemp)) {
        w.m_dDayMaxExtTemp = (w.m_dDayMaxExtTemp < w.m_dDayMaxExtTempTarget) ? w.m_dDayMaxExtTemp + step
                                                                              : w.m_dDayMaxExtTemp - step;
    } else {
        w.m_dDayMaxExtTempTarget = rng.randRange(w.m_dDayMaxExtTempLo, w.m_dDayMaxExtTempHi);
    }
    if (step < std::fabs(w.m_dNightMinExtTempTarget - w.m_dNightMinExtTemp)) {
        w.m_dNightMinExtTemp = (w.m_dNightMinExtTemp < w.m_dNightMinExtTempTarget) ? w.m_dNightMinExtTemp + step
                                                                                    : w.m_dNightMinExtTemp - step;
    } else {
        w.m_dNightMinExtTempTarget = rng.randRange(w.m_dNightMinExtTempLo, w.m_dNightMinExtTempHi);
    }
    // outside temperature: minimum at 06:00, maximum at 12:00
    const uint32_t tod = timeOfDay();
    double f;
    if (tod < 0x5460) f = static_cast<double>(0x5460 - tod) * 1.54320987654321e-05;
    else if (tod < 0xa8c0) f = static_cast<double>(tod - 0x5460) * 4.6296296296296294e-05;
    else f = static_cast<double>(tod - 0xa8c0) * -1.54320987654321e-05 + 1.0;
    w.m_dExtTemp = (w.m_dDayMaxExtTemp - w.m_dNightMinExtTemp) * f + w.m_dNightMinExtTemp;
    // wind: the same random walk, ten times as fast
    double wstep = dt * 0.0002777777777777778;
    if (wstep < std::fabs(w.m_dWindSpeedTarget - w.m_dWindSpeed)) {
        w.m_dWindSpeed = (w.m_dWindSpeed < w.m_dWindSpeedTarget) ? w.m_dWindSpeed + wstep : w.m_dWindSpeed - wstep;
    } else {
        w.m_dWindSpeedTarget = rng.randRange(w.m_dWindSpeedLo, w.m_dWindSpeedHi);
    }
    for (uint32_t a : kBiomeAddrs) updateTargetTemp(a);
    updateBiomeCels();
    // stored power
    w.m_dPower = w.m_dPower - w.m_dPowerDemand * dt;
    w.m_dPower = w.m_dPowerGeneration * dt + w.m_dPower;
    if ((w.m_dwPowerFlags & 0x40000000u) == 0) {      // blacked out
        if (3.0 <= w.m_dPower) {
            powerRestored();
            return;
        }
        if (w.m_dPower < 0.0) w.m_dPower = 0.0;
    } else {
        if (w.m_dPower <= 0.0) {
            powerFailure();
            return;
        }
        if (200.0 < w.m_dPower) w.m_dPower = 200.0;
    }
}

// dll:10013cc0
bool Sim::updateBiomeCels() {
    WorldHeader& w = header();
    const uint32_t dt = static_cast<uint32_t>(time()) - static_cast<uint32_t>(w.m_dwStamp_164);
    uint32_t dead = 0;
    w.m_dwStamp_164 = time();
    double sum = 0.0;
    for (Item* it : sg_.world.list(Table::Cels)) {
        CBiomeCel& c = as<CBiomeCel>(it);
        c.updateState(*this, dt);
        c.updatePlantHealthAvg();
        if ((c.flags() & 0x80000000u) != 0) {          // the cels of the dead biome
            sum = c.plantHealthAvg() + sum;
            dead++;
        }
    }
    // the player is awake and not on a journey (DAT_100c1fe4 / DAT_100c1f8c): revival milestones of the dead biome
    const Self& me = sg_.self;
    if (me.m_bAwake != 0 && me.m_pPath == nullptr) {
        double mean = sum / static_cast<double>(dead);
        if (0.01 < mean && hooks.triggerEvent) hooks.triggerEvent(0x10000000);   // CGame::TriggerEvent
        if (0.3 < mean && hooks.triggerEvent) hooks.triggerEvent(0x20000000);
        if (0.4 < mean && hooks.triggerEvent) hooks.triggerEvent(0x40000000);
    }
    return true;
}

// dll:10013c10
void Sim::commitBiomeCels() {
    for (Item* it : sg_.world.list(Table::Cels)) as<CBiomeCel>(it).commit();
}

// dll:10013e00
bool Sim::updateAirFlows() {
    WorldHeader& w = header();
    const i32 dt = time() - w.m_dwStamp_184;
    w.m_dwStamp_184 = time();
    for (Item* it : sg_.world.list(Table::Airflows)) as<CAirFlow>(it).update(*this, static_cast<uint32_t>(dt));
    commitBiomeCels();
    return true;
}

// dll:100140b0
bool Sim::updateWaterFlows() {
    WorldHeader& w = header();
    const i32 dt = time() - w.m_dwStamp_1c0;
    w.m_dwStamp_1c0 = time();
    for (Item* it : sg_.world.list(Table::Waterflows)) as<CWaterFlow>(it).update(*this, static_cast<uint32_t>(dt));
    commitWaterTanks();
    return true;
}

// dll:10014040
void Sim::commitWaterTanks() {
    for (Item* it : sg_.world.list(Table::Tanks)) as<CWaterTank>(it).commit();
}

// COcean::UpdateState(DAT_100c1cc8, DAT_100c1c1c)
void Sim::updateOcean() {
    ocean().updateState(*this, time());
}

// dll:10014540
bool Sim::updateAllPlants() {
    plantCursor_ = kNoCursor;
    const i32 now = time();
    while (updateNextPlant(now)) {}
    return true;
}

// dll:100144f0
bool Sim::updateNextPlant(i32 now) {
    const std::vector<Item*>& plants = sg_.world.list(Table::Plants);
    if (plantCursor_ == kNoCursor) {
        if (plants.empty()) return false;
        plantCursor_ = 0;
    }
    const size_t cur = plantCursor_;
    plantCursor_ = (cur + 1 < plants.size()) ? cur + 1 : kNoCursor;
    as<CPlant>(plants[cur]).update(*this, now);
    return plantCursor_ != kNoCursor;
}

// dll:10013e60
void Sim::switchAirFlows(uint16_t id, int on) {
    if (id == 0) return;
    for (Item* it : sg_.world.list(Table::Airflows)) {
        CAirFlow& f = as<CAirFlow>(it);
        if (f.id() == id) f.turnOn(on);
    }
}

// dll:10013eb0
void Sim::setAirFlowsReversed(uint16_t id, int reversed) {
    for (Item* it : sg_.world.list(Table::Airflows)) {
        CAirFlow& f = as<CAirFlow>(it);
        if (f.id() == id) f.setReversed(reversed);
    }
}

// dll:10013f00
void Sim::setAirFlowsDest(uint16_t id, CBiomeCel* dest) {
    for (Item* it : sg_.world.list(Table::Airflows)) {
        CAirFlow& f = as<CAirFlow>(it);
        if (f.id() == id) f.setTo(dest);
    }
}

// dll:10014110
void Sim::switchWaterFlows(uint16_t id, int on) {
    if (id == 0) return;
    for (Item* it : sg_.world.list(Table::Waterflows)) {
        CWaterFlow& f = as<CWaterFlow>(it);
        if (f.id() == id) f.turnOn(on);
    }
}

// dll:10014160
void Sim::toggleWaterFlowsReversed(uint16_t id) {
    for (Item* it : sg_.world.list(Table::Waterflows)) {
        CWaterFlow& f = as<CWaterFlow>(it);
        if (f.id() == id) f.toggleReversed();
    }
}

// dll:100141b0
void Sim::setWaterFlowsPurity(uint16_t id, double purity) {
    for (Item* it : sg_.world.list(Table::Waterflows)) {
        CWaterFlow& f = as<CWaterFlow>(it);
        if (f.id() == id) f.setPurityOverride(purity);
    }
}

// dll:100149f0 / 10014a20 (the demand stays within 0..100)
void Sim::addPowerDemand(double d) {
    header().m_dPowerDemand = header().m_dPowerDemand + d;
    clampD(header().m_dPowerDemand, 0.0, 100.0);
}
void Sim::removePowerDemand(double d) {
    header().m_dPowerDemand = header().m_dPowerDemand - d;
    clampD(header().m_dPowerDemand, 0.0, 100.0);
}
// dll:10014a50 / 10014a70
void Sim::addPowerGeneration(double d) { header().m_dPowerGeneration = header().m_dPowerGeneration + d; }
void Sim::removePowerGeneration(double d) { header().m_dPowerGeneration = header().m_dPowerGeneration - d; }

// dll:10014a90: every machine learns that the power is gone
void Sim::powerFailure() {
    header().m_dwPowerFlags &= 0xbfffffffu;
    header().m_dPower = 0.0;
    for (Item* it : sg_.world.list(Table::Features)) {
        if (hooks.featurePowerOff) hooks.featurePowerOff(as<CFeature>(it));   // feature vtable +0x6c
    }
}

// dll:10014b20
void Sim::powerRestored() {
    header().m_dwPowerFlags |= 0x40000000u;
    for (Item* it : sg_.world.list(Table::Features)) {
        if (hooks.featurePowerOn) hooks.featurePowerOn(as<CFeature>(it));     // feature vtable +0x68
    }
}

// dll:10014ae0
void Sim::powerOffFeatures(uint32_t mask) {
    for (Item* it : sg_.world.list(Table::Features)) {
        CFeature& f = as<CFeature>(it);
        if ((f.powerMask() & mask) != 0 && hooks.featurePowerOff) hooks.featurePowerOff(f);   // vtable +0x6c
    }
}

// dll:10014b60 (only while the power is on)
void Sim::powerOnFeatures(uint32_t mask) {
    if ((header().m_dwPowerFlags & 0x40000000u) == 0) return;
    for (Item* it : sg_.world.list(Table::Features)) {
        CFeature& f = as<CFeature>(it);
        if ((f.powerMask() & mask) != 0 && hooks.featurePowerOn) hooks.featurePowerOn(f);     // vtable +0x68
    }
}

// dll:10014740: the fruit that grows on a dying plant disappears from the scene
void Sim::removePlantFruit(CPlant& plant) {
    for (Item* it : sg_.world.related(Rel::WorldPlantFood, nullptr)) {
        ObjFood* food = dynamic_cast<ObjFood*>(it);
        if (food && food->plant() == &plant && food->state() == 2) {
            if (hooks.removeFromScene) hooks.removeFromScene(*food);   // the virtual +0x94 including its engine call
            else food->removeFromScene();
        }
    }
}

// dll:10007a60. The step table holds (x, y) pairs, `n` is the index of the last one: the y of the first entry whose x
// is not below the argument. Beyond the last x it reports once (log, "doh" sound) and returns the last y.
double Sim::lookUpVal(uint32_t table, double x, int n) {
    const StepTable* t = stepTable(table);
    if (!t || t->last != n) throw Error("LookUpVal: no step table at that address with that length");
    const double* p = t->xy;
    const double* last = p + 2 * n;
    if (x <= last[0]) {
        int i = 0;
        if (p[0] < x) {
            const double* q = p;
            do {
                q += 2;
                i++;
            } while (q[0] < x);
        }
        return p[2 * i + 1];
    }
    char b[160];
    std::snprintf(b, sizeof b, "BiosGame.dll: dStat(%f) > lookUp[ulLastEntry].dTopLimit(%f) in LookUpVal", x, last[0]);
    sg_.world.state().message = b;
    if (lookUpReportEnabled) {
        log(b);
        if (hooks.playSound) hooks.playSound("sounds\\silly\\doh.wav");
        // PORT: the original now dumps the ocean (vtable +0x1c) and one water tank (debugging text only)
        lookUpReportEnabled = false;
    }
    return last[1];
}

} // namespace biosys::game
