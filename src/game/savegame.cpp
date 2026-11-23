#include "game/savegame.h"
#include "engine/savetrailer.h"

namespace biosys::game {

SaveGame::SaveGame(const Options& options) : options_(options) {
    world.state().fixes = options.fixes;
    world.state().model = options.model;
    world.state().tuning = options.tuning;
    if (options.repairOriginalLoadBugs) world.state().fixes.lungLoadBaseBlock = world.state().fixes.hatchLoadBaseBlock = true;
    world.state().sim.world = &worldHeader;       // the DLL globals CWorld / CTimeline / story live in these records
    world.state().sim.timeline = &timeline;
    world.state().sim.story = &header;
    world.state().rng.seed(options.randSeed);     // FUN_10079370(GetTickCount()) at the top of Event_OnStartup
    buildWorld(world);
}

size_t SaveGame::blobSize() {
    size_t n = BlobHeader::kSize + WorldHeader::kSize;
    for (int t = 0; t < kTableCount; t++) {
        const TableSpec& s = tableSpec(static_cast<Table>(t));
        n += static_cast<size_t>(s.slots) * s.slotSize;
    }
    return n + Timeline::kSize + Self::kSize + SubTechHandler::kSize + EngineFx::kSize + Trailer::kSize;
}

std::vector<Segment> SaveGame::segments() const {
    std::vector<Segment> out;
    size_t pos = 0;
    auto fixed = [&](const char* name, size_t size, const Record* r) {
        Segment s;
        s.section = name; s.key = name == std::string("world") ? "state" : name;
        s.offset = pos; s.size = size; s.record = const_cast<Record*>(r);
        out.push_back(s);
        pos += size;
    };
    fixed("header", BlobHeader::kSize, &header);
    fixed("world", WorldHeader::kSize, &worldHeader);
    for (int t = 0; t < kTableCount; t++) {
        const TableSpec& spec = tableSpec(static_cast<Table>(t));
        const std::vector<Item*>& items = world.list(static_cast<Table>(t));
        if (items.size() > spec.slots)
            throw Error(std::string(spec.key) + ": " + std::to_string(items.size()) + " items but only " +
                        std::to_string(spec.slots) + " slots");
        std::map<std::string, int> seen;
        for (size_t i = 0; i < items.size(); i++) {
            Segment s;
            s.section = spec.key; s.index = i;
            int n = ++seen[items[i]->name()];
            s.key = n == 1 ? items[i]->name() : items[i]->name() + "#" + std::to_string(n);
            s.offset = pos + i * spec.slotSize; s.size = spec.slotSize;
            s.record = items[i]; s.item = items[i];
            out.push_back(s);
        }
        pos += static_cast<size_t>(spec.slots) * spec.slotSize;
    }
    fixed("timeline", Timeline::kSize, &timeline);
    fixed("self", Self::kSize, &self);
    fixed("subtech", SubTechHandler::kSize, &subTech);
    fixed("enginefx", EngineFx::kSize, &engineFx);
    fixed("trailer", Trailer::kSize, &trailer);
    return out;
}

std::vector<u8> SaveGame::save() const {
    std::vector<u8> out;
    out.reserve(blobSize());
    for (const Segment& s : segments()) {
        // unused slots are zero: the buffer is zero-extended up to the next segment
        if (out.size() < s.offset) out.resize(s.offset, 0);
        SlotWriter w(out, s.size, s.section + "/" + s.key);
        s.record->saveSlot(w);
    }
    out.resize(blobSize(), 0);
    return out;
}

void SaveGame::load(const std::vector<u8>& blob) {
    if (blob.size() != blobSize())
        throw Error("game blob has " + std::to_string(blob.size()) + " bytes, expected " + std::to_string(blobSize()));
    for (const Segment& s : segments()) {
        SlotReader r(blob.data() + s.offset, s.size, s.section + "/" + s.key);
        s.record->loadSlot(r);
        r.finishPad();
    }
    // the slots after the last live item are read and discarded by the original (CWorld::Load dll:10012590)
    self.resolve(world);      // the end of CSelf::Load dll:10064890: the saved names become pointers
}

std::vector<u8> SaveGame::blobOf(const std::vector<u8>& sav, size_t* offset) {
    ByteReader br(sav);
    size_t off = br.u32At(8);      // engine header +8: where the control-program blob starts
    if (off > sav.size()) throw Error("saved game: blob offset is beyond the end of the file");
    if (offset) *offset = off;
    return std::vector<u8>(sav.begin() + static_cast<long>(off), sav.begin() + static_cast<long>(savetrailer::blobEnd(sav, off, blobSize())));
}

} // namespace biosys::game
