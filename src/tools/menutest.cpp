// Unit checks of the settings menu's model and input handling (app/settingsmenu.h), of the save trailer (engine/savetrailer.h) and of the
// settings text inside it (app/savesettings.h). No window, no engine: build/biosys-menutest.
#include "app/savesettings.h"
#include "app/settingsmenu.h"
#include "engine/savetrailer.h"
#include <cstdio>
#include <string>

using namespace biosys;
using namespace biosys::smenu;

static int passed = 0, failed = 0;
static void check(const char* name, bool ok) {
    if (ok) { passed++; std::printf("PASS %s\n", name); }
    else { failed++; std::printf("FAIL %s\n", name); }
}

static bool hasChange(const std::vector<Change>& c, const char* key, const char* text) {
    for (const Change& x : c)
        if (x.key == key && x.text == text) return true;
    return false;
}

static int selectKey(Model& m, const char* key) {
    for (int t = 0; t < kTabs; t++) {
        m.setTab(t);
        for (int r = 0; r < m.rowCount(); r++) {
            m.setRow(r);
            if (m.selected() && m.selected()->key == key) return 1;
        }
    }
    return 0;
}

int main() {
    // ---- the rows
    {
        Model m;
        m.setTab(TabGame);
        check("Game tab: 4 rows (the fix list stays hidden without fixes)", m.rowCount() == 4);
        m.setRunning("corrected", "guided", "restored");
        check("Game tab: 5 rows with a fix list", m.rowCount() == 5);
        m.setRunning("closedloop", "survivor", "");
        check("running game: names", m.at(TabGame, 0).info == "Closed loop" && m.at(TabGame, 1).info == "Survivor");
        const int counts[kTabs] = {4, 8, 6, 3, 3};
        bool ok = true;
        for (int t = 0; t < kTabs; t++) { m.setTab(t); ok = ok && m.rowCount() == counts[t] && m.rowCount() <= kMaxRows; }
        check("rows per tab (Game 4, Picture 8, Sound 6, Controls 3, Saving 3), all fit the panel", ok);
        // every row but the read-only ones has a help text, a key and a known apply time; only the next-new-game rows are not "now"
        bool help = true, when = true;
        for (const Item& it : m.items()) {
            help = help && !it.help.empty() && !it.label.empty();
            if (it.kind == Kind::Info) when = when && it.when == When::Fixed;
            else if (it.key == "realism" || it.key == "difficulty") when = when && it.when == When::NextNewGame;
            else when = when && it.when == When::Now;
        }
        check("every row has a label and an explanation", help);
        check("apply times: info rows fixed, realism / difficulty next new game, the rest now", when);
        check("the words of apply times", std::string(whenText(When::Now)) == "applies now" && std::string(whenText(When::NextStart)) == "applies at the next start" &&
                                              std::string(whenText(When::NextNewGame)) == "applies to the next new game");
    }
    // ---- loading and changing
    {
        Model m;
        m.load({{"volume", "70"}, {"panorama", "smooth"}, {"mouse-sensitivity", "1.25"}, {"autosave", "no"}, {"pass-ms", "20"}, {"realism", "closedloop"},
                {"nonsense", "1"}, {"volume-movies", "250"}});
        check("load sets choices, ranges, the factor in percent; unknown keys are ignored; out of range is clamped",
              m.find("volume")->value == 70 && m.find("panorama")->value == 1 && m.find("mouse-sensitivity")->value == 125 && m.find("autosave")->value == 0 &&
                  m.find("pass-ms")->value == 20 && m.find("realism")->value == 2 && m.find("volume-movies")->value == 100);
        check("loading makes no change", !m.hasChanges());
        check("text of values", Model::valueText(*m.find("volume")) == "70 %" && Model::valueText(*m.find("panorama")) == "smooth" &&
                                    Model::prefText(*m.find("mouse-sensitivity")) == "1.25" && Model::prefText(*m.find("pass-ms")) == "20" &&
                                    Model::prefText(*m.find("autosave")) == "no");
        selectKey(m, "volume");
        check("right on a range steps by 5", m.change(1) && m.find("volume")->value == 75);
        check("big step by 20", m.change(1, true) && m.find("volume")->value == 95);
        check("clamped at 100: a change that does not change is no change", m.change(1) && m.find("volume")->value == 100 && !m.change(1));
        const std::vector<Change> c = m.takeChanges();
        check("changes carry key and preference text", c.size() == 3 && c[0].key == "volume" && c[0].text == "75" && c[2].text == "100");
        check("taken changes are gone", !m.hasChanges());
        selectKey(m, "pause-on-focus-loss");
        check("left / right on a two-way choice toggle", m.change(-1) && m.find("pause-on-focus-loss")->value == 0 && m.change(1) && m.find("pause-on-focus-loss")->value == 1);
        selectKey(m, "realism");
        check("a three-way choice cycles", m.change(1) && m.find("realism")->value == 0 && m.change(-1) && m.find("realism")->value == 2);
        m.takeChanges();
        selectKey(m, "info:realism");
        check("a read-only row cannot be changed", !m.change(1) && !m.hasChanges());
        selectKey(m, "mouse-sensitivity");
        m.setSelectedValue(100);
        check("sensitivity 100 percent is the factor 1", Model::prefText(*m.find("mouse-sensitivity")) == "1");
        check("set by key and text", m.set("autosave-minutes", "30") && m.find("autosave-minutes")->value == 30 && !m.set("autosave-minutes", "30") && !m.set("nothing", "1"));
        m.setScreens({"Built-in Retina Display", "DELL U3419W"});
        check("screens: one choice per screen, numbers as preference text", m.find("display")->choices.size() == 2 && m.find("display")->choices[1].pref == "2" &&
                                                                          m.find("display")->choices[0].label.find("main") != std::string::npos);
    }
    // ---- the rules between the picture items
    {
        Model m;
        selectKey(m, "widescreen");
        m.takeChanges();
        m.change(1);
        std::vector<Change> c = m.takeChanges();
        check("widescreen on switches panorama to smooth", hasChange(c, "widescreen", "yes") && hasChange(c, "panorama", "smooth"));
        selectKey(m, "picture");
        m.change(1);
        c = m.takeChanges();
        check("clean picture needs smooth panorama (already) and sharp text", hasChange(c, "picture", "clean") && hasChange(c, "text", "sharp") && !hasChange(c, "panorama", "smooth"));
        selectKey(m, "text");
        m.change(-1);
        c = m.takeChanges();
        check("original text takes the clean picture away", hasChange(c, "text", "original") && hasChange(c, "picture", "original"));
        selectKey(m, "panorama");
        m.change(-1);
        c = m.takeChanges();
        check("original panorama takes widescreen away", hasChange(c, "panorama", "original") && hasChange(c, "widescreen", "no"));
        check("... and nothing else when nothing else was on", c.size() == 2);
    }
    // ---- the controller: keys
    {
        Controller c;
        check("closed: no key is used", !c.key(MKey::Down));
        c.openSettings();
        check("open: starts on the Game tab, first row", c.isOpen() && c.model.tab() == TabGame && c.model.row() == 0);
        c.key(MKey::Down);
        check("down moves a row", c.model.row() == 1);
        c.key(MKey::Up);
        c.key(MKey::Up);
        check("up wraps", c.model.row() == c.model.rowCount() - 1);
        c.key(MKey::Tab);
        check("tab goes to the next tab", c.model.tab() == TabPicture);
        c.key(MKey::Tab, true);
        c.key(MKey::Tab, true);
        check("shift+tab goes back and wraps", c.model.tab() == TabSaving);
        c.model.setTab(TabSound);
        c.model.setRow(1);
        c.key(MKey::Left);
        check("left on a slider lowers it", c.model.find("volume")->value == 95);
        c.key(MKey::Left, true);
        check("shift+left by 20", c.model.find("volume")->value == 75);
        c.key(MKey::End);
        check("end: the top", c.model.find("volume")->value == 100);
        c.key(MKey::Home);
        check("home: zero", c.model.find("volume")->value == 0);
        c.model.setRow(5);
        c.key(MKey::Return);
        check("return toggles a switch", c.model.find("start-muted")->value == 1);
        c.key(MKey::F10);
        check("F10 closes", !c.isOpen());
        c.openSettings();
        c.key(MKey::Escape);
        check("Esc closes", !c.isOpen());
        c.openSettings();
        const uint64_t r0 = c.revision();
        c.key(MKey::Down);
        check("every input changes the revision (the layer is redrawn)", c.revision() != r0);
    }
    // ---- the controller: mouse
    {
        Controller c;
        c.openSettings();
        c.mouseDown(tabRect(2).x + 5, tabRect(2).y + 5);
        check("click on a tab", c.model.tab() == TabSound);
        const R row0 = rowRect(0);
        c.mouseDown(row0.x + 20, rowRect(2).y + 5);
        check("click on a row's label selects it and changes nothing", c.model.row() == 2 && !c.model.hasChanges());
        c.mouseDown(row0.x + 20, row0.y + 5);
        check("... the first row of Sound is a choice", c.model.row() == 0 && c.model.find("audio")->value == 0);
        c.mouseDown((kValX0 + kValX1) / 2, row0.y + 5);
        check("click on the value cycles a choice", c.model.find("audio")->value == 1);
        c.mouseDown(arrowLRect(0).x + 3, arrowLRect(0).y + 3);
        check("click on the left arrow goes back", c.model.find("audio")->value == 0);
        c.mouseDown(arrowRRect(0).x + 3, arrowRRect(0).y + 3);
        check("click on the right arrow goes forward", c.model.find("audio")->value == 1);
        c.model.takeChanges();
        const R tr = trackRect(1);
        c.mouseDown(tr.x + tr.w / 2, tr.y + 5);
        check("click on a slider track sets it there (about half)", c.dragging() && c.model.row() == 1 && c.model.find("volume")->value >= 45 && c.model.find("volume")->value <= 55);
        c.mouseMove(tr.x + tr.w + 30, tr.y + 5);
        check("dragging past the end gives the top", c.model.find("volume")->value == 100);
        c.mouseMove(tr.x - 40, tr.y + 5);
        check("dragging past the start gives zero", c.model.find("volume")->value == 0);
        c.mouseUp(0, 0);
        check("release ends the drag", !c.dragging());
        c.mouseMove(tr.x + tr.w / 2, tr.y + 5);
        check("moving without the button does not change a slider", c.model.find("volume")->value == 0);
        c.wheel(row0.x + 20, rowRect(2).y + 5, 1);
        check("the wheel changes the row under the pointer", c.model.row() == 2 && c.model.find("volume-effects")->value == 100 && c.model.find("volume")->value == 0);
        c.wheel(row0.x + 20, rowRect(2).y + 5, -1);
        check("wheel down lowers it", c.model.find("volume-effects")->value == 95);
        c.mouseDown(5, 5);
        check("a click outside the panel does nothing", c.isOpen() && c.model.row() == 2);
        c.mouseDown(closeRect().x + 5, closeRect().y + 5);
        check("the Close button closes", !c.isOpen());
        check("a closed menu ignores the mouse", (c.mouseDown(100, 100), !c.isOpen()));
        check("the hover follows the pointer", [] {
            Controller d;
            d.openSettings();
            d.mouseMove(rowRect(1).x + 5, rowRect(1).y + 5);
            const bool a = d.hoverRow() == 1;
            d.mouseMove(tabRect(3).x + 5, tabRect(3).y + 5);
            return a && d.hoverRow() == -1 && d.hoverTab() == 3;
        }());
    }
    // ---- the layout
    {
        const R p = panelRect(), n = ngPanelRect();
        check("panels are centred on the 640 x 480 grid", p.x * 2 + p.w == kGridW && n.x * 2 + n.w == kGridW);
        check("panels lie inside the grid", p.y >= 0 && p.y + p.h <= kGridH && n.y >= 0 && n.y + n.h <= kGridH);
        bool in = true;
        for (int t = 0; t < kTabs; t++) in = in && tabRect(t).x >= p.x && tabRect(t).x + tabRect(t).w <= p.x + p.w;
        for (int r = 0; r < kMaxRows; r++) in = in && rowRect(r).y + rowRect(r).h <= kFootY - 4;
        in = in && closeRect().x + closeRect().w <= p.x + p.w;
        check("tabs, rows and the Close button lie inside the panel; rows end above the explanation", in);
        bool ng = true;
        for (int col = 0; col < 2; col++)
            for (int i = 0; i < 3; i++) {
                const R b = ngBlockRect(col, i);
                ng = ng && b.x >= n.x && b.x + b.w <= n.x + n.w && b.y + b.h <= kNoteY;
            }
        ng = ng && ngStartRect().y + ngStartRect().h <= n.y + n.h && ngBackRect().x + ngBackRect().w <= n.x + n.w;
        check("the new-game blocks and buttons lie inside its panel", ng);
    }
    // ---- the new-game screen
    {
        Controller c;
        c.openNewGame("faithful", "original");
        check("preselection from the options", c.mode() == Controller::Mode::NewGame && c.newGame.realism() == "faithful" && c.newGame.difficulty() == "original" &&
                                                   c.newGame.focus() == NewGameModel::FocusStart);
        c.key(MKey::Return);
        check("return with the defaults starts with the defaults", c.newGame.outcome() == NewGameModel::Outcome::Start && c.newGame.realism() == "faithful" && c.newGame.difficulty() == "original");
        c.openNewGame("corrected", "guided");
        check("another preselection", c.newGame.realism() == "corrected" && c.newGame.difficulty() == "guided");
        c.key(MKey::Left);
        c.key(MKey::Down);
        check("left focuses realism, down picks the next one", c.newGame.realism() == "closedloop");
        c.key(MKey::Down);
        check("the list stops at its end", c.newGame.realism() == "closedloop");
        c.key(MKey::Right);
        c.key(MKey::Down);
        check("right focuses difficulty", c.newGame.difficulty() == "original");
        c.key(MKey::Tab);
        check("tab goes on to Start", c.newGame.focus() == NewGameModel::FocusStart);
        c.key(MKey::Tab);
        check("... then Back; return there is Back", c.newGame.focus() == NewGameModel::FocusBack && (c.key(MKey::Return), c.newGame.outcome() == NewGameModel::Outcome::Back));
        c.openNewGame("faithful", "original");
        c.key(MKey::Escape);
        check("Esc is Back", c.newGame.outcome() == NewGameModel::Outcome::Back);
        c.openNewGame("faithful", "original");
        c.key(MKey::F10);
        check("F10 does nothing on this screen", c.isOpen() && c.newGame.outcome() == NewGameModel::Outcome::None);
        c.mouseDown(ngBlockRect(1, 2).x + 10, ngBlockRect(1, 2).y + 10);
        check("click on a block selects it", c.newGame.difficulty() == "survivor" && c.newGame.focus() == NewGameModel::FocusDifficulty);
        c.mouseDown(ngBlockRect(0, 1).x + 10, ngBlockRect(0, 1).y + 10);
        check("click on the other column", c.newGame.realism() == "corrected");
        c.mouseDown(ngStartRect().x + 5, ngStartRect().y + 5);
        check("click on Start", c.newGame.outcome() == NewGameModel::Outcome::Start && c.newGame.realism() == "corrected" && c.newGame.difficulty() == "survivor");
        c.openNewGame("bogus", "bogus");
        check("an unknown preselection falls back to the defaults", c.newGame.realism() == "faithful" && c.newGame.difficulty() == "original");
        check("the texts: three options each, the 1998 game is named", realismOptions().size() == 3 && difficultyOptions().size() == 3 &&
                                                                     std::string(newGameNote()).find("Faithful + Original is the 1998 game") != std::string::npos &&
                                                                     std::string(newGameNote()).find("play-tested") != std::string::npos);
        check("the mouse and keys of a closed screen do nothing", [] {
            Controller d;
            d.mouseDown(ngStartRect().x + 5, ngStartRect().y + 5);
            return !d.isOpen() && d.newGame.outcome() == NewGameModel::Outcome::None;
        }());
    }
    // ---- the trailer of a save
    {
        const std::string text = settingsText(SettingsText{"corrected", "guided", "restored,-rainWashesPollution"});
        const std::vector<uint8_t> block = savetrailer::make(text);
        std::vector<uint8_t> file(1000, 0x5a);
        file.insert(file.end(), block.begin(), block.end());
        std::string got, why;
        size_t end = 0;
        check("a block is found at the end of the blob", savetrailer::inspect(file, 1000, got, why, &end) == savetrailer::Status::Valid && got == text && end == file.size());
        SettingsText back;
        check("the text parses back", parseSettingsText(got, back) && back == SettingsText{"corrected", "guided", "restored,-rainWashesPollution"});
        check("the blob ends where the block starts", savetrailer::blobEnd(file, 900, 100) == 1000);
        check("a file without a block: the whole file", savetrailer::blobEnd(std::vector<uint8_t>(1000, 1), 900, 100) == 1000 &&
                                                             savetrailer::blobEnd(std::vector<uint8_t>(1005, 1), 900, 100) == 1005);
        std::vector<uint8_t> cut(file.begin(), file.end() - 9);
        check("a cut block is damaged, and the blob still ends before it", savetrailer::inspect(cut, 1000, got, why) == savetrailer::Status::Damaged && savetrailer::blobEnd(cut, 900, 100) == 1000);
        std::vector<uint8_t> few(file.begin(), file.begin() + 1003);
        few[1000] = 'B'; few[1001] = 'S'; few[1002] = 'Y';
        check("three bytes of the magic are a damaged block too", savetrailer::inspect(few, 1000, got, why) == savetrailer::Status::Damaged);
        std::vector<uint8_t> flip = file;
        flip[1020] ^= 1;
        check("a flipped bit fails the check sum", savetrailer::inspect(flip, 1000, got, why) == savetrailer::Status::Damaged && why.find("check sum") != std::string::npos);
        std::vector<uint8_t> big = file;
        big[1012] = 0xff; big[1013] = 0xff;
        check("a length that is too big is damaged, never read", savetrailer::inspect(big, 1000, got, why) == savetrailer::Status::Damaged);
        std::vector<uint8_t> ver = file;
        ver[1008] = 2;
        check("another version is damaged (not understood)", savetrailer::inspect(ver, 1000, got, why) == savetrailer::Status::Damaged);
        std::vector<uint8_t> junk(file.begin(), file.begin() + 1000);
        for (int i = 0; i < 40; i++) junk.push_back(static_cast<uint8_t>(i * 37));
        check("other bytes are not our block", savetrailer::inspect(junk, 1000, got, why) == savetrailer::Status::Absent);
        check("nothing after the blob: absent", savetrailer::inspect(std::vector<uint8_t>(1000, 0), 1000, got, why) == savetrailer::Status::Absent);
        check("settings text with an unknown word is refused", !parseSettingsText("biosys-settings 1\nrealism=nonsense\ndifficulty=original\nfixes=\n", back));
        check("settings text of another version is refused", !parseSettingsText("biosys-settings 2\nrealism=faithful\ndifficulty=original\nfixes=\n", back));
        check("settings text round trip (defaults)", parseSettingsText(settingsText(SettingsText()), back) && back == SettingsText());
    }
    std::printf("%s: %d checks passed, %d failed\n", failed ? "FAILED" : "menutest", passed, failed);
    return failed ? 1 : 0;
}
