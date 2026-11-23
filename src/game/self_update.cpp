// CSelf: the metabolism (stat setters, Update, UpdateAilments), death, the symptom sounds. Translated from BiosGame.dll
// dll:10066730..10066a60 (stats), 10066aa0 (Update), 10067c80 (UpdateAilments), and the helpers named below. The
// original computes in x87 extended precision; the port in double, in the same operation order (read from the
// disassembly wherever the decompiler lost the FPU state).
#include "game/self.h"
#include "game/biomecel.h"
#include "game/blobrecords.h"
#include "game/feature.h"
#include "game/site.h"
#include <cstdio>
#include <strings.h>

namespace biosys::game {

namespace {

// Clamp dll:100074b0 (Clamp(&x, lo, hi))
void clamp(double& x, double lo, double hi) {
    if (x < lo) {
        x = lo;
        return;
    }
    if (hi < x) x = hi;
}

// index of the sound cue flag at CSelf+off (self.h m_aSoundCue starts at +0x2c0)
constexpr int cue(int off) { return (off - 0x2c0) / 4; }

template <size_t N>
std::string text(const std::array<u8, N>& a) {
    size_t n = 0;
    while (n < N && a[n]) n++;
    return std::string(reinterpret_cast<const char*>(a.data()), n);
}

CBiomeCel& celOf(Self& s) { return as<CBiomeCel>(s.m_pCel); }

// FUN_100605f0(r, g, b, p4, p5, p6) = Eng_FadeToColour(r, g, b, 0, p4, p6, p5); FUN_10060620 passes unk04 = 1
void fadeToColour0(Self& s, int r, int g, int b, int p4, int p5, int p6) { s.host().fadeToColour(r, g, b, 0, p4, p6, p5); }
void fadeToColour1(Self& s, int r, int g, int b, int p4, int p5, int p6) { s.host().fadeToColour(r, g, b, 1, p4, p6, p5); }

// FUN_10060820(&CEngineFX, prefix, n): plays "<prefix><rand() % n + 1>.wav" (the draw happens even when sounds are off)
void playRandomWav(Self& s, const char* prefix, unsigned n) {
    unsigned r = static_cast<unsigned>(s.world().state().rng.rand());
    char buf[300];
    std::snprintf(buf, sizeof buf, "%s%d.wav", prefix, static_cast<int>(r % n + 1));
    s.host().playSound(buf);
}

bool chance(Self& s, double percent) { return s.world().state().rng.chance(percent * 0.01 * 32767.0); }   // ChancePercent dll:10007570: rand() <= (int)(percent * 0.01 * 32767)

// dll:1006c860 (also FUN_1006c880 .. FUN_1006cc80 below: the sound cues of PlaySymptomSounds)
void dyingMoan(Self& s) { playRandomWav(s, "sounds\\russell\\moans\\dying", 2); }   // dll:1006c860
void moan(Self& s) { playRandomWav(s, "sounds\\russell\\moans\\moan", 5); }          // dll:1006c880
void yawn(Self& s) { playRandomWav(s, "sounds\\russell\\yawns\\yawn", 3); }          // dll:1006c8a0
void gibber(Self& s) { playRandomWav(s, "sounds\\russell\\gibbers\\gibber", 5); }    // dll:1006c8c0

// dll:1006c8e0: a yawn and a fade to black whose strength depends on the energy
void yawnFade(Self& s) {
    yawn(s);
    if (s.m_dEnergy < 2.0) {
        fadeToColour1(s, 0, 0, 0, 2, 100, 0x36);
        return;
    }
    if (s.m_dEnergy < 5.0) {
        fadeToColour1(s, 0, 0, 0, 1, 100, 0x36);
        return;
    }
    fadeToColour1(s, 0, 0, 0, 0, 100, 0x36);
}

// dll:1006c960
void shiver(Self& s) {
    playRandomWav(s, "sounds\\russell\\shivers\\shiver", 5);
    s.host().addJiggle(10, 0, 0, 0);
    s.host().addJiggle(-20, 0, 0, 0);
    s.host().addJiggle(10, 0, 0, 0);
    s.host().jigglePage(2, 0, -1);
}

void phew(Self& s) { playRandomWav(s, "sounds\\russell\\phews\\phew", 5); }   // dll:1006ca50

// dll:1006ca70
void cough(Self& s, short strength) {
    if (strength == 0x14) {
        playRandomWav(s, "sounds\\russell\\coughs\\m_cough", 2);
        return;
    }
    if (strength == 0x1e) {
        playRandomWav(s, "sounds\\russell\\coughs\\h_cough", 2);
        return;
    }
    playRandomWav(s, "sounds\\russell\\coughs\\l_cough", 2);
}

// dll:1006cac0: the blue flash of the cold shiver
void coldFade(Self& s) {
    if (s.m_dTemperature < 10.0) {
        fadeToColour1(s, 0, 0x80, 0xff, 1, 0x14, 0x36);
        return;
    }
    if (s.m_dTemperature < 20.0) {
        fadeToColour1(s, 0, 0x80, 0xff, 1, 10, 0x36);
        return;
    }
    fadeToColour1(s, 0, 0x80, 0xff, 2, 8, 0x36);
}

// dll:1006cb40: the red flash of the heat
void heatFade(Self& s) {
    if (80.0 < s.m_dTemperature) {
        fadeToColour1(s, 0xff, 0, 0, 1, 0x14, 0x36);
        return;
    }
    fadeToColour1(s, 0xff, 0, 0, 2, 10, 0x36);
}

void greenFade(Self& s, int p4, int p5) { fadeToColour1(s, 0, 0xff, 0, p4, p5, 0x36); }   // dll:1006cb90

// dll:1006cbc0
void yellowFadeMoan(Self& s, int p4, int p5) {
    fadeToColour1(s, 0xff, 0xff, 0, p4, p5, 0x36);
    moan(s);
}

void wheeze(Self& s) { playRandomWav(s, "sounds\\russell\\wheezing\\wheeze", 5); }   // dll:1006cc00

// dll:1006cc30 / 1006cc50: a voice in the head
void voices(Self& s) { s.host().playSound("sounds\\self\\voices.wav"); }
void poem(Self& s) { s.host().playSound("sounds\\self\\poem.wav"); }

// dll:1006cc80: thirst: a sound and, at most every 5 seconds (GetTickCount), the help message 0x179
void thirst(Self& s) {
    playRandomWav(s, "sounds\\russell\\thirst\\thirst", 5);
    u32 tick = s.host().getTickCount();
    if (s.HelpTextsOn() && 5000u < tick - s.g.lastThirstMessageTick) {
        s.g.lastThirstMessageTick = tick;
        s.ShowMessageDeferred(0x179);
    }
}

// dll:1006f090: the burnt hand: pain sound and the "burnt" use animation (only when the player has no feature page open)
void burntHand(Self& s) {
    if (s.m_pFeature.item == nullptr) {    // DAT_100c1f9c == 0
        s.host().sendKeyUp(0x53, 0xc0000001u);   // SendMessageA(window, WM_KEYUP, 'S', 0xc0000001)
        int r = s.world().state().rng.rand();
        char buf[64];
        std::snprintf(buf, sizeof buf, "sounds\\russell\\pain\\pain%d.wav", r % 5 + 1);
        s.host().playObjectUseAnimation("anims\\obj_use\\burnt.tan", buf, 1, 0, 200, 0x45, 3, 0x42, nullptr, -2);
    }
}

} // namespace

// dll:10066730
void Self::SetHydration(double v) {
    m_dHydration = v;
    clamp(m_dHydration, 0.0, 100.0);
}

// dll:10066760
void Self::SetEnergy(double v) {
    m_dEnergy = v;
    clamp(m_dEnergy, 0.0, 100.0);
}

// dll:10066790
void Self::SetHealth(double v) {
    m_dHealth = v;
    clamp(m_dHealth, 0.0, GetMaxHealth());
}

// dll:100667c0
void Self::SetNutrition(double v) {
    m_dNutrition = v;
    clamp(m_dNutrition, 0.0, 100.0);
}

// dll:100667f0
void Self::SetTemperature(double v) {
    m_dTemperature = v;
    clamp(m_dTemperature, 0.0, 100.0);
}

// dll:10066820
void Self::ResetDeltas() {
    m_dHydrationDelta = 0;
    m_dNutritionDelta = 0;
    m_dProteinDelta = 0;
    m_dTemperatureDelta = 0;
    m_dHealthDelta = 0;
    m_dEnergyDelta = 0;
}

// dll:10066850
void Self::AddHealth(double v) {
    m_dHealth = m_dHealth + v;
    m_dHealthDelta = m_dHealthDelta + v;
}

// dll:10066870
void Self::AddNutrition(double v) {
    m_dNutrition = m_dNutrition + v;
    m_dNutritionDelta = m_dNutritionDelta + v;
}

// dll:10066890
void Self::AddProtein(double v) {
    m_dProtein = m_dProtein + v;
    m_dProteinDelta = m_dProteinDelta + v;
}

// dll:100668b0
void Self::AddHydration(double v) {
    m_dHydration = m_dHydration + v;
    m_dHydrationDelta = m_dHydrationDelta + v;
}

// dll:100668d0
void Self::AddEnergy(double v) {
    m_dEnergy = m_dEnergy + v;
    clamp(m_dEnergy, 0.0, 100.0);
    m_dEnergyDelta = m_dEnergyDelta + v;
}

// dll:10066910
void Self::AddTemperature(double v) {
    m_dTemperature = m_dTemperature + v;
    if (m_dMaxTemp < m_dTemperature && 40.0 < m_dHydration) m_dTemperature = m_dMaxTemp;
    m_dTemperatureDelta = m_dTemperatureDelta + v;
}

// dll:10066950
void Self::LoseHealth(double v, u16 cause, int dieNow) {
    m_dHealth = m_dHealth - v;
    m_dHealthDelta = m_dHealthDelta - v;
    if (cause != 0 && m_dLargestHealthLoss < v) {
        m_wCauseOfDeath = cause;
        m_dLargestHealthLoss = v;
    }
    if (m_dHealth <= 0.0) {
        if (dieNow != 0) {
            Die(0);
            return;
        }
        m_bDieOnUpdate = 1;
    }
}

// dll:100669d0
void Self::LoseNutrition(double v) {
    m_dNutrition = m_dNutrition - v;
    m_dNutritionDelta = m_dNutritionDelta - v;
}

// dll:100669f0
void Self::LoseProtein(double v) {
    m_dProtein = m_dProtein - v;
    m_dProteinDelta = m_dProteinDelta - v;
}

// dll:10066a10
void Self::LoseHydration(double v) {
    m_dHydration = m_dHydration - v;
    m_dHydrationDelta = m_dHydrationDelta - v;
}

// dll:10066a30
void Self::LoseEnergy(double v) {
    m_dEnergy = m_dEnergy - v;
    m_dEnergyDelta = m_dEnergyDelta - v;
}

// dll:10066a60
void Self::LoseTemperature(double v) {
    m_dTemperature = m_dTemperature - v;
    if (m_dMaxTemp < m_dTemperature && 40.0 < m_dHydration) m_dTemperature = m_dMaxTemp;
    m_dTemperatureDelta = m_dTemperatureDelta - v;
}

// dll:10066aa0
void Self::Update(int full) {
    i32 clock = now();
    i32 elapsed = clock - m_dwTimeLastUpdate;
    m_dwTimeLastUpdate = clock;
    if (elapsed == 0) return;
    if (m_bAwake == 0) return;

    // the two hallucination sequences (the timer runs on the previous update's seconds)
    if (m_bInsideAngelSequence != 0) {
        u32 t = static_cast<u32>(m_dwWhiteAngelTrip) + static_cast<u32>(m_dwUpdateDt);
        m_dwWhiteAngelTrip = static_cast<i32>(t);
        if (full == 0) return;
        if (t < 0x438) return;
        m_bInsideAngelSequence = 0;
        fadeToColour0(*this, 0, 0, 0, 2, 100, 0x36);
        host().jumpToPage(text(m_szReturnPage).c_str(), 0, 0x148, 0, 0);
        host().fadeToPalette(3);
        return;
    }
    if (m_bInsidePeyoteSequence != 0) {
        u32 t = static_cast<u32>(m_dwUpdateDt) + static_cast<u32>(m_dwPeyoteTripTimer);
        m_dwPeyoteTripTimer = static_cast<i32>(t);
        if (full == 0) return;
        if (t < 0x2d0) return;
        m_bInsidePeyoteSequence = 0;
        fadeToColour0(*this, 0, 0, 0, 2, 100, 0x36);
        host().jumpToPage(text(m_szReturnPage).c_str(), 0, 0x148, 0, 0);
        host().fadeToPalette(3);
        return;
    }

    m_dwUpdateDt = elapsed;
    const u32 dt = static_cast<u32>(elapsed);
    const double dtd = dt;
    u32 tick = host().getTickCount();    // GetTickCount()
    if (m_pCel == nullptr) return;
    CBiomeCel& cel = celOf(*this);
    double airTemp = cel.airTemp();      // local_8: also the (unintended) result of the temperature step below when equal
    m_wCauseOfDeath = 0;
    m_dLargestHealthLoss = 0;
    ResetDeltas();
    m_bImmuneToMalaria = 0;
    for (int off : {0x2c0, 0x2c4, 0x2c8, 0x2cc, 0x2d0, 0x2d4, 0x2d8, 0x2dc, 0x2e0, 0x2e4, 0x2e8, 0x2ec, 0x2f0, 0x31c,
                    0x2f4, 0x2f8, 0x2fc, 0x300, 0x304, 0x308, 0x30c, 0x310, 0x314})
        m_aSoundCue[cue(off)] = 0;
    m_bAbleToSleep = 1;
    m_wBreathing = m_wBreathing & 0xff;
    m_aSoundCue[cue(0x318)] = 0;

    // Tuning (settings.h): the multipliers are the last factor of each product, 1.0 leaves the original double unchanged
    const Tuning& tu = world().state().tuning;
    const u32 awakeLimit = static_cast<u32>(tu.awakeLimitHours * 3600.0);       // 24 h = 0x15180
    if (static_cast<u32>(clock - m_dwLastWokeUp) > awakeLimit) LoseEnergy(dtd * 0.004166666666666667);
    LoseNutrition(dtd * 0.001736111111111111 * tu.nutritionDrain);
    if (m_dNutrition < 45.5) LoseEnergy(dtd * 0.00034722222222222224);
    if (m_dNutrition < 20.0) LoseHealth(dtd * 0.0002314814814814815 * tu.starveDamage, 4, 0);
    // ORIGINAL BUG (study A1): LoseProtein gets 1/10800 without dt: one update (90..125 game s) takes 0.0000926, i.e. 0.06 per
    // game day instead of 8; the protein bar never moves and the vitamins are pointless. Model::proteinPerSecond: times dt,
    // like every neighbouring drain (1/10800 per s: 100 -> 20 in 10 game days)
    LoseProtein(world().state().model.proteinPerSecond ? dtd * 9.259259259259259e-05 : 9.259259259259259e-05);
    if (m_dProtein < 20.0) LoseHealth(dtd * 3.472222222222222e-05, 0xb, 0);
    LoseHydration(dtd * 0.0011574074074074073 * tu.hydrationDrain);
    if (20.0 < airTemp) LoseHydration((airTemp - 20.0) * (dtd * 0.00011574074074074075) * tu.heatWaterTerm);
    if (m_dHydration < 20.0) {
        m_wBreathing |= 0x200;
        LoseHealth(dtd * 0.0005787037037037037 * tu.thirstDamage, 0xc, 0);
    }

    // The dried-out rainforest (cel "S9"): the FX 10000 runs while the player is dehydrated and hot there
    if (g.s9Cel == nullptr) g.s9Cel = world().findBiomeCel("S9");
    {
        bool hot = false;
        if (m_dHydration < 20.0 && 65.0 < m_dTemperature) {
            CBiomeCel& s9 = as<CBiomeCel>(g.s9Cel);
            if ((s9.flags() & 0x10) == 0 && 0.2 < s9.plantHealthAvg()) hot = true;
        }
        if (hot) {
            if (m_dwUnk2bc == 0) {
                m_dwUnk2bc = 1;
                host().setFXState(10000, 1);
            }
        } else if (m_dwUnk2bc != 0) {
            m_dwUnk2bc = 0;
            host().setFXState(10000, 0);
        }
    }

    // body temperature
    double outside = airTemp;
    if (m_bWearingPoncho != 0) outside = outside + 15.0;
    if ((cel.flags() & 2) != 0) outside = outside - 10.0;
    if (m_pPath != nullptr) outside = outside + 10.0;
    double target = (as<CSite>(m_pSite.item).unk0f8() + outside) * 2.5;
    double tempChange = airTemp;
    // ORIGINAL BUG: when the body temperature equals the target exactly, AddTemperature gets the cel's air temperature
    // (the local still holds it)
    if (target < m_dTemperature) {
        tempChange = (target - m_dTemperature) * dtd * 0.00044444444444444447;
        if (m_dIdealTemp < m_dTemperature)
            tempChange = (m_dTemperature - m_dIdealTemp) * dtd * -0.0017777777777777779 + tempChange;
    } else if (m_dTemperature < target) {
        tempChange = (target - m_dTemperature) * dtd * 0.00044444444444444447;
        if (m_dTemperature < m_dIdealTemp)
            tempChange = (m_dTemperature - m_dIdealTemp) * dtd * -0.0017777777777777779 + tempChange;
    } else if (world().state().fixes.bodyTempAtTarget) {
        tempChange = 0.0;       // fix: at the target nothing changes (both neighbouring branches are proportional to the distance)
    }
    AddTemperature(tempChange);
    if (73.0 < m_dTemperature) {
        LoseHealth((m_dTemperature - 73.0) * dtd * 0.0002777777777777778 * tu.heatDamage, 0xd, 0);
    } else if (m_dTemperature < 27.0) {
        LoseHealth((27.0 - m_dTemperature) * dtd * 0.0002777777777777778 * tu.coldDamage, 1, 0);
    }

    if ((m_wBreathing & 1) == 0) {
        if (50.0 < cel.pollution()) {
            m_wBreathing |= 0x400;
            if (m_dwFumes == 0) m_dwFumes = 1;
        }
        if (cel.oxygen() < 16.0) m_wBreathing |= 0x100;
    }
    if (m_lHeartBeatVol == 0 && m_pPath == nullptr) {
        Die(0xf);
        return;
    }

    double drain = dtd * -0.00011574074074074075;
    m_dStimulant = drain + m_dStimulant;
    if (m_dStimulant < 0.0) m_dStimulant = 0;
    m_dSedative = m_dSedative + drain;
    if (m_dSedative <= 0.0) {
        m_dSedative = 0;
        m_dwSleepingPill = 0;
    }
    m_dSalt = m_dSalt + drain;
    if (m_dSalt < 0.0) m_dSalt = 0;
    AddHealth(dtd * 2.3148148148148147e-05 * tu.healing);
    UpdateAilments();

    // malaria in the rainforest (page "R4" once the story milestone 0x80 is reached)
    {
        u32 milestones = world().state().sim.story ? world().state().sim.story->milestones : 0;   // DAT_100c272c
        if (strcasecmp(text(m_szLocation).c_str(), "R4") == 0 && (milestones & 0x80) != 0 &&
            30.0 < cel.soilWater() && chance(*this, dtd * 0.013888888888888888 * tu.malaria))
            CatchMalaria();
    }

    if ((host().cheatFlags() & 1) != 0) {    // IMMORTAL
        m_dHealth = 100.0;
        m_dEnergy = 100.0;
        m_dTemperature = 50.0;
        m_dHydration = 100.0;
        m_dNutrition = 100.0;
        m_dProtein = 100.0;
        return;
    }

    clamp(m_dHealth, 0.0, GetMaxHealth());
    clamp(m_dEnergy, 0.0, m_dEnergyGainCoca + m_dEnergyGainGuarana + m_dEnergyGainCoffee + m_dEnergyGainAdrenaline + m_dHealth);
    clamp(m_dEnergy, 0.0, 100.0);
    clamp(m_dTemperature, 0.0, 100.0);
    clamp(m_dNutrition, 0.0, 100.0);
    clamp(m_dHydration, 0.0, 100.0);
    if (m_dHealth == 0.0 && m_pPath == nullptr) {
        Die(0);
        return;
    }
    if (m_dHealth < 10.0) m_aSoundCue[cue(0x2c0)] = 1;
    if (m_dHealth < 30.0) m_aSoundCue[cue(0x2c4)] = 1;
    if (80.0 < m_dEnergy) m_bAbleToSleep = 0;

    u32 hover = host().lastHoverTick();   // DAT_1009f834
    if (0.0 < m_dEnergy || full == 0 || hover == 0 || tick <= hover + 1000u || m_pPath != nullptr) {
        if (m_dEnergy < 22.5) {
            m_aSoundCue[cue(0x2c8)] = 1;
            if (m_dEnergy < 10.0) {
                m_wBreathing |= 4;
                m_aSoundCue[cue(0x2cc)] = 1;
            }
        }
        if (m_dNutrition < 80.0) m_aSoundCue[cue(0x2d0)] = 1;
        if (m_dHydration < 40.0) m_aSoundCue[cue(0x2d4)] = 1;
        if (m_dTemperature <= 73.0) {
            if (m_dTemperature <= 65.0) {
                if (30.0 <= m_dTemperature) {
                    if (m_dTemperature < 40.0) m_aSoundCue[cue(0x2dc)] = 1;
                } else {
                    m_aSoundCue[cue(0x300)] = 1;
                }
            } else {
                m_aSoundCue[cue(0x2d8)] = 1;
            }
        } else {
            m_aSoundCue[cue(0x2f8)] = 1;
        }
    } else {
        Sleep(1, 1, 1);
    }
    if (full != 0) {
        UpdateLoopSounds();
        PlaySymptomSounds();
        ShowMessage(0, 0, 0, 0);
    }
}

// dll:10067c80
void Self::UpdateAilments() {
    const u32 dt = static_cast<u32>(m_dwUpdateDt);
    const double dtd = dt;
    const i32 clock = now();

    m_dwSymptoms &= 0xfffffeffu;
    m_dwSymptoms &= 0xfffffffeu;
    CBiomeCel& cel = celOf(*this);
    // low oxygen (not with the air mask on)
    if ((m_wBreathing & 1) == 0) {
        double oxygen = cel.oxygen();
        if (oxygen < 14.0) {
            m_dwSymptoms |= 1;
            LoseHealth((14.0 - oxygen) * dtd * 0.002777777777777778 * world().state().tuning.oxygenDamage, 0x12, 0);
        } else if (oxygen < 16.0) {
            m_dwSymptoms |= 0x100;
        }
    }
    if (73.0 < m_dTemperature)
        m_dwSymptoms |= 0x80;
    else
        m_dwSymptoms &= 0xffffff7fu;

    if (m_dwSpiderBiteTimer != 0) {
        m_dwSpiderBiteTimer = static_cast<i32>(static_cast<u32>(m_dwSpiderBiteTimer) + dt);
        LoseHealth(dtd * 0.001388888888888889, 0x11, 0);
        m_dwSymptoms |= 4;
        m_dwSymptomsCured &= 0xfffffffbu;
    }

    if (m_dwHogweedBurnTimer != 0) {
        u32 t = static_cast<u32>(m_dwHogweedBurnTimer) + dt;
        m_dwHogweedBurnTimer = static_cast<i32>(t);
        if (t > 0x15180u) {
            m_dwHogweedBurnTimer = 0;
            m_dwSymptoms &= 0xfffff7ffu;
        } else {
            m_aSoundCue[cue(0x318)] = 1;
            m_dwSymptoms |= 0x800;
            m_dwSymptomsCured &= 0xfffff7ffu;
        }
    }

    // fumes
    if (m_dwFumes != 0) {
        u32 fumes = static_cast<u32>(m_dwFumes);
        if ((m_wBreathing & 1) != 0 || cel.pollution() < 50.0) {
            m_dwSymptomsCured &= 0xfffffff7u;
            m_dwSymptomsCured &= 0xffff7fffu;
            m_dwSymptoms &= 0xfffffff7u;
            m_dwSymptoms |= 0x8000;
            if (dt + 1u < fumes)
                m_dwFumes = static_cast<i32>(fumes - dt);
            else
                m_dwFumes = 1;
        } else {
            fumes += dt;
            m_dwFumes = static_cast<i32>(fumes);
            if (fumes != 0) {
                m_aSoundCue[cue(0x2e8)] = 1;
                m_dwSymptomsCured &= 0xfffffff7u;
                m_dwSymptomsCured &= 0xffff7fffu;
                m_dwSymptoms &= 0xffff7fffu;
                m_dwSymptoms |= 8;
                if (fumes > 0xe10u) {
                    m_aSoundCue[cue(0x31c)] = 1;
                    m_aSoundCue[cue(0x2ec)] = 1;
                    if (fumes > 0x2a30u) m_aSoundCue[cue(0x2f0)] = 1;
                    if (50.0 < cel.pollution()) LoseHealth(dtd * 0.001388888888888889, 7, 0);
                }
            }
        }
    }

    // coffee
    if (m_dwCoffee != 0) {
        u32 t = static_cast<u32>(m_dwCoffee) + dt;
        m_dwCoffee = static_cast<i32>(t);
        double td = t;
        if (td <= 7200.0) {
            if (3600.0 < td) {
                m_lHeartBeatVol = static_cast<int>((td - 3600.0) * -0.2777777777777778 - 2000.0);
                LoseEnergy(static_cast<double>(static_cast<u32>(m_byDosesCoffee) * dt) * 0.002777777777777778);
            }
        } else {
            m_dwCoffee = 0;
            m_dEnergyGainCoffee = 0;
            m_byDosesCoffee = 0;
            m_lHeartBeatVol = -10000;
        }
    }
    // guarana
    if (m_dwGuarana != 0) {
        u32 t = static_cast<u32>(m_dwGuarana) + dt;
        m_dwGuarana = static_cast<i32>(t);
        double td = t;
        if (td <= 23400.0) {
            if (16200.0 < td) {
                m_lHeartBeatVol = static_cast<int>((td - 16200.0) * -0.20833333333333334 - 1500.0);
                LoseEnergy(0.0);
            }
        } else {
            m_byDosesGuarana = 0;
            m_dwGuarana = 0;
            m_lHeartBeatVol = -10000;
            m_dEnergyGainGuarana = 0;
        }
    }
    // coca
    if (m_dwCoca != 0) {
        u32 t = static_cast<u32>(m_dwCoca) + dt;
        m_dwCoca = static_cast<i32>(t);
        double td = t;
        if (td <= 30600.0) {
            if (23400.0 < td) {
                m_lHeartBeatVol = static_cast<int>((td - 23400.0) * -0.2777777777777778 - 1000.0);
                LoseEnergy(static_cast<double>(static_cast<u32>(m_byDosesCoca) * dt) * 0.006944444444444444);
                LoseNutrition(dtd * 0.002777777777777778);
            }
        } else {
            m_dwCoca = 0;
            m_byDosesCoca = 0;
            m_lHeartBeatVol = -10000;
            m_dEnergyGainCoca = 0;
        }
    }
    // adrenaline
    if (m_dwAdrenaline != 0) {
        u32 t = static_cast<u32>(m_dwAdrenaline) + dt;
        m_dwAdrenaline = static_cast<i32>(t);
        if (static_cast<u32>(m_dwSleepingPill) > 1u) {
            m_dwSleepingPill = 1;
            m_wSleepingPillsPopped = 0;
        }
        if (t > 0x1c20u) {
            m_lHeartBeatVol = -10000;
            m_dwAdrenaline = 0;
            m_dEnergyGainAdrenaline = 0;
        } else {
            double td = t;
            if (3600.0 < td) {
                m_lHeartBeatVol = static_cast<int>((td - 3600.0) * -0.825 - 30.0);
                LoseEnergy(dtd * 0.009722222222222222);
            }
        }
    }
    // cinchona and chloroquine cure malaria
    if (m_dwCinchona != 0) {
        u32 t = static_cast<u32>(m_dwCinchona) + dt;
        m_dwCinchona = static_cast<i32>(t);
        if (t > 0x3f480u) {
            m_dwCinchona = 0;
        } else {
            m_bImmuneToMalaria = 1;
            if (m_dwMalaria != 0) {
                m_dwMalaria = 0;
                playRandomWav(*this, "sounds\\russell\\sighs\\sigh", 4);
                AddHealth(m_dMalariaHealthLoss * 0.5);
                m_dMalariaHealthLoss = 0;
            }
        }
    }
    if (m_dwChloroquine != 0) {
        u32 t = static_cast<u32>(m_dwChloroquine) + dt;
        m_dwChloroquine = static_cast<i32>(t);
        if (t > 0x15180u) {
            m_dwChloroquine = 0;
        } else {
            m_bImmuneToMalaria = 1;
            if (m_dwMalaria != 0) {
                m_dwMalaria = 0;
                playRandomWav(*this, "sounds\\russell\\sighs\\sigh", 4);
                AddHealth(m_dMalariaHealthLoss * 0.5);
                m_dMalariaHealthLoss = 0;
            }
        }
    }
    // malaria
    if (m_dwMalaria != 0) {
        u32 t = static_cast<u32>(m_dwMalaria) + dt;
        m_dwMalaria = static_cast<i32>(t);
        if (t > 0x15180u && m_bImmuneToMalaria == 0) {
            m_dMaxTemp = 73.0;
            m_dIdealTemp = 73.0;
            LoseHealth(dtd * 0.00011574074074074075, 2, 0);
            m_dwSymptomsCured &= 0xfffffffdu;
            m_dwSymptoms |= 2;
            m_aSoundCue[cue(0x304)] = 1;
        } else {
            m_dwSymptoms &= 0xfffffffdu;
            m_dMaxTemp = 73.0;
            m_dIdealTemp = 50.0;
        }
    }
    // champagne
    if (m_dwChampagne != 0) {
        u32 t = static_cast<u32>(m_dwChampagne) + dt;
        m_dwChampagne = static_cast<i32>(t);
        double td = t;
        if (!(td < 3600.0)) {
            if (td < 36000.0) {
                if ((m_dwSymptoms & 0x40000000u) != 0) {
                    m_dwSymptoms &= 0xbfffffffu;
                    if (m_bInsidePeyoteSequence == 0 && m_bInsideAngelSequence == 0) host().enableMouseDeltaEvent(0);
                }
                m_dwSymptomsCured &= 0xfffffdffu;
                m_dwSymptoms |= 0x200;
                m_aSoundCue[cue(0x2e4)] = 1;
                m_dHydration = dtd * -0.001388888888888889 + m_dHydration;
            } else {
                m_dwChampagne = 0;
                m_dwSymptoms &= 0xbfffffffu;
                m_dwSymptoms &= 0xfffffdffu;
            }
        }
    }
    // mescal
    if (m_dwMescal != 0) {
        u32 t = static_cast<u32>(m_dwMescal) + dt;
        m_dwMescal = static_cast<i32>(t);
        double td = t;
        if (!(td < 3600.0)) {
            if (td < 36000.0) {
                if ((m_dwSymptoms & 0x40000000u) != 0) {
                    m_dwSymptoms &= 0xbfffffffu;
                    if (m_bInsidePeyoteSequence == 0 && m_bInsideAngelSequence == 0) host().enableMouseDeltaEvent(0);
                    host().setFXState(static_cast<i16>(0xc350), 0);
                }
                m_dwSymptomsCured &= 0xfffffdffu;
                m_dwSymptoms |= 0x200;
                m_aSoundCue[cue(0x2e4)] = 1;
                m_dHydration = dtd * -0.001388888888888889 + m_dHydration;
            } else {
                m_dwSymptoms &= 0xbfffffffu;
                m_dwSymptoms &= 0xfffffdffu;
                host().setFXState(static_cast<i16>(0xc350), 0);
                m_dwMescal = 0;
            }
        }
    }
    // synth attack
    if (m_dwSynthAttack != 0) {
        u32 t = static_cast<u32>(m_dwSynthAttack) + dt;
        m_dwSynthAttack = static_cast<i32>(t);
        if (t > 0x7080u) {
            m_dwSynthAttack = 0;
            m_dSynthAttackHealthLoss = 0;
            m_dwSymptoms &= 0xffffbfffu;
        } else {
            m_dwSymptomsCured &= 0xffffbfffu;
            m_dwSymptoms |= 0x4000;
        }
    }
    // dioscorin
    if (m_dwDioscorin != 0) {
        u32 t = static_cast<u32>(m_dwDioscorin) + dt;
        m_dwDioscorin = static_cast<i32>(t);
        if (t > 0x15180u) {
            m_dwDioscorin = 0;
            m_dDioscorinHealthLoss = 0;
            m_dwSymptoms &= 0xffffffbfu;
        } else if (t > 2u) {
            m_dwSymptomsCured &= 0xffffffbfu;
            m_dwSymptoms |= 0x40;
            if (t > 0x258u && static_cast<u32>(clock - m_dwLastBarfed) > 0x15180u)
                m_aSoundCue[cue(0x2e0)] = 1;
            else
                m_aSoundCue[cue(0x2e4)] = 1;
            double loss = dtd * 0.0005555555555555556;
            LoseHealth(loss, 0xe, 0);
            m_dDioscorinHealthLoss = m_dDioscorinHealthLoss + loss;
        }
    }
    // prussic acid
    if (m_dwPrussic != 0) {
        u32 t = static_cast<u32>(m_dwPrussic) + dt;
        m_dwPrussic = static_cast<i32>(t);
        if (t > 0x15180u) {
            m_dwPrussic = 0;
            m_dPrussicHealthLoss = 0;
            m_dwSymptoms &= 0xffffffdfu;
        } else if (t > 2u) {
            m_dwSymptomsCured &= 0xffffffdfu;
            m_dwSymptoms |= 0x20;
            if (t > 0x258u && static_cast<u32>(clock - m_dwLastBarfed) > 0x15180u)
                m_aSoundCue[cue(0x2e0)] = 1;
            else
                m_aSoundCue[cue(0x2e4)] = 1;
            double loss = dtd * 0.0005555555555555556;
            LoseHealth(loss, 5, 0);
            m_dPrussicHealthLoss = m_dPrussicHealthLoss + loss;
        }
    }
    // trips
    m_dwSymptoms &= 0xffffefffu;
    if (m_dwWhiteAngelTrip != 0) {
        m_dwSymptoms |= 0x1000;
        m_dwWhiteAngelTrip = static_cast<i32>(dt + static_cast<u32>(m_dwWhiteAngelTrip));
    }
    if (m_dwPeyoteTripTimer != 0) {
        m_dwSymptoms |= 0x1000;
        m_dwPeyoteTripTimer = static_cast<i32>(dt + static_cast<u32>(m_dwPeyoteTripTimer));
    }
    // indigestion
    if (m_dwIndigestionTimer != 0) {
        u32 t = static_cast<u32>(m_dwIndigestionTimer) + dt;
        m_dwSymptoms |= 0x10000;
        m_dwIndigestionTimer = static_cast<i32>(t);
        m_aSoundCue[cue(0x2e0)] = 1;
        if (t > 0x2a30u) {
            m_dwIndigestionTimer = 0;
            m_dwSymptoms &= 0xfffeffffu;
        }
    }
    if (m_dwCactusPoisonTimer != 0) {
        m_aSoundCue[cue(0x2e0)] = 1;
        u32 t = static_cast<u32>(m_dwCactusPoisonTimer) + dt;
        m_dwCactusPoisonTimer = static_cast<i32>(t);
        if (t > 0x2a30u) m_dwCactusPoisonTimer = 0;
    }
    // ephedrine
    if (m_dwEphedrineTimer != 0) {
        u32 t = static_cast<u32>(m_dwEphedrineTimer) + dt;
        m_dwEphedrineTimer = static_cast<i32>(t);
        if (t > 0x1c20u) {
            m_dwEphedrineTimer = 0;
        } else {
            double td = t;
            if (3600.0 < td) m_lHeartBeatVol = static_cast<int>((td - 3600.0) * -0.825 - 30.0);
        }
    }
    // worms
    if (m_dwWorms != 0) {
        u32 t = static_cast<u32>(m_dwWorms) + dt;
        m_dwWorms = static_cast<i32>(t);
        if (t > 0x15180u) {
            m_dwSymptoms |= 0x10;
            m_aSoundCue[cue(0x2e0)] = 1;
        }
    }
    if (50.0 < m_dSalt)
        m_dwSymptoms |= 0x2000;
    else
        m_dwSymptoms &= 0xffffdfffu;
    if (static_cast<u32>(m_dwSleepingPill) > 1u) m_dwSleepingPill = static_cast<i32>(dt + static_cast<u32>(m_dwSleepingPill));
}

// dll:1006df90
void Self::CatchMalaria() {
    if (m_bImmuneToMalaria == 0 && m_dwMalaria == 0) {
        m_dwMalaria = 1;
        return;
    }
}

// dll:1006f1c0
double Self::GetMaxHealth() const {
    double max = 100.0;
    int count = 0;
    for (int bit = 0; bit < 0x20; bit++) {
        if (((1u << bit) & m_dwSymptoms & 0xba7cu) != 0) {
            count++;
            max += -20.0 / count;
        }
    }
    return max;
}

// dll:10068f10
void Self::Die(u16 cause) {
    if (host().modalFlag() == 0 && m_pPath == nullptr) {
        if (cause != 0) m_wCauseOfDeath = cause;
        fadeToColour0(*this, 0, 0, 0, 3, 100, 0x36);
        host().clearWindow(-1, 0);            // CEngineFX::ClearWindow dll:10060df0
        host().clearWindow(m_lTopBarHandle, 0);   // FUN_1006c790
        host().jumpToPage("eDeath", 0, 0, 1, 0);
        host().setDead(1);
    }
}

// dll:1006a8e0
int Self::GetSymptomStringID(u32 bit) {
    switch (bit) {
    case 8: return 0xcc;
    case 1: return 0xce;
    case 2: return 0xca;
    case 4: return 0xd7;
    case 0x20: return 0xd3;
    case 0x10: return 0xcb;
    case 0x80: return 0xd0;
    case 0x40: return 0xd4;
    case 0x200: return 0xd2;
    case 0x100: return 0xcd;
    case 0x800: return 0xd5;
    case 0x400: return 0xcf;
    case 0x2000: return 0xd1;
    case 0x1000: return 0xd6;
    case 0x8000: return 0x158;
    case 0x4000: return 0x156;
    case 0x10000: return 0x161;
    case 0x40000000: return 0x14f;
    default: break;
    }
    // The original also formats "CSelf::GetSymptomStringID duff symptom %x" into DAT_100c2650 and logs it
    // (Eng_WriteLog); this function has no world to write it to.
    return 0;
}

// dll:10068960
void Self::UpdateLoopSounds() {
    if (m_bMalariaVisuals != 0) {
        // FUN_1006cc70: empty in the original
    }
    if (m_dwWhiteAngelTrip != 0) WhiteAngelTrip();
    UpdateBreathingSound(0);
    UpdateHeartBeatSound(0);
}

// dll:100689a0
void Self::PlaySymptomSounds() {
    const CFeature* feature = m_pFeature.item ? &as<CFeature>(m_pFeature.item) : nullptr;
    if (feature != nullptr && (feature->flags() & 0x800u) != 0 && feature->running()) return;
    if (m_hDrunkVoice != -1) return;

    // Each cue plays with a percentage chance; the first that does ends the update (except the gibber).
    if (m_aSoundCue[cue(0x2c0)] != 0 && chance(*this, 10.0)) {
        dyingMoan(*this);
        return;
    }
    if (m_aSoundCue[cue(0x2cc)] != 0 && chance(*this, 10.0)) {
        yawnFade(*this);
        return;
    }
    if (m_aSoundCue[cue(0x2e0)] != 0 && chance(*this, 30.0)) {
        Vomit();
    } else if (m_aSoundCue[cue(0x2e4)] != 0 && chance(*this, 40.0)) {
        Retch();
    } else if (m_aSoundCue[cue(0x2e8)] != 0 && chance(*this, 30.0)) {
        cough(*this, 10);
    } else if (m_aSoundCue[cue(0x2ec)] != 0 && chance(*this, 20.0)) {
        cough(*this, 0x14);
    } else if (m_aSoundCue[cue(0x2f0)] != 0 && chance(*this, 10.0)) {
        cough(*this, 0x1e);
    } else if (m_aSoundCue[cue(0x2dc)] != 0 && chance(*this, 60.0)) {
        shiver(*this);
    } else if (m_aSoundCue[cue(0x318)] != 0 && chance(*this, 30.0)) {
        burntHand(*this);
    } else if (m_aSoundCue[cue(0x30c)] != 0 && chance(*this, 20.0)) {
        poem(*this);
    } else if (m_aSoundCue[cue(0x310)] != 0 && chance(*this, 20.0)) {
        voices(*this);
    } else if (m_aSoundCue[cue(0x2c4)] != 0 && chance(*this, 20.0)) {
        moan(*this);
    } else {
        if (m_aSoundCue[cue(0x314)] != 0 && chance(*this, 30.0)) gibber(*this);
        if (m_aSoundCue[cue(0x2c8)] != 0 && chance(*this, 30.0)) {
            yawn(*this);
        } else if (m_aSoundCue[cue(0x2d4)] != 0 && chance(*this, 30.0)) {
            thirst(*this);
        } else if (m_aSoundCue[cue(0x31c)] != 0 && chance(*this, 30.0)) {
            wheeze(*this);
        } else {
            u32 milestones = world().state().sim.story ? world().state().sim.story->milestones : 0;   // DAT_100c272c
            if (((milestones >> 24) & 0x20) != 0 && chance(*this, 1.0)) {
                playRandomWav(*this, "sounds\\russell\\devlin\\devlin", 4);
            } else if (m_aSoundCue[cue(0x300)] != 0 && chance(*this, 20.0)) {
                shiver(*this);
                coldFade(*this);
            } else if (m_aSoundCue[cue(0x2fc)] != 0 && chance(*this, 20.0)) {
                greenFade(*this, 2, 0x14);
            } else if (m_aSoundCue[cue(0x304)] != 0 && chance(*this, 20.0)) {
                yellowFadeMoan(*this, 2, 0x14);
            } else if (m_aSoundCue[cue(0x2f8)] != 0 && chance(*this, 20.0)) {
                phew(*this);
                heatFade(*this);
            } else if (m_aSoundCue[cue(0x2d8)] != 0 && chance(*this, 30.0)) {
                phew(*this);
            }
        }
    }
    if (m_aSoundCue[cue(0x308)] != 0 && chance(*this, 20.0)) {
        // FUN_1006cc20: empty in the original
    }
}

// dll:1006da60
void Self::UpdateHeartBeatSound(int p) {
    if (m_lHeartBeatVol != g.lastHeartBeatVol || p != 0) {
        g.lastHeartBeatVol = m_lHeartBeatVol;
        if (m_lHeartBeatVol == -10000) {
            host().closeSoundHandle(&m_hHeartBeatLoop);
            m_hHeartBeatLoop = -1;
            return;
        }
        if (m_hHeartBeatLoop == -1) {
            host().playLoopedWav("sounds\\russell\\stimus\\heart1.wav", m_lHeartBeatVol, &m_hHeartBeatLoop);
            return;
        }
        host().setSoundVolume(m_hHeartBeatLoop, m_lHeartBeatVol);
    }
}

// dll:1006dae0
void Self::UpdateBreathingSound(int p) {
    u16 state = 0;
    if ((m_wBreathing & 2) != 0) {
        state = 2;
    } else if ((m_wBreathing & 1) != 0) {
        state = 1;
    } else if ((m_wBreathing & 0x100) != 0) {
        state = 0x100;
    }
    if (g.lastBreathingState != state || p != 0) {
        g.lastBreathingState = state;
        host().closeSoundHandle(&m_hBreathingLoop);
        m_hBreathingLoop = -1;
        const char* file = nullptr;
        switch (state) {
        case 0x100: file = "sounds\\objects\\lowair.wav"; break;
        case 1: file = "sounds\\objects\\mask.wav"; break;
        case 2: file = "sounds\\objects\\lowmask.wav"; break;
        case 4: file = "sounds\\self\\panting2.wav"; break;      // unreachable: state is only 0, 1, 2 or 0x100
        case 0x200: file = "sounds\\self\\drythrt.wav"; break;   // unreachable
        case 0x400: file = "sounds\\self\\wheezing.wav"; break;  // unreachable
        default: break;
        }
        if (file) host().playLoopedWav(file, 0, &m_hBreathingLoop);
    }
}

// dll:10065150
void Self::ResetPlayer() {
    host().closeSound(m_hUnk730);
    m_hUnk730 = -1;
    host().closeSound(m_hDrunkVoice);
    m_hDrunkVoice = -1;
    host().closeSound(m_hBreathingLoop);
    m_hBreathingLoop = -1;
    host().closeSound(m_hHeartBeatLoop);
    m_hHeartBeatLoop = -1;
    if (m_pSite.item != nullptr) host().resetSiteSoundHandles(m_pSite.item);   // FUN_1000c6e0
}

} // namespace biosys::game
