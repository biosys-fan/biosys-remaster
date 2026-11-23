// CSite behaviour (dll:1000c7c0..1000e9e0): entering and leaving a site, its palettes through the time of day (five
// palettes per site, blended with the XFT lookups by CEngineFX::Blend), its ambient sounds by time of day and by health.
//
// The original keeps the "current" ambient sounds, sound sets and palettes in DLL globals (DAT_100c4258 ...), and
// compares the site's string / buffer pointers with them. Equal pointers are equal strings here (site.h), so the
// comparisons are done on the file names / the sound-set addresses.
#include "game/biosgame.h"
#include "game/obj_gear.h"
#include "game/story.h"
#include "game/feat_power.h"
#include "game/watertank.h"
#include <cstring>

namespace biosys::game {

namespace {
bool strstrReplace(std::string& s, const char* from, const char* to) {   // FUN_100793b0 = strstr; then a 3-byte store
    size_t p = s.find(from);
    if (p == std::string::npos) return false;
    s.replace(p, 3, to);
    return true;
}
} // namespace

// CBiome +0x70 (simulation.md section 3): Club Eden 0x20000000, the house 0x10000000, every other biome 0
// port helper for CBiome +0x70 (the static CBiome objects of dll:1002b560, simulation.md section 3)
uint32_t BiosGame::biomePowerBit(const Biome* b) {
    if (!b) return 0;
    if (b->addr == 0x100c2560) return 0x20000000;
    if (b->addr == 0x100c1208) return 0x10000000;
    return 0;
}

// port helper: the sound set buffer a CSite constructor argument points to (dll:100b3ec8.., soundsets.gen.cpp)
SoundSet* BiosGame::soundSet(DllPtr p) {
    if (p.addr == 0) return nullptr;
    auto it = soundSets_.find(p.addr);
    if (it != soundSets_.end()) return &it->second;
    for (const SoundSetDef& d : soundSetDefs()) {
        if (d.addr != p.addr) continue;
        SoundSet s;
        s.addr = d.addr;
        s.files = d.files;
        s.handles.assign(d.files.size(), -1);
        return &soundSets_.emplace(p.addr, std::move(s)).first->second;
    }
    throw Error("sound set 0x" + std::to_string(p.addr) + " is not in soundsets.gen.cpp");
}

// port helper: the entry (file string constant + handle at +100) of an ambient file name
int BiosGame::ambientHandle(const std::string& file) {
    auto it = ambient_.find(file);
    return it == ambient_.end() ? -1 : it->second;
}
void BiosGame::setAmbientHandle(const std::string& file, int h) { ambient_[file] = h; }

// port helper: the 24 palette / lookup names the site's palette pointer (dll:1000c270 argument 10) starts (data/palette_sets.tsv)
const PaletteSet* BiosGame::paletteSetOf(const CSite& s) {
    if (s.palette().empty()) return nullptr;
    int idx = palettes_.setForPage(s.name());
    if (idx < 0) {
        for (int i = 0; i < palettes_.setCount(); i++)
            if (palettes_.set(i).pal[0][0] == s.palette()) { idx = i; break; }
    }
    return idx < 0 ? nullptr : &palettes_.set(idx);
}

// dll:10018860 CBiomeCel::UpdatePlantHealthAvg
void BiosGame::CBiomeCel_UpdatePlantHealthAvg(CBiomeCel& c) {
    if (c.plants().empty()) return;   // *(cel + 0xdc) == 0: the plant list is empty
    double oldMax = c.plantHealthAvg();
    double old = c.plantHealthAvg();
    double sum = 0.0;
    int n = 0;
    for (Item* it : c.plants()) {
        CPlant& p = as<CPlant>(it);
        if ((p.flags() & 8) == 0) {
            n++;
            sum = p.health() + sum;
        }
    }
    double avg = sum;
    if (n > 0) avg = sum / static_cast<double>(n);
    c.setPlantHealthAvg(avg);
    if (c.plantHealthMax() < avg) c.setPlantHealthMax(avg);
    if (avg < c.plantHealthMin()) c.setPlantHealthMin(avg);
    if (old < avg) {
        uint32_t f = c.flags() & 0xffffffbfu;
        c.setFlags(f | 0x20);
        return;
    }
    if (avg < oldMax) {
        uint32_t f = c.flags();
        c.setFlags((f & 0xffffffdfu) | 0x40);
    }
}

// dll:1000e9e0 (FUN_1000e9e0): starts the looped ambient wav of a site. "<w>" in the name is replaced by dry / wet
// by the cel's flag 0x10; the handle lives in the entry of the unreplaced name.
void BiosGame::CSite_PlayAmbient(CSite& s, const std::string& file) {
    if (file.empty() || g.soundsOff) return;
    std::string buf = file;
    int h = ambientHandle(file);
    if (buf.find("<w>") != std::string::npos) {
        CBiomeCel* cel = s.cel();
        strstrReplace(buf, "<w>", (cel->flags() & 0x10) == 0 ? "dry" : "wet");
        CEngineFX_PlayLoopedWav(buf, -10000, &h, nullptr, 0);
    } else {
        CEngineFX_PlayLoopedWav(file, -10000, &h, nullptr, 0);
    }
    setAmbientHandle(file, h);
}

// dll:1000eb00 (FUN_1000eb00)
void BiosGame::CSite_CloseAmbient(const std::string& file) {
    if (file.empty()) return;
    int h = ambientHandle(file);
    if (h != -1) {
        CEngineFX_CloseSound(&h);
        setAmbientHandle(file, h);
    }
}

// dll:1000eb20 (FUN_1000eb20)
void BiosGame::CSite_CloseAllAmbient(CSite& s) {
    CSite_CloseAmbient(s.daySound());
    g.ambDay.clear();
    CSite_CloseAmbient(s.nightSound());
    g.ambNight.clear();
    CSite_CloseAmbient(s.deadSound());
    g.ambDead.clear();
}

// dll:1000e710 (FUN_1000e710)
void BiosGame::CSite_CloseSet(SoundSet* set) {
    if (!set) return;
    for (size_t i = 0; i < set->handles.size(); i++) {
        if (set->handles[i] != -1) {
            eng_.CloseSound(set->handles[i]);
            set->handles[i] = -1;
        }
    }
}

// dll:1000e6b0 (FUN_1000e6b0)
void BiosGame::CSite_CloseAllSets() {
    CSite_CloseSet(g.setDay);
    g.setDay = nullptr;
    CSite_CloseSet(g.setDusk);
    g.setDusk = nullptr;
    CSite_CloseSet(g.setNight);
    g.setNight = nullptr;
    CSite_CloseSet(g.setDawn);
    g.setDawn = nullptr;
}

// dll:1000e5d0 CSite::CloseSounds (the name is wrong: it opens every wav of the site's set for `slot` that is not open
// yet and makes it the current one of that slot)
void BiosGame::CSite_OpenSounds(CSite& s, int slot) {
    SoundSet* set = nullptr;
    switch (slot) {
    case 10: set = g.setDawn = soundSet(s.buffer(3)); break;
    case 0x14: set = g.setDay = soundSet(s.buffer(0)); break;
    case 0x20: set = g.setDusk = soundSet(s.buffer(1)); break;
    case 0x28: set = g.setNight = soundSet(s.buffer(2)); break;
    default: eng_.WriteLog("CSite::CloseSounds open wrong!"); return;
    }
    if (!set) return;
    for (size_t i = 0; i < set->files.size(); i++) {
        if (set->handles[i] == -1) {
            int h = -1;
            if (eng_.OpenSound(set->files[i], 0, true, 0, 0, 0, h)) set->handles[i] = h;
        }
    }
}

// dll:1000e750 (FUN_1000e750): maybe plays one random wav of the set for the time-of-day slot
void BiosGame::CSite_RandomAmbient(CSite& s, int slot, double weight) {
    if (g.noPalette || self().m_bAwake == 0 || g.silent) return;
    double x = weight * 50.0;
    if ((s.flags() & 0x100) != 0) x = s.cel()->plantHealthAvg() * x;
    if (!chancePercent(x)) return;
    SoundSet* set = nullptr;
    switch (slot) {
    case 10: set = soundSet(s.buffer(3)); break;
    case 0x14: set = soundSet(s.buffer(0)); break;
    case 0x20: set = soundSet(s.buffer(1)); break;
    case 0x28: set = soundSet(s.buffer(2)); break;
    default: eng_.WriteLog("BiosGame: PlaySound, invalid time o' day"); return;
    }
    if (!set || set->files.empty()) return;
    uint32_t n = static_cast<uint32_t>(set->files.size());
    uint32_t k = static_cast<uint32_t>(static_cast<int>(crand()) * static_cast<int>(n)) / 0x7fff + 1;
    // ORIGINAL BUG: rand() == 0x7fff gives index count + 1, one entry past the set (it reads the memory behind it); the
    // port plays nothing then
    // fix: the random index is meant to stay inside the set (the draw is scaled by 0x7fff, its maximum overshoots by one)
    if (k > n) { if (!gs().fixes.ambientSoundIndex) return; k = n; }
    eng_.PlaySound(set->handles[k - 1], 1);
}

// dll:1000e910 (FUN_1000e910): is `handle` one of the open sounds of this site's four sets?
bool BiosGame::CSite_OwnsSound(CSite& s, int handle) {
    const int order[4] = {0, 2, 1, 3};   // +0xc0, +0xc8, +0xc4, +0xcc
    for (int o : order) {
        SoundSet* set = soundSet(s.buffer(o));
        if (!set) continue;
        for (int h : set->handles)
            if (h == handle) return true;
    }
    return false;
}

// dll:1000d990 (FUN_1000d990): which palette group (A / B) the site uses now
int BiosGame::CSite_PaletteGroup(CSite& s) {
    g.paletteGroupPrev = g.paletteGroup;
    uint32_t f = s.flags();
    if ((f & 0x400) != 0) g.paletteGroup = gs().lightsOn;
    else if ((f & 0x8000) != 0) g.paletteGroup = gs().lightsOn;
    else if ((f & 0x1000) == 0) g.paletteGroup = 0;
    else g.paletteGroup = 0.2 < s.cel()->plantHealthAvg() ? 1 : 0;
    return g.paletteGroup;
}

// dll:1000d9f0 (FUN_1000d9f0): loads the site's five palette files (night, dawn, day, cloud, dusk) into the DLL buffers.
// Each is loaded only when the group-A name of the same slot is not empty (also for group B: the original tests the
// group-A string).
void BiosGame::CSite_LoadPalettes(CSite& s) {
    const PaletteSet* ps = paletteSetOf(s);
    if (!ps) return;
    const int grp = g.paletteGroup != 0 ? 1 : 0;
    const int slots[5] = {0, 1, 2, 4, 3};   // load order of the original
    for (int slot : slots) {
        if (ps->pal[0][slot].empty()) continue;
        readPalette(ps->pal[grp][slot], g.sitePal[static_cast<size_t>(slot)]);
    }
}

// dll:1000e470 (FUN_1000e470): in a living site (state != 1) tints green / blue pixels of the palette towards the
// plant health (site flag 0x100) or the ocean's health (0x200)
void BiosGame::CSite_TintPalette(CSite& s, Pal& p) {
    if (s.state() == 1) return;
    uint32_t f = s.flags();
    if ((f & 0x100) != 0) {
        double k = 1.0 - s.cel()->plantHealthAvg();
        for (int i = 0x3e; i < 0x100; i++) {
            u8* e = p.data() + i * 4;
            double r = e[0], gg = e[1], b = e[2];
            if (r * 1.2 < gg && b * 1.2 < gg) e[0] = static_cast<u8>(e[0] + static_cast<u8>(static_cast<int>((gg - r) * k)));
        }
        return;
    }
    if ((f & 0x200) != 0) {
        const COcean* oc = dynamic_cast<const COcean*>(gs().ocean);
        double k = 1.0 - (oc ? oc->health() : 0.0);
        for (int i = 0x3e; i < 0x100; i++) {
            u8* e = p.data() + i * 4;
            double r = e[0], gg = e[1], b = e[2];
            if (gg * 1.2 < b && r * 1.2 < b) e[1] = static_cast<u8>(e[1] + static_cast<u8>(static_cast<int>((b - gg) * k)));
        }
    }
}

// dll:1000e3a0 (FUN_1000e3a0): the palette of the day: the day palette, or (flag 0x80000: open air) the cloudy palette
// blended into it by the clear-sky fraction of the weather
void BiosGame::CSite_BuildDayPalette(CSite& s) {
    const PaletteSet* ps = paletteSetOf(s);
    const int grp = g.paletteGroup != 0 ? 1 : 0;
    auto dayXft = [&]() { return ps ? ps->xft[grp][2] : std::string(); };
    auto cloudXft = [&]() { return ps ? ps->xft[grp][4] : std::string(); };
    if ((s.flags() & 0x80000) == 0) {
        g.sitePal[5] = g.sitePal[2];
        g.dayXftName = dayXft();
    } else {
        double blue = save_->worldHeader.m_dBlueSky;
        MakeIntermediatePalette(g.sitePal[4], g.sitePal[2], g.sitePal[5], blue, 0x3e);
        g.dayXftName = blue < 0.5 ? cloudXft() : dayXft();   // (the blend clamps `blue` itself; the test uses the stored value)
    }
    CSite_TintPalette(s, g.sitePal[5]);
}

// dll:1000db70 CSite::OnTimeOfDayChange (vtable +0x20)
//   p1: apply the sound volumes / open sounds, p2: change the palette, steps / immediate: for the palette change,
//   state: 5 dawn, 0xf morning, 0x14 day, 0x1e evening, 0x23 dusk, 0x28 night, frac: progress inside the phase
void BiosGame::CSite_OnTimeOfDayChange(CSite& s, int p1, int p2, int state, int steps, int immediate, double frac) {
    if (CSiteVariant* v = dynamic_cast<CSiteVariant*>(&s)) {   // vtable 100915d0 +0x20
        CSiteVariant_OnTimeOfDayChange(*v, steps, immediate);
        return;
    }
    const PaletteSet* ps = paletteSetOf(s);
    const int grp = g.paletteGroup != 0 ? 1 : 0;
    auto xft = [&](int slot) { return ps ? ps->xft[grp][slot] : std::string(); };   // P + 600 + 100 * slot (A) / +1800 (B)
    int local_c = 0, local_8 = 0;
    switch (state) {
    case 5:
        if (p2 != 0) CEngineFX_Blend(g.sitePal[0], g.sitePal[1], frac, xft(0), xft(1), steps, immediate, 0, 0);
        local_c = static_cast<int>(frac * 900.0 - 900.0);
        local_8 = static_cast<int>(frac * -2100.0 - 900.0);
        s.setUnk0f8(frac * 0.5 * (s.unk0e8() - s.unk0f0()) + s.unk0f0());
        break;
    case 0xf:
        if (p2 != 0) CEngineFX_Blend(g.sitePal[1], g.sitePal[5], frac, xft(1), g.dayXftName, steps, immediate, 0, 0);
        local_c = static_cast<int>(frac * 900.0 - 900.0);
        local_8 = static_cast<int>(frac * -2100.0 - 900.0);
        s.setUnk0f8((s.unk0f0() - s.unk0e8()) * (1.0 - frac) * 0.5 + s.unk0e8());
        break;
    case 0x14:
        if (p2 != 0) CEngineFX_SetPalette(g.sitePal[5], g.dayXftName.c_str(), steps, immediate, 0, 0);
        local_8 = -10000;
        local_c = 0;
        s.setUnk0f8(s.unk0e8());
        break;
    case 0x1e:
        if (p2 != 0) CEngineFX_Blend(g.sitePal[5], g.sitePal[3], frac, g.dayXftName, xft(3), steps, immediate, 0, 0);
        local_c = static_cast<int>(frac * -900.0);
        local_8 = static_cast<int>(frac * 2100.0 - 3000.0);
        s.setUnk0f8((s.unk0f0() - s.unk0e8()) * (frac * 0.5) + s.unk0e8());
        break;
    case 0x23:
        if (p2 != 0) CEngineFX_Blend(g.sitePal[3], g.sitePal[0], frac, xft(3), xft(0), steps, immediate, 0, 0);
        local_c = static_cast<int>(frac * -2100.0 - 900.0);
        local_8 = static_cast<int>(frac * 900.0 - 900.0);
        s.setUnk0f8((s.unk0e8() - s.unk0f0()) * (1.0 - frac) * 0.5 + s.unk0f0());
        break;
    case 0x28:
        if (p2 != 0) CEngineFX_SetPalette(g.sitePal[0], xft(0).c_str(), steps, immediate, 0, 0);
        local_8 = 0;
        local_c = -10000;
        s.setUnk0f8(s.unk0f0());
        break;
    default:
        break;
    }
    CEngineFX_UnlockPalette(8);
    if (p1 == 0) {
        g.lastDiurnal = state;
        return;
    }
    // volumes: the site's day sound rises / falls with local_c, the night sound with local_8, the dead sound follows the
    // plant health
    int deadVol = -10000;
    bool flag100 = (s.flags() & 0x100) != 0;
    if (flag100 && !s.deadSound().empty() && s.cel()->plantHealthAvg() < 0.5) {
        double a = s.cel()->plantHealthAvg();
        deadVol = static_cast<int>(a <= 0.25 ? a * -900.0 : a * -2100.0 - 900.0);
        local_c = local_c - (deadVol + 3000);
        local_8 = local_8 - (deadVol + 3000);
    }
    if (!s.nightSound().empty()) {
        CEngineFX_SetVolume(ambientHandle(s.daySound()), local_c);
        CEngineFX_SetVolume(ambientHandle(s.nightSound()), local_8);
    } else if (!flag100 || deadVol == -10000) {
        CEngineFX_SetVolume(ambientHandle(s.daySound()), 0);
    } else {
        CEngineFX_SetVolume(ambientHandle(s.daySound()), -3000 - deadVol);
    }
    if (!s.deadSound().empty()) CEngineFX_SetVolume(ambientHandle(s.deadSound()), deadVol);

    // close the sound sets of the phase that ended
    bool reopen = false;
    const bool changed = state != g.lastDiurnal;
    SoundSet* wantDay = soundSet(s.buffer(0));
    SoundSet* wantDusk = soundSet(s.buffer(1));
    SoundSet* wantNight = soundSet(s.buffer(2));
    SoundSet* wantDawn = soundSet(s.buffer(3));
    auto check = [&](SoundSet* want, SoundSet*& cur) {
        if (changed || want != cur) {
            reopen = true;
            CSite_CloseSet(cur);
        }
    };
    switch (g.lastDiurnal) {
    case 0:
        reopen = true;
        break;
    default:
        log("CSite::OnTimeOfDayChange close arse " + std::to_string(g.lastDiurnal));
        break;
    case 5:
        check(wantNight, g.setNight);
        check(wantDawn, g.setDawn);
        break;
    case 0xf:
        check(wantDay, g.setDay);
        check(wantDawn, g.setDawn);
        break;
    case 0x14:
        check(wantDay, g.setDay);
        break;
    case 0x1e:
        check(wantDay, g.setDay);
        check(wantDusk, g.setDusk);
        break;
    case 0x23:
        check(wantNight, g.setNight);
        check(wantDusk, g.setDusk);
        break;
    case 0x28:
        check(wantNight, g.setNight);
        break;
    }
    if (!reopen) {
        g.lastDiurnal = state;
        return;
    }
    int slot = 0;
    switch (state) {
    case 0:
        g.lastDiurnal = state;
        return;
    default:
        log("CSite::OnTimeOfDayChange open arse " + std::to_string(g.lastDiurnal));
        g.lastDiurnal = state;
        return;
    case 5: CSite_OpenSounds(s, 0x28); slot = 10; break;
    case 0xf: CSite_OpenSounds(s, 0x14); slot = 10; break;
    case 0x14:
    case 0x28: slot = state; break;
    case 0x1e: CSite_OpenSounds(s, 0x14); slot = 0x20; break;
    case 0x23: CSite_OpenSounds(s, 0x28); slot = 0x20; break;
    }
    CSite_OpenSounds(s, slot);
    g.lastDiurnal = state;
}

// dll:1000ca00 (FUN_1000ca00): the site's FX: start those that may, then play
void BiosGame::CSite_RefreshFx(CSite& s) {
    std::vector<CSpotFX*> list = spotFxOf(s.fx());
    for (CSpotFX* f : list)
        if (CSpotFX_CanStart(*f)) fxStart(*f);
    for (CSpotFX* f : list) fxPlay(*f);
}

// dll:1000ca70 (FUN_1000ca70): vtable +0x28 (close) of every FX of the site
void BiosGame::CSite_StopFx(CSite& s) {
    for (CSpotFX* f : spotFxOf(s.fx())) fxClose(*f);
}

// dll:1000d900 (vtable +0x1c): the site is left: every FX stops
void BiosGame::CSite_Exit(CSite& s) {
    for (CSpotFX* f : spotFxOf(s.fx())) fxStop(*f);
    self().m_pPreviousSite.set(&s);     // _DAT_100c1f88 = this
}

// dll:1000c7c0: after a game was loaded: the ambient sounds start again, the palette group is chosen
void BiosGame::CSite_AfterLoad(CSite& s) {
    g.lastDiurnal = 0;
    if (!s.daySound().empty()) setAmbientHandle(s.daySound(), -1);
    CSite_PlayAmbient(s, s.daySound());
    g.ambDay = s.daySound();
    if (!s.nightSound().empty()) {
        setAmbientHandle(s.nightSound(), -1);
        CSite_PlayAmbient(s, s.nightSound());
    }
    g.ambNight = s.nightSound();
    if (!s.deadSound().empty()) setAmbientHandle(s.deadSound(), -1);
    if ((s.flags() & 0x100) != 0) CSite_PlayAmbient(s, s.deadSound());
    g.ambDead = s.deadSound();
    if (s.cel()) CBiomeCel_UpdatePlantHealthAvg(*s.cel());
    CSite_PaletteGroup(s);
    CSite_LoadPalettes(s);
    CSite_BuildDayPalette(s);
}

// dll:10001290 (FUN_10001290): the display of a link from the site: the vine images by the plant health, the hotspot
// pair "<name>" / "Y<name>" (walkable / overgrown)
void BiosGame::CPath_Display(CPath& p) {
    p.setUnk30(-1);
    p.setUnk34(-1);
    CFeature* cur = dynamic_cast<CFeature*>(self().m_pFeature.item);
    uint32_t flags = p.flags();
    if ((!cur || (cur->flags() & 0x10000000) == 0) && (flags & 10) == 0) {
        CBiomeCel* cel = curCel();
        if ((flags & 0x1000) == 0) {
            if ((flags & 0x20000) != 0) {
                double avg = cel ? cel->plantHealthAvg() : 0.0;
                if (0.9 < avg || ((flags & 0x80000000u) == 0 && avg != 0.0)) {
                    eng_.DisplayImage("vine3", -1, nullptr, 0, -1, nullptr, 0);
                    flags &= 0x7fffffffu;
                    p.setFlags(flags);
                } else if (avg <= 0.6) {
                    if (0.2 < avg) eng_.DisplayImage("vine1", -1, nullptr, 0, -1, nullptr, 0);
                } else {
                    eng_.DisplayImage("vine2", -1, nullptr, 0, -1, nullptr, 0);
                }
            }
        } else if (!cel || 47.5 <= cel->soilWater()) {
            flags |= 0x80000000u;
            p.setFlags(flags);
        } else {
            flags &= 0x7fffffffu;
            p.setFlags(flags);
        }
        std::string name = p.pathName();
        std::string y = name;
        if (!y.empty()) y[0] = 'Y';
        flags = p.flags();
        if ((flags & 0x80000000u) != 0) {
            CEngineFX_HotspotEnable(name, 0, -1);
            CEngineFX_HotspotEnable(y, 1, -1);
            if ((flags & 0x40000000u) == 0) {
                eng_.DisplayImage(y, -1, nullptr, 0, -1, nullptr, 0);
                CEngineFX_ImageVisibility(y, 1, -1);
                return;
            }
            y[1] = 'l';
            eng_.DisplayImage(y, -1, nullptr, 0, -1, nullptr, 0);
            CEngineFX_ImageVisibility(y, 1, -1);
            y[1] = 'r';
            eng_.DisplayImage(y, -1, nullptr, 0, -1, nullptr, 0);
            CEngineFX_ImageVisibility(y, 1, -1);
            return;
        }
        CEngineFX_HotspotEnable(name, 1, -1);
        CEngineFX_HotspotEnable(y, 0, -1);
    }
}

// dll:1000d600 (vtable +0x18): the page of the site was drawn
void BiosGame::CSite_OnPageDrawn(CSite& s, const std::string& pageName) {
    if (CPath* jp = dynamic_cast<CPath*>(self().m_pPath)) story_->CPath_End(*jp);     // dll:10002330 FUN_10002330(path)
    if (s.feature()) {
        if (CFeature* cf = dynamic_cast<CFeature*>(self().m_pFeature.item)) cf->onPageDrawn(pageName);   // DAT_100c1f9c vtable +0x38
    }
    for (Item* it : s.features()) as<CFeature>(it).slot60(pageName.c_str(), 0);   // vtable +0x60(pageName, 0): display of the feature
    if ((s.flags() & 0x40000000) != 0) eng_.SetPanoramaScrollLimits(0x1d5, 0x3bf, 0);
    for (Item* it : s.links()) CPath_Display(as<CPath>(it));
    for (Item* it : s.list60()) CPlantPair_DisplayInDistance(as<CPlantPair>(it), 0);    // dll:10009ce0
    for (Item* it : s.plants()) CPlant_DisplayAll(as<CPlant>(it));                      // dll:10072910
    CSite_PaletteGroup(s);
    // the palette set changes (or the group does): load the five palettes again
    int setIdx = -1;
    if (!s.palette().empty()) {
        int i = palettes_.setForPage(s.name());
        setIdx = i >= 0 ? i : -2;
    }
    if (setIdx != g.lastPaletteSetIndex || g.paletteGroup != g.paletteGroupPrev) {
        g.forcePalette = false;
        CSite_LoadPalettes(s);
        CSite_BuildDayPalette(s);
        g.lastPaletteSetIndex = setIdx;
        g.paletteChanged = true;
        CEngineFX_ClearWindow();
    }
    if (g.forcePalette) {
        g.forcePalette = false;
        g.paletteChanged = true;
        CEngineFX_ClearWindow();
    }
    if (Item* goggles = self().m_pGoggles.item; goggles) {
        // dll:1001d600 FUN_1001d600(goggles): shows the goggles' palette (the object-state byte +0x48 is not 1)
        if (auto* gg = dynamic_cast<ObjGoggles*>(goggles)) gg->showPalette(self());
    }
    if (g.paletteChanged) {
        if (g.startupPhase == 0) {
            CTimeline_UpdateDiurnal(1, 1, 0, 1);
        } else {
            CEngineFX_LoadPalette("palettes\\misc\\black.pal", nullptr, 0, 1, 0, 0);
            eng_.RedrawScreen();
            int dc = 0, s20 = 0, s14 = 0;
            eng_.GetWindowDC(-1, dc, s20, s14);
            // SetBkMode / SetTextAlign / SetTextColor / SetBkColor / TextOutA(dc, 0, 0, "", 0): GDI calls on the DC
            eng_.ReleaseWindowDC(-1, dc, 1, 1);
            g.fadeFromBlack = true;
            g.fadeFromBlackUntil = tick() + 6000;
            CTimeline_UpdateDiurnal(1, 1, g.startupPhase, 1);
            g.startupPhase = 0;
        }
    }
    if (auto* held = dynamic_cast<GameObj*>(self().m_pObjHolding.item); held && (held->flags() & 0x2000000) != 0 && !iequals(s.name(), "ce7")) {
        eng_.RemoveGameObject(held->name());            // dll:1000d600: the held object that is also worn (+0x17 & 2) is taken out of the scene,
        eng_.ReturnGameObject(held->name());            // put back (Eng_Api52) and leaves the hand (vtable +0x7c)
        held->clearHolding(self());
    }
    if (g.redrawPending) {
        eng_.RedrawScreen();
        g.redrawPending = false;
    }
    (void)pageName;
}

// dll:1000cab0 CSite::UpdateDisplayState (vtable +0x14): the site is entered. Sounds, the images of its state (alive /
// dead / flooded / lights), the effects, the feature.
void BiosGame::CSite_UpdateDisplayState(CSite& s, const std::string& pageName) {
    if (CSiteVariant* v = dynamic_cast<CSiteVariant*>(&s)) {   // vtable 100915d0 +0x14
        CSiteVariant_UpdateDisplayState(*v);
        return;
    }
    s.setFlags(s.flags() | 0x80000000u);
    story_->hotspotHandled = false;       // DAT_1009f838 = 0
    g.paletteChanged = false;
    bool differs = g.ambDay != s.daySound();
    if (differs) {
        CSite_PlayAmbient(s, s.daySound());
        CSite_CloseAmbient(g.ambDay);
        g.ambDay = s.daySound();
    }
    g.paletteChanged = differs;
    if (s.nightSound() != g.ambNight) {
        CSite_PlayAmbient(s, s.nightSound());
        CSite_CloseAmbient(g.ambNight);
        g.ambNight = s.nightSound();
        g.paletteChanged = true;
    }
    if (g.ambDead != s.deadSound()) {
        if ((s.flags() & 0x100) != 0) CSite_PlayAmbient(s, s.deadSound());
        CSite_CloseAmbient(g.ambDead);
        g.ambDead = s.deadSound();
        g.paletteChanged = true;
    }
    CBiomeCel& cel = *s.cel();
    CBiomeCel_UpdatePlantHealthAvg(cel);
    const uint32_t powerFlags = static_cast<uint32_t>(save_->worldHeader.m_dwPowerFlags);
    auto img = [&](const char* name, int v) { CEngineFX_ImageVisibility(name, v, -1); };
    auto hot = [&](const char* name, int v) { CEngineFX_HotspotEnable(name, v, -1); };
    auto lights = [&]() {   // the lights-on / lights-off pair (flag 0x400)
        if ((powerFlags & 0x40000000) == 0 || (powerFlags & biomePowerBit(s.biome())) == 0) {
            gs().lightsOn = 0;
            img("lightsoff", 1);
            s.setState(9);
        } else {
            gs().lightsOn = 1;
            img("lightson", 1);
            s.setState(8);
        }
    };
    const COcean* ocean = dynamic_cast<const COcean*>(gs().ocean);
    uint32_t f = s.flags();
    bool toTail = false;
    if ((f & 0x2000000) == 0) {
        if ((f & 0x100) == 0) {
            if ((f & 0x800) == 0) {
                if ((f & 0x400) != 0) {
                    lights();
                    toTail = true;
                } else if ((f & 0x8000) != 0) {
                    uint32_t mask = 0x10;
                    switch (static_cast<char>(self().m_szLocation[1])) {
                    case 0x44: mask = 0x8000010; break;
                    case 0x4f: mask = 0x4000010; break;
                    case 0x50: mask = 0x10000010; break;
                    case 0x52: mask = 0x1000010; break;
                    case 0x53: mask = 0x2000010; break;
                    }
                    gs().lightsOn = CWorld_HasPower(mask) == 0 ? 1 : 0;
                    toTail = true;
                } else if ((f & 0x200) == 0) {
                    toTail = true;
                } else if (0.1 <= (ocean ? ocean->health() : 0.0)) {
                    img("healthy", 1);
                    s.setState(2);
                    toTail = true;
                } else {
                    img("dead", 1);
                    s.setState(1);
                }
            } else {
                if ((save_->header.milestones & 0x10000000) != 0) {
                    img("healthy", 1);
                    if ((s.flags() & 0x80) != 0) hot("yWater", 1);
                    s.setState(2);
                    gs().lightsOn = 0;
                    toTail = true;
                } else if ((f & 0x400) != 0) {
                    if ((f & 0x80) != 0) hot("yWater", 0);
                    lights();
                    toTail = true;
                } else {
                    img("dead", 1);
                    if ((s.flags() & 0x80) != 0) hot("yWater", 0);
                    s.setState(1);
                }
            }
        } else {
            // flag 0x100: a plant-dependent room
            img("dead", 0);
            img("dFlood", 0);
            img("unhealthy", 0);
            img("healthy", 0);
            img("hFlood", 0);
            bool allOk = true;
            f = s.flags();
            if ((f & 0x1000) == 0) {
                for (Item* it : s.links())
                    if ((as<CPath>(it).flags() & 0x80000000u) != 0) allOk = false;   // the path is overgrown
            }
            const double avg = cel.plantHealthAvg();
            if ((f & 0x2000) == 0 || cel.soilWater() < 47.5) {
                if (0.2 < avg || !allOk) {
                    if ((f & 0x200000) == 0 || (cel.flags() & 0x10) != 0) {
                        img("healthy", 1);
                        s.setState(2);
                    } else {
                        img("unhealthy", 1);
                        s.setState(4);
                        if ((s.flags() & 0x80) != 0) hot("yWater", 0);
                    }
                } else {
                    s.setState(1);
                    if ((f & 0x80) != 0) hot("yWater", 0);
                    img("dead", 1);
                    if ((s.flags() & 0x1000) != 0 && 0.01 < avg) {
                        int n = static_cast<int>((avg - 0.01) * 10.526315789473685) + 1;
                        for (int i = 1; i <= n; i++) img(("shoot_" + std::to_string(i)).c_str(), 1);
                    }
                }
            } else if (0.2 < avg) {
                img("hFlood", 1);
                s.setState(2);
                if ((s.flags() & 0x80) != 0) hot("yWater", 1);
            } else {
                img("dFlood", 1);
                s.setState(1);
                if ((s.flags() & 0x80) != 0) hot("yWater", 1);
            }
        }
    } else {
        // flag 0x2000000: the beach, by the ocean's water level
        img("h_high", 0);
        img("h_mid", 0);
        img("h_low", 0);
        img("d_high", 0);
        img("d_mid", 0);
        img("d_low", 0);
        const double lvl = ocean ? ocean->waterVol() : 0.0;
        const double avg = cel.plantHealthAvg();
        if (lvl <= 11.25) {
            if (10.75 <= lvl) {
                img(0.2 < avg ? "h_mid" : "d_mid", 1);
                s.setState(6);
            } else {
                img(0.2 < avg ? "h_low" : "d_low", 1);
                s.setState(7);
            }
        } else {
            img(0.2 < avg ? "h_high" : "d_high", 1);
            s.setState(5);
        }
    }
    (void)toTail;
    // LAB_1000d1b0
    if ((s.flags() & 0x4000) != 0 && (powerFlags & 0x20000000) != 0 && s.state() == 1) img("celights", 1);
    if ((s.flags() & 0x10000) != 0) {
        int night = (tl.diurnal == 0x28 || tl.diurnal == 5 || tl.diurnal == 0x23) ? 1 : 0;
        img("night", night);
        if (iequals(s.name(), "PH1")) img("night_2", night);
    }
    CSite_RefreshFx(s);
    // the feature of the site becomes the current one
    CFeature* cur = dynamic_cast<CFeature*>(self().m_pFeature.item);
    if (cur && s.feature() != cur) cur->onLeave();   // vtable +0x4c of the old current feature
    self().m_pFeature.set(s.feature());
    if (s.feature()) s.feature()->onEnterPage(pageName);   // vtable +0x34 of the site's feature
    // object / feature dependent images of single sites
    const std::string& nm = s.name();
    auto dropZonePage = [&](const char* object, std::string& page) {
        std::array<int, 12> info{};
        eng_.GetGameObjectInfo(object, info);
        int hotspot = 0;
        std::string obj;
        page.clear();
        eng_.GetDropZoneInfo(info[11], page, hotspot, obj);
        return info;
    };
    std::string page;
    if (iequals(nm, "ce3")) {
        dropZonePage("wifelet", page);
        img("wifelet", iequals(page, "nce3dead") ? 1 : 0);
    }
    if (iequals(nm, "ph1")) {
        dropZonePage("mescal1", page);
        img("mescal1", iequals(page, "nph1drink") ? 1 : 0);
        dropZonePage("video1", page);
        img("jenny", iequals(page, "nph1shelve") ? 1 : 0);
        dropZonePage("video3", page);
        img("devlin", iequals(page, "nph1shelve") ? 1 : 0);
    }
    if (iequals(nm, "ph5")) {
        if (CFeature* mir = world().findFeature("fph6mir")) {
            if (mir->active()) img("fph6mir_b", 1);   // +0x34 != 0
        }
    }
    if (iequals(nm, "db4")) {
        dropZonePage("russmemo", page);
        img("russmemo", iequals(page, "ndb4dead") ? 1 : 0);
    }
    if (iequals(nm, "db5")) {
        auto info = dropZonePage("poncho", page);
        if (iequals(page, "ndb5dead")) {
            if (s.state() == 1) {
                img("poncho", 1);
                return;
            }
            if (info[6] == 0) {
                if (GameObj* o = world().findObject("poncho")) {
                    if (eng_.RemoveGameObject(o->name())) o->removeFromScene();   // vtable +0x94 dll:10029510
                }
                img("poncho", 0);
                return;
            }
        } else {
            img("poncho", 0);
        }
    }
    (void)pageName;
}

// dll:100250f0 CSiteVariant::UpdateDisplayState: the special sites (EAngel1, EDeath ...): no feature, no plant; the three
// ambient sounds are swapped (closing the old one first), the palettes loaded, the day sound set opened
void BiosGame::CSiteVariant_UpdateDisplayState(CSiteVariant& s) {
    self().m_pFeature.clear();      // DAT_100c1f9c = 0
    self().m_pPlant.clear();        // DAT_100c1f98 = 0
    bool a = s.daySound() != g.ambDay;
    if (a) {
        CSite_CloseAmbient(g.ambDay);
        CSite_PlayAmbient(s, s.daySound());
        g.ambDay = s.daySound();
    }
    bool b = s.nightSound() != g.ambNight;
    if (b) {
        CSite_CloseAmbient(g.ambNight);
        CSite_PlayAmbient(s, s.nightSound());
        g.ambNight = s.nightSound();
    }
    bool c = s.deadSound() != g.ambDead;
    if (c) {
        CSite_CloseAmbient(g.ambDead);
        CSite_PlayAmbient(s, s.deadSound());
        g.ambDead = s.deadSound();
    }
    bool changed = c || (b || a);
    if (!s.palette().empty()) {
        int i = palettes_.setForPage(s.name());
        int idx = i >= 0 ? i : -2;
        if (idx != g.lastPaletteSetIndex) {
            CSite_LoadPalettes(s);
            CSite_BuildDayPalette(s);
            g.lastPaletteSetIndex = idx;
            changed = true;
        }
    }
    if (changed) CTimeline_UpdateDiurnal(1, 1, 0, 1);
    g.setDay = soundSet(s.buffer(0));
    if (g.setDay) {
        for (size_t i = 0; i < g.setDay->files.size(); i++) {
            if (g.setDay->handles[i] == -1) {
                int h = -1;
                if (eng_.OpenSound(g.setDay->files[i], 0, true, 0, 0, 0, h)) g.setDay->handles[i] = h;
            }
        }
    }
    eng_.EnableMouseDeltaEvent(1);
}

// dll:10025250 CSiteVariant::OnTimeOfDayChange: ignores the time: shows the night palette and the day palette in turn,
// silences the site's sound and plays one random wav of its set half of the time
void BiosGame::CSiteVariant_OnTimeOfDayChange(CSiteVariant& s, int steps, int immediate) {
    if (const PaletteSet* ps = paletteSetOf(s)) {
        if (!g.variantToggle) CEngineFX_SetPalette(g.sitePal[0], ps->xft[0][0].c_str(), steps, immediate, 0, 0);
        else CEngineFX_SetPalette(g.sitePal[2], ps->xft[0][2].c_str(), steps, immediate, 0, 0);
    }
    if (!s.daySound().empty()) CEngineFX_SetVolume(ambientHandle(s.daySound()), 0);
    g.variantToggle = !g.variantToggle;
    if (chancePercent(50.0)) {
        SoundSet* set = soundSet(s.buffer(0));
        if (set && !set->files.empty()) {
            uint32_t n = static_cast<uint32_t>(set->files.size());
            uint32_t k = static_cast<uint32_t>(static_cast<int>(crand()) * static_cast<int>(n)) / 0x7fff;
            // ORIGINAL BUG: rand() == 0x7fff indexes one entry past the set (as in CSite::RandomAmbient)
            if (k >= n && gs().fixes.variantSoundIndex) k = n - 1;   // fix: stay inside the set (same slip as CSite::RandomAmbient)
            if (k < n) eng_.PlaySound(set->handles[k], 1);
        }
    }
}

// dll:10014910 CWorld::HasPower: is the power group `mask` supplied? (the fuse boxes carry the circuits)
uint32_t BiosGame::CWorld_HasPower(uint32_t mask) {
    uint32_t pf = static_cast<uint32_t>(save_->worldHeader.m_dwPowerFlags);
    if ((pf & 0x40000000) == 0) return 0;
    int box;
    if ((mask & 0x1000000) != 0) {
        if ((pf & 0x1000000) == 0) return 0;
        box = 0;
    } else if ((mask & 0x2000000) != 0) {
        if ((pf & 0x2000000) == 0) return 0;
        box = 1;
    } else if ((mask & 0x4000000) != 0) {
        if ((pf & 0x4000000) == 0) return 0;
        box = 2;
    } else if ((mask & 0x8000000) != 0) {
        if ((pf & 0x8000000) == 0 && (mask & 0x10000) == 0) return 0;
        box = 3;
    } else if ((mask & 0x10000000) != 0) {
        if ((pf & 0x10000000) == 0) return 0;
        box = 4;
    } else if ((mask & 0x20000000) != 0) {
        return (pf & 0x20000000) >> 0x1d;
    } else {
        return 1;
    }
    Item* it = gs().features.fuseBox[box];
    if (!it) throw Error("CWorld::HasPower: fuse box " + std::to_string(box) + " does not exist");
    return (as<CFuseBox>(it).fuseMask() & mask) == mask ? 1 : 0;
}

} // namespace biosys::game
