#include "engine/bs4fixes.h"

namespace biosys {

const std::vector<Bs4Fixes::Info>& Bs4Fixes::all() {
    static const std::vector<Info> v = {
#define BIOSYS_BS4_FIX_INFO(name, preset, desc) {#name, preset != 0, desc, &Bs4Fixes::name},
        BIOSYS_BS4_FIXES(BIOSYS_BS4_FIX_INFO)
#undef BIOSYS_BS4_FIX_INFO
    };
    return v;
}

Bs4Fixes Bs4Fixes::restored() {
    Bs4Fixes f;
    for (const Info& i : all())
        if (i.preset) f.*(i.member) = true;
    return f;
}

bool Bs4Fixes::applyWord(const std::string& word) {
    const bool minus = !word.empty() && word[0] == '-';
    const std::string name = minus ? word.substr(1) : word;
    for (const Info& i : all())
        if (name == i.name) {
            this->*(i.member) = !minus;
            return true;
        }
    if (name == "faithful") {
        if (!minus) *this = Bs4Fixes();
        return true;
    }
    if (name == "safe") return true;                 // like game::Fixes, a preset adds its flags: the terminal has none of this tier
    if (name == "cosmetic" || name == "restored") {
        for (const Info& i : all())
            if (i.preset) this->*(i.member) = !minus;
        return true;
    }
    return false;
}

bool Bs4Fixes::isPresetWord(const std::string& word) {
    const std::string n = !word.empty() && word[0] == '-' ? word.substr(1) : word;
    return n == "faithful" || n == "safe" || n == "cosmetic" || n == "restored";
}

bool Bs4Fixes::any() const {
    for (const Info& i : all())
        if (this->*(i.member)) return true;
    return false;
}

std::string Bs4Fixes::toString() const {
    std::string s;
    for (const Info& i : all())
        if (this->*(i.member)) s += (s.empty() ? "" : ",") + std::string(i.name);
    return s.empty() ? "faithful" : s;
}

} // namespace biosys
