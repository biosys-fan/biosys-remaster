// CWaterFlow (dll:1005cce0, 61 items of the waterflows table) and the two classes that derive from it,
// CEvaporation (dll:1006f990) and CRain (dll:10071d60). Their constructors call FUN_1005cc10 (sets the
// CWaterFlow vtable only), so every CWaterFlow member they do not set themselves stays zero. Save/Load are
// shared (dll:1005cec0 / 1005cf10). Members by object offset (simulation.md 6.2 - 6.4).
#pragma once
#include "game/biomecel.h"
#include "game/watertank.h"
#include "game/world.h"

namespace biosys::game {

class Sim;

class CWaterFlow : public Item {
public:
    // dll:1005cce0. `to` may be null; the tanks are looked up with CWorld::FindWaterTank.
    CWaterFlow(World& w, std::string name, const char* from, const char* to, uint16_t id, double mag,
               int32_t on, double clogRate, double purityOverride, int32_t proportional);
    bool typed() const override { return true; }
    void saveSlot(SlotWriter& w) const override;   // dll:1005cec0
    void loadSlot(SlotReader& r) override;         // dll:1005cf10

    CWaterTank* from() const { return m_pFrom; }
    CWaterTank* to() const { return m_pTo; }
    // The stores other classes make into a flow: CWorld::SetWaterFlowsSource dll:10014200 (+0x04) and
    // CWaterPump::SetParam dll:10005bb0 (+0x08).
    void setFrom(CWaterTank* tank) { m_pFrom = tank; }
    void setTo(CWaterTank* tank) { m_pTo = tank; }
    uint16_t id() const { return m_wId; }
    bool on() const { return m_bOn != 0; }
    bool reversed() const { return m_bReversed != 0; }
    double mag() const { return m_dMag; }
    double freeFlow() const { return m_dFreeFlow; }
    double purityOverride() const { return m_dPurityOverride; }
    double clogRate() const { return m_dClogRate; }
    bool proportional() const { return m_bProportional != 0; }

    // Vtable slots of the original (simulation.md sections 6.2 - 6.4). `dt` is the game time since the last update.
    void turnOn(int on) { m_bOn = on; }                         // +0x20 dll:1005d070 (CRain 10071fb0: the same)
    virtual void update(Sim& sim, uint32_t dt);                 // +0x24 dll:1005d080 (CEvaporation 1006fc50, CRain 10071fc0)
    void toggleReversed() { m_bReversed = (m_bReversed == 0); } // CWorld::ToggleWaterFlowsReversed dll:10014160
    void setFreeFlow(double v) { m_dFreeFlow = v; }             // CSiltScrubber::Update dll:10005fa0 (+0x30, the feature code)
    void setPurityOverride(double p) { m_dPurityOverride = p; } // CWorld::SetWaterFlowsPurity dll:100141b0 (+0x38)

protected:
    CWaterFlow(u32 ctorAddr, const char* className, std::string name);   // FUN_1005cc10: vtable only

    CWaterTank* m_pFrom = nullptr;  // +0x04 source tank
    CWaterTank* m_pTo = nullptr;    // +0x08 destination tank (null allowed)
    uint16_t m_wId = 0;             // +0x0c flow id
    int32_t m_bOn = 0;              // +0x10
    int32_t m_bReversed = 0;        // +0x14
    int32_t m_bProportional = 0;    // +0x18 amount scaled by the source fill fraction
    double m_dMag = 0;              // +0x20 volume units per hour
    double m_dClogRate = 0;         // +0x28 per second
    double m_dFreeFlow = 0;         // +0x30 1 -> 0 as the drain clogs
    double m_dPurityOverride = 0;   // +0x38 -1 = pass the source purity
};

class CEvaporation final : public CWaterFlow {
public:
    // dll:1006f990 (soil tank -> condensate tank, always on)
    CEvaporation(World& w, std::string name, const char* from, const char* to, const char* celName, uint16_t id,
                 double mag);
    CBiomeCel* cel() const { return m_pCel; }
    void update(Sim& sim, uint32_t dt) override;   // dll:1006fc50
private:
    CBiomeCel* m_pCel = nullptr;    // +0x40 biome cel whose humidity drives it
};

class CRain final : public CWaterFlow {
public:
    // dll:10071d60 (clean tank -> soil tank while a rain machine / mister runs)
    CRain(World& w, std::string name, const char* from, const char* to, const char* celName, uint16_t id,
          double mag, double humidityRate);
    CBiomeCel* cel() const { return m_pCel; }
    double humidityRate() const { return m_dHumidityRate; }
    void update(Sim& sim, uint32_t dt) override;   // dll:10071fc0
private:
    CBiomeCel* m_pCel = nullptr;    // +0x40 biome cel
    double m_dHumidityRate = 0;     // +0x48 relative humidity gained per second while raining
};

} // namespace biosys::game
