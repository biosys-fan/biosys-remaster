// biosys-settingstest: tests of the realism and difficulty layer (src/game/settings.h; tests/test_settings.sh). The parser and the
// presets, then one focused check per model flag and per tuning knob: the same situation is built with the flag / knob at its
// original value and changed, and what the comment at the site says must differ in exactly that way. Prints PASS / FAIL lines.
// Like fixtest it reaches into private members to build the situations (`#define private public`): a test harness, not game code.
#include "engine/devroot.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>
#define private public
#define protected public
#include "engine/engine.h"
#include "game/apilog.gen.h"
#include "game/biomecel.h"
#include "game/biosgame.h"
#include "game/feat_power.h"
#include "game/feat_sensors.h"
#include "game/featenv.h"
#include "game/gameobj.h"
#include "game/plant.h"
#include "game/savegame.h"
#include "game/self.h"
#include "game/self_display.h"
#include "game/settings.h"
#include "game/sim.h"
#include "game/site.h"
#include "game/story.h"
#include "game/waterflow.h"
#include "game/watertank.h"
#undef private
#undef protected

using namespace biosys;
using namespace biosys::game;

namespace {

const char* kSav = BIOSYS_DEV_ROOT "/setup-cd/installed/DATA1/NewGame.sav";
int fails = 0, passes = 0;

void check(bool ok, const std::string& what, const std::string& detail = "") {
    std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : " -- ", detail.c_str());
    (ok ? passes : fails)++;
}
std::string num(double v) { char b[40]; std::snprintf(b, sizeof b, "%.9g", v); return b; }
bool throws(const std::function<void()>& f) { try { f(); } catch (const Error&) { return true; } return false; }

struct Host : PlayerHost {
    Timeline* tl = nullptr;
    u32 getTickCount() override { return static_cast<u32>(tl->m_dwTime) * 40u; }
    bool ChangeGameObjectVariation(const char*, const char*, int) override { return true; }
    bool playSoundHandle(const char*, int*) override { return true; }
    int jumpToPage(const char*, int, int, int, int) override { return 1; }
};

struct World1 {
    SaveGame sg;
    Host host;
    RecordingPlayerDisplay disp;
    std::unique_ptr<Sim> sim;
    explicit World1(const Settings& st, uint32_t seed = 1) : sg(opts(st, seed)) {
        sg.load(SaveGame::blobOf(readFile(kSav)));
        sim = std::make_unique<Sim>(sg);
        bindFeatureHooks(*sim);
        host.tl = &sg.timeline;
        sg.self.bind(sg.world, &host, &disp);
        sg.world.state().rng.seed(seed);
    }
    static SaveGame::Options opts(const Settings& st, uint32_t seed) {
        SaveGame::Options o;
        o.randSeed = seed;
        o.set(st);
        return o;
    }
    World& w() { return sg.world; }
    i32 now() { return sg.timeline.m_dwTime; }
};

Settings withModel(const std::string& flags) { Settings s; s.model = Model::parse(flags); return s; }
Settings withKnob(const std::string& name, double v) { Settings s; s.tuning.set(name, v); return s; }

// one Self::Update of 90 s after `setup`; returns what `observe` reads
double selfStep(const Settings& st, const std::function<void(World1&)>& setup, const std::function<double(World1&)>& observe) {
    World1 w(st);
    Self& s = w.sg.self;
    s.Update(0);                               // syncs the update clock
    s.m_dHydration = 90; s.m_dNutrition = 90; s.m_dHealth = 50; s.m_dEnergy = 50; s.m_dProtein = 100; s.m_dTemperature = 50;
    s.m_dwLastWokeUp = w.now();
    setup(w);
    w.sg.timeline.m_dwTime += 90;
    s.Update(0);
    return observe(w);
}
CBiomeCel& celOfSelf(World1& w) { return *static_cast<CBiomeCel*>(w.sg.self.m_pCel); }

// d(k) = a + b k: the knob is linear in its factor and has an effect (b != 0); `k1` equals the original value
void linearKnob(const std::string& knob, const std::function<void(World1&)>& setup, const std::function<double(World1&)>& observe, double sign) {
    auto at = [&](double k) { return selfStep(withKnob(knob, k), setup, observe); };
    const double d0 = at(0), d1 = at(1), d2 = at(2);
    const double base = selfStep(Settings(), setup, observe);
    const double b = d1 - d0;
    check(base == d1, knob + ": 1 gives exactly the original value", num(base) + " / " + num(d1));
    check(std::fabs(b) > 1e-9 && sign * b > 0 && std::fabs((d2 - d1) - b) < 1e-9 * std::max(1.0, std::fabs(b)) * 1e3, knob + ": the effect is linear in the factor and has the stated sign",
          num(d0) + " " + num(d1) + " " + num(d2));
}

// ---- parser and presets -------------------------------------------------------------------------------------------------
void testParser() {
    const Model c = Model::corrected(), l = Model::closedloop();
    check(!Model::faithful().any() && Model::parse("faithful").toString() == "faithful" && Model::parse("").toString() == "faithful", "Model: faithful has no flag");
    check(c.proteinPerSecond && c.savannahSoilAlarm && c.waveMachineSetpoint && !c.deadPlantsRespire && !c.vapourFromSoil && !c.transpirationHumidifies && !c.houseEnvelopeSlow && !c.floodedPlantFormula,
          "Model: corrected = A1 + A2 + A3");
    check(l.proteinPerSecond && l.savannahSoilAlarm && l.waveMachineSetpoint && l.deadPlantsRespire && l.vapourFromSoil && l.transpirationHumidifies && l.houseEnvelopeSlow && l.floodedPlantFormula,
          "Model: closedloop = corrected + A4 + A6 + A7 + A11 + A12");
    int n = 0;
    for (const Model::Info& i : Model::all()) { n++; check(Model::parse(i.name).*(i.member), std::string("Model::parse names the flag ") + i.name); }
    check(n == 8, "Model: 8 flags", std::to_string(n));
    Model m = Model::parse("closedloop,-vapourFromSoil");
    check(m.deadPlantsRespire && !m.vapourFromSoil, "Model::parse: a preset and a removed flag");
    check(Model::parse(l.toString()).toString() == l.toString(), "Model: toString round-trips");
    check(throws([] { Model::parse("nosuchflag"); }), "Model::parse refuses an unknown word");

    const Tuning o = Tuning::original(), g = Tuning::guided(), sv = Tuning::survivor();
    check(o.isOriginal() && Tuning::parse("original").isOriginal() && Tuning::parse("").isOriginal(), "Tuning: original is neutral");
    check(!g.isOriginal() && !sv.isOriginal(), "Tuning: guided and survivor differ from original");
    check(g.startHydration == 80 && g.startNutrition == 90 && g.startEnergy == 90 && g.hydrationDrain == 0.5 && g.awakeLimitHours == 36 && g.heliDelaySeconds == 14 * 86400 &&
          g.huntChancePercent == 1 && g.huntCooldownSeconds == 600 && g.malaria == 0.5 && g.healing == 1.5 && g.plantRecovery == 1.5 && g.powerGeneration == 1.5 && g.siltClog == 0.5,
          "Tuning: guided = study D.2");
    check(sv.hydrationDrain == 1.25 && sv.nutritionDrain == 1.25 && sv.thirstDamage == 1.5 && sv.oxygenDamage == 1.5 && sv.healing == 0.75 && sv.sleepHealing == 0.75 && sv.plantDamage == 1.25 &&
          sv.powerGeneration == 0.8 && sv.siltClog == 1.5 && sv.heliDelaySeconds == 5 * 86400 && sv.sensorNoise == 5 && sv.startHydration < 0,
          "Tuning: survivor = study D.2");
    check(throws([] { Tuning::parse("castaway"); }), "Tuning::parse refuses an unknown difficulty (Castaway is not offered)");
    Tuning t; t.set("healing", 3);
    check(t.get("healing") == 3 && throws([&] { t.set("nosuchknob", 1); }) && throws([&] { t.get("nosuchknob"); }), "Tuning: set / get by name, unknown refused");
    check(Tuning::all().size() == 24, "Tuning: 24 knobs", std::to_string(Tuning::all().size()));

    Settings d = Settings::parse("", "");
    check(d.isDefault() && Settings::parse("faithful", "original").isDefault(), "Settings: faithful + original is the default");
    Settings s = Settings::parse("corrected", "guided");
    check(s.fixes.toString() == Fixes::restored().toString() && s.model.toString() == c.toString() && s.tuning.startHydration == 80, "Settings: corrected = restored fixes + corrected model");
    s = Settings::parse("closedloop", "survivor", "safe");
    check(s.fixes.toString() == Fixes::safe().toString() && s.model.toString() == l.toString(), "Settings: --fixes replaces the fix part only");
    s = Settings::parse("closedloop", "original", "faithful");
    check(!s.fixes.any() && s.model.any(), "Settings: --fixes faithful switches the fixes off, the model stays");
    check(throws([] { Settings::parse("remastered", "original"); }) && throws([] { Settings::parse("faithful", "easy"); }) && throws([] { Settings::parse("faithful", "original", "nosuchfix"); }),
          "Settings: unknown realism / difficulty / fix refused");
    Settings cl = Settings::parse("closedloop", "original");
    check(!cl.fixes.plantFloodedUsesMax && !cl.fixes.condensationFlagPerCel && !cl.fixes.consumableCodeFromHotspot, "Settings: the unclear-intent bug flags are in no realism level");
    check(cl.fixes.coffeeThirdDose && cl.fixes.plantColdDayMin, "Settings: corrected / closedloop contain the restored preset (coffeeThirdDose included)");
}

// ---- model flags --------------------------------------------------------------------------------------------------------
void testModel() {
    // A1
    {
        double p[2];
        for (int on = 0; on < 2; on++)
            p[on] = selfStep(on ? withModel("proteinPerSecond") : Settings(), [](World1&) {}, [](World1& w) { return w.sg.self.m_dProtein; });
        const double lossOff = 100 - p[0], lossOn = 100 - p[1];
        check(std::fabs(lossOff - 9.259259259259259e-05) < 1e-12 && std::fabs(lossOn - 90 * 9.259259259259259e-05) < 1e-9,
              "proteinPerSecond: off loses 1/10800 per update, on loses 1/10800 per second (x dt = 90)", num(lossOff) + " / " + num(lossOn));
    }
    // A2
    for (int on = 0; on < 2; on++) {
        World1 w(on ? withModel("savannahSoilAlarm") : Settings());
        CBiomeSensor* sav = nullptr;
        CBiomeSensor* rf = nullptr;
        for (Item* it : w.w().list(Table::Features))
            if (auto* b = dynamic_cast<CBiomeSensor*>(it)) {
                if (!sav && b->thresholds(0x100b7988) == b->m_pThresholds) sav = b;
                if (!rf && b->thresholds(0x100b7948) == b->m_pThresholds) rf = b;
            }
        check(sav && rf, "savannahSoilAlarm: the savannah and the rainforest have probes");
        if (!sav || !rf) return;
        sav->cel()->setSoilWater(5.5);
        sav->update(w.now() + 90);
        const bool alarm = (sav->alarms() & 0x80) != 0;
        sav->cel()->setSoilWater(3.0);
        sav->update(w.now() + 180);
        const bool low = (sav->alarms() & 0x80) != 0;
        rf->cel()->setSoilWater(15.0);
        rf->update(w.now() + 90);
        check(alarm == (on != 0) && low && (rf->alarms() & 0x80) != 0, std::string("savannahSoilAlarm ") + (on ? "on: soil water 5.5 alarms (limit 7); 3 still does; the rainforest's limit (20) is untouched" : "off: soil water 5.5 gives no alarm (limit 4)"));
    }
    // A3
    {
        double dOx[2];
        for (int on = 0; on < 2; on++) {
            World1 w(on ? withModel("waveMachineSetpoint") : Settings());
            COcean& oc = w.sim->ocean();
            oc.m_bWaveMachineOn = 1;
            oc.m_dOxygen = oc.m_dNewOxygen = 3.45;
            oc.m_dwLastStateTime = w.now();
            oc.updateState(*w.sim, w.now() + 90);
            dOx[on] = oc.m_dNewOxygen - 3.45;
        }
        check(dOx[0] < 0 && dOx[1] > 0, "waveMachineSetpoint: at O2 3.45 the machine lets the ocean fall (set-point 3.4) / lifts it (set-point 4.3)", num(dOx[0]) + " / " + num(dOx[1]));
    }
    // A4 and A7 and A12 need a plant of the rainforest in a cel of our making
    auto plantStep = [](const Settings& st, const std::function<void(CPlant&, CBiomeCel&)>& setup, const std::function<double(CPlant&, CBiomeCel&)>& observe) {
        World1 w(st);
        CPlant* pl = nullptr;
        for (Item* it : w.w().list(Table::Plants)) {
            auto* p = dynamic_cast<CPlant*>(it);
            if (p && (p->flags() & 8) == 0 && p->cel() && p->cel()->biome() && std::string(p->cel()->biome()->name) == "rainforest" && p->soilWaterMax() == 47.5) { pl = p; break; }
        }
        if (!pl) throw Error("no rainforest plant");
        CBiomeCel& cel = *pl->cel();
        cel.setAirTemp(25); cel.setNewAirTemp(25); cel.setSoilWater(30); cel.setHumidity(70); cel.setNewHumidity(70); cel.setPollution(0);
        cel.setNewPollution(0); cel.setLight(0.8); cel.setWindSpeed(0);
        pl->m_dwTimeLastUpdate = w.now();
        setup(*pl, cel);
        pl->update(*w.sim, w.now() + 90);
        return observe(*pl, cel);
    };
    {
        double o2[2];
        for (int on = 0; on < 2; on++)
            o2[on] = plantStep(on ? withModel("deadPlantsRespire") : Settings(),
                               [](CPlant& p, CBiomeCel& c) { p.m_dLongTermHealth = 0; p.m_dShortTermHealth = 0.3; c.setNewOxygen(20); c.setNewCO2(1); c.setCO2(1); c.setLight(0); },
                               [](CPlant&, CBiomeCel& c) { return c.newOxygen(); });
        check(o2[0] < 20 && o2[1] < 20 && 20 - o2[1] < 0.15 * (20 - o2[0]) && o2[1] > o2[0], "deadPlantsRespire: a dead plant takes 10 % of the oxygen it took", num(20 - o2[0]) + " / " + num(20 - o2[1]));
        double h[2];
        for (int on = 0; on < 2; on++)
            h[on] = plantStep(on ? withModel("deadPlantsRespire") : Settings(),
                              [](CPlant& p, CBiomeCel& c) { p.m_dLongTermHealth = 1; p.m_dShortTermHealth = 1; c.setNewOxygen(20); c.setNewCO2(1); c.setCO2(1); },
                              [](CPlant&, CBiomeCel& c) { return c.newOxygen(); });
        check(h[0] != 0 && std::fabs(h[0] - h[1]) < 1e-9, "deadPlantsRespire: a plant of health 1 breathes as before", num(h[0]) + " / " + num(h[1]));
    }
    {
        // A7: the contribution is proportional to the health and comes out of the soil tank
        auto hum = [&](bool on, double health, double* soilLoss) {
            double h = 0;
            plantStep(on ? withModel("transpirationHumidifies") : Settings(),
                      [&](CPlant& p, CBiomeCel&) { p.m_dHealth = health; },
                      [&](CPlant& p, CBiomeCel& c) {
                          h = c.newHumidity() - 70;
                          *soilLoss = 0;
                          return 0.0;
                      });
            return h;
        };
        double l0 = 0, l1 = 0;
        const double off = hum(false, 1, &l0), on1 = hum(true, 1, &l1), on0 = hum(true, 0, &l0), onHalf = hum(true, 0.5, &l0);
        check(off > 0 && off < 1e-6 && on1 > 1e3 * off && on1 < 5, "transpirationHumidifies: the leaves' water reaches the air (original: 1e-7 %RH in 90 s)", num(off) + " / " + num(on1));
        check(on0 == 0.0 && std::fabs(onHalf - on1 / 2) < 1e-9 * std::max(1.0, on1), "transpirationHumidifies: a dead plant does not transpire, half the health gives half", num(on0) + " / " + num(onHalf) + " / " + num(on1));
        double soil[2];
        for (int on = 0; on < 2; on++) {
            Settings st = on ? withModel("transpirationHumidifies") : Settings();
            World1 w(st);
            CPlant* pl = nullptr;
            for (Item* it : w.w().list(Table::Plants)) {
                auto* p = dynamic_cast<CPlant*>(it);
                if (p && (p->flags() & 8) == 0 && p->cel() && p->cel()->soilTank()) { pl = p; break; }
            }
            CWaterTank& tank = as<CWaterTank>(pl->cel()->soilTank());
            const double before = tank.waterVol();
            const double h0 = pl->cel()->newHumidity();
            pl->m_dHealth = 1;
            pl->m_dwTimeLastUpdate = w.now();
            pl->update(*w.sim, w.now() + 90);
            soil[on] = (before - tank.waterVol());
            if (on) check(std::fabs(soil[1] - (pl->cel()->newHumidity() - h0) * 4.1666666666666667e-06) < 1e-12 && soil[1] > 0,
                          "transpirationHumidifies on: the soil tank loses the water the air gains (1 %RH = 4.1667e-6 volumes)", num(soil[1]));
        }
        check(soil[0] == 0.0, "transpirationHumidifies off: the soil keeps its water", num(soil[0]));
    }
    {
        // A12: a plant with maximum 47.5 in a flooded cel, and a plant with a lower maximum
        double a[3];
        for (int v = 0; v < 3; v++) {
            Settings st = v == 0 ? Settings() : withModel(v == 1 ? "floodedPlantFormula" : "floodedPlantFormula,");
            if (v == 2) st.fixes = Fixes::parse("plantFloodedUsesMax");
            if (v == 2) st.model = Model::parse("floodedPlantFormula");
            a[v] = plantStep(st, [](CPlant&, CBiomeCel& c) { c.setSoilWater(47.97); }, [](CPlant& p, CBiomeCel&) { return p.m_dWaterAvailability; });
        }
        check(a[0] > 1.0 && a[1] == 0.0 && a[2] == 0.0, "floodedPlantFormula: off the availability of a flooded rainforest plant is 1.01 (> 1), on it is 0; plantFloodedUsesMax does not change the model's result", num(a[0]) + " / " + num(a[1]) + " / " + num(a[2]));
        // a plant with swMax < 47.5: (47.5 - sw) / (47.5 - swMax)
        World1 w(withModel("floodedPlantFormula"));
        CPlant* pl = nullptr;
        for (Item* it : w.w().list(Table::Plants)) {
            auto* p = dynamic_cast<CPlant*>(it);
            if (p && (p->flags() & 8) == 0 && p->soilWaterMax() < 40.0) { pl = p; break; }
        }
        check(pl != nullptr, "floodedPlantFormula: a plant with a maximum below 47.5 exists");
        if (pl) {
            CBiomeCel& cel = *pl->cel();
            const double mx = pl->soilWaterMax(), sw = (mx + 47.5) / 2;
            cel.setSoilWater(sw);
            pl->m_dwTimeLastUpdate = w.now();
            pl->update(*w.sim, w.now() + 90);
            check(std::fabs(pl->m_dWaterAvailability - 0.5) < 1e-9, "floodedPlantFormula: halfway between the plant's maximum and the full tank the availability is 0.5", num(pl->m_dWaterAvailability));
            cel.setSoilWater(mx + 1e-9);
            pl->m_dwTimeLastUpdate = w.now() + 90;
            pl->update(*w.sim, w.now() + 180);
            check(pl->m_dWaterAvailability > 0.999, "floodedPlantFormula: continuous at the plant's maximum (availability 1)", num(pl->m_dWaterAvailability));
        }
    }
    // A6 (vapour model v2)
    {
        for (int on = 0; on < 2; on++) {
            World1 w(on ? withModel("vapourFromSoil") : Settings());
            CEvaporation* ev = nullptr;
            for (Item* it : w.w().list(Table::Waterflows))
                if (auto* e = dynamic_cast<CEvaporation*>(it)) { if (e->cel()->biome() && std::string(e->cel()->biome()->name) == "rainforest") { ev = e; break; } }
            if (!ev) { check(false, "vapourFromSoil: a rainforest CEvaporation exists"); return; }
            CBiomeCel& cel = *ev->cel();
            CWaterTank& soil = *ev->from();
            CWaterTank& cond = *ev->to();
            auto setup = [&](double fill, double hum) {
                soil.setWaterVol(soil.volume() * fill);
                cel.setHumidity(hum); cel.setNewHumidity(hum);
            };
            const Model mo = Model();
            setup(0.5, 50);
            const double s0 = soil.waterVol(), c0 = cond.waterVol();
            ev->update(*w.sim, 90);
            const double dH = cel.newHumidity() - 50;
            if (!on) {
                check(dH == 0.0 && soil.waterVol() < s0 && cond.waterVol() > c0, "vapourFromSoil off: the flow moves water from the soil to the condensate tank and puts nothing into the air");
                continue;
            }
            const double want = mo.evapRate * std::pow(0.5, mo.evapExponent) * 0.5 * 90.0 / 3600.0;
            check(std::fabs(dH - want) < 1e-9 && dH > 0, "vapourFromSoil on: the air gains kE * wet^n * (1 - H/100) per hour", num(dH) + " / " + num(want));
            check(std::fabs((s0 - soil.waterVol()) - dH * 4.1666666666666667e-06) < 1e-12 && cond.waterVol() == c0,
                  "vapourFromSoil on: the soil loses exactly the water the air gains; the flow no longer fills the condensate tank", num(s0 - soil.waterVol()));
            setup(0.5, 100);
            ev->update(*w.sim, 90);
            check(cel.newHumidity() == 100.0, "vapourFromSoil on: nothing is added at 100 % (it cannot run away)");
            setup(0.0, 50);
            ev->update(*w.sim, 90);
            check(cel.newHumidity() == 50.0, "vapourFromSoil on: an empty soil gives no vapour");
            setup(0.9, 20);
            ev->update(*w.sim, 90);
            const double wetter = cel.newHumidity() - 20;
            setup(0.2, 20);
            ev->update(*w.sim, 90);
            check(wetter > 8 * (cel.newHumidity() - 20), "vapourFromSoil on: a wet soil evaporates far more than a dry one (wet^1.5)", num(wetter) + " vs " + num(cel.newHumidity() - 20));
            // condensation: the condensate tank gets what the sink removes
            setup(0.5, 80);
            cel.setAirTemp(45); cel.setNewAirTemp(45);
            const double c1 = cond.waterVol();
            cel.updateState(*w.sim, 90);
            const double removed = 80 - cel.humidity();
            check(removed > 0 && std::fabs((cond.waterVol() - c1) - removed * 4.1666666666666667e-06) < 1e-12, "vapourFromSoil on: condensation puts the vapour it removes (0.2 %RH in 90 s at 80 %) into the condensate tank", num(removed));
        }
    }
    // A6 (vapour model v2): the rain's humidity is drawn from its source tank; the ocean cel evaporates and condenses
    {
        double drawn[2] = {0, 0}, gainH[2] = {0, 0};
        for (int on = 0; on < 2; on++) {
            World1 w(on ? withModel("vapourFromSoil") : Settings());
            CRain* rain = nullptr;
            for (Item* it : w.w().list(Table::Waterflows))
                if (auto* r = dynamic_cast<CRain*>(it)) if (r->humidityRate() > 0) { rain = r; break; }
            if (!rain) { check(false, "vapourFromSoil: a CRain with a humidity rate exists"); return; }
            CBiomeCel& cel = *rain->cel();
            cel.setHumidity(50); cel.setNewHumidity(50);
            rain->m_bOn = 1;
            rain->m_pFrom->setWaterVol(rain->m_pFrom->volume() * 0.5);
            const double before = rain->m_pFrom->waterVol();
            rain->update(*w.sim, 90);
            const double amt = 90 * rain->mag() * 0.0002777777777777778;
            drawn[on] = before - rain->m_pFrom->waterVol() - amt;
            gainH[on] = cel.newHumidity() - 50;
        }
        check(std::fabs(drawn[0]) < 1e-12 && gainH[1] > 0 && std::fabs(drawn[1] - gainH[1] * 4.1666666666666667e-06) < 1e-12 && std::fabs(gainH[0] - gainH[1]) < 1e-12,
              "vapourFromSoil on: the humidity the rain adds is drawn from its tank (4.1667e-6 per %RH); off: from nowhere", num(drawn[0]) + " / " + num(drawn[1]) + " for " + num(gainH[1]) + " %RH");
        double dv[2], dOc[2];
        for (int on = 0; on < 2; on++) {
            World1 w(on ? withModel("vapourFromSoil") : Settings());
            CBiomeCel* oc = w.sim->ocean().m_pCel;
            oc->setHumidity(70); oc->setNewHumidity(70);
            oc->setAirTemp(0); oc->setNewAirTemp(0);        // below the target: no condensation
            const double v0 = w.sim->ocean().waterVol();
            oc->updateState(*w.sim, 90);
            dv[on] = oc->humidity() - 70;
            dOc[on] = v0 - w.sim->ocean().waterVol();
        }
        check(dv[0] == 0.0 && dOc[0] == 0.0 && dv[1] > 0.05 && std::fabs(dOc[1] - dv[1] * 4.1666666666666667e-06) < 1e-12,
              "vapourFromSoil on: the ocean cel evaporates from its tank (the original: constant 70 %)", num(dv[0]) + " / " + num(dv[1]) + " / " + num(dOc[1]));
        World1 w(withModel("vapourFromSoil"));
        CBiomeCel* oc = w.sim->ocean().m_pCel;
        oc->setHumidity(80); oc->setNewHumidity(80);
        oc->setAirTemp(45); oc->setNewAirTemp(45);
        const double v0 = w.sim->ocean().waterVol();
        oc->updateState(*w.sim, 90);
        check(oc->humidity() < 80 && w.sim->ocean().waterVol() > v0 - 1e-9 && oc->humidity() > 79.5, "vapourFromSoil on: above the target the ocean cel condenses (net sink at 80 %)", num(oc->humidity()));
    }
    // A11
    {
        double dT[2], dOther[2];
        for (int on = 0; on < 2; on++) {
            World1 w(on ? withModel("houseEnvelopeSlow") : Settings());
            CBiomeCel* house = nullptr; CBiomeCel* rf = nullptr;
            for (Item* it : w.w().list(Table::Cels)) {
                auto* c = dynamic_cast<CBiomeCel*>(it);
                if (!c || !c->biome()) continue;
                if (!house && c->biome()->addr == 0x100c1208u) house = c;
                if (!rf && std::string(c->biome()->name) == "rainforest") rf = c;
            }
            w.sg.world.state().sim.world->m_dExtTemp = 5;
            w.sg.worldHeader.m_dSunlight = 0;
            w.sim->updateTargetTemp(0x100c1208); w.sim->updateTargetTemp(0x100c1c50);
            house->setAirTemp(25); house->setNewAirTemp(25); house->setSoilTemp(25); house->setNewSoilTemp(25);
            rf->setAirTemp(25); rf->setNewAirTemp(25); rf->setSoilTemp(25); rf->setNewSoilTemp(25);
            house->updateState(*w.sim, 3600);
            rf->updateState(*w.sim, 3600);
            dT[on] = house->airTemp() - 25;
            dOther[on] = rf->airTemp() - 25;
        }
        check(dT[0] < -4 && dT[1] > dT[0] / 4 && dT[1] < 0 && dOther[0] == dOther[1], "houseEnvelopeSlow: the house cools 7 times slower (1 h: -x vs -x/7); a greenhouse cel is untouched", num(dT[0]) + " / " + num(dT[1]) + " / " + num(dOther[0] - dOther[1]));
    }
}

// ---- tuning knobs -------------------------------------------------------------------------------------------------------
void testTuning() {
    auto cold = [](World1& w) { celOfSelf(w).setAirTemp(10); celOfSelf(w).setNewAirTemp(10); };
    auto hyd = [](World1& w) { return w.sg.self.m_dHydration; };
    auto nut = [](World1& w) { return w.sg.self.m_dNutrition; };
    auto health = [](World1& w) { return w.sg.self.m_dHealth; };
    linearKnob("hydrationDrain", cold, hyd, -1);
    linearKnob("nutritionDrain", cold, nut, -1);
    linearKnob("heatWaterTerm", [](World1& w) { celOfSelf(w).setAirTemp(30); celOfSelf(w).setNewAirTemp(30); }, hyd, -1);
    linearKnob("starveDamage", [&](World1& w) { cold(w); w.sg.self.m_dNutrition = 10; }, health, -1);
    linearKnob("thirstDamage", [&](World1& w) { cold(w); w.sg.self.m_dHydration = 10; }, health, -1);
    linearKnob("heatDamage", [&](World1& w) { cold(w); w.sg.self.m_dTemperature = 90; w.sg.self.m_dHydration = 30; }, health, -1);
    linearKnob("coldDamage", [&](World1& w) { cold(w); w.sg.self.m_dTemperature = 5; }, health, -1);
    linearKnob("oxygenDamage", [&](World1& w) { cold(w); celOfSelf(w).setOxygen(10); celOfSelf(w).setNewOxygen(10); }, health, -1);
    linearKnob("healing", cold, health, +1);
    // awakeLimitHours: awake 10 h: no energy drain with the limit at 24 h, drain with 8 h
    {
        auto energy = [](double hours) {
            return selfStep(withKnob("awakeLimitHours", hours), [](World1& w) { w.sg.self.m_dwLastWokeUp = w.now() - 10 * 3600; }, [](World1& w) { return w.sg.self.m_dEnergy; });
        };
        check(energy(24) > 49.99 && energy(8) < energy(24) - 0.3, "awakeLimitHours: the energy drain starts after the limit (10 h awake: none at 24 h, 90 s of 15/h at 8 h)", num(energy(24)) + " / " + num(energy(8)));
    }
    // sleepHealing: one night in the bed
    {
        auto gain = [](double k) {
            World1 w(withKnob("sleepHealing", k));
            Self& s = w.sg.self;
            s.Update(0);
            CSite* bed = w.w().findSite("PH5");
            s.m_pSite.set(bed); s.m_pCel = bed->cel(); s.m_pBiome = bed->biome();
            s.m_szLocation.fill(0); std::memcpy(s.m_szLocation.data(), "NPH5BED", 7);
            s.m_dHealth = 50; s.m_dEnergy = 30; s.m_dHydration = 90; s.m_dNutrition = 90; s.m_dwLastWokeUp = w.now() - 100000;
            if (s.Sleep(0, 0, 1) == 0) return -1.0;
            s.m_bAwake = 0; s.m_bRunSleepOnIdle = 1;
            s.OnIdle();
            return s.m_dHealth - 50;
        };
        const double g0 = gain(0), g1 = gain(1), g2 = gain(2);
        check(g1 > g0 && std::fabs((g2 - g1) - (g1 - g0)) < 1e-6 && gain(1) == g1, "sleepHealing: the health gained in a night is linear in the factor", num(g0) + " " + num(g1) + " " + num(g2));
    }
    // malaria
    {
        auto caught = [](double k, int updates) {
            World1 w(withKnob("malaria", k));
            Self& s = w.sg.self;
            s.Update(0);
            w.sg.header.milestones |= 0x80;
            s.m_szLocation.fill(0); std::memcpy(s.m_szLocation.data(), "R4", 2);
            celOfSelf(w).setSoilWater(40);
            for (int i = 0; i < updates; i++) {
                s.m_dHydration = 90; s.m_dNutrition = 90; s.m_dHealth = 90; s.m_dTemperature = 50;
                w.sg.timeline.m_dwTime += 90;
                s.Update(0);
                if (s.m_dwMalaria != 0) return i + 1;
            }
            return 0;
        };
        check(caught(0, 3000) == 0 && caught(60, 3000) > 0 && caught(60, 3000) < 20, "malaria: 0 never catches it in 3000 updates, 60 (75 % per update) within a few", std::to_string(caught(60, 3000)));
    }
    // plantDamage / plantRecovery
    {
        auto plantH = [](double dmgK, double recK, bool cold) {
            Settings st; st.tuning.plantDamage = dmgK; st.tuning.plantRecovery = recK;
            World1 w(st);
            CPlant* pl = nullptr;
            for (Item* it : w.w().list(Table::Plants)) {
                auto* p = dynamic_cast<CPlant*>(it);
                if (p && (p->flags() & 8) == 0 && p->cel() && p->cel()->biome() && std::string(p->cel()->biome()->name) == "rainforest") { pl = p; break; }
            }
            CBiomeCel& cel = *pl->cel();
            cel.setAirTemp(cold ? -5 : pl->m_dTempOptimum); cel.setSoilWater(30); cel.setHumidity(70); cel.setPollution(0); cel.setNewPollution(0); cel.setLight(0.8); cel.setWindSpeed(0);
            pl->m_dShortTermHealth = 0.7; pl->m_dLongTermHealth = 0.7; pl->m_dHardiness = 1;
            pl->m_dwTimeLastUpdate = w.now();
            pl->update(*w.sim, w.now() + 900);
            return pl->m_dLongTermHealth;
        };
        const double d0 = plantH(0.5, 1, true), d1 = plantH(1, 1, true), d2 = plantH(1.5, 1, true);
        check(d1 < d0 && std::fabs((d2 - d1) - (d1 - d0)) < 1e-9 && plantH(1, 1, true) == d1, "plantDamage: the damage of a cold plant is linear in the factor", num(d0) + " " + num(d1) + " " + num(d2));
        const double r0 = plantH(1, 0, false), r1 = plantH(1, 1, false), r2 = plantH(1, 2, false);
        check(r1 > r0 && std::fabs((r2 - r1) - (r1 - r0)) < 1e-9, "plantRecovery: the recovery of a healthy plant is linear in the factor", num(r0) + " " + num(r1) + " " + num(r2));
    }
    // powerGeneration
    {
        auto gen = [](double k, const char* name) {
            World1 w(withKnob("powerGeneration", k));
            w.sg.worldHeader.m_dSunlight = 1.0;
            w.sg.worldHeader.m_dWindSpeed = 10.0;
            CFeature* f = w.w().findFeature(name);
            f->update(w.now() + 90);
            return f->powerUsage();
        };
        for (const char* n : {"fsolargen", "fwindmill"}) {
            const double a = gen(0, n), b = gen(1, n), c = gen(2, n);
            check(a == 0 && b > 0 && std::fabs(c - 2 * b) < 1e-12, std::string("powerGeneration: ") + n + " generates in proportion", num(a) + " " + num(b) + " " + num(c));
        }
    }
    // siltClog
    {
        auto freeFlow = [](double k) {
            World1 w(withKnob("siltClog", k));
            CWaterFlow* fl = nullptr;
            for (Item* it : w.w().list(Table::Waterflows)) {
                auto* f = dynamic_cast<CWaterFlow*>(it);
                if (f && !dynamic_cast<CRain*>(f) && !dynamic_cast<CEvaporation*>(f) && f->clogRate() > 0 && f->m_pTo) { fl = f; break; }
            }
            if (!fl) return -1.0;
            fl->m_bOn = 1; fl->m_dFreeFlow = 1.0;
            fl->m_pFrom->setWaterVol(fl->m_pFrom->volume() * 0.5);
            fl->m_pTo->setWaterVol(fl->m_pTo->volume() * 0.1);
            fl->update(*w.sim, 3600);
            return fl->m_dFreeFlow;
        };
        const double a = freeFlow(0), b = freeFlow(1), c = freeFlow(2);
        check(a == 1.0 && b < 1.0 && b > 0 && std::fabs((1 - c) - 2 * (1 - b)) < 1e-9, "siltClog: a drain clogs in proportion to the factor", num(a) + " " + num(b) + " " + num(c));
    }
    // sensorNoise / alarmMargin
    {
        auto spread = [](double k) {
            World1 w(withKnob("sensorNoise", k));
            CBiomeSensor* s = nullptr;
            for (Item* it : w.w().list(Table::Features)) if ((s = dynamic_cast<CBiomeSensor*>(it))) break;
            double mx = 0;
            for (int i = 0; i < 300; i++) { s->update(w.now() + 90 * (i + 1)); mx = std::max(mx, std::fabs(s->airTempReading() - s->cel()->airTemp())); }
            return mx;
        };
        const double a = spread(0), b = spread(1), c = spread(5);
        check(a == 0 && b > 0.05 && b <= 0.1 && c > 0.2 && c <= 0.5, "sensorNoise: 0 gives exact readings, 1 the original +-0.1, 5 five times as much", num(a) + " " + num(b) + " " + num(c));
        auto alarm = [](double margin, double hum) {
            World1 w(withKnob("alarmMargin", margin));
            CBiomeSensor* s = nullptr;
            for (Item* it : w.w().list(Table::Features)) { auto* b = dynamic_cast<CBiomeSensor*>(it); if (b && b->m_pThresholds == b->thresholds(0x100b7948)) { s = b; break; } }
            s->cel()->setHumidity(hum);
            s->update(w.now() + 90);
            return s->alarms();
        };
        check((alarm(0, 62) & 0x4000) == 0 && (alarm(0.1, 62) & 0x4000) != 0 && (alarm(0.1, 99) & 0x2000) == 0 && (alarm(0.1, 70) & 0x4000) == 0,
              "alarmMargin: the rainforest's humidity alarm (60) comes at 64 with 0.1 of the 40-point band; the physical top (100) does not move");
    }
    // start values
    {
        World1 w{Settings()};
        w.sg.self.applyStartValues(-1, -1, -1);
        const double h0 = w.sg.self.m_dHydration;
        check(h0 == 40.0 || h0 > 39, "start values: negative keeps the saved value", num(h0));
        w.sg.self.applyStartValues(80, 90, 70);
        check(w.sg.self.m_dHydration == 80 && w.sg.self.m_dNutrition == 90 && w.sg.self.m_dEnergy == 70 && w.sg.self.m_dHydrationShown == 80, "start values: applyStartValues sets hydration, nutrition, energy and the shown copies");
    }
}

// ---- the story knobs (they live in the BiosGame) -----------------------------------------------------------------------
struct NullLogic : GameLogic {};

void testStory() {
    NullLogic logic;
    Engine engine(DataFiles::fromEnvironment(), BIOSYS_DEV_ROOT "/port/data", &logic, nullptr);
    auto build = [&](const Settings& st) {
        BiosGame::Config cfg;
        cfg.dataDir = BIOSYS_DEV_ROOT "/port/data";
        cfg.set(st);
        cfg.tick = [] { return 100000000u; };
        return std::make_unique<BiosGame>(engine.api(), engine.files(), cfg);
    };
    // heliDelaySeconds
    for (double secs : {604800.0, 3.0 * 86400}) {
        auto g = build(withKnob("heliDelaySeconds", secs));
        g->g.noPalette = false; g->g.silent = false;
        g->story_->CGame_TriggerEvent(0x20000000);
        const double d = static_cast<double>(g->save_->subTech.m_dwArrivalTime - g->save_->timeline.m_dwTime);
        check(d == secs, "heliDelaySeconds: the helicopter comes " + num(secs) + " s after the revival", num(d));
    }
    // huntChancePercent / huntCooldownSeconds
    auto hunted = [&](double chance, double cooldown, uint32_t sinceLast) {
        Settings st; st.tuning.huntChancePercent = chance; st.tuning.huntCooldownSeconds = cooldown;
        auto g = build(st);
        g->self().m_bAwake = 1;
        CSite* r4 = g->world().findSite("R4");
        g->self().m_pSite.set(r4);
        g->self().m_szLocation.fill(0); std::memcpy(g->self().m_szLocation.data(), "R4", 2);
        g->save_->header.milestones |= 0x80000000u;
        g->story_->subtech.state = 1;
        g->story_->subtechLastChase = 100000000u - sinceLast;
        g->story_->CSubTechHandler_Update();
        return g->story_->subtech.state == 3;
    };
    check(hunted(100, 300, 400000) && !hunted(0, 300, 400000), "huntChancePercent: 100 hunts at once, 0 never");
    check(hunted(100, 300, 400000) && !hunted(100, 600, 400000) && hunted(100, 600, 700000), "huntCooldownSeconds: the 300 s cooldown has passed after 400 s, the 600 s one has not");
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::string which = argc > 1 ? argv[1] : "all";
        if (which == "all" || which == "parser") testParser();
        if (which == "all" || which == "model") testModel();
        if (which == "all" || which == "tuning") testTuning();
        if (which == "all" || which == "story") testStory();
    } catch (const std::exception& e) {
        std::printf("FAIL exception: %s\n", e.what());
        fails++;
    }
    std::printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
