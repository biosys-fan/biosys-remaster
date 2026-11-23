// Climate machines: CHeater, CRainMachine, CMister, CAirMixer, CFan, CVent. Notes: simulation.md section 8.2.
#pragma once
#include "game/feature.h"

namespace biosys::game {

class CHeater final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:10016a40. The "off" arguments get +2.0 added by the constructor; `unused1`/`unused2` are arguments the
    // original never reads (param_10/11, param_15/16).
    CHeater(World& w, const char* name, u32 a1, u32 a2, u16 id, Biome* biome, double dayOn, double dayOffArg,
            double unused1, double nightOn, double nightOffArg, double unused2, double powerUsage, i32 working,
            i32 on);
    void saveSlot(SlotWriter& w) const override;   // dll:10016ce0
    void loadSlot(SlotReader& r) override;         // dll:10016d50

    bool setParam(i32 id, i32 value, double d) override;                 // +0x18 dll:10016ba0 (ids 100..0x67: the four thermostat set-points)
    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:10016c40
    void update(i32 now) override;                                       // +0x58 dll:10017330
    void thermostat();                                                   // dll:100173c0
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:10017220 (CHeater::OnClick: the +/- hotspots)
    void display(int redraw) override;                                   // +0x5c dll:10016e70 (disp_features.cpp)

    const Biome* biome() const { return m_pBiome; }
    double dayOn() const { return m_dDayOn; }
    double dayOff() const { return m_dDayOff; }
    double nightOn() const { return m_dNightOn; }
    double nightOff() const { return m_dNightOff; }

private:
    Biome* m_pBiome;            // +0x58 biome whose cels it heats
    double m_dDayOn;            // +0x60 thermostat day switch-on temperature
    double m_dDayOff;           // +0x68 day switch-off temperature (argument + 2.0)
    double m_dNightOn;          // +0x70
    double m_dNightOff;         // +0x78 (argument + 2.0)
};

class CRainMachine final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:10025dc0. `osmTank` is the clean-water tank (+0x60), `condTank` the condensate tank (+0x5c).
    CRainMachine(World& w, const char* name, u32 a1, u32 a2, const char* osmTank, const char* condTank, u16 id);
    void saveSlot(SlotWriter& w) const override;   // dll:10025fd0
    void loadSlot(SlotReader& r) override;         // dll:10026020

    CWaterTank* sourceTank() const { return m_pSourceTank; }
    CWaterTank* condTank() const { return m_pCondTank; }
    CWaterTank* osmTank() const { return m_pOsmTank; }

    bool setParam(i32 id, i32 value, double d) override;                 // +0x18 dll:10025f10
    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:10025f80
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:10026190
    void display(int redraw) override;                                   // +0x5c dll:10026060
    void update(i32 now) override;                                       // +0x58 dll:10026210
    void statusMessage(i32 id) override;                                 // +0x64 dll:10026160
    void startRunning() override;                                        // +0x80 dll:10026250
    void stopRunning() override;                                         // +0x84 dll:10026280

private:

    CWaterTank* m_pSourceTank;  // +0x58 tank the rain is taken from (= +0x5c or +0x60)
    CWaterTank* m_pCondTank;    // +0x5c condensate tank (code 0x61)
    CWaterTank* m_pOsmTank;     // +0x60 clean tank (code 0x60)
};

class CMister final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:1002a480. `sensor` is the name of a CBiomeSensor feature.
    CMister(World& w, const char* name, u32 a1, u32 a2, const char* sensor, const char* osmTank,
            const char* condTank, u16 id);
    // saved by CFeature::Save / Load (dll:10019800 / 10019890)

    CFeature* sensor() const { return m_pSensor; }
    CWaterTank* sourceTank() const { return m_pSourceTank; }
    bool misting() const { return m_bMisting != 0; }

    bool setParam(i32 id, i32 value, double d) override;                 // +0x18 dll:1002a620
    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:1002a6c0
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:1002a840
    void display(int redraw) override;                                   // +0x5c dll:1002a740
    void update(i32 now) override;                                       // +0x58 dll:1002a8c0
    void statusMessage(i32 id) override;                                 // +0x64 dll:1002a810
    void startRunning() override;                                        // +0x80 dll:1002a950
    void stopRunning() override;                                         // +0x84 dll:1002a9b0

private:
    i32 m_bMisting = 0;         // +0x58 set while the mist runs (StartRunning), not saved
    CFeature* m_pSensor;        // +0x5c CBiomeSensor
    CWaterTank* m_pSourceTank;  // +0x60 (= +0x68)
    CWaterTank* m_pCondTank;    // +0x64
    CWaterTank* m_pOsmTank;     // +0x68
    double m_dHumidityOn;       // +0x70 activates below this relative humidity (70.0)
    double m_dHumidityOff;      // +0x78 deactivates above (90.0)
};

class CAirMixer final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:10029e80. The three cel names may be null.
    CAirMixer(World& w, const char* name, u32 a1, u32 a2, u16 id, const char* cel1, const char* cel2,
              const char* cel3);

    CBiomeCel* cel(int i) const { return m_pCel[i]; }

    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:1002a0a0
    void display(int redraw) override;                                   // +0x5c dll:1002a180
    void startRunning() override;                                        // +0x80 dll:1002a350 (+2 wind speed in each cel)
    void stopRunning() override;                                         // +0x84 dll:1002a3c0

private:
    CBiomeCel* m_pCel[3];       // +0x58, +0x5c, +0x60 CBiomeCel, null when the name was null
};

class CFan final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:100174d0. `airFlowId` is stored at +0x58 (the id at +0x18 stays 0).
    CFan(World& w, const char* name, u32 a1, u32 a2, u16 airFlowId);
    void saveSlot(SlotWriter& w) const override;   // dll:10017600
    void loadSlot(SlotReader& r) override;         // dll:10017620

    u16 airFlowId() const { return m_wAirFlowId; }

    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:10017590 (FEATURE_AIR_FLOW)
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:10017650
    void display(int redraw) override;                                   // +0x5c dll:10017720
    void startRunning() override;                                        // +0x80 dll:100177d0
    void stopRunning() override;                                         // +0x84 dll:10017830

private:
    u16 m_wAirFlowId;           // +0x58 id of the air flows it switches
};

class CVent final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:10062750
    CVent(World& w, const char* name, i32 a1);
    void saveSlot(SlotWriter& w) const override;   // dll:10062880
    void loadSlot(SlotReader& r) override;         // dll:100628c0

    double shownAirFlow() const { return m_dShownAirFlow; }
    double airFlowTarget() const { return m_dAirFlowTarget; }
    // CAirFlow::TurnOn (dll:1005af40) adds / removes the flow's airflow to the target (+0x60) of its vent
    void adjustTarget(double delta) { m_dAirFlowTarget += delta; }

    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:10062850 (FEATURE_AIR_FLOW)
    void update(i32 now) override;                                       // +0x58 dll:10062940

private:
    double m_dShownAirFlow;     // +0x58
    double m_dAirFlowTarget;    // +0x60
};

} // namespace biosys::game
