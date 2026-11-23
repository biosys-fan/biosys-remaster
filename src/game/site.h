// CSite (ctor dll:1000c270, size 0x100) and its subclass CSiteVariant (ctor dll:10024f60): one view position
// of the habitat (a room, a clearing) tied to a biome cel. Vtables 1008ffc0 / 100915d0.
#pragma once
#include "game/biomecel.h"
#include "game/world.h"
#include <cstring>

namespace biosys::game {

class CSite : public Item {
public:
    // dll:1000c270. The four strings are pointers to DLL string constants in the original; equal pointers are
    // equal strings here (see the constructor).
    CSite(World& world, const char* name, int32_t dwUnk0e0, int32_t flags, Biome* biome, CBiomeCel* cel, double dPosX,
          double dPosY, const char* palette, const char* daySound, const char* nightSound, const char* deadSound,
          DllPtr pBuf0, DllPtr pBuf1, DllPtr pBuf2, DllPtr pBuf3);

    bool typed() const override { return true; }
    void saveSlot(SlotWriter& w) const override;   // dll:1000c5a0
    void loadSlot(SlotReader& r) override;         // dll:1000c640
    Item* siteCel() const override { return m_pCel; }

    const Biome* biome() const { return m_pBiome; }
    CBiomeCel* cel() const { return m_pCel; }
    CFeature* feature() const { return m_pFeature; }      // +0x0c, set by the feature builder
    void setFeature(CFeature* f) { m_pFeature = f; }
    uint32_t flags() const { return m_dwFlags; }
    void setFlags(uint32_t f) { m_dwFlags = f; }
    uint16_t state() const { return m_wState; }
    void setState(uint16_t s) { m_wState = s; }
    double posX() const { return m_dPosX; }
    double posY() const { return m_dPosY; }
    const std::string& daySound() const { return m_szDaySound; }
    const std::string& nightSound() const { return m_szNightSound; }   // empty = none
    const std::string& deadSound() const { return m_szDeadSound; }     // empty = none
    const std::string& palette() const { return m_szPalette; }
    DllPtr buffer(int i) const { return m_pBuf[i]; }
    uint32_t unk0e0() const { return m_dwUnk0e0; }
    // +0xf8: the double the time-of-day code (CSite::OnTimeOfDayChange dll:1000db70) writes; the two dwords of the
    // original's members m_dwUnk0f8 / m_dwUnk0fc combined
    double unk0f8() const { double d; std::memcpy(&d, &m_dwUnk0f8, 8); return d; }
    void setUnk0f8(double d) { std::memcpy(&m_dwUnk0f8, &d, 8); }
    double unk0e8() const { return m_dUnk0e8; }
    void setUnk0e8(double v) { m_dUnk0e8 = v; }
    double unk0f0() const { return m_dUnk0f0; }
    void setUnk0f0(double v) { m_dUnk0f0 = v; }

    // The lists of the original (CObList members) are kept by the World as relations.
    const std::vector<Item*>& features() const { return world_->related(Rel::SiteFeature, this); }   // +0x28
    const std::vector<Item*>& plants() const { return world_->related(Rel::SitePlant, this); }       // +0x44
    const std::vector<Item*>& list60() const { return world_->related(Rel::SiteList60, this); }      // +0x60
    const std::vector<Item*>& links() const { return world_->related(Rel::SiteLink, this); }         // +0x7c (CPath)
    const std::vector<Item*>& fx() const { return world_->related(Rel::SiteFx, this); }              // +0x98

    // DAT_100c3224: lights of the current room on/off (World::state().lightsOn); written into every site slot,
    // loaded from the last one
    int32_t lightsOn() const { return world_->state().lightsOn; }
    void setLightsOn(int32_t v) { world_->state().lightsOn = v; }

protected:
    // CSiteVariant base: FUN_1000c160 only builds the five lists; everything else is zero
    CSite(World& world, u32 ctorAddr, const char* className, const char* name);

    World* world_;   // port only: the owner of the relation lists
    // +0x00 vtable
    Biome* m_pBiome = nullptr;            // +0x04
    CBiomeCel* m_pCel = nullptr;          // +0x08
    CFeature* m_pFeature = nullptr;       // +0x0c
    uint32_t m_dwFlags = 0;               // +0x10
    uint16_t m_wState = 0;                // +0x14 light/display state (CSite::UpdateDisplayState)
    double m_dPosX = 0.0;                 // +0x18 (position on the map; guess)
    double m_dPosY = 0.0;                 // +0x20
    // +0x28 CObList features, +0x44 plants, +0x60 list60, +0x7c links, +0x98 FX: World relations
    std::string m_szDaySound;             // +0xb4
    std::string m_szNightSound;           // +0xb8 NULL when equal to the day sound
    std::string m_szDeadSound;            // +0xbc NULL when equal to the day sound
    DllPtr m_pBuf[4] = {{0}, {0}, {0}, {0}};   // +0xc0, +0xc4, +0xc8, +0xcc pointers to shared buffers
    std::string m_szPalette;              // +0xd0 (pointer to a DLL string constant; empty = NULL)
    // +0xd4 char[12] name = Item::name()
    uint32_t m_dwUnk0e0 = 0;              // +0xe0 ctor argument 1 (283..), meaning not determined
    double m_dUnk0e8 = 0.0;               // +0xe8 saved, builder sets -5.0 on two sites
    double m_dUnk0f0 = 0.0;               // +0xf0 saved, builder sets 5.0 on those sites
    int32_t m_dwUnk0f8 = 0;               // +0xf8
    int32_t m_dwUnk0fc = 0;               // +0xfc
};

class CSiteVariant final : public CSite {
public:
    // dll:10024f60. a3 is a dword stored at +0xb8 (always 0).
    CSiteVariant(World& world, const char* name, const char* palette, const char* sound, int32_t dwUnk0b8,
                 DllPtr pBuf);
    Item* siteCel() const override { return m_pCel; }   // null
};

} // namespace biosys::game
