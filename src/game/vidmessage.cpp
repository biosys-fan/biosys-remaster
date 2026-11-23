#include "game/vidmessage.h"
#include "game/world_ctors.gen.h"

namespace biosys::game {

namespace {
std::string copyChars(const char* s, size_t cap, const char* what) {
    std::string r(s ? s : "");
    if (r.size() >= cap) throw Error(std::string(what) + " too long: " + r);   // the original copies unchecked
    return r;
}
}

// dll:1001acd0
CVidMessage::CVidMessage(const char* movie, const char* imageName, const char* palette, int32_t x, int32_t y,
                         int32_t milestoneMask, int32_t played)
    : Item(0x1001acd0, "CVidMessage", movie),
      m_dwX(x), m_dwY(y), m_szMovie(copyChars(movie, 100, "video message file")),
      m_szImage(copyChars(imageName, 20, "video message image name")),
      m_szPalette(copyChars(palette, 100, "video message palette")),
      m_dwMilestoneMask(static_cast<uint32_t>(milestoneMask)), m_dwFlagA(milestoneMask == 0 ? 1 : 0),
      m_dwFlagB(played) {}

void CVidMessage::saveSlot(SlotWriter&) const { throw Error("CVidMessage has no slot"); }
void CVidMessage::loadSlot(SlotReader&) { throw Error("CVidMessage has no slot"); }

BIOSYS_REGISTER_CTOR(1001acd0, [](World& w, const char* name, const char* a1, const char* a2, int32_t a3, int32_t a4,
                                  int32_t a5, int32_t a6) -> Item* {
    return w.adopt(std::make_unique<CVidMessage>(name, a1, a2, a3, a4, a5, a6));
});

} // namespace biosys::game
