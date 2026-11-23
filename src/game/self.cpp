#include "game/self.h"
#include "game/biomecel.h"
#include "game/feature.h"
#include "game/page.h"
#include "game/plant.h"
#include "game/site.h"
#include "game/gameobj.h"
#include <cstring>
#include <strings.h>

namespace biosys::game {

std::string NamedRef::name() const {
    size_t n = 0;
    while (n < raw.size() && raw[n]) n++;
    return std::string(reinterpret_cast<const char*>(raw.data()), n);
}

void NamedRef::set(Item* target) {
    item = target;
    raw.fill(0);
    if (!target) return;
    const std::string& s = target->name();
    if (s.size() > raw.size()) throw Error("name too long for the 11-byte name copy: " + s);
    std::memcpy(raw.data(), s.data(), s.size());
}

void NamedRef::clear() { set(nullptr); }

void Self::applyStartValues(double hydration, double nutrition, double energy) {
    if (hydration >= 0) m_dHydration = m_dHydrationShown = hydration;
    if (nutrition >= 0) m_dNutrition = m_dNutritionShown = nutrition;
    if (energy >= 0) m_dEnergy = m_dEnergyShown = energy;
}

// dll:10063ca0
Self::Self() {
    m_dHydration = dwordsToDouble(0x40440000);        // +0x00 40
    m_dHydrationShown = dwordsToDouble(0x40440000);   // +0x10
    m_dNutrition = dwordsToDouble(0x4052c000);        // +0x18 75
    m_dNutritionShown = dwordsToDouble(0x4052c000);   // +0x28
    m_dProtein = dwordsToDouble(0x40590000);          // +0x30 100
    m_dTemperature = dwordsToDouble(0x40490000);      // +0x40 50
    m_dTemperatureShown = dwordsToDouble(0x40490000); // +0x50
    m_dIdealTemp = dwordsToDouble(0x40490000);        // +0x58
    m_dMaxTemp = dwordsToDouble(0x40524000);          // +0x60 73
    m_dHealth = dwordsToDouble(0x40568000);           // +0x68 90
    m_dHealthShown = dwordsToDouble(0x40568000);      // +0x78
    m_dEnergy = dwordsToDouble(0x404f8000);           // +0x80 63
    m_dEnergyShown = dwordsToDouble(0x404f8000);      // +0x90
    m_dStimulant = 0;                                     // +0x98
    m_dSedative = 0;                                      // +0xa0
    m_dSalt = 0;                                          // +0xa8
    m_dWeightCarried = 0;                                 // +0xb0
    m_wHydrationState = 3;                                // +0xb8
    m_wNutritionState = 3;                                // +0xba
    m_wTempState = 3;                                     // +0xbc
    m_wHealthState = 3;                                   // +0xbe
    m_wEnergyState = 3;                                   // +0xc0
    m_wBreathing = 0;                                     // +0xc2
    m_szLastJourneyName[0] = 0;                           // +0xc4
    m_dJourneyCost = 0;                                   // +0xd0
    m_dwJourneySeconds = 0;                               // +0xd8
    m_dJourneyEnergy = 0;                                 // +0xe0
    m_dwJourneyTime = 0;                                  // +0xe8
    {   // +0xec: strcpy(DAT_1009fa94 = "R4"), then strupr, then copied over itself
        static const char kStartPage[] = "R4";
        std::memcpy(m_szLocation.data(), kStartPage, sizeof kStartPage);
        for (u8& c : m_szLocation) if (c >= 'a' && c <= 'z') c = static_cast<u8>(c - 'a' + 'A');
    }
    m_ulCompassHeading = 1;                               // +0xf8
    m_bAbleToSleep = 0;                                   // +0xfc
    m_dwLastFellAsleep = 0;                               // +0x100
    m_dwLastWokeUp = 0x7080;                              // +0x104
    m_wWakeReason = 1;                                    // +0x110
    m_dwDreamsAvailable = 0;                              // +0x114
    m_dwDreamsSeen = 0x80000000;                          // +0x118
    m_dwNextDream = 0;                                    // +0x11c
    m_dwLastAte = 1;                                      // +0x120
    m_dwLastBarfed = 0;                                   // +0x124
    m_pLastEatenPlant.clear();                            // +0x128
    m_dwLastDrank = 0;                                    // +0x12c
    m_wLastDrinkType = 1;                                 // +0x130
    m_bWearingPoncho = 0;                                 // +0x134
    m_bHasSleepingBag = 0;                                // +0x138
    m_dwSymptoms = 0;                                     // +0x13c
    m_dwSymptomsCured = 0;                                // +0x140
    m_dwSpiderBiteTimer = 0;                              // +0x144
    m_dwHogweedBurnTimer = 0;                             // +0x148
    m_dwFumes = 0;                                        // +0x14c
    m_dwMalaria = 0;                                      // +0x150
    m_dMalariaHealthLoss = 0;                             // +0x158
    m_dwChloroquine = 0;                                  // +0x160
    m_dwCinchona = 0;                                     // +0x164
    m_bImmuneToMalaria = 0;                               // +0x168
    m_dSynthHealthLoss = 0;                               // +0x170
    m_dwIndigestionTimer = 0;                             // +0x178
    m_dwCactusPoisonTimer = 0;                            // +0x17c
    m_dwEphedrineTimer = 0;                               // +0x180
    m_dwWorms = 0;                                        // +0x184
    m_dWormsHealthLoss = 0;                               // +0x188
    m_dwDioscorin = 0;                                    // +0x190
    m_dDioscorinHealthLoss = 0;                           // +0x198
    m_dwSynthAttack = 0;                                  // +0x1a0
    m_dSynthAttackHealthLoss = 0;                         // +0x1a8
    m_dwPrussic = 0;                                      // +0x1b0
    m_dPrussicHealthLoss = 0;                             // +0x1b8
    m_dwChampagne = 0;                                    // +0x1c0
    m_byGlassesDrunk = 0;                                 // +0x1c4
    m_dwMescal = 0;                                       // +0x1c8
    m_bySwigsDrunk = 0;                                   // +0x1cc
    m_dwWhiteAngelTrip = 0;                               // +0x1d0
    m_bInsideAngelSequence = 0;                           // +0x1d4
    m_dwPeyoteTripTimer = 0;                              // +0x1d8
    m_bInsidePeyoteSequence = 0;                          // +0x1dc
    m_dwCoffee = 0;                                       // +0x1e0
    m_dEnergyGainCoffee = 0;                              // +0x1e8
    m_byDosesCoffee = 0;                                  // +0x1f0
    m_dwGuarana = 0;                                      // +0x1f4
    m_dEnergyGainGuarana = 0;                             // +0x1f8
    m_byDosesGuarana = 0;                                 // +0x200
    m_dwCoca = 0;                                         // +0x204
    m_dEnergyGainCoca = 0;                                // +0x208
    m_byDosesCoca = 0;                                    // +0x210
    m_dwAdrenaline = 0;                                   // +0x214
    m_dEnergyGainAdrenaline = 0;                          // +0x218
    m_dwSleepingPill = 0;                                 // +0x220
    m_wSleepingPillsPopped = 0;                           // +0x224
    m_dwTimeLastUpdate = 0x7080;                          // +0x228
    m_dwUpdateDt = 0;                                     // +0x22c
    m_bDrunkVisuals = 0;                                  // +0x230
    m_bTripAurals = 0;                                    // +0x234
    m_bTripVisuals = 0;                                   // +0x238
    m_bMalariaVisuals = 0;                                // +0x23c
    m_lHeartBeatVol = static_cast<i32>(0xffffd8f0);       // +0x240 -10000
    m_bDrunkenCursor = 0;                                 // +0x244
    m_pPreviousSite.clear();                              // +0x250
    m_pPath = nullptr;                                    // +0x254
    m_pCel = nullptr;                                     // +0x25c
    m_pPlant.clear();                                     // +0x260
    m_pFeature.clear();                                   // +0x264
    m_pObjHolding.clear();                                // +0x268
    m_pObjTarget = nullptr;                               // +0x26c
    m_pGoggles.clear();                                   // +0x270
    m_pAirMask.clear();                                   // +0x274
    m_dwUnk278 = 0;                                       // +0x278
    m_szHotspotName[0] = 0;                               // +0x287
    m_dwHotspotExpiry = 0;                                // +0x294
    m_dwUnk298 = 0;                                       // +0x298
    m_dwUnk2a0 = 0;                                       // +0x2a0
    m_bUnk2a4 = 0;                                        // +0x2a4
    m_dwUnk2a8 = 1;                                       // +0x2a8
    m_bAwake = 1;                                         // +0x2ac
    m_dwUnk2bc = 0;                                       // +0x2bc
    m_dwUnk320 = 0;                                       // +0x320
    m_dwUnk324 = 0;                                       // +0x324
    m_lTopBarHandle = 0x29a;                              // +0x328
    m_hDrunkVoice = -1;                                   // +0x72c
    m_hUnk730 = -1;                                       // +0x730
    m_hBreathingLoop = -1;                                // +0x734
    m_bStatusScreenVisible = 0;                           // +0x738
    m_hUnk73c = -1;                                       // +0x73c
    m_hUnk740 = -1;                                       // +0x740
    m_hUnk744 = -1;                                       // +0x744
    m_hUnk748 = -1;                                       // +0x748
    m_hUnk74c = -1;                                       // +0x74c
    m_bFallAsleepOnIdle = 0;                              // +0x758
    m_bRunSleepOnIdle = 0;                                // +0x75c
    m_bDieOnUpdate = 0;                                   // +0x764
    m_wCauseOfDeath = 0;                                  // +0x768
    m_dLargestHealthLoss = 0;                             // +0x770
    m_hHeartBeatLoop = -1;                                // +0x778
}

// dll:10064110. Pointer members are written as the 11-byte name copy of their target (zero for NULL).
void Self::saveSlot(SlotWriter& w) const {
    w.writeStr("SELF", 4);
    w.writeF64(m_dHydration);
    w.writeF64(m_dHydrationShown);
    w.writeF64(m_dNutrition);
    w.writeF64(m_dNutritionShown);
    w.writeF64(m_dProtein);
    w.writeF64(m_dTemperature);
    w.writeF64(m_dTemperatureShown);
    w.writeF64(m_dIdealTemp);
    w.writeF64(m_dMaxTemp);
    w.writeF64(m_dHealth);
    w.writeF64(m_dHealthShown);
    w.writeF64(m_dEnergy);
    w.writeF64(m_dEnergyShown);
    w.writeF64(m_dStimulant);
    w.writeF64(m_dSedative);
    w.writeF64(m_dSalt);
    w.writeF64(m_dWeightCarried);
    w.writeU16(m_wHydrationState);
    w.writeU16(m_wNutritionState);
    w.writeU16(m_wTempState);
    w.writeU16(m_wHealthState);
    w.writeU16(m_wEnergyState);
    w.writeU16(m_wBreathing);
    w.writeBytes(m_pSite.raw.data(), 11);
    w.writeBytes(m_pPlant.raw.data(), 11);
    w.writeBytes(m_pFeature.raw.data(), 11);
    w.writeBytes(m_pObjHolding.raw.data(), 11);
    w.writeBytes(m_pAirMask.raw.data(), 11);
    w.writeBytes(m_pGoggles.raw.data(), 11);
    w.writeBytes(m_szLocation.data(), 11);
    w.writeBytes(m_pPreviousSite.raw.data(), 11);
    w.writeU32(m_ulCompassHeading);
    w.writeBytes(m_szLastJourneyName.data(), 11);
    w.writeF64(m_dJourneyCost);
    w.writeI32(m_dwJourneySeconds);
    w.writeF64(m_dJourneyEnergy);
    w.writeI32(m_dwJourneyTime);
    w.writeI32(m_bAbleToSleep);
    w.writeI32(m_dwLastFellAsleep);
    w.writeI32(m_dwLastWokeUp);
    w.writeU16(m_wWakeReason);
    w.writeU32(m_dwDreamsAvailable);
    w.writeU32(m_dwDreamsSeen);
    w.writeI32(m_dwNextDream);
    w.writeI32(m_dwLastAte);
    w.writeI32(m_dwLastBarfed);
    w.writeBytes(m_pLastEatenPlant.raw.data(), 11);
    w.writeI32(m_dwLastDrank);
    w.writeU16(m_wLastDrinkType);
    w.writeI32(m_bWearingPoncho);
    w.writeI32(m_bHasSleepingBag);
    w.writeU32(m_dwSymptoms);
    w.writeU32(m_dwSymptomsCured);
    w.writeI32(m_dwSpiderBiteTimer);
    w.writeI32(m_dwHogweedBurnTimer);
    w.writeI32(m_dwFumes);
    w.writeI32(m_dwMalaria);
    w.writeF64(m_dMalariaHealthLoss);
    w.writeI32(m_dwChloroquine);
    w.writeI32(m_dwCinchona);
    w.writeI32(m_bImmuneToMalaria);
    w.writeF64(m_dSynthHealthLoss);
    w.writeI32(m_dwIndigestionTimer);
    w.writeI32(m_dwCactusPoisonTimer);
    w.writeI32(m_dwEphedrineTimer);
    w.writeI32(m_dwWorms);
    w.writeF64(m_dWormsHealthLoss);
    w.writeI32(m_dwDioscorin);
    w.writeF64(m_dDioscorinHealthLoss);
    w.writeI32(m_dwSynthAttack);
    w.writeF64(m_dSynthAttackHealthLoss);
    w.writeI32(m_dwPrussic);
    w.writeF64(m_dPrussicHealthLoss);
    w.writeI32(m_dwChampagne);
    w.writeU8(m_byGlassesDrunk);
    w.writeI32(m_dwMescal);
    w.writeU8(m_bySwigsDrunk);
    w.writeI32(m_dwWhiteAngelTrip);
    w.writeI32(m_dwCoffee);
    w.writeF64(m_dEnergyGainCoffee);
    w.writeU8(m_byDosesCoffee);
    w.writeI32(m_dwGuarana);
    w.writeF64(m_dEnergyGainGuarana);
    w.writeU8(m_byDosesGuarana);
    w.writeI32(m_dwCoca);
    w.writeF64(m_dEnergyGainCoca);
    w.writeU8(m_byDosesCoca);
    w.writeI32(m_dwAdrenaline);
    w.writeF64(m_dEnergyGainAdrenaline);
    w.writeI32(m_dwSleepingPill);
    w.writeU16(m_wSleepingPillsPopped);
    w.writeI32(m_dwTimeLastUpdate);
    w.writeI32(m_bDrunkVisuals);
    w.writeI32(m_bTripAurals);
    w.writeI32(m_bTripVisuals);
    w.writeI32(m_bMalariaVisuals);
    w.writeI32(m_lHeartBeatVol);
    w.writeI32(m_bDrunkenCursor);
    w.writeI32(m_bInsideAngelSequence);
    w.writeI32(m_bInsidePeyoteSequence);
    w.writeBytes(m_szReturnPage.data(), 11);
    // bytes 568..599 are never written
}

static void readName(SlotReader& r, const char* name, std::array<u8, 11>& dst) {
    std::vector<u8> v = r.readStrRaw(name, 11);
    std::memcpy(dst.data(), v.data(), 11);
}

// dll:10064890 (the 600 bytes; the lookups of the names are in resolve())
void Self::loadSlot(SlotReader& r) {
    if (r.readStr("tag", 4) != "SELF") throw Error("CSelf: start marker 'SELF' not found");
    m_dHydration = r.readF64("m_dHydration");
    m_dHydrationShown = r.readF64("m_dHydrationShown");
    m_dNutrition = r.readF64("m_dNutrition");
    m_dNutritionShown = r.readF64("m_dNutritionShown");
    m_dProtein = r.readF64("m_dProtein");
    m_dTemperature = r.readF64("m_dTemperature");
    m_dTemperatureShown = r.readF64("m_dTemperatureShown");
    m_dIdealTemp = r.readF64("m_dIdealTemp");
    m_dMaxTemp = r.readF64("m_dMaxTemp");
    m_dHealth = r.readF64("m_dHealth");
    m_dHealthShown = r.readF64("m_dHealthShown");
    m_dEnergy = r.readF64("m_dEnergy");
    m_dEnergyShown = r.readF64("m_dEnergyShown");
    m_dStimulant = r.readF64("m_dStimulant");
    m_dSedative = r.readF64("m_dSedative");
    m_dSalt = r.readF64("m_dSalt");
    m_dWeightCarried = r.readF64("m_dWeightCarried");
    m_wHydrationState = r.readU16("m_wHydrationState");
    m_wNutritionState = r.readU16("m_wNutritionState");
    m_wTempState = r.readU16("m_wTempState");
    m_wHealthState = r.readU16("m_wHealthState");
    m_wEnergyState = r.readU16("m_wEnergyState");
    m_wBreathing = r.readU16("m_wBreathing");
    readName(r, "m_pSite->name", m_pSite.raw);
    readName(r, "m_pPlant->name", m_pPlant.raw);
    readName(r, "m_pFeature->name", m_pFeature.raw);
    readName(r, "m_pObjHolding->name", m_pObjHolding.raw);
    readName(r, "m_pAirMask->name", m_pAirMask.raw);
    readName(r, "m_pGoggles->name", m_pGoggles.raw);
    readName(r, "m_szLocation", m_szLocation);
    readName(r, "previousSite->name", m_pPreviousSite.raw);
    m_ulCompassHeading = static_cast<u32>(r.readI32("m_ulCompassHeading"));
    readName(r, "m_szLastJourneyName", m_szLastJourneyName);
    m_dJourneyCost = r.readF64("m_dJourneyCost");
    m_dwJourneySeconds = r.readI32("m_dwJourneySeconds");
    m_dJourneyEnergy = r.readF64("m_dJourneyEnergy");
    m_dwJourneyTime = r.readI32("m_dwJourneyTime");
    m_bAbleToSleep = r.readI32("m_bAbleToSleep");
    m_dwLastFellAsleep = r.readI32("m_dwLastFellAsleep");
    m_dwLastWokeUp = r.readI32("m_dwLastWokeUp");
    m_wWakeReason = r.readU16("m_wWakeReason");
    m_dwDreamsAvailable = r.readHex32("m_dwDreamsAvailable");
    m_dwDreamsSeen = r.readHex32("m_dwDreamsSeen");
    m_dwNextDream = r.readI32("m_dwNextDream");
    m_dwLastAte = r.readI32("m_dwLastAte");
    m_dwLastBarfed = r.readI32("m_dwLastBarfed");
    readName(r, "lastEatenPlant->name", m_pLastEatenPlant.raw);
    m_dwLastDrank = r.readI32("m_dwLastDrank");
    m_wLastDrinkType = r.readU16("m_wLastDrinkType");
    m_bWearingPoncho = r.readI32("m_bWearingPoncho");
    m_bHasSleepingBag = r.readI32("m_bHasSleepingBag");
    m_dwSymptoms = r.readHex32("m_dwSymptoms");
    m_dwSymptomsCured = r.readHex32("m_dwSymptomsCured");
    m_dwSpiderBiteTimer = r.readI32("m_dwSpiderBiteTimer");
    m_dwHogweedBurnTimer = r.readI32("m_dwHogweedBurnTimer");
    m_dwFumes = r.readI32("m_dwFumes");
    m_dwMalaria = r.readI32("m_dwMalaria");
    m_dMalariaHealthLoss = r.readF64("m_dMalariaHealthLoss");
    m_dwChloroquine = r.readI32("m_dwChloroquine");
    m_dwCinchona = r.readI32("m_dwCinchona");
    m_bImmuneToMalaria = r.readI32("m_bImmuneToMalaria");
    m_dSynthHealthLoss = r.readF64("m_dSynthHealthLoss");
    m_dwIndigestionTimer = r.readI32("m_dwIndigestionTimer");
    m_dwCactusPoisonTimer = r.readI32("m_dwCactusPoisonTimer");
    m_dwEphedrineTimer = r.readI32("m_dwEphedrineTimer");
    m_dwWorms = r.readI32("m_dwWorms");
    m_dWormsHealthLoss = r.readF64("m_dWormsHealthLoss");
    m_dwDioscorin = r.readI32("m_dwDioscorin");
    m_dDioscorinHealthLoss = r.readF64("m_dDioscorinHealthLoss");
    m_dwSynthAttack = r.readI32("m_dwSynthAttack");
    m_dSynthAttackHealthLoss = r.readF64("m_dSynthAttackHealthLoss");
    m_dwPrussic = r.readI32("m_dwPrussic");
    m_dPrussicHealthLoss = r.readF64("m_dPrussicHealthLoss");
    m_dwChampagne = r.readI32("m_dwChampagne");
    m_byGlassesDrunk = r.readU8("m_byGlassesDrunk");
    m_dwMescal = r.readI32("m_dwMescal");
    m_bySwigsDrunk = r.readU8("m_bySwigsDrunk");
    m_dwWhiteAngelTrip = r.readI32("m_dwWhiteAngelTrip");
    m_dwCoffee = r.readI32("m_dwCoffee");
    m_dEnergyGainCoffee = r.readF64("m_dEnergyGainCoffee");
    m_byDosesCoffee = r.readU8("m_byDosesCoffee");
    m_dwGuarana = r.readI32("m_dwGuarana");
    m_dEnergyGainGuarana = r.readF64("m_dEnergyGainGuarana");
    m_byDosesGuarana = r.readU8("m_byDosesGuarana");
    m_dwCoca = r.readI32("m_dwCoca");
    m_dEnergyGainCoca = r.readF64("m_dEnergyGainCoca");
    m_byDosesCoca = r.readU8("m_byDosesCoca");
    m_dwAdrenaline = r.readI32("m_dwAdrenaline");
    m_dEnergyGainAdrenaline = r.readF64("m_dEnergyGainAdrenaline");
    m_dwSleepingPill = r.readI32("m_dwSleepingPill");
    m_wSleepingPillsPopped = r.readU16("m_wSleepingPillsPopped");
    m_dwTimeLastUpdate = r.readI32("m_dwTimeLastUpdate");
    m_bDrunkVisuals = r.readI32("m_bDrunkVisuals");
    m_bTripAurals = r.readI32("m_bTripAurals");
    m_bTripVisuals = r.readI32("m_bTripVisuals");
    m_bMalariaVisuals = r.readI32("m_bMalariaVisuals");
    m_lHeartBeatVol = r.readI32("m_lHeartBeatVol");
    m_bDrunkenCursor = r.readI32("m_bDrunkenCursor");
    m_bInsideAngelSequence = r.readI32("m_bInsideAngelSequence");
    m_bInsidePeyoteSequence = r.readI32("m_bInsidePeyoteSequence");
    readName(r, "m_szReturnPage", m_szReturnPage);
    r.finishPad();       // 32 bytes never written
    // the pointers read from names stay unresolved until resolve()
    for (NamedRef* p : {&m_pSite, &m_pPlant, &m_pFeature, &m_pObjHolding, &m_pAirMask, &m_pGoggles, &m_pPreviousSite,
                        &m_pLastEatenPlant})
        p->item = nullptr;
}

// dll:10064890 (the lookups): CWorld::FindSite dll:100139b0, FUN_10014400 (plants), CWorld::FindFeature dll:100147c0,
// CWorld::FindObject dll:100145a0, FUN_10013850 (pages). An empty saved name stays NULL without a look-up; a name
// that is not found logs and leaves NULL (the original goes on), except that m_pCel is only set from a site that
// was found.
void Self::resolve(World& world) {
    bind(world, host_, display_);   // keeps an earlier host / display
    if (m_pSite.empty()) {
        m_pSite.item = nullptr;
        m_pCel = nullptr;
    } else {
        CSite* site = world.findSite(m_pSite.name().c_str());
        m_pSite.item = site;
        if (site) {
            m_pCel = site->cel();                // site+8
            m_pBiome = site->biome();            // site+4 (CSelf::OnEnterPage dll:1006e390 sets it on every page change)
        }
    }
    m_pPlant.item = m_pPlant.empty() ? nullptr : world.findPlant(m_pPlant.name().c_str());
    m_pFeature.item = m_pFeature.empty() ? nullptr : world.findFeature(m_pFeature.name().c_str());
    m_pObjHolding.item = m_pObjHolding.empty() ? nullptr : world.findObject(m_pObjHolding.name().c_str());
    m_pAirMask.item = m_pAirMask.empty() ? nullptr : world.findObject(m_pAirMask.name().c_str());
    m_pGoggles.item = m_pGoggles.empty() ? nullptr : world.findObject(m_pGoggles.name().c_str());
    {   // m_szLocation is always looked up as a page
        std::string loc(reinterpret_cast<const char*>(m_szLocation.data()), strnlen(reinterpret_cast<const char*>(m_szLocation.data()), 11));
        m_pPage = world.findPage(loc.c_str());
    }
    m_pPreviousSite.item = world.findSite(m_pPreviousSite.name().c_str());           // looked up even when empty
    m_pLastEatenPlant.item = world.findPlant(m_pLastEatenPlant.name().c_str());      // likewise
}

std::vector<FieldLog> Self::dump() const {
    std::vector<u8> buf;
    SlotWriter w(buf, kSize, "self");
    saveSlot(w);
    std::vector<FieldLog> log;
    SlotReader r(buf.data(), buf.size(), "self", &log);
    Self copy;
    copy.loadSlot(r);
    return log;
}

} // namespace biosys::game
