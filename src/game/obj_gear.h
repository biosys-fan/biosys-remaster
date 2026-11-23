// The remaining object classes: tools, wearables and miscellany.
// Each class lists the vtable slots it overrides (the table of the class is in the comment: dll:addr of the vtable).
#pragma once
#include "game/gameobj.h"

namespace biosys::game {

class ObjOxyCylinder;

// "CMiscObject" of saveblob.md (keys, apple, wand, kukri, medipass, statue, crowbar, floptical): the constructor
// dll:10028c20 is that of CObject itself (it writes the same vtables as FUN_10028b90), so the true class is CObject.
// Save dll:10028d00, Load dll:10028d70 (the common block). Vtable 10091b70: no override.
class ObjMisc final : public GameObj {
public:
    ObjMisc(const char* name, i32 nameStringId, u32 flags, double weight);   // dll:10028c20
};

// CBabySynth: baby1-7, dbaby1-6, babylab. Save dll:1005a600, Load dll:1005a630. Vtable 100929c8.
class ObjBabySynth final : public GameObj {
public:
    ObjBabySynth(const char* name, i32 nameStringId);   // dll:1005a500
    void saveSlot(SlotWriter& w) const override;
    void loadSlot(SlotReader& r) override;
    void poke(u32 offset, unsigned bytes, u32 value, PokeOp op) override;
    u32 peek(u32 offset, unsigned bytes) const override;
    u16 state() const { return m_wState; }

    int use(Self& s) override;                                // CBabySynth::Use dll:1005a770
    int onKey(Self& s, int key, u32* allow) override;         // dll:1005a7f0
    int update(Self& s, i32 now) override;                    // CBabySynth::Update dll:1005a660
    void dumpMembers(ObjMembers& out) const override;

private:
    u16 m_wState = 0;          // +0x40 1 alive, 2 dead, 3 rotten (CBabySynth::Update dll:1005a660); saved as unk_40
    i32 m_dwTimeOfDeath = 0;   // +0x44 game time of death (never written by the constructor); saved as unk_44
};

// CFishingRod "fishrod". Save dll:1001b030, Load dll:1001b060. Vtable 10090d10.
class ObjFishingRod final : public GameObj {
public:
    ObjFishingRod(const char* name, i32 nameStringId);   // dll:1001af20
    void saveSlot(SlotWriter& w) const override;
    void loadSlot(SlotReader& r) override;
    i32 state() const { return m_dwState; }
    void setState(i32 v) { m_dwState = v; }

    int use(Self& s) override;                                           // dll:1001b090
    int onUsedOnHotspot(Self& s, const std::string& hotspot) override;   // dll:1001b190
    int onSector(Self& s, int sector) override;                          // dll:1001b400
    int update(Self& s, i32 now) override;                               // dll:1001b490
    int setHolding(Self& s) override;                                    // dll:1001b3b0
    void dumpMembers(ObjMembers& out) const override;

private:
    i32 m_dwState = 0;         // +0x40 baited (0 = no bait); saved as unk_40
};

// CBlowpipe "sb_blow" (dll:1005a170) and CBlowpipePick "sb_pick" (dll:10006200): common Save/Load only.
// Vtables 10092930 / 1008f7e8.
class ObjBlowpipe final : public GameObj {
public:
    ObjBlowpipe(const char* name, i32 nameStringId);
    int use(Self& s) override;                                           // dll:1005a2d0
    int onUsedOnHotspot(Self& s, const std::string& hotspot) override;   // dll:1005a370
    int setHolding(Self& s) override;                                    // dll:1005a280
    int clearHolding(Self& s) override;                                  // dll:1005a2b0
};
class ObjBlowpipePick final : public GameObj {
public:
    ObjBlowpipePick(const char* name, i32 nameStringId);
    int onUsedOnHotspot(Self& s, const std::string& hotspot) override;   // dll:10006350
    int setHolding(Self& s) override;                                    // dll:10006310
    int clearHolding(Self& s) override;                                  // dll:10006330
};

// CTracker "tracker" (dll:100268d0), CCompass "compass" (dll:1005bcf0), CSurvivalBag "survibag" (dll:100191e0),
// CPoncho "poncho" (dll:10002d90): common Save/Load only.
class ObjTracker final : public GameObj {
public:
    ObjTracker(const char* name, i32 nameStringId);
    int redraw(Self& s, int full) override;                              // dll:10026a30 (vtable 100918c0)
    int setHolding(Self& s) override;                                    // dll:100269e0
    int clearHolding(Self& s) override;                                  // dll:10026a10
    void dumpMembers(ObjMembers& out) const override;
private:
    i32 m_dwUnk40 = 0;         // +0x40 (-1) handle of the looped sweep sound, not saved
};
class ObjCompass final : public GameObj {
public:
    ObjCompass(const char* name, i32 nameStringId);
    int onSector(Self& s, int sector) override;                          // dll:1005beb0 (vtable 10092d78)
    int setHolding(Self& s) override;                                    // dll:1005be80
    void onPageEntered(Self& s, const std::string& page) override;       // dll:1005bd70 (+0x88)
};
class ObjSurvivalBag final : public GameObj {
public:
    ObjSurvivalBag(const char* name, i32 nameStringId);
    int use(Self& s) override;                                           // dll:100192f0 (vtable 10090a40)
    int pickUp(Self& s) override;                                        // dll:10019260
    int onDropped(Self& s, const std::string& hotspot) override;         // dll:10019370
    int onKey(Self& s, int key, u32* allow) override;                    // dll:10019280
    int setHolding(Self& s) override;                                    // dll:10019390
};
class ObjPoncho final : public GameObj {
public:
    ObjPoncho(const char* name, i32 nameStringId);
    int onWorn(Self& s) override;                                        // CPoncho::Wear dll:10002e20 (vtable 1008f158)
    int onUnworn(Self& s) override;                                      // dll:10002e70
};

// CTorch "torch" (dll:10022f70): common Save/Load only (dll:10023070 / dll:10023090). Vtable 10091298.
class ObjTorch final : public GameObj {
public:
    ObjTorch(const char* name, i32 nameStringId);
    // The oxygen cylinder the torch burns (set by COxyCylinder); the original stores the pointer at +0x44
    void setCylinder(ObjOxyCylinder* c) { m_pCylinder = c; }
    ObjOxyCylinder* cylinder() const { return m_pCylinder; }

    void afterLoad(Self& s) override;                                    // dll:100230b0
    int use(Self& s) override;                                           // CTorch::Use dll:100231e0
    int onUsedOnHotspot(Self& s, const std::string& hotspot) override;   // dll:10023100
    int update(Self& s, i32 now) override;                               // CTorch::Update dll:100232d0
    int setHolding(Self& s) override;                                    // dll:10023280
    int clearHolding(Self& s) override;                                  // dll:100232a0
    void dumpMembers(ObjMembers& out) const override;

private:
    i32 m_dwUnk40 = 0;                    // +0x40 (-1) handle of the burning sound, not saved
    ObjOxyCylinder* m_pCylinder = nullptr; // +0x44 (0), not saved
};

// CGoggles "goggles" (infra-red goggles). Save dll:1001d2b0, Load dll:1001d2f0. Vtable 10091020.
class ObjGoggles final : public GameObj {
public:
    ObjGoggles(const char* name, i32 nameStringId);   // dll:1001d1a0
    void saveSlot(SlotWriter& w) const override;
    void loadSlot(SlotReader& r) override;
    double charge() const { return m_dCharge; }
    bool on() const { return m_bOn != 0; }

    // CGoggles::SetPower dll:1001d780 (the story code calls it): charge = clamp(v, 0, 100), power state 0
    void setPower(double v);
    // CGoggles::CalcPowerState dll:1001d640
    void calcPowerState();
    // CGoggles::ShowPower dll:1001d6b0: the worn image "power0".."power5" of the charge
    void showPower(Self& s);
    // FUN_1001d600 dll:1001d600: (re)load the site's goggles palette; false when the goggles are flat (state 1)
    bool showPalette(Self& s);

    void dump(std::vector<std::string>& lines) const override;           // dll:1001d330
    int pickUp(Self& s) override;                                        // dll:1001d430
    int onWorn(Self& s) override;                                        // CGoggles::Wear dll:1001d450
    int onUnworn(Self& s) override;                                      // dll:1001d4d0
    int onUsedOnHotspot(Self& s, const std::string& hotspot) override;   // dll:1001d3f0 (the charging station)
    int update(Self& s, i32 now) override;                               // CGoggles::Update dll:1001d530
    int redraw(Self& s, int full) override;                              // dll:1001d3d0
    void dumpMembers(ObjMembers& out) const override;

private:
    double m_dCharge = 0;      // +0x40 power 0..100 (CGoggles::Update dll:1001d530)
    u8 m_byPowerState = 0;     // +0x48 1..6 (CGoggles::CalcPowerState dll:1001d640); not set by the constructor
    u8 m_byShownPowerState = 0;// +0x49 power state last shown
    i32 m_bOn = 0;             // +0x4c switched on; saved first, as unk_4c
};

// COxyMask "oxy_mask". Save dll:10076aa0, Load dll:10076ad0. Vtable 10094208.
class ObjOxyMask final : public GameObj {
public:
    ObjOxyMask(const char* name, i32 nameStringId);   // dll:10076a10
    void saveSlot(SlotWriter& w) const override;
    void loadSlot(SlotReader& r) override;
    double filter() const { return m_dFilter; }

    void describe(Self& s) override;                                     // dll:10076c40
    int onWorn(Self& s) override;                                        // COxyMask::Wear dll:10076b00
    int onUnworn(Self& s) override;                                      // COxyMask::Remove dll:10076b70
    int refill(Self& s, double* source, i16 code) override;              // COxyMask::Refill dll:10076ce0
    int update(Self& s, i32 now) override;                               // COxyMask::Update dll:10076bc0
    void dumpMembers(ObjMembers& out) const override;

private:
    double m_dFilter = 0;      // +0x40 reservoir 0..1 (0.98 at start)
};

// COxyCylinder "oxy_cyl". Save dll:10062af0, Load dll:10062b40. Vtable 10093248.
class ObjOxyCylinder final : public GameObj {
public:
    ObjOxyCylinder(const char* name, i32 nameStringId, Item* fx);   // dll:100629d0
    void saveSlot(SlotWriter& w) const override;
    void loadSlot(SlotReader& r) override;
    double contents() const { return m_dContents; }
    void setContents(double v) { m_dContents = v; }
    bool valveOpen() const { return m_bValveOpen != 0; }                 // +0x50 (CSynthCave::OnEnter dll:1000b740)

    void afterLoad(Self& s) override;                                    // dll:10062b90
    void describe(Self& s) override;                                     // COxyCylinder::Describe dll:10063090
    int use(Self& s) override;                                           // COxyCylinder::ToggleValve dll:10062e30
    int pickUp(Self& s) override;                                        // dll:10062d20
    int onDropped(Self& s, const std::string& hotspot) override;         // dll:10062d80
    int onUsedOnHotspot(Self& s, const std::string& hotspot) override;   // dll:10062ef0
    int refill(Self& s, double* source, i16 code) override;              // dll:10062f40
    int update(Self& s, i32 now) override;                               // COxyCylinder::Update dll:10062fa0
    int setHolding(Self& s) override;                                    // dll:10062df0
    void onPageEnter(Self& s, const std::string& page) override;         // dll:10062c60 (+0x80)
    void onDropZoneClicked(Self& s, const std::string& hotspot) override;   // dll:10063150 (+0x90)
    void removeFromScene(Self* s = nullptr) override;                    // dll:10063130
    void dumpMembers(ObjMembers& out) const override;

private:
    Item* m_pFx = nullptr;     // +0x40 the page sound FX whose cel receives the oxygen (CLocalFX "oxyhiss.wav")
    i32 m_bCarried = 0;        // +0x44 picked up (not saved, not set by the constructor)
    double m_dContents = 0;    // +0x48 0..2, drained while the valve is open (COxyCylinder::Update dll:10062fa0)
    i32 m_bValveOpen = 0;      // +0x50 saved as unk_50
    i32 m_dwUnk54 = 0;         // +0x54 saved as unk_54 (written before +0x50): the cylinder is attached to the torch
};

} // namespace biosys::game
