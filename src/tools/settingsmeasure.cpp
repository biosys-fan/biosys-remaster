// biosys-settingsmeasure: what do the realism levels and difficulty levels (src/game/settings.h, SETTINGS.md) do to the game?
// Runs the world of NewGame.sav headless in 90 game-second steps (the part of CTimeline::Advance dll:100154b0 the machines need:
// weather, ocean, air flows, water flows, objects, features, plants, then the player's CSelf::Update) and prints one TSV row per
// game day. The columns are those of biosys-fixmeasure plus, per biome, the plants alive / mean plant health and the share of the
// biome's sensors that raise a temperature or a humidity alarm, and a few player counters.
//
//   biosys-settingsmeasure [--seed N] [--days N] [--realism R] [--difficulty D] [--fixes LIST] [--model LIST] [--tune k=v]
//                          [--scenario idle|programme] [--player idle|sensible] [--sav FILE] [--no-header] [--list]
//       --realism R / --difficulty D / --fixes LIST   the options of the game window (Settings::parse)
//       --model LIST    replaces the model part of the realism level (Model::parse text, e.g. "closedloop,-vapourFromSoil")
//       --calib k=v     overrides a vapour-model constant of the model: kE, n, k7 (Model::evapRate, evapExponent, transpRate)
//       --tune k=v      sets one tuning knob after the difficulty preset (repeatable)
//       --scenario      idle (default): the machines are as the saved game leaves them (the new game's own programme: heaters, the
//                       rain machine of R1 always, two other rain machines 10-14, RO and UV 13-14), nobody touches anything
//                       frozen: every machine off (as the study's simtest runs)
//                       programme: the default machine programme (see applyProgramme): every machine with a daily timer is
//                       switched on at its default window, the new-game start-up of BiosGame::onStartup (wave machine) is done
//       --player        idle (default): no action; sensible: drinks when thirsty, eats when hungry, sleeps at night in the house
//       --kill-plants   every plant is held at health 0 for the whole run
//       --trace BIOME   print "trace day hour humidity" lines (mean humidity of the biome) every game hour
//       --watch CEL     extra columns for one biome cel (soil water, O2, CO2, humidity, plants, alive, health, fruit)
//       --start         apply the tuning's start values to the player first (a new game); without it the saved values stay
//       --no-collapse   the idle player never passes out (the behaviour before the collapse was added; biosys-fixmeasure has no collapse
//                       either, so with it the idle world equals that tool's)
//       --list          print the features, foods and house sites of the saved game and exit
// An exhausted player (energy 0) passes out like in the game: the full CSelf::Update makes it fall asleep (Sleep(1,1,1)) and the night runs in
// CSelf::OnIdle; the rain machines raise and clear the cel's rain flag (RainFxHost), which wakes a sleeper in the open.
// The idle player of fixmeasure is --player idle (no start values). The sensible player calls the game's own functions:
// Self::Drink(0xbe) (the water hotspots), ObjFood::pickUp / use (CFood::Eat), Self::Sleep(0, 0, 1) and Self::OnIdle (the night).
#include "engine/devroot.h"
#include "game/biomecel.h"
#include "game/blobrecords.h"
#include "game/feat_climate.h"
#include "game/feat_sensors.h"
#include "game/feat_water.h"
#include "game/featenv.h"
#include "game/gameobj.h"
#include "game/obj_food.h"
#include "game/plant.h"
#include "game/savegame.h"
#include "game/self.h"
#include "game/self_display.h"
#include "game/settings.h"
#include "game/sim.h"
#include "game/site.h"
#include "game/soundfx.h"
#include "game/waterflow.h"
#include "game/watertank.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>

using namespace biosys;
using namespace biosys::game;

namespace {

struct Host : PlayerHost {
    Timeline* tl = nullptr;
    i32 deathTime = -1;
    std::function<void(unsigned, int)> advance;
    u32 getTickCount() override { return static_cast<u32>(tl->m_dwTime) * 40u; }   // 40 ms per game second
    bool ChangeGameObjectVariation(const char*, const char*, int) override { return true; }
    bool playSoundHandle(const char*, int*) override { return true; }
    // The window's idle player has moved the pointer over a hotspot at least once (lastHoverTick != 0): with energy 0 the full update of
    // CSelf::Update (dll:10066aa0) makes it pass out (Sleep(1,1,1)). The sleep page then runs the night in CSelf::OnIdle.
    u32 lastHoverTick() override { return 1; }
    bool inUpdate = false, collapse = false, noCollapse = false;   // noCollapse: the old idle player, which never passes out
    int jumpToPage(const char* page, int, int, int, int) override {
        if (inUpdate && std::strcmp(page, "zLandOfNod") == 0) collapse = true;
        return 1;
    }
    void CTimeline_SetTicksPerGameSec(int t) override { tl->m_dwTicksPerGameSec = t; }
    void advanceTime(unsigned seconds, int updateSelf) override { if (advance) advance(seconds, updateSelf); }
    void setDead(int v) override {
        if (v != 0 && dead() == 0) deathTime = tl->m_dwTime;
        PlayerHost::setDead(v);
    }
};

// The rain machines switch a CRainFX by its id (CWorld::StartPageFX dll:10014280 / StopPageFX dll:100142d0), and the FX raises or clears the
// "it is raining here" flag (bit 1) of its site's cel (CRainFX::Update dll:10073e20 / CRainFX::Stop dll:10073d30; BiosGame::localFxUpdate /
// localFxReset do the same in the game). The player's sleep reads that flag (rain wakes a sleeper in the open, CSelf::OnIdle) and so do
// the plants and the rain gauge. Without these two calls the flag stays at whatever the saved game says for ever.
struct RainFxHost : FeatureHost {
    World* w = nullptr;
    static void flag(CSoundFX& f, bool raining) {
        if (f.site() && f.site()->cel()) f.site()->cel()->setFlags(raining ? (f.site()->cel()->flags() | 2u) : (f.site()->cel()->flags() & 0xfffffffdu));
    }
    void startPageFX(int id) override {
        for (Item* it : w->list(Table::LocalFx))
            if (auto* f = dynamic_cast<CRainFX*>(it))
                if (f->id() == id) { f->setOn(true); flag(*f, true); }
    }
    void stopPageFX(int id) override {
        for (Item* it : w->list(Table::LocalFx))
            if (auto* f = dynamic_cast<CRainFX*>(it))
                if (f->id() == id) { f->setOn(false); flag(*f, false); }
    }
    // CWorld::LocalFxUpdate (dll:10014350) right after a game is loaded: the flag follows the saved on / off of each rain FX
    void syncAll() {
        for (Item* it : w->list(Table::LocalFx))
            if (auto* f = dynamic_cast<CRainFX*>(it)) flag(*f, f->on());
    }
};

struct Rig {
    SaveGame sg;
    Host host;
    RecordingPlayerDisplay disp;
    std::unique_ptr<Sim> sim;
    i32 start = 0;
    bool killPlants = false;          // --kill-plants: every plant is held at health 0 (long term 0, short term at its floor)
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

    void step(bool updateSelf) {
        Timeline& t = sg.timeline;
        sim->updateWeather();
        sim->updateOcean();
        sim->updateAirFlows();
        sim->updateWaterFlows();
        cworld::updateAllObjects(sg.world, sg.self, t.m_dwTime);
        cworld::updateAllFeatures(sg.world, t.m_dwTime);
        sim->updateAllPlants();
        if (killPlants)
            for (Item* it : w().list(Table::Plants)) {
                CPlant& p = as<CPlant>(it);
                if ((p.flags() & 8) == 0) { p.setLongTermHealth(0.0); p.setShortTermHealth(0.3); p.setHealth(0.0); }
            }
        if (updateSelf && host.dead() == 0) {
            host.inUpdate = true;
            sg.self.Update(sg.self.m_dEnergy <= 0.0 && !host.noCollapse ? 1 : 0);      // only the full update lets an exhausted player pass out (it is what the game's timer does)
            host.inUpdate = false;
            if (sg.self.m_bDieOnUpdate != 0) sg.self.Die(0);      // the next timer call does this (BiosGame::onTimer)
        }
        t.m_dwTime = static_cast<i32>(static_cast<u32>(t.m_dwTime) + 0x5a);
        timeOfDay();
        if (host.collapse) {        // the page "zLandOfNod" was drawn (BiosGame::onPageLoaded): the night, then WakeUp
            host.collapse = false;
            sg.self.m_bAwake = 0;
            sg.self.m_bRunSleepOnIdle = 1;
            sg.self.OnIdle();
        }
    }

    // CTimeline::Advance dll:100154b0 as BiosGame::CTimeline_Advance does it
    void advance(unsigned seconds, int updateSelf) {
        Timeline& t = sg.timeline;
        const unsigned steps = (seconds + 0x5a) / 0x5a;
        for (unsigned n = 0; n < steps; n++) step(updateSelf != 0);
        if (seconds < steps * 0x5a) {
            t.m_dwTime = static_cast<i32>(static_cast<u32>(t.m_dwTime) + seconds - steps * 0x5a);
            timeOfDay();
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------------
// the scenarios

// The default machine programme. A new game starts with the machines in the state NewGame.sav saves (the heaters thermostatic,
// the rest off) and the start-up of BiosGame::onStartup (dll:10008cf0 InitNewGameState: the wave machine is activated). The
// programme is what a player who changes nothing but switches every machine with a timer on would have: each machine runs on its
// own default window (rain machines 10:00-14:00, the RO plant and UV 13:00-14:00, the algae scrubber 13:00-14:00, the bubbler
// 06:00-18:00, the air mixers 06:00-18:00 ...), the humidistat of each mister is on, the fans run their default 12 hours once
// (a fan's limit is a run time, not a window), the silt scrubbers are on. Nothing is re-programmed, nothing is refilled.
void applyProgramme(Rig& r) {
    if (CFeature* f = r.w().findFeature("fbo1Wave")) f->activate();       // BiosGame::onStartup
    for (Item* it : r.w().list(Table::Features)) {
        CFeature* f = dynamic_cast<CFeature*>(it);
        if (!f) continue;
        const std::string& c = f->className();
        if (c == "CRainMachine" || c == "CMister" || c == "CAirMixer" || c == "CFan" || c == "CPurifier" || c == "CSiltScrubber" ||
            c == "CAlgaeScrubber" || c == "CBubbler" || c == "CHeater" || c == "COxygenStore" || c == "CTideMachine")
            if (!f->on()) f->turnOn();
    }
}

// The machines off: every machine class of applyProgramme is switched off and deactivated (the generators, the fuse boxes and the
// sensors stay). The study's "frozen" run.
void applyFrozen(Rig& r) {
    for (Item* it : r.w().list(Table::Features)) {
        CFeature* f = dynamic_cast<CFeature*>(it);
        if (!f) continue;
        const std::string& c = f->className();
        if (c == "CRainMachine" || c == "CMister" || c == "CAirMixer" || c == "CFan" || c == "CPurifier" || c == "CSiltScrubber" ||
            c == "CAlgaeScrubber" || c == "CBubbler" || c == "CHeater" || c == "COxygenStore" || c == "CTideMachine" ||
            c == "CWaveMachine" || c == "CWaterPump") {
            f->turnOff();
            f->deactivate();
        }
    }
}

void listState(Rig& r) {
    std::printf("features: name class on working active running timerStart timerEnd timeLimit id mask\n");
    for (Item* it : r.w().list(Table::Features)) {
        const CFeature* f = dynamic_cast<const CFeature*>(it);
        if (!f) continue;
        std::printf("  %-12s %-14s on=%d wk=%d act=%d run=%d t=%d..%d lim=%d id=%u mask=%x\n", f->featureName().c_str(), f->className().c_str(),
                    f->on(), f->working(), f->active(), f->running(), f->timerStart(), f->timerEnd(), f->timeLimit(), f->id(), f->powerMask());
    }
    std::printf("foods: name state nutrition protein water code plant cel\n");
    for (Item* it : r.w().list(Table::Objects)) {
        const ObjFood* o = dynamic_cast<const ObjFood*>(it);
        if (!o) continue;
        std::printf("  %-12s st=%d nut=%g prot=%g water=%g code=%x plant=%s cel=%s\n", o->name().c_str(), o->state(), o->nutrition(), o->protein(),
                    o->water(), o->consumableCode(), o->plant() ? o->plant()->name().c_str() : "-",
                    o->plant() && o->plant()->cel() ? o->plant()->cel()->name().c_str() : "-");
    }
    std::printf("player: site=%s location=%s\n", r.sg.self.m_pSite.name().c_str(), reinterpret_cast<const char*>(r.sg.self.m_szLocation.data()));
}

// ---------------------------------------------------------------------------------------------------------------------
// the sensible player

struct SensiblePlayer {
    Rig& r;
    int drinks = 0, meals = 0, sleeps = 0, refusedMeals = 0;
    bool inHouse = false;
    SensiblePlayer(Rig& rig) : r(rig) {}

    void moveTo(const char* siteName, const char* page) {
        Self& s = r.sg.self;
        CSite* site = r.w().findSite(siteName);
        if (!site) throw Error(std::string("no site ") + siteName);
        s.m_pSite.set(site);
        s.m_pCel = site->cel();
        s.m_pBiome = site->biome();
        s.m_szLocation.fill(0);
        std::memcpy(s.m_szLocation.data(), page, std::min<size_t>(std::strlen(page), s.m_szLocation.size() - 1));
    }

    // the best fruit or other food the world offers: on its plant or lying in the scene (state 2), not a drug
    ObjFood* bestFood() {
        ObjFood* best = nullptr;
        for (Item* it : r.w().list(Table::Objects)) {
            ObjFood* o = dynamic_cast<ObjFood*>(it);
            if (!o || o->state() != 2 || o->nutrition() < 5.0) continue;
            if (o->consumableCode() > 0x1 && o->consumableCode() != 0x118 && o->consumableCode() != 0x122) continue;   // drugs, poisons
            if (o->plant() && o->plant()->health() <= 0.4) continue;
            if (!best || o->nutrition() > best->nutrition()) best = o;
        }
        return best;
    }

    void act() {
        Self& s = r.sg.self;
        if (r.host.dead() != 0) return;
        const u32 tod = static_cast<u32>(r.sg.timeline.m_dwTimeOfDay);
        const bool night = tod >= 21u * 3600u || tod < 6u * 3600u;
        if (night && !inHouse) { moveTo("PH5", "NPH5BED"); inHouse = true; }
        if (!night && inHouse) { moveTo("R4", "R4"); inHouse = false; }
        if (s.m_bAwake == 0) return;
        for (int guard = 0; s.m_dHydration < 45.0 && guard < 5; guard++)
            if (s.Drink(0xbe) != 0) drinks++;
        if (s.m_dNutrition < 55.0) {
            if (ObjFood* f = bestFood()) {
                f->pickUp(s);
                f->use(s);
                meals++;
            } else {
                refusedMeals++;
            }
        }
        if (night && inHouse && s.CanSleep() != 0 && s.Sleep(0, 0, 1) != 0) {
            s.m_bAwake = 0;                  // the page "zLandOfNod" (BiosGame::onPageLoaded)
            s.m_bRunSleepOnIdle = 1;
            s.OnIdle();                      // the night: 90 s steps through host().advanceTime, then WakeUp
            sleeps++;
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------------
// the columns

std::string f(double v) {
    char b[40];
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

std::string g_watch;      // --watch CEL: extra columns for one cel

struct Extra {
    int drinks = 0, meals = 0, sleeps = 0;
    double minHealth = 100;
    std::map<std::string, double> humSum;     // per biome: sum over the steps of the day of the mean cel humidity
    int humN = 0;
};

std::vector<std::string> columns(Rig* r, bool header, const Extra& ex) {
    std::vector<std::string> out;
    auto put = [&](const std::string& name, const std::string& value) { out.push_back(header ? name : value); };
    struct Acc { double t = 0, h = 0, c = 0, o = 0, p = 0, sw = 0; int n = 0; };
    std::map<std::string, Acc> bio;
    struct PlantAcc { int alive = 0, n = 0; double health = 0; };
    std::map<std::string, PlantAcc> bioPlants;
    std::map<std::string, std::pair<int, int>> bioAlarm;          // sensors with a temperature alarm, with a humidity alarm
    std::map<std::string, int> bioSensors;
    if (r) {
        for (Item* it : r->w().list(Table::Cels)) {
            const CBiomeCel& c = as<CBiomeCel>(it);
            Acc& a = bio[c.biome() ? c.biome()->name : "?"];
            a.t += c.airTemp(); a.h += c.humidity(); a.c += c.co2(); a.o += c.oxygen(); a.p += c.pollution(); a.sw += c.soilWater(); a.n++;
        }
        for (Item* it : r->w().list(Table::Plants)) {
            const CPlant& p = as<CPlant>(it);
            if ((p.flags() & 8) != 0 || !p.cel() || !p.cel()->biome()) continue;
            auto& e = bioPlants[p.cel()->biome()->name];
            e.health += p.health();
            e.n++;
            if (p.health() > 0.0) e.alive++;
        }
        for (Item* it : r->w().related(Rel::WorldSensor, nullptr)) {
            const CBiomeSensor& s = as<CBiomeSensor>(it);
            if (!s.cel() || !s.cel()->biome() || dynamic_cast<const CLung*>(&s)) continue;
            const std::string n = s.cel()->biome()->name;
            bioSensors[n]++;
            if (s.alarms() & 0x3) bioAlarm[n].first++;
            if (s.alarms() & 0x6000) bioAlarm[n].second++;
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
        put(n + ".humMean", ex.humN && ex.humSum.count(n) ? f(ex.humSum.at(n) / ex.humN) : "");
        put(n + ".co2", f(a.c * k));
        put(n + ".o2", f(a.o * k));
        put(n + ".pollution", f(a.p * k));
        put(n + ".soilWater", f(a.sw * k));
        const auto& pl = bioPlants[n];
        const int sensors = bioSensors[n];
        put(n + ".plantsAlive", f(pl.alive));
        put(n + ".plantHealth", pl.n ? f(pl.health / pl.n) : "");
        put(n + ".tempAlarm", sensors ? f(static_cast<double>(bioAlarm[n].first) / sensors) : "");
        put(n + ".humAlarm", sensors ? f(static_cast<double>(bioAlarm[n].second) / sensors) : "");
    }
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
    int alive = 0, n = 0;
    double health = 0;
    long fruit = 0;
    if (r) {
        for (Item* it : r->w().list(Table::Plants)) {
            const CPlant& p = as<CPlant>(it);
            fruit += p.fruitCount();
            if ((p.flags() & 8) != 0) continue;
            n++;
            health += p.health();
            if (p.health() > 0.0) alive++;
        }
    }
    put("plants.alive", f(alive));
    put("plants.health", n ? f(health / n) : "");
    put("plants.fruit", f(static_cast<double>(fruit)));
    if (!g_watch.empty()) {
        const CBiomeCel* c = r ? r->w().findBiomeCel(g_watch.c_str()) : nullptr;
        int alive = 0, n = 0;
        double health = 0;
        long fruit = 0;
        if (c)
            for (Item* it : r->w().list(Table::Plants)) {
                const CPlant& p = as<CPlant>(it);
                if (p.cel() != c || (p.flags() & 8) != 0) continue;
                n++; health += p.health(); fruit += p.fruitCount();
                if (p.health() > 0.0) alive++;
            }
        const std::string w = g_watch + ".";
        put(w + "soilWater", c ? f(c->soilWater()) : "");
        put(w + "o2", c ? f(c->oxygen()) : "");
        put(w + "co2", c ? f(c->co2()) : "");
        put(w + "humidity", c ? f(c->humidity()) : "");
        put(w + "plants", c ? f(n) : "");
        put(w + "alive", c ? f(alive) : "");
        put(w + "health", n ? f(health / n) : "");
        put(w + "fruit", c ? f(static_cast<double>(fruit)) : "");
    }
    {
        double tanks = 0, vap = 0;
        if (r) {
            for (Item* it : r->w().list(Table::Tanks)) tanks += as<CWaterTank>(it).waterVol();
            for (Item* it : r->w().list(Table::Cels)) vap += as<CBiomeCel>(it).humidity() * 4.1666666666666667e-06;
        }
        auto hp = [](double v) { char b[40]; std::snprintf(b, sizeof b, "%.12g", v); return std::string(b); };   // the check needs the digits
        {
            double ev = 0, tr = 0;
            if (r) for (auto& kv : r->sim->vapourIn) { ev += kv.second.first; tr += kv.second.second; }
            put("vapour.evap", r ? hp(ev) : ""); put("vapour.transp", r ? hp(tr) : "");
            put("vapour.condensed", r ? hp(r->sim->statCondensed) : ""); put("vapour.rain", r ? hp(r->sim->statRain) : "");
        }
        put("water.tanks", r ? hp(tanks) : "");
        put("water.vapour", r ? hp(vap) : "");
        put("water.total", r ? hp(tanks + vap) : "");
        for (const char* b : {"rainforest", "savannah", "deadbiome"}) {
            double ev = 0, tr = 0;
            if (r)
                for (auto& kv : r->sim->vapourIn)
                    if (kv.first && std::string(kv.first->name) == b) { ev = kv.second.first; tr = kv.second.second; }
            put(std::string(b) + ".vapourEvap", r ? f(ev) : "");
            put(std::string(b) + ".vapourTransp", r ? f(tr) : "");
        }
    }
    const WorldHeader* h = r ? r->sg.world.state().sim.world : nullptr;
    put("power.stored", h ? f(h->m_dPower) : "");
    put("power.generation", h ? f(h->m_dPowerGeneration * 3600.0) : "");
    put("power.demand", h ? f(h->m_dPowerDemand * 3600.0) : "");
    put("power.balance", h ? f((h->m_dPowerGeneration - h->m_dPowerDemand) * 3600.0) : "");
    const Self* s = r ? &r->sg.self : nullptr;
    put("player.alive", r ? (r->host.dead() ? "0" : "1") : "");
    put("player.health", s ? f(s->m_dHealth) : "");
    put("player.minHealth", r ? f(ex.minHealth) : "");
    put("player.temperature", s ? f(s->m_dTemperature) : "");
    put("player.hydration", s ? f(s->m_dHydration) : "");
    put("player.nutrition", s ? f(s->m_dNutrition) : "");
    put("player.protein", s ? f(s->m_dProtein) : "");
    put("player.energy", s ? f(s->m_dEnergy) : "");
    put("player.drinks", r ? f(ex.drinks) : "");
    put("player.meals", r ? f(ex.meals) : "");
    put("player.sleeps", r ? f(ex.sleeps) : "");
    put("player.deathTime", r && r->host.dead() ? f(r->host.deathTime - r->start) : "");
    put("player.deathCause", r && r->host.dead() ? f(s->m_wCauseOfDeath) : "");
    return out;
}

void row(Rig& r, unsigned seed, const std::string& label, int day, bool header, const Extra& ex) {
    std::string line;
    auto add = [&](const std::string& s) { line += (line.empty() ? "" : "\t") + s; };
    add(header ? "seed" : std::to_string(seed));
    add(header ? "settings" : label);
    add(header ? "day" : std::to_string(day));
    for (const std::string& c : columns(&r, header, ex)) add(c);
    std::puts(line.c_str());
}

} // namespace

int main(int argc, char** argv) {
    try {
        unsigned seed = 1;
        int days = 30;
        bool header = true, list = false, start = false, killPlants = false, noCollapse = false;
        std::string realism = "faithful", difficulty = "original", fixes, modelOverride, scenario = "idle", player = "idle";
        std::vector<std::pair<std::string, double>> tunes, calibs;
        std::string trace;
        bool dumpTanks = false;
        bool haveModel = false;
        std::string sav = BIOSYS_DEV_ROOT "/setup-cd/installed/DATA1/NewGame.sav";
        for (int i = 1; i < argc; i++) {
            const std::string a = argv[i];
            auto next = [&]() -> std::string {
                if (i + 1 >= argc) throw Error("missing value after " + a);
                return argv[++i];
            };
            if (a == "--seed") seed = static_cast<unsigned>(std::strtoul(next().c_str(), nullptr, 10));
            else if (a == "--days") days = std::atoi(next().c_str());
            else if (a == "--realism") realism = next();
            else if (a == "--difficulty") difficulty = next();
            else if (a == "--fixes") fixes = next();
            else if (a == "--model") { modelOverride = next(); haveModel = true; }
            else if (a == "--tune") {
                const std::string kv = next();
                const size_t eq = kv.find('=');
                if (eq == std::string::npos) throw Error("--tune wants name=value");
                tunes.push_back({kv.substr(0, eq), std::strtod(kv.c_str() + eq + 1, nullptr)});
            }
            else if (a == "--calib") {
                const std::string kv = next();
                const size_t eq = kv.find('=');
                if (eq == std::string::npos) throw Error("--calib wants name=value");
                calibs.push_back({kv.substr(0, eq), std::strtod(kv.c_str() + eq + 1, nullptr)});
            }
            else if (a == "--scenario") scenario = next();
            else if (a == "--player") player = next();
            else if (a == "--start") start = true;
            else if (a == "--no-collapse") noCollapse = true;
            else if (a == "--watch") g_watch = next();
            else if (a == "--trace") trace = next();
            else if (a == "--dump-tanks") dumpTanks = true;
            else if (a == "--kill-plants") killPlants = true;
            else if (a == "--sav") sav = next();
            else if (a == "--no-header") header = false;
            else if (a == "--list") list = true;
            else throw Error("usage: biosys-settingsmeasure [--seed N] [--days N] [--realism R] [--difficulty D] [--fixes LIST] [--model LIST] [--tune k=v] "
                             "[--scenario idle|programme] [--player idle|sensible] [--start] [--no-collapse] [--sav FILE] [--no-header] [--list]");
        }
        if (scenario != "idle" && scenario != "programme" && scenario != "frozen") throw Error("bad --scenario " + scenario);
        if (player != "idle" && player != "sensible") throw Error("bad --player " + player);
        Settings st = Settings::parse(realism, difficulty, fixes);
        if (haveModel) st.model = Model::parse(modelOverride);
        for (auto& t : tunes) st.tuning.set(t.first, t.second);
        for (auto& c : calibs) {
            if (c.first == "kE") st.model.evapRate = c.second;
            else if (c.first == "n") st.model.evapExponent = c.second;
            else if (c.first == "k7") st.model.transpRate = c.second;
            else throw Error("--calib wants kE, n or k7");
        }
        SaveGame::Options o;
        o.randSeed = seed;
        o.set(st);
        Rig r(o);
        r.sg.load(SaveGame::blobOf(readFile(sav)));
        r.sim = std::make_unique<Sim>(r.sg);
        r.host.noCollapse = noCollapse;
        RainFxHost rainHost;
        rainHost.w = &r.sg.world;
        r.sg.world.state().host = &rainHost;
        rainHost.syncAll();
        bindFeatureHooks(*r.sim);
        r.host.tl = &r.sg.timeline;
        r.host.advance = [&r](unsigned s, int u) { r.advance(s, u); };
        r.sg.self.bind(r.sg.world, &r.host, &r.disp);
        r.sg.world.state().rng.seed(seed);
        r.timeOfDay();
        if (list) { listState(r); return 0; }
        if (scenario == "programme") applyProgramme(r);
        if (scenario == "frozen") applyFrozen(r);
        r.killPlants = killPlants;
        std::map<std::string, double> tank0;
        for (Item* it : r.w().list(Table::Tanks)) tank0[it->name()] = as<CWaterTank>(it).waterVol();
        if (start) r.sg.self.applyStartValues(st.tuning.startHydration, st.tuning.startNutrition, st.tuning.startEnergy);
        r.start = r.sg.timeline.m_dwTime;
        SensiblePlayer pl(r);
        Extra ex;
        ex.minHealth = r.sg.self.m_dHealth;
        const std::string label = st.toString() + "|" + scenario + "|" + player;
        if (header) row(r, seed, label, 0, true, ex);
        row(r, seed, label, 0, false, ex);
        for (int d = 1; d <= days; d++) {
            const i32 until = r.start + d * 86400;
            while (r.sg.timeline.m_dwTime < until) {
                if (player == "sensible") pl.act();
                r.step(true);
                ex.minHealth = std::min(ex.minHealth, r.sg.self.m_dHealth);
                if (!trace.empty() && static_cast<u32>(r.sg.timeline.m_dwTimeOfDay) % 3600u < 90u) {   // --trace BIOME: hourly
                    double hs = 0; int cn = 0;
                    for (Item* it : r.w().list(Table::Cels)) {
                        const CBiomeCel& c = as<CBiomeCel>(it);
                        if (c.biome() && trace == c.biome()->name) { hs += c.humidity(); cn++; }
                    }
                    std::printf("trace\t%d\t%u\t%.2f\n", d, static_cast<u32>(r.sg.timeline.m_dwTimeOfDay) / 3600u, cn ? hs / cn : -1.0);
                }
                {   // daily mean humidity per biome
                    std::map<std::string, std::pair<double, int>> acc;
                    for (Item* it : r.w().list(Table::Cels)) {
                        const CBiomeCel& c = as<CBiomeCel>(it);
                        auto& e = acc[c.biome() ? c.biome()->name : "?"];
                        e.first += c.humidity(); e.second++;
                    }
                    for (auto& kv : acc) ex.humSum[kv.first] += kv.second.first / kv.second.second;
                    ex.humN++;
                }
            }
            ex.drinks = pl.drinks; ex.meals = pl.meals; ex.sleeps = pl.sleeps;
            row(r, seed, label, d, false, ex);
            ex.humSum.clear(); ex.humN = 0;
        }
        if (dumpTanks)
            for (Item* it : r.w().list(Table::Tanks)) {
                const double dv = as<CWaterTank>(it).waterVol() - tank0[it->name()];
                if (std::fabs(dv) > 1e-9) std::fprintf(stderr, "tank %-10s %+.3e\n", it->name().c_str(), dv);
            }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
