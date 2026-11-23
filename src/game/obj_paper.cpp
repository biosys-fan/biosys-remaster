#include "game/obj_paper.h"
#include "game/obj_common.h"
#include "game/featenv.h"
#include "game/site.h"
#include "game/world_ctors.gen.h"
#include <cctype>
#include <cstdio>
#include <strings.h>

namespace biosys::game {

// dll:1001c470
ObjPaperItem::ObjPaperItem(const char* name, i32 nameStringId, u32 flags, const char* text, double weight)
    : GameObj(0x1001c470, "CPaperItem", name, nameStringId) {
    m_dwFlags = flags;       // +0x14
    m_szText = text;         // +0x40
    m_dWeight = weight;      // +0x20
}

// dll:1001c390 (then FUN_10028e90 as in every constructor)
ObjPaperItem::ObjPaperItem(u32 ctorAddr, const char* className, const char* name, i32 nameStringId)
    : GameObj(ctorAddr, className, name, nameStringId) {}

// dll:10015f20
ObjAlarmClock::ObjAlarmClock(const char* name, i32 nameStringId, const char* text, Item* soundFx)
    : ObjPaperItem(0x10015f20, "CAlarmClock", name, nameStringId) {
    m_szText = text;                                // +0x40
    m_bBeingCarried = 0;                            // +0x54
    m_dWeight = dwordsToDouble(0x40000000);         // +0x20 2.0
    m_pSoundFx = soundFx;                           // +0x50
    m_dwAlarmTime = 0x5b68;                         // +0x58
    m_bAlarmOn = 0;                                 // +0x64
    m_bRinging = 0;                                 // +0x68
    // CEngineFX__CreateFontResource(&DAT_100c12f8, "highgfb.ttf") (dll:10015f20) and the matching remove in the destructor dll:10016000:
    // the constructors run without a host here, so the font file is registered while the clock face is open (onOpened / onClosed)
}

// dll:10016070
void ObjAlarmClock::saveSlot(SlotWriter& w) const {
    saveCommon(w);
    w.writeI32(m_bAlarmOn);
    w.writeI32(m_bBeingCarried);
    w.writeI32(m_dwAlarmTime);
}

// dll:100160b0
void ObjAlarmClock::loadSlot(SlotReader& r) {
    loadCommon(r);
    m_bAlarmOn = r.readI32("unk_64");
    m_bBeingCarried = r.readI32("unk_54");
    m_dwAlarmTime = r.readI32("m_dwAlarmTime");
    r.finishPad();
}

// dll:10015af0
ObjDataDisk::ObjDataDisk(const char* name, i32 nameStringId, const char* sound)
    : GameObj(0x10015af0, "CDataDisk", name, nameStringId) {
    m_szSound = sound;       // +0x40
    m_dWeight = 0;           // +0x20
}

// dll:10063360
ObjVideoTape::ObjVideoTape(const char* name, i32 nameStringId, const char* movie)
    : GameObj(0x10063360, "CVideoTape", name, nameStringId) {
    m_dwFlags |= 0x200;                       // +0x14
    m_szMovie = movie;                        // +0x40
    m_dWeight = dwordsToDouble(0x3ff00000);   // +0x20 1.0
    m_dwPlayEvent = 0;                        // +0xa4
}

void ObjVideoTape::poke(u32 offset, unsigned bytes, u32 value, PokeOp op) {
    if (bytes == 4 && offset == 0xa4 && op == PokeOp::Set) { m_dwPlayEvent = value; return; }
    GameObj::poke(offset, bytes, value, op);
}

u32 ObjVideoTape::peek(u32 offset, unsigned bytes) const {
    if (bytes == 4 && offset == 0xa4) return m_dwPlayEvent;
    return GameObj::peek(offset, bytes);
}

// ---------------------------------------------------------------------------------------------------------------------
// behaviour

namespace {
// FUN_1006efa0 dll:1006efa0 (CSelf member): the panorama position the page jump after the paper returns to
void savePanoramaPosition(Self& s) {
    int w = 0, startX = 0, centreX = s.g.panoramaX;
    s.host().Eng_GetPanoramaInfo(w, startX, centreX);
    s.g.panoramaX = centreX - 0x140;
}

// The jump back from the paper page (shared by the key filter and the right click): the return page of the paper, with the
// panorama position unless the player is on the sites A, F or N
bool returnFromPaper(Self& s, ObjPaperItem& paper) {
    char c = s.g.paperReturnPage.empty() ? '\0' : s.g.paperReturnPage[0];
    int x, y;
    if (c == 'A' || c == 'F' || c == 'N') {
        x = 0;
        y = 0;
    } else {
        y = 0x148;
        x = s.g.panoramaX;
    }
    if (strcasecmp(reinterpret_cast<const char*>(s.m_szLocation.data()), paper.text().c_str()) != 0) return false;
    if (s.host().jumpToPage(s.g.paperReturnPage.c_str(), x, y, 1, 0) == 0) return false;
    return true;
}
}  // namespace

void ObjPaperItem::dumpMembers(ObjMembers& out) const {
    commonMembers(out);
    putS(out, "m_szText", m_szText);
}

// dll:1001c560
int ObjPaperItem::use(Self& s) {
    if (m_dwNameStringId != 0x112) s.host().playSound("sounds\\objects\\paprfld4.wav");
    std::string here(reinterpret_cast<const char*>(s.m_szLocation.data()));
    savePanoramaPosition(s);
    if (name().find("poem") != std::string::npos) {
        bool all = true;
        for (int i = 1; i < 3; i++) {
            std::array<int, 12> info{};
            s.host().Eng_GetGameObjectInfo(("poem" + std::to_string(i)).c_str(), info);
            if (info[2] != 0 || (info[5] == 0 && info[4] == 0)) {
                all = false;
                break;
            }
        }
        if (all) m_szText = "xpoem3";
        else m_szText = "x" + name();
    }
    if (s.host().jumpToPage(m_szText.c_str(), 0, 0, 1, 0) == 0) {
        std::string text = name() + " Use - GotoPage(" + m_szText + ") failed";
        s.world().state().message = text;
        s.world().log(text);
        return 0;
    }
    s.g.paperReturnPage = here;
    return 1;
}

// dll:1001c790
int ObjPaperItem::onKey(Self& s, int key, u32* allow) {
    if (s.m_bAwake == 0) *allow = 0;
    if (key != 0x10 && returnFromPaper(s, *this)) {
        onClosed(s);                                                     // vtable +0x9c
        if (key != 0x44 && key != 0x53) *allow = 0;
    }
    return 1;
}

// dll:1001c840
int ObjPaperItem::onRightClick(Self& s) {
    if (returnFromPaper(s, *this)) {
        onClosed(s);
        return 1;
    }
    return 0;
}

// dll:1001c8c0: on the paper's own page ("x...") the routes the player knows are drawn on the maps
void ObjPaperItem::onPageEnter(Self& s, const std::string& page) {
    static const char* const kMapA[15] = {"R1R2", "R1R3", "R1R4", "R1R7", "R2R4", "R3R5", "R3R6", "R3R8",
                                          "R4R5", "R4R10", "R5R6", "R6R8", "R7R8", "R7R9", "R8R9"};
    static const char* const kMapB[12] = {"S1S2", "S1S5", "S1S6", "S2S3", "S2S9", "S3S4", "S3S5", "S4S5", "S5S6", "S6S7", "S7S8", "S8S9"};
    if (page.empty() || std::toupper(static_cast<unsigned char>(page[0])) != 'X') return;
    s.host().enableUserInput(0, 0);
    onOpened(s);
    if ((m_dwFlags & 0x8000000) == 0) {
        if ((m_dwFlags & 0x10000000) != 0) {
            u32 known = s.host().mapRoutes(1);
            for (int i = 0; i < 12; i++)
                if ((known & (1u << i)) != 0) s.host().CEngineFX_SetImageVisibility(kMapB[i], 1, -1);
        }
    } else {
        u32 known = s.host().mapRoutes(0);
        for (int i = 0; i < 15; i++)
            if ((known & (1u << i)) != 0) s.host().CEngineFX_SetImageVisibility(kMapA[i], 1, -1);
    }
}

// ---- CAlarmClock (vtable 10090590)

// dll:100160f0
void ObjAlarmClock::afterLoad(Self& s) {
    if (m_bBeingCarried != 0) objFxFollow(m_pSoundFx, s);
}

// dll:10016190
void ObjAlarmClock::dump(std::vector<std::string>& lines) const {
    lines.push_back(m_bAlarmOn == 0 ? "m_bAlarmOn \t\tFALSE" : "m_bAlarmOn \t\tTRUE");
    lines.push_back(m_bBeingCarried == 0 ? "m_bBeingCarried \t\tFALSE" : "m_bBeingCarried \t\tTRUE");
    char b[64];
    std::snprintf(b, sizeof b, "m_dwAlarmTime \t\t%d", m_dwAlarmTime);
    lines.push_back(b);
}

// dll:100161f0
int ObjAlarmClock::pickUp(Self& s) {
    GameObj::pickUp(s);
    m_bBeingCarried = 1;
    return 1;
}

// dll:10016210
int ObjAlarmClock::onDropped(Self& s, const std::string& hotspot) {
    GameObj::onDropped(s, hotspot);
    m_bBeingCarried = 0;
    return 1;
}

// dll:10016310: from the alarm time for 0x4b0 (1200) seconds the alarm rings; it wakes the player when the sleep is on
// the page of the clock's sound
int ObjAlarmClock::update(Self& s, i32 now) {
    u32 tod = cworld::timeOfDay(s.world());                               // DAT_100c1c28
    u32 alarm = static_cast<u32>(m_dwAlarmTime);
    if (m_bAlarmOn == 0 || tod <= alarm || alarm + 0x4b0 <= tod) {
        if (m_bRinging != 0) {
            s.host().CWorld_StopPageFX(5000);
            m_bRinging = 0;
            changeVariation(s, "OFF", -2);
        }
        return 1;
    }
    m_bRinging = 1;
    if (s.m_bAwake == 0) {
        if (objFxSite(m_pSoundFx) == s.m_pSite.item) {
            s.m_dwUnk320 = 1;                                             // DAT_100c2058
            s.m_wWakeReason = 2;                                          // DAT_100c1e48: woken by the alarm clock
        }
    } else {
        s.host().CWorld_StartPageFX(5000);
    }
    changeVariation(s, "RINGING", -2);
    return 1;
}

// dll:100165a0
int ObjAlarmClock::redraw(Self& s, int full) {
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d", static_cast<u32>(m_dwAlarmTime) / 0xe10, (static_cast<u32>(m_dwAlarmTime) % 0xe10) / 0x3c);
    m_szClock = b;
    PlayerHost& h = s.host();
    if (full == 0) {
        const char* image;
        if (m_bAlarmOn == 0) {
            image = "off";
        } else {
            h.Eng_DisplayImage("bell", -1, nullptr, 0, -1, nullptr, 0);
            image = "on";
        }
        h.Eng_DisplayImage(image, -1, nullptr, 0, -1, nullptr, 0);
    } else {
        bool off = m_bAlarmOn == 0;
        h.CEngineFX_SetImageVisibility("bell", off ? 0 : 1, -1);
        h.CEngineFX_SetImageVisibility("on", off ? 0 : 1, -1);
        h.CEngineFX_SetImageVisibility("off", off ? 1 : 0, -1);
        h.redrawPage();
    }
    h.drawAlarmDisplay(m_szClock, m_hFont6c, m_hFont70);
    h.redrawScreen();
    return 1;
}

// dll:100163c0
int ObjAlarmClock::onButtonHotspot(Self& s, const std::string& hotspot) {
    if (objutil::iequals(hotspot, "bOn")) {
        if (m_bAlarmOn == 0) {
            m_bAlarmOn = 1;
        } else {
            m_bAlarmOn = 0;
            if (m_bRinging != 0) {
                s.host().CWorld_StopPageFX(5000);
                changeVariation(s, "OFF", -2);
                m_bRinging = 0;
            }
        }
        redraw(s, 1);
        return 1;
    }
    if (objutil::iequals(hotspot, "bUp")) {
        u32 t = static_cast<u32>(m_dwAlarmTime) + 900;
        m_dwAlarmTime = static_cast<i32>(t);
        m_dwAlarmTime = static_cast<i32>(t % 0x15180);
        redraw(s, 1);
        return 1;
    }
    if (objutil::iequals(hotspot, "bDown")) {
        if (m_dwAlarmTime == 0) m_dwAlarmTime = 0x14dfc;
        if (static_cast<u32>(m_dwAlarmTime) < 0x385) m_dwAlarmTime = 0;
        else m_dwAlarmTime = static_cast<i32>(static_cast<u32>(m_dwAlarmTime) - 900);
        redraw(s, 1);
    }
    return 1;
}

// dll:100164c0
void ObjAlarmClock::onPageEnter(Self& s, const std::string& page) {
    ObjPaperItem::onPageEnter(s, page);
    if (m_bBeingCarried != 0) objFxFollow(m_pSoundFx, s);
}

// dll:10016570
void ObjAlarmClock::onPageLeave(Self& s, const std::string& page) {
    if (!page.empty() && std::toupper(static_cast<unsigned char>(page[0])) == 'X') redraw(s, 0);
}

// dll:10016230: the clock face is opened (time runs 1000 ticks per game second slower while it is on screen)
void ObjAlarmClock::onOpened(Self& s) {
    s.host().CTimeline_SetTicksPerGameSec(1000);
    s.host().CEngineFX_CreateFontResource("highgfb.ttf");
    s.host().CEngineFX_CreateLogicalFont("HighlandGothicFLF", 0x30, &m_hFont70);   // the family name inside highgfb.ttf
    s.host().CEngineFX_CreateLogicalFont("HighlandGothicFLF", 0x48, &m_hFont6c);
    s.host().CEngineFX_SetImageVisibility("AlarmOn", m_bAlarmOn != 0 ? 1 : 0, -1);
    objFxPlace(m_pSoundFx, s);
}

// dll:100162e0
void ObjAlarmClock::onClosed(Self& s) {
    s.host().CTimeline_SetTicksPerGameSec(0x28);
    s.host().CEngineFX_DeleteFont(m_hFont6c);
    s.host().CEngineFX_DeleteFont(m_hFont70);
    s.host().CEngineFX_RemoveFontResource("highgfb.ttf");
}

void ObjAlarmClock::dumpMembers(ObjMembers& out) const {
    commonMembers(out);
    putS(out, "m_szText", m_szText);
    putP(out, "m_pSoundFx", m_pSoundFx);
    putI(out, "m_bBeingCarried", m_bBeingCarried);
    putI(out, "m_dwAlarmTime", m_dwAlarmTime);
    putS(out, "m_szClock", m_szClock);
    putI(out, "m_bAlarmOn", m_bAlarmOn);
    putI(out, "m_bRinging", m_bRinging);
}

void ObjDataDisk::dumpMembers(ObjMembers& out) const {
    commonMembers(out);
    putS(out, "m_szSound", m_szSound);
}

void ObjVideoTape::dumpMembers(ObjMembers& out) const {
    commonMembers(out);
    putS(out, "m_szMovie", m_szMovie);
    putX(out, "m_dwPlayEvent", m_dwPlayEvent);
}

BIOSYS_REGISTER_CTOR(1001c470, [](World& w, const char* name, int32_t a1, int32_t a2, const char* a3, double a4) -> Item* {
    return w.adopt(std::make_unique<ObjPaperItem>(name, a1, static_cast<u32>(a2), a3, a4));
});
BIOSYS_REGISTER_CTOR(10015f20, [](World& w, const char* name, int32_t a1, const char* a2, Item* a3) -> Item* {
    return w.adopt(std::make_unique<ObjAlarmClock>(name, a1, a2, a3));
});
BIOSYS_REGISTER_CTOR(10015af0, [](World& w, const char* name, int32_t a1, const char* a2) -> Item* {
    return w.adopt(std::make_unique<ObjDataDisk>(name, a1, a2));
});
BIOSYS_REGISTER_CTOR(10063360, [](World& w, const char* name, int32_t a1, const char* a2) -> Item* {
    return w.adopt(std::make_unique<ObjVideoTape>(name, a1, a2));
});

} // namespace biosys::game
