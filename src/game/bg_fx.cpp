// The effects of pages and sites: the CSpotFX family (CAnimFX, CSynthAnimFX, CMovieFX, CSoundSiteFX) and the local FX
// (CSoundFX, CRainFX). A spot FX is one animation, movie or wav that a page or a site may start; CanStart decides by the
// flags the builders gave it (time of day, site health, lights, ocean health, a linked game object). The virtual slots of
// the original are the `fx*` functions below, dispatched on the class.
#include "game/biosgame.h"
#include "game/obj_food.h"
#include "game/watertank.h"
#include <cstring>

namespace biosys::game {

// port helper (the original walks the CObList of the page / site)
std::vector<CSpotFX*> BiosGame::spotFxOf(const std::vector<Item*>& list) {
    std::vector<CSpotFX*> out;
    for (Item* it : list) out.push_back(&as<CSpotFX>(it));
    return out;
}

// dll:1000a5b0 CSpotFX::CanStart (vtable +0x18)
bool BiosGame::CSpotFX_CanStart(CSpotFX& f) {
    if (!f.on() || f.playing()) return false;
    const uint32_t fl = f.flags();
    if (g.linkedObjectLock && (fl & 0x10) != 0 && (fl & 0x80) == 0) return false;
    const int d = tl.diurnal;
    if ((fl & 0x1000) != 0 && (d == 0xf || d == 0x14 || d == 0x1e)) return false;
    if ((fl & 0x2000) != 0 && (d == 0x23 || d == 0x28 || d == 5)) return false;
    CSite* site = curSite();   // DAT_100c1f84 (read unchecked by the original)
    const int st = site ? site->state() : 0;
    if ((fl & 0x10000) != 0 && st != 2) return false;
    if ((fl & 0x40000) != 0 && st != 4) return false;
    if ((fl & 0x20000) != 0 && st != 1) return false;
    const COcean* ocean = dynamic_cast<const COcean*>(gs().ocean);
    const double oceanHealth = ocean ? ocean->health() : 0.0;
    if ((fl & 0x80000) != 0 && oceanHealth < 0.1) return false;
    if ((fl & 0x100000) != 0 && 0.1 <= oceanHealth) return false;
    const double avg = site && site->cel() ? site->cel()->plantHealthAvg() : 0.0;
    if ((fl & 0x200000) != 0 && (st != 7 || avg <= 0.2)) return false;
    if ((fl & 0x400000) != 0 && (st != 6 || avg <= 0.2)) return false;
    if ((fl & 0x800000) != 0 && (st != 5 || avg <= 0.2)) return false;
    if ((fl & 0x4000) != 0 && gs().lightsOn == 0) return false;
    if ((fl & 0x8000) != 0 && gs().lightsOn != 0) return false;
    if ((fl & 0x10) == 0) return true;
    // linked to a game object: the object named like the file stem must be in state 2 (CFood: on its plant)
    std::string path = f.file();
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    path.resize(dot);
    size_t bs = path.rfind('\\');
    std::string stem = bs == std::string::npos ? path : path.substr(bs + 1);
    GameObj* obj = world().findObject(stem.c_str());
    if (!obj) return false;
    const ObjFood* food = dynamic_cast<const ObjFood*>(obj);
    return food && food->state() == 2;
}

// the 'Y' / 'P' hotspot of the stem of the FX's file: dll:1000a850 / 1000a930 (inlined). "ANIMS\PREY\R3P_1.TAN" -> "YR3P_1".
// Returns false when the file name has no '.'.
static bool linkedHotspotName(const std::string& file, std::string& out) {
    std::string path = file;
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    path.resize(dot);
    size_t bs = path.rfind('\\');
    // the buffer starts one byte before the path (DAT_100c0de8 / DAT_100c0de9); the backslash is replaced by 'Y'
    out = "Y" + (bs == std::string::npos ? path : path.substr(bs + 1));
    return true;
}

// dll:1000a850 / dll:1000a930 (the inlined 'Y' / 'P' hotspot code)
void BiosGame::CSpotFX_ToggleLinkedHotspot(CSpotFX& f, bool enable, bool* found) {
    std::string name;
    if (found) *found = false;
    if (!linkedHotspotName(f.file(), name)) return;
    if (found) *found = true;
    if (!eng_.EnableHotspot(name, -1, enable ? 1 : 0, nullptr, 0)) {
        name[0] = 'P';
        eng_.EnableHotspot(name, -1, enable ? 1 : 0, nullptr, 0);
    }
}

// dll:1000a850 (vtable +0x2c, CSynthAnimFX: dll:10015dd0): the engine started the media `handle` of type `mask`
bool BiosGame::CSpotFX_OnStarted(CSpotFX& f, int handle, unsigned mask) {
    if (dynamic_cast<CSynthAnimFX*>(&f)) {
        // dll:10015dd0
        if (f.handle() == handle && (f.flags() & mask) != 0) {
            g.synthState = 2;
            g.synthX = static_cast<CSynthAnimFX&>(f).x() + 100;
            g.synthParam = static_cast<CSynthAnimFX&>(f).synthParam();
            CEngineFX_HotspotEnable("SYNTH", 1, -1);
            CEngineFX_HotspotEnable("SYNTH_GRR", 1, -1);
            return true;
        }
        return false;
    }
    if (f.handle() != handle || (mask & f.flags()) == 0) return false;
    if ((f.flags() & 0x1000000) != 0) eng_.EnableUserInput(0, static_cast<int>(f.flags() & 0x2000000));
    if ((f.flags() & 0x10) != 0) {
        bool found = false;
        CSpotFX_ToggleLinkedHotspot(f, true, &found);
        if (!found) return false;
    }
    return true;
}

// dll:1000a930 (vtable +0x30, CSynthAnimFX: dll:10015e50): the media stopped (ended)
bool BiosGame::CSpotFX_OnStopped(CSpotFX& f, int handle, unsigned mask) {
    if (dynamic_cast<CSynthAnimFX*>(&f)) {
        if (f.handle() == handle && (f.flags() & mask) != 0) {
            g.synthState = 1;
            CEngineFX_HotspotEnable("SYNTH", 0, -1);
            CEngineFX_HotspotEnable("SYNTH_GRR", 0, -1);
            return true;
        }
        return false;
    }
    if (f.handle() != handle || (mask & f.flags()) == 0) return false;
    f.setPlaying(false);
    if ((f.flags() & 0x1000000) != 0) eng_.EnableUserInput(1, 0);
    if ((f.flags() & 0x10) != 0) {
        bool found = false;
        CSpotFX_ToggleLinkedHotspot(f, false, &found);
        if (!found) return true;
        fxClose(f);        // vtable +0x28
    }
    return true;
}

// +0x34: dll:1000aa60 (CMovieFX: dll:10003440)
void BiosGame::fxReset(CSpotFX& f) {
    f.setPlaying(false);
    f.setHandle(-1);
    if (CMovieFX* m = dynamic_cast<CMovieFX*>(&f)) {
        if (!m->wavFile().empty()) CEngineFX_StopSound(m->wavHandle());
        if ((f.flags() & 0x1000000) != 0) eng_.EnableUserInput(1, 0);
    }
}

// +0x38: dll:1000aa20 (CMovieFX: dll:100033f0): the media was closed by the engine
bool BiosGame::fxOnClosed(CSpotFX& f, int handle, unsigned mask) {
    bool base = false;
    if (f.handle() == handle && (f.flags() & mask) != 0) {
        f.setPlaying(false);
        f.setHandle(-1);
        eng_.RedrawScreen();
        base = true;
    }
    if (CMovieFX* m = dynamic_cast<CMovieFX*>(&f)) {
        if (base) return true;
        if (m->wavHandle() == handle && (mask & 4) != 0) {
            m->setWavHandle(-1);
            return true;
        }
        return false;
    }
    return base;
}

// +0x1c: dll:100707e0 (CAnimFX, CSynthAnimFX), dll:10003110 (CMovieFX), dll:100179c0 (CSoundSiteFX)
bool BiosGame::fxStart(CSpotFX& f) {
    if (!f.on() || f.playing()) return false;
    if (g.silent) return false;
    const uint32_t fl = f.flags();
    const double wind = save_->worldHeader.m_dWindSpeed;
    if (CAnimFX* a = dynamic_cast<CAnimFX*>(&f)) {
        if ((fl & 0x20) != 0) f.setParam7c(static_cast<int32_t>((20.0 - wind) * 6.25 + 33.0));
        int local = -1;
        int ok = eng_.OpenSpriteAnimation(a->file(), -1, a->x(), a->y(), 1, a->param7c(), true, (fl & 0x80) >> 7,
                                          (fl & 0x8000000) >> 27, a->wavFile(), 0, (fl & 0x80000000u) == 0 ? 1 : 0, 1, local, -1, 0);
        a->setHandle(local);
        return ok != 0;
    }
    if (CMovieFX* m = dynamic_cast<CMovieFX*>(&f)) {
        if ((fl & 0x40) != 0) {
            if (static_cast<uint32_t>(save_->timeline.m_dwTime) <= g.movieThrottle) return false;
            g.movieThrottle = static_cast<uint32_t>(save_->timeline.m_dwTime) + 0x2a30;
        }
        int local = -1;
        int ok = eng_.OpenAVIMovie(m->file(), -1, m->x(), m->y(), true, (fl & 0x80) >> 7, 0, fx.palette.data(), local);
        m->setHandle(local);
        if (!ok) return false;
        fx.aviHandle = local;
        if ((fl & 0x10000000) != 0) {
            const u8 key[4] = {0, 0xff, 0xff, 0};
            eng_.SetAVIMovieParam(m->handle(), key);
            eng_.AviOption42(m->handle());
        }
        if ((fl & 0x200) != 0) eng_.PrepareSynthAVIMovie(m->handle());
        if ((fl & 0x100) != 0) eng_.AviOption41(m->handle());
        if ((fl & 0x4000000) != 0) eng_.AviOption4f(m->handle(), 1, 0);
        if ((fl & 0x80) != 0) {
            fx.loopedAvi = true;
            fx.loopedAviFile = m->file();
            CPage* page = curPage();
            fx.loopedAviPage = page ? page->name() : "nowhere";
            fx.loopedAviUnk = 0;
            fx.aviX = m->x();
            fx.aviY = m->y();
            fx.aviVolume = 0;
            fx.aviKey = 1;
            fx.aviOption = static_cast<int>((fl & 0x10000000) >> 28);
        }
        if (m->wavFile().empty()) return true;
        int wl = -1;
        int ok2 = eng_.OpenSound(m->wavFile(), (fl & 0x80) >> 7, true, 0, 0, 0, wl);
        m->setWavHandle(wl);
        return ok2 != 0;
    }
    if (dynamic_cast<CSoundSiteFX*>(&f)) {
        if ((fl & 0x20) != 0) f.setParam7c(static_cast<int32_t>((20.0 - wind) * -100.0));
        int local = -1;
        int ok = eng_.OpenSound(f.file(), (fl & 0x80) >> 7, true, 0, 0, f.param7c(), local);
        f.setHandle(local);
        return ok != 0;
    }
    return false;   // CSpotFX base: dll:1000a810
}

// +0x20: dll:100708e0 / dll:10003340 / dll:10017a70
bool BiosGame::fxPlay(CSpotFX& f) {
    if (dynamic_cast<CAnimFX*>(&f)) {
        if (!f.on() || f.playing()) return false;
        if (f.handle() == -1) return false;
        f.setPlaying(eng_.PlaySpriteAnimation(f.handle(), 1, 0));
        return true;
    }
    if (CMovieFX* m = dynamic_cast<CMovieFX*>(&f)) {
        if (!f.on() || f.playing()) return false;
        if (f.handle() == -1) return false;
        f.setPlaying(eng_.PlayAVIMovie(f.handle(), 1));
        if (!m->wavFile().empty()) eng_.PlaySound(m->wavHandle(), 1);
        return true;
    }
    if (dynamic_cast<CSoundSiteFX*>(&f)) {
        if (!f.on() || f.playing()) return false;
        if (f.handle() == -1) return false;
        f.setPlaying(eng_.PlaySound(f.handle(), 1));
        return true;
    }
    return false;
}

// +0x24: dll:10070920 / dll:10003390 / dll:10017ab0
bool BiosGame::fxStop(CSpotFX& f) {
    if (dynamic_cast<CAnimFX*>(&f)) {
        if (f.playing()) {
            eng_.StopSpriteAnimation(f.handle());     // FUN_10060b40
            f.setPlaying(false);
            if ((f.flags() & 0x1000000) != 0) eng_.EnableUserInput(1, 0);
        }
        return true;
    }
    if (dynamic_cast<CMovieFX*>(&f)) {
        if (f.playing()) {
            CEngineFX_StopLoopedAvi();                // FUN_10061110: StopAVIMovie(CEngineFX's movie handle)
            f.setPlaying(false);
        }
        return true;
    }
    if (dynamic_cast<CSoundSiteFX*>(&f)) {
        if (f.playing()) {
            CEngineFX_StopSound(f.handle());
            f.setPlaying(false);
        }
        return true;
    }
    return false;
}

// +0x28: dll:10070960 / dll:100033c0 / dll:10017ae0
bool BiosGame::fxClose(CSpotFX& f) {
    if (dynamic_cast<CAnimFX*>(&f)) {
        if (eng_.CloseSpriteAnimation(f.handle())) { f.setPlaying(false); f.setHandle(-1); }
        return true;
    }
    if (dynamic_cast<CMovieFX*>(&f)) {
        if (eng_.CloseAVIMovie(f.handle())) { f.setPlaying(false); f.setHandle(-1); }
        return true;
    }
    if (dynamic_cast<CSoundSiteFX*>(&f)) {
        if (eng_.CloseSound(f.handle())) { f.setPlaying(false); f.setHandle(-1); }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------
// local FX

// dll:1006ef40 CSelf::IsAtPage
bool BiosGame::isAtPage(const std::string& page) {
    return iequals(page, std::string(reinterpret_cast<const char*>(self().m_szLocation.data())));
}

// vtable +0x30: dll:10078230 CSoundFX::Update, dll:10073e20 CRainFX::Update
bool BiosGame::localFxUpdate(CLocalFX& lf) {
    CSoundFX& f = as<CSoundFX>(&lf);
    CRainFX* rain = dynamic_cast<CRainFX*>(&f);
    const std::string loc(reinterpret_cast<const char*>(self().m_szLocation.data()));
    bool active = false;
    int volume = 0;
    if (rain) {
        if (!f.on()) {
            if (f.site() && f.site()->cel()) f.site()->cel()->setFlags(f.site()->cel()->flags() & 0xfffffffdu);
            return true;
        }
        if (f.site() && f.site()->cel()) f.site()->cel()->setFlags(f.site()->cel()->flags() | 2);
        if (g.silent) return false;
        if (isCurrentSite(f.site())) {
            if (!loc.empty() && loc[0] == 'B' && loc.find("LIFT") != std::string::npos) return false;
            active = true;
            volume = f.volumeAtSite();
        }
        if (active) {
            g.rainFilter = true;
            CFeature* feat = dynamic_cast<CFeature*>(self().m_pFeature.item);
            if (!feat || feat->unk54() == 0) {
                eng_.BeginPageFilter(0x76, 0x56, 0x57);
                rain->setFilterActive(true);
            }
            if (!f.playing()) {
                int h = f.handle();
                CEngineFX_PlayLoopedWav(f.file(), volume, &h, nullptr, 0);
                f.setHandle(h);
                f.setPlaying(true);
                return true;
            }
        }
        if (!active) {
            if (f.playing()) {
                int h = f.handle();
                CEngineFX_CloseSound(&h);
                f.setHandle(h);
                f.setPlaying(false);
                rain->setFilterActive(false);
            }
            return true;
        }
        return true;
    }
    if (!f.on()) return true;
    if (g.silent) return false;
    bool tryAtSite = true;
    if (f.mode() == 1) {
        if (isAtPage(f.page()) ||
            (!loc.empty() && loc[0] == 'U' && iequals(f.page().substr(1), loc.substr(1)))) {
            active = true;
            volume = f.volumeAtPage();
            tryAtSite = false;
        }
    } else if (f.mode() != 2) {
        tryAtSite = false;
    }
    if (tryAtSite && isCurrentSite(f.site())) {
        active = true;
        volume = f.volumeAtSite();
    }
    if (active) {
        if (!f.playing()) {
            int h = f.handle();
            CEngineFX_PlayLoopedWav(f.file(), volume, &h, f.panTarget().c_str(), 0);
            f.setHandle(h);
            f.setPlaying(true);
            return true;
        }
    } else if (f.playing()) {
        int h = f.handle();
        CEngineFX_CloseSound(&h);
        f.setHandle(h);
        f.setPlaying(false);
    }
    return true;
}

// vtable +0x28: dll:10078200 CSoundFX, dll:10073db0 CRainFX
void BiosGame::localFxReset(CLocalFX& lf) {
    CSoundFX& f = as<CSoundFX>(&lf);
    f.setOn(false);
    f.setPlaying(false);
    int h = f.handle();
    CEngineFX_CloseSound(&h);
    f.setHandle(-1);
    if (CRainFX* rain = dynamic_cast<CRainFX*>(&f)) {
        (void)rain;
        if (f.site() && f.site()->cel()) f.site()->cel()->setFlags(f.site()->cel()->flags() & 0xfffffffdu);
        if (isCurrentSite(f.site())) {
            eng_.EndPageFilter();
            g.rainFilter = false;
        }
    }
}

// vtable +0x34: dll:10078340 CSoundFX, dll:10073f60 CRainFX
bool BiosGame::localFxStop(CLocalFX& lf) {
    CSoundFX& f = as<CSoundFX>(&lf);
    if (!f.playing()) return false;
    int h = f.handle();
    CEngineFX_CloseSound(&h);
    f.setHandle(-1);
    if (dynamic_cast<CRainFX*>(&f) && isCurrentSite(f.site())) eng_.EndPageFilter();
    f.setPlaying(false);
    return true;
}

// vtable +0x24: dll:10006190
void BiosGame::localFxSetOn(CLocalFX& f, int id) {
    if (f.id() == id) {
        f.setOn(true);
        localFxUpdate(f);   // vtable +0x30
    }
}

// vtable +0x2c: dll:100781c0 CSoundFX, dll:10073d30 CRainFX (the same as +0x28 for the FX with that id)
void BiosGame::localFxSetOff(CLocalFX& f, int id) {
    if (f.id() == id) localFxReset(f);
}

// dll:10014310
void BiosGame::CWorld_LocalFxReset() {
    for (Item* it : world().list(Table::LocalFx)) localFxReset(as<CLocalFX>(it));
}
// dll:10014350
void BiosGame::CWorld_LocalFxUpdate() {
    for (Item* it : world().list(Table::LocalFx)) localFxUpdate(as<CLocalFX>(it));
}
// dll:10014390
void BiosGame::CWorld_LocalFxStop() {
    for (Item* it : world().list(Table::LocalFx)) localFxStop(as<CLocalFX>(it));
}
// dll:10014280
void BiosGame::CWorld_StartPageFX(int id) {
    if (id == 0) return;
    for (Item* it : world().list(Table::LocalFx)) localFxSetOn(as<CLocalFX>(it), id);
}
// dll:100142d0
void BiosGame::CWorld_StopPageFX(int id) {
    for (Item* it : world().list(Table::LocalFx)) localFxSetOff(as<CLocalFX>(it), id);
}

// dll:100138d0 CWorld::SetFXState: switches every spot FX with that id (off: stops the playing ones first)
void BiosGame::CWorld_SetFXState(int id, int on) {
    if (id == 0) return;
    for (Item* it : world().list(Table::Fx)) {
        CSpotFX& f = as<CSpotFX>(it);
        if (on == 0) {
            if (f.id() == id) {
                if (f.playing()) fxStop(f);
                f.setOn(false);
            }
        } else if (f.id() == id) {
            f.setOn(true);
        }
    }
    if (CPage* p = curPage()) CPage_UpdateFx(*p);
    if (CSite* s = curSite()) CSite_RefreshFx(*s);
}

} // namespace biosys::game
