// BioSys 4 terminal, packet P1b: the page hooks, the password entry and the hotspot -> spec panel mapping (spec analysis/notes/bs4.md 1.12, 1.13).
// Everything here plugs into Bs4 through Bs4Registry::hooks() (bs4page.h); the sounds (exe:0040c1e0 PreloadClicks, exe:0040c310 PlayClick,
// exe:0040c4b0 StartLoop, exe:0040c660 StopLoop, exe:0040c6a0 WelcomeCallback) are behind Bs4Host::playClick / startLoop / stopLoop /
// playWelcome and live in the engine glue (engine_bs4.cpp).
//
// Not ported on purpose (spec section 6, dead code of this BIOSYS4.DAT): the zoom pages and their embedded-page branch of exe:004090a0 (it
// throws if a page of that name is ever loaded), the marker half of exe:0040acf0, the variant lists of the dead panels panelsav / panelrf.
#include "engine/bs4.h"

namespace biosys {
namespace {

constexpr int kSpecX = 0x1ba, kSpecY = 0x48;       // where a spec sheet opens: (442, 72)

// exe:0040c0f0 DrawPasswordText: the field is erased with its background colour, the typed text is drawn left aligned, vertically
// centred in black; then the terminal is recomposed and flipped.
void drawPasswordText(Bs4& b) {
    if (!b.textEntry) return;
    Bs4Page& m = b.mainPage();
    m.fill(b.textRect, b.textBackground);
    m.drawText(b.textRect, b.textBuffer, 0, 0, 0, 0x24);
    b.compose();
    b.flip();
}

// exe:004090a0 OnMainPageLoaded. The text field of a password page: rect, background palette index.
void onMainPageLoaded(Bs4& b, const std::string& name) {
    static const char* const zoomPages[] = {"techZ", "powZ", "atmZ", "hydZ", "terrZ", "baseZ", "monZ", "mterrZ", "muwZ", "mwethZ", "inZ", "outZ"};
    for (const char* z : zoomPages)
        if (iequals(name, z))
            throw Error("BS4: the page '" + name + "' would be embedded as a zoom page (exe:004090a0), which is dead code in this BIOSYS4.DAT (spec section 6)");
    const bool initial = iequals(name, "initial"), emailpw = iequals(name, "emailpw"), synthpw = iequals(name, "synthpw");
    if (!initial && !emailpw && !synthpw) return;
    b.textBuffer.clear();                                           // +0x1bb4
    b.textRect = initial ? Bs4Rect{0xaf, 0x125, 0x11b, 0x132} : Bs4Rect{0x14a, 0xac, 0x1b7, 0xba};
    b.textEntry = true;                                             // +0x1bb0
    b.textBackground = initial ? b.colours().white : b.colours().offWhite;
    drawPasswordText(b);
    // (+0x18dc, the page name, is kept by Bs4::loadMainPage)
}

// exe:0040bb30 SubmitPassword. The typed text is transformed (+1 at even, -1 at odd positions) and compared with the constants
// STTRFKM / DXCDSRQGFQFR / HQFDOJJMH at exe:004825c0 / 004825b0 / 004825a4: RUSSELL, CYBERSPHERES, GREENKING.
std::string encode(const std::string& s) {
    std::string t = s;
    for (size_t i = 0; i < t.size(); i++) t[i] = static_cast<char>(static_cast<unsigned char>(t[i]) + ((i & 1) == 0 ? 1 : -1));
    return t;
}

void submitPassword(Bs4& b) {
    if (!b.textEntry) return;
    b.textEntry = false;
    const std::string typed = encode(b.textBuffer);
    const std::string& page = b.currentMainName();
    auto open = [&](int pageNo) {
        bool ok = false;
        try {
            ok = b.loadMainPage(pageNo);
        } catch (const Error& e) {
            b.host().log(e.what());
        }
        if (!ok) b.host().requestClose();                           // PostCloseToMainWnd
    };
    if (iequals(page, "initial") && typed == "STTRFKM") {           // RUSSELL
        b.session().initialPassword = true;                         // DAT_00499080
        b.host().playWelcome(nullptr);                              // bs4sound\welcome.wav once (the host keeps the handle: exe:0040c6a0)
        open(b.pageNumber("menuscreen"));
        return;
    }
    if (iequals(page, "emailpw") && typed == "DXCDSRQGFQFR") {      // CYBERSPHERES
        b.session().emailPassword = true;                           // DAT_00497a20
        b.host().playClick(5);                                      // Blip9
        open(b.pageEmail1());
        return;
    }
    if (iequals(page, "synthpw") && typed == "HQFDOJJMH") {         // GREENKING
        b.session().synthPassword = true;                           // DAT_004990e8
        b.host().playClick(5);                                      // PreloadClicks(4), then Blip9
        open(b.pageSynth1());
        return;
    }
    b.host().playClick(4);                                          // a wrong password: Synth10
    b.textEntry = true;
    b.textBuffer.clear();
    drawPasswordText(b);
}

// exe:0040a760: hotspot "tech<x>" of the technical overview opens the spec sheet "spec<x>"
const char* specPageOf(const std::string& hotspot) {
    static const struct { const char* tech; const char* spec; } map[] = {
        {"techah", "specah"},     {"techlock", "speclock"}, {"techair", "specair"},   {"techvent", "specvent"}, {"techalg", "specalg"},
        {"techbub", "specbub"},   {"techcfan", "speccfan"}, {"techcond", "speccond"}, {"techsump", "specsump"}, {"techfilt", "specfilt"},
        {"techheat", "specheat"}, {"techmist", "specmist"}, {"techoxy", "specoxy"},   {"techpod", "specpod"},   {"techres", "specres"},
        {"techrain", "specrain"}, {"techosm", "specosm"},   {"techsfan", "specsfan"}, {"techsol", "specsol"},   {"techsprk", "specsprk"},
        {"techsub", "specsub"},   {"techterm", "specterm"}, {"techtide", "spectide"}, {"techutil", "specutil"}, {"techuv", "specuv"},
        {"techwave", "specwave"}, {"techwind", "specwind"}};
    for (const auto& m : map)
        if (iequals(hotspot, m.tech)) return m.spec;
    return nullptr;
}

bool openSpec(Bs4& b, const char* specName) {
    const int n = b.pageNumber(specName);
    if (n == 0) return false;
    Bs4Action a;
    a.rect = Bs4Rect{0, 0, 10, 10};
    a.verb = 0x1a;
    a.arg[0] = n;
    a.arg[1] = kSpecX;
    a.arg[2] = kSpecY;
    b.dispatchAction(a);
    return true;
}

// exe:0040a760 AfterAction (after every successful verb)
void afterAction(Bs4& b, const Bs4Action& act) {
    const std::string hot = b.lastHotspot();                         // +0x18e7
    if (bs4IstartsWith(hot, "tech")) {
        const char* spec = specPageOf(hot);
        if (spec) openSpec(b, spec);
        return;
    }
    Bs4Page& m = b.mainPage();
    if (Bs4Registry::pageSpecific())                                 // exe:0041deb0 (main-page dynamics, P3): emailin / probe sensors
        for (const Bs4MainExt& e : Bs4Registry::mainExts())
            if (e.hotspotAction && (e.page.empty() || iequals(e.page, m.name()))) e.hotspotAction(m, act);
    if (act.verb == 0x12 && iequals(b.currentMainName(), "pod") && openSpec(b, "specpod")) return;
    if (!iequals(b.currentMainName(), "species")) return;
    bool almond, hog;
    if (iequals(hot, "hotalmond")) { almond = true; hog = false; }
    else if (iequals(hot, "hothog")) { almond = false; hog = true; }
    else if (iequals(hot, "hotspot")) { almond = false; hog = false; }
    else return;
    m.setOrnamentVisible("almhilite", almond);
    m.setOrnamentVisible("almtext", almond);
    m.setOrnamentVisible("hoghilite", hog);
    m.setOrnamentVisible("hogtext", hog);
    m.paint();                                                       // exe:0041baf0
    b.compose();
    b.flip();
}

// exe:0040acf0 UpdateHover (after every mouse move). In this edition it only keeps the hover cache +0x1b78: the hover frame +0x1b88 and the
// location markers are set only inside the branches of the zoom / overview pages atmZ hydZ InZ OutZ powZ, which are not in BIOSYS4.DAT
// (spec 1.13 says otherwise: every writer of +0x1b88 is inside those branches), so there is no hover frame.
void updateHover(Bs4& b) {
    Bs4Page& m = b.mainPage();
    if (!m.isActive()) return;
    std::string now;
    m.hoverName(now);
    if (now == b.hoverName()) return;
    b.hoverName() = now;
    static const char* const markerPages[] = {"atmZ", "hydZ", "InZ", "OutZ", "powZ"};
    for (const char* z : markerPages)
        if (iequals(b.currentMainName(), z))
            throw Error(std::string("BS4: the location markers of the page ") + z + " (exe:0040acf0) are dead code in this BIOSYS4.DAT (spec section 6)");
}

// exe:00409720 kind 3: the panel's variant is the n-th device button of its page (item `shift` of the panel)
struct Variant { const char* opener; int shift; };
struct PanelVariants { const char* panel; std::vector<Variant> v; };
const std::vector<PanelVariants>& variants() {
    static const std::vector<PanelVariants> t = {
        {"panelcfan", {{"14_cfans1", 3}, {"14_cfans2", 4}, {"14_cfans3", 5}, {"14_cfano1", 6}, {"14_cfano2", 7}, {"14_cfano3", 8},
                       {"14_cfane1", 9}, {"14_cfane2", 10}, {"14_cfane3", 11}, {"14_cfanr1", 12}, {"14_cfanr2", 13}, {"14_cfanr3", 14}}},
        {"panelsfan", {{"2_soils1", 3}, {"2_soils2", 4}, {"2_soils3", 5}, {"2_soile1", 9}, {"2_soile2", 10}, {"2_soile3", 11},
                       {"2_soilr1", 12}, {"2_soilr2", 13}, {"2_soilr3", 14}}},
        {"panelah", {{"5_ahs2", 3}, {"5_aho1", 6}, {"5_ahe2", 9}, {"5_ahr2", 12}}},
        {"panelvent", {{"1_vents1", 3}, {"1_vents2", 4}, {"1_vento1", 6}, {"1_vente1", 9}, {"1_vente2", 10}, {"1_ventr1", 12}, {"1_ventr2", 13}}},
        {"panelheat", {{"4_heats1", 3}, {"4_heato1", 6}, {"4_heate1", 9}, {"4_heatr1", 12}}},
        {"panelsub", {{"4_subs1", 3}, {"4_subs2", 4}, {"4_subs3", 5}, {"4_sube1", 9}, {"4_sube2", 10}, {"4_sube3", 11},
                      {"4_subr1", 12}, {"4_subr2", 13}, {"4_subr3", 14}}},
        {"panelcond", {{"13_conds1", 3}, {"13_condo1", 6}, {"13_conde1", 9}, {"13_condr1", 12}}},
        {"panelfilt", {{"9_fils1", 3}, {"9_fils2", 4}, {"9_fils3", 5}, {"9_file1", 9}, {"9_file2", 10}, {"9_file3", 11},
                       {"9_filr1", 12}, {"9_filr2", 13}, {"9_filr3", 14}}},
        {"panelsump", {{"0_sumps1", 3}, {"0_sumps2", 4}, {"0_sumps3", 5}, {"0_sumpe1", 9}, {"0_sumpe2", 10}, {"0_sumpe3", 11},
                       {"0_sumpr1", 12}, {"0_sumpr2", 13}, {"0_sumpr3", 14}}},
        {"paneluv", {{"3_uvs1", 3}, {"3_uvo1", 6}, {"3_uve1", 9}, {"3_uvr1", 12}, {"3_uvho1", 24}}},
        {"panelosm", {{"2_oss1", 3}, {"2_oso1", 6}, {"2_ose1", 9}, {"2_osr1", 12}}},
        {"panelrain", {{"6_rains1", 3}, {"6_rains2", 4}, {"6_rains3", 5}, {"6_raino1", 6}, {"6_raine1", 9}, {"6_raine2", 10},
                       {"6_raine3", 11}, {"6_rainr1", 12}, {"6_rainr2", 13}, {"6_rainr3", 14}}},
        {"panelsprk", {{"8_sprs1", 3}, {"8_spro1", 6}, {"8_spre1", 9}, {"8_sprr1", 12}, {"8_sprho1", 24}}},
        {"panelmist", {{"1_mistr1", 12}, {"1_mistr2", 13}, {"1_mistr3", 14}}},
        {"panellock", {{"18_locks1", 3}, {"18_locks2", 4}, {"18_locko1", 6}, {"18_locke1", 9}, {"18_locke2", 10}, {"18_lockr1", 12}}},
    };
    return t;
}

// exe:00409720. Kind 1 (the saved item mask of a loaded game) is done by Bs4Page::load itself.
void onPageLoaded(Bs4& b, const std::string& name, int kind) {
    if (kind != 3) return;
    for (const PanelVariants& p : variants()) {
        if (!iequals(name, p.panel)) continue;
        for (const Variant& v : p.v)
            if (iequals(b.panelOpener(), v.opener)) {               // the hotspot that opened the panel (+0x1b9c)
                b.panelPage().applyMask(1u << v.shift);            // exe:0041cf40: only that item stays shown
                return;
            }
        return;
    }
}

struct Registrar {
    Registrar() {
        Bs4Hooks& h = Bs4Registry::hooks();
        h.onMainPageLoaded = onMainPageLoaded;
        h.onPageLoaded = onPageLoaded;
        h.afterAction = afterAction;
        h.updateHover = updateHover;
        h.submitPassword = submitPassword;
        h.drawPasswordText = drawPasswordText;
    }
} registrar;

} // namespace
} // namespace biosys
