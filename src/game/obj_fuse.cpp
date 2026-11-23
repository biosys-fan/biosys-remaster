#include "game/obj_fuse.h"
#include "game/obj_common.h"
#include <cstdlib>
#include "game/world_ctors.gen.h"

namespace biosys::game {

// dll:10063180
ObjFuse::ObjFuse(const char* name, i32 nameStringId, u32 flags, i32 fuseFlags)
    : GameObj(0x10063180, "CFuse", name, nameStringId) {
    m_dwFlags |= flags;                                  // +0x14
    m_dWeight = dwordsToDouble(0x3fc99999, 0x9999999a);  // +0x20 0.2
    m_dwFuseFlags = static_cast<u32>(fuseFlags);         // +0x40
}

// dll:10063280
void ObjFuse::saveSlot(SlotWriter& w) const {
    saveCommon(w);
    w.writeU32(m_dwFuseFlags);
}

// dll:100632b0
void ObjFuse::loadSlot(SlotReader& r) {
    loadCommon(r);
    m_dwFuseFlags = r.readHex32("m_dwFuseFlags");
    r.finishPad();
}

// dll:100632e0
int ObjFuse::pickUp(Self& s) {
    GameObj::pickUp(s);
    m_dwFuseFlags = 0;
    return 1;
}

// dll:10063300: the fuse put into a fuse box hotspot takes the circuit bit of the hotspot ("..." + 5 characters: its number)
int ObjFuse::onDropped(Self& s, const std::string& hotspot) {
    GameObj::onDropped(s, hotspot);
    m_dwFuseFlags = 0;
    CFeature* f = objutil::feature(s);
    if (f && (f->flags() & 0x08000000) != 0) {
        u32 mask = f->powerMask() & 0x3f000000;
        m_dwFuseFlags = mask;
        int n = std::atoi(hotspot.size() > 5 ? hotspot.c_str() + 5 : "");
        m_dwFuseFlags = (1u << ((static_cast<unsigned>(static_cast<signed char>(n)) - 1u) & 0x1f)) | mask;
    }
    return 1;
}

void ObjFuse::dumpMembers(ObjMembers& out) const {
    commonMembers(out);
    putX(out, "m_dwFuseFlags", m_dwFuseFlags);
}

BIOSYS_REGISTER_CTOR(10063180, [](World& w, const char* name, int32_t a1, uint32_t a2, int32_t a3) -> Item* {
    return w.adopt(std::make_unique<ObjFuse>(name, a1, a2, a3));
});

} // namespace biosys::game
