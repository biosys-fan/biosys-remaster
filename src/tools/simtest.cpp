// biosys-simtest: runs the simulation core (src/game/sim.*) headlessly on a saved game and dumps its state.
//
//   run <file.sav> [options]    load the game, run, dump at the chosen times
//       --seed N        srand value of the game (SaveGame::Options::randSeed; default 1)
//       --mode M        timer (default): the Event_OnTimer round robin dll:1001d970 on a simulated clock: one call per
//                       second of real time, five calls per cycle, the timeline advances 125 game seconds per cycle
//                       (dll:10015470, 40 ms per game second); advance: CTimeline::Advance dll:100154b0 (90 s steps)
//       --at S,S,...    dump after S game seconds from the start (timer: at the end of the first cycle that reaches
//                       S; advance: exactly S, run in one Advance call per interval)
//       --flows off|on  switch every air and water flow off (conservation tests) or every one on (a stress test of every flow
//                       class) before the run; the lung flows (they need their machine) and the water flows from a tank to
//                       itself (a pump whose destination is not set yet: it would create water) stay as they are
//       --pollute P     set the pollution of every cel to P percent before the run (plant damage, light)
//       --drain F       multiply the water of every soil tank by F before the run (plant water stress)
//       --cel NAME:airTemp=V[,soilTemp=V,...]   set members of one cel before the run (current and new value)
//       --roundtrip     after the run: save the game, load the save into a fresh game, save again and compare the bytes
//       --state-at-start  also dump the initial state
//   static <file.sav>   the static data of the world the dump does not carry (topology, constants), for the oracle
//
// The dump is `section/item/member = value` lines in the format of biosys_savblob.py dump (every typed member of the
// sections world, cels, tanks, airflows, waterflows, plants, timeline), preceded by `== t=<game second>` and followed by
// `sim/...` lines for the state the blob does not carry. The machines, objects and the player are not updated here:
// those are other workers' (TODO(behaviour) in BiosGame).
#include "game/airflow.h"
#include "game/biomecel.h"
#include "game/feat_sensors.h"
#include "game/plant.h"
#include "game/savegame.h"
#include "game/sim.h"
#include "game/waterflow.h"
#include "game/watertank.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <sstream>

using namespace biosys;
using namespace biosys::game;

namespace {

// The part of CTimeline the simulation reads, on a simulated millisecond clock (dll:100153b0 AdvanceRealTime, 10015470
// RealTimeTick, 100155e0 UpdateTimeOfDay, 10015710 UpdateDiurnal without its site calls).
struct HeadlessClock {
    SaveGame& sg;
    Sim& sim;
    uint32_t nowMs = 0;            // GetTickCount()
    uint32_t lastTick = 0;         // CTimeline+0x28
    uint32_t lastRealTick = 0;     // DAT_100c426c
    int ticksPerGameSec = 40;      // CTimeline+0x0c (NewGame.sav stores -1: the clock waits for the game to start)

    void updateTimeOfDay() { sg.timeline.m_dwTimeOfDay = static_cast<i32>(static_cast<uint32_t>(sg.timeline.m_dwTime) % 0x15180); }

    void updateDiurnal() {
        const uint32_t tod = static_cast<uint32_t>(sg.timeline.m_dwTimeOfDay);
        SimRefs& r = sg.world.state().sim;
        if (tod < 0x2a30 || tod > 0x1274f) {
            r.daylight = 0.0;
            r.diurnal = 0x28;
        } else if (tod < 0x5460) {
            r.diurnal = 5;
            r.daylight = static_cast<double>(tod - 0x2a30) * 9.259259259259259e-05 * 0.5;
        } else if (tod < 0x7080) {
            r.diurnal = 0xf;
            r.daylight = static_cast<double>(tod - 0x5460) * 0.0001388888888888889 * 0.5 + 0.5;
        } else if (tod < 0xe100) {
            r.daylight = 1.0;
            r.diurnal = 0x14;
        } else if (tod < 0xfd20) {
            r.diurnal = 0x1e;
            r.daylight = static_cast<double>(tod - 0xe100) * 0.0001388888888888889 * -0.5 + 1.0;
        } else {
            r.diurnal = 0x23;
            r.daylight = static_cast<double>(tod - 0xfd20) * 9.259259259259259e-05 * -0.5 + 0.5;
        }
    }

    // dll:10014ee0 CWorld::OnNewDay
    void onNewDay() {
        for (Item* it : sg.world.related(Rel::WorldSensor, nullptr)) as<CBiomeSensor>(it).onNewDay();
        for (Item* it : sg.world.list(Table::Cels)) as<CBiomeCel>(it).onNewDay();
    }

    // CTimeline::Advance dll:100154b0 moves the time and the time of day only: the day number (and OnNewDay) wait for the
    // next AdvanceRealTime
    void setTimeNoDay(i32 t) {
        sg.timeline.m_dwTime = t;
        updateTimeOfDay();
        updateDiurnal();
    }

    void setTime(i32 t) {
        sg.timeline.m_dwTime = t;
        const int day = static_cast<int>(static_cast<uint32_t>(t) / 0x15180 + 1);
        if (sg.timeline.m_dwDay != day) {
            sg.timeline.m_dwDay = day;
            onNewDay();
        }
        updateTimeOfDay();
        updateDiurnal();
    }

    // dll:100153b0 + 10015470
    void realTimeTick() {
        if (nowMs - lastRealTick > 4999) {
            lastRealTick = nowMs;
            const uint32_t elapsed = nowMs - lastTick;
            i32 t = sg.timeline.m_dwTime;
            if (elapsed < 10000) t = static_cast<i32>(static_cast<uint32_t>(t) + elapsed / static_cast<uint32_t>(ticksPerGameSec));
            lastTick = nowMs;
            setTime(t);
        }
    }
};

struct Run {
    SaveGame sg;
    std::unique_ptr<Sim> sim;
    std::unique_ptr<HeadlessClock> clock;
    int phase = 0;                 // DAT_100c42a0
    uint32_t lastHeavyUpdate = 0;  // DAT_100a17b8 (only the idle handler moves it)
    size_t logStart = 0;           // GameState::log entries before the run (the world builders write some)

    explicit Run(const SaveGame::Options& o) : sg(o) {}
    World& world() { return sg.world; }

    // dll:1001d970 Event_OnTimer, the part that updates the simulation (objects, features and the player are other
    // workers'; the display at the end of phase 3 is the shell's)
    void timerCall() {
        clock->nowMs += 1000;
        const int p = phase;
        phase++;
        switch (p) {
        case 0:
            sim->updateWeather();
            sim->updateOcean();
            clock->realTimeTick();
            return;
        case 1:
            sim->updateAirFlows();
            return;
        case 2:
            sim->updateWaterFlows();
            return;
        case 3:
            if (static_cast<uint32_t>(sg.timeline.m_dwTime) - lastHeavyUpdate > 0x5a) {
                // TODO(behaviour): CWorld::UpdateAllObjects, CWorld::UpdateAllFeatures
                sim->updateAllPlants();
            }
            return;
        default:
            phase = 0;      // CSelf::Update: the player
            return;
        }
    }

    // dll:100154b0 CTimeline::Advance without the sounds and the other workers' updates
    void advance(uint32_t seconds) {
        uint32_t steps = (seconds + 0x5a) / 0x5a;
        uint32_t n = steps;
        do {
            sim->updateWeather();
            sim->updateOcean();
            sim->updateAirFlows();
            sim->updateWaterFlows();
            sim->updateAllPlants();
            clock->setTimeNoDay(static_cast<i32>(static_cast<uint32_t>(sg.timeline.m_dwTime) + 0x5a));
            n--;
        } while (n != 0);
        if (seconds < steps * 0x5a)
            clock->setTimeNoDay(static_cast<i32>(static_cast<uint32_t>(sg.timeline.m_dwTime) + seconds - steps * 0x5a));
    }
};

std::unique_ptr<Run> openRun(const std::string& path, uint32_t seed) {
    SaveGame::Options o;
    o.randSeed = seed;
    o.repairOriginalLoadBugs = true;     // the dump compares members, not the bug
    auto r = std::make_unique<Run>(o);
    std::vector<u8> sav = readFile(path);
    r->sg.load(SaveGame::blobOf(sav));
    r->sim = std::make_unique<Sim>(r->sg);
    r->clock = std::make_unique<HeadlessClock>(HeadlessClock{r->sg, *r->sim});
    // the clock as a started game has it: 40 ms per game second, daylight of the saved time of day
    r->clock->updateTimeOfDay();
    r->clock->updateDiurnal();
    r->clock->nowMs = 4000;      // the next timer call (5000 ms) is a whole cycle of 125 game seconds
    r->clock->lastTick = 0;
    r->clock->lastRealTick = 0;
    r->logStart = r->sg.world.state().log.size();
    return r;
}

// every typed member of the simulation sections, saved and read back through logging readers (the format of
// biosys_savblob.py dump), taken from a copy so that the running game is not touched
void dumpState(Run& run, FILE* out) {
    SaveGame::Options o;
    o.repairOriginalLoadBugs = true;
    SaveGame copy(o);
    copy.load(run.sg.save());
    static const std::set<std::string> kSections = {"world", "cels", "tanks", "airflows", "waterflows", "plants", "timeline"};
    for (const Segment& s : copy.segments()) {
        if (!kSections.count(s.section)) continue;
        std::vector<u8> buf;
        SlotWriter w(buf, s.size, s.section);
        s.record->saveSlot(w);
        std::vector<FieldLog> log;
        SlotReader r(buf.data(), buf.size(), s.section, &log);
        s.record->loadSlot(r);
        r.finishPad();
        for (const FieldLog& f : log) {
            if (f.name.compare(0, 4, "_pad") == 0 || f.name == "uninit_tail") continue;
            std::fprintf(out, "%s/%s/%s = %s\n", s.section.c_str(), s.key.c_str(), f.name.c_str(), f.text.c_str());
        }
    }
    // state the blob does not carry
    Sim& sim = *run.sim;
    std::fprintf(out, "sim/world/blueSkyTarget = %s\n", pyRepr(sim.blueSkyTarget).c_str());
    std::fprintf(out, "sim/world/blueSkyLo = %s\n", pyRepr(sim.blueSkyLo).c_str());
    std::fprintf(out, "sim/world/blueSkyHi = %s\n", pyRepr(sim.blueSkyHi).c_str());
    std::fprintf(out, "sim/world/rngState = %u\n", run.sg.world.state().rng.state());
    std::fprintf(out, "sim/world/daylight = %s\n", pyRepr(run.sg.world.state().sim.daylight).c_str());
    std::fprintf(out, "sim/world/diurnal = %d\n", run.sg.world.state().sim.diurnal);
    for (uint32_t a : Sim::kBiomeAddrs)
        std::fprintf(out, "sim/biome/%08x/target = %s\n", a, pyRepr(sim.biomeParams(a).target).c_str());
    std::fprintf(out, "sim/world/condensationFlowsOn = %d\n", sim.condensationFlowsOn ? 1 : 0);
    std::fprintf(out, "sim/world/lookUpReportEnabled = %d\n", sim.lookUpReportEnabled ? 1 : 0);
    for (Item* it : run.sg.world.list(Table::Airflows)) {
        const CAirFlow& f = as<CAirFlow>(it);
        if (f.amount() != 0.0) std::fprintf(out, "sim/airflow/%s/amount = %s\n", it->name().c_str(), pyRepr(f.amount()).c_str());
    }
    for (Item* it : run.sg.world.list(Table::Cels)) {
        const CBiomeCel& c = as<CBiomeCel>(it);
        std::fprintf(out, "sim/cel/%s/windSpeed = %s\n", it->name().c_str(), pyRepr(c.windSpeed()).c_str());
    }
    // what the simulation wrote to the log since the start (out-of-range reports, LookUpVal overflows)
    const std::vector<std::string>& lg = run.sg.world.state().log;
    for (size_t i = run.logStart; i < lg.size(); i++) std::fprintf(out, "sim/log/%zu = %s\n", i - run.logStart, lg[i].c_str());
    std::fflush(out);
}

int cmdStatic(const std::string& path) {
    auto run = openRun(path, 1);
    std::set<const Item*> seen;
    for (const Segment& s : run->sg.segments()) {
        if (!s.item) continue;
        const Item* it = s.item;
        if (s.section == "cels") {
            const CBiomeCel& c = as<CBiomeCel>(it);
            std::printf("cel\t%s\t%s\tbiome=%08x\tsoil=%s\tcond=%s\tid=%u\n", s.key.c_str(), it->name().c_str(), c.biome()->addr,
                        c.soilTank() ? c.soilTank()->name().c_str() : "-", c.condensateTank() ? c.condensateTank()->name().c_str() : "-",
                        c.id());
        } else if (s.section == "tanks") {
            const CWaterTank& t = as<CWaterTank>(it);
            const COcean* oc = dynamic_cast<const COcean*>(it);
            std::printf("tank\t%s\t%s\tvolume=%s\tbounded=%d\tocean=%s\n", s.key.c_str(), it->name().c_str(),
                        pyRepr(t.volume()).c_str(), t.bounded() ? 1 : 0, oc ? oc->cel()->name().c_str() : "-");
        } else if (s.section == "airflows") {
            const CAirFlow& f = as<CAirFlow>(it);
            const char* kind = dynamic_cast<const CSoilBedFlow*>(it) ? "soilbed" : dynamic_cast<const CLungAirFlow*>(it) ? "lung"
                               : dynamic_cast<const CBubbleFlow*>(it) ? "bubble" : "flow";
            std::string master = "-";
            if (f.masterFlow()) {
                const auto& fl = run->sg.world.list(Table::Airflows);
                for (size_t i = 0; i < fl.size(); i++)
                    if (fl[i] == f.masterFlow()) master = std::to_string(i);
            }
            std::printf("airflow\t%s\t%s\tkind=%s\tfrom=%s\tto=%s\tid=%u\tmag=%s\ttwoway=%d\tmaster=%s\tvent=%s\n", s.key.c_str(),
                        it->name().c_str(), kind, f.from() ? f.from()->name().c_str() : "-", f.to() ? f.to()->name().c_str() : "-",
                        f.id(), pyRepr(f.mag()).c_str(), f.twoWay() ? 1 : 0, master.c_str(), f.ventFeature() ? f.ventFeature()->name().c_str() : "-");
        } else if (s.section == "waterflows") {
            const CWaterFlow& f = as<CWaterFlow>(it);
            const char* kind = dynamic_cast<const CEvaporation*>(it) ? "evap" : dynamic_cast<const CRain*>(it) ? "rain" : "flow";
            const CBiomeCel* cel = dynamic_cast<const CEvaporation*>(it) ? dynamic_cast<const CEvaporation*>(it)->cel()
                                   : dynamic_cast<const CRain*>(it) ? dynamic_cast<const CRain*>(it)->cel() : nullptr;
            std::printf("waterflow\t%s\t%s\tkind=%s\tfrom=%s\tto=%s\tid=%u\tmag=%s\tclog=%s\tproportional=%d\tcel=%s\thumidityRate=%s\n",
                        s.key.c_str(), it->name().c_str(), kind, f.from() ? f.from()->name().c_str() : "-",
                        f.to() ? f.to()->name().c_str() : "-", f.id(), pyRepr(f.mag()).c_str(), pyRepr(f.clogRate()).c_str(),
                        f.proportional() ? 1 : 0, cel ? cel->name().c_str() : "-",
                        pyRepr(dynamic_cast<const CRain*>(it) ? dynamic_cast<const CRain*>(it)->humidityRate() : 0.0).c_str());
        } else if (s.section == "plants") {
            const CPlant& p = as<CPlant>(it);
            std::printf("plant\t%s\t%s\tcel=%s\tflags=%u\tmaxps=%s\tresp=%s\ttopt=%s\ttnight=%s\ttday=%s\ttmax=%s\thard=%s\tswmin=%s\tswmax=%s\t"
                        "light=%08x\tco2=%08x\ttemp=%08x\n",
                        s.key.c_str(), it->name().c_str(), p.cel() ? p.cel()->name().c_str() : "-", p.flags(),
                        pyRepr(p.maxPSRate()).c_str(), pyRepr(p.respirationRate()).c_str(), pyRepr(p.tempOptimum()).c_str(),
                        pyRepr(p.tempMinNight()).c_str(), pyRepr(p.tempMinDay()).c_str(), pyRepr(p.tempMax()).c_str(),
                        pyRepr(p.hardiness()).c_str(), pyRepr(p.soilWaterMin()).c_str(), pyRepr(p.soilWaterMax()).c_str(),
                        p.lightTable(), p.co2Table(), p.tempTable());
        }
    }
    return 0;
}

int cmdRun(int argc, char** argv) {
    std::string path = argv[2];
    uint32_t seed = 1;
    std::string mode = "timer";
    std::vector<uint32_t> at;
    std::string flows;
    double pollute = -1, drain = -1;
    bool atStart = false, roundtrip = false;
    std::vector<std::string> celSets;
    for (int i = 3; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw Error("missing value after " + a);
            return argv[++i];
        };
        if (a == "--seed") seed = static_cast<uint32_t>(std::stoul(next()));
        else if (a == "--mode") mode = next();
        else if (a == "--flows") flows = next();
        else if (a == "--pollute") pollute = std::stod(next());
        else if (a == "--drain") drain = std::stod(next());
        else if (a == "--cel") celSets.push_back(next());
        else if (a == "--roundtrip") roundtrip = true;
        else if (a == "--state-at-start") atStart = true;
        else if (a == "--at") {
            std::stringstream ss(next());
            std::string tok;
            while (std::getline(ss, tok, ',')) at.push_back(static_cast<uint32_t>(std::stoul(tok)));
        } else throw Error("unknown option " + a);
    }
    if (mode != "timer" && mode != "advance") throw Error("unknown mode " + mode);
    auto run = openRun(path, seed);
    if (flows == "off" || flows == "on") {
        const int on = flows == "on" ? 1 : 0;
        for (Item* it : run->world().list(Table::Airflows))
            if (!(on && dynamic_cast<CLungAirFlow*>(it))) as<CAirFlow>(it).turnOn(on);
        for (Item* it : run->world().list(Table::Waterflows)) {
            CWaterFlow& f = as<CWaterFlow>(it);
            if (!(on && f.from() == f.to())) f.turnOn(on);
        }
    }
    if (pollute >= 0) {
        for (Item* it : run->world().list(Table::Cels)) {
            as<CBiomeCel>(it).setPollution(pollute);
            as<CBiomeCel>(it).setNewPollution(pollute);
        }
    }
    if (drain >= 0) {
        for (Item* it : run->world().list(Table::Cels)) {
            if (Item* t = as<CBiomeCel>(it).soilTank()) as<CWaterTank>(t).setWaterVol(as<CWaterTank>(t).waterVol() * drain);
        }
    }
    for (const std::string& spec : celSets) {
        const size_t colon = spec.find(':');
        if (colon == std::string::npos) throw Error("--cel NAME:member=value,...");
        CBiomeCel& c = run->world().requireBiomeCel(spec.substr(0, colon).c_str());
        std::stringstream ss(spec.substr(colon + 1));
        std::string kv;
        while (std::getline(ss, kv, ',')) {
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) throw Error("--cel member=value expected: " + kv);
            const std::string m = kv.substr(0, eq);
            const double v = std::stod(kv.substr(eq + 1));
            if (m == "airTemp") { c.setAirTemp(v); c.setNewAirTemp(v); }
            else if (m == "soilTemp") { c.setSoilTemp(v); c.setNewSoilTemp(v); }
            else if (m == "humidity") { c.setHumidity(v); c.setNewHumidity(v); }
            else if (m == "oxygen") { c.setOxygen(v); c.setNewOxygen(v); }
            else if (m == "co2") { c.setCO2(v); c.setNewCO2(v); }
            else if (m == "pollution") { c.setPollution(v); c.setNewPollution(v); }
            else throw Error("unknown cel member " + m);
        }
    }
    const i32 start = run->sg.timeline.m_dwTime;
    auto emit = [&] {
        std::printf("== t=%d elapsed=%d\n", run->sg.timeline.m_dwTime, run->sg.timeline.m_dwTime - start);
        dumpState(*run, stdout);
    };
    if (atStart) emit();
    for (uint32_t target : at) {
        if (mode == "timer") {
            while (static_cast<uint32_t>(run->sg.timeline.m_dwTime - start) < target) {
                for (int k = 0; k < 5; k++) run->timerCall();
            }
        } else {
            const uint32_t done = static_cast<uint32_t>(run->sg.timeline.m_dwTime - start);
            if (target > done) run->advance(target - done);
        }
        emit();
    }
    if (roundtrip) {
        // a save made after simulating loads, and loads to the same game (the two ORIGINAL BUG loads repaired)
        std::vector<u8> first = run->sg.save();
        SaveGame::Options o;
        o.repairOriginalLoadBugs = true;
        SaveGame again(o);
        again.load(first);
        std::vector<u8> second = again.save();
        if (first != second) {
            size_t i = 0;
            while (i < first.size() && i < second.size() && first[i] == second[i]) i++;
            std::printf("ROUNDTRIP FAIL: the reloaded save differs at blob offset 0x%zx (%zu / %zu bytes)\n", i, first.size(), second.size());
            return 1;
        }
        std::printf("ROUNDTRIP OK: %zu bytes\n", first.size());
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc >= 3 && std::strcmp(argv[1], "run") == 0) return cmdRun(argc, argv);
        if (argc == 3 && std::strcmp(argv[1], "static") == 0) return cmdStatic(argv[2]);
    } catch (const Error& e) {
        std::printf("biosys::Error: %s\n", e.what());
        return 2;
    }
    std::fprintf(stderr, "usage: biosys-simtest run <file.sav> [--seed N] [--mode timer|advance] [--at S,S,...] [--flows off]\n"
                         "       biosys-simtest static <file.sav>\n");
    return 64;
}
