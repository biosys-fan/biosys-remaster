#include "game/obj_gear.h"
#include "game/obj_common.h"
#include "game/obj_food.h"
#include "game/biomecel.h"
#include "game/site.h"
#include "game/soundfx.h"
#include "game/watertank.h"
#include "game/world_ctors.gen.h"
#include <cmath>
#include <cstdio>

namespace biosys::game {

// dll:10028c20
ObjMisc::ObjMisc(const char* name, i32 nameStringId, u32 flags, double weight)
    : GameObj(0x10028c20, "CObject", name, nameStringId) {
    m_dwFlags = flags;       // +0x14
    m_dWeight = weight;      // +0x24/+0x20
}

// dll:1005a500
ObjBabySynth::ObjBabySynth(const char* name, i32 nameStringId)
    : GameObj(0x1005a500, "CBabySynth", name, nameStringId) {
    m_dwFlags |= 0x80040;    // +0x14
    m_wState = 1;            // +0x40
}

// dll:1005a600
void ObjBabySynth::saveSlot(SlotWriter& w) const {
    saveCommon(w);
    w.writeI32(m_dwTimeOfDeath);
    w.writeU16(m_wState);
}

// dll:1005a630
void ObjBabySynth::loadSlot(SlotReader& r) {
    loadCommon(r);
    m_dwTimeOfDeath = r.readI32("unk_44");
    m_wState = r.readU16("unk_40");
    r.finishPad();
}

void ObjBabySynth::poke(u32 offset, unsigned bytes, u32 value, PokeOp op) {
    if (bytes == 2 && offset == 0x40 && op == PokeOp::Set) { m_wState = static_cast<u16>(value); return; }
    GameObj::poke(offset, bytes, value, op);
}

u32 ObjBabySynth::peek(u32 offset, unsigned bytes) const {
    if (bytes == 2 && offset == 0x40) return m_wState;
    return GameObj::peek(offset, bytes);
}

// dll:1001af20
ObjFishingRod::ObjFishingRod(const char* name, i32 nameStringId)
    : GameObj(0x1001af20, "CFishingRod", name, nameStringId) {
    m_dwFlags |= 0x800400;                       // +0x14
    m_dWeight = dwordsToDouble(0x40180000);      // +0x20 6.0
    m_dwState = 0;                               // +0x40
}

// dll:1001b030
void ObjFishingRod::saveSlot(SlotWriter& w) const {
    saveCommon(w);
    w.writeI32(m_dwState);
}

// dll:1001b060
void ObjFishingRod::loadSlot(SlotReader& r) {
    loadCommon(r);
    m_dwState = r.readI32("unk_40");
    r.finishPad();
}

// dll:1005a170
ObjBlowpipe::ObjBlowpipe(const char* name, i32 nameStringId)
    : GameObj(0x1005a170, "CBlowpipe", name, nameStringId) {
    m_dWeight = dwordsToDouble(0x40140000);      // 5.0
    m_dwFlags |= 0x400;
}

// dll:10006200
ObjBlowpipePick::ObjBlowpipePick(const char* name, i32 nameStringId)
    : GameObj(0x10006200, "CBlowpipePick", name, nameStringId) {
    m_dWeight = dwordsToDouble(0x40240000);      // 10.0
    m_dwFlags |= 0x40400;
}

// dll:100268d0
ObjTracker::ObjTracker(const char* name, i32 nameStringId)
    : GameObj(0x100268d0, "CTracker", name, nameStringId) {
    m_dwFlags |= 0x100;
    m_dWeight = dwordsToDouble(0x40140000);      // 5.0
    m_dwUnk40 = -1;
}

// dll:1005bcf0
ObjCompass::ObjCompass(const char* name, i32 nameStringId)
    : GameObj(0x1005bcf0, "CCompass", name, nameStringId) {
    m_dWeight = dwordsToDouble(0x3ff00000);      // 1.0
}

// dll:100191e0
ObjSurvivalBag::ObjSurvivalBag(const char* name, i32 nameStringId)
    : GameObj(0x100191e0, "CSurvivalBag", name, nameStringId) {
    m_dWeight = dwordsToDouble(0x40140000);      // 5.0
}

// dll:10002d90
ObjPoncho::ObjPoncho(const char* name, i32 nameStringId)
    : GameObj(0x10002d90, "CPoncho", name, nameStringId) {
    m_dwFlags |= 0x200;
    m_dWeight = dwordsToDouble(0x40100000);      // 4.0
}

// dll:10022f70
ObjTorch::ObjTorch(const char* name, i32 nameStringId)
    : GameObj(0x10022f70, "CTorch", name, nameStringId) {
    m_pCylinder = nullptr;                       // +0x44
    m_dwFlags |= 0x2000800;
    m_dwUnk40 = -1;
}

// dll:1001d1a0
ObjGoggles::ObjGoggles(const char* name, i32 nameStringId)
    : GameObj(0x1001d1a0, "CGoggles", name, nameStringId) {
    m_bOn = 0;                                   // +0x4c
    m_dWeight = dwordsToDouble(0x40100000);      // +0x20 4.0
    m_dCharge = dwordsToDouble(0x40590000);      // +0x40 100.0
    m_byShownPowerState = 0;                     // +0x49 (+0x48 is not written)
}

// dll:1001d2b0
void ObjGoggles::saveSlot(SlotWriter& w) const {
    saveCommon(w);
    w.writeI32(m_bOn);
    w.writeU8(m_byPowerState);
    w.writeF64(m_dCharge);
}

// dll:1001d2f0
void ObjGoggles::loadSlot(SlotReader& r) {
    loadCommon(r);
    m_bOn = r.readI32("unk_4c");
    m_byPowerState = r.readU8("unk_48");
    m_dCharge = r.readF64("m_dCharge");
    r.finishPad();
}

// dll:10076a10
ObjOxyMask::ObjOxyMask(const char* name, i32 nameStringId)
    : GameObj(0x10076a10, "COxyMask", name, nameStringId) {
    m_dWeight = dwordsToDouble(0x40140000);          // +0x20 5.0
    m_dFilter = dwordsToDouble(0x3fef5c28, 0xf5c28f5c);   // +0x40 0.98
}

// dll:10076aa0
void ObjOxyMask::saveSlot(SlotWriter& w) const {
    saveCommon(w);
    w.writeF64(m_dFilter);
}

// dll:10076ad0
void ObjOxyMask::loadSlot(SlotReader& r) {
    loadCommon(r);
    m_dFilter = r.readF64("m_dFilter");
    r.finishPad();
}

// dll:100629d0
ObjOxyCylinder::ObjOxyCylinder(const char* name, i32 nameStringId, Item* fx)
    : GameObj(0x100629d0, "COxyCylinder", name, nameStringId) {
    m_pFx = fx;                                  // +0x40
    m_dContents = 0;                             // +0x48
    m_bValveOpen = 0;                            // +0x50
    m_dwUnk54 = 0;                               // +0x54
    m_bCarried = 0;                              // +0x44 (not written by the constructor)
    m_dWeight = dwordsToDouble(0x403e0000);      // +0x20 30.0
    m_dwFlags |= 0x800;
}

// dll:10062af0
void ObjOxyCylinder::saveSlot(SlotWriter& w) const {
    saveCommon(w);
    w.writeF64(m_dContents);
    w.writeI32(m_dwUnk54);
    w.writeI32(m_bValveOpen);
}

// dll:10062b40
void ObjOxyCylinder::loadSlot(SlotReader& r) {
    loadCommon(r);
    m_dContents = r.readF64("m_dContents");
    m_dwUnk54 = r.readI32("unk_54");
    m_bValveOpen = r.readI32("unk_50");
    r.finishPad();
}

// ---------------------------------------------------------------------------------------------------------------------
// behaviour

namespace {

// FUN_1005a4c0 dll:1005a4c0: DAT_100bb90c = the first thorn (the list at world+0xf0) that is held, or NULL
void findHeldThorn(Self& s) {
    s.g.blowTarget = nullptr;
    for (Item* it : s.world().related(Rel::WorldList_f0, nullptr)) {
        GameObj& o = as<GameObj>(it);
        if (o.held()) {
            s.g.blowTarget = &o;
            return;
        }
    }
}

// The feature display the wearables refresh: `if (feature && (feature+0x13 & 0x80)) feature->vtable+0x5c(1)`
void redisplayFeature(Self& s) {
    CFeature* f = objutil::feature(s);
    if (f && (f->flags() & 0x80000000u) != 0) f->display(1);
}

}  // namespace

// The writes of COxyCylinder / CAlarmClock into the sound FX they move to the player's page (the FX members are the original's
// +4 page name, +0x8c site, +0x94 biome, +0x18 playing flag)
struct ObjFxAccess {
    static void place(Item* fx, Self& s) {
        CSoundFX& f = as<CSoundFX>(fx);
        f.m_szPage = std::string(reinterpret_cast<const char*>(s.m_szLocation.data()));
        f.m_pSite = dynamic_cast<CSite*>(s.m_pSite.item);
        f.m_pBiome = const_cast<Biome*>(s.m_pBiome);
    }
    // the +0x20 / +0x80 form: only when the FX is not on the player's page already
    static void follow(Item* fx, Self& s) {
        CSoundFX& f = as<CSoundFX>(fx);
        if (f.m_szPage == std::string(reinterpret_cast<const char*>(s.m_szLocation.data()))) return;
        place(fx, s);
        f.m_bPlaying = 0;
    }
    static CSite* site(Item* fx) { return as<CSoundFX>(fx).m_pSite; }
};
void objFxPlace(Item* fx, Self& s) { ObjFxAccess::place(fx, s); }
void objFxFollow(Item* fx, Self& s) { ObjFxAccess::follow(fx, s); }
CSite* objFxSite(Item* fx) { return ObjFxAccess::site(fx); }

// ---- CBabySynth (vtable 100929c8)

// dll:1005a660
int ObjBabySynth::update(Self& s, i32 now) {
    if (m_wState == 1) {
        if (m_bHeld != 0 && s.m_pPath == nullptr) {                // DAT_100c1f8c
            s.host().playObjectUseAnimation("anims\\obj_use\\synthrun.tan", "sounds\\objects\\squeal.wav", 1, 1, 0x154, 0, 1, 0x3c,
                                            nullptr, -2);
            removeFromScene(&s);
        }
    } else if (m_wState == 2) {
        if (3600.0 < static_cast<double>(static_cast<u32>(now - m_dwTimeOfDeath))) {
            m_wState = 3;
            changeVariation(s, "rotten", -2);
        }
    } else if (m_wState == 3 && 86400.0 < static_cast<double>(static_cast<u32>(now - m_dwTimeOfDeath))) {
        if (!objutil::iequals(name(), "babylab")) {
            removeFromScene(&s);
            if (s.host().Eng_Api52_ReturnGameObject(name().c_str())) m_wState = 1;
        }
    }
    return 1;
}

// dll:1005a770
int ObjBabySynth::use(Self& s) {
    if (m_wState == 1) {
        m_dwTimeOfDeath = s.now();
        safeSetVariant(s, "dead", -2);
        m_wState = 2;
        return 1;
    }
    if (1 < m_wState) {
        if (3 < m_wState) return 1;
        s.Eat(20.0, 10.0, 5.0, 0xfa);
        removeFromScene(&s);
    }
    return 1;
}

// dll:1005a7f0
int ObjBabySynth::onKey(Self& s, int key, u32* allow) {
    if (s.m_bAwake == 0) *allow = 0;
    if (key == 0x44) {
        if (m_wState == 1) {
            *allow = 0;
            s.host().playObjectUseAnimation("anims\\obj_use\\synthrun.tan", "squeal.wav", 1, 1, 0x154, 0, 1, 0x3c, nullptr, -2);
            removeFromScene(&s);
            return 1;
        }
    } else {
        if (key != 0x53) return 1;
        if (m_wState == 1) {
            *allow = 0;
            s.host().playObjectUseAnimation("anims\\obj_use\\synthrun.tan", "squeal.wav", 1, 1, 0x154, 0, 1, 0x3c, nullptr, -2);
            removeFromScene(&s);
        }
    }
    return 1;
}

void ObjBabySynth::dumpMembers(ObjMembers& out) const {
    commonMembers(out);
    putI(out, "m_wState", m_wState);
    putI(out, "m_dwTimeOfDeath", m_dwTimeOfDeath);
}

// ---- CFishingRod (vtable 10090d10)

// dll:1001b090
int ObjFishingRod::use(Self& s) {
    const std::string v = variant();
    if (!objutil::iequals(v, "caught")) {
        if (!objutil::iequals(v, "fishing") && !objutil::iequals(v, "fishnob")) return 1;
        if (m_dwState == 0) safeSetVariant(s, "NoBait", -2);
        else safeSetVariant(s, "Bait", -2);
        return 1;
    }
    GameObj* fish = s.world().findObject(s.g.fishObject.c_str());
    if (!fish) return 1;
    if (fish->weight() + s.m_dWeightCarried <= s.m_dEnergy) {
        s.host().Eng_DoHotspotAction(s.g.fishObject.c_str(), -1);
        s.m_pObjHolding.set(this);                                   // DAT_100c1fa0 = this
        safeSetVariant(s, "NoBait", -2);
        m_dwState = 0;
        return 1;
    }
    s.ShowMessage(1, 0x35, 0xffff, 0);
    return 1;
}

// dll:1001b190
int ObjFishingRod::onUsedOnHotspot(Self& s, const std::string& hotspot) {
    const std::string v = variant();
    if (objutil::iequals(v, "caught") || objutil::iequals(v, "fishing") || objutil::iequals(v, "fishnob")) return use(s);   // +0x2c
    bool found = false;
    safeSetVariant(s, m_dwState == 0 ? "fishnob" : "fishing", -2);
    s.g.fishHeading = static_cast<i32>(s.m_ulCompassHeading);            // DAT_100c428c = DAT_100c1e30
    CSite* site = dynamic_cast<CSite*>(s.m_pSite.item);
    GameObj* fish = nullptr;
    std::string fishName;
    int i = 1;
    do {
        do {
            fishName = site->name() + "fish_" + std::to_string(i);       // "%sfish_%d"
            i++;
            fish = s.world().findObject(fishName.c_str());
            if (!fish) goto notFound;
        } while (as<ObjFood>(fish).state() != 2);
    } while (objutil::chancePercent(s, 30.0));
    s.g.fishObject = fishName;
    found = true;
    goto decided;
notFound:
    s.g.fishObject.clear();
decided:
    s.g.fishBiteTime = -1;                                               // DAT_100a1378
    if (m_dwState == 0) {
        s.ShowMessage(1, 0x5e, 0xffff, 0);
        return 1;
    }
    if (!found) return 1;
    if ((site->flags() & 0x2000000) == 0) {
        double avg = as<CBiomeCel>(s.m_pCel).plantHealthAvg();
        if (0.3 < avg) s.g.fishBiteTime = static_cast<i32>(630.0 / (avg - 0.3) + static_cast<double>(static_cast<u32>(s.now())));
    } else {
        const COcean* oc = dynamic_cast<const COcean*>(s.world().state().ocean);
        if (!oc) throw Error("CFishingRod: the world has no ocean");
        if (0.3 < oc->health()) s.g.fishBiteTime = static_cast<i32>(630.0 / (oc->health() - 0.3) + static_cast<double>(static_cast<u32>(s.now())));
    }
    return 1;
}

// dll:1001b400
int ObjFishingRod::onSector(Self& s, int sector) {
    if (s.g.fishHeading == sector) return 1;
    const std::string v = variant();
    if (!objutil::iequals(v, "fishing") && !objutil::iequals(v, "fishnob") && !objutil::iequals(v, "caught")) return 1;
    if (m_dwState == 0) changeVariation(s, "NoBait", -2);
    else changeVariation(s, "Bait", -2);
    return 1;
}

// dll:1001b490
int ObjFishingRod::update(Self& s, i32 now) {
    if (s.m_pObjHolding.item != this) return 1;
    if (objutil::iequals(variant(), "fishing")) {
        if (s.g.fishBiteTime < now) {
            changeVariation(s, "caught", -2);
            s.host().playSound("sounds\\objects\\biting.wav");
            return 1;
        }
    } else if (objutil::iequals(variant(), "caught")) {
        changeVariation(s, "fishnob", -2);
        m_dwState = 0;
        s.g.fishBiteTime = -1;
    }
    return 1;
}

// dll:1001b3b0
int ObjFishingRod::setHolding(Self& s) {
    GameObj::setHolding(s);
    if (m_dwState == 0) safeSetVariant(s, "NoBait", -2);
    else safeSetVariant(s, "Bait", -2);
    return 1;
}

void ObjFishingRod::dumpMembers(ObjMembers& out) const {
    commonMembers(out);
    putI(out, "m_dwState", m_dwState);
}

// ---- CBlowpipe / CBlowpipePick (vtables 10092930 / 1008f7e8)

// dll:1005a2d0
int ObjBlowpipe::use(Self& s) {
    if (objutil::iequals(variant(), "mouth")) {
        findHeldThorn(s);
        if (s.g.blowTarget) {
            s.host().playObjectUseAnimation("anims\\obj_use\\blowfire.tan", "sounds\\objects\\blowpipe.wav", 1, 0, 0xf3, 0x8f, 1,
                                            0x42, nullptr, -2);
            s.g.blowTarget->removeFromScene(&s);
            return 1;
        }
        s.ShowMessage(1, 0x5f, 0xffff, 0);
    } else if (!s.m_pAirMask.item) {
        safeSetVariant(s, "mouth", -2);
        return 1;
    }
    return 1;
}

// dll:1005a370
int ObjBlowpipe::onUsedOnHotspot(Self& s, const std::string& hotspot) {
    if (!objutil::iequals(variant(), "mouth")) {
        if (!s.m_pAirMask.item) safeSetVariant(s, "mouth", -2);
        return 1;
    }
    findHeldThorn(s);
    if (!s.g.blowTarget) {
        s.ShowMessage(1, 0x5f, 0xffff, 0);
        return 1;
    }
    s.host().playObjectUseAnimation("anims\\obj_use\\blowfire.tan", "sounds\\objects\\killbird.wav", 1, 0, 0xf3, 0x8f, 1, 0x42,
                                    nullptr, -2);
    s.g.blowTarget->removeFromScene(&s);
    if (hotspot.find("SYNTH") != std::string::npos) {
        s.host().CSynthHandler_Attack();                                 // dll:10008a30 CSynthHandler::Attack (story_handlers.cpp)
        return 1;
    }
    std::string target = hotspot.empty() ? std::string() : hotspot.substr(1);
    s.host().CPage_FxNamedStop(s.m_pPage, target.c_str());               // dll:1000fb30 FUN_1000fb30(DAT_100c1f80, name) (page FX code)
    GameObj* o = s.world().findObject(target.c_str());
    if (!o) return 1;
    if (s.m_dEnergy < o->weight() + s.m_dWeightCarried) {
        s.ShowMessage(1, 0x35, 0xffff, 0);
        o->removeFromScene(&s);
        return 1;
    }
    s.host().Eng_DoHotspotAction(target.c_str(), -1);
    return 1;
}

// dll:1005a280
int ObjBlowpipe::setHolding(Self& s) {
    GameObj::setHolding(s);
    safeSetVariant(s, "hold", -2);
    s.host().Eng_Api6D_SetScrollOptions(0, 1);
    return 1;
}

// dll:1005a2b0
int ObjBlowpipe::clearHolding(Self& s) {
    GameObj::clearHolding(s);
    s.host().Eng_Api6D_SetScrollOptions(1, 1);
    return 1;
}

// dll:10006350
int ObjBlowpipePick::onUsedOnHotspot(Self& s, const std::string& hotspot) {
    CFeature* f = objutil::feature(s);
    if (objutil::iequals(hotspot, "yHoney") && f) {
        f->onApply(*this, hotspot);
    } else if (hotspot.find("SYNTH") == std::string::npos) {
        std::string local = hotspot;
        if (!local.empty()) local[0] = 'R';
        GameObj* o = s.world().findObject(local.empty() ? "" : local.c_str() + 1);
        if (!o) return 1;
        if (s.m_dEnergy < o->weight() + s.m_dWeightCarried) {
            s.ShowMessage(1, 0x35, 0xffff, 0);
            return 1;
        }
        s.host().Eng_DoHotspotAction(local.c_str(), -1);
        if (!local.empty()) local[0] = 'O';
        s.host().CEngineFX_HotspotEnable(local.c_str(), 0, -1);
    } else {
        s.host().CSynthHandler_Attack();                                 // dll:10008a30 CSynthHandler::Attack (story_handlers.cpp)
    }
    return 1;
}

// dll:10006310
int ObjBlowpipePick::setHolding(Self& s) {
    GameObj::setHolding(s);
    s.host().Eng_Api6D_SetScrollOptions(0, 1);
    return 1;
}

// dll:10006330
int ObjBlowpipePick::clearHolding(Self& s) {
    GameObj::clearHolding(s);
    s.host().Eng_Api6D_SetScrollOptions(1, 1);
    return 1;
}

// ---- CTracker (vtable 100918c0)

// dll:10026a30: the blip of the stalking synth on the tracker's screen
int ObjTracker::redraw(Self& s, int full) {
    PlayerHost& h = s.host();
    if (h.synthState() == 1) return 1;
    double dist = h.synthDistance();
    if (20.0 < dist) return 1;
    int w = 0, startX = 0, centreX = 0;
    h.Eng_GetPanoramaInfo(w, startX, centreX);
    double angle = (static_cast<double>(static_cast<u32>(h.synthX())) - static_cast<double>(static_cast<u32>(centreX))) /
                   static_cast<double>(static_cast<u32>(w)) * 6.2831854;
    double sx = std::sin(angle) * dist * 3.75 + 430.0;
    double sy = -(std::cos(angle) * dist) * 3.75 + 280.0;
    int handle = -1;
    int y = static_cast<int>(sy);
    int x = static_cast<int>(sx);
    h.CEngineFX_PlayAnim("anims\\obj_use\\blip.tan", &handle, -1, 0x42, x, y, 0, 1, "sounds\\objects\\tracping.wav");
    return 1;
}

// dll:100269e0
int ObjTracker::setHolding(Self& s) {
    GameObj::setHolding(s);
    s.host().playLoopedWav("sounds\\objects\\trckswp1.wav", 0, &m_dwUnk40);
    return 1;
}

// dll:10026a10
int ObjTracker::clearHolding(Self& s) {
    GameObj::clearHolding(s);
    s.host().closeSoundHandle(&m_dwUnk40);
    return 1;
}

void ObjTracker::dumpMembers(ObjMembers& out) const {
    commonMembers(out);
    putI(out, "m_dwUnk40", m_dwUnk40);
}

// ---- CCompass (vtable 10092d78)

// dll:1005beb0
int ObjCompass::onSector(Self& s, int sector) {
    s.host().CEngineFX_ChangeHoldingImagesGroup(name().c_str(), sector);
    return 0;
}

// dll:1005be80
int ObjCompass::setHolding(Self& s) {
    GameObj::setHolding(s);
    onSector(s, static_cast<int>(s.m_ulCompassHeading));
    return 1;
}

// dll:1005bd70: the sector from the panorama position on the sites with a panorama (pages B C D O P R S T W)
void ObjCompass::onPageEntered(Self& s, const std::string& page) {
    switch (objutil::pageChar(s)) {
    case 'B': case 'C': case 'D': case 'O': case 'P': case 'R': case 'S': case 'T': case 'W': {
        int w = 0, startX = 0, centreX = 0;
        s.host().Eng_GetPanoramaInfo(w, startX, centreX);
        if (w == 0) return;                          // the wrapper's failure result (the page is no panorama)
        double v = (static_cast<double>(static_cast<u32>(centreX)) -
                    (static_cast<double>(static_cast<u32>(startX)) - 0.0625 * static_cast<double>(static_cast<u32>(w)))) /
                   static_cast<double>(static_cast<u32>(w)) * 8.0;
        if (v < 0.0) v = v + 8.0;
        if (!(v < 8.0)) v = v - 8.0;
        onSector(s, static_cast<int>(v) + 1);
        return;
    }
    default:
        return;
    }
}

// ---- CSurvivalBag (vtable 10090a40)

// dll:100192f0
int ObjSurvivalBag::use(Self& s) {
    if (!objutil::iequals(variant(), "rolled")) {
        safeSetVariant(s, "rolled", -2);
        return 1;
    }
    if (s.CanSleep() == 0) {
        s.ShowMessage(1, 199, 0xffff, 0);
        return 0;
    }
    safeSetVariant(s, "unrolled", -2);
    s.m_bFallAsleepOnIdle = 1;                                           // DAT_100c2490
    return 1;
}

// dll:10019260
int ObjSurvivalBag::pickUp(Self& s) {
    GameObj::pickUp(s);
    s.m_bHasSleepingBag = 1;                                             // DAT_100c1e70
    return 1;
}

// dll:10019370
int ObjSurvivalBag::onDropped(Self& s, const std::string& hotspot) {
    GameObj::onDropped(s, hotspot);
    s.m_bHasSleepingBag = 0;
    return 1;
}

// dll:10019280
int ObjSurvivalBag::onKey(Self& s, int key, u32* allow) {
    GameObj::onKey(s, key, allow);
    if (key == 0x20) {
        char c = objutil::pageChar(s);
        if (c != 'F' && c != 'A' && c != 'N') {
            s.host().addJiggle(0, 0x400, 0, 0x14);
            s.host().jigglePage(0, 0, -1);
            return 1;
        }
        *allow = 0;
    }
    return 1;
}

// dll:10019390
int ObjSurvivalBag::setHolding(Self& s) {
    GameObj::setHolding(s);
    safeSetVariant(s, "rolled", -2);
    return 1;
}

// ---- CPoncho (vtable 1008f158)

// dll:10002e20
int ObjPoncho::onWorn(Self& s) {
    GameObj::onWorn(s);
    s.host().playSound("sounds\\objects\\zipup.wav");
    s.m_bWearingPoncho = 1;                                              // DAT_100c1e6c
    redisplayFeature(s);
    return 1;
}

// dll:10002e70
int ObjPoncho::onUnworn(Self& s) {
    s.m_bWearingPoncho = 0;
    redisplayFeature(s);
    return 1;
}

// ---- CTorch (vtable 10091298)

// dll:100230b0
void ObjTorch::afterLoad(Self& s) {
    m_dwUnk40 = -1;
    if (objutil::iequals(variant(), "torchon")) s.host().playLoopedWav("sounds\\objects\\torch.wav", 0, &m_dwUnk40);
}

// dll:100231e0
int ObjTorch::use(Self& s) {
    if (objutil::iequals(variant(), "torchon")) {
        s.host().Eng_StopSound(m_dwUnk40);
        m_dwUnk40 = -1;
        safeSetVariant(s, "torchoff", -2);
        return 1;
    }
    if (m_pCylinder && 0.0 < m_pCylinder->contents()) {
        s.host().playLoopedWav("sounds\\objects\\torch.wav", 0, &m_dwUnk40);
        safeSetVariant(s, "torchon", -2);
        return 1;
    }
    s.ShowMessage(1, 0xb4, 0xffff, 0);
    return 1;
}

// dll:10023100
int ObjTorch::onUsedOnHotspot(Self& s, const std::string& hotspot) {
    if (!m_pCylinder || m_pCylinder->contents() <= 0.0) {
        s.ShowMessage(1, 0xb4, 0xffff, 0);
        return 1;
    }
    m_pCylinder->setContents(m_pCylinder->contents() - 1.0);
    if (m_pCylinder->contents() < 0.0) m_pCylinder->setContents(0.0);
    if (!objutil::iequals(hotspot, "yDoor")) return 1;           // "yFire": the original compares and does nothing
    s.host().closeSound(m_dwUnk40);
    m_dwUnk40 = -1;
    if (CFeature* f = objutil::feature(s)) f->onApply(*this, hotspot);
    removeFromScene(&s);
    return 1;
}

// dll:100232d0
int ObjTorch::update(Self& s, i32 now) {
    i32 old = m_dwTimeStamp;
    m_dwTimeStamp = now;
    if (m_szVariant[6] == 'n') {                                         // "torchon"
        if (!m_pCylinder) {
            changeVariation(s, "torchoff", -2);
            s.host().Eng_StopSound(m_dwUnk40);
            m_dwUnk40 = -1;
        } else {
            m_pCylinder->setContents(static_cast<double>(static_cast<u32>(now - old)) * -0.0005555555555555556 + m_pCylinder->contents());
            if (m_pCylinder->contents() <= 0.0) {
                m_pCylinder->setContents(0.0);
                s.host().Eng_StopSound(m_dwUnk40);
                m_dwUnk40 = -1;
                changeVariation(s, "torchoff", -2);
            }
        }
    }
    return 1;
}

// dll:10023280
int ObjTorch::setHolding(Self& s) {
    GameObj::setHolding(s);
    safeSetVariant(s, "torchoff", -2);
    return 1;
}

// dll:100232a0
int ObjTorch::clearHolding(Self& s) {
    GameObj::clearHolding(s);
    s.host().closeSound(m_dwUnk40);
    m_dwUnk40 = -1;
    return 1;
}

void ObjTorch::dumpMembers(ObjMembers& out) const {
    commonMembers(out);
    putI(out, "m_dwUnk40", m_dwUnk40);
    putP(out, "m_pCylinder", m_pCylinder);
}

// ---- CGoggles (vtable 10091020)

// dll:1001d780
void ObjGoggles::setPower(double v) {
    m_dCharge = v;
    m_dCharge = m_dCharge < 0.0 ? 0.0 : (m_dCharge > 100.0 ? 100.0 : m_dCharge);   // Clamp(&power, 0, 100)
    m_byPowerState = 0;
}

// dll:1001d640
void ObjGoggles::calcPowerState() {
    if (m_dCharge == 0.0) m_byPowerState = 1;
    else if (m_dCharge < 20.0) m_byPowerState = 2;
    else if (m_dCharge < 40.0) m_byPowerState = 3;
    else if (m_dCharge < 60.0) m_byPowerState = 4;
    else if (m_dCharge < 80.0) m_byPowerState = 5;
    else m_byPowerState = 6;
}

// dll:1001d6b0
void ObjGoggles::showPower(Self& s) {
    const char* image = "";
    switch (m_byPowerState) {
    case 1: image = "power0"; break;
    case 2: image = "power1"; break;
    case 3: image = "power2"; break;
    case 4: image = "power3"; break;
    case 5: image = "power4"; break;
    case 6: image = "power5"; break;
    default: {
        std::string text = "duff goggles power state, " + std::to_string(m_byPowerState);
        s.world().state().message = text;
        s.world().log(text);
        // ORIGINAL BUG: the image name stays uninitialised here and FUN_10060c30 is called with it anyway
        // fix: with no image there is nothing to place (the message above already reports the state)
        if (s.world().state().fixes.gogglesImageUninit) return;
        break;
    }
    }
    s.host().CEngineFX_PlaceWorn(name().c_str(), image, 0x1b8, 0xec);
}

// dll:1001d600
bool ObjGoggles::showPalette(Self& s) {
    if (m_byPowerState == 1) return false;
    return s.host().CEngineFX_LoadGogglesPalette(s.m_pSite.item);
}

// dll:1001d330
void ObjGoggles::dump(std::vector<std::string>& lines) const {
    dumpBase(lines);
    lines.push_back(m_bOn == 0 ? "m_bOn \t\tFALSE" : "m_bOn \t\tTRUE");
    char b[64];
    std::snprintf(b, sizeof b, "m_byOldPowerState \t\t%d", m_byShownPowerState);
    lines.push_back(b);
    std::snprintf(b, sizeof b, "m_byPowerState \t\t%d", m_byPowerState);
    lines.push_back(b);
    std::snprintf(b, sizeof b, "m_dPower \t\t%f", m_dCharge);
    lines.push_back(b);
}

// dll:1001d430
int ObjGoggles::pickUp(Self& s) {
    GameObj::pickUp(s);
    calcPowerState();
    showPower(s);
    return 1;
}

// dll:1001d450
int ObjGoggles::onWorn(Self& s) {
    GameObj::clearHolding(s);                                  // FUN_100291b0 (not through the vtable)
    s.m_pGoggles.set(this);
    if (0.0 < m_dCharge) {
        showPalette(s);
        update(s, s.now());                                     // vtable +0x68(DAT_100c1c1c)
        m_bOn = 1;
        redraw(s, 0);                                           // vtable +0x6c(0)
        s.host().playSound("sounds\\objects\\gogon.wav");
    }
    redisplayFeature(s);
    return 1;
}

// dll:1001d4d0
int ObjGoggles::onUnworn(Self& s) {
    s.m_pGoggles.clear();
    s.host().CEngineFX_UnlockPalette(1);
    s.host().CTimeline_ResetDiurnal();
    m_bOn = 0;
    redisplayFeature(s);
    return 1;
}

// dll:1001d3f0
int ObjGoggles::onUsedOnHotspot(Self& s, const std::string& hotspot) {
    s.host().playSound("sounds\\features\\pwrrech1.wav");
    m_dCharge = 100.0;
    m_byPowerState = 6;
    m_byShownPowerState = 6;
    return 1;
}

// dll:1001d530
int ObjGoggles::update(Self& s, i32 now) {
    u32 dt = static_cast<u32>(now - m_dwTimeStamp);
    if (dt == 0) return 0;
    m_dwTimeStamp = now;
    if (m_bOn != 0) {
        m_dCharge = static_cast<double>(dt) * -0.005555555555555556 + m_dCharge;
        m_dCharge = m_dCharge < 0.0 ? 0.0 : (m_dCharge > 100.0 ? 100.0 : m_dCharge);   // Clamp(&power, 0, 100)
        calcPowerState();
        if (m_dCharge == 0.0) {
            m_bOn = 0;
            s.host().playSound("sounds\\objects\\gogoff.wav");
            s.host().CEngineFX_UnlockPalette(1);
            s.host().CTimeline_ResetDiurnal();
        }
        if (m_byShownPowerState != m_byPowerState) {
            m_byShownPowerState = m_byPowerState;
            redraw(s, 0);
            showPalette(s);
        }
    }
    return 1;
}

// dll:1001d3d0
int ObjGoggles::redraw(Self& s, int full) {
    if (s.m_pGoggles.item == this) showPower(s);
    return 1;
}

void ObjGoggles::dumpMembers(ObjMembers& out) const {
    commonMembers(out);
    putD(out, "m_dCharge", m_dCharge);
    putI(out, "m_byPowerState", m_byPowerState);
    putI(out, "m_byShownPowerState", m_byShownPowerState);
    putI(out, "m_bOn", m_bOn);
}

// ---- COxyMask (vtable 10094208)

// dll:10076c40
void ObjOxyMask::describe(Self& s) {
    if (s.g.describeSuppressed != 0) {
        s.g.describeSuppressed = 0;
        return;
    }
    int p4;
    if (m_dFilter == 0.0) p4 = 0x31;
    else if (m_dFilter <= 0.99) {
        if (m_dFilter <= 0.8) {
            if (0.1 <= m_dFilter) {
                p4 = 0x37;
                if (0.4 <= m_dFilter) p4 = 0x2f;
            } else {
                p4 = 0x30;
            }
        } else {
            p4 = 0x36;
        }
    } else {
        p4 = 0x2e;
    }
    s.ShowMessage(1, m_dwNameStringId, p4, 0);
}

// dll:10076b00
int ObjOxyMask::onWorn(Self& s) {
    s.m_pAirMask.set(this);                                              // DAT_100c1fac
    if (0.0 < m_dFilter) {
        if (m_dFilter < 0.1) s.m_wBreathing = static_cast<u16>(s.m_wBreathing | 2);
        s.m_wBreathing = static_cast<u16>(s.m_wBreathing | 1);
        s.UpdateBreathingSound(0);
    }
    redisplayFeature(s);
    return 1;
}

// dll:10076b70
int ObjOxyMask::onUnworn(Self& s) {
    s.m_pAirMask.clear();
    s.m_wBreathing = static_cast<u16>(s.m_wBreathing & 0xfffc);
    s.UpdateBreathingSound(0);
    redisplayFeature(s);
    return 1;
}

// dll:10076ce0
int ObjOxyMask::refill(Self& s, double* source, i16 code) {
    if (code != 0xf0) return 0;
    if (*source == 0.0) {
        s.ShowMessage(1, 0xc4, 0xffff, 0);
        return 0;
    }
    s.host().playSound("sounds\\objects\\oxrefill.wav");
    double amount = (1.0 - m_dFilter) * 0.5;
    if (*source < amount) amount = *source;
    m_dFilter = amount * 2.0 + m_dFilter;
    *source = *source - amount;
    describe(s);
    return 1;
}

// dll:10076bc0
int ObjOxyMask::update(Self& s, i32 now) {
    i32 old = m_dwTimeStamp;
    m_dwTimeStamp = now;
    if ((s.m_wBreathing & 1) != 0) {
        m_dFilter = static_cast<double>(static_cast<u32>(now - old)) * -1.388888888888889e-05 + m_dFilter;
        if (m_dFilter <= 0.0) {
            m_dFilter = 0.0;
            s.m_wBreathing = static_cast<u16>(s.m_wBreathing & 0xfffc);
            return 1;
        }
        if (m_dFilter < 0.1) s.m_wBreathing = static_cast<u16>(s.m_wBreathing | 2);
    }
    return 1;
}

void ObjOxyMask::dumpMembers(ObjMembers& out) const {
    commonMembers(out);
    putD(out, "m_dFilter", m_dFilter);
}

// ---- COxyCylinder (vtable 10093248)

// dll:10062b90
void ObjOxyCylinder::afterLoad(Self& s) {
    if (m_bCarried != 0) objFxFollow(m_pFx, s);
    GameObj* torch = s.world().findObject("torch");
    if (torch) {
        if (m_dwUnk54 != 0) as<ObjTorch>(torch).setCylinder(this);
        else as<ObjTorch>(torch).setCylinder(nullptr);
    }
}

// dll:10063090
void ObjOxyCylinder::describe(Self& s) {
    if (s.g.describeSuppressed != 0) {
        s.g.describeSuppressed = 0;
        return;
    }
    int msg = 0xb3;
    int p4;
    if (m_dContents <= 1.8) {
        if (m_dContents <= 1.4) {
            if (m_dContents <= 1.0) {
                if (m_dContents <= 0.6) {
                    p4 = 0x30;
                    if (m_dContents <= 0.0) {
                        msg = m_dwNameStringId;
                        p4 = 0x31;
                    }
                } else {
                    p4 = 0x37;
                }
            } else {
                p4 = 0x2f;
            }
        } else {
            p4 = 0x36;
        }
    } else {
        p4 = 0x2e;
    }
    s.ShowMessage(1, msg, p4, 0);
}

// dll:10062e30
int ObjOxyCylinder::use(Self& s) {
    PlayerHost& h = s.host();
    if (objutil::iequals(variant(), "oxyon")) {
        m_bValveOpen = 0;
        safeSetVariant(s, "oxyoff", -2);
        h.CWorld_StopPageFX(0x1389);
        return 1;
    }
    if (0.0 < m_dContents) {
        m_bValveOpen = 1;
        safeSetVariant(s, "oxyon", -2);
        h.CWorld_StartPageFX(0x1389);
        h.setSynthState(5);                                              // DAT_100c27e4 = 5
        if (strcasecmp(reinterpret_cast<const char*>(s.m_szLocation.data()), "DB6CAVE") == 0) {
            h.setFXState(-0x63bd, 0);
            h.setFXState(-0x63bc, 1);
        }
    }
    return 1;
}

// dll:10062d20
int ObjOxyCylinder::pickUp(Self& s) {
    GameObj::pickUp(s);
    m_bCarried = 1;
    objFxPlace(m_pFx, s);
    return 1;
}

// dll:10062d80
int ObjOxyCylinder::onDropped(Self& s, const std::string& hotspot) {
    GameObj::onDropped(s, hotspot);
    m_bCarried = 0;
    if (objutil::iequals(hotspot, "dCylinder")) {
        m_dwUnk54 = 1;
        if (GameObj* torch = s.world().findObject("torch")) as<ObjTorch>(torch).setCylinder(this);
    }
    s.host().playSound("sounds\\objects\\cyldrop.wav");
    return 1;
}

// dll:10062ef0
int ObjOxyCylinder::onUsedOnHotspot(Self& s, const std::string& hotspot) {
    safeSetVariant(s, "oxyoff", -2);
    CFeature* f = objutil::feature(s);
    if (objutil::iequals(hotspot, "yOxygen") && f) {
        f->onApply(*this, hotspot);
        describe(s);
    }
    return 1;
}

// dll:10062f40
int ObjOxyCylinder::refill(Self& s, double* source, i16 code) {
    if (code != 0xf0) return 0;
    double amount = 2.0 - m_dContents;
    if (*source < amount) amount = *source;
    m_dContents = m_dContents + amount;
    *source = *source - amount;
    return 1;
}

// dll:10062fa0
int ObjOxyCylinder::update(Self& s, i32 now) {
    i32 old = m_dwTimeStamp;
    m_dwTimeStamp = now;
    if (m_bValveOpen != 0) {
        double dt = static_cast<double>(static_cast<u32>(now - old));
        m_dContents = dt * -0.0005555555555555556 + m_dContents;
        CSite* site = objFxSite(m_pFx);
        if (!site) throw Error("COxyCylinder::Update: the FX has no site");
        CBiomeCel* cel = site->cel();
        cel->setOxygen(dt * 0.0005555555555555556 + cel->oxygen());      // *(cel + 0x50)
        if (m_dContents < 0.0) m_dContents = 0.0;
        if (m_dContents == 0.0) {
            m_bValveOpen = 0;
            // ORIGINAL BUG?: the variation is "off" here but "oxyoff" everywhere else
            // fix: "oxyoff" is the empty cylinder's variation in use() and everywhere else
            changeVariation(s, s.world().state().fixes.oxyCylinderEmptyVariation ? "oxyoff" : "off", -2);
            s.host().CWorld_StopPageFX(0x1389);
            if (s.host().synthState() == 5) s.host().setSynthState(1);
            if (strcasecmp(reinterpret_cast<const char*>(s.m_szLocation.data()), "DB6CAVE") == 0) {
                s.host().setFXState(-0x63bd, 1);
                s.host().setFXState(-0x63bc, 0);
            }
        }
    }
    return 1;
}

// dll:10062df0
int ObjOxyCylinder::setHolding(Self& s) {
    GameObj::setHolding(s);
    m_dwUnk54 = 0;
    if (GameObj* torch = s.world().findObject("torch")) as<ObjTorch>(torch).setCylinder(nullptr);
    describe(s);
    return 1;
}

// dll:10062c60
void ObjOxyCylinder::onPageEnter(Self& s, const std::string& page) {
    if (m_bCarried == 0) {
        s.host().CEngineFX_HotspotEnable("yCylinder", 0, -1);
    } else {
        objFxFollow(m_pFx, s);
    }
}

// dll:10063150
void ObjOxyCylinder::onDropZoneClicked(Self& s, const std::string& hotspot) {
    if (objutil::iequals(hotspot, "dCylinder")) s.host().Eng_Api48_DropHeldObject(hotspot.c_str());
}

// dll:10063130
void ObjOxyCylinder::removeFromScene(Self* s) {
    GameObj::removeFromScene(s);
    if (s) s->host().CWorld_StopPageFX(0x1389);
}

void ObjOxyCylinder::dumpMembers(ObjMembers& out) const {
    commonMembers(out);
    putP(out, "m_pFx", m_pFx);
    putI(out, "m_bCarried", m_bCarried);
    putD(out, "m_dContents", m_dContents);
    putI(out, "m_bValveOpen", m_bValveOpen);
    putI(out, "m_dwUnk54", m_dwUnk54);
}

BIOSYS_REGISTER_CTOR(10028c20, [](World& w, const char* name, int32_t a1, int32_t a2, double a3) -> Item* {
    return w.adopt(std::make_unique<ObjMisc>(name, a1, static_cast<u32>(a2), a3));
});
BIOSYS_REGISTER_CTOR(1005a500, [](World& w, const char* name, int32_t a1) -> Item* {
    return w.adopt(std::make_unique<ObjBabySynth>(name, a1));
});
BIOSYS_REGISTER_CTOR(1001af20, [](World& w, const char* name, int32_t a1) -> Item* {
    return w.adopt(std::make_unique<ObjFishingRod>(name, a1));
});
BIOSYS_REGISTER_CTOR(1005a170, [](World& w, const char* name, int32_t a1) -> Item* {
    return w.adopt(std::make_unique<ObjBlowpipe>(name, a1));
});
BIOSYS_REGISTER_CTOR(10006200, [](World& w, const char* name, int32_t a1) -> Item* {
    return w.adopt(std::make_unique<ObjBlowpipePick>(name, a1));
});
BIOSYS_REGISTER_CTOR(100268d0, [](World& w, const char* name, int32_t a1) -> Item* {
    return w.adopt(std::make_unique<ObjTracker>(name, a1));
});
BIOSYS_REGISTER_CTOR(1005bcf0, [](World& w, const char* name, int32_t a1) -> Item* {
    return w.adopt(std::make_unique<ObjCompass>(name, a1));
});
BIOSYS_REGISTER_CTOR(100191e0, [](World& w, const char* name, int32_t a1) -> Item* {
    return w.adopt(std::make_unique<ObjSurvivalBag>(name, a1));
});
BIOSYS_REGISTER_CTOR(10002d90, [](World& w, const char* name, int32_t a1) -> Item* {
    return w.adopt(std::make_unique<ObjPoncho>(name, a1));
});
BIOSYS_REGISTER_CTOR(10022f70, [](World& w, const char* name, int32_t a1) -> Item* {
    return w.adopt(std::make_unique<ObjTorch>(name, a1));
});
BIOSYS_REGISTER_CTOR(1001d1a0, [](World& w, const char* name, int32_t a1) -> Item* {
    return w.adopt(std::make_unique<ObjGoggles>(name, a1));
});
BIOSYS_REGISTER_CTOR(10076a10, [](World& w, const char* name, int32_t a1) -> Item* {
    return w.adopt(std::make_unique<ObjOxyMask>(name, a1));
});
BIOSYS_REGISTER_CTOR(100629d0, [](World& w, const char* name, int32_t a1, Item* a2) -> Item* {
    return w.adopt(std::make_unique<ObjOxyCylinder>(name, a1, a2));
});

} // namespace biosys::game
