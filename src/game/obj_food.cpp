#include "game/obj_food.h"
#include "game/obj_fuse.h"
#include "game/obj_gear.h"
#include "game/obj_common.h"
#include "game/world_ctors.gen.h"
#include <cstdio>

namespace biosys::game {

// dll:10063520
ObjFood::ObjFood(const char* name, u32 flags, i32 nameStringId, CPlant* plant, i32 plantSlot, double nutrition,
                 double protein, double weight, double water, i32 age70, i32 age74, i32 regrowTime,
                 u16 consumableCode)
    : GameObj(0x10063520, "CFood", name, nameStringId) {
    m_pPlant = plant;                                   // +0x40
    m_dwFlags = flags | 0x20000000;                     // +0x14
    if (m_dwNameStringId != 0x114) m_dwFlags = flags | 0x20000040;
    m_dNutrition = nutrition;                           // +0x50
    m_dProtein = protein;                               // +0x58
    m_dWeight = weight;                                 // +0x20
    m_dWater = water;                                   // +0x60
    m_wConsumableCode = consumableCode;                 // +0x68
    m_dwAgeTime70 = age70;
    m_dwAgeTime74 = age74;
    m_dwRegrowTime = regrowTime;
    m_nPlantSlot = plantSlot;                           // +0x44
    if ((m_dwFlags & 0x8000) == 0) {                    // byte +0x15 bit 0x80
        m_dwTimePicked = 0;
        m_byState = 2;
        if (plant) plant->addFruit(plantSlot);          // plant+0x10 += 1; plant+0x14 (u16) |= 1 << (slot - 1)
    } else {
        m_dwTimePicked = 0x7080;
        m_byState = 1;
        // FUN_10063750 dll:10063750: FUN_10029510 removes the object from the engine's scene (+0x2c = 0); since
        // +0x2c is then 0, the state byte is set to 1 (the plant bookkeeping branch needs state 2: not taken).
        removeFromScene();
        m_byState = 1;
    }
}

// dll:10063640
void ObjFood::saveSlot(SlotWriter& w) const {
    saveCommon(w);
    w.writeU8(m_byState);
    w.writeI32(m_dwTimePicked);
    w.writeU16(m_wConsumableCode);
}

// dll:10063680
void ObjFood::loadSlot(SlotReader& r) {
    loadCommon(r);
    m_byState = r.readU8("m_byState");
    m_dwTimePicked = r.readI32("m_dwTimePicked");
    m_wConsumableCode = r.readU16("m_wConsumableCode");
    r.finishPad();
}

// dll:100636c0
void ObjFood::dump(std::vector<std::string>& lines) const {
    dumpBase(lines);
    char b[80];
    std::snprintf(b, sizeof b, "m_byState \t\t%d", m_byState);
    lines.push_back(b);
    std::snprintf(b, sizeof b, "m_dwTimePicked \t\t%d", m_dwTimePicked);
    lines.push_back(b);
    if (!m_pPlant) {
        lines.push_back("m_pPlant->m_dHealth \t\tNULL");
        return;
    }
    std::snprintf(b, sizeof b, "m_pPlant->m_dHealth \t\t%f", m_pPlant->health());
    lines.push_back(b);
}

void ObjFood::dumpMembers(ObjMembers& out) const {
    commonMembers(out);
    putP(out, "m_pPlant", m_pPlant);
    putI(out, "m_nPlantSlot", m_nPlantSlot);
    putI(out, "m_byState", m_byState);
    putD(out, "m_dNutrition", m_dNutrition);
    putD(out, "m_dProtein", m_dProtein);
    putD(out, "m_dWater", m_dWater);
    putI(out, "m_wConsumableCode", m_wConsumableCode);
    putI(out, "m_dwTimePicked", m_dwTimePicked);
    putI(out, "m_dwAgeTime70", m_dwAgeTime70);
    putI(out, "m_dwAgeTime74", m_dwAgeTime74);
    putI(out, "m_dwRegrowTime", m_dwRegrowTime);
}

// dll:10063c10
void ObjFood::describe(Self& s) {
    if (s.g.describeSuppressed != 0) {
        s.g.describeSuppressed = 0;
        return;
    }
    if (m_dwNameStringId != 0) {
        int p2 = 0xffff;
        if (m_byState == 5) p2 = 0x117;
        else if ((m_dwFlags & 1) != 0) p2 = 0x153;
        s.ShowMessage(1, m_dwNameStringId, p2, 0);
    }
}

// dll:10063790 CFood::Eat. The decompiled control flow is the binary's (checked in the disassembly): a cooked item (state 5)
// goes through CSelf::Eat twice, once with the plain values and once with the values the other states use.
int ObjFood::use(Self& s) {
    if ((m_dwFlags & 0x200000) != 0) return 1;
    GameObj* hold = objutil::holding(s);
    if ((m_dwFlags & 0x400000) != 0 && hold && (hold->flags() & 0x800000) != 0 && objutil::iequals(hold->variant(), "NoBait")) {
        hold->changeVariation(s, "Bait", -2);
        as<ObjFishingRod>(hold).setState(1);                    // *(hold + 0x40) = 1
        removeFromScene(&s);
        return 1;
    }
    if (s.m_pAirMask.item) {
        s.ShowMessage(1, 0x165, 0xffff, 0);
        return 1;
    }
    if (m_byState == 4) {
        s.m_pLastEatenPlant.set(m_pPlant);
        s.Vomit();
        s.LoseHealth(5.0, 10, 1);
    } else {
        if (m_byState == 5 && s.Eat(m_dNutrition, m_dProtein, m_dWater, m_wConsumableCode) != 0) {
            s.m_pLastEatenPlant.set(m_pPlant);
            removeFromScene(&s);
            if (m_dwNameStringId == 0x118 && (s.m_dwChampagne != 0 || s.m_dwMescal != 0)) {
                s.m_dwChampagne = 0;
                s.m_dwMescal = 0;
                if ((s.m_dwSymptoms & 0x40000000) == 0) s.m_dwSymptomsCured |= 0x200;
                else s.m_dwSymptoms &= 0xbfffffff;
                s.host().CEngineFX_PlayRandomSound("sounds\\russell\\sighs\\sigh", 4);
                s.host().enableMouseDeltaEvent(0);
            }
        }
        double nutrition = m_dNutrition;
        double protein = m_dProtein;
        if ((m_dwFlags & 1) != 0) {
            nutrition = nutrition * 0.2;
            protein = protein * 0.2;
        }
        if (s.Eat(nutrition, protein, m_dWater, m_wConsumableCode) == 0) return m_bInScene == 0 ? 1 : 0;
        s.m_pLastEatenPlant.set(m_pPlant);
    }
    removeFromScene(&s);
    return m_bInScene == 0 ? 1 : 0;
}

// dll:10063ad0
int ObjFood::pickUp(Self& s) {
    if (m_dwNameStringId == 0x146) s.host().playSound("sounds\\objects\\termite.wav");
    s.AddWeight(m_dWeight);
    m_bHeld = 1;
    i32 now = s.now();
    if (m_byState == 2) {
        m_byState = 3;
        m_dwTimePicked = now;
        if (m_pPlant) {
            if ((m_dwFlags & 0x4000000) != 0) s.host().setFXState(0x3ec, 0);
            s.host().playSound("sounds\\objects\\pickuse.wav");
            m_pPlant->setFruitMask(static_cast<u16>(m_pPlant->fruitMask() & ~(1 << ((m_nPlantSlot - 1) & 0x1f))));
            m_pPlant->setFruitCount(m_pPlant->fruitCount() - 1);
            if (m_pPlant == s.m_pPlant.item) {
                char b[16];
                std::snprintf(b, sizeof b, "fruit%d", m_nPlantSlot);
                s.host().CEngineFX_SetImageVisibility(b, 0, -1);
                s.host().CPlant_Display(m_pPlant, 1);
            } else {
                s.host().Eng_Api5E_ReleasePageImageSlot(m_pPlant, m_nPlantSlot, -1, 1);
                s.host().CEngineFX_SetImageVisibility(name().c_str(), 0, -1);
            }
        }
    }
    if ((m_dwFlags & 0x80) == 0) describe(s);
    return 1;
}

// dll:100639b0
int ObjFood::update(Self& s, i32 now) {
    if (m_byState == 2) return 1;
    if (m_bInScene == 0 && static_cast<u32>(m_dwRegrowTime) < static_cast<u32>(now - m_dwTimePicked)) {
        if (!m_pPlant) {
            if (s.host().Eng_Api45_ReturnGameObjectHidden(name().c_str())) {
                changeVariation(s, "fresh", -2);
                m_bInScene = 1;
                m_byState = 2;
            }
        } else if (0.4 < m_pPlant->health() && m_pPlant->site() != s.m_pSite.item &&
                   ((m_dwFlags & 0x2000) == 0 || (m_pPlant->cel()->flags() & 0x10) == 0) &&
                   ((m_dwFlags & 0x4000) == 0 || (m_pPlant->cel()->flags() & 0x10) != 0)) {
            if (s.host().Eng_Api45_ReturnGameObjectHidden(name().c_str())) {
                changeVariation(s, "fresh", -2);
                m_bInScene = 1;
                m_byState = 2;
                m_pPlant->addFruit(m_nPlantSlot);
                if ((m_dwFlags & 0x4000000) != 0) s.host().setFXState(0x3ec, 1);
            }
        }
    }
    return 1;
}

// FUN_10063750 dll:10063750
void ObjFood::removeFromScene(Self* s) {
    GameObj::removeFromScene(s);
    if (m_bInScene == 0) {
        if (m_pPlant && m_byState == 2) {
            m_pPlant->setFruitMask(static_cast<u16>(m_pPlant->fruitMask() & ~(1 << ((m_nPlantSlot - 1) & 0x1f))));
            m_pPlant->setFruitCount(m_pPlant->fruitCount() - 1);
        }
        m_byState = 1;
    }
}

BIOSYS_REGISTER_CTOR(10063520, [](World& w, const char* name, uint32_t a1, int32_t a2, Item* a3, int32_t a4, double a5,
                                  double a6, double a7, double a8, int32_t a9, int32_t a10, int32_t a11,
                                  uint16_t a12) -> Item* {
    return w.adopt(std::make_unique<ObjFood>(name, a1, a2, a3 ? &as<CPlant>(a3) : nullptr, a4, a5, a6, a7, a8, a9, a10, a11, a12));
});

} // namespace biosys::game
