// Event_OnHotspot (dll:1001db50) and the small input events next to it: hover (event 33), right click (39), mouse delta
// (40), the status-bar object (43), the event 35 / 44 questions and the BS4 screen events. The first letter of the hotspot
// name selects the handler (the table is the jump table at dll:1001e290 / 1001e2d0, indexed by letter - 'A'):
//   A plant   B object / feature   D drop zone   E page   F N R S U X nothing   J journey (by letter)
//   L path   M demo   O message   P stop FX   Y use (the object / hand on the thing)   anything else: non-standard name
#include "game/story.h"
#include "game/plant.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace biosys::game {

namespace {
std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
}

StoryLogic::StoryLogic(BiosGame& game) : b(game) {
    subtech.sound = -1;     // FUN_10022920: +0x18 = -1
    // ORIGINAL BUG: the synth handler's constructor (FUN_100084b0) initialises nothing; the sound handle is 0
    // fix: -1 = "no sound", which the handler tests (story_handlers.cpp) and which the sub-tech handler sets above.
    // (Applies at construction: Config::fixes, not a later setFixes.)
    synth.sound = game.gs().fixes.synthSoundHandleInit ? -1 : 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Event_OnHotspot

// dll:1001db50. Returns 1: the engine performs the hotspot's own action; anything else suppresses it.
int StoryLogic::onHotspot(const std::string& name, int /*window*/, const std::string& pageName) {
    GameState& gs = world().state();
    const std::string up = upper(name);        // CharUpperA of a copy in a 12-byte local; hotspot names are at most 10 characters
    bool nonStandard = false;
    auto done = [&](const char* kind, int rc) {
        lastKind = kind;
        trace.push_back(name + " " + kind);
        return rc;
    };
    if (!b.g.gameRunning) {     // DAT_1009f824 == 0: the intro runs
        if (!intro_) intro_ = dynamic_cast<CIntroPage*>(world().findPage("eIntro"));    // DAT_100a180c
        if (!intro_) return done("intro-none", 0);
        return done("intro", CIntroPage_OnHotspot(*intro_, up));    // page vtable +0x34
    }
    if (CSynthHandler_OnHotspot(up)) return done("synth", 0);
    const char first = up.empty() ? '\0' : up[0];
    switch (first) {
    case 'A': {
        CSite* s = site();
        if (s && s->state() == 1) {
            CPlant* pl = world().findPlant(up.c_str());
            if (pl && (pl->flags() & 8) == 0) Self_ShowMessage(0x159);
            return done("A:plant", 0);
        }
        return done("A:plant", 1);
    }
    case 'B': {
        const char pc = static_cast<char>(std::toupper(static_cast<unsigned char>(pageName.empty() ? '\0' : pageName[0])));
        if (pc == 'X') {
            if (held()) return done("B:object", heldHotspot(up));                    // object vtable +0x70
        } else if (CFeature* f = feature()) {
            return done("B:feature", f->onHotspot(up) ? 1 : 0);                   // feature vtable +0x30
        }
        return done("B:none", 1);
    }
    case 'D': {
        if (GameObj* h = held(); h && (h->flags() & 0x80000) != 0) return done("D:held-blocked", 0);   // +0x16 & 8
        int zone = 0;
        bool found = name.size() <= 10 && b.eng_.api().GetDropZoneForHotspot(name, zone) == 0;   // Eng_GetDropZoneForHotspot dll:10075010
        if (!found) { nonStandard = true; break; }
        std::string zonePage, zoneObject;
        int zoneHotspot = 0;
        b.eng_.GetDropZoneInfo(zone, zonePage, zoneHotspot, zoneObject);
        if (zoneObject.empty()) {
            if (held()) heldDropOn(up);                                           // object vtable +0x90
        } else if (GameObj* o = world().findObject(zoneObject.c_str())) {
            return done("D:dropzone-object", Self_CanPickUp(o->weight()));
        }
        return done("D:dropzone", 1);
    }
    case 'E':
        if (iequals(up, "eExit")) {
            me().m_dwPeyoteTripTimer = 0x2d1;    // DAT_100c1f10 (CSelf+0x1d8)
            return done("E:exit", 1);
        }
        if (CPage* pg = page()) pageOnHotspot(*pg, up);                           // page vtable +0x34 (the result is ignored)
        return done("E:page", 1);
    case 'F': case 'N': case 'R': case 'S': case 'U': case 'X':
        return done("none", 1);
    case 'J':
        return done("J:journey", CSelf_Journey(up.size() > 1 ? up[1] : '\0') ? 1 : 0);
    case 'L': {
        CSite* s = site();
        CPath* p = s ? CSite_FindLink(*s, up) : nullptr;
        if (!p) return done("L:nopath", 0);
        double cost = CPath_GetEnergyCost(*p);
        if (1.0 <= cost && me().m_dEnergy < cost + 1.0) {
            b.CEngineFX_PlayRandomSound("sounds\\russell\\moans\\moan", 5);
            Self_ShowMessage(0x32);       // too tired
            return done("L:tired", 0);
        }
        if (1.0 <= cost && me().m_dEnergy < me().m_dWeightCarried) {
            b.CEngineFX_PlayRandomSound("sounds\\russell\\moans\\moan", 5);
            Self_ShowMessage(0x34);       // carrying too much
            return done("L:heavy", 0);
        }
        me().m_pPath = p;                 // DAT_100c1f8c
        return done("L:path", 1);
    }
    case 'M':
        b.gs().message = "WARNING: demo hotspot " + up + " triggered using non-demo verion of dll";
        b.eng_.WriteLog(b.gs().message);
        b.CEngineFX_FadeToColour(0, 0, 0, 0, 100, 0x36, 0);
        // ORIGINAL BUG: the test is `strcmpi(&local_20, "MEXIT")` on a stack slot 12 bytes behind the name copy (never
        // written for names of up to 10 characters), not on the name itself: the quit is never reached.
        // fix: the string says what was meant: the hotspot MEXIT quits the game
        if (world().state().fixes.demoExitQuits && up == "MEXIT") b.eng_.QuitGame();
        return done("M:demo", 1);
    case 'O':
        Self_ShowMessage(0xc6);   // (the decompiler shows register garbage here: the code pushes 0xc6, 0xffff, 0)
        return done("O:message", 1);
    case 'P':
        b.eng_.DoHotspotAction(up.substr(1), -1);
        if (CPage* pg = page()) {
            pageStopFxByHotspot(*pg, up.substr(1), 0x24);     // FUN_1000fa60
            pageStopFxByHotspot(*pg, up.substr(1), 0x28);     // FUN_1000fb30
        }
        return done("P:fx", 1);
    case 'Y': {
        GameObj* h = held();
        if (!h) {
            if (CFeature* f = feature()) return done("Y:feature", f->onHotspot(up) ? 1 : 0);
            std::string n = up;
            n[0] = 'F';
            if (CFeature* f = world().findFeature(n.c_str())) f->onHotspot(up);    // the feature's own hotspot (vtable +0x30)
            n[0] = 'L';
            if (CSite* s = site()) if (CPath* p = CSite_FindLink(*s, n)) CPath_OnHandAction(*p);
            if (up.find("HOGWEED") != std::string::npos) Self_Consume(300);
        } else if (iequals(up, "yCylinder") && h->nameStringId() == 0xe8) {
            std::string hs = "dCylinder";    // Eng_Api48_DropHeldObject("dCylinder") (dll:1001e05a: one call, the named zone)
            b.eng_.api().DropHeldObject(&hs);
        } else {
            std::string n = up;
            n[0] = 'L';
            if (CSite* s = site()) if (CPath* p = CSite_FindLink(*s, n)) CPath_OnHandAction(*p);
        }
        // the drink / eat part (the current plant, or water and worms)
        if (!me().m_pPlant.item) {
            if (!h || !iequals(h->name(), "canteen")) {
                if (up.find("WATER") == std::string::npos) {
                    if (up.find("WORMS") != std::string::npos && Self_Drink(200) != 0) {
                        int r = b.crand();
                        int idx = (r & 1) + 1;    // ((r ^ s) - s & 1 ^ s) - s + 1 with s = r >> 31 = 0
                        char wav[64];
                        std::snprintf(wav, sizeof wav, "\\sounds\\russell\\disgust\\drinkyk%d.wav", idx);
                        b.eng_.api().PlayObjectUseAnimation("anims\\obj_use\\water.tan", wav, true, false, 200, 0xda, 1, 0x42, "", -2);
                        Self_ShowMessage(0x15a);
                    }
                } else if (Self_Drink(0xbe) != 0) {
                    b.CEngineFX_PlayRandomSound("\\sounds\\russell\\drinking\\drink", 4);
                    b.eng_.api().PlayObjectUseAnimation("anims\\obj_use\\water.tan", "", true, false, 200, 0xda, 1, 0x42, "", -2);
                }
            }
        } else if (CPlant* pl = dynamic_cast<CPlant*>(me().m_pPlant.item)) {
            // FUN_10073140(plant, name): a hotspot of a plant page used by hand
            if (up[0] == 'Y' && (!h || (h->flags() & 0x1000) == 0)) {       // held +0x15 & 0x10
                if ((pl->flags() & 0x20) != 0) {
                    plantPickFruit(*pl, up);             // FUN_100731b0(plant, NULL, name)
                } else if (up.find("CACTUS") != std::string::npos) {
                    b.CEngineFX_FadeToColour(0xff, 0, 0, 1, 0x14, 0x36, 0);    // FUN_10060620
                    b.CEngineFX_PlayRandomSound("sounds\\russell\\pain\\pain", 5);
                }
            }
        }
        return done("Y:use", 1);
    }
    default:
        nonStandard = true;
        break;
    }
    if (nonStandard) {
        // The message is built in the text buffer DAT_100c2650 and not logged (the original never shows it).
        gs.message = "BiosGame.dll: a hotspot (" + name + ") has been given a non-standard name, and frankly I'm appalled";
    }
    return done("nonstandard", 1);
}

// ---------------------------------------------------------------------------------------------------------------
// pages

// page vtable +0x34 (dll:1000fe00 for the generic page and CDeathPage: 0)
int StoryLogic::pageOnHotspot(CPage& p, const std::string& name) {
    if (auto* ip = dynamic_cast<CIntroPage*>(&p)) return CIntroPage_OnHotspot(*ip, name);
    if (auto* gp = dynamic_cast<CGameEndPage*>(&p)) return CGameEndPage_OnHotspot(*gp, name);
    return 0;
}

// dll:1000fa60 (slot 0x24 = stop) / dll:1000fb30 (slot 0x28 = close): every FX of the page whose file name contains `hotspot`
void StoryLogic::pageStopFxByHotspot(CPage& p, const std::string& hotspot, int slot) {
    for (const std::vector<Item*>* l : {&p.occFx(), &p.occLoopedFx(), &p.fx()}) {
        for (CSpotFX* f : b.spotFxOf(*l)) {
            if (f->file().find(hotspot) == std::string::npos) continue;
            if (slot == 0x24) b.fxStop(*f);
            else b.fxClose(*f);
        }
    }
}

// dll:1000f940: stop every FX of the page (the occasional ones, the looped ones, the page's own)
void StoryLogic::pageCancelFx(CPage& p) {
    for (const std::vector<Item*>* l : {&p.occFx(), &p.occLoopedFx(), &p.fx()})
        for (CSpotFX* f : b.spotFxOf(*l)) b.fxStop(*f);
}

// dll:1000f9c0: stop the FX of the page that switch under `id`
void StoryLogic::pageDisableFx(CPage& p, int id) {
    for (const std::vector<Item*>* l : {&p.occFx(), &p.occLoopedFx(), &p.fx()})
        for (CSpotFX* f : b.spotFxOf(*l))
            if (f->id() == static_cast<uint16_t>(id)) b.fxStop(*f);
}

// ---------------------------------------------------------------------------------------------------------------
// the other input events

// dll:100223c0 Event_OnHotspotHover: the player's hotspot hint (CSelf+0x287 name, +0x294 expiry)
void StoryLogic::onHotspotHover(const std::string& name, int, bool entered) {
    Self& s = me();
    hoverTick = b.tick();                       // DAT_1009f834
    if (!entered) {
        s.m_szHotspotName[0] = 0;               // DAT_100c1fbf._0_1_ = 0
        s.m_dwHotspotExpiry = 0;                // _DAT_100c1fcc
        if (s.m_bUnk2a4 != 0) {                 // DAT_100c1fdc
            s.m_dwUnk298 = 0;                   // _DAT_100c1fd0
            uiClearHint();                      // FUN_1006c700(&CSelf): fills the hint line with the background
            s.m_bUnk2a4 = 0;
        }
        return;
    }
    const std::string up = upper(name);
    s.m_szHotspotName.fill(0);
    std::memcpy(s.m_szHotspotName.data(), up.data(), std::min<size_t>(up.size(), s.m_szHotspotName.size() - 1));
    s.m_dwHotspotExpiry = static_cast<i32>(hoverTick + 500);
}

// dll:100224d0 Event_OnRightClick
void StoryLogic::onRightClick() {
    if (held()) heldRightClick();                                // object vtable +0x74
    if (CFeature* f = feature()) f->slot50();                    // feature vtable +0x50
}

// dll:10022510 Event_OnMouseDelta -> FUN_100618a0(&CEngineFX, dx, dy): the synth stalker's mouse-follow filter
void StoryLogic::onMouseDelta(int& dx, int& dy) {
    synthMouseDelta(dx, dy);
}

// dll:10022860 Event_Slot43: the status bar object under the cursor changed
void StoryLogic::onStatusBarObject(int, const std::string& name, int arg) {
    if (arg != 0) return;
    // DAT_100c1fa4 = CWorld::FindObject(name); _DAT_100c1fd8 = 0
    me().m_pObjTarget = world().findObject(name.c_str());
    me().m_dwUnk2a0 = 0;
    if (!me().m_pObjTarget) return;
    heldStatusBar();                                             // object vtable +0x28
    if (((me().m_dwDreamsSeen) >> 31) != 0) {                    // FUN_1006f320: help texts on
        const GameObj* o = dynamic_cast<const GameObj*>(me().m_pObjTarget);
        if (o && (o->flags() & 0x20000000) != 0) Self_QueueMessage(0x178);   // +0x17 & 0x20
        else Self_QueueMessage(0x174);
    }
}

// dll:100224b0 Event_Slot35: may the right click return to the saved page (DAT_1009f838 set by a hotspot handler)
bool StoryLogic::onEvent35(const std::string&) { return !hotspotHandled; }

// dll:100228f0 Event_Slot44
bool StoryLogic::onEvent44() {
    if (b.g.synthState == 6) return false;                       // DAT_100c27e4
    return subtech.state != 6;                                   // DAT_100c24bc == 6 (the sub-tech handler's state)
}

// dll:100224c0 Event_Slot36 -> CSelf dll:1006ede0
void StoryLogic::onReturnToSavedPage(const std::string& page) { Self_ReturnToSavedPage(page); }

// dll:10022450 Event_OnSimStatusHotspot
void StoryLogic::onSimStatusHotspot(const std::string& name) {
    Self_ShowStatus(0);
    if (!iequals(name, "sleep")) {
        Self_Rest();
        return;
    }
    if (!Self_CanSleep()) {
        Self_ShowMessage(199);
        return;
    }
    Self_Sleep(0, 1, 1);
}

// dll:10021ae0 Event_OnBS4Screen: the control panel screen is entered (0) / left (1)
void StoryLogic::onBS4Screen(int leaving) {
    if (leaving == 0) {
        b.g.noPalette = true;                                    // DAT_100c275c: the modal flag
        CGame_TriggerEvent(0x20);
        return;
    }
    if (leaving == 1) {
        b.g.noPalette = false;
        b.CEngineFX_ClearWindow();
        Self_CloseStatusScreen();
        b.tl.diurnal = 0;                                        // DAT_100c1c44 = 0
        b.gs().sim.diurnal = 0;
        b.CTimeline_UpdateDiurnal(1, 1, 0, 1);
    }
}

// CTime::Format of the game clock (MFC, strftime formats). dll:10015630 sets the CTime behind DAT_100c1c34 to the base CTime(2007, 12, 27, 0, 0, 1,
// DST 0) (dll:10015050, DAT_100c4268) plus days * 86400 + time of day seconds, days = m_dwTime / 0x15180. The port uses plain calendar
// arithmetic; the original formats in the machine's local time zone (a daylight-saving shift of the hour is not modelled).
namespace {
int64_t daysFromCivil(int y, int m, int d) {        // days since 1970-01-01 (proleptic Gregorian)
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
void civilFromDays(int64_t z, int& y, int& m, int& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yoe + era * 400 + (m <= 2));
}
}

// dll:10021b50 Event_OnBS4Query: the control panel asks for data. Ids 5 (does the feature exist), 6 (game time), 7 (the left clock field:
// CTime::Format("%H:%M", format string dll:100a1f50)) and 0xb (the right clock field: CTime::Format("%a %#d %b %Y", dll:100a1f40)).
bool StoryLogic::onBS4Query(int id, void* buffer) {
    switch (id) {
    case 5:
        return world().findFeature(static_cast<const char*>(buffer) + 0x1f8) != nullptr;
    case 6:
        *static_cast<int32_t*>(buffer) = b.save_->timeline.m_dwTime;
        return true;
    case 7: case 0xb: {
        const Timeline& t = b.save_->timeline;
        const int64_t secs = static_cast<int64_t>(static_cast<uint32_t>(t.m_dwTime) / 0x15180u) * 86400 + static_cast<uint32_t>(t.m_dwTimeOfDay) + 1;
        const int64_t day = daysFromCivil(2007, 12, 27) + secs / 86400;
        char text[64];
        if (id == 7) {
            std::snprintf(text, sizeof text, "%02d:%02d", static_cast<int>(secs / 3600 % 24), static_cast<int>(secs / 60 % 60));
        } else {
            static const char* const wd[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
            static const char* const mon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            int y, m, d;
            civilFromDays(day, y, m, d);
            std::snprintf(text, sizeof text, "%s %d %s %d", wd[((day + 4) % 7 + 7) % 7], d, mon[m - 1], y);
        }
        std::memcpy(buffer, text, std::strlen(text) + 1);
        return true;
    }
    default:
        return false;
    }
}

// dll:10022380 Event_OnTextInputDone: the text entry mode ended: the current feature's vtable +0x90 gets the result
void StoryLogic::onTextInputDone(bool cancelled, const std::string& text) {
    if (CFeature* f = feature()) featureTextInput(*f, cancelled, text);
}

} // namespace biosys::game
