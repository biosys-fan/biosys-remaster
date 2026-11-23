// CSelf: eating, drinking, drugs (Consume), vomiting, weight, the drug trips, the synth attack and the debug dump.
// Original: BiosGame.dll, the CSelf member functions at dll:10068d70, 1006c9c0..1006cf40, 1006dc20..1006def0, 1006efd0, 100651d0.
//
// Engine/shell calls of the original and the PlayerHost virtual each one goes through:
//   FUN_10060700(&CEngineFX, file, 0, 0)            CEngineFX::PlaySound          host().playSound(file)
//   FUN_10060820(&CEngineFX, stem, n)               play stem + (rand() % n + 1) + ".wav"   Self::PlayRandomSound
//   FUN_10060780(&CEngineFX, file, &handle, 0, 0)   host().playSoundHandle
//   FUN_10060620 / FUN_100605f0 (r,g,b,a,b,c)       Eng_FadeToColour(r,g,b,1 or 0,a,c,b)  host().fadeToColour
//   FUN_10060670(&CEngineFX, p)                     host().fadeToPalette(p)
//   FUN_10061820(&CEngineFX)                        host().resetEngineFXSway()
//   FUN_1006c790 (Eng_ClearWindow(CSelf+0x328, 0)), FUN_1006f100 (black fill of the top bar)  host().clearWindow / blankTopBar
//   FUN_1000f730(page)                              host().refreshPage(page)
//   FUN_1006f320 = HelpTextsOn(), FUN_1006f330 = ShowMessageDeferred (self.h)
#include "game/self.h"
#include "game/sim.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace biosys::game {

namespace {

// FUN_10060620: Eng_FadeToColour(r, g, b, 1, p4, p6, p5) dll:10060620
void fadeA(PlayerHost& h, int r, int g, int b, int p4, int p5, int p6) { h.fadeToColour(r, g, b, 1, p4, p6, p5); }
// FUN_100605f0: Eng_FadeToColour(r, g, b, 0, p4, p6, p5) dll:100605f0
void fadeB(PlayerHost& h, int r, int g, int b, int p4, int p5, int p6) { h.fadeToColour(r, g, b, 0, p4, p6, p5); }

const char* const kSigh = "sounds\\russell\\sighs\\sigh";               // 1009f618
const char* const kEatYuk = "sounds\\russell\\disgust\\eatyuk";         // 100bd134
const char* const kDrinkYuk = "sounds\\russell\\disgust\\drinkyk";      // 100bd350
const char* const kCaffeine = "sounds\\russell\\stimus\\caffeine.wav";  // 100bd32c
const char* const kAdrenal = "sounds\\russell\\stimus\\adrenal.wav";    // 100bd3fc
const char* const kStrain = "sounds\\russell\\strains\\strain";         // 100a0f08

std::string fmt(const char* f, int v) {
    char buf[256];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

} // namespace

// FUN_10060820 dll:10060820
void Self::PlayRandomSound(const char* stem, unsigned count) {
    unsigned r = static_cast<unsigned>(world().state().rng.rand());
    std::string name = std::string(stem) + std::to_string(r % count + 1) + ".wav";   // "%s%d.wav" 100bc27c
    host().playSound(name.c_str());
}

// dll:1006cb90
void Self::GreenFlash(int p1, int p2) { fadeA(host(), 0, 0xff, 0, p1, p2, 0x36); }

// dll:10068d70
int Self::Eat(double nutrition, double protein, double hydration, u16 consumableCode) {
    // the original tests the high dword with 0x7fffffff and the low dword: nutrition != 0
    if (99.0 < m_dNutrition && nutrition != 0.0) {
        ShowMessage(1, 0xb1, 0xffff, 0);
        return 0;
    }
    switch (consumableCode) {
    case 0x118:
    case 0x122:
        break;
    default:
        host().playSound("sounds\\russell\\eating.wav");
        break;
    case 0x154:
    case 0x15e:
        PlayRandomSound(kEatYuk, 3);
    }
    AddNutrition(nutrition);
    clampD(m_dNutrition, 0, 100.0);
    AddProtein(protein);
    clampD(m_dProtein, 0, 100.0);
    AddHydration(hydration);
    clampD(m_dHydration, 0, 100.0);
    Consume(consumableCode);
    ShowMessage(0, 0, 0, 0);
    m_dwLastAte = now();
    if (HelpTextsOn()) ShowMessageDeferred(0x179);
    return 1;
}

// dll:1006ccd0
int Self::Drink(i16 code) {
    if (m_pAirMask.item != nullptr) {    // DAT_100c1fac = CSelf+0x274
        ShowMessage(1, 0x165, 0xffff, 0);
        return 0;
    }
    if (99.0 < m_dHydration) {
        ShowMessage(1, 0xc1, 0xffff, 0);
        return 0;
    }
    if (code == 0xc3) PlayRandomSound(kDrinkYuk, 2);
    switch (code) {
    case 200:
        if (m_dwWorms == 0) m_dwWorms = 1;
        [[fallthrough]];
    case 0xbe:
    case 0xc3:
        AddHydration(20.0);
        if (50.0 < m_dTemperature) LoseTemperature(10.0);
        if (m_dMaxTemp < m_dTemperature) m_dTemperature = m_dMaxTemp;
        break;
    case 0xd2:
        // ORIGINAL BUG: the dose limit (the doses drain energy later and are reset in Update) is tested after the hydration was
        // added: the refused third coffee still gives +10 hydration, without the clamp, the message or the bookkeeping.
        // Fixes::coffeeThirdDose: the test comes first; a refused coffee changes nothing
        if (world().state().fixes.coffeeThirdDose && 2 < m_byDosesCoffee) return 0;
        AddHydration(10.0);
        if (2 < m_byDosesCoffee && !world().state().fixes.coffeeThirdDose) return 0;
        if (m_dwChampagne == 0 && m_dwMescal == 0) {
            host().playSound(kCaffeine);
        } else {
            m_dwChampagne = 0;
            m_dwMescal = 0;
            // _DAT_100c1e74 = m_dwSymptoms, _DAT_100c1e78 = m_dwSymptomsCured
            if ((m_dwSymptoms & 0x40000000) == 0) m_dwSymptomsCured |= 0x200;
            else m_dwSymptoms &= 0xbfffffff;
            PlayRandomSound(kSigh, 4);
            host().enableMouseDeltaEvent(0);
        }
        m_byDosesCoffee = static_cast<u8>(m_byDosesCoffee + 1);
        if (m_dTemperature < 50.0) m_dTemperature = 50.0;
        AddEnergy(10.0);
        m_dwCoffee = 1;
        m_dEnergyGainCoffee = m_dEnergyGainCoffee + 10.0;
        if (m_lHeartBeatVol < -2000) m_lHeartBeatVol = -2000;
    }
    clampD(m_dHydration, 0, 100.0);
    ShowMessage(0, 0, 0, 0);
    m_wLastDrinkType = static_cast<u16>(code);
    m_dwLastDrank = now();
    if (HelpTextsOn()) ShowMessageDeferred(0x179);
    return 1;
}

// dll:1006c9c0
void Self::Vomit() {
    if (static_cast<u32>(m_dwLastAte) < static_cast<u32>(m_dwLastBarfed)) {
        Retch();
        return;
    }
    PlayRandomSound("sounds\\russell\\retching\\vomit", 3);
    GreenFlash(1, 0x14);
    m_dwLastBarfed = now();
    LoseNutrition(10.0);
    LoseHydration(5.0);
}

// dll:1006ca20
void Self::Retch() {
    PlayRandomSound("sounds\\russell\\retching\\retch", 5);
    GreenFlash(2, 10);
}

// dll:1006cf40
int Self::Consume(u16 code) {
    switch (code) {
    case 1:
        return 1;
    case 10:
        if (m_dwPrussic == 0) m_dwPrussic = 1;
        return 1;
    case 0x14:
        if (m_dwDioscorin == 0) m_dwDioscorin = 1;
        return 1;
    case 0x1e:    // adrenaline
        host().playSound(kAdrenal);
        fadeA(host(), 0xff, 0xff, 0xff, 1, 0x14, 0x36);
        m_dStimulant = m_dStimulant + 60.0;
        clampD(m_dStimulant, 0, 100.0);
        AddEnergy(100.0);
        m_lHeartBeatVol = 0;
        m_dEnergyGainAdrenaline = m_dEnergyGainAdrenaline + 100.0;
        if (m_dEnergyGainAdrenaline < 200.0) m_lHeartBeatVol = -30;
        m_dwAdrenaline = 1;
        UpdateHeartBeatSound(0);
        return 1;
    case 0x28:    // coca
        if (1 < m_byDosesCoca) return 0;
        m_byDosesCoca = static_cast<u8>(m_byDosesCoca + 1);
        m_dwCoca = 1;
        host().playSound("sounds\\russell\\stimus\\coca.wav");
        fadeA(host(), 0xff, 0xff, 0xff, 1, 0x14, 0x36);
        m_dStimulant = m_dStimulant + 30.0;
        clampD(m_dStimulant, 0, 100.0);
        AddEnergy(50.0);
        m_dEnergyGainCoca = m_dEnergyGainCoca + 50.0;
        if (m_lHeartBeatVol < -1000) m_lHeartBeatVol = -1000;
        UpdateHeartBeatSound(0);
        return 1;
    case 0x32:    // guarana
        if (1 < m_byDosesGuarana) return 0;
        host().playSound(kCaffeine);
        m_byDosesGuarana = static_cast<u8>(m_byDosesGuarana + 1);
        m_dwGuarana = 1;
        m_dStimulant = m_dStimulant + 20.0;
        clampD(m_dStimulant, 0, 100.0);
        AddEnergy(20.0);
        m_dEnergyGainGuarana = m_dEnergyGainGuarana + 20.0;
        if (m_lHeartBeatVol < -1500) m_lHeartBeatVol = -1500;
        UpdateHeartBeatSound(0);
        return 1;
    case 0x3c: {  // champagne
        m_dwChampagne = 1;
        m_dwMescal = 0;
        m_byGlassesDrunk = static_cast<u8>(m_byGlassesDrunk + 1);
        std::string file = fmt("sounds\\russell\\drunk\\cdrunk%d.wav", m_byGlassesDrunk);
        if (m_byGlassesDrunk < 6 && host().playSoundHandle(file.c_str(), &m_hDrunkVoice)) {
            m_dwSymptoms |= 0x40000000;
            host().resetEngineFXSway();
            host().enableMouseDeltaEvent(1);
            return 1;
        }
        Vomit();
        Sleep(1, 1, 1);
        return 1;
    }
    case 0x46:    // gas mask / fumes cure
        if (m_dwFumes != 0) {
            u32 cured = m_dwSymptomsCured;
            m_dwSymptomsCured = cured | 8;
            m_dwSymptomsCured = cured | 0x8008;
            PlayRandomSound(kSigh, 4);
            m_dwFumes = 0;
        }
        return 1;
    case 0x50:    // chloroquine
        m_dwChloroquine = 1;
        if ((m_dwSymptoms & 2) != 0) {
            PlayRandomSound(kSigh, 4);
            m_dwSymptomsCured |= 2;
        }
        return 1;
    case 0x5a:    // salt
        m_dSalt = m_dSalt + 10.0;
        clampD(m_dSalt, 0, 100.0);
        return 1;
    case 0x6e:    // sleeping pill
        m_wSleepingPillsPopped = static_cast<u16>(m_wSleepingPillsPopped + 1);
        m_dSedative = m_dSedative + 30.0;
        if (m_dwAdrenaline != 0) m_dwAdrenaline = 0x1c21;
        m_dwSleepingPill = 2;
        Sleep(1, 1, 1);
        return 1;
    case 0x78:
        PlayRandomSound(kSigh, 4);
        m_dProtein = (100.0 - m_dProtein) * 0.3 + m_dProtein;
        clampD(m_dProtein, 0, 100.0);
        return 1;
    case 0x82:
        if (m_dwWorms != 0) {
            m_dwSymptomsCured |= 0x10;
            PlayRandomSound(kSigh, 4);
            m_dwWorms = 0;
        }
        return 1;
    case 0x8c:
        if (m_dwPrussic != 0) {
            AddHealth(m_dPrussicHealthLoss);
            PlayRandomSound(kSigh, 4);
            m_dwSymptomsCured |= 0x20;
            m_dPrussicHealthLoss = 0;
            m_dwPrussic = 0;
        }
        return 1;
    case 0x96:    // cinchona
        m_dwCinchona = 1;
        if ((m_dwSymptoms & 2) != 0) {
            PlayRandomSound(kSigh, 4);
            m_dwSymptomsCured |= 2;
        }
        return 1;
    case 0xa0:
        if (m_dwSpiderBiteTimer != 0) {
            PlayRandomSound(kSigh, 4);
            m_dwSymptomsCured |= 4;
            m_dwSpiderBiteTimer = 0;
        }
        return 1;
    case 0xb4:    // white angel
        m_dwWhiteAngelTrip = 2;
        WhiteAngelTrip();
        m_dwPeyoteTripTimer = 0;
        return 1;
    case 0xbe:
    case 0xc3:
    case 200:
    case 0xd2:
        return Drink(static_cast<i16>(code));
    case 0xe6:    // peyote
        PlayRandomSound(kEatYuk, 3);
        m_dwPeyoteTripTimer = 2;
        PeyoteTrip();
        m_dwWhiteAngelTrip = 0;
        return 1;
    case 0xfa:
        m_dwIndigestionTimer = 1;
        return 1;
    case 0x104:   // ephedrine
        if (m_dwFumes == 0) {
            if (m_dwSynthAttack == 0) {
                host().playSound(kAdrenal);
            } else {
                m_dwSymptomsCured |= 0x4000;
                PlayRandomSound(kSigh, 4);
                m_dwSynthAttack = 0;
            }
        } else {
            u32 cured = m_dwSymptomsCured;
            m_dwSymptomsCured = cured | 8;
            m_dwSymptomsCured = cured | 0x8008;
            PlayRandomSound(kSigh, 4);
            m_dwFumes = 0;
        }
        fadeA(host(), 0xff, 0xff, 0xff, 1, 0x14, 0x36);
        m_dStimulant = m_dStimulant + 20.0;
        clampD(m_dStimulant, 0, 100.0);
        AddEnergy(20.0);
        if (m_lHeartBeatVol < -30) m_lHeartBeatVol = -30;
        m_dwEphedrineTimer = 1;
        UpdateHeartBeatSound(0);
        AddEnergy(10.0);
        return 1;
    case 0x105:
        return 1;
    case 0x10e:   // cactus
        PlayRandomSound(kDrinkYuk, 2);
        m_dwCactusPoisonTimer = 1;
        return 1;
    case 0x118:
        Vomit();
        LoseHealth(10.0, 0x10, 0);
        return 1;
    case 0x122:
        if (m_dwHogweedBurnTimer != 0) {
            PlayRandomSound(kSigh, 4);
            m_dwSymptomsCured |= 0x800;
            m_dwHogweedBurnTimer = 0;
            ShowMessage(1, 0xb0, 0xffff, 0);
        }
        return 1;
    case 300: {   // hogweed burn
        int n = world().state().rng.rand();
        std::string wav = fmt("sounds\\russell\\pain\\pain%d.wav", n % 5 + 1);
        host().playObjectUseAnimation("anims\\obj_use\\burnt.tan", wav.c_str(), 1, 0, 200, 0x45, 3, 0x42, nullptr, -2);
        m_dwHogweedBurnTimer = 1;
        return 1;
    }
    case 0x136:   // spider bite
        host().playObjectUseAnimation("anims\\obj_use\\spider.tan", "\\sounds\\misc\\spider.wav", 1, 0, 0xaa, 0x88, 1, 0x42,
                                      nullptr, -2);
        LoseHealth(10.0, 0x11, 0);
        m_dwSpiderBiteTimer = 1;
        return 1;
    case 0x140: { // mescal
        m_dwMescal = 1;
        m_dwChampagne = 0;
        host().setFXState(0xc350, 1);   // the original passes the short -0x3cb0
        m_bySwigsDrunk = static_cast<u8>(m_bySwigsDrunk + 1);
        std::string file = fmt("sounds\\russell\\drunk\\mdrunk%d.wav", m_bySwigsDrunk);
        if (m_bySwigsDrunk < 5 && host().playSoundHandle(file.c_str(), &m_hDrunkVoice)) {
            m_dwSymptoms |= 0x40000000;
            host().resetEngineFXSway();
            host().enableMouseDeltaEvent(1);
            return 1;
        }
        Vomit();
        host().setFXState(0xc350, 0);
        Sleep(1, 1, 1);
        return 1;
    }
    case 0x154:
        return 1;
    case 0x15e:
        return 1;
    }
    char text[96];
    std::snprintf(text, sizeof text, "BiosGame.dll: Dodgy consumable: %d", static_cast<int>(code));
    world().state().message = text;       // DAT_100c2650
    host().writeLog(text);
    return 0;
}

// dll:1006dc20
void Self::WhiteAngelTrip() {
    u32 t = static_cast<u32>(m_dwWhiteAngelTrip);
    if (0x7e90 < t) {
        m_dwWhiteAngelTrip = 0;
        return;
    }
    if (0x437 < t) {
        m_aSoundCue[21] = 1;    // CSelf+0x314
        return;
    }
    if (1 < t && m_szLocation[0] != 'E') {
        m_bInsideAngelSequence = 1;
        m_bInsidePeyoteSequence = 0;
        if (!m_pSite.item) throw Error("CSelf::WhiteAngelTrip: no current site");
        const std::string& site = m_pSite.item->name();
        if (site.size() + 1 > m_szReturnPage.size()) throw Error("site name too long for m_szReturnPage");
        std::memcpy(m_szReturnPage.data(), site.c_str(), site.size() + 1);
        PlayRandomSound("sounds\\russell\\moans\\moan", 5);
        host().blankTopBar(m_lTopBarHandle);                          // FUN_1006f100
        fadeB(host(), 0xff, 0xff, 0xff, 3, 100, 0x36);                // FUN_100605f0
        m_dwUnk2a8 = 0;
        host().jumpToPage("EAngel2", 0, 0x148, 0, 0);
        host().clearWindow(m_lTopBarHandle, 0);                       // FUN_1006c790
        host().fadeToPalette(3);
    }
}

// dll:1006dd30
void Self::PeyoteTrip() {
    u32 t = static_cast<u32>(m_dwPeyoteTripTimer);
    if (0x7e90 < t) {
        m_dwPeyoteTripTimer = 0;
        return;
    }
    if (0x2cf < t) {
        m_aSoundCue[21] = 1;    // CSelf+0x314
        return;
    }
    if (1 < t && m_szLocation[0] != 'E') {
        m_bInsidePeyoteSequence = 1;
        m_bInsideAngelSequence = 0;
        if (!m_pSite.item) throw Error("CSelf::PeyoteTrip: no current site");
        const std::string& site = m_pSite.item->name();
        if (site.size() + 1 > m_szReturnPage.size()) throw Error("site name too long for m_szReturnPage");
        std::memcpy(m_szReturnPage.data(), site.c_str(), site.size() + 1);
        PlayRandomSound("sounds\\russell\\moans\\moan", 5);
        host().blankTopBar(m_lTopBarHandle);                          // FUN_1006f100
        fadeB(host(), 0xff, 0xff, 0xff, 3, 100, 0x36);                // FUN_100605f0
        m_dwUnk2a8 = 0;
        host().jumpToPage("EPeyote1", 0, 0x148, 0, 0);
        host().clearWindow(m_lTopBarHandle, 0);                       // FUN_1006c790
        host().enableUserInput(0, 0);
        host().fadeToPalette(3);
    }
}

// dll:1006de50
int Self::AddWeight(double w) {
    m_dWeightCarried = m_dWeightCarried + w;
    if (10.0 < w && m_dEnergy < m_dWeightCarried) PlayRandomSound(kStrain, 3);
    return 1;
}

// dll:1006dea0
int Self::Drop(double w) {
    m_dWeightCarried = m_dWeightCarried - w;
    if (m_dWeightCarried < 0.0) {
        host().writeLog("CSelf::Drop - m_dWeightCarried < 0");
        m_dWeightCarried = 0;
    }
    return 1;
}

// dll:1006def0
int Self::CanPickUp(double w) {
    if (w <= 3.0) return 1;
    if (m_dEnergy < w) {
        PlayRandomSound(kStrain, 3);
        ShowMessage(1, 0x33, 0xffff, 0);
        return 0;
    }
    if (m_dEnergy - m_dWeightCarried < w) {
        PlayRandomSound(kStrain, 3);
        ShowMessage(1, 0x35, 0xffff, 0);
        return 0;
    }
    return 1;
}

// dll:1006efd0
void Self::OnSynthAttack() {
    host().clearWindow(m_lTopBarHandle, 0);      // FUN_1006c790
    LoseHealth(40.0, 8, 0);
    if (m_dHealth <= 0.0) {
        m_bDieOnUpdate = 1;
        return;
    }
    m_dwSynthAttack = 1;
    if (m_dHealth < m_dEnergy) m_dEnergy = m_dHealth;
    if (CanSleep() != 0) {
        m_bFallAsleepOnIdle = 1;
        return;
    }
    host().redrawPage();
    host().redrawScreen();
    host().fadeToPalette(1);
    if (m_pPage != nullptr) host().refreshPage(m_pPage);
    if (m_pObjHolding.item != nullptr) host().enableUserInput(1, 0);
}

// dll:100651d0
void Self::Dump() {
    auto line = [this](const char* f, double v) {
        char buf[256];
        std::snprintf(buf, sizeof buf, f, v);
        world().state().message = buf;
        host().writeLog(buf);
    };
    auto lineInt = [this](const char* f, int v) {
        char buf[256];
        std::snprintf(buf, sizeof buf, f, v);
        world().state().message = buf;
        host().writeLog(buf);
    };
    auto text = [this](const std::string& s) {
        world().state().message = s;
        host().writeLog(s);
    };
    auto named = [&](const char* prefix, const Item* it) {
        text(std::string(prefix) + (it ? it->name() : std::string("NULL")));
    };
    auto flag = [&](const char* t, const char* f, int v) { text(v == 0 ? f : t); };

    line("m_dHydration \t\t%f", m_dHydration);
    line("m_dNutrition \t\t%f", m_dNutrition);
    line("m_dProtein \t\t%f", m_dProtein);
    line("m_dTemperature \t\t%f", m_dTemperature);
    line("m_dIdealTemp \t\t%f", m_dIdealTemp);
    line("m_dMaxTemp \t\t%f", m_dMaxTemp);
    line("m_dHealth \t\t%f", m_dHealth);
    line("m_dEnergy \t\t%f", m_dEnergy);
    line("m_dWeightCarried \t\t%f", m_dWeightCarried);
    lineInt("m_wHydrationState \t\t%d", m_wHydrationState);
    lineInt("m_wNutritionState \t\t%d", m_wNutritionState);
    lineInt("m_wTempState \t\t%d", m_wTempState);
    lineInt("m_wHealthState \t\t%d", m_wHealthState);
    lineInt("m_wEnergyState \t\t%d", m_wEnergyState);
    lineInt("m_wBreathing \t\t%d", m_wBreathing);
    named("m_pPlant->m_szName \t\t", m_pPlant.item);
    named("m_pFeature->m_szName \t\t", m_pFeature.item);
    named("m_pObjHolding->m_szName \t\t", m_pObjHolding.item);
    named("m_pAirMask->m_szName \t\t", m_pAirMask.item);
    named("m_pGoggles->m_szName \t\t", m_pGoggles.item);
    text(std::string("m_szLocation \t\t") +
         std::string(reinterpret_cast<const char*>(m_szLocation.data()), strnlen(reinterpret_cast<const char*>(m_szLocation.data()), m_szLocation.size())));
    // ORIGINAL BUG: m_pSite->m_szName is read without a NULL test (the original would crash without a site).
    if (m_pSite.item) {
        text("m_pSite->m_szName, \t\t" + m_pSite.item->name());
    } else if (world().state().fixes.selfDumpNullSite) {
        text("m_pSite->m_szName, \t\t<none>");     // fix: as the line of the page below
    } else {
        throw Error("CSelf::Dump: no current site (the original dereferences NULL)");
    }
    if (m_pPage == nullptr) text("m_pPage->m_szName, \t\t<none>");
    else text("m_pPage->m_szName, \t\t" + m_pPage->name());
    lineInt("m_ulCompassHeading \t\t%d", static_cast<int>(m_ulCompassHeading));
    flag("m_bAbleToSleep \t\tTRUE", "m_bAbleToSleep \t\tFALSE", m_bAbleToSleep);
    lineInt("m_dwLastFellAsleep \t\t%d", m_dwLastFellAsleep);
    lineInt("m_dwLastWokeUp \t\t%d", m_dwLastWokeUp);
    lineInt("m_dwLastAte \t\t%d", m_dwLastAte);
    lineInt("m_dwLastBarfed \t\t%d", m_dwLastBarfed);
    lineInt("m_dwLastDrank \t\t%d", m_dwLastDrank);
    flag("m_bWearingPoncho \t\tTRUE", "m_bWearingPoncho \t\tFALSE", m_bWearingPoncho);
    flag("m_bHasSleepingBag \t\tTRUE", "m_bHasSleepingBag \t\tFALSE", m_bHasSleepingBag);
    lineInt("m_dwFumes \t\t%d", m_dwFumes);
    lineInt("m_dwMalaria \t\t%d", m_dwMalaria);
    line("m_dMalariaHealthLoss \t\t%f", m_dMalariaHealthLoss);
    lineInt("m_dwChloroquine \t\t%d", m_dwChloroquine);
    lineInt("m_dwCinchona \t\t%d", m_dwCinchona);
    flag("m_bImmuneToMalaria \t\tTRUE", "m_bImmuneToMalaria \t\tFALSE", m_bImmuneToMalaria);
    line("m_dSynthHealthLoss \t\t%f", m_dSynthHealthLoss);
    lineInt("m_dwWorms \t\t%d", m_dwWorms);
    line("m_dWormsHealthLoss \t\t%f", m_dWormsHealthLoss);
    lineInt("m_dwDioscorin \t\t%d", m_dwDioscorin);
    line("m_dDioscorinHealthLoss \t\t%f", m_dDioscorinHealthLoss);
    lineInt("m_dwSynthAttack \t\t%d", m_dwSynthAttack);
    line("m_dSynthAttackHealthLoss \t\t%f", m_dSynthAttackHealthLoss);
    lineInt("m_dwPrussic \t\t%d", m_dwPrussic);
    line("m_dPrussicHealthLoss \t\t%f", m_dPrussicHealthLoss);
    lineInt("m_dwChampagne \t\t%d", m_dwChampagne);
    lineInt("m_dwMescal \t\t%d", m_dwMescal);
    lineInt("m_dwWhiteAngelTrip \t\t%d", m_dwWhiteAngelTrip);
    lineInt("m_dwCoffee \t\t%d", m_dwCoffee);
    line("m_dEnergyGainCoffee \t\t%f", m_dEnergyGainCoffee);
    lineInt("m_byDosesCoffee \t\t%d", m_byDosesCoffee);
    lineInt("m_dwGuarana \t\t%d", m_dwGuarana);
    line("m_dEnergyGainGuarana \t\t%f", m_dEnergyGainGuarana);
    lineInt("m_byDosesGuarana \t\t%d", m_byDosesGuarana);
    lineInt("m_dwCoca \t\t%d", m_dwCoca);
    line("m_dEnergyGainCoca \t\t%f", m_dEnergyGainCoca);
    lineInt("m_byDosesCoca \t\t%d", m_byDosesCoca);
    lineInt("m_dwAdrenaline \t\t%d", m_dwAdrenaline);
    line("m_dEnergyGainAdrenaline \t\t%f", m_dEnergyGainAdrenaline);
    lineInt("m_dwSleepingPill \t\t%d", m_dwSleepingPill);
    lineInt("m_wSleepingPillsPopped \t\t%d", m_wSleepingPillsPopped);
    lineInt("m_dwTimeLastUpdate \t\t%d", m_dwTimeLastUpdate);
    flag("m_bDrunkVisuals \t\tTRUE", "m_bDrunkVisuals \t\tFALSE", m_bDrunkVisuals);
    flag("m_bTripAurals \t\tTRUE", "m_bTripAurals \t\tFALSE", m_bTripAurals);
    flag("m_bTripVisuals \t\tTRUE", "m_bTripVisuals \t\tFALSE", m_bTripVisuals);
    flag("m_bMalariaVisuals \t\tTRUE", "m_bMalariaVisuals \t\tFALSE", m_bMalariaVisuals);
    lineInt("m_lHeartBeatVol \t\t%d", m_lHeartBeatVol);
    flag("m_bDrunkenCursor \t\tTRUE", "m_bDrunkenCursor \t\tFALSE", m_bDrunkenCursor);
}

} // namespace biosys::game
