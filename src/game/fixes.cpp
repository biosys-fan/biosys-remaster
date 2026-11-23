#include "game/fixes.h"
#include "engine/common.h"
#include <algorithm>
#include <cctype>

namespace biosys::game {

const std::vector<Fixes::Info>& Fixes::all() {
    static const std::vector<Info> table = {
#define BIOSYS_FIX_INFO(name, tier, desc) {#name, FixTier::tier, desc, &Fixes::name},
        BIOSYS_FIXES(BIOSYS_FIX_INFO)
#undef BIOSYS_FIX_INFO
    };
    return table;
}

namespace {
Fixes upTo(FixTier t) {
    Fixes f;
    for (const Fixes::Info& i : Fixes::all())
        if (i.tier != FixTier::Unclear && static_cast<int>(i.tier) <= static_cast<int>(t)) f.*(i.member) = true;
    return f;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}
}

Fixes Fixes::safe() { return upTo(FixTier::Safe); }
Fixes Fixes::cosmetic() { return upTo(FixTier::Cosmetic); }
Fixes Fixes::restored() { return upTo(FixTier::Restored); }

Fixes Fixes::parse(const std::string& text) {
    Fixes f;
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
        Fixes set;
        bool known = false;
        if (word == "faithful") known = true;
        else if (word == "safe") { set = safe(); known = true; }
        else if (word == "cosmetic") { set = cosmetic(); known = true; }
        else if (word == "restored") { set = restored(); known = true; }
        else {
            for (const Info& i : all())
                if (word == i.name) { set.*(i.member) = true; known = true; }
        }
        if (!known) throw Error("unknown fix or preset '" + word + "'");
        for (const Info& i : all())
            if (set.*(i.member)) f.*(i.member) = on;
    }
    return f;
}

bool Fixes::any() const {
    for (const Info& i : all())
        if (this->*(i.member)) return true;
    return false;
}

std::string Fixes::toString() const {
    std::string s;
    for (const Info& i : all())
        if (this->*(i.member)) s += (s.empty() ? "" : ",") + std::string(i.name);
    return s.empty() ? "faithful" : s;
}

} // namespace biosys::game
