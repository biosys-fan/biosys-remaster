// Game-object manager (class TCGameObjects, exe:00446e40 and friends): per-object state, drop zones,
// the held object, wearing slots, variations, fruit; every state transition of spec 1.2 / 1.5-1.9 with
// the DLL event order of 1.13. The public query/command methods are the engine side of the API of
// spec 1.12 (they return the API error codes). No SDL.
#pragma once
#include "engine/viewlayer.h"
#include "engine/common.h"
#include "engine/framebuffer.h"
#include "engine/gamelogic.h"
#include "engine/objectfile.h"
#include "engine/pageview.h"
#include "engine/savefile.h"
#include "engine/statusbar.h"
#include "engine/touchinfo.h"
#include <array>
#include <map>

namespace biosys {

enum ObjStatus : int { ST_ZONE = 1, ST_HELD = 2, ST_BAR = 3, ST_REMOVED = 4, ST_WORN = 5, ST_FRUIT = 6 };

// API return codes of spec 1.12.
enum ApiCode : int {
    API_OK = 0, API_NOT_FOUND_HOTSPOT = 4, API_REFUSED = 10, API_NOTHING_DROPPED = 0x11, API_NOT_A_ZONE = 0x11,
    API_UNKNOWN_OBJECT = 0x0d, API_ZONE_RANGE = 0x0e, API_BAD_NAME = 0x0f, API_BAD_PARAM = 0x12, API_INACTIVE = 0x15,
    API_NO_SUCH_GROUP = 0x1a, API_NO_EXTRA_IMAGE = 0x1b, API_FAILED = 0x24, API_HOME_OCCUPIED = 0x25, API_IMAGE_MISSING = 2,
};

struct ObjectInfo {            // API 0x0e info array
    int moveable = 0, wearable = 0, inZone = 0, fruitInZone = 0, held = 0, onBar = 0, removed = 0, worn = 0;
    int weight = 0, usageCount = 0, fruit = 0, zone = -1;
};

struct ZoneInfo {              // API 0x10
    std::string page;
    int hotspotIndex = 0;
    std::string object;        // "" if free
};

class GameObjects {
public:
    GameObjects(const std::string& objectsPath, const TouchInfo& touch, PageView& view, StatusBar& bar);
    ~GameObjects();
    GameObjects(const GameObjects&) = delete;
    GameObjects& operator=(const GameObjects&) = delete;

    void setLogic(GameLogic* l) { logic_ = l; }
    void setAnimPlayer(BlockingAnimPlayer* p) { anim_ = p; }
    void setInputEnabled(bool on) { inputEnabled_ = on; }   // API 0x2e
    bool inputEnabled() const { return inputEnabled_; }
    // DAT_0049a39c: the sim-status window is up (exe:00447bb0 / 00447ca0 / 00447f40 / 00448740 / 004487b0 / 00448ac0 / 00448eb0 / 0044d030:
    // no held-object drawing, no pick-up, stash or use, no S / D / Space).
    void setSimStatusUp(bool on) { simStatusUp_ = on; }
    bool usable() const { return inputEnabled_ && !simStatusUp_; }

    // --- state ---
    void resetAllRemoved();                                  // every object status 4, nothing held/worn, bar empty
    void loadSave(const SaveFile& sf);                       // exe:0044b3b0 (new game = NewGame.sav)
    void saveTo(SaveFile& sf) const;                         // exe:0044aaf0: counts, the object records, the bar order
    void pageLeaving();                                      // surfaces dropped (the page surface is replaced)
    void pageBaked();                                        // page surface (re)built: paint objects, enable zones (spec 1.3)
    // exe:004478a0 mode 0 / mode 1 over all objects (DisplayImage lifts them off the surface while it paints, RedrawPage
    // paints them again without lifting): eraseAllPainted puts every save-under back, repaintErased paints those again.
    void eraseAllPainted();
    void repaintErased();
    void redrawPainted();

    // --- input-driven transitions (spec 1.5-1.6.8) ---
    // Verb-13 hotspot activated (pick-up). Returns false if (page, index) is not a zone.
    bool activateZone(int pageNumber, int hotspotIndex);
    // Verb-14 hotspot activated with an object held ("use on hotspot", 1.6.7). Returns true if it did something.
    bool useOnHotspot(const Hotspot& h);
    void keyStash();                                         // S
    void keyDrop(bool overBar, int barObject);               // D (barObject: object under the pointer on the bar, or -1)
    void keySpace(bool overBar, int barObject, const Hotspot* hovered);   // Space
    void barClick(int obj);                                  // left button release over a bar icon (1.6.8)

    void update(u32 nowMs);                                  // holding-image frame animation (1.6.1)

    // --- back-buffer layers (spec 1.3 draw order) ---
    void drawHeld(Frame& f, int viewTop) const;
    // the image drawHeld would draw (image, view position, colour key); false when it draws nothing
    bool heldLayer(ViewLayer& out) const;
    void drawWearing(Frame& f, int viewTop) const;
    bool heldHidden() const { return heldHidden_; }

    // --- hotspot state for hit testing ---
    HotspotOverride zoneOverride(int pageNumber, int hotspotIndex, const Hotspot& hs) const;

    // --- queries ---
    int objectCount() const;
    int findObject(const std::string& name) const;           // -1 if unknown
    const std::string& objectName(int obj) const;
    int status(int obj) const;
    int zoneOf(int obj) const;                               // current zone, -1
    int homeZoneOf(int obj) const;
    const std::string& variationOf(int obj) const;
    int heldObject() const { return held_; }
    std::vector<int> wornObjects() const;                    // wearing slots in order
    int normalImageOf(int obj) const;                        // current normal image number (1-based), 0 if none
    int zoneCount() const;
    int zoneIndexAt(int pageNumber, int hotspotIndex) const;
    Rect zoneRect(int zone) const;                           // page coordinates; only if the zone's page is current
    bool zoneOccupied(int zone) const;
    bool zoneEnabled(int zone) const;
    int zoneObject(int zone) const;
    std::vector<int> zonesOfPage(int pageNumber) const;
    std::string describe(int obj) const;                     // one text line (dump)
    std::string lastMessage() const { return message_; }     // last error text (what the original logs)

    // --- API commands (spec 1.12); object names compared case-insensitively ---
    int apiObjectCount() const { return objectCount(); }                                  // 0x0c
    int apiObjectName(int index, std::string& name) const;                                // 0x0d
    int apiObjectInfo(const std::string& name, ObjectInfo& info) const;                   // 0x0e
    int apiZoneCount() const { return zoneCount(); }                                      // 0x0f
    int apiZoneInfo(int zone, ZoneInfo& info) const;                                      // 0x10
    int apiZoneOfHotspot(const std::string& hotspot, int& zone) const;                    // 0x25 (current page)
    int apiSetObjectValue(const std::string& name, const std::string& what, int value);   // 0x12 "weight" / "usagecount"
    int apiRemoveObject(const std::string& name);                                         // 0x26
    int apiSetHoldingGroup(const std::string& name, u32 group);                           // 0x30
    int apiPlaceImage(const std::string& srcObject, const std::string& extraName, const std::string& dstObject,
                      const std::string& kind, int holdIndex, u32 group, int x, int y);   // 0x31
    int apiRestoreFruit(const std::string& name);                                         // 0x45
    int apiChangeVariation(const std::string& name, const std::string& variation, u32 group);          // 0x46 immediate
    int apiChangeVariationDeferred(const std::string& name, const std::string& variation, u32 group);  // 0x57
    int apiChangeVariationDeferred(const std::string& variation, u32 group);   // 0x57 for the object of the running event
    int apiPlayUseAnimation(const std::string& tanFile, const std::string& wav, int x, int y, int mode, int delayMs,
                            bool hideHeld, bool removeHeldAfter, const std::string& variation, u32 group);  // 0x47
    int apiDropHeld(const std::string* hotspotName);                                      // 0x48 (nullptr = nearest zone)
    int apiReturnObject(const std::string& name);                                         // 0x52
    // port, tests only (script command `hold`): the object goes into the hand as if it had been on the bar and clicked there (the
    // game gets the same events, 0x0c and 3); from a zone or from nowhere it is put on the bar first without events
    int debugHold(const std::string& name);

private:
    struct Rec {               // the record fields a variation overwrites (spec 1.7)
        NormalImage normal[3];
        u32 icon = 0, wornIcon = 0;
        std::vector<HoldImage> holding;
        u32 holdDelayMs = 0;
        HoldImage wearing;
        std::string shortName;
    };
    struct Painted {           // area of the page surface covered by a painted picture + its save-under
        bool on = false;
        Rect r;
        std::vector<u8> under;
    };
    struct Obj {
        int status = ST_REMOVED;
        int zone = -1, home = -1, zoneType = 1;
        int normalImage = 0, normalEntry = 0;   // image number (1-based) and which of the 3 normal entries it came from
        std::string variation;
        Rec rec;
        int holdGroup = 0, holdGroupCount = 0;
        int weight = 0, usage = -1;
        Painted painted;       // normal image / filtered ornament in its zone
        Painted fruit;         // page background saved under the fruit ornament (spec 1.9)
    };
    struct Zone {
        int page = 0, hotspot = 0, type = 1;
        std::string pageName, hotName;
        bool occupied = false, fruit = false;   // spec 1.1 per-zone flag bits 0 and 1
        bool enabled = false;
        bool hasHit = false;
        int hl = 0, ht = 0, hr = 0, hb = 0;     // dynamic hit rectangle (inclusive)
    };
    struct DeferredSlot { int obj = -1; u32 group = 0; std::string name; };

    const ObjectDef& def(int obj) const;
    bool validObject(int obj) const { return obj >= 0 && obj < objectCount(); }
    const Image8& img(int obj, int imageNo) const;
    void initRec(int obj);
    int chooseNormal(int obj, int type, int& typeOut) const;
    bool handLocked(int obj) const { return validObject(obj) && def(obj).unk1a8 == 1; }
    int keyIndex(u32 flag, i32 transp) const;                // palette index of a record colour key, -1 opaque

    // zones
    Rect zoneRectOf(const Zone& z) const;
    static std::string filterOf(const Hotspot& h);
    static bool filterMatches(const std::string& filter, const std::string& name, bool emptyAccepts);
    bool neverActive(const Zone& z) const;
    void enableZone(int zone);
    void disableZone(int zone);
    bool zoneOnCurrentPage(int zone) const;
    int nearestFreeZone(int obj) const;

    // painting
    void paintObject(int obj, bool eraseFirst = true);
    void eraseObject(int obj);
    void paintFruitOrnaments(int onlyObj);                   // spec 1.9: event 29 per fruit ornament of the page
    void restoreFruitBackground(int obj);

    // transitions
    void fire(int obj, int code, const std::string& extra = std::string());
    void clearDeferred();
    void flushDeferred();
    void playAnim(int obj, int code, bool hideHeld, bool priority);
    const ObjectAnim* findAnim(int obj, int code) const;
    void setupHolding(int obj);
    void pickUpFruit(int obj, int zone);
    bool stashHeld();
    void barToHand(int obj);
    void useFruit(int obj);
    void selfUse();
    bool wear(int obj);
    void takeOff(int obj);
    void sortWearing();
    void dropHeld(const std::string* hotspotName, bool fromKey, bool* dropped);
    void dropHeldInto(int zone);
    void dropFromBar(int obj, bool fromKey);
    void placeInZone(int obj, int zone);
    void insertOnBar(int obj, bool worn);
    bool applyVariation(int obj, const std::string& name, u32 group, bool dataOnly);
    void removeObject(int obj);
    void refreshHeldImage();

    PageView& view_;
    StatusBar& bar_;
    std::unique_ptr<ObjectFile> file_;
    std::vector<Obj> objs_;
    std::vector<Zone> zones_;
    std::vector<int> zoneObj_;
    std::map<std::pair<int, int>, int> zoneByHot_;
    std::map<std::string, int> zoneByName_;
    std::map<int, std::vector<int>> zonesByPage_;
    mutable std::map<uint64_t, Image8> cache_;               // decoded object images (lazily loaded)
    std::map<uint64_t, Image8> overrides_;                   // images patched by API 0x31
    GameLogic* logic_ = nullptr;
    BlockingAnimPlayer* anim_ = nullptr;
    bool inputEnabled_ = true;
    bool simStatusUp_ = false;
    u32 now_ = 0;
    int held_ = -1;
    size_t heldIdx_ = 0;
    Image8 heldImg_;
    u32 heldTick_ = 0;
    bool heldHidden_ = false;
    std::vector<int> wear_;                                  // wearing slots (max 3), sorted
    std::vector<int> nesting_;                               // objects inside OnGameObject (max 5)
    std::array<DeferredSlot, 4> deferred_;
    std::string message_;
    std::vector<int> erased_;                // objects lifted off the surface by eraseAllPainted
};

} // namespace biosys
