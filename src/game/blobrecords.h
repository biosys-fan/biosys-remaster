// The fixed records of the game-logic blob (saveblob.md section 4) as typed classes. Each translates the
// Save/Load pair of the original; a load failure of the original (returns false) is a biosys::Error here.
#pragma once
#include "game/slot.h"

namespace biosys::game {

// CGame::Save dll:10008db0 / CGame::Load dll:10009080 -- first 121 bytes
struct BlobHeader final : Record {
    static constexpr size_t kSize = 121;
    static constexpr u32 kDllSaveVersion = 0x23;
    std::string magic = "JON IS A YOYO";        // 13 bytes, no NUL
    std::vector<u8> description = std::vector<u8>(100, 0);   // char[100]: "Saved Game: Day N, HH:MM, <site>, NN% completed"
    u32 dllSaveVersion = kDllSaveVersion;
    u32 milestones = 0;                          // story object +0x0c: milestone bit mask
    std::string descriptionText() const;
    void saveSlot(SlotWriter& w) const override;
    void loadSlot(SlotReader& r) override;
};

// CWorld::Save dll:10011710 / FUN_10012590 -- the 256-byte world header (member names follow CWorld+offset)
struct WorldHeader final : Record {
    static constexpr size_t kSize = 256;
    double m_dBlueSky = 0;                       // world+0x270
    i32 m_dwStamp_1c8 = 0;                       // world+0x1c8
    double m_dDayMaxExtTemp = 0;                 // +0x210
    double m_dDayMaxExtTempTarget = 0;           // +0x218
    double m_dDayMaxExtTempLo = 0;               // +0x220
    double m_dDayMaxExtTempHi = 0;               // +0x228
    double m_dNightMinExtTemp = 0;               // +0x230
    double m_dNightMinExtTempTarget = 0;         // +0x238
    double m_dNightMinExtTempLo = 0;             // +0x240
    double m_dNightMinExtTempHi = 0;             // +0x248
    double m_dExtTemp = 0;                       // +0x208
    double m_dPower = 0;                         // +0x1d0
    u32 m_dwPowerFlags = 0;                      // +0x1d8
    double m_dPowerGeneration = 0;               // +0x1e8
    double m_dPowerDemand = 0;                   // +0x1e0
    double m_dSunlight = 0;                      // +0x290
    double m_dWindSpeed = 0;                     // +0x250
    double m_dWindSpeedTarget = 0;               // +0x258
    double m_dWindSpeedLo = 0;                   // +0x260
    double m_dWindSpeedHi = 0;                   // +0x268
    i32 m_dwStamp_1c4 = 0;                       // +0x1c4
    i32 m_dwStamp_184 = 0;                       // +0x184 (CWorld::Save writes it twice; the loader's second store wins)
    i32 m_dwStamp_164 = 0;                       // +0x164
    i32 m_dwStamp_94 = 0;                        // +0x94
    i32 m_dwStamp_1c0 = 0;                       // +0x1c0
    double unk_298 = 0;                          // +0x298
    i32 unk_2a0 = 0;                             // +0x2a0
    std::vector<u8> uninit_tail = std::vector<u8>(68, 0);   // rest of the malloc(0x100) buffer: heap garbage, carried verbatim
    void saveSlot(SlotWriter& w) const override;
    void loadSlot(SlotReader& r) override;
};

// CTimeline::Save dll:100151c0 / Load dll:10015250
struct Timeline final : Record {
    static constexpr size_t kSize = 16;
    i32 m_dwTicksPerGameSec = 0;                 // timeline+0x0c (the ctor sets 0x28; -1 in NewGame.sav)
    i32 m_dwTime = 0;                            // +0x04 game seconds
    i32 m_dwDay = 0;                             // +0x08
    i32 m_dwTimeOfDay = 0;                       // +0x10
    // CTimeline::Load also stamps +0x28 with GetTickCount() and runs FUN_10015630 (clock string); neither is saved state.
    void saveSlot(SlotWriter& w) const override;
    void loadSlot(SlotReader& r) override;
};

// CSubTechHandler::Save dll:10022d10 / Load dll:10022d90
struct SubTechHandler final : Record {
    static constexpr size_t kSize = 16;
    i32 m_dwArrivalTime = 0;                     // CSubTechHandler+0x28; -1 = none
    void saveSlot(SlotWriter& w) const override;
    void loadSlot(SlotReader& r) override;
};

// CEngineFX::Save FUN_1005ff30 / Load FUN_1005ffb0
struct EngineFx final : Record {
    static constexpr size_t kSize = 16;
    u16 m_wState = 0;                            // CEngineFX+0x568
    std::vector<u8> uninit_tail = std::vector<u8>(14, 0);   // rest of malloc(16): heap garbage, carried verbatim
    void saveSlot(SlotWriter& w) const override;
    void loadSlot(SlotReader& r) override;
};

// "THE END" written by CGame::Save dll:10008db0, checked by CGame::Load dll:10009080
struct Trailer final : Record {
    static constexpr size_t kSize = 7;
    void saveSlot(SlotWriter& w) const override;
    void loadSlot(SlotReader& r) override;
};

} // namespace biosys::game
