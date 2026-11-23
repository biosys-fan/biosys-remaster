// CSelf: sleep, wake-up, rest, dreams and the idle handler (dll:10065e80, 10067540..10067bb0, 1006dfc0).
// Everything outside the player goes through PlayerHost (self_env.h), named after the original call.
#include "game/biomecel.h"
#include "game/feature.h"
#include "game/gameobj.h"
#include "game/path.h"
#include "game/plant.h"
#include "game/self.h"
#include "game/site.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace biosys::game {

namespace {

// Clamp dll:100074b0
void clampD(double& x, double lo, double hi) {
    if (x < lo) x = lo;
    else if (hi < x) x = hi;
}

// the text of an 11-byte name buffer
std::string textOf(const u8* p, size_t n) {
    size_t len = 0;
    while (len < n && p[len]) ++len;
    return std::string(reinterpret_cast<const char*>(p), len);
}

bool equalsNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// CBiome members +0x40 (sleep quality: energy and health gain) and +0x48 (sleep duration factor). CBiome::CBiome
// dll:10009f60 sets both to 1.0; World__InitBiomeParams dll:1002b560 sets both to 1.5 for the house biome (0x100c1208,
// its members at 0x100c1248 / 0x100c1250) and leaves the other eight biomes alone.
struct BiomeSleepFactors {
    double quality;
    double duration;
};
BiomeSleepFactors biomeSleepFactors(const Biome* b) {
    if (!b) throw Error("CSelf::Sleep: the player has no biome (crash in the original)");
    if (b->addr == 0x100c1208u) return {1.5, 1.5};
    return {1.0, 1.0};
}

} // namespace

// dll:10067540
int Self::CanSleep() {
    if (host().modalFlag() != 0) return 0;                       // DAT_100c275c
    if (m_szLocation[0] == 'U' || m_szLocation[0] == 'X') return 0;
    if (m_dwSynthAttack == 1) return 1;
    const CSite* site = dynamic_cast<const CSite*>(m_pSite.item);
    if (!site) throw Error("CSelf::CanSleep: the player has no site (crash in the original)");
    if ((site->flags() & 0x01000000u) != 0) return 0;            // byte +0x13 bit 0
    if (80.0 < m_dEnergy && g.sleepAwayFromBed == 0) return 0;
    if (m_bInsideAngelSequence != 0) return 0;
    if (m_bInsidePeyoteSequence != 0) return 0;
    g.sleepingPill = (1u < static_cast<u32>(m_dwSleepingPill)) ? 1 : 0;
    if (now() == m_dwLastWokeUp && g.sleepingPill == 0) return 0;
    return 1;
}

// dll:10067600. a: nap (nonzero) / b: a dream follows / c: remember the current page as the page to return to
int Self::Sleep(int a, int b, int c) {
    g.sleepAwayFromBed = a;
    g.sleepWithDream = b;
    if (CanSleep() == 0) return 0;
    Rng& rng = world().state().rng;
    g.sleepSeconds = static_cast<u32>(static_cast<int64_t>(rng.randRange(10800.0, 21600.0)));   // overwritten below; the draw stays
    host().CEngineFX_FadeToColour(0, 0, 0, 2, 100, 0x36);
    m_dwLastFellAsleep = now();
    if (m_pGoggles.item) host().CEngineFX_UnlockPalette(1);
    host().CEngineFX_LockPalette(7);
    m_dwUnk324 = 0;
    m_wWakeReason = 1;
    m_dwUnk320 = 0;
    host().clearWindow(m_lTopBarHandle, 0);                      // FUN_1006c790(this)
    host().closeSound(m_hBreathingLoop);
    m_hBreathingLoop = -1;
    host().closeSound(m_hHeartBeatLoop);
    m_hHeartBeatLoop = -1;
    host().closeSound(m_hDrunkVoice);
    m_hDrunkVoice = -1;
    if (CFeature* f = dynamic_cast<CFeature*>(m_pFeature.item)) f->onLeave();   // vtable +0x4c

    g.sleepHealthGain = 0.05;
    g.sleepEnergyGain = 0.25;
    const BiomeSleepFactors bf = biomeSleepFactors(m_pBiome);
    double quality = bf.quality;       // local_10 / [esp+0xc]
    double duration = bf.duration;     // [esp+0x14]
    g.sleepEnergyCap = 80.0;
    if (IsAtPage("RHUT")) {
        quality = 1.5;
        duration = 1.5;
    }
    if (m_bHasSleepingBag != 0 && a == 0) {
        quality = quality * 1.5;
        duration = duration * 1.5;
    }
    if (IsAtPage("nPH5BED") || IsAtPage("nPH17berth")) {
        quality = 2.0;
        duration = 2.0;
        g.sleepEnergyCap = 100.0;
    }
    if ((m_wBreathing & 1) == 0) {
        const CBiomeCel* cel = dynamic_cast<const CBiomeCel*>(m_pCel);
        if (!cel) throw Error("CSelf::Sleep: the player has no cel (crash in the original)");
        if (cel->oxygen() < 16.0) duration = duration * 0.5;
    }
    if (2u < static_cast<u32>(m_dwSleepingPill)) {
        duration = duration * 1.5;
        m_wSleepingPillsPopped = 0;
        m_dwSleepingPill = 1;
    }
    g.sleepEnergyGain = quality * g.sleepEnergyGain;
    g.sleepHealthGain = g.sleepHealthGain * quality;
    g.sleepSeconds = static_cast<u32>(static_cast<int64_t>(static_cast<double>(g.sleepSeconds) * duration));
    if (c != 0) {                                                // the copy includes the terminating NUL
        const std::string here = textOf(m_szLocation.data(), m_szLocation.size());
        m_szReturnPage.fill(0);
        std::memcpy(m_szReturnPage.data(), here.c_str(), std::min(here.size() + 1, m_szReturnPage.size()));
    }
    host().CSite_CloseAllSets();                                 // FUN_1000e6b0
    host().CEngineFX_LockSounds(1);
    UpdatePanoramaOffset();
    host().jumpToPage("zLandOfNod", 0, 0, 1, 0);
    return 1;
}

// dll:10067910
void Self::WakeUp() {
    host().CEngineFX_ClearWindow();
    m_bAwake = 1;
    m_dwTimeLastUpdate = now();
    m_dwLastWokeUp = now();
    if (!host().CEngineFX_UnlockPalette(7)) {
        host().writeLog("WakeUp() - unlock palette failed, lock:" + std::to_string(static_cast<u32>(host().paletteLockOwner())));
        return;
    }
    if (!host().CEngineFX_UnlockSounds(1)) {
        host().writeLog("WakeUp() - unlock sound failed");
        return;
    }
    host().CEngineFX_LoadPalette("palettes\\misc\\black.pal", nullptr, 0, 1, 0, 0);
    if (m_bHasSleepingBag != 0) {
        if (GameObj* bag = world().findObject("survibag")) {     // FUN_10029440(bag, "rolled", -2)
            if (host().ChangeGameObjectVariation(bag->name().c_str(), "rolled", -2)) bag->setVariant("rolled");
        }
    }
    switch (m_wWakeReason) {
    case 2:
        host().CWorld_StartPageFX(5000);
        break;
    case 3:
        host().CEngineFX_PlayRandomSound("sounds\\russell\\coughs\\h_cough", 2);
        break;
    case 4:
        host().CEngineFX_PlayRandomSound("sounds\\russell\\shivers\\shiver", 5);
        break;
    case 5:
        host().CEngineFX_PlayRandomSound("sounds\\russell\\phews\\phew", 5);
        break;
    default:
        break;
    }
    host().setForcePalette(1);
    host().setStartupPhase(2);
    const char c0 = static_cast<char>(m_szReturnPage[0]);
    int x, y;
    if (c0 == 'A' || c0 == 'F' || c0 == 'N') {
        x = 0;
        y = 0;
    } else {
        x = g.panoramaX;
        y = 0x148;
    }
    if (!host().jumpToPage(textOf(m_szReturnPage.data(), m_szReturnPage.size()).c_str(), x, y, 1, 0)) {
        host().writeLog("just didn't WakeUp()");
        return;
    }
    const std::string& cur = world().state().features.currentSiteName;   // DAT_100c1e24, set by the page-entered handler
    const char s0 = cur.empty() ? '\0' : cur[0];
    if (s0 == 'F' || s0 == 'N' || s0 == 'A') {
        host().setForcePalette(0);
        host().setStartupPhase(0);
        if (!m_pSite.item) host().writeLog("CSelf::WakeUp self.m_pSite = NULL");
        else host().CSite_PlayAmbientSounds(m_pSite.item);       // FUN_1000eaa0
        host().CTimeline_UpdateDiurnal(1, 1, 0, 1);
    }
    host().CTimeline_SetTicksPerGameSec(0x28);
    clampD(m_dHealth, 0.0, GetMaxHealth());
    clampD(m_dEnergy, 0.0, m_dHealth);
    clampD(m_dNutrition, 0.0, 100.0);
    clampD(m_dHydration, 0.0, 100.0);
    g.sleepAwayFromBed = 0;
    g.sleepingPill = 0;
    g.sleepWithDream = 0;
    UpdateBreathingSound(1);
    UpdateHeartBeatSound(1);
    m_dwSymptoms = m_dwSymptoms & ~m_dwSymptomsCured;
    m_dwSymptomsCured = 0;
}

// dll:10067bb0
int Self::Rest() {
    host().advanceTime(0xe10, 1);
    if (m_pPage) host().refreshPage(m_pPage);                    // FUN_1000f730
    if (m_pSite.item) host().CSite_RefreshFx(m_pSite.item);      // FUN_1000ca00
    host().CWorld_LocalFxUpdate();                               // FUN_10014350
    host().CTimeline_UpdateDiurnal(1, 1, 0, 1);
    m_dEnergy = (100.0 - m_dEnergy) * 0.01 * 10.0 + m_dEnergy;
    clampD(m_dEnergy, 0.0, m_dHealth);
    ShowMessage(1, 0, 0, 0);
    host().CEngineFX_PlayRandomSound("sounds\\russell\\sighs\\sigh", 4);
    UpdateBreathingSound(0);
    UpdateHeartBeatSound(0);
    return 1;
}

// dll:1006dfc0
bool Self::Dream() {
    host().CTimeline_SetTicksPerGameSec(-1);
    if (m_dwNextDream == 0) {
        int count = 0;
        for (int i = 0; i < 0x20; ++i) {
            const u32 bit = 1u << (i & 0x1f);
            if ((bit & m_dwDreamsAvailable) != 0 && (m_dwDreamsSeen & bit) == 0) ++count;
        }
        int seen = 0;
        const int r = world().state().rng.rand();
        for (int i = 0; i < 0x20; ++i) {
            const u32 bit = 1u << (i & 0x1f);
            if ((bit & m_dwDreamsAvailable) != 0 && (m_dwDreamsSeen & bit) == 0 &&
                (++seen, static_cast<u32>(r * count) / 0x7fff + 1 == static_cast<u32>(seen)))
                m_dwNextDream = static_cast<i32>(bit);
        }
    }
    const char* name;
    switch (static_cast<u32>(m_dwNextDream)) {
    case 8: name = "mem_2"; break;
    case 1: name = "dis_1"; break;
    case 2: name = "dis_2"; break;
    case 4: name = "mem_1"; break;
    case 0x20: name = "poem_2"; break;
    case 0x10: name = "poem_1"; break;
    case 0x80: name = "synth_1"; break;
    case 0x40: name = "poem_3"; break;
    case 0x100: name = "synth_2"; break;
    case 0x200: name = "synth_3"; break;
    default: name = "general"; break;
    }
    const std::string movie = std::string("movies\\dreams\\") + name + ".avi";
    int handle = -1;
    host().CEngineFX_StopLoopedAvi();                            // FUN_10061110
    host().CEngineFX_LoopedAviClosed();                          // FUN_10061120
    const int opened = host().Eng_OpenAVIMovie(movie.c_str(), -1, 0x4b, 0, handle);
    host().setMovieHandle(handle);                               // DAT_100c18f0
    bool result;
    if (opened == 0) {
        const int err = host().Eng_GetLastError();
        host().writeLog("CSelf::Dream OpenAVIMovie failed, " + movie + ", error " + std::to_string(err));
        result = false;
    } else {
        host().CEngineFX_FadeToColour(0, 0, 0, 0, 100, 0x36);
        host().CEngineFX_ClearWindow();
        host().clearWindow(m_lTopBarHandle, 0);                  // FUN_1006c790
        RefreshForPage();
        const std::string pal = std::string("palettes\\movies\\dreams\\") + name + ".pal";
        result = host().CEngineFX_LoadPalette(pal.c_str(), nullptr, 0, 1, 7, 0);
        host().Eng_PlayAVIMovie(handle, 1);
    }
    m_dwNextDream = 0;
    return result;
}

// dll:1006ef40
bool Self::IsAtPage(const char* page) const {
    return equalsNoCase(page, textOf(m_szLocation.data(), m_szLocation.size()));
}

// dll:10065e80. Returns 1 while the game runs normally, 0 after the sleep / wake-up code handled this idle call.
int Self::OnIdle() {
    if (m_bFallAsleepOnIdle != 0) {
        m_bFallAsleepOnIdle = 0;
        Sleep(0, 1, 1);
        return 0;
    }
    u32 elapsed = 0;
    if (m_bRunSleepOnIdle == 0) {
        if (m_bAwake == 0 && m_dwUnk324 != 0) {
            WakeUp();
            return 0;
        }
        if (g.angelSequenceDone != 0) {                          // CSelf+0x760
            g.angelSequenceDone = 0;
            m_bInsideAngelSequence = 0;
            m_dwWhiteAngelTrip = 0x438;
            host().CEngineFX_FadeToColour(0, 0, 0, 2, 100, 0x36);
            host().setForcePalette(1);
            host().setStartupPhase(3);
            host().jumpToPage(textOf(m_szReturnPage.data(), m_szReturnPage.size()).c_str(), 0, 0x148, 0, 0);
            return 0;
        }
        const u32 tick = host().getTickCount();
        if (g.hotspotMsgExpiry == 0 || tick <= g.hotspotMsgExpiry) {
            if (static_cast<u32>(m_dwHotspotExpiry) != 0 && static_cast<u32>(m_dwHotspotExpiry) < tick &&
                host().fadeFromBlackUntil() < tick) {
                // the hotspot name under the cursor (m_szHotspotName, +0x287) selects the message
                char* name = reinterpret_cast<char*>(m_szHotspotName.data());
                m_dwHotspotExpiry = 0;
                int stringId = 0;
                const char first = name[0];
                bool restoreY = false;
                if (first == 'Y') {
                    restoreY = true;
                    name[0] = 'L';
                }
                if (name[0] == 'L') {
                    CSite* site = dynamic_cast<CSite*>(m_pSite.item);
                    CPath* path = nullptr;                       // FUN_1000c960: the site's link with this name
                    if (site) {
                        for (Item* l : site->links()) {
                            CPath* p = dynamic_cast<CPath*>(l);
                            if (p && equalsNoCase(p->pathName(), textOf(m_szHotspotName.data(), m_szHotspotName.size()))) {
                                path = p;
                                break;
                            }
                        }
                    } else {
                        throw Error("CSelf::OnIdle: the player has no site (crash in the original)");
                    }
                    if (path) {
                        const u32 fl = path->flags();            // +0x1c
                        if ((fl & 0x80000000u) == 0) {
                            if (path->to() == m_pPreviousSite.item) stringId = 0x29;   // +8 compared with +0x250
                        } else if ((fl & 0x1000) == 0) {
                            if ((fl & 0x2000) == 0) {
                                if ((fl & 0x8000) == 0) {
                                    if ((fl & 0x10000) == 0) {
                                        if ((fl & 0x20000) != 0) stringId = 0xad;
                                    } else {
                                        stringId = 0x79;
                                        host().playSound("sounds\\house\\padlock.wav");
                                    }
                                } else if ((fl & 0x100) == 0) {
                                    stringId = 0x7a;
                                } else {
                                    stringId = 0x164;
                                }
                            } else {
                                stringId = 0x28;
                            }
                        } else {
                            const CBiomeCel* cel = dynamic_cast<const CBiomeCel*>(m_pCel);
                            if (!cel) throw Error("CSelf::OnIdle: the player has no cel (crash in the original)");
                            stringId = (cel->flags() & 2) == 0 ? 1 : 0x27;
                        }
                    }
                } else {
                    const CFeature* feat = dynamic_cast<const CFeature*>(m_pFeature.item);
                    if (feat && (feat->flags() & 0x08000000u) != 0 && name[1] == 'F' && name[2] == 'U' && name[3] == 'S' &&
                        name[4] == 'E') {
                        // "xFUSE_nn": the fuse number nn names the message 0x17b + nn - 1 (the original's switch on 1 << (nn - 1))
                        const int n = std::atoi(name + 6);
                        const u32 bit = 1u << ((static_cast<u32>(static_cast<signed char>(n)) - 1u) & 0x1f);
                        for (int i = 0; i <= 18; ++i)
                            if (bit == (1u << i)) stringId = 0x17b + i;
                    }
                }
                if (restoreY) name[0] = 'Y';
                int idLocal = 0xffff;
                if (stringId == 0) {
                    if ((m_dwDreamsSeen & 0x80000000u) != 0) HotspotHelpText(name, &stringId, &idLocal);
                    if (stringId == 0) return 1;
                }
                ShowMessage(1, stringId, idLocal, 0);
                m_bUnk2a4 = 1;                                   // DAT_100c1fdc
            }
        } else if (m_dwUnk2a0 == 0) {
            m_dwUnk298 = 0;
            g.hotspotMsgExpiry = 0;
            ShowMessage(1, 0xffff, 0, 0);
        } else {
            ShowMessage(1, m_dwUnk2a0, 0xffff, 0);
            m_dwUnk2a0 = 0;
        }
        return 1;
    }

    // the sleep page was drawn: the whole night is simulated here, in steps of 90 s
    m_bRunSleepOnIdle = 0;
    const double startEnergy = m_dEnergy;
    CSite* site = dynamic_cast<CSite*>(m_pSite.item);
    CBiomeCel* cel = dynamic_cast<CBiomeCel*>(m_pCel);
    if (!site || !cel) throw Error("CSelf::OnIdle: the player has no site / cel (crash in the original)");
    do {
        host().advanceTime(0x5a, 0);
        ResetDeltas();
        if ((~m_dwSymptomsCured & m_dwSymptoms & 0x40000200u) == 0 && m_dwSynthAttack == 0) {
            if (m_dEnergy - startEnergy < g.sleepEnergyCap) AddEnergy(g.sleepEnergyGain);
            AddHealth(g.sleepHealthGain * world().state().tuning.sleepHealing);
        }
        if ((m_wBreathing & 1) == 0 && 50.0 < cel->pollution() && m_dwFumes == 0) m_dwFumes = 1;
        m_dwUpdateDt = 0x5a;
        UpdateAilments();
        LoseNutrition(m_dNutrition * 0.01 * static_cast<double>(static_cast<u32>(m_dwUpdateDt)) * 0.00034722222222222224);
        LoseHydration(m_dHydration * 0.01 * static_cast<double>(static_cast<u32>(m_dwUpdateDt)) * 0.0002314814814814815);
        double airTemp = cel->airTemp() + site->unk0f8();
        double change = 0.0;
        if (m_bWearingPoncho != 0 && airTemp < 20.0) airTemp = airTemp + 15.0;
        if (m_bHasSleepingBag != 0 && g.sleepAwayFromBed == 0 && airTemp < 20.0) airTemp = airTemp + 15.0;
        const double target = 2.5 * airTemp;
        const double dt = static_cast<double>(static_cast<u32>(m_dwUpdateDt));
        if (target < m_dTemperature) {
            change = (target - m_dTemperature) * dt * 0.00044444444444444447;
            if (m_dIdealTemp < m_dTemperature) change = (m_dTemperature - m_dIdealTemp) * dt * -0.0017777777777777779 + change;
        } else if (target > m_dTemperature) {
            change = (target - m_dTemperature) * dt * 0.00044444444444444447;
            if (m_dIdealTemp > m_dTemperature) change = (m_dTemperature - m_dIdealTemp) * dt * -0.0017777777777777779 + change;
        }
        AddTemperature(change);
        if (g.sleepingPill == 0 && 20.0 < m_dEnergy) {
            if (75.0 < m_dTemperature) {
                m_dwUnk320 = 1;
                m_wWakeReason = 5;
            }
            if (m_dTemperature < 25.0) {
                m_dwUnk320 = 1;
                m_wWakeReason = 4;
            }
            if (0xe10u < static_cast<u32>(m_dwFumes)) {
                m_dwUnk320 = 1;
                m_wWakeReason = 3;
            }
            if ((cel->flags() & 2) != 0 && (site->flags() & 0x00800000u) == 0) {   // site byte +0x12 bit 7
                m_dwUnk320 = 1;
                m_wWakeReason = 7;
            }
            if (m_dwUnk320 != 0) break;
        }
        elapsed += 0x5a;
    } while (elapsed <= g.sleepSeconds - 0x5au);
    if (m_dwUnk320 == 0) host().advanceTime(g.sleepSeconds - elapsed, 0);
    clampD(m_dEnergy, 0.0, m_dHealth);
    g.sleepEnergyGained = m_dEnergy - startEnergy;               // CSelf+0x108
    if (!(0.0 < m_dHealth)) {
        Die(0);
        return 0;
    }
    if (g.sleepWithDream == 0 || !Dream()) WakeUp();
    return 0;
}

} // namespace biosys::game
