// The parameter interface the engine's BS4 control panels use: Event_OnSetFeatureParameter (dll:10021db0) and
// Event_OnGetFeatureParameter (dll:10021df0). The ids are FEATURE_* (answered by the feature's SetParam / GetParam, vtable
// +0x18 / +0x1c), WORLD_* (feature name empty = NULL in the original) and SIM_* (0x834..0x837, 2000).
#pragma once
#include "game/world.h"
#include <string>

namespace biosys::game {

// dll:10021db0: false = no such feature or id. `b`, `c` are the low and high dword of the double value.
bool featureSetParameter(World& w, const std::string& featureName, int id, int a, int b, int c);
// dll:10021df0: outInt / outValue as the original's uint* / double*; false = not answered
bool featureGetParameter(World& w, const std::string& featureName, int id, u32& outInt, double& outValue);

} // namespace biosys::game
