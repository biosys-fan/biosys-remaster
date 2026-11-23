#include "game/obj_consumable.h"
#include "game/obj_common.h"
#include "game/watertank.h"
#include "game/biomecel.h"
#include "game/plant.h"
#include <cstdio>
#include "game/world_ctors.gen.h"

namespace biosys::game {

// dll:10076e20
ObjConsumable::ObjConsumable(const char* name, i32 nameStringId, u32 flags, u16 contentCode, u16 maxDoses,
                             double weightFull, double weightEmpty)
    : GameObj(0x10076e20, "CConsumable", name, nameStringId) {
    m_dwFlags = flags;               // +0x14
    m_wContentCode = contentCode;    // +0x40
    m_wMaxDoses = maxDoses;          // +0x42
    m_dWeightFull = weightFull;      // +0x48
    m_dWeight = weightFull;          // +0x20
    m_dWeightEmpty = weightEmpty;    // +0x50
}

// dll:10076ed0
void ObjConsumable::saveSlot(SlotWriter& w) const {
    saveCommon(w);
    w.writeU16(m_wContentCode);
}

// dll:10076f00
void ObjConsumable::loadSlot(SlotReader& r) {
    loadCommon(r);
    m_wContentCode = r.readU16("m_wConsumableCode");
    r.finishPad();
}

// dll:1006f3f0
ObjSingleDose::ObjSingleDose(const char* name, i32 nameStringId, u16 contentCode, double weight)
    : GameObj(0x1006f3f0, "CSingleDose", name, nameStringId) {
    m_wContentCode = contentCode;    // +0x40
    m_dWeight = weight;              // +0x20
}

// dll:1006f480
void ObjSingleDose::saveSlot(SlotWriter& w) const {
    saveCommon(w);
    w.writeU16(m_wContentCode);
}

// dll:1006f4b0
void ObjSingleDose::loadSlot(SlotReader& r) {
    loadCommon(r);
    m_wContentCode = r.readU16("m_wDosesLeft");
    r.finishPad();
}

// dll:10007b40
ObjExtinguisher::ObjExtinguisher(const char* name, i32 nameStringId, u16 type)
    : GameObj(0x10007b40, "CExtinguisher", name, nameStringId) {
    m_dwFlags |= 0x100000;                       // +0x14
    m_dWeight = dwordsToDouble(0x402e0000);      // +0x20 15.0
    m_wType = type;                              // +0x40
}

// dll:10076f30
void ObjConsumable::dump(std::vector<std::string>& lines) const {
    dumpBase(lines);
    char b[64];
    std::snprintf(b, sizeof b, "m_wContents \t\t%d", m_wContentCode);
    lines.push_back(b);
}

void ObjConsumable::dumpMembers(ObjMembers& out) const {
    commonMembers(out);
    putI(out, "m_wContentCode", m_wContentCode);
    putI(out, "m_wMaxDoses", m_wMaxDoses);
    putD(out, "m_dWeightFull", m_dWeightFull);
    putD(out, "m_dWeightEmpty", m_dWeightEmpty);
}

// dll:100774f0
void ObjConsumable::describe(Self& s) {
    if (s.g.describeSuppressed != 0) {
        s.g.describeSuppressed = 0;
        return;
    }
    int p4 = 0xffff;
    int msg;
    switch (m_wContentCode) {
    case 0x1e: msg = 0xe0; break;
    default: msg = m_dwNameStringId; break;
    case 0x50: msg = 0xe1; break;
    case 0x6e: msg = 0xdf; break;
    case 0x78: msg = 0xe2; break;
    case 0x82: msg = 0x2d; break;
    case 0xbe: case 0xc3: case 200: msg = 0x2a; break;
    case 0xd2: msg = 0x2b; break;
    }
    std::array<int, 12> info{};
    if ((m_dwFlags & 4) == 0) {
        if ((m_dwFlags & 8) == 0) {
            p4 = 0xffff;
        } else {
            s.host().Eng_GetGameObjectInfo(name().c_str(), info);
            u32 u = static_cast<u32>(info[9]);
            if (u == 0) { msg = m_dwNameStringId; p4 = 0x31; }
            else if (u == 1) p4 = 0x38;
            else if (u == 2) p4 = 0x39;
            else if (u == 3) p4 = 0x3a;
            else if (u == 4) p4 = 0x3b;
            else if (u == 5) p4 = 0x3c;
        }
    } else {
        s.host().Eng_GetGameObjectInfo(name().c_str(), info);
        u32 u = static_cast<u32>(info[9]);
        if (u == 0) { msg = m_dwNameStringId; p4 = 0x31; }
        else if (m_wMaxDoses == u) p4 = 0x2e;
        else if (u == 1) p4 = 0x30;
        else if (u == 2) p4 = 0x37;
        else p4 = (u == 4 ? 7 : 0) + 0x2f;
    }
    s.ShowMessage(1, msg, p4, 0);
}

// dll:100770a0
int ObjConsumable::use(Self& s) {
    if (s.m_pAirMask.item && static_cast<i16>(m_wContentCode) != 0x1e) {
        s.ShowMessage(1, 0x165, 0xffff, 0);
        return 1;
    }
    std::array<int, 12> info{};
    s.host().Eng_GetGameObjectInfo(name().c_str(), info);
    if (info[9] == 0) {
        if ((m_dwFlags & 0x10) != 0) removeFromScene(&s);
        if ((m_dwFlags & 0x40000000) != 0) {
            s.host().CEngineFX_PlayRandomSound("sounds\\russell\\drinking\\drink", 4);
            safeSetVariant(s, "empty", -2);
        }
    }
    s.Consume(m_wContentCode);
    describe(s);
    return 1;
}

// dll:10077150
int ObjConsumable::onUsedOnHotspot(Self& s, const std::string& hotspot) {
    if ((m_dwFlags & 0x40000000) != 0 && m_szVariant[0] != 0) {
        if (!objutil::iequals(variant(), "empty")) {
            s.host().Eng_SetGameObjectProperty(name().c_str(), "usagecount", 0);
            use(s);
            return 1;
        }
    }
    if ((m_dwFlags & 2) == 0) return 1;
    std::string h = objutil::upper(hotspot);
    if (s.m_pPlant.item) {
        s.host().CPlant_UseObject(s.m_pPlant.item, this, h);                // dll:10072a00 (BiosGame::CPlant_UseObject)
        return 1;
    }
    if (CFeature* f = objutil::feature(s)) {
        f->onApply(*this, h);                                               // feature vtable +0x44
        return 1;
    }
    u16 code;
    if (h.find("WORMS") != std::string::npos) {
        code = 200;
    } else if (h.find("WATER") != std::string::npos) {
        code = 0xbe;
    } else if (h.find("LOMB") != std::string::npos) {
        code = 0x82;
    } else {
        // ORIGINAL BUG: the code is the low word of the hotspot text, its first two characters
        code = static_cast<u16>(static_cast<u8>(h.empty() ? 0 : h[0]) | (static_cast<u8>(h.size() > 1 ? h[1] : 0) << 8));
        // Fixes::consumableCodeFromHotspot (intent unclear, FIXES.md): candidate reading = the content stays what it is
        if (s.world().state().fixes.consumableCodeFromHotspot) code = m_wContentCode;
    }
    fill(s, code, -1);                                                      // vtable +0x60
    describe(s);                                                            // vtable +0x28
    return 1;
}

// dll:10076f70
int ObjConsumable::onKey(Self& s, int key, u32* allow) {
    if (key == 0x20) {
        u16 c = m_wContentCode;
        bool water = false, champagne = false;
        if (c < 0xbf) {
            if (c == 0xbe) water = true;
            else if (c == 0x3c) champagne = true;
        } else if (c == 0xc3 || c == 200 || c == 0xd2) {
            water = true;
        } else if (c == 0x140) {
            champagne = true;
        }
        if (water) {
            if (99.0 < s.m_dHydration) {
                s.ShowMessage(1, 0xc1, 0xffff, 0);
                *allow = 0;
            }
        } else if (champagne) {
            if (s.m_hDrunkVoice != -1) *allow = 0;                          // DAT_100c2464
        }
    }
    return GameObj::onKey(s, key, allow);
}

// dll:100772b0
int ObjConsumable::fill(Self& s, u16 code, int amount) {
    u32 uses;
    if (amount == -1) {
        uses = m_wMaxDoses;
    } else {
        std::array<int, 12> info{};
        s.host().Eng_GetGameObjectInfo(name().c_str(), info);
        uses = static_cast<u32>(info[9]) + static_cast<u32>(amount);
        if (static_cast<u32>(m_wMaxDoses) < uses) uses = m_wMaxDoses;
    }
    PlayerHost& h = s.host();
    if ((m_dwFlags & 0x40000000) == 0) {
        if (m_dwNameStringId == 0xdb)
            h.playObjectUseAnimation("anims\\obj_up\\cantup.tan", "sounds\\objects\\cantrfl2.wav", 1, 0, 0x154, 0x72, 4, 0x42,
                                     nullptr, -2);
    } else if (code < 0x83) {
        if (code == 0x82) {
            h.playObjectUseAnimation("anims\\obj_use\\l_latex.tan", "sounds\\objects\\latex.wav", 1, 0, 300, 0x96, 1, 0x42,
                                     "latex", -2);
            setVariant("latex");
        } else if (code == 1) {
            h.playObjectUseAnimation("anims\\obj_use\\l_nowt.tan", "sounds\\objects\\latex.wav", 1, 0, 300, 0x96, 1, 0x42,
                                     "empty", -2);
            setVariant("empty");
            uses = 0;
        }
    } else if (code == 0xbe || code == 0xc3) {
        h.playObjectUseAnimation("anims\\obj_use\\l_water.tan", "sounds\\objects\\latex.wav", 1, 0, 300, 0x96, 1, 0x42, "water",
                                 -2);
        setVariant("water");
    } else if (code == 0x10e) {
        h.playObjectUseAnimation("anims\\obj_use\\l_poison.tan", "sounds\\objects\\latex.wav", 1, 0, 300, 0x96, 1, 0x42,
                                 "poison", -2);
        setVariant("poison");
    }
    m_wContentCode = code;
    h.Eng_SetGameObjectProperty(name().c_str(), "usagecount", static_cast<int>(uses));
    describe(s);
    return 1;
}

// dll:1006f4e0
void ObjSingleDose::dump(std::vector<std::string>& lines) const {
    dumpBase(lines);
    char b[64];
    std::snprintf(b, sizeof b, "m_wNature \t\t%d", m_wContentCode);
    lines.push_back(b);
}

void ObjSingleDose::dumpMembers(ObjMembers& out) const {
    commonMembers(out);
    putI(out, "m_wContentCode", m_wContentCode);
}

// dll:1006f570
int ObjSingleDose::use(Self& s) {
    if (s.Consume(m_wContentCode) == 0) return 0;
    s.host().Eng_RemoveGameObject(name().c_str());
    if (s.m_pObjHolding.item == this) s.m_pObjHolding.clear();
    m_bInScene = 0;
    return 1;
}

// dll:1006f510
int ObjSingleDose::onKey(Self& s, int key, u32* allow) {
    if (key == 0x20 && (m_wContentCode == 0x3c || m_wContentCode == 0x140) && s.m_hDrunkVoice != -1) *allow = 0;
    return GameObj::onKey(s, key, allow);
}

// dll:10007c50
int ObjExtinguisher::use(Self& s) {
    if (m_wType == 1) {
        CBiomeCel* cel = dynamic_cast<CBiomeCel*>(s.m_pCel);
        if (cel && cel->soilTank()) {
            CWaterTank& tank = as<CWaterTank>(cel->soilTank());
            tank.setWaterVol(tank.waterVol() + 0.1);
            cel->setTimeLastRain(s.now());
            return 1;
        }
    } else {
        if (m_wType != 2) return 1;
        CBiomeCel& cel = as<CBiomeCel>(s.m_pCel);              // the original has no null check
        cel.setNewCO2(cel.newCO2() + 0.01);
    }
    return 1;
}

u32 ObjExtinguisher::peek(u32 offset, unsigned bytes) const {
    if (bytes == 2 && offset == 0x40) return m_wType;
    return GameObj::peek(offset, bytes);
}

void ObjExtinguisher::dumpMembers(ObjMembers& out) const {
    commonMembers(out);
    putI(out, "m_wType", m_wType);
}

BIOSYS_REGISTER_CTOR(10076e20, [](World& w, const char* name, int32_t a1, int32_t a2, uint16_t a3, uint16_t a4,
                                  double a5, double a6) -> Item* {
    return w.adopt(std::make_unique<ObjConsumable>(name, a1, static_cast<u32>(a2), a3, a4, a5, a6));
});
BIOSYS_REGISTER_CTOR(1006f3f0, [](World& w, const char* name, int32_t a1, uint16_t a2, double a3) -> Item* {
    return w.adopt(std::make_unique<ObjSingleDose>(name, a1, a2, a3));
});
BIOSYS_REGISTER_CTOR(10007b40, [](World& w, const char* name, int32_t a1, uint16_t a2) -> Item* {
    return w.adopt(std::make_unique<ObjExtinguisher>(name, a1, a2));
});

} // namespace biosys::game
