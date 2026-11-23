#include "game/settings.h"
#include "engine/common.h"
#include <cctype>
#include <cstdio>

namespace biosys::game {

namespace {
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}
Model upTo(ModelTier t) {
    Model m;
    for (const Model::Info& i : Model::all())
        if (static_cast<int>(i.tier) <= static_cast<int>(t)) m.*(i.member) = true;
    return m;
}
}

const std::vector<Model::Info>& Model::all() {
    static const std::vector<Info> table = {
#define BIOSYS_MODEL_INFO(name, tier, item, desc) {#name, ModelTier::tier, item, desc, &Model::name},
        BIOSYS_MODEL(BIOSYS_MODEL_INFO)
#undef BIOSYS_MODEL_INFO
    };
    return table;
}

Model Model::corrected() { return upTo(ModelTier::Corrected); }
Model Model::closedloop() { return upTo(ModelTier::ClosedLoop); }

Model Model::parse(const std::string& text) {
    Model m;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t comma = text.find(',', pos);
        if (comma == std::string::npos) comma = text.size();
        std::string word = trim(text.substr(pos, comma - pos));
        pos = comma + 1;
        if (word.empty()) continue;
        bool on = true;
        if (word[0] == '-') { on = false; word = trim(word.substr(1)); }
        else if (word[0] == '+') word = trim(word.substr(1));
        Model set;
        bool known = false;
        if (word == "faithful") known = true;
        else if (word == "corrected") { set = corrected(); known = true; }
        else if (word == "closedloop") { set = closedloop(); known = true; }
        else {
            for (const Info& i : all())
                if (word == i.name) { set.*(i.member) = true; known = true; }
        }
        if (!known) throw Error("unknown model flag or realism level '" + word + "'");
        for (const Info& i : all())
            if (set.*(i.member)) m.*(i.member) = on;
    }
    return m;
}

bool Model::any() const {
    for (const Info& i : all())
        if (this->*(i.member)) return true;
    return false;
}

std::string Model::toString() const {
    std::string s;
    for (const Info& i : all())
        if (this->*(i.member)) s += (s.empty() ? "" : ",") + std::string(i.name);
    return s.empty() ? "faithful" : s;
}

// ---------------------------------------------------------------------------------------------------------------

const std::vector<Tuning::Info>& Tuning::all() {
    static const std::vector<Info> table = {
#define BIOSYS_TUNING_INFO(name, value, desc) {#name, value, desc, &Tuning::name},
        BIOSYS_TUNING(BIOSYS_TUNING_INFO)
#undef BIOSYS_TUNING_INFO
    };
    return table;
}

// Values of the study (remaster_design.md D.2). Castaway is not offered. Where the study gives a column "Survivor" equal to
// "Original" the preset leaves the original value.
Tuning Tuning::guided() {
    Tuning t;
    t.startHydration = 80;
    t.startNutrition = 90;
    t.startEnergy = 90;
    t.hydrationDrain = 0.5;
    t.nutritionDrain = 0.5;
    t.heatWaterTerm = 0.5;
    t.starveDamage = t.thirstDamage = t.heatDamage = t.coldDamage = t.oxygenDamage = 0.5;
    t.awakeLimitHours = 36;
    t.healing = 1.5;
    t.sleepHealing = 1.5;
    t.plantDamage = 0.5;
    t.plantRecovery = 1.5;
    t.powerGeneration = 1.5;
    t.siltClog = 0.5;                 // 0.05 per day
    t.heliDelaySeconds = 14.0 * 86400.0;
    t.huntChancePercent = 1;
    t.huntCooldownSeconds = 600;
    t.malaria = 0.5;
    t.sensorNoise = 1;
    t.alarmMargin = 0.1;              // "early warning"
    return t;
}

Tuning Tuning::survivor() {
    Tuning t;
    t.hydrationDrain = 1.25;
    t.nutritionDrain = 1.25;
    t.starveDamage = t.thirstDamage = t.heatDamage = t.coldDamage = t.oxygenDamage = 1.5;
    t.healing = 0.75;
    t.sleepHealing = 0.75;
    t.plantDamage = 1.25;
    t.powerGeneration = 0.8;
    t.siltClog = 1.5;                 // 0.15 per day
    t.heliDelaySeconds = 5.0 * 86400.0;
    t.sensorNoise = 5;
    return t;
}

Tuning Tuning::parse(const std::string& preset) {
    const std::string p = trim(preset);
    if (p.empty() || p == "original") return original();
    if (p == "guided") return guided();
    if (p == "survivor") return survivor();
    throw Error("unknown difficulty '" + p + "' (guided, original, survivor)");
}

void Tuning::set(const std::string& name, double value) {
    for (const Info& i : all())
        if (name == i.name) { this->*(i.member) = value; return; }
    throw Error("unknown tuning knob '" + name + "'");
}

double Tuning::get(const std::string& name) const {
    for (const Info& i : all())
        if (name == i.name) return this->*(i.member);
    throw Error("unknown tuning knob '" + name + "'");
}

bool Tuning::isOriginal() const {
    for (const Info& i : all())
        if (this->*(i.member) != i.original) return false;
    return true;
}

std::string Tuning::toString() const {
    std::string s;
    for (const Info& i : all())
        if (this->*(i.member) != i.original) {
            char b[48];
            std::snprintf(b, sizeof b, "%s=%g", i.name, this->*(i.member));
            s += (s.empty() ? "" : ",") + std::string(b);
        }
    return s.empty() ? "original" : s;
}

// ---------------------------------------------------------------------------------------------------------------

Settings Settings::parse(const std::string& realism, const std::string& difficulty, const std::string& fixesOverride) {
    Settings s;
    const std::string r = trim(realism);
    if (r.empty() || r == "faithful") {
    } else if (r == "corrected") {
        s.fixes = Fixes::restored();
        s.model = Model::corrected();
    } else if (r == "closedloop") {
        s.fixes = Fixes::restored();
        s.model = Model::closedloop();
    } else {
        throw Error("unknown realism '" + r + "' (faithful, corrected, closedloop)");
    }
    s.tuning = Tuning::parse(difficulty);
    if (!trim(fixesOverride).empty()) s.fixes = Fixes::parse(fixesOverride);
    return s;
}

std::string Settings::toString() const {
    return "fixes=" + fixes.toString() + "; model=" + model.toString() + "; tuning=" + tuning.toString();
}

} // namespace biosys::game
