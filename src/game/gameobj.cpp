#include "game/gameobj.h"
#include "game/obj_common.h"
#include "game/site.h"
#include "game/path.h"
#include "game/plant.h"
#include "game/watertank.h"
#include <cstdio>
#include <cstring>

namespace biosys::game {

double dwordsToDouble(u32 hi, u32 lo) {
    uint64_t bits = (static_cast<uint64_t>(hi) << 32) | lo;
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
}

// dll:10028e90
GameObj::GameObj(u32 ctorAddr, const char* className, const char* name, i32 nameStringId)
    : Item(ctorAddr, className, name) {
    if (std::strlen(name) > 11) throw Error(std::string("object name too long for char[12]: ") + name);
    m_dwNameStringId = nameStringId;
    m_dwFlags = 0;
    m_dwPickupEvent = 0;
    m_szVariant[0] = 0;
    m_dWeight = 0;
    m_dwUnk1c = 0;
    m_bInScene = 1;
    m_dwTimeStamp = 0x7080;
    m_bHeld = 0;
}

// dll:10029490 (copy loop after the engine accepted the variant)
void GameObj::setVariant(const char* variant) {
    size_t n = std::strlen(variant) + 1;
    if (n > m_szVariant.size()) throw Error(std::string("variant name too long: ") + variant);
    std::memcpy(m_szVariant.data(), variant, n);
}

std::string GameObj::variant() const {
    return std::string(reinterpret_cast<const char*>(m_szVariant.data()), strnlen(reinterpret_cast<const char*>(m_szVariant.data()), 11));
}

// dll:10029440 (FUN_10029440)
int GameObj::changeVariation(Self& s, const char* variant, int mode) {
    bool ok = s.host().ChangeGameObjectVariation(name().c_str(), variant, mode);
    if (ok) setVariant(variant);
    return ok ? 1 : 0;
}

// dll:10029490
int GameObj::safeSetVariant(Self& s, const char* variant, int mode) {
    if (!s.host().Eng_ChangeGameObjectVariation1(variant, mode)) {
        int err = s.host().Eng_GetLastError();
        s.world().log(std::string("CGameObj::SafeSetVariant, szVariant: ") + variant + ", error: " + std::to_string(err));
        return 0;
    }
    setVariant(variant);
    return 1;
}

// dll:10029510 (FUN_10029510)
void GameObj::removeFromScene(Self* s) {
    if (s && !s->host().Eng_RemoveGameObject(name().c_str())) return;
    m_bInScene = 0;
    if (m_bHeld != 0) {
        m_bHeld = 0;
        if (!s) throw Error("GameObj::removeFromScene: a held object is removed while the world is built");
        s->Drop(m_dWeight);
    }
    if (s && s->m_pObjHolding.item == this) s->m_pObjHolding.clear();
}

// dll:100291c0 (FUN_100291c0)
void GameObj::describe(Self& s) {
    if (s.g.describeSuppressed != 0) {
        s.g.describeSuppressed = 0;
        return;
    }
    if (m_dwNameStringId != 0) s.ShowMessage(1, m_dwNameStringId, 0xffff, 0);
}

// dll:10029120 (FUN_10029120)
int GameObj::onStashed(Self& s) {
    clearHolding(s);
    return 1;
}

// dll:10029130 (FUN_10029130)
int GameObj::onBarToHand(Self& s) {
    s.g.describeSuppressed = 1;
    setHolding(s);
    return 1;
}

// dll:10028ff0 (FUN_10028ff0)
int GameObj::onHoldingImage(Self& s) {
    setHolding(s);
    return 1;
}

// dll:10029000 CGameObj::PickUp
int GameObj::pickUp(Self& s) {
    if ((m_dwFlags & 0x1000000) != 0 && objutil::iequals(variant(), "burnt")) {
        removeFromScene(&s);
        return 1;
    }
    s.AddWeight(m_dWeight);
    m_bHeld = 1;
    if (m_dwPickupEvent != 0) s.host().triggerEvent(m_dwPickupEvent);
    if ((m_dwFlags & 0x80000000u) != 0 && s.m_bInsideAngelSequence != 0) s.g.angelSequenceDone = 1;   // DAT_100c1f0c, DAT_100c2498
    if ((m_dwFlags & 0x200) != 0 && (objutil::pageChar(s) == 'N' || objutil::pageChar(s) == 'A')) s.g.dat1009f838 = 1;
    return 1;
}

// dll:100290a0 (FUN_100290a0)
int GameObj::onDropped(Self& s, const std::string& hotspot) {
    s.Drop(m_dWeight);
    m_bHeld = 0;
    clearHolding(s);
    CFeature* f = objutil::feature(s);
    if ((m_dwFlags & 1) != 0 && f && (f->flags() & 0x40000000) != 0) {
        s.host().setFeatureObject(f, this);                     // *(feature + 0x58) = this; the feature classes (CVideoPlayer / CDataTerminal / CThermalCooker) own that member
        s.host().playSound("sounds\\house\\thermdnk.wav");
    }
    if ((m_dwFlags & 0x200) != 0 && objutil::pageChar(s) == 'N') s.g.dat1009f838 = 1;
    return 1;
}

// dll:10029160 (FUN_10029160)
int GameObj::onWorn(Self& s) {
    if (s.m_pObjHolding.item == this) clearHolding(s);
    return 1;
}

// dll:10028f10 (FUN_10028f10)
int GameObj::onUsedOnHotspot(Self& s, const std::string& hotspot) {
    std::string h = objutil::upper(hotspot);
    CFeature* f = objutil::feature(s);
    if (!f) {
        if (s.m_pPlant.item) {
            s.host().CPlant_UseObject(s.m_pPlant.item, this, h);          // dll:10072a00 (BiosGame::CPlant_UseObject)
            goto done;
        }
        if (h.empty() || h[0] != 'Y') goto done;
        h[0] = 'L';
        {
            CSite* site = dynamic_cast<CSite*>(s.m_pSite.item);
            // FUN_1000c960(site, name): the site's link list (site+0x7c) searched by the path name (path+0x10), case blind
            CPath* path = nullptr;
            if (site)
                for (Item* it : site->links()) {
                    CPath& p = as<CPath>(it);
                    if (objutil::iequals(p.pathName(), h.c_str())) { path = &p; break; }
                }
            if (path) {
                s.host().CPath_UseObject(path, this);                      // dll:10001200 (bg_player.cpp)
                goto done;
            }
        }
        h[0] = 'F';
        f = s.world().findFeature(h.c_str());
        if (!f) goto done;
    }
    f->onApply(*this, h);                                                   // feature vtable +0x44
done:
    if ((m_dwFlags & 0x20) != 0) removeFromScene(&s);
    return 1;
}

// dll:10029230 (FUN_10029230)
int GameObj::onKey(Self& s, int key, u32* allow) {
    if (s.m_bAwake == 0) *allow = 0;
    CFeature* f = objutil::feature(s);
    if (key == 0x20) {
        if ((m_dwFlags & 0x200000) != 0) {
            *allow = 0;
            return 0;
        }
        if ((m_dwFlags & 0x20000000) != 0 && s.m_dNutrition == 100.0) {
            s.ShowMessage(1, 0xb1, 0xffff, 0);
            *allow = 0;
            return 0;
        }
    } else if (key == 0x44) {
        if (f) {
            if ((f->flags() & 0x40000000) != 0 && (m_dwFlags & 1) == 0) *allow = 0;
            if (objutil::pageChar(s) == 'F' && (f->flags() & 0x40) != 0) *allow = 0;
        }
        if ((m_dwFlags & 0x2000000) != 0) {
            s.host().Eng_Api48_DropHeldObject("dropzone1");
            *allow = 0;
        }
        u32 food = m_dwFlags & 0x20000000;
        if ((food == 0 || !f || (f->flags() & 0x40000000) == 0) && (m_dwFlags & 0x40) != 0) {
            if (food != 0) {
                s.ShowMessage(1, 0x15c, 0xffff, 0);
                s.g.describeSuppressed = 1;
            }
            removeFromScene(&s);
            *allow = 0;
            return 0;
        }
    } else {
        if (key != 0x53) return 0;
        if ((m_dwFlags & 0x800) != 0) {
            s.ShowMessage(1, 0xb2, 0xffff, 0);
            *allow = 0;
        }
    }
    return 0;
}

// dll:10029190 (FUN_10029190)
int GameObj::setHolding(Self& s) {
    s.m_pObjHolding.set(this);
    describe(s);
    return 1;
}

// dll:100291b0 (FUN_100291b0)
int GameObj::clearHolding(Self& s) {
    s.m_pObjHolding.clear();
    return 1;
}

// dll:10028df0 (FUN_10028df0)
void GameObj::dumpBase(std::vector<std::string>& lines) const {
    lines.push_back("\n");
    lines.push_back(name());
    lines.push_back(m_bInScene == 0 ? "m_bExists \t\tFALSE" : "m_bExists \t\tTRUE");
    lines.push_back(m_bHeld == 0 ? "m_bCarrying \t\tFALSE" : "m_bCarrying \t\tTRUE");
    char b[64];
    std::snprintf(b, sizeof b, "m_dWeight \t\t%f", m_dWeight);
    lines.push_back(b);
    std::snprintf(b, sizeof b, "m_dwTimeLastUpdate \t\t%d", m_dwTimeStamp);
    lines.push_back(b);
}

void GameObj::dump(std::vector<std::string>& lines) const { dumpBase(lines); }

void GameObj::putD(ObjMembers& out, const char* name, double v) { out.emplace_back(name, pyRepr(v)); }
void GameObj::putI(ObjMembers& out, const char* name, long long v) { out.emplace_back(name, std::to_string(v)); }
void GameObj::putX(ObjMembers& out, const char* name, u32 v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%x", v);
    out.emplace_back(name, b);
}
void GameObj::putS(ObjMembers& out, const char* name, const std::string& v) { out.emplace_back(name, v); }
void GameObj::putP(ObjMembers& out, const char* name, const Item* v) { out.emplace_back(name, v ? v->name() : std::string()); }

void GameObj::commonMembers(ObjMembers& out) const {
    putS(out, "name", name());
    putI(out, "m_dwNameStringId", m_dwNameStringId);
    putX(out, "m_dwFlags", m_dwFlags);
    putX(out, "m_dwPickupEvent", m_dwPickupEvent);
    putD(out, "m_dWeight", m_dWeight);
    putI(out, "m_dwTimeStamp", m_dwTimeStamp);
    putI(out, "m_bInScene", m_bInScene);
    putI(out, "m_bHeld", m_bHeld);
    putS(out, "m_szVariant", variant());
}

void GameObj::dumpMembers(ObjMembers& out) const { commonMembers(out); }

void GameObj::poke(u32 offset, unsigned bytes, u32 value, PokeOp op) {
    if (bytes == 4 && offset == 0x14) { m_dwFlags = op == PokeOp::Or ? (m_dwFlags | value) : value; return; }
    if (bytes == 4 && offset == 0x18 && op == PokeOp::Set) { m_dwPickupEvent = value; return; }
    if (bytes == 4 && offset == 0x2c && op == PokeOp::Set) { m_bInScene = static_cast<i32>(value); return; }   // dll:100092d0 (mikememo: baby1-7 back in the scene)
    throw Error(className() + " '" + name() + "': no member at object offset 0x" + [&] {
        char b[16]; std::snprintf(b, sizeof b, "%x", offset); return std::string(b); }());
}

u32 GameObj::peek(u32 offset, unsigned bytes) const {
    if (bytes == 4 && offset == 0x14) return m_dwFlags;
    if (bytes == 4 && offset == 0x18) return m_dwPickupEvent;
    if (bytes == 4 && offset == 0x2c) return static_cast<u32>(m_bInScene);
    throw Error(className() + " '" + name() + "': no member at object offset " + std::to_string(offset));
}

// FUN_10028d00 dll:10028d00: holder/variant name as dword, dword, word, byte (11 bytes), then +0x2c, +0x30, weight, time stamp
void GameObj::saveCommon(SlotWriter& w) const {
    w.writeBytes(m_szVariant.data(), 11);
    w.writeI32(m_bInScene);
    w.writeI32(m_bHeld);
    w.writeF64(m_dWeight);
    w.writeI32(m_dwTimeStamp);
}

// FUN_10028d70 dll:10028d70 (the member names are the ones of saveblob.md section 5.0)
void GameObj::loadCommon(SlotReader& r) {
    std::vector<u8> v = r.readStrRaw("m_szHolderName", 11);
    std::memcpy(m_szVariant.data(), v.data(), 11);
    m_bInScene = r.readI32("unk_2c");
    m_bHeld = r.readI32("unk_30");
    m_dWeight = r.readF64("m_dWeight");
    m_dwTimeStamp = r.readI32("m_dwTimeStamp");
}

void GameObj::saveSlot(SlotWriter& w) const { saveCommon(w); }

void GameObj::loadSlot(SlotReader& r) {
    loadCommon(r);
    r.finishPad();
}

static GameObj& asObject(Item* item) { return as<GameObj>(item); }

void objectPoke(Item* item, u32 offset, unsigned bytes, u32 value, PokeOp op) { asObject(item).poke(offset, bytes, value, op); }

void objectPokeString(Item* item, u32 offset, const char* s) {
    if (offset != 0x34) throw Error("builder string store at an unknown object offset");
    asObject(item).setVariant(s);
}

void objectVirtualCall(Item* item, u32 vtableOffset) {
    if (vtableOffset != 0x94) throw Error("builder virtual call at an unknown vtable offset");
    asObject(item).removeFromScene(nullptr);
}

// dll:1001e380 Event_OnGameObject
void dispatchObjectEvent(World& w, Self& s, const std::string& name, int code, const std::string& extra) {
    GameObj* o = w.findObject(name.c_str());
    if (!o) return;
    if (!o->inScene()) {
        std::string text = "BiosGame.dll: Undead object - " + o->name();
        w.state().message = text;
        w.log(text);
    }
    std::string hotspot = objutil::upper(extra);
    switch (code) {
    case 1:
        o->pickUp(s);                                // +0x44
        s.ShowMessageDeferred(0x172);
        return;
    case 2:
        o->onStashed(s);                             // +0x34
        s.ShowMessageDeferred(0x173);
        return;
    case 3:
        o->onTaken(s);                               // +0x3c
        s.ShowMessageDeferred(0x172);
        return;
    case 4:
        o->use(s);                                   // +0x2c
        return;
    case 5:
        if ((o->flags() & 0x100000) != 0) s.host().playSound("sounds\\objects\\extempty.wav");
        return;
    case 6:
        o->onDropped(s, hotspot);                    // +0x48
        return;
    case 7:
        o->onUsedOnHotspot(s, hotspot);              // +0x54
        return;
    case 8:
        o->onWorn(s);                                // +0x4c
        return;
    case 9:
        o->onUnworn(s);                              // +0x50
        return;
    case 10:
        o->onHoldingImage(s);                        // +0x40
        return;
    case 0xb: case 0xd: case 0xe: case 0xf:
        return;
    case 0xc:
        o->onBarToHand(s);                           // +0x38
        return;
    case 0x10: {
        char c = objutil::pageChar(s);
        if (c != 'A' && c != 'F' && c != 'N') {
            s.ShowMessage(1, 0x136, 0xffff, 0);
            return;
        }
        s.ShowMessage(1, 0x15d, 0xffff, 0);
        return;
    }
    default: {
        std::string text = "BiosGame.dll: mystery game object event - " + std::to_string(code) + ", " + o->name();
        w.state().message = text;
        w.log(text);
        return;
    }
    }
}

namespace cworld {

// dll:10014650 CWorld::UpdateNextObject (the cursor, world+0xb4, is the 1-based position of the next node, 0 = none)
bool updateNextObject(World& w, Self& s, i32 now) {
    GameState& gs = w.state();
    const std::vector<Item*>& list = w.list(Table::Objects);
    if (gs.objectCursor == 0) {
        if (list.empty()) return false;
        gs.objectCursor = 1;
    }
    size_t pos = gs.objectCursor - 1;
    gs.objectCursor = pos + 1 < list.size() ? pos + 2 : 0;
    as<GameObj>(list[pos]).update(s, now);           // vtable +0x68
    return gs.objectCursor != 0;
}

// dll:100146a0 CWorld::UpdateAllObjects
bool updateAllObjects(World& w, Self& s, i32 now) {
    w.state().objectCursor = 0;
    while (updateNextObject(w, s, now)) {}
    return true;
}

} // namespace cworld

} // namespace biosys::game
