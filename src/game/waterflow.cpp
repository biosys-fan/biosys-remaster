// Operation order and rounding follow the decompiled expressions; no fused multiply-add (see biomecel.cpp).
#pragma clang fp contract(off)
#include "game/waterflow.h"
#include "game/sim.h"
#include "game/watertank.h"
#include "game/world_ctors.gen.h"
#include <algorithm>
#include <cmath>

namespace biosys::game {

CWaterFlow::CWaterFlow(u32 ctorAddr, const char* className, std::string name)
    : Item(ctorAddr, className, std::move(name)) {}

// dll:1005cce0
CWaterFlow::CWaterFlow(World& w, std::string name, const char* from, const char* to, uint16_t id, double mag,
                       int32_t on, double clogRate, double purityOverride, int32_t proportional)
    : Item(0x1005cce0u, "CWaterFlow", std::move(name)) {
    m_pFrom = &w.requireWaterTank(from);           // original: log "unrecognised water tank", delete this
    m_pTo = to ? &w.requireWaterTank(to) : nullptr;
    m_wId = id;
    m_bOn = on;
    m_dMag = mag;
    m_bReversed = 0;
    m_dClogRate = clogRate;
    m_dFreeFlow = 1.0;                             // +0x30 = 0, +0x34 = 0x3ff00000
    m_dPurityOverride = purityOverride;
    m_bProportional = proportional;
}

// dll:1005cec0
void CWaterFlow::saveSlot(SlotWriter& w) const {
    w.writeI32(m_bOn);
    w.writeI32(m_bReversed);
    w.writeF64(m_dFreeFlow);
    w.writeF64(m_dPurityOverride);
}

// dll:1005cf10
void CWaterFlow::loadSlot(SlotReader& r) {
    m_bOn = r.readI32("m_bOn");
    m_bReversed = r.readI32("m_bReversed");
    m_dFreeFlow = r.readF64("m_dFreeFlow");
    m_dPurityOverride = r.readF64("m_dPurityOverride");
    r.finishPad();
}

// dll:1006f990
CEvaporation::CEvaporation(World& w, std::string name, const char* from, const char* to, const char* celName,
                           uint16_t id, double mag)
    : CWaterFlow(0x1006f990u, "CEvaporation", std::move(name)) {
    m_pFrom = &w.requireWaterTank(from);
    m_pTo = &w.requireWaterTank(to);
    m_pCel = &w.requireBiomeCel(celName);          // original: log "m_pCel == NULL", delete this
    m_wId = id;
    m_bReversed = 0;
    m_dClogRate = 0;
    m_dMag = mag;
    m_bProportional = 0;
    m_bOn = 1;
    m_dPurityOverride = 0.7;                       // +0x38 = 0x66666666, +0x3c = 0x3fe66666
    // m_dFreeFlow (+0x30) is not set by this constructor: it stays 0 (the saved value is 0.0 as well)
}

// dll:10071d60
CRain::CRain(World& w, std::string name, const char* from, const char* to, const char* celName, uint16_t id,
             double mag, double humidityRate)
    : CWaterFlow(0x10071d60u, "CRain", std::move(name)) {
    m_pFrom = &w.requireWaterTank(from);
    m_pTo = &w.requireWaterTank(to);
    m_pCel = &w.requireBiomeCel(celName);
    m_bOn = 0;
    m_wId = id;
    m_bReversed = 0;
    m_dClogRate = 0;
    m_dPurityOverride = -1.0;                      // +0x38 = 0, +0x3c = 0xbff00000
    m_bProportional = 0;
    m_dMag = mag;
    m_dHumidityRate = humidityRate;
    // m_dFreeFlow (+0x30) is not set by this constructor: it stays 0
}

// dll:1005d080 (vtable +0x24): moves water from the source to the destination tank and mixes the "new" values
void CWaterFlow::update(Sim& sim, uint32_t dtU) {
    if (m_bOn == 0) return;
    CWaterTank* dst = m_bReversed == 0 ? m_pTo : m_pFrom;
    CWaterTank* src = m_bReversed == 0 ? m_pFrom : m_pTo;
    if (!dst || !src) throw Error("CWaterFlow " + name() + ": a tank is missing (crash in the original)");
    const double dt = static_cast<double>(dtU);
    double amt = dt * m_dMag * m_dFreeFlow * 0.0002777777777777778;
    if (m_bProportional != 0) amt = (src->waterVol() / src->volume()) * amt;
    if (!(amt < src->waterVol())) return;
    if (dst->bounded()) {
        if (dst->volume() <= dst->waterVol()) return;
        double room = dst->volume() - dst->waterVol();
        if (room < amt) amt = room;
    }
    if (amt != 0.0) {
        double v = dst->waterVol() + amt;
        dst->setNewOxygen((src->oxygen() * amt + dst->oxygen() * dst->waterVol()) / v);
        dst->setNewPH((src->ph() * amt + dst->ph() * dst->waterVol()) / v);
        dst->setNewSalinity((src->salinity() * amt + dst->salinity() * dst->waterVol()) / v);
        dst->setNewTemp((src->temp() * amt + dst->temp() * dst->waterVol()) / v);
        double purity = m_dPurityOverride < 0.0 ? src->purity() : m_dPurityOverride;
        dst->setNewPurity((purity * amt + dst->purity() * dst->waterVol()) / v);
        src->setWaterVol(src->waterVol() - amt);
        dst->setWaterVol(v);
        if (m_dClogRate != 0.0) {
            m_dFreeFlow = m_dFreeFlow - m_dClogRate * dt * sim.tuning().siltClog;   // Tuning::siltClog
            clampD(m_dFreeFlow, 0.0, 1.0);
        }
    }
}

// dll:1006fc50: the soil tank gives off water in proportion to the cel's humidity; it condenses (fresh, purity 0.7)
void CEvaporation::update(Sim& sim, uint32_t dtU) {
    if (m_bOn == 0) return;
    CWaterTank* src = m_pFrom;
    CWaterTank* cond = m_pTo;
    if (!src || !cond || !m_pCel) throw Error("CEvaporation " + name() + ": a tank or the cel is missing (crash in the original)");
    if (sim.model().vapourFromSoil) {
        // Model::vapourFromSoil (study A6, redesigned: vapour model v2, SETTINGS.md). The original moves the condensation amount
        // straight from the soil to the condensate tank and puts no vapour into the air. Here the soil tank gives its water to
        // the AIR: a deficit-limited source that runs all the time, dH = kE * wet^n * (1 - H/100) per hour (wet = fill fraction of
        // the soil tank, 0 empty .. 1 full), so it stops at 100 % and cannot run away. The condensate tank is filled by the
        // condensation itself (CBiomeCel::updateState, same flag). One %RH = 4.1667e-6 volumes (the mist calibration).
        const Model& mo = sim.model();
        const double kVol = 4.1666666666666667e-06;      // the mist calibration, the same constant everywhere (the flows' own mag differs per cel)
        double wet = src->volume() > 0.0 ? src->waterVol() / src->volume() : 0.0;
        wet = wet < 0.0 ? 0.0 : (wet > 1.0 ? 1.0 : wet);
        double dH = mo.evapRate * std::pow(wet, mo.evapExponent) * (1.0 - m_pCel->humidity() * 0.01) * (static_cast<double>(dtU) / 3600.0);
        if (dH < 0.0) dH = 0.0;
        double room = 100.0 - m_pCel->newHumidity();
        if (dH > room) dH = room > 0.0 ? room : 0.0;
        double vol = dH * kVol;
        if (vol > src->waterVol()) { vol = src->waterVol(); dH = vol / kVol; }
        if (vol > 0.0) {
            const double vSrc = src->waterVol() - vol;
            if (vSrc > 0.0) src->setNewSalinity((src->waterVol() * src->salinity()) / vSrc);     // the salt stays behind
            src->setWaterVol(vSrc);
            m_pCel->setNewHumidity(m_pCel->newHumidity() + dH);
            sim.vapourIn[m_pCel->biome()].first += dH;
        }
        return;
    }
    double amt = static_cast<double>(dtU) * m_pCel->humidity() * 0.01 * m_dMag * 0.0002777777777777778;
    if (amt <= src->waterVol()) {
        double vCond = cond->waterVol() + amt;
        double vSrc = src->waterVol() - amt;
        cond->setNewOxygen((src->oxygen() * amt + cond->oxygen() * cond->waterVol()) / vCond);
        cond->setNewPH((src->ph() * amt + cond->waterVol() * cond->ph()) / vCond);
        cond->setNewTemp((src->temp() * amt + cond->temp() * cond->waterVol()) / vCond);
        src->setNewSalinity((src->waterVol() * src->salinity()) / vSrc);     // the salt stays behind
        cond->setNewSalinity((cond->waterVol() * cond->salinity()) / vCond);
        cond->setNewPurity((m_dPurityOverride * amt + cond->purity() * cond->waterVol()) / vCond);
        src->setWaterVol(vSrc);
        cond->setWaterVol(vCond);
    }
}

// dll:10071fc0: a rain machine / mister moves clean water onto the soil and wets the air of the cel
void CRain::update(Sim& sim, uint32_t dtU) {
    if (m_bOn == 0) return;
    CWaterTank* src = m_pFrom;
    CWaterTank* soil = m_pTo;
    CBiomeCel* cel = m_pCel;
    if (!src || !soil || !cel) throw Error("CRain " + name() + ": a tank or the cel is missing (crash in the original)");
    const double dt = static_cast<double>(dtU);
    double amt = dt * m_dMag * 0.0002777777777777778;
    if (((sim.milestones() & 2) != 0 || soil->waterVol() <= soil->volume() * 1.01) && amt <= src->waterVol()) {
        double vSoil = soil->waterVol() + amt;
        double w = amt * 4.0;                                  // the rain mixes into the air with four times its volume
        double t = (cel->airTemp() * cel->airVolume() + src->temp() * w) / (cel->airVolume() + w);
        cel->setAirTemp(t);
        cel->setNewAirTemp(t);
        double soilMass = cel->airVolume() * 3.0;
        double ts = (w * t + cel->soilTemp() * soilMass) / (w + soilMass);
        cel->setSoilTemp(ts);
        cel->setNewSoilTemp(ts);
        soil->setNewOxygen((amt * 4.5 + soil->waterVol() * soil->oxygen()) / vSoil);
        soil->setNewPH((src->ph() * amt + soil->waterVol() * soil->ph()) / vSoil);
        soil->setNewSalinity((src->salinity() * amt + soil->waterVol() * soil->salinity()) / vSoil);
        soil->setNewTemp((ts * amt + soil->temp() * soil->waterVol()) / vSoil);
        src->setWaterVol(src->waterVol() - amt);
        soil->setWaterVol(vSoil);
        // ORIGINAL BUG: the rain washes the pollution out of the *current* value only; the next commit of the cels
        // copies the untouched "new" value over it, so nothing is ever washed out
        cel->setPollution(dt * -0.0002777777777777778 + cel->pollution());
        if (cel->pollution() < 0.0) cel->setPollution(0.0);
        if (sim.fixes().rainWashesPollution) {      // fix: the commit copies the "new" value back, so wash that one too (as the humidity below does)
            cel->setNewPollution(dt * -0.0002777777777777778 + cel->newPollution());
            if (cel->newPollution() < 0.0) cel->setNewPollution(0.0);
        }
        if (0.0 < m_dHumidityRate) {
            double gain = m_dHumidityRate * dt;
            if (sim.model().vapourFromSoil) {
                // Model::vapourFromSoil (vapour model v2): the humidity the rain or mist adds is drawn from the source tank
                // (1 %RH = 4.1667e-6 volumes), so it is no longer vapour from nowhere; nothing is added above 100 %
                const double kVol = 4.1666666666666667e-06;
                double room = 100.0 - cel->newHumidity();
                if (gain > room) gain = room > 0.0 ? room : 0.0;
                if (gain * kVol > src->waterVol()) gain = src->waterVol() / kVol;
                src->setWaterVol(src->waterVol() - gain * kVol);
            }
            sim.statRain += gain;
            cel->setNewHumidity(gain + cel->newHumidity());
            double h = cel->newHumidity();
            clampD(h, 0.0, 100.0);
            cel->setNewHumidity(h);
            cel->setHumidity(cel->newHumidity());
        }
    }
}

BIOSYS_REGISTER_CTOR(1005cce0, [](World& w, const char* name, const char* a1, uint16_t a2, double a3, int32_t a5,
                                  double a6, double a8, int32_t a10) -> Item* {
    return w.adopt(std::make_unique<CWaterFlow>(w, std::string(name) + ">" + a1 + "#" + std::to_string(a2), name, a1,
                                                a2, a3, a5, a6, a8, a10));
});
BIOSYS_REGISTER_CTOR(1006f990, [](World& w, const char* name, const char* a1, const char* a2, uint16_t a3,
                                  double a4) -> Item* {
    return w.adopt(std::make_unique<CEvaporation>(w, std::string(name) + ">" + a1 + "#" + a2, name, a1, a2, a3, a4));
});
BIOSYS_REGISTER_CTOR(10071d60, [](World& w, const char* name, const char* a1, const char* a2, uint16_t a3,
                                  double a4, double a6) -> Item* {
    return w.adopt(std::make_unique<CRain>(w, std::string(name) + ">" + a1 + "#" + a2, name, a1, a2, a3, a4, a6));
});

} // namespace biosys::game
