#include "game/spotfx.h"
#include "game/world_ctors.gen.h"
#include <cctype>

namespace biosys::game {

std::string CSpotFX::copyString(const char* s, size_t cap, const char* what) {
    // the original copies with no bound; the destination is a char[cap]
    std::string r(s ? s : "");
    if (r.size() >= cap) throw Error(std::string(what) + " too long: " + r);
    return r;
}

// the part shared by dll:100706b0, 10002eb0, 10015ce0, 10017870 (their base constructors only set vtables)
CSpotFX::CSpotFX(u32 ctorAddr, const char* className, const char* name, int32_t param7c, uint16_t id, uint32_t flags,
                 uint32_t typeBit, int32_t on)
    : Item(ctorAddr, className, name),
      m_szFile(copyString(name, 100, "FX file name")),
      m_dwFlags(flags | typeBit), m_bOn(on), m_bPlaying(0), m_wId(id), m_dwHandle(-1), m_dwParam7c(param7c) {
    for (char& c : m_szFile) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));   // CharUpperA
}

// dll:1000aa80
void CSpotFX::saveSlot(SlotWriter& w) const { w.writeI32(m_bOn); }

// dll:1000aaa0
void CSpotFX::loadSlot(SlotReader& r) {
    m_bOn = r.readI32("m_dwState");
    r.finishPad();
}

// dll:100705c0 (default constructor, vtable only) followed by the part of dll:100706b0 that CSynthAnimFX repeats
CAnimFX::CAnimFX(u32 ctorAddr, const char* className, const char* name, int32_t delay, int32_t x, int32_t y,
                 uint16_t id, uint32_t flags, int32_t on)
    : CSpotFX(ctorAddr, className, name, delay, id, flags, SpotFxFlag::Anim, on), m_dwX(x), m_dwY(y) {}

// dll:100706b0
CAnimFX::CAnimFX(const char* name, int32_t delay, int32_t x, int32_t y, uint16_t id, uint32_t flags, int32_t on,
                 const char* wavFile)
    : CAnimFX(0x100706b0, "CAnimFX", name, delay, x, y, id, flags, on) {
    m_szWavFile = wavFile ? copyString(wavFile, 100, "FX wav file name") : std::string();
}

// dll:10015ce0
CSynthAnimFX::CSynthAnimFX(const char* name, int32_t delay, int32_t x, int32_t y, uint16_t id, uint32_t flags,
                           int32_t on, double synthParam)
    : CAnimFX(0x10015ce0, "CSynthAnimFX", name, delay, x, y, id, flags, on), m_dSynthParam(synthParam) {}

// dll:10002eb0
CMovieFX::CMovieFX(const char* name, int32_t x, int32_t y, uint16_t id, uint32_t flags, int32_t on,
                   const char* wavFile)
    : CSpotFX(0x10002eb0, "CMovieFX", name, x, id, flags, SpotFxFlag::Movie, on),
      m_dwY(y), m_szWavFile(wavFile ? copyString(wavFile, 100, "FX wav file name") : std::string()),
      m_dwWavHandle(-1) {}

// dll:10017870
CSoundSiteFX::CSoundSiteFX(const char* name, int32_t param7c, uint16_t id, uint32_t flags, int32_t on)
    : CSpotFX(0x10017870, "CSoundSiteFX", name, param7c, id, flags, SpotFxFlag::Sound, on) {}

BIOSYS_REGISTER_CTOR(100706b0, [](World& w, const char* name, int32_t a1, int32_t a2, int32_t a3, uint16_t a4,
                                  uint32_t a5, int32_t a6, const char* a7) -> Item* {
    return w.adopt(std::make_unique<CAnimFX>(name, a1, a2, a3, a4, a5, a6, a7));
});

BIOSYS_REGISTER_CTOR(10002eb0, [](World& w, const char* name, int32_t a1, int32_t a2, uint16_t a3, uint32_t a4,
                                  int32_t a5, const char* a6) -> Item* {
    return w.adopt(std::make_unique<CMovieFX>(name, a1, a2, a3, a4, a5, a6));
});

BIOSYS_REGISTER_CTOR(10015ce0, [](World& w, const char* name, int32_t a1, int32_t a2, int32_t a3, uint16_t a4,
                                  uint32_t a5, int32_t a6, double a7) -> Item* {
    return w.adopt(std::make_unique<CSynthAnimFX>(name, a1, a2, a3, a4, a5, a6, a7));
});

BIOSYS_REGISTER_CTOR(10017870, [](World& w, const char* name, int32_t a1, uint16_t a2, uint32_t a3,
                                  int32_t a4) -> Item* {
    return w.adopt(std::make_unique<CSoundSiteFX>(name, a1, a2, a3, a4));
});

} // namespace biosys::game
