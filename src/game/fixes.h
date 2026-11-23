// The remaster switch: one bool per distinct bug of the original (every `ORIGINAL BUG` site of src/game). The default
// (all false = preset "faithful") is the byte-for-byte translation. Each bug site reads `world().state().fixes.<flag>`.
// Report with the evidence and measurements: FIXES.md
#pragma once
#include <string>
#include <vector>

namespace biosys::game {

// Tier of a flag: the smallest preset that contains it. Unclear (0) is in no preset.
enum class FixTier { Unclear = 0, Safe = 1, Cosmetic = 2, Restored = 3 };

// X(name, tier, one-line description)
#define BIOSYS_FIXES(X)                                                                                             \
    /* safe: over-reads, uninitialised data, NULL reads, the two Load-calls-Save slips */                           \
    X(lungLoadBaseBlock, Safe, "CLung::Load calls CFeature::Save: the saved base block of lungs is ignored")        \
    X(hatchLoadBaseBlock, Safe, "CHatch::Load calls CFeature::Save: the saved base block of hatches is ignored")    \
    X(gogglesImageUninit, Safe, "goggles with an invalid power state place an uninitialised image name")            \
    X(synthSoundHandleInit, Safe, "the synth handler's sound handle starts at 0 (not -1)")                          \
    X(journeySoundUninit, Safe, "a path with no terrain bit plays an uninitialised file name")                      \
    X(selfDumpNullSite, Safe, "CSelf::Dump dereferences a NULL site")                                               \
    X(ambientSoundIndex, Safe, "CSite::RandomAmbient: rand()==0x7fff indexes one past the sound set")               \
    X(variantSoundIndex, Safe, "CSiteVariant::OnTimeOfDayChange: rand()==0x7fff indexes one past the sound set")    \
    X(plantStateTextUninit, Safe, "environment status: plant health state with none of the known bits loads a stale string id") \
    X(sealMovieNullCheck, Safe, "CSeal::Close tests the first movie for NULL but starts the second")                \
    /* cosmetic: display only */                                                                                    \
    X(panelNumberAlign, Cosmetic, "panel numbers are right-aligned by the width of the %d text, not their own")     \
    X(sensorAlarmFxStop, Cosmetic, "sensor alarm FX stopped when there was no alarm before (inverted test)")        \
    X(coffeeMakerCloseSound, Cosmetic, "coffee machine closes sound handle -1 instead of the playing sound")        \
    X(oxyCylinderEmptyVariation, Cosmetic, "empty oxygen cylinder switches to variation \"off\" (elsewhere \"oxyoff\")") \
    X(pumpParamShared, Cosmetic, "water pump: source and destination code share one member")                       \
    X(helpKeyFallthrough, Cosmetic, "F1 falls through into the typed-letters (cheat word) code")                    \
    /* restored: behaviour-affecting, intended behaviour evident */                                                 \
    X(oceanProbeClarity, Restored, "ocean probe clamps the salinity twice and the clarity never")                   \
    X(oceanProbeLevelNoise, Restored, "ocean probe level reading uses the temperature noise range")                 \
    X(bodyTempAtTarget, Restored, "body temperature exactly at its target adds the air temperature as a change")    \
    X(plantColdDayMin, Restored, "cold damage of a plant is always measured from the night minimum")                \
    X(rainWashesPollution, Restored, "rain lowers the current pollution only; the commit restores it")              \
    X(podDoorSoundHandle, Restored, "pod door: the sound-handle global becomes 4, not -1")                          \
    X(lungParamUnknownId, Restored, "CLung::SetParam: an unknown source code runs the mode code")                    \
    X(lungParamUnknownMode, Restored, "CLung::SetParam: an unknown mode stops the lung for good and is stored (then NULL target cel)")                    \
    X(demoExitQuits, Restored, "hotspot MEXIT never quits (the test reads a wrong stack slot)")                     \
    X(coffeeThirdDose, Restored, "a refused third coffee still adds hydration (the dose limit is tested after it)")  \
    /* intent unclear: in no preset */                                                                              \
    X(plantFloodedUsesMax, Unclear, "plant flooded water availability divides by (min-47.5), probably max")        \
    X(condensationFlagPerCel, Unclear, "condensation flows: one on/off flag for all cels (and flow id 0)")          \
    X(consumableCodeFromHotspot, Unclear, "CConsumable::FillFromHotspot uses the first two characters as content code")

struct Fixes {
#define BIOSYS_FIX_MEMBER(name, tier, desc) bool name = false;
    BIOSYS_FIXES(BIOSYS_FIX_MEMBER)
#undef BIOSYS_FIX_MEMBER

    struct Info {
        const char* name;
        FixTier tier;
        const char* description;
        bool Fixes::*member;
    };
    static const std::vector<Info>& all();

    static Fixes faithful() { return Fixes(); }
    static Fixes safe();
    static Fixes cosmetic();
    static Fixes restored();
    // "faithful" | "safe" | "cosmetic" | "restored" | flag names, comma separated, applied left to right: a preset or a
    // flag name switches those flags on, "-name" (or "-preset") switches them off. Empty text = faithful.
    // Throws biosys::Error on an unknown word.
    static Fixes parse(const std::string& text);

    bool any() const;
    // Comma list of the flags that are on ("faithful" when none).
    std::string toString() const;
};

} // namespace biosys::game
