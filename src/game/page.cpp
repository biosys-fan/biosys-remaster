#include "game/page.h"
#include "game/world_ctors.gen.h"
#include <cstring>

namespace biosys::game {

// FUN_1000f100 (lists) + the name copy of the page constructors
CPage::CPage(World& world, u32 ctorAddr, const char* className, const char* name)
    : Item(ctorAddr, className, name), world_(&world) {
    if (std::strlen(name) > 11) throw Error(std::string(className) + " name too long: " + name);   // char[12] at +4
}

// dll:1000f270
CPage::CPage(World& world, const char* name) : CPage(world, 0x1000f270, "CPage", name) {
    m_dwMilestone = 0;
    m_dwUnk48 = 0;
}

void CPage::saveSlot(SlotWriter&) const { throw Error("page '" + name() + "' is not saved"); }
void CPage::loadSlot(SlotReader&) { throw Error("page '" + name() + "' is not saved"); }

// dll:1002af10
CDeathPage::CDeathPage(World& world, const char* name) : CPage(world, 0x1002af10, "CDeathPage", name) {
    m_wStage = 1;
    m_dwMilestone = 0;
    m_dwUnk48 = 0;
}

// dll:10061c70
CIntroPage::CIntroPage(World& world, const char* name) : CPage(world, 0x10061c70, "CIntroPage", name) {
    m_wStage = 10;
    m_dwUnk70 = m_dwUnk74 = m_dwUnk78 = m_dwUnk7c = m_dwUnk80 = -1;
}

// dll:1006fd40
CGameEndPage::CGameEndPage(World& world, const char* name) : CPage(world, 0x1006fd40, "CGameEndPage", name) {
    m_dwMilestone = 0;
    m_dwUnk48 = 0;
    m_dwStage = 0;
}

BIOSYS_REGISTER_CTOR(1000f270, [](World& w, const char* name) -> Item* { return w.adopt(std::make_unique<CPage>(w, name)); });
BIOSYS_REGISTER_CTOR(1002af10, [](World& w, const char* name) -> Item* { return w.adopt(std::make_unique<CDeathPage>(w, name)); });
BIOSYS_REGISTER_CTOR(10061c70, [](World& w, const char* name) -> Item* { return w.adopt(std::make_unique<CIntroPage>(w, name)); });
BIOSYS_REGISTER_CTOR(1006fd40, [](World& w, const char* name) -> Item* { return w.adopt(std::make_unique<CGameEndPage>(w, name)); });

} // namespace biosys::game
