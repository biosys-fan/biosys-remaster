// The behaviour of the story feature classes whose constructors / saves are in feat_story.cpp: CPoison, CSpFeature, CDedFeature,
// CMicroscope, CThermalCooker, CMirror, CBloodFeature, CBees, CCoffeeMachine, CTerminal, CDataTerminal, CVidLink, CCabinet, CMedBox,
// CForestFeature. Each function is a translation of the vtable slot (named after feature.h) with the address in the comment.
// The shell calls the original makes through CEngineFX globals (palette lock, window clear, page jump) go through FeatureHostExt
// (feat_hostext.h); the text the pages draw goes through FeatureHost::openDc, so every string drawn is also recorded.
#include "game/biomecel.h"
#include "game/blobrecords.h"
#include "game/eng.h"
#include "game/feat_hostext.h"
#include "game/feat_story.h"
#include "game/gameobj.h"
#include "game/obj_food.h"
#include "game/obj_paper.h"
#include "game/self.h"
#include "game/self_text.gen.h"
#include "game/site.h"
#include "game/vidmessage.h"
#include <cstdio>
#include <cstring>
#include <map>

namespace biosys::game {

namespace {

// DLL globals of these classes (one set per world)
struct StoryGlobals {
    char poiPage[24] = {};      // DAT_100c0dd0: the page of the POI terminal that was shown last (a char[24] up to DAT_100c0de8)
    int termiteCount = 0;       // DAT_100c4278: the living grubs / termites of the terminals' objects
    u32 terminalLastIdle = 0;   // _DAT_100a1114
};
StoryGlobals& G(World& w) {
    static std::map<const World*, StoryGlobals> m;
    return m[&w];
}

std::string fmt(const char* f, const std::string& s) {
    char b[64];
    std::snprintf(b, sizeof b, f, s.c_str());
    return b;
}
std::string fmt(const char* f, const std::string& s, int n) {
    char b[64];
    std::snprintf(b, sizeof b, f, s.c_str(), n);
    return b;
}
std::string fmt(const char* f, int n) {
    char b[64];
    std::snprintf(b, sizeof b, f, n);
    return b;
}
char lastChar(const std::string& s) { return s.empty() ? '\0' : s.back(); }   // `param_1[strlen - 1]`
const char* str(int id) {
    const char* t = dllString(id);
    return t ? t : "";
}
Eng* engOf(FeatureHost* h) { return h ? h->eng() : nullptr; }

// Eng_DisplayImage(image, -1, 0, unk04, -1, 0, 0)
void displayImage(FeatureHost* h, const std::string& image, int unk04 = 0) {
    if (Eng* e = engOf(h)) e->DisplayImage(image, -1, nullptr, unk04, -1, nullptr, 0);
}
void doHotspot(FeatureHost* h, const std::string& hotspot) {
    if (Eng* e = engOf(h)) e->DoHotspotAction(hotspot, -1);
}
void userInputOff(FeatureHost* h) {   // Eng_Api2E_EnableUserInput(0, 0)
    if (Eng* e = engOf(h)) e->EnableUserInput(0, 0);
}

// CDC::DrawText(text, -1, rect, DT_WORDBREAK) on a recording session: the lines the Canvas lays out are each drawn and recorded
// at the pitch of the font (the clip of the rectangle is not applied). Returns the height of the text.
// PORT: GdiSession has no drawText; the line-by-line output is what Canvas::drawText draws for left-aligned DT_WORDBREAK text.
int drawWrapped(GdiSession& dc, const std::string& text, const GdiRect& r) {
    Canvas& c = dc.dc();
    const std::vector<std::string> lines = c.layout(text.data(), static_cast<int>(text.size()), r.right - r.left, DT_WORDBREAK);
    if (lines.empty()) {
        if (!text.empty()) dc.textOut(r.left, r.top, text);     // no font: record what would be drawn
        return 0;
    }
    const int lh = c.textExtent("X").cy;
    int y = r.top;
    for (const std::string& l : lines) {
        if (!l.empty()) dc.textOut(r.left, y, l);
        y += lh;
    }
    return y - r.top;
}

// the three text colours the DLL uses on these pages: PALETTERGB(0, 135, 166) (0x02a68700)
constexpr u32 kInfoColour = paletteRgb(0, 135, 166);

void setInfoStyle(GdiSession& dc) {
    dc.dc().setTextColor(kInfoColour);
    dc.dc().setBkMode(kTransparent);
}

} // namespace

void resetStoryGlobals(World& w) { G(w) = StoryGlobals(); }

// =============================================================================================================================
// CPoison (vtable 1008f880): the POI information terminal. Its pages "uPOI...": the palette is locked (token 3) while one is shown.

// dll:10006560
void CPoison::onEnterPage(const std::string& page) {
    FeatureHost* h = host();
    FeatureHostExt* x = hostExt(h);
    userInputOff(h);
    StoryGlobals& g = G(world());
    auto remember = [&]() {      // strcpy(DAT_100c0dd0, page): the bytes behind the terminator stay
        const size_t n = std::min<size_t>(page.size() + 1, sizeof g.poiPage);
        std::memcpy(g.poiPage, page.c_str(), n);
    };
    if (iequals(page, featureName())) {
        unk_54 = 0;
        if (world().state().lightsOn == 0) {
            imgVis("lightsoff", 1);
            hotspotEnable("uPOITitle", 0);
        } else {
            imgVis("lightson", 1);
        }
        if (g.poiPage[0] == 'U') {
            if (x) x->unlockPalette(3);
            if (h) h->resetDiurnal();      // DAT_100c1c44 = 0 ... CTimeline::UpdateDiurnal(1, 1, 0, 1); ClearWindow in between
            if (x) x->clearWindow();
            display(0);
            g.poiPage[0] = 0;
        }
        return;
    }
    unk_54 = 1;
    if (x) x->lockPalette(3);
    if (iequals(page, "upoititle")) {
        if (x) {
            x->clearWindow();
            x->loadPalette("palettes\\poi\\poititle.pal", nullptr, 0, 0, 3, 0);
        }
        remember();
        return;
    }
    playSound("sounds\\features\\poipage.wav");
    if (page.size() > 4 ? page[4] != g.poiPage[4] : g.poiPage[4] != 0) {   // param_1[4] != DAT_100c0dd4
        const char* pal = nullptr;
        if (iequals(page, "upoiintro1")) pal = "palettes\\poi\\poiintro.pal";
        else if (iequals(page, "upoiprof1")) pal = "palettes\\poi\\poiprof.pal";
        else if (iequals(page, "upoihist1")) pal = "palettes\\poi\\poihist.pal";
        if (pal) {
            if (x) {
                x->clearWindow();
                x->loadPalette(pal, nullptr, 0, 0, 3, 0);
            }
        }
    }
    remember();
}

// dll:10006810
void CPoison::onLeavePage() {
    if (unk_54 != 0) {
        if (FeatureHostExt* x = hostExt(host())) x->unlockPalette(3);
        if (host()) host()->forcePalette();        // DAT_100c3630 = 1
    }
}

// dll:100067f0
void CPoison::display(int redraw) {
    if (redraw != 0) redrawPage();
}

// dll:10006770 / dll:100067b0 (the same code): the page of the terminal shows the new state, or the player is taken there
static void poisonPowerChanged(CFeature& f, FeatureHost* h) {
    if (!h) return;
    if (h->isAtPage(f.featureName().c_str())) {
        f.display(1);
        return;
    }
    if (h->isCurrentFeature(&f))
        if (FeatureHostExt* x = hostExt(h)) x->jumpToPage(f.featureName().c_str(), 0, 0, 1, 0);
}
void CPoison::onPowerOn() { poisonPowerChanged(*this, host()); }
void CPoison::onPowerOff() { poisonPowerChanged(*this, host()); }

// =============================================================================================================================
// CSpFeature (vtable 1008fdf8): the spirometer

// dll:1000ac70: on the page whose name ends in 'O' the movie of the result plays (the player's symptoms 0x8008 = bad lungs)
void CSpFeature::onEnterPage(const std::string& page) {
    FeatureHost* h = host();
    userInputOff(h);
    if (lastChar(page) != 'O') return;
    if (cworld::hasPower(world(), m_dwPowerMask) == 0 || !h) return;
    FeatureHostExt* x = hostExt(h);
    Self* me = x ? x->player() : nullptr;
    if (!me) return;     // PORT (host gap): the symptoms live in the player
    if ((me->m_dwSymptoms & 0x8008) != 0) {
        h->playAvi("movies\\house\\spiro\\bad.avi", 0x14, 0x1c, 0, 0, "nowhere", 0, 0, 0, 0);
        return;
    }
    h->playAvi("movies\\house\\spiro\\healthy.avi", 0x14, 0x1c, 0, 0, "nowhere", 0, 0, 0, 0);
}

// dll:1000ad00: the title line on the 'O' page
void CSpFeature::onPageDrawn(const std::string& page) {
    FeatureHost* h = host();
    if (lastChar(page) != 'O' || cworld::hasPower(world(), m_dwPowerMask) == 0 || !h) return;
    std::unique_ptr<GdiSession> dc = h->openDc(-1);
    if (!dc) return;
    setInfoStyle(*dc);
    dc->textOut(0x18, 10, str(0x7c));
    // SelectObject(old font); Eng_ReleaseWindowDC(-1, dc, 0, 1)
}

// dll:1000b140
void CSpFeature::onResume() { onMediaClosed(0, 1); }

// dll:1000ae40: the movie ended (mask 1): the findings are written on the page
bool CSpFeature::onMediaClosed(int, unsigned mask) {
    if (mask != 1) return false;
    FeatureHost* h = host();
    FeatureHostExt* x = hostExt(h);
    Self* me = x ? x->player() : nullptr;
    const u32 symptoms = me ? me->m_dwSymptoms : 0;     // PORT (host gap): without the player no symptom is set
    if (!h) return true;
    std::unique_ptr<GdiSession> dc = h->openDc(-1);
    if (!dc) return true;
    setInfoStyle(*dc);
    dc->textOut(0x18, 10, str(0x7c));
    const bool bad = (symptoms & 0x8008) != 0;
    drawWrapped(*dc, str(bad ? 0x7e : 0x7d), {0x18, 0x1e, 0x1ac, 100});
    dc->textOut(0x18, 0x78, str(0x7f));
    int yTop = 0x8c;
    if (bad) {
        drawWrapped(*dc, str(0x81), {0x18, 0x8c, 0x1ac, 400});
        yTop = 0xb4;
    }
    const bool second = (symptoms & 0x100) != 0;
    if (second) drawWrapped(*dc, str(0x80), {0x18, yTop, 0x1ac, 400});
    if (!second && !bad) drawWrapped(*dc, str(0x82), {0x18, 0x8c, 0x1ac, 400});
    drawWrapped(*dc, str(bad ? 0x84 : 0x83), {0x18, 0xe6, 0x1ac, 400});
    dc.reset();                                   // Eng_ReleaseWindowDC(-1, dc, 1, 1): the page is shown
    if (Eng* e = engOf(h)) e->RedrawScreen();     // PORT: openDc releases without the redraw flag
    return true;
}

// =============================================================================================================================
// CDedFeature (vtable 10090638): the rock in the way ("b_Rock" strains against it; strong enough with adrenaline or ephedrine)

// dll:100169a0
bool CDedFeature::onHotspot(const std::string& hotspot) {
    if (m_bActive != 0) return true;
    if (iequals(hotspot, "b_Rock")) {
        FeatureHost* h = host();
        if (h) h->playRandomSound("sounds\\russell\\strains\\strain", 3);
        FeatureHostExt* x = hostExt(h);
        Self* me = x ? x->player() : nullptr;
        if (me && 90.0 < me->m_dEnergy && (me->m_dwAdrenaline != 0 || me->m_dwEphedrineTimer != 0)) {
            m_bActive = 1;
            doHotspot(h, "b_open");
            return true;
        }
        if (h) h->showMessage(0x151);
    }
    return true;
}

// dll:10016800
void CDedFeature::display(int redraw) {
    imgVis(world().state().lightsOn == 0 ? "lightsoff" : "lightson", 1);
    if (m_bActive == 0) hotspotEnable("b_open", 0);
    if (redraw != 0) redrawPage();
}

// dll:10016860: the rock's state images and its hotspots (name_c / name_o); the lizard rock image on the site 8
void CDedFeature::slot60(const char*, int) {
    FeatureHost* h = host();
    imgVis("rockoff", 0);
    imgVis("lrockoff", 0);
    const std::string c = fmt("%s_c", featureName()), o = fmt("%s_o", featureName());
    if (m_bActive == 0) {
        hotspotEnable(c.c_str(), 1);
        hotspotEnable(o.c_str(), 0);
        return;
    }
    hotspotEnable(c.c_str(), 0);
    hotspotEnable(o.c_str(), 1);
    CSite* site = h ? h->currentSite() : nullptr;      // the original dereferences DAT_100c1f84
    const char* image = (site && site->state() == 8) ? "lrockoff" : "rockoff";
    displayImage(h, image);
    imgVis(image, 1);
}

// =============================================================================================================================
// CMicroscope (vtable 10092b80): pages "F..." (the bench) and "U..." (the view: its own palette, token 0xb)

// dll:1005b4b0
bool CMicroscope::onHotspot(const std::string&) {
    playSound("sounds\\house\\mscope.wav");
    return true;
}

// dll:1005b400
void CMicroscope::onEnterPage(const std::string& page) {
    FeatureHost* h = host();
    FeatureHostExt* x = hostExt(h);
    const char c = page.empty() ? '\0' : page[0];
    if (c == 'F') {
        if (unk_54 != 0) {
            if (x) x->clearWindow();
            unk_54 = 0;
        }
        if (x) x->unlockPalette(0xb);
        if (h) h->resetDiurnal();
        return;
    }
    if (c != 'U') return;
    userInputOff(h);
    if (x) x->lockPalette(0xb);
    if (unk_54 == 0) {
        unk_54 = 1;
        if (x) {
            x->clearWindow();
            x->loadPalette("palettes\\house\\mscope.pal", "lookups\\house\\mscope.xft", 0, 0, 0xb, 0);
        }
    }
}

// dll:1005b4f0
void CMicroscope::onLeavePage() {
    if (unk_54 != 0) {
        if (FeatureHostExt* x = hostExt(host())) x->unlockPalette(0xb);
        if (host()) host()->forcePalette();
    }
}

// dll:1005b4d0: any object used on the bench takes the player to the view
void CMicroscope::onApply(GameObj&, const std::string&) {
    if (FeatureHostExt* x = hostExt(host())) x->jumpToPage("uPh12Mic_7", 0, 0, 1, 0);
}

// =============================================================================================================================
// CThermalCooker (vtable 10093bd0): the food put on the pad is cooked while the bubbling sound plays

// dll:1006f6c0 CCooker::OnButton
bool CThermalCooker::onHotspot(const std::string& hotspot) {
    FeatureHost* h = host();
    Eng* e = engOf(h);
    if (iequals(hotspot, "bPickUp")) {
        if (e && e->DoHotspotAction("dropzone", -1) && m_pObject) {
            playSound("sounds\\house\\thermejc.wav");
            FeatureHostExt* x = hostExt(h);
            if (x && x->player()) m_pObject->describe(*x->player());       // vtable +0x28
            m_pObject = nullptr;
            return true;
        }
    } else if (iequals(hotspot, "bStart")) {
        displayImage(h, "closed", 1);
        if (m_hSound != -1) {
            const i32 playing = m_hSound;
            m_hSound = -1;
            // fix: the handle in m_hSound is the sound to close (the original resets the variable first and closes -1)
            if (e) e->CloseSound(fixes().coffeeMakerCloseSound ? playing : -1);      // ORIGINAL BUG: closes handle -1
        }
        if (h) h->playLoopedSoundInto("sounds\\house\\therbub1.wav", &m_hSound);
        hotspotEnable("bStart", 0);
        hotspotEnable("bPickUp", 0);
    }
    return true;
}

// dll:1006f7d0: the lid is closed while the sound plays
void CThermalCooker::onPageDrawn(const std::string&) {
    int playing = 0;
    Eng* e = engOf(host());
    const bool ok = e && e->IsSoundPlaying(m_hSound, playing);
    displayImage(host(), (!ok || playing == 0) ? "open" : "closed");
}

// dll:1006f840
void CThermalCooker::onPageEntered(const std::string&) {
    if (host() && !host()->currentLocation().empty() && host()->currentLocation()[0] == 'F') host()->showMessage(0xaa);
}

// dll:1006f7c0: vtable +0x38 with the player's page
void CThermalCooker::onClickAway() {
    if (host()) onPageDrawn(host()->currentLocation());
}

// the tail of dll:1006f940 / dll:1006f870: the food is cooked: state 5, the variation "cooked", the consumable code
// (0x105 -> 0x104, anything else -> 1; PORT: only a CFood has these members, the original writes them into any object)
void CThermalCooker::cookObject() {
    ObjFood* f = dynamic_cast<ObjFood*>(m_pObject);
    if (!f || f->state() == 5) return;
    f->setState(5);
    FeatureHostExt* x = hostExt(host());
    if (x && x->player()) f->changeVariation(*x->player(), "cooked", -2);      // FUN_10029440(object, "cooked", -2)
    if (f->consumableCode() != 0x105) f->setConsumableCode(1);
    else f->setConsumableCode(0x104);
}

// dll:1006f940
void CThermalCooker::onLeave() {
    m_hSound = -1;
    cookObject();
}

// dll:1006f870 CCooker::OnSoundEnd (the sound ended: code 4 of the sound handle)
bool CThermalCooker::onMediaClosed(int handle, unsigned mask) {
    if (handle != m_hSound || mask != 4) return false;
    m_hSound = -1;
    displayImage(host(), "open", 1);
    hotspotEnable("bStart", 1);
    hotspotEnable("bPickUp", 1);
    cookObject();
    return true;
}

bool storyFeatureSetObject(Item* feature, GameObj* object) {
    if (auto* c = dynamic_cast<CThermalCooker*>(feature)) { c->setObject(object); return true; }
    if (auto* d = dynamic_cast<CDataTerminal*>(feature)) { d->setObject(object); return true; }
    return false;
}

// =============================================================================================================================
// CMirror (vtable 10093d08)

// dll:10070400: an object used on the mirror breaks it (the held object goes, the page of the broken mirror "<name>_b" shows)
void CMirror::onApply(GameObj&, const std::string&) {
    FeatureHost* h = host();
    m_bActive = 1;
    playSound("sounds\\features\\shatter.wav");
    if ((m_dwFlags & 4) != 0 && h && h->heldObject()) {
        FeatureHostExt* x = hostExt(h);
        h->heldObject()->removeFromScene(x ? x->player() : nullptr);        // vtable +0x94 of DAT_100c1fa0
    }
    if (FeatureHostExt* x = hostExt(h)) x->jumpToPage((featureName() + "_b").c_str(), 0, 0, 1, 0);
}

// dll:10070350: the reflection image "xxx" with G (goggles), M (mask), P (poncho) for what the player wears
void CMirror::display(int redraw) {
    if (m_bActive == 0) return;
    FeatureHostExt* x = hostExt(host());
    Self* me = x ? x->player() : nullptr;
    char img[4] = {'x', 'x', 'x', 0};
    if (me) {
        if (me->m_pGoggles.item != nullptr) img[0] = 'G';     // DAT_100c1fa8 = CSelf+0x270
        if (me->m_pAirMask.item != nullptr) img[1] = 'M';     // DAT_100c1fac = CSelf+0x274
        if (me->m_bWearingPoncho != 0) img[2] = 'P';          // DAT_100c1e6c = CSelf+0x134
    }
    imgVis(m_szImage.c_str(), 0);
    imgVis(img, 1);
    m_szImage = img;
    if (redraw != 0) redrawPage();
}

// dll:100704c0: the images / hotspots of the whole and the broken mirror
void CMirror::slot60(const char*, int) {
    const std::string broken = featureName() + "_b";
    if (m_bActive == 0) {
        imgVis(broken.c_str(), 0);
        hotspotEnable(featureName().c_str(), 1);
        hotspotEnable(broken.c_str(), 0);
        return;
    }
    displayImage(host(), broken);
    imgVis(broken.c_str(), 1);
    hotspotEnable(featureName().c_str(), 0);
    hotspotEnable(broken.c_str(), 1);
}

// =============================================================================================================================
// CBloodFeature (vtable 10093f98): the scanner of the house: ECG movie, blood pressure sound, blood test with a text per page

// dll:10071080
bool CBloodFeature::onHotspot(const std::string& hotspot) {
    FeatureHost* h = host();
    if (iequals(hotspot, "bNext")) {
        if (++unk_5e > 7) unk_5e = 4;
        drawFindings(1);
        return true;
    }
    if (iequals(hotspot, "bEcg")) {
        unk_5c = 1;
        display(1);
        startMode();
        return true;
    }
    if (iequals(hotspot, "bPressure")) {
        unk_5c = 2;
        if (h) h->stopLoopedAvi();
        display(1);
        startMode();
        return true;
    }
    if (iequals(hotspot, "bTest")) {
        unk_5c = 3;
        if (h) h->stopLoopedAvi();
        display(1);
        startMode();
    }
    return true;
}

// dll:10070fd0
void CBloodFeature::onEnterPage(const std::string& page) {
    userInputOff(host());
    if (lastChar(page) == 'O') {
        unk_58 = 0;
        unk_5c = 0;
        unk_5e = 4;
        display(0);
    }
}

// dll:10071020
void CBloodFeature::onPageDrawn(const std::string& page) {
    if (lastChar(page) == 'O') drawTitle(0);
}

// dll:10071ce0
void CBloodFeature::onResume() {
    if (unk_5c == 1) startMode();
}

// dll:10071170
void CBloodFeature::display(int redraw) {
    imgVis("ecg", 0);
    imgVis("pressure", 0);
    imgVis("test", 0);
    imgVis("next", 0);
    const bool power = cworld::hasPower(world(), m_dwPowerMask) != 0;
    hotspotEnable("bEcg", power);
    hotspotEnable("bPressure", power);
    hotspotEnable("bTest", power);
    hotspotEnable("bNext", 0);
    switch (unk_5c) {
    case 1:
        imgVis("ecg", 1);
        hotspotEnable("bEcg", 0);
        break;
    case 2:
        imgVis("pressure", 1);
        hotspotEnable("bPressure", 0);
        break;
    case 3:
        imgVis("test", 1);
        hotspotEnable("bTest", 0);
        imgVis("next", 1);
        hotspotEnable("bNext", 1);
        break;
    default:
        break;
    }
    if (redraw != 0) {
        redrawPage();
        drawTitle(1);
    }
}

// dll:10071050 (the blood pressure / test sound ended: code 4)
bool CBloodFeature::onMediaClosed(int handle, unsigned mask) {
    if (handle == m_hSound && mask == 4) {
        drawFindings(1);
        return true;
    }
    return false;
}

// dll:10071b10: the text of the ECG appears two seconds after the movie started
void CBloodFeature::onIdle() {
    const u32 due = static_cast<u32>(unk_58);
    if (due != 0 && unk_5c == 1 && host() && due < host()->tick()) {
        unk_58 = 0;
        drawFindings(0);
    }
}

// dll:10071310: the movie / sound of the mode; the ECG movie depends on the stimulant (CSelf+0x98) and sedative (+0xa0) levels
void CBloodFeature::startMode() {
    FeatureHost* h = host();
    FeatureHostExt* x = hostExt(h);
    Self* me = x ? x->player() : nullptr;
    m_hSound = -1;
    if (unk_5c == 1) {
        const double stim = me ? me->m_dStimulant : 0.0, sed = me ? me->m_dSedative : 0.0;
        const char* file;
        if (stim <= 50.0 || sed <= 50.0) {
            if (stim <= 50.0) file = sed <= 50.0 ? "movies\\house\\scanner\\healthy.avi" : "movies\\house\\scanner\\slow.avi";
            else file = "movies\\house\\scanner\\fast.avi";
        } else {
            file = "movies\\house\\scanner\\fast.avi";
        }
        if (h) h->playLoopedAvi(file, 0x19a, 200, 0, 0, "nowhere", 0, 0, 0);
        unk_58 = h ? static_cast<i32>(h->tick() + 2000) : 0;
        return;
    }
    if (unk_5c == 2) {
        if (h) h->playLoopedSoundInto("sounds\\house\\sphyg.wav", &m_hSound);
        unk_58 = 0;
        return;
    }
    if (unk_5c == 3) {
        if (h) h->playLoopedSoundInto("sounds\\house\\bloodtst.wav", &m_hSound);
        unk_58 = 0;
    }
}

// dll:10071450: the result text of the mode (the levels of the substances in the player's blood)
void CBloodFeature::drawFindings(int redraw) {
    FeatureHost* h = host();
    if (redraw != 0) display(1);
    if (!h) return;
    FeatureHostExt* x = hostExt(h);
    Self* me = x ? x->player() : nullptr;
    std::unique_ptr<GdiSession> dc = h->openDc(-1);
    if (!dc) return;
    setInfoStyle(*dc);
    static const Self blank;
    const Self& s = me ? *me : blank;       // PORT (host gap): without the player every level is 0
    GdiRect r{0xd2, 0x6e, 0x276, 400};
    if (unk_5c == 1) {
        int id;
        if (s.m_dStimulant <= 50.0 || s.m_dSedative <= 50.0) {
            if (s.m_dStimulant <= 50.0) id = s.m_dSedative <= 50.0 ? 0x89 : 0x87;
            else id = 0x86;
        } else {
            id = 0x88;
        }
        drawWrapped(*dc, str(id), r);
    } else if (unk_5c == 2) {
        drawWrapped(*dc, str((50.0 < s.m_dStimulant || 50.0 < s.m_dSalt) ? 0x8b : 0x8c), r);
    } else if (unk_5c == 3) {
        r = {0xd2, 0x96, 0x276, 400};
        // dll:10071a90: DrawText(string, rect, DT_WORDBREAK), then the rectangle's top moves down by the height of the text
        auto line = [&](int id) { r.top += drawWrapped(*dc, str(id), r); };
        bool any = false;
        switch (unk_5e) {
        case 4:
            dc->textOut(0xd2, 0x6e, str(0x8e));
            if (s.m_dwCoffee != 0 || s.m_dwGuarana != 0) { any = true; line(0x8f); }
            if (s.m_dwCoca != 0) { any = true; line(0x91); }
            if (s.m_dwAdrenaline != 0) { any = true; line(0x92); }
            if (s.m_dwChampagne != 0 || s.m_dwMescal != 0) { any = true; line(0x93); }
            if (!any) line(0xa0);
            if (s.m_dwSleepingPill != 0) line(0x94);
            if (50.0 < s.m_dStimulant) line(0x96);
            if (50.0 < s.m_dSalt) line(0x95);
            break;
        case 5: {
            dc->textOut(0xd2, 0x6e, str(0x98));
            const bool a = s.m_dwPrussic != 0, b = s.m_dwDioscorin != 0, c = s.m_dwSpiderBiteTimer != 0,
                       d = s.m_dwIndigestionTimer != 0, e = s.m_dwWhiteAngelTrip != 0, f = s.m_dwCactusPoisonTimer != 0;
            if (a) line(0x99);
            if (b) line(0x9a);
            if (c) line(0x9c);
            if (d) line(0x9d);
            if (e) line(0x9e);
            if (f) line(0x9f);
            if (!a && !b && !c && !d && !e && !f) line(0xa0);
            break;
        }
        case 6: {
            dc->textOut(0xd2, 0x6e, str(0xa1));
            const bool a = s.m_dwMalaria != 0, b = s.m_dwWorms != 0;
            if (a) line(0xa2);
            if (b) line(0xa3);
            if (!a && !b) line(0xa0);
            break;
        }
        case 7: {
            dc->textOut(0xd2, 0x6e, str(0xa5));
            const bool a = s.m_dProtein < 20.0, b = s.m_dNutrition < 20.0;
            if (a) line(0xa6);
            if (b) line(0xa7);
            if (!a && !b) line(0xa8);
            break;
        }
        default:
            break;
        }
    }
    dc.reset();                                   // Eng_ReleaseWindowDC(-1, dc, 1, 1)
    if (Eng* e = engOf(h)) e->RedrawScreen();     // PORT: openDc releases without the redraw flag
}

// dll:10071b40: the heading of the page and of the mode
void CBloodFeature::drawTitle(int redraw) {
    FeatureHost* h = host();
    if (!h) return;
    std::unique_ptr<GdiSession> dc = h->openDc(-1);
    if (!dc) return;
    setInfoStyle(*dc);
    dc->textOut(0xd2, 0x32, str(0xa9));
    int id = 0;
    if (unk_5c == 1) id = 0x85;
    else if (unk_5c == 2) id = 0x8a;
    else if (unk_5c == 3) id = 0x8d;
    if (id) dc->textOut(0xd2, 0x46, str(id));
    dc.reset();
    if (redraw != 0)
        if (Eng* e = engOf(h)) e->RedrawScreen();     // PORT: the redraw flag of Eng_ReleaseWindowDC(-1, dc, redraw, 1)
}

// =============================================================================================================================
// CBees (vtable 10092ac0): the nest. A click hurts (3 health); honey on it (the object "honey" on the plant) calms it.

// dll:1005b100
bool CBees::onHotspot(const std::string& hotspot) {
    FeatureHost* h = host();
    if (h) {
        h->fadeToColour(0xff, 0, 0, 1, 0x14, 0x36);
        h->playRandomSound("sounds\\russell\\pain\\pain", 5);
    }
    FeatureHostExt* x = hostExt(h);
    if (x && x->player()) x->player()->LoseHealth(3.0, 9, 0);
    if (iequals(hotspot, "bHoney")) {
        m_bActive = 0;
        if (h) h->setFXState(0x3eb, 0);
        hotspotEnable("bHoney", 0);
        doHotspot(h, "dropzoneH");
        return true;
    }
    m_bOn = 1;
    return true;
}

// dll:1005b1e0: the honey is out of the hive (state 2): the bees settle on it
void CBees::onApply(GameObj&, const std::string&) {
    auto* honey = dynamic_cast<ObjFood*>(world().findObject("honey"));
    if (honey && honey->state() == 2) {
        playSound("sounds\\silly\\thud.wav");
        m_bOn = 1;
        m_bActive = 1;
        if (host()) host()->setFXState(0x3eb, 1);
        hotspotEnable("bHoney", 1);
    }
}

// dll:1005b250: the bees sting while the player is at the nest (a chance of half a percent per second since the last update)
void CBees::update(i32 now) {
    FeatureHost* h = host();
    const u32 elapsed = static_cast<u32>(now - m_dwTimeLastUpdate);
    m_dwTimeLastUpdate = now;
    FeatureHostExt* x = hostExt(h);
    Self* me = x ? x->player() : nullptr;
    if (!h || !h->isCurrentFeature(this) || !me) return;
    if (me->m_pPath != nullptr || me->m_bStatusScreenVisible != 0 || m_bOn == 0 || x->silent()) return;
    if (world().state().rng.chance(static_cast<double>(elapsed) * 0.5 * 0.01 * 32767.0)) {      // ChancePercent(elapsed * 0.5)
        h->fadeToColour(0xff, 0, 0, 1, 0x14, 0x36);
        h->playRandomSound("sounds\\russell\\pain\\pain", 5);
        me->LoseHealth(3.0, 9, 1);
    }
}

// dll:1005b1b0
void CBees::display(int) {
    hotspotEnable("bHoney", m_bActive != 0);
}

// =============================================================================================================================
// CCoffeeMachine (vtable 10090290): up to 5 cups, one more every 600 game seconds

// dll:10010d80
bool CCoffeeMachine::onHotspot(const std::string& hotspot) {
    FeatureHost* h = host();
    if (!iequals(hotspot, "yCoffee")) return true;
    FeatureHostExt* x = hostExt(h);
    Self* me = x ? x->player() : nullptr;
    if (unk_5c != 0) {
        if (me && 99.0 < me->m_dHydration) {
            if (h) h->showMessage(0xc1);
            return true;
        }
        unk_5c--;
        if (m_bRunning != 0) unk_58 = cworld::gameTime(world()) + 600;
        if (h) h->playRandomSound("\\sounds\\russell\\drinking\\drink", 4);
        if (me) me->Drink(0xd2);
        display(1);
        return true;
    }
    if (m_bRunning != 0 && h) h->showMessage(0xd8);
    return true;
}

// dll:10010f20: a mug used on the machine is filled with the cups (code 0xd2)
void CCoffeeMachine::onApply(GameObj& object, const std::string&) {
    if (unk_5c == 0) return;
    FeatureHostExt* x = hostExt(host());
    Self* me = x ? x->player() : nullptr;
    if (me) {
        object.fill(*me, 0xd2, unk_5c);      // vtable +0x60
        object.describe(*me);                // vtable +0x28
    }
    unk_5c = 0;
    if (m_bRunning != 0) unk_58 = cworld::gameTime(world()) + 600;
    display(1);
}

// dll:10010ea0
void CCoffeeMachine::update(i32 now) {
    m_dwTimeLastUpdate = now;
    if (m_bRunning != 0 && static_cast<u32>(unk_5c) < 5 && static_cast<u32>(unk_58) < static_cast<u32>(cworld::gameTime(world()))) {
        unk_5c++;
        if (static_cast<u32>(unk_5c) < 5) unk_58 = cworld::gameTime(world()) + 600;
        if (isCurrentFeature()) {
            display(1);
            return;
        }
        if (host() && iequals(host()->currentLocation(), "PH3")) slot60("PH3", 0);
    }
}

// dll:10010cd0
void CCoffeeMachine::display(int redraw) {
    displayImage(host(), "empty");
    for (i32 n = 1; unk_5c != 0 && n <= unk_5c; n++) displayImage(host(), fmt("coffee_%d", n));
    if (redraw != 0) redrawPage();
}

// dll:10010d40
void CCoffeeMachine::slot60(const char*, int) {
    displayImage(host(), unk_5c == 0 ? "empty" : "full");
}

// dll:10010e50
void CCoffeeMachine::startRunning() {
    CFeature::startRunning();
    if (host()) host()->setFXState(m_wId, 1);
    unk_58 = cworld::gameTime(world()) + 600;
}

// dll:10010e80
void CCoffeeMachine::stopRunning() {
    CFeature::stopRunning();
    if (host()) host()->setFXState(m_wId, 0);
}

// =============================================================================================================================
// CTerminal (vtable 10090980): "grub" / "term": termite mounds. Up to unk_c4 insects (animations at the entry's x, y) crawl on
// the page, one hotspot "b<name>_<n>" each; a click kills the insect, which gives a grub object back on its plant.

// dll:10018b90
bool CTerminal::onHotspot(const std::string& hotspot) {
    FeatureHost* h = host();
    Eng* e = engOf(h);
    StoryGlobals& g = G(world());
    const int n = hotspot.empty() ? 0 : std::atoi(&hotspot.back());       // atoi(name + strlen - 1)
    if (n < 0 || n >= static_cast<int>(m_aEntry.size())) return true;      // the original indexes by the digit unchecked
    if (e) e->StopSpriteAnimation(m_aEntry[n].c);
    unk_d8 = static_cast<i32>(static_cast<u32>(unk_d8) & ~(1u << ((n - 1) & 0x1f)));
    if (e) e->EnableHotspot(hotspot, -1, 0, nullptr, 0);
    GameObj* found = nullptr;
    for (int i = 1;; i++) {
        found = world().findObject(fmt("%s_%d", m_szText, i).c_str());
        if (!found) return true;
        auto* f = dynamic_cast<ObjFood*>(found);
        if (f && f->state() == 2) break;                                   // `*(char *)(object + 0x48) == 2`
    }
    playSound("sounds\\objects\\termite.wav");
    doHotspot(h, found->name());
    g.termiteCount--;
    return true;
}

// dll:10018c60: counts the living grubs, then starts the insects of the page
void CTerminal::onEnterPage(const std::string&) {
    FeatureHost* h = host();
    Eng* e = engOf(h);
    StoryGlobals& g = G(world());
    g.termiteCount = 0;
    for (int n = 1;; n++) {
        GameObj* o = world().findObject(fmt("%s_%d", m_szText, n).c_str());
        if (!o) break;
        auto* f = dynamic_cast<ObjFood*>(o);
        if (f && f->state() == 2) g.termiteCount++;
    }
    const i32 now = cworld::gameTime(world());
    auto openAnim = [&](int n, int loop) {       // Eng_OpenSpriteAnimation(anims\insects\<name>_<n>.tan, -1, x, y, 1, delay, cb, loop, 0, 0, 0, 0, 1, &handle, -1, 1)
        int hnd = -1;
        if (e) e->OpenSpriteAnimation(fmt("anims\\insects\\%s_%d.tan", m_szText, n), -1, m_aEntry[n].a, m_aEntry[n].b, 1, unk_d0, true, loop,
                                      0, "", 0, 0, 1, hnd, -1, 1);
        m_aEntry[n].c = hnd;
    };
    const std::string none;
    auto hot = [&](int n, int on) { hotspotEnable(fmt("b%s_%d", m_szText, n).c_str(), on); };
    const u32 count = static_cast<u32>(unk_c4);
    if (count > m_aEntry.size() - 1) return;     // the original has room for 7 insects (the entries are +0x64 .. +0xb3)
    if (unk_cc == 0) {
        for (u32 n = 1; unk_c4 != 0 && n <= count; n++) {
            m_aEntry[n].c = -1;
            openAnim(static_cast<int>(n), 0);
            hot(static_cast<int>(n), 0);
        }
        return;
    }
    const u32 age = static_cast<u32>(now - unk_d4);
    if (3600.0 < static_cast<double>(age) || unk_d4 == 0) {
        unk_d4 = now;
        unk_d8 = 0;
        int spawned = 0;
        for (u32 n = 1; unk_c4 != 0 && n <= count; n++) {
            m_aEntry[n].c = -1;
            hot(static_cast<int>(n), 0);
            if (world().state().rng.chance(60.0 * 0.01 * 32767.0) && spawned < g.termiteCount) {
                openAnim(static_cast<int>(n), 1);
                if (e) e->PlaySpriteAnimation(m_aEntry[n].c, 1, 0);
                hot(static_cast<int>(n), 1);
                spawned++;
                unk_d8 = static_cast<i32>(static_cast<u32>(unk_d8) | (1u << ((n - 1) & 0x1f)));
            }
        }
        return;
    }
    for (u32 n = 1; unk_c4 != 0 && n <= count; n++) {
        m_aEntry[n].c = -1;
        hot(static_cast<int>(n), 0);
        if ((static_cast<u32>(unk_d8) & (1u << ((n - 1) & 0x1f))) != 0) {
            openAnim(static_cast<int>(n), 1);
            if (e) e->PlaySpriteAnimation(m_aEntry[n].c, 1, 0);
            hot(static_cast<int>(n), 1);
        }
    }
}

// dll:10018fd0: the handles are dropped (the page's animations are closed by the engine)
void CTerminal::onLeavePage() {
    for (u32 n = 1; unk_c4 != 0 && n <= static_cast<u32>(unk_c4) && n < m_aEntry.size(); n++) m_aEntry[n].c = -1;
}

// dll:100190e0: an insect's animation finished (sprite code 3, mask 2): its hotspot goes
bool CTerminal::onMediaStopped(int handle, unsigned mask) {
    if (mask != 2) return false;
    u32 n = 1;
    while (n <= static_cast<u32>(unk_c4) && n < m_aEntry.size() && m_aEntry[n].c != handle) n++;
    if (static_cast<u32>(unk_c4) < n || n >= m_aEntry.size()) return false;
    hotspotEnable(fmt("b%s_%d", m_szText, static_cast<int>(n)).c_str(), 0);
    return true;
}

// dll:10019000: now and then an insect crawls (mode 0 only): the more grubs, the more often
void CTerminal::onIdle() {
    StoryGlobals& g = G(world());
    FeatureHost* h = host();
    if (unk_cc != 0 || g.termiteCount <= 0 || !h) return;
    const u32 now = h->tick();
    if (static_cast<u32>(unk_c8) / static_cast<u32>(g.termiteCount) < now - g.terminalLastIdle) {
        if (world().state().rng.chance(30.0 * 0.01 * 32767.0)) {
            const int r = world().state().rng.rand();
            const u32 n = static_cast<u32>(r * unk_c4) / 0x7fff + 1;
            if (n >= m_aEntry.size()) return;                    // the original indexes unchecked
            Eng* e = engOf(h);
            int playing = 0;
            if (e) e->api().IsSpriteAnimationPlaying(m_aEntry[n].c, playing);
            if (playing == 0 && e) e->PlaySpriteAnimation(m_aEntry[n].c, 1, 0);
            g.terminalLastIdle = now;
            hotspotEnable(fmt("b%s_%d", m_szText, static_cast<int>(n)).c_str(), 1);
        }
    }
}

// =============================================================================================================================
// CDataTerminal (vtable 10092cd0): the data disk reader of the house (like the video player: bPlay / bRew / bStop / bEject)

// dll:1005b9e0
bool CDataTerminal::onHotspot(const std::string& hotspot) {
    FeatureHost* h = host();
    playSound("sounds\\features\\click.wav");
    if (iequals(hotspot, "bPlay") && m_bActive == 0) {
        if (m_pObject) {
            activate();                                    // vtable +0x78
            return true;
        }
    } else if (iequals(hotspot, "bRew")) {
        if (!m_pObject || m_bActive == 0) {
            playSound("sounds\\features\\click.wav");
            return true;
        }
        playSound("sounds\\features\\rewind.wav");
        deactivate();                                      // vtable +0x7c
        return true;
    } else if (iequals(hotspot, "bStop")) {
        if (m_pObject && m_bActive != 0) {
            deactivate();
            return true;
        }
    } else if (iequals(hotspot, "bEject") && m_pObject) {
        if (m_bActive != 0) deactivate();
        m_pObject = nullptr;
        m_szName = "none";
        doHotspot(h, "DropSlot");
        display(1);
    }
    return true;
}

// dll:1005bb40: a disk dropped on the slot
void CDataTerminal::onApply(GameObj& object, const std::string&) {
    if (m_pObject) {
        playSound("sounds\\features\\vid_doh.wav");
        return;
    }
    m_pObject = &object;
    m_szName = object.name();
    playSound("sounds\\features\\vid.wav");
    if (host() && host()->eng()) {
        const std::string slot = "DropSlot";
        host()->eng()->api().DropHeldObject(&slot);        // Eng_Api48_DropHeldObject("DropSlot")
    }
    display(1);
}

// dll:1005bc50: leaving the feature stops the disk (not while the player is on the house pages "F...")
void CDataTerminal::onLeave() {
    if (m_bActive == 0 || (host() && !host()->currentLocation().empty() && host()->currentLocation()[0] == 'F')) return;
    deactivate();
}

// dll:1005bc10
void CDataTerminal::display(int redraw) {
    imgVis("InUse", m_pObject != nullptr);
    if (redraw != 0) redrawPage();
}

// dll:1005bbc0: the sound of the disk (the wave named in the disk object) plays
void CDataTerminal::startRunning() {
    CFeature::startRunning();
    auto* disk = dynamic_cast<ObjDataDisk*>(m_pObject);
    if (disk && host()) host()->playLoopedSoundInto(disk->sound().c_str(), &unk_68);
}

// dll:1005bbf0
void CDataTerminal::stopRunning() {
    CFeature::stopRunning();
    if (FeatureHostExt* x = hostExt(host())) x->stopSound(unk_68);
    unk_68 = -1;
}

// =============================================================================================================================
// CVidLink (vtable 10090c48): the videophone of Club Eden "fce9vidpho" and the house message viewer; the messages (CVidMessage)
// become available through the milestones (dll:1001af00)

namespace {
// CVidMessage::CheckMilestone dll:1001af00: DAT_100c272c (the milestones of the story) & the message's mask
void checkMilestone(World& w, CVidMessage& m) {
    const BlobHeader* story = w.state().sim.story;
    const u32 mask = story ? story->milestones : 0;
    if ((mask & m.milestoneMask()) != 0) m.markAvailable();
}
} // namespace

// dll:1001aa20 CVidLink::OnButton
bool CVidLink::onHotspot(const std::string& hotspot) {
    FeatureHost* h = host();
    FeatureHostExt* x = hostExt(h);
    playSound("sounds\\features\\click.wav");
    if (iequals(featureName(), "fce9vidpho") && cworld::hasPower(world(), 0x20000000) == 0) return true;
    const std::vector<Item*>& msgs = world().related(Rel::VidLinkMessage, this);
    const u32 count = static_cast<u32>(msgs.size());
    if (count == 0) return true;                               // the original reads the empty array unchecked
    if (count <= static_cast<u32>(unk_bc)) unk_bc = 0;
    if (iequals(hotspot, "bPlay")) {
        if (h) h->stopLoopedAvi();
        if (x) x->loopedAviClosed();
        if (Eng* e = engOf(h)) {
            if (!e->DisplayImage("blank", -1, nullptr, 1, -1, nullptr, 0)) {
                const int err = e->lastError();
                if (h) h->eng()->WriteLog(fmt("test: %d", err));
            }
        }
        CVidMessage& m = as<CVidMessage>(msgs[static_cast<size_t>(unk_bc)]);
        // FUN_1001ae40(message, unk_d4, unk_d8, 0)
        m.setPlayed(true);
        if (x) x->playVideoMessage(m.movie().c_str(), unk_d4, unk_d8, 0);
        return true;
    }
    if (iequals(hotspot, "bNext")) {
        do {
            if (static_cast<u32>(++unk_bc) >= count) unk_bc = 0;
        } while (as<CVidMessage>(msgs[static_cast<size_t>(unk_bc)]).flagA() == 0);
        if (h) h->stopLoopedAvi();
        display(1);
    } else if (iequals(hotspot, "bPrevious")) {
        do {
            i32 i = unk_bc;
            if (i == 0) i = static_cast<i32>(count);
            unk_bc = i - 1;
        } while (as<CVidMessage>(msgs[static_cast<size_t>(unk_bc)]).flagA() == 0);
        if (h) h->stopLoopedAvi();
        display(1);
        return true;
    }
    return true;
}

// dll:1001acc0
void CVidLink::onResume() { display(1); }

// dll:1001abf0: the messages the milestones made available; the feature's FX (the blinking light) runs while one is unplayed
void CVidLink::update(i32) {
    const std::vector<Item*>& msgs = world().related(Rel::VidLinkMessage, this);
    bool pending = false;
    for (i32 i = static_cast<i32>(msgs.size()) - 1; i >= 0; i--) {
        CVidMessage& m = as<CVidMessage>(msgs[static_cast<size_t>(i)]);
        checkMilestone(world(), m);
        if (m.flagA() != 0 && m.flagB() == 0) {
            pending = true;
            unk_bc = i;
        }
    }
    if (pending) {
        if (unk_dc == 0) {
            if (host()) host()->setFXState(static_cast<i16>(m_wId), 1);
            unk_dc = 1;
        }
    } else if (unk_dc != 0) {
        if (host()) host()->setFXState(static_cast<i16>(m_wId), 0);
        unk_dc = 0;
    }
}

// dll:1001a920
void CVidLink::display(int redraw) {
    FeatureHost* h = host();
    m_bRunning = 1;                                            // +0x38
    if (iequals(featureName(), "fce9vidpho") && cworld::hasPower(world(), 0x20000000) == 0) {
        imgVis("poweroff", 1);
        return;
    }
    const std::vector<Item*>& msgs = world().related(Rel::VidLinkMessage, this);
    if (static_cast<size_t>(unk_bc) >= msgs.size()) return;    // the original reads the array unchecked
    const CVidMessage& m = as<CVidMessage>(msgs[static_cast<size_t>(unk_bc)]);
    const bool isNew = m.flagA() != 0 && m.flagB() == 0;
    if (redraw != 0) {
        displayImage(h, m.imageName(), 1);
        if (!isNew) {
            imgVis("new", 0);
            return;
        }
        displayImage(h, "new", 1);
        return;
    }
    imgVis(m.imageName().c_str(), 1);
    if (isNew) imgVis("new", 1);
}

// dll:1001aca0: the message movie ended (mask 1)
bool CVidLink::onMediaClosed(int, unsigned mask) {
    if (mask == 1) display(1);
    return false;
}

// =============================================================================================================================
// CCabinet (vtable 10092c28): a locked cabinet (unk_64 = 1) that a key opens

// dll:1005b710
bool CCabinet::onHotspot(const std::string& hotspot) {
    if (iequals(hotspot, "b_Locked") || (!hotspot.empty() && hotspot[0] == 'Y')) {
        if (unk_64 != 0) {
            playSound("sounds\\features\\cablockd.wav");
            if (host()) host()->showMessage(0xd9);
        }
        return true;
    }
    if (iequals(hotspot, "b_Open")) {
        playSound("sounds\\features\\cabopen.wav");
        return true;
    }
    if (iequals(hotspot, "b_Close")) {
        playSound("sounds\\features\\cabclose.wav");
        return true;
    }
    return true;
}

// dll:1005b7d0: the key opens it and is used up (the held object leaves the scene)
void CCabinet::onApply(GameObj&, const std::string&) {
    FeatureHost* h = host();
    unk_64 = 0;
    if (h && h->heldObject()) {
        FeatureHostExt* x = hostExt(h);
        h->heldObject()->removeFromScene(x ? x->player() : nullptr);         // vtable +0x94 of DAT_100c1fa0 (unchecked in the original)
    }
    playSound("sounds\\features\\cabopen.wav");
    doHotspot(h, "b_open");
}

// dll:1005b6b0
void CCabinet::display(int) {
    if (unk_64 == 0) {
        hotspotEnable("b_Locked", 0);
        hotspotEnable("b_Open", 1);
        return;
    }
    hotspotEnable("b_Locked", 1);
    hotspotEnable("b_Open", 0);
}

// =============================================================================================================================
// CMedBox (vtable 10092e30), the drug dispenser: five drugs (adrenaline, chloroquine, inhaler, sleeping pills, vitamins) in
// unk_[0..4] units dispensed so far; unk_[5..9] = the button of the drug was pressed (the units on display shrink by one)

// dll:1005c920
bool CMedBox::onHotspot(const std::string& hotspot) {
    FeatureHost* h = host();
    World& w = world();
    auto dispense = [&](const char* dis, const char* btn, const char* prefix, int idx, u32 limit, int flag) {
        if (iequals(hotspot, dis)) {
            if (static_cast<u32>(unk_[idx]) < limit) {
                unk_[flag] = 1;
                playSound("sounds\\features\\drugdrop.wav");
            }
            return true;
        }
        if (iequals(hotspot, btn)) {
            const int n = unk_[idx]++;
            w.state().message = fmt(prefix, n);          // sprintf(DAT_100c2650, "dAdren%d", n)
            doHotspot(h, w.state().message);
            unk_[flag] = 0;
            return true;
        }
        return false;
    };
    if (iequals(hotspot, "bLocked")) {
        playSound("sounds\\features\\cablockd.wav");
        if (h) h->showMessage(0xd9);
    } else if (dispense("bDisAdren", "bAdren", "dAdren%d", 0, 6, 5) || dispense("bDisChlor", "bChlor", "dChlor%d", 1, 6, 6) ||
               dispense("bDisInhal", "bInhal", "dInhal%d", 2, 5, 7) || dispense("bDisSleep", "bSleep", "dSleep%d", 3, 5, 8) ||
               dispense("bDisVit", "bVit", "dVit%d", 4, 6, 9)) {
    }
    display(1);       // (**(vtable + 0x5c))(): the redraw argument is not set by the caller; the display ends `if (redraw)`
    return true;
}

// dll:1005c110
void CMedBox::onEnterPage(const std::string&) { display(0); }

// dll:1005c120: the key (any applied object) unlocks the box and is used up
void CMedBox::onApply(GameObj& object, const std::string&) {
    FeatureHostExt* x = hostExt(host());
    m_bOn = 1;
    if (host()) host()->flagHotspotHandled();      // DAT_1009f838 = 1
    display(1);
    object.removeFromScene(x ? x->player() : nullptr);    // vtable +0x94
}

// dll:1005c150 CDrugDispenser::Display
void CMedBox::display(int redraw) {
    FeatureHost* h = host();
    if (m_bOn == 0) {
        imgVis("locked", 1);
        hotspotEnable("bLocked", 1);
        hotspotEnable("yMediPass", 1);
        for (const char* i : {"adren", "chlor", "inhal", "sleep", "vit"}) imgVis(i, 0);
        for (const char* b : {"bDisAdren", "bAdren", "bDisChlor", "bChlor", "bDisInhal", "bInhal", "bDisSleep", "bSleep", "bDisVit", "bVit"})
            hotspotEnable(b, 0);
    } else {
        imgVis("locked", 0);
        hotspotEnable("bLocked", 0);
        hotspotEnable("yMediPass", 0);
        struct Drug { const char* image; const char* btn; const char* dis; int idx; };
        static const Drug drugs[5] = {{"adren", "bAdren", "bDisAdren", 5}, {"Chlor", "bChlor", "bDisChlor", 6},
                                      {"Inhal", "bInhal", "bDisInhal", 7}, {"Sleep", "bSleep", "bDisSleep", 8},
                                      {"Vit", "bVit", "bDisVit", 9}};
        for (const Drug& d : drugs) {
            const bool on = unk_[d.idx] != 0;
            imgVis(d.image, on);
            hotspotEnable(d.btn, on);
            hotspotEnable(d.dis, !on);
        }
    }
    slot60(h ? h->currentLocation().c_str() : "", 0);
    const i32 dg = world().state().sim.diurnal;           // DAT_100c1c44
    imgVis("night", dg == 0x28 || dg == 5 || dg == 0x23);
    if (redraw != 0) redrawPage();
}

// dll:1005c560 (CDrugDispenser::DisplayInDist): the box image and the units still in the box: 6 - (button pressed) - dispensed
void CMedBox::slot60(const char*, int arg) {
    FeatureHost* h = host();
    if (arg != 0) {
        displayImage(h, "medbox");
        imgVis("medbox", 1);
    }
    int locked = 0;
    if (m_bOn == 0) {
        if (Eng* e = engOf(h)) {
            if (!e->DisplayImage("locked", -1, nullptr, 0, -1, nullptr, 0)) {
                e->WriteLog(fmt("CDrugDispenser::DisplayInDist DisplayImage('locked'... error %d", e->lastError()));
            }
        }
        locked = 1;
    }
    imgVis("locked", locked);
    struct Row { const char* prefix; int total; int dispensed; int pressed; };
    const Row rows[5] = {{"adren_%d", 6, 0, 5}, {"chlor_%d", 6, 1, 6}, {"inhal_%d", 5, 2, 7}, {"sleep_%d", 5, 3, 8}, {"vit_%d", 6, 4, 9}};
    for (const Row& r : rows) {
        const int shown = r.total - (unk_[r.pressed] != 0 ? 1 : 0) - unk_[r.dispensed];
        int n = 1;
        for (; n <= shown; n++) {
            const std::string img = fmt(r.prefix, n);
            displayImage(h, img);
            imgVis(img.c_str(), 1);
        }
        for (; n < r.total; n++) imgVis(fmt(r.prefix, n).c_str(), 0);
    }
}

// =============================================================================================================================
// CForestFeature (vtable 1008fb78): the Club Eden "forest" terminal; unk_58 = a disk is in (the pages "U..." have their own palette)

// dll:10007e30
void CForestFeature::onEnterPage(const std::string& page) {
    FeatureHost* h = host();
    FeatureHostExt* x = hostExt(h);
    if (!page.empty() && page[0] == 'U') {
        userInputOff(h);
        if (unk_54 == 0) {
            unk_54 = 1;
            if (x) {
                x->clearWindow();
                x->loadPalette("palettes\\clubeden\\ce9for.pal", "lookups\\clubeden\\ce9for.xft", 0, 0, 0, 0);
            }
        }
        if (x) x->lockPalette(0xf);
        return;
    }
    if (unk_54 != 0) {
        unk_54 = 0;
        if (x) x->unlockPalette(0xf);
        if (h) h->resetDiurnal();
        if (x) x->clearWindow();      // PORT: resetDiurnal also runs UpdateDiurnal; the original clears the window in between
    }
    if (cworld::hasPower(world(), 0x20000000) == 0) {
        imgVis("insert", 0);
        imgVis("text", 0);
        hotspotEnable("bInfo", 0);
        hotspotEnable("yflop1", 0);
        return;
    }
    if (unk_58 == 0) {
        imgVis("insert", 1);
        imgVis("text", 0);
        hotspotEnable("bInfo", 0);
        return;
    }
    imgVis("insert", 0);
    imgVis("text", 1);
    hotspotEnable("bInfo", 1);
}

// dll:10007ff0
void CForestFeature::onLeavePage() {
    if (unk_54 != 0) {
        if (FeatureHostExt* x = hostExt(host())) x->unlockPalette(0xf);
        if (host()) host()->forcePalette();
    }
}

// dll:10007fb0: the disk is used up and the terminal reads it
void CForestFeature::onApply(GameObj& object, const std::string&) {
    FeatureHostExt* x = hostExt(host());
    object.removeFromScene(x ? x->player() : nullptr);     // vtable +0x94
    unk_58 = 1;
    if (cworld::hasPower(world(), 0x20000000) != 0) doHotspot(host(), "bInfo");
}

} // namespace biosys::game
