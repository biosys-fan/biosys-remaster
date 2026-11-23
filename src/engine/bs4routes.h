// BS4: the two route tests of the air-lung and water-pump overlays (packet P3; also used by the air / water panels, packet P6).
// The ornament name of an overlay (`hiair<from><to>on`, `hiutil<from><to>on`, the rows 79..102 of exe:00486410) says which biome the
// lung / pump of the feature is routed to: the second letter s / o / e / r = destination 0x16 / 0x18 / 0x17 / 0x15 of the
// FEATURE_DESTINATION parameter (0x14).
#pragma once
#include "engine/bs4.h"
#include <string>

namespace biosys {

// exe:0043b5e0 LungRouteIsOpen(feature, overlayOrnament, &open). Returns false when a parameter cannot be read (the original returns 0);
// `open` is set to 1 only when FEATURE_DRAW_AIR_FROM_BIOME (0x11) is 0, FEATURE_DESTINATION (0x14) is 0x15..0x18 and equals the
// destination named by the ornament. Original quirks kept: DRAW_AIR_FROM_BIOME != 0 returns true with open = 0, destination 0x1a returns
// true with open = 0, a destination below 0x15 or above 0x18 (other than 0x1a) returns false, an unknown ornament name returns false.
bool bs4LungRouteIsOpen(Bs4Host& host, const std::string& feature, const std::string& overlayOrnament, int& open);

// exe:0043b7e0 PumpRouteIsOpen: the same without the DRAW_AIR_FROM_BIOME step (destination outside 0x15..0x18 returns false).
bool bs4PumpRouteIsOpen(Bs4Host& host, const std::string& feature, const std::string& overlayOrnament, int& open);

} // namespace biosys
