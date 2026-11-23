// CAirFlow (dll:1005a980, 86 items) and its derived flows CSoilBedFlow (dll:1000b210), CLungAirFlow
// (dll:10024280) and CBubbleFlow (dll:1002aa00) of the airflows table. The derived constructors only call
// FUN_1005a8b0 (sets the CAirFlow vtable) and initialise their own members. Save/Load are CAirFlow's
// (dll:1005ab90 / 1005abd0) for all four. Members by object offset (simulation.md section 5).
#pragma once
#include "game/biomecel.h"
#include "game/feature.h"
#include "game/world.h"

namespace biosys::game {

class Sim;

class CAirFlow : public Item {
public:
    // dll:1005a980. `from`/`to` are biome cel names (CWorld::FindBiomeCel).
    CAirFlow(World& w, std::string name, const char* from, const char* to, uint16_t id, double mag, int32_t twoWay,
             int32_t on, double windSpeed, Item* ventFeature, double ventAirflow, Item* masterFlow);
    bool typed() const override { return true; }
    void saveSlot(SlotWriter& w) const override;   // dll:1005ab90
    void loadSlot(SlotReader& r) override;         // dll:1005abd0

    CBiomeCel* from() const { return m_pFrom; }
    CBiomeCel* to() const { return m_pTo; }
    uint16_t id() const { return m_wId; }
    bool on() const { return m_bOn != 0; }
    bool reversed() const { return m_bReversed != 0; }
    bool twoWay() const { return m_bTwoWay != 0; }
    double mag() const { return m_dMag; }
    double windSpeed() const { return m_dWindSpeed; }
    Item* masterFlow() const { return m_pMasterFlow; }
    Item* ventFeature() const { return m_pVentFeature; }
    double ventAirflow() const { return m_dVentAirflow; }
    double amount() const { return m_dAmount; }

    // Vtable slots of the original (simulation.md section 5). `dt` is the game time since the last update.
    virtual void update(Sim& sim, uint32_t dt);   // +0x20 dll:1005aca0 (CSoilBedFlow 1000b3d0, CLungAirFlow 10024500, CBubbleFlow 1002ab70)
    void turnOn(int on);                          // +0x24 dll:1005af40 (CWorld::SwitchAirFlows)
    void setReversed(int reversed);               // +0x28 dll:1005afb0
    void setTo(CBiomeCel* cel) { m_pTo = cel; }   // CWorld::SetAirFlowsDest dll:10013f00 stores into +0x08

protected:
    CAirFlow(u32 ctorAddr, const char* className, std::string name);   // FUN_1005a8b0: vtable only
    Sim* sim() const;               // the simulation of the flow's world (port only)
    World* m_pWorld = nullptr;      // port only: the world the flow was built in

    CBiomeCel* m_pFrom = nullptr;   // +0x04 source cel
    CBiomeCel* m_pTo = nullptr;     // +0x08 destination cel
    Item* m_pMasterFlow = nullptr;  // +0x0c master (CSoilBedFlow / CBubbleFlow) a slave flow follows
    double m_dAmount = 0;           // +0x10 amount moved this step (master flows); not set by any constructor
    Item* m_pVentFeature = nullptr; // +0x18 vent feature (CVent) that shows this flow's airflow
    double m_dVentAirflow = 0;      // +0x20 airflow shown on that vent
    uint16_t m_wId = 0;             // +0x28 flow id (CWorld::SwitchAirFlows ...)
    int32_t m_bOn = 0;              // +0x2c
    int32_t m_bReversed = 0;        // +0x30
    int32_t m_bTwoWay = 0;          // +0x34
    double m_dMag = 0;              // +0x38
    double m_dWindSpeed = 0;        // +0x40
};

class CSoilBedFlow final : public CAirFlow {
public:
    // dll:1000b210 (the constructor's 6th value, param_7, is not used by the original)
    CSoilBedFlow(World& w, std::string name, const char* from, const char* to, uint16_t id, double mag, int32_t on,
                 double windSpeed);
    void update(Sim& sim, uint32_t dt) override;   // dll:1000b3d0
private:
    int32_t m_unk48 = 0;            // +0x48 zeroed by the constructor, meaning unknown
    int32_t m_unk4c = 0;            // +0x4c zeroed by the constructor, meaning unknown
};

class CLungAirFlow final : public CAirFlow {
public:
    // dll:10024280. `lungFeature` is a CLung feature name (CWorld::FindFeature).
    CLungAirFlow(World& w, std::string name, const char* from, const char* to, uint16_t id, const char* lungFeature,
                 int32_t flag, double mag, double windSpeed, Item* ventFeature, double ventAirflow);
    CFeature* lung() const { return m_pLung; }
    void update(Sim& sim, uint32_t dt) override;   // dll:10024500 (does not use dt)
private:
    int32_t m_iLungFlag = 0;        // +0x48 constructor argument 5, meaning unknown
    CFeature* m_pLung = nullptr;    // +0x4c CLung feature driving the flow
};

class CBubbleFlow final : public CAirFlow {
public:
    // dll:1002aa00 (TO -> O1, mag 1.0)
    CBubbleFlow(World& w, std::string name, const char* from, const char* to, uint16_t id);
    void update(Sim& sim, uint32_t dt) override;   // dll:1002ab70
};

} // namespace biosys::game
