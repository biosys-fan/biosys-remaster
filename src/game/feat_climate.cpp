#include "game/feat_climate.h"
#include "game/biomecel.h"
#include "game/watertank.h"
#include "game/airflow.h"
#include "game/site.h"
#include "game/eng.h"
#include "game/feat_sensors.h"
#include <strings.h>
#include "game/world_ctors.gen.h"

namespace biosys::game {

// ---------------------------------------------------------------------------------------------
// CHeater

// dll:10016a40
CHeater::CHeater(World& w, const char* name, u32 a1, u32 a2, u16 id, Biome* biome, double dayOn, double dayOffArg,
                 double unused1, double nightOn, double nightOffArg, double unused2, double powerUsage, i32 working,
                 i32 on)
    : CFeature(w, 0x10016a40, "CHeater", name, a1 | 0x203, a2 | 0x1000) {
    (void)unused1;
    (void)unused2;
    m_wId = id;                         // +0x18
    m_pBiome = biome;                   // +0x58
    m_dDayOff = dayOffArg + 2.0;        // +0x68
    m_dDayOn = dayOn;                   // +0x60
    m_dNightOn = nightOn;               // +0x70
    m_dNightOff = nightOffArg + 2.0;    // +0x78
    m_bOn = on;                         // +0x1c
    m_bWorking = working;               // +0x30
    m_dMalfunctionProb = 0;             // +0x40
    m_dPowerUsage = powerUsage;         // +0x48
}

void CHeater::saveSlot(SlotWriter& w) const {   // dll:10016ce0
    saveBase(w);
    w.writeF64(m_dDayOn);
    w.writeF64(m_dDayOff);
    w.writeF64(m_dNightOn);
    w.writeF64(m_dNightOff);
}

void CHeater::loadSlot(SlotReader& r) {         // dll:10016d50
    loadBase(r);
    m_dDayOn = r.readF64("m_dDayOn");
    m_dDayOff = r.readF64("m_dDayOff");
    m_dNightOn = r.readF64("m_dNightOn");
    m_dNightOff = r.readF64("m_dNightOff");
    r.finishPad();
}

// ---------------------------------------------------------------------------------------------
// CRainMachine

// dll:10025dc0
CRainMachine::CRainMachine(World& w, const char* name, u32 a1, u32 a2, const char* osmTank, const char* condTank,
                           u16 id)
    : CFeature(w, 0x10025dc0, "CRainMachine", name, a1 | 0x200, a2 | 4) {
    m_pCondTank = w.findWaterTank(condTank);
    if (!m_pCondTank)
        throw Error(std::string("BiosGame.dll: CRainMachine::CRainMachine m_pCondTank = NULL, tank name ") + condTank);
    m_pOsmTank = w.findWaterTank(osmTank);
    if (!m_pOsmTank)
        throw Error(std::string("BiosGame.dll: CRainMachine::CRainMachine m_pOsmTank = NULL, tank name ") + osmTank);
    m_dPowerUsage = dblBits(0x789abcdf, 0x3f423456);   // +0x48
    m_wId = id;                         // +0x18
    m_pSourceTank = m_pOsmTank;         // +0x58
    m_dwTimerStart = 36000;             // +0x24 10:00
    m_dwTimerEnd = 0xc4e0;              // +0x28 14:00
    m_dMalfunctionProb = 0;             // +0x40
}

void CRainMachine::saveSlot(SlotWriter& w) const {   // dll:10025fd0
    saveBase(w);
    i32 code = 0x60;
    if (m_pSourceTank != m_pOsmTank) code = 0x61;
    w.writeI32(code);
}

void CRainMachine::loadSlot(SlotReader& r) {         // dll:10026020
    loadBase(r);
    i32 code = r.readI32("m_dwSourceTankCode");
    setParam(0x5f, code, 0.0);           // virtual +0x18 of the original
    r.finishPad();
}

// dll:10025f10 CRainMachine::SetParam: +0x58 = (value == 0x60 ? clean tank : condensate tank), then
// CWorld::SetWaterFlowsSource (dll:10014200) points every water flow with this machine's id at that tank.
bool CRainMachine::setParam(i32 id, i32 value, double) {
    if (id != 0x5f) return baseSetParam(id, value);
    m_pSourceTank = value == 0x60 ? m_pOsmTank : m_pCondTank;
    world().setWaterFlowsSource(m_wId, m_pSourceTank);
    return true;
}
// dll:10025f80
bool CRainMachine::getParam(i32 id, i32* pi, double* pd) const {
    if (id != 0x5f) return baseGetParam(id, pi, pd);
    *pi = m_pSourceTank == m_pOsmTank ? 0x60 : 0x61;
    return true;
}
// dll:10026190: the "bActive" hotspot toggles the machine (and sets a 24 h window from now: start 0, end 0x15180)
bool CRainMachine::onHotspot(const std::string& hotspot) {
    if (strcasecmp(hotspot.c_str(), "bActive") == 0) {
        if (m_bOn == 0 || m_bActive == 0) {
            playSound("sounds\\features\\rainon.wav");
            m_dwTimerStart = 0;
            m_dwTimerEnd = 0x15180;
            turnOn();
        } else {
            playSound("sounds\\features\\rainoff.wav");
            turnOff();
        }
    }
    display(1);
    return true;
}
// dll:10026060 CRainMachine::display
void CRainMachine::display(int redraw) {
    if ((m_dwFlags & 8) == 0) {
        imgVis("hActive", m_bActive == 0 ? 0 : 1);
    } else {
        siteStateImages();
        imgVis("active", 0);
        imgVis("hActive", 0);
        imgVis("uActive", 0);
        imgVis("dActive", 0);
        if (m_bActive != 0 && !imgVis("active", 1)) {       // no plain "active" image on the page: the one of the site's state
            switch (host() && host()->currentSite() ? host()->currentSite()->state() : 0) {
            case 1: imgVis("dActive", 1); break;
            case 2: imgVis("hActive", 1); break;
            case 3: case 4: imgVis("uActive", 1); break;
            }
        }
    }
    if (redraw != 0) redrawPage();
}
// dll:1002a740 CMister::display (the same without the plain "active" image)
void CMister::display(int redraw) {
    if ((m_dwFlags & 8) == 0) {
        imgVis("hActive", m_bActive == 0 ? 0 : 1);
    } else {
        siteStateImages();
        imgVis("hActive", 0);
        imgVis("uActive", 0);
        imgVis("dActive", 0);
        if (m_bActive != 0) {
            switch (host() && host()->currentSite() ? host()->currentSite()->state() : 0) {
            case 1: imgVis("dActive", 1); break;
            case 2: imgVis("hActive", 1); break;
            case 3: case 4: imgVis("uActive", 1); break;
            }
        }
    }
    if (redraw != 0) redrawPage();
}
// dll:1002a180 CAirMixer::display: the start / stop indicators (plain, or the variants of the site's state)
void CAirMixer::display(int redraw) {
    siteStateImages();
    for (const char* n : {"stopin", "hstopin", "ustopin", "dstopin", "startin", "hstartin", "ustartin", "dstartin"}) imgVis(n, 0);
    const int state = host() && host()->currentSite() ? host()->currentSite()->state() : 0;
    if (m_bActive == 0) {
        if (!imgVis("stopin", 1)) {
            switch (state) {
            case 1: imgVis("dStopIn", 1); break;
            case 2: imgVis("hStopIn", 1); break;
            case 3: case 4: imgVis("uStopIn", 1); break;
            }
        }
        hotspotEnable("bStart", 1);
        hotspotEnable("bStop", 0);
    } else {
        if (!imgVis("startin", 1)) {
            switch (state) {
            case 1: imgVis("dStartIn", 1); break;
            case 2: imgVis("hStartIn", 1); break;
            case 3: case 4: imgVis("uStartIn", 1); break;
            }
        }
        hotspotEnable("bStart", 0);
        hotspotEnable("bStop", 1);
    }
    if (redraw != 0) redrawPage();
}
// dll:10026210: the base timer, then off when the source tank is (nearly) empty
void CRainMachine::update(i32 now) {
    CFeature::update(now);
    if (m_bActive != 0 && m_pSourceTank->waterVol() < 0.01) deactivate();
}
// dll:10026160: an empty source tank is "no water" (0xbf)
void CRainMachine::statusMessage(i32 id) {
    if (m_pSourceTank->waterVol() < 0.01) { CFeature::statusMessage(0xbf); return; }
    CFeature::statusMessage(id);
}
// dll:10026250 / 10026280: the rain flows (CRain, same id) and the page FX of the id
void CRainMachine::startRunning() {
    CFeature::startRunning();
    cworld::switchWaterFlows(world(), static_cast<i16>(m_wId), 1);
    cworld::startPageFX(world(), static_cast<i16>(m_wId));
}
void CRainMachine::stopRunning() {
    CFeature::stopRunning();
    cworld::switchWaterFlows(world(), static_cast<i16>(m_wId), 0);
    cworld::stopPageFX(world(), m_wId);
    if (strcasecmp(name().c_str(), "fR1Rain") == 0 && host()) host()->triggerEvent(1);   // CGame::TriggerEvent(&story, 1): R4 floods
}

// ---------------------------------------------------------------------------------------------
// CMister

// dll:1002a480
CMister::CMister(World& w, const char* name, u32 a1, u32 a2, const char* sensor, const char* osmTank,
                 const char* condTank, u16 id)
    : CFeature(w, 0x1002a480, "CMister", name, a1 | 0x200, a2 | 2) {
    m_pSensor = w.findFeature(sensor);      // the original logs "m_pSensor = NULL" and goes on with NULL
    m_pCondTank = w.findWaterTank(condTank);
    if (!m_pCondTank)
        throw Error(std::string("BiosGame.dll: CRainMachine::CRainMachine m_pCondTank = NULL, tank name ") + condTank);
    m_pOsmTank = w.findWaterTank(osmTank);
    if (!m_pOsmTank)
        throw Error(std::string("BiosGame.dll: CRainMachine::CRainMachine m_pOsmTank = NULL, tank name ") + osmTank);
    m_pSourceTank = m_pOsmTank;             // +0x60
    m_dHumidityOn = dblBits(0, 0x40518000);     // +0x70 = 70.0
    m_dMalfunctionProb = 0;                 // +0x40
    m_wId = id;                             // +0x18
    m_bMisting = 0;                         // +0x58
    m_dHumidityOff = dblBits(0, 0x40568000);    // +0x78 = 90.0
    m_dPowerUsage = dblBits(0x789abcdf, 0x3f223456);   // +0x48
    m_bOn = 1;                              // +0x1c
}

// ---------------------------------------------------------------------------------------------
// CAirMixer

// dll:10029e80
CAirMixer::CAirMixer(World& w, const char* name, u32 a1, u32 a2, u16 id, const char* cel1, const char* cel2,
                     const char* cel3)
    : CFeature(w, 0x10029e80, "CAirMixer", name, a1 | 0x200, a2 | 0x2000) {
    m_wId = id;                             // +0x18
    const char* names[3] = {cel1, cel2, cel3};
    for (int i = 0; i < 3; i++) {           // +0x58, +0x5c, +0x60
        m_pCel[i] = nullptr;
        if (!names[i]) continue;
        m_pCel[i] = w.findBiomeCel(names[i]);
        if (!m_pCel[i])
            throw Error(std::string("BiosGame.dll: CAirMixer::CAirMixer pCel = NULL, cel name ") + names[i]);
    }
    m_dPowerUsage = dblBits(0x789abcdf, 0x3f423456);   // +0x48
    m_dwTimerStart = 0x5460;                // +0x24 06:00
    m_dwTimerEnd = 0xfd20;                  // +0x28 18:00
    m_dMalfunctionProb = 0;                 // +0x40
}

// ---------------------------------------------------------------------------------------------
// CFan

// dll:100174d0
CFan::CFan(World& w, const char* name, u32 a1, u32 a2, u16 airFlowId)
    : CFeature(w, 0x100174d0, "CFan", name, a1 | 0x200, a2 | 0x800) {
    m_dMalfunctionProb = 0;                 // +0x40
    m_dPowerUsage = dblBits(0x16c16c17, 0x3f56c16c);   // +0x48
    m_dwTimeLimit = 0xa8c0;                 // +0x2c 43200 s
    m_bOn = 1;                              // +0x1c
    m_wAirFlowId = airFlowId;               // +0x58
}

void CFan::saveSlot(SlotWriter& w) const { saveBase(w); }   // dll:10017600 (CFeature::Save only)

void CFan::loadSlot(SlotReader& r) {                        // dll:10017620 (CFeature::Load only)
    loadBase(r);
    r.finishPad();
}

// ---------------------------------------------------------------------------------------------
// CVent

// dll:10062750
CVent::CVent(World& w, const char* name, i32 a1)
    : CFeature(w, 0x10062750, "CVent", name, static_cast<u32>(a1), 0x80000000) {
    m_dShownAirFlow = 0;                    // +0x58 (zeroed as two dwords)
    m_dAirFlowTarget = 0;                   // +0x60
}

void CVent::saveSlot(SlotWriter& w) const {   // dll:10062880
    saveBase(w);
    w.writeF64(m_dShownAirFlow);
    w.writeF64(m_dAirFlowTarget);
}

void CVent::loadSlot(SlotReader& r) {         // dll:100628c0
    loadBase(r);
    m_dShownAirFlow = r.readF64("m_dShownAirFlow");
    m_dAirFlowTarget = r.readF64("m_dAirFlowTarget");
    r.finishPad();
}

// ---------------------------------------------------------------------------------------------
// CHeater behaviour

bool CHeater::setParam(i32 id, i32 value, double d) {    // dll:10016ba0
    switch (id) {
    case 100: m_dDayOn = d; return true;       // FEATURE_DAY_ACTIVATE_TEMP
    case 0x65: m_dDayOff = d; return true;     // FEATURE_DAY_DEACTIVATE_TEMP
    case 0x66: m_dNightOn = d; return true;    // FEATURE_NIGHT_ACTIVATE_TEMP
    case 0x67: m_dNightOff = d; return true;   // FEATURE_NIGHT_DEACTIVATE_TEMP
    default: return baseSetParam(id, value);
    }
}
bool CHeater::getParam(i32 id, i32* pi, double* pd) const {   // dll:10016c40
    switch (id) {
    case 100: *pd = m_dDayOn; return true;
    case 0x65: *pd = m_dDayOff; return true;
    case 0x66: *pd = m_dNightOn; return true;
    case 0x67: *pd = m_dNightOff; return true;
    default: return baseGetParam(id, pi, pd);
    }
}

// dll:100173c0 CHeater::Thermostat: the night set-points apply in dawn, dusk and night; the input is the mean air
// temperature the biome's probes read; switches on below "on" and off above "off"
void CHeater::thermostat() {
    i32 phase = world().state().sim.diurnal;     // DAT_100c1c44
    double on, off;
    if (phase == 5 || phase == 0x23 || phase == 0x28) { on = m_dNightOn; off = m_dNightOff; }
    else { on = m_dDayOn; off = m_dDayOff; }
    double avg = biomeAvgProbeAirTemp(*m_pBiome);
    if (m_bActive == 0) {
        if (avg < on) activate();
    } else if (off < avg) {
        deactivate();
    }
}

// dll:10017330 CHeater::Update: while on, working and running every cel of the biome gets 8 degrees per hour
void CHeater::update(i32 now) {
    i32 last = m_dwTimeLastUpdate;
    m_dwTimeLastUpdate = now;
    if (m_bOn != 0 && m_bWorking != 0) {
        thermostat();
        if (m_bRunning != 0) {
            double dt = static_cast<double>(static_cast<u32>(now - last));
            for (Item* it : m_pBiome->cels) {
                CBiomeCel& cel = as<CBiomeCel>(it);
                cel.setNewAirTemp(cel.newAirTemp() + dt * 0.0022222222222222222);
                cel.setAirTemp(cel.newAirTemp());     // the current value too (+0x38 = +0xa0)
            }
        }
    }
}

// dll:10017220 CHeater::OnClick: the hotspot names carry "DAY"/"NIGHT", "ON"/"OFF" and "UP"/"DOWN"; one click moves a
// set-point by 0.5 inside -10..40 and the "on" point may not pass the "off" point
bool CHeater::onHotspot(const std::string& hotspot) {
    playSound("sounds\\features\\click.wav");
    double lo = -10.0, hi = 40.0;
    double* target;
    const bool day = hotspot.find("DAY") != std::string::npos;
    const bool onPoint = hotspot.find("ON") != std::string::npos;
    if (day) {
        if (onPoint) { target = &m_dDayOn; hi = m_dDayOff; }
        else { target = &m_dDayOff; lo = m_dDayOn; }
    } else {
        if (onPoint) { target = &m_dNightOn; hi = m_dNightOff; }
        else { target = &m_dNightOff; lo = m_dNightOn; }
    }
    *target = hotspot.find("UP") != std::string::npos ? *target + 0.5 : *target - 0.5;
    if (*target < lo) *target = lo;
    else if (hi < *target) *target = hi;
    thermostat();
    display(1);
    return true;
}

// ---------------------------------------------------------------------------------------------
// CMister behaviour

bool CMister::setParam(i32 id, i32 value, double d) {   // dll:1002a620
    if (id == 0x5f) {
        m_pSourceTank = value == 0x60 ? m_pOsmTank : m_pCondTank;
        world().setWaterFlowsSource(m_wId, m_pSourceTank);
        return true;
    }
    if (id == 0x6e) { m_dHumidityOn = d; return true; }
    if (id == 0x6f) { m_dHumidityOff = d; return true; }
    return baseSetParam(id, value);
}
bool CMister::getParam(i32 id, i32* pi, double* pd) const {   // dll:1002a6c0
    if (id == 0x5f) { *pi = m_pOsmTank == m_pSourceTank ? 0x60 : 0x61; return true; }
    if (id == 0x6e) { *pd = m_dHumidityOn; return true; }
    if (id == 0x6f) { *pd = m_dHumidityOff; return true; }
    return baseGetParam(id, pi, pd);
}
// dll:1002a840: the "bActive" hotspot; switching on sets the switch-off humidity (+0x78) to 100 %
bool CMister::onHotspot(const std::string& hotspot) {
    if (strcasecmp(hotspot.c_str(), "bActive") == 0) {
        if (m_bOn == 0 || m_bActive == 0) {
            playSound("sounds\\features\\rainon.wav");
            m_dHumidityOff = 100.0;               // *(+0x78) = 0, *(+0x7c) = 0x40590000
            activate();
        } else {
            playSound("sounds\\features\\rainoff.wav");
            deactivate();
        }
    }
    display(1);
    return true;
}
// dll:1002a8c0 CMister::Update: a humidistat on the probe's humidity reading (no base update: no timer)
void CMister::update(i32 now) {
    m_dwTimeLastUpdate = now;
    if (m_bOn == 0) return;
    double humidity = as<CBiomeSensor>(m_pSensor).humidityReading();     // sensor +0x80
    if (m_bMisting == 0) {
        if (humidity < m_dHumidityOn && 0.01 < m_pSourceTank->waterVol()) {
            activate();
            if (isCurrentFeature()) display(1);
        }
    } else if (m_dHumidityOff < humidity || m_pSourceTank->waterVol() < 0.01) {
        deactivate();
        if (isCurrentFeature()) display(1);
    }
}
void CMister::statusMessage(i32 id) {   // dll:1002a810
    if (m_pSourceTank->waterVol() < 0.01) { CFeature::statusMessage(0xbf); return; }
    CFeature::statusMessage(id);
}
// dll:1002a950 / 1002a9b0
void CMister::startRunning() {
    CFeature::startRunning();
    m_bMisting = 1;
    cworld::switchWaterFlows(world(), static_cast<i16>(m_wId), 1);
    cworld::startPageFX(world(), static_cast<i16>(m_wId));
    cworld::setFXState(world(), static_cast<i16>(m_wId), 1);
    if (isCurrentFeature()) host()->showMessage(0x7b);
}
void CMister::stopRunning() {
    CFeature::stopRunning();
    m_bMisting = 0;
    cworld::switchWaterFlows(world(), static_cast<i16>(m_wId), 0);
    cworld::stopPageFX(world(), m_wId);
    cworld::setFXState(world(), static_cast<i16>(m_wId), 0);
}

// ---------------------------------------------------------------------------------------------
// CAirMixer behaviour

// dll:1002a0a0: the "bStart" / "bStop" hotspots (a window of 24 h from now); without power the click only says so
bool CAirMixer::onHotspot(const std::string& hotspot) {
    if (cworld::hasPower(world(), m_dwPowerMask) == 0) {
        playSound("sounds\\features\\click.wav");
        if (host()) host()->showMessage(0xbe);
        return true;
    }
    if (strcasecmp(hotspot.c_str(), "bStart") == 0 && m_bActive == 0) {
        playSound("sounds\\features\\fanon.wav");
        m_dwTimerStart = 0;
        m_dwTimerEnd = 0x15180;
        turnOn();
    } else if (strcasecmp(hotspot.c_str(), "bStop") == 0 && m_bActive != 0) {
        playSound("sounds\\features\\fanoff.wav");
        turnOff();
    }
    display(1);
    return true;
}
// dll:1002a350 / 1002a3c0: the mixer stirs its cels: +2 wind speed each while it runs
void CAirMixer::startRunning() {
    CFeature::startRunning();
    for (int i = 0; i < 3; i++)
        if (m_pCel[i]) m_pCel[i]->setWindSpeed(m_pCel[i]->windSpeed() + 2.0);
    cworld::setFXState(world(), static_cast<i16>(m_wId), 1);
    if (isCurrentFeature()) host()->showMessage(0xc5);
}
void CAirMixer::stopRunning() {
    CFeature::stopRunning();
    for (int i = 0; i < 3; i++)
        if (m_pCel[i]) m_pCel[i]->setWindSpeed(m_pCel[i]->windSpeed() - 2.0);
    cworld::setFXState(world(), static_cast<i16>(m_wId), 0);
}

// ---------------------------------------------------------------------------------------------
// CFan behaviour

// dll:10017590: FEATURE_AIR_FLOW reads 2.0 while the fan is active, working and powered
bool CFan::getParam(i32 id, i32* pi, double* pd) const {
    if (id != 0x50) return baseGetParam(id, pi, pd);
    if (m_bActive != 0 && m_bWorking != 0 && cworld::hasPower(const_cast<World&>(world()), m_dwPowerMask) != 0) {
        *pd = 2.0;
        return true;
    }
    *pd = 0.0;
    return true;
}
// dll:10017650: "bStart" runs the fan for a day (time limit 86400 s), "bStop" stops it
bool CFan::onHotspot(const std::string& hotspot) {
    if (cworld::hasPower(world(), m_dwPowerMask) == 0) {
        playSound("sounds\\features\\click.wav");
        if (host()) host()->showMessage(0xbe);
        return true;
    }
    if (strcasecmp(hotspot.c_str(), "bStart") == 0 && m_bActive == 0) {
        m_dwTimeLimit = 0x15180;
        playSound("sounds\\features\\fanon.wav");
        activate();
    } else if (strcasecmp(hotspot.c_str(), "bStop") == 0 && m_bActive != 0) {
        playSound("sounds\\features\\fanoff.wav");
        deactivate();
    }
    display(1);
    return true;
}
// dll:10017720: the start / stop buttons and their indicator images
void CFan::display(int redraw) {
    const bool idle = m_bActive == 0;
    imgVis("startin", idle ? 0 : 1);
    imgVis("stopin", idle ? 1 : 0);
    hotspotEnable("bStart", idle ? 1 : 0);
    hotspotEnable("bStop", idle ? 0 : 1);
    if (redraw != 0) redrawPage();
}
// dll:100177d0 / 10017830: the fan switches the air flows of its id (the soil-bed loop) and the FX of the same id
void CFan::startRunning() {
    CFeature::startRunning();
    cworld::startPageFX(world(), static_cast<i16>(m_wAirFlowId));
    cworld::setFXState(world(), static_cast<i16>(m_wAirFlowId), 1);
    cworld::switchAirFlows(world(), static_cast<i16>(m_wAirFlowId), 1);
    if (isCurrentFeature()) host()->showMessage(2);
}
void CFan::stopRunning() {
    CFeature::stopRunning();
    cworld::stopPageFX(world(), m_wAirFlowId);
    cworld::setFXState(world(), static_cast<i16>(m_wAirFlowId), 0);
    cworld::switchAirFlows(world(), static_cast<i16>(m_wAirFlowId), 0);
}

// ---------------------------------------------------------------------------------------------
// CVent behaviour

bool CVent::getParam(i32 id, i32* pi, double* pd) const {   // dll:10062850
    if (id != 0x50) return baseGetParam(id, pi, pd);
    *pd = m_dShownAirFlow;
    return true;
}
// dll:10062940 CVent::Update: the shown airflow follows the target at 1/360 per game second
void CVent::update(i32 now) {
    i32 last = m_dwTimeLastUpdate;
    m_dwTimeLastUpdate = now;
    if (m_dShownAirFlow != m_dAirFlowTarget) {
        double step = static_cast<double>(static_cast<u32>(now - last)) * 0.002777777777777778;
        double diff = m_dAirFlowTarget - m_dShownAirFlow;
        if ((diff < 0 ? -diff : diff) <= step) {
            m_dShownAirFlow = m_dAirFlowTarget;
            return;
        }
        if (m_dAirFlowTarget <= m_dShownAirFlow) {
            m_dShownAirFlow = m_dShownAirFlow - step;
            return;
        }
        m_dShownAirFlow = m_dShownAirFlow + step;
    }
}

void CHeater::members(MemberList& out) const {
    CFeature::members(out);
    putS(out, "m_pBiome", m_pBiome ? std::string(m_pBiome->name) : std::string("null"));
    putD(out, "m_dDayOn", m_dDayOn);
    putD(out, "m_dDayOff", m_dDayOff);
    putD(out, "m_dNightOn", m_dNightOn);
    putD(out, "m_dNightOff", m_dNightOff);
}

void CRainMachine::members(MemberList& out) const {
    CFeature::members(out);
    putP(out, "m_pSourceTank", m_pSourceTank);
    putP(out, "m_pCondTank", m_pCondTank);
    putP(out, "m_pOsmTank", m_pOsmTank);
}

void CMister::members(MemberList& out) const {
    CFeature::members(out);
    putI(out, "m_bMisting", m_bMisting);
    putP(out, "m_pSensor", m_pSensor);
    putP(out, "m_pSourceTank", m_pSourceTank);
    putP(out, "m_pCondTank", m_pCondTank);
    putP(out, "m_pOsmTank", m_pOsmTank);
    putD(out, "m_dHumidityOn", m_dHumidityOn);
    putD(out, "m_dHumidityOff", m_dHumidityOff);
}

void CAirMixer::members(MemberList& out) const {
    CFeature::members(out);
    putP(out, "m_pCel[0]", m_pCel[0]);
    putP(out, "m_pCel[1]", m_pCel[1]);
    putP(out, "m_pCel[2]", m_pCel[2]);
}

void CFan::members(MemberList& out) const {
    CFeature::members(out);
    putI(out, "m_wAirFlowId", m_wAirFlowId);
}

void CVent::members(MemberList& out) const {
    CFeature::members(out);
    putD(out, "m_dShownAirFlow", m_dShownAirFlow);
    putD(out, "m_dAirFlowTarget", m_dAirFlowTarget);
}

BIOSYS_REGISTER_CTOR(10016a40, [](World& w, const char* name, uint32_t a1, uint32_t a2, uint16_t a3, Biome* a4,
                                  double a5, double a6, double a7, double a8, double a9, double a10, double a11,
                                  int32_t a12, int32_t a13) -> Item* {
    return w.adopt(std::make_unique<CHeater>(w, name, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13));
});
BIOSYS_REGISTER_CTOR(10025dc0, [](World& w, const char* name, uint32_t a1, uint32_t a2, const char* a3,
                                  const char* a4, uint16_t a5) -> Item* {
    return w.adopt(std::make_unique<CRainMachine>(w, name, a1, a2, a3, a4, a5));
});
BIOSYS_REGISTER_CTOR(1002a480, [](World& w, const char* name, uint32_t a1, uint32_t a2, const char* a3,
                                  const char* a4, const char* a5, uint16_t a6) -> Item* {
    return w.adopt(std::make_unique<CMister>(w, name, a1, a2, a3, a4, a5, a6));
});
BIOSYS_REGISTER_CTOR(10029e80, [](World& w, const char* name, uint32_t a1, uint32_t a2, uint16_t a3, const char* a4,
                                  const char* a5, const char* a6) -> Item* {
    return w.adopt(std::make_unique<CAirMixer>(w, name, a1, a2, a3, a4, a5, a6));
});
BIOSYS_REGISTER_CTOR(100174d0, [](World& w, const char* name, uint32_t a1, uint32_t a2, uint16_t a3) -> Item* {
    return w.adopt(std::make_unique<CFan>(w, name, a1, a2, a3));
});
BIOSYS_REGISTER_CTOR(10062750, [](World& w, const char* name, int32_t a1) -> Item* {
    return w.adopt(std::make_unique<CVent>(w, name, a1));
});

} // namespace biosys::game
