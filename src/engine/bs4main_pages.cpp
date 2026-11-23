// BS4 main-page dynamics: the pages `health` (exe:0043a5f0), `pod` (exe:0043a920) and `menuscreen` (exe:0043ad00).
// Spec analysis/notes/bs4.md section 3 (health table) and 1.13. Each function has a mode: 0 = after the page load, 1 = the 1 Hz tick
// (a failed read is then silent and the page is repainted at the end).
#include "engine/bs4main_priv.h"

namespace biosys {
namespace bs4main {

namespace {
int g_podPosition = 0;      // page +0x7c: the pod position of the last update
}

void resetPodPosition() { g_podPosition = 0; }
int podPositionNow() { return g_podPosition; }

// exe:0043a5f0: SIM_HEALTH_STATE (2000) of every zone -> its dead / unhealthy / healthy ornament
// ORIGINAL BUG: the row `ocean` (table row 10) names the ornaments oceand / oceanu / oceanh, but the page `health` has ocd / ocu / och, so the
// ocean zone never changes its picture (SetOrnamentVisible of a missing name does nothing).
// fix (Bs4Fixes::healthOceanOrnaments): the row uses the names the page has. The intent is evident: every other row names its three ornaments
// <zone>d / <zone>u / <zone>h and the page has exactly those for every other zone; ocd / ocu / och are the only ornaments of the page
// that belong to no row, and the ocean zone is the only row without ornaments.
void updateHealthStates(Bs4Page& p, int mode) {
    if (!pageIs(p, "health")) return;
    for (size_t i = 0; i < kHealthCount; i++) {
        HealthRow r = kHealth[i];
        if (p.owner().fixes().healthOceanOrnaments && bs4Iequals(r.feature, "ocean")) {
            r.dead = "ocd";
            r.unhealthy = "ocu";
            r.healthy = "och";
        }
        u32 state = 0;
        double d = 0;
        if (!getFeature(p, r.feature, 2000, state, d)) {
            if (mode == 1) return;
            logf(p, "BS4: Failed to get the value of parameter SIM_HEALTH_STATE for BiosFear feature '%s'.", r.feature);
            return;
        }
        if (state == 0x7d1) {                    // dead
            p.setOrnamentVisible(r.dead, true);
            p.setOrnamentVisible(r.unhealthy, false);
            p.setOrnamentVisible(r.healthy, false);
        } else if (state == 0x7d2) {             // unhealthy
            p.setOrnamentVisible(r.dead, false);
            p.setOrnamentVisible(r.unhealthy, true);
            p.setOrnamentVisible(r.healthy, false);
        } else if (state == 0x7d3) {             // healthy
            p.setOrnamentVisible(r.dead, false);
            p.setOrnamentVisible(r.unhealthy, false);
            p.setOrnamentVisible(r.healthy, true);
        } else {
            logf(p, "BS4: Unknown value (%lu) returned for parameter SIM_HEALTH_STATE for BiosFear feature '%s'.",
                 static_cast<unsigned long>(state), r.feature);
        }
    }
    if (mode == 1) repaint(p);
}

// exe:0043a920: FEATURE_POD_POSITION (0x85) of the feature `fPod`: 0x15 rf, 0x16 sav, 0x17 evo, 0x18 oc, 0x19 house -> the matching
// `pod<biome>` ornament and hotspot are enabled, the other four disabled
void updatePodPosition(Bs4Page& p, int mode) {
    if (!pageIs(p, "pod")) return;
    u32 pos = 0;
    double d = 0;
    if (!getFeature(p, "fPod", 0x85, pos, d)) {                                // exe:0048b5a8
        if (mode == 1) return;
        logf(p, "BS4: Failed to get the value of parameter FEATURE_POD_POSITION");
        return;
    }
    if (mode == 1 && g_podPosition == static_cast<int>(pos)) return;
    g_podPosition = static_cast<int>(pos);
    static const char* const kNames[] = {"podrf", "podsav", "podevo", "podoc", "podhouse"};
    for (const char* n : kNames) p.setOrnamentVisible(n, false);
    for (const char* n : kNames) p.setHotspotEnabled(n, false);
    if (pos < 0x15 || pos > 0x19) {
        logf(p, "BS4: Invalid pod position value %lu", static_cast<unsigned long>(pos));
        return;
    }
    const char* n = kNames[pos - 0x15];
    p.setOrnamentVisible(n, true);
    p.setHotspotEnabled(n, true);
    if (mode == 1) {
        repaint(p);
        p.owner().host().requestMouseMove();                                   // exe:00451d50
    }
}

// exe:0043ad00: the four biome health values (0x837 evolution "dead biome", 0x834 rainforest, 0x835 savannah, 0x836 ocean; the double is
// the percentage) and WORLD_AEB_POWER_DISCONNECTED (0x424) -> one status ornament per biome, the overall `biogood` / `biobad`, and
// `nopower` / `okpower`
void updateMenuScreen(Bs4Page& p, int mode) {
    if (!pageIs(p, "menuscreen")) return;
    struct Biome { int id; const char* what; const char* ded; const char* dy; const char* health; const char* rej; u32 i; double v; };
    Biome b[4] = {{0x837, "SIM_DEAD_BIOME_HEALTH", "evoded", "evody", "evohealth", "evorej", 0, 0},
                  {0x834, "SIM_RAINFOREST_BIOME_HEALTH", "rfded", "rfdy", "rfhealth", "rfrej", 0, 0},
                  {0x835, "SIM_SAVANNAH_BIOME_HEALTH", "savded", "savdy", "savhealth", "savrej", 0, 0},
                  {0x836, "SIM_OCEAN_BIOME_HEALTH", "ocded", "ocdy", "ochealth", "ocrej", 0, 0}};
    for (Biome& m : b)
        if (!getFeature(p, "", m.id, m.i, m.v)) {
            if (mode != 1) logf(p, "BS4: Failed to get the value of parameter %s", m.what);
            return;
        }
    u32 disconnected = 0;
    double d = 0;
    if (!getFeature(p, "", 0x424, disconnected, d)) {
        if (mode != 1) logf(p, "BS4: Failed to get the value of parameter WORLD_AEB_POWER_DISCONNECTED");
        return;
    }
    if (disconnected != 0) disconnected = 1;
    for (const Biome& m : b) {
        p.setOrnamentVisible(m.ded, false);
        p.setOrnamentVisible(m.dy, false);
        p.setOrnamentVisible(m.health, false);
        p.setOrnamentVisible(m.rej, false);
    }
    p.setOrnamentVisible("biogood", false);
    p.setOrnamentVisible("biobad", false);
    p.setOrnamentVisible("okpower", false);
    p.setOrnamentVisible("nopower", false);
    p.setOrnamentVisible(b[3].v <= 50.0 || b[2].v <= 50.0 || b[0].v <= 50.0 || b[1].v <= 50.0 ? "biobad" : "biogood", true);
    for (const Biome& m : b) {
        const char* n;
        if (m.v >= 20.0) n = m.v >= 50.0 ? m.health : m.i == 0 ? m.rej : m.dy;
        else n = m.ded;
        p.setOrnamentVisible(n, true);
    }
    p.setOrnamentVisible(disconnected == 1 ? "nopower" : "okpower", true);
    if (mode == 1) repaint(p);
}

} // namespace bs4main
} // namespace biosys
