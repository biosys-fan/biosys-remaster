// CPage and its three subclasses: the non-saved page objects of the world (CWorld::AddPage dll:10013820).
// A page is a named screen with three lists of FX (CPage::AddFX / AddOccFX / AddOccLoopedFX).
#pragma once
#include "game/world.h"

namespace biosys::game {

class CPage : public Item {
public:
    // dll:1000f270 (also the base of the others: FUN_1000f100 builds the three lists)
    CPage(World& world, const char* name);

    bool typed() const override { return true; }
    void saveSlot(SlotWriter&) const override;   // pages are not saved
    void loadSlot(SlotReader&) override;

    const std::vector<Item*>& fx() const { return world_->related(Rel::PageFx, this); }                    // +0x10
    const std::vector<Item*>& occFx() const { return world_->related(Rel::PageOccFx, this); }              // +0x2c
    const std::vector<Item*>& occLoopedFx() const { return world_->related(Rel::PageOccLoopedFx, this); }  // +0x4c

    // +0x68 (set by the page builder after construction) and +0x48: CPage::TriggerMilestone dll:1000f480
    uint32_t milestone() const { return m_dwMilestone; }
    void setMilestone(uint32_t m) { m_dwMilestone = m; }
    int32_t unk48() const { return m_dwUnk48; }
    void setUnk48(int32_t v) { m_dwUnk48 = v; }

protected:
    CPage(World& world, u32 ctorAddr, const char* className, const char* name);   // FUN_1000f100 + name copy

    World* world_;   // port only: owner of the relation lists
    // +0x00 vtable, +0x04 char[12] name = Item::name()
    // +0x10 CObList fx, +0x2c CObList occasional fx, +0x4c CObList occasional looped fx: World relations
    int32_t m_dwUnk48 = 0;         // +0x48 cleared by TriggerMilestone
    uint32_t m_dwMilestone = 0;    // +0x68 milestone number passed to CGame::TriggerEvent
};

// "EDeath" page (vtable 10092030)
class CDeathPage final : public CPage {
public:
    CDeathPage(World& world, const char* name);   // dll:1002af10
    uint16_t stage() const { return m_wStage; }
    void setStage(uint16_t s) { m_wStage = s; }
private:
    uint16_t m_wStage;   // +0x6c = 1 (meaning not determined)
};

// "EIntro" page (vtable 10093138)
class CIntroPage final : public CPage {
public:
    CIntroPage(World& world, const char* name);   // dll:10061c70
    uint16_t stage() const { return m_wStage; }
    void setStage(uint16_t s) { m_wStage = s; }
    int32_t unk70() const { return m_dwUnk70; }
    int32_t unk74() const { return m_dwUnk74; }
    int32_t unk78() const { return m_dwUnk78; }
    int32_t unk7c() const { return m_dwUnk7c; }
    int32_t unk80() const { return m_dwUnk80; }
    void setUnk70(int32_t v) { m_dwUnk70 = v; }
    void setUnk74(int32_t v) { m_dwUnk74 = v; }
    void setUnk78(int32_t v) { m_dwUnk78 = v; }
    void setUnk7c(int32_t v) { m_dwUnk7c = v; }
    void setUnk80(int32_t v) { m_dwUnk80 = v; }
private:
    uint16_t m_wStage;   // +0x6c = 10 (meaning not determined)
    int32_t m_dwUnk70, m_dwUnk74, m_dwUnk78, m_dwUnk7c, m_dwUnk80;   // +0x70..+0x80 = -1
};

// "EGameEnd" page (vtable 10093cc0): m_dwStage is the ending screen counter (0 = first, 5 = movie)
class CGameEndPage final : public CPage {
public:
    CGameEndPage(World& world, const char* name);   // dll:1006fd40
    int32_t stage() const { return m_dwStage; }
    void setStage(int32_t s) { m_dwStage = s; }
private:
    int32_t m_dwStage;   // +0x6c
};

} // namespace biosys::game
