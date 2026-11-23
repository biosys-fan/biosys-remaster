// The preferences file of the window (`biosys.conf` in the save folder) and the option table that drives it. No SDL, header only
// (build/biosys-apptest includes it).
//
// One table (OptTable<O>, built in main.cpp) lists every option once: the command-line name, the preference key (empty = command line
// only), what it takes, its default and its help line, and two functions that set the option from text and give it back as text. The
// command line, the preferences file, the generated default file and `--write-config` all go through it, so there is no second list.
//
// The file: `key = value` lines, `#` (or `;`) at the start of a line = comment, blank lines. Keys are lower case, the same words as the
// long options without the dashes (`pass-ms`, `panorama-filter`); switches take yes / no (also true / false, on / off, 1 / 0). The order
// of precedence is: built-in default, the file, the command line. A line that is not understood (unknown key, bad value, no `=`) is
// reported on stderr once and ignored. When the program changes the file (volume, mute, full screen at exit), only the values of those
// keys are replaced: comments, other keys and unknown lines stay as they are.
#pragma once
#include "engine/common.h"
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace biosys {

inline std::string prefTrim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

inline std::string prefLower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return s;
}

// yes / no, true / false, on / off, 1 / 0 (any case)
inline bool parsePrefBool(const std::string& text, bool& out) {
    const std::string v = prefLower(prefTrim(text));
    if (v == "yes" || v == "true" || v == "on" || v == "1") { out = true; return true; }
    if (v == "no" || v == "false" || v == "off" || v == "0") { out = false; return true; }
    return false;
}
inline std::string prefBoolText(bool b) { return b ? "yes" : "no"; }

struct PrefEntry {
    std::string key, value;
    int line = 0;
};
struct PrefFile {
    std::vector<PrefEntry> entries;       // in file order; a key may repeat (the last one wins when applied)
    std::vector<std::string> problems;    // "line N: ..." for lines that are not `key = value`
};

inline PrefFile parsePrefs(const std::string& text) {
    PrefFile f;
    size_t pos = 0;
    int n = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = prefTrim(text.substr(pos, nl - pos));
        pos = nl + 1;
        n++;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            f.problems.push_back("line " + std::to_string(n) + ": expected `key = value`");
            continue;
        }
        PrefEntry e;
        e.key = prefLower(prefTrim(line.substr(0, eq)));
        e.value = prefTrim(line.substr(eq + 1));
        if (e.value.size() >= 2 && e.value.front() == '"' && e.value.back() == '"') e.value = e.value.substr(1, e.value.size() - 2);
        e.line = n;
        if (e.key.empty()) {
            f.problems.push_back("line " + std::to_string(n) + ": no key before `=`");
            continue;
        }
        f.entries.push_back(e);
    }
    return f;
}

// How an option takes its value on the command line
enum class OptKind { Switch, Value, Optional };   // `--x`, `--x V`, `--x [V]` (V is only taken when it does not start with `--`)

template <class O>
struct OptSpec {
    std::string cli;       // "--volume"
    std::string key;       // "volume" ("" = command line only, not a preference)
    OptKind kind = OptKind::Value;
    std::string def;       // the default as text (what the generated file shows)
    std::string help;      // one line for the generated file
    // From text; a switch given as `--x` gets "yes". Throws biosys::Error with a message that names the option.
    std::function<void(O&, const std::string&)> set;
    // The current value as text, such that set(get()) is the identity; empty function for options that are not preferences
    std::function<std::string(const O&)> get;
};

template <class O>
class OptTable {
public:
    std::vector<OptSpec<O>> specs;

    const OptSpec<O>* byCli(const std::string& name) const {
        for (const OptSpec<O>& s : specs)
            if (s.cli == name) return &s;
        return nullptr;
    }
    const OptSpec<O>* byKey(const std::string& key) const {
        if (key.empty()) return nullptr;
        for (const OptSpec<O>& s : specs)
            if (s.key == key) return &s;
        return nullptr;
    }
    std::vector<std::string> keys() const {
        std::vector<std::string> k;
        for (const OptSpec<O>& s : specs)
            if (!s.key.empty()) k.push_back(s.key);
        return k;
    }

    // Applies the entries of a preferences file to `o`. Unknown keys are reported once each, bad values and malformed lines once;
    // every problem is appended to `problems` and the line is ignored.
    void apply(O& o, const PrefFile& f, std::vector<std::string>& problems) const {
        for (const std::string& p : f.problems) problems.push_back(p);
        std::set<std::string> unknownSeen;
        for (const PrefEntry& e : f.entries) {
            const OptSpec<O>* s = byKey(e.key);
            if (!s) {
                if (unknownSeen.insert(e.key).second) problems.push_back("line " + std::to_string(e.line) + ": unknown key `" + e.key + "` ignored");
                continue;
            }
            try {
                if (s->kind == OptKind::Switch) {
                    bool b = false;
                    if (!parsePrefBool(e.value, b)) throw Error("want yes or no");
                    s->set(o, prefBoolText(b));
                } else {
                    s->set(o, e.value);
                }
            } catch (const Error& err) {
                problems.push_back("line " + std::to_string(e.line) + ": " + e.key + ": " + err.what() + "; line ignored");
            }
        }
    }

    // The commented default file: every key with its help line and its default, nothing active.
    std::string defaultFile() const {
        std::string t =
            "# Biosys preferences. One `key = value` per line; a line that starts with # is a comment.\n"
            "# Order of precedence: built-in default, this file, the command line (`--key value` overrides the line here).\n"
            "# Remove the # in front of a line to change that setting. The program changes only the values of volume, start-muted and\n"
            "# fullscreen in this file (when you change them while playing); everything else you write here stays as it is.\n"
            "# Switches take yes or no.\n\n";
        for (const OptSpec<O>& s : specs) {
            if (s.key.empty()) continue;
            t += "# " + s.help + "\n# " + s.key + " = " + s.def + "\n\n";
        }
        return t;
    }

    // key -> current value, for every preference
    std::map<std::string, std::string> currentValues(const O& o) const {
        std::map<std::string, std::string> m;
        for (const OptSpec<O>& s : specs)
            if (!s.key.empty() && s.get) m[s.key] = s.get(o);
        return m;
    }
};

// Puts `updates` (key -> value) into the text of a preferences file: an active `key = value` line gets the new value (the key as written is
// kept, the line is rewritten as `key = value`); a key without an active line is added right after its commented default (`# key = ...`),
// else at the end. Everything else is kept byte for byte.
inline std::string updatePrefsText(const std::string& text, const std::map<std::string, std::string>& updates) {
    std::vector<std::string> lines;
    {
        size_t pos = 0;
        while (pos < text.size()) {
            size_t nl = text.find('\n', pos);
            if (nl == std::string::npos) nl = text.size();
            lines.push_back(text.substr(pos, nl - pos));
            pos = nl + 1;
        }
    }
    std::set<std::string> done;
    for (std::string& l : lines) {
        const std::string t = prefTrim(l);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = prefLower(prefTrim(t.substr(0, eq)));
        auto it = updates.find(key);
        if (it == updates.end()) continue;
        l = key + " = " + it->second;
        done.insert(key);
    }
    for (const auto& u : updates) {
        if (done.count(u.first)) continue;
        size_t at = lines.size();
        for (size_t i = 0; i < lines.size(); i++) {
            const std::string t = prefTrim(lines[i]);
            if (t.size() > 1 && t[0] == '#' && prefLower(prefTrim(t.substr(1))).compare(0, u.first.size() + 1, u.first + " ") == 0) { at = i + 1; break; }
        }
        lines.insert(lines.begin() + static_cast<long>(at), u.first + " = " + u.second);
    }
    std::string out;
    for (const std::string& l : lines) out += l + "\n";
    return out;
}

} // namespace biosys
