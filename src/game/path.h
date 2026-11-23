// CPath: a walkable link from one site to another ("L_R1R3"); built by FUN_10039130, owned by the source
// site's link list (CSite+0x7c). Named after the string "CPath::CPath" in the constructor's error message.
// Not stored in a table; only its flags dword is saved, inside the owner site's slot (dll:100011c0/100011e0).
#pragma once
#include "game/world.h"

namespace biosys::game {

class CPath final : public Item {
public:
    // dll:10001000
    CPath(CSite* from, const char* toSiteName, CSite* to, int32_t dwUnk0c, const char* pathName, int32_t dwUnk20,
          int32_t dwFlags, double dUnk28);

    bool typed() const override { return true; }
    void saveSlot(SlotWriter&) const override;   // not a slot item: throws
    void loadSlot(SlotReader&) override;

    void saveFlags(SlotWriter& w) const;   // dll:100011c0
    void loadFlags(SlotReader& r, const char* memberName);   // dll:100011e0

    CSite* from() const { return m_pFrom; }
    CSite* to() const { return m_pTo; }     // null if the destination site name was not found
    const std::string& pathName() const { return m_szName; }
    uint32_t flags() const { return m_dwFlags; }
    void setFlags(uint32_t f) { m_dwFlags = f; }
    int32_t unk0c() const { return m_dwUnk0c; }
    uint32_t unk20() const { return m_dwUnk20; }
    double unk28() const { return m_dUnk28; }
    int32_t unk30() const { return m_dwUnk30; }
    int32_t unk34() const { return m_dwUnk34; }
    void setUnk30(int32_t v) { m_dwUnk30 = v; }
    void setUnk34(int32_t v) { m_dwUnk34 = v; }

private:
    // +0x00 vtable (1008f000)
    CSite* m_pFrom;          // +0x04 ctor argument 0: the owner site
    CSite* m_pTo;            // +0x08 CWorld::FindSite(argument 1)
    int32_t m_dwUnk0c;       // +0x0c ctor argument 2 (meaning not determined)
    std::string m_szName;    // +0x10 char[12] "L_R1R3"
    uint32_t m_dwFlags;      // +0x1c ctor argument 5, saved with the site
    uint32_t m_dwUnk20;      // +0x20 ctor argument 4 (0x400000 | bit)
    double m_dUnk28;         // +0x28 ctor argument 6
    int32_t m_dwUnk30;       // +0x30 -1
    int32_t m_dwUnk34;       // +0x34 -1
};

} // namespace biosys::game
