#include "game/site.h"
#include "game/world_ctors.gen.h"
#include "game/path.h"
#include <cstring>

namespace biosys::game {

CSite::CSite(World& world, u32 ctorAddr, const char* className, const char* name)
    : Item(ctorAddr, className, name), world_(&world) {
    if (std::strlen(name) > 11) throw Error(std::string(className) + " name too long: " + name);   // char[12] at +0xd4
}

// dll:1000c270
CSite::CSite(World& world, const char* name, int32_t dwUnk0e0, int32_t flags, Biome* biome, CBiomeCel* cel, double dPosX,
             double dPosY, const char* palette, const char* daySound, const char* nightSound, const char* deadSound,
             DllPtr pBuf0, DllPtr pBuf1, DllPtr pBuf2, DllPtr pBuf3)
    : CSite(world, 0x1000c270, "CSite", name) {
    m_wState = 0;
    m_dwUnk0e0 = static_cast<uint32_t>(dwUnk0e0);
    m_pFeature = nullptr;
    m_dwFlags = static_cast<uint32_t>(flags);
    m_pBiome = biome;
    m_pCel = cel;
    m_dPosX = dPosX;
    m_dPosY = dPosY;
    m_szPalette = palette ? palette : "";
    // DAT_100c425c = 0 in the original: a different global (not DAT_100c3224), not modelled
    m_szDaySound = daySound ? daySound : "";
    // The original compares the string pointers; identical DLL string constants are one object.
    m_szNightSound = nightSound ? nightSound : "";
    if (m_szNightSound == m_szDaySound) m_szNightSound.clear();
    m_szDeadSound = deadSound ? deadSound : "";
    if (m_szDeadSound == m_szDaySound) m_szDeadSound.clear();
    m_pBuf[0] = pBuf0;
    m_pBuf[1] = pBuf1;
    m_pBuf[2] = pBuf2;
    m_pBuf[3] = pBuf3;
    m_dUnk0e8 = 0.0;
    m_dUnk0f0 = 0.0;
    m_dwUnk0f8 = 0;
    m_dwUnk0fc = 0;
}

// dll:1000c5a0
void CSite::saveSlot(SlotWriter& w) const {
    w.writeU32(m_dwFlags);
    w.writeF64(m_dUnk0e8);
    w.writeF64(m_dUnk0f0);
    w.writeI32(world_->state().lightsOn);
    w.writeU16(m_wState);
    for (Item* l : links()) {
        as<CPath>(l).saveFlags(w);
    }
}

// dll:1000c640
void CSite::loadSlot(SlotReader& r) {
    m_dwFlags = r.readHex32("m_dwFlags");
    m_dUnk0e8 = r.readF64("unk_e8");
    m_dUnk0f0 = r.readF64("unk_f0");
    world_->state().lightsOn = r.readI32("g_bLightsOn");
    m_wState = r.readU16("m_wState");
    size_t i = 0;
    for (Item* l : links()) {
        const std::string member = "link[" + std::to_string(i++) + "].m_dwFlags";   // the dump names each element
        as<CPath>(l).loadFlags(r, member.c_str());
    }
    r.finishPad();
}

// dll:10024f60
CSiteVariant::CSiteVariant(World& world, const char* name, const char* palette, const char* sound, int32_t dwUnk0b8,
                           DllPtr pBuf)
    : CSite(world, 0x10024f60, "CSiteVariant", name) {
    if (dwUnk0b8 != 0) throw Error("UNRESOLVED dll:10024f60: argument 3 is stored at +0xb8 and is always 0");
    m_wState = 0;
    m_dwFlags = 0;
    m_pBiome = nullptr;
    m_pCel = nullptr;
    m_dPosX = m_dPosY = 0.0;
    {
        const uint64_t bits = 0xc12e847e00000000ull;   // +0x18/+0x20 low dword 0, +0x1c/+0x24 high dword 0xc12e847e
        std::memcpy(&m_dPosX, &bits, 8);
        std::memcpy(&m_dPosY, &bits, 8);
    }
    m_szPalette = palette ? palette : "";
    m_dwUnk0e0 = 0x162;
    // DAT_100c425c = 0 (global not modelled)
    m_szDaySound = sound ? sound : "";
    m_szNightSound.clear();   // +0xb8 = 0
    m_szDeadSound = m_szDaySound;   // +0xbc = param_3, same pointer as +0xb4
    m_dUnk0e8 = 0.0;
    m_pBuf[0] = m_pBuf[1] = m_pBuf[2] = m_pBuf[3] = pBuf;
    m_dUnk0f0 = 0.0;
    m_dwUnk0f8 = m_dwUnk0fc = 0;
    m_pFeature = nullptr;
}

BIOSYS_REGISTER_CTOR(1000c270, [](World& w, const char* name, int32_t a1, int32_t a2, Biome* a3, Item* a4, double a5,
                                  double a7, const char* a9, const char* a10, const char* a11, const char* a12,
                                  DllPtr a13, DllPtr a14, DllPtr a15, DllPtr a16) -> Item* {
    return w.adopt(std::make_unique<CSite>(w, name, a1, a2, a3, &as<CBiomeCel>(a4), a5, a7, a9, a10, a11, a12, a13, a14, a15, a16));
});

BIOSYS_REGISTER_CTOR(10024f60, [](World& w, const char* name, const char* a1, const char* a2, int32_t a3,
                                  DllPtr a4) -> Item* {
    return w.adopt(std::make_unique<CSiteVariant>(w, name, a1, a2, a3, a4));
});

} // namespace biosys::game
