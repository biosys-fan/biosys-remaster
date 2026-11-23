// The remaster layer on top of the faithful translation (SETTINGS.md; design study analysis/notes/remaster_design.md):
//   Fixes  (fixes.h)   one bool per bug of the original                         -> "realism" presets set them
//   Model  (here)      one bool per correction of the simulation model          -> "realism" presets set them
//   Tuning (here)      plain numbers: multipliers and start values              -> "difficulty" presets set them
//   Settings           the three together: the one front door (`Settings::parse`), held by the world (GameState::settings)
// The default (realism faithful, difficulty original) is the unchanged translation: every `Model` flag is false and every
// `Tuning` value is neutral (multiplier 1.0, start values "keep the saved value"), and every site is written so that the neutral
// value gives the same double as the original expression (the multiplier is the last factor of the product).
#pragma once
#include "game/fixes.h"
#include <string>
#include <vector>

namespace biosys::game {

// Tier of a model flag = the smallest realism level that contains it.
enum class ModelTier { Off = 0, Corrected = 1, ClosedLoop = 2 };

// X(name, tier, study item, one-line description)
// vapourFromSoil / transpirationHumidifies: the closed-loop vapour model v2, see SETTINGS.md
#define BIOSYS_MODEL(X)                                                                                              \
    X(proteinPerSecond, Corrected, "A1", "the protein loss is multiplied by dt like every other drain of CSelf::Update") \
    X(savannahSoilAlarm, Corrected, "A2", "savannah soil-water low alarm 4 -> 7 (the plants there need 6..7)")            \
    X(waveMachineSetpoint, Corrected, "A3", "wave-machine oxygen set-point 3.4 -> 4.3 (above the 3.5 damage limit)")     \
    X(deadPlantsRespire, ClosedLoop, "A4", "plant respiration scales with health: 0.25 maxPS (0.1 + 0.9 health)")        \
    X(vapourFromSoil, ClosedLoop, "A6", "deficit-limited soil evaporation into the air; the condensate tank gets what condensation removes (water conserved)") \
    X(transpirationHumidifies, ClosedLoop, "A7", "plant transpiration raises the humidity of the cel and takes its water from the soil") \
    X(houseEnvelopeSlow, ClosedLoop, "A11", "the house cel's envelope time constant is 86400 s (glasshouses 12000 s)")     \
    X(floodedPlantFormula, ClosedLoop, "A12", "flooded plants: water availability (47.5 - sw) / (47.5 - swMax), the mirror of the dry rule")

struct Model {
#define BIOSYS_MODEL_MEMBER(name, tier, item, desc) bool name = false;
    BIOSYS_MODEL(BIOSYS_MODEL_MEMBER)
#undef BIOSYS_MODEL_MEMBER

    struct Info {
        const char* name;
        ModelTier tier;
        const char* item;
        const char* description;
        bool Model::*member;
    };
    static const std::vector<Info>& all();

    // Calibration constants of the vapour model (vapourFromSoil, transpirationHumidifies; SETTINGS.md). Not flags: no preset
    // or parser touches them; the measurement tool overrides them (--calib) to fit them.
    double evapRate = 30.0;          // kE: %RH per hour of the evaporation of a full soil tank into dry air
    double evapExponent = 1.5;       // n: the soil wetness (fill fraction of the soil tank) enters as wet^n
    double transpRate = 6.0e-4;      // %RH per second per unit of transpiration rate (the original: 2.3148e-9)

    static Model faithful() { return Model(); }
    static Model corrected();
    static Model closedloop();
    // "faithful" | "corrected" | "closedloop" | flag names, comma separated, applied left to right ("-name" removes).
    // Throws biosys::Error on an unknown word.
    static Model parse(const std::string& text);
    bool any() const;
    std::string toString() const;      // comma list of the flags that are on ("faithful" when none)
};

// X(name, original value, one-line description). Multipliers: 1 = original. Start values: < 0 = keep what the saved game says.
#define BIOSYS_TUNING(X)                                                                                              \
    X(startHydration, -1, "new game: hydration at the start (original 40; < 0 keeps the saved value)")                \
    X(startNutrition, -1, "new game: nutrition at the start (original 75)")                                           \
    X(startEnergy, -1, "new game: energy at the start (original 63)")                                                 \
    X(hydrationDrain, 1, "x the hydration drain (1/864 per s)")                                                       \
    X(nutritionDrain, 1, "x the nutrition drain (1/576 per s)")                                                       \
    X(heatWaterTerm, 1, "x the extra hydration drain above 20 C (1/8640 per s per degree)")                           \
    X(starveDamage, 1, "x the health loss from nutrition < 20 (1/4320 per s)")                                        \
    X(thirstDamage, 1, "x the health loss from hydration < 20 (1/1728 per s)")                                        \
    X(heatDamage, 1, "x the health loss from body temperature > 73")                                                  \
    X(coldDamage, 1, "x the health loss from body temperature < 27")                                                  \
    X(oxygenDamage, 1, "x the health loss from air with < 14 % oxygen")                                               \
    X(awakeLimitHours, 24, "hours awake before the energy drain starts (1/240 per s)")                                \
    X(healing, 1, "x the natural healing of the awake player (1/43200 per s)")                                        \
    X(sleepHealing, 1, "x the health gained per sleep step (0.05 per 90 s)")                                          \
    X(plantDamage, 1, "x every damage rate of the plants (water, temperature, pollution)")                            \
    X(plantRecovery, 1, "x the recovery rates of the plants (short term 1/3600 per s, long term 3.5e-6 per s)")   \
    X(powerGeneration, 1, "x the generation of the solar panel and the windmill")                                     \
    X(siltClog, 1, "x the clogging rate of the drains (0.1 per day)")                                                 \
    X(heliDelaySeconds, 604800, "seconds from the revival of the dead biome to the helicopter (7 days)")              \
    X(huntChancePercent, 2, "percent chance of a helicopter hunt per roll (every 500 ms real time)")                  \
    X(huntCooldownSeconds, 300, "real seconds between two hunts")                                                     \
    X(malaria, 1, "x the malaria chance per update (dt/72 percent)")                                                  \
    X(sensorNoise, 1, "x the random error of the biome sensors")                                                      \
    X(alarmMargin, 0, "biome sensor alarms trigger this fraction of the band (high - low) before the limit (0 = original)")

struct Tuning {
#define BIOSYS_TUNING_MEMBER(name, value, desc) double name = value;
    BIOSYS_TUNING(BIOSYS_TUNING_MEMBER)
#undef BIOSYS_TUNING_MEMBER

    struct Info {
        const char* name;
        double original;
        const char* description;
        double Tuning::*member;
    };
    static const std::vector<Info>& all();

    static Tuning original() { return Tuning(); }
    static Tuning guided();
    static Tuning survivor();
    // "guided" | "original" | "survivor"; throws biosys::Error otherwise
    static Tuning parse(const std::string& preset);
    // knob by name (tools and tests): throws biosys::Error for an unknown name
    void set(const std::string& name, double value);
    double get(const std::string& name) const;
    bool isOriginal() const;
    std::string toString() const;      // "original" or the knobs that differ: "name=value,..."
};

struct Settings {
    Fixes fixes;
    Model model;
    Tuning tuning;

    // realism: "faithful" (default) | "corrected" | "closedloop"; difficulty: "guided" | "original" (default) | "survivor";
    // fixesOverride: if not empty (Fixes::parse text, e.g. "restored,-plantColdDayMin"), it replaces the fix part of the realism
    // level (the model flags stay). Empty text = the realism level's own fixes. Throws biosys::Error on unknown words.
    static Settings parse(const std::string& realism, const std::string& difficulty, const std::string& fixesOverride = "");
    bool isDefault() const { return !fixes.any() && !model.any() && tuning.isOriginal(); }
    std::string toString() const;      // "fixes=...; model=...; tuning=..."
};

} // namespace biosys::game
