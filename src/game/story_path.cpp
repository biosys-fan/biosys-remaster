// CPath: the walk from one site to another (dll:10001000 constructor in path.cpp; the behaviour here). A hotspot "L_R1R3" of
// the current site starts a journey (Event_OnHotspot case 'L' checks the energy and stores the path as the player's path,
// DAT_100c1f8c); the page "L_R1R3" the engine then jumps to calls Arrive when it is loaded (CSelf::OnEnterPage) and
// BeginJourney when it is drawn (CSelf::OnPageDrawn); the idle event calls TravelStep, which simulates the world for the
// time of the walk (BeginJourney advanced the clock) and jumps to the destination.
#include "game/story.h"
#include "game/self_text.gen.h"
#include "game/feat_story.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace biosys::game {

namespace {
bool iless(const std::string& a, const std::string& b) { return iequals(a, b); }
std::string fmt(const char* f, const std::string& a) { char buf[64]; std::snprintf(buf, sizeof buf, f, a.c_str()); return buf; }
std::string fmt2(const char* f, const std::string& a, const std::string& b) {
    char buf[64];
    std::snprintf(buf, sizeof buf, f, a.c_str(), b.c_str());
    return buf;
}
}

// dll:1000c960 FUN_1000c960 (CSite+0x80: the site's link list): the link with this name
CPath* StoryLogic::CSite_FindLink(CSite& s, const std::string& name) {
    for (Item* it : s.links()) {
        CPath* p = dynamic_cast<CPath*>(it);
        if (p && iless(p->pathName(), name)) return p;
    }
    return nullptr;
}

// dll:10002680 CPath::GetEnergyCost(cost): ((weight carried * 0.03) + 1) * cost * 0.01
double StoryLogic::CPath_GetEnergyCost(const CPath& p) const {
    return (b.save_->self.m_dWeightCarried * 0.03 + 1.0) * p.unk28() * 0.01;
}

// dll:10001230 CPath::OnHandAction: the hand used on the vines of the path (hotspot "Y_...")
bool StoryLogic::CPath_OnHandAction(CPath& p) {
    uint32_t f = p.flags();
    if ((f & 0x4000) == 0) {
        if ((f & 0x8000) != 0 && (f & 0x100) != 0) {
            CPath_Clear(p, 1);
            return true;
        }
        if ((f & 0x80) != 0) b.CEngineFX_ClearWindow();
    } else if (!held()) {
        Self_Consume(0x136);
        return true;
    }
    return true;
}

// dll:10002540 FUN_10002540: the vines of the path are gone (cut by hand or with a tool): the walkable hotspot "L_.." is
// on, the overgrown one ("Y_..") and its images are off
void StoryLogic::CPath_Clear(CPath& p, int alsoReverse) {
    p.setFlags(p.flags() & 0x7fffffffu);
    if (alsoReverse != 0 && p.to() && p.from()) {
        std::string rev = fmt2("L_%s%s", p.to()->name(), p.from()->name());
        if (CPath* r = CSite_FindLink(*p.to(), rev)) r->setFlags(r->flags() & 0x7fffffffu);
    }
    if ((p.flags() & 0x100) != 0) b.CEngineFX_PlaySound("sounds\\rforest\\treefall.wav", 0, 0);
    std::string y = p.pathName();
    if (!y.empty()) y[0] = 'Y';
    b.CEngineFX_HotspotEnable(p.pathName(), 1, -1);
    b.CEngineFX_HotspotEnable(y, 0, -1);
    if ((p.flags() & 0x40000000) != 0) {
        y[1] = 'l';
        b.CEngineFX_ImageVisibility(y, 0, -1);
        y[1] = 'r';
    }
    b.CEngineFX_ImageVisibility(y, 0, -1);
    if (CSite* s = site())
        for (Item* it : s->plants())
            if (CPlant* pl = dynamic_cast<CPlant*>(it)) plantRefreshFruitImages(*pl);    // FUN_10073090
    b.eng_.api().RedrawPage();
}

// dll:100014e0 CPath::Arrive: the journey page was loaded. The user input is off, the transition palette of the terrain
// and the map images of the places walked through are shown.
bool StoryLogic::CPath_Arrive(CPath& p) {
    b.eng_.EnableUserInput(0, 0);
    b.eng_.ShowCursor(0);
    Self_CloseStatusScreen();                  // FUN_1006c790
    b.CWorld_LocalFxStop();                    // FUN_10014390
    if (me().m_pGoggles.item) b.CEngineFX_UnlockPalette(1);
    if ((p.flags() & 0x80) != 0) b.CEngineFX_ClearWindow();
    const uint32_t u = p.unk20();
    auto pal = [&](const char* f) { b.CEngineFX_LoadPalette(f, nullptr, 0, 1, 10, 0); };
    if ((u & 0x400000) != 0) pal("palettes\\trans\\rftrans.pal");
    else if ((u & 0x800000) != 0) pal("palettes\\trans\\strans.pal");
    else if ((u & 0x40000000) != 0) pal("palettes\\trans\\otrans.pal");
    else if ((u & 0x80000000) != 0) pal("palettes\\trans\\wtrans.pal");
    else if ((u & 0x1000000) != 0) pal("palettes\\trans\\dbtrans.pal");
    else if ((u & 0x4000000) != 0) pal("palettes\\trans\\cetrans.pal");
    else if ((u & 0x2000000) != 0) pal("palettes\\trans\\ttrans.pal");
    else if ((u & 0x8000) != 0) pal("palettes\\trans\\t2trans.pal");
    else if ((u & 0x8000000) != 0) {
        pal("palettes\\trans\\phtrans.pal");
        b.CEngineFX_ImageVisibility("living", 1, -1);
    } else if ((u & 0x10000000) != 0) {
        pal("palettes\\trans\\phtrans.pal");
        b.CEngineFX_ImageVisibility("mezzo", 1, -1);
    } else if ((u & 0x20000000) != 0) {
        pal("palettes\\trans\\comtrans.pal");
        b.CEngineFX_ImageVisibility("command", 1, -1);
    } else if ((u & 0x200000) != 0) {
        pal("palettes\\trans\\btrans.pal");
    }
    auto vis = [&](const std::string& n) { b.CEngineFX_ImageVisibility(n, 1, -1); };
    const std::string from = p.from() ? p.from()->name() : std::string();
    const std::string to = p.to() ? p.to()->name() : std::string();
    const std::string sub = p.pathName().size() > 2 ? p.pathName().substr(2) : std::string();   // "R1R3" of "L_R1R3" (+0x12)
    bool tail = true;
    if ((u & 0x38000000) != 0) {
        vis(from);
        vis(sub);
        vis(fmt("%s_t", to));
        vis(fmt("%s_t", sub));
        vis(fmt("%s_hi", to));
        for (Item* it : world().list(Table::Sites)) {
            CSite& s = as<CSite>(it);
            if (s.biome() && (s.biome()->addr == 0x100c1208 || s.biome()->addr == 0x100c1ba0) && (s.flags() & 0x80000000u) != 0)
                vis(s.name());
        }
        tail = false;
    } else if ((u & 0x200000) == 0) {
        vis(sub);
        vis(fmt("%s_a", sub));
        vis(fmt("%s_t", sub));
        vis(from);
        vis(fmt("%s_hi", to));
        uint32_t biomeAddr = 0;
        if ((u & 0x400000) != 0) biomeAddr = 0x100c1c50;
        if ((u & 0x800000) != 0) biomeAddr = 0x100c0ee8;
        if ((u & 0x1000000) != 0) biomeAddr = 0x100c24e8;
        if ((u & 0x4000000) != 0) biomeAddr = 0x100c2560;
        if ((u & 0x2000000) != 0) biomeAddr = 0x100c25d8;
        if ((u & 0x8000) != 0) biomeAddr = 0x100c25d8;
        if ((u & 0x80000000) != 0) biomeAddr = 0x100c2768;
        tail = false;
        if (biomeAddr != 0) {
            for (Item* it : world().list(Table::Sites)) {
                CSite& s = as<CSite>(it);
                if (s.biome() && s.biome()->addr == biomeAddr && (s.flags() & 0x80000000u) != 0) vis(s.name());
            }
            if (biomeAddr == 0x100c24e8) {
                CSite* ce3 = world().findSite("CE3");
                if (ce3 && (ce3->flags() & 0x80000000u) != 0) vis(ce3->name());
            }
        }
    } else {
        if ((u & 0x10000) != 0) vis("BR");
        if ((u & 0x20000) != 0) vis("BS");
        if ((u & 0x40000) != 0) vis("BO");
        if ((u & 0x80000) != 0) vis("BD");
        if ((u & 0x100000) != 0) vis("BP");
        vis(fmt("%s_a", sub));
        if ((p.flags() & 4) == 0) vis(fmt("%s_hi", to));
        else vis(fmt2("%s%s_hi", from, to));
    }
    (void)tail;
    if ((p.flags() & 0x40) != 0 && p.to() && p.from()) {      // the way back is walkable now
        std::string rev = fmt2("L_%s%s", to, from);
        if (CPath* r = CSite_FindLink(*p.to(), rev)) r->setFlags(r->flags() & 0x7fffffffu);
    }
    return true;
}

// dll:10001ac0 CPath::BeginJourney (CSelf::OnPageDrawn on the journey page): the energy is paid, the clock advanced, the
// terrain sound starts and the cost is shown. Returns 1.
bool StoryLogic::CPath_BeginJourney(CPath& p, const std::string& hotspot) {
    Self& s = me();
    if (std::toupper(static_cast<unsigned char>(hotspot.empty() ? '\0' : hotspot[0])) != 'L') return true;
    if (s.m_pGoggles.item) b.CEngineFX_UnlockPalette(1);
    // ((energy * -0.01 + 1.0) * 3.0 + 1.0) * cost * 2.0, truncated (__ftol)
    s.m_dwJourneySeconds = static_cast<i32>(static_cast<int64_t>((((s.m_dEnergy * -0.01 + 1.0) * 3.0 + 1.0) * p.unk28()) * 2.0));
    double energyCost = CPath_GetEnergyCost(p);
    s.m_dJourneyEnergy = energyCost;           // DAT_100c1e18
    s.m_dJourneyCost = p.unk28();              // DAT_100c1e08
    s.m_dEnergy = s.m_dEnergy - energyCost;
    b.CTimeline_Advance(static_cast<uint32_t>(s.m_dwJourneySeconds), 1);
    s.m_szLastJourneyName.fill(0);
    std::memcpy(s.m_szLastJourneyName.data(), p.pathName().data(), std::min<size_t>(p.pathName().size(), 10));
    const uint32_t f = p.flags();
    const char* wav = nullptr;
    if ((f & 0x100000) != 0) wav = "sounds\\trans\\junjorn1.wav";
    else if ((f & 0x200000) != 0) wav = "sounds\\trans\\mshjorn1.wav";
    else if ((f & 0x400000) != 0) wav = "sounds\\trans\\grsjorn1.wav";
    else if ((f & 0x800000) != 0) wav = "sounds\\trans\\sndjorn1.wav";
    else if ((f & 0x1000000) != 0) wav = "sounds\\trans\\rubble1.wav";
    else if ((f & 0x2000000) != 0) wav = "sounds\\trans\\pipjorn1.wav";
    else if ((f & 0x4000000) != 0) wav = "sounds\\trans\\strstep1.wav";
    else if ((f & 0x8000000) != 0) wav = "sounds\\trans\\spirstep.wav";
    // ORIGINAL BUG: with none of the terrain bits the original still calls PlayLoopedWav with an uninitialised stack buffer
    // as the file name; the port passes the empty name (the call fails as the original's does for a garbage name).
    // fix: no terrain, no footstep loop
    if (wav || !world().state().fixes.journeySoundUninit) b.CEngineFX_PlayLoopedWav(wav ? wav : "", 0, &journeySound, nullptr, 0);
    const uint32_t u = p.unk20();
    if ((u & 0x3c200000) != 0) return true;
    int x, y1, y2, y3;
    if ((u & 0x800000) != 0) { x = 0x241; y1 = 0xf1; y2 = 0x10a; y3 = 0x123; }
    else if ((u & 0x2000000) != 0) { x = 200; y1 = 0x22; y2 = 0x3a; y3 = 0x54; }
    else if ((u & 0x8000) != 0) { x = 0x243; y1 = 0x22; y2 = 0x3c; y3 = 0x54; }
    else { x = 0xd3; y1 = 0xf1; y2 = 0x10a; y3 = 0x123; }
    // the DC of the main window, the system font, text colour (186, 184, 134) on black, transparent (dll:10001ac0)
    GdiSession dc(b.eng_, -1, b.fx.systemFont, [this](int tx, int ty, const std::string& t) { uiRecordText(tx, ty, t); }, true);
    if (!dc.ok()) return true;
    dc.dc().setTextColor(0x0286b8bau);
    dc.dc().setBkColor(0x02000000u);
    dc.dc().setBkMode(kTransparent);
    char buf[64];
    std::snprintf(buf, sizeof buf, "  %0.0f", s.m_dJourneyCost);
    dc.textOutRight(x, y1, buf);                               // the walking distance
    if (s.m_dwJourneySeconds < 0x3c) std::snprintf(buf, sizeof buf, "  <1");
    else std::snprintf(buf, sizeof buf, "  %d", s.m_dwJourneySeconds / 0x3c);
    dc.textOutRight(x, y2, buf);                               // the minutes
    if (energyCost <= 1.0) std::snprintf(buf, sizeof buf, "   <1");
    else std::snprintf(buf, sizeof buf, "   %0.0f", -energyCost);
    dc.textOutRight(x, y3, buf);                               // the energy
    const CBiomeCel* toCel = p.to() ? p.to()->cel() : nullptr;
    const CBiomeCel* fromCel = p.from() ? p.from()->cel() : nullptr;
    auto note = [&](int id, int tx, int ty) { dc.textOut(tx, ty, std::string(dllString(id) ? dllString(id) : "")); };
    if ((u & 0x1400000) == 0) {
        if ((u & 0x800000) != 0) {
            int line = 1;
            if ((f & 0x800000) == 0) {
                note(0x13a, 0x19e, 0x86);
                if (toCel && fromCel && 40.0 < toCel->soilWater() && 40.0 < fromCel->soilWater()) {
                    line = 2;
                    note(0x139, 0x19e, 0x9a);
                }
            } else {
                note(0x13b, 0x19e, 0x86);
            }
            int next = line;
            if ((f & 0x21) == 0 && toCel && toCel->plantHealthAvg() <= 0.2) {
                note(0x13f, 0x19e, line * 0x14 + 0x86);
                next = line + 1;
            }
            if ((f & 0x40000) != 0) note(0x13c, 0x19e, next * 0x14 + 0x86);
            else if ((f & 0x80000) != 0) note(0x13d, 0x19e, next * 0x14 + 0x86);
        }
    } else {
        int line = 1;
        note(0x138, 0x31, 0x86);
        if (toCel && fromCel && 40.0 < toCel->soilWater() && 40.0 < fromCel->soilWater()) {
            line = 2;
            note(0x139, 0x31, 0x9a);
        }
        int next = line;
        if ((f & 0x21) == 0 && toCel && toCel->plantHealthAvg() <= 0.2) {
            note(0x13f, 0x31, line * 0x14 + 0x86);
            next = line + 1;
        }
        if ((f & 0x40000) != 0) note(0x13c, 0x31, next * 0x14 + 0x86);
        else if ((f & 0x80000) != 0) note(0x13d, 0x31, next * 0x14 + 0x86);
    }
    return true;      // the DC is released with (redraw 1) when `dc` goes out of scope
}

// dll:100023b0 CPath::TravelStep (the idle event while a journey is in progress): the world is simulated for the time of the
// walk and the player is sent to the destination page
bool StoryLogic::CPath_TravelStep(CPath& p) {
    // FUN_10007a40(ftol((energy * -0.01 + 1.0) * 1000.0)): a busy wait of that many milliseconds (the journey page stays up, the
    // player is not asked anything). PORT: the window waits on the real clock (Config::busyWait); headless runs do not.
    journeyDelayMs = static_cast<int>(static_cast<int64_t>((me().m_dEnergy * -0.01 + 1.0) * 1000.0));
    if (journeyDelayMs > 0 && b.config_.busyWait) b.config_.busyWait(static_cast<u32>(journeyDelayMs));
    b.CWorld_UpdateWeather();
    b.COcean_UpdateState();
    b.CWorld_UpdateAirFlows();
    b.CWorld_UpdateWaterFlows();
    b.CWorld_UpdateAllObjects();
    b.CWorld_UpdateAllFeatures();
    b.CWorld_UpdateAllPlants();
    b.CSelf_Update(false);
    const std::string dest = p.to() ? p.to()->name() : std::string();
    const uint32_t u = p.unk20();
    if ((u & 0x1000000) != 0 && (u & 1) != 0) {
        CExitDoor* door = dynamic_cast<CExitDoor*>(world().findFeature("fdb6exit"));
        if (!door || door->unk68() == 0 || door->unk6c() != 0) {
            b.CEngineFX_JumpToPage(dest, p.unk0c(), 0x148, 1, 1);
            return false;
        }
        b.CEngineFX_JumpToPage(dest, 0x8c, 0, 1, 0);
        return false;
    }
    const uint32_t f = p.flags();
    if ((f & 1) != 0) {
        b.CEngineFX_JumpToPage(fmt("%s_i", dest), 0, 0, 1, 1);
        return false;
    }
    if ((f & 0x10) == 0) {
        b.CEngineFX_JumpToPage(dest, p.unk0c(), (f & 4) == 0 ? 0x148 : 0, 1, 1);
        return false;
    }
    b.CEngineFX_JumpToPage(dest, p.unk0c(), 0x80, 1, 1);
    return false;
}

// dll:10002330 FUN_10002330 (CSite::Exit while a journey is in progress): the journey ends; the terrain sound stops and the
// kind of path is remembered in the world (+0x29c rainforest, +0x2a0 savannah)
void StoryLogic::CPath_End(CPath& p) {
    me().m_pPath = nullptr;
    b.CEngineFX_CloseSound(&journeySound);
    journeySound = -1;
    uint32_t u = p.unk20();
    WorldHeader& wh = b.save_->worldHeader;
    if ((u & 0x400000) != 0) {
        uint64_t bits;
        std::memcpy(&bits, &wh.unk_298, 8);
        bits |= static_cast<uint64_t>(u) << 32;                // _DAT_100c11fc |= unk20
        std::memcpy(&wh.unk_298, &bits, 8);
        return;
    }
    if ((u & 0x800000) != 0) wh.unk_2a0 = static_cast<i32>(static_cast<uint32_t>(wh.unk_2a0) | u);   // _DAT_100c1200
}

// dll:10002380 FUN_10002380: the journey page is left
bool StoryLogic::CPath_PageLeft() {
    b.CEngineFX_UnlockPalette(10);
    b.g.lastPaletteSetIndex = -1;                      // DAT_100c425c = 0
    me().m_dwJourneyTime = b.save_->timeline.m_dwTime;  // _DAT_100c1e20
    return true;
}

// dll:1006e220 CSelf::Journey (hotspot 'J' + letter): L = on foot, U = up, S = down: the energy and nutrition it costs and the
// clock it advances (the player worker's translation, self_page.cpp)
bool StoryLogic::CSelf_Journey(char letter) { return me().Journey(static_cast<u8>(letter)) != 0; }

} // namespace biosys::game
