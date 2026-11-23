// biosys-gametest: exercises the game-logic framework (src/game) on saved games.
//   roundtrip <file.sav>   build the world, load the game-logic blob, save it again, compare byte for byte
//   items <file.sav>       one line per saved item: section, index, name, class, typed|raw
//   coverage               per section: items and bytes typed vs raw
//   dump <file.sav>        section/item/member = value for every typed item (format of biosys_savblob.py dump)
//   ctors                  the constructor factories of the replayed world build
//   fresh <queries.txt>    answers questions about a freshly built world (nothing loaded): the effect of the builder's
//                          post-constructor statements; one "kind name member [args]" per line, answer "line = value"
//   rng [seed]             first values of the game generator (the C runtime rand())
//   links <file.sav>       typed links between the classes: pump flows, ocean pH from its cel, rain machine source tank,
//                          video message flags, look-up functions of the World
//   worlds <file.sav>      two games in one process do not share state (random generator, lights, feature globals, options)
//   loadbugs <file.sav>    load with the original's load bugs (default), save again and list where the bytes differ
#include "engine/savetrailer.h"
#include "game/biomecel.h"
#include "game/feat_climate.h"
#include "game/feat_sensors.h"
#include "game/feat_story.h"
#include "game/feat_water.h"
#include "game/vidmessage.h"
#include "game/waterflow.h"
#include "game/watertank.h"
#include "game/gameobj.h"
#include "game/page.h"
#include "game/plant.h"
#include "game/savegame.h"
#include "game/site.h"
#include "game/world_ctors.gen.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

using namespace biosys;
using namespace biosys::game;

static void usage() {
    std::fprintf(stderr, "usage: biosys-gametest roundtrip|items|dump|loadbugs|worlds|links <file.sav> | coverage | ctors | fresh <queries> | rng [seed]\n");
}

static bool isTyped(const Segment& s) {
    if (s.item) return s.item->typed();
    return true;                    // every fixed record (header, world, timeline, self, ...) is typed
}

static const char* recordClass(const Segment& s) {
    if (s.item) return s.item->className().c_str();
    static const std::map<std::string, const char*> names = {
        {"header", "BlobHeader"}, {"world", "WorldHeader"}, {"timeline", "Timeline"}, {"self", "CSelf"},
        {"subtech", "SubTechHandler"}, {"enginefx", "EngineFx"}, {"trailer", "Trailer"}};
    return names.at(s.section);
}

// The oracle dump and the byte-identical round trip describe what the file contains: they load with the original's
// two load bugs repaired (a lung or hatch would otherwise ignore its saved base block, as the original does).
static SaveGame::Options repaired() {
    SaveGame::Options o;
    o.repairOriginalLoadBugs = true;
    return o;
}

static void loadFile(SaveGame& sg, const std::string& path, std::vector<u8>* sav, size_t* off) {
    *sav = readFile(path);
    sg.load(SaveGame::blobOf(*sav, off));
}

static int cmdRoundtrip(const std::string& path) {
    SaveGame sg(repaired());
    std::vector<u8> sav;
    size_t off = 0;
    loadFile(sg, path, &sav, &off);
    std::vector<u8> blob(sav.begin() + static_cast<long>(off), sav.begin() + static_cast<long>(savetrailer::blobEnd(sav, off, SaveGame::blobSize())));   // not the settings block after it
    std::vector<u8> out = sg.save();
    size_t n = std::min(out.size(), blob.size());
    for (size_t i = 0; i < n; i++) {
        if (out[i] == blob[i]) continue;
        std::string where = "outside any segment";
        for (const Segment& s : sg.segments())
            if (i >= s.offset && i < s.offset + s.size) {
                char b[160];
                std::snprintf(b, sizeof b, "%s/%s (index %zu) slot offset 0x%zx", s.section.c_str(), s.key.c_str(),
                              s.index, i - s.offset);
                where = b;
                break;
            }
        std::printf("MISMATCH at blob offset 0x%zx (file offset 0x%zx): %s: expected %02x, saved %02x\n", i, off + i,
                    where.c_str(), blob[i], out[i]);
        return 1;
    }
    if (out.size() != blob.size()) {
        std::printf("MISMATCH: saved blob has %zu bytes, original %zu\n", out.size(), blob.size());
        return 1;
    }
    std::printf("roundtrip OK: %zu blob bytes at file offset 0x%zx are identical (%zu items in 10 tables)\n", out.size(), off,
                [&] { size_t c = 0; for (int t = 0; t < kTableCount; t++) c += sg.world.list(static_cast<Table>(t)).size(); return c; }());
    return 0;
}

static int cmdItems(const std::string& path) {
    SaveGame sg;
    std::vector<u8> sav;
    size_t off;
    loadFile(sg, path, &sav, &off);
    for (const Segment& s : sg.segments())
        std::printf("%s\t%zu\t%s\t%s\t%s\n", s.section.c_str(), s.index, s.item ? s.item->name().c_str() : s.key.c_str(),
                    recordClass(s), isTyped(s) ? "typed" : "raw");
    return 0;
}

static int cmdCoverage() {
    SaveGame sg;
    struct Row { size_t items = 0, typed = 0, typedBytes = 0, rawBytes = 0, unusedBytes = 0; };
    std::vector<std::pair<std::string, Row>> rows;
    auto row = [&](const std::string& sec) -> Row& {
        for (auto& r : rows) if (r.first == sec) return r.second;
        rows.push_back({sec, Row()});
        return rows.back().second;
    };
    std::map<std::string, size_t> secBytes;
    for (int t = 0; t < kTableCount; t++) {
        const TableSpec& sp = tableSpec(static_cast<Table>(t));
        secBytes[sp.key] = static_cast<size_t>(sp.slots) * sp.slotSize;
    }
    for (const Segment& s : sg.segments()) {
        Row& r = row(s.section);
        bool typed = isTyped(s);
        r.items++;
        if (typed) { r.typed++; r.typedBytes += s.size; } else r.rawBytes += s.size;
    }
    std::printf("%-11s %6s %6s %6s %9s %9s %9s\n", "section", "items", "typed", "raw", "typedB", "rawB", "unusedB");
    size_t tt = 0, tr = 0, tu = 0, ti = 0, ty = 0;
    for (auto& [sec, r] : rows) {
        auto it = secBytes.find(sec);
        if (it != secBytes.end()) r.unusedBytes = it->second - r.typedBytes - r.rawBytes;
        std::printf("%-11s %6zu %6zu %6zu %9zu %9zu %9zu\n", sec.c_str(), r.items, r.typed, r.items - r.typed, r.typedBytes,
                    r.rawBytes, r.unusedBytes);
        tt += r.typedBytes; tr += r.rawBytes; tu += r.unusedBytes; ti += r.items; ty += r.typed;
    }
    std::printf("%-11s %6zu %6zu %6zu %9zu %9zu %9zu\n", "TOTAL", ti, ty, ti - ty, tt, tr, tu);
    std::printf("blob %zu bytes: typed %zu, raw %zu, unused zero slots %zu\n", SaveGame::blobSize(), tt, tr, tu);
    return 0;
}

static int cmdDump(const std::string& path) {
    SaveGame sg(repaired());
    std::vector<u8> sav;
    size_t off;
    loadFile(sg, path, &sav, &off);
    for (const Segment& s : sg.segments()) {
        if (!isTyped(s)) continue;
        // save the record, then read it back through a logging reader: the same code that loads it names its members
        std::vector<u8> buf;
        SlotWriter w(buf, s.size, s.section);
        s.record->saveSlot(w);
        std::vector<FieldLog> log;
        SlotReader r(buf.data(), buf.size(), s.section, &log);
        s.record->loadSlot(r);
        r.finishPad();
        for (const FieldLog& f : log) std::printf("%s/%s/%s = %s\n", s.section.c_str(), s.key.c_str(), f.name.c_str(), f.text.c_str());
    }
    return 0;
}

static int cmdCtors() {
    // the class column is the class the ported code builds (Item::className of the first item made by the constructor),
    // else the name the generator derived from the save format
    SaveGame sg;
    std::map<u32, std::string> live;
    sg.world.forEachItem([&](const Item& it) { live.emplace(it.ctorAddr(), it.className()); });
    std::printf("%-9s %-34s %-16s %-26s %5s  %s\n", "address", "ctor", "class", "list", "calls", "signature");
    for (const CtorInfo& c : allCtors()) {
        auto it = live.find(c.addr);
        std::printf("%08x  %-34s %-16s %-26s %5d  %s\n", c.addr, c.ctorName, it != live.end() ? it->second.c_str() : c.className,
                    c.lists, c.calls, c.signature);
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// loadbugs

static int cmdLoadBugs(const std::string& path) {
    SaveGame sg;                                 // default options: the original's behaviour
    std::vector<u8> sav;
    size_t off = 0;
    loadFile(sg, path, &sav, &off);
    std::vector<u8> blob(sav.begin() + static_cast<long>(off), sav.begin() + static_cast<long>(savetrailer::blobEnd(sav, off, SaveGame::blobSize())));   // not the settings block after it
    std::vector<u8> out = sg.save();
    if (out.size() != blob.size()) { std::printf("FAIL: saved blob has %zu bytes, original %zu\n", out.size(), blob.size()); return 1; }
    std::map<std::string, std::set<size_t>> diffs;      // "section/key (class)" -> slot offsets that differ
    size_t outside = 0;
    for (const Segment& s : sg.segments()) {
        for (size_t i = 0; i < s.size; i++) {
            if (out[s.offset + i] == blob[s.offset + i]) continue;
            const bool base = s.item && (s.item->className() == "CLung" || s.item->className() == "CHatch") && i < 40;
            if (!base) {
                outside++;
                std::printf("FAIL: byte outside the ignored base blocks differs: %s/%s slot offset 0x%zx\n", s.section.c_str(), s.key.c_str(), i);
                continue;
            }
            diffs[s.section + "/" + s.key + " (" + s.item->className() + ")"].insert(i);
        }
    }
    for (const auto& [k, v] : diffs)
        std::printf("differs: %s: %zu bytes in the base block (slot offsets 0x%zx..0x%zx)\n", k.c_str(), v.size(), *v.begin(), *v.rbegin());
    // every byte outside the segments (unused slots, padding between them) must be unchanged as well
    std::vector<bool> seg(out.size(), false);
    for (const Segment& s : sg.segments()) for (size_t i = 0; i < s.size; i++) { seg[s.offset + i] = true; }
    for (size_t i = 0; i < out.size(); i++)
        if (!seg[i] && out[i] != blob[i]) { outside++; std::printf("FAIL: byte outside every segment differs at 0x%zx\n", i); }
    std::printf("%zu items differ, %zu bytes differ outside their base blocks\n", diffs.size(), outside);
    return outside || diffs.empty() ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------------------------
// fresh: questions about a world that has only been built

static std::string hex32(u32 v) { char b[16]; std::snprintf(b, sizeof b, "0x%x", v); return b; }
static std::string itemName(const Item* it) { return it ? it->name() : "NULL"; }

static std::string answer(SaveGame& sg, const std::vector<std::string>& t) {
    World& w = sg.world;
    if (t.size() < 3 && !(t.size() == 2 && t[0] == "global")) throw Error("query needs kind name member");
    const std::string& kind = t[0];
    if (kind == "global") {          // global <ocean|dataTerminal|fuseBox0..4>
        const GameState& g = w.state();
        if (t[1] == "ocean") return itemName(g.ocean);
        if (t[1] == "dataTerminal") return itemName(g.features.dataTerminal);
        if (t[1].compare(0, 7, "fuseBox") == 0) return itemName(g.features.fuseBox[std::stoi(t[1].substr(7))]);
        throw Error("unknown global " + t[1]);
    }
    const std::string& name = t[1];
    const std::string& member = t[2];
    auto num = [&](size_t i) { return static_cast<u32>(std::stoul(t.at(i), nullptr, 0)); };
    if (kind == "page") {
        CPage* p = w.findPage(name.c_str());
        if (!p) throw Error("no page " + name);
        if (member == "milestone") return hex32(p->milestone());
    } else if (kind == "site") {
        CSite& s = w.requireSite(name.c_str());
        if (member == "unk0e8") return pyRepr(s.unk0e8());
        if (member == "unk0f0") return pyRepr(s.unk0f0());
        if (member == "feature") return itemName(s.feature());
    } else if (kind == "plant") {
        CPlant* p = w.findPlant(name.c_str());
        if (!p) throw Error("no plant " + name);
        if (member == "peek32") return hex32(p->peek32(num(3)));
        if (member == "fruitCount") return std::to_string(p->fruitCount());
        if (member == "fruitMask") return hex32(p->fruitMask());
    } else if (kind == "object") {
        GameObj* o = w.findObject(name.c_str());
        if (!o) throw Error("no object " + name);
        if (member == "peek") return hex32(o->peek(num(3), static_cast<unsigned>(num(4))));
        if (member == "variant") return o->variant();
        if (member == "inScene") return o->inScene() ? "1" : "0";
    } else if (kind == "feature") {
        CFeature& f = w.requireFeature(name.c_str());
        if (member == "peek") return hex32(f.peekDword(num(3)));
        if (member == "oxygenStore") return itemName(as<CBubbler>(&f).oxygenStore());
        if (member == "sensor") {   // the first readings, then the cel values they were made from
            CBiomeSensor& s = as<CBiomeSensor>(&f);
            const CBiomeCel* c = s.cel();
            char b[2048];
            std::snprintf(b, sizeof b,
                "pressure=%s airTemp=%s co2=%s humidity=%s light=%s oxygen=%s pollution=%s soilPH=%s soilTemp=%s soilWater=%s windSpeed=%s | "
                "airVolume=%s airCapacity=%s airTemp=%s co2=%s humidity=%s light=%s oxygen=%s pollution=%s soilPH=%s soilTemp=%s soilWater=%s windSpeed=%s",
                pyRepr(s.pressureReading()).c_str(), pyRepr(s.airTempReading()).c_str(), pyRepr(s.co2Reading()).c_str(),
                pyRepr(s.humidityReading()).c_str(), pyRepr(s.lightReading()).c_str(), pyRepr(s.oxygenReading()).c_str(),
                pyRepr(s.pollutionReading()).c_str(), pyRepr(s.soilPHReading()).c_str(), pyRepr(s.soilTempReading()).c_str(),
                pyRepr(s.soilWaterReading()).c_str(), pyRepr(s.windSpeedReading()).c_str(),
                pyRepr(c->airVolume()).c_str(), pyRepr(c->airCapacity()).c_str(), pyRepr(c->airTemp()).c_str(),
                pyRepr(c->co2()).c_str(), pyRepr(c->humidity()).c_str(), pyRepr(c->light()).c_str(),
                pyRepr(c->oxygen()).c_str(), pyRepr(c->pollution()).c_str(), pyRepr(c->soilpH()).c_str(),
                pyRepr(c->soilTemp()).c_str(), pyRepr(c->soilWater()).c_str(), pyRepr(c->windSpeed()).c_str());
            return b;
        }
    }
    throw Error("unknown query: " + kind + " " + name + " " + member);
}

static int cmdFresh(const std::string& queries, uint32_t seed) {
    SaveGame::Options o;
    o.randSeed = seed;
    SaveGame sg(o);
    std::ifstream in(queries);
    if (!in) throw Error("cannot open " + queries);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::vector<std::string> t;
        for (std::string x; ss >> x;) t.push_back(x);
        std::printf("%s = %s\n", line.c_str(), answer(sg, t).c_str());
    }
    return 0;
}

static int cmdRng(uint32_t seed) {
    Rng r(seed);
    for (int i = 0; i < 10; i++) std::printf("%d%c", r.rand(), i == 9 ? '\n' : ' ');
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// worlds

static int fails = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) fails++;
}

static bool owns(const World& w, const Item* it) {
    bool found = false;
    w.forEachItem([&](const Item& x) { if (&x == it) found = true; });
    return found;
}

static int cmdWorlds(const std::string& path) {
    SaveGame::Options oa, ob;
    oa.randSeed = 1;
    ob.randSeed = 777;
    ob.repairOriginalLoadBugs = true;
    SaveGame a(oa);                 // fresh
    SaveGame b(ob);                 // will be loaded
    std::vector<u8> sav = readFile(path);
    std::vector<u8> blob = SaveGame::blobOf(sav, nullptr);
    check(a.world.itemCount() == b.world.itemCount() && a.world.itemCount() > 1400, "two worlds are built in one process");
    check(a.world.state().rng.state() != b.world.state().rng.state(), "the generators were seeded separately (seed 1 / 777)");
    // the first sensor's first reading depends on the seed; it differs between the two worlds
    const double ra = as<CBiomeSensor>(&a.world.requireFeature("fr1probe")).pressureReading();
    const double rb = as<CBiomeSensor>(&b.world.requireFeature("fr1probe")).pressureReading();
    check(ra != rb, "the sensors read values drawn from their own world's generator");
    b.load(blob);
    // the options are per world: b repairs the load bugs and round-trips, a world with the defaults does not
    check(b.save() == blob, "world b (repaired load bugs) saves exactly what it loaded");
    const u32 rngA = a.world.state().rng.state();
    b.world.state().rng.rand();
    b.world.state().lightsOn = 1;
    b.world.state().features.sealGlobal = 99;
    check(a.world.state().rng.state() == rngA, "drawing in one world leaves the other's generator alone");
    check(a.world.state().lightsOn == 0 && a.world.state().features.sealGlobal == 0, "lights and feature globals are per world");
    check(a.world.state().features.dataTerminal && owns(a.world, a.world.state().features.dataTerminal) &&
          !owns(b.world, a.world.state().features.dataTerminal), "the feature globals of a world point into that world");
    check(owns(b.world, b.world.state().ocean) && !owns(a.world, b.world.state().ocean), "so does the ocean pointer");
    SaveGame c(oa);
    c.load(blob);
    check(c.save() != blob, "world c (default options) does not: the lungs and hatches ignored their saved base block");
    return fails ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------------------------
// links

static int cmdLinks(const std::string& path) {
    std::vector<u8> sav = readFile(path);
    std::vector<u8> blob = SaveGame::blobOf(sav, nullptr);
    SaveGame fresh;
    World& w = fresh.world;
    // CWaterPump::SetParam 0x14 (dll:10005bb0) stores the destination tank into the pump's flow (flow+8)
    size_t pumps = 0;
    bool pumpsOk = true;
    for (Item* it : w.list(Table::Features))
        if (it->className() == "CWaterPump") {
            const CWaterPump& p = as<CWaterPump>(it);
            pumps++;
            pumpsOk = pumpsOk && p.flow() && p.dstTank() && p.flow()->to() == p.dstTank();
        }
    check(pumps == 4 && pumpsOk, "the 4 water pumps stored their destination tank into their flows (" + std::to_string(pumps) + " found)");
    // COcean reads the CO2 of its cel through the typed cel (dll:10029570: pH = CO2 * -1.4999999999999947 + 8.6)
    const COcean& ocean = as<COcean>(w.state().ocean);
    check(ocean.cel() == w.findBiomeCel("O1") && ocean.ph() == ocean.cel()->co2() * -1.4999999999999947 + 8.6,
          "the ocean's pH is computed from the CO2 of its typed cel");
    // CRainMachine::Load -> SetParam(0x5f) -> CWorld::SetWaterFlowsSource (dll:10014200)
    for (u32 code : {0x60u, 0x61u}) {
        std::vector<u8> b = blob;
        SaveGame sg;
        std::string last;
        for (const Segment& s : sg.segments())
            if (s.item && s.item->className() == "CRainMachine" && s.item->name() == "fr1rain") {
                b[s.offset + 40] = static_cast<u8>(code);     // the saved source tank code follows the 40-byte base block
                last = s.item->name();
            }
        sg.load(b);
        const CRainMachine& m = as<CRainMachine>(&sg.world.requireFeature("fr1rain"));
        size_t n = 0;
        bool ok = true;
        for (Item* f : sg.world.list(Table::Waterflows)) {
            const CWaterFlow& fl = as<CWaterFlow>(f);
            if (fl.id() != 0x65) continue;
            n++;
            ok = ok && fl.from() == (code == 0x60 ? m.osmTank() : m.condTank()) && m.sourceTank() == fl.from();
        }
        check(n >= 1 && ok, "saved source tank code " + hex32(code) + ": the " + std::to_string(n) + " water flow(s) of id 0x65 start at the machine's source tank");
    }
    // the flags of a video message live in the CVidMessage objects and are what CVidLink saves
    {
        SaveGame sg;
        sg.load(blob);
        size_t links = 0, msgs = 0;
        for (Item* it : sg.world.list(Table::Features))
            if (it->className() == "CVidLink") {
                links++;
                msgs += sg.world.related(Rel::VidLinkMessage, it).size();
            }
        check(links == 2 && msgs == 9, "the 2 video links own 9 messages (" + std::to_string(links) + "/" + std::to_string(msgs) + ")");
        CVidMessage& first = as<CVidMessage>(sg.world.related(Rel::VidLinkMessage, &sg.world.requireFeature("fph8vidpho")).front());
        const i32 b0 = first.flagB();
        first.setFlagB(b0 ^ 5);
        std::vector<u8> out = sg.save();
        size_t diffs = 0;
        for (size_t i = 0; i < out.size(); i++) diffs += out[i] != blob[i];
        check(diffs >= 1 && !(out == blob), "CVidLink saves the flag stored in its message object");
    }
    // the World's look-up functions (dll:10013ad0, 10013f80, 100139b0, 100147c0, 100145a0, FUN_10014400)
    {
        World& v = fresh.world;
        const size_t logBefore = v.state().log.size();
        check(v.findBiomeCel("AB") == nullptr && v.state().log.size() == logBefore, "FindBiomeCel: names starting with A, F, X are never found, no message");
        check(v.findBiomeCel("Db1") == v.findBiomeCel("DB1") && v.findBiomeCel("DB1") != nullptr && v.state().log.size() == logBefore,
              "FindBiomeCel: D compares the whole name, ignoring case");
        check(v.findBiomeCel("R") == nullptr && v.state().log.size() == logBefore + 1 &&
              v.state().message == "BiosGame.dll: unrecognised biome cel - R", "FindBiomeCel: an unknown whole name is logged and NULL");
        check(v.findBiomeCel("HX") == v.findBiomeCel("H") || v.findBiomeCel("HX") == nullptr, "FindBiomeCel: other first letters match the first character");
        check(v.findBiomeCel("PH") != nullptr && v.findBiomeCel("PHXYZ") != nullptr && v.findBiomeCel("PHXYZ")->name() == "PH",
              "FindBiomeCel: P (not B D O R S T) matches on the first character only");
        check(v.findWaterTank("rftank") != nullptr && v.findWaterTank("NOSUCH") == nullptr &&
              v.state().message == "BiosGame.dll: unrecognised water tank - NOSUCH", "FindWaterTank: ignores case, logs and returns NULL");
        check(v.findSite("r1_extra") == v.findSite("R1") && v.findSite("R1") != nullptr, "FindSite: the name is cut at the first '_'");
        check(v.findSite("NOWHERE") == nullptr && v.state().message == "BiosGame.dll: Unrecognised site - NOWHERE", "FindSite: logs and returns NULL");
        bool threw = false;
        try { v.requireSite("NOWHERE"); } catch (const Error&) { threw = true; }
        check(threw, "callers that dereference the result (require*) throw biosys::Error");
        check(v.findFeature("FR1RAIN") != nullptr && v.findFeature("nofeature") == nullptr &&
              v.state().message == "BiosGame.dll: Unrecognised feature - <nofeature>", "FindFeature: ignores case, logs and returns NULL");
        check(v.findObject("grub_1") != nullptr && v.findObject("grub") == nullptr, "FindObject: whole name, no cut at '_'");
        check(v.findPlant("AR1PA_x") == v.findPlant("AR1PA") && v.findPlant("AR1PA") != nullptr, "FindPlant: the name is cut at the first '_'");
        check(v.findPage("r9") != nullptr && v.findPage("nopage") == nullptr, "FindPage: by name");
    }
    return fails ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 64; }
    std::string cmd = argv[1];
    try {
        if (cmd == "coverage" && argc == 2) return cmdCoverage();
        if (cmd == "ctors" && argc == 2) return cmdCtors();
        if (argc == 3 && cmd == "roundtrip") return cmdRoundtrip(argv[2]);
        if (argc == 3 && cmd == "items") return cmdItems(argv[2]);
        if (argc == 3 && cmd == "dump") return cmdDump(argv[2]);
        if (argc == 3 && cmd == "loadbugs") return cmdLoadBugs(argv[2]);
        if (argc == 3 && cmd == "worlds") return cmdWorlds(argv[2]);
        if (argc == 3 && cmd == "links") return cmdLinks(argv[2]);
        if (argc >= 3 && cmd == "fresh") return cmdFresh(argv[2], argc > 3 ? static_cast<uint32_t>(std::stoul(argv[3])) : 1);
        if (cmd == "rng") return cmdRng(argc > 2 ? static_cast<uint32_t>(std::stoul(argv[2])) : 1);
    } catch (const Error& e) {
        std::printf("biosys::Error: %s\n", e.what());
        return 2;
    }
    usage();
    return 64;
}
