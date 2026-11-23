// The settings a saved game was made under (realism, difficulty, fix list). They are written as a block after the game's part of the
// save (engine/savetrailer.h), so the engine's part and the game's part stay byte-for-byte the original's. Saves made by earlier
// versions of the port have them in a small text file next to the save instead ("Saved Game 3.sav" -> "Saved Game 3.settings", the
// sidecar); those still load. No SDL here.
#pragma once
#include "engine/bs4fixes.h"
#include "engine/common.h"
#include "game/settings.h"
#include <cstdint>
#include <string>
#include <vector>

namespace biosys {

// The three words of the command line (--realism, --difficulty, --fixes)
struct SettingsText {
    std::string realism = "faithful", difficulty = "original", fixes;
    bool operator==(const SettingsText& o) const { return realism == o.realism && difficulty == o.difficulty && fixes == o.fixes; }
    bool operator!=(const SettingsText& o) const { return !(*this == o); }
    std::string describe() const {
        return "realism " + realism + ", difficulty " + difficulty + (fixes.empty() ? "" : ", fixes " + fixes);
    }
};

struct ResolvedSettings {
    game::Settings settings;
    Bs4Fixes bs4;
};

// The terminal's fixes: all on for corrected / closedloop, all off for faithful; the fix list replaces them. The words of the list that
// name a terminal flag go to the terminal, the rest (and every preset word) to game::Fixes::parse, which refuses unknown ones.
// Throws biosys::Error on an unknown word.
inline ResolvedSettings resolveSettings(const SettingsText& t) {
    ResolvedSettings r;
    const bool corrected = t.realism == "corrected" || t.realism == "closedloop";
    r.bs4 = corrected ? Bs4Fixes::restored() : Bs4Fixes();
    std::string gameList;
    bool listed = false;
    if (!t.fixes.empty()) {
        r.bs4 = Bs4Fixes();
        size_t pos = 0;
        while (pos <= t.fixes.size()) {
            size_t comma = t.fixes.find(',', pos);
            if (comma == std::string::npos) comma = t.fixes.size();
            std::string w = t.fixes.substr(pos, comma - pos);
            pos = comma + 1;
            while (!w.empty() && w.front() == ' ') w.erase(w.begin());
            while (!w.empty() && w.back() == ' ') w.pop_back();
            if (w.empty()) continue;
            listed = true;
            const bool terminal = r.bs4.applyWord(w);
            if (!terminal || Bs4Fixes::isPresetWord(w)) gameList += (gameList.empty() ? "" : ",") + w;
        }
    }
    // a list that names only terminal flags replaces the game's fixes of the realism level by none
    r.settings = game::Settings::parse(t.realism, t.difficulty, listed && gameList.empty() ? "faithful" : gameList);
    return r;
}

// "<dir>/Saved Game 3.sav" -> "<dir>/Saved Game 3.settings"; "" for a file that does not end in .sav (any case) and for NewGame.sav
inline std::string sidecarPathOf(const std::string& savPath) {
    if (savPath.size() < 5 || !iequals(savPath.substr(savPath.size() - 4), ".sav")) return "";
    const std::string base = savPath.substr(savPath.find_last_of("/\\") + 1);
    if (iequals(base, "newgame.sav")) return "";
    return savPath.substr(0, savPath.size() - 4) + ".settings";
}

// FNV-1a over the whole save: the sidecar belongs to exactly these bytes (a save copied over its slot makes the sidecar stale)
inline uint64_t saveHash(const std::vector<uint8_t>& bytes) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : bytes) { h ^= b; h *= 1099511628211ull; }
    return h;
}

inline std::string sidecarText(const SettingsText& t, uint64_t hash) {
    char hx[32];
    std::snprintf(hx, sizeof hx, "%016llx", static_cast<unsigned long long>(hash));
    return "biosys-settings 1\nrealism=" + t.realism + "\ndifficulty=" + t.difficulty + "\nfixes=" + t.fixes + "\nsave=" + hx + "\n";
}

// The text inside the trailer of a save: the sidecar's lines without the hash of the file (the block is part of the file)
inline std::string settingsText(const SettingsText& t) {
    return "biosys-settings 1\nrealism=" + t.realism + "\ndifficulty=" + t.difficulty + "\nfixes=" + t.fixes + "\n";
}

// false for text that is not settings text of this version, or names a word the program does not know
inline bool parseSettingsText(const std::string& text, SettingsText& out) {
    size_t pos = 0;
    bool head = false, haveR = false, haveD = false, haveF = false;
    SettingsText t;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!head) {
            if (line != "biosys-settings 1") return false;
            head = true;
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "realism") { t.realism = v; haveR = true; }
        else if (k == "difficulty") { t.difficulty = v; haveD = true; }
        else if (k == "fixes") { t.fixes = v; haveF = true; }
    }
    if (!(head && haveR && haveD && haveF)) return false;
    try {
        resolveSettings(t);
    } catch (const Error&) {
        return false;
    }
    out = t;
    return true;
}

// false for text that is not a sidecar of this version (the caller then treats the save as an original one)
inline bool parseSidecar(const std::string& text, SettingsText& out, uint64_t& hash) {
    size_t pos = 0;
    bool head = false, haveR = false, haveD = false, haveF = false, haveH = false;
    SettingsText t;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!head) {
            if (line != "biosys-settings 1") return false;
            head = true;
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "realism") { t.realism = v; haveR = true; }
        else if (k == "difficulty") { t.difficulty = v; haveD = true; }
        else if (k == "fixes") { t.fixes = v; haveF = true; }
        else if (k == "save") {
            if (v.size() != 16 || v.find_first_not_of("0123456789abcdef") != std::string::npos) return false;
            hash = std::strtoull(v.c_str(), nullptr, 16);
            haveH = true;
        }
    }
    if (!(head && haveR && haveD && haveF && haveH)) return false;
    out = t;
    return true;
}

} // namespace biosys
