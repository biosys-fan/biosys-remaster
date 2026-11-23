// Event_OnKeyDown (dll:1001e6b0) / Event_OnKeyUp (dll:10021790): the keys of the game (Tab status, Caps Lock environment,
// F1 help texts, F5 / F6 save and load, Escape menu) and the typed cheat words. The typed keys accumulate in a string
// (`DAT_100c429c`, as the virtual-key codes: letters and digits are their own characters) that is emptied after 2 s without a
// key; Backspace deletes the last character. `SILVERFEATHER` unlocks the words (cheat flag 2); a word that matches (exactly, or
// with the digits the word takes) does its work and empties the buffer.
#include "game/story.h"
#include <cstdio>
#include <cstdlib>

// DAT_1009f844: the menu slot text colour passed as the second argument of Eng_OpenMenu (dll:10076600).
static const unsigned char kMenuTextColour[4] = {0x73, 0x52, 0x00, 0x00};


namespace biosys::game {

namespace {
std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
}

// dll:10021790 Event_OnKeyUp. The status screens are shown while their key is down: Tab (9) and Caps Lock (0x14) end them; Shift ends the
// held message; any other key goes to the object in the hand, or to the one under the pointer on the status bar (vtable +0x58, OnKey:
// `allow` = the engine's doDefault, so the object can refuse the S / D / Space default).
void StoryLogic::onKeyUp(int vkey, int, int, int& doDefault) {
    Self& s = me();
    if (vkey == 9) {
        if (s.m_bStatusScreenVisible != 0) {            // DAT_100c2470
            b.CEngineFX_ClearWindow();
            Self_ShowStatus(0);
        }
        return;
    }
    if (vkey == 0x10) {
        s.m_dwUnk278 = 0;                               // _DAT_100c1fb0
        Self_ShowMessage(0xffff, 0xffff, 0);
        return;
    }
    if (vkey == 0x14) {
        Self_ShowEnvironmentStatus(0);
        return;
    }
    int overBar = 0;
    std::string name;
    if (b.eng_.api().GetStatusBarObjectUnderCursor(overBar, name) != 0) return;
    GameObj* o = nullptr;
    if (overBar == 0) o = dynamic_cast<GameObj*>(s.m_pObjHolding.item);
    else o = world().findObject(name.c_str());
    if (o) o->onKey(s, vkey, reinterpret_cast<u32*>(&doDefault));
}

// A typed string (port): one WM_KEYDOWN per character, then Enter when asked
void StoryLogic::typeWord(const std::string& text, bool enter) {
    int doDefault = 1;
    for (char c : text) onKeyDown(static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(c))), 1, 0, doDefault);
    if (enter) onKeyDown(13, 1, 0, doDefault);
}

// dll:1001e6b0
void StoryLogic::onKeyDown(int vkey, int /*repeat*/, int flags, int& doDefault) {
    Self& s = me();
    if ((flags & 0x4000) != 0 && vkey != 8) return;         // auto repeat (bit 14 of lParam), except Backspace
    if (!b.g.gameRunning) { if (CPage* pg = page()) if (pageOnKey(*pg, vkey)) return; }
    if (b.g.gameEnd) { if (CPage* pg = page()) if (pageOnKey(*pg, vkey)) return; }
    if (b.g.dead) { if (CPage* pg = page()) if (pageOnKey(*pg, vkey)) return; }
    if (s.m_bAwake == 0) {                                    // DAT_100c1fe4: asleep
        b.CEngineFX_StopLoopedAvi();
        return;
    }
    if (s.m_bInsideAngelSequence != 0 || s.m_bInsidePeyoteSequence != 0 || b.g.synthState == 6 || subtech.state == 6 ||
        locChar() == 'U' || locChar() == 'X')
        return;
    CFeature* f = feature();
    switch (vkey) {
    case 9:     // Tab: the personal status
        if ((!f || f->unk54() == 0) && b.g.synthState != 4 && subtech.state != 4) b.CEngineFX_ClearWindow();
        Self_ShowStatus(1);
        return;
    case 0x10:
        return;
    case 0x14:  // Caps Lock: the environment status
        Self_ShowEnvironmentStatus(1);
        return;
    case 0x1b:  // Escape: the menu, unless the pod's panel is in its charging state
        if (!f || !iequals(f->featureName(), "fPod") || podState(*f) != 0x14) {
            b.CTimeline_SetTicksPerGameSec(-1);
            b.eng_.api().OpenMenu("", kMenuTextColour, 0);
            doDefault = 0;
        }
        return;
    case 0x74:  // F5
        b.CTimeline_SetTicksPerGameSec(-1);
        b.eng_.api().OpenMenu("savegame", kMenuTextColour, 0);
        doDefault = 0;
        return;
    case 0x75:  // F6
        b.CTimeline_SetTicksPerGameSec(-1);
        b.eng_.api().OpenMenu("loadgame", kMenuTextColour, 0);
        doDefault = 0;
        return;
    case 0x70:  // F1: the help texts. ORIGINAL BUG: the case falls through into the typing code with the key's value
        s.m_dwDreamsSeen ^= 0x80000000u;
        Self_ShowMessage((s.m_dwDreamsSeen & 0x80000000u) == 0 ? 0x169 : 0x168);
        if (world().state().fixes.helpKeyFallthrough) return;     // fix: a help key is not a typed letter (F5, F6 above return too)
        break;
    default:
        break;
    }
    if (vkey > 0xf && vkey < 0x12) return;                    // Shift, Ctrl
    const uint32_t now = b.tick();
    if (now - typedTick_ > 2000) typed_.clear();
    typedTick_ = now;
    if (vkey == 8) {
        if (!typed_.empty()) typed_.pop_back();
        return;
    }
    typed_.push_back(static_cast<char>(vkey));
    typed_ = upper(typed_);
    if ((cheatFlags & 2) == 0) {
        if (typed_ == "SILVERFEATHER") {
            cheatFlags |= 2;
            b.CEngineFX_PlaySound("sounds\\misc\\mileston.wav", 0, 0);
            typed_.clear();
        }
        return;
    }
    cheatWord();
}

// the words (dll:1001e6b0 after SILVERFEATHER). Each does its work and empties the buffer.
void StoryLogic::cheatWord() {
    Self& s = me();
    GameState& gs = world().state();
    const std::string w = typed_;
    auto done = [&]() { typed_.clear(); lastCheat = w; };
    auto num = [&](size_t from) { return std::atoi(w.c_str() + from); };
    auto numd = [&](size_t from) { return std::atof(w.c_str() + from); };
    auto prefix = [&](const char* word, size_t total) { return w.size() == total && w.compare(0, std::strlen(word), word) == 0; };
    auto redrawSite = [&]() {
        if (CSite* cs = site()) b.CSite_UpdateDisplayState(*cs, loc());
        b.eng_.api().RedrawPage();
    };
    auto sitePlants = [&](double v, bool all) {          // the plants of the current site: health members
        if (CSite* cs = site())
            for (Item* it : cs->plants())
                if (CPlant* p = dynamic_cast<CPlant*>(it)) {
                    p->setLongTermHealth(v);
                    p->setShortTermHealth(all ? v : p->shortTermHealth());
                    p->setHealth(v);
                    p->setHealthState(0);
                }
    };
    auto restorePlayer = [&]() {                          // TZING: DAT_100c1da0.. = 100.0 and the fumes cleared
        s.m_dHealth = 100.0;
        s.m_dTemperature = 50.0;
        s.m_dEnergy = 100.0;
        s.m_dHydration = 100.0;
        s.m_dNutrition = 100.0;
        s.m_dProtein = 100.0;
        s.m_dwFumes = 0;
    };
    if (w == "TEST") {                                    // pod power
        CFeature* pod = world().findFeature("fPod");
        char t[64];
        std::snprintf(t, sizeof t, "pod power = %f", pod ? podCharge(*pod) : 0.0);
        b.eng_.api().NoOp13(t, 7);
        return done();
    }
    if (w == "BLOWFUSE") { cworld::blowFuse(world(), 0x8001000); return done(); }
    if (w == "HAPPYENDING") { b.CEngineFX_JumpToPage("eGameEnd_0", 0, 0, 1, 0); return done(); }
    if (prefix("DAMPA", 7)) { mouseFilterSet(numd(5) * 0.01, mf.feedback, mf.window); mouseFilterReset(); return done(); }
    if (prefix("DAMPB", 7)) { mouseFilterSet(mf.gain, -numd(5), mf.window); mouseFilterReset(); return done(); }
    if (prefix("DAMPS", 7)) { mf.window = num(5); mouseFilterReset(); return done(); }
    if (prefix("DAMPM", 7)) { mouseFilterReset(); return done(); }
    if (w == "NOSYNTHS") { b.g.synthState = 7; return done(); }
    if (w == "CRASH") { s.m_dHealth = 10.0; CSynthHandler_Attack(); return done(); }
    if (w == "BANG") { b.CEngineFX_PlayAvi("movies\\deadbiom\\explode.avi", 0x130, 0, 0, 1, "nowhere", 0, 1, 0, 0); return done(); }
    if (w == "WHERE") { b.eng_.api().NoOp13(loc(), 0x18); return done(); }
    if (prefix("PLANTS", 8)) { sitePlants(numd(6) * 0.01, false); redrawSite(); return done(); }
    if (w == "HEALTHY" || w == "DEAD" || w == "WET" || w == "DRY") {
        sitePlants(w == "DEAD" ? 0.0 : 1.0, true);
        if (w == "WET" || w == "DRY") if (CBiomeCel* c = b.curCel()) c->setSoilWater(w == "WET" ? 47.5 : 0.0);
        redrawSite();
        return done();
    }
    if (w == "OCEANDEAD" || w == "OCEANOK") {
        setOceanHealth(w == "OCEANOK" ? 100.0 : 0.0);
        b.CEngineFX_ImageVisibility("healthy", 0, -1);
        b.CEngineFX_ImageVisibility("dead", 0, -1);
        redrawSite();
        return done();
    }
    if (w == "TZING") {
        restorePlayer();
        b.CEngineFX_PlayRandomSound("sounds\\russell\\sighs\\sigh", 4);
        return done();
    }
    if (w == "HUNKYDORY") {
        restorePlayer();
        setOceanHealth(100.0);
        uint32_t& power = b.save_->worldHeader.m_dwPowerFlags;
        if ((power & 0x20000000u) != 0) {
            power &= 0xdfffffffu;
            cworld::powerOffFeatures(world(), 0x20000000);
            power |= 0x8000000u;
            cworld::powerOnFeatures(world(), 0x8000000);
        }
        for (Item* it : world().list(Table::Cels)) {
            CBiomeCel& c = as<CBiomeCel>(it);
            c.setPollution(0); c.setNewPollution(0);
            c.setOxygen(21.0); c.setNewOxygen(21.0);
            c.setCO2(0.03); c.setNewCO2(0.03);
        }
        sitePlants(1.0, true);
        redrawSite();
        return done();
    }
    if (w == "WATER") { Self_Drink(0xbe); return done(); }
    if (w == "ADRENALINE") { Self_Consume(0x1e); return done(); }
    if (w == "TRIP") { Self_Consume(0xb4); return done(); }
    if (w == "BANANA") { Self_Eat(); return done(); }
    if (w == "FF" || w == "ZZ") {
        b.CTimeline_Advance(w == "FF" ? 0xe10 : 0xa8c0, 1);
        b.CTimeline_UpdateDiurnal(1, 1, 0, 1);
        if (CPage* pg = page()) b.CPage_UpdateFx(*pg);          // FUN_1000f730
        if (CSite* cs = site()) b.CSite_RefreshFx(*cs);         // FUN_1000ca00
        b.CWorld_LocalFxUpdate();                               // FUN_10014350
        return done();
    }
    if (prefix("SKIP", 6)) {
        b.CTimeline_Advance(static_cast<uint32_t>(std::atol(w.c_str() + 4) * 0xe10), 1);
        b.CTimeline_UpdateDiurnal(1, 1, 0, 1);
        return done();
    }
    if (w == "TIME") { b.eng_.api().NoOp13(b.tl.clock, 0x18); return done(); }
    if (w == "MALARIA") { Self_CatchMalaria(); return done(); }
    if (w == "CHLOROQUINE") { Self_Consume(0x50); return done(); }
    if (w == "CINCHONA") { Self_Consume(0x96); return done(); }
    if (w == "KILLKILLKILL") return done();
    if (w == "MIXON" || w == "MIXOFF") {
        int on = w == "MIXON" ? 1 : 0;
        cworld::switchAirFlows(world(), 0x29, on);
        cworld::switchAirFlows(world(), 0x2a, on);
        cworld::switchAirFlows(world(), 0x2c, on);
        return done();
    }
    if (w == "DRAINON" || w == "DRAINOFF") {
        int on = w == "DRAINON" ? 1 : 0;
        cworld::switchWaterFlows(world(), 100, on);
        cworld::switchWaterFlows(world(), 0x2a, on);
        return done();
    }
    if (prefix("MS", 4)) {
        uint32_t bit = 1u << (num(2) & 31);
        char t[48];
        std::snprintf(t, sizeof t, "Milestone %x", bit);
        gs.message = t;
        b.eng_.WriteLog(t);
        CGame_TriggerEvent(bit);
        return done();
    }
    if (prefix("MORERED", 10)) { b.fx.tintR = num(7); b.CEngineFX_ShowPalette(); return done(); }
    if (prefix("MOREGREEN", 12)) { b.fx.tintG = num(9); b.CEngineFX_ShowPalette(); return done(); }
    if (prefix("MOREBLUE", 11)) { b.fx.tintB = num(8); b.CEngineFX_ShowPalette(); return done(); }
    if (prefix("LESSRED", 10)) { b.fx.tintR = -num(7); b.CEngineFX_ShowPalette(); return done(); }
    if (prefix("LESSGREEN", 12)) { b.fx.tintG = -num(9); b.CEngineFX_ShowPalette(); return done(); }
    if (prefix("LESSBLUE", 11)) { b.fx.tintB = -num(8); b.CEngineFX_ShowPalette(); return done(); }
    if (w == "IMMORTAL") { cheatFlags |= 1; return done(); }
    if (w == "NOCHEAT") { cheatFlags = 0; return done(); }
    if (w == "DUMPSELF") { for (const FieldLog& f : s.dump()) b.eng_.WriteLog(f.name + " = " + f.text); return done(); }
    if (w == "DUMPFEATURES") {
        for (Item* it : world().list(Table::Features)) {
            std::vector<std::string> lines;
            as<CFeature>(it).dump(lines);
            for (const std::string& l : lines) b.eng_.WriteLog(l);
        }
        b.eng_.WriteLog("features: " + std::to_string(world().list(Table::Features).size()));
        return done();
    }
    if (w.compare(0, 4, "DUMP") == 0 && w.size() > 4) {
        // DUMPWORLD DUMPCELS DUMPTANKS DUMPOCEAN DUMPAIRFLOWS DUMPWATERFLOWS DUMPPLANTS DUMPOBJECTS DUMPSENSORS DUMPFX
        // DUMPPAGEFX DUMPSITEFX DUMPSPOTFX: the original writes the class dumps through Eng_WriteLog (CWorld::Dump
        // dll:10012fc0, FUN_100131d0..10013380). PORT: only the count of the list is written.
        static const struct { const char* word; Table t; } lists[] = {
            {"DUMPCELS", Table::Cels}, {"DUMPTANKS", Table::Tanks}, {"DUMPAIRFLOWS", Table::Airflows},
            {"DUMPWATERFLOWS", Table::Waterflows}, {"DUMPPLANTS", Table::Plants}, {"DUMPOBJECTS", Table::Objects},
            {"DUMPFX", Table::Fx}, {"DUMPSPOTFX", Table::Fx}};
        for (const auto& e : lists)
            if (w == e.word) {
                b.eng_.WriteLog(w.substr(4) + ": " + std::to_string(world().list(e.t).size()));
                return done();
            }
        if (w == "DUMPWORLD" || w == "DUMPOCEAN" || w == "DUMPSENSORS" || w == "DUMPPAGEFX" || w == "DUMPSITEFX") {
            b.eng_.WriteLog(w);
            return done();
        }
    }
    // GOTO<page>\r (the Enter key ends it)
    if (w.size() > 5 && w.compare(0, 4, "GOTO") == 0 && w.back() == '\r') {
        b.CEngineFX_PlaySound("sounds\\misc\\teleport.wav", 0, 0);
        b.CEngineFX_FadeToColour(0xff, 0xff, 0xff, 1, 100, 0x36, 0);
        b.eng_.ClearWindow(-1, 0xff);
        Self_CloseStatusScreen();
        s.m_dwUnk2a8 = 0;                                  // _DAT_100c1fe0
        b.CEngineFX_JumpToPage(w.substr(4, w.size() - 5), 0, 0x148, 0, 0);
        b.CEngineFX_FadeToCurrent(1);
        return done();
    }
    // the numeric environment words of the current cel / the player: HEALTHnn ENERGYnn NUTRITIONnn HYDRATIONnn TEMPnn CLOUDnn
    // SOILTEMPnn AIRTEMPnn HUMIDITYnn OXYGENnn CO2nn SMELLnn POWERnn GOGGLESnn PLANTHEALTHnn
    struct NumWord { const char* word; };
    auto nn = [&](const char* word) { return prefix(word, std::strlen(word) + 2); };
    CBiomeCel* cel = b.curCel();
    if (nn("HEALTH")) { s.SetHealth(numd(6)); return done(); }                 // CSelf::SetHealth dll:10066790 (clamps to the maximum)
    if (nn("ENERGY")) { s.SetEnergy(numd(6)); return done(); }                 // dll:10066760
    if (nn("NUTRITION")) { s.SetNutrition(numd(9)); return done(); }           // dll:100667c0
    if (nn("HYDRATION")) { s.SetHydration(numd(9)); return done(); }           // dll:10066730
    if (nn("TEMP")) { s.m_dTemperature = num(4); return done(); }
    if (cel && nn("AIRTEMP")) { cel->setAirTemp(num(7)); cel->setNewAirTemp(num(7)); return done(); }
    if (cel && nn("SOILTEMP")) { cel->setSoilTemp(num(8)); cel->setNewSoilTemp(num(8)); return done(); }
    if (cel && nn("HUMIDITY")) { cel->setHumidity(num(8)); cel->setNewHumidity(num(8)); return done(); }
    if (cel && nn("OXYGEN")) { cel->setOxygen(num(6)); cel->setNewOxygen(num(6)); return done(); }
    if (cel && nn("CO2")) { cel->setCO2(num(3) / 10000.0); cel->setNewCO2(num(3) / 10000.0); return done(); }
    if (cel && nn("SMELL")) { cel->setPollution(num(5)); cel->setNewPollution(num(5)); return done(); }
    if (nn("CLOUD")) { b.save_->worldHeader.m_dSunlight = num(5) * 0.01; return done(); }
    if (nn("POWER")) { b.save_->worldHeader.m_dPower = num(5); return done(); }
    if (nn("GOGGLES")) { Self_SetGogglesPower(num(7)); return done(); }
    if (nn("PLANTHEALTH")) { sitePlants(num(11) * 0.01, true); redrawSite(); return done(); }
}

} // namespace biosys::game
