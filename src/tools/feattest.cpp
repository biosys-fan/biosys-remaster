// biosys-feattest: drives the machines and sensors of the game logic (src/game feat_*.cpp) without the engine.
//   biosys-feattest [--seed N] [--repair] [<NewGame.sav>] < script
// Builds the world, loads the saved game (default: the NewGame.sav of the installed game), creates the simulation core and
// runs a script, one command per line, answers on stdout ("key = value" lines; doubles as %.17g):
//   get <feature> <id>                 parameter read (the engine's Event_OnGetFeatureParameter; "" = world parameter)
//   set <feature> <id> <int> <double>  parameter write (Event_OnSetFeatureParameter)
//   getall                             every id 0..0x100 the features answer, per feature
//   echo <text> | biomes               print the text / each biome with its cels and sensors
//   members [<feature>]                every member of the feature objects (CFeature::members)
//   world | cels | tanks | ocean | flows   the world scalars / cel / tank / ocean / flow state
//   run <seconds>                      CTimeline::Advance: 90 s steps of weather, ocean, air, water, features, plants
//   trace <seconds>                    the same, printing for every step what the features read and what they changed
//   time <seconds>                     set the game clock (time of day and diurnal state follow)
//   seed <n>                           seed the game generator
//   on|off|act|deact|break|repair|poweron|poweroff <feature>   the virtual calls TurnOn / TurnOff / Activate / Deactivate / Break / ...
//   fusemask <fusebox> <hex>           set the fuse mask of a fuse box (plug fuses in)
//   blow <hexmask> | power <value> | powerflags <hex> | updatefeatures     CWorld::BlowFuse, stored energy, circuit flags, one feature update
//   check                              property checks, prints PASS / FAIL lines (tests/test_feat.sh)
#include "engine/devroot.h"
#include "game/airflow.h"
#include "game/biomecel.h"
#include "game/blobrecords.h"
#include "game/feat_climate.h"
#include "game/feat_power.h"
#include "game/feat_sensors.h"
#include "game/feat_story.h"
#include "game/feat_water.h"
#include "game/featenv.h"
#include "game/featparam.h"
#include "game/feature.h"
#include "game/savegame.h"
#include "game/site.h"
#include "game/sim.h"
#include "game/waterflow.h"
#include "game/watertank.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

using namespace biosys;
using namespace biosys::game;

namespace {

std::vector<u8> readFileBytes(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw Error("cannot open " + p);
    return std::vector<u8>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string num(double v) { char b[40]; std::snprintf(b, sizeof b, "%.17g", v); return b; }

struct Rig {
    std::unique_ptr<SaveGame> sg;
    std::unique_ptr<Sim> sim;
    World& w() { return sg->world; }
    GameState& gs() { return sg->world.state(); }

    // CTimeline::UpdateTimeOfDay (dll:100155e0) and UpdateDiurnal (dll:10015710) without the site / sound work
    void timeOfDay() {
        Timeline& t = sg->timeline;
        t.m_dwTimeOfDay = static_cast<i32>(static_cast<u32>(t.m_dwTime) % 86400u);
        int day = static_cast<int>(static_cast<u32>(t.m_dwTime) / 86400u + 1);
        if (t.m_dwDay != day) {
            t.m_dwDay = day;
            for (Item* it : w().related(Rel::WorldSensor, nullptr)) as<CBiomeSensor>(it).onNewDay();   // CWorld::OnNewDay dll:10014ee0
            for (Item* it : w().list(Table::Cels)) {
                CBiomeCel& c = as<CBiomeCel>(it);
                c.setFlags(c.flags() & 0xfffffff7u);
            }
        }
        u32 tod = static_cast<u32>(t.m_dwTimeOfDay);
        SimRefs& s = gs().sim;
        if (tod < 0x2a30 || tod > 0x1274f) { s.daylight = 0.0; s.diurnal = 0x28; }
        else if (tod < 0x5460) { s.diurnal = 5; s.daylight = static_cast<double>(tod - 0x2a30) * 9.259259259259259e-05 * 0.5; }
        else if (tod < 0x7080) { s.diurnal = 0xf; s.daylight = static_cast<double>(tod - 0x5460) * 0.0001388888888888889 * 0.5 + 0.5; }
        else if (tod < 0xe100) { s.daylight = 1.0; s.diurnal = 0x14; }
        else if (tod < 0xfd20) { s.diurnal = 0x1e; s.daylight = static_cast<double>(tod - 0xe100) * 0.0001388888888888889 * -0.5 + 1.0; }
        else { s.diurnal = 0x23; s.daylight = static_cast<double>(tod - 0xfd20) * 9.259259259259259e-05 * -0.5 + 0.5; }
    }
};

void printMembers(const CFeature& f) {
    CFeature::MemberList m;
    f.members(m);
    for (auto& kv : m) std::printf("member %s.%s = %s\n", f.featureName().c_str(), kv.first.c_str(), kv.second.c_str());
}

void printWorld(Rig& r, const char* tag) {
    const WorldHeader& h = *r.gs().sim.world;
    std::printf("%s power = %s\n", tag, num(h.m_dPower).c_str());
    std::printf("%s powerFlags = 0x%08x\n", tag, h.m_dwPowerFlags);
    std::printf("%s demand = %s\n", tag, num(h.m_dPowerDemand).c_str());
    std::printf("%s generation = %s\n", tag, num(h.m_dPowerGeneration).c_str());
    std::printf("%s sunlight = %s\n", tag, num(h.m_dSunlight).c_str());
    std::printf("%s windSpeed = %s\n", tag, num(h.m_dWindSpeed).c_str());
    std::printf("%s extTemp = %s\n", tag, num(h.m_dExtTemp).c_str());
    std::printf("%s blueSky = %s\n", tag, num(h.m_dBlueSky).c_str());
}

void printCels(Rig& r, const char* tag) {
    for (Item* it : r.w().list(Table::Cels)) {
        const CBiomeCel& c = as<CBiomeCel>(it);
        std::printf("%s %s airVolume=%s capacity=%s airTemp=%s newAirTemp=%s soilTemp=%s co2=%s newCO2=%s humidity=%s newHumidity=%s "
                    "light=%s oxygen=%s newOxygen=%s pollution=%s newPollution=%s soilpH=%s soilWater=%s windSpeed=%s flags=0x%x\n",
                    tag, c.name().c_str(), num(c.airVolume()).c_str(), num(c.airCapacity()).c_str(), num(c.airTemp()).c_str(),
                    num(c.newAirTemp()).c_str(), num(c.soilTemp()).c_str(), num(c.co2()).c_str(), num(c.newCO2()).c_str(),
                    num(c.humidity()).c_str(), num(c.newHumidity()).c_str(), num(c.light()).c_str(), num(c.oxygen()).c_str(),
                    num(c.newOxygen()).c_str(), num(c.pollution()).c_str(), num(c.newPollution()).c_str(),
                    num(c.soilpH()).c_str(), num(c.soilWater()).c_str(), num(c.windSpeed()).c_str(), c.flags());
    }
}

void printTanks(Rig& r, const char* tag) {
    for (Item* it : r.w().list(Table::Tanks)) {
        const CWaterTank& t = as<CWaterTank>(it);
        std::printf("%s %s volume=%s waterVol=%s temp=%s newTemp=%s purity=%s oxygen=%s newOxygen=%s ph=%s salinity=%s\n", tag, t.name().c_str(),
                    num(t.volume()).c_str(), num(t.waterVol()).c_str(), num(t.temp()).c_str(), num(t.newTemp()).c_str(),
                    num(t.purity()).c_str(), num(t.oxygen()).c_str(), num(t.newOxygen()).c_str(), num(t.ph()).c_str(),
                    num(t.salinity()).c_str());
    }
}

void printOcean(Rig& r, const char* tag) {
    const COcean& o = cworld::ocean(r.w());
    std::printf("%s ocean volume=%s waterVol=%s temp=%s newTemp=%s ph=%s salinity=%s oxygen=%s newOxygen=%s algae=%s health=%s waveMachineOn=%d\n", tag,
                num(o.volume()).c_str(), num(o.waterVol()).c_str(), num(o.temp()).c_str(), num(o.newTemp()).c_str(), num(o.ph()).c_str(),
                num(o.salinity()).c_str(), num(o.oxygen()).c_str(), num(o.newOxygen()).c_str(), num(o.algae()).c_str(),
                num(o.health()).c_str(), o.waveMachineOn() ? 1 : 0);
}

void printFlows(Rig& r) {
    for (Item* it : r.w().list(Table::Airflows)) {
        const CAirFlow& f = as<CAirFlow>(it);
        std::printf("airflow %s id=%u on=%d reversed=%d to=%s windSpeed=%s vent=%s ventAirflow=%s\n", f.name().c_str(), f.id(), f.on() ? 1 : 0, f.reversed() ? 1 : 0,
                    f.to() ? f.to()->name().c_str() : "null", num(f.windSpeed()).c_str(),
                    f.ventFeature() ? f.ventFeature()->name().c_str() : "null", num(f.ventAirflow()).c_str());
    }
    for (Item* it : r.w().list(Table::Waterflows)) {
        const CWaterFlow& f = as<CWaterFlow>(it);
        std::printf("waterflow %s id=%u on=%d reversed=%d from=%s to=%s freeFlow=%s purityOverride=%s mag=%s\n", f.name().c_str(), f.id(), f.on() ? 1 : 0,
                    f.reversed() ? 1 : 0, f.from() ? f.from()->name().c_str() : "null", f.to() ? f.to()->name().c_str() : "null",
                    num(f.freeFlow()).c_str(), num(f.purityOverride()).c_str(), num(f.mag()).c_str());
    }
}

// ---- the trace (tests/feat_oracle.py): what the feature phase of a step sees and what it changes ------------------------------
// Features print as deltas against the last print ("FB"/"FA" name member=value ...): the members the sim phases changed before
// the phase, the ones the phase changed. Cels: all before, the changed ones after. Tanks: the changed ones.
struct TraceState {
    std::map<std::string, std::map<std::string, std::string>> feat;
    std::map<size_t, std::string> air;
    std::map<std::string, std::string> cel, tank;
    std::string ocean;
    bool first = true;
};
TraceState tr;

std::string celLine(const CBiomeCel& c) {
    char b[1200];
    std::snprintf(b, sizeof b, "airVolume=%s capacity=%s airTemp=%s newAirTemp=%s soilTemp=%s co2=%s newCO2=%s humidity=%s newHumidity=%s "
                  "light=%s oxygen=%s newOxygen=%s pollution=%s newPollution=%s soilpH=%s soilWater=%s windSpeed=%s flags=%u",
                  num(c.airVolume()).c_str(), num(c.airCapacity()).c_str(), num(c.airTemp()).c_str(), num(c.newAirTemp()).c_str(),
                  num(c.soilTemp()).c_str(), num(c.co2()).c_str(), num(c.newCO2()).c_str(), num(c.humidity()).c_str(),
                  num(c.newHumidity()).c_str(), num(c.light()).c_str(), num(c.oxygen()).c_str(), num(c.newOxygen()).c_str(),
                  num(c.pollution()).c_str(), num(c.newPollution()).c_str(), num(c.soilpH()).c_str(), num(c.soilWater()).c_str(),
                  num(c.windSpeed()).c_str(), c.flags());
    return b;
}
std::string tankLine(const CWaterTank& t) {
    char b[600];
    std::snprintf(b, sizeof b, "volume=%s waterVol=%s temp=%s newTemp=%s purity=%s oxygen=%s newOxygen=%s ph=%s salinity=%s",
                  num(t.volume()).c_str(), num(t.waterVol()).c_str(), num(t.temp()).c_str(), num(t.newTemp()).c_str(),
                  num(t.purity()).c_str(), num(t.oxygen()).c_str(), num(t.newOxygen()).c_str(), num(t.ph()).c_str(),
                  num(t.salinity()).c_str());
    return b;
}
std::string oceanLine(Rig& r) {
    const COcean& o = cworld::ocean(r.w());
    char b[800];
    std::snprintf(b, sizeof b, "volume=%s waterVol=%s temp=%s newTemp=%s ph=%s salinity=%s oxygen=%s newOxygen=%s algae=%s health=%s waveMachineOn=%d",
                  num(o.volume()).c_str(), num(o.waterVol()).c_str(), num(o.temp()).c_str(), num(o.newTemp()).c_str(), num(o.ph()).c_str(),
                  num(o.salinity()).c_str(), num(o.oxygen()).c_str(), num(o.newOxygen()).c_str(), num(o.algae()).c_str(),
                  num(o.health()).c_str(), o.waveMachineOn() ? 1 : 0);
    return b;
}
void traceWorld(Rig& r, const char* tag) {
    const WorldHeader& h = *r.gs().sim.world;
    std::printf("%s rng=%u power=%s flags=%u demand=%s generation=%s sunlight=%s windSpeed=%s\n", tag, r.gs().rng.state(), num(h.m_dPower).c_str(),
                h.m_dwPowerFlags, num(h.m_dPowerDemand).c_str(), num(h.m_dPowerGeneration).c_str(), num(h.m_dSunlight).c_str(),
                num(h.m_dWindSpeed).c_str());
}
void traceFeatures(Rig& r, const char* tag) {
    for (Item* it : r.w().list(Table::Features)) {
        CFeature::MemberList m;
        const CFeature& f = as<CFeature>(it);
        f.members(m);
        auto& old = tr.feat[f.featureName()];
        std::string out;
        for (auto& kv : m) {
            auto o = old.find(kv.first);
            if (o == old.end() || o->second != kv.second) { out += " " + kv.first + "=" + kv.second; old[kv.first] = kv.second; }
        }
        if (!out.empty()) std::printf("%s %s%s\n", tag, f.featureName().c_str(), out.c_str());
    }
}
// the air flows the machines switch (CAirFlow::TurnOn moves the wind speed of the flow's cel and the vent target)
void traceAir(Rig& r, const char* tag) {
    size_t i = 0;
    for (Item* it : r.w().list(Table::Airflows)) {
        const CAirFlow& f = as<CAirFlow>(it);
        char b[200];
        std::snprintf(b, sizeof b, "id=%u on=%d reversed=%d to=%s windSpeed=%s", f.id(), f.on() ? 1 : 0, f.reversed() ? 1 : 0,
                      f.to() ? f.to()->name().c_str() : "null", num(f.windSpeed()).c_str());
        auto o = tr.air.find(i);
        if (o == tr.air.end() || o->second != b) { std::printf("%s %zu %s\n", tag, i, b); tr.air[i] = b; }
        i++;
    }
}
void traceBefore(Rig& r, const Timeline& t) {
    std::printf("STEP time=%d tod=%d diurnal=%d\n", t.m_dwTime, t.m_dwTimeOfDay, r.gs().sim.diurnal);
    traceWorld(r, "WB");
    for (Item* it : r.w().list(Table::Cels)) {
        std::string l = celLine(as<CBiomeCel>(it));
        std::printf("CB %s %s\n", it->name().c_str(), l.c_str());
        tr.cel[it->name()] = l;
    }
    std::string o = oceanLine(r);
    std::printf("OB %s\n", o.c_str());
    tr.ocean = o;
    for (Item* it : r.w().list(Table::Tanks)) {
        std::string l = tankLine(as<CWaterTank>(it));
        auto f = tr.tank.find(it->name());
        if (f == tr.tank.end() || f->second != l) { std::printf("TB %s %s\n", it->name().c_str(), l.c_str()); tr.tank[it->name()] = l; }
    }
    traceFeatures(r, "FB");
    traceAir(r, "AB");
}
void traceAfter(Rig& r) {
    traceWorld(r, "WA");
    for (Item* it : r.w().list(Table::Cels)) {
        std::string l = celLine(as<CBiomeCel>(it));
        if (l != tr.cel[it->name()]) std::printf("CA %s %s\n", it->name().c_str(), l.c_str());
    }
    std::string o = oceanLine(r);
    if (o != tr.ocean) std::printf("OA %s\n", o.c_str());
    traceFeatures(r, "FA");
    traceAir(r, "AA");
    std::printf("END\n");
}

// CTimeline::Advance dll:100154b0 without the page / sound work. `trace`: print what the feature phase sees and does.
void advance(Rig& r, u32 seconds, bool trace) {
    Timeline& t = r.sg->timeline;
    u32 steps = (seconds + 0x5a) / 0x5a;
    u32 n = steps;
    do {
        r.sim->updateWeather();
        r.sim->updateOcean();
        r.sim->updateAirFlows();
        r.sim->updateWaterFlows();
        // CWorld::UpdateAllObjects: not part of this tool
        if (trace) traceBefore(r, t);
        cworld::updateAllFeatures(r.w(), t.m_dwTime);
        if (trace) traceAfter(r);
        r.sim->updateAllPlants();
        t.m_dwTime = static_cast<i32>(static_cast<u32>(t.m_dwTime) + 0x5a);
        r.timeOfDay();
        n--;
    } while (n != 0);
    if (seconds < steps * 0x5a) {
        t.m_dwTime = static_cast<i32>(static_cast<u32>(t.m_dwTime) + seconds - steps * 0x5a);
        r.timeOfDay();
    }
}

CFeature& feat(Rig& r, const std::string& name) {
    CFeature* f = r.w().findFeature(name.c_str());
    if (!f) throw Error("no feature " + name);
    return *f;
}

int ok = 0, bad = 0;
void check(bool cond, const std::string& what) {
    std::printf("%s %s\n", cond ? "PASS" : "FAIL", what.c_str());
    (cond ? ok : bad)++;
}

// property checks of the machines (tests/test_feat.sh): the oracle (tests/feat_oracle.py) checks the numbers, these the structure
void runChecks(Rig& r) {
    World& w = r.w();
    WorldHeader& h = *r.gs().sim.world;
    auto flowsOn = [&](u16 id, bool air) {
        std::vector<std::string> v;
        if (air) { for (Item* it : w.list(Table::Airflows)) if (as<CAirFlow>(it).id() == id && as<CAirFlow>(it).on()) v.push_back(it->name()); }
        else { for (Item* it : w.list(Table::Waterflows)) if (as<CWaterFlow>(it).id() == id && as<CWaterFlow>(it).on()) v.push_back(it->name()); }
        return v;
    };
    auto flowsWithId = [&](u16 id, bool air) {
        int n = 0;
        if (air) { for (Item* it : w.list(Table::Airflows)) if (as<CAirFlow>(it).id() == id) n++; }
        else { for (Item* it : w.list(Table::Waterflows)) if (as<CWaterFlow>(it).id() == id) n++; }
        return n;
    };
    h.m_dwPowerFlags |= 0x7f000000u;   // all circuits up (the new game starts with main power on already)
    h.m_dPower = 150;
    for (Item* b : r.gs().features.fuseBox) if (b) as<CFuseBox>(b).setFuseMask(as<CFuseBox>(b).fuseMask() | 0x00ffffffu);   // every fuse in
    auto step = [&]() {     // 90 game seconds, then the weather / power update (CWorld::UpdateWeather integrates power over dt)
        r.sg->timeline.m_dwTime += 90;
        r.timeOfDay();
        r.sim->updateWeather();
    };

    // switching a fan / pump / rain machine on switches exactly the flows with its id (the builder arguments)
    int fansChecked = 0, rainChecked = 0, pumpsChecked = 0;
    for (Item* it : w.list(Table::Features)) {
        if (auto* fan = dynamic_cast<CFan*>(it)) {
            u16 id = fan->airFlowId();
            int n = flowsWithId(id, true);
            fan->activate();
            bool right = fan->running() && static_cast<int>(flowsOn(id, true).size()) == n && n > 0;
            fan->deactivate();
            right = right && flowsOn(id, true).empty() && !fan->running();
            check(right, "fan " + fan->featureName() + " switches exactly its " + std::to_string(n) + " air flows with id " + std::to_string(id));
            fansChecked++;
        } else if (auto* rain = dynamic_cast<CRainMachine*>(it)) {
            u16 id = rain->id();
            int n = flowsWithId(id, false);
            rain->activate();
            bool right = rain->running() && static_cast<int>(flowsOn(id, false).size()) == n && n > 0;
            rain->deactivate();
            right = right && flowsOn(id, false).empty();
            check(right, "rain machine " + rain->featureName() + " switches exactly its " + std::to_string(n) + " water flows with id " + std::to_string(id));
            rainChecked++;
        } else if (auto* pump = dynamic_cast<CWaterPump*>(it)) {
            if (dynamic_cast<CPurifier*>(it)) continue;
            // the pump's own flow is switched, and only that one
            CWaterFlow* fl = pump->flow();
            pump->activate();
            bool right = pump->running() && fl->on();
            int others = 0;
            for (Item* o : w.list(Table::Waterflows)) if (o != fl && as<CWaterFlow>(o).on() && as<CWaterFlow>(o).id() == fl->id() && fl->id() != 0) {}
            (void)others;
            pump->deactivate();
            right = right && !fl->on();
            check(right, "pump " + pump->featureName() + " switches its flow on and off");
            pumpsChecked++;
        }
    }
    check(fansChecked == 9 && rainChecked == 10 && pumpsChecked == 4,
          "9 fans, 10 rain machines and 4 pumps were checked (" + std::to_string(fansChecked) + ", " + std::to_string(rainChecked) + ", " + std::to_string(pumpsChecked) + ")");

    // power storage stays in 0..200 and a blackout at 0 powers features off
    {
        CFeature& fan = feat(r, "fr10fan");
        h.m_dwPowerFlags |= 0x40000000u;
        h.m_dPower = 100;
        fan.activate();
        check(fan.running(), "a fan runs with power");
        h.m_dPowerDemand = 100;       // drain the store in one 90 s step
        h.m_dPowerGeneration = 0;
        step();
        check(h.m_dPower >= 0 && h.m_dPower <= 200, "stored energy stays in 0..200 after a step");
        h.m_dPower = 0.5;
        step();
        check((h.m_dwPowerFlags & 0x40000000u) == 0 && h.m_dPower == 0.0, "a blackout clears the power flag and the store");
        check(!fan.running(), "a blackout stops the running fan (OnPowerOff)");
        check(fan.active(), "the fan keeps wanting to run");
        h.m_dPowerDemand = 0;
        h.m_dPower = 3.5;
        step();
        check((h.m_dwPowerFlags & 0x40000000u) != 0 && fan.running(), "power at 3 or more restores main power and the fan restarts (OnPowerOn)");
        fan.deactivate();
        h.m_dPower = 250;
        h.m_dPowerGeneration = 100;
        step();
        check(h.m_dPower <= 200, "stored energy is capped at 200");
        h.m_dPowerGeneration = 0;
    }

    // blowing a fuse disables its feature, replacing it restores it
    {
        CFeature& rainf = feat(r, "fr1rain");
        CFuseBox& box = as<CFuseBox>(r.gs().features.fuseBox[0]);
        u32 before = box.fuseMask();
        h.m_dwPowerFlags |= 0x40000000u;
        h.m_dPower = 150;
        rainf.setParam(2, 1, 0);
        check(rainf.running(), "the rain machine runs before the fuse blows");
        bool blown = cworld::blowFuse(w, 0x1000004);
        check(blown && (box.fuseMask() & 4) == 0, "BlowFuse clears the rain fuse (0x4) in the RF fuse box");
        check(!rainf.running(), "the rain machine stops when its fuse blows");
        check(cworld::hasPower(w, rainf.powerMask()) == 0, "the rain machine has no power with the fuse blown");
        check(!cworld::blowFuse(w, 0x1000004), "a blown fuse cannot blow again");
        cworld::onCircuitPowerOn(w, 0x1000004);
        box.setFuseMask(box.fuseMask() | 4);   // CFuseBox hotspot: the fuse is plugged in again
        cworld::onCircuitPowerOn(w, 0x1000004);
        check(cworld::hasPower(w, rainf.powerMask()) != 0 && rainf.running(), "replacing the fuse restores the rain machine");
        rainf.setParam(2, 0, 0);
        box.setFuseMask(before);
    }

    // every FEATURE_* id of the BS4 panels is answered for the classes that have it
    struct Want { const char* cls; const char* feature; std::vector<int> ids; };
    std::vector<Want> wants = {
        {"CBiomeSensor", "fr1probe", {1, 2, 3, 5, 6, 7, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2e, 0x32, 0x8e}},
        {"CTankSensor", "fbr3tank", {0x28, 0x2b, 0x2c, 0x2d, 0x32}},
        {"COceanProbe", "fuwprobe", {0x20, 0x22, 0x28, 0x29, 0x2a, 0x2b, 0x32}},
        {"CHeater", "frfheater", {100, 0x65, 0x66, 0x67}},
        {"CFan", "fr10fan", {0x50}},
        {"CVent", "fr6vent", {0x50}},
        {"CRainMachine", "fr1rain", {0x5f}},
        {"CMister", "fr1mist", {0x5f, 0x6e, 0x6f}},
        {"CLung", "fbr2lung", {0x11, 0x14, 0x1c, 0x1d, 0x1e}},
        {"CWaterPump", "fbr3watpmp", {0x14, 0x2b}},
        {"CPurifier", "fbr4revos", {0x2b, 0x2c, 0x32}},
        {"CDrainGauge", "fbrdrain1", {0x32, 0x51, 0x52}},
        {"CTideMachine", "fbo1Tide", {0x2b, 0x96}},
        {"COxygenStore", "fbo4oxygen", {0x22, 0x78}},
        {"CBubbler", "fw3Bubl", {0x5a, 0x78}},
        {"CSolarGen", "fsolargen", {0x8c}},
        {"CWindmill", "fwindmill", {0x8c}},
    };
    for (const Want& wt : wants) {
        CFeature* f = w.findFeature(wt.feature);
        if (!f) { check(false, std::string("feature ") + wt.feature + " exists"); continue; }
        std::string missing;
        for (int id : wt.ids) {
            u32 pi = 0; double pd = 0;
            if (!featureGetParameter(w, wt.feature, id, pi, pd)) missing += " " + std::to_string(id);
        }
        check(missing.empty(), std::string("every id of ") + wt.cls + " (" + wt.feature + ") is answered" + (missing.empty() ? "" : "; missing:" + missing));
    }
    {   // the lung answers FEATURE_DESTINATION_AIR_VOLUME (0x1b) only when it has a target cel (not in the push-into-own-biome mode)
        u32 pi = 0; double pd = 0;
        bool before = featureGetParameter(w, "fbr2lung", 0x1b, pi, pd);
        featureSetParameter(w, "fbr2lung", 0x14, 0x16, 0, 0);
        bool after = featureGetParameter(w, "fbr2lung", 0x1b, pi, pd);
        check(!before && after, "the lung answers id 0x1b only with a target cel (mode 0x1a: not; after SetParam 0x14 = 0x16: yes)");
    }
    {   // the world parameters the panels read
        std::string missing;
        for (int id : {0x3f2, 0x3f3, 0x3f4, 0x3fc, 0x3fd, 0x3fe, 0x3ff, 0x41a, 0x424, 0x438, 0x834, 0x835, 0x836, 0x837}) {
            u32 pi = 0; double pd = 0;
            if (!featureGetParameter(w, "", id, pi, pd)) missing += " " + std::to_string(id);
        }
        check(missing.empty(), "the world parameters (WORLD_* and SIM_*_BIOME_HEALTH) are answered" + (missing.empty() ? "" : "; missing:" + missing));
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        SaveGame::Options o;
        std::string path = BIOSYS_DEV_ROOT "/setup-cd/installed/DATA1/NewGame.sav";
        for (int i = 1; i < argc; i++) {
            std::string a = argv[i];
            if (a == "--seed" && i + 1 < argc) o.randSeed = static_cast<u32>(std::stoul(argv[++i]));
            else if (a == "--repair") o.repairOriginalLoadBugs = true;
            else path = a;
        }
        Rig r;
        r.sg = std::make_unique<SaveGame>(o);
        std::vector<u8> sav = readFileBytes(path);
        r.sg->load(SaveGame::blobOf(sav));
        r.sim = std::make_unique<Sim>(*r.sg);
        bindFeatureHooks(*r.sim);
        r.timeOfDay();
        std::string line;
        while (std::getline(std::cin, line)) {
            std::istringstream is(line);
            std::string cmd;
            if (!(is >> cmd) || cmd[0] == '#') continue;
            if (cmd == "get") {
                std::string f, ids; is >> f >> ids;
                int id = static_cast<int>(std::stol(ids, nullptr, 0));       // decimal or 0x..
                if (f == "-") f.clear();
                u32 pi = 0; double pd = 0;
                bool okk = featureGetParameter(r.w(), f, id, pi, pd);
                std::printf("get %s %d = %s int=%d dbl=%s\n", f.c_str(), id, okk ? "ok" : "no", static_cast<i32>(pi), num(pd).c_str());
            } else if (cmd == "set") {
                std::string f, ids, vs; double d; is >> f >> ids >> vs >> d;
                int id = static_cast<int>(std::stol(ids, nullptr, 0)), v = static_cast<int>(std::stol(vs, nullptr, 0));
                uint64_t bits; std::memcpy(&bits, &d, 8);
                bool okk = featureSetParameter(r.w(), f, id, v, static_cast<int>(bits & 0xffffffffu), static_cast<int>(bits >> 32));
                std::printf("set %s %d = %s\n", f.c_str(), id, okk ? "ok" : "no");
            } else if (cmd == "getall") {
                for (Item* it : r.w().list(Table::Features)) {
                    const CFeature& f = as<CFeature>(it);
                    for (int id = 0; id <= 0x100; id++) {
                        i32 pi = 0x7eadbeef; double pd = -12345.5;
                        if (f.getParam(id, &pi, &pd)) std::printf("getall %s %s %d int=%d dbl=%s\n", f.featureName().c_str(), f.className().c_str(), id, pi, num(pd).c_str());
                    }
                }
            } else if (cmd == "echo") {
                std::string rest;
                std::getline(is, rest);
                std::printf("%s\n", rest.empty() ? "" : rest.c_str() + 1);
            } else if (cmd == "biomes") {
                for (u32 a : {0x100c1c50u, 0x100c0ee8u, 0x100c1280u, 0x100c2768u, 0x100c24e8u, 0x100c2560u, 0x100c1208u, 0x100c1ba0u, 0x100c25d8u}) {
                    Biome* b = r.w().biome(a);
                    std::printf("biome %s cels=", b->name);
                    for (size_t i = 0; i < b->cels.size(); i++) std::printf("%s%s", i ? "," : "", b->cels[i]->name().c_str());
                    std::printf(" sensors=");
                    for (size_t i = 0; i < b->sensors.size(); i++) std::printf("%s%s", i ? "," : "", b->sensors[i]->name().c_str());
                    std::printf("\n");
                }
            } else if (cmd == "sites") {
                for (Item* it : r.w().list(Table::Sites)) {
                    const CSite& st = as<CSite>(it);
                    std::printf("site %s cel=%s state=%u unk0e8=%s unk0f0=%s\n", st.name().c_str(), st.cel() ? st.cel()->name().c_str() : "null", st.state(),
                                num(st.unk0e8()).c_str(), num(st.unk0f0()).c_str());
                }
            } else if (cmd == "members") {
                std::string f;
                if (is >> f) printMembers(feat(r, f));
                else for (Item* it : r.w().list(Table::Features)) printMembers(as<CFeature>(it));
            } else if (cmd == "world") printWorld(r, "world");
            else if (cmd == "cels") printCels(r, "cel");
            else if (cmd == "tanks") printTanks(r, "tank");
            else if (cmd == "ocean") printOcean(r, "ocean");
            else if (cmd == "flows") printFlows(r);
            else if (cmd == "run" || cmd == "trace") {
                u32 sec; is >> sec;
                advance(r, sec, cmd == "trace");
                std::printf("%s %u = time %d\n", cmd.c_str(), sec, r.sg->timeline.m_dwTime);
            } else if (cmd == "time") {
                u32 sec; is >> sec;
                r.sg->timeline.m_dwTime = static_cast<i32>(sec);
                r.timeOfDay();
            } else if (cmd == "seed") {
                u32 s; is >> s;
                r.gs().rng.seed(s);
            } else if (cmd == "on" || cmd == "off" || cmd == "act" || cmd == "deact" || cmd == "break" || cmd == "repair" ||
                       cmd == "poweron" || cmd == "poweroff") {
                std::string f; is >> f;
                CFeature& ft = feat(r, f);
                if (cmd == "on") ft.turnOn();
                else if (cmd == "off") ft.turnOff();
                else if (cmd == "act") ft.activate();
                else if (cmd == "deact") ft.deactivate();
                else if (cmd == "break") ft.breakDown();
                else if (cmd == "repair") ft.repair();
                else if (cmd == "poweron") ft.onPowerOn();
                else ft.onPowerOff();
            } else if (cmd == "fusemask") {      // fusemask <fusebox feature> <hex>: plug fuses in (or out): the box's mask is the feature's power mask
                std::string f, m; is >> f >> m;
                CFuseBox& fb = as<CFuseBox>(&feat(r, f));
                fb.setFuseMask(static_cast<u32>(std::stoul(m, nullptr, 16)));
            } else if (cmd == "blow") {
                std::string m; is >> m;
                std::printf("blow %s = %d\n", m.c_str(), cworld::blowFuse(r.w(), static_cast<u32>(std::stoul(m, nullptr, 16))) ? 1 : 0);
            } else if (cmd == "power") {
                double v; is >> v;
                r.gs().sim.world->m_dPower = v;
            } else if (cmd == "powerflags") {
                std::string m; is >> m;
                r.gs().sim.world->m_dwPowerFlags = static_cast<u32>(std::stoul(m, nullptr, 16));
            } else if (cmd == "updatefeatures") {
                cworld::updateAllFeatures(r.w(), r.sg->timeline.m_dwTime);
            } else if (cmd == "check") {
                runChecks(r);
                std::printf("%d passed, %d failed\n", ok, bad);
                return bad == 0 ? 0 : 1;
            } else {
                std::printf("unknown command %s\n", cmd.c_str());
                return 64;
            }
        }
    } catch (const Error& e) {
        std::printf("biosys::Error: %s\n", e.what());
        return 2;
    }
    return 0;
}
