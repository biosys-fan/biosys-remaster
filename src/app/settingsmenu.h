// The settings menu (F10) and the new-game screen: their model, their layout on the 640 x 480 grid and their input handling. No SDL, no
// engine, header only (build/biosys-menutest includes it). The pictures are drawn by settingsmenu_draw.cpp, the window and the script
// commands (main.cpp) apply what the menu changes.
//
// The menu knows only preference keys and their text (the keys of the option table in main.cpp and src/app/prefs.h): it is loaded from
// `key -> text` (OptTable::currentValues), and every change it makes is a `Change{key, text}` that the host applies to the live state and
// writes into the preferences file. Items that depend on each other (widescreen needs smooth panoramas, clean pictures need smooth
// panoramas and sharp text) switch the others by emitting changes for them too, so the host sees one list of plain changes.
//
// Coordinates are the 640 x 480 grid of the game's picture. The panels are centred on it, so in a wide window they stay in the 4:3 middle.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace biosys {
namespace smenu {

// ---------------------------------------------------------------------------------------------------------------- geometry
struct R {
    int x = 0, y = 0, w = 0, h = 0;
    bool has(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

constexpr int kGridW = 640, kGridH = 480;

// the settings panel
constexpr int kPX = 60, kPY = 32, kPW = 520, kPH = 416;
constexpr int kTabs = 5, kTabW = 94, kTabGap = 4, kTabH = 26;
constexpr int kRowY0 = kPY + 82, kRowH = 28, kMaxRows = 8;
constexpr int kFootY = kPY + 322;
inline R panelRect() { return R{kPX, kPY, kPW, kPH}; }
inline R closeRect() { return R{kPX + kPW - 96, kPY + 8, 80, 26}; }
inline R tabRect(int i) { return R{kPX + 16 + i * (kTabW + kTabGap), kPY + 44, kTabW, kTabH}; }
inline R rowRect(int r) { return R{kPX + 8, kRowY0 + r * kRowH, kPW - 16, kRowH}; }
constexpr int kLabelX = kPX + 20, kValX0 = kPX + 262, kValX1 = kPX + kPW - 20;
inline R arrowLRect(int r) { return R{kValX0, kRowY0 + r * kRowH + 2, 26, kRowH - 4}; }
inline R arrowRRect(int r) { return R{kValX1 - 26, kRowY0 + r * kRowH + 2, 26, kRowH - 4}; }
inline R trackRect(int r) { return R{kValX0 + 4, kRowY0 + r * kRowH + 2, 150, kRowH - 4}; }
constexpr int kRangeTextX = kValX0 + 168;

// the new-game screen
constexpr int kNX = 40, kNY = 24, kNW = 560, kNH = 432;
constexpr int kNBlockH = 90;
inline R ngPanelRect() { return R{kNX, kNY, kNW, kNH}; }
inline R ngBlockRect(int col, int i) { return R{kNX + 16 + col * 280, kNY + 68 + i * kNBlockH, 248, kNBlockH - 6}; }
inline R ngStartRect() { return R{kNX + kNW - 216, kNY + 392, 96, 30}; }
inline R ngBackRect() { return R{kNX + kNW - 112, kNY + 392, 96, 30}; }
constexpr int kNoteY = kNY + 340;

// ---------------------------------------------------------------------------------------------------------------- items
enum class When { Now, NextStart, NextNewGame, Fixed };
enum class Kind { Choice, Range, Info };
enum Tab { TabGame, TabPicture, TabSound, TabControls, TabSaving };
inline const char* tabName(int t) {
    static const char* n[kTabs] = {"Game", "Picture", "Sound", "Controls", "Saving"};
    return t >= 0 && t < kTabs ? n[t] : "";
}

struct Choice {
    std::string pref, label;
};
struct Item {
    std::string key;            // the preference key ("info:..." for the read-only rows)
    std::string label;
    int tab = 0;
    Kind kind = Kind::Choice;
    When when = When::Now;
    std::string help;
    std::vector<Choice> choices;
    int lo = 0, hi = 0, step = 1, big = 10, div = 1;
    std::string unit, zeroText;
    int value = 0;              // Choice: index; Range: the number (percent, ms, ...; with div 100 the factor times 100)
    std::string info;           // Info: the text shown
    bool visible = true;
};
struct Change {
    std::string key, text;
    bool operator==(const Change& o) const { return key == o.key && text == o.text; }
};

inline const char* whenText(When w) {
    switch (w) {
    case When::Now: return "applies now";
    case When::NextStart: return "applies at the next start";
    case When::NextNewGame: return "applies to the next new game";
    default: return "fixed when this game was started";
    }
}

inline std::string capital(std::string s) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 32);
    return s;
}
// "closedloop" -> "Closed loop"
inline std::string realismName(const std::string& w) { return w == "closedloop" ? "Closed loop" : capital(w); }
inline std::string difficultyName(const std::string& w) { return capital(w); }

class Model {
public:
    Model() { build(); }

    // the names of the screens, in the order of the preference `display` (1 = the main screen); none known: one entry
    void setScreens(const std::vector<std::string>& names) {
        Item& it = item("display");
        it.choices.clear();
        for (size_t i = 0; i < names.size(); i++)
            it.choices.push_back({std::to_string(i + 1), std::to_string(i + 1) + (names[i].empty() ? "" : " " + shortName(names[i])) + (i == 0 ? " (main)" : "")});
        if (it.choices.empty()) it.choices.push_back({"1", "1 (main)"});
        it.value = std::min(it.value, static_cast<int>(it.choices.size()) - 1);
    }
    // the game that runs now (read-only rows of the Game tab); `fixes` is the developer's fix list ("" = none)
    void setRunning(const std::string& realism, const std::string& difficulty, const std::string& fixes) {
        item("info:realism").info = realismName(realism);
        item("info:difficulty").info = difficultyName(difficulty);
        item("info:fixes").info = fixes;
        item("info:fixes").visible = !fixes.empty();
    }
    // false: the preferences are not written (--no-config)
    void setPersistent(bool on) { persistent_ = on; }
    bool persistent() const { return persistent_; }

    // Loads the values (key -> text, as OptTable::currentValues gives them); keys of no item are ignored. Makes no changes.
    void load(const std::map<std::string, std::string>& v) {
        for (Item& it : items_) {
            if (it.kind == Kind::Info) continue;
            auto f = v.find(it.key);
            if (f == v.end()) continue;
            if (it.kind == Kind::Choice) {
                for (size_t i = 0; i < it.choices.size(); i++)
                    if (lower(it.choices[i].pref) == lower(f->second)) it.value = static_cast<int>(i);
            } else {
                const double d = std::strtod(f->second.c_str(), nullptr);
                it.value = clampRange(it, static_cast<int>(std::lround(d * it.div)));
            }
        }
        clampRow();
    }

    // ---- what is shown
    int tab() const { return tab_; }
    int row() const { return row_; }
    const std::vector<Item>& items() const { return items_; }
    std::vector<int> rows(int tab) const {
        std::vector<int> r;
        for (size_t i = 0; i < items_.size(); i++)
            if (items_[i].tab == tab && items_[i].visible) r.push_back(static_cast<int>(i));
        return r;
    }
    int rowCount() const { return static_cast<int>(rows(tab_).size()); }
    const Item* selected() const {
        const std::vector<int> r = rows(tab_);
        return row_ >= 0 && row_ < static_cast<int>(r.size()) ? &items_[static_cast<size_t>(r[static_cast<size_t>(row_)])] : nullptr;
    }
    const Item* find(const std::string& key) const {
        for (const Item& it : items_) if (it.key == key) return &it;
        return nullptr;
    }
    const Item& at(int tab, int row) const { return items_[static_cast<size_t>(rows(tab)[static_cast<size_t>(row)])]; }
    static std::string valueText(const Item& it) {
        if (it.kind == Kind::Info) return it.info;
        if (it.kind == Kind::Choice) return it.choices.empty() ? "" : it.choices[static_cast<size_t>(it.value)].label;
        if (it.value == 0 && !it.zeroText.empty()) return it.zeroText;
        return std::to_string(it.value) + it.unit;
    }
    static std::string prefText(const Item& it) {
        if (it.kind == Kind::Info) return "";
        if (it.kind == Kind::Choice) return it.choices.empty() ? "" : it.choices[static_cast<size_t>(it.value)].pref;
        if (it.div == 1) return std::to_string(it.value);
        char b[32];
        std::snprintf(b, sizeof b, "%g", static_cast<double>(it.value) / it.div);
        return b;
    }
    std::string footNote() const {
        return persistent_ ? "Changes are saved in the preferences file." : "No preferences file is in use (--no-config): changes last until you quit.";
    }

    // ---- selection
    void setTab(int t) { tab_ = std::min(std::max(t, 0), kTabs - 1); row_ = std::min(row_, std::max(0, rowCount() - 1)); }
    void setRow(int r) { row_ = std::min(std::max(r, 0), std::max(0, rowCount() - 1)); }
    void moveRow(int d) {
        const int n = rowCount();
        if (n > 0) row_ = ((row_ + d) % n + n) % n;
    }
    void moveTab(int d) { tab_ = ((tab_ + d) % kTabs + kTabs) % kTabs; row_ = std::min(row_, std::max(0, rowCount() - 1)); }

    // ---- changes. Each returns true if the value changed; the changes go to takeChanges().
    // dir -1 / +1 (a choice cycles, a range steps by `step`, or `big` when big is set)
    bool change(int dir, bool big = false) {
        const Item* s = selected();
        if (!s || s->kind == Kind::Info) return false;
        Item& it = items_[static_cast<size_t>(rows(tab_)[static_cast<size_t>(row_)])];
        if (it.kind == Kind::Choice) {
            const int n = static_cast<int>(it.choices.size());
            return n > 1 && setValue(it, ((it.value + dir) % n + n) % n);
        }
        return setValue(it, clampRange(it, it.value + dir * (big ? it.big : it.step)));
    }
    bool setSelectedValue(int v) {
        const Item* s = selected();
        if (!s || s->kind == Kind::Info) return false;
        Item& it = items_[static_cast<size_t>(rows(tab_)[static_cast<size_t>(row_)])];
        return setValue(it, it.kind == Kind::Choice ? std::min(std::max(v, 0), static_cast<int>(it.choices.size()) - 1) : clampRange(it, v));
    }
    bool toEnd(bool max) {
        const Item* s = selected();
        return s && s->kind == Kind::Range && setSelectedValue(max ? s->hi : s->lo);
    }
    // a value of a range row from a position on its track (grid x)
    static int valueAt(const Item& it, int x) {
        const R t = trackRect(0);
        const double f = std::min(1.0, std::max(0.0, static_cast<double>(x - t.x) / (t.w - 1)));
        int v = it.lo + static_cast<int>(std::lround(f * (it.hi - it.lo)));
        if (it.step > 1) v = it.lo + static_cast<int>(std::lround(static_cast<double>(v - it.lo) / it.step)) * it.step;
        return clampRange(it, v);
    }
    // sets by key (tests; also what the host does for the dependencies)
    bool set(const std::string& key, const std::string& text) {
        for (Item& it : items_) {
            if (it.key != key) continue;
            if (it.kind == Kind::Choice) {
                for (size_t i = 0; i < it.choices.size(); i++)
                    if (lower(it.choices[i].pref) == lower(text)) return setValue(it, static_cast<int>(i));
                return false;
            }
            return setValue(it, clampRange(it, static_cast<int>(std::lround(std::strtod(text.c_str(), nullptr) * it.div))));
        }
        return false;
    }
    std::vector<Change> takeChanges() {
        std::vector<Change> c;
        c.swap(pending_);
        return c;
    }
    bool hasChanges() const { return !pending_.empty(); }

private:
    static std::string lower(std::string s) {
        for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        return s;
    }
    static std::string shortName(const std::string& n) { return n.size() > 22 ? n.substr(0, 21) + "." : n; }
    static int clampRange(const Item& it, int v) { return std::min(it.hi, std::max(it.lo, v)); }
    Item& item(const std::string& key) {
        for (Item& it : items_) if (it.key == key) return it;
        static Item none;
        return none;
    }
    void clampRow() { row_ = std::min(row_, std::max(0, rowCount() - 1)); }

    // the one place a value changes: records the change and applies the rules between items
    bool setValue(Item& it, int v) {
        if (it.value == v) return false;
        it.value = v;
        pending_.push_back({it.key, prefText(it)});
        const std::string t = prefText(it);
        if (it.key == "panorama" && t == "original") { force("widescreen", "no"); force("picture", "original"); force("interface", "original"); }
        else if (it.key == "widescreen" && t == "yes") force("panorama", "smooth");
        else if (it.key == "widescreen" && t == "no") force("interface", "original");
        else if (it.key == "interface" && t == "overlay") { force("panorama", "smooth"); force("widescreen", "yes"); }
        else if (it.key == "picture" && t == "clean") { force("panorama", "smooth"); force("text", "sharp"); }
        else if (it.key == "text" && t == "original") force("picture", "original");
        return true;
    }
    void force(const std::string& key, const std::string& text) {
        for (Item& it : items_)
            if (it.key == key && it.kind == Kind::Choice)
                for (size_t i = 0; i < it.choices.size(); i++)
                    if (it.choices[i].pref == text && it.value != static_cast<int>(i)) {
                        it.value = static_cast<int>(i);
                        pending_.push_back({it.key, text});
                    }
    }

    void add(Item it) { items_.push_back(std::move(it)); }
    void info(int tab, const char* id, const char* label, const char* help) {
        Item it;
        it.key = id;            // "info:..." (not a preference)
        it.label = label;
        it.tab = tab;
        it.kind = Kind::Info;
        it.when = When::Fixed;
        it.help = help;
        items_.push_back(std::move(it));
    }
    void choice(int tab, const char* key, const char* label, When when, const char* help, std::vector<Choice> ch, int value = 0) {
        Item it;
        it.key = key;
        it.label = label;
        it.tab = tab;
        it.kind = Kind::Choice;
        it.when = when;
        it.help = help;
        it.choices = std::move(ch);
        it.value = value;
        items_.push_back(std::move(it));
    }
    void yesno(int tab, const char* key, const char* label, When when, const char* help, const char* no = "off", const char* yes = "on", int value = 0) {
        choice(tab, key, label, when, help, {{"no", no}, {"yes", yes}}, value);
    }
    void range(int tab, const char* key, const char* label, When when, const char* help, int lo, int hi, int step, int big, int value, const char* unit,
               int div = 1, const char* zero = "") {
        Item it;
        it.key = key;
        it.label = label;
        it.tab = tab;
        it.kind = Kind::Range;
        it.when = when;
        it.help = help;
        it.lo = lo;
        it.hi = hi;
        it.step = step;
        it.big = big;
        it.value = value;
        it.unit = unit;
        it.div = div;
        it.zeroText = zero;
        items_.push_back(std::move(it));
    }

    void build() {
        info(TabGame, "info:realism", "Realism of this game",
             "The realism level this game was started with. It cannot change while the game runs; a saved game continues under the level stored in it.");
        info(TabGame, "info:difficulty", "Difficulty of this game",
             "The difficulty this game was started with. It cannot change while the game runs; a saved game continues under the one stored in it.");
        info(TabGame, "info:fixes", "Fix list (developer)", "A list of fixes given with --fixes; it replaces the fixes of the realism level for this run.");
        items_.back().visible = false;
        choice(TabGame, "realism", "Realism, next new game", When::NextNewGame,
               "Faithful is the 1998 game, bug for bug; Corrected repairs its bugs; Closed loop adds a water cycle. Preselects the New game screen.",
               {{"faithful", "Faithful"}, {"corrected", "Corrected"}, {"closedloop", "Closed loop"}});
        choice(TabGame, "difficulty", "Difficulty, next new game", When::NextNewGame,
               "Guided gives room to learn, Original is the 1998 pressure, Survivor is harder. Preselects the New game screen.",
               {{"guided", "Guided"}, {"original", "Original"}, {"survivor", "Survivor"}}, 1);

        choice(TabPicture, "panorama", "Panorama", When::Now,
               "Original draws the panoramas as 1998 did. Smooth draws them at the window's resolution and glides while you turn.",
               {{"original", "original"}, {"smooth", "smooth"}});
        choice(TabPicture, "panorama-filter", "Panorama filter", When::Now,
               "How the smooth panorama is sampled: soft (linear) or hard-edged pixels (nearest). It matters only while Panorama is smooth.",
               {{"linear", "soft"}, {"nearest", "hard"}});
        yesno(TabPicture, "widescreen", "Widescreen", When::Now,
              "On a wide window the panoramas show more of the world beside the 4:3 picture. Switches Panorama to smooth, which it needs.");
        choice(TabPicture, "picture", "Picture", When::Now,
               "Clean takes the dither pattern out of the pictures and enlarges them smoothly. Switches Panorama to smooth and Text to sharp.",
               {{"original", "original"}, {"clean", "clean"}});
        choice(TabPicture, "text", "Text", When::Now,
               "Sharp draws the game's text at the window's resolution. Text already on the screen turns sharp when the game next draws it.",
               {{"original", "original"}, {"sharp", "sharp"}});
        choice(TabPicture, "interface", "Interface", When::Now,
               "Over the picture: the view fills the window, the bars show over it only when needed. Needs smooth panorama and widescreen.",
               {{"original", "bars (1998)"}, {"overlay", "over the picture"}});
        yesno(TabPicture, "fullscreen", "Full screen", When::Now, "Desktop full screen on the screen the window is on. Option+Return does the same.");
        choice(TabPicture, "display", "Screen", When::Now,
               "The screen the window is on. Changing it moves the window there; a full-screen window stays full screen.", {{"1", "1 (main)"}});

        choice(TabSound, "audio", "Sound", When::Now,
               "Original mixes the sounds as the 1998 code does; Enhanced converts the same sounds more cleanly, without clicks. The audio device is reopened.",
               {{"original", "original"}, {"enhanced", "enhanced"}});
        range(TabSound, "volume", "Master volume", When::Now, "Overall loudness, 0 to 100. The - and = keys change it too.", 0, 100, 5, 20, 100, " %");
        range(TabSound, "volume-effects", "Effects volume", When::Now, "Loudness of the sounds of the world: one-shot sounds, ambience, loops.", 0, 100, 5, 20, 100, " %");
        range(TabSound, "volume-movies", "Movies volume", When::Now, "Loudness of the sound of the films.", 0, 100, 5, 20, 100, " %");
        range(TabSound, "volume-terminal", "Terminal volume", When::Now, "Loudness of the BioSys 4 terminal's own sounds.", 0, 100, 5, 20, 100, " %");
        yesno(TabSound, "start-muted", "Muted", When::Now, "Silences all sound. The backslash key toggles it too.", "no", "yes");

        range(TabControls, "pass-ms", "Pace", When::Now,
              "Milliseconds per game step: lower is faster (turning, zooms, fades, animations), higher is slower. The default is 33.", 10, 100, 1, 10, 33, " ms");
        range(TabControls, "mouse-sensitivity", "Mouse sensitivity", When::Now, "How far the view turns for a mouse movement: 25 to 400 percent.", 25, 400, 5, 25, 100,
              " %", 100);
        yesno(TabControls, "pause-on-focus-loss", "Pause when the window loses focus", When::Now,
              "Pauses the game while another window has the focus. A minimised window always pauses.", "no", "yes", 1);

        yesno(TabSaving, "autosave", "Autosave", When::Now, "Saves by itself every few minutes of play, on waking up and when you quit. Keeps the last three.", "off", "on", 1);
        range(TabSaving, "autosave-minutes", "Minutes between autosaves", When::Now,
              "Minutes of play between two autosaves, counted from now; 0 = none by the clock (waking up and quitting still save).", 0, 1440, 1, 10, 10, " min", 1, "none");
        range(TabSaving, "save-pages", "Pages of save slots", When::Now,
              "Pages of ten slots in the save and load lists, 1 to 20 (Page Up and Page Down turn them). Takes effect when a list is next opened.", 1, 20, 1, 5, 4, "");
    }

    std::vector<Item> items_;
    std::vector<Change> pending_;
    int tab_ = 0, row_ = 0;
    bool persistent_ = true;
};

// ---------------------------------------------------------------------------------------------------------------- new game
struct NewGameOption {
    std::string pref, name, text;
};
inline const std::vector<NewGameOption>& realismOptions() {
    static const std::vector<NewGameOption> o = {
        {"faithful", "Faithful", "The 1998 game, bug for bug. With Original difficulty it is exactly the game of 1998."},
        {"corrected", "Corrected", "Repairs the original's evident bugs and three model errors; the rules stay."},
        {"closedloop", "Closed loop", "Corrected, plus a closed water and humidity cycle. Can be harder than 1998."}};
    return o;
}
inline const std::vector<NewGameOption>& difficultyOptions() {
    static const std::vector<NewGameOption> o = {
        {"guided", "Guided", "Half the drain and damage, better start values, threat only after 14 days."},
        {"original", "Original", "The 1998 pressure, unchanged. The threat from outside comes after 7 days."},
        {"survivor", "Survivor", "Harder: 25 percent faster drain, 50 percent more damage, threat after 5 days."}};
    return o;
}
inline const char* newGameNote() {
    return "Faithful + Original is the 1998 game. The realism levels were measured but not play-tested for long, and the balance of Guided and "
           "Survivor is unproven. The choice is stored in the saves you make and remembered for the next new game.";
}

class NewGameModel {
public:
    enum Focus { FocusRealism, FocusDifficulty, FocusStart, FocusBack, kFoci };
    enum class Outcome { None, Start, Back };
    void begin(const std::string& realism, const std::string& difficulty) {
        realism_ = indexOf(realismOptions(), realism, 0);
        difficulty_ = indexOf(difficultyOptions(), difficulty, 1);
        focus_ = FocusStart;
        outcome_ = Outcome::None;
    }
    std::string realism() const { return realismOptions()[static_cast<size_t>(realism_)].pref; }
    std::string difficulty() const { return difficultyOptions()[static_cast<size_t>(difficulty_)].pref; }
    int realismIndex() const { return realism_; }
    int difficultyIndex() const { return difficulty_; }
    int focus() const { return focus_; }
    Outcome outcome() const { return outcome_; }
    void clearOutcome() { outcome_ = Outcome::None; }
    void select(int col, int i) {
        (col == 0 ? realism_ : difficulty_) = std::min(2, std::max(0, i));
        focus_ = col == 0 ? FocusRealism : FocusDifficulty;
    }
    void moveFocus(int d) { focus_ = ((focus_ + d) % kFoci + kFoci) % kFoci; }
    void setFocus(int f) { focus_ = f; }
    void start() { outcome_ = Outcome::Start; }
    void back() { outcome_ = Outcome::Back; }
    // Up / Down inside the focused column
    bool moveChoice(int d) {
        if (focus_ == FocusRealism) { const int v = std::min(2, std::max(0, realism_ + d)); const bool c = v != realism_; realism_ = v; return c; }
        if (focus_ == FocusDifficulty) { const int v = std::min(2, std::max(0, difficulty_ + d)); const bool c = v != difficulty_; difficulty_ = v; return c; }
        return false;
    }

private:
    static int indexOf(const std::vector<NewGameOption>& o, const std::string& pref, int def) {
        for (size_t i = 0; i < o.size(); i++) if (o[i].pref == pref) return static_cast<int>(i);
        return def;
    }
    int realism_ = 0, difficulty_ = 1, focus_ = FocusStart;
    Outcome outcome_ = Outcome::None;
};

// ---------------------------------------------------------------------------------------------------------------- the controller
enum class MKey { Up, Down, Left, Right, Tab, Return, Escape, F10, PageUp, PageDown, Home, End };

class Controller {
public:
    enum class Mode { Closed, Settings, NewGame };
    Model model;
    NewGameModel newGame;

    Mode mode() const { return mode_; }
    bool isOpen() const { return mode_ != Mode::Closed; }
    // incremented by everything that changes what is drawn
    uint64_t revision() const { return rev_; }
    int hoverRow() const { return hoverRow_; }
    int hoverTab() const { return hoverTab_; }
    int hoverButton() const { return hoverBtn_; }      // settings: 1 = Close; new game: 1 = Start, 2 = Back
    bool dragging() const { return drag_; }

    void openSettings() { mode_ = Mode::Settings; drag_ = false; resetHover(); rev_++; }
    void openNewGame(const std::string& realism, const std::string& difficulty) {
        newGame.begin(realism, difficulty);
        mode_ = Mode::NewGame;
        drag_ = false;
        resetHover();
        rev_++;
    }
    void close() { mode_ = Mode::Closed; drag_ = false; resetHover(); rev_++; }

    // a key went down; true = the menu used it (always, while open)
    bool key(MKey k, bool shift = false) {
        if (mode_ == Mode::Closed) return false;
        rev_++;
        if (mode_ == Mode::NewGame) { ngKey(k, shift); return true; }
        switch (k) {
        case MKey::Escape: case MKey::F10: close(); break;
        case MKey::Up: model.moveRow(-1); break;
        case MKey::Down: model.moveRow(1); break;
        case MKey::Left: model.change(-1, shift); break;
        case MKey::Right: model.change(1, shift); break;
        case MKey::PageUp: model.change(1, true); break;
        case MKey::PageDown: model.change(-1, true); break;
        case MKey::Home: model.toEnd(false); break;
        case MKey::End: model.toEnd(true); break;
        case MKey::Tab: model.moveTab(shift ? -1 : 1); break;
        case MKey::Return: {
            const Item* s = model.selected();
            if (s && s->kind == Kind::Choice) model.change(1);
            break;
        }
        }
        return true;
    }

    void mouseDown(int x, int y) {
        if (mode_ == Mode::Closed) return;
        rev_++;
        if (mode_ == Mode::NewGame) { ngClick(x, y); return; }
        if (closeRect().has(x, y)) { close(); return; }
        for (int t = 0; t < kTabs; t++)
            if (tabRect(t).has(x, y)) { model.setTab(t); return; }
        const int r = rowAt(x, y);
        if (r < 0) return;
        model.setRow(r);
        const Item* s = model.selected();
        if (!s || s->kind == Kind::Info) return;
        if (s->kind == Kind::Choice) {
            if (arrowLRect(r).has(x, y)) model.change(-1);
            else if (x >= kValX0) model.change(1);          // the right arrow and the value cycle forward; the label only selects the row
        } else if (x >= trackRect(r).x - 2 && x < trackRect(r).x + trackRect(r).w + 6) {
            drag_ = true;
            model.setSelectedValue(Model::valueAt(*s, x));
        }
    }
    void mouseMove(int x, int y) {
        if (mode_ == Mode::Closed) return;
        const int hr = mode_ == Mode::Settings ? rowAt(x, y) : -1;
        int ht = -1, hb = 0;
        if (mode_ == Mode::Settings) {
            for (int t = 0; t < kTabs; t++) if (tabRect(t).has(x, y)) ht = t;
            if (closeRect().has(x, y)) hb = 1;
        } else {
            if (ngStartRect().has(x, y)) hb = 1;
            if (ngBackRect().has(x, y)) hb = 2;
        }
        if (hr != hoverRow_ || ht != hoverTab_ || hb != hoverBtn_) { hoverRow_ = hr; hoverTab_ = ht; hoverBtn_ = hb; rev_++; }
        if (drag_ && mode_ == Mode::Settings) {
            const Item* s = model.selected();
            if (s && s->kind == Kind::Range && model.setSelectedValue(Model::valueAt(*s, x))) rev_++;
        }
    }
    void mouseUp(int, int) { if (drag_) { drag_ = false; rev_++; } }
    // dir > 0: up / away from the user
    void wheel(int x, int y, int dir) {
        if (mode_ != Mode::Settings || dir == 0) return;
        rev_++;
        const int r = rowAt(x, y);
        if (r >= 0) model.setRow(r);
        model.change(dir > 0 ? 1 : -1);
    }

    // where a row is on screen: the index in the tab's visible rows, or -1
    int rowAt(int x, int y) const {
        if (mode_ != Mode::Settings) return -1;
        const int n = model.rowCount();
        for (int r = 0; r < n && r < kMaxRows; r++)
            if (rowRect(r).has(x, y)) return r;
        return -1;
    }

private:
    void resetHover() { hoverRow_ = hoverTab_ = -1; hoverBtn_ = 0; }
    void ngKey(MKey k, bool shift) {
        NewGameModel& n = newGame;
        switch (k) {
        case MKey::Escape: n.back(); break;
        case MKey::F10: break;
        case MKey::Up: n.moveChoice(-1); break;
        case MKey::Down: n.moveChoice(1); break;
        case MKey::Left: n.setFocus(NewGameModel::FocusRealism); break;
        case MKey::Right: n.setFocus(NewGameModel::FocusDifficulty); break;
        case MKey::Tab: n.moveFocus(shift ? -1 : 1); break;
        case MKey::Return:
            if (n.focus() == NewGameModel::FocusBack) n.back();
            else n.start();
            break;
        default: break;
        }
    }
    void ngClick(int x, int y) {
        for (int col = 0; col < 2; col++)
            for (int i = 0; i < 3; i++)
                if (ngBlockRect(col, i).has(x, y)) { newGame.select(col, i); return; }
        if (ngStartRect().has(x, y)) newGame.start();
        else if (ngBackRect().has(x, y)) newGame.back();
    }

    Mode mode_ = Mode::Closed;
    uint64_t rev_ = 1;
    int hoverRow_ = -1, hoverTab_ = -1, hoverBtn_ = 0;
    bool drag_ = false;
};

} // namespace smenu
} // namespace biosys
