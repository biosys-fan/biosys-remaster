// Helpers the object classes share (not part of the original: the original reads DLL globals and calls the C runtime).
#pragma once
#include "game/feature.h"
#include "game/gameobj.h"
#include "game/self.h"
#include <cctype>
#include <string>
#include <strings.h>

namespace biosys::game::objutil {

// __strcmpi(a, b) == 0
inline bool iequals(const std::string& a, const char* b) { return strcasecmp(a.c_str(), b) == 0; }

// CharUpperA on a copy
inline std::string upper(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return r;
}

// DAT_100c1e24: the first character of the current page name (CSelf+0xec)
inline char pageChar(const Self& s) { return static_cast<char>(s.m_szLocation[0]); }

// DAT_100c1f9c: the feature the player looks at (CSelf+0x264)
inline CFeature* feature(const Self& s) { return dynamic_cast<CFeature*>(s.m_pFeature.item); }

// DAT_100c1fa0: the object in the hand (CSelf+0x268)
inline GameObj* holding(const Self& s) { return dynamic_cast<GameObj*>(s.m_pObjHolding.item); }

// ChancePercent dll:10007570: rand() <= (int)(percent * 0.01 * 32767)
inline bool chancePercent(Self& s, double percent) { return s.world().state().rng.chance(percent * 0.01 * 32767.0); }

} // namespace biosys::game::objutil

namespace biosys::game {
// The writes of COxyCylinder / CAlarmClock into the sound FX they carry to the player's page (obj_gear.cpp)
void objFxPlace(Item* fx, Self& s);    // +4 page name, +0x8c site, +0x94 biome
void objFxFollow(Item* fx, Self& s);   // the same when the FX is not on the player's page yet, and +0x18 = 0
CSite* objFxSite(Item* fx);            // the FX's site (+0x8c)
}
