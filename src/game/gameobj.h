// CGameObj / CObject: the common base of the 423 saved game objects (table "objects" of the save blob).
// Layout of the common block (object offsets), set by FUN_10028e90 (dll:10028e90) in every object constructor:
//   +0x04 name[12]   +0x10 name string id   +0x14 flags   +0x18 pick-up event   +0x1c (never written)
//   +0x20 weight     +0x28 time stamp       +0x2c in scene   +0x30 held   +0x34 variant name[12]   +0x40 class members
// Saved by FUN_10028d00, loaded by FUN_10028d70 (31 bytes at slot+0); the common slot block of every item class.
// The original has no class between CGameObj and the item classes that matters for the data: the vtable chain
// 10094764 (CGameObj) -> 10091b70 (CObject) is written by FUN_10028b90 in every constructor.
//
// The behaviour: the virtual methods below are the slots of the object vtable (0x98 bytes, dll:10091b70 for the default
// entries); the slot offset and the address of the default function are in the comment of each. Every handler takes the
// player (`Self`, DAT_100c1d38: s.world() the world, s.host() the shell) because the original reaches it through
// DLL globals. Return values are the original's (almost always 1).
#pragma once
#include "game/world.h"
#include <array>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace biosys::game {

class Self;
class CFeature;

double dwordsToDouble(u32 hi, u32 lo = 0);

enum class PokeOp { Set, Or };

// Name / value text of every member, in object order (the oracle dump: names are the C++ member names, doubles in the
// shortest round-trip text of Python's repr).
using ObjMembers = std::vector<std::pair<std::string, std::string>>;

class GameObj : public Item {
public:
    // FUN_10028e90 dll:10028e90 (GameObj part of every object constructor; FUN_10028b90 only sets vtables)
    GameObj(u32 ctorAddr, const char* className, const char* name, i32 nameStringId);
    bool typed() const override { return true; }

    // CGameObj::SafeSetVariant dll:10029490, the part after the engine call succeeded: copies the NUL-terminated
    // string into +0x34 byte by byte, not touching the bytes behind it.
    void setVariant(const char* variant);
    std::string variant() const;                       // text of the +0x34 buffer

    // FUN_10029440 dll:10029440: Eng_ChangeGameObjectVariation(name, variant, mode); on success the variant name is
    // copied to +0x34. Returns the engine's answer (1 / 0).
    int changeVariation(Self& s, const char* variant, int mode);
    // CGameObj::SafeSetVariant dll:10029490: Eng_ChangeGameObjectVariation1(variant, mode) (the object of the event in
    // progress); on failure the error is logged and 0 returned, on success the variant name is copied.
    int safeSetVariant(Self& s, const char* variant, int mode);

    // The stores the original builders make into a freshly built object (objectPoke calls in world_build.gen.cpp):
    // `*(T*)(obj + offset) = value` (Set) and `*(uint*)(obj + offset) |= value` (Or). Throws biosys::Error for an
    // offset the class does not have.
    virtual void poke(u32 offset, unsigned bytes, u32 value, PokeOp op);
    // The member poke writes at that offset (tests of the builder statements); throws for an offset that holds none.
    virtual u32 peek(u32 offset, unsigned bytes) const;

    // --- members, in object order -----------------------------------------------------------------------------
    // +0x04 name[12]: Item::name()
    i32 nameStringId() const { return m_dwNameStringId; }
    u32 flags() const { return m_dwFlags; }
    u32 pickupEvent() const { return m_dwPickupEvent; }
    double weight() const { return m_dWeight; }
    i32 timeStamp() const { return m_dwTimeStamp; }
    bool inScene() const { return m_bInScene != 0; }
    bool held() const { return m_bHeld != 0; }

    void saveSlot(SlotWriter& w) const override;       // FUN_10028d00 (common block only)
    void loadSlot(SlotReader& r) override;             // FUN_10028d70 + end of slot

    // Port only (tests): every typed member as name / value text, the common block first.
    virtual void dumpMembers(ObjMembers& out) const;

    // ---- the object vtable ----------------------------------------------------------------------------------------
    // +0x20 FUN_10028de0 (empty): after the game was loaded (CGame::Load calls it for every object)
    virtual void afterLoad(Self& s) {}
    // +0x24 FUN_10028df0: Eng_WriteLog of the members (the lines it logs)
    virtual void dump(std::vector<std::string>& lines) const;
    // +0x28 FUN_100291c0: the message of the object's name (and the state text `p2` the classes add)
    virtual void describe(Self& s);
    // +0x2c FUN_10028ef0: the object is used (event 4, "used"; CFood::Eat ...)
    virtual int use(Self& s) { return 1; }
    // +0x30 FUN_10028f00 (returns 1; nothing overrides it)
    virtual int slot30(Self& s) { return 1; }
    // +0x34 FUN_10029120: stashed on the bar (event 2)
    virtual int onStashed(Self& s);
    // +0x38 FUN_10029130: event 0xc, before bar -> hand
    virtual int onBarToHand(Self& s);
    // +0x3c FUN_10029150: bar -> hand finished (event 3)
    virtual int onTaken(Self& s) { return 1; }
    // +0x40 FUN_10028ff0: event 0xa, before the holding image is set during pick-up
    virtual int onHoldingImage(Self& s);
    // +0x44 CGameObj::PickUp dll:10029000: picked up from the world (event 1)
    virtual int pickUp(Self& s);
    // +0x48 FUN_100290a0: dropped in a zone (event 6); `hotspot` is the upper-cased zone hotspot (most classes ignore it)
    virtual int onDropped(Self& s, const std::string& hotspot);
    // +0x4c FUN_10029160: worn (event 8)
    virtual int onWorn(Self& s);
    // +0x50 FUN_10029180: taken off (event 9)
    virtual int onUnworn(Self& s) { return 1; }
    // +0x54 FUN_10028f10: used on a verb-14 hotspot (event 7)
    virtual int onUsedOnHotspot(Self& s, const std::string& hotspot);
    // +0x58 FUN_10029230: key shortcut filter (Event_OnKey with the object in hand): `*allow` = 0 refuses the key.
    // Keys: 0x20 use, 0x44 'D' drop, 0x53 'S' stash. Returns 0 (1 in CBabySynth / CPaperItem)
    virtual int onKey(Self& s, int key, u32* allow);
    // +0x5c FUN_10029410 (returns 0): the compass sector changed (CCompass computes it, CFishingRod listens)
    virtual int onSector(Self& s, int sector) { return 0; }
    // +0x60 FUN_10029400 (returns 0): CConsumable::Fill(code, amount); amount -1 = full
    virtual int fill(Self& s, u16 code, int amount) { return 0; }
    // +0x64 FUN_100293f0 (returns 0): refill from a container (COxyMask / COxyCylinder::Refill)
    virtual int refill(Self& s, double* source, i16 code) { return 0; }
    // +0x68 FUN_10029210 (returns 0): per-tick update (CWorld::UpdateNextObject)
    virtual int update(Self& s, i32 now) { return 0; }
    // +0x6c FUN_10029220 (returns 0): redraw of the hand-held / worn display (full = 1 after a page redraw)
    virtual int redraw(Self& s, int full) { return 0; }
    // +0x70 FUN_10029420 (returns 0): hotspot "B..." clicked with key 'X' while the object is in hand (CAlarmClock buttons)
    virtual int onButtonHotspot(Self& s, const std::string& hotspot) { return 0; }
    // +0x74 FUN_10029430 (returns 0): right click with the object in hand (CPaperItem opens it)
    virtual int onRightClick(Self& s) { return 0; }
    // +0x78 FUN_10029190: becomes the object in hand (DAT_100c1fa0 = this), then Describe
    virtual int setHolding(Self& s);
    // +0x7c FUN_100291b0: leaves the hand (DAT_100c1fa0 = NULL)
    virtual int clearHolding(Self& s);
    // +0x80 FUN_100293b0 .. +0x90 FUN_10029560: page hooks of the object in hand / worn (bg_page.cpp): +0x80 on the page
    // being entered, +0x84 on leaving it, +0x88 when the jump is complete (Event_OnEnterPage), +0x8c unused,
    // +0x90 a drop zone hotspot was clicked while the hand holds the object
    virtual void onPageEnter(Self& s, const std::string& page) {}
    virtual void onPageLeave(Self& s, const std::string& page) {}
    virtual void onPageEntered(Self& s, const std::string& page) {}
    virtual void slot8c(Self& s, const std::string& arg) {}
    virtual void onDropZoneClicked(Self& s, const std::string& hotspot) {}
    // +0x94 FUN_10029510: takes the object out of the scene (Eng_RemoveGameObject). `s` is null while the world is
    // built (the constructors call it; nothing is held then and the engine call is assumed to succeed).
    virtual void removeFromScene(Self* s = nullptr);

protected:
    void saveCommon(SlotWriter& w) const;              // FUN_10028d00
    void loadCommon(SlotReader& r);                    // FUN_10028d70
    void dumpBase(std::vector<std::string>& lines) const;   // FUN_10028df0
    void commonMembers(ObjMembers& out) const;
    static void putD(ObjMembers& out, const char* name, double v);
    static void putI(ObjMembers& out, const char* name, long long v);
    static void putX(ObjMembers& out, const char* name, u32 v);
    static void putS(ObjMembers& out, const char* name, const std::string& v);
    static void putP(ObjMembers& out, const char* name, const Item* v);   // the item's name or ""

    // The members the constructors set; derived classes assign them directly.
    i32 m_dwNameStringId = 0;                          // +0x10 id of the display name string
    u32 m_dwFlags = 0;                                 // +0x14 (bit 0x1000000 / 0x80 of +0x17: see CGameObj::PickUp)
    u32 m_dwPickupEvent = 0;                           // +0x18 CGame::TriggerEvent argument when picked up (0 = none)
    u32 m_dwUnk1c = 0;                                 // +0x1c never written by any constructor or save
    double m_dWeight = 0;                              // +0x20
    i32 m_dwTimeStamp = 0x7080;                        // +0x28 game time (28800 = 08:00)
    i32 m_bInScene = 1;                                // +0x2c cleared by FUN_10029510; saved as unk_2c
    i32 m_bHeld = 0;                                   // +0x30 set by CGameObj::PickUp; saved as unk_30
    std::array<u8, 12> m_szVariant{};                  // +0x34 variant/model name; 11 bytes saved (the 12th is padding)
};

// Applies a builder store to an item of the objects table (see GameObj::poke); throws if the item is no GameObj.
void objectPoke(Item* item, u32 offset, unsigned bytes, u32 value, PokeOp op);
void objectPokeString(Item* item, u32 offset, const char* s);   // strcpy into the object at +0x34 (variant name)
void objectVirtualCall(Item* item, u32 vtableOffset);           // vtable +0x94: removeFromScene (no player yet)

// Event_OnGameObject dll:1001e380 (the engine's object event 11): finds the object and calls the slot of the event code
// 1..0x10 (engine_runtime.md 1.13). `extra` is the hotspot name of events 6 and 7.
void dispatchObjectEvent(World& w, Self& s, const std::string& name, int code, const std::string& extra);

namespace cworld {
// CWorld::UpdateAllObjects dll:100146a0: every object's update(now) in table order (the cursor is GameState::objectCursor)
bool updateAllObjects(World& w, Self& s, i32 now);
// CWorld::UpdateNextObject dll:10014650: updates one object; true while more remain
bool updateNextObject(World& w, Self& s, i32 now);
} // namespace cworld

} // namespace biosys::game
