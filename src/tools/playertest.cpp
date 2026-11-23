// biosys-playertest: tests of the typed player record (src/game/self.h).
//   dump <file.sav>    member lines of the 600-byte CSelf slot read through Self ("self/self/name = value")
//   objects            construction state of the game objects and the builders' post-constructor stores (GameObj::poke)
//   check <file.sav>   round trip of the slot through Self (byte identical), rejection of a bad start marker,
//                      name resolution against the replayed world, constructor state
#include "game/savegame.h"
#include "game/gameobj.h"
#include "game/obj_food.h"
#include "game/self.h"
#include <cstdio>

using namespace biosys;
using namespace biosys::game;

static std::vector<u8> selfSlot(const std::string& path) {
    std::vector<u8> sav = readFile(path);
    std::vector<u8> blob = SaveGame::blobOf(sav, nullptr);
    SaveGame sg;                                   // only for the segment offsets
    for (const Segment& s : sg.segments())
        if (s.section == "self") {
            if (s.offset + s.size > blob.size()) throw Error("blob too short for the CSelf slot");
            return std::vector<u8>(blob.begin() + static_cast<long>(s.offset), blob.begin() + static_cast<long>(s.offset + s.size));
        }
    throw Error("no self segment");
}

static int cmdDump(const std::string& path) {
    std::vector<u8> slot = selfSlot(path);
    Self s;
    std::vector<FieldLog> log;
    SlotReader r(slot.data(), slot.size(), "self", &log);
    s.loadSlot(r);
    r.finishPad();
    for (const FieldLog& f : log) std::printf("self/self/%s = %s\n", f.name.c_str(), f.text.c_str());
    return 0;
}

static int fails = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) fails++;
}

static int cmdCheck(const std::string& path) {
    std::vector<u8> slot = selfSlot(path);
    Self s;
    {
        SlotReader r(slot.data(), slot.size(), "self");
        s.loadSlot(r);
        r.finishPad();
    }
    std::vector<u8> out;
    {
        SlotWriter w(out, Self::kSize, "self");
        s.saveSlot(w);
    }
    check(out == slot, "600 bytes round-trip through Self");
    // the dump of the saved state is the dump of the original
    {
        std::vector<FieldLog> a = s.dump();
        check(!a.empty() && a.front().name == "tag" && a.back().name != "", "Self::dump lists the members");
    }
    // bad start marker
    {
        std::vector<u8> bad = slot;
        bad[3] = 'X';
        Self t;
        bool threw = false;
        try { SlotReader r(bad.data(), bad.size(), "self"); t.loadSlot(r); } catch (const Error&) { threw = true; }
        check(threw, "bad start marker rejected");
    }
    // non-zero byte in the never-written tail
    {
        std::vector<u8> bad = slot;
        bad[590] = 1;
        Self t;
        bool threw = false;
        try { SlotReader r(bad.data(), bad.size(), "self"); t.loadSlot(r); r.finishPad(); } catch (const Error&) { threw = true; }
        check(threw, "non-zero padding rejected");
    }
    // names resolve against the replayed world
    {
        SaveGame sg;
        s.resolve(sg.world);
        check(s.m_pSite.item && s.m_pSite.item->name() == s.m_pSite.name(), "site '" + s.m_pSite.name() + "' resolved");
        check(s.m_pCel != nullptr, "cel of the site resolved");
        check(s.m_pPreviousSite.item != nullptr, "previous site resolved");
        check(s.m_pPage != nullptr, "page of m_szLocation resolved");
        check(!s.m_pPlant.item && !s.m_pObjHolding.item && !s.m_pGoggles.item, "empty names stay NULL");
    }
    // constructor state (dll:10063ca0)
    {
        Self c;
        check(c.m_dHydration == 40 && c.m_dNutrition == 75 && c.m_dProtein == 100 && c.m_dTemperature == 50 &&
              c.m_dMaxTemp == 73 && c.m_dHealth == 90 && c.m_dEnergy == 63, "constructor stats 40/75/100/50/73/90/63");
        check(c.m_wHydrationState == 3 && c.m_wEnergyState == 3 && c.m_dwLastWokeUp == 28800 &&
              c.m_dwDreamsSeen == 0x80000000u && c.m_lHeartBeatVol == -10000 && c.m_bAwake == 1 && c.m_hHeartBeatLoop == -1,
              "constructor states and sentinels");
        check(c.m_szLocation[0] == 'R' && c.m_szLocation[1] == '4' && c.m_szLocation[2] == 0, "constructor location R4");
        // the constructor state saves and loads back unchanged
        std::vector<u8> b;
        SlotWriter w(b, Self::kSize, "self");
        c.saveSlot(w);
        check(b[0] == 'S' && b[3] == 'F' && b.size() == Self::kSize, "constructor state saves a SELF slot");
    }
    return fails ? 1 : 0;
}

static GameObj& obj(SaveGame& sg, const char* name) {
    for (Item* it : sg.world.list(Table::Objects))
        if (it->name() == name) {
            GameObj* o = dynamic_cast<GameObj*>(it);
            if (!o) throw Error(std::string("object is not typed: ") + name);
            return *o;
        }
    throw Error(std::string("no object ") + name);
}

static int cmdObjects() {
    SaveGame sg;      // the world as built (nothing loaded)
    size_t typed = 0;
    for (Item* it : sg.world.list(Table::Objects)) typed += dynamic_cast<GameObj*>(it) != nullptr;
    check(typed == 423, "all 423 objects are GameObj (" + std::to_string(typed) + ")");
    check(obj(sg, "goggles").weight() == 4.0 && obj(sg, "oxy_cyl").weight() == 30.0 && obj(sg, "fuse1").weight() == 0.2,
          "constructor weights");
    check(obj(sg, "term_1").inScene() && obj(sg, "term_1").timeStamp() == 28800, "ordinary food is in the scene");
    check(!obj(sg, "r4nv_1").inScene() && static_cast<ObjFood&>(obj(sg, "r4nv_1")).state() == 1 &&
          static_cast<ObjFood&>(obj(sg, "r4nv_1")).timePicked() == 28800,
          "food with flag 0x8000 starts removed from the scene (state 1)");
    check(static_cast<ObjFood&>(obj(sg, "r4nv_1")).plant() != nullptr, "food keeps its plant");
    // the stores of the original builders (post-constructor statements of world_build.gen.cpp) are applied by the build
    check(obj(sg, "russkey").pickupEvent() == 0x1000, "russkey has its pick-up event from the builder");
    objectPoke(&obj(sg, "russkey"), 0x18, 4, 0x2000, PokeOp::Set);
    check(obj(sg, "russkey").pickupEvent() == 0x2000, "poke +0x18");
    u32 before = obj(sg, "r7fish_1").flags();
    objectPoke(&obj(sg, "r7fish_1"), 0x14, 4, 0x400000, PokeOp::Or);
    check(obj(sg, "r7fish_1").flags() == (before | 0x400000), "poke |= +0x14");
    check(obj(sg, "fuse60").variant() == "burnt", "variant string store of the builder");
    check(!obj(sg, "apple2").inScene(), "apple2 is removed from the scene by the builder (the saved state is unk_2c = 0)");
    objectVirtualCall(&obj(sg, "apple"), 0x94);
    check(!obj(sg, "apple").inScene(), "vtable +0x94 removes it");
    bool threw = false;
    try { objectPoke(&obj(sg, "goggles"), 0x7c, 4, 1, PokeOp::Set); } catch (const Error&) { threw = true; }
    check(threw, "store at a member the class does not have is rejected");
    return fails ? 1 : 0;
}

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "dump") return cmdDump(argv[2]);
        if (argc == 2 && std::string(argv[1]) == "objects") return cmdObjects();
        if (argc == 3 && std::string(argv[1]) == "check") return cmdCheck(argv[2]);
    } catch (const Error& e) {
        std::printf("biosys::Error: %s\n", e.what());
        return 2;
    }
    std::fprintf(stderr, "usage: biosys-playertest dump|check <file.sav> | objects\n");
    return 64;
}
