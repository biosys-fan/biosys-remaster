#include "game/featparam.h"
#include "game/blobrecords.h"
#include "game/feat_sensors.h"
#include "game/featenv.h"
#include "game/feature.h"
#include "game/watertank.h"
#include <cctype>
#include <cstring>
#include <strings.h>

namespace biosys::game {

namespace {
double dword2(int lo, int hi) { return dblBits(static_cast<u32>(lo), static_cast<u32>(hi)); }
void clampTo(double& x, double lo, double hi) { if (x < lo) x = lo; else if (hi < x) x = hi; }

// the nine static CBiome objects by DLL address
constexpr u32 kRainforest = 0x100c1c50, kSavannah = 0x100c0ee8, kOceanBiome = 0x100c1280, kDeadBiome = 0x100c24e8;

// SIM_*_BIOME_HEALTH: percent of the mean plant health (dbl), and 1 in the int when the trend is negative
void biomeHealth(World& w, u32 addr, u32& outInt, double& outValue) {
    Biome* b = w.biome(addr);
    outValue = biomeAvgPlantHealth(*b) * 100.0;
    outInt = biomeHealthTrend(*b) < 0 ? 1 : 0;
}
}

// dll:10021db0 Event_OnSetFeatureParameter(name, id, a, lo, hi) -> feature vtable +0x18
bool featureSetParameter(World& w, const std::string& featureName, int id, int a, int b, int c) {
    if (featureName.empty()) return false;
    CFeature* f = w.findFeature(featureName.c_str());
    if (!f) return false;
    return f->setParam(id, a, dword2(b, c));
}

// dll:10021df0 Event_OnGetFeatureParameter(name, id, uint*, double*)
bool featureGetParameter(World& w, const std::string& featureName, int id, u32& outInt, double& outValue) {
    const WorldHeader& h = cworld::header(w);
    switch (id) {
    case 0x834: biomeHealth(w, kRainforest, outInt, outValue); return true;     // SIM_RAINFOREST_BIOME_HEALTH
    case 0x835: biomeHealth(w, kSavannah, outInt, outValue); return true;       // SIM_SAVANNAH_BIOME_HEALTH
    case 0x836: {                                                               // SIM_OCEAN_BIOME_HEALTH
        Biome* b = w.biome(kOceanBiome);
        outValue = biomeAvgPlantHealth(*b) * 100.0;
        double v = cworld::ocean(w).health() * 900.0 + outValue;
        outValue = v;
        outValue = v * 0.1;
        clampTo(outValue, 0.0, 100.0);
        outInt = biomeHealthTrend(*b) < 0 ? 1 : 0;
        return true;
    }
    case 0x837: biomeHealth(w, kDeadBiome, outInt, outValue); return true;      // SIM_DEAD_BIOME_HEALTH
    case 2000: {                                                                // SIM_HEALTH_STATE of an area: 2001 dead, 2002 unhealthy, 2003 healthy
        if (strcasecmp(featureName.c_str(), "ocean") == 0) {
            double hp = cworld::ocean(w).health();
            if (hp == 0.0) outInt = 0x7d1;
            else if (0.5 <= hp) outInt = 0x7d3;
            else outInt = 0x7d2;
            return true;
        }
        // ORIGINAL BUG: the original copies the name into a 7-byte stack buffer (strcpy) before upper-casing it: a name of 7 or
        // more characters overwrites the frame. The port looks the whole name up.
        std::string up = featureName;
        for (char& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        CBiomeCel* cel = w.findBiomeCel(up.c_str());
        if (!cel) return false;
        if (w.state().host) w.state().host->updatePlantHealthAvg(*cel);
        if (0.2 < cel->plantHealthAvg()) outInt = 0.5 <= cel->plantHealthAvg() ? 0x7d3 : 0x7d2;
        else outInt = 0x7d1;
        return true;
    }
    }
    if (featureName.empty()) {      // the world parameters (the original's NULL name)
        switch (id) {
        case 1000:
            throw Error("UNRESOLVED dll:10021df0: id 1000 answers a pointer to the clock text (CTime::Format) in the uint; not ported");
        case 0x3f2: outValue = h.m_dPowerGeneration * 3600.0; return true;     // WORLD_POWER_GENERATION
        case 0x3f3: outValue = h.m_dPower; return true;                        // WORLD_POWER_AVAILABLE
        case 0x3f4:                                                            // WORLD_POWER_USAGE
            outValue = h.m_dPowerDemand * 3600.0;
            clampTo(outValue, 0.0001, 100.0);
            return true;
        case 0x3fc: outValue = h.m_dSunlight * 100.0; return true;             // sunlight in percent
        case 0x3fd: outValue = h.m_dExtTemp; return true;                      // WORLD_TEMPERATURE
        case 0x3fe: outValue = h.m_dWindSpeed; return true;                    // WORLD_WINDSPEED
        case 0x3ff: outValue = (1.0 - h.m_dBlueSky) * 100.0; return true;      // WORLD_CLOUD_COVER
        case 0x41a: {                                                          // WORLD_ALARMS
            outInt = 0;
            for (Item* it : w.related(Rel::WorldSensor, nullptr))
                if (as<CBiomeSensor>(it).alarms() != 0) { outInt = 1; break; }
            for (Item* it : w.list(Table::Features))
                if (!as<CFeature>(it).working()) { outInt |= 2; break; }
            if (h.m_dPower < 200.0) outInt |= 8;
            return true;
        }
        case 0x424: outInt = (h.m_dwPowerFlags & 0x20000000u) >> 0x1d; return true;   // WORLD_AEB_POWER_DISCONNECTED
        case 0x438: {                                                          // story flag: bit 29 of the milestones
            if (!w.state().sim.story) throw Error("world has no story record (GameState::sim.story)");
            outInt = (w.state().sim.story->milestones & 0x20000000u) >> 0x1d;
            return true;
        }
        default: return false;
        }
    }
    CFeature* f = w.findFeature(featureName.c_str());
    if (!f) return false;
    i32 pi = static_cast<i32>(outInt);
    double pd = outValue;
    if (!f->getParam(id, &pi, &pd)) return false;
    outInt = static_cast<u32>(pi);
    outValue = pd;
    return true;
}

} // namespace biosys::game
