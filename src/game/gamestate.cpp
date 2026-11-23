#include "game/gamestate.h"
#include "game/world.h"

namespace biosys::game {

void GameState::setPointer(u32 addr, Item* it) {
    if (addr == 0x100c1cc8) { ocean = it; return; }
    if (addr == 0x100c4274) { features.dataTerminal = it; return; }
    if (addr >= 0x100c1150 && addr <= 0x100c1160 && (addr & 3) == 0) {
        features.fuseBox[(addr - 0x100c1150) / 4] = it;
        return;
    }
    throw Error("GameState::setPointer: unknown global " + std::to_string(addr));
}

} // namespace biosys::game
