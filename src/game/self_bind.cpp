// The link between the player and the shell: the world, the host and the display (see self_env.h).
#include "game/self.h"
#include "game/featenv.h"

namespace biosys::game {

namespace {
// What a missing host / display does: nothing.
PlayerHost g_nullHost;
PlayerDisplay g_nullDisplay;
}

void Self::bind(World& world, PlayerHost* host, PlayerDisplay* display) {
    world_ = &world;
    host_ = host ? host : &g_nullHost;
    display_ = display ? display : &g_nullDisplay;
}

// DAT_100c1c1c: the timeline's game time
i32 Self::now() const { return cworld::gameTime(*world_); }

} // namespace biosys::game
