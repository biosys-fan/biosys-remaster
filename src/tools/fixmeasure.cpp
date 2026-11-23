// biosys-fixmeasure: how much does a set of Fixes (src/game/fixes.h) change the game? Runs the world of NewGame.sav headless
// with no player action and prints one TSV row per game day.
//
//   biosys-fixmeasure [--seed N] [--days N] [--fixes SPEC] [--sav FILE]
//       --seed N     srand value of the game (default 1)
//       --days N     game days to run (default 30; one day = 86400 game seconds)
//       --fixes S    Fixes::parse text: faithful (default) | safe | cosmetic | restored | flag,-flag,...
//       --sav FILE   saved game (default: NewGame.sav of the installed game)
//       --rain       extra scenario: every CRain machine (the rain and the misters) is switched on at the start, as a player
//                    who turned them all on would have it. Default (no option): nobody does anything.
//
// The run is the part of CTimeline::Advance dll:100154b0 that the machines need, in 90 game second steps: weather, ocean,
// air flows, water flows, objects, features, plants (src/game/sim.h, featenv.h, gameobj.h), then the player's CSelf::Update.
// The player is left idle (no input, no eating, drinking or sleeping). The first row (day 0) is the state at the start.
//
// Columns: seed, fixes, day, then per biome (name) airTemp, humidity, co2, o2, pollution, soilWater as the mean over the cels of
// the biome; ocean (temp, ph, salinity, algae, oxygen, level %, health); tanks (mean fill %, mean purity over the other
// tanks); plants (alive = non-inert plants with health > 0, mean health of the non-inert plants, fruit count);
// power (stored, generation, demand, balance = generation - demand, per game hour); player (alive, health, temperature,
// hydration, nutrition, protein, energy, death time in game seconds from the start, cause code of the death).
//
// When the idle player dies (CSelf::Die: the original then jumps to the death page and stops the game loop) the world keeps
// running for the measurement; only the player's updates stop. The rows keep the player's state at the time of death.
#include "engine/devroot.h"
#include "game/biomecel.h"
#include "game/blobrecords.h"
#include "game/featenv.h"
#include "game/feat_sensors.h"
#include "game/gameobj.h"
#include "game/plant.h"
#include "game/savegame.h"
#include "game/self.h"
#include "game/self_display.h"
#include "game/sim.h"
#include "game/waterflow.h"
#include "game/watertank.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

using namespace biosys;
using namespace biosys::game;

namespace {

struct Host : PlayerHost {
    Timeline* tl = nullptr;
    i32 deathTime = -1;
    u32 getTickCount() override { return static_cast<u32>(tl->m_dwTime) * 40u; }   // 40 ms per game second
    bool ChangeGameObjectVariation(const char*, const char*, int) override { return true; }
    bool playSoundHandle(const char*, int*) override { return true; }
    int jumpToPage(const char*, int, int, int, int) override { return 1; }
    void CTimeline_SetTicksPerGameSec(int t) override { tl->m_dwTicksPerGameSec = t; }
    void setDead(int v) override {
        if (v != 0 && dead() == 0) deathTime = tl->m_dwTime;
        PlayerHost::setDead(v);
    }
};

struct Rig {
    SaveGame sg;
    Host host;
    RecordingPlayerDisplay disp;
    std::unique_ptr<Sim> sim;
    i32 start = 0;
    explicit Rig(const SaveGame::Options& o) : sg(o) {}
    World& w() { return sg.world; }

    // CTimeline::UpdateTimeOfDay dll:100155e0 / UpdateDiurnal dll:10015710 without the site and sound work, and OnNewDay
    void timeOfDay() {
        Timeline& t = sg.timeline;
        t.m_dwTimeOfDay = static_cast<i32>(static_cast<u32>(t.m_dwTime) % 86400u);
        const int day = static_cast<int>(static_cast<u32>(t.m_dwTime) / 86400u + 1);
        if (t.m_dwDay != day) {
            t.m_dwDay = day;
            for (Item* it : w().related(Rel::WorldSensor, nullptr)) as<CBiomeSensor>(it).onNewDay();   // dll:10014ee0
            for (Item* it : w().list(Table::Cels)) as<CBiomeCel>(it).onNewDay();
        }
        const u32 tod = static_cast<u32>(t.m_dwTimeOfDay);
        SimRefs& s = sg.world.state().sim;
        if (tod < 0x2a30 || tod > 0x1274f) { s.daylight = 0.0; s.diurnal = 0x28; }
        else if (tod < 0x5460) { s.diurnal = 5; s.daylight = static_cast<double>(tod - 0x2a30) * 9.259259259259259e-05 * 0.5; }
        else if (tod < 0x7080) { s.diurnal = 0xf; s.daylight = static_cast<double>(tod - 0x5460) * 0.0001388888888888889 * 0.5 + 0.5; }
        else if (tod < 0xe100) { s.daylight = 1.0; s.diurnal = 0x14; }
        else if (tod < 0xfd20) { s.diurnal = 0x1e; s.daylight = static_cast<double>(tod - 0xe100) * 0.0001388888888888889 * -0.5 + 1.0; }
        else { s.diurnal = 0x23; s.daylight = static_cast<double>(tod - 0xfd20) * 9.259259259259259e-05 * -0.5 + 0.5; }
    }

    void step() {
        Timeline& t = sg.timeline;
        sim->updateWeather();
        sim->updateOcean();
        sim->updateAirFlows();
        sim->updateWaterFlows();
        cworld::updateAllObjects(sg.world, sg.self, t.m_dwTime);
        cworld::updateAllFeatures(sg.world, t.m_dwTime);
        sim->updateAllPlants();
        if (host.dead() == 0) {
            sg.self.Update(0);
            if (sg.self.m_bDieOnUpdate != 0) sg.self.Die(0);      // the next timer call does this (BiosGame::onTimer)
        }
        t.m_dwTime = static_cast<i32>(static_cast<u32>(t.m_dwTime) + 0x5a);
        timeOfDay();
    }
};

std::string f(double v) {
    char b[40];
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

std::vector<std::string> columns(Rig* r, bool header) {
    std::vector<std::string> out;
    auto put = [&](const std::string& name, const std::string& value) { out.push_back(header ? name : value); };
    // per biome means
    struct Acc { double t = 0, h = 0, c = 0, o = 0, p = 0, sw = 0; int n = 0; };
    std::map<std::string, Acc> bio;
    if (r) {
        for (Item* it : r->w().list(Table::Cels)) {
            const CBiomeCel& c = as<CBiomeCel>(it);
            Acc& a = bio[c.biome() ? c.biome()->name : "?"];
            a.t += c.airTemp(); a.h += c.humidity(); a.c += c.co2(); a.o += c.oxygen(); a.p += c.pollution(); a.sw += c.soilWater(); a.n++;
        }
    }
    static std::vector<std::string> names;
    if (r && names.empty())
        for (auto& kv : bio) names.push_back(kv.first);
    if (!r && names.empty()) return out;
    for (const std::string& n : names) {
        Acc a = r ? bio[n] : Acc();
        const double k = a.n ? 1.0 / a.n : 0.0;
        put(n + ".airTemp", f(a.t * k));
        put(n + ".humidity", f(a.h * k));
        put(n + ".co2", f(a.c * k));
        put(n + ".o2", f(a.o * k));
        put(n + ".pollution", f(a.p * k));
        put(n + ".soilWater", f(a.sw * k));
    }
    // ocean and tanks
    COcean* oc = nullptr;
    double fill = 0, purity = 0;
    int nt = 0;
    if (r) {
        oc = dynamic_cast<COcean*>(r->w().state().ocean);
        for (Item* it : r->w().list(Table::Tanks)) {
            const CWaterTank& t = as<CWaterTank>(it);
            if (dynamic_cast<const COcean*>(it) || t.volume() <= 0) continue;
            fill += t.waterVol() / t.volume() * 100.0;
            purity += t.purity();
            nt++;
        }
    }
    put("ocean.temp", oc ? f(oc->temp()) : "");
    put("ocean.ph", oc ? f(oc->ph()) : "");
    put("ocean.salinity", oc ? f(oc->salinity()) : "");
    put("ocean.algae", oc ? f(oc->algae()) : "");
    put("ocean.oxygen", oc ? f(oc->oxygen()) : "");
    put("ocean.level", oc ? f(oc->waterVol() / oc->volume() * 100.0) : "");
    put("ocean.health", oc ? f(oc->health()) : "");
    put("tanks.fill", nt ? f(fill / nt) : "");
    put("tanks.purity", nt ? f(purity / nt) : "");
    // plants
    int alive = 0, n = 0;
    double health = 0;
    long fruit = 0;
    if (r) {
        for (Item* it : r->w().list(Table::Plants)) {
            const CPlant& p = as<CPlant>(it);
            fruit += p.fruitCount();
            if ((p.flags() & 8) != 0) continue;     // inert
            n++;
            health += p.health();
            if (p.health() > 0.0) alive++;
        }
    }
    put("plants.alive", f(alive));
    put("plants.health", n ? f(health / n) : "");
    put("plants.fruit", f(static_cast<double>(fruit)));
    // power
    const WorldHeader* h = r ? r->sg.world.state().sim.world : nullptr;
    put("power.stored", h ? f(h->m_dPower) : "");
    put("power.generation", h ? f(h->m_dPowerGeneration * 3600.0) : "");
    put("power.demand", h ? f(h->m_dPowerDemand * 3600.0) : "");
    put("power.balance", h ? f((h->m_dPowerGeneration - h->m_dPowerDemand) * 3600.0) : "");
    // player
    const Self* s = r ? &r->sg.self : nullptr;
    put("player.alive", r ? (r->host.dead() ? "0" : "1") : "");
    put("player.health", s ? f(s->m_dHealth) : "");
    put("player.temperature", s ? f(s->m_dTemperature) : "");
    put("player.hydration", s ? f(s->m_dHydration) : "");
    put("player.nutrition", s ? f(s->m_dNutrition) : "");
    put("player.protein", s ? f(s->m_dProtein) : "");
    put("player.energy", s ? f(s->m_dEnergy) : "");
    put("player.deathTime", r && r->host.dead() ? f(r->host.deathTime - r->start) : "");
    put("player.deathCause", r && r->host.dead() ? f(s->m_wCauseOfDeath) : "");
    return out;
}

void row(Rig& r, unsigned seed, const std::string& fixes, int day, bool header) {
    std::string line;
    auto add = [&](const std::string& s) { line += (line.empty() ? "" : "\t") + s; };
    add(header ? "seed" : std::to_string(seed));
    add(header ? "fixes" : fixes);
    add(header ? "day" : std::to_string(day));
    for (const std::string& c : columns(&r, header)) add(c);
    std::puts(line.c_str());
}

} // namespace

int main(int argc, char** argv) {
    try {
        unsigned seed = 1;
        int days = 30;
        bool header = true, rain = false;
        std::string spec = "faithful";
        std::string sav = BIOSYS_DEV_ROOT "/setup-cd/installed/DATA1/NewGame.sav";
        for (int i = 1; i < argc; i++) {
            const std::string a = argv[i];
            auto next = [&]() -> std::string {
                if (i + 1 >= argc) throw Error("missing value after " + a);
                return argv[++i];
            };
            if (a == "--seed") seed = static_cast<unsigned>(std::strtoul(next().c_str(), nullptr, 10));
            else if (a == "--days") days = std::atoi(next().c_str());
            else if (a == "--fixes") spec = next();
            else if (a == "--sav") sav = next();
            else if (a == "--no-header") header = false;
            else if (a == "--rain") rain = true;
            else throw Error("usage: biosys-fixmeasure [--seed N] [--days N] [--fixes SPEC] [--sav FILE] [--rain] [--no-header]");
        }
        SaveGame::Options o;
        o.randSeed = seed;
        o.fixes = Fixes::parse(spec);
        Rig r(o);
        r.sg.load(SaveGame::blobOf(readFile(sav)));
        r.sim = std::make_unique<Sim>(r.sg);
        bindFeatureHooks(*r.sim);
        r.host.tl = &r.sg.timeline;
        r.sg.self.bind(r.sg.world, &r.host, &r.disp);
        r.sg.world.state().rng.seed(seed);
        r.timeOfDay();
        if (rain)
            for (Item* it : r.w().list(Table::Waterflows))
                if (CRain* m = dynamic_cast<CRain*>(it)) m->turnOn(1);
        r.start = r.sg.timeline.m_dwTime;
        if (header) row(r, seed, spec, 0, true);
        row(r, seed, spec, 0, false);
        for (int d = 1; d <= days; d++) {
            const i32 until = r.start + d * 86400;
            while (r.sg.timeline.m_dwTime < until) r.step();
            row(r, seed, spec, d, false);
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
