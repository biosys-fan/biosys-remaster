// CPlant (saved table `plants`, size 0x114, analysis/notes/simulation.md section 7) and the display helper
// CPlantPair (never saved, never in a list).
#pragma once
#include <array>
#include "game/biomecel.h"
#include "game/site.h"
#include "game/world.h"

namespace biosys::game {

class Sim;

namespace PlantFlag {                      // CPlant +0x0c (constructor argument 3), from CPlant::Update dll:100733c0
enum : uint32_t {
    C3 = 0x1, C4 = 0x2, Cam = 0x4,         // photosynthesis type; a plant without any of C3/C4/CAM must have Inert
    Inert = 0x8,                           // not simulated at all
    FruitRegrows = 0x10,                   // one fruit regrows every 4 h
    RegrowsInRain = 0x40,                  // one fruit regrows per 10 min while it rains
    WaterVine = 0x80,
};
}
namespace PlantHealthState {               // +0x88 bit mask (CPlant::Update dll:100733c0), 1 in the constructor
enum : uint32_t { Healthy = 0x1, Unhealthy = 0x2, Dead = 0x4, Stressed = 0x800 };   // plus the limiting-factor bits
}

// dll:10072210, vtable 100940d0 (CObject -> CPlant, no virtual methods of its own besides the destructor)
class CPlant final : public Item {
public:
    CPlant(World& w, const char* name, const char* type, int32_t speciesId, int32_t flags, CSite* site,
           double maxPSRate, double tempOptimum, double tempMinNight, double tempMax, double soilWaterMin,
           double soilWaterMax, double hardiness, double tempMinDay);

    bool typed() const override { return true; }
    // CPlant::Update dll:100733c0 (simulation.md 7.1): `now` is the game time; skipped when nothing has passed
    void update(Sim& sim, i32 now);
    void saveSlot(SlotWriter& w) const override;   // dll:100724f0
    void loadSlot(SlotReader& r) override;         // dll:10072620

    // The builder statements `*(undefined4 *)(plant + off) = v` that follow the constructor of the seven dead-biome
    // plants (poke32 calls in world_build.gen.cpp): the dword halves of the doubles at +0x68, +0x70, +0x78, +0x80.
    void poke32(uint32_t off, uint32_t value);
    uint32_t peek32(uint32_t off) const;   // the dword poke32 writes (tests)

    const std::string& type() const { return m_szType; }
    int32_t speciesId() const { return m_dwSpeciesId; }
    uint32_t flags() const { return m_dwFlags; }
    CSite* site() const { return m_pSite; }
    CBiomeCel* cel() const { return m_pCel; }
    double shortTermHealth() const { return m_dShortTermHealth; }
    void setShortTermHealth(double v) { m_dShortTermHealth = v; }
    double longTermHealth() const { return m_dLongTermHealth; }
    void setLongTermHealth(double v) { m_dLongTermHealth = v; }
    double health() const { return m_dHealth; }
    void setHealth(double v) { m_dHealth = v; }
    double healthPrev() const { return m_dHealthPrev; }
    void setHealthPrev(double v) { m_dHealthPrev = v; }
    uint32_t healthState() const { return m_dwHealthState; }
    void setHealthState(uint32_t v) { m_dwHealthState = v; }
    int32_t timeLastUpdate() const { return m_dwTimeLastUpdate; }
    void setTimeLastUpdate(int32_t t) { m_dwTimeLastUpdate = t; }
    int32_t timeLastPicked() const { return m_dwTimeLastPicked; }
    void setTimeLastPicked(int32_t t) { m_dwTimeLastPicked = t; }
    int32_t numPicked() const { return m_dwNumPicked; }
    void setNumPicked(int32_t n) { m_dwNumPicked = n; }
    uint16_t fruitMask() const { return m_wFruitMask; }
    void setFruitMask(uint16_t m) { m_wFruitMask = m; }
    // Fruit on the plant: the count (+0x10, saved as unk_10) and the slot bit mask (+0x14, saved as unk_14). A food
    // object that grows on the plant registers itself in its constructor (CFood dll:10063520): count++ and
    // mask |= 1 << (slot - 1), the original's 16-bit store.
    int32_t fruitCount() const { return m_dwFruitCount; }
    void setFruitCount(int32_t n) { m_dwFruitCount = n; }
    void addFruit(int32_t slot) {
        m_dwFruitCount++;
        m_wFruitMask = static_cast<uint16_t>(m_wFruitMask | (1u << ((slot - 1) & 31)));
    }
    double soilWaterMin() const { return m_dSoilWaterMin; }
    double soilWaterMax() const { return m_dSoilWaterMax; }
    double maxPSRate() const { return m_dMaxPSRate; }
    double respirationRate() const { return m_dRespirationRate; }
    double tempOptimum() const { return m_dTempOptimum; }
    double tempMinNight() const { return m_dTempMinNight; }
    double tempMinDay() const { return m_dTempMinDay; }
    double tempMax() const { return m_dTempMax; }
    double hardiness() const { return m_dHardiness; }
    double stomatalAperture() const { return m_dStomatalAperture; }
    void setStomatalAperture(double v) { m_dStomatalAperture = v; }
    double waterAvailability() const { return m_dWaterAvailability; }
    void setWaterAvailability(double v) { m_dWaterAvailability = v; }
    double transpirationRate() const { return m_dTranspirationRate; }
    void setTranspirationRate(double v) { m_dTranspirationRate = v; }
    double leafTemp() const { return m_dLeafTemp; }
    void setLeafTemp(double v) { m_dLeafTemp = v; }
    double psRate() const { return m_dPSRate; }
    void setPSRate(double v) { m_dPSRate = v; }
    // Display state the original keeps in the plant but does not save: +0x16 (the fruit mask the page shows), the engine slots of
    // the fruit images (+0x1c .. +0x38, one per fruit, -1 = none) and the image name shown now (+0x3c, "noimage").
    const std::string& plantName() const { return m_szName; }              // +0x50 ("AR1PA"; the images use name + 1)
    const std::string& image() const { return m_szImage; }
    void setImage(const std::string& s) { m_szImage = s; }
    uint16_t shownMask() const { return m_wShownMask; }
    void setShownMask(uint16_t m) { m_wShownMask = m; }
    int32_t& imageSlot(int n) { return m_aImageSlot[n]; }                  // n = 1 .. 8
    int32_t waterUntil() const { return m_dwUnk110; }                      // +0x110: game time the plant's water (yWater) is available until
    void setWaterUntil(int32_t t) { m_dwUnk110 = t; }
    // DLL data addresses of the three step tables (light, CO2, |leaf temp - optimum|); 0 = none (inert plant)
    uint32_t lightTable() const { return m_pLightTable; }
    uint32_t co2Table() const { return m_pCO2Table; }
    uint32_t tempTable() const { return m_pTempTable; }

private:
    // +0x00 vtable (100940d0)
    std::string m_szType;           // +0x04 char[4] species type code ("PA")
    int32_t m_dwSpeciesId;          // +0x08
    uint32_t m_dwFlags;             // +0x0c PlantFlag + display/interaction options
    int32_t m_dwFruitCount;         // +0x10 saved as "unk_10": number of fruit on the plant
    uint16_t m_wFruitMask;          // +0x14 saved as "unk_14": bit n set = fruit n+1 is on the plant
    std::string m_szImage;          // +0x3c char[12] image name, "noimage"
    CSite* m_pSite;                 // +0x48
    CBiomeCel* m_pCel;              // +0x4c the site's CBiomeCel (site +8)
    std::string m_szName;           // +0x50 char[12] plant name ("AR1PA")
    int32_t m_dwTimeLastUpdate;     // +0x5c game time of the last Update (28800 = 08:00 at the start)
    int32_t m_dwTimeLastPicked;     // +0x60
    int32_t m_dwNumPicked;          // +0x64
    double m_dShortTermHealth;      // +0x68
    double m_dLongTermHealth;       // +0x70
    double m_dHealth;               // +0x78 min(short, long)
    double m_dHealthPrev;           // +0x80
    uint32_t m_dwHealthState;       // +0x88
    double m_dSoilWaterMin;         // +0x90
    double m_dSoilWaterMax;         // +0x98
    double m_dStomatalAperture;     // +0xa0
    double m_dWaterAvailability;    // +0xa8
    double m_dTranspirationRate;    // +0xb0
    double m_dLeafTemp;             // +0xb8
    double m_dMaxPSRate;            // +0xc0 maximum photosynthesis rate per second
    double m_dPSRate;               // +0xc8 (0.5 in the constructor)
    double m_dRespirationRate;      // +0xd0 = 0.25 * m_dMaxPSRate
    uint32_t m_pLightTable;         // +0xd8 DLL data address of a step table (DAT_100bdad8 / 100bdc18)
    uint32_t m_pCO2Table;           // +0xdc DLL data address (DAT_100bdd58 / 100bded8)
    uint32_t m_pTempTable;          // +0xe0 DLL data address DAT_100be058
    double m_dTempOptimum;          // +0xe8
    double m_dTempMinNight;         // +0xf0
    double m_dTempMinDay;           // +0xf8 (= night minimum when the constructor argument is -999)
    double m_dTempMax;              // +0x100
    double m_dHardiness;            // +0x108 damage divisor
    int32_t m_dwUnk110;             // +0x110 saved as "unk_110" (0 in the constructor and in NewGame.sav)
    uint16_t m_wShownMask = 0;      // +0x16 (not saved)
    std::array<int32_t, 9> m_aImageSlot = {-1, -1, -1, -1, -1, -1, -1, -1, -1};   // +0x18 + 4 * n (not saved)
};

// dll:10009be0, vtable 1008fcf0: two plants shown together in the distance (CPlantPair::DisplayInDistance
// dll:10009ce0). Five are built, none is added to a list or saved.
class CPlantPair final : public Item {
public:
    CPlantPair(CPlant* plantA, CPlant* plantB, uint32_t flags);
    bool typed() const override { return true; }
    void saveSlot(SlotWriter&) const override;   // not a slot item: throws
    void loadSlot(SlotReader&) override;

    CPlant* plantA() const { return m_pPlantA; }
    CPlant* plantB() const { return m_pPlantB; }
    uint32_t flags() const { return m_dwFlags; }
    const std::string& image() const { return m_szImage; }
    void setImage(const std::string& s) { m_szImage = s; }

private:
    CPlant* m_pPlantA;       // +0x04
    CPlant* m_pPlantB;       // +0x08
    uint32_t m_dwFlags;      // +0x0c (bit 0x200 and 0x20000 seen; tested through byte +0xd & 2)
    // +0x10: image name buffer written by DisplayInDistance, not initialised by the constructor (PORT: starts as "noimage")
    std::string m_szImage = "noimage";
};

} // namespace biosys::game
