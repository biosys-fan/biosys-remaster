#include "engine/gameobjects.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace biosys {

namespace {

std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return s;
}

std::string zoneKey(const std::string& page, const std::string& hotspot) { return lower(page) + '\x01' + lower(hotspot); }

int wrapDelta(int d, int period) {   // d into [-period/2, period/2)
    d %= period;
    if (d < -period / 2) d += period;
    if (d >= period / 2) d -= period;
    return d;
}

uint64_t imgKey(int obj, int no) { return (static_cast<uint64_t>(obj) << 16) | static_cast<uint64_t>(no); }

} // namespace

// ---------------------------------------------------------------------------------------------
// construction / state

GameObjects::GameObjects(const std::string& objectsPath, const TouchInfo& touch, PageView& view, StatusBar& bar)
    : view_(view), bar_(bar), file_(new ObjectFile(objectsPath)) {
    const int n = file_->objectCount();
    objs_.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; i++) {
        const ObjectDef& d = file_->object(i);
        initRec(i);
        objs_[static_cast<size_t>(i)].weight = static_cast<int>(d.unk198);
        objs_[static_cast<size_t>(i)].usage = d.unk19c;
    }
    zones_.resize(touch.dropZones.size());
    zoneObj_.assign(zones_.size(), -1);
    for (size_t z = 0; z < zones_.size(); z++) {
        const DropZone& dz = touch.dropZones[z];
        zones_[z].page = static_cast<int>(dz.page);
        zones_[z].hotspot = static_cast<int>(dz.hotspot);
        zones_[z].type = static_cast<int>(dz.type);
        zones_[z].pageName = dz.pageName;
        zones_[z].hotName = dz.hotspotName;
        zoneByHot_[{zones_[z].page, zones_[z].hotspot}] = static_cast<int>(z);
        zoneByName_[zoneKey(dz.pageName, dz.hotspotName)] = static_cast<int>(z);
        zonesByPage_[zones_[z].page].push_back(static_cast<int>(z));
    }
    for (int i = 0; i < n; i++) {
        const ObjectDef& d = file_->object(i);
        if (d.fruit == 1 && d.initZone >= 0 && static_cast<size_t>(d.initZone) < zones_.size())
            zones_[static_cast<size_t>(d.initZone)].fruit = true;
    }
}

GameObjects::~GameObjects() = default;

const ObjectDef& GameObjects::def(int obj) const { return file_->object(obj); }
int GameObjects::objectCount() const { return file_->objectCount(); }
int GameObjects::findObject(const std::string& name) const { return file_->findObject(name); }
const std::string& GameObjects::objectName(int obj) const { return def(obj).name; }
int GameObjects::status(int obj) const { return objs_.at(static_cast<size_t>(obj)).status; }
int GameObjects::zoneOf(int obj) const { return objs_.at(static_cast<size_t>(obj)).zone; }
int GameObjects::homeZoneOf(int obj) const { return objs_.at(static_cast<size_t>(obj)).home; }
const std::string& GameObjects::variationOf(int obj) const { return objs_.at(static_cast<size_t>(obj)).variation; }
int GameObjects::normalImageOf(int obj) const { return objs_.at(static_cast<size_t>(obj)).normalImage; }
int GameObjects::zoneCount() const { return static_cast<int>(zones_.size()); }
bool GameObjects::zoneOccupied(int z) const { return zones_.at(static_cast<size_t>(z)).occupied; }
bool GameObjects::zoneEnabled(int z) const { return zones_.at(static_cast<size_t>(z)).enabled; }
int GameObjects::zoneObject(int z) const { return zoneObj_.at(static_cast<size_t>(z)); }
std::vector<int> GameObjects::wornObjects() const { return wear_; }

int GameObjects::zoneIndexAt(int pageNumber, int hotspotIndex) const {
    auto it = zoneByHot_.find({pageNumber, hotspotIndex});
    return it == zoneByHot_.end() ? -1 : it->second;
}

std::vector<int> GameObjects::zonesOfPage(int pageNumber) const {
    auto it = zonesByPage_.find(pageNumber);
    return it == zonesByPage_.end() ? std::vector<int>() : it->second;
}

// Record fields as they are before any variation is applied.
void GameObjects::initRec(int obj) {
    const ObjectDef& d = def(obj);
    Obj& o = objs_[static_cast<size_t>(obj)];
    for (int k = 0; k < 3; k++) o.rec.normal[k] = d.normal[k];
    o.rec.icon = d.icon;
    o.rec.wornIcon = d.unk44;
    o.rec.holding = d.holding;
    o.rec.holdDelayMs = d.holdDelayMs;
    o.rec.wearing = d.wearing;
    o.rec.shortName = d.shortName;
    // holding group = lowest group number present (spec 1.1)
    bool any = false;
    u32 lo = 0;
    for (const HoldImage& h : o.rec.holding)
        if (h.image && (!any || h.group < lo)) { lo = h.group; any = true; }
    o.holdGroup = any ? static_cast<int>(lo) : -1;     // NewGame.sav: group 0xffffffff, count 0 for an object without holding images
    o.holdGroupCount = 0;
    for (const HoldImage& h : o.rec.holding)
        if (h.image && h.group == lo) o.holdGroupCount++;
}

void GameObjects::resetAllRemoved() {
    held_ = -1;
    heldImg_ = Image8();
    wear_.clear();
    bar_.clear();
    cache_.clear();
    overrides_.clear();
    clearDeferred();
    for (size_t i = 0; i < objs_.size(); i++) {
        Obj fresh;
        objs_[i] = fresh;
        initRec(static_cast<int>(i));
        objs_[i].weight = static_cast<int>(def(static_cast<int>(i)).unk198);
        objs_[i].usage = def(static_cast<int>(i)).unk19c;
    }
    for (size_t z = 0; z < zones_.size(); z++) {
        zones_[z].occupied = false;
        zones_[z].enabled = false;
        zones_[z].hasHit = false;
        zoneObj_[z] = -1;
    }
}

// exe:0044b3b0: apply a saved game's object section (new game = NewGame.sav, spec 1.11).
void GameObjects::loadSave(const SaveFile& sf) {
    if (!sf.hasObjectsSection) throw Error("save file has no game-objects section");
    if (static_cast<int>(sf.objectCount) != objectCount() || sf.objects.size() != sf.objectCount ||
        sf.zoneCount != zones_.size())
        throw Error("Invalid game objects data");
    resetAllRemoved();
    auto zoneByNames = [&](const std::string& page, const std::string& hot) -> int {
        if (page.empty() && hot.empty()) return -1;
        auto it = zoneByName_.find(zoneKey(page, hot));
        if (it == zoneByName_.end()) throw Error("saved game: unknown zone " + page + "/" + hot);
        return it->second;
    };
    for (const SavedObject& so : sf.objects) {
        int i = findObject(so.name);
        if (i < 0) throw Error("saved game: unknown object " + so.name);
        const ObjectDef& d = def(i);
        Obj& o = objs_[static_cast<size_t>(i)];
        if (d.nVariations > 0) {
            // spec: the name is present iff the object has variations; tolerate an empty name (initial variation)
            std::string v = so.variation.empty() ? d.initVariation : so.variation;
            if (!applyVariation(i, v, 0xfffffffeu, true)) throw Error("saved game: bad variation " + v + " of " + so.name);
        }
        o.status = static_cast<int>(so.status);
        o.weight = static_cast<int>(so.obj198);
        o.usage = so.obj19c;
        o.home = zoneByNames(so.homeZonePage, so.homeZoneHotspot);
        if (so.status == ST_ZONE || so.status == ST_FRUIT) {
            int z = zoneByNames(so.curZonePage, so.curZoneHotspot);
            if (z < 0) throw Error("saved game: object " + so.name + " lies nowhere");
            zones_[static_cast<size_t>(z)].occupied = true;
            zoneObj_[static_cast<size_t>(z)] = i;
            o.zone = z;
            int tt;
            o.normalEntry = chooseNormal(i, zones_[static_cast<size_t>(z)].type, tt);
            o.zoneType = tt;
            o.normalImage = static_cast<int>(o.rec.normal[o.normalEntry].image);
            if (d.fruit == 1) zones_[static_cast<size_t>(z)].fruit = true;
        }
        if (d.fruit == 1 && o.home >= 0) zones_[static_cast<size_t>(o.home)].fruit = true;
        bool matched = false;
        for (const HoldImage& h : o.rec.holding)
            if (h.image && h.group == so.holdGroup) {
                o.holdGroup = static_cast<int>(so.holdGroup);
                o.holdGroupCount = 0;
                for (const HoldImage& h2 : o.rec.holding) if (h2.image && h2.group == so.holdGroup) o.holdGroupCount++;
                matched = true;
                break;
            }
        if (!matched) {       // exe:0044b3b0: the record's group and count are taken as they are (-1 / 0 without holding images)
            o.holdGroup = static_cast<int>(so.holdGroup);
            o.holdGroupCount = static_cast<int>(so.holdGroupCount);
        }
    }
    for (size_t i = 0; i < objs_.size(); i++) {
        if (objs_[i].status == ST_HELD) { setupHolding(static_cast<int>(i)); held_ = static_cast<int>(i); }
        if (objs_[i].status == ST_WORN) {
            if (!def(static_cast<int>(i)).wearable) throw Error("saved game: object worn but not wearable");
            wear_.push_back(static_cast<int>(i));
        }
    }
    sortWearing();
    for (const std::string& name : sf.statusBar) {
        int i = findObject(name);
        if (i < 0) throw Error("saved game: unknown object on the bar " + name);
        int st = objs_[static_cast<size_t>(i)].status;
        if (st != ST_BAR && st != ST_WORN)
            throw Error("object " + name + " is marked in the saved game as on the status bar but has another status");
        insertOnBar(i, st == ST_WORN);
    }
}

// exe:0044aaf0: nObjects, nMoveable, nZones, nOnBar, one 0x58-byte record per object in OBJECTS.DAT order (the zone names only
// for status 1 and 6, the home zone always), then the names of the bar's objects in bar order. Worn objects are status 5 and on
// the bar; the held object is status 2.
void GameObjects::saveTo(SaveFile& sf) const {
    sf.hasObjectsSection = true;
    sf.objectCount = static_cast<u32>(objs_.size());
    sf.moveableCount = 0;
    sf.zoneCount = static_cast<u32>(zones_.size());
    sf.objects.clear();
    sf.statusBar.clear();
    for (size_t i = 0; i < objs_.size(); i++) {
        const ObjectDef& d = def(static_cast<int>(i));
        const Obj& o = objs_[i];
        if (d.moveable == 1) sf.moveableCount++;
        SavedObject so;
        so.name = d.name;
        so.status = static_cast<u32>(o.status);
        so.obj198 = static_cast<u32>(o.weight);
        so.obj19c = o.usage;
        if (d.moveable == 1) {
            if ((o.status == ST_ZONE || o.status == ST_FRUIT) && o.zone >= 0) {
                so.curZonePage = zones_[static_cast<size_t>(o.zone)].pageName;
                so.curZoneHotspot = zones_[static_cast<size_t>(o.zone)].hotName;
            }
            if (o.home >= 0) {
                so.homeZonePage = zones_[static_cast<size_t>(o.home)].pageName;
                so.homeZoneHotspot = zones_[static_cast<size_t>(o.home)].hotName;
            }
            so.holdGroup = static_cast<u32>(o.holdGroup);
            so.holdGroupCount = static_cast<u32>(o.holdGroupCount);
            so.variation = o.variation;
        }
        sf.objects.push_back(so);
    }
    for (int obj : bar_.objects()) sf.statusBar.push_back(def(obj).name);
}

// ---------------------------------------------------------------------------------------------
// images

const Image8& GameObjects::img(int obj, int no) const {
    uint64_t k = imgKey(obj, no);
    auto ov = overrides_.find(k);
    if (ov != overrides_.end()) return ov->second;
    auto it = cache_.find(k);
    if (it != cache_.end()) return it->second;
    if (cache_.size() > 128) cache_.clear();
    return cache_.emplace(k, file_->loadImage(obj, no)).first->second;
}

int GameObjects::keyIndex(u32 flag, i32 transp) const {
    if (!flag) return -1;
    Color c;
    c.b = static_cast<u8>(transp & 0xff);
    c.g = static_cast<u8>((transp >> 8) & 0xff);
    c.r = static_cast<u8>((transp >> 16) & 0xff);
    return view_.palette().find(c);   // a colour absent from the palette: drawn opaque (spec 1.3)
}

// Normal image of the zone type (spec 1.3): entry t, else t+1, t+2 cyclically. Returns the entry index.
int GameObjects::chooseNormal(int obj, int type, int& typeOut) const {
    const Obj& o = objs_[static_cast<size_t>(obj)];
    int t = std::clamp(type, 1, 3);
    typeOut = t;
    for (int i = 0; i < 3; i++) {
        int e = (t - 1 + i) % 3;
        if (o.rec.normal[e].image) return e;
    }
    throw Error("failed to find a normal image for object " + def(obj).name);
}

// ---------------------------------------------------------------------------------------------
// zones and painting

Rect GameObjects::zoneRectOf(const Zone& z) const {
    const Hotspot& h = view_.page().hotspots.at(static_cast<size_t>(z.hotspot));
    return {h.left, h.top, h.right - h.left + 1, h.bottom - h.top + 1};
}

bool GameObjects::zoneOnCurrentPage(int zone) const {
    return view_.pageNumber() != 0 && zones_[static_cast<size_t>(zone)].page == view_.pageNumber();
}

Rect GameObjects::zoneRect(int zone) const { return zoneRectOf(zones_.at(static_cast<size_t>(zone))); }

// Filter string of a verb-13 (action bytes from a5) or verb-14 (from a1) hotspot (spec 1.4).
std::string GameObjects::filterOf(const Hotspot& h) {
    int first = h.action.verb == VERB_ALLOWOBJECTDROP ? 4 : 0;
    char buf[13];
    for (int i = 0; i < 3; i++) {
        u32 v = static_cast<u32>(h.action.arg[first + i]);
        for (int b = 0; b < 4; b++) buf[i * 4 + b] = static_cast<char>((v >> (8 * b)) & 0xff);
    }
    buf[12] = 0;
    return buf;
}

bool GameObjects::filterMatches(const std::string& filter, const std::string& name, bool emptyAccepts) {
    if (filter.empty()) return emptyAccepts;
    if (filter.back() == '*') {
        size_t n = filter.size() - 1;
        return name.size() >= n && iequals(name.substr(0, n), filter.substr(0, n));
    }
    return iequals(filter, name);
}

// Verb-13 a2 bit0 = "never active": the hotspot is re-disabled by every enable (exe:004029d0).
bool GameObjects::neverActive(const Zone& z) const {
    const Hotspot& h = view_.page().hotspots.at(static_cast<size_t>(z.hotspot));
    return h.action.arg[1] & 1;
}

void GameObjects::enableZone(int zone) {
    Zone& z = zones_[static_cast<size_t>(zone)];
    z.enabled = zoneOnCurrentPage(zone) ? !neverActive(z) : true;
}

void GameObjects::disableZone(int zone) {
    Zone& z = zones_[static_cast<size_t>(zone)];
    z.enabled = false;
    z.hasHit = false;
}

HotspotOverride GameObjects::zoneOverride(int pageNumber, int hotspotIndex, const Hotspot& hs) const {
    HotspotOverride o;
    int z = zoneIndexAt(pageNumber, hotspotIndex);
    if (z < 0) {
        o.disabled = hs.flags & 1;
        return o;
    }
    const Zone& zz = zones_[static_cast<size_t>(z)];
    o.disabled = !zz.enabled;
    if (zz.hasHit) { o.hasRect = true; o.l = zz.hl; o.t = zz.ht; o.r = zz.hr; o.b = zz.hb; }
    return o;
}

void GameObjects::eraseObject(int obj) {
    Obj& o = objs_[static_cast<size_t>(obj)];
    if (o.painted.on) copyIn(view_.surface(), o.painted.r, o.painted.under);
    o.painted.on = false;
}

// exe:004478a0 mode 1 (spec 1.3): paint the object into its zone on the page surface with save-under,
// then set the zone's hit rectangle.
void GameObjects::paintObject(int obj, bool eraseFirst) {
    Obj& o = objs_[static_cast<size_t>(obj)];
    if (eraseFirst) eraseObject(obj);
    Zone& z = zones_[static_cast<size_t>(o.zone)];
    Surface& surf = view_.surface();
    const Hotspot& hs = view_.page().hotspots.at(static_cast<size_t>(z.hotspot));
    Rect zr = zoneRectOf(z);

    // Zone with a name filter matching the object and an object short name (spec 1.4): the page
    // ornament of that name is the picture, at the ornament's own position.
    int orn = -1;
    if (!o.rec.shortName.empty()) {
        std::string f = filterOf(hs);
        if (!f.empty() && filterMatches(f, def(obj).name, false)) orn = view_.findOrnament(o.rec.shortName);
    }
    Image8 pic;
    int key, dx, dy, sx = 0, sy = 0, w, h;
    if (orn >= 0) {
        const Ornament& oo = view_.page().ornaments[static_cast<size_t>(orn)];
        pic = view_.ornamentPixels(orn);
        key = view_.ornamentKey(orn);
        dx = oo.x; dy = oo.y; w = pic.w; h = pic.h;
    } else {
        pic = img(obj, o.normalImage);
        key = keyIndex(o.rec.normal[o.normalEntry].flag, o.rec.normal[o.normalEntry].transp);
        w = pic.w; h = pic.h;
        if (w <= zr.w) { dx = zr.x + (zr.w - w) / 2; }                 // narrower: centred
        else { sx = (w - zr.w) / 2; dx = zr.x; w = zr.w; }             // wider: cropped symmetrically
        if (h <= zr.h) { dy = zr.y + zr.h - h; }                       // shorter: bottom-aligned
        else { sy = h - zr.h; dy = zr.y; h = zr.h; }                   // taller: top rows cropped
    }
    Rect r = clipRect({dx, dy, w, h}, surf.w, surf.h);
    o.painted.r = r;
    o.painted.under = copyOut(surf, r);
    o.painted.on = true;
    blitRegion(surf, pic, dx, dy, sx, sy, w, h, key);

    // hit rectangle: drawn rectangle widened by 4 px each side, clipped to the zone (inclusive coordinates)
    z.hl = std::max(r.x - 4, zr.x);
    z.ht = std::max(r.y - 4, zr.y);
    z.hr = std::min(r.x + r.w - 1 + 4, zr.x + zr.w - 1);
    z.hb = std::min(r.y + r.h - 1 + 4, zr.y + zr.h - 1);
    z.hasHit = z.hl <= z.hr && z.ht <= z.hb;
}

// exe:0044d820: for every page ornament flagged "fruit" ask the game logic which fruit object it shows
// (event 29); a fruit lying in its zone gets the ornament painted over the page, the background under it
// is kept so taking the fruit can restore it.
void GameObjects::paintFruitOrnaments(int onlyObj) {
    if (!logic_ || view_.pageNumber() == 0) return;
    const Page& p = view_.page();
    for (size_t oi = 0; oi < p.ornaments.size(); oi++) {
        const Ornament& orn = p.ornaments[oi];
        if (!orn.fruit() || orn.type != 1) continue;
        std::string fruitName;
        if (!logic_->onGetNameOfFruitObjectFromPageImageName(orn.name, fruitName)) continue;   // event 29
        std::optional<std::string> nm = fruitName;
        int obj = findObject(*nm);
        if (obj < 0) { message_ = "Unknown object " + *nm + " returned by the Control Program"; continue; }
        if (onlyObj >= 0 && obj != onlyObj) continue;
        Obj& o = objs_[static_cast<size_t>(obj)];
        if (o.status != ST_FRUIT || o.zone < 0 || !zoneOnCurrentPage(o.zone)) {
            message_ = "Object " + *nm + " returned by the Control Program is not a fruit lying in a zone";
            continue;
        }
        if (o.fruit.on) copyIn(view_.surface(), o.fruit.r, o.fruit.under);
        const Image8& pic = view_.ornamentPixels(static_cast<int>(oi));
        Surface& surf = view_.surface();
        o.fruit.r = clipRect({orn.x, orn.y, pic.w, pic.h}, surf.w, surf.h);
        o.fruit.under = copyOut(surf, o.fruit.r);
        o.fruit.on = true;
        blitRegion(surf, pic, orn.x, orn.y, 0, 0, pic.w, pic.h, view_.ornamentKey(static_cast<int>(oi)));
    }
}

void GameObjects::restoreFruitBackground(int obj) {
    Obj& o = objs_[static_cast<size_t>(obj)];
    if (o.fruit.on) copyIn(view_.surface(), o.fruit.r, o.fruit.under);
    o.fruit.on = false;
}

void GameObjects::eraseAllPainted() {
    erased_.clear();
    for (size_t i = 0; i < objs_.size(); i++)
        if (objs_[i].painted.on) {
            eraseObject(static_cast<int>(i));
            erased_.push_back(static_cast<int>(i));
        }
}

void GameObjects::repaintErased() {
    std::vector<int> list;
    list.swap(erased_);
    for (int i : list) {
        try {
            paintObject(i);
        } catch (const Error& e) {
            message_ = e.what();
        }
    }
}

// RedrawPage (exe:00411760): mode 1 without a mode 0 before - the save-under is taken from what is on the
// surface now, which still shows the object itself.
void GameObjects::redrawPainted() {
    for (size_t i = 0; i < objs_.size(); i++)
        if (objs_[i].painted.on) {
            try {
                paintObject(static_cast<int>(i), false);
            } catch (const Error& e) {
                message_ = e.what();
            }
        }
}

void GameObjects::pageLeaving() {
    for (Obj& o : objs_) { o.painted.on = false; o.fruit.on = false; }
}

// exe:00447700 (page entry): objects of status 1 painted, zones of status 1/6 enabled.
void GameObjects::pageBaked() {
    for (Obj& o : objs_) { o.painted.on = false; o.fruit.on = false; }
    const int pn = view_.pageNumber();
    for (int z : zonesOfPage(pn)) {
        Zone& zz = zones_[static_cast<size_t>(z)];
        zz.enabled = false;
        zz.hasHit = false;
        int obj = zoneObj_[static_cast<size_t>(z)];
        if (obj < 0) continue;
        int st = objs_[static_cast<size_t>(obj)].status;
        if (st == ST_ZONE) {
            try {
                paintObject(obj);
            } catch (const Error& e) {
                message_ = e.what();
            }
            enableZone(z);
        } else if (st == ST_FRUIT) {
            enableZone(z);
        }
    }
    paintFruitOrnaments(-1);
}

// ---------------------------------------------------------------------------------------------
// events, animations, deferred variations

void GameObjects::fire(int obj, int code, const std::string& extra) {
    if (!logic_) return;
    nesting_.push_back(obj);
    logic_->onGameObject(def(obj).name, code, extra);
    nesting_.pop_back();
}

void GameObjects::clearDeferred() {
    for (DeferredSlot& s : deferred_) s = DeferredSlot();
}

void GameObjects::flushDeferred() {
    for (DeferredSlot& s : deferred_) {
        if (s.obj < 0) continue;
        DeferredSlot copy = s;
        s = DeferredSlot();
        applyVariation(copy.obj, copy.name, copy.group, false);
    }
}

// exe:0044ddb0: first record with the event code whose variation name equals the current variation.
const ObjectAnim* GameObjects::findAnim(int obj, int code) const {
    const ObjectDef& d = def(obj);
    const Obj& o = objs_[static_cast<size_t>(obj)];
    for (size_t k = 0; k < d.anims.size(); k++)
        if (d.anims[k].event == code && iequals(d.anims[k].variation, o.variation))
            return &d.anims[k];
    return nullptr;
}

void GameObjects::playAnim(int obj, int code, bool hideHeld, bool priority) {
    if (!anim_) return;
    const ObjectAnim* a = findAnim(obj, code);
    if (!a || a->file.empty()) return;
    heldHidden_ = hideHeld;
    anim_->play(*a, hideHeld, priority);
    heldHidden_ = false;
}

// ---------------------------------------------------------------------------------------------
// holding image (spec 1.6.1)

void GameObjects::setupHolding(int obj) {
    Obj& o = objs_[static_cast<size_t>(obj)];
    heldImg_ = Image8();
    auto has = [&](int g) {
        for (const HoldImage& h : o.rec.holding) if (h.image && static_cast<int>(h.group) == g) return true;
        return false;
    };
    if (!has(o.holdGroup)) {
        bool any = false;
        u32 lo = 0;
        for (const HoldImage& h : o.rec.holding) if (h.image && (!any || h.group < lo)) { lo = h.group; any = true; }
        o.holdGroup = static_cast<int>(lo);
    }
    o.holdGroupCount = 0;
    for (const HoldImage& h : o.rec.holding) if (h.image && static_cast<int>(h.group) == o.holdGroup) o.holdGroupCount++;
    heldIdx_ = 0;
    for (size_t i = 0; i < o.rec.holding.size(); i++)
        if (o.rec.holding[i].image && static_cast<int>(o.rec.holding[i].group) == o.holdGroup) { heldIdx_ = i; break; }
    heldTick_ = now_;
    if (heldIdx_ < o.rec.holding.size() && o.rec.holding[heldIdx_].image)
        heldImg_ = img(obj, static_cast<int>(o.rec.holding[heldIdx_].image));
}

void GameObjects::refreshHeldImage() {
    if (held_ >= 0) {
        Obj& o = objs_[static_cast<size_t>(held_)];
        if (heldIdx_ < o.rec.holding.size() && o.rec.holding[heldIdx_].image)
            heldImg_ = img(held_, static_cast<int>(o.rec.holding[heldIdx_].image));
    }
}

// exe:00448eb0: one frame per idle pass when the group has two or more images.
void GameObjects::update(u32 nowMs) {
    now_ = nowMs;
    if (held_ < 0) return;
    Obj& o = objs_[static_cast<size_t>(held_)];
    if (o.holdGroupCount < 2 || o.rec.holdDelayMs == 0) return;
    if (nowMs - heldTick_ < o.rec.holdDelayMs) return;
    size_t n = o.rec.holding.size();
    for (size_t step = 1; step <= n; step++) {
        size_t i = (heldIdx_ + step) % n;
        if (o.rec.holding[i].image && static_cast<int>(o.rec.holding[i].group) == o.holdGroup) {
            heldIdx_ = i;
            break;
        }
    }
    heldTick_ = nowMs;
    refreshHeldImage();
}

void GameObjects::drawHeld(Frame& f, int viewTop) const {
    if (held_ < 0 || heldHidden_ || !inputEnabled_ || heldImg_.px.empty()) return;   // (exe:00447bb0: a keyed sim-status window blits the page first with its flag off)
    const Obj& o = objs_[static_cast<size_t>(held_)];
    if (heldIdx_ >= o.rec.holding.size()) return;
    const HoldImage& h = o.rec.holding[heldIdx_];
    blit(f, heldImg_, h.x, viewTop + h.y, {0, viewTop, Frame::W, PageView::ViewH}, keyIndex(h.flag, h.transp));
}

bool GameObjects::heldLayer(ViewLayer& out) const {
    if (held_ < 0 || heldHidden_ || !inputEnabled_ || heldImg_.px.empty()) return false;
    const Obj& o = objs_[static_cast<size_t>(held_)];
    if (heldIdx_ >= o.rec.holding.size()) return false;
    const HoldImage& h = o.rec.holding[heldIdx_];
    out.image = heldImg_;
    out.x = h.x;
    out.y = h.y;
    out.transparent = keyIndex(h.flag, h.transp);
    return true;
}

void GameObjects::drawWearing(Frame& f, int viewTop) const {
    for (int obj : wear_) {
        const Obj& o = objs_[static_cast<size_t>(obj)];
        if (!o.rec.wearing.image) continue;
        try {
            const Image8& pic = img(obj, static_cast<int>(o.rec.wearing.image));
            blit(f, pic, o.rec.wearing.x, viewTop + o.rec.wearing.y, {0, viewTop, Frame::W, PageView::ViewH},
                 keyIndex(o.rec.wearing.flag, o.rec.wearing.transp));
        } catch (const Error&) {
        }
    }
}

// ---------------------------------------------------------------------------------------------
// bar helpers

void GameObjects::insertOnBar(int obj, bool worn) {
    const Obj& o = objs_[static_cast<size_t>(obj)];
    u32 icon = worn ? o.rec.wornIcon : o.rec.icon;
    if (!icon) { message_ = "object " + def(obj).name + " has no bar icon"; return; }
    bar_.insert(-1, img(obj, static_cast<int>(icon)), obj, def(obj).unk18);
}

// ---------------------------------------------------------------------------------------------
// transitions

// Left click on a drop-zone hotspot (spec 1.5, exe:00447f40).
bool GameObjects::activateZone(int pageNumber, int hotspotIndex) {
    int z = zoneIndexAt(pageNumber, hotspotIndex);
    if (z < 0) return false;
    if (!usable()) return true;
    if (held_ >= 0 && handLocked(held_)) return true;
    if (!zones_[static_cast<size_t>(z)].occupied) return true;
    int obj = zoneObj_[static_cast<size_t>(z)];
    if (obj < 0) return true;
    clearDeferred();
    if (def(obj).fruit != 1) {
        if (held_ >= 0 && !stashHeld()) return true;
        Obj& o = objs_[static_cast<size_t>(obj)];
        eraseObject(obj);
        zones_[static_cast<size_t>(z)].occupied = false;
        zoneObj_[static_cast<size_t>(z)] = -1;
        o.zone = -1;
        o.status = ST_HELD;
        playAnim(obj, 1, true, true);
        fire(obj, 0x0a);
        setupHolding(obj);
        held_ = obj;
        flushDeferred();
        fire(obj, 0x01);
        disableZone(z);
    } else {
        pickUpFruit(obj, z);
    }
    return true;
}

void GameObjects::pickUpFruit(int obj, int z) {
    Obj& o = objs_[static_cast<size_t>(obj)];
    restoreFruitBackground(obj);
    zones_[static_cast<size_t>(z)].occupied = false;
    zoneObj_[static_cast<size_t>(z)] = -1;
    o.zone = -1;
    insertOnBar(obj, false);
    o.status = ST_BAR;
    fire(obj, 0x0a);
    fire(obj, 0x01);
    fire(obj, 0x0b);
    fire(obj, 0x02);
    disableZone(z);
    flushDeferred();
    bar_.flash(now_ + 1000);   // "flash" the bar for one second (DAT_004979d8, exe:0045b0c0)
}

// exe:0044e9a0 (spec 1.6.2)
bool GameObjects::stashHeld() {
    if (held_ < 0 || handLocked(held_)) return false;
    int obj = held_;
    held_ = -1;
    heldImg_ = Image8();
    playAnim(obj, 2, false, false);
    insertOnBar(obj, false);
    objs_[static_cast<size_t>(obj)].status = ST_BAR;
    fire(obj, 0x0b);
    fire(obj, 0x02);
    flushDeferred();
    return true;
}

// exe:0044f370 (spec 1.6.3)
void GameObjects::barToHand(int obj) {
    if (held_ >= 0) {
        if (handLocked(held_)) return;
        stashHeld();
    }
    clearDeferred();
    bar_.remove(obj);
    playAnim(obj, 5, false, false);
    fire(obj, 0x0c);
    setupHolding(obj);
    held_ = obj;
    objs_[static_cast<size_t>(obj)].status = ST_HELD;
    flushDeferred();
    fire(obj, 0x03);
}

void GameObjects::useFruit(int obj) {
    Obj& o = objs_[static_cast<size_t>(obj)];
    clearDeferred();
    if (o.usage == 0) {
        fire(obj, 5);
    } else {
        if (o.usage != -1) o.usage--;
        fire(obj, 4);
    }
    flushDeferred();
}

void GameObjects::sortWearing() {
    std::stable_sort(wear_.begin(), wear_.end(), [&](int a, int b) {
        return objs_[static_cast<size_t>(a)].rec.wearing.flag < objs_[static_cast<size_t>(b)].rec.wearing.flag;
    });
}

// exe:0044e240 + events (spec 1.6.6): the held wearable moves to a wearing slot and the bar.
bool GameObjects::wear(int obj) {
    if (wear_.size() >= 3) {
        message_ = "trying to wear more than 3 objects";
        return false;
    }
    Obj& o = objs_[static_cast<size_t>(obj)];
    held_ = -1;
    heldImg_ = Image8();
    o.status = ST_WORN;
    wear_.push_back(obj);
    sortWearing();
    insertOnBar(obj, true);
    fire(obj, 0x0e);
    fire(obj, 0x08);
    flushDeferred();
    return true;
}

// exe:0044f620 (spec 1.6.8)
void GameObjects::takeOff(int obj) {
    Obj& o = objs_[static_cast<size_t>(obj)];
    clearDeferred();
    fire(obj, 0x0f);
    wear_.erase(std::remove(wear_.begin(), wear_.end(), obj), wear_.end());
    if (o.rec.icon) bar_.changeImage(obj, img(obj, static_cast<int>(o.rec.icon)));
    o.status = ST_BAR;
    fire(obj, 0x09);
    flushDeferred();
}

void GameObjects::selfUse() {
    int obj = held_;
    Obj& o = objs_[static_cast<size_t>(obj)];
    clearDeferred();
    bool used;
    if (o.usage == 0) {
        playAnim(obj, 4, true, true);
        used = false;
    } else {
        playAnim(obj, 3, true, true);
        if (o.usage != -1) o.usage--;
        used = true;
    }
    if (def(obj).wearable && used) {
        if (wear(obj)) return;
    }
    fire(obj, used ? 4 : 5);
    flushDeferred();
}

// Verb 14 (spec 1.6.7, exe:004487b0)
bool GameObjects::useOnHotspot(const Hotspot& h) {
    if (!usable() || held_ < 0) return false;
    if (!filterMatches(filterOf(h), def(held_).name, false)) return false;
    int obj = held_;
    clearDeferred();
    playAnim(obj, 7, true, true);
    fire(obj, 0x07, h.name);
    flushDeferred();
    return true;
}

void GameObjects::keyStash() {
    if (!usable()) return;
    if (held_ >= 0) stashHeld();
    else if (!wear_.empty()) takeOff(wear_.front());
}

void GameObjects::keySpace(bool overBar, int barObject, const Hotspot* hovered) {
    if (!usable()) return;
    if (overBar) {
        if (barObject < 0) return;
        int st = objs_[static_cast<size_t>(barObject)].status;
        if (st == ST_WORN) return;
        if (st != ST_BAR) return;
        if (def(barObject).fruit == 1) { useFruit(barObject); return; }
        barToHand(barObject);
        return;
    }
    if (held_ < 0) return;
    if (hovered && hovered->action.verb == 14 && useOnHotspot(*hovered)) return;
    selfUse();
}

void GameObjects::barClick(int obj) {
    if (!usable() || obj < 0) return;
    if (held_ >= 0 && handLocked(held_)) return;
    int st = objs_[static_cast<size_t>(obj)].status;
    if (st == ST_WORN) takeOff(obj);
    else if (st == ST_BAR) {
        if (def(obj).fruit == 1) useFruit(obj);
        else barToHand(obj);
    }
}

// Nearest free, non-fruit zone of the current page whose filter accepts the object; distance from the
// zone rectangle centre to the view centre (320,184), x wrapped on panoramas (spec 1.6.4).
int GameObjects::nearestFreeZone(int obj) const {
    if (view_.pageNumber() == 0) return -1;
    const Page& p = view_.page();
    const std::string& name = def(obj).name;
    int best = -1;
    long bestD = 0;
    for (int z : zonesOfPage(p.number)) {
        const Zone& zz = zones_[static_cast<size_t>(z)];
        if (zz.occupied || zz.fruit) continue;
        const Hotspot& hs = p.hotspots.at(static_cast<size_t>(zz.hotspot));
        if (hs.action.verb != VERB_ALLOWOBJECTDROP) continue;
        if (hs.action.arg[1] & 1) continue;   // "never active" zones are DLL-only (guess: not drop candidates)
        if (!filterMatches(filterOf(hs), name, true)) continue;
        int cx = (hs.left + hs.right + 1) / 2, cy = (hs.top + hs.bottom + 1) / 2;
        int dx = cx - (view_.scrollX() + 320), dy = cy - (view_.scrollY() + PageView::ViewH / 2);
        if (p.panorama()) dx = wrapDelta(dx, p.pixelWidth());
        long d = static_cast<long>(dx) * dx + static_cast<long>(dy) * dy;
        if (best < 0 || d < bestD) { best = z; bestD = d; }
    }
    return best;
}

// Zone assigned, normal image chosen by zone type, painted when the zone's page is current.
void GameObjects::placeInZone(int obj, int z) {
    Obj& o = objs_[static_cast<size_t>(obj)];
    Zone& zz = zones_[static_cast<size_t>(z)];
    int tt;
    o.normalEntry = chooseNormal(obj, zz.type, tt);
    o.zoneType = tt;
    o.normalImage = static_cast<int>(o.rec.normal[o.normalEntry].image);
    o.zone = z;
    zz.occupied = true;
    zoneObj_[static_cast<size_t>(z)] = obj;
    o.status = ST_ZONE;
    if (zoneOnCurrentPage(z)) paintObject(obj);
}

void GameObjects::dropHeldInto(int z) {
    int obj = held_;
    held_ = -1;
    heldImg_ = Image8();
    clearDeferred();
    playAnim(obj, 6, false, true);
    placeInZone(obj, z);
    fire(obj, 0x0d);
    flushDeferred();
    fire(obj, 0x06, zones_[static_cast<size_t>(z)].hotName);
    enableZone(z);
    flushDeferred();
}

// exe:0044d030 (spec 1.6.4)
void GameObjects::dropHeld(const std::string* hotspotName, bool fromKey, bool* dropped) {
    if (dropped) *dropped = false;
    // (no hand-lock test here: exe:0044d030 never reads the object's +0x1a8, nor do its two callers, the D key and API 0x48; a hand-locked
    //  object such as the oxygen cylinder cannot be stashed or swapped, but it can be put down)
    if (!usable() || held_ < 0 || view_.pageNumber() == 0) return;
    int z = -1;
    if (hotspotName) {
        // guess (spec 1.4): a named target only has to exist and be free; the filter is not checked
        for (int cand : zonesOfPage(view_.pageNumber()))
            if (iequals(zones_[static_cast<size_t>(cand)].hotName, *hotspotName)) { z = cand; break; }
        if (z < 0 || zones_[static_cast<size_t>(z)].occupied) return;
    } else {
        z = nearestFreeZone(held_);
        if (z < 0) {
            if (fromKey) fire(held_, 0x10);
            return;
        }
    }
    dropHeldInto(z);
    if (dropped) *dropped = true;
}

// exe:0044e450 (spec 1.6.5): drop an object from the bar (worn objects are taken off first).
void GameObjects::dropFromBar(int obj, bool fromKey) {
    int st = objs_[static_cast<size_t>(obj)].status;
    if (st != ST_BAR && st != ST_WORN) return;
    int z = nearestFreeZone(obj);
    if (z < 0) {
        if (fromKey) fire(obj, 0x10);
        return;
    }
    clearDeferred();
    if (st == ST_WORN) takeOff(obj);
    bar_.remove(obj);
    placeInZone(obj, z);
    fire(obj, 0x0d);
    fire(obj, 0x06, zones_[static_cast<size_t>(z)].hotName);
    enableZone(z);
    flushDeferred();
}

void GameObjects::keyDrop(bool overBar, int barObject) {
    if (!usable()) return;
    if (overBar) {
        if (barObject >= 0) dropFromBar(barObject, true);
        return;
    }
    if (held_ >= 0) {
        dropHeld(nullptr, true, nullptr);
        return;
    }
    if (!wear_.empty()) dropFromBar(wear_.front(), true);
}

// ---------------------------------------------------------------------------------------------
// variations (spec 1.7)

bool GameObjects::applyVariation(int obj, const std::string& name, u32 group, bool dataOnly) {
    if (!validObject(obj)) return false;
    const ObjectDef& d = def(obj);
    if (d.nVariations == 0 || name.empty()) return false;
    size_t vi = d.variations.size();
    for (size_t k = 0; k < d.variations.size(); k++)
        if (iequals(d.variations[k].name, name)) { vi = k; break; }
    if (vi == d.variations.size()) return false;
    Obj& o = objs_[static_cast<size_t>(obj)];
    if (iequals(o.variation, name)) return true;
    const ObjectVariation& v = d.variations[vi];
    if (!dataOnly && o.status == ST_ZONE) eraseObject(obj);
    for (int k = 0; k < 3; k++) o.rec.normal[k] = v.normal[k];
    o.rec.icon = v.icon;
    o.rec.wornIcon = v.unk28;
    o.rec.holding = v.holding;
    o.rec.holdDelayMs = v.holdDelayMs;
    o.rec.wearing = v.wearing;
    o.rec.shortName = def(obj).variations[vi].shortName;
    o.variation = v.name;
    // holding group: 0xffffffff keep the current one if present, 0xfffffffe or missing = lowest
    auto has = [&](u32 g) {
        for (const HoldImage& h : o.rec.holding) if (h.image && h.group == g) return true;
        return false;
    };
    u32 g = group;
    if (g == 0xffffffffu) g = static_cast<u32>(o.holdGroup);
    if (g == 0xfffffffeu || !has(g)) {
        bool any = false;
        u32 lo = 0;
        for (const HoldImage& h : o.rec.holding) if (h.image && (!any || h.group < lo)) { lo = h.group; any = true; }
        g = lo;
    }
    o.holdGroup = static_cast<int>(g);
    o.holdGroupCount = 0;
    for (const HoldImage& h : o.rec.holding) if (h.image && h.group == g) o.holdGroupCount++;
    if (dataOnly) return true;
    switch (o.status) {
    case ST_ZONE:
        if (o.zone >= 0) {
            int tt;
            o.normalEntry = chooseNormal(obj, zones_[static_cast<size_t>(o.zone)].type, tt);
            o.zoneType = tt;
            o.normalImage = static_cast<int>(o.rec.normal[o.normalEntry].image);
            if (zoneOnCurrentPage(o.zone)) paintObject(obj);
        }
        break;
    case ST_HELD:
        setupHolding(obj);
        break;
    case ST_BAR:
        if (o.rec.icon) bar_.changeImage(obj, img(obj, static_cast<int>(o.rec.icon)));
        break;
    case ST_WORN:
        if (o.rec.wornIcon) bar_.changeImage(obj, img(obj, static_cast<int>(o.rec.wornIcon)));
        break;
    default:
        break;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// API commands (spec 1.12)

int GameObjects::apiObjectName(int index, std::string& name) const {
    if (index < 0 || index >= objectCount()) return API_UNKNOWN_OBJECT;
    name = def(index).name;
    return API_OK;
}

int GameObjects::apiObjectInfo(const std::string& name, ObjectInfo& info) const {
    int obj = findObject(name);
    if (obj < 0) return API_UNKNOWN_OBJECT;
    const ObjectDef& d = def(obj);
    const Obj& o = objs_[static_cast<size_t>(obj)];
    info = ObjectInfo();
    info.moveable = d.moveable == 1;
    info.wearable = d.wearable == 1;
    info.inZone = o.status == ST_ZONE || d.moveable != 1;
    info.fruitInZone = o.status == ST_FRUIT;
    info.held = o.status == ST_HELD;
    info.onBar = o.status == ST_BAR || o.status == ST_WORN;
    info.removed = o.status == ST_REMOVED;
    info.worn = o.status == ST_WORN;
    info.weight = o.weight;
    info.usageCount = o.usage;
    info.fruit = d.fruit == 1;
    info.zone = (o.status == ST_ZONE || o.status == ST_FRUIT) ? o.zone : -1;
    return API_OK;
}

int GameObjects::apiZoneInfo(int zone, ZoneInfo& info) const {
    if (zone < 0 || zone >= zoneCount()) return API_ZONE_RANGE;
    const Zone& z = zones_[static_cast<size_t>(zone)];
    info.page = z.pageName;
    info.hotspotIndex = z.hotspot;
    int obj = zoneObj_[static_cast<size_t>(zone)];
    info.object = obj >= 0 ? def(obj).name : "";
    return API_OK;
}

int GameObjects::apiZoneOfHotspot(const std::string& hotspot, int& zone) const {
    if (view_.pageNumber() == 0) return API_NOT_FOUND_HOTSPOT;
    const Page& p = view_.page();
    for (size_t i = 0; i < p.hotspots.size(); i++)
        if (iequals(p.hotspots[i].name, hotspot)) {
            int z = zoneIndexAt(p.number, static_cast<int>(i));
            if (z < 0) return API_NOT_A_ZONE;
            zone = z;
            return API_OK;
        }
    return API_NOT_FOUND_HOTSPOT;
}

int GameObjects::apiSetObjectValue(const std::string& name, const std::string& what, int value) {
    int obj = findObject(name);
    if (obj < 0) return API_UNKNOWN_OBJECT;
    Obj& o = objs_[static_cast<size_t>(obj)];
    if (iequals(what, "weight")) o.weight = std::min(value, 100);
    else if (iequals(what, "usagecount")) o.usage = value;
    else return API_BAD_NAME;
    return API_OK;
}

void GameObjects::removeObject(int obj) {
    Obj& o = objs_[static_cast<size_t>(obj)];
    switch (o.status) {
    case ST_ZONE:
        eraseObject(obj);
        if (o.zone >= 0) {
            zones_[static_cast<size_t>(o.zone)].occupied = false;
            zoneObj_[static_cast<size_t>(o.zone)] = -1;
            disableZone(o.zone);
        }
        o.zone = -1;
        break;
    case ST_FRUIT:
        restoreFruitBackground(obj);
        if (o.zone >= 0) {
            zones_[static_cast<size_t>(o.zone)].occupied = false;
            zoneObj_[static_cast<size_t>(o.zone)] = -1;
            disableZone(o.zone);
        }
        o.zone = -1;
        break;
    case ST_HELD:
        held_ = -1;
        heldImg_ = Image8();
        break;
    case ST_WORN:
        takeOff(obj);
        bar_.remove(obj);
        break;
    case ST_BAR:
        bar_.remove(obj);
        break;
    default:
        break;
    }
    o.status = ST_REMOVED;
}

int GameObjects::apiRemoveObject(const std::string& name) {
    int obj = findObject(name);
    if (obj < 0) return API_UNKNOWN_OBJECT;
    removeObject(obj);
    return API_OK;
}

int GameObjects::apiSetHoldingGroup(const std::string& name, u32 group) {
    int obj = findObject(name);
    if (obj < 0) return API_UNKNOWN_OBJECT;
    Obj& o = objs_[static_cast<size_t>(obj)];
    bool present = false;
    for (const HoldImage& h : o.rec.holding) present |= (h.image && h.group == group);
    if (!present) return API_NO_SUCH_GROUP;
    if (static_cast<u32>(o.holdGroup) == group) return API_OK;
    o.holdGroup = static_cast<int>(group);
    o.holdGroupCount = 0;
    for (const HoldImage& h : o.rec.holding) if (h.image && h.group == group) o.holdGroupCount++;
    if (o.status == ST_HELD) setupHolding(obj);
    return API_OK;
}

// exe:0044a030: copy a named extra image of `srcObject` into an image of `dstObject` (goggles gauge).
int GameObjects::apiPlaceImage(const std::string& srcObject, const std::string& extraName, const std::string& dstObject,
                               const std::string& kind, int holdIndex, u32 group, int x, int y) {
    int src = findObject(srcObject), dst = findObject(dstObject);
    if (src < 0 || dst < 0) return API_UNKNOWN_OBJECT;
    if (def(src).moveable != 1 || def(dst).moveable != 1) return API_UNKNOWN_OBJECT;
    if (def(src).nVariations != 0 || def(dst).nVariations != 0) return API_BAD_PARAM;
    int extraImg = 0;
    for (const NamedImage& n : def(src).named)
        if (n.image && iequals(n.name, extraName)) { extraImg = static_cast<int>(n.image); break; }
    if (!extraImg) return API_NO_EXTRA_IMAGE;
    Obj& o = objs_[static_cast<size_t>(dst)];
    int target = 0;
    std::string k = lower(kind);
    if (k == "normal1" || k == "normal2" || k == "normal3") target = static_cast<int>(o.rec.normal[k[6] - '1'].image);
    else if (k == "normal4") target = static_cast<int>(o.rec.icon);
    else if (k == "icon1") { if (def(dst).wearable != 1) return API_BAD_PARAM; target = static_cast<int>(o.rec.wornIcon); }
    else if (k == "holding") {
        int n = 0;
        for (const HoldImage& h : o.rec.holding)
            if (h.image && h.group == group) { if (n == holdIndex) { target = static_cast<int>(h.image); break; } n++; }
    } else if (k == "wearing") { if (def(dst).wearable != 1) return API_BAD_PARAM; target = static_cast<int>(o.rec.wearing.image); }
    else return API_BAD_PARAM;
    if (!target) return API_IMAGE_MISSING;
    Image8 patched = img(dst, target);
    const Image8& piece = img(src, extraImg);
    for (int yy = 0; yy < piece.h; yy++)
        for (int xx = 0; xx < piece.w; xx++) {
            int dx = x + xx, dy = y + yy;
            if (dx >= 0 && dx < patched.w && dy >= 0 && dy < patched.h)
                patched.px[static_cast<size_t>(dy) * patched.w + dx] = piece.px[static_cast<size_t>(yy) * piece.w + xx];
        }
    overrides_[imgKey(dst, target)] = patched;
    // refresh the visible copy
    switch (o.status) {
    case ST_ZONE:
        if (o.zone >= 0 && zoneOnCurrentPage(o.zone) && k.rfind("normal", 0) == 0) paintObject(dst);
        break;
    case ST_BAR:
    case ST_WORN:
        if (k == "normal4" || k == "icon1") bar_.changeImage(dst, patched);
        break;
    case ST_HELD:
        if (k == "holding") refreshHeldImage();
        break;
    default:
        break;
    }
    return API_OK;
}

// API 0x45 (spec 1.9): a removed fruit lies in its home zone again; its ornament is shown again.
int GameObjects::apiRestoreFruit(const std::string& name) {
    int obj = findObject(name);
    if (obj < 0) return API_UNKNOWN_OBJECT;
    Obj& o = objs_[static_cast<size_t>(obj)];
    if (o.status != ST_REMOVED || def(obj).fruit != 1 || o.home < 0) return API_BAD_PARAM;
    o.zone = o.home;
    o.status = ST_FRUIT;
    Zone& zz = zones_[static_cast<size_t>(o.home)];
    zz.occupied = zz.fruit = true;
    zoneObj_[static_cast<size_t>(o.home)] = obj;
    if (zoneOnCurrentPage(o.home)) {
        enableZone(o.home);
        paintFruitOrnaments(obj);
    }
    return API_OK;
}

int GameObjects::apiChangeVariation(const std::string& name, const std::string& variation, u32 group) {
    int obj = findObject(name);
    if (obj < 0) return API_UNKNOWN_OBJECT;
    if (std::find(nesting_.begin(), nesting_.end(), obj) != nesting_.end()) {
        message_ = "Warning, ChangeGameObjectVariation called while handling an event of the same object";
        return API_REFUSED;
    }
    return applyVariation(obj, variation, group, false) ? API_OK : API_FAILED;
}

// API 0x57: the object is the one whose OnGameObject event is being handled.
int GameObjects::apiChangeVariationDeferred(const std::string& variation, u32 group) {
    if (nesting_.empty()) return API_FAILED;
    return apiChangeVariationDeferred(def(nesting_.back()).name, variation, group);
}

int GameObjects::apiChangeVariationDeferred(const std::string& name, const std::string& variation, u32 group) {
    int obj = findObject(name);
    if (obj < 0) return API_UNKNOWN_OBJECT;
    if (nesting_.empty() || nesting_.back() != obj) return API_FAILED;
    DeferredSlot* slot = nullptr;
    for (DeferredSlot& s : deferred_) if (s.obj == obj) { slot = &s; break; }
    if (variation.empty()) {                     // empty name cancels
        if (slot) *slot = DeferredSlot();
        return API_OK;
    }
    if (!slot) for (DeferredSlot& s : deferred_) if (s.obj < 0) { slot = &s; break; }
    if (!slot) { message_ = "no free slot in table"; return API_FAILED; }
    slot->obj = obj;
    slot->group = group;
    slot->name = variation;
    return API_OK;
}

// API 0x47: play an arbitrary animation with the object-animation player.
int GameObjects::apiPlayUseAnimation(const std::string& tanFile, const std::string& wav, int x, int y, int mode, int delayMs,
                                     bool hideHeld, bool removeHeldAfter, const std::string& variation, u32 group) {
    if (anim_ && !tanFile.empty()) {
        ObjectAnim rec;
        rec.x = x; rec.y = y; rec.unk0c = mode; rec.delayMs = delayMs; rec.file = tanFile; rec.wav = wav;
        heldHidden_ = hideHeld;
        bool ok = anim_->play(rec, hideHeld, true);
        heldHidden_ = false;
        if (!ok) return API_NOTHING_DROPPED;   // 0x11: the animation could not be opened
    }
    if (held_ >= 0) {
        int obj = held_;
        if (removeHeldAfter) removeObject(obj);
        else if (!variation.empty()) applyVariation(obj, variation, group, false);
    }
    return API_OK;
}

int GameObjects::apiDropHeld(const std::string* hotspotName) {
    bool dropped = false;
    dropHeld(hotspotName, false, &dropped);
    return dropped ? API_OK : API_NOTHING_DROPPED;
}

// API 0x52 (spec 1.9)
int GameObjects::apiReturnObject(const std::string& name) {
    int obj = findObject(name);
    if (obj < 0) return API_UNKNOWN_OBJECT;
    Obj& o = objs_[static_cast<size_t>(obj)];
    if (def(obj).fruit == 1 || def(obj).moveable != 1 || o.status != ST_REMOVED || o.home < 0) return API_BAD_PARAM;
    if (zones_[static_cast<size_t>(o.home)].occupied) return API_HOME_OCCUPIED;
    placeInZone(obj, o.home);
    if (zoneOnCurrentPage(o.home)) enableZone(o.home);
    return API_OK;
}

int GameObjects::debugHold(const std::string& name) {
    int obj = findObject(name);
    if (obj < 0) return API_UNKNOWN_OBJECT;
    if (def(obj).fruit == 1) return API_BAD_PARAM;
    if (held_ == obj) return API_OK;
    if (held_ >= 0 && handLocked(held_)) return API_BAD_PARAM;
    Obj& o = objs_[static_cast<size_t>(obj)];
    if (o.status == ST_WORN) takeOff(obj);
    if (held_ == obj) return API_OK;
    if (o.status != ST_BAR) {
        if (o.status != ST_REMOVED) removeObject(obj);
        insertOnBar(obj, false);
        o.status = ST_BAR;
    }
    barToHand(obj);
    return held_ == obj ? API_OK : API_BAD_PARAM;
}

std::string GameObjects::describe(int obj) const {
    const Obj& o = objs_.at(static_cast<size_t>(obj));
    std::string s = "obj " + std::to_string(obj) + " " + def(obj).name + " status=" + std::to_string(o.status);
    if (o.zone >= 0) s += " zone=" + zones_[static_cast<size_t>(o.zone)].pageName + "/" + zones_[static_cast<size_t>(o.zone)].hotName;
    if (o.home >= 0) s += " home=" + zones_[static_cast<size_t>(o.home)].pageName + "/" + zones_[static_cast<size_t>(o.home)].hotName;
    if (!o.variation.empty()) s += " var=" + o.variation;
    if (o.status == ST_ZONE) s += " image=" + std::to_string(o.normalImage);
    if (o.status == ST_ZONE && o.painted.on)
        s += " rect=" + std::to_string(o.painted.r.x) + "," + std::to_string(o.painted.r.y) + "," +
             std::to_string(o.painted.r.w) + "," + std::to_string(o.painted.r.h);
    s += " usage=" + std::to_string(o.usage);
    return s;
}

} // namespace biosys
