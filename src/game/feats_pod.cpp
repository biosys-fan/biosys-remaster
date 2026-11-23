// CPodTransport (dll:1001b530, vtable 10090dc8): the pod console of the lift room. The destination buttons b_bR / b_bS / b_bD /
// b_bO / b_bP choose the basement of the rainforest, savannah, dead biome, ocean or house (+0x6c = 0x15 .. 0x19); the four
// that are not the rainforest's ask for a password once (the typed word is compared with the string table's 0x14b VISITOR,
// 0x14d HIDDEN, 0x14c RESIDE, 0x14e HOUSE; the bits 1, 4, 2, 8 of world +0x298 remember it); "b_Go" starts the ride (the movie
// pod.avi; its end is the arrival). The display (+0x5c dll:1001b800) is at the end of this file.
#include "game/blobrecords.h"
#include "game/eng.h"
#include "game/feat_power.h"
#include "game/feat_story.h"
#include "game/self_text.gen.h"
#include "game/featenv.h"
#include "game/site.h"
#include <cstring>

namespace biosys::game {

namespace {
uint32_t passwordBits(World& w) {
    SimRefs& r = w.state().sim;
    if (!r.world) return 0;
    uint64_t bits;
    std::memcpy(&bits, &r.world->unk_298, 8);
    return static_cast<uint32_t>(bits);
}
void setPasswordBits(World& w, uint32_t v) {
    SimRefs& r = w.state().sim;
    if (!r.world) return;
    uint64_t bits;
    std::memcpy(&bits, &r.world->unk_298, 8);
    bits = (bits & 0xffffffff00000000ull) | v;
    std::memcpy(&r.world->unk_298, &bits, 8);
}
bool g_podPasswordFailed = false;     // DAT_100a13d8 (per process in the original too; here per call chain)
}

// dll:1001bfa0 FUN_1001bfa0: the password entry of the pod: the text box is opened, the state is 0x32
void CPodTransport::beginPassword() {
    if (host()) {
        host()->playSound("sounds\\features\\enter.wav");
        host()->showMessage(0x154);
        if (Eng* e = host()->eng()) {
            int rect[4] = {0xcf, 0x11b, 0x126, 0x126};
            u8 colour[4] = {0x1c, 0x9a, 0, 0};
            e->api().BeginTextInput(rect, 8, 0, colour);
        }
    }
    unk_58 = 0x32;
}

// dll:1001bda0 CPodTransport::OnHotspot
bool CPodTransport::onHotspot(const std::string& hotspot) {
    if (host() && host()->eng()) host()->eng()->api().EndTextInput(true);
    g_podPasswordFailed = false;
    if (iequals(hotspot, "b_Go") && unk_70 != 0) {
        if (unk_60 < 2.0) {
            if (host() && host()->eng()) host()->eng()->DisplayImage("low_power", -1, nullptr, 1, -1, nullptr, 0);
            return true;
        }
        unk_58 = 0x14;
        display(1);
        cworld::setFXState(world(), 0x100e, 0);
        if (host()) host()->playAvi("movies\\pod\\pod.avi", 0, 0x54, 0, 1, "nowhere", 0, 0, 0, 0);
        unk_74 = cworld::gameTime(world());
        return true;
    }
    struct Dest { const char* button; i32 code; uint32_t bit; };
    static const Dest dests[] = {{"b_bR", 0x15, 0}, {"b_bS", 0x16, 1}, {"b_bD", 0x17, 4}, {"b_bO", 0x18, 2}, {"b_bP", 0x19, 8}};
    for (const Dest& d : dests) {
        if (!iequals(hotspot, d.button)) continue;
        unk_6c = d.code;
        display(1);
        if (d.bit != 0 && (passwordBits(world()) & d.bit) == 0) beginPassword();
        return true;
    }
    return true;     // FUN_10019c80: nothing
}

// dll:1001c070 CPodConsole::OnPasswordEntered (vtable +0x90): `cancelled` = the entry was left
void CPodTransport::onTextInput(bool cancelled, const std::string& text) {
    unk_58 = 10;
    g_podPasswordFailed = false;
    if (cancelled) return;
    struct Dest { i32 code; const char* word; uint32_t bit; };
    static const Dest dests[] = {{0x16, "VISITOR", 1}, {0x17, "HIDDEN", 4}, {0x18, "RESIDE", 2}, {0x19, "HOUSE", 8}};
    for (const Dest& d : dests) {
        if (unk_6c != d.code) continue;
        if (iequals(text, d.word)) setPasswordBits(world(), passwordBits(world()) | d.bit);
        else g_podPasswordFailed = true;
        break;
    }
    if (!g_podPasswordFailed) {
        playSound("sounds\\features\\thankyou.wav");
        onHotspot("b_go");                       // (*(vtable + 0x30))("b_go"): the ride starts
    } else {
        playSound("sounds\\features\\notaccs.wav");
    }
    display(1);
}

// dll:1001c030 (vtable +0xa0): the movie of the ride ended (kind 1): the pod has arrived
bool CPodTransport::onMediaClosed(int, unsigned mask) {
    if (mask != 1) return false;
    unk_58 = 10;
    unk_60 = unk_60 - 2.0;
    unk_68 = unk_6c;
    display(1);
    return true;
}

// dll:1001c350 (+0x60): the pod display light is on while the pod has power
void CPodTransport::slot60(const char*, int arg) {
    if (arg == 0 && cworld::hasPower(world(), m_dwPowerMask) != 0 && host() && host()->eng())
        host()->eng()->DisplayImage("poddisp", -1, nullptr, 0, -1, nullptr, 0);
}

} // namespace biosys::game

namespace biosys::game {

// The pod station (CPodFeature, dll:100026a0): one per biome basement. FUN_10002c30 and friends.

// dll:10002b70 CPodFeature::OnHotspot: b_Call sends the pod here (when it has the charge), b_charge toggles its charging
bool CPodFeature::onHotspot(const std::string& hotspot) {
    playSound("sounds\\features\\click.wav");
    CPodTransport* pod = dynamic_cast<CPodTransport*>(unk_5c);
    if (!pod) return true;
    if (iequals(hotspot, "b_Call")) {
        playSound("sounds\\features\\switch3.wav");
        if (pod->position() == unk_58) return true;
        pod->timeStampNow_ = cworld::gameTime(world());
        pod->callTo(unk_58);                    // `+0x6c = station; FUN_1001c330(pod)`
        display(1);
    }
    if (iequals(hotspot, "b_charge")) {
        if (pod->podState() == 0x1e) pod->stopCharging();
        else pod->startCharging();
        display(1);
    }
    if (!hotspot.empty() && hotspot[0] == 'F') pod->setPodState(10);
    return true;
}

// dll:10002890 (+0x3c): the page of the station was entered while the pod stands here: its door opens
void CPodFeature::onPageEntered(const std::string&) {
    CPodTransport* pod = dynamic_cast<CPodTransport*>(unk_5c);
    if (!pod || pod->position() != unk_58 || !host()) return;
    if (cworld::hasPower(world(), m_dwPowerMask) == 0) return;
    const std::string loc = host()->currentLocation();
    if (!loc.empty() && std::toupper(static_cast<unsigned char>(loc.back())) == 'O') return;
    playSound("sounds\\features\\poddoor.wav");
    if (host()->eng()) host()->eng()->DoHotspotAction("b_Open", -1);
}

// dll:10002c30 CPodFeature::Update (+0x58, no base update): the pod arrived at this station (state 0x28, position here): on the
// station's own page (not the one ending in 'O') the arrival sound plays and the pod is idle again; the stations of the site on
// display just play the sound
void CPodFeature::update(i32) {
    CPodTransport* pod = dynamic_cast<CPodTransport*>(unk_5c);
    if (!pod || pod->podState() != 0x28 || pod->position() != unk_58 || !host()) return;
    FeatureGlobals& g = globals();
    if (host()->isCurrentFeature(this)) {
        const std::string loc = host()->currentLocation();
        if (!(loc.size() && std::toupper(static_cast<unsigned char>(loc.back())) == 'O') && cworld::hasPower(world(), m_dwPowerMask) != 0) {
            const bool ok = host()->playLoopedSoundInto("sounds\\features\\podarriv.wav", &g.podGlobal);   // DAT_1009f424
            pod->setPodState(10);
            if (!ok) onMediaClosed(g.podGlobal, 4);
            return;
        }
    }
    const CSite* cur = host()->currentSite();
    if (cur) {
        bool listed = false;
        for (Item* f : cur->features()) if (f == this) listed = true;           // FUN_1000eb70
        if (listed) {
            const bool ok = host()->eng() && true;
            playSound("sounds\\features\\podarriv.wav");
            pod->setPodState(10);
            if (!ok) onMediaClosed(g.podGlobal, 4);
        }
    }
}

// dll:10002b30 (+0x60): the pod display light
void CPodFeature::slot60(const char*, int arg) {
    if (arg == 0 && cworld::hasPower(world(), m_dwPowerMask) != 0 && host() && host()->eng())
        host()->eng()->DisplayImage("poddisp", -1, nullptr, 0, -1, nullptr, 0);
}

// dll:10002910 (+0xa0): the arrival sound ended: the door opens. ORIGINAL BUG: the original stores 4 (not -1) into the sound
// handle global DAT_1009f424 here.
bool CPodFeature::onMediaClosed(int handle, unsigned mask) {
    FeatureGlobals& g = globals();
    if (g.podGlobal == handle && mask == 4) {
        // fix: -1 is the "no sound" value this global has everywhere else (CPodFeature::Load sets it, dll:10002800)
        g.podGlobal = fixes().podDoorSoundHandle ? -1 : 4;
        playSound("sounds\\features\\poddoor.wav");
        if (host() && host()->eng()) host()->eng()->DoHotspotAction("b_Open", -1);
        return true;
    }
    return false;
}

} // namespace biosys::game

namespace biosys::game {

// dll:10002960 CPodFeature::Display (+0x5c): the station's console page: charge and reserve bars (images drawn clipped to the
// value), the pod's state ("charging" / "coming") and the station the pod is at; no text is drawn.
void CPodFeature::display(int redraw) {
    FeatureHost* h = host();
    if (!h) return;
    Eng* e = h->eng();
    hotspotEnable("b_open", 0);
    if (cworld::hasPower(world(), m_dwPowerMask) == 0) {
        hotspotEnable("b_charge", 0);
        hotspotEnable("b_call", 0);
        return;
    }
    hotspotEnable("bPowerOff", 0);
    if (!e) return;
    auto image = [&](const char* name, int unk20 = -1) { e->DisplayImage(name, -1, nullptr, 0, unk20, nullptr, 0); };
    image("background");
    image("map");
    CPodTransport* pod = dynamic_cast<CPodTransport*>(unk_5c);       // the original dereferences it unchecked
    if (!pod) return;
    image("pod_power", static_cast<int>(pod->charge() * 2.95));
    image("res_power", static_cast<int>(cworld::header(world()).m_dPower * 0.295));
    if (pod->podState() == 0x1e) image("charging");
    else if (pod->podState() == 0x28) image("coming");
    switch (pod->position()) {
    case 0x15: image("br"); break;
    case 0x16: image("bs"); break;
    case 0x17: image("bd"); break;
    case 0x18: image("bo"); break;
    case 0x19: image("bp"); break;
    default: break;
    }
    if (redraw != 0) e->RedrawScreen();
}

// dll:1001b800 CPodTransport::Display (+0x5c): the pod's console in the lift room. The destination buttons and the marker of the
// current position; the destination's lamp and its password / power state ("LocalPower" when the circuit is dead or the fuse is
// out); b_Go and the message line (string 0x14a.. the password the player knows, 0x17a after a wrong one) in dark green.
void CPodTransport::display(int redraw) {
    FeatureHost* h = host();
    if (!h) return;
    Eng* e = h->eng();
    if (e) e->EnableUserInput(0, 0);                                  // Eng_Api2E_EnableUserInput(0, 0)
    if (redraw == 0) playSound("sounds\\features\\pdintcl1.wav");
    auto image = [&](const char* name, int unk20 = -1) { if (e) e->DisplayImage(name, -1, nullptr, 0, unk20, nullptr, 0); };
    image("background");
    for (const char* n : {"l_bpodbr", "l_bpodbs", "l_bpodbo", "l_bpodbd", "l_bpodbp"}) hotspotEnable(n, 0);
    image("power", static_cast<int>(unk_60 * 3.95));
    if (unk_58 == 0x14) {                                             // travelling
        image("go");
        for (const char* n : {"b_br", "b_bs", "b_bo", "b_bd", "b_bp", "b_Go"}) hotspotEnable(n, 0);
    } else {
        for (const char* n : {"b_br", "b_bs", "b_bo", "b_bd", "b_bp"}) hotspotEnable(n, 1);
        static const struct { const char* lamp; const char* image; } here[5] = {
            {"l_bpodbr", "js_br"}, {"l_bpodbs", "js_bs"}, {"l_bpodbd", "js_bd"}, {"l_bpodbo", "js_bo"}, {"l_bpodbp", "js_bp"}};
        if (unk_68 >= 0x15 && unk_68 <= 0x19) {
            hotspotEnable(here[unk_68 - 0x15].lamp, 1);
            image(here[unk_68 - 0x15].image);
        }
        unk_70 = 1;
        std::string text;                                             // DAT_100c0de8
        // by destination: the image, the password the player knows (string), the password bit, the circuit bit of the world's
        // power flags and the fuse box (DAT_100c1150 .. 1160) of that circuit
        struct Dest { const char* image; int string; uint32_t pwBit; uint32_t powerBit; int fuse; };
        static const Dest dest[5] = {{"br", 0x14a, 0, 0x1000000, 0}, {"bs", 0x14b, 1, 0x2000000, 1}, {"bd", 0x14d, 4, 0x8000000, 3},
                                     {"bo", 0x14c, 2, 0x4000000, 2}, {"bp", 0x14e, 8, 0x10000000, 4}};
        if (unk_6c >= 0x15 && unk_6c <= 0x19) {
            const Dest& d = dest[unk_6c - 0x15];
            image(d.image);
            auto load = [&](int id) { const char* t = dllString(id); text = t ? t : ""; };
            if (d.pwBit == 0) {
                load(d.string);                                       // the rainforest needs no password
            } else if ((passwordBits(world()) & d.pwBit) != 0) {
                unk_70 = 1;
                load(d.string);
            } else {
                unk_70 = 0;
            }
            bool ok = false;
            if ((cworld::header(world()).m_dwPowerFlags & d.powerBit) != 0) {
                const CFuseBox* box = dynamic_cast<const CFuseBox*>(globals().fuseBox[d.fuse]);
                ok = box && (box->fuseMask() & 0x8000) != 0;          // byte +0x15 bit 7 of the box
            }
            if (!ok) {
                unk_70 = 0;
                image("LocalPower");
            }
        }
        if (unk_68 == unk_6c) unk_70 = 0;
        if (unk_70 != 0) imgVis("Go", 1);
        hotspotEnable("b_Go", unk_70 != 0 ? 1 : 0);
        h->setTextColours(paletteRgb(0x28, 0x4d, 0x28), 1, 0x02000000);   // FUN_100611b0(&CEngineFX, 0x02284d28, 1, 0x02000000)
        if (g_podPasswordFailed) { const char* t = dllString(0x17a); text = t ? t : ""; }
        h->drawText(0xcf, 0x114, text);                               // FUN_10061500 (also when the text is empty)
    }
    if (redraw != 0 && e) e->RedrawScreen();
}

} // namespace biosys::game
