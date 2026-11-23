// The game world: ten ordered item lists (the ones the save blob stores) plus the side lists the original
// builds next to them. Items are created by constructor factories (world_ctors.gen.h), registered by the
// builder code (world_build.gen.cpp, a replay of the original start-up code) and owned by the World.
#pragma once
#include "game/gamestate.h"
#include "game/slot.h"
#include <map>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

namespace biosys::game {

// The ten tables of CWorld::Save, in blob order (analysis/notes/saveblob.md section 3).
enum class Table { Cels, Sites, Tanks, Airflows, Waterflows, LocalFx, Fx, Plants, Features, Objects };
constexpr int kTableCount = 10;
struct TableSpec {
    const char* key;      // "cels", "sites", ... (names used by biosys_savblob.py)
    u32 slots;            // XXX_SLOTS: slots in the blob
    u32 slotSize;         // XXX_SAVE_BUFFER_SIZE
};
const TableSpec& tableSpec(Table t);

// Owner lists the original builds besides the ten tables. The owner is the item whose list it is
// (nullptr = the world itself); the comment names the original Add function.
enum class Rel {
    WorldPage,         // CWorld::AddPage dll:10013820
    PageFx,            // CPage::AddFX dll:1000f6e0 (page+0x10), also CWorld::AddFX
    PageOccFx,         // CPage::AddOccFX dll:1000f7a0 (page+0x2c), also CWorld::AddFX
    PageOccLoopedFx,   // CPage::AddOccLoopedFX dll:1000f890 (page+0x4c), also CWorld::AddFX
    SiteFx,            // CSite::AddFX dll:1000c9b0 (site+0x98), also CWorld::AddFX
    SiteFeature,       // CSite::AddFeature dll:1000c880 (site+0x28), also CWorld::AddFeature; or a bare CObList::AddTail
    SitePlant,         // CSite::AddPlant dll:1000c8c0, also the world plants list
    SiteLink,          // dll:1000c930 (site+0x7c)
    SiteList60,        // dll:1000c900 (site+0x60)
    WorldPlantFood,    // dll:100146d0 (world+0xd4), also CWorld::AddObject
    WorldList_f0,      // dll:10014710 (world+0xf0)
    WorldFuse,         // dll:10014f40 (world+0xb8)
    WorldSensor,       // CWorld::AddSensor dll:100148e0 (world+0x12c)
    VidLinkMessage,    // CVidLink::AddMessage
};
const char* relName(Rel r);

struct Biome {                       // the nine static CBiome objects (analysis/notes/simulation.md section 3)
    uint32_t addr = 0;               // DLL data address of the original global
    const char* name = "";
    std::vector<class Item*> cels;       // CBiome::AddCel dll:1000a0b0
    std::vector<class Item*> sensors;    // CBiome::AddSensor dll:1000a0c0
};
struct DllPtr { uint32_t addr; };    // pointer to DLL global data passed as a constructor argument

class CBiomeCel;
class CWaterTank;
class CWaterFlow;
class CSite;
class CFeature;
class CPlant;
class CPage;
class GameObj;

class Item : public Record {
public:
    Item(u32 ctorAddr, std::string className, std::string name)
        : ctorAddr_(ctorAddr), className_(std::move(className)), name_(std::move(name)) {}
    u32 ctorAddr() const { return ctorAddr_; }
    const std::string& className() const { return className_; }
    // Construction-order name: first constructor argument ("a>b#id" for flows, "file@site" for sound FX).
    const std::string& name() const { return name_; }
    // true once a class has real members and a typed saveSlot/loadSlot (RawItem is false)
    virtual bool typed() const { return false; }
    // The biome cel a site was constructed with (CSite+8, ctor argument 4). dll:1000c270
    virtual Item* siteCel() const;
protected:
    std::string& nameRef() { return name_; }
private:
    u32 ctorAddr_;
    std::string className_, name_;
};

// One constructor argument of a RawItem, exactly as the original builder passed it.
struct RawArg {
    enum Kind { Int, Double, Str, Null, ItemRef, BiomeRef, Ptr };
    Kind kind = Null;
    i32 i = 0;            // Int (the stack dword), Ptr (address)
    double d = 0;
    std::string s;
    Item* item = nullptr;
    const Biome* biome = nullptr;
    static RawArg ofInt(i32 v) { RawArg a; a.kind = Int; a.i = v; return a; }
    static RawArg ofDouble(double v) { RawArg a; a.kind = Double; a.d = v; return a; }
    static RawArg ofStr(const char* v) { RawArg a; if (v) { a.kind = Str; a.s = v; } return a; }
    static RawArg ofItem(Item* v) { RawArg a; if (v) { a.kind = ItemRef; a.item = v; } return a; }
    static RawArg ofBiome(const Biome* v) { RawArg a; if (v) { a.kind = BiomeRef; a.biome = v; } return a; }
    static RawArg ofPtr(DllPtr v) { RawArg a; a.kind = Ptr; a.i = static_cast<i32>(v.addr); return a; }
    std::string text() const;
};

// Placeholder for a class that has not been ported yet: remembers its constructor call and carries its
// save slot as opaque bytes, so the blob round-trips unchanged.
class RawItem final : public Item {
public:
    RawItem(u32 ctorAddr, std::string className, std::string name, std::vector<RawArg> args)
        : Item(ctorAddr, std::move(className), std::move(name)), args_(std::move(args)) {}
    const std::vector<RawArg>& args() const { return args_; }
    const std::vector<u8>& slotBytes() const { return bytes_; }
    void saveSlot(SlotWriter& w) const override;
    void loadSlot(SlotReader& r) override;
    Item* siteCel() const override;
private:
    std::vector<RawArg> args_;
    std::vector<u8> bytes_;
};

class World {
public:
    World();
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // The world owns every item a factory creates, whether or not a list ever receives it.
    Item* adopt(std::unique_ptr<Item> it);
    size_t itemCount() const { return pool_.size(); }
    // Every item the factories created, in creation order (also those no list ever received: pages, paths, messages).
    template <class F> void forEachItem(F&& f) const { for (const auto& p : pool_) f(*p); }

    const std::vector<Item*>& list(Table t) const { return lists_[static_cast<int>(t)]; }

    // CWorld::Add* (the Add functions only append; throw Error where the original logs "pointer NULL")
    void addBiomeCel(Item* it);     // dll:10013aa0
    void addSite(Item* it);         // dll:10013980
    void addWaterTank(Item* it);    // dll:10013f50
    void addAirFlow(Item* it);      // dll:10013dd0
    void addWaterFlow(Item* it);    // dll:10014080
    void addLocalFX(Item* it);      // dll:10014250
    void addFX(Item* it);           // dll:100138a0
    void addPlant(Item* it);        // dll:100143d0
    void addFeature(Item* it);      // dll:10014790
    void addObject(Item* it);       // dll:10014570

    void relate(Rel r, Item* owner, Item* item);
    const std::vector<Item*>& related(Rel r, const Item* owner) const;

    Biome* biome(uint32_t addr);    // creates the entry on first use
    void biomeAddCel(Biome* b, Item* cel);        // dll:1000a0b0
    void biomeAddSensor(Biome* b, Item* sensor);  // dll:1000a0c0

    // The state the original keeps in DLL globals (random generator, lights, feature globals, ...): one per World.
    GameState& state() { return state_; }
    const GameState& state() const { return state_; }
    // What the original passes to Eng_WriteLog (kept in state().log).
    void log(const std::string& text) { state_.log.push_back(text); }

    // The CWorld look-up functions, one implementation each. They return null when the name is unknown, after
    // leaving the "Unrecognised ..." text in state().message / state().log, exactly as the original logs and returns
    // NULL. The require* forms are for callers that dereference the result in the original: they throw
    // biosys::Error instead of crashing.
    CBiomeCel* findBiomeCel(const char* name) const;   // dll:10013ad0 (first letter A F X: never found; B D O R S T: whole name)
    CWaterTank* findWaterTank(const char* name) const; // dll:10013f80
    CSite* findSite(const char* name) const;           // dll:100139b0 (name cut at the first '_', compare ignores case)
    CFeature* findFeature(const char* name) const;     // dll:100147c0 (same cut)
    GameObj* findObject(const char* name) const;       // dll:100145a0
    CPlant* findPlant(const char* name) const;         // FUN_10014400 (name cut at the first '_')
    CPage* findPage(const char* name) const;           // FUN_10013850
    CBiomeCel& requireBiomeCel(const char* name) const;
    CWaterTank& requireWaterTank(const char* name) const;
    CSite& requireSite(const char* name) const;
    CFeature& requireFeature(const char* name) const;

    // CWorld::SetWaterFlowsSource dll:10014200: every water flow with this id takes `tank` as its source (+4).
    void setWaterFlowsSource(uint16_t flowId, CWaterTank* tank);

private:
    void notFound(const std::string& text) const;
    void add(Table t, Item* it, const char* fn);
    std::vector<Item*> lists_[kTableCount];
    std::vector<std::unique_ptr<Item>> pool_;
    std::map<std::pair<int, const Item*>, std::vector<Item*>> rel_;
    std::map<uint32_t, Biome> biomes_;
    mutable GameState state_;
};

// Checked downcast of an item reference (builder statements, typed look-ups): throws biosys::Error when the item
// is null or of another class.
template <class T>
T& as(Item* it) {
    T* p = dynamic_cast<T*>(it);
    if (!p) throw Error(std::string("item '") + (it ? it->name() : std::string("NULL")) + "' is not a " + typeid(T).name());
    return *p;
}
template <class T>
const T& as(const Item* it) {
    const T* p = dynamic_cast<const T*>(it);
    if (!p) throw Error(std::string("item '") + (it ? it->name() : std::string("NULL")) + "' is not a " + typeid(T).name());
    return *p;
}

// Name of one of the nine static CBiome objects by its DLL data address (generated, world_ctors.gen.cpp).
const char* biomeName(uint32_t addr);

// Replays the original start-up code (Event_OnStartup dll:1001d7b0 builders) into an empty world.
// Generated: world_build.gen.cpp.
void buildWorld(World& w);

} // namespace biosys::game
