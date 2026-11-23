#include "game/feat_water.h"
#include "game/biomecel.h"
#include "game/waterflow.h"
#include "game/watertank.h"
#include "game/eng.h"
#include "game/feat_hostext.h"
#include "game/gameobj.h"
#include "game/self.h"
#include <cctype>
#include <cstdio>
#include "game/world_ctors.gen.h"
#include <strings.h>

namespace biosys::game {

// ---------------------------------------------------------------------------------------------
// CWaterPump

// dll:10005a10 + CFeature__Init
CWaterPump::CWaterPump(World& w, u32 ctorAddr, const char* className, const char* name, u32 flags, u32 powerMask)
    : CFeature(w, ctorAddr, className, name, flags, powerMask) {}

// dll:10005ae0
CWaterPump::CWaterPump(World& w, const char* name, i32 a1, u32 a2, u16 id, i32 srcCode, i32 dstCode,
                       CWaterFlow* flow)
    : CFeature(w, 0x10005ae0, "CWaterPump", name, static_cast<u32>(a1), a2 | 0x40) {
    unk_58 = 0;                         // +0x58
    m_wId = id;                         // +0x18
    m_dMalfunctionProb = 0;             // +0x40
    m_dPowerUsage = dblBits(0x11111111, 0x3f611111);   // +0x48
    m_pFlow = flow;                     // +0x68
    setParam(0x13, srcCode, 0.0);
    setParam(0x14, dstCode, 0.0);
    m_bOn = 1;                          // +0x1c
}

// dll:10005bb0 CWaterPump::SetParam: ids 0x13 (source) and 0x14 (destination); the code is also kept in +0x64
bool CWaterPump::setParam(i32 id, i32 value, double) {
    const char* tank = nullptr;
    if (id == 0x13) {
        // fix: GetParam(0x14) reports the destination code, so a set destination keeps its code (the original overwrites it)
        if (!(fixes().pumpParamShared && m_pDstTank)) m_dwParam = value;              // +0x64
        switch (value) {
        case 0x15: m_pSrcTank = world().findWaterTank("RFTANK"); return true;
        case 0x16: m_pSrcTank = world().findWaterTank("STANK"); return true;
        case 0x17: m_pSrcTank = world().findWaterTank("DBTANK"); return true;
        case 0x18: m_pSrcTank = world().findWaterTank("OTANK"); return true;
        }
        // an unknown source code falls through to the destination code below (as in the decompilation)
    } else if (id != 0x14) {
        return baseSetParam(id, value);
    }
    // ORIGINAL BUG: +0x64 is the shared member of both ids; setting the destination overwrites the source code the constructor
    // stored, so FEATURE_DESTINATION (0x14) reads the last code of either id
    m_dwParam = value;                  // +0x64
    switch (value) {
    case 0x15: tank = "RFTANK"; break;
    case 0x16: tank = "STANK"; break;
    case 0x17: tank = "DBTANK"; break;
    case 0x18: tank = "OTANK"; break;
    default: return false;
    }
    m_pDstTank = world().findWaterTank(tank);   // +0x60
    // flow+0x08 = +0x60: the pump's flow now ends in the destination tank (the original dereferences the flow)
    if (!m_pFlow) throw Error(className() + " '" + name() + "': SetParam 0x14 with no flow (the original dereferences it)");
    m_pFlow->setTo(m_pDstTank);
    return true;
}

// dll:10005cf0 CWaterPump::GetParam: 0x14 the last code, 0x2b the destination tank's fill in percent
bool CWaterPump::getParam(i32 id, i32* pi, double* pd) const {
    if (id == 0x14) { *pi = m_dwParam; return true; }
    if (id != 0x2b) return baseGetParam(id, pi, pd);
    if (!m_pDstTank) { *pd = 0; return true; }
    *pd = m_pDstTank->waterVol() / m_pDstTank->volume() * 100.0;
    return true;
}
// dll:10005db0: "b_on" toggles: with the master switch on it activates, otherwise it deactivates
bool CWaterPump::onHotspot(const std::string& hotspot) {
    if (strcasecmp(hotspot.c_str(), "b_on") == 0) {
        if (m_bOn != 0) {
            activate();
            return true;
        }
        deactivate();
    }
    return true;
}
// dll:10005df0 CWaterPump::Update: stops when the source is empty or the destination full
void CWaterPump::update(i32) {
    if (m_bActive != 0 && (m_pSrcTank->waterVol() <= 0.0 || m_pDstTank->volume() <= m_pDstTank->waterVol())) deactivate();
}
// dll:10005e20 / 10005e50: the page FX of the id and the pump's CWaterFlow (CWaterFlow::TurnOn, vtable +0x20)
void CWaterPump::startRunning() {
    CFeature::startRunning();
    cworld::startPageFX(world(), static_cast<i16>(m_wId));
    m_pFlow->turnOn(1);
}
void CWaterPump::stopRunning() {
    CFeature::stopRunning();
    cworld::stopPageFX(world(), m_wId);
    m_pFlow->turnOn(0);
}

void CWaterPump::saveSlot(SlotWriter& w) const { saveBase(w); }   // dll:10005d60

void CWaterPump::loadSlot(SlotReader& r) {                        // dll:10005d80
    loadBase(r);
    r.finishPad();
}

// ---------------------------------------------------------------------------------------------
// CPurifier

// dll:10023370
CPurifier::CPurifier(World& w, const char* name, u32 a1, i32 a2, u16 id, double purity, const char* src,
                     const char* dst)
    : CWaterPump(w, 0x10023370, "CPurifier", name, a1 | 0x200, static_cast<u32>(a2)) {
    m_wId = id;                         // +0x18
    m_dPurity = purity;                 // +0x70
    m_dPowerUsage = dblBits(0x789abcdf, 0x3f323456);   // +0x48
    m_dwTimerStart = 0xb6d0;            // +0x24 13:00
    m_dwTimerEnd = 0xc4e0;              // +0x28 14:00
    m_pSrcTank = w.findWaterTank(src); // +0x5c
    m_pDstTank = w.findWaterTank(dst); // +0x60
    m_bOn = 1;                          // +0x1c
}

// ---------------------------------------------------------------------------------------------

// dll:10023510 CPurifier::GetParam: 0x2b / 0x2c the destination tank's level and purity (percent), 0x32 an alarm bit when the
// destination is cleaner by less than the target - 0.1
bool CPurifier::getParam(i32 id, i32* pi, double* pd) const {
    if (id == 0x2b) { *pd = m_pDstTank->waterVol() / m_pDstTank->volume() * 100.0; return true; }
    if (id == 0x2c) { *pd = m_pDstTank->purity() * 100.0; return true; }
    if (id == 0x32) { *pi = m_pDstTank->purity() < m_dPurity - 0.1 ? 1 : 0; return true; }
    return baseGetParam(id, pi, pd);
}
// dll:100235a0 / 100235d0: the FX and the water flows of the machine's id
void CPurifier::startRunning() {
    CFeature::startRunning();
    cworld::setFXState(world(), static_cast<i16>(m_wId), 1);
    cworld::switchWaterFlows(world(), static_cast<i16>(m_wId), 1);
}
void CPurifier::stopRunning() {
    CFeature::stopRunning();
    cworld::setFXState(world(), static_cast<i16>(m_wId), 0);
    cworld::switchWaterFlows(world(), static_cast<i16>(m_wId), 0);
}
// dll:10023600 CPurifier::Break: the flows pass the source purity again (override -1); dll:10023620 repairs (the machine's purity)
void CPurifier::breakDown() { cworld::setWaterFlowsPurity(world(), static_cast<i16>(m_wId), -1.0); }
void CPurifier::repair() { cworld::setWaterFlowsPurity(world(), static_cast<i16>(m_wId), m_dPurity); }

// ---------------------------------------------------------------------------------------------

// dll:10005fa0 CSiltScrubber::Update: while active and powered the drain flow's free-flow factor recovers by 1 per 12000 s
void CSiltScrubber::update(i32 now) {
    i32 last = m_dwTimeLastUpdate;
    CFeature::update(now);
    if (m_bActive != 0 && cworld::hasPower(world(), m_dwPowerMask) != 0) {
        m_pFlow->setFreeFlow(m_pFlow->freeFlow() + static_cast<double>(static_cast<u32>(now - last)) * 8.333333333333333e-05);
        if (1.0 < m_pFlow->freeFlow()) m_pFlow->setFreeFlow(1.0);
    }
}

// dll:1002ae70 CDrainGauge::GetParam: 0x32 alarm when more than half clogged, 0x51 the flow, 0x52 the silt level in percent
bool CDrainGauge::getParam(i32 id, i32* pi, double* pd) const {
    if (id == 0x32) { *pi = m_pFlow->freeFlow() < 0.5 ? 1 : 0; return true; }
    if (id == 0x51) {
        const CWaterTank& src = *m_pFlow->from();
        *pd = src.waterVol() / src.volume() * m_pFlow->freeFlow() * m_pFlow->mag() * 400.0;
        return true;
    }
    if (id == 0x52) { *pd = (1.0 - m_pFlow->freeFlow()) * 100.0; return true; }
    return baseGetParam(id, pi, pd);
}

// dll:10005e80
CSiltScrubber::CSiltScrubber(World& w, const char* name, i32 a1, u32 a2, CWaterFlow* flow)
    : CFeature(w, 0x10005e80, "CSiltScrubber", name, static_cast<u32>(a1), a2 | 1) {
    m_dPowerUsage = dblBits(0x789abcdf, 0x3f323456);   // +0x48
    m_dwTimerStart = 0xb6d0;            // +0x24
    m_dwTimerEnd = 0xc4e0;              // +0x28
    m_pFlow = flow;                     // +0x58
}

// dll:1002ad70
CDrainGauge::CDrainGauge(World& w, const char* name, i32 a1, i32 a2, CWaterFlow* flow)
    : CFeature(w, 0x1002ad70, "CDrainGauge", name, static_cast<u32>(a1), static_cast<u32>(a2)) {
    m_pFlow = flow;                     // +0x58
}

// dll:10004620
CTanksCabinet::CTanksCabinet(World& w, const char* name, u32 a1, i32 a2, const char* t1, const char* t2,
                             const char* t3, const char* t4)
    : CFeature(w, 0x10004620, "CTanksCabinet", name, a1 | 3, static_cast<u32>(a2)) {
    m_pTank[0] = w.findWaterTank(t1);  // +0x58
    m_pTank[1] = w.findWaterTank(t2);  // +0x5c
    m_pTank[2] = w.findWaterTank(t3);  // +0x60
    m_pTank[3] = w.findWaterTank(t4);  // +0x64
}

// The water the cabinet hands out, the same in dll:100056c0, dll:10005810 and dll:100058e0: 1/18 of the capacity of the last tank
// (+0x64) leaves it (not below 0) and, when the cabinet has a first tank (+0x58), arrives there (not above its capacity).
// PORT: these four handlers (vtable +0x30, +0x34, +0x3c, +0x44) were missing until 2026-11-22: no drink from the tap, no container
// filled at it, no shower.
void CTanksCabinet::takeWater() {
    CWaterTank* last = m_pTank[3];
    CWaterTank* first = m_pTank[0];
    last->setWaterVol(last->waterVol() - last->volume() * 0.05555555555555555);
    if (last->waterVol() < 0.0) last->setWaterVol(0.0);
    if (first) {
        first->setWaterVol(last->volume() * 0.05555555555555555 + first->waterVol());
        if (first->volume() < first->waterVol()) first->setWaterVol(first->volume());
    }
}

// dll:100056c0 (vtable +0x30; the hotspot's name is not looked at): a drink from the tap. With more than 1/36 of the last tank's
// capacity in it the player drinks (code 0xbe) unless his hydration is above 99 (message 0xc1); with less, message 0xbf.
bool CTanksCabinet::onHotspot(const std::string&) {
    CWaterTank* last = m_pTank[3];
    if (!last) return false;
    FeatureHost* h = host();
    FeatureHostExt* x = hostExt(h);
    Self* me = x ? x->player() : nullptr;
    if (last->volume() * 0.027777777777777776 < last->waterVol()) {
        if (me && 99.0 < me->m_dHydration) {                      // _DAT_100c1d38: CSelf+0
            if (h) h->showMessage(0xc1);
            return true;
        }
        char wav[64];
        std::snprintf(wav, sizeof wav, "sounds\\russell\\drinking\\drink%d.wav", world().state().rng.rand() % 4 + 1);
        if (h && h->eng()) h->eng()->api().PlayObjectUseAnimation("anims\\obj_use\\water.tan", wav, true, false, 200, 0xda, 1, 0x42, "", -2);
        if (me) me->Drink(0xbe);
        takeWater();
        display(1);
        return true;
    }
    if (h) h->showMessage(0xbf);
    return true;
}

// dll:10004760 (vtable +0x34; replaces CFeature::OnEnterPage): on a page whose name ends in 'S' (the shower; the name as it comes,
// not upper-cased here) the image "fph6mir_b" is shown while the feature "fph6mir" is active (+0x34 != 0).
void CTanksCabinet::onEnterPage(const std::string& page) {
    if (page.empty() || page.back() != 'S') return;
    CFeature* mir = world().findFeature("fph6mir");
    if (mir && mir->active()) imgVis("fph6mir_b", 1);
}

// dll:100058e0 (vtable +0x3c; replaces CFeature::OnPageEntered): the shower. The page's name is upper-cased (CharUpperA on a copy);
// when it ends in 'S': with no more than 1/36 of the last tank's capacity in it message 0xbf, else the water filter over the page
// (Eng_BeginPageFilter(0x76, 0x56, 0x57)), the FX 0xfdc (the shower's sound), one of four sighs, and the water is taken.
// PORT: the original does not test the tank pointer here; the port skips the handler without a last tank.
void CTanksCabinet::onPageEntered(const std::string& page) {
    if (page.empty() || std::toupper(static_cast<unsigned char>(page.back())) != 'S') return;
    CWaterTank* last = m_pTank[3];
    if (!last) return;
    FeatureHost* h = host();
    if (last->waterVol() <= last->volume() * 0.027777777777777776) {
        if (h) h->showMessage(0xbf);
        return;
    }
    if (h && h->eng()) h->eng()->BeginPageFilter(0x76, 0x56, 0x57);
    cworld::setFXState(world(), 0xfdc, 1);
    if (h) h->playRandomSound("sounds\\russell\\sighs\\sigh", 4);    // FUN_10060820
    takeWater();
}

// dll:10005810 (vtable +0x44): an object used on the cabinet is filled with water: its vtable +0x60 (CConsumable::Fill) with code
// 0xbe and amount -1 (full), the water is taken and the display redrawn; with too little water message 0xbf.
void CTanksCabinet::onApply(GameObj& object, const std::string&) {
    CWaterTank* last = m_pTank[3];
    if (!last) return;
    FeatureHost* h = host();
    if (last->volume() * 0.027777777777777776 < last->waterVol()) {
        FeatureHostExt* x = hostExt(h);
        if (x && x->player()) object.fill(*x->player(), 0xbe, -1);
        takeWater();
        display(1);
        return;
    }
    if (h) h->showMessage(0xbf);
}

// ---------------------------------------------------------------------------------------------

// dll:10003d40
CTideMachine::CTideMachine(World& w, const char* name, u32 a1, u32 a2)
    : CFeature(w, 0x10003d40, "CTideMachine", name, a1 | 0x602, a2 | 0x100) {
    m_pTideTank = w.findWaterTank("TIDE");
    m_dPowerUsage = dblBits(0x789abcdf, 0x3f523456);   // +0x48
    m_dwLastToggle = 0;                 // +0x5c
    m_dwDirection = 0;                  // +0x60
    m_bOn = 1;                          // +0x1c
    m_wId = 0xc1d;                      // +0x18
}

// dll:10003e80 CTideMachine::GetParam: 0x2b the tide tank's level, 0x96 the direction
bool CTideMachine::getParam(i32 id, i32* pi, double* pd) const {
    if (id == 0x2b) { *pd = m_pTideTank->waterVol() / m_pTideTank->volume() * 100.0; return true; }
    if (id == 0x96) { *pi = m_dwDirection; return true; }
    return baseGetParam(id, pi, pd);
}
// dll:10003f40: the hotspot toggles active / inactive
bool CTideMachine::onHotspot(const std::string&) {
    if (m_bActive == 0) activate(); else deactivate();
    display(1);
    return true;
}
// dll:10004330 CTideMachine::Update: every 20700 s of running (5 h 45 min) the OCEAN->TIDE flows reverse; when the tide is
// back out (tank below 0.01) they reverse again. No base update: the tide has no daily timer.
void CTideMachine::update(i32 now) {
    i32 last = m_dwTimeLastUpdate;
    m_dwTimeLastUpdate = now;
    if (m_bActive != 0 && cworld::hasPower(world(), m_dwPowerMask) != 0) {
        if (m_dwDirection == 0) {
            u32 t = static_cast<u32>(m_dwLastToggle) + static_cast<u32>(now - last);
            m_dwLastToggle = static_cast<i32>(t);
            if (20700.0 < static_cast<double>(t)) {
                m_dwDirection = 1;
                m_dwLastToggle = 0;
                cworld::toggleWaterFlowsReversed(world(), static_cast<i16>(m_wId));
            }
        } else if (m_pTideTank->waterVol() < 0.01) {
            m_dwDirection = 0;
            m_dwLastToggle = 0;
            cworld::toggleWaterFlowsReversed(world(), static_cast<i16>(m_wId));
        }
    }
}
// dll:100042b0
void CTideMachine::slot60(const char*, int) {
    Eng* e = host() ? host()->eng() : nullptr;
    if (!e) return;
    if (m_bActive == 0) {
        e->DisplayImage("tideoff", -1, nullptr, 0, -1, nullptr, 0);
        imgVis("tideon", 0);
        imgVis("tideoff", 1);
        return;
    }
    e->DisplayImage("tideon", -1, nullptr, 0, -1, nullptr, 0);
    imgVis("tideon", 1);
    imgVis("tideoff", 0);
}
// dll:100043f0 / 10004440: the tide sound (when the player is at the machine), the FX and the tide flows
void CTideMachine::startRunning() {
    CFeature::startRunning();
    if (isCurrentFeature()) playSound("sounds\\features\\tidemch1.wav");
    cworld::setFXState(world(), static_cast<i16>(m_wId), 1);
    cworld::switchWaterFlows(world(), static_cast<i16>(m_wId), 1);
}
void CTideMachine::stopRunning() {
    CFeature::stopRunning();
    if (isCurrentFeature()) playSound("sounds\\features\\tidemch1.wav");
    cworld::setFXState(world(), static_cast<i16>(m_wId), 0);
    cworld::switchWaterFlows(world(), static_cast<i16>(m_wId), 0);
}

// ---------------------------------------------------------------------------------------------
// CWaveMachine / CAlgaeScrubber behaviour

// dll:10070b90: the "bActive" hotspot toggles the machine
bool CWaveMachine::onHotspot(const std::string& hotspot) {
    playSound("sounds\\features\\clikon5.wav");
    if (strcasecmp(hotspot.c_str(), "bActive") == 0) {
        if (m_bActive == 0) activate(); else deactivate();
    }
    display(1);
    return true;
}
// dll:10070ab0
void CWaveMachine::display(int redraw) {
    const bool idle = m_bActive == 0;
    imgVis("on", idle ? 0 : 1);
    imgVis("off", idle ? 1 : 0);
    if (redraw != 0) redrawPage();
}
// dll:10070b10
void CWaveMachine::slot60(const char*, int) {
    Eng* e = host() ? host()->eng() : nullptr;
    if (!e) return;
    if (m_bActive == 0) {
        e->DisplayImage("waveoff", -1, nullptr, 0, -1, nullptr, 0);
        imgVis("waveon", 0);
        imgVis("waveoff", 1);
        return;
    }
    e->DisplayImage("waveon", -1, nullptr, 0, -1, nullptr, 0);
    imgVis("waveon", 1);
    imgVis("waveoff", 0);
}
// dll:10070be0 / 10070c10: the FX 0xc1c and the ocean's wave-machine flag (COcean +0x80)
void CWaveMachine::startRunning() {
    CFeature::startRunning();
    cworld::setFXState(world(), 0xc1c, 1);
    cworld::ocean(world()).setWaveMachineOn(1);
}
void CWaveMachine::stopRunning() {
    CFeature::stopRunning();
    cworld::setFXState(world(), 0xc1c, 0);
    cworld::ocean(world()).setWaveMachineOn(0);
}

// dll:10003a80: any hotspot sets a 24 h window from now; "bStart" turns on, anything else off
bool CAlgaeScrubber::onHotspot(const std::string& hotspot) {
    playSound("sounds\\features\\click.wav");
    m_dwTimerStart = 0;
    m_dwTimerEnd = 0x15180;
    if (strcasecmp(hotspot.c_str(), "bStart") == 0) turnOn(); else turnOff();
    display(1);
    return true;
}
// dll:10003ae0 CAlgaeScrubber::Update: while active and powered the ocean's algae shrink by 3 per hour
void CAlgaeScrubber::update(i32 now) {
    i32 last = m_dwTimeLastUpdate;
    CFeature::update(now);
    if (m_bActive != 0 && cworld::hasPower(world(), m_dwPowerMask) != 0) {
        COcean& oc = cworld::ocean(world());
        oc.setAlgae(oc.algae() + static_cast<double>(static_cast<u32>(now - last)) * -0.0008333333333333334);
        if (oc.algae() < 0.0) oc.setAlgae(0.0);
    }
}
// dll:10003b60 / 10003b80
void CAlgaeScrubber::startRunning() {
    CFeature::startRunning();
    cworld::setFXState(world(), static_cast<i16>(m_wId), 1);
}
void CAlgaeScrubber::stopRunning() {
    CFeature::stopRunning();
    cworld::setFXState(world(), static_cast<i16>(m_wId), 0);
}

// ---------------------------------------------------------------------------------------------
// COxygenStore behaviour. Mode 1 compresses the TO cel's oxygen into the store, mode 2 releases it (store 0..10).

// dll:100254b0: id 2 (FEATURE_ACTIVE) switches on in the compress mode, off otherwise
bool COxygenStore::setParam(i32 id, i32 value, double) {
    if (id != 2) return baseSetParam(id, value);
    if (value == 0) { deactivate(); return true; }
    m_wMode = 1;
    activate();
    return true;
}
// dll:10025450: 0x22 the cel's new oxygen (+0x50 = the current value, in %), 0x78 the store x 10
bool COxygenStore::getParam(i32 id, i32* pi, double* pd) const {
    if (id == 0x22) { *pd = m_pCel->oxygen(); return true; }
    if (id == 0x78) { *pd = m_dStore * 10.0; return true; }
    return baseGetParam(id, pi, pd);
}
// dll:10025a70: "bSiphIn" compresses, "bSiphOut" releases, any other button stops
bool COxygenStore::onHotspot(const std::string& hotspot) {
    playSound("sounds\\features\\click.wav");
    if (strcasecmp(hotspot.c_str(), "bSiphIn") == 0) {
        if (m_bActive != 0) deactivate();
        m_wMode = 1;
        activate();
    } else if (strcasecmp(hotspot.c_str(), "bSiphOut") == 0) {
        if (m_bActive != 0) deactivate();
        m_wMode = 2;
        activate();
    } else {
        deactivate();
    }
    display(1);
    return true;
}
// dll:10025bc0 (vtable +0x44): refreshes the page when this is the player's feature
void COxygenStore::onApply(GameObj&, const std::string&) {
    if (isCurrentFeature()) display(1);
}
// dll:10025bf0 COxygenStore::Update (no base update). While running: mode 1 takes dt/360 of oxygen out of the TO cel (when the
// cel is at least 16 % oxygen and 0.1 inflated) into the store until it holds 10; mode 2 gives it back.
void COxygenStore::update(i32 now) {
    u32 dt = static_cast<u32>(now) - static_cast<u32>(m_dwTimeLastUpdate);
    m_dwTimeLastUpdate = now;
    if (m_bRunning != 0) {
        CBiomeCel& cel = *m_pCel;
        if (m_wMode == 1) {
            if (0.1 <= cel.airCapacity()) {
                if (m_dStore < 10.0) {
                    if (16.0 <= cel.oxygen()) {
                        double d = static_cast<double>(dt) * 0.002777777777777778;
                        cel.setNewOxygen(cel.newOxygen() - d / (cel.airVolume() / cel.airCapacity()));
                        m_dStore = d + m_dStore;
                    } else {
                        turnOff();
                        if (isCurrentFeature()) display(1);
                    }
                } else {
                    m_dStore = 10.0;
                    turnOff();
                    if (isCurrentFeature()) display(1);
                }
            } else {
                turnOff();
                if (isCurrentFeature()) display(1);
            }
        } else if (m_wMode == 2) {
            double d = static_cast<double>(dt) * 0.002777777777777778;
            cel.setNewOxygen(cel.newOxygen() + d);
            m_dStore = m_dStore - d;
            if (m_dStore <= 0.0) {
                m_dStore = 0.0;
                turnOff();
            }
        }
    }
    if (m_dStore < 0.0) m_dStore = 0.0; else if (10.0 < m_dStore) m_dStore = 10.0;
}
// dll:10025b10 / 10025b60
void COxygenStore::startRunning() {
    CFeature::startRunning();
    if (m_bRunning == 0) return;
    if (m_wMode == 1) cworld::setFXState(world(), 0xce5, 1);
    else if (m_wMode == 2) cworld::setFXState(world(), 0xce6, 1);
    else { cworld::setFXState(world(), 0xce4, 1); return; }
    cworld::setFXState(world(), 0xce4, 1);
}
void COxygenStore::stopRunning() {
    CFeature::stopRunning();
    if (isCurrentFeature()) playSound("sounds\\features\\airthump.wav");
    if (m_wMode == 1) cworld::setFXState(world(), 0xce5, 0);
    else if (m_wMode == 2) cworld::setFXState(world(), 0xce6, 0);
}

// ---------------------------------------------------------------------------------------------
// CBubbler behaviour. Gas source: air (flow id 0xc1f) or the stored oxygen (0xc20, from the COxygenStore).

// dll:100778f0: id 0x5a: 0x5b = oxygen, otherwise air; a running bubbler is stopped and started around the change
bool CBubbler::setParam(i32 id, i32 value, double) {
    if (id != 0x5a) return baseSetParam(id, value);
    if (m_bRunning == 0) {
        m_wGasSource = value == 0x5b ? 0xc20 : 0xc1f;
        return true;
    }
    stopRunning();
    m_wGasSource = value == 0x5b ? 0xc20 : 0xc1f;
    startRunning();
    return true;
}
// dll:10077980: 0x5a the gas source (0x5b oxygen / 0x5c air), 0x78 the oxygen store x 10
bool CBubbler::getParam(i32 id, i32* pi, double* pd) const {
    if (id == 0x5a) { *pi = m_wGasSource == 0xc20 ? 0x5b : 0x5c; return true; }
    if (id == 0x78) { *pd = as<COxygenStore>(m_pOxygenStore).store() * 10.0; return true; }
    return baseGetParam(id, pi, pd);
}
// dll:10077b30: "bStart" / "bStop", and "bOxygen" / "bAir" choose the gas (oxygen needs at least 1/24 in the store)
bool CBubbler::onHotspot(const std::string& hotspot) {
    if (strcasecmp(hotspot.c_str(), "bStart") == 0 && m_bActive == 0) {
        playSound("sounds\\features\\click.wav");
        m_dwTimerStart = 0;
        m_dwTimerEnd = 0x15180;
        turnOn();
    } else if (strcasecmp(hotspot.c_str(), "bStop") == 0 && m_bActive != 0) {
        playSound("sounds\\features\\click.wav");
        turnOff();
    } else if (strcasecmp(hotspot.c_str(), "bOxygen") == 0 || strcasecmp(hotspot.c_str(), "bAir") == 0) {
        playSound("sounds\\features\\powlever.wav");
        i32 running = m_bRunning;
        bool handled = false;
        if (m_wGasSource == 0xc1f) {
            if (running != 0) stopRunning();
            m_wGasSource = 0xc20;
            if (as<COxygenStore>(m_pOxygenStore).store() < 0.041666666666666664) {
                if (host()) host()->showMessage(0xc4);
                turnOff();
                handled = true;
            }
        } else {
            if (running != 0) stopRunning();
            m_wGasSource = 0xc1f;
        }
        if (!handled && running != 0) startRunning();
    }
    display(1);
    return true;
}
// dll:10077a40 CBubbler::display: stop / start light, and the air / oxygen lever
void CBubbler::display(int redraw) {
    Eng* e = host() ? host()->eng() : nullptr;
    auto show = [&](const char* img) { if (e) e->DisplayImage(img, -1, nullptr, 0, -1, nullptr, 0); };
    const bool idle = m_bActive == 0;
    show(idle ? "stopin" : "startin");
    hotspotEnable("bStart", idle ? 1 : 0);
    hotspotEnable("bStop", idle ? 0 : 1);
    const bool air = m_wGasSource != 0xc20;
    show(air ? "air" : "oxygen");
    hotspotEnable("bOxygen", air ? 1 : 0);
    hotspotEnable("bAir", air ? 0 : 1);
    if (redraw != 0) redrawPage();
}
// dll:10077d10 CBubbler::Update: with the oxygen source and power, each update moves a = dt/7200 of the store into the ocean
// (temperature) and the O1 cel (heat, humidity, oxygen); the ocean's oxygen moves toward 5
void CBubbler::update(i32 now) {
    i32 last = m_dwTimeLastUpdate;
    CFeature::update(now);
    if (m_bActive != 0 && cworld::hasPower(world(), m_dwPowerMask) != 0 && m_wGasSource == 0xc20) {
        double a = static_cast<double>(static_cast<u32>(now - last)) * 0.0001388888888888889;
        COxygenStore& store = as<COxygenStore>(m_pOxygenStore);
        if (store.store() < a) {
            turnOff();
            return;
        }
        COcean& oc = cworld::ocean(world());
        oc.setNewTemp(a * 20.0 / oc.waterVol() * 0.25 + oc.newTemp());
        CBiomeCel& cel = *m_pCel;
        cel.setNewAirTemp(oc.temp() * a / cel.airVolume() + cel.newAirTemp());
        cel.setNewHumidity(a * 100.0 / cel.airVolume() + cel.newHumidity());
        cel.setNewOxygen(a * 0.1 + cel.newOxygen());
        store.setStore(store.store() - a);
        oc.setNewOxygen((5.0 - oc.oxygen()) * static_cast<double>(static_cast<u32>(now - last)) * 0.0002777777777777778 + oc.newOxygen());
    }
}
// dll:100779f0: with the oxygen source and an empty store the message is "no oxygen" (0xc4)
void CBubbler::statusMessage(i32 id) {
    if (m_wGasSource == 0xc20 && as<COxygenStore>(m_pOxygenStore).store() < 0.041666666666666664) {
        CFeature::statusMessage(0xc4);
        return;
    }
    CFeature::statusMessage(id);
}
// dll:10077c80 / 10077cd0: the bubble FX 0xc1e; the air source also switches the bubble air flows 0xc1f
void CBubbler::startRunning() {
    CFeature::startRunning();
    if (isCurrentFeature()) playSound("sounds\\features\\oxybubb1.wav");
    cworld::setFXState(world(), 0xc1e, 1);
    if (m_wGasSource == 0xc1f) cworld::switchAirFlows(world(), 0xc1f, 1);
}
void CBubbler::stopRunning() {
    CFeature::stopRunning();
    cworld::setFXState(world(), 0xc1e, 0);
    if (m_wGasSource == 0xc1f) cworld::switchAirFlows(world(), 0xc1f, 0);
}

void CTideMachine::saveSlot(SlotWriter& w) const {   // dll:10003ee0
    saveBase(w);
    w.writeI32(m_dwLastToggle);
    w.writeI32(m_dwDirection);
}

void CTideMachine::loadSlot(SlotReader& r) {         // dll:10003f10
    loadBase(r);
    m_dwLastToggle = r.readI32("unk_5c");
    m_dwDirection = r.readI32("unk_60");
    r.finishPad();
}

// dll:10070990
CWaveMachine::CWaveMachine(World& w, const char* name, u32 a1, u32 a2)
    : CFeature(w, 0x10070990, "CWaveMachine", name, a1 | 0x600, a2 | 0x200) {
    m_dPowerUsage = dblBits(0xb4e81b4f, 0x3f4b4e81);   // +0x48
    m_bOn = 1;                          // +0x1c
    m_wId = 0xc1c;                      // +0x18
}

// dll:100036c0
CAlgaeScrubber::CAlgaeScrubber(World& w, const char* name, u32 a1, u32 a2)
    : CFeature(w, 0x100036c0, "CAlgaeScrubber", name, a1 | 0x203, a2 | 0x80) {
    m_dwTimerStart = 0xb6d0;            // +0x24 13:00
    m_dwTimerEnd = 0xc4e0;              // +0x28 14:00
    m_dPowerUsage = dblBits(0x789abcdf, 0x3f423456);   // +0x48
    m_wId = 0xd16;                      // +0x18
}

// dll:10025320
COxygenStore::COxygenStore(World& w, const char* name, u32 a1, u32 a2)
    : CFeature(w, 0x10025320, "COxygenStore", name, a1 | 3, a2 | 0x40000) {
    m_dPowerUsage = dblBits(0x789abcdf, 0x3f423456);   // +0x48
    m_pCel = w.findBiomeCel("TO");     // +0x58
    m_dStore = 0;                       // +0x60
    m_wMode = 1;                        // +0x68
}

void COxygenStore::saveSlot(SlotWriter& w) const {   // dll:10025500
    saveBase(w);
    w.writeU16(m_wMode);
    w.writeF64(m_dStore);
}

void COxygenStore::loadSlot(SlotReader& r) {         // dll:10025540
    loadBase(r);
    m_wMode = r.readU16("unk_68");
    m_dStore = r.readF64("m_dStore");
    r.finishPad();
}

// dll:10077750
CBubbler::CBubbler(World& w, const char* name, u32 a1, u32 a2, i32 a3)
    : CFeature(w, 0x10077750, "CBubbler", name, a1 | 2, a2 | 0x20000) {
    m_dPowerUsage = dblBits(0x789abcdf, 0x3f223456);   // +0x48
    m_wGasSource = 0xc1f;               // +0x58
    if (a3 != 0) throw Error("CBubbler: argument 4 is a pointer in the original and always 0");
    m_pOxygenStore = nullptr;           // +0x5c
    m_pCel = w.findBiomeCel("O1");     // +0x60
    m_dwTimerStart = 0x5460;            // +0x24 06:00
    m_dwTimerEnd = 0xfd20;              // +0x28 18:00
}

void CBubbler::saveSlot(SlotWriter& w) const {   // dll:10077890
    saveBase(w);
    w.writeU16(m_wGasSource);
}

void CBubbler::loadSlot(SlotReader& r) {         // dll:100778c0
    loadBase(r);
    m_wGasSource = r.readU16("m_wGasSource");
    r.finishPad();
}

void CWaterPump::members(MemberList& out) const {
    CFeature::members(out);
    putI(out, "unk_58", unk_58);
    putP(out, "m_pSrcTank", m_pSrcTank);
    putP(out, "m_pDstTank", m_pDstTank);
    putI(out, "m_dwParam", m_dwParam);
    putP(out, "m_pFlow", m_pFlow);
}

void CPurifier::members(MemberList& out) const {
    CWaterPump::members(out);
    putD(out, "m_dPurity", m_dPurity);
}

void CSiltScrubber::members(MemberList& out) const {
    CFeature::members(out);
    putP(out, "m_pFlow", m_pFlow);
}

void CDrainGauge::members(MemberList& out) const {
    CFeature::members(out);
    putP(out, "m_pFlow", m_pFlow);
}

void CTanksCabinet::members(MemberList& out) const {
    CFeature::members(out);
    putP(out, "m_pTank[0]", m_pTank[0]);
    putP(out, "m_pTank[1]", m_pTank[1]);
    putP(out, "m_pTank[2]", m_pTank[2]);
    putP(out, "m_pTank[3]", m_pTank[3]);
}

void CTideMachine::members(MemberList& out) const {
    CFeature::members(out);
    putP(out, "m_pTideTank", m_pTideTank);
    putI(out, "m_dwLastToggle", m_dwLastToggle);
    putI(out, "m_dwDirection", m_dwDirection);
}

void CWaveMachine::members(MemberList& out) const {
    CFeature::members(out);
}

void CAlgaeScrubber::members(MemberList& out) const {
    CFeature::members(out);
}

void COxygenStore::members(MemberList& out) const {
    CFeature::members(out);
    putP(out, "m_pCel", m_pCel);
    putD(out, "m_dStore", m_dStore);
    putI(out, "m_wMode", m_wMode);
}

void CBubbler::members(MemberList& out) const {
    CFeature::members(out);
    putI(out, "m_wGasSource", m_wGasSource);
    putP(out, "m_pOxygenStore", m_pOxygenStore);
    putP(out, "m_pCel", m_pCel);
}

BIOSYS_REGISTER_CTOR(10005ae0, [](World& w, const char* name, int32_t a1, uint32_t a2, uint16_t a3, int32_t a4,
                                  int32_t a5, Item* a6) -> Item* {
    return w.adopt(std::make_unique<CWaterPump>(w, name, a1, a2, a3, a4, a5, &as<CWaterFlow>(a6)));
});
BIOSYS_REGISTER_CTOR(10023370, [](World& w, const char* name, uint32_t a1, int32_t a2, uint16_t a3, double a4,
                                  const char* a5, const char* a6) -> Item* {
    return w.adopt(std::make_unique<CPurifier>(w, name, a1, a2, a3, a4, a5, a6));
});
BIOSYS_REGISTER_CTOR(10005e80, [](World& w, const char* name, int32_t a1, uint32_t a2, Item* a3) -> Item* {
    return w.adopt(std::make_unique<CSiltScrubber>(w, name, a1, a2, &as<CWaterFlow>(a3)));
});
BIOSYS_REGISTER_CTOR(1002ad70, [](World& w, const char* name, int32_t a1, int32_t a2, Item* a3) -> Item* {
    return w.adopt(std::make_unique<CDrainGauge>(w, name, a1, a2, &as<CWaterFlow>(a3)));
});
BIOSYS_REGISTER_CTOR(10004620, [](World& w, const char* name, uint32_t a1, int32_t a2, const char* a3,
                                  const char* a4, const char* a5, const char* a6) -> Item* {
    return w.adopt(std::make_unique<CTanksCabinet>(w, name, a1, a2, a3, a4, a5, a6));
});
BIOSYS_REGISTER_CTOR(10003d40, [](World& w, const char* name, uint32_t a1, uint32_t a2) -> Item* {
    return w.adopt(std::make_unique<CTideMachine>(w, name, a1, a2));
});
BIOSYS_REGISTER_CTOR(10070990, [](World& w, const char* name, uint32_t a1, uint32_t a2) -> Item* {
    return w.adopt(std::make_unique<CWaveMachine>(w, name, a1, a2));
});
BIOSYS_REGISTER_CTOR(100036c0, [](World& w, const char* name, uint32_t a1, uint32_t a2) -> Item* {
    return w.adopt(std::make_unique<CAlgaeScrubber>(w, name, a1, a2));
});
BIOSYS_REGISTER_CTOR(10025320, [](World& w, const char* name, uint32_t a1, uint32_t a2) -> Item* {
    return w.adopt(std::make_unique<COxygenStore>(w, name, a1, a2));
});
BIOSYS_REGISTER_CTOR(10077750, [](World& w, const char* name, uint32_t a1, uint32_t a2, int32_t a3) -> Item* {
    return w.adopt(std::make_unique<CBubbler>(w, name, a1, a2, a3));
});

} // namespace biosys::game
