#include "game/feat_power.h"
#include "game/blobrecords.h"
#include "game/eng.h"
#include "game/gameobj.h"
#include "game/obj_consumable.h"
#include "game/site.h"
#include "game/world_ctors.gen.h"

namespace biosys::game {

// dll:10003480
CSolarGen::CSolarGen(World& w, const char* name, i32 a1)
    : CFeature(w, 0x10003480, "CSolarGen", name, static_cast<u32>(a1), 0x80000000) {
    m_dMalfunctionProb = 0;             // +0x40
    m_dPowerUsage = 0;                  // +0x48
    m_bOn = 1;                          // +0x1c
}

// dll:10070c40
CWindmill::CWindmill(World& w, const char* name, i32 a1)
    : CFeature(w, 0x10070c40, "CWindmill", name, static_cast<u32>(a1), 0x80000000) {
    m_dMalfunctionProb = 0;             // +0x40
    m_dPowerUsage = 0;                  // +0x48
    m_bOn = 1;                          // +0x1c
    m_wId = 0xdac;                      // +0x18
}

// dll:10026b30
CFuseBox::CFuseBox(World& w, const char* name, u32 a1, const char* site, i32 a3, i32 a4)
    : CFeature(w, 0x10026b30, "CFuseBox", name, a1 | 0x8000440, static_cast<u32>(a3)) {
    m_pSite = w.findSite(site);         // the original logs "m_pSite = NULL for %s" and goes on with NULL
    unk_5c = a4;                        // +0x5c
    unk_60 = 0;                         // +0x60
    unk_64 = 0;                         // +0x64
}

// ---------------------------------------------------------------------------------------------------------------
// CSolarGen

// dll:10003590 CSolarGen::GetParam: FEATURE_POWER_GENERATION (per hour); 0 while not active
bool CSolarGen::getParam(i32 id, i32* pi, double* pd) const {
    if (id != 0x8c) return baseGetParam(id, pi, pd);
    *pd = m_bActive == 0 ? 0.0 : m_dPowerUsage * 3600.0;
    return true;
}
// dll:10003670 CSolarGen::Update: the generation follows the sunlight (0.0277778 = 1/36 per s); the world gets the difference
void CSolarGen::update(i32 now) {
    m_dwTimeLastUpdate = now;
    if (m_bActive != 0) {
        double g = cworld::header(world()).m_dSunlight * 0.027777777777777776 * world().state().tuning.powerGeneration;   // Tuning::powerGeneration
        cworld::addPowerGeneration(world(), g - m_dPowerUsage);
        m_dPowerUsage = g;
    }
}
// dll:100035f0: active; the base's power check does not apply (the generator needs no circuit)
void CSolarGen::activate() {
    m_bActive = 1;
    if (m_bWorking != 0) startRunning();
}
// dll:10003610: the generation is taken off the world again
void CSolarGen::deactivate() {
    m_bActive = 0;
    if (m_bWorking != 0) {
        cworld::removePowerGeneration(world(), m_dPowerUsage);
        m_dPowerUsage = 0;
    }
    stopRunning();
}

// ---------------------------------------------------------------------------------------------------------------
// CWindmill

bool CWindmill::getParam(i32 id, i32* pi, double* pd) const {   // dll:10070d50
    if (id != 0x8c) return baseGetParam(id, pi, pd);
    *pd = m_bActive == 0 ? 0.0 : m_dPowerUsage * 3600.0;
    return true;
}
// dll:10070e50 CWindmill::Update: 5 units per hour per unit of wind speed, at most 100 per second
void CWindmill::update(i32 now) {
    m_dwTimeLastUpdate = now;
    if (m_bActive != 0) {
        double g = cworld::header(world()).m_dWindSpeed * 0.001388888888888889 * world().state().tuning.powerGeneration;
        if (100.0 < g) g = 100.0;
        cworld::addPowerGeneration(world(), g - m_dPowerUsage);
        m_dPowerUsage = g;
    }
}
void CWindmill::activate() {                                    // dll:10070db0
    m_bActive = 1;
    if (m_bWorking != 0) startRunning();
}
void CWindmill::deactivate() {                                  // dll:10070dd0
    m_bActive = 0;
    if (m_bWorking != 0) {
        cworld::removePowerGeneration(world(), m_dPowerUsage);
        m_dPowerUsage = 0;
    }
    stopRunning();
}
void CWindmill::startRunning() { cworld::setFXState(world(), static_cast<i16>(m_wId), 1); }   // dll:10070e10
void CWindmill::stopRunning() { cworld::setFXState(world(), static_cast<i16>(m_wId), 0); }    // dll:10070e30

// ---------------------------------------------------------------------------------------------------------------
// CFuseBox

// dll:10027450 CFuseBox::Update: a fire in the box escalates one stage every 100 game seconds (state bit 1 = started;
// 2, 4, 8, 0x10 the stages, each starts its FX 0x2788..0x278b; stage 1 also 0x278d) and burns the room's air
void CFuseBox::update(i32 now) {
    u32 state = unk_60;
    i32 last = m_dwTimeLastUpdate;
    m_dwTimeLastUpdate = now;
    if ((state & 1) != 0 && 100u < static_cast<u32>(now - unk_64) && (state & 0x10) == 0) {
        unk_64 = now;
        if ((state & 8) == 0) {
            if ((state & 4) == 0) {
                if ((state & 2) == 0) {
                    cworld::setFXState(world(), 0x2788, 1);
                    unk_60 |= 2;
                    cworld::setFXState(world(), 0x278d, 1);
                } else {
                    cworld::setFXState(world(), 0x2789, 1);
                    unk_60 |= 4;
                }
            } else {
                cworld::setFXState(world(), 0x278a, 1);
                unk_60 |= 8;
            }
        } else {
            cworld::setFXState(world(), 0x278b, 1);
            unk_60 |= 0x10;
        }
        m_pSite->setUnk0e8(m_pSite->unk0e8() + 4.0);      // site +0xe8 / +0xf0
        m_pSite->setUnk0f0(m_pSite->unk0f0() + 4.0);
        if (host() && host()->isAtPage("bd")) slot60(nullptr, 0);   // CSelf::IsAtPage(&DAT_1009f450): redraw the fuses
    }
    if (unk_60 != 0) {
        CBiomeCel* cel = m_pSite->cel();     // *(site + 8)
        cel->setNewPollution(static_cast<double>(static_cast<u32>(now - last)) * 0.0005555555555555556 + cel->newPollution());
    }
}

// dll:100272a0 CFuseBox::OnApply(object, hotspot): an extinguisher used on a fuse of the box. Water (type 1) electrocutes the
// player; CO2 (type 2) puts the fire out. The stage the hotspot ("..1".."..4") names is cleared; the last stage out ends the fire.
void CFuseBox::onApply(GameObj& object, const std::string& hotspot) {
    if ((object.flags() & 0x100000u) == 0) return;      // *(byte*)(object + 0x16) & 0x10
    FeatureHost* h = host();
    u16 type = static_cast<u16>(object.peek(0x40, 2));  // CExtinguisher +0x40
    if (type == 1) {
        if (h && h->eng())
            h->eng()->api().PlayObjectUseAnimation("anims\\obj_use\\electrct.tan", "sounds\\objects\\electrct.wav", true, false,
                                                   0xfa, 0x91, 1, 0x42, "", -2);
        if (h) h->dieOfElectrocution();     // DAT_100c249c = 1 (dieOnUpdate), DAT_100c24a0 = 0x13 (cause)
        return;
    }
    if (type == 2) {
        if (h && h->eng())
            h->eng()->api().PlayObjectUseAnimation("anims\\obj_use\\co2use.tan", "sounds\\objects\\exting1.wav", true, false,
                                                   0x112, 0x98, 1, 0x42, "", -2);
    }
    unk_60 &= 0xfffffffeu;
    cworld::setFXState(world(), 0x2791, 0);
    char last = hotspot.empty() ? '\0' : hotspot[hotspot.size() - 1];   // param_2[strlen - 1]
    switch (last) {
    case '1': cworld::setFXState(world(), 0x2788, 0); unk_60 &= 0xfffffffdu; break;
    case '2': cworld::setFXState(world(), 0x2789, 0); unk_60 &= 0xfffffffbu; break;
    case '3': cworld::setFXState(world(), 0x278a, 0); unk_60 &= 0xfffffff7u; break;
    case '4': cworld::setFXState(world(), 0x278b, 0); unk_60 &= 0xffffffefu; break;
    default:
        world().log("CFuseBox::OnApply fuck up");   // Eng_WriteLog(0x100a30d4)
    }
    if (unk_60 == 0) {
        cworld::setFXState(world(), 0x278d, 0);
        m_pSite->setUnk0e8(0);
        m_pSite->setUnk0f0(0);
    } else {
        m_pSite->setUnk0e8(m_pSite->unk0e8() - 4.0);
        m_pSite->setUnk0f0(m_pSite->unk0f0() - 4.0);
    }
    slot60(nullptr, 0);
}

void CFuseBox::saveSlot(SlotWriter& w) const {   // dll:10026c70
    saveBase(w);
    w.writeI32(unk_5c);
    w.writeU32(m_dwPowerMask);
    w.writeI32(unk_60);
    w.writeI32(unk_64);
}

void CFuseBox::loadSlot(SlotReader& r) {         // dll:10026cc0
    loadBase(r);
    unk_5c = r.readI32("unk_5c");
    m_dwPowerMask = r.readHex32("m_dwFuseMask");
    unk_60 = r.readI32("unk_60");
    unk_64 = r.readI32("unk_64");
    r.finishPad();
}

void CSolarGen::members(MemberList& out) const {
    CFeature::members(out);
}

void CWindmill::members(MemberList& out) const {
    CFeature::members(out);
}

void CFuseBox::members(MemberList& out) const {
    CFeature::members(out);
    putP(out, "m_pSite", m_pSite);
    putI(out, "unk_5c", unk_5c);
    putI(out, "unk_60", unk_60);
    putI(out, "unk_64", unk_64);
}

BIOSYS_REGISTER_CTOR(10003480, [](World& w, const char* name, int32_t a1) -> Item* {
    return w.adopt(std::make_unique<CSolarGen>(w, name, a1));
});
BIOSYS_REGISTER_CTOR(10070c40, [](World& w, const char* name, int32_t a1) -> Item* {
    return w.adopt(std::make_unique<CWindmill>(w, name, a1));
});
BIOSYS_REGISTER_CTOR(10026b30, [](World& w, const char* name, uint32_t a1, const char* a2, int32_t a3,
                                  int32_t a4) -> Item* {
    return w.adopt(std::make_unique<CFuseBox>(w, name, a1, a2, a3, a4));
});

} // namespace biosys::game
