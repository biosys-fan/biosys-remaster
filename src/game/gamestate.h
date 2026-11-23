// The mutable state the original keeps in DLL globals (.data/.bss), owned by the World so that several worlds can
// live in one process. Everything here has an original address; members of the classes (not globals) stay in the
// classes.
#pragma once
#include "game/fixes.h"
#include "game/settings.h"
#include "game/rng.h"
#include "game/slot.h"
#include <string>
#include <vector>

namespace biosys::game {

class Item;

// Globals the feature classes read or write.
struct FeatureGlobals {
    i32 podGlobal = -1;               // DAT_1009f424  set to -1 by CPodFeature::Load dll:10002800
    u32 sealGlobal = 0;               // DAT_100c42cc  written by CSeal::Save, read by CSeal::Load dll:10024940/10024980
    std::string currentSiteName;      // DAT_100c1e24  compared with "DB6CAVE" by CExitDoor::Load dll:1000b6e0
    Item* dataTerminal = nullptr;     // _DAT_100c4274 builder statement after CDataTerminal
    Item* fuseBox[5] = {};            // DAT_100c1150..DAT_100c1160 builder statements after CFuseBox
    struct LiftState { i32 sound = -1, target = 0, current = 0, textFail = 0; } lift;   // DAT_100a2bac, 100c42bc, 100c42c0, 100c42c4 (CLift)
};

struct WorldHeader;
struct Timeline;
struct BlobHeader;
class FeatureHost;
class Sim;

// What the machine code reads of the DLL's world / timeline / story objects (the scalars of CWorld, CTimeline, CGame
// live in the SaveGame's records; SaveGame points these at them). dll:100c0f60 (CWorld), 100c1c18 (CTimeline),
// 100c2720 (story).
struct SimRefs {
    WorldHeader* world = nullptr;     // CWorld scalars: power (+0x1d0..+0x1e8), weather (+0x208..+0x290)
    Timeline* timeline = nullptr;     // DAT_100c1c18: time +0x04 (DAT_100c1c1c), time of day +0x10 (DAT_100c1c28)
    BlobHeader* story = nullptr;      // DAT_100c2720: milestones +0x0c (DAT_100c272c)
    i32 diurnal = 0x28;               // DAT_100c1c44 timeline+0x2c: 5 dawn, 0xf morning, 0x14 day, 0x1e evening, 0x23 dusk, 0x28 night
    double daylight = 0;              // timeline+0x20
};

struct GameState {
    Rng rng;                          // the C runtime's rand state (_holdrand)
    SimRefs sim;
    Sim* simCore = nullptr;           // the simulation core of this world (sim.h); set by Sim's constructor, null before
    FeatureHost* host = nullptr;      // what the machines need from the shell (engine calls, messages); null in tests
    size_t featureCursor = 0;         // CWorld+0x128: the next feature CWorld::UpdateNextFeature updates (0 = start); not saved
    size_t objectCursor = 0;          // CWorld+0xb4: the next object CWorld::UpdateNextObject updates (1-based, 0 = start); not saved
    i32 lightsOn = 0;                 // DAT_100c3224: lights of the current room on/off; written into every site slot
    FeatureGlobals features;
    Item* ocean = nullptr;            // DAT_100c1cc8 the COcean tank (builder statement after COcean's constructor)
    std::string message;              // DAT_100c2650: text buffer the CWorld::Find* functions fill before returning NULL
    std::vector<std::string> log;     // what the original passes to Eng_WriteLog (game code has no engine in tests)
    // The remaster switch (fixes.h): every ORIGINAL BUG site reads its flag here. All false = the faithful translation.
    // (The old repairOriginalLoadBugs option is the pair lungLoadBaseBlock + hatchLoadBaseBlock.)
    Fixes fixes;
    // The remaster layer (settings.h): model corrections and difficulty numbers; defaults = the faithful translation.
    Model model;
    Tuning tuning;
    Settings settings() const { return Settings{fixes, model, tuning}; }
    void setSettings(const Settings& s) { fixes = s.fixes; model = s.model; tuning = s.tuning; }

    // Applies a builder statement `DAT_xxxxxxxx = pCVar;`; throws for an unknown address.
    void setPointer(u32 addr, Item* it);
};

} // namespace biosys::game
