// The CWorld functions the features use (power, fuse boxes, the feature update loop, the flow switches), and the CBiome
// averages. Free functions on a World; the original's CWorld members are cited by address.
#include "game/airflow.h"
#include "game/biomecel.h"
#include "game/blobrecords.h"
#include "game/eng.h"
#include "game/feat_power.h"
#include "game/featenv.h"
#include "game/feature.h"
#include "game/gameobj.h"
#include "game/obj_fuse.h"
#include "game/feat_sensors.h"
#include "game/feat_climate.h"
#include "game/sim.h"
#include "game/site.h"
#include "game/waterflow.h"
#include "game/watertank.h"
#include <strings.h>

namespace biosys::game {

void bindFeatureHooks(Sim& sim) {
    Sim::Hooks& h = sim.hooks;
    h.featurePowerOn = [](CFeature& f) { f.onPowerOn(); };                                      // vtable +0x68
    h.featurePowerOff = [](CFeature& f) { f.onPowerOff(); };                                    // vtable +0x6c
    h.ventAdjustTarget = [](CFeature& vent, double delta) { dynamic_cast<CVent&>(vent).adjustTarget(delta); };   // CVent+0x60
    h.lungSwitchOff = [](CFeature& lung) { lung.turnOff(); };                                   // vtable +0x74
    h.lungSetMoving = [](CFeature& lung, int v) { dynamic_cast<CLung&>(lung).setMoving(v); };   // lung+0x1dc
}

namespace cworld {

namespace {
// The features list in iteration order. The originals fetch the next node before they call the feature (so a feature
// may not remove itself; none does), the port copies the pointers.
std::vector<CFeature*> features(World& w) {
    std::vector<CFeature*> v;
    for (Item* it : w.list(Table::Features)) v.push_back(&as<CFeature>(it));
    return v;
}
}

WorldHeader& header(World& w) {
    if (!w.state().sim.world) throw Error("world has no CWorld record (GameState::sim.world)");
    return *w.state().sim.world;
}
Timeline& timeline(World& w) {
    if (!w.state().sim.timeline) throw Error("world has no CTimeline record (GameState::sim.timeline)");
    return *w.state().sim.timeline;
}
u32 timeOfDay(World& w) { return static_cast<u32>(timeline(w).m_dwTimeOfDay); }   // DAT_100c1c28
i32 gameTime(World& w) { return timeline(w).m_dwTime; }                          // DAT_100c1c1c
COcean& ocean(World& w) {
    if (!w.state().ocean) throw Error("world has no ocean (DAT_100c1cc8)");
    return as<COcean>(w.state().ocean);
}

// dll:10014910 CWorld::HasPower: is the power group `mask` supplied? The low bits of a feature's mask name its fuse.
u32 hasPower(World& w, u32 mask) {
    u32 pf = header(w).m_dwPowerFlags;
    if ((pf & 0x40000000) == 0) return 0;
    int box;
    if ((mask & 0x1000000) != 0) {
        if ((pf & 0x1000000) == 0) return 0;
        box = 0;
    } else if ((mask & 0x2000000) != 0) {
        if ((pf & 0x2000000) == 0) return 0;
        box = 1;
    } else if ((mask & 0x4000000) != 0) {
        if ((pf & 0x4000000) == 0) return 0;
        box = 2;
    } else if ((mask & 0x8000000) != 0) {
        if ((pf & 0x8000000) == 0 && (mask & 0x10000) == 0) return 0;
        box = 3;
    } else if ((mask & 0x10000000) != 0) {
        if ((pf & 0x10000000) == 0) return 0;
        box = 4;
    } else if ((mask & 0x20000000) != 0) {
        return (pf & 0x20000000) >> 0x1d;
    } else {
        return 1;
    }
    Item* it = w.state().features.fuseBox[box];
    if (!it) throw Error("CWorld::HasPower: fuse box " + std::to_string(box) + " does not exist");
    return (as<CFuseBox>(it).fuseMask() & mask) == mask ? 1 : 0;
}

// The power storage and the flow switches are the simulation core's (sim.h): thin forwards so the machines read like the
// original's `CWorld__AddPowerDemand(&DAT_100c0f60, ...)`
Sim& sim(World& w) {
    if (!w.state().simCore) throw Error("world has no simulation core (GameState::simCore): construct a Sim first");
    return *w.state().simCore;
}
// While the world is being built (SaveGame constructor, buildWorld) there is no Sim yet; the builders' virtual calls
// (Activate / TurnOn on the first features) can add power demand or generation then, which the original adds to the
// CWorld scalars directly. The port does the same on the record.
void addPowerDemand(World& w, double d) {                                          // dll:100149f0
    if (w.state().simCore) { sim(w).addPowerDemand(d); return; }
    WorldHeader& h = header(w);
    h.m_dPowerDemand += d;
    if (h.m_dPowerDemand < 0.0) h.m_dPowerDemand = 0.0; else if (100.0 < h.m_dPowerDemand) h.m_dPowerDemand = 100.0;
}
void removePowerDemand(World& w, double d) {                                       // dll:10014a20
    if (w.state().simCore) { sim(w).removePowerDemand(d); return; }
    WorldHeader& h = header(w);
    h.m_dPowerDemand -= d;
    if (h.m_dPowerDemand < 0.0) h.m_dPowerDemand = 0.0; else if (100.0 < h.m_dPowerDemand) h.m_dPowerDemand = 100.0;
}
void addPowerGeneration(World& w, double d) {                                      // dll:10014a50
    if (w.state().simCore) { sim(w).addPowerGeneration(d); return; }
    header(w).m_dPowerGeneration += d;
}
void removePowerGeneration(World& w, double d) {                                   // dll:10014a70
    if (w.state().simCore) { sim(w).removePowerGeneration(d); return; }
    header(w).m_dPowerGeneration -= d;
}
void powerFailure(World& w) { sim(w).powerFailure(); }                             // dll:10014a90
void powerRestored(World& w) { sim(w).powerRestored(); }                           // dll:10014b20
void powerOffFeatures(World& w, u32 mask) { sim(w).powerOffFeatures(mask); }       // dll:10014ae0
void powerOnFeatures(World& w, u32 mask) { sim(w).powerOnFeatures(mask); }         // dll:10014b60
void switchAirFlows(World& w, i16 id, int on) { sim(w).switchAirFlows(static_cast<u16>(id), on); }              // dll:10013e60
void setAirFlowsReversed(World& w, i16 id, int r) { sim(w).setAirFlowsReversed(static_cast<u16>(id), r); }     // dll:10013eb0
void setAirFlowsDest(World& w, i16 id, CBiomeCel* d) { sim(w).setAirFlowsDest(static_cast<u16>(id), d); }       // dll:10013f00
void switchWaterFlows(World& w, i16 id, int on) { sim(w).switchWaterFlows(static_cast<u16>(id), on); }         // dll:10014110
void toggleWaterFlowsReversed(World& w, i16 id) { sim(w).toggleWaterFlowsReversed(static_cast<u16>(id)); }     // dll:10014160
void setWaterFlowsPurity(World& w, i16 id, double p) { sim(w).setWaterFlowsPurity(static_cast<u16>(id), p); }  // dll:100141b0

namespace {
// The circuit -> room name pairs of OnCircuitPowerOn / Off (DAT_1009f188 "BR", 1009f184 "BS", 1009f180 "BO", 1009f17c
// "BD", 1009f178 "BP"): the basement the player stands in
struct CircuitRoom { u32 bit; const char* room; };
const CircuitRoom kRooms[5] = {{0x1000000, "BR"}, {0x2000000, "BS"}, {0x4000000, "BO"}, {0x8000000, "BD"}, {0x10000000, "BP"}};
bool inCircuitRoom(World& w, u32 mask) {
    CSite* site = w.state().host ? w.state().host->currentSite() : nullptr;
    if (!site) return false;
    for (const CircuitRoom& r : kRooms)
        if ((mask & r.bit) != 0 && strcasecmp(site->name().c_str(), r.room) == 0) return true;
    return false;
}
}

// dll:10014bb0 CWorld::OnCircuitPowerOn(mask): the features of the circuit get power (mask without bit 0x10: the lights)
void onCircuitPowerOn(World& w, u32 mask) {
    if ((header(w).m_dwPowerFlags & 0x40000000u) == 0) return;
    if ((mask & 0x10) == 0) {
        for (CFeature* f : features(w))
            if ((f->powerMask() & mask) == mask) f->onPowerOn();
        return;
    }
    // the room lights: the original tests the circuit bit in the power flags too
    u32 pf = header(w).m_dwPowerFlags;
    if (inCircuitRoom(w, pf & mask)) {
        FeatureHost* h = w.state().host;
        h->playSound("sounds\\basement\\lightson.wav");
        w.state().lightsOn = 0;     // DAT_100c3224
        h->siteLightsChanged(0);
    }
}

// dll:10014d50 CWorld::OnCircuitPowerOff(mask)
void onCircuitPowerOff(World& w, u32 mask) {
    if ((header(w).m_dwPowerFlags & 0x40000000u) == 0) return;
    if ((mask & 0x10) == 0) {
        for (CFeature* f : features(w))
            if ((f->powerMask() & mask) == mask) f->onPowerOff();
        return;
    }
    if (inCircuitRoom(w, mask)) {
        FeatureHost* h = w.state().host;
        h->playSound("sounds\\basement\\lightsof.wav");
        w.state().lightsOn = 1;
        h->siteLightsChanged(1);
    }
}

// dll:10014f70 CWorld::BlowFuse(mask): clears the fuse bits in the fuse box of the circuit, powers the features off and burns
// the matching fuse object
bool blowFuse(World& w, u32 mask) {
    int box;
    if ((mask & 0x1000000) != 0) box = 0;
    else if ((mask & 0x2000000) != 0) box = 1;
    else if ((mask & 0x4000000) != 0) box = 2;
    else if ((mask & 0x10000000) != 0) box = 4;
    else if ((mask & 0x8000000) != 0) box = 3;
    else return false;
    Item* it = w.state().features.fuseBox[box];
    if (!it) throw Error("CWorld::BlowFuse: fuse box " + std::to_string(box) + " does not exist");
    CFuseBox& fb = as<CFuseBox>(it);
    if ((fb.fuseMask() & mask & 0xc0ffffffu) == 0) return false;
    fb.setFuseMask((~mask | 0x3f000000u) & fb.fuseMask());
    onCircuitPowerOff(w, mask);
    for (Item* o : w.related(Rel::WorldFuse, nullptr)) {    // world+0xb8: the fuse objects
        ObjFuse& f = as<ObjFuse>(o);
        if (f.fuseFlags() != mask) continue;
        // FUN_10029440: Eng_ChangeGameObjectVariation(name, "burnt", -2) and on success the variant name is copied
        FeatureHost* h = w.state().host;
        if (h && h->eng() && h->eng()->api().ChangeGameObjectVariation(f.name(), "burnt", -2) == 0) f.setVariant("burnt");
        break;
    }
    return true;
}

// dll:10014860 CWorld::UpdateNextFeature(now): updates one feature; true while more remain (the cursor is world+0x128)
bool updateNextFeature(World& w, i32 now) {
    GameState& gs = w.state();
    const std::vector<Item*>& list = w.list(Table::Features);
    if (gs.featureCursor == 0) {
        if (list.empty()) return false;
        gs.featureCursor = 1;                     // cursor = list head (1-based position, 0 = NULL)
    }
    size_t pos = gs.featureCursor - 1;
    gs.featureCursor = pos + 1 < list.size() ? pos + 2 : 0;    // cursor = node->next
    as<CFeature>(list[pos]).update(now);
    return gs.featureCursor != 0;
}
// dll:100148b0 CWorld::UpdateAllFeatures
bool updateAllFeatures(World& w, i32 now) {
    w.state().featureCursor = 0;
    while (updateNextFeature(w, now)) {}
    return true;
}

void startPageFX(World& w, i32 id) { if (w.state().host) w.state().host->startPageFX(id); }
void stopPageFX(World& w, i32 id) { if (w.state().host) w.state().host->stopPageFX(id); }
void setFXState(World& w, i32 id, int on) { if (w.state().host) w.state().host->setFXState(id, on); }

} // namespace cworld

// dll:1000a100 CBiome::AvgProbeAirTemp: mean of the air-temperature readings of the biome's probes (0 without)
double biomeAvgProbeAirTemp(const Biome& b) {
    double sum = 0.0;
    int n = 0;
    for (Item* s : b.sensors) {
        n++;
        sum += as<CBiomeSensor>(s).airTempReading();
    }
    if (n == 0) return 0.0;
    return sum / n;
}
// dll:1000a160 CBiome::AvgPlantHealth: mean of the cels' plant health average (+0xf0)
double biomeAvgPlantHealth(const Biome& b) {
    double sum = 0.0;
    int n = 0;
    for (Item* c : b.cels) {
        n++;
        sum += as<CBiomeCel>(c).plantHealthAvg();
    }
    if (n == 0) return 0.0;
    return sum / n;
}
// dll:1000a1c0 CBiome::HealthTrend: cels with a rising average (flag 0x20) minus those with a falling one (0x40)
int biomeHealthTrend(const Biome& b) {
    int n = 0;
    for (Item* c : b.cels) {
        u32 f = as<CBiomeCel>(c).flags();
        if ((f & 0x20) == 0) {
            if ((f & 0x40) != 0) n--;
        } else {
            n++;
        }
    }
    return n;
}

} // namespace biosys::game
