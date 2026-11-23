// Operation order and rounding follow the decompiled expressions; no fused multiply-add (see biomecel.cpp).
#pragma clang fp contract(off)
#include "game/watertank.h"
#include "game/sim.h"
#include <cstdio>
#include "game/world_ctors.gen.h"
#include <cstring>
#include <strings.h>

namespace biosys::game {

// dll:1000ec80
CWaterTank::CWaterTank(std::string name, int32_t bounded, double volume, float fillPercent, double oxygen,
                       double ph, double salinity, double temp, double purity)
    : Item(0x1000ec80u, "CWaterTank", std::move(name)) {
    m_dVolume = volume;
    m_bBounded = bounded;
    m_dWaterVol = fillPercent * 0.01 * volume;
    m_dOxygen = oxygen;
    m_dPH = ph;
    m_dSalinity = salinity;
    m_dTemp = temp;
    m_dPurity = purity;
    m_dNewTemp = temp;
    m_dNewOxygen = oxygen;
    m_dNewSalinity = salinity;
    m_dNewPH = ph;
    m_dNewPurity = purity;
}

CWaterTank::CWaterTank(u32 ctorAddr, const char* className, std::string name)
    : Item(ctorAddr, className, std::move(name)) {}

// dll:1000eda0
void CWaterTank::saveSlot(SlotWriter& w) const {
    w.writeF64(m_dOxygen);
    w.writeF64(m_dPH);
    w.writeF64(m_dSalinity);
    w.writeF64(m_dTemp);
    w.writeF64(m_dPurity);
    w.writeF64(m_dWaterVol);
}

// dll:1000ee30 (the "new" values are set equal to the loaded ones)
void CWaterTank::loadSlot(SlotReader& r) {
    m_dOxygen = r.readF64("m_dOxygen");       m_dNewOxygen = m_dOxygen;
    m_dPH = r.readF64("m_dPH");               m_dNewPH = m_dPH;
    m_dSalinity = r.readF64("m_dSalinity");   m_dNewSalinity = m_dSalinity;
    m_dTemp = r.readF64("m_dTemp");           m_dNewTemp = m_dTemp;
    m_dPurity = r.readF64("m_dPurity");       m_dNewPurity = m_dPurity;
    m_dWaterVol = r.readF64("m_dWaterVol");
    r.finishPad();
}

// dll:10029570
COcean::COcean(World& w, std::string name, const char* celName, double volume, double waterVol, double oxygen,
               double newPH, double salinity, double temp)
    : CWaterTank(0x10029570u, "COcean", std::move(name)) {
    m_pCel = &w.requireBiomeCel(celName);   // original logs and then dereferences NULL when the cel is missing
    m_dVolume = volume;
    m_dWaterVol = waterVol;
    m_dNewPH = newPH;
    m_dOxygen = oxygen;
    m_dNewOxygen = oxygen;
    m_dSalinity = salinity;
    m_dNewSalinity = salinity;
    m_dTemp = temp;
    m_dNewTemp = temp;
    m_bBounded = 0;
    double co2 = m_pCel->co2();          // cel+0x58
    m_dPurity = 0;
    m_dNewPurity = 0;
    m_wLevelState = 0;
    m_wProblemFlags = 0;
    m_dHealth = 1.0;                      // +0x88 = 0, +0x8c = 0x3ff00000
    m_bWaveMachineOn = 1;
    m_dAlgae = 0;
    m_dPH = co2 * -1.4999999999999947 + 8.6;
    m_dwLastStateTime = 0x7080;
}

// dll:10029870
void COcean::saveSlot(SlotWriter& w) const {
    CWaterTank::saveSlot(w);
    w.writeI32(m_bWaveMachineOn);
    w.writeF64(m_dAlgae);
    w.writeF64(m_dHealth);
    w.writeU16(m_wProblemFlags);
    w.writeU16(m_wLevelState);
    w.writeI32(m_dwLastStateTime);
}

// dll:10029900 (CWaterTank::Load reads the tank part; its trailing pad check is done once at the end)
void COcean::loadSlot(SlotReader& r) {
    m_dOxygen = r.readF64("m_dOxygen");       m_dNewOxygen = m_dOxygen;
    m_dPH = r.readF64("m_dPH");               m_dNewPH = m_dPH;
    m_dSalinity = r.readF64("m_dSalinity");   m_dNewSalinity = m_dSalinity;
    m_dTemp = r.readF64("m_dTemp");           m_dNewTemp = m_dTemp;
    m_dPurity = r.readF64("m_dPurity");       m_dNewPurity = m_dPurity;
    m_dWaterVol = r.readF64("m_dWaterVol");
    m_bWaveMachineOn = r.readI32("m_bWaveMachineOn");
    m_dAlgae = r.readF64("m_dAlgae");
    m_dHealth = r.readF64("m_dHealth");
    m_wProblemFlags = r.readU16("m_wProblemFlags");
    m_wLevelState = r.readU16("m_wLevelState");
    m_dwLastStateTime = r.readI32("unk_78");
    r.finishPad();
}

// dll:1000f020 (vtable +0x20): clamp the new values, then they become the current ones
void CWaterTank::commit() {
    clampD(m_dNewOxygen, 0.0, 100.0);
    m_dOxygen = m_dNewOxygen;
    clampD(m_dNewPH, 3.0, 13.0);
    m_dPH = m_dNewPH;
    clampD(m_dNewSalinity, 0.0, 100.0);
    m_dSalinity = m_dNewSalinity;
    clampD(m_dNewTemp, -20.0, 50.0);
    m_dTemp = m_dNewTemp;
    clampD(m_dNewPurity, 0.0, 100.0);
    m_dPurity = m_dNewPurity;
}

// dll:10029990. `now` is the game time; the original then redraws the ocean level on the current page (BiosGame).
void COcean::updateState(Sim& sim, i32 now) {
    const i32 last = m_dwLastStateTime;
    m_dwLastStateTime = now;
    CBiomeCel& cel = *m_pCel;
    const double dt = static_cast<double>(static_cast<uint32_t>(now - last));
    // the ocean and the air above it exchange heat; the water has four times the heat capacity
    double q = (cel.airTemp() - m_dTemp) * dt * 0.0002777777777777778;
    m_dNewTemp = q * 0.25 + m_dNewTemp;
    cel.setNewAirTemp(cel.newAirTemp() - q);
    double ph = cel.co2() * -1.4999999999999947 + 8.6;
    clampD(ph, 8.3, 8.6);
    double x = (ph - m_dPH) * dt * 5.555555555555555e-07 + m_dNewPH;
    m_dNewPH = x;
    // ORIGINAL BUG (study A3): the wave machine aims at 3.4 while the damage limit below and the probe's alarm are 3.5, so
    // with the machine on the ocean is permanently flagged 0x20 (health -0.01 per day, and no recovery while any flag is set).
    // Model::waveMachineSetpoint: 4.3, the oxygen the ocean is built with (COcean's constructor)
    const double waveTarget = sim.model().waveMachineSetpoint ? 4.3 : 3.4;
    if (m_bWaveMachineOn == 0) {
        x = dt * -2.777777777777778e-05;
    } else if (waveTarget <= m_dOxygen) {
        x = dt * -2.777777777777778e-05;
    } else {
        x = (waveTarget - m_dOxygen) * dt * 0.0002777777777777778;
    }
    m_dNewOxygen = x + m_dNewOxygen;
    m_dAlgae = cel.light() * m_dTemp * dt * 1.1574074074074073e-05 + m_dAlgae;
    clampD(m_dAlgae, 0.0, 100.0);
    m_wProblemFlags = 0;
    if (23.0 <= m_dTemp) {
        if (26.0 < m_dTemp) {
            m_wProblemFlags = 1;
            m_dHealth = (m_dTemp - 26.0) * dt * -1.1574074074074073e-05 + m_dHealth;
        }
    } else {
        m_wProblemFlags = 2;
        m_dHealth = (23.0 - m_dTemp) * dt * -1.1574074074074073e-05 + m_dHealth;
    }
    if (m_dOxygen < 3.5) {
        m_wProblemFlags = static_cast<uint16_t>(m_wProblemFlags | 0x20);
        m_dHealth = (3.5 - m_dOxygen) * dt * -1.1574074074074074e-06 + m_dHealth;
    }
    if (3.5 <= m_dSalinity) {
        if (m_dSalinity > 4.2) {
            m_wProblemFlags = static_cast<uint16_t>(m_wProblemFlags | 0x80);
            m_dHealth = (m_dSalinity - 4.2) * dt * -1.1574074074074073e-05 + m_dHealth;
        }
    } else {
        m_wProblemFlags = static_cast<uint16_t>(m_wProblemFlags | 0x40);
        m_dHealth = (3.5 - m_dSalinity) * dt * -1.1574074074074073e-05 + m_dHealth;
    }
    if (70.0 < m_dAlgae) {
        m_wProblemFlags = static_cast<uint16_t>(m_wProblemFlags | 0x100);
        m_dHealth = (m_dAlgae - 70.0) * dt * -3.8580246913580245e-07 + m_dHealth;
    }
    if (m_wProblemFlags == 0) m_dHealth = dt * 3.858024691358025e-06 + m_dHealth;   // no problem: full recovery in 3 days
    clampD(m_dHealth, 0.0, 1.0);
    if (m_dWaterVol <= 11.25) m_wLevelState = (10.75 <= m_dWaterVol) ? 2 : 3;
    else m_wLevelState = 1;
    // (the page display of the level follows in BiosGame::COcean_UpdateState; the log lines below come after it in the
    // original, which changes nothing but their position among the log lines of the display code)
    char b[64];
    if (m_dTemp < 0.0 || 1000.0 < m_dTemp) {
        std::snprintf(b, sizeof b, "Ocean m_dTemp %f", m_dTemp);
        sim.log(b);   // PORT: the original then calls the tank's Dump (vtable +0x1c: debugging text only)
    }
    if (30.0 < m_dTemp) {
        sim.log("Ocean::UpdateState  m_dTemp > 30 ");
        std::snprintf(b, sizeof b, "Ocean m_dTemp %f", m_dTemp);
        sim.log(b);
    }
}

BIOSYS_REGISTER_CTOR(1000ec80, [](World& w, const char* name, int32_t a1, double a2, double a4, double a5, double a7,
                                  double a9, double a11, double a13) -> Item* {
    return w.adopt(std::make_unique<CWaterTank>(name ? name : "", a1, a2, static_cast<float>(a4), a5, a7, a9, a11, a13));
});
BIOSYS_REGISTER_CTOR(10029570, [](World& w, const char* name, const char* a1, double a2, double a4, double a6,
                                  double a8, double a10, double a12) -> Item* {
    return w.adopt(std::make_unique<COcean>(w, name ? name : "", a1, a2, a4, a6, a8, a10, a12));
});

} // namespace biosys::game
