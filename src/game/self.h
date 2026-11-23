// CSelf: the player ("Russell"), a global object of the original (DAT_100c1d38). Constructor dll:10063ca0,
// CSelf::Save dll:10064110 / Load dll:10064890 (600 bytes), CSelf::Dump dll:100651d0 (names most members).
// Members are listed in object order with the object offset; the save format is in analysis/notes/saveblob.md 4.4.
// SaveGame owns one (`SaveGame::self`) and calls resolve() at the end of load: the pointer members are saved as names
// and re-resolved by the original's Load.
#pragma once
#include "game/self_env.h"
#include "game/world.h"
#include <array>

namespace biosys::game {

// A pointer member that is saved as the 11-byte name copy of its target (zero bytes = NULL).
struct NamedRef {
    std::array<u8, 11> raw{};      // the name bytes exactly as saved; the target's name buffer is copied verbatim
    Item* item = nullptr;          // resolved by Self::resolve (CWorld::FindSite / FindFeature / FindObject / plant lookup)
    std::string name() const;      // text before the first NUL
    bool empty() const { return raw[0] == 0; }
    void set(Item* target);        // pointer assigned by the game: the name copy is rebuilt from the target
    void clear();
};

class Self final : public Record {
public:
    static constexpr size_t kSize = 600;

    Self();                        // CSelf::CSelf dll:10063ca0
    void saveSlot(SlotWriter& w) const override;   // CSelf::Save dll:10064110
    void loadSlot(SlotReader& r) override;         // CSelf::Load dll:10064890, the part that reads the 600 bytes
    // The rest of CSelf::Load: resolves the saved names to the objects of the world (sites, plants, features,
    // objects, the page of m_szLocation) and sets m_pCel from the site. Empty names stay NULL.
    void resolve(World& world);
    // The member lines of the oracle dump ("name = value") of the current state.
    std::vector<FieldLog> dump() const;

    // --- members in object order -----------------------------------------------------------------------------
    // Each "main stat" is value, *delta since the status screen was opened (ResetDeltas dll:10066820), *value when
    // the status screen was opened.
    double m_dHydration = 0;                // +0x000 (40)
    double m_dHydrationDelta = 0;           // +0x008
    double m_dHydrationShown = 0;           // +0x010
    double m_dNutrition = 0;                // +0x018 (75)
    double m_dNutritionDelta = 0;           // +0x020
    double m_dNutritionShown = 0;           // +0x028
    double m_dProtein = 0;                  // +0x030 (100) really "diet quality"
    double m_dProteinDelta = 0;             // +0x038
    double m_dTemperature = 0;              // +0x040 body temperature (50 = normal)
    double m_dTemperatureDelta = 0;         // +0x048
    double m_dTemperatureShown = 0;         // +0x050
    double m_dIdealTemp = 0;                // +0x058 (50; 73 during malaria fever)
    double m_dMaxTemp = 0;                  // +0x060 (73)
    double m_dHealth = 0;                   // +0x068 (90)
    double m_dHealthDelta = 0;              // +0x070
    double m_dHealthShown = 0;              // +0x078
    double m_dEnergy = 0;                   // +0x080 (63)
    double m_dEnergyDelta = 0;              // +0x088
    double m_dEnergyShown = 0;              // +0x090
    double m_dStimulant = 0;                // +0x098 blood stimulant level (ECG/BP test)
    double m_dSedative = 0;                 // +0x0a0 barbiturate level
    double m_dSalt = 0;                     // +0x0a8 (corned beef)
    double m_dWeightCarried = 0;            // +0x0b0
    u16 m_wHydrationState = 0;              // +0x0b8 (3)
    u16 m_wNutritionState = 0;              // +0x0ba (3)
    u16 m_wTempState = 0;                   // +0x0bc (3)
    u16 m_wHealthState = 0;                 // +0x0be (3)
    u16 m_wEnergyState = 0;                 // +0x0c0 (3)
    u16 m_wBreathing = 0;                   // +0x0c2 bit flags (player.md 2.7)
    std::array<u8, 11> m_szLastJourneyName{};   // +0x0c4 name of the last journey (path)
    double m_dJourneyCost = 0;              // +0x0d0
    i32 m_dwJourneySeconds = 0;             // +0x0d8
    double m_dJourneyEnergy = 0;            // +0x0e0
    i32 m_dwJourneyTime = 0;                // +0x0e8 game time of the journey
    std::array<u8, 11> m_szLocation{};      // +0x0ec current page name, upper case ("R4" at construction)
    u32 m_ulCompassHeading = 0;             // +0x0f8 (1)
    i32 m_bAbleToSleep = 0;                 // +0x0fc
    i32 m_dwLastFellAsleep = 0;             // +0x100
    i32 m_dwLastWokeUp = 0;                 // +0x104 (28800)
    u16 m_wWakeReason = 0;                  // +0x110 (1) 1..8: nothing, alarm clock, cough, cold, heat, sensor, rain, rest
    u32 m_dwDreamsAvailable = 0;            // +0x114 bit mask
    u32 m_dwDreamsSeen = 0;                 // +0x118 bit mask; bit 31 = help texts on (0x80000000)
    i32 m_dwNextDream = 0;                  // +0x11c
    i32 m_dwLastAte = 0;                    // +0x120 (1)
    i32 m_dwLastBarfed = 0;                 // +0x124
    NamedRef m_pLastEatenPlant;             // +0x128 plant of the last thing eaten (CPlant, name at plant+0x50)
    i32 m_dwLastDrank = 0;                  // +0x12c
    u16 m_wLastDrinkType = 0;               // +0x130 (1)
    i32 m_bWearingPoncho = 0;               // +0x134
    i32 m_bHasSleepingBag = 0;              // +0x138
    u32 m_dwSymptoms = 0;                   // +0x13c bit mask
    u32 m_dwSymptomsCured = 0;              // +0x140 bit mask
    i32 m_dwSpiderBiteTimer = 0;            // +0x144
    i32 m_dwHogweedBurnTimer = 0;           // +0x148
    i32 m_dwFumes = 0;                      // +0x14c
    i32 m_dwMalaria = 0;                    // +0x150
    double m_dMalariaHealthLoss = 0;        // +0x158
    i32 m_dwChloroquine = 0;                // +0x160
    i32 m_dwCinchona = 0;                   // +0x164
    i32 m_bImmuneToMalaria = 0;             // +0x168
    double m_dSynthHealthLoss = 0;          // +0x170
    i32 m_dwIndigestionTimer = 0;           // +0x178 (consumable 0xfa)
    i32 m_dwCactusPoisonTimer = 0;          // +0x17c (consumable 0x10e, name a guess)
    i32 m_dwEphedrineTimer = 0;             // +0x180 (consumable 0x104)
    i32 m_dwWorms = 0;                      // +0x184
    double m_dWormsHealthLoss = 0;          // +0x188
    i32 m_dwDioscorin = 0;                  // +0x190
    double m_dDioscorinHealthLoss = 0;      // +0x198
    i32 m_dwSynthAttack = 0;                // +0x1a0
    double m_dSynthAttackHealthLoss = 0;    // +0x1a8
    i32 m_dwPrussic = 0;                    // +0x1b0
    double m_dPrussicHealthLoss = 0;        // +0x1b8
    i32 m_dwChampagne = 0;                  // +0x1c0
    u8 m_byGlassesDrunk = 0;                // +0x1c4
    i32 m_dwMescal = 0;                     // +0x1c8
    u8 m_bySwigsDrunk = 0;                  // +0x1cc
    i32 m_dwWhiteAngelTrip = 0;             // +0x1d0
    i32 m_bInsideAngelSequence = 0;         // +0x1d4
    i32 m_dwPeyoteTripTimer = 0;            // +0x1d8 (not saved)
    i32 m_bInsidePeyoteSequence = 0;        // +0x1dc
    i32 m_dwCoffee = 0;                     // +0x1e0
    double m_dEnergyGainCoffee = 0;         // +0x1e8
    u8 m_byDosesCoffee = 0;                 // +0x1f0
    i32 m_dwGuarana = 0;                    // +0x1f4
    double m_dEnergyGainGuarana = 0;        // +0x1f8
    u8 m_byDosesGuarana = 0;                // +0x200
    i32 m_dwCoca = 0;                       // +0x204
    double m_dEnergyGainCoca = 0;           // +0x208
    u8 m_byDosesCoca = 0;                   // +0x210
    i32 m_dwAdrenaline = 0;                 // +0x214
    double m_dEnergyGainAdrenaline = 0;     // +0x218
    i32 m_dwSleepingPill = 0;               // +0x220
    u16 m_wSleepingPillsPopped = 0;         // +0x224
    i32 m_dwTimeLastUpdate = 0;             // +0x228 (28800)
    i32 m_dwUpdateDt = 0;                   // +0x22c seconds of this update (not saved)
    i32 m_bDrunkVisuals = 0;                // +0x230
    i32 m_bTripAurals = 0;                  // +0x234
    i32 m_bTripVisuals = 0;                 // +0x238
    i32 m_bMalariaVisuals = 0;              // +0x23c
    i32 m_lHeartBeatVol = 0;                // +0x240 DirectSound attenuation (-10000 silent, 0 full)
    i32 m_bDrunkenCursor = 0;               // +0x244
    Item* m_pPage = nullptr;                // +0x248 current page (not saved: found again from m_szLocation)
    NamedRef m_pSite;                       // +0x24c current site (CSite, name at site+0xd4)
    NamedRef m_pPreviousSite;               // +0x250
    Item* m_pPath = nullptr;                // +0x254 CPath while travelling (not saved, not an item of any table)
    const Biome* m_pBiome = nullptr;        // +0x258 site[1]
    Item* m_pCel = nullptr;                 // +0x25c CBiomeCel of the site (site+8)
    NamedRef m_pPlant;                      // +0x260 plant being looked at
    NamedRef m_pFeature;                    // +0x264
    NamedRef m_pObjHolding;                 // +0x268 object in the hand
    Item* m_pObjTarget = nullptr;           // +0x26c (guess) object picked by the cursor (dll:100c1fa4)
    NamedRef m_pGoggles;                    // +0x270 worn
    NamedRef m_pAirMask;                    // +0x274 worn
    i32 m_dwUnk278 = 0;                     // +0x278
    std::array<u8, 13> m_szHotspotName{};   // +0x287 upper-cased name of the hotspot under the cursor (guess); the
                                            //        constructor clears its first byte; 0x27c..0x286 are not used
    i32 m_dwHotspotExpiry = 0;              // +0x294 GetTickCount() + 500 (guess)
    i32 m_dwUnk298 = 0;                     // +0x298
    i32 m_dwUnk29c = 0;                     // +0x29c second message line id (ShowMessage; appended to the text of +0x298); not saved
    i32 m_dwUnk2a0 = 0;                     // +0x2a0
    i32 m_bUnk2a4 = 0;                      // +0x2a4
    i32 m_dwUnk2a8 = 0;                     // +0x2a8 (1)
    i32 m_bAwake = 0;                       // +0x2ac (1) 0 while asleep
    std::array<u8, 11> m_szReturnPage{};    // +0x2b0 page to return to after sleep/trip
    i32 m_dwUnk2bc = 0;                     // +0x2bc
    std::array<i32, 24> m_aSoundCue{};      // +0x2c0..0x31c per-update sound cue flags (player.md 2.8)
    i32 m_dwUnk320 = 0;                     // +0x320
    i32 m_dwUnk324 = 0;                     // +0x324
    i32 m_lTopBarHandle = 0;                // +0x328 (0x29a) value set by the "topbar" surface callback (guess)
    i32 m_hDrunkVoice = 0;                  // +0x72c (-1) sound handle
    i32 m_hUnk730 = 0;                      // +0x730 (-1)
    i32 m_hBreathingLoop = 0;               // +0x734 (-1) sound handle
    i32 m_bStatusScreenVisible = 0;         // +0x738
    i32 m_hUnk73c = 0;                      // +0x73c (-1)
    i32 m_hUnk740 = 0;                      // +0x740 (-1)
    i32 m_hUnk744 = 0;                      // +0x744 (-1)
    i32 m_hUnk748 = 0;                      // +0x748 (-1)
    i32 m_hUnk74c = 0;                      // +0x74c (-1)
    i32 m_bFallAsleepOnIdle = 0;            // +0x758 after a synth attack
    i32 m_bRunSleepOnIdle = 0;              // +0x75c set when the sleep page is shown
    i32 m_bDieOnUpdate = 0;                 // +0x764
    u16 m_wCauseOfDeath = 0;                // +0x768 cause of death / of the largest health loss of this update
    double m_dLargestHealthLoss = 0;        // +0x770 (+0x774 high dword)
    i32 m_hHeartBeatLoop = 0;               // +0x778 (-1) sound handle

    // =====================================================================================================================
    // Behaviour. Every function carries `// dll:ADDR` in its .cpp. Names are the original's. A function is declared in
    // the block of the file that defines it; before adding one, grep for its address in src/game/self*.{h,cpp}.
    // =====================================================================================================================

    // The shell: set once after the world is built (SaveGame::load calls resolve(), which also binds the world).
    void bind(World& world, PlayerHost* host, PlayerDisplay* display);
    World& world() const { return *world_; }
    PlayerHost& host() const { return *host_; }
    PlayerDisplay& display() const { return *display_; }
    i32 now() const;                       // DAT_100c1c1c: the game clock (CTimeline+0x04)
    PlayerGlobals g;                       // DLL globals of the player and the objects

    // ---- self_update.cpp: the metabolism ------------------------------------------------------------------------------
    void SetHydration(double v);           // dll:10066730
    void SetEnergy(double v);              // dll:10066760
    void SetHealth(double v);              // dll:10066790
    void SetNutrition(double v);           // dll:100667c0
    void SetTemperature(double v);         // dll:100667f0
    void ResetDeltas();                    // dll:10066820
    void AddHealth(double v);              // dll:10066850
    void AddNutrition(double v);           // dll:10066870
    void AddProtein(double v);             // dll:10066890
    void AddHydration(double v);           // dll:100668b0
    void AddEnergy(double v);              // dll:100668d0
    void AddTemperature(double v);         // dll:10066910
    void LoseHealth(double v, u16 cause, int dieNow);   // dll:10066950
    void LoseNutrition(double v);          // dll:100669d0
    void LoseProtein(double v);            // dll:100669f0
    void LoseHydration(double v);          // dll:10066a10
    void LoseEnergy(double v);             // dll:10066a30
    void LoseTemperature(double v);        // dll:10066a60
    void Update(int full);                 // dll:10066aa0 CSelf::Update(param_1): param_1 = 0 from CTimeline::Advance
    void UpdateAilments();                 // dll:10067c80
    void CatchMalaria();                   // dll:1006df90
    double GetMaxHealth() const;           // dll:1006f1c0
    void Die(u16 cause);                   // dll:10068f10
    static int GetSymptomStringID(u32 bit);   // dll:1006a8e0
    void UpdateLoopSounds();               // dll:10068960
    void PlaySymptomSounds();              // dll:100689a0
    void UpdateHeartBeatSound(int p);      // dll:1006da60
    void UpdateBreathingSound(int p);      // dll:1006dae0
    // Port only (Tuning::start*): sets hydration / nutrition / energy (and their shown copies) of the player of a NEW game; a
    // negative value keeps what is there. The caller (BiosGame, the new-game menu command) never calls it for a loaded game.
    void applyStartValues(double hydration, double nutrition, double energy);
    void ResetPlayer();                    // FUN_10065150 dll:10065150: closes the four sound handles, resets the site's sound buffers
    // Every member of Self in header order as (name, value text) in the dump format of tests/self_protocol.txt
    // (self_dump.cpp)
    std::vector<std::pair<std::string, std::string>> dumpAll() const;

    // ---- self_consume.cpp: eating, drinking, drugs, weight, the debug dump -------------------------------------------
    int Eat(double nutrition, double protein, double hydration, u16 consumableCode);   // dll:10068d70 (all other doubles are 2 dwords)
    int Drink(i16 code);                   // dll:1006ccd0
    int Consume(u16 code);                 // dll:1006cf40 (returns 1 when handled/used, 0 when refused or unknown)
    void Vomit();                          // dll:1006c9c0
    void Retch();                          // dll:1006ca20
    int AddWeight(double w);               // dll:1006de50
    int Drop(double w);                    // dll:1006dea0
    int CanPickUp(double w);               // dll:1006def0
    void WhiteAngelTrip();                 // dll:1006dc20 (jumps to page EAngel2 when m_dwWhiteAngelTrip is in 2..0x437)
    void PeyoteTrip();                     // dll:1006dd30 (jumps to page EPeyote1 when m_dwPeyoteTripTimer is in 2..0x2cf)
    void OnSynthAttack();                  // dll:1006efd0
    void Dump();                           // dll:100651d0 debug dump (cheat DUMPSELF): every line through host().writeLog
    // FUN_10060820(&CEngineFX, stem, n): plays stem + (rand() % n + 1) + ".wav". The draw is made here (the shared
    // generator), so the order of the original's draws holds whatever the host does with the sound.
    void PlayRandomSound(const char* stem, unsigned count);
    // FUN_1006cb90 dll:1006cb90: FUN_10060620(0, 0xff, 0, p1, p2, 0x36) = Eng_FadeToColour(0, 0xff, 0, 1, p1, 0x36, p2)
    void GreenFlash(int p1, int p2);

    // ---- self_sleep.cpp: sleep, wake, dream, idle, page parts ---------------------------------------------------------
    int CanSleep();                        // dll:10067540
    int Sleep(int a, int b, int c);        // dll:10067600
    void WakeUp();                         // dll:10067910
    int Rest();                            // dll:10067bb0
    int OnIdle();                          // dll:10065e80
    bool IsAtPage(const char* page) const; // dll:1006ef40
    bool Dream();                          // dll:1006dfc0 (true when the dream movie started)
    bool IsCurrentSite(const Item* site) const { return m_pSite.item == site; }   // dll:1006ef60
    void UpdatePanoramaOffset();           // dll:1006efa0 FUN_1006efa0
    void OnFireBurn();                     // dll:1006f090 FUN_1006f090 (burnt by fire: key S, pain sound, burnt animation)
    void RefreshForPage();                 // dll:1006f100 FUN_1006f100 (per-page player state refresh: blank the top bar)
    void HotspotHelpText(const char* hotspot, int* stringId, int* unused);   // dll:1006f220 FUN_1006f220
    int Journey(u8 typeLetter);            // dll:1006e220 FUN_1006e220 (cost of one journey step; returns 1)
    void OnExitPage();                     // dll:1006ec20 FUN_1006ec20 (Event_OnExitPage)
    void OnEnterPageDone(const char* page);   // dll:1006eca0 FUN_1006eca0 (Event_OnEnterPage: the page jump is complete)
    void ReturnToSavedPage(const char* page); // dll:1006ede0 FUN_1006ede0

    // ---- self_msg.cpp: messages and status screens -------------------------------------------------------------------
    void ShowMessage(int p1, int stringId, int p3, int p4);   // dll:1006bc40
    void ShowMessageDeferred(int stringId);                   // FUN_1006f330 dll:1006f330
    bool HelpTextsOn() const { return (m_dwDreamsSeen >> 31) != 0; }   // FUN_1006f320 dll:1006f320
    void ShowMessageLine(int p1, int p2);                     // FUN_1006c400 dll:1006c400 (message line only: p1 = line, p2 = second line)
    void ClearMessageLine();                                  // FUN_1006c700 dll:1006c700
    void CloseStatusScreen();                                 // FUN_1006c7b0 dll:1006c7b0 (releases the palette locks, back to the page)
    void ShowStatus(int open);                                // dll:10068f90 (0 = close the player's status screen)
    void ShowEnvironmentStatus(int open);                     // dll:1006aa80 (0 = close the site / plant status screen)
    // The symptom sound cues (called by PlaySymptomSounds dll:100689a0; FUN_10060820 = PlayRandomSound, FUN_10060620 =
    // host().fadeToColour). dll:1006cc20 and 1006cc70 are empty functions and are not ported.
    void PlayDyingMoan();                  // FUN_1006c860
    void PlayMoan();                       // FUN_1006c880
    void PlayYawn();                       // FUN_1006c8a0
    void PlayGibber();                     // FUN_1006c8c0
    void PlayYawnFade();                   // FUN_1006c8e0 (yawn and a fade to black, stronger when energy is low)
    void PlayShiver();                     // FUN_1006c960 (shiver sound and a page jiggle)
    void PlayPhew();                       // FUN_1006ca50
    void PlayCough(i16 severity);          // FUN_1006ca70 (0x14 medium, 0x1e heavy, else light)
    void FadeCold();                       // FUN_1006cac0 (blue flash by body temperature)
    void FadeHot();                        // FUN_1006cb40 (red flash by body temperature)
    void PlayMoanFade(int p1, int p2);     // FUN_1006cbc0 (yellow flash and a moan)
    void PlayWheeze();                     // FUN_1006cc00
    void PlayVoices();                     // FUN_1006cc30
    void PlayPoem();                       // FUN_1006cc50
    void PlayThirst();                     // FUN_1006cc80 (thirst sound and, with help texts on, message 0x179 every 5 s)

private:
    World* world_ = nullptr;
    PlayerHost* host_ = nullptr;
    PlayerDisplay* display_ = nullptr;
};

} // namespace biosys::game
