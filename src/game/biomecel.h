// CBiomeCel: one air/soil box of the habitat graph (analysis/notes/simulation.md section 4). Size 0x110.
#pragma once
#include "game/world.h"

namespace biosys::game {

class Sim;

class CBiomeCel final : public Item {
public:
    // dll:10017b90. Argument a5 (stack dword 6 of the original) is never read by the constructor.
    CBiomeCel(const char* name, int32_t flags, Biome* biome, double airCapacity, double airTemp, double soilTemp,
              double humidity, double oxygen, double co2, double light, double lightLetThru, double soilpH,
              double soilWater, double soilNutrients, double pollution, Item* soilTank, Item* condensateTank,
              uint16_t id);

    bool typed() const override { return true; }
    void saveSlot(SlotWriter& w) const override;   // dll:10017da0
    void loadSlot(SlotReader& r) override;         // dll:10017f40

    const Biome* biome() const { return m_pBiome; }
    Item* soilTank() const { return m_pSoilTank; }
    Item* condensateTank() const { return m_pCondensateTank; }
    uint16_t id() const { return m_wId; }
    uint32_t flags() const { return m_dwFlags; }
    void setFlags(uint32_t f) { m_dwFlags = f; }

    double airCapacity() const { return m_dAirCapacity; }
    void setAirCapacity(double v) { m_dAirCapacity = v; }
    double airVolume() const { return m_dAirVolume; }
    void setAirVolume(double v) { m_dAirVolume = v; }
    double airTemp() const { return m_dAirTemp; }
    void setAirTemp(double v) { m_dAirTemp = v; }
    double newAirTemp() const { return m_dNewAirTemp; }
    void setNewAirTemp(double v) { m_dNewAirTemp = v; }
    double soilTemp() const { return m_dSoilTemp; }
    void setSoilTemp(double v) { m_dSoilTemp = v; }
    double newSoilTemp() const { return m_dNewSoilTemp; }
    void setNewSoilTemp(double v) { m_dNewSoilTemp = v; }
    double humidity() const { return m_dHumidity; }
    void setHumidity(double v) { m_dHumidity = v; }
    double newHumidity() const { return m_dNewHumidity; }
    void setNewHumidity(double v) { m_dNewHumidity = v; }
    double oxygen() const { return m_dOxygen; }
    void setOxygen(double v) { m_dOxygen = v; }
    double newOxygen() const { return m_dNewOxygen; }
    void setNewOxygen(double v) { m_dNewOxygen = v; }
    double co2() const { return m_dCO2; }
    void setCO2(double v) { m_dCO2 = v; }
    double newCO2() const { return m_dNewCO2; }
    void setNewCO2(double v) { m_dNewCO2 = v; }
    double light() const { return m_dLight; }
    void setLight(double v) { m_dLight = v; }
    double lightLetThru() const { return m_dLightLetThru; }
    void setLightLetThru(double v) { m_dLightLetThru = v; }
    double soilpH() const { return m_dSoilpH; }
    double soilWater() const { return m_dSoilWater; }
    void setSoilWater(double v) { m_dSoilWater = v; }
    int32_t timeLastRain() const { return m_dwTimeLastRain; }
    void setTimeLastRain(int32_t t) { m_dwTimeLastRain = t; }
    double windSpeed() const { return m_dWindSpeed; }
    void setWindSpeed(double v) { m_dWindSpeed = v; }
    double soilNutrients() const { return m_dSoilNutrients; }
    double pollution() const { return m_dPollution; }
    void setPollution(double v) { m_dPollution = v; }
    double newPollution() const { return m_dNewPollution; }
    void setNewPollution(double v) { m_dNewPollution = v; }
    double plantHealthAvg() const { return m_dPlantHealthAvg; }
    void setPlantHealthAvg(double v) { m_dPlantHealthAvg = v; }
    double plantHealthMin() const { return m_dPlantHealthMin; }
    void setPlantHealthMin(double v) { m_dPlantHealthMin = v; }
    double plantHealthMax() const { return m_dPlantHealthMax; }
    void setPlantHealthMax(double v) { m_dPlantHealthMax = v; }

    // The update step of the simulation (simulation.md 4.2). `dt` is the game time since the last cel update.
    void updateState(Sim& sim, uint32_t dt);       // dll:10018530 (ends with the commit of the "new" values)
    void updatePlantHealthAvg();                   // dll:10018860
    void onNewDay() { m_dwFlags &= 0xfffffff7u; }  // dll:100189a0 (forgets "reached 33 degrees today")
    // The direct setters (scripts, cheats): bring the clock and the air flows up to date, then store current and new
    void setAirTempNow(Sim& sim, double v);        // dll:10018390
    void setOxygenNow(Sim& sim, double v);         // dll:100183f0
    void setCO2Now(Sim& sim, double v);            // dll:10018430
    void setHumidityNow(Sim& sim, double v);       // dll:10018470
    void setPollutionNow(Sim& sim, double v);      // dll:100184b0
    // CWorld::CommitBiomeCels (dll:10013c10) for this cel: the "new" values become current
    void commit();

    // CBiomeCel::AddPlant dll:10018370 (the original calls it from the plant constructor)
    void addPlant(Item* plant) { m_plants.push_back(plant); }
    const std::vector<Item*>& plants() const { return m_plants; }

private:
    // +0x00 vtable (100908e0); +0x04 char[12] name = Item::name()
    Biome* m_pBiome;                 // +0x10
    Item* m_pSoilTank;               // +0x14 CWaterTank*, may be null
    Item* m_pCondensateTank;         // +0x18 CWaterTank*, may be null
    uint16_t m_wId;                  // +0x1c
    bool m_bCondensationFlowsOn = false;   // Fixes::condensationFlagPerCel: the per-cel version of Sim::condensationFlowsOn (not saved)
    double m_dAirCapacity;           // +0x20 nominal air volume
    double m_dAirVolume;             // +0x28 current air amount
    double m_dAirTemp;               // +0x38
    double m_dSoilTemp;              // +0x40
    double m_dHumidity;              // +0x48
    double m_dOxygen;                // +0x50
    double m_dCO2;                   // +0x58
    double m_dLight;                 // +0x60
    double m_dSoilpH;                // +0x68
    double m_dSoilWater;             // +0x70
    int32_t m_dwTimeLastRain;        // +0x78 -1 = never
    double m_dWindSpeed;             // +0x80
    double m_dLightLetThru;          // +0x88
    double m_dSoilNutrients;         // +0x90
    double m_dPollution;             // +0x98
    double m_dNewAirTemp;            // +0xa0 value for the next update step
    double m_dNewSoilTemp;           // +0xa8
    double m_dNewHumidity;           // +0xb0
    double m_dNewOxygen;             // +0xb8
    double m_dNewCO2;                // +0xc0
    double m_dNewPollution;          // +0xc8
    std::vector<Item*> m_plants;     // +0xd0 CObList of plants
    double m_dPlantHealthAvg;        // +0xf0
    double m_dPlantHealthMin;        // +0xf8 starts at +infinity
    double m_dPlantHealthMax;        // +0x100 starts at -infinity
    uint32_t m_dwFlags;              // +0x108
};

} // namespace biosys::game
