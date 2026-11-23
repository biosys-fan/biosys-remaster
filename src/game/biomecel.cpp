// The x87 original evaluates every expression in one pass; the port keeps each expression's order and rounds every
// operation to double, with no fused multiply-add (the oracle in tests/sim_oracle.py agrees to the last bit).
#pragma clang fp contract(off)
#include "game/biomecel.h"
#include "game/sim.h"
#include "game/plant.h"
#include "game/watertank.h"
#include "game/world_ctors.gen.h"
#include <algorithm>
#include <cstdio>
#include <limits>

namespace biosys::game {

// dll:10017b90
CBiomeCel::CBiomeCel(const char* name, int32_t flags, Biome* biome, double airCapacity, double airTemp,
                     double soilTemp, double humidity, double oxygen, double co2, double light,
                     double lightLetThru, double soilpH, double soilWater, double soilNutrients, double pollution,
                     Item* soilTank, Item* condensateTank, uint16_t id)
    : Item(0x10017b90, "CBiomeCel", name),
      m_pBiome(biome), m_pSoilTank(soilTank), m_pCondensateTank(condensateTank), m_wId(id),
      m_dAirCapacity(airCapacity), m_dAirVolume(airCapacity),
      m_dAirTemp(airTemp), m_dSoilTemp(soilTemp), m_dHumidity(humidity), m_dOxygen(oxygen), m_dCO2(co2),
      m_dLight(light), m_dSoilpH(soilpH), m_dSoilWater(soilWater), m_dwTimeLastRain(-1), m_dWindSpeed(0.0),
      m_dLightLetThru(lightLetThru), m_dSoilNutrients(soilNutrients), m_dPollution(pollution),
      m_dNewAirTemp(airTemp), m_dNewSoilTemp(soilTemp), m_dNewHumidity(humidity), m_dNewOxygen(oxygen),
      m_dNewCO2(co2), m_dNewPollution(pollution),
      m_dPlantHealthAvg(0.0),
      m_dPlantHealthMin(std::numeric_limits<double>::infinity()),   // DAT_100beb40 = 0x7ff0000000000000
      m_dPlantHealthMax(-std::numeric_limits<double>::infinity()),
      m_dwFlags(static_cast<uint32_t>(flags)) {
    if (std::strlen(name) > 11) throw Error(std::string("CBiomeCel name too long: ") + name);   // char[12] at +4
}

// dll:10017da0
void CBiomeCel::saveSlot(SlotWriter& w) const {
    w.writeF64(m_dAirCapacity);
    w.writeF64(m_dAirVolume);
    w.writeF64(m_dAirTemp);
    w.writeF64(m_dCO2);
    w.writeF64(m_dHumidity);
    w.writeF64(m_dLight);
    w.writeF64(m_dLightLetThru);
    w.writeF64(m_dOxygen);
    w.writeF64(m_dPollution);
    w.writeF64(m_dSoilNutrients);
    w.writeF64(m_dSoilpH);
    w.writeF64(m_dSoilTemp);
    w.writeF64(m_dSoilWater);
    w.writeI32(m_dwTimeLastRain);
    w.writeF64(m_dWindSpeed);
    w.writeF64(m_dPlantHealthAvg);
    w.writeF64(m_dPlantHealthMin);
    w.writeF64(m_dPlantHealthMax);
    w.writeU32(m_dwFlags);
}

// dll:10017f40: the "new" copies follow the loaded current values
void CBiomeCel::loadSlot(SlotReader& r) {
    m_dAirCapacity = r.readF64("m_dAirCapacity");
    m_dAirVolume = r.readF64("m_dAirVolume");
    m_dAirTemp = m_dNewAirTemp = r.readF64("m_dAirTemp");
    m_dCO2 = m_dNewCO2 = r.readF64("m_dCO2");
    m_dHumidity = m_dNewHumidity = r.readF64("m_dHumidity");
    m_dLight = r.readF64("m_dLight");
    m_dLightLetThru = r.readF64("m_dLightLetThru");
    m_dOxygen = m_dNewOxygen = r.readF64("m_dOxygen");
    m_dPollution = m_dNewPollution = r.readF64("m_dPollution");
    m_dSoilNutrients = r.readF64("m_dSoilNutrients");
    m_dSoilpH = r.readF64("m_dSoilpH");
    m_dSoilTemp = m_dNewSoilTemp = r.readF64("m_dSoilTemp");
    m_dSoilWater = r.readF64("m_dSoilWater");
    m_dwTimeLastRain = r.readI32("m_dwTimeLastRain");
    m_dWindSpeed = r.readF64("m_dWindSpeed");
    m_dPlantHealthAvg = r.readF64("m_dPlantHealthAvg");
    m_dPlantHealthMin = r.readF64("m_dPlantHealthMin");
    m_dPlantHealthMax = r.readF64("m_dPlantHealthMax");
    m_dwFlags = r.readHex32("m_dwFlags");
    r.finishPad();
}

namespace {
// Model::houseEnvelopeSlow: 1 / tau of the house cel (tau 86400 s; the original's 1 / 12000)
constexpr double kHouseEnvelopeRate = 1.0 / 86400.0;
}

// dll:10018530. `dt` is the argument of the original (game seconds since the last update, unsigned).
void CBiomeCel::updateState(Sim& sim, uint32_t dtU) {
    const double dt = static_cast<double>(dtU);
    const BiomeParams& bp = sim.biomeParams(m_pBiome);
    // greenhouse / envelope heat exchange: the cel relaxes towards (letThru * gain * sunlight + outside temperature)
    // Model::houseEnvelopeSlow (study A11): the house (a sealed building, gain 0) has the time constant of a glasshouse, 12000 s,
    // so its air follows the outside temperature within hours (frozen run: 22 -> 6 C in a day). A house holds heat for about a
    // day: 86400 s. Only the house; the greenhouses and the basements keep theirs. Calibration: SETTINGS.md
    const double envelopeRate = (sim.model().houseEnvelopeSlow && m_pBiome->addr == 0x100c1208u) ? kHouseEnvelopeRate : 8.333333333333333e-05;
    m_dNewAirTemp = (((m_dPollution * -0.01 + 1.0) * bp.gain * sim.sunlight() + sim.extTemp()) - m_dAirTemp) * dt *
                        envelopeRate + m_dNewAirTemp;
    if (33.0 <= m_dNewAirTemp) m_dwFlags |= 8;
    // air <-> soil heat exchange; the soil has three times the heat capacity
    double q = (m_dAirTemp - m_dSoilTemp) * dt * 0.0002777777777777778;
    m_dNewSoilTemp = q * 0.3333333333333333 + m_dNewSoilTemp;
    m_dNewAirTemp = m_dNewAirTemp - q;
    double letThru = 1.0 - m_dPollution * 0.01;
    m_dLightLetThru = letThru;
    m_dLight = letThru * sim.sunlight();
    if (m_pSoilTank) {
        const CWaterTank& tank = as<CWaterTank>(m_pSoilTank);
        uint32_t f = m_dwFlags;
        m_dSoilWater = (46.5 / tank.volume()) * tank.waterVol() + 1.0;
        if ((f & 4) != 0) {                    // savannah cel: "green" after a hot day with enough soil water
            bool dry = m_dSoilWater < 20.0;
            if ((f & 0x10) == 0) {
                if (!dry && (f & 8) != 0) m_dwFlags = f | 0x10;
            } else if (dry) {
                m_dwFlags = f & 0xffffffefu;
            }
        }
        if ((m_dwFlags & 2) != 0) m_dwTimeLastRain = sim.time();   // raining now
    }
    if (m_pCondensateTank) {
        // Fixes::condensationFlagPerCel (intent unclear, see FIXES.md): the on/off state is the cel's own
        bool& flowsOn = sim.fixes().condensationFlagPerCel ? m_bCondensationFlowsOn : sim.condensationFlowsOn;
        if (m_dHumidity <= 0.0 || m_dAirTemp <= sim.biomeParams(m_pBiome).target) {
            if (flowsOn) {
                sim.switchWaterFlows(m_wId, 0);    // the id is 0 in every cel, so this does nothing
                flowsOn = false;
            }
        } else {
            if (!flowsOn) {
                sim.switchWaterFlows(m_wId, 1);
                flowsOn = true;    // ORIGINAL BUG: one flag for all cels (and a flow id that is always 0)
            }
            const double before = m_dNewHumidity;
            m_dNewHumidity = m_dHumidity * 0.01 * dt * -0.002777777777777778 + m_dNewHumidity;
            clampD(m_dNewHumidity, 0.0, 100.0);
            if (sim.model().vapourFromSoil) {
                // Model::vapourFromSoil (vapour model v2): the condensate tank gets the vapour the condensation removed (the original
                // CEvaporation moved the same amount from the soil instead); one %RH = 4.1667e-6 volumes. The soil gave this water to
                // the air in CEvaporation::update.
                double removed = before - m_dNewHumidity;
                const double sink = m_dHumidity * 0.01 * dt * 0.002777777777777778;
                if (removed > sink) removed = sink;
                if (removed > 0.0) {
                    sim.statCondensed += removed;
                    CWaterTank& cond = as<CWaterTank>(m_pCondensateTank);
                    const CWaterTank& soil = as<CWaterTank>(m_pSoilTank);
                    const double amt = removed * 4.1666666666666667e-06, vCond = cond.waterVol() + amt;
                    cond.setNewOxygen((soil.oxygen() * amt + cond.oxygen() * cond.waterVol()) / vCond);
                    cond.setNewPH((soil.ph() * amt + cond.waterVol() * cond.ph()) / vCond);
                    cond.setNewTemp((soil.temp() * amt + cond.temp() * cond.waterVol()) / vCond);
                    cond.setNewPurity((0.7 * amt + cond.purity() * cond.waterVol()) / vCond);     // fresh water: CEvaporation's purity 0.7
                    cond.setWaterVol(vCond);
                }
            }
        }
    }
    if (!m_pCondensateTank && sim.model().vapourFromSoil && m_pBiome && m_pBiome->addr == 0x100c1280u) {
        // Model::vapourFromSoil, the ocean cel (vapour model v2): it has no soil and no condensate tank, so its one plant would
        // humidify the air with nothing to take it out again (the original: constant 70 %). Water surface: the evaporation of the
        // soil cels with wet = 1, dH = kE (1 - H/100) per hour, from the ocean tank; and the original condensation sink (0.1 H per
        // hour while the air is above the biome's target), the condensate going back into the ocean.
        COcean& oc = sim.ocean();
        const double kVol = 4.1666666666666667e-06;
        double ev = sim.model().evapRate * (1.0 - m_dHumidity * 0.01) * (dt / 3600.0);
        if (ev < 0.0) ev = 0.0;
        if (m_dNewHumidity + ev > 100.0) ev = std::max(0.0, 100.0 - m_dNewHumidity);
        if (ev * kVol > oc.waterVol()) ev = oc.waterVol() / kVol;
        m_dNewHumidity += ev;
        double vol = -ev * kVol;
        if (m_dHumidity > 0.0 && m_dAirTemp > sim.biomeParams(m_pBiome).target) {
            double sink = m_dHumidity * 0.01 * dt * 0.002777777777777778;
            if (sink > m_dNewHumidity) sink = m_dNewHumidity;
            m_dNewHumidity -= sink;
            vol += sink * kVol;
        }
        oc.setWaterVol(oc.waterVol() + vol);
    }
    if ((m_dNewAirTemp < -20.0 || 60.0 < m_dNewAirTemp) && !sim.airTempReported) {
        char b[96];
        std::snprintf(b, sizeof b, "CBiomeCel::UpdateState  m_dAirTemp out of range: %f", m_dAirTemp);
        sim.log(b);
        // PORT: the original now calls CBiomeCel::Dump, CTimeline::Dump, FUN_100131d0 and FUN_10013340, which only
        // write more debugging text to the log
        sim.airTempReported = true;
    }
    clampD(m_dNewAirTemp, -20.0, 50.0);
    m_dAirTemp = m_dNewAirTemp;
    clampD(m_dNewSoilTemp, -20.0, 50.0);
    m_dSoilTemp = m_dNewSoilTemp;
    clampD(m_dNewOxygen, 0.0, 100.0);
    m_dOxygen = m_dNewOxygen;
    clampD(m_dNewCO2, 0.0, 100.0);
    m_dCO2 = m_dNewCO2;
    clampD(m_dNewHumidity, 0.0, 100.0);
    m_dHumidity = m_dNewHumidity;
    clampD(m_dNewPollution, 0.0, 100.0);
    m_dPollution = m_dNewPollution;
}

// dll:10018860: mean health of the cel's plants (without the inert ones), its extremes and its trend
void CBiomeCel::updatePlantHealthAvg() {
    if (m_plants.empty()) return;                        // the plant list's count (+0xdc) is 0
    const double prevAvg = m_dPlantHealthAvg;
    int n = 0;
    m_dPlantHealthAvg = 0.0;
    for (Item* it : m_plants) {
        const CPlant& p = as<CPlant>(it);
        if ((p.flags() & 8) == 0) {
            n++;
            m_dPlantHealthAvg = p.health() + m_dPlantHealthAvg;
        }
    }
    if (0 < n) m_dPlantHealthAvg = m_dPlantHealthAvg / static_cast<double>(n);
    if (m_dPlantHealthMax < m_dPlantHealthAvg) m_dPlantHealthMax = m_dPlantHealthAvg;
    if (m_dPlantHealthAvg < m_dPlantHealthMin) m_dPlantHealthMin = m_dPlantHealthAvg;
    if (prevAvg < m_dPlantHealthAvg) {
        m_dwFlags = (m_dwFlags & 0xffffffbfu) | 0x20;    // rising
        return;
    }
    if (m_dPlantHealthAvg < prevAvg) m_dwFlags = (m_dwFlags & 0xffffffdfu) | 0x40;   // falling
}

// dll:10018390 / 100183f0 / 10018430 / 10018470 / 100184b0: bring the clock and the air flows up to date, then store
void CBiomeCel::setAirTempNow(Sim& sim, double v) {
    if (sim.hooks.realTimeTick) sim.hooks.realTimeTick();
    sim.updateAirFlows();
    m_dAirTemp = v;
    m_dNewAirTemp = v;
}
void CBiomeCel::setOxygenNow(Sim& sim, double v) {
    if (sim.hooks.realTimeTick) sim.hooks.realTimeTick();
    sim.updateAirFlows();
    m_dOxygen = v;
    m_dNewOxygen = v;
}
void CBiomeCel::setCO2Now(Sim& sim, double v) {
    if (sim.hooks.realTimeTick) sim.hooks.realTimeTick();
    sim.updateAirFlows();
    m_dCO2 = v;
    m_dNewCO2 = v;
}
void CBiomeCel::setHumidityNow(Sim& sim, double v) {
    if (sim.hooks.realTimeTick) sim.hooks.realTimeTick();
    sim.updateAirFlows();
    m_dHumidity = v;
    m_dNewHumidity = v;
}
void CBiomeCel::setPollutionNow(Sim& sim, double v) {
    if (sim.hooks.realTimeTick) sim.hooks.realTimeTick();
    sim.updateAirFlows();
    m_dPollution = v;
    m_dNewPollution = v;
}

// dll:10013c10 (the loop body): current values take the new ones, unclamped
void CBiomeCel::commit() {
    m_dAirTemp = m_dNewAirTemp;
    m_dSoilTemp = m_dNewSoilTemp;
    m_dHumidity = m_dNewHumidity;
    m_dOxygen = m_dNewOxygen;
    m_dCO2 = m_dNewCO2;
    m_dPollution = m_dNewPollution;
}

// a5 (an int, always 1 in the builder) is dropped: the original constructor never reads it
BIOSYS_REGISTER_CTOR(10017b90, [](World& w, const char* name, int32_t a1, Biome* a2, double a3, int32_t, double a6,
                                  double a8, double a10, double a12, double a14, double a16, double a18, double a20,
                                  double a22, double a24, double a26, Item* a28, Item* a29, uint16_t a30) -> Item* {
    return w.adopt(std::make_unique<CBiomeCel>(name, a1, a2, a3, a6, a8, a10, a12, a14, a16, a18, a20, a22, a24, a26,
                                               a28, a29, a30));
});

} // namespace biosys::game
