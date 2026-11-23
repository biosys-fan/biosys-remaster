#include "game/path.h"
#include "game/site.h"
#include "game/world_ctors.gen.h"

namespace biosys::game {

// dll:10001000
CPath::CPath(CSite* from, const char* toSiteName, CSite* to, int32_t dwUnk0c, const char* pathName, int32_t dwUnk20,
             int32_t dwFlags, double dUnk28)
    : Item(0x10001000, "CPath", toSiteName), m_pFrom(from), m_pTo(to), m_dwUnk0c(dwUnk0c), m_szName(pathName),
      m_dwFlags(static_cast<uint32_t>(dwFlags)), m_dwUnk20(static_cast<uint32_t>(dwUnk20)), m_dUnk28(dUnk28),
      m_dwUnk30(-1), m_dwUnk34(-1) {
    if (m_szName.size() > 11) throw Error("CPath name too long: " + m_szName);   // char[12] at +0x10
}

void CPath::saveSlot(SlotWriter&) const { throw Error("CPath '" + name() + "' has no slot of its own"); }
void CPath::loadSlot(SlotReader&) { throw Error("CPath '" + name() + "' has no slot of its own"); }

// dll:100011c0
void CPath::saveFlags(SlotWriter& w) const { w.writeU32(m_dwFlags); }
// dll:100011e0
void CPath::loadFlags(SlotReader& r, const char* memberName) { m_dwFlags = r.readHex32(memberName); }

BIOSYS_REGISTER_CTOR(10001000, [](World& w, Item* a0, const char* a1, int32_t a2, const char* a3, int32_t a4,
                                  int32_t a5, double a6) -> Item* {
    CSite* to = w.findSite(a1);   // the original logs "CPath::CPath iffy site" when null and keeps the null pointer
    return w.adopt(std::make_unique<CPath>(&as<CSite>(a0), a1, to, a2, a3, a4, a5, a6));
});

} // namespace biosys::game
