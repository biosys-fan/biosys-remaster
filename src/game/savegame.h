// Container for the game-logic blob of a saved game: everything CGame::Save dll:10008db0 writes after the
// engine part of the .sav (analysis/notes/saveblob.md sections 1-4). The blob has no counts or lengths: load
// needs a world constructed first and matches saved slots to live items by list position.
#pragma once
#include "game/blobrecords.h"
#include "game/self.h"
#include "game/world.h"

namespace biosys::game {

// One stretch of the blob: a fixed record or one slot of a table (item != nullptr).
struct Segment {
    std::string section;     // "header", "world", "cels", ... "trailer"
    std::string key;         // item name, "#n" appended to repeated names (as biosys_savblob.py dump)
    size_t index = 0;        // position in its section
    size_t offset = 0, size = 0;
    Record* record = nullptr;     // points into the SaveGame (const-ness is not tracked: load() writes through it)
    const Item* item = nullptr;
};

class SaveGame {
public:
    struct Options {
        // The two ORIGINAL BUG loads (CLung::Load dll:100102d0, CHatch::Load dll:1001ce10) call the base Save instead of
        // Load, so the original ignores the saved base block of lungs and hatches. By default the port does the same;
        // with true it loads the block, so that a saved game round-trips byte for byte.
        bool repairOriginalLoadBugs = false;       // shorthand for fixes.lungLoadBaseBlock + fixes.hatchLoadBaseBlock
        Fixes fixes;                               // the remaster switch (fixes.h); default faithful
        Model model;                               // model corrections (settings.h); default faithful
        Tuning tuning;                             // difficulty numbers (settings.h); default original
        void set(const Settings& s) { fixes = s.fixes; model = s.model; tuning = s.tuning; }
        // srand() of the C runtime: the original seeds with GetTickCount() in Event_OnStartup (dll:1001d7b0). The
        // game code has no clock; the host passes the value. 1 is the runtime's own initial seed.
        uint32_t randSeed = 1;
    };

    SaveGame() : SaveGame(Options()) {}
    // Replays Event_OnStartup: seeds the generator, then buildWorld() replays the original start-up code.
    explicit SaveGame(const Options& options);
    const Options& options() const { return options_; }
    // The one setter: switches the fixes of this world (may be called at any time; the flags are read where the bug sits).
    void setFixes(const Fixes& f) { world.state().fixes = f; }
    const Fixes& fixes() const { return world.state().fixes; }
    void setSettings(const Settings& s) { world.state().setSettings(s); }
    Settings settings() const { return world.state().settings(); }

    // Throws biosys::Error on any size, marker or version mismatch.
    void load(const std::vector<u8>& blob);
    std::vector<u8> save() const;

    static size_t blobSize();                    // 179017: fixed by the table constants
    // The blob inside a whole .sav file: from the offset stored at file header+8 to EOF. dll:10009080 reads it there.
    static std::vector<u8> blobOf(const std::vector<u8>& savFile, size_t* offset = nullptr);

    std::vector<Segment> segments() const;       // blob order, with offsets

    BlobHeader header;
    WorldHeader worldHeader;
    World world;
    Timeline timeline;
    Self self;
    SubTechHandler subTech;
    EngineFx engineFx;
    Trailer trailer;

private:
    Options options_;
};

} // namespace biosys::game
