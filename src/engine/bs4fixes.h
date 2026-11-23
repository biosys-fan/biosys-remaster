// The terminal's own remaster switch: one named bool per original bug of the terminal (engine side, so it cannot read game::Fixes).
// Default = all false = the unchanged translation of BioSys.exe. Every bug site reads `owner().fixes().<flag>`:
//     if (fixes.flag) { intended behaviour } else { the original, unchanged }
// The front end sets it (src/app/main.cpp: `--realism corrected|closedloop` = every flag whose preset column is 1; `--fixes a,b,-c` names
// them individually, next to the flags of game::Fixes). Evidence per flag: FIXES.md, section "Terminal".
#pragma once
#include <string>
#include <vector>

namespace biosys {

// X(name, preset, one-line description): preset 1 = switched on by the realism levels corrected / closedloop and by the fix preset
// `restored`; 0 = intent not evident, only by name.
#define BIOSYS_BS4_FIXES(X)                                                                                                       \
    X(healthOceanOrnaments, 1, "health page: the row `ocean` names ornaments the page lacks (oceand/u/h instead of ocd/ocu/och)")  \
    X(panelFollowsVariant, 1, "a variant tab inside a panel re-initialises the panel for the device it shows")

struct Bs4Fixes {
#define BIOSYS_BS4_FIX_MEMBER(name, preset, desc) bool name = false;
    BIOSYS_BS4_FIXES(BIOSYS_BS4_FIX_MEMBER)
#undef BIOSYS_BS4_FIX_MEMBER

    struct Info {
        const char* name;
        bool preset;
        const char* description;
        bool Bs4Fixes::*member;
    };
    static const std::vector<Info>& all();
    static Bs4Fixes faithful() { return Bs4Fixes(); }
    static Bs4Fixes restored();                      // every flag with preset 1

    // One word of a --fixes list: a flag name switches it on, "-name" off; the words "faithful" (all off), "safe" (changes nothing: none of the
    // terminal's flags is a memory-safety fix), "cosmetic" / "restored" (the preset flags on; "-restored" off). Returns false for a word
    // that is none of these (the caller hands it to game::Fixes::parse, which refuses unknown words).
    bool applyWord(const std::string& word);
    static bool isPresetWord(const std::string& word);      // faithful | safe | cosmetic | restored, with or without a leading '-'

    bool any() const;
    std::string toString() const;                    // comma list of the flags that are on ("faithful" when none)
};

} // namespace biosys
