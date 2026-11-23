// CWaterTank (dll:1000ec80, 65 items of the tanks table) and COcean (dll:10029570, derives CWaterTank).
// Save formats: analysis/notes/saveblob.md 5.3. Members by object offset (simulation.md 6.1, 6.5).
#pragma once
#include "game/biomecel.h"
#include "game/world.h"

namespace biosys::game {

class Sim;

class CWaterTank : public Item {
public:
    // dll:1000ec80. `fillPercent` is the original's float parameter (param_5).
    CWaterTank(std::string name, int32_t bounded, double volume, float fillPercent, double oxygen, double ph,
               double salinity, double temp, double purity);
    bool typed() const override { return true; }
    void saveSlot(SlotWriter& w) const override;   // dll:1000eda0
    void loadSlot(SlotReader& r) override;         // dll:1000ee30

    double volume() const { return m_dVolume; }
    double waterVol() const { return m_dWaterVol; }
    bool bounded() const { return m_bBounded != 0; }
    double oxygen() const { return m_dOxygen; }
    double ph() const { return m_dPH; }
    double salinity() const { return m_dSalinity; }
    double temp() const { return m_dTemp; }
    double purity() const { return m_dPurity; }
    double newTemp() const { return m_dNewTemp; }
    double newSalinity() const { return m_dNewSalinity; }
    double newOxygen() const { return m_dNewOxygen; }
    double newPH() const { return m_dNewPH; }
    double newPurity() const { return m_dNewPurity; }
    // The flows move water directly (+0x18) and write the mixed values into the "new" members (CWaterFlow::Update)
    void setWaterVol(double v) { m_dWaterVol = v; }
    void setNewTemp(double v) { m_dNewTemp = v; }
    void setNewSalinity(double v) { m_dNewSalinity = v; }
    void setNewOxygen(double v) { m_dNewOxygen = v; }
    void setNewPH(double v) { m_dNewPH = v; }
    void setNewPurity(double v) { m_dNewPurity = v; }

    // Vtable +0x20 dll:1000f020: clamp the "new" values and make them current (CWorld::CommitWaterTanks)
    void commit();

protected:
    // What COcean's call of FUN_1000ebb0 does: only the vtable is set, every member stays at zero.
    CWaterTank(u32 ctorAddr, const char* className, std::string name);

    // +0x04 name (Item::name)
    double m_dVolume = 0;       // +0x10 capacity
    double m_dWaterVol = 0;     // +0x18 current volume
    int32_t m_bBounded = 0;     // +0x20 a CWaterFlow will not fill it past capacity
    // +0x24 not touched by the constructor
    double m_dTemp = 0;         // +0x28
    double m_dSalinity = 0;     // +0x30
    double m_dOxygen = 0;       // +0x38
    double m_dPH = 0;           // +0x40
    double m_dPurity = 0;       // +0x48
    double m_dNewTemp = 0;      // +0x50 value of the next commit
    double m_dNewSalinity = 0;  // +0x58
    double m_dNewOxygen = 0;    // +0x60
    double m_dNewPH = 0;        // +0x68
    double m_dNewPurity = 0;    // +0x70
};

class COcean final : public CWaterTank {
public:
    // dll:10029570. `celName` is looked up with CWorld::FindBiomeCel (the ocean cel "O1").
    COcean(World& w, std::string name, const char* celName, double volume, double waterVol, double oxygen,
           double newPH, double salinity, double temp);
    void saveSlot(SlotWriter& w) const override;   // dll:10029870 (CWaterTank::Save first)
    void loadSlot(SlotReader& r) override;         // dll:10029900

    CBiomeCel* cel() const { return m_pCel; }
    bool waveMachineOn() const { return m_bWaveMachineOn != 0; }
    double algae() const { return m_dAlgae; }
    double health() const { return m_dHealth; }
    void setHealth(double v) { m_dHealth = v; }                // the cheats OCEANDEAD / OCEANOK (dll:1001e6b0)
    void setAlgae(double v) { m_dAlgae = v; }                  // CAlgaeScrubber::Update dll:10003ae0 (+0x98, the feature code)
    void setWaveMachineOn(int v) { m_bWaveMachineOn = v; }     // CWaveMachine::StartRunning / StopRunning dll:10070be0 / 10070c10 (+0x80)
    uint16_t levelState() const { return m_wLevelState; }
    uint16_t problemFlags() const { return m_wProblemFlags; }
    // COcean::UpdateState dll:10029990 without the page display at its end (BiosGame does that); `now` = game time
    void updateState(Sim& sim, i32 now);

private:
    i32 m_dwLastStateTime = 0;  // +0x78 saved as "unk_78"; ctor 0x7080 (28800 = 08:00); 28918 in NewGame (guess: time of last UpdateState)
    CBiomeCel* m_pCel = nullptr;   // +0x7c biome cel
    int32_t m_bWaveMachineOn = 0;   // +0x80
    uint16_t m_wLevelState = 0;     // +0x84 1 high / 2 mid / 3 low
    double m_dHealth = 0;           // +0x88
    uint16_t m_wProblemFlags = 0;   // +0x90
    double m_dAlgae = 0;            // +0x98
};

} // namespace biosys::game
