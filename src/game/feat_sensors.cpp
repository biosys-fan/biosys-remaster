#include "game/feat_sensors.h"
#include "game/biomecel.h"
#include "game/watertank.h"
#include "game/airflow.h"
#include <cctype>
#include "game/eng.h"
#include "game/world_ctors.gen.h"

namespace biosys::game {

// ---------------------------------------------------------------------------------------------
// CTankSensor

// dll:100262d0
CTankSensor::CTankSensor(World& w, const char* name, u32 a1, i32 a2, CWaterTank* tank, u16 id)
    : CFeature(w, 0x100262d0, "CTankSensor", name, a1 | 1, static_cast<u32>(a2)) {
    m_wId = id;                 // +0x18
    m_dMalfunctionProb = 0;     // +0x40
    m_dwAlarms = 0;             // +0x90
    m_pTank = tank;             // +0x58
    m_dTempError = 0;           // +0x78
    m_dPurityError = 0;         // +0x80
    m_dLevelError = 0;          // +0x88
}

void CTankSensor::saveSlot(SlotWriter& w) const {   // dll:100264d0
    saveBase(w);
    w.writeU32(m_dwAlarms);
    w.writeF64(m_dTempReading);
    w.writeF64(m_dPurityReading);
    w.writeF64(m_dLevelReading);
    w.writeF64(m_dTempError);
    w.writeF64(m_dPurityError);
    w.writeF64(m_dLevelError);
}

// The names are the ones of the save-format tables (saveblob.md; biosys_savblob.py dump); see the member comments for what the
// doubles are.
void CTankSensor::loadSlot(SlotReader& r) {         // dll:10026580
    loadBase(r);
    m_dwAlarms = r.readHex32("m_dwAlarms");
    m_dTempReading = r.readF64("m_dTempReading");
    m_dPurityReading = r.readF64("unk_68");
    m_dLevelReading = r.readF64("m_dPurityReading");
    m_dTempError = r.readF64("unk_78");
    m_dPurityError = r.readF64("m_dLevelReading");
    m_dLevelError = r.readF64("unk_88");
    r.finishPad();
}

// dll:10026400 CTankSensor::GetParam
bool CTankSensor::getParam(i32 id, i32* pi, double* pd) const {
    switch (id) {
    case 0x28: *pd = m_dTempReading; return true;      // FEATURE_WATER_TEMPERATURE
    case 0x2b: *pd = m_dLevelReading; return true;     // FEATURE_WATER_LEVEL
    case 0x2c: *pd = m_dPurityReading; return true;    // FEATURE_WATER_PURITY
    case 0x2d: *pd = m_pTank->volume(); return true;   // tank capacity
    case 0x32: *pi = static_cast<i32>(m_dwAlarms); return true;   // FEATURE_ALARMS
    default: return baseGetParam(id, pi, pd);
    }
}

// dll:10026770 CTankSensor::Update: the readings are the tank's values plus RandPlusMinus(error) (the errors are 0, the
// generator still advances); alarms: temperature < 10 (0x20000) / > 30 (0x10000)
void CTankSensor::update(i32 now) {
    m_dwTimeLastUpdate = now;
    Rng& rng = world().state().rng;
    m_dTempReading = rng.randPlusMinus(m_dTempError) + m_pTank->temp();
    m_dPurityReading = m_pTank->purity() * 100.0 + rng.randPlusMinus(m_dPurityError);
    if (m_dPurityReading < 0) m_dPurityReading = 0; else if (100.0 < m_dPurityReading) m_dPurityReading = 100.0;
    m_dLevelReading = m_pTank->waterVol() / m_pTank->volume() * 100.0 + rng.randPlusMinus(m_dLevelError);
    if (m_dLevelReading < 0) m_dLevelReading = 0; else if (100.0 < m_dLevelReading) m_dLevelReading = 100.0;
    u32 prev = m_dwAlarms;
    m_dwAlarms = 0;
    if (m_dTempReading < 10.0) m_dwAlarms = 0x20000;
    if (30.0 < m_dTempReading) m_dwAlarms |= 0x10000;
    if (m_dwAlarms != 0) {
        if (prev == 0) {
            cworld::startPageFX(world(), static_cast<i16>(m_wId));
            return;
        }
        if (m_dwAlarms != 0) return;
    }
    // ORIGINAL BUG: dll:10026770 stops the alarm FX when there was no alarm before either (`if (prev == 0)`), not when the
    // alarm has just ended (`prev != 0`): the FX of an ended alarm keeps playing and StopPageFX is called on every update
    // of a quiet sensor
    // fix: stop it when an alarm has just ended (the sibling sensors, e.g. COceanProbe below, test `prev != 0`)
    if (fixes().sensorAlarmFxStop ? prev != 0 : prev == 0) cworld::stopPageFX(world(), m_wId);
}

// ---------------------------------------------------------------------------------------------
// CBiomeSensor

// The error ranges both constructors store (doubles written as dword pairs): 0.1, 1e-4, 0.001, 0.01.
void CBiomeSensor::setErrorRanges() {
    m_dAirTempError = dblBits(0x9999999a, 0x3fb99999);      // +0xd8  0.1
    m_dCO2Error = dblBits(0xeb1c432d, 0x3f1a36e2);          // +0xe0  0.0001
    m_dHumidityError = dblBits(0x9999999a, 0x3fb99999);     // +0xe8  0.1
    m_dLightError = dblBits(0xd2f1a9fc, 0x3f50624d);        // +0xf0  0.001
    m_dOxygenError = dblBits(0xd2f1a9fc, 0x3f50624d);       // +0xf8  0.001
    m_dPollutionError = dblBits(0x47ae147b, 0x3f847ae1);    // +0x100 0.01
    m_dSoilTempError = dblBits(0x9999999a, 0x3fb99999);     // +0x108 0.1
    m_dSoilPHError = dblBits(0x47ae147b, 0x3f847ae1);       // +0x110 0.01
    m_dSoilWaterError = dblBits(0x47ae147b, 0x3f847ae1);    // +0x118 0.01
    m_dWindSpeedError = dblBits(0x47ae147b, 0x3f847ae1);    // +0x120 0.01
}

// dll:1005d220 (default constructor, vtable only) followed by the caller's CFeature__Init
CBiomeSensor::CBiomeSensor(World& w, u32 ctorAddr, const char* className, const char* name, u32 flags, u32 powerMask)
    : CFeature(w, ctorAddr, className, name, flags, powerMask) {}

// dll:1005d300
CBiomeSensor::CBiomeSensor(World& w, const char* name, u32 a1, u32 a2, CBiomeCel* cel, u16 id, const char* file,
                           DllPtr thresholds, double rainToday)
    : CFeature(w, 0x1005d300, "CBiomeSensor", name, a1 | 3, a2 | 0x10000) {
    m_dMalfunctionProb = 0;                         // +0x40
    m_wId = id;                                     // +0x18
    m_dwThresholdAddr = thresholds.addr;            // +0x1c8
    m_pThresholds = CBiomeSensor::thresholds(thresholds.addr);
    m_dwAlarms = 0;                                 // +0x1cc
    m_pCel = cel;                                   // +0x58
    m_szFile = checkedText(file, 12, "CBiomeSensor file name");   // +0x5c
    m_dPressureError = dblBits(0x9999999a, 0x3fb99999);           // +0xd0  0.1
    m_dRainToday = rainToday * 0.002777777777777778;              // +0xc0
    setErrorRanges();
    // The first value of every reading: the cel's value plus RandPlusMinus(error), the draws in the original's order
    // (the original computes the sums in x87 extended precision, the port in double).
    if (!cel) throw Error("CBiomeSensor " + std::string(name) + ": cel pointer is NULL (the original dereferences it)");
    Rng& rng = w.state().rng;
    m_dPressureReading = cel->airVolume() / cel->airCapacity() * 1000.0 + rng.randPlusMinus(0.1);   // +0x68
    m_dAirTempReading = rng.randPlusMinus(m_dAirTempError) + cel->airTemp();                          // +0x70
    m_dCO2Reading = rng.randPlusMinus(m_dCO2Error) + cel->co2();                                      // +0x78
    m_dHumidityReading = rng.randPlusMinus(m_dHumidityError) + cel->humidity();                       // +0x80
    m_dLightReading = (rng.randPlusMinus(m_dLightError) + cel->light()) * 100.0;                      // +0x88
    m_dOxygenReading = rng.randPlusMinus(m_dOxygenError) + cel->oxygen();                             // +0x90
    m_dPollutionReading = rng.randPlusMinus(m_dPollutionError) + cel->pollution();                    // +0x98
    m_dSoilPHReading = rng.randPlusMinus(m_dSoilPHError) + cel->soilpH();                             // +0xa8
    m_dSoilTempReading = rng.randPlusMinus(m_dSoilTempError) + cel->soilTemp();                       // +0xa0
    m_dSoilWaterReading = rng.randPlusMinus(m_dSoilWaterError) + cel->soilWater();                    // +0xb0
    m_dWindSpeedReading = rng.randPlusMinus(m_dWindSpeedError) + cel->windSpeed();                    // +0xb8
    onNewDay();
}

// dll:1005e860
void CBiomeSensor::onNewDay() {
    m_dRainYesterday = m_dRainToday;                       // +0xc8 = +0xc0
    m_dRainToday = 0;                                      // +0xc0
    for (int i = 1; i < 10; i++) m_dMin[i] = 100.0;        // +0x130..+0x170 (the one at +0x128 is left alone)
    for (int i = 1; i < 10; i++) m_dMax[i] = 0.0;          // +0x180..+0x1c0 (the one at +0x178 is left alone)
}

// The alarm thresholds the sensors point at: airT lo/hi, soilT lo/hi, humidity lo/hi, soil water lo/hi (read from the DLL image
// at the addresses the builders pass: rainforest, savannah, ocean, dead biome, house, and the lungs' own table)
const double* CBiomeSensor::thresholds(u32 addr) {
    static const double t7948[8] = {22.0, 35.0, 22.0, 35.0, 60.0, 100.0, 20.0, 47.5};
    static const double t7988[8] = {10.0, 37.0, 10.0, 37.0, 0.0, 20.0, 4.0, 40.0};
    static const double t79c8[8] = {22.0, 30.0, 22.0, 30.0, 0.0, 100.0, 4.0, 38.0};
    static const double t7a08[8] = {10.0, 35.0, 10.0, 35.0, 20.0, 100.0, 20.0, 38.0};
    static const double t7a48[8] = {10.0, 35.0, 10.0, 35.0, 0.0, 100.0, 0.0, 100.0};
    static const double t7a88[8] = {10.0, 35.0, 10.0, 35.0, 0.0, 0.0, 0.0, 0.0};
    switch (addr) {
    case 0x100b7948: return t7948;
    case 0x100b7988: return t7988;
    case 0x100b79c8: return t79c8;
    case 0x100b7a08: return t7a08;
    case 0x100b7a48: return t7a48;
    case 0x100b7a88: return t7a88;
    }
    throw Error("CBiomeSensor: no alarm table at DLL address " + std::to_string(addr));
}

// dll:1005d620 CBiomeSensor::GetParam
bool CBiomeSensor::getParam(i32 id, i32* pi, double* pd) const {
    switch (id) {
    case 0x1d: *pd = m_dPressureReading; return true;
    case 0x1e: *pd = m_dAirTempReading; return true;
    case 0x1f: *pd = m_dSoilTempReading; return true;
    case 0x20: *pd = m_dSoilPHReading; return true;
    case 0x21: *pd = m_dSoilWaterReading; return true;
    case 0x22: *pd = m_dOxygenReading; return true;
    case 0x23: *pd = m_dCO2Reading; return true;
    case 0x24: *pd = m_dLightReading; return true;
    case 0x25: *pd = m_dWindSpeedReading; return true;
    case 0x26: *pd = m_dPollutionReading; return true;
    case 0x27: *pd = m_dHumidityReading; return true;
    case 0x2e: *pd = m_dRainToday; return true;
    case 0x32: *pi = static_cast<i32>(m_dwAlarms); return true;
    default: return baseGetParam(id, pi, pd);
    }
}

// dll:1005e0a0 CBiomeSensor::Update. Every reading is the cel's value plus RandPlusMinus(error), drawn in the order
// pressure, air temperature, CO2, humidity, light, oxygen, pollution, soil pH, soil temperature, soil water, wind (the
// constructor draws in the same order); the daily minimum / maximum (+0x128 / +0x178, index 0 pressure, 1 air T, 2 CO2,
// 3 humidity, 4 light, 5 oxygen, 6 pollution, 7 soil T, 8 soil pH, 9 soil water) are kept; the alarm bits follow.
void CBiomeSensor::update(i32 now) {
    i32 last = m_dwTimeLastUpdate;
    CBiomeCel& cel = *m_pCel;
    m_dwTimeLastUpdate = now;
    Rng& rng = world().state().rng;
    const double noise = world().state().tuning.sensorNoise;     // Tuning::sensorNoise: x the error range (the draws stay the same)
    auto clamp = [](double& x, double lo, double hi) { if (x < lo) x = lo; else if (hi < x) x = hi; };
    auto track = [&](int i, double v) {
        if (m_dMax[i] < v) m_dMax[i] = v;
        if (v < m_dMin[i]) m_dMin[i] = v;
    };
    if ((cel.flags() & 2) != 0) m_dRainToday = static_cast<double>(static_cast<u32>(now - last)) * 0.002777777777777778 + m_dRainToday;   // raining: +dt/360

    m_dPressureReading = cel.airVolume() / cel.airCapacity() * (cel.airTemp() + 273.0) * 0.0033003300330033004 * 1000.0 +
                         rng.randPlusMinus(m_dPressureError * noise);
    track(0, m_dPressureReading);
    m_dAirTempReading = rng.randPlusMinus(m_dAirTempError * noise) + cel.airTemp();
    track(1, m_dAirTempReading);
    m_dCO2Reading = rng.randPlusMinus(m_dCO2Error * noise) + cel.co2();
    clamp(m_dCO2Reading, 0.0, 100.0);
    track(2, m_dCO2Reading);
    m_dHumidityReading = rng.randPlusMinus(m_dHumidityError * noise) + cel.humidity();
    clamp(m_dHumidityReading, 0.0, 100.0);
    track(3, m_dHumidityReading);
    m_dLightReading = (rng.randPlusMinus(m_dLightError * noise) + cel.light()) * 100.0;
    clamp(m_dLightReading, 0.0, 100.0);
    track(4, m_dLightReading);
    m_dOxygenReading = rng.randPlusMinus(m_dOxygenError * noise) + cel.oxygen();
    clamp(m_dOxygenReading, 0.0, 100.0);
    track(5, m_dOxygenReading);
    m_dPollutionReading = rng.randPlusMinus(m_dPollutionError * noise) + cel.pollution();
    clamp(m_dPollutionReading, 0.0, 100.0);
    track(6, m_dPollutionReading);
    m_dSoilPHReading = rng.randPlusMinus(m_dSoilPHError * noise) + cel.soilpH();
    track(8, m_dSoilPHReading);
    m_dSoilTempReading = rng.randPlusMinus(m_dSoilTempError * noise) + cel.soilTemp();
    track(7, m_dSoilTempReading);
    m_dSoilWaterReading = rng.randPlusMinus(m_dSoilWaterError * noise) + cel.soilWater();
    clamp(m_dSoilWaterReading, 0.0, 50.0);
    track(9, m_dSoilWaterReading);
    m_dWindSpeedReading = rng.randPlusMinus(m_dWindSpeedError * noise) + cel.windSpeed();
    clamp(m_dWindSpeedReading, 0.0, 100.0);

    u32 prev = m_dwAlarms;
    m_dwAlarms = 0;
    // The alarm band, possibly moved: Model::savannahSoilAlarm (study A2): the savannah's soil-water low alarm is 4 while its
    // plants need 6 (AS3AL) to 7 (AS3OK, AS5OK, AS5WM), so a plant could be below its minimum with no warning: 7.
    // Tuning::alarmMargin: every limit that is not a physical bound (a low above 0, a high below 100) moves inward by that
    // fraction of the band (the warning comes early).
    double eff[8];
    const double* t = m_pThresholds;
    const Model& mdl = world().state().model;
    const double margin = world().state().tuning.alarmMargin;
    if ((mdl.savannahSoilAlarm && m_pThresholds == thresholds(0x100b7988)) || margin != 0.0) {
        for (int k = 0; k < 8; k++) eff[k] = t[k];
        if (mdl.savannahSoilAlarm && m_pThresholds == thresholds(0x100b7988)) eff[6] = 7.0;
        if (margin != 0.0) {
            for (int k = 0; k < 8; k += 2) {
                const double band = eff[k + 1] - eff[k];
                if (eff[k] > 0.0) eff[k] += margin * band;
                if (eff[k + 1] < 100.0 && eff[k + 1] > eff[k]) eff[k + 1] -= margin * band;
            }
        }
        t = eff;
    }
    if (m_dAirTempReading < t[0]) m_dwAlarms = 2;
    if (t[1] < m_dAirTempReading) m_dwAlarms |= 1;
    if (m_dSoilTempReading < t[2]) m_dwAlarms |= 8;
    if (t[3] < m_dSoilTempReading) m_dwAlarms |= 4;
    if (m_dHumidityReading < t[4]) m_dwAlarms |= 0x4000;
    if (t[5] < m_dHumidityReading) m_dwAlarms |= 0x2000;
    if (m_dSoilWaterReading < t[6]) m_dwAlarms |= 0x80;
    if (t[7] < m_dSoilWaterReading) m_dwAlarms |= 0x40;
    if (m_dPressureReading < 900.0) m_dwAlarms |= 0x4000000;
    if (1100.0 < m_dPressureReading) m_dwAlarms |= 0x2000000;
    if (m_dOxygenReading < 18.0) m_dwAlarms |= 0x800;
    if (23.0 < m_dOxygenReading) m_dwAlarms |= 0x400;
    if (m_dCO2Reading < 0.01) m_dwAlarms |= 0x200;
    if (3.0 < m_dCO2Reading) m_dwAlarms |= 0x100;
    if (10.0 < m_dPollutionReading) m_dwAlarms |= 0x1000;

    // the pollution alarm has its own FX (id + 1); any alarm switches the feature's FX (id) on
    if ((m_dwAlarms & 0x1000) != 0) {
        if ((prev & 0x1000) == 0 && cworld::hasPower(world(), m_dwPowerMask) != 0) {
            cworld::startPageFX(world(), static_cast<i16>(m_wId + 1));
            if (host()) host()->onPollutionAlarm(m_pCel);   // the player is in this cel and the status value > 20: two CSelf stores
        }
    } else if ((prev & 0x1000) != 0) {
        cworld::stopPageFX(world(), m_wId + 1);
    }
    if (m_dwAlarms != 0) {
        if (prev == 0 && cworld::hasPower(world(), m_dwPowerMask) != 0) {
            cworld::setFXState(world(), static_cast<i16>(m_wId), 1);
            return;
        }
        if (m_dwAlarms != 0) return;
    }
    if (prev != 0) cworld::setFXState(world(), static_cast<i16>(m_wId), 0);
}

bool CBiomeSensor::onHotspot(const std::string&) {   // dll:1005dfe0
    playSound("sounds\\features\\clikon6.wav");
    return true;
}
// dll:1005e000 CBiomeSensor::EnterPage: a page whose name ends in 'O' is the probe's own panel (the sensor palette, palette
// slot 5); any other page shows the readings
void CBiomeSensor::onEnterPage(const std::string& page) {
    if (host() && host()->eng()) host()->eng()->EnableUserInput(0, 0);
    if (!page.empty() && std::toupper(static_cast<unsigned char>(page[page.size() - 1])) == 'O') {
        if (host() && host()->eng()) host()->eng()->ShowCursor(0);
        if (host()) host()->openSensorPanel();      // CEngineFX::ClearWindow; FUN_1006c790(&CSelf); LoadPalette("palettes\\sensor\\sensor.pal", 0, 0, 0, 5, 0)
        unk_54 = 1;
        return;
    }
    unk_54 = 0;
    display(0);
    statusMessage(0);
}
// dll:1005e970
void CBiomeSensor::onLeavePage() {
    if (unk_54 != 0) {
        if (host()) host()->closeSensorPanel();    // CEngineFX::UnlockPalette(5); ClearWindow; DAT_100c3630 = 1
    }
}
// dll:1005fd70 / 1005fd80: power on forgets the alarms; power off stops the alarm FX
void CBiomeSensor::onPowerOn() { m_dwAlarms = 0; }
void CBiomeSensor::onPowerOff() {
    if (m_dwAlarms != 0) {
        cworld::stopPageFX(world(), m_wId + 1);
        cworld::setFXState(world(), static_cast<i16>(m_wId), 0);
        m_dwAlarms = 0;
    }
}

void CBiomeSensor::saveSensor(SlotWriter& w) const {
    w.writeU32(m_dwAlarms);
    w.writeF64(m_dPressureReading);
    w.writeF64(m_dAirTempReading);
    w.writeF64(m_dCO2Reading);
    w.writeF64(m_dHumidityReading);
    w.writeF64(m_dLightReading);
    w.writeF64(m_dOxygenReading);
    w.writeF64(m_dPollutionReading);
    w.writeF64(m_dSoilPHReading);
    w.writeF64(m_dSoilTempReading);
    w.writeF64(m_dSoilWaterReading);
    w.writeF64(m_dWindSpeedReading);
    w.writeF64(m_dAirTempError);
    w.writeF64(m_dCO2Error);
    w.writeF64(m_dHumidityError);
    w.writeF64(m_dLightError);
    w.writeF64(m_dOxygenError);
    w.writeF64(m_dPollutionError);
    w.writeF64(m_dSoilPHError);
    w.writeF64(m_dSoilTempError);
    w.writeF64(m_dSoilWaterError);
    w.writeF64(m_dWindSpeedError);
}

void CBiomeSensor::loadSensor(SlotReader& r) {
    m_dwAlarms = r.readHex32("m_dwAlarms");
    m_dPressureReading = r.readF64("m_dPressureReading");
    m_dAirTempReading = r.readF64("m_dAirTempReading");
    m_dCO2Reading = r.readF64("m_dCO2Reading");
    m_dHumidityReading = r.readF64("m_dHumidityReading");
    m_dLightReading = r.readF64("m_dLightReading");
    m_dOxygenReading = r.readF64("m_dOxygenReading");
    m_dPollutionReading = r.readF64("m_dPollutionReading");
    m_dSoilPHReading = r.readF64("m_dSoilPHReading");
    m_dSoilTempReading = r.readF64("m_dSoilTempReading");
    m_dSoilWaterReading = r.readF64("m_dSoilWaterReading");
    m_dWindSpeedReading = r.readF64("m_dWindSpeedReading");
    m_dAirTempError = r.readF64("m_dAirTempError");
    m_dCO2Error = r.readF64("m_dCO2Error");
    m_dHumidityError = r.readF64("m_dHumidityError");
    m_dLightError = r.readF64("m_dLightError");
    m_dOxygenError = r.readF64("m_dOxygenError");
    m_dPollutionError = r.readF64("m_dPollutionError");
    m_dSoilPHError = r.readF64("m_dSoilPHError");
    m_dSoilTempError = r.readF64("m_dSoilTempError");
    m_dSoilWaterError = r.readF64("m_dSoilWaterError");
    m_dWindSpeedError = r.readF64("m_dWindSpeedError");
}

void CBiomeSensor::saveSlot(SlotWriter& w) const {   // dll:1005d810
    saveBase(w);
    saveSensor(w);
}

void CBiomeSensor::loadSlot(SlotReader& r) {         // dll:1005da10
    loadBase(r);
    loadSensor(r);
    r.finishPad();
}

// ---------------------------------------------------------------------------------------------
// CLung

// dll:1000fe10
CLung::CLung(World& w, const char* name, u32 a1, u32 a2, u16 id, u16 id2, i32 src, i32 dst, DllPtr thresholds)
    : CBiomeSensor(w, 0x1000fe10, "CLung", name, a1 | 3, a2 | 0x400) {
    setParam(0x13, src, 0.0);
    setParam(0x14, dst, 0.0);
    m_dwThresholdAddr = thresholds.addr;                        // +0x1c8
    m_pThresholds = CBiomeSensor::thresholds(thresholds.addr);
    m_wId = id;                         // +0x18
    m_dwAlarms = 0;                     // +0x1cc
    m_wId2 = id2;                       // +0x1d8
    setErrorRanges();                   // +0xd8..+0x120 (not +0xd0)
    m_dwBellowsDir = 1;                 // +0x1dc
}

// dll:10010050 CLung::SetParam. Modes (+0x1d4): 0x11 draw air from the own biome, 0x15..0x18 send to the lung TRF / TS / TDB / TO,
// 0x1a push into the own biome. Id 0x13 sets the lung's own cel, 0x14 the mode and its target cel; a running lung is stopped
// and started again around the change.
bool CLung::setParam(i32 id, i32 value, double) {
    if (id == 0x11) {     // FEATURE_DRAW_AIR_FROM_BIOME
        if (m_bActive != 0) stopRunning();
        m_dwMode = 0x11;
        if (m_bActive != 0) startRunning();
        return true;
    }
    const char* cel = nullptr;
    if (id == 0x13) {
        switch (value) {
        case 0x15: m_pCel = world().findBiomeCel("TRF"); return true;
        case 0x16: m_pCel = world().findBiomeCel("TS"); return true;
        case 0x17: m_pCel = world().findBiomeCel("TDB"); return true;
        case 0x18: m_pCel = world().findBiomeCel("TO"); return true;
        }
        // ORIGINAL BUG: dll:10010050: any other value of id 0x13 leaves the switch without returning and runs the
        // code of id 0x14 below with it (stops the lung, stores the value as the mode, returns 0 for an unknown code)
        // fix: an unknown source code is rejected as it is for every other unknown code (the cel stays as it is)
        if (fixes().lungParamUnknownId) return false;
    } else if (id != 0x14) {
        return baseSetParam(id, value);
    }
    if (m_bActive != 0) stopRunning();
    const i32 previousMode = m_dwMode;
    m_dwMode = value;                   // +0x1d4
    switch (value) {
    case 0x15: cel = "TRF"; break;
    case 0x16: cel = "TS"; break;
    case 0x17: cel = "TDB"; break;
    case 0x18: cel = "TO"; break;
    case 0x1a: break;                   // push into its own biome: no target cel
    default:
        // ORIGINAL BUG: an unknown mode returns here after the lung was stopped and never starts it again
        // fix: a rejected mode leaves the lung running as it was (stopRunning above was only for the change) and is not stored:
        // an unknown mode that stays stored and is then switched on sends CLung::Update into the target cel (+0x1d0), which only
        // the modes 0x15..0x18 ever set (see update())
        if (fixes().lungParamUnknownMode) {
            m_dwMode = previousMode;
            if (m_bActive != 0) startRunning();
        }
        return false;
    }
    if (cel) m_pTargetCel = world().findBiomeCel(cel);   // +0x1d0
    if (m_bActive != 0) startRunning();
    return true;
}

// dll:100101e0 CLung::GetParam: 0x11 mode is "draw", 0x14 the mode, 0x1b the target cel's air volume x 100, 0x1c the lung cel's
bool CLung::getParam(i32 id, i32* pi, double* pd) const {
    switch (id) {
    case 0x11: *pi = m_dwMode == 0x11 ? 1 : 0; return true;
    case 0x14: *pi = m_dwMode; return true;
    case 0x1c: *pd = m_pCel->airVolume() * 100.0; return true;
    case 0x1b:
        if (!m_pTargetCel) return false;
        *pd = m_pTargetCel->airVolume() * 100.0;
        return true;
    default: return CBiomeSensor::getParam(id, pi, pd);
    }
}

// dll:10010a30 CLung::Update: while active, powered and moving the bellows (the lung cel's capacity) travel by dt/10800 per
// stroke direction (a stroke takes 3 game hours); at the end of the stroke the lung switches itself off. Then the sensor part.
void CLung::update(i32 now) {
    u32 dt = static_cast<u32>(now) - static_cast<u32>(m_dwTimeLastUpdate);
    if (m_bActive != 0 && cworld::hasPower(world(), m_dwPowerMask) != 0 && m_dwBellowsDir != 0) {
        CBiomeCel& cel = *m_pCel;
        double d = m_dwMode == 0x11 ? static_cast<double>(dt) * 9.259259259259259e-05 : static_cast<double>(dt) * -9.259259259259259e-05;
        cel.setAirCapacity(cel.airCapacity() + d);
        bool off = false;
        if (m_dwMode == 0x11) {
            if (cel.airCapacity() > 1.0) {
                cel.setAirCapacity(1.0);
                off = true;
            }
        } else if (m_dwMode == 0x1a) {
            if (cel.airCapacity() < 0.0) {
                cel.setAirCapacity(0.0);
                off = true;
            }
        } else if (0.0 <= cel.airCapacity()) {
            // ORIGINAL BUG: dll:10010a30 reads the capacity of the target cel +0x1d0 for every mode that is not 0x11 / 0x1a, but
            // only the modes 0x15..0x18 set it: a lung given an unknown mode (SetParam 0x14 stores it before it rejects it) and
            // then switched on dereferences NULL (+0x1d0 is never written by the constructor for the modes 0x11 / 0x1a) and the
            // original game dies with an access violation. The port must not: a missing target reads as an empty cel (capacity 0),
            // the lung ends its stroke and switches itself off. Fixes::lungParamUnknownMode keeps the unknown mode from being stored.
            const double targetCapacity = m_pTargetCel ? m_pTargetCel->airCapacity() : 0.0;
            if (targetCapacity <= 1.0) {
                if (world().state().features.sealGlobal == 0) off = true;   // DAT_100c42cc: set by CSeal::Save
            } else {
                cel.setAirCapacity(1.0);
                off = true;
            }
        } else {
            cel.setAirCapacity(0.0);
            off = true;
        }
        if (off) turnOff();
    }
    CBiomeSensor::update(now);
}

// dll:100108e0 CLung::StartRunning: the lung's flows start: id2 flows run lung<->biome, id flows lung->lung
void CLung::startRunning() {
    CFeature::startRunning();
    m_dwBellowsDir = 1;
    switch (m_dwMode) {
    case 0x11:
        cworld::setAirFlowsReversed(world(), static_cast<i16>(m_wId2), 1);
        cworld::switchAirFlows(world(), static_cast<i16>(m_wId2), 1);
        return;
    case 0x15: case 0x16: case 0x17: case 0x18:
        cworld::setAirFlowsDest(world(), static_cast<i16>(m_wId), m_pTargetCel);
        cworld::switchAirFlows(world(), static_cast<i16>(m_wId), 1);
        return;
    case 0x1a:
        cworld::setAirFlowsReversed(world(), static_cast<i16>(m_wId2), 0);
        cworld::switchAirFlows(world(), static_cast<i16>(m_wId2), 1);
    }
}
// dll:100109b0 CLung::StopRunning: the flows stop and the bellows freeze (capacity := current air volume), except for a lung
// that sends to another lung
void CLung::stopRunning() {
    CFeature::stopRunning();
    switch (m_dwMode) {
    case 0x11: case 0x1a:
        cworld::switchAirFlows(world(), static_cast<i16>(m_wId2), 0);
        break;
    case 0x15: case 0x16: case 0x17: case 0x18:
        cworld::switchAirFlows(world(), static_cast<i16>(m_wId), 0);
        return;
    }
    m_pCel->setAirCapacity(m_pCel->airVolume());
}

void CLung::saveSlot(SlotWriter& w) const {   // dll:100102b0 (CFeature::Save only)
    saveBase(w);
}

// ORIGINAL BUG: dll:100102d0 calls CFeature__Save instead of CFeature__Load, so the original ignores the saved
// base block (and overwrites it in the load buffer with the live state). The port does the same unless
// Fixes::lungLoadBaseBlock is set (the sibling CFeature::Load of every other class loads the block; then the blob round-trips).
void CLung::loadSlot(SlotReader& r) {         // dll:100102d0
    loadBaseOriginalBug(r, fixes().lungLoadBaseBlock);
    r.finishPad();
}

// ---------------------------------------------------------------------------------------------
// COceanProbe

// dll:10027610
COceanProbe::COceanProbe(World& w, const char* name, u32 a1, i32 a2, const char* file)
    : CFeature(w, 0x10027610, "COceanProbe", name, a1 | 3, static_cast<u32>(a2)) {
    m_dMalfunctionProb = 0;             // +0x40
    m_dwAlarms = 0;                     // +0xc8
    m_szFile = checkedText(file, 16, "COceanProbe file name");   // +0x58
    m_dTempError = 0;                   // +0x98
    m_dPHError = 0;                     // +0xa0
    m_dSalinityError = 0;               // +0xa8
    m_dClarityError = 0;                // +0xb0
    m_dOxygenError = 0;                 // +0xb8
    m_dLevelError = 0;                  // +0xc0
    m_wId = 0x7fe;                      // +0x18
}

void COceanProbe::saveSlot(SlotWriter& w) const {   // dll:100278f0
    saveBase(w);
    w.writeU32(m_dwAlarms);
    w.writeF64(m_dTempReading);
    w.writeF64(m_dPHReading);
    w.writeF64(m_dSalinityReading);
    w.writeF64(m_dClarityReading);
    w.writeF64(m_dOxygenReading);
    w.writeF64(m_dLevelReading);
    w.writeF64(m_dTempError);
    w.writeF64(m_dPHError);
    w.writeF64(m_dSalinityError);
    w.writeF64(m_dClarityError);
    w.writeF64(m_dOxygenError);
    w.writeF64(m_dLevelError);
}

void COceanProbe::loadSlot(SlotReader& r) {         // dll:10027a20
    loadBase(r);
    m_dwAlarms = r.readHex32("m_dwAlarms");
    m_dTempReading = r.readF64("m_dTempReading");
    m_dPHReading = r.readF64("m_dPHReading");
    m_dSalinityReading = r.readF64("m_dSalinityReading");
    m_dClarityReading = r.readF64("m_dClarityReading");
    m_dOxygenReading = r.readF64("m_dOxygenReading");
    m_dLevelReading = r.readF64("m_dLevelReading");
    m_dTempError = r.readF64("m_dTempError");
    m_dPHError = r.readF64("m_dPHError");
    m_dSalinityError = r.readF64("m_dSalinityError");
    m_dClarityError = r.readF64("m_dClarityError");
    m_dOxygenError = r.readF64("m_dOxygenError");
    m_dLevelError = r.readF64("m_dLevelError");
    r.finishPad();
}

// dll:100277d0 COceanProbe::GetParam
bool COceanProbe::getParam(i32 id, i32* pi, double* pd) const {
    switch (id) {
    case 0x20: *pd = m_dPHReading; return true;
    case 0x22: *pd = m_dOxygenReading; return true;
    case 0x28: *pd = m_dTempReading; return true;
    case 0x29: *pd = m_dClarityReading; return true;
    case 0x2a: *pd = m_dSalinityReading; return true;
    case 0x2b: *pd = m_dLevelReading; return true;
    case 0x32: *pi = static_cast<i32>(m_dwAlarms); return true;
    default: return baseGetParam(id, pi, pd);
    }
}

// dll:10027de0 COceanProbe::Update: readings of the ocean (DAT_100c1cc8) with RandPlusMinus(error) in the order temperature,
// pH, salinity, clarity (100 - algae), oxygen, level
void COceanProbe::update(i32 now) {
    m_dwTimeLastUpdate = now;
    COcean& oc = cworld::ocean(world());
    Rng& rng = world().state().rng;
    auto clamp = [](double& x, double lo, double hi) { if (x < lo) x = lo; else if (hi < x) x = hi; };
    m_dTempReading = rng.randPlusMinus(m_dTempError) + oc.temp();
    m_dPHReading = rng.randPlusMinus(m_dPHError) + oc.ph();
    m_dSalinityReading = rng.randPlusMinus(m_dSalinityError) + oc.salinity();
    clamp(m_dSalinityReading, 0.0, 100.0);
    m_dClarityReading = (100.0 - oc.algae()) + rng.randPlusMinus(m_dClarityError);
    // ORIGINAL BUG: dll:10027de0 clamps the salinity reading (+0x78) here a second time instead of the clarity just computed
    // fix: every other reading is clamped right after it is drawn; the clarity is a percentage like them
    if (fixes().oceanProbeClarity) clamp(m_dClarityReading, 0.0, 100.0);
    else clamp(m_dSalinityReading, 0.0, 100.0);
    m_dOxygenReading = rng.randPlusMinus(m_dOxygenError) + oc.oxygen();
    clamp(m_dOxygenReading, 0.0, 100.0);
    // ORIGINAL BUG: the level reading is drawn with the temperature error range (+0x98), not the level one (+0xc0)
    // fix: the probe has its own level error range (+0xc0, m_dLevelError) which nothing else uses
    m_dLevelReading = oc.waterVol() / oc.volume() * 100.0 +
                      rng.randPlusMinus(fixes().oceanProbeLevelNoise ? m_dLevelError : m_dTempError);
    clamp(m_dLevelReading, 0.0, 100.0);
    u32 prev = m_dwAlarms;
    m_dwAlarms = 0;
    if (m_dTempReading < 23.0) m_dwAlarms = 0x20000;
    if (26.0 < m_dTempReading) m_dwAlarms |= 0x10000;
    if (m_dPHReading < 8.3) m_dwAlarms |= 0x20;
    if (8.6 < m_dPHReading) m_dwAlarms |= 0x10;
    if (m_dSalinityReading < 3.5) m_dwAlarms |= 0x100000;
    if (4.2 < m_dSalinityReading) m_dwAlarms |= 0x80000;
    if (m_dOxygenReading < 3.5) m_dwAlarms |= 0x800;
    if (m_dClarityReading < 30.0) m_dwAlarms |= 0x40000;
    if (m_dLevelReading < 80.0) m_dwAlarms |= 0x400000;
    if (99.0 < m_dLevelReading) m_dwAlarms |= 0x200000;
    if (m_dwAlarms != 0) {
        if (prev == 0) {
            cworld::startPageFX(world(), 0x7fe);
            return;
        }
        if (m_dwAlarms != 0) return;
    }
    if (prev != 0) cworld::stopPageFX(world(), 0x7fe);
}

void CTankSensor::members(MemberList& out) const {
    CFeature::members(out);
    putP(out, "m_pTank", m_pTank);
    putD(out, "m_dTempReading", m_dTempReading);
    putD(out, "m_dPurityReading", m_dPurityReading);
    putD(out, "m_dLevelReading", m_dLevelReading);
    putD(out, "m_dTempError", m_dTempError);
    putD(out, "m_dPurityError", m_dPurityError);
    putD(out, "m_dLevelError", m_dLevelError);
    putX(out, "m_dwAlarms", m_dwAlarms);
}

void CBiomeSensor::members(MemberList& out) const {
    CFeature::members(out);
    putP(out, "m_pCel", m_pCel);
    putS(out, "m_szFile", m_szFile);
    putD(out, "m_dPressureReading", m_dPressureReading);
    putD(out, "m_dAirTempReading", m_dAirTempReading);
    putD(out, "m_dCO2Reading", m_dCO2Reading);
    putD(out, "m_dHumidityReading", m_dHumidityReading);
    putD(out, "m_dLightReading", m_dLightReading);
    putD(out, "m_dOxygenReading", m_dOxygenReading);
    putD(out, "m_dPollutionReading", m_dPollutionReading);
    putD(out, "m_dSoilTempReading", m_dSoilTempReading);
    putD(out, "m_dSoilPHReading", m_dSoilPHReading);
    putD(out, "m_dSoilWaterReading", m_dSoilWaterReading);
    putD(out, "m_dWindSpeedReading", m_dWindSpeedReading);
    putD(out, "m_dRainToday", m_dRainToday);
    putD(out, "m_dRainYesterday", m_dRainYesterday);
    putD(out, "m_dPressureError", m_dPressureError);
    putD(out, "m_dAirTempError", m_dAirTempError);
    putD(out, "m_dCO2Error", m_dCO2Error);
    putD(out, "m_dHumidityError", m_dHumidityError);
    putD(out, "m_dLightError", m_dLightError);
    putD(out, "m_dOxygenError", m_dOxygenError);
    putD(out, "m_dPollutionError", m_dPollutionError);
    putD(out, "m_dSoilTempError", m_dSoilTempError);
    putD(out, "m_dSoilPHError", m_dSoilPHError);
    putD(out, "m_dSoilWaterError", m_dSoilWaterError);
    putD(out, "m_dWindSpeedError", m_dWindSpeedError);
    for (int i = 0; i < 10; i++) putD(out, (std::string("m_dMin[") + std::to_string(i) + "]").c_str(), m_dMin[i]);
    for (int i = 0; i < 10; i++) putD(out, (std::string("m_dMax[") + std::to_string(i) + "]").c_str(), m_dMax[i]);
    putX(out, "m_dwThresholdAddr", m_dwThresholdAddr);
    putX(out, "m_dwAlarms", m_dwAlarms);
}

void CLung::members(MemberList& out) const {
    CBiomeSensor::members(out);
    putP(out, "m_pTargetCel", m_pTargetCel);
    putI(out, "m_dwMode", m_dwMode);
    putI(out, "m_wId2", m_wId2);
    putI(out, "m_dwBellowsDir", m_dwBellowsDir);
}

void COceanProbe::members(MemberList& out) const {
    CFeature::members(out);
    putS(out, "m_szFile", m_szFile);
    putD(out, "m_dTempReading", m_dTempReading);
    putD(out, "m_dPHReading", m_dPHReading);
    putD(out, "m_dSalinityReading", m_dSalinityReading);
    putD(out, "m_dClarityReading", m_dClarityReading);
    putD(out, "m_dOxygenReading", m_dOxygenReading);
    putD(out, "m_dLevelReading", m_dLevelReading);
    putD(out, "m_dTempError", m_dTempError);
    putD(out, "m_dPHError", m_dPHError);
    putD(out, "m_dSalinityError", m_dSalinityError);
    putD(out, "m_dClarityError", m_dClarityError);
    putD(out, "m_dOxygenError", m_dOxygenError);
    putD(out, "m_dLevelError", m_dLevelError);
    putX(out, "m_dwAlarms", m_dwAlarms);
}

BIOSYS_REGISTER_CTOR(100262d0, [](World& w, const char* name, uint32_t a1, int32_t a2, Item* a3, uint16_t a4) -> Item* {
    return w.adopt(std::make_unique<CTankSensor>(w, name, a1, a2, &as<CWaterTank>(a3), a4));
});
BIOSYS_REGISTER_CTOR(1005d300, [](World& w, const char* name, uint32_t a1, uint32_t a2, Item* a3, uint16_t a4,
                                  const char* a5, DllPtr a6, double a7) -> Item* {
    return w.adopt(std::make_unique<CBiomeSensor>(w, name, a1, a2, &as<CBiomeCel>(a3), a4, a5, a6, a7));
});
BIOSYS_REGISTER_CTOR(1000fe10, [](World& w, const char* name, uint32_t a1, uint32_t a2, uint16_t a3, uint16_t a4,
                                  int32_t a5, int32_t a6, DllPtr a7) -> Item* {
    return w.adopt(std::make_unique<CLung>(w, name, a1, a2, a3, a4, a5, a6, a7));
});
BIOSYS_REGISTER_CTOR(10027610, [](World& w, const char* name, uint32_t a1, int32_t a2, const char* a3) -> Item* {
    return w.adopt(std::make_unique<COceanProbe>(w, name, a1, a2, a3));
});

} // namespace biosys::game
