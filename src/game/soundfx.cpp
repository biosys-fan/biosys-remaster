#include "game/soundfx.h"
#include "game/site.h"
#include "game/world_ctors.gen.h"

namespace biosys::game {

namespace {
std::string copyChars(const char* s, size_t cap, const char* what) {
    std::string r(s ? s : "");
    if (r.size() >= cap) throw Error(std::string(what) + " too long: " + r);   // the original copies unchecked
    return r;
}

}

// dll:100060f0
void CLocalFX::saveBase(SlotWriter& w) const { w.writeI32(m_bOn); }

// dll:10006120
void CLocalFX::loadBase(SlotReader& r) {
    m_bOn = r.readI32("m_dwState");
    m_bPlaying = 0;
}

// dll:10077e40 (CLocalFX default constructor dll:10006020 + vtable); the members the original leaves
// uninitialised are zero here
CSoundFX::CSoundFX(u32 ctorAddr, const char* className, World& w, std::string itemName)
    : CLocalFX(ctorAddr, className, std::move(itemName)), m_pWorld(&w) {}

// dll:10077f10
CSoundFX::CSoundFX(World& w, const char* file, const char* a1, const char* siteName, Biome* biome, uint16_t id,
                   uint16_t mode, int32_t volumePage, int32_t volumeSite, int32_t param98, const char* panTarget,
                   int32_t on)
    : CSoundFX(0x10077f10, "CSoundFX", w, std::string(file) + "@" + siteName) {
    m_szFile = copyChars(file, 100, "sound FX file name");
    m_szPage = copyChars(a1, 12, "sound FX page name");
    m_pSite = w.findSite(siteName);   // original logs "duff site name in ..." when null
    m_wId = id;
    m_pBiome = biome;
    m_bOn = on;
    m_dwVolumeAtPage = volumePage;
    m_dwVolumeAtSite = volumeSite;
    m_dwParam98 = param98;
    m_wMode = mode;
    m_szPanTarget = panTarget ? copyChars(panTarget, 12, "sound FX pan target") : std::string();
    m_bPlaying = 0;
    m_dwHandle = -1;
}

// dll:10073bd0
CRainFX::CRainFX(World& w, const char* file, const char* siteName, Biome* biome, uint16_t id, int32_t volume,
                 int32_t param98)
    : CSoundFX(0x10073bd0, "CRainFX", w, std::string(file) + "@" + biome->name) {
    m_szFile = copyChars(file, 100, "rain FX file name");
    m_pSite = w.findSite(siteName);
    m_wId = id;
    m_pBiome = biome;
    m_bOn = 0;
    m_bPlaying = 0;
    m_bFilterActive = 0;
    m_dwVolumeAtSite = volume;
    m_dwParam98 = param98;
    m_szPanTarget.clear();   // +0x9c = 0
    m_dwHandle = -1;
}

// dll:100780d0: the owner site's 11-byte name (site +0xd4) is written only while the site pointer is set
void CSoundFX::saveSlot(SlotWriter& w) const {
    saveBase(w);
    if (m_pSite) w.writeStr(m_pSite->name(), 11);
    else w.skip(11);
}

// dll:10078120
void CSoundFX::loadSlot(SlotReader& r) {
    loadBase(r);
    std::string site = r.readStr("m_szOwnerSite", 11);
    if (!site.empty()) m_pSite = m_pWorld->findSite(site.c_str());
    r.finishPad();
}

BIOSYS_REGISTER_CTOR(10077f10, [](World& w, const char* name, const char* a1, const char* a2, Biome* a3, uint16_t a4,
                                  uint16_t a5, int32_t a6, int32_t a7, int32_t a8, const char* a9,
                                  int32_t a10) -> Item* {
    return w.adopt(std::make_unique<CSoundFX>(w, name, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10));
});

BIOSYS_REGISTER_CTOR(10073bd0, [](World& w, const char* name, const char* a1, Biome* a2, uint16_t a3, int32_t a4,
                                  int32_t a5) -> Item* {
    return w.adopt(std::make_unique<CRainFX>(w, name, a1, a2, a3, a4, a5));
});

} // namespace biosys::game
