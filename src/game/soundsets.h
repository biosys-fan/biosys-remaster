// The sound sets the CSite constructors point to (DllPtr arguments): arrays of wav files the site plays at random
// for a time of day (CSite::OnTimeOfDayChange dll:1000db70). The data is read from BiosGame.dll by
// tools/gen/gen_soundsets.py (soundsets.gen.cpp).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace biosys::game {

struct SoundSetDef {
    uint32_t addr;                     // DLL address of the set (the DllPtr value)
    std::vector<std::string> files;    // entries 1..count (entry 0 is {"count", n})
};

const std::vector<SoundSetDef>& soundSetDefs();

} // namespace biosys::game
