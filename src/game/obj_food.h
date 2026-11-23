// CFood: fruit, meat, eggs, insects ... (259 items). Constructor dll:10063520, Save dll:10063640, Load dll:10063680.
#pragma once
#include "game/gameobj.h"
#include "game/plant.h"

namespace biosys::game {

class ObjFood final : public GameObj {
public:
    // CFood::CFood dll:10063520
    ObjFood(const char* name, u32 flags, i32 nameStringId, CPlant* plant, i32 plantSlot, double nutrition,
            double protein, double weight, double water, i32 age70, i32 age74, i32 regrowTime, u16 consumableCode);

    void saveSlot(SlotWriter& w) const override;   // dll:10063640
    void loadSlot(SlotReader& r) override;         // dll:10063680

    // vtable 10093470: Dump +0x24, Describe +0x28, Eat +0x2c, PickUp +0x44, Update +0x68, +0x94
    void dump(std::vector<std::string>& lines) const override;   // dll:100636c0
    void describe(Self& s) override;                             // dll:10063c10
    int use(Self& s) override;                                   // CFood::Eat dll:10063790 (true when the object left the scene)
    int pickUp(Self& s) override;                                // dll:10063ad0
    int update(Self& s, i32 now) override;                       // dll:100639b0
    void removeFromScene(Self* s = nullptr) override;            // FUN_10063750
    void dumpMembers(ObjMembers& out) const override;

    CPlant* plant() const { return m_pPlant; }
    i32 plantSlot() const { return m_nPlantSlot; }
    void setState(u8 s) { m_byState = s; }                   // CThermalCooker (dll:1006f870 / 1006f940) pokes +0x48 / +0x68
    void setConsumableCode(u16 c) { m_wConsumableCode = c; }
    u8 state() const { return m_byState; }
    double nutrition() const { return m_dNutrition; }
    double protein() const { return m_dProtein; }
    double water() const { return m_dWater; }
    u16 consumableCode() const { return m_wConsumableCode; }
    i32 timePicked() const { return m_dwTimePicked; }
    i32 regrowTime() const { return m_dwRegrowTime; }

private:
    CPlant* m_pPlant = nullptr;      // +0x40 the plant it grows on, or NULL
    i32 m_nPlantSlot = 0;            // +0x44 1-based fruit slot on the plant (bit in the plant's +0x14 mask)
    u8 m_byState = 0;                // +0x48 1 not grown, 2 on plant, 3 picked, 4 rotten, 5 cooked
    double m_dNutrition = 0;         // +0x50
    double m_dProtein = 0;           // +0x58
    double m_dWater = 0;             // +0x60
    u16 m_wConsumableCode = 0;       // +0x68
    i32 m_dwTimePicked = 0;          // +0x6c
    i32 m_dwAgeTime70 = 0;           // +0x70 ageing time (-1 for meat)
    i32 m_dwAgeTime74 = 0;           // +0x74 ageing time (-1 for meat)
    i32 m_dwRegrowTime = 0;          // +0x78 seconds until a picked item reappears (CFood::Update dll:100639b0)
};

} // namespace biosys::game
