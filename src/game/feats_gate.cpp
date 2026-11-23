// CGate (dll:1000a200, vtable 1008fd40): the closed gate of Club Eden's dead-biome entrance. CExitDoor (dll:1000b590, vtable
// 1008fef0) is the synth cave "DB6CAVE" ("CSynthCave" in the code): four switches arm four charges; a second press or 40 s in the cave
// bring the synths (fatal) unless they are pacified (the oxygen cylinder's valve open in the cave, synth state 5); with all four armed
// an alarm sounds and when it ends the cave blows up: in the cave that is death ("blownup"), when the player is away the cave is
// blown, the way out "eGameEnd_1" appears and the game can be ended.
#include "game/biomecel.h"
#include "game/blobrecords.h"
#include "game/eng.h"
#include "game/feat_story.h"
#include "game/featenv.h"
#include "game/gameobj.h"
#include "game/obj_gear.h"
#include "game/site.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace biosys::game {

// ---------------------------------------------------------------------------------------------------------------
// CGate

// dll:1000a3b0 CGate::OnHotspot: a closed gate rattles and the player is told it is locked
bool CGate::onHotspot(const std::string&) {
    if (unk_84 == 0) {
        playSound("sounds\\features\\rattle.wav");
        if (host()) host()->showMessage(0xd9);
    }
    return true;
}

// dll:1000a3f0 CGate::OnApply (vtable +0x44): the object used on the gate opens it
void CGate::onApply(GameObj&, const std::string&) {
    FeatureHost* h = host();
    if (h && h->eng()) {
        h->eng()->DisplayImage("arm", -1, nullptr, 0, -1, nullptr, 0);
        h->eng()->EnableUserInput(0, 0);
        h->eng()->RedrawScreen();
    }
    if ((m_dwFlags & 4) != 0 && h) {
        if (GameObj* held = h->heldObject())
            if (h->eng() && h->eng()->RemoveGameObject(held->name())) held->removeFromScene();     // vtable +0x94
    }
    playSound("sounds\\features\\apple.wav");
    if (h && h->eng()) h->eng()->DoHotspotAction("fCrossfade", -1);
    unk_84 = 1;
}

// dll:1000a470 (+0x5c)
void CGate::display(int redraw) {
    hotspotEnable("fCrossfade", 0);
    if (redraw != 0) redrawPage();
}

// dll:1000a4a0 (+0x60): the gate's images and hotspots by its state
void CGate::slot60(const char*, int arg) {
    FeatureHost* h = host();
    const CSite* cur = h ? h->currentSite() : nullptr;
    const bool dead = cur && cur->state() == 1;
    if (unk_84 == 0) {
        if (dead && h && h->eng()) h->eng()->DisplayImage("gateclosed", -1, nullptr, 0, -1, nullptr, 0);
        imgVis("gateopen", 0);
        imgVis("gateclosed", 1);
        hotspotEnable(unk_63.c_str(), 1);
    } else {
        if (dead && h && h->eng()) h->eng()->DisplayImage("gateopen", -1, nullptr, 0, -1, nullptr, 0);
        imgVis("gateopen", 1);
        imgVis("gateclosed", 0);
        hotspotEnable(unk_63.c_str(), 0);
    }
    hotspotEnable(unk_79.c_str(), dead ? 1 : 0);      // (the decompiler lost the argument: the gate's second hotspot follows the state)
    if (arg != 0) redrawPage();
}

// ---------------------------------------------------------------------------------------------------------------
// CExitDoor = CSynthCave. +0x58 armed switches (bit n-1), +0x5c presses, +0x60 time of the last press, +0x64 time of entering,
// +0x68 blown, +0x6c the explosion movie was shown, +0x70 the cel of the cave.
namespace {
struct CaveStatics { int alarmSound = -1, attackMovie = -1; GameObj* cylinder = nullptr; };    // DAT_1009fe00 / 04 / 08
CaveStatics g_cave;
bool inCave(FeatureHost* h) { return h && iequals(h->currentLocation(), "db6cave"); }
}

// dll:1000b740 CSynthCave::OnEnter
void CExitDoor::onEnterPage(const std::string&) {
    FeatureHost* h = host();
    if (inCave(h)) {
        if (unk_68 == 0) {
            hotspotEnable("eGameEnd_1", 0);
            imgVis("normal", 1);
            unk_58 = 0;
            unk_5c = 0;
            unk_60 = 0;
        } else {
            imgVis("blown", 1);
        }
    }
    unk_64 = h ? static_cast<i32>(h->tick()) : 0;            // GetTickCount()
    if (!g_cave.cylinder) {
        g_cave.cylinder = world().findObject("oxy_cyl");
        if (!g_cave.cylinder) return;
    }
    if (auto* cyl = dynamic_cast<ObjOxyCylinder*>(g_cave.cylinder))
        if (cyl->held() && cyl->valveOpen() && h) h->setSynthState(5);       // the open canister pacifies the synths
}

// dll:1000b800 (+0x38): away from the cave after the blast the explosion movie plays once
void CExitDoor::onPageDrawn(const std::string&) {
    if (!inCave(host()) && unk_68 != 0 && unk_6c == 0) {
        if (host()) host()->playAvi("movies\\deadbiom\\explode.avi", 0x130, 0, 0, 1, "nowhere", 0, 1, 0, 0);
        unk_6c = 1;
    }
}

// dll:1000b860 CSynthCave::Display: the armed charges are shown; with four armed the lights go out and the alarm starts
void CExitDoor::display(int) {
    FeatureHost* h = host();
    if (!inCave(h)) return;
    char n[16];
    if (unk_68 != 0) {
        hotspotEnable("eGameEnd_1", 1);
        for (int i = 1; i <= 4; i++) { std::snprintf(n, sizeof n, "bon%d", i); hotspotEnable(n, 0); }
        if (h && h->eng()) h->eng()->RedrawScreen();
        return;
    }
    hotspotEnable("eGameEnd_1", 0);
    for (int i = 0; i < 4; i++) {
        if ((static_cast<unsigned>(unk_58) & (1u << i)) != 0) {
            std::snprintf(n, sizeof n, "charge%d", i + 1);
            if (h && h->eng()) h->eng()->DisplayImage(n, -1, nullptr, 0, -1, nullptr, 0);
            std::snprintf(n, sizeof n, "bon%d", i + 1);
            hotspotEnable(n, 0);
        }
    }
    if (unk_58 == 0xf) {
        unk_68 = 1;
        if (h && h->eng()) h->eng()->DisplayImage("lights", -1, nullptr, 0, -1, nullptr, 0);
        if (h) h->playLoopedSoundInto("sounds\\features\\bioalrm2.wav", &g_cave.alarmSound);
    }
    if (h && h->eng()) h->eng()->RedrawScreen();
}

// dll:1000b9a0 CSynthCave::OnButton: a switch of the cave ("B.." hotspot, not the synth ones) arms its charge; the second press
// brings the synths, unless they are pacified
bool CExitDoor::onHotspot(const std::string& hotspot) {
    FeatureHost* h = host();
    if (inCave(h) && !hotspot.empty() && hotspot[0] == 'B' && hotspot.find("SYNTH") == std::string::npos) {
        playSound("sounds\\features\\switch1.wav");
        const int n = std::atoi(hotspot.c_str() + 3);
        unk_58 = static_cast<u16>(unk_58 | static_cast<u16>(1u << ((n - 1) & 0x1f)));
        display(1);
        unk_60 = h ? static_cast<i32>(h->tick()) : 0;
        unk_5c = unk_5c + 1;
        if (unk_5c > 1 && h && h->synthState() != 5) {
            if (h->currentPage()) h->stopPageFx(h->currentPage());
            if (!h->startAttackMovie("movies\\synth\\deadbiom\\attack.avi", &g_cave.attackMovie)) return false;
        }
    }
    return true;
}

// dll:1000bb90 CSynthCave::OnIdle: 40 s in the cave without the cylinder's valve open bring the synths
void CExitDoor::onIdle() {
    FeatureHost* h = host();
    if (!h || unk_68 != 0 || g_cave.attackMovie != -1 || h->synthState() == 5 || !inCave(h)) return;
    if (h->tick() - static_cast<uint32_t>(unk_64) <= 40000) return;
    if (h->currentPage()) h->stopPageFx(h->currentPage());
    h->startAttackMovie("movies\\synth\\deadbiom\\attack.avi", &g_cave.attackMovie);
}

// dll:1000bd20 CSynthCave::OnMediaEnd: kind 4 = the alarm ended (in the cave: death, away: the cave is blown up), kind 1 = the
// synth attack movie ended (death)
bool CExitDoor::onMediaStopped(int handle, unsigned mask) {
    FeatureHost* h = host();
    if (g_cave.alarmSound == handle && mask == 4) {
        if (inCave(h)) {
            playSound("sounds\\features\\Explsion.WAV");
            if (h) h->closeStatusScreen();
            if (h) h->fadeToColour(0xff, 0xff, 0xff, 2, 100, 0x36);
            if (h) h->killPlayer(0x15);                     // blownup
            return false;
        }
        cworld::setFXState(world(), -0x63be, 0);
        cworld::setFXState(world(), -0x63bd, 0);
        cworld::setFXState(world(), -0x63bc, 0);
        cworld::setFXState(world(), -0x63b6, 1);
        for (const char* f : {"fcavefire1", "fcavefire2", "fcavefire3"})
            if (CFeature* cf = world().findFeature(f)) cf->activate();      // vtable +0x78
        if (m_pCel) m_pCel->setWindSpeed(m_pCel->windSpeed() + 6.4);        // cel +0x80
        g_cave.alarmSound = -1;
        if (g_cave.cylinder && h && h->eng() && h->eng()->RemoveGameObject(g_cave.cylinder->name()))
            g_cave.cylinder->removeFromScene();                              // the canister is destroyed
        return false;
    }
    if (g_cave.attackMovie == handle && mask == 1) {
        if (h) {
            h->playRandomSound("sounds\\russell\\screams\\scream", 5);
            h->fadeToColour(0xff, 0, 0, 0, 0x50, 0x36);
            h->fadeToColour(0, 0, 0, 1, 100, 0x36);
            if (h->eng()) h->eng()->CloseAVIMovie(g_cave.attackMovie);
        }
        g_cave.attackMovie = -1;
        if (h) h->killPlayer(8);                              // synth
        return true;
    }
    return false;
}

// dll:1000bf00 CExitDoor::Update (+0x58, no base update): after the blast the cave's air relaxes towards the outside's
void CExitDoor::update(i32 now) {
    const i32 last = m_dwTimeLastUpdate;
    m_dwTimeLastUpdate = now;
    if (unk_68 == 0 || !m_pCel) return;
    const double dt = static_cast<double>(static_cast<uint32_t>(now - last)) * 0.0001388888888888889;
    CBiomeCel& c = *m_pCel;
    SimRefs& r = world().state().sim;
    const double outsideTemp = r.world ? r.world->m_dExtTemp : 0.0;          // _DAT_100c1168 = CWorld +0x208
    c.setNewAirTemp(((outsideTemp - c.airTemp()) / c.airCapacity()) * dt + c.newAirTemp());
    c.setNewHumidity(((20.0 - c.humidity()) / c.airCapacity()) * dt + c.newHumidity());
    c.setNewCO2(((0.03 - c.co2()) / c.airCapacity()) * dt + c.newCO2());
    c.setNewOxygen(((21.0 - c.oxygen()) / c.airCapacity()) * dt + c.newOxygen());
    c.setNewPollution((-c.pollution() / c.airCapacity()) * dt + c.newPollution());
}

// dll:1000bfd0 (+0x44): an object used on a hotspot of the cave is passed to the feature named like the hotspot, with 'F' first
void CExitDoor::onApply(GameObj& object, const std::string& hotspot) {
    std::string n = hotspot;
    for (char& c : n) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (n.empty()) return;
    n[0] = 'F';
    if (CFeature* f = world().findFeature(n.c_str())) f->onApply(object, hotspot);
}

} // namespace biosys::game
