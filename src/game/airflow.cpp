// Operation order and rounding follow the decompiled expressions; no fused multiply-add (see biomecel.cpp).
#pragma clang fp contract(off)
#include "game/airflow.h"
#include "game/feat_sensors.h"
#include "game/sim.h"
#include "game/watertank.h"
#include "game/world_ctors.gen.h"
#include <cmath>
#include <strings.h>

namespace biosys::game {

CAirFlow::CAirFlow(u32 ctorAddr, const char* className, std::string name)
    : Item(ctorAddr, className, std::move(name)) {}

// dll:1005a980
CAirFlow::CAirFlow(World& w, std::string name, const char* from, const char* to, uint16_t id, double mag,
                   int32_t twoWay, int32_t on, double windSpeed, Item* ventFeature, double ventAirflow,
                   Item* masterFlow)
    : Item(0x1005a980u, "CAirFlow", std::move(name)) {
    m_pWorld = &w;
    m_pFrom = &w.requireBiomeCel(from);   // original: log "can't find cel", delete this (then keeps using it)
    m_pTo = &w.requireBiomeCel(to);
    m_bOn = on;
    m_wId = id;
    m_dMag = mag;
    m_bTwoWay = twoWay;
    m_dWindSpeed = windSpeed;
    m_pVentFeature = ventFeature;
    m_dVentAirflow = ventAirflow;
    m_pMasterFlow = masterFlow;
    m_bReversed = 0;
    // original: if (master != NULL && twoWay) log "m_pMasterFlow != NULL && m_bTwoWay ... eeek!" (no state change)
    if (m_bOn != 0) {
        // CAirFlow::TurnOn(1) dll:1005af40: m_bOn is already nonzero, so the wind/vent additions are skipped
        // and it only stores 1.
        m_bOn = 1;
    }
}

// dll:1005ab90
void CAirFlow::saveSlot(SlotWriter& w) const {
    w.writeI32(m_bOn);
    w.writeI32(m_bReversed);
    w.writeF64(m_dWindSpeed);
}

// dll:1005abd0
void CAirFlow::loadSlot(SlotReader& r) {
    m_bOn = r.readI32("m_bOn");
    m_bReversed = r.readI32("m_bReversed");
    m_dWindSpeed = r.readF64("m_dWindSpeed");
    r.finishPad();
}

// dll:1000b210
CSoilBedFlow::CSoilBedFlow(World& w, std::string name, const char* from, const char* to, uint16_t id, double mag,
                           int32_t on, double windSpeed)
    : CAirFlow(0x1000b210u, "CSoilBedFlow", std::move(name)) {
    m_pWorld = &w;
    m_pFrom = &w.requireBiomeCel(from);
    m_pTo = &w.requireBiomeCel(to);
    m_dMag = mag;
    m_wId = id;
    m_bOn = on;
    m_bReversed = 0;
    m_dWindSpeed = windSpeed;
    m_bTwoWay = 0;
    m_unk48 = 0;
    m_pVentFeature = nullptr;
    m_unk4c = 0;
}

// dll:10024280
CLungAirFlow::CLungAirFlow(World& w, std::string name, const char* from, const char* to, uint16_t id,
                           const char* lungFeature, int32_t flag, double mag, double windSpeed, Item* ventFeature,
                           double ventAirflow)
    : CAirFlow(0x10024280u, "CLungAirFlow", std::move(name)) {
    m_pWorld = &w;
    m_pFrom = &w.requireBiomeCel(from);
    m_pTo = &w.requireBiomeCel(to);
    m_pLung = &w.requireFeature(lungFeature);   // original: log "can't find feature", delete this
    m_wId = id;
    m_iLungFlag = flag;
    m_bOn = 0;
    m_bReversed = 0;
    m_dMag = mag;
    m_bTwoWay = 0;
    m_pVentFeature = ventFeature;
    m_dWindSpeed = windSpeed;
    m_dVentAirflow = ventAirflow;
}

// dll:1002aa00
CBubbleFlow::CBubbleFlow(World& w, std::string name, const char* from, const char* to, uint16_t id)
    : CAirFlow(0x1002aa00u, "CBubbleFlow", std::move(name)) {
    m_pWorld = &w;
    m_pFrom = w.findBiomeCel(from);   // original: log "duff cel name" and carry on with NULL
    m_pTo = w.findBiomeCel(to);
    m_wId = id;
    m_bOn = 0;
    m_bReversed = 0;
    m_bTwoWay = 0;
    m_dMag = 1.0;                      // +0x38 = 0, +0x3c = 0x3ff00000
    m_dWindSpeed = 0;
    m_pVentFeature = nullptr;
}

Sim* CAirFlow::sim() const {
    Sim* s = m_pWorld ? m_pWorld->state().simCore : nullptr;
    if (!s) throw Error("CAirFlow " + name() + ": the world has no simulation core");
    return s;
}

// dll:1005af40 (vtable +0x24): switching on adds the flow's wind to the destination cel and its airflow to the vent
void CAirFlow::turnOn(int on) {
    if (on == 0) {
        if (m_bOn != 0) {
            m_pTo->setWindSpeed(m_pTo->windSpeed() - m_dWindSpeed);
            // the vent's target airflow (CVent+0x60 -= m_dVentAirflow) is the machine classes' member: a hook
            if (m_pVentFeature)
                if (Sim* s = sim(); s->hooks.ventAdjustTarget) s->hooks.ventAdjustTarget(as<CFeature>(m_pVentFeature), -m_dVentAirflow);
        }
        m_bOn = 0;
        return;
    }
    if (m_bOn == 0) {
        m_pTo->setWindSpeed(m_dWindSpeed + m_pTo->windSpeed());
        if (m_pVentFeature)
            if (Sim* s = sim(); s->hooks.ventAdjustTarget) s->hooks.ventAdjustTarget(as<CFeature>(m_pVentFeature), m_dVentAirflow);
    }
    m_bOn = 1;
}

// dll:1005afb0 (vtable +0x28): the wind contribution changes sign
void CAirFlow::setReversed(int reversed) {
    if (m_bReversed != reversed) {
        m_dWindSpeed = -m_dWindSpeed;
        m_bReversed = reversed;
        if (m_bOn != 0) {
            // the old contribution +w becomes -w: adding the new value twice is -2w (the original does exactly that)
            m_pTo->setWindSpeed(m_dWindSpeed + m_pTo->windSpeed());
            m_pTo->setWindSpeed(m_pTo->windSpeed() + m_dWindSpeed);
        }
    }
}

// dll:1005aca0 (vtable +0x20)
void CAirFlow::update(Sim&, uint32_t dtU) {
    if (m_bOn == 0) return;
    CBiomeCel& from = *m_pFrom;
    CBiomeCel& to = *m_pTo;
    if (m_bTwoWay != 0) {
        // diffusion between two neighbours: air volume moves a (from -> to) and b (to -> from)
        double k = static_cast<double>(dtU) * m_dMag * 0.0001388888888888889;
        double a = (from.airVolume() / from.airCapacity()) * k;
        double b = (to.airVolume() / to.airCapacity()) * k;
        double vFrom = (from.airVolume() - a) + b;
        double vTo = (to.airVolume() - b) + a;
        to.setNewAirTemp(((from.airTemp() - to.airTemp()) / vTo) * a + to.newAirTemp());
        from.setNewAirTemp(((to.airTemp() - from.airTemp()) / vFrom) * b + from.newAirTemp());
        to.setNewHumidity(((from.humidity() - to.humidity()) / vTo) * a + to.newHumidity());
        from.setNewHumidity(((to.humidity() - from.humidity()) / vFrom) * b + from.newHumidity());
        to.setNewCO2(((from.co2() - to.co2()) / vTo) * a + to.newCO2());
        from.setNewCO2(((to.co2() - from.co2()) / vFrom) * b + from.newCO2());
        to.setNewOxygen(((from.oxygen() - to.oxygen()) / vTo) * a + to.newOxygen());
        from.setNewOxygen(((to.oxygen() - from.oxygen()) / vFrom) * b + from.newOxygen());
        to.setNewPollution(((from.pollution() - to.pollution()) / vTo) * a + to.newPollution());
        from.setNewPollution(((to.pollution() - from.pollution()) / vFrom) * b + from.newPollution());
        to.setAirVolume(vTo);
        from.setAirVolume(vFrom);
        return;
    }
    // one-way slave of a master flow (soil bed or bubbler): moves the master's amount, scaled by mag
    if (!m_pMasterFlow) throw Error("CAirFlow " + name() + ": one-way flow without a master flow (crash in the original)");
    double amt = as<CAirFlow>(m_pMasterFlow).amount() * m_dMag;
    to.setNewAirTemp((from.airTemp() * amt) / to.airVolume() + to.newAirTemp());
    from.setNewAirTemp(from.newAirTemp() - (from.airTemp() * amt) / from.airVolume());
    to.setNewHumidity((from.humidity() * amt) / to.airVolume() + to.newHumidity());
    from.setNewHumidity(from.newHumidity() - (from.humidity() * amt) / from.airVolume());
    to.setNewCO2((from.co2() * amt) / to.airVolume() + to.newCO2());
    from.setNewCO2(from.newCO2() - (from.co2() * amt) / from.airVolume());
    to.setNewOxygen((from.oxygen() * amt) / to.airVolume() + to.newOxygen());
    from.setNewOxygen(from.newOxygen() - (from.oxygen() * amt) / from.airVolume());
    to.setNewPollution((from.pollution() * amt) / to.airVolume() + to.newPollution());
    from.setNewPollution(from.newPollution() - (amt * from.pollution()) / from.airVolume());
}

// dll:1000b3d0: fan-driven air through the soil bed. The soil of `from` and `to` exchanges heat with the air that passes
// and the pollution of `from` is removed (it is not delivered to `to`).
void CSoilBedFlow::update(Sim&, uint32_t dtU) {
    if (m_bOn == 0) return;
    CBiomeCel& from = *m_pFrom;
    CBiomeCel& to = *m_pTo;
    m_dAmount = static_cast<double>(dtU) * m_dMag * 0.0001388888888888889;
    if (from.airVolume() < m_dAmount) {
        m_dAmount = 0;
        return;
    }
    from.setNewSoilTemp((m_dAmount * from.airTemp()) / (from.airVolume() * 3.0) + from.newSoilTemp());
    from.setNewAirTemp(from.newAirTemp() - (from.airTemp() * m_dAmount) / from.airVolume());
    to.setNewSoilTemp((from.soilTemp() * m_dAmount) / (to.airVolume() * 3.0) + to.newSoilTemp());
    from.setNewSoilTemp(from.newSoilTemp() - (from.soilTemp() * m_dAmount) / (from.airVolume() * 3.0));
    to.setNewAirTemp((to.soilTemp() * m_dAmount) / to.airVolume() + to.newAirTemp());
    to.setNewSoilTemp(to.newSoilTemp() - (to.soilTemp() * m_dAmount) / (to.airVolume() * 3.0));
    to.setNewHumidity((from.humidity() * m_dAmount) / to.airVolume() + to.newHumidity());
    from.setNewHumidity(from.newHumidity() - (from.humidity() * m_dAmount) / from.airVolume());
    to.setNewCO2((from.co2() * m_dAmount) / to.airVolume() + to.newCO2());
    from.setNewCO2(from.newCO2() - (from.co2() * m_dAmount) / from.airVolume());
    to.setNewOxygen((from.oxygen() * m_dAmount) / to.airVolume() + to.newOxygen());
    from.setNewOxygen(from.newOxygen() - (from.oxygen() * m_dAmount) / from.airVolume());
    from.setNewPollution(from.newPollution() - (from.pollution() * m_dAmount) / from.airVolume());
}

// dll:10024500: the lung moves air between its plenum cel (`from`) and a biome cel; mode 0x11 draws, 0x1a pushes into
// the lung's own biome, the others send to another lung. No dt: the amount follows the bellows (capacity - volume).
void CLungAirFlow::update(Sim& sim, uint32_t) {
    if (m_bOn == 0) return;
    CFeature& lungFeature = *m_pLung;
    const CLung& lung = dynamic_cast<const CLung&>(lungFeature);
    bool limited = false;
    CBiomeCel* src = m_pFrom;
    CBiomeCel* dst;
    double amt = std::fabs((src->airCapacity() - src->airVolume()) * m_dMag);
    if (lung.mode() == 0x11) {
        dst = src;
        src = m_pTo;
        if (src->airVolume() / src->airCapacity() < 0.9) {
            if (sim.hooks.lungSetMoving) sim.hooks.lungSetMoving(lungFeature, 0);   // lung+0x1dc = 0
            if (sim.hooks.lungSwitchOff) sim.hooks.lungSwitchOff(lungFeature);      // lung vtable +0x74
            return;
        }
    } else {
        dst = m_pTo;
        if (lung.mode() == 0x1a) {
            if (1.1 < dst->airVolume() / dst->airCapacity()) {
                if (sim.hooks.lungSwitchOff) sim.hooks.lungSwitchOff(lungFeature);  // lung vtable +0x74
                return;
            }
        } else {
            if (1.0 <= dst->airVolume()) {
                if (sim.hooks.lungSetMoving) sim.hooks.lungSetMoving(lungFeature, 0);   // lung+0x1dc = 0
                return;
            }
            double room = 1.0 - dst->airVolume();
            limited = true;
            if (room < amt) amt = room;
        }
    }
    if (amt < src->airVolume()) {
        if (sim.hooks.lungSetMoving) sim.hooks.lungSetMoving(lungFeature, 1);       // lung+0x1dc = 1
        double vol = dst->airVolume() + amt;
        dst->setNewAirTemp(((src->airTemp() - dst->airTemp()) * amt) / vol + dst->newAirTemp());
        dst->setNewHumidity(((src->humidity() - dst->humidity()) * amt) / vol + dst->newHumidity());
        dst->setNewCO2(((src->co2() - dst->co2()) * amt) / vol + dst->newCO2());
        dst->setNewOxygen(((src->oxygen() - dst->oxygen()) * amt) / vol + dst->newOxygen());
        dst->setNewPollution(((src->pollution() - dst->pollution()) * amt) / vol + dst->newPollution());
        src->setAirVolume(src->airVolume() - amt);
        dst->setAirVolume(vol);
        if (limited) dst->setAirCapacity(vol);   // the destination lung's capacity follows its volume
    }
}

// dll:1002ab70: air bubbled through the ocean (TO -> O1)
void CBubbleFlow::update(Sim& sim, uint32_t dtU) {
    if (m_bOn == 0) return;
    CBiomeCel& from = *m_pFrom;
    CBiomeCel& to = *m_pTo;
    CWaterTank& oc = sim.ocean();
    const double dt = static_cast<double>(dtU);
    m_dAmount = dt * m_dMag * 0.0001388888888888889;
    from.setNewAirTemp(from.newAirTemp() - (m_dAmount * from.airTemp()) / from.airVolume());
    oc.setNewTemp(((from.airTemp() * m_dAmount) / oc.waterVol()) * 0.25 + oc.newTemp());
    to.setNewAirTemp((oc.temp() * m_dAmount) / to.airVolume() + to.newAirTemp());
    from.setNewHumidity(from.newHumidity() - (from.humidity() * m_dAmount) / from.airVolume());
    to.setNewHumidity((m_dAmount * 100.0) / to.airVolume() + to.newHumidity());
    to.setNewCO2((from.co2() * m_dAmount) / to.airVolume() + to.newCO2());
    from.setNewCO2(from.newCO2() - (from.co2() * m_dAmount) / from.airVolume());
    double ph = from.co2() * -1.4999999999999947 + 8.6;
    clampD(ph, 8.3, 8.6);
    oc.setNewPH((ph - oc.ph()) * dt * 5.555555555555555e-07 + oc.newPH());
    to.setNewOxygen((from.oxygen() * m_dAmount) / to.airVolume() + to.newOxygen());
    from.setNewOxygen(from.newOxygen() - (from.oxygen() * m_dAmount) / from.airVolume());
    oc.setNewOxygen(((from.oxygen() * 0.09523809523809523 + 3.0) - oc.oxygen()) * dt * 0.0002777777777777778 +
                    oc.newOxygen());
    to.setNewPollution((from.pollution() * m_dAmount) / to.airVolume() + to.newPollution());
    from.setNewPollution(from.newPollution() - (from.pollution() * m_dAmount) / from.airVolume());
}

static std::string flowName(const char* from, const char* to, uint16_t id) {
    return std::string(from) + ">" + to + "#" + std::to_string(id);
}

BIOSYS_REGISTER_CTOR(1005a980, [](World& w, const char* name, const char* a1, uint16_t a2, double a3, int32_t a5,
                                  int32_t a6, double a7, Item* a9, double a10, Item* a12) -> Item* {
    return w.adopt(std::make_unique<CAirFlow>(w, flowName(name, a1, a2), name, a1, a2, a3, a5, a6, a7, a9, a10, a12));
});
BIOSYS_REGISTER_CTOR(1000b210, [](World& w, const char* name, const char* a1, uint16_t a2, double a3, int32_t a5,
                                  int32_t a6, double a7) -> Item* {
    (void)a6;   // param_7 is never read by the original
    return w.adopt(std::make_unique<CSoilBedFlow>(w, flowName(name, a1, a2), name, a1, a2, a3, a5, a7));
});
BIOSYS_REGISTER_CTOR(10024280, [](World& w, const char* name, const char* a1, uint16_t a2, const char* a3, int32_t a4,
                                  double a5, double a7, Item* a9, double a10) -> Item* {
    return w.adopt(std::make_unique<CLungAirFlow>(w, flowName(name, a1, a2), name, a1, a2, a3, a4, a5, a7, a9, a10));
});
BIOSYS_REGISTER_CTOR(1002aa00, [](World& w, const char* name, const char* a1, uint16_t a2) -> Item* {
    return w.adopt(std::make_unique<CBubbleFlow>(w, flowName(name, a1, a2), name, a1, a2));
});

} // namespace biosys::game
