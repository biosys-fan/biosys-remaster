// Power features: CSolarGen, CWindmill, CFuseBox. Notes: simulation.md sections 2.3 and 8.2.
#pragma once
#include "game/feature.h"

namespace biosys::game {

class CSolarGen final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:10003480
    CSolarGen(World& w, const char* name, i32 a1);
    // m_dPowerUsage (+0x48) holds the generation: sunlight x 1/36 per game second (100 units per hour at full sun)
    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:10003590
    void update(i32 now) override;                                       // +0x58 dll:10003670
    void activate() override;                                            // +0x78 dll:100035f0
    void deactivate() override;                                          // +0x7c dll:10003610
    void startRunning() override {}                                      // +0x80 dll:10003650 (empty)
    void stopRunning() override {}                                       // +0x84 dll:10003660 (empty)
};

class CWindmill final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:10070c40
    CWindmill(World& w, const char* name, i32 a1);
    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:10070d50
    void update(i32 now) override;                                       // +0x58 dll:10070e50
    void activate() override;                                            // +0x78 dll:10070db0
    void deactivate() override;                                          // +0x7c dll:10070dd0
    void startRunning() override;                                        // +0x80 dll:10070e10 (FX id on)
    void stopRunning() override;                                         // +0x84 dll:10070e30 (FX id off)
};

class CFuseBox final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:10026b30. `site` is the name of the CSite whose circuit the box carries; `a3` the power mask
    // (the circuit), `a4` the value stored at +0x5c.
    CFuseBox(World& w, const char* name, u32 a1, const char* site, i32 a3, i32 a4);
    void saveSlot(SlotWriter& w) const override;   // dll:10026c70
    void loadSlot(SlotReader& r) override;         // dll:10026cc0

    CSite* site() const { return m_pSite; }
    u32 fuseMask() const { return m_dwPowerMask; }   // +0x14: the fuses that are in and not blown (low 24 bits) + circuit bits
    void setFuseMask(u32 m) { m_dwPowerMask = m; }
    u32 fusesPresent() const { return unk_5c; }      // +0x5c: bit n-1 set = fuse n is plugged in (also blown ones)
    u32 fireState() const { return unk_60; }         // +0x60: fire stage bits (1 started, 2 4 8 0x10 escalation)
    i32 fireTime() const { return unk_64; }          // +0x64: game time of the last escalation

    void update(i32 now) override;                                       // +0x58 dll:10027450
    void onApply(GameObj& object, const std::string& hotspot) override;  // +0x44 dll:100272a0 (an extinguisher used)
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:10026ff0 (feats_fusebox.cpp: b_connect, the fuse holes)
    void display(int redraw) override;                                   // +0x5c dll:10026d10 (feats_fusebox.cpp: the fuses, the blown ones, the lamps)
    void slot60(const char* page, int arg) override;                     // +0x60 dll:10026ed0 (feats_fusebox.cpp: the power images and fire hotspots)

private:
    void markBurntFuses();  // dll:10027580
    CSite* m_pSite;         // +0x58
    i32 unk_5c;             // +0x5c
    i32 unk_60 = 0;         // +0x60
    i32 unk_64 = 0;         // +0x64
};

} // namespace biosys::game
