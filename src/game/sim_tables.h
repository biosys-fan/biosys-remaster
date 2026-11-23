// The step tables of the plant model (photosynthesis: light, CO2, temperature), read from the DLL image by
// tools/gen/gen_simtables.py. A CPlant keeps the DLL address of its tables (+0xd8 / +0xdc / +0xe0).
#pragma once
#include <cstdint>

namespace biosys::game {

struct StepTable {
    uint32_t addr;       // DLL data address
    int last;            // index of the last (x, y) pair: the `n` LookUpVal (dll:10007a60) is called with
    const double* xy;    // (last + 1) pairs
};

// null for an address that holds no table
const StepTable* stepTable(uint32_t addr);

} // namespace biosys::game
