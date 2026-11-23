// Operation order and rounding follow the decompiled expressions; no fused multiply-add (see biomecel.cpp).
#pragma clang fp contract(off)
#include "game/plant.h"
#include "game/biomecel.h"
#include "game/sim.h"
#include "game/watertank.h"
#include "game/world_ctors.gen.h"
#include <cmath>
#include <cstring>

namespace biosys::game {

namespace {
std::string copyChars(const char* s, size_t cap, const char* what) {
    std::string r(s ? s : "");
    if (r.size() >= cap) throw Error(std::string(what) + " too long: " + r);   // the original copies unchecked
    return r;
}
}

// dll:10072210
CPlant::CPlant(World&, const char* name, const char* type, int32_t speciesId, int32_t flags, CSite* site,
               double maxPSRate, double tempOptimum, double tempMinNight, double tempMax, double soilWaterMin,
               double soilWaterMax, double hardiness, double tempMinDay)
    : Item(0x10072210, "CPlant", name),
      m_szType(copyChars(type, 4, "plant type")), m_dwSpeciesId(speciesId), m_dwFlags(static_cast<uint32_t>(flags)),
      m_dwFruitCount(0), m_wFruitMask(0), m_szImage("noimage"), m_pSite(site), m_pCel(nullptr),
      m_szName(copyChars(name, 12, "plant name")),
      m_dwTimeLastUpdate(0x7080), m_dwTimeLastPicked(0), m_dwNumPicked(0),
      m_dShortTermHealth(1.0), m_dLongTermHealth(1.0), m_dHealth(1.0), m_dHealthPrev(1.0), m_dwHealthState(1),
      m_dSoilWaterMin(soilWaterMin), m_dSoilWaterMax(soilWaterMax),
      m_dStomatalAperture(1.0), m_dWaterAvailability(1.0), m_dTranspirationRate(1.0), m_dLeafTemp(25.0),
      m_dMaxPSRate(maxPSRate), m_dPSRate(0.5), m_dRespirationRate(maxPSRate * 0.25),
      m_pLightTable(0), m_pCO2Table(0), m_pTempTable(0x100be058),
      m_dTempOptimum(tempOptimum), m_dTempMinNight(tempMinNight),
      m_dTempMinDay(tempMinDay == -999.0 ? tempMinNight : tempMinDay), m_dTempMax(tempMax),
      m_dHardiness(hardiness), m_dwUnk110(0) {
    if (!site) throw Error("CPlant " + std::string(name) + ": site pointer is NULL");
    m_pCel = site->cel();                           // site +8
    if (m_pCel) m_pCel->addPlant(this);             // CBiomeCel::AddPlant dll:10018370
    if ((m_dwFlags & 5) == 0) {
        if ((m_dwFlags & 2) == 0) {
            if ((m_dwFlags & 8) == 0)
                // the original only logs "plant %s is an invalid type of plant" and leaves +0xd8/+0xdc unset
                throw Error("plant " + std::string(name) + " is an invalid type of plant");
            m_pLightTable = 0;
            m_pCO2Table = 0;
        } else {
            m_pLightTable = 0x100bdc18;   // C4
            m_pCO2Table = 0x100bded8;
        }
    } else {
        m_pLightTable = 0x100bdad8;       // C3 / CAM
        m_pCO2Table = 0x100bdd58;
    }
}

// dll:100733c0
void CPlant::update(Sim& sim, i32 now) {
    const uint32_t dtU = static_cast<uint32_t>(now) - static_cast<uint32_t>(m_dwTimeLastUpdate);
    m_dwTimeLastUpdate = now;
    if (dtU == 0) return;
    if ((m_dwFlags & 8) != 0) return;                   // inert plant
    m_dHealthPrev = m_dHealth;
    if ((m_dwHealthState & 4) != 0 && m_dwFruitCount != 0) sim.removePlantFruit(*this);
    CBiomeCel& cel = *m_pCel;
    const double dt = static_cast<double>(dtU);
    double dShort = 0.0, dLong = 0.0;                   // damage of this step to the short and long-term health
    m_dwHealthState = 0;
    m_dStomatalAperture = 1.0;
    // --- water
    if (cel.soilWater() <= 20.0) m_dWaterAvailability = cel.soilWater() * 0.05;
    else m_dWaterAvailability = 1.0;
    if (m_dSoilWaterMin <= cel.soilWater()) {
        if (m_dSoilWaterMax < cel.soilWater()) {
            // ORIGINAL BUG (probably): the flooded case scales with the plant's minimum (+0x90) where its maximum
            // (+0x98) looks intended; the value is 1 - (sw - 47.5) / (min - 47.5)
            m_dwHealthState = 0x10080;
            // Fixes::plantFloodedUsesMax (intent unclear, FIXES.md): the maximum, keeping the shape of the formula
            const double lim = sim.fixes().plantFloodedUsesMax ? m_dSoilWaterMax : m_dSoilWaterMin;
            if (sim.model().floodedPlantFormula) {
                // Model::floodedPlantFormula (study A12, reviewer's form; replaces both formulas above, whatever plantFloodedUsesMax
                // says): the mirror of the dry rule (sw - 1) / (swMin - 1) with the code's own scale ends (1 = empty .. 47.5 = full
                // tank): (47.5 - sw) / (47.5 - swMax), 1 at the plant's maximum (continuous there, as the dry rule is at the
                // minimum), 0 at a full tank; plants whose maximum is 47.5 (rainforest) get 0 for any flood. The original's value
                // is 1 + (sw - 47.5) / (47.5 - min) > 1. The flat flood damage (dShort / dLong) is unchanged.
                double a = 0.0;
                if (m_dSoilWaterMax < 47.5) a = (47.5 - cel.soilWater()) / (47.5 - m_dSoilWaterMax);
                a = a < 0.0 ? 0.0 : (a > 1.0 ? 1.0 : a);
                m_dWaterAvailability = a * m_dWaterAvailability;
            } else {
                m_dWaterAvailability = (1.0 - (cel.soilWater() - 47.5) / (lim - 47.5)) * m_dWaterAvailability;
            }
            dShort = dt * 4.050925925925926e-06;
            dLong = dt * 1.6534391534391535e-06;
        }
    } else {
        m_dwHealthState = 0x10040;
        m_dWaterAvailability = ((cel.soilWater() - 1.0) / (m_dSoilWaterMin - 1.0)) * m_dWaterAvailability;
        dShort = dt * 4.050925925925926e-06;
        dLong = dt * 1.6534391534391535e-06;
    }
    // --- transpiration (limited by the water the roots deliver; the stomata close to match)
    const double d = (cel.light() + 1.0) * (cel.humidity() * -0.01 + 1.0) * cel.airTemp() * 0.03333333333333333;
    m_dTranspirationRate = (cel.windSpeed() * 0.3 + 1.0) * d;
    if (m_dWaterAvailability < m_dTranspirationRate) {
        m_dTranspirationRate = m_dWaterAvailability;
        m_dStomatalAperture = m_dTranspirationRate / ((cel.windSpeed() + 1.0) * d);
    }
    // ORIGINAL BUG (study A7): the coefficient is 2.3e-9 %RH per second per unit of transpiration: three plants give 1e-5 %RH
    // per hour against a condensation sink of 8 %RH per hour, the link from the leaves to the air is dead.
    // Model::transpirationHumidifies (vapour model v2): Model::transpRate (calibrated, SETTINGS.md); the water comes from the soil
    // tank (one %RH = 4.1667e-6 volumes), the rate is already deficit-limited by the factor 1 - H/100 of the transpiration
    if (sim.model().transpirationHumidifies) {
        // a dead plant does not transpire: the contribution is proportional to its health (of the previous step)
        double dH = dt * m_dTranspirationRate * sim.model().transpRate * m_dHealth;
        if (cel.soilTank()) {
            CWaterTank& soil = as<CWaterTank>(cel.soilTank());
            double vol = dH * 4.1666666666666667e-06;
            if (vol > soil.waterVol()) { vol = soil.waterVol(); dH = vol / 4.1666666666666667e-06; }
            soil.setWaterVol(soil.waterVol() - vol);
        }
        cel.setNewHumidity(dH + cel.newHumidity());
        sim.vapourIn[cel.biome()].second += dH;
    } else {
        cel.setNewHumidity(dt * m_dTranspirationRate * 2.3148148148148148e-09 + cel.newHumidity());
    }
    m_dLeafTemp = (cel.airTemp() - m_dTranspirationRate) + cel.light();
    // --- temperature damage (the minimum differs by day; the damage below the minimum always uses the night value)
    const double tMin = sim.diurnal() == 0x14 ? m_dTempMinDay : m_dTempMinNight;
    bool hurt = false;
    double x = 0.0;
    if (tMin <= m_dLeafTemp) {
        if (m_dTempMax < m_dLeafTemp) {
            x = m_dLeafTemp - m_dTempMax;
            m_dwHealthState |= 0x40200;
            hurt = true;
        }
    } else {
        // ORIGINAL BUG: the cold damage is measured from the night minimum even when the day minimum (+0xf8) made it
        // cold; a plant with a higher day minimum is hurt less than its own limit says
        // fix: the minimum that made it cold (tMin, the day one by day) is the limit the damage is measured from
        x = (sim.fixes().plantColdDayMin ? tMin : m_dTempMinNight) - m_dLeafTemp;
        m_dwHealthState |= 0x400;
        hurt = true;
    }
    if (hurt) {
        dShort = x * dt * 8.101851851851852e-06 + dShort;
        dLong = x * dt * 5.787037037037037e-06 + dLong;
    }
    // --- photosynthesis: the smallest of the light, CO2 and temperature factors
    m_dPSRate = sim.lookUpVal(m_pLightTable, cel.light(), 0x13);
    m_dwHealthState |= 8;
    double f = sim.lookUpVal(m_pCO2Table, cel.co2(), 0x17);
    if (f < m_dPSRate) {
        m_dwHealthState = (m_dwHealthState & 0xfffffff7u) | 0x20;
        m_dPSRate = f;
    }
    f = sim.lookUpVal(m_pTempTable, std::fabs(m_dLeafTemp - m_dTempOptimum), 0x13);
    if (f < m_dPSRate) {
        m_dwHealthState = (m_dwHealthState & 0xffffffd7u) | 0x10;
        m_dPSRate = f;
    }
    double ps = (cel.windSpeed() * 0.3 + 1.0) * m_dMaxPSRate * m_dPSRate;
    if ((m_dwFlags & 4) == 0) {                         // not CAM: the stomata limit the uptake
        m_dPSRate = ps;
        ps = ps * m_dStomatalAperture;
    }
    m_dPSRate = ps;
    // --- pollution
    if (10.0 < cel.pollution()) {
        m_dwHealthState |= 0x1000;
        double p = cel.pollution() * 0.01 * dt * 3.858024691358025e-06;
        dShort = dShort + p;
        dLong = p + dLong;
    }
    // --- health
    const Tuning& tu = sim.tuning();
    dShort = dShort * tu.plantDamage;
    dLong = dLong * tu.plantDamage;
    dShort = dShort / m_dHardiness;
    if (dShort <= 0.0) {
        m_dShortTermHealth = dt * 0.0002777777777777778 * tu.plantRecovery + m_dShortTermHealth;
        m_dLongTermHealth = dt * 3.472222222222222e-06 * tu.plantRecovery + m_dLongTermHealth;
    } else {
        m_dShortTermHealth = m_dShortTermHealth - dShort;
        m_dLongTermHealth = m_dLongTermHealth - dLong / m_dHardiness;
    }
    clampD(m_dShortTermHealth, 0.3, 1.0);
    clampD(m_dLongTermHealth, 0.0, 1.0);
    m_dHealth = m_dLongTermHealth <= m_dShortTermHealth ? m_dLongTermHealth : m_dShortTermHealth;
    m_dPSRate = m_dPSRate * m_dHealth;
    if (0.2 < m_dLongTermHealth) {
        if (0.5 <= m_dLongTermHealth) {
            m_dwHealthState |= (0.5 <= m_dShortTermHealth) ? 1u : 0x800u;
        } else {
            m_dwHealthState |= 2;
            if (m_dwFruitCount != 0) sim.removePlantFruit(*this);
        }
    } else {
        m_dwHealthState |= 4;
        if (m_dwFruitCount != 0) sim.removePlantFruit(*this);
    }
    // --- gas exchange with the cel (net respiration is damped between 10 and 20 percent CO2, absent above)
    // ORIGINAL BUG (study A4): the respiration is the constant 0.25 maxPS whatever the health, so a dead plant (PS 0) keeps
    // eating oxygen: a neglected biosphere loses 16 points of O2 in 30 days. Model::deadPlantsRespire: 10 % of it at health 0
    const double respiration = sim.model().deadPlantsRespire ? m_dRespirationRate * (0.1 + 0.9 * m_dHealth) : m_dRespirationRate;
    double net = (m_dPSRate - respiration) * dt;
    bool exchange = true;
    if (net < 0.0) {
        if (20.0 <= cel.co2()) exchange = false;
        else if (10.0 < cel.co2()) net = (20.0 - cel.co2()) * net * 0.1;
    }
    if (exchange) {
        cel.setNewOxygen(cel.newOxygen() + net);
        cel.setNewCO2(cel.newCO2() - net);
    }
    double o2 = cel.newOxygen();
    clampD(o2, 0.0, 100.0);
    cel.setNewOxygen(o2);
    cel.setOxygen(o2);
    double co2 = cel.newCO2();
    clampD(co2, 0.0, 100.0);
    cel.setNewCO2(co2);
    cel.setCO2(co2);
    // --- fruit regrowth (the timestamps are the timeline's, not `now`)
    if ((m_dwFlags & 0x40) != 0 && (cel.flags() & 2) != 0 && m_dwNumPicked != 0 &&
        600u < static_cast<uint32_t>(sim.time() - m_dwTimeLastPicked)) {
        m_dwNumPicked--;
        m_dwTimeLastPicked = sim.time();
        return;
    }
    if ((m_dwFlags & 0x10) != 0 && m_dwNumPicked != 0 && 0x3840u < static_cast<uint32_t>(sim.time() - m_dwTimeLastPicked)) {
        m_dwNumPicked--;
        m_dwTimeLastPicked = sim.time();
    }
}

// dll:100724f0
void CPlant::saveSlot(SlotWriter& w) const {
    w.writeF64(m_dLeafTemp);
    w.writeF64(m_dHealth);
    w.writeF64(m_dHealthPrev);
    w.writeF64(m_dLongTermHealth);
    w.writeF64(m_dPSRate);
    w.writeF64(m_dShortTermHealth);
    w.writeF64(m_dStomatalAperture);
    w.writeF64(m_dTranspirationRate);
    w.writeF64(m_dWaterAvailability);
    w.writeI32(m_dwTimeLastUpdate);
    w.writeI32(m_dwTimeLastPicked);
    w.writeI32(m_dwNumPicked);
    w.writeU32(m_dwHealthState);
    w.writeI32(m_dwFruitCount);
    w.writeU16(m_wFruitMask);
    w.writeI32(m_dwUnk110);
}

// dll:10072620
void CPlant::loadSlot(SlotReader& r) {
    m_dLeafTemp = r.readF64("m_dLeafTemp");
    m_dHealth = r.readF64("m_dHealth");
    m_dHealthPrev = r.readF64("m_dHealthPrev");
    m_dLongTermHealth = r.readF64("m_dLongTermHealth");
    m_dPSRate = r.readF64("m_dPSRate");
    m_dShortTermHealth = r.readF64("m_dShortTermHealth");
    m_dStomatalAperture = r.readF64("m_dStomatalAperture");
    m_dTranspirationRate = r.readF64("m_dTranspirationRate");
    m_dWaterAvailability = r.readF64("m_dWaterAvailability");
    m_dwTimeLastUpdate = r.readI32("m_dwTimeLastUpdate");
    m_dwTimeLastPicked = r.readI32("m_dwTimeLastPicked");
    m_dwNumPicked = r.readI32("m_dwNumPicked");
    m_dwHealthState = r.readHex32("m_dwHealthState");
    m_dwFruitCount = r.readI32("unk_10");
    m_wFruitMask = r.readU16("unk_14");
    m_dwUnk110 = r.readI32("unk_110");
    r.finishPad();
}

void CPlant::poke32(uint32_t off, uint32_t value) {
    double* d = nullptr;
    switch (off & ~7u) {
    case 0x68: d = &m_dShortTermHealth; break;
    case 0x70: d = &m_dLongTermHealth; break;
    case 0x78: d = &m_dHealth; break;
    case 0x80: d = &m_dHealthPrev; break;
    default: throw Error("CPlant::poke32: offset not supported: " + std::to_string(off));
    }
    uint32_t half[2];
    std::memcpy(half, d, 8);
    half[(off & 4) ? 1 : 0] = value;     // little endian: +0 low dword, +4 high dword
    std::memcpy(d, half, 8);
}

uint32_t CPlant::peek32(uint32_t off) const {
    const double* d = nullptr;
    switch (off & ~7u) {
    case 0x68: d = &m_dShortTermHealth; break;
    case 0x70: d = &m_dLongTermHealth; break;
    case 0x78: d = &m_dHealth; break;
    case 0x80: d = &m_dHealthPrev; break;
    default: throw Error("CPlant::peek32: offset not supported: " + std::to_string(off));
    }
    uint32_t half[2];
    std::memcpy(half, d, 8);
    return half[(off & 4) ? 1 : 0];
}

// dll:10009be0 (only the vtable and three stores)
CPlantPair::CPlantPair(CPlant* plantA, CPlant* plantB, uint32_t flags)
    : Item(0x10009be0, "CPlantPair", ""), m_pPlantA(plantA), m_pPlantB(plantB), m_dwFlags(flags) {}

void CPlantPair::saveSlot(SlotWriter&) const { throw Error("CPlantPair has no slot"); }
void CPlantPair::loadSlot(SlotReader&) { throw Error("CPlantPair has no slot"); }

BIOSYS_REGISTER_CTOR(10072210, [](World& w, const char* name, const char* a1, int32_t a2, int32_t a3, Item* a4,
                                  double a5, double a7, double a9, double a11, double a13, double a15, double a17,
                                  double a19) -> Item* {
    return w.adopt(std::make_unique<CPlant>(w, name, a1, a2, a3, &as<CSite>(a4), a5, a7, a9, a11, a13, a15, a17, a19));
});

BIOSYS_REGISTER_CTOR(10009be0, [](World& w, Item* a0, Item* a1, int32_t a2) -> Item* {
    return w.adopt(std::make_unique<CPlantPair>(&as<CPlant>(a0), &as<CPlant>(a1), static_cast<uint32_t>(a2)));
});

} // namespace biosys::game
