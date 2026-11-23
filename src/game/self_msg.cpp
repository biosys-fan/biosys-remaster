// CSelf messages and status screens: the status bar words and the message line (ShowMessage), the player's status
// screen (ShowStatus), the site / plant screen (ShowEnvironmentStatus) and the symptom sound cues.
// The original draws with GDI (LoadStringA into the buffer DAT_100c0de8, TextOutA on the window DC); here the logic is
// translated up to the drawing and the finished text goes to PlayerDisplay (positions and colours stay in comments).
#include "game/self.h"
#include "game/biomecel.h"
#include "game/featenv.h"
#include "game/feature.h"
#include "game/obj_food.h"
#include "game/plant.h"
#include "game/self_text.gen.h"
#include "game/sim.h"
#include "game/site.h"
#include "game/watertank.h"
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace biosys::game {

namespace {

// LoadStringA(DAT_100c0de0, id, buffer, 100): at most 99 characters; an unknown id gives an empty string.
std::string loadString(int id, size_t bufferSize = 100) {
    const char* s = dllString(id);
    if (!s) return std::string();
    std::string t(s);
    if (t.size() > bufferSize - 1) t.resize(bufferSize - 1);
    return t;
}

// __ftol: truncation toward zero; out of range gives the x87 "integer indefinite" 0x80000000
int ftol(double v) {
    if (!(v > -2147483649.0 && v < 2147483648.0)) return INT_MIN;
    return static_cast<int>(v);
}

std::string fmtInt(int v) {
    char b[32];
    std::snprintf(b, sizeof b, "%d", v);
    return b;
}

// "%0.0f" / "%+0.0f" of the 1998 C runtime: halves round away from zero (glibc / libc round them to even)
std::string fmtFixed0(double v, bool plus) {
    if (std::isfinite(v) && std::fabs(v - std::trunc(v)) == 0.5) v = v + (v < 0 ? -0.5 : 0.5), v = std::trunc(v);
    char b[400];
    std::snprintf(b, sizeof b, plus ? "%+0.0f" : "%0.0f", v);
    return b;
}

// "<1" when the quotient is 0, else the number: the hours / minutes columns of the status screen
std::string lessThanOneOr(u32 q) { return q == 0 ? std::string("<1") : fmtInt(static_cast<int>(q)); }

std::string lowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);   // CharLowerA
    return s;
}

CFeature* featureOf(const NamedRef& r) { return r.item ? dynamic_cast<CFeature*>(r.item) : nullptr; }

} // namespace

// The message line shared by ShowMessage (dll:1006bc40, second half) and ShowMessageLine (dll:1006c400). `id` is the
// new first id (0 = none), `second` the new second id; `last1/last2` are the DLL statics that remember the ids drawn last.
// Returns nothing; the state change is on CSelf +0x298 / +0x29c, DAT_100bc7b0 and +0x2a4.
static void drawMessageLine(Self& s, int id, int second, i32& last1, i32& last2) {
    if (id != 0) {
        s.m_dwUnk298 = id;
        s.g.hotspotMsgExpiry = s.host().getTickCount() + 3000;   // DAT_100bc7b0
        s.m_bUnk2a4 = 0;
    }
    if (s.m_dwUnk298 == 0xffff) {
        s.display().showMessage(0xffff, std::string());          // FillRect(0, 0x24, 0x27f, 0x37) in the background colour
        return;
    }
    if (s.m_dwUnk298 == 0) return;
    // SetTextColor(0x2d2dce1)
    std::string text = loadString(s.m_dwUnk298);
    if (second != 0) s.m_dwUnk29c = second;
    if (second == 0xffff) s.m_dwUnk29c = 0;
    if (s.m_dwUnk29c != 0) {
        std::string t2 = loadString(s.m_dwUnk29c);
        s.world().state().message = t2;                           // DAT_100c2650 is the buffer the second string is loaded into
        text += t2;                                               // strcat: appended without a separator
    }
    if (s.m_dwUnk298 != last1 || s.m_dwUnk29c != last2) {
        // the line is cleared (FillRect) when the ids changed
        last1 = s.m_dwUnk298;
        last2 = s.m_dwUnk29c;
    }
    // drawn centred at x = 0x140 - width / 2, y = 0x24
    bool fresh = id != 0 || second != 0;
    if (fresh) s.display().showMessage(s.m_dwUnk298, text);
    else s.display().redrawMessage(s.m_dwUnk298, text);
}

// dll:1006bc40
void Self::ShowMessage(int p1, int stringId, int p3, int p4) {
    CFeature* feature = featureOf(m_pFeature);
    if (!host().gameStarted() || m_lTopBarHandle == 0x29a || m_bStatusScreenVisible != 0 ||
        (feature && feature->unk54() != 0) || m_szLocation[0] == 'E' || m_bAwake == 0 || !host().windowDC())
        return;

    // health word at x = 0x21a, y = 0x12 (colours: 1 = 0x21c19ff, 2 = 0x27f7f7f, 3 = the background)
    if (10.0 <= m_dHealth) {
        if (30.0 <= m_dHealth) m_wHealthState = 3;
        else m_wHealthState = 2;
    } else {
        m_wHealthState = 1;
    }
    if (m_wHealthState == 3) display().showStatusWord("health", 3, std::string());   // FillRect (0x21a, 0x12, 0x27e, 0x26)
    else if (m_wHealthState == 1) display().showStatusWord("health", 1, loadString(0x43));
    else if (m_wHealthState == 2) display().showStatusWord("health", 2, loadString(0x44));

    // energy word at x = 0x121
    if (10.0 <= m_dEnergy) {
        if (22.5 <= m_dEnergy) m_wEnergyState = 3;
        else m_wEnergyState = 2;
    } else {
        m_wEnergyState = 1;
    }
    display().showStatusWord("energy", m_wEnergyState, loadString(0x3f));

    // nutrition word at x = 0x22
    if (20.0 <= m_dNutrition) {
        if (45.5 <= m_dNutrition) m_wNutritionState = 3;
        else m_wNutritionState = 2;
    } else {
        m_wNutritionState = 1;
    }
    display().showStatusWord("nutrition", m_wNutritionState, loadString(0x3d));

    // hydration word at x = 0xa2
    if (20.0 <= m_dHydration) {
        if (40.0 <= m_dHydration) m_wHydrationState = 3;
        else m_wHydrationState = 2;
    } else {
        m_wHydrationState = 1;
    }
    display().showStatusWord("hydration", m_wHydrationState, loadString(0x3e));

    // temperature word at x = 0x19e: 1 and 2 "cold" (0x41), 4 and 5 "hot" (0x40), 3 clears the place (FillRect 0x19e..0x1d0)
    if (27.0 <= m_dTemperature) {
        if (40.0 <= m_dTemperature) {
            if (65.0 < m_dTemperature) {
                if (73.0 < m_dTemperature) m_wTempState = 5;
                else m_wTempState = 4;
            } else {
                m_wTempState = 3;
            }
        } else {
            m_wTempState = 2;
        }
    } else {
        m_wTempState = 1;
    }
    switch (m_wTempState) {
    case 3: display().showStatusWord("temperature", 3, std::string()); break;
    case 1: case 2: display().showStatusWord("temperature", m_wTempState, loadString(0x41)); break;
    case 4: case 5: display().showStatusWord("temperature", m_wTempState, loadString(0x40)); break;
    }

    drawMessageLine(*this, stringId, p3, g.lastShownMsg1, g.lastShownMsg2);
    if (p4 != 0) m_dwUnk2a0 = p4;
}

// FUN_1006c400 dll:1006c400: the message line without the status bar (called while a message page is built)
void Self::ShowMessageLine(int p1, int p2) {
    CFeature* feature = featureOf(m_pFeature);
    if (!(host().gameStarted() && m_lTopBarHandle != 0x29a && m_bStatusScreenVisible == 0 &&
          (feature == nullptr || feature->unk54() == 0) && m_bInsideAngelSequence == 0 && m_bInsidePeyoteSequence == 0))
        return;
    if (!host().windowDC()) return;
    drawMessageLine(*this, p1, p2, g.lastShownLine1, g.lastShownLine2);
}

// FUN_1006c700 dll:1006c700: Eng_GetWindowDC, FillRect(0, 0x24, 0x27f, 0x37) in the background colour
void Self::ClearMessageLine() {
    display().showMessage(0xffff, std::string());
}

// FUN_1006f330 dll:1006f330
void Self::ShowMessageDeferred(int stringId) {
    if (m_dwUnk298 == 0 || m_dwUnk298 == 0xffff) {
        ShowMessage(1, stringId, 0xffff, 0);
        return;
    }
    m_dwUnk2a0 = stringId;
}

// FUN_1006c7b0 dll:1006c7b0
void Self::CloseStatusScreen() {
    host().closeSound(m_hUnk73c);                 // FUN_10060b40(handle)
    m_hUnk73c = -1;
    host().closeSound(m_hUnk740);
    m_hUnk740 = -1;
    host().closeSound(m_hUnk744);
    m_hUnk744 = -1;
    host().closeSound(m_hUnk748);
    m_hUnk748 = -1;
    host().closeSound(m_hUnk74c);
    m_hUnk74c = -1;
    host().clearWindow(m_lTopBarHandle, 0);       // FUN_1006c790
    m_bStatusScreenVisible = 0;
    ShowMessage(1, 0, 0, 0);
}

// dll:10068f90
void Self::ShowStatus(int open) {
    if (open == 0) {
        display().statusScreenClose("status");     // Eng_DisplayImage("blackbar", hide), Eng_ShowSimStatusWindow(0)
        host().CEngineFX_UnlockPalette(9);          // FUN_10060cb0(&CEngineFX, 9)
        if (m_pGoggles.item) host().restoreGogglesPalette(m_pGoggles.item);   // FUN_1001d600
        m_dEnergyShown = m_dEnergy;
        m_dHealthShown = m_dHealth;
        m_dTemperatureShown = m_dTemperature;
        m_dNutritionShown = m_dNutrition;
        m_dHydrationShown = m_dHydration;
        m_bStatusScreenVisible = 0;
        ShowMessage(1, 0, 0, 0);
        return;
    }
    CFeature* feature = featureOf(m_pFeature);
    if (m_bAwake == 0 || m_pPath != nullptr || (feature && feature->unk54() != 0) || m_szLocation[0] == 'U' ||
        m_bStatusScreenVisible != 0 || host().synthState() == 4 || host().subTechState() == 4 || host().modalFlag() != 0)
        return;
    RefreshForPage();                               // FUN_1006f100
    host().clearWindow(m_lTopBarHandle, 0);         // FUN_1006c790
    m_dwUnk298 = 0;
    m_dwUnk29c = 0;
    m_dwUnk2a0 = 0;
    if (m_pGoggles.item) host().CEngineFX_UnlockPalette(1);   // FUN_10060cb0(&CEngineFX, 1)
    host().CEngineFX_LockPalette(9);                // FUN_10060ce0(&CEngineFX, 9)
    m_bStatusScreenVisible = 1;
    if (!display().statusScreenOpen("status", false)) {
        m_bStatusScreenVisible = 0;                 // Eng_ShowSimStatusWindow(0) and give up
        return;
    }
    PlayerDisplay& d = display();
    // the four parts of the day; the current one is drawn again in another colour
    d.showText("daypartNight", loadString(0x6a));          // (0x234, 0)
    d.showText("daypartDay", loadString(0x6c));            // (0x239, 0x3d)
    d.showText("daypartDawn", loadString(0x6b));           // (0x25c, 0x1f)
    d.showText("daypartDusk", loadString(0x6d));           // (0x20d, 0x1f)
    switch (world().state().sim.diurnal) {                 // DAT_100c1c44
    case 5: case 0xf: d.showText("daypartNowDawn", loadString(0x6b)); break;
    case 0x14: d.showText("daypartNowDay", loadString(0x6c)); break;
    case 0x1e: case 0x23: d.showText("daypartNowDusk", loadString(0x6d)); break;
    case 0x28: d.showText("daypartNowNight", loadString(0x6a)); break;
    default: break;
    }
    // Pie(0x233, 0x17, 0x255, 0x39, ...) from DAT_100c1c28 (time of day) over 24 h
    d.statusScreenClock(static_cast<int>(cworld::timeOfDay(world())));
    d.statusScreenHotspot("sleep");                        // Eng_EnableSimStatusHotspot("sleep", 1)
    d.showText("sleepButton", loadString(0x68));           // (0x1bb, 3)
    d.showText("restButton", loadString(0x75));            // (0x1be, 0x1d)

    // energy: number right-aligned at x = 0x1f, y = 0x47; gauge x = 0x25 + ftol(energy * 2), y = 0x51
    std::string t;
    if (m_dEnergy <= 0.0 || 1.0 <= m_dEnergy) t = fmtInt(ftol(m_dEnergy));
    else t = "1";
    d.showText("energyValue", t);
    d.statusScreenGauge("energy", m_dEnergy, ftol(m_dEnergy * 2.0));
    if (m_dwAdrenaline != 0 || m_dwCoca != 0 || m_dwEphedrineTimer != 0 || m_dwGuarana != 0 || m_dwCoffee != 0)
        d.showText("stimulus", loadString(0x160));         // (0x75, 0x34)
    // weight carried: gauge x = 0x25 + ftol(weight * 2), y = 0x68
    d.statusScreenGauge("weight", m_dWeightCarried, ftol(m_dWeightCarried * 2.0));
    if (m_dEnergy < m_dWeightCarried) d.showText("overloaded", loadString(0x69));   // (0x8f, 0x86)

    // health: number centred at x = 620, y = 0x5f; gauge x = 0x192 + ftol(health * 2), y = 0x69
    if (1.0 <= m_dHealth) t = fmtInt(ftol(m_dHealth));
    else t = "1";
    d.showText("healthValue", t);
    d.statusScreenGauge("health", m_dHealth, ftol(m_dHealth * 2.0));

    // the symptoms, one per line from y = 0x85 down to 0xc4, at x = 0x197
    u32 bit = 1;
    if (m_dwSymptoms != 0) {
        int y = 0x85;
        do {
            if (0xc4 < y) break;
            if ((m_dwSymptoms & bit) != 0) {
                std::string line = loadString(GetSymptomStringID(bit));
                if ((m_dwSymptomsCured & bit) != 0) {
                    std::string cured = loadString(0x15b);
                    world().state().message = cured;       // DAT_100c2650
                    line += cured;                         // " (cured)"
                }
                d.showText("symptom", line);
                y += 0x10;
            }
            bit *= 2;
            // ORIGINAL BUG: the loop ends only when bit exceeds the mask; with bit 31 set in m_dwSymptoms, bit wraps to 0
            // and the original never ends. The symptom bits stop at 0x8000, so this does not happen.
            if (bit == 0) break;
        } while (bit <= m_dwSymptoms);
    }

    // temperature: gauge (0x11e, 0x109 - ftol(temperature * 2))
    d.statusScreenGauge("temperature", m_dTemperature, ftol(m_dTemperature * 2.0));
    // hydration: hours since the last drink at x = 0x1bb (right-aligned), y = 0x155; gauge (0x196, 0x165 - ftol(hydration))
    if (m_dwLastDrank != 0) {
        u32 e = static_cast<u32>(now()) - static_cast<u32>(m_dwLastDrank);
        d.showText("lastDrank", lessThanOneOr(e / 0xe10));
    }
    d.statusScreenGauge("hydration", m_dHydration, ftol(m_dHydration));
    // nutrition: hours since the last meal at x = 0x22e, y = 0x155; gauge (0x208, 0x165 - ftol(nutrition))
    if (1u < static_cast<u32>(m_dwLastAte)) {
        u32 e = static_cast<u32>(now()) - static_cast<u32>(m_dwLastAte);
        d.showText("lastAte", lessThanOneOr(e / 0xe10));
    }
    d.statusScreenGauge("nutrition", m_dNutrition, ftol(m_dNutrition));
    // protein: gauge (0x27a, 0x165 - ftol(protein))
    d.statusScreenGauge("protein", m_dProtein, ftol(m_dProtein));

    // sleep: hours since the player woke up (x = 0xa8, y = 0xa4), hours slept (y = 0xb9), the wake reason (x = 0x9e, y = 0xd0)
    // and the energy gained by the sleep (x = 0xe2, y = 0xe3)
    if (0x7080u < static_cast<u32>(m_dwLastWokeUp)) {
        u32 hoursAwake = (static_cast<u32>(now()) - static_cast<u32>(m_dwLastWokeUp)) / 0xe10;
        d.showText("wokeUp", lessThanOneOr(hoursAwake));
        u32 hoursSlept = static_cast<u32>(m_dwLastWokeUp - m_dwLastFellAsleep) / 0xe10;
        std::string slept = lessThanOneOr(hoursSlept);
        d.showText("slept", slept);
        std::string reason = slept;     // the text of the hours slept stays in the buffer when no reason string is loaded
        switch (m_wWakeReason) {
        case 1: reason = loadString(0x6e); break;
        case 2: reason = loadString(0x6f); break;
        case 3: reason = loadString(0x70); break;
        case 4: reason = loadString(0x71); break;
        case 5: reason = loadString(0x72); break;
        case 6: reason = loadString(0x73); break;
        case 7: reason = loadString(0x74); break;
        default: break;
        }
        d.showText("wakeReason", reason);
        // CSelf+0x108 is a double OnIdle writes (g.sleepEnergyGained); printed with "%+d" of its integer part
        char b[32];
        std::snprintf(b, sizeof b, "%+d", ftol(g.sleepEnergyGained));
        d.showText("sleepEnergy", b);
    }

    // the last journey: minutes ago (y = 0x116), cost (y = 299), minutes taken (y = 0x140), energy (x = 0xe2, y = 0x155)
    if (m_szLastJourneyName[0] != 0) {
        u32 minutesAgo = (static_cast<u32>(now()) - static_cast<u32>(m_dwJourneyTime)) / 0x3c;
        d.showText("journeyAgo", lessThanOneOr(minutesAgo));
        d.showText("journeyCost", fmtFixed0(m_dJourneyCost, false));
        u32 sec = static_cast<u32>(m_dwJourneySeconds);
        if (sec < 0x3c) t = "<1";
        else t = fmtInt(static_cast<int>(sec / 0x3c));
        d.showText("journeyTime", t);
        if (m_dJourneyEnergy < 1.0) t = "<1";
        else t = fmtFixed0(-m_dJourneyEnergy, true);
        d.showText("journeyEnergy", t);
    }
    d.statusScreenDone("status");                   // Eng_GetReleaseSimStatusDC(0), Eng_RedrawScreen
}

// dll:1006aa80
void Self::ShowEnvironmentStatus(int open) {
    if (open == 0) {
        display().statusScreenClose("environment");   // Eng_ShowSimStatusWindow(0, 0, 0, -1)
        m_bStatusScreenVisible = 0;
        return;
    }
    CFeature* feature = featureOf(m_pFeature);
    if (!m_pBiome) throw Error("CSelf::ShowEnvironmentStatus (dll:1006aa80): the player has no biome");
    const u32 biomeAddr = m_pBiome->addr;
    if (m_bAwake == 0 || m_pPath != nullptr || (feature && feature->unk54() != 0) || biomeAddr == 0x100c2560 ||
        biomeAddr == 0x100c1208 || biomeAddr == 0x100c25d8 || biomeAddr == 0x100c1ba0 || m_bStatusScreenVisible != 0 ||
        host().synthState() == 6 || host().subTechState() == 6 || m_pGoggles.item != nullptr || host().modalFlag() != 0)
        return;
    RefreshForPage();                               // FUN_1006f100
    host().clearWindow(m_lTopBarHandle, 0);         // FUN_1006c790
    m_dwUnk298 = 0;
    m_dwUnk29c = 0;
    m_dwUnk2a0 = 0;

    CSite* site = dynamic_cast<CSite*>(m_pSite.item);
    CBiomeCel* cel = dynamic_cast<CBiomeCel*>(m_pCel);
    if (!site || !cel) throw Error("CSelf::ShowEnvironmentStatus (dll:1006aa80): the player has no site / cel");

    // the plant to show: the one looked at, else the hotspot's plant, else the site's first plant
    CPlant* plant = nullptr;
    if (m_pPlant.item == nullptr) {
        if ((site->flags() & 0x1000) == 0) {
            char c = static_cast<char>(m_szHotspotName[0]);
            if (c == 'A') {
                plant = world().findPlant(reinterpret_cast<const char*>(m_szHotspotName.data()));   // FUN_10014400
            } else if (c == 'O' || c == 'R') {
                GameObj* obj = world().findObject(reinterpret_cast<const char*>(m_szHotspotName.data()) + 1);
                // the original reads the pointer at obj+0x40; only the food objects hold a plant there
                if (obj) {
                    if (ObjFood* food = dynamic_cast<ObjFood*>(obj)) plant = food->plant();
                }
            }
        } else if (!site->plants().empty()) {
            plant = dynamic_cast<CPlant*>(site->plants().front());   // *(*(site+0x48) + 8): head of the site's plant list
        }
    } else {
        plant = dynamic_cast<CPlant*>(m_pPlant.item);
    }
    if (plant && (plant->flags() & 8) != 0) plant = nullptr;   // inert plants are not shown
    m_bStatusScreenVisible = 1;
    display().statusScreenOpen("environment", plant != nullptr);   // site_bk, plant_bk; Eng_GetReleaseSimStatusDC(1)
    PlayerDisplay& d = display();

    if (plant) {
        const CBiomeCel* pcel = plant->cel();
        int deadFlag = 0;
        int textId = 0;   // ORIGINAL BUG: when the health state has none of the bits 1, 0x800, 2, 4 the original loads the
                          // string whose id is left over in a stack slot; here nothing is shown for it
        // plant health gauge (0x32 + ftol(health * 113), 0x4c)
        d.statusScreenGauge("plantHealth", plant->health(), ftol(plant->health() * 113.0));
        d.showText("plantName", lowerAscii(loadString(plant->speciesId())));   // (6, 0x1e)
        u32 st = plant->healthState();
        // the colour: green (3, 119, 3) = 0x02037703, red (255, 25, 28) = 0x021c19ff, dark red (174, 0, 0) = 0x020000ae
        if ((st & 1) != 0) {
            textId = 0x59;                                       // healthy
            d.statusTextColour(0x02037703u);
        } else if ((st & 0x800) != 0) {
            const bool worse = plant->health() <= plant->healthPrev();
            textId = worse ? 0x5d : 0xc9;
            d.statusTextColour(worse ? 0x020000aeu : 0x02037703u);
        } else if ((st & 2) != 0) {
            const bool worse = plant->health() <= plant->healthPrev();
            textId = worse ? 0x5b : 0xc9;
            d.statusTextColour(worse ? 0x021c19ffu : 0x02037703u);
        } else if ((st & 4) != 0) {
            if (plant->health() <= plant->healthPrev()) {
                textId = 0x5c;
                deadFlag = 1;
                d.statusTextColour(0x021c19ffu);
            } else {
                textId = 200;
                d.statusTextColour(0x02037703u);
            }
        }
        // fix: no known health bit leaves nothing to say (the original loads whatever string id was left in the stack slot)
        if (textId != 0 || !world().state().fixes.plantStateTextUninit)
            d.showText("plantState", loadString(textId));            // (0x32, 0x31)
        if ((plant->flags() & 0x200000) == 0) {
            textId = (plant->flags() & 0x80000) == 0 ? 0x67 : 0xbc;     // no flowers / flowers
            if (0.4 < plant->health()) {
                for (Item* it : world().list(Table::Objects)) {
                    ObjFood* food = dynamic_cast<ObjFood*>(it);
                    if (!food || food->plant() != plant) continue;
                    if (food->state() == 2) {
                        textId = (plant->flags() & 0x80000) == 0 ? 0x66 : 0xbb;   // has fruit / flowering
                        break;
                    }
                    if (food->state() == 1) {
                        u32 f = plant->flags();
                        if ((f & 0x80000) != 0) {
                            textId = 0xbd;                       // flowers growing
                        } else if (((f & 0x800000) == 0 || (pcel->flags() & 0x10) == 0) &&
                                   ((f & 0x400000) == 0 || (pcel->flags() & 0x10) != 0)) {
                            textId = 0x65;                       // fruit growing
                        }
                    }
                }
            }
            d.showText("plantFruit", loadString(textId));        // (0x32, 0x9c)
        }
        d.statusTextColour(0x020000aeu);                         // SetTextColor before the problem lines, whether or not there are any
        // the problem lines (colour 0x20000ae) are drawn from y = 0x54, 0x12 apart, one line per text shown (the last
        // one, "losing water", is drawn at the next free line without counting it)
        if ((st & 0x1000) != 0) d.showText("plantProblem", loadString(0xba));        // polluted
        if ((st & 0x40) != 0) d.showText("plantProblem", loadString(0xb7));          // too dry
        if ((st & 0x80) != 0) d.showText("plantProblem", loadString(0xb8));          // too wet
        if ((st & 0x100) != 0) {
            d.showText("plantProblem", loadString(0xb9));        // losing water
        }
        // leaf temperature gauge (0x32 + ftol(clamp(leafTemp * 0.025, 0, 1) * 113), 0xd9)
        double v = plant->leafTemp() * 0.025;
        clampD(v, 0.0, 1.0);
        d.statusScreenGauge("leafTemp", plant->leafTemp(), ftol(v * 113.0));
        if ((st & 0x400) != 0) d.showText("plantTemp", loadString(0xb6));    // too cold (0x32, 0xe2)
        if ((st & 0x200) != 0) d.showText("plantTemp", loadString(0xb5));    // too hot
        if (pcel->timeLastRain() != -1) {
            // days since it last rained at the plant's cel (DAT_100c1c20 is the timeline's day), right-aligned at x = 0x82, y = 0x103
            u32 day = static_cast<u32>(cworld::timeline(world()).m_dwDay);
            d.showText("daysSinceRain", fmtInt(static_cast<int>(day - static_cast<u32>(pcel->timeLastRain()) / 0x15180u - 1u)));
        }
        // water availability gauge (0x32 + ftol(v * 113), 0x132): zero for a dead plant
        v = deadFlag == 0 ? plant->waterAvailability() : 0.0;
        d.statusScreenGauge("waterAvailability", v, ftol(v * 113.0));
        v = deadFlag == 0 ? plant->transpirationRate() : 0.0;
        clampD(v, 0.0, 1.0);
        d.statusScreenGauge("transpiration", v, ftol(v * 113.0));       // x = 0xbb + ..., y = 0x132
        if (plant->stomatalAperture() < 0.2) d.showText("stomata", loadString(0x15e));   // (0x32, 0x13e)
        d.statusTextColour(0x02fa6000u);                                 // SetTextColor(0, 166, 47) before the cooling text
        if (0.2 < plant->transpirationRate()) d.showText("cooling", loadString(0x15f));  // (0xbb, 0x13d)
        v = plant->psRate() / plant->maxPSRate();
        clampD(v, 0.0, 1.0);
        d.statusScreenGauge("photosynthesis", v, ftol(v * 113.0));      // x = 0xbb + ..., y = 0x167
    }

    // the biome's health: gauge (0x206 + ftol(f * 113), 0x29) and "%0.0f%%" of f * 100 right-aligned at x = 0x27a
    double f = host().biomeHealthFraction(*m_pBiome);                   // FUN_10009b10
    d.statusScreenGauge("biomeHealth", f, ftol(f * 113.0));
    {
        std::string pct = fmtFixed0(f * 100.0, false) + "%";
        d.showText("biomePercent", pct);
    }
    int nameId = 0;   // the nine biomes minus the four excluded above leave exactly these five
    if (biomeAddr == 0x100c1c50) nameId = 0x11b;
    else if (biomeAddr == 0x100c0ee8) nameId = 0x11d;
    else if (biomeAddr == 0x100c24e8) nameId = 0x121;
    else if (biomeAddr == 0x100c1280 || biomeAddr == 0x100c2768) nameId = 0xc3;
    d.showText("biomeName", loadString(nameId));                        // (0x206, 0x31)
    u32 siteFlags = site->flags();
    if ((siteFlags & 0x200) != 0) {
        double h = cworld::ocean(world()).health();                     // COcean +0x88
        d.statusScreenGauge("oceanHealth", h, ftol(h * 113.0));         // (0x206 + ..., 0xda)
    } else if ((siteFlags & 0x2001100) != 0) {
        d.statusScreenGauge("celPlantHealth", cel->plantHealthAvg(), ftol(cel->plantHealthAvg() * 113.0));
    }
    double airT = cel->airTemp() + site->unk0f8();                      // the cel's air temperature plus the site's offset (+0xf8)
    int id;
    if (airT <= 32.0) {
        if (airT <= 25.0) {
            if (airT <= 20.0) {
                if (airT <= 16.0) {
                    id = 0x49;
                    if (airT <= 7.0) id = 0x4a;
                } else {
                    id = 0x48;
                }
            } else {
                id = 0x47;
            }
        } else {
            id = 0x46;
        }
    } else {
        id = 0x45;
    }
    d.showText("airTemp", loadString(id));                              // (0x206, 0xe2)
    double sw = cel->soilWater();
    if (sw <= 47.5) {
        if (sw <= 30.0) {
            if (sw <= 20.0) {
                id = 0x4e;
                if (sw <= 5.0) id = 0x4f;
            } else {
                id = 0x4d;
            }
        } else {
            id = 0x4c;
        }
    } else {
        id = 0x4b;
    }
    d.showText("soilWater", loadString(id));                            // (0x206, 0xf4)
    double wind = cel->windSpeed();
    uint64_t windBits;
    std::memcpy(&windBits, &wind, 8);
    id = 0x51;
    if ((windBits & UINT64_C(0x7fffffffffffffff)) != 0) id = 0x50;             // (+0x84 & 0x7fffffff) != 0 || +0x80 != 0
    d.showText("wind", loadString(id));                                 // (0x206, 0x106)
    double pol = cel->pollution();
    if (pol <= 20.0) {
        id = 0x54;
        if (10.0 < pol) id = 0x53;
    } else {
        id = 0x52;
    }
    if (pol > 20.0) d.statusTextColour(0x021c19ffu);                    // the colour changes only for the two worse readings
    else if (10.0 < pol) d.statusTextColour(0x020000aeu);
    d.showText("pollution", loadString(id));                            // (0x206, 0x118)
    double hum = cel->humidity();
    if (hum <= 80.0) {
        if (hum <= 70.0) {
            id = 0x57;
            if (hum <= 30.0) id = 0x58;
        } else {
            id = 0x56;
        }
    } else {
        id = 0x55;
    }
    d.showText("humidity", loadString(id));                             // (0x206, 0x12a)
    d.statusScreenDone("environment");     // Eng_GetReleaseSimStatusDC(0), Eng_RedrawScreen, Eng_ShowSimStatusWindow(1, 0, 0, 0x36)
}

// ---- the symptom sound cues (PlaySymptomSounds dll:100689a0) ------------------------------------------------------------

// FUN_1006c860 dll:1006c860
void Self::PlayDyingMoan() { PlayRandomSound("sounds\\russell\\moans\\dying", 2); }
// FUN_1006c880 dll:1006c880
void Self::PlayMoan() { PlayRandomSound("sounds\\russell\\moans\\moan", 5); }
// FUN_1006c8a0 dll:1006c8a0
void Self::PlayYawn() { PlayRandomSound("sounds\\russell\\yawns\\yawn", 3); }
// FUN_1006c8c0 dll:1006c8c0
void Self::PlayGibber() { PlayRandomSound("sounds\\russell\\gibbers\\gibber", 5); }

// FUN_1006c8e0 dll:1006c8e0
void Self::PlayYawnFade() {
    PlayRandomSound("sounds\\russell\\yawns\\yawn", 3);
    // FUN_10060620(r, g, b, p4, p5, 0x36) = Eng_FadeToColour(r, g, b, 1, p4, 0x36, p5)
    if (m_dEnergy < 2.0) host().fadeToColour(0, 0, 0, 1, 2, 0x36, 100);
    else if (m_dEnergy < 5.0) host().fadeToColour(0, 0, 0, 1, 1, 0x36, 100);
    else host().fadeToColour(0, 0, 0, 1, 0, 0x36, 100);
}

// FUN_1006c960 dll:1006c960
void Self::PlayShiver() {
    PlayRandomSound("sounds\\russell\\shivers\\shiver", 5);
    host().addJiggle(10, 0, 0, 0);               // FUN_100616f0
    host().addJiggle(-20, 0, 0, 0);
    host().addJiggle(10, 0, 0, 0);
    host().jigglePage(2, 0, -1);                 // FUN_10061790
}

// FUN_1006ca50 dll:1006ca50
void Self::PlayPhew() { PlayRandomSound("sounds\\russell\\phews\\phew", 5); }

// FUN_1006ca70 dll:1006ca70
void Self::PlayCough(i16 severity) {
    if (severity == 0x14) PlayRandomSound("sounds\\russell\\coughs\\m_cough", 2);
    else if (severity == 0x1e) PlayRandomSound("sounds\\russell\\coughs\\h_cough", 2);
    else PlayRandomSound("sounds\\russell\\coughs\\l_cough", 2);
}

// FUN_1006cac0 dll:1006cac0
void Self::FadeCold() {
    if (m_dTemperature < 10.0) host().fadeToColour(0, 0x80, 0xff, 1, 1, 0x36, 0x14);
    else if (m_dTemperature < 20.0) host().fadeToColour(0, 0x80, 0xff, 1, 1, 0x36, 10);
    else host().fadeToColour(0, 0x80, 0xff, 1, 2, 0x36, 8);
}

// FUN_1006cb40 dll:1006cb40
void Self::FadeHot() {
    if (80.0 < m_dTemperature) host().fadeToColour(0xff, 0, 0, 1, 1, 0x36, 0x14);
    else host().fadeToColour(0xff, 0, 0, 1, 2, 0x36, 10);
}

// FUN_1006cbc0 dll:1006cbc0
void Self::PlayMoanFade(int p1, int p2) {
    host().fadeToColour(0xff, 0xff, 0, 1, p1, 0x36, p2);
    PlayRandomSound("sounds\\russell\\moans\\moan", 5);
}

// FUN_1006cc00 dll:1006cc00
void Self::PlayWheeze() { PlayRandomSound("sounds\\russell\\wheezing\\wheeze", 5); }
// FUN_1006cc30 dll:1006cc30
void Self::PlayVoices() { host().playSound("sounds\\self\\voices.wav"); }
// FUN_1006cc50 dll:1006cc50
void Self::PlayPoem() { host().playSound("sounds\\self\\poem.wav"); }

// FUN_1006cc80 dll:1006cc80
void Self::PlayThirst() {
    PlayRandomSound("sounds\\russell\\thirst\\thirst", 5);
    u32 tick = host().getTickCount();
    if (HelpTextsOn() && 5000u < tick - g.lastThirstMessageTick) {   // _DAT_100bd308
        g.lastThirstMessageTick = tick;
        ShowMessageDeferred(0x179);
    }
}

} // namespace biosys::game
