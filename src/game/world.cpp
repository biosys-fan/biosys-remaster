#include "game/world.h"
#include "game/biomecel.h"
#include "game/feature.h"
#include "game/gameobj.h"
#include "game/page.h"
#include "game/plant.h"
#include "game/site.h"
#include "game/waterflow.h"
#include "game/watertank.h"
#include <cstring>
#include <strings.h>

namespace biosys::game {

const TableSpec& tableSpec(Table t) {
    // saveblob.md section 3: slot count x slot size of the ten tables
    static const TableSpec specs[kTableCount] = {
        {"cels", 0x40, 0x100},   // BIOME_CEL_SLOTS, BIOME_CEL_SAVE_BUFFER_SIZE
        {"sites", 0x7f, 0x7f},   // SITE_SLOTS, SITE_SAVE_BUFFER_SIZE
        {"tanks", 0x7f, 0x80},   // WATERTANK_SLOTS, WATERTANK_SAVE_BUFFER_SIZE
        {"airflows", 0x7f, 0x20},
        {"waterflows", 0x7f, 0x20},
        {"localfx", 100, 0x10},
        {"fx", 300, 0x10},
        {"plants", 0x80, 0x80},
        {"features", 0x100, 0x100},
        {"objects", 0x200, 0x40},
    };
    return specs[static_cast<int>(t)];
}

const char* relName(Rel r) {
    static const char* names[] = {"WorldPage", "PageFx", "PageOccFx", "PageOccLoopedFx", "SiteFx", "SiteFeature",
                                  "SitePlant", "SiteLink", "SiteList60", "WorldPlantFood", "WorldList_f0",
                                  "WorldFuse", "WorldSensor", "VidLinkMessage"};
    return names[static_cast<int>(r)];
}

Item* Item::siteCel() const { throw Error(className_ + " '" + name_ + "' is not a site"); }

std::string RawArg::text() const {
    switch (kind) {
    case Int: return std::to_string(i);
    case Double: return pyRepr(d);
    case Str: return pyRepr(s);
    case Null: return "NULL";
    case ItemRef: return "item:" + item->name();
    case BiomeRef: return std::string("biome:") + biome->name;
    case Ptr: { char b[24]; std::snprintf(b, sizeof b, "ptr:0x%08x", static_cast<u32>(i)); return b; }
    }
    return "";
}

void RawItem::saveSlot(SlotWriter& w) const { w.writeBytes(bytes_); }
void RawItem::loadSlot(SlotReader& r) { bytes_ = r.takeRest(); }

// CSite ctor dll:1000c270: site+8 = param_5 = ctor argument 4
Item* RawItem::siteCel() const {
    if (ctorAddr() != 0x1000c270) return Item::siteCel();
    if (args_.size() <= 4 || args_[4].kind != RawArg::ItemRef) throw Error("site '" + name() + "' has no cel");
    return args_[4].item;
}

World::World() = default;
World::~World() = default;

Item* World::adopt(std::unique_ptr<Item> it) {
    pool_.push_back(std::move(it));
    return pool_.back().get();
}

void World::add(Table t, Item* it, const char* fn) {
    if (!it) throw Error(std::string(fn) + ": item pointer is NULL");   // the original logs and returns 0
    lists_[static_cast<int>(t)].push_back(it);
}

void World::addBiomeCel(Item* it) { add(Table::Cels, it, "CWorld::AddBiomeCel"); }
void World::addSite(Item* it) { add(Table::Sites, it, "CWorld::AddSite"); }
void World::addWaterTank(Item* it) { add(Table::Tanks, it, "CWorld::AddWaterTank"); }
void World::addAirFlow(Item* it) { add(Table::Airflows, it, "CWorld::AddAirFlow"); }
void World::addWaterFlow(Item* it) { add(Table::Waterflows, it, "CWorld::AddWaterFlow"); }
void World::addLocalFX(Item* it) { add(Table::LocalFx, it, "CWorld::AddLocalFX"); }
void World::addFX(Item* it) { add(Table::Fx, it, "CWorld::AddFX"); }
void World::addPlant(Item* it) { add(Table::Plants, it, "CWorld::AddPlant"); }
void World::addFeature(Item* it) { add(Table::Features, it, "CWorld::AddFeature"); }
void World::addObject(Item* it) { add(Table::Objects, it, "CWorld::AddObject"); }

void World::relate(Rel r, Item* owner, Item* item) {
    if (!item) throw Error(std::string(relName(r)) + ": item pointer is NULL");
    rel_[{static_cast<int>(r), owner}].push_back(item);
}

const std::vector<Item*>& World::related(Rel r, const Item* owner) const {
    static const std::vector<Item*> none;
    auto it = rel_.find({static_cast<int>(r), owner});
    return it == rel_.end() ? none : it->second;
}

Biome* World::biome(uint32_t addr) {
    Biome& b = biomes_[addr];
    if (!b.addr) { b.addr = addr; b.name = biomeName(addr); }
    return &b;
}

void World::biomeAddCel(Biome* b, Item* cel) {
    if (!b || !cel) throw Error("CBiome::AddCel: NULL argument");
    b->cels.push_back(cel);
}

void World::biomeAddSensor(Biome* b, Item* sensor) {
    if (!b || !sensor) throw Error("CBiome::AddSensor: NULL argument");
    b->sensors.push_back(sensor);
}

static std::string cutAtUnderscore(const char* name) {
    std::string s(name);
    size_t p = s.find('_');
    if (p != std::string::npos) s.resize(p);
    return s;
}

// The original strcpy/strcat/sprintf's the message into the global text buffer DAT_100c2650 and returns NULL.
void World::notFound(const std::string& text) const {
    state_.message = text;
    state_.log.push_back(text);
}

// dll:10013ad0: no cels at all, or a name starting with A, F or X (never found), or no match: log, NULL. B D O R S T
// compare the whole name (FUN_10018510, case-insensitive); any other first letter compares only the first character
// of the cel name (FUN_100184f0).
CBiomeCel* World::findBiomeCel(const char* name) const {
    const std::vector<Item*>& cels = list(Table::Cels);
    if (!cels.empty()) {
        switch (*name) {
        case 'A': case 'F': case 'X':
            return nullptr;                  // returns before the log text is built
        case 'B': case 'D': case 'O': case 'R': case 'S': case 'T':
            for (Item* c : cels)
                if (strcasecmp(c->name().c_str(), name) == 0) return &as<CBiomeCel>(c);
            break;
        default:
            for (Item* c : cels)
                if (!c->name().empty() && c->name()[0] == *name) return &as<CBiomeCel>(c);
        }
    }
    notFound(std::string("BiosGame.dll: unrecognised biome cel - ") + name);
    return nullptr;
}

// dll:10013f80 (FUN_1000f000: __strcmpi with the name at +4)
CWaterTank* World::findWaterTank(const char* name) const {
    for (Item* t : list(Table::Tanks))
        if (strcasecmp(t->name().c_str(), name) == 0) return &as<CWaterTank>(t);
    if (!list(Table::Tanks).empty()) notFound(std::string("BiosGame.dll: unrecognised water tank - ") + name);
    return nullptr;
}

// dll:100139b0
CSite* World::findSite(const char* name) const {
    std::string q = cutAtUnderscore(name);
    for (Item* it : list(Table::Sites))
        if (strcasecmp(it->name().c_str(), q.c_str()) == 0) return &as<CSite>(it);
    notFound(std::string("BiosGame.dll: Unrecognised site - ") + name);
    return nullptr;
}

// dll:100147c0
CFeature* World::findFeature(const char* name) const {
    std::string q = cutAtUnderscore(name);
    for (Item* it : list(Table::Features))
        if (strcasecmp(it->name().c_str(), q.c_str()) == 0) return &as<CFeature>(it);
    notFound(std::string("BiosGame.dll: Unrecognised feature - <") + name + ">");
    return nullptr;
}

// dll:100145a0 (FUN_100291f0: __strcmpi with the name at +4)
GameObj* World::findObject(const char* name) const {
    for (Item* it : list(Table::Objects))
        if (strcasecmp(it->name().c_str(), name) == 0) return &as<GameObj>(it);
    notFound(std::string("BiosGame.dll: Unrecognised object - ") + name);
    return nullptr;
}

// FUN_10014400 (FUN_100728f0 compares the plant name at +0x50)
CPlant* World::findPlant(const char* name) const {
    std::string q = cutAtUnderscore(name);
    for (Item* it : list(Table::Plants))
        if (strcasecmp(it->name().c_str(), q.c_str()) == 0) return &as<CPlant>(it);
    notFound(std::string("BiosGame.dll: Unrecognised Plant - ") + name);
    return nullptr;
}

// FUN_10013850 (virtual +0x18 of the page compares the name; no log text)
CPage* World::findPage(const char* name) const {
    for (Item* it : related(Rel::WorldPage, nullptr)) {
        if (dynamic_cast<CGameEndPage*>(it)) {      // CGameEndPage::MatchesName dll:1006fe50 (page vtable +0x18): "eGameEnd_3" is the page "eGameEnd"
            std::string n = name;
            size_t u = n.find('_');
            if (u != std::string::npos) n.resize(u);
            if (strcasecmp(it->name().c_str(), n.c_str()) == 0) return &as<CPage>(it);
        } else if (strcasecmp(it->name().c_str(), name) == 0) {
            return &as<CPage>(it);
        }
    }
    return nullptr;
}

CBiomeCel& World::requireBiomeCel(const char* name) const {
    CBiomeCel* c = findBiomeCel(name);
    if (!c) throw Error(std::string("unrecognised biome cel ") + name);
    return *c;
}
CWaterTank& World::requireWaterTank(const char* name) const {
    CWaterTank* t = findWaterTank(name);
    if (!t) throw Error(std::string("unrecognised water tank ") + name);
    return *t;
}
CSite& World::requireSite(const char* name) const {
    CSite* s = findSite(name);
    if (!s) throw Error(std::string("unrecognised site ") + name);
    return *s;
}
CFeature& World::requireFeature(const char* name) const {
    CFeature* f = findFeature(name);
    if (!f) throw Error(std::string("unrecognised feature ") + name);
    return *f;
}

// dll:10014200
void World::setWaterFlowsSource(uint16_t flowId, CWaterTank* tank) {
    for (Item* it : list(Table::Waterflows)) {
        CWaterFlow& f = as<CWaterFlow>(it);
        if (f.id() == flowId) f.setFrom(tank);
    }
}

} // namespace biosys::game
