// Self::dumpAll: every member of Self as (name, value text) for the headless script runner (tests/self_protocol.txt,
// "Dump block"). Generated from the member list of self.h; keep the two in step.
#include "game/self.h"
#include <string>

namespace biosys::game {

namespace {

using Members = std::vector<std::pair<std::string, std::string>>;

void put(Members& o, const std::string& name, double v) { o.emplace_back(name, pyRepr(v)); }
void put(Members& o, const std::string& name, long long v) { o.emplace_back(name, std::to_string(v)); }

std::string itemName(const Item* it) { return it ? it->name() : std::string(); }

// the text before the first NUL
std::string bytesToText(const u8* p, size_t n) {
    size_t len = 0;
    while (len < n && p[len]) len++;
    return std::string(reinterpret_cast<const char*>(p), len);
}

} // namespace

std::vector<std::pair<std::string, std::string>> Self::dumpAll() const {
    Members o;
    put(o, "m_dHydration", m_dHydration);
    put(o, "m_dHydrationDelta", m_dHydrationDelta);
    put(o, "m_dHydrationShown", m_dHydrationShown);
    put(o, "m_dNutrition", m_dNutrition);
    put(o, "m_dNutritionDelta", m_dNutritionDelta);
    put(o, "m_dNutritionShown", m_dNutritionShown);
    put(o, "m_dProtein", m_dProtein);
    put(o, "m_dProteinDelta", m_dProteinDelta);
    put(o, "m_dTemperature", m_dTemperature);
    put(o, "m_dTemperatureDelta", m_dTemperatureDelta);
    put(o, "m_dTemperatureShown", m_dTemperatureShown);
    put(o, "m_dIdealTemp", m_dIdealTemp);
    put(o, "m_dMaxTemp", m_dMaxTemp);
    put(o, "m_dHealth", m_dHealth);
    put(o, "m_dHealthDelta", m_dHealthDelta);
    put(o, "m_dHealthShown", m_dHealthShown);
    put(o, "m_dEnergy", m_dEnergy);
    put(o, "m_dEnergyDelta", m_dEnergyDelta);
    put(o, "m_dEnergyShown", m_dEnergyShown);
    put(o, "m_dStimulant", m_dStimulant);
    put(o, "m_dSedative", m_dSedative);
    put(o, "m_dSalt", m_dSalt);
    put(o, "m_dWeightCarried", m_dWeightCarried);
    put(o, "m_wHydrationState", static_cast<long long>(m_wHydrationState));
    put(o, "m_wNutritionState", static_cast<long long>(m_wNutritionState));
    put(o, "m_wTempState", static_cast<long long>(m_wTempState));
    put(o, "m_wHealthState", static_cast<long long>(m_wHealthState));
    put(o, "m_wEnergyState", static_cast<long long>(m_wEnergyState));
    put(o, "m_wBreathing", static_cast<long long>(m_wBreathing));
    o.emplace_back("m_szLastJourneyName", bytesToText(m_szLastJourneyName.data(), m_szLastJourneyName.size()));
    put(o, "m_dJourneyCost", m_dJourneyCost);
    put(o, "m_dwJourneySeconds", static_cast<long long>(m_dwJourneySeconds));
    put(o, "m_dJourneyEnergy", m_dJourneyEnergy);
    put(o, "m_dwJourneyTime", static_cast<long long>(m_dwJourneyTime));
    o.emplace_back("m_szLocation", bytesToText(m_szLocation.data(), m_szLocation.size()));
    put(o, "m_ulCompassHeading", static_cast<long long>(m_ulCompassHeading));
    put(o, "m_bAbleToSleep", static_cast<long long>(m_bAbleToSleep));
    put(o, "m_dwLastFellAsleep", static_cast<long long>(m_dwLastFellAsleep));
    put(o, "m_dwLastWokeUp", static_cast<long long>(m_dwLastWokeUp));
    put(o, "m_wWakeReason", static_cast<long long>(m_wWakeReason));
    put(o, "m_dwDreamsAvailable", static_cast<long long>(m_dwDreamsAvailable));
    put(o, "m_dwDreamsSeen", static_cast<long long>(m_dwDreamsSeen));
    put(o, "m_dwNextDream", static_cast<long long>(m_dwNextDream));
    put(o, "m_dwLastAte", static_cast<long long>(m_dwLastAte));
    put(o, "m_dwLastBarfed", static_cast<long long>(m_dwLastBarfed));
    o.emplace_back("m_pLastEatenPlant", m_pLastEatenPlant.name());
    put(o, "m_dwLastDrank", static_cast<long long>(m_dwLastDrank));
    put(o, "m_wLastDrinkType", static_cast<long long>(m_wLastDrinkType));
    put(o, "m_bWearingPoncho", static_cast<long long>(m_bWearingPoncho));
    put(o, "m_bHasSleepingBag", static_cast<long long>(m_bHasSleepingBag));
    put(o, "m_dwSymptoms", static_cast<long long>(m_dwSymptoms));
    put(o, "m_dwSymptomsCured", static_cast<long long>(m_dwSymptomsCured));
    put(o, "m_dwSpiderBiteTimer", static_cast<long long>(m_dwSpiderBiteTimer));
    put(o, "m_dwHogweedBurnTimer", static_cast<long long>(m_dwHogweedBurnTimer));
    put(o, "m_dwFumes", static_cast<long long>(m_dwFumes));
    put(o, "m_dwMalaria", static_cast<long long>(m_dwMalaria));
    put(o, "m_dMalariaHealthLoss", m_dMalariaHealthLoss);
    put(o, "m_dwChloroquine", static_cast<long long>(m_dwChloroquine));
    put(o, "m_dwCinchona", static_cast<long long>(m_dwCinchona));
    put(o, "m_bImmuneToMalaria", static_cast<long long>(m_bImmuneToMalaria));
    put(o, "m_dSynthHealthLoss", m_dSynthHealthLoss);
    put(o, "m_dwIndigestionTimer", static_cast<long long>(m_dwIndigestionTimer));
    put(o, "m_dwCactusPoisonTimer", static_cast<long long>(m_dwCactusPoisonTimer));
    put(o, "m_dwEphedrineTimer", static_cast<long long>(m_dwEphedrineTimer));
    put(o, "m_dwWorms", static_cast<long long>(m_dwWorms));
    put(o, "m_dWormsHealthLoss", m_dWormsHealthLoss);
    put(o, "m_dwDioscorin", static_cast<long long>(m_dwDioscorin));
    put(o, "m_dDioscorinHealthLoss", m_dDioscorinHealthLoss);
    put(o, "m_dwSynthAttack", static_cast<long long>(m_dwSynthAttack));
    put(o, "m_dSynthAttackHealthLoss", m_dSynthAttackHealthLoss);
    put(o, "m_dwPrussic", static_cast<long long>(m_dwPrussic));
    put(o, "m_dPrussicHealthLoss", m_dPrussicHealthLoss);
    put(o, "m_dwChampagne", static_cast<long long>(m_dwChampagne));
    put(o, "m_byGlassesDrunk", static_cast<long long>(m_byGlassesDrunk));
    put(o, "m_dwMescal", static_cast<long long>(m_dwMescal));
    put(o, "m_bySwigsDrunk", static_cast<long long>(m_bySwigsDrunk));
    put(o, "m_dwWhiteAngelTrip", static_cast<long long>(m_dwWhiteAngelTrip));
    put(o, "m_bInsideAngelSequence", static_cast<long long>(m_bInsideAngelSequence));
    put(o, "m_dwPeyoteTripTimer", static_cast<long long>(m_dwPeyoteTripTimer));
    put(o, "m_bInsidePeyoteSequence", static_cast<long long>(m_bInsidePeyoteSequence));
    put(o, "m_dwCoffee", static_cast<long long>(m_dwCoffee));
    put(o, "m_dEnergyGainCoffee", m_dEnergyGainCoffee);
    put(o, "m_byDosesCoffee", static_cast<long long>(m_byDosesCoffee));
    put(o, "m_dwGuarana", static_cast<long long>(m_dwGuarana));
    put(o, "m_dEnergyGainGuarana", m_dEnergyGainGuarana);
    put(o, "m_byDosesGuarana", static_cast<long long>(m_byDosesGuarana));
    put(o, "m_dwCoca", static_cast<long long>(m_dwCoca));
    put(o, "m_dEnergyGainCoca", m_dEnergyGainCoca);
    put(o, "m_byDosesCoca", static_cast<long long>(m_byDosesCoca));
    put(o, "m_dwAdrenaline", static_cast<long long>(m_dwAdrenaline));
    put(o, "m_dEnergyGainAdrenaline", m_dEnergyGainAdrenaline);
    put(o, "m_dwSleepingPill", static_cast<long long>(m_dwSleepingPill));
    put(o, "m_wSleepingPillsPopped", static_cast<long long>(m_wSleepingPillsPopped));
    put(o, "m_dwTimeLastUpdate", static_cast<long long>(m_dwTimeLastUpdate));
    put(o, "m_dwUpdateDt", static_cast<long long>(m_dwUpdateDt));
    put(o, "m_bDrunkVisuals", static_cast<long long>(m_bDrunkVisuals));
    put(o, "m_bTripAurals", static_cast<long long>(m_bTripAurals));
    put(o, "m_bTripVisuals", static_cast<long long>(m_bTripVisuals));
    put(o, "m_bMalariaVisuals", static_cast<long long>(m_bMalariaVisuals));
    put(o, "m_lHeartBeatVol", static_cast<long long>(m_lHeartBeatVol));
    put(o, "m_bDrunkenCursor", static_cast<long long>(m_bDrunkenCursor));
    o.emplace_back("m_pPage", itemName(m_pPage));
    o.emplace_back("m_pSite", m_pSite.name());
    o.emplace_back("m_pPreviousSite", m_pPreviousSite.name());
    o.emplace_back("m_pPath", itemName(m_pPath));
    o.emplace_back("m_pBiome", m_pBiome ? m_pBiome->name : "");
    o.emplace_back("m_pCel", itemName(m_pCel));
    o.emplace_back("m_pPlant", m_pPlant.name());
    o.emplace_back("m_pFeature", m_pFeature.name());
    o.emplace_back("m_pObjHolding", m_pObjHolding.name());
    o.emplace_back("m_pObjTarget", itemName(m_pObjTarget));
    o.emplace_back("m_pGoggles", m_pGoggles.name());
    o.emplace_back("m_pAirMask", m_pAirMask.name());
    put(o, "m_dwUnk278", static_cast<long long>(m_dwUnk278));
    o.emplace_back("m_szHotspotName", bytesToText(m_szHotspotName.data(), m_szHotspotName.size()));
    put(o, "m_dwHotspotExpiry", static_cast<long long>(m_dwHotspotExpiry));
    put(o, "m_dwUnk298", static_cast<long long>(m_dwUnk298));
    put(o, "m_dwUnk2a0", static_cast<long long>(m_dwUnk2a0));
    put(o, "m_bUnk2a4", static_cast<long long>(m_bUnk2a4));
    put(o, "m_dwUnk2a8", static_cast<long long>(m_dwUnk2a8));
    put(o, "m_bAwake", static_cast<long long>(m_bAwake));
    o.emplace_back("m_szReturnPage", bytesToText(m_szReturnPage.data(), m_szReturnPage.size()));
    put(o, "m_dwUnk2bc", static_cast<long long>(m_dwUnk2bc));
    for (size_t i = 0; i < m_aSoundCue.size(); i++) put(o, "m_aSoundCue[" + std::to_string(i) + "]", static_cast<long long>(m_aSoundCue[i]));
    put(o, "m_dwUnk320", static_cast<long long>(m_dwUnk320));
    put(o, "m_dwUnk324", static_cast<long long>(m_dwUnk324));
    put(o, "m_lTopBarHandle", static_cast<long long>(m_lTopBarHandle));
    put(o, "m_hDrunkVoice", static_cast<long long>(m_hDrunkVoice));
    put(o, "m_hUnk730", static_cast<long long>(m_hUnk730));
    put(o, "m_hBreathingLoop", static_cast<long long>(m_hBreathingLoop));
    put(o, "m_bStatusScreenVisible", static_cast<long long>(m_bStatusScreenVisible));
    put(o, "m_hUnk73c", static_cast<long long>(m_hUnk73c));
    put(o, "m_hUnk740", static_cast<long long>(m_hUnk740));
    put(o, "m_hUnk744", static_cast<long long>(m_hUnk744));
    put(o, "m_hUnk748", static_cast<long long>(m_hUnk748));
    put(o, "m_hUnk74c", static_cast<long long>(m_hUnk74c));
    put(o, "m_bFallAsleepOnIdle", static_cast<long long>(m_bFallAsleepOnIdle));
    put(o, "m_bRunSleepOnIdle", static_cast<long long>(m_bRunSleepOnIdle));
    put(o, "m_bDieOnUpdate", static_cast<long long>(m_bDieOnUpdate));
    put(o, "m_wCauseOfDeath", static_cast<long long>(m_wCauseOfDeath));
    put(o, "m_dLargestHealthLoss", m_dLargestHealthLoss);
    put(o, "m_hHeartBeatLoop", static_cast<long long>(m_hHeartBeatLoop));
    return o;
}

} // namespace biosys::game
