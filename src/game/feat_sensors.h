// Sensor features: CTankSensor, CBiomeSensor (and CLung, which derives from it), COceanProbe.
// Slot layouts: saveblob.md 5.9; member meanings: simulation.md section 9.
#pragma once
#include "game/feature.h"
#include <array>

namespace biosys::game {

class CTankSensor final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:100262d0
    CTankSensor(World& w, const char* name, u32 a1, i32 a2, CWaterTank* tank, u16 id);
    void saveSlot(SlotWriter& w) const override;   // dll:100264d0
    void loadSlot(SlotReader& r) override;         // dll:10026580

    CWaterTank* tank() const { return m_pTank; }
    u32 alarms() const { return m_dwAlarms; }
    double tempReading() const { return m_dTempReading; }
    double purityReading() const { return m_dPurityReading; }
    double levelReading() const { return m_dLevelReading; }

    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:10026400
    void update(i32 now) override;                                       // +0x58 dll:10026770
    void onEnterPage(const std::string&) override {}                     // +0x34 dll:10026760 (empty)
    void onPageDrawn(const std::string&) override { display(0); }        // +0x38 dll:100268a0
    void display(int) override {}                                        // +0x5c dll:100268b0: `if (+0x54) CWorld::HasPower(mask)`, result unused

private:

    CWaterTank* m_pTank;              // +0x58 watched
    // The save-format notes (saveblob.md) label the six doubles +0x60..+0x88 as reading / unk pairs; Update dll:10026770
    // shows: +0x60 temperature, +0x68 purity, +0x70 level readings, +0x78 / +0x80 / +0x88 their error ranges.
    double m_dTempReading = 0;        // +0x60 (saved as m_dTempReading)
    double m_dPurityReading = 0;      // +0x68 (saved as unk_68)
    double m_dLevelReading = 0;       // +0x70 (saved as m_dPurityReading)
    double m_dTempError = 0;          // +0x78 (saved as unk_78)
    double m_dPurityError = 0;        // +0x80 (saved as m_dLevelReading)
    double m_dLevelError = 0;         // +0x88 (saved as unk_88)
    u32 m_dwAlarms = 0;               // +0x90
};

class CBiomeSensor : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:1005d300. `cel` is the biome cel the probe reads (the site cel of its site), `file` a name of at most
    // 11 characters (".sen" file of the original), `thresholds` the table pointer the original stores at +0x1c8,
    // `thresholds` the DLL address of the 8-double alarm table the original stores as a pointer at +0x1c8 (airT lo/hi, soilT
    // lo/hi, humidity lo/hi, soil water lo/hi; tables 100b7948.. in simulation.md section 9.1), `rainToday` the initial value of the rainfall counter times 360 (the original multiplies by 1/360).
    CBiomeSensor(World& w, const char* name, u32 a1, u32 a2, CBiomeCel* cel, u16 id, const char* file, DllPtr thresholds,
                 double rainToday);
    void saveSlot(SlotWriter& w) const override;   // dll:1005d810
    void loadSlot(SlotReader& r) override;         // dll:1005da10

    CBiomeCel* cel() const { return m_pCel; }
    u32 alarms() const { return m_dwAlarms; }
    double pressureReading() const { return m_dPressureReading; }
    double airTempReading() const { return m_dAirTempReading; }
    double co2Reading() const { return m_dCO2Reading; }
    double humidityReading() const { return m_dHumidityReading; }
    double lightReading() const { return m_dLightReading; }
    double oxygenReading() const { return m_dOxygenReading; }
    double pollutionReading() const { return m_dPollutionReading; }
    double soilTempReading() const { return m_dSoilTempReading; }
    double soilPHReading() const { return m_dSoilPHReading; }
    double soilWaterReading() const { return m_dSoilWaterReading; }
    double windSpeedReading() const { return m_dWindSpeedReading; }

    // CBiomeSensor::OnNewDay dll:1005e860 (also run once by the constructor)
    void onNewDay();

    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:1005d620
    void update(i32 now) override;                                       // +0x58 dll:1005e0a0
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:1005dfe0 (click sound)
    void onEnterPage(const std::string& page) override;                  // +0x34 dll:1005e000
    void onLeavePage() override;                                         // +0x40 dll:1005e970
    void display(int redraw) override;                                   // +0x5c dll:1005e9a0 (disp_features.cpp)
    bool onMediaClosed(int handle, unsigned mask) override;              // +0xa0 dll:1005fba0 (an alarm icon's animation was closed)
    void onPowerOn() override;                                           // +0x68 dll:1005fd70 (clears the alarms)
    void onPowerOff() override;                                          // +0x6c dll:1005fd80 (stops the alarm FX)
    u32 thresholdTable() const { return m_dwThresholdAddr; }

protected:
    // What CLung's call of FUN_1005d220 plus its own Init do: only the CFeature part exists, every member stays 0.
    CBiomeSensor(World& w, u32 ctorAddr, const char* className, const char* name, u32 flags, u32 powerMask);
    // The saved part of CBiomeSensor after the CFeature block (the class saves it, CLung does not).
    void saveSensor(SlotWriter& w) const;
    void loadSensor(SlotReader& r);

    CBiomeCel* m_pCel = nullptr;           // +0x58
    std::string m_szFile;                  // +0x5c char[12]
    double m_dPressureReading = 0;         // +0x68
    double m_dAirTempReading = 0;          // +0x70
    double m_dCO2Reading = 0;              // +0x78
    double m_dHumidityReading = 0;         // +0x80
    double m_dLightReading = 0;            // +0x88
    double m_dOxygenReading = 0;           // +0x90
    double m_dPollutionReading = 0;        // +0x98
    double m_dSoilTempReading = 0;         // +0xa0
    double m_dSoilPHReading = 0;           // +0xa8
    double m_dSoilWaterReading = 0;        // +0xb0
    double m_dWindSpeedReading = 0;        // +0xb8
    double m_dRainToday = 0;               // +0xc0 rainfall counter of today
    double m_dRainYesterday = 0;           // +0xc8
    double m_dPressureError = 0;           // +0xd0 (not saved)
    double m_dAirTempError = 0;            // +0xd8
    double m_dCO2Error = 0;                // +0xe0
    double m_dHumidityError = 0;           // +0xe8
    double m_dLightError = 0;              // +0xf0
    double m_dOxygenError = 0;             // +0xf8
    double m_dPollutionError = 0;          // +0x100
    double m_dSoilTempError = 0;           // +0x108
    double m_dSoilPHError = 0;             // +0x110
    double m_dSoilWaterError = 0;          // +0x118
    double m_dWindSpeedError = 0;          // +0x120
    double m_dMin[10] = {};                // +0x128 daily minimum of each reading (reset to 100.0 by OnNewDay)
    double m_dMax[10] = {};                // +0x178 daily maximum of each reading (reset to 0.0)
    u32 m_dwThresholdAddr = 0;             // +0x1c8 pointer to the alarm thresholds (DLL data address; see thresholds())
    const double* m_pThresholds = nullptr; // the 8 doubles at that address
    u32 m_dwAlarms = 0;                    // +0x1cc
    // The sprite animation handles of the alarm icons: DLL globals DAT_100bbd68..100bbda8 in the original (one set for all
    // sensors; the port keeps them per sensor: they are reset to -1 whenever a page without the panel is shown)
    std::array<int, 13> m_hAlarmAnim = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

    static const double* thresholds(u32 addr);   // the DLL's tables 100b7948 / 100b7988 / 100b79c8 / 100b7a08 / 100b7a48 / 100b7a88
    void setErrorRanges();                 // the constants the constructors of CBiomeSensor and CLung both store
};

// CLung (dll:1000fe10) derives from CBiomeSensor; its Save/Load (dll:100102b0 / 100102d0) handle the CFeature block only.
class CLung final : public CBiomeSensor {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // `a4`/`a5` are the two flow ids, `src`/`dst` the SetParam codes 0x13 / 0x14 (0x15 TRF, 0x16 TS, 0x17 TDB,
    // 0x18 TO, 0x1a: the lung's own biome).
    CLung(World& w, const char* name, u32 a1, u32 a2, u16 id, u16 id2, i32 src, i32 dst, DllPtr thresholds);
    void saveSlot(SlotWriter& w) const override;   // dll:100102b0
    void loadSlot(SlotReader& r) override;         // dll:100102d0

    CBiomeCel* targetCel() const { return m_pTargetCel; }
    i32 mode() const { return m_dwMode; }
    void setMoving(i32 v) { m_dwBellowsDir = v; }   // the store into +0x1dc CLungAirFlow::Update makes (sim.h hooks.lungSetMoving)

    bool setParam(i32 id, i32 value, double d) override;                 // +0x18 dll:10010050
    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:100101e0
    void update(i32 now) override;                                       // +0x58 dll:10010a30
    void display(int redraw) override;                                   // +0x5c dll:100102f0 (disp_features.cpp)
    void startRunning() override;                                        // +0x80 dll:100108e0
    void stopRunning() override;                                         // +0x84 dll:100109b0

private:

    CBiomeCel* m_pTargetCel = nullptr;  // +0x1d0 cel the air is sent to
    i32 m_dwMode = 0;                   // +0x1d4 last value of SetParam 0x14 (0x11 draw, 0x15..0x18 send, 0x1a push)
    u16 m_wId2 = 0;                     // +0x1d8
    i32 m_dwBellowsDir = 0;             // +0x1dc set to 1 by the constructor (meaning unknown)
};

class COceanProbe final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:10027610
    COceanProbe(World& w, const char* name, u32 a1, i32 a2, const char* file);
    void saveSlot(SlotWriter& w) const override;   // dll:100278f0
    void loadSlot(SlotReader& r) override;         // dll:10027a20

    u32 alarms() const { return m_dwAlarms; }
    double tempReading() const { return m_dTempReading; }
    double phReading() const { return m_dPHReading; }
    double salinityReading() const { return m_dSalinityReading; }
    double clarityReading() const { return m_dClarityReading; }
    double oxygenReading() const { return m_dOxygenReading; }
    double levelReading() const { return m_dLevelReading; }

    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:100277d0
    void update(i32 now) override;                                       // +0x58 dll:10027de0
    void onEnterPage(const std::string& page) override;                  // +0x34 dll:10027d90 (disp_features.cpp)
    void display(int redraw) override;                                   // +0x5c dll:10028090 (disp_features.cpp)
    bool onMediaClosed(int handle, unsigned mask) override;              // +0xa0 dll:10028a30 (an alarm icon's animation was closed)

private:
    std::string m_szFile;              // +0x58 char[16]
    double m_dTempReading = 0;         // +0x68
    double m_dPHReading = 0;           // +0x70
    double m_dSalinityReading = 0;     // +0x78
    double m_dClarityReading = 0;      // +0x80
    double m_dOxygenReading = 0;       // +0x88
    double m_dLevelReading = 0;        // +0x90
    double m_dTempError = 0;           // +0x98
    double m_dPHError = 0;             // +0xa0
    double m_dSalinityError = 0;       // +0xa8
    double m_dClarityError = 0;        // +0xb0
    double m_dOxygenError = 0;         // +0xb8
    double m_dLevelError = 0;          // +0xc0
    u32 m_dwAlarms = 0;                // +0xc8
    std::array<int, 11> m_hAlarmAnim = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};   // DLL globals DAT_100a3128.. (see CBiomeSensor)
};

} // namespace biosys::game
