// Containers and drugs: CConsumable (dll:10076e20), CSingleDose (dll:1006f3f0), CExtinguisher (dll:10007b40).
#pragma once
#include "game/gameobj.h"

namespace biosys::game {

// CConsumable: canteen, latex extractor, mescal, the drug dispenser's phials. Save dll:10076ed0, Load dll:10076f00.
class ObjConsumable final : public GameObj {
public:
    ObjConsumable(const char* name, i32 nameStringId, u32 flags, u16 contentCode, u16 maxDoses, double weightFull,
                  double weightEmpty);   // dll:10076e20
    void saveSlot(SlotWriter& w) const override;
    void loadSlot(SlotReader& r) override;
    u16 contentCode() const { return m_wContentCode; }
    u16 maxDoses() const { return m_wMaxDoses; }

    // vtable 10094320: Dump +0x24, Describe +0x28, Use +0x2c, UseOnHotspot +0x54, key filter +0x58, Fill +0x60, +0x78
    void dump(std::vector<std::string>& lines) const override;               // dll:10076f30
    void describe(Self& s) override;                                         // dll:100774f0
    int use(Self& s) override;                                               // CConsumable::Use dll:100770a0
    int onUsedOnHotspot(Self& s, const std::string& hotspot) override;       // CConsumable::FillFromHotspot dll:10077150
    int onKey(Self& s, int key, u32* allow) override;                        // dll:10076f70
    int fill(Self& s, u16 code, int amount) override;                        // CConsumable::Fill dll:100772b0
    void dumpMembers(ObjMembers& out) const override;

private:
    u16 m_wContentCode = 0;       // +0x40 what it holds (consumable code; saved as m_wConsumableCode)
    u16 m_wMaxDoses = 0;          // +0x42 capacity (CConsumable::Fill dll:100772b0)
    double m_dWeightFull = 0;     // +0x48 also the initial weight (guess: weight when full)
    double m_dWeightEmpty = 0;    // +0x50 (guess: weight when empty)
};

// CSingleDose: the champagne glasses. Save dll:1006f480, Load dll:1006f4b0.
class ObjSingleDose final : public GameObj {
public:
    ObjSingleDose(const char* name, i32 nameStringId, u16 contentCode, double weight);   // dll:1006f3f0
    void saveSlot(SlotWriter& w) const override;
    void loadSlot(SlotReader& r) override;

    // vtable 10093b38: Dump +0x24, Use +0x2c, key filter +0x58
    void dump(std::vector<std::string>& lines) const override;               // dll:1006f4e0
    int use(Self& s) override;                                               // CSingleDose::Use dll:1006f570
    int onKey(Self& s, int key, u32* allow) override;                        // dll:1006f510
    void dumpMembers(ObjMembers& out) const override;

private:
    u16 m_wContentCode = 0;       // +0x40 consumable code (0x3c champagne); saveblob.md calls it m_wDosesLeft
};

// CExtinguisher: fireCO22-5 (type 2), fireH2O1-3 (type 1). Save/Load are the common ones (FUN_10028d00/d70).
class ObjExtinguisher final : public GameObj {
public:
    ObjExtinguisher(const char* name, i32 nameStringId, u16 type);   // dll:10007b40
    u16 type() const { return m_wType; }

    // vtable 1008fad0: Use +0x2c (+0x54 is the base one)
    int use(Self& s) override;                                               // dll:10007c50
    void dumpMembers(ObjMembers& out) const override;
    u32 peek(u32 offset, unsigned bytes) const override;                     // CFire / CFuseBox::OnApply read the type at +0x40

private:
    u16 m_wType = 0;              // +0x40 1 water, 2 CO2 (not saved)
};

} // namespace biosys::game
