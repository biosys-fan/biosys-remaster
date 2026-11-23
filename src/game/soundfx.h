// The local FX (saved table `localfx`): looped wavs attached to a site or to a whole biome.
//   CObject 10094764 -> CLocalFX 1008f7a8 (size 0x1c; default constructor dll:10006020)
//     -> CSoundFX 100944c0 (size 0xa8)   dll:10077f10 (default constructor dll:10077e40 sets only the vtable)
//          -> CRainFX 100941d0 (size 0xac)   dll:10073bd0
// CRainFX re-uses CSoundFX's members and overrides Stop/Update (dll:10073d30 / 10073e20); the rain machines
// switch it by its id. Both share Save/Load dll:100780d0 / 10078120.
#pragma once
#include "game/world.h"

namespace biosys::game {

class CLocalFX : public Item {
public:
    uint16_t id() const { return m_wId; }
    bool on() const { return m_bOn != 0; }
    void setOn(bool v) { m_bOn = v ? 1 : 0; }
    bool playing() const { return m_bPlaying != 0; }
    void setPlaying(bool v) { m_bPlaying = v ? 1 : 0; }

protected:
    CLocalFX(u32 ctorAddr, const char* className, std::string name) : Item(ctorAddr, className, std::move(name)) {}
    void saveBase(SlotWriter& w) const;   // dll:100060f0
    void loadBase(SlotReader& r);         // dll:10006120

    // +0x00 vtable
    uint16_t m_wId = 0;       // +0x10 FX id the machines switch it by (SetFXState-like calls)
    int32_t m_bOn = 0;        // +0x14 BOOL, saved ("m_dwState")
    int32_t m_bPlaying = 0;   // +0x18 BOOL, cleared by Load
};

// dll:10077f10
class CSoundFX : public CLocalFX {
public:
    // `a1` is the page name copied to +4, `siteName` is looked up with CWorld::FindSite (a failed lookup is only logged
    // by the original: the pointer stays null).
    CSoundFX(World& w, const char* file, const char* a1, const char* siteName, Biome* biome, uint16_t id,
             uint16_t mode, int32_t volumePage, int32_t volumeSite, int32_t param98, const char* panTarget,
             int32_t on);

    bool typed() const override { return true; }
    void saveSlot(SlotWriter& w) const override;   // dll:100780d0
    void loadSlot(SlotReader& r) override;         // dll:10078120

    const std::string& file() const { return m_szFile; }
    CSite* site() const { return m_pSite; }
    Biome* biome() const { return m_pBiome; }
    int32_t handle() const { return m_dwHandle; }
    void setHandle(int32_t h) { m_dwHandle = h; }
    uint16_t mode() const { return m_wMode; }   // 1: heard on its own page (m_szPage), 2: heard near the site
    int32_t volumeAtPage() const { return m_dwVolumeAtPage; }
    int32_t volumeAtSite() const { return m_dwVolumeAtSite; }
    const std::string& page() const { return m_szPage; }
    const std::string& panTarget() const { return m_szPanTarget; }
    friend struct ObjFxAccess;     // obj_gear.cpp: the oxygen cylinder / alarm clock move their FX to the player's page

protected:
    CSoundFX(u32 ctorAddr, const char* className, World& w, std::string itemName);   // dll:10077e40, for CRainFX

    World* m_pWorld;               // not an original member: Load resolves the site name through it
    std::string m_szPage;          // +0x04 char[12] page name; compared with the player's page when m_wMode == 1
    std::string m_szFile;          // +0x1c char[100] wav file name
    int32_t m_dwHandle = -1;       // +0x80 engine sound handle
    uint16_t m_wMode = 0;          // +0x84 1 = audible on page m_szPage (or its 'U'-variant), 2 = audible near the site
    int32_t m_dwVolumeAtPage = 0;  // +0x88 volume (1/100 dB) in mode 1
    CSite* m_pSite = nullptr;      // +0x8c, Load re-resolves it from the saved name
    int32_t m_dwVolumeAtSite = 0;  // +0x90 volume in mode 2 (and the only volume of CRainFX)
    Biome* m_pBiome = nullptr;     // +0x94 CBiome*, may be null
    int32_t m_dwParam98 = 0;       // +0x98 (-10000 in the builder; not read by Start/Update; meaning not determined)
    std::string m_szPanTarget;     // +0x9c char[12] hotspot name the wav is panned to ("" = none)
};

// dll:10073bd0
class CRainFX final : public CSoundFX {
public:
    CRainFX(World& w, const char* file, const char* siteName, Biome* biome, uint16_t id, int32_t volume,
            int32_t param98);
    bool filterActive() const { return m_bFilterActive != 0; }   // +0xa8
    void setFilterActive(bool v) { m_bFilterActive = v ? 1 : 0; }

private:
    int32_t m_bFilterActive = 0;   // +0xa8 set while this FX has begun the rain page filter (Eng_BeginPageFilter)
};

} // namespace biosys::game
