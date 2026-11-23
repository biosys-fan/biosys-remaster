// BS4 main-page dynamics: the lung and pump route tests (exe:0043b5e0, exe:0043b7e0).
#include "engine/bs4routes.h"
#include "engine/bs4.h"

namespace biosys {
namespace {

// The destination (FEATURE_DESTINATION value) an overlay ornament stands for. The original compares the name against the twelve
// ornaments in a fixed order (exe:0043b5e0 / 0043b7e0); the twelve names of each kind, in that order.
struct Route { const char* ornament; u32 destination; };
const Route kLungRoutes[] = {
    {"hiairsoon", 0x18}, {"hiairseon", 0x17}, {"hiairsron", 0x15}, {"hiairoson", 0x16}, {"hiairoeon", 0x17}, {"hiairoron", 0x15},
    {"hiaireson", 0x16}, {"hiaireoon", 0x18}, {"hiaireron", 0x15}, {"hiairrson", 0x16}, {"hiairroon", 0x18}, {"hiairreon", 0x17},
};
const Route kPumpRoutes[] = {
    {"hiutilsoon", 0x18}, {"hiutilseon", 0x17}, {"hiutilsron", 0x15}, {"hiutiloson", 0x16}, {"hiutiloeon", 0x17}, {"hiutiloron", 0x15},
    {"hiutileson", 0x16}, {"hiutileoon", 0x18}, {"hiutileron", 0x15}, {"hiutilrson", 0x16}, {"hiutilroon", 0x18}, {"hiutilreon", 0x17},
};

// the tail both tests share: the ornament names the destination the lung / pump has to be routed to
bool matchRoute(const std::string& ornament, const Route* routes, size_t n, u32 dest, int& open) {
    for (size_t i = 0; i < n; i++) {
        if (!bs4Iequals(ornament, routes[i].ornament)) continue;
        if (routes[i].destination == dest) open = 1;
        return true;
    }
    return false;                                           // not one of the twelve names
}

} // namespace

// exe:0043b5e0. ORIGINAL BUG (kept): a lung that draws its air from a biome (DRAW_AIR_FROM_BIOME != 0) and a destination 0x1a answer "ok" with
// open = 0, so the overlay of such a lung never lights. (No observable effect: the caller lights an overlay for ok && open == 1 only, so "ok, not open" and "no" look the same; FIXES.md, Terminal.)
bool bs4LungRouteIsOpen(Bs4Host& host, const std::string& feature, const std::string& overlayOrnament, int& open) {
    open = 0;
    u32 v = 0;
    double d = 0;
    if (!host.onGetFeatureParameter(feature, 0x11, v, d)) return false;       // FEATURE_DRAW_AIR_FROM_BIOME
    if (v != 0) return true;
    u32 dest = 0;
    if (!host.onGetFeatureParameter(feature, 0x14, dest, d)) return false;    // FEATURE_DESTINATION
    if (dest < 0x15) return false;
    if (dest > 0x18) return dest == 0x1a;
    return matchRoute(overlayOrnament, kLungRoutes, sizeof kLungRoutes / sizeof kLungRoutes[0], dest, open);
}

// exe:0043b7e0
bool bs4PumpRouteIsOpen(Bs4Host& host, const std::string& feature, const std::string& overlayOrnament, int& open) {
    open = 0;
    u32 dest = 0;
    double d = 0;
    if (!host.onGetFeatureParameter(feature, 0x14, dest, d)) return false;    // FEATURE_DESTINATION
    if (dest < 0x15 || dest > 0x18) return false;
    return matchRoute(overlayOrnament, kPumpRoutes, sizeof kPumpRoutes / sizeof kPumpRoutes[0], dest, open);
}

} // namespace biosys
