// CLift (dll:10023660, vtable 10091420): the basement lifts ("frlift", "fslift", "folift", "fdlift", "fplift"). Three floors:
// the basement of the biome, the biome's site and (ocean, house) a third. The doors of the floors are the path hotspots
// "L_LIFT<floor>" (ordinary CPath walks, Event_OnHotspot case 'L'); the lift feature switches the door hotspot of the floor the
// lift stands at, moves the lift between floors (sound, doors image) and keeps the password lock: the lift of a destination
// needs its password typed once on the panel (world +0x298 bit `unk_58`).
#include "game/feat_story.h"
#include "game/eng.h"
#include "game/featenv.h"
#include "game/site.h"
#include "game/blobrecords.h"
#include "game/watertank.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace biosys::game {

namespace {
// the strings of the DLL's string table the lift uses (0x76 PASSWORD:, 0x77 ENTERED, 0x78 REQUIRED, 0x14a.. the passwords)
const char* kPassword[5] = {"LANDLADY", "VISITOR", "RESIDE", "HIDDEN", "HOUSE"};
}

// the password entered bits (world +0x298 = the low dword of the double at WorldHeader::unk_298; `_DAT_100c11f8`)
static uint32_t passwordBits(World& w);
static void setPasswordBits(World& w, uint32_t v);

// dll:10023fd0 FUN_10023fd0: the text entry of the password starts
void CLift::beginPassword() {
    if (!host()) return;
    host()->playSound("sounds\\features\\enter.wav");
    host()->showMessage(0x154);                      // "please enter the password"
    if (Eng* e = host()->eng()) {
        int rect[4] = {0x1d9, 0xd1, 0x235, 0xdd};
        u8 colour[4] = {0, 0x7f, 0, 0};
        e->api().BeginTextInput(rect, 8, 0x7e, colour);
    }
}

// dll:10023e40 CLift::OnHotspot
bool CLift::onHotspot(const std::string& hotspot) {
    FeatureGlobals::LiftState& st = globals().lift;
    if (iequals(hotspot, "b_Unlock")) {
        playSound("sounds\\features\\click.wav");
        if (cworld::hasPower(world(), m_dwPowerMask) != 0) beginPassword();
        return true;
    }
    if (iequals(hotspot, "b_Call")) {
        playSound("sounds\\features\\liftopen.wav");
        return true;
    }
    int floor = hotspot.empty() ? 0 : std::atoi(hotspot.c_str() + hotspot.size() - 1);    // _atoi(name + len - 1): the last character
    if (floor == 0) return true;
    if (st.sound != -1) return true;                 // a ride is on
    st.target = floor;                               // DAT_100c42bc
    playSound("sounds\\features\\click.wav");
    if (st.current == st.target) return true;
    hotspotEnable(unk_66.c_str(), 0);
    hotspotEnable(unk_70.c_str(), 0);
    hotspotEnable(unk_7a.c_str(), 0);
    if (host() && host()->eng()) host()->eng()->DisplayImage("doors", -1, nullptr, 1, -1, nullptr, 0);
    const char* wav = st.target < st.current ? "sounds\\features\\liftdown.wav" : "sounds\\features\\liftup.wav";
    if (!host() || !host()->playLoopedSoundInto(wav, &st.sound)) onMediaClosed(st.sound, 4);
    return true;
}

// dll:10023e10: the page was drawn; the lift's pages (first letter F) are displayed
void CLift::onPageDrawn(const std::string& page) {
    CFeature::onPageDrawn(page);
    if (!page.empty() && page[0] == 'F') display(0);
}

// dll:100241f0: the ride's sound ended: the lift stands at the floor it was sent to
bool CLift::onMediaClosed(int handle, unsigned mask) {
    FeatureGlobals::LiftState& st = globals().lift;
    if (mask == 4 && st.sound == handle) {
        st.sound = -1;
        st.current = st.target;                      // DAT_100c42c0 = DAT_100c42bc
        display(1);
        return true;
    }
    return false;
}

// dll:10024230 +0x60: the light of the lift's display
void CLift::slot60(const char*, int arg) {
    if (arg == 0 && cworld::hasPower(world(), m_dwPowerMask) != 0 && host() && host()->eng())
        host()->eng()->DisplayImage("liftdisp", -1, nullptr, 0, -1, nullptr, 0);
}

// dll:10023860 CLift::Display. On the panel page (the page name does not end in 'I') the password state is shown; inside
// the lift the floor indicator, the images of the floor's view and the door hotspot of the floor are set.
void CLift::display(int redraw) {
    if ((m_dwFlags & 8) != 0) siteStateImages();             // FUN_10019b70
    FeatureGlobals::LiftState& st = globals().lift;
    FeatureHost* h = host();
    if (!h) return;
    const std::string loc = h->currentLocation();
    if (loc.empty() || loc.back() != 'I') {
        if (cworld::hasPower(world(), m_dwPowerMask) == 0) {
            hotspotEnable("b_Call", 0);
            hotspotEnable("b_Unlock", 1);
            return;
        }
        hotspotEnable("bPowerOff", 0);
        // DAT_100c42c0 = _atoi(&DAT_100c1e20 + len + 1) = the number the location ends in, from the third last character
        st.current = loc.size() >= 3 ? std::atoi(loc.c_str() + loc.size() - 3) : 0;
        const CSite* cur = h->currentSite();
        const Biome* biome = cur ? cur->biome() : nullptr;
        if (biome && (biome->addr == 0x100c1208 || biome->addr == 0x100c1ba0 || biome->addr == 0x100c2768)) return;
        if (redraw != 0) redrawPage();
        h->setTextColours(paletteRgb(0, 200, 0), 1, 0x02000000);   // FUN_100611b0(&CEngineFX, 0x0200c800, 1, 0x02000000)
        h->drawText(0x1d8, 0xaa, "PASSWORD:");              // FUN_10061500, string 0x76
        const uint32_t bits = passwordBits(world());
        if (unk_58 != 0 && (bits & static_cast<uint32_t>(unk_58)) == 0) {
            hotspotEnable("b_Call", 0);
            hotspotEnable("b_Unlock", 1);
            h->drawText(0x1d8, 0xbe, "REQUIRED");
            return;
        }
        hotspotEnable("b_Call", 1);
        hotspotEnable("b_Unlock", 0);
        h->drawText(0x1d8, 0xbe, "ENTERED");
        int idx = 0;                                         // which password the panel shows (by the circuit bit of the lift)
        if ((m_dwFlags & 0x4000000) != 0) idx = 4;
        else if ((m_dwFlags & 0x800000) != 0) idx = 3;
        else if ((m_dwFlags & 0x2000000) != 0) idx = 2;
        else if ((m_dwFlags & 0x1000000) != 0) idx = 1;
        h->drawText(0x1d8, 0xd2, kPassword[idx]);
        return;
    }
    // inside the lift
    hotspotEnable(unk_66.c_str(), 0);
    hotspotEnable(unk_70.c_str(), 0);
    hotspotEnable(unk_7a.c_str(), 0);
    for (const char* n : {"js_1", "js_2", "js_3"}) imgVis(n, 0);         // the floor lights (the strings at dll:100a2bc0 / 2bb8 / 2bb0)
    const int cur = st.current;
    const std::string* names[3] = {&unk_66, &unk_70, &unk_7a};
    if (cur >= 1 && cur <= 3) hotspotEnable(names[cur - 1]->c_str(), 1);      // +0x5c + cur * 10
    char img[16];
    std::snprintf(img, sizeof img, "js_%d", cur);
    imgVis(img, 1);
    for (const char* n : {"basement", "basement_e", "healthy_2", "house_2", "dry_2", "wet_2", "dead_2", "healthy_3", "dead_3",
                          "house_3", "doors"})
        imgVis(n, 0);
    const char* show = nullptr;
    auto fuseOk = [&](int i) { Item* f = globals().fuseBox[i]; return f && (static_cast<CFeature*>(f)->flags() & 0x1000) != 0; };
    if (cur == 1) {
        const bool ok = ((m_dwFlags & 0x400000) == 0 || fuseOk(0)) && ((m_dwFlags & 0x1000000) == 0 || fuseOk(1)) &&
                        ((m_dwFlags & 0x2000000) == 0 || fuseOk(2)) && ((m_dwFlags & 0x800000) == 0 || fuseOk(3)) &&
                        ((m_dwFlags & 0x4000000) == 0 || fuseOk(4));
        show = ok ? "basement" : "basement_e";
    } else if (cur == 2) {
        if ((m_dwFlags & 0x2000000) != 0) {
            const COcean* oc = dynamic_cast<const COcean*>(world().state().ocean);
            show = oc && 0.2 <= oc->health() ? "healthy_2" : "dead_2";
        } else if ((m_dwFlags & 0x4000000) != 0) {
            show = "house_2";
        } else if (m_pSite[1]) {
            const CBiomeCel* cel = m_pSite[1]->cel();
            if (0.2 <= cel->plantHealthAvg()) {
                if ((m_dwFlags & 0x1000000) == 0) show = "healthy_2";
                else show = (cel->flags() & 0x10) == 0 ? "dry_2" : "wet_2";
            } else {
                show = "dead_2";
            }
        }
    } else if (cur == 3) {
        if ((m_dwFlags & 0x4000000) == 0) {
            if (m_pSite[2]) show = 0.2 <= m_pSite[2]->cel()->plantHealthAvg() ? "healthy_3" : "dead_3";
        } else {
            show = "house_3";
        }
    }
    if (show) imgVis(show, 1);
    // dll:10023bbf `cmp dword ptr [esp + 0xc], 0`: the function's one argument (two registers pushed, every callee before it removes
    // its own arguments, the function ends `ret 4`). The decompiler listing calls it `param_4`, which is its stack tracking gone
    // wrong, not an argument no caller passes: the original redraws the lift on arrival.
    if (redraw != 0) redrawPage();
}

// dll:10024050 CLift::OnTextInputDone: the password was typed (cancelled = the entry was left)
void CLift::onTextInput(bool cancelled, const std::string& text) {
    FeatureGlobals::LiftState& st = globals().lift;
    st.textFail = 0;
    if (cancelled) return;
    int idx = -1;
    switch (unk_58) {
    case 1: idx = 1; break;
    case 2: idx = 2; break;
    case 4: idx = 3; break;
    case 8: idx = 4; break;
    default: break;
    }
    if (idx > 0 && !iequals(text, kPassword[idx])) st.textFail = 1;
    else if (idx > 0) setPasswordBits(world(), passwordBits(world()) | static_cast<uint32_t>(unk_58));
    if (st.textFail == 0) {
        playSound("sounds\\features\\thankyou.wav");
        playSound("sounds\\features\\liftopen.wav");
        if (host() && host()->eng()) host()->eng()->DoHotspotAction("b_call", -1);
    } else {
        playSound("sounds\\features\\notaccs.wav");
    }
    display(1);
    if (host() && host()->eng()) host()->eng()->RedrawScreen();
}

// WorldHeader::unk_298 is the double at world +0x298: its low dword is the password bit mask (_DAT_100c11f8), the high
// dword (+0x29c) the kinds of path walked (story_path.cpp)
static uint32_t passwordBits(World& w) {
    SimRefs& r = w.state().sim;
    if (!r.world) return 0;
    uint64_t bits;
    std::memcpy(&bits, &r.world->unk_298, 8);
    return static_cast<uint32_t>(bits);
}
static void setPasswordBits(World& w, uint32_t v) {
    SimRefs& r = w.state().sim;
    if (!r.world) return;
    uint64_t bits;
    std::memcpy(&bits, &r.world->unk_298, 8);
    bits = (bits & 0xffffffff00000000ull) | v;
    std::memcpy(&r.world->unk_298, &bits, 8);
}

} // namespace biosys::game
