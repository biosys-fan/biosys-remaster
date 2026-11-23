// CFuse: the 61 fuses of the fuse boxes. Constructor dll:10063180, Save dll:10063280, Load dll:100632b0.
#pragma once
#include "game/gameobj.h"

namespace biosys::game {

class ObjFuse final : public GameObj {
public:
    ObjFuse(const char* name, i32 nameStringId, u32 flags, i32 fuseFlags);   // dll:10063180
    void saveSlot(SlotWriter& w) const override;                             // dll:10063280
    void loadSlot(SlotReader& r) override;                                   // dll:100632b0
    u32 fuseFlags() const { return m_dwFuseFlags; }

    // vtable 10093338: PickUp +0x44, Dropped +0x48
    int pickUp(Self& s) override;                                            // dll:100632e0
    int onDropped(Self& s, const std::string& hotspot) override;             // dll:10063300
    void dumpMembers(ObjMembers& out) const override;

private:
    u32 m_dwFuseFlags = 0;     // +0x40 fuse bit mask (saved as hex)
};

} // namespace biosys::game
