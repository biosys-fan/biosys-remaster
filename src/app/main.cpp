// SDL3 front end of the Biosys port. Option parsing, the headless paths (--screenshot, --script,
// --selftest-all-pages) run before any SDL call.
//
// Key map (live window)
//   mouse           left button release = hotspot / pick up an object / bar icon (spec 1.5, 1.6.8)
//                   On pages larger than the view the pointer is captured: moving the mouse scrolls
//                   (spec 5.2). Hold Ctrl for a free pointer (top bar). Shift toggles the bar mode
//                   (pointer pinned to the bar, mouse x scrolls the bar); moving into the bar enters it too.
//   S               stash the held object on the bar (nothing held: take off the first worn object)
//   D               drop the held object into the nearest free zone (over the bar: the object under the pointer)
//   Space           use the held object / wear a wearable / take the object under the pointer from the bar
//   Tab (hold)      personal status          Caps Lock       plant and environment status (see StatusHold: the lock being ON
//                                                  is the key being held), the backquote key holds it too
//   Esc             close popup / menu (the viewer: quit)    F5 / F6  save / load        Alt+Return  full screen (not in a text entry)
//   F10 / Cmd+,     the settings menu (also on the main menu) and, in the original start-up flow, New game asks for realism and difficulty first
//   - / =           master volume down / up (10 %), \\ mutes; the level shows in the title for 1.5 s; not in a menu or text entry
//   Developer keys, only with --dev (they are leftovers of the page viewer and can wreck a game):
//   arrow keys      scroll the view (debug aid)           F2   start a new game        Backspace  page history back
//   G               goto page (type name or number, Enter; Esc cancels)
//   [ and ]         previous / next page in PAGES.DAT     H   hotspot outlines    O   show hidden ornaments
//   P / L           next / previous palette set           1..6  time slot night dawn day dusk cloud goggles
//   M               (debug) play the next movie of movies\intro centred in the view, with the palette of the same
//                   base name under palettes\movies\intro when present (else the current palette)
//   N               (debug) play the looping test sprite anims\sensor\alarm.tan, page-relative, at the pointer;
//                   Shift+N: fixed on the screen position instead
//   command line    --movie FILE [--movie-palette FILE] plays that movie centred at start; --mute: no audio device
//   The window losing focus pauses sounds, movies and animations; regaining it resumes them.
#include "app/autosave.h"
#include "app/firstrun.h"
#include "install/importer.h"
#include "app/menulogic.h"
#include "app/prefs.h"
#include "app/savesettings.h"
#include "app/settingsmenu.h"
#include "app/settingsmenu_draw.h"
#include "app/pacing.h"
#include "app/clean.h"
#include "app/panorama_smooth.h"
#include "app/overlay_sdl.h"
#include "engine/panowide.h"
#include "engine/savetrailer.h"
#include "app/text_sharp_sdl.h"
#include "app/script.h"
#include "app/windowlogic.h"
#include "engine/engine.h"
#include "app/audio_sdl.h"
#include "engine/framebuffer.h"
#include "game/apilog.gen.h"
#include "game/biosgame.h"
#include "game/savegame.h"
#include "game/settings.h"
#include <filesystem>
#include <SDL3/SDL.h>
#include <algorithm>
#include <dirent.h>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <sys/stat.h>

using namespace biosys;

bool readTextFile(const std::string& path, std::string& out);
bool writeTextFile(const std::string& path, const std::string& text);

namespace {

struct Options {
    std::string page;                 // empty = the page of the new game
    int slot = SLOT_DAY;
    bool haveScroll = false;
    int scrollX = 0, scrollY = 0;
    bool hotspots = false;
    std::string screenshot;
    bool selftest = false;
    std::string script;
    bool haveScript = false;
    bool logEvents = false, logIdle = false;
    bool noObjects = false;
    bool mute = false;
    bool audioEnhanced = false;       // --audio original|enhanced (engine/audio.h AudioMode)
    int volMaster = 100, volEffects = 100, volMovies = 100, volTerminal = 100;   // --volume / --volume-effects / --volume-movies / --volume-terminal
    bool game = false, logApi = false;   // --game: the game logic (BiosGame); --log-api: print every EngineApi call it makes
    uint32_t seed = 1;                   // --seed N: the C runtime seed of the game logic
    std::string saveDir;                 // --save-dir DIR: where "Saved Game N.sav" goes (default: SDL's pref path)
    bool intro = false, skipIntro = false, menuLogic = false;   // --intro / --skip-intro (with --game), --menu-logic
    std::string movie, moviePalette;   // --movie / --movie-palette
    std::string realism = "faithful", difficulty = "original", fixes;   // --realism / --difficulty / --fixes (src/game/settings.h)
    game::Settings settings;          // the three parsed (default = the faithful translation)
    Bs4Fixes bs4Fixes;                // the terminal's own bug fixes (engine/bs4fixes.h), set by the realism level / --fixes
    bool dev = false;                 // --dev: the developer keys of the window (page viewer leftovers)
    bool fullscreen = false;          // --fullscreen
    std::string display;              // --display N|NAME|list: the screen the window opens on (windowlogic.h pickDisplay); empty = the main screen
    uint32_t passMs = kDefaultPassMs; // --pass-ms N: the interval of the engine's idle passes (app/pacing.h)
    bool smoothPanorama = false;      // --panorama smooth: the high-resolution presentation of the panorama view (app/panorama_smooth.h)
    bool panoramaLinear = true;       // --panorama-filter linear|nearest
    bool widescreen = false;          // --widescreen: panorama pages use the window's full width (engine/panowide.h); implies smooth panoramas
    bool interfaceOverlay = false;    // --interface overlay: the view fills the window, the bars are overlays (app/overlaylayout.h); implies smooth panoramas and widescreen
    bool textSharp = false;           // --text sharp: the GDI text at the window's real resolution (app/text_sharp.h)
    bool pictureClean = false;        // --picture clean: de-dither + Catmull-Rom enlargement of the pictures (app/clean.h); implies smooth panoramas and sharp text
    int cleanTexScale = 2;            // --clean-texture N (developer): the cleaned panorama texture is the page times N (1 or 2)
    int cleanSlew = 1;                // --clean-slew N (developer): the partner weights' slew per palette change in 1/32 (0 = the reference's decisions every time)
    bool help = false;
    std::vector<std::pair<std::string, std::string>> fruit;   // ornament -> object (test GameLogic)
    bool startMuted = false;          // --start-muted: the mixer starts muted (the backslash key toggles it)
    bool autosave = true;             // --autosave yes|no (app/autosave.h)
    int autosaveMinutes = kDefaultAutosaveMinutes;   // --autosave-minutes N, 0 = no timed autosave
    int loadAutosave = 0;             // --load-autosave [1|2|3]: start in that autosave (-1 = the newest), 0 = no
    int savePages = 4;                // --save-pages N: pages of save slots (Engine::setSlotPages)
    bool pauseOnFocusLoss = true;     // --pause-on-focus-loss yes|no (windowlogic.h PauseState)
    double mouseSensitivity = 1.0;    // --mouse-sensitivity F (windowlogic.h MotionScaler)
    std::string configFile;           // --config FILE: this preferences file (also for --script / --screenshot)
    bool noConfig = false;            // --no-config: do not read or write the preferences file
    bool writeConfig = false;         // --write-config: write the effective settings to the preferences file and exit
    std::string configPath;           // the preferences file in use ("" = none): set by main(), saved to at exit
    uint32_t quitAfterMs = 0;         // --quit-after-ms N (test aid): close the window after N ms
    int windowW = 0, windowH = 0;     // --window-size WxH (test aid): the window's size in pixels
    std::string dataHome;             // --data-home DIR: where the imported copy of the two CDs lives (default: <save folder>/gamedata)
    bool firstrunOnly = false;        // --firstrun-only (test aid): show the first-run screen only (never BIOSYS_DATA / development roots); exit 0 instead of starting the game
    bool firstrunLog = false;         // --firstrun-log (test aid): one line per state change of the first-run screen on stdout
    std::string firstrunShot;         // --firstrun-shot FILE.bmp (test aid): the first-run window's picture at 800 ms and when it closes
    std::vector<std::string> testDrops;   // --test-drop PATH (test aid, repeatable): pushed to the first-run window as real drop events from 1 s on, 300 ms apart
    std::string testShot;             // --test-shot FILE.bmp (test aid, with --quit-after-ms): the window's picture shortly before it closes
    std::string testKeys;             // --test-keys LIST (test aid): key presses (names, comma separated) fed to the window 300 ms apart from 1.5 s on
    bool autosaveExplicit = false;    // --autosave / --autosave-minutes given (command line or an explicit --config file): headless runs autosave only then
};

const char* kHelp =
    "usage: biosys [options]\n"
    "  --page NAME|N            open this page instead of the new-game page (object state of a new game)\n"
    "  --slot night|dawn|day|dusk|cloud|goggles   time slot (default day)\n"
    "  --scroll X,Y             scroll position\n"
    "  --hotspots               draw hotspot outlines\n"
    "  --no-objects             new game without any object (viewer tests)\n"
    "  --play                   the original start-up flow (= --game); automatic when a bundled app is started without arguments\n"
    "  --data-home DIR          where the imported copy of the two CDs is kept (default: <save folder>/gamedata)\n"
    "  --version                print the version and exit\n"
    "  --screenshot FILE.bmp    render one frame headless and exit\n"
    "  --script \"cmd; cmd\"      run commands headless (page, scroll, move, click, rclick, key, advance, shot, dump, ...)\n"
    "  --log-events             print every game-logic call as 'event ...' lines on stdout\n"
    "  --log-idle               with --log-events: also the OnIdle event of every update\n"
    "  --movie FILE             play this movie (relative to BIOTIME, e.g. movies\\intro\\jumpstrt.avi) centred at start\n"
    "  --movie-palette FILE     palette (relative to BIODATA) shown with --movie, default: the page palette\n"
    "  --mute                   do not open an audio device\n"
    "  --audio original|enhanced  sound path: as the original (default) or float mixing, band-limited resampling, no clicks\n"
    "  --volume N               master volume 0..100 (default 100); keys: - quieter, = louder, \\ mute (not in a text entry)\n"
    "  --volume-effects N / --volume-movies N / --volume-terminal N   group volumes 0..100 (default 100)\n"
    "  --game                   attach the game logic (the port of BiosGame.dll) instead of the viewer logic\n"
    "  --log-api                with --game: print every API command the game makes as 'apicall ...' lines\n"
    "  --seed N                 with --game: srand() seed of the game logic (default 1)\n"
    "  --realism LEVEL          with --game: faithful (default) | corrected | closedloop: corrections of the original's bugs and of its\n"
    "                           simulation model (see SETTINGS.md)\n"
    "  --difficulty LEVEL       with --game: guided | original (default) | survivor: the pressure on the player\n"
    "  --fixes LIST             (developer option) with --game: replaces the bug fixes of the realism level: faithful | safe | cosmetic | restored | flag,-flag,...\n"
    "                           (the flags of the game logic, src/game/fixes.h, and of the BioSys 4 terminal, src/engine/bs4fixes.h)\n"
    "  --skip-intro / --intro   with --game: start a new game at once / run the original start-up (title, intro, menu); the\n"
    "                           window defaults to the intro, --script and --screenshot to a new game\n"
    "  --save-dir DIR           directory of the saved games (default: the per-user application directory)\n"
    "  --menu-logic             test game logic for the menu (Esc, F5, F6 open it; new / save / load work on files only)\n"
    "  --test-fruit ORN=OBJECT  test game logic: ornament ORN shows fruit object OBJECT (repeatable)\n"
    "  --selftest-all-pages     render every page, report failures\n"
    "  --fullscreen             start in desktop full-screen (Alt/Option+Return toggles it while playing)\n"
    "  --display N|NAME|list    the screen the window opens on: a number (1 = main screen), part of its name (dell), or list to print the screens;\n"
    "                           Alt/Option+N moves the window to the next screen, Alt/Option+M lets go of the mouse (click the picture to go on)\n"
    "  --panorama original|smooth   panoramas as the original draws them (default) or at the window's resolution with filtering and\n"
    "                           continuous scrolling (the game, hit testing and the 640x480 frame do not change)\n"
    "  --pass-ms N              interval of the engine's idle passes in ms, 10..100 (default 33): the pace of scrolling, zooms, fades and\n"
    "                           animations, independent of the display's refresh rate; lower = faster, higher = slower\n"
    "  --panorama-filter linear|nearest   texture filter of the smooth panoramas (default linear)\n"
    "  --interface original|overlay  the 1998 bars (default), or the picture over the whole window with the bars as overlays (implies --panorama smooth --widescreen)\n"
    "  --widescreen             on panoramas use the window's full width: the view shows more of the cylinder beside the 4:3 picture (the\n"
    "                           sides are for looking only); other pages stay 4:3; implies --panorama smooth\n"
    "  --picture original|clean the pictures as the original draws them (default) or cleaned of their dither and enlarged with Catmull-Rom\n"
    "                           (implies --panorama smooth and --text sharp; the terminal and films are enlarged only)\n"
    "  --text original|sharp    the game's text (top-bar messages, status screens, machine displays, menu slot names, the terminal's\n"
    "                           numbers) enlarged in blocks with the frame (default) or drawn at the window's resolution\n"
    "  --start-muted            start with the sound muted (the backslash key toggles it)\n"
    "  --autosave yes|no        autosave on (default) or off; --autosave-minutes N: minutes of play between two timed autosaves\n"
    "                           (default 10, 0 = none; waking up and quitting still save); files autosave1..3 in the save directory\n"
    "  --load-autosave [1|2|3]  with --game: start in that autosave (default: the newest); F9 twice within 3 s loads the newest while playing\n"
    "  --save-pages N           pages of save slots in the save and load menus (default 4, 1 = the original's ten slots); Page Up /\n"
    "                           Page Down or the mouse wheel turn the pages\n"
    "  --pause-on-focus-loss yes|no   pause the game while the window has lost the focus (default yes; minimised always pauses)\n"
    "  --mouse-sensitivity F    factor 0.25 .. 4 on the mouse movement that scrolls a panorama (default 1)\n"
    "  --config FILE            preferences file to use (default: biosys.conf in the save directory; scripts and screenshots ignore it\n"
    "                           unless this is given); --no-config: neither read nor write it; --write-config: write the effective\n"
    "                           settings into it and exit. Every option marked in that file's comments can be set there as key = value;\n"
    "                           the command line overrides it (--name=value also works, e.g. --fullscreen=no)\n"
    "  --dev                    developer keys of the window (see below); off by default\n"
    "keys (window): S stash, D drop, Space use/wear/take, Shift bar mode, Ctrl free pointer, Tab personal status (hold), F9 F9 load autosave,\n"
    "  Caps Lock on (or hold the backquote key `) plant and environment status, Esc menu, F5 save, F6 load,\n"
    "  Alt/Option+Return full screen, F10 (or Cmd+,) settings menu\n"
    "developer keys (--dev): arrows scroll, F2 new game, Backspace back, G goto, [ ] prev/next page, H outlines, O hidden ornaments,\n"
    "  P/L palette set, 1-6 time slot, M next intro movie, N test sprite at the pointer (Shift: fixed), F3 smooth / original panoramas,\n"
    "  F4 linear / nearest filter of the smooth panoramas,\n"
    "  F7 widescreen on / off\n";

bool isDir(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// The port's own tables (data/*.tsv): inside an app bundle SDL_GetBasePath() is Contents/Resources, so that is tried first.
std::string findDataDir(const char* argv0) {
    std::string a = argv0 ? argv0 : "";
    size_t slash = a.rfind('/');
    std::vector<std::string> cands;
    if (const char* base = SDL_GetBasePath()) cands.push_back(std::string(base) + "data");
    cands.push_back(slash == std::string::npos ? "../data" : a.substr(0, slash) + "/../data");
    cands.push_back("data");
#ifdef BIOSYS_DEV_ROOT
    cands.push_back(BIOSYS_DEV_ROOT "/port/data");
#endif
    for (const std::string& c : cands)
        if (isDir(c)) return c;
    return cands.back();
}

// where the saved games go when --save-dir is not given: the per-user application directory
std::string defaultSaveDir() {
    char* p = SDL_GetPrefPath("biosys", "biosys");
    std::string r = p ? p : "";
    SDL_free(p);
    while (r.size() > 1 && r.back() == '/') r.pop_back();
    return r;
}

std::string trimmedDir(std::string d) {
    while (d.size() > 1 && d.back() == '/') d.pop_back();
    return d;
}

std::string dataHomeOf(const Options& o) { return o.dataHome.empty() ? defaultSaveDir() + "/gamedata" : trimmedDir(o.dataHome); }

// Where the game's files are, in this order: (a) BIOSYS_DATA; (b) the imported copy in the data home when it is complete; (c) the development
// roots (a development build only), unless --data-home was given; empty: the player has to hand over the two CDs first.
// --firstrun-only looks at (b) alone, so that the first-run screen can be tried on a development machine.
std::vector<std::string> resolveDataRoots(const Options& o) {
    if (!o.firstrunOnly) {
        std::vector<std::string> env = DataFiles::environmentRoots();
        if (!env.empty()) return env;
    }
    const std::string home = dataHomeOf(o);
    if (install::status(home).complete()) return install::dataRoots(home);
    if (!o.firstrunOnly && o.dataHome.empty()) {
        std::vector<std::string> dev;
        for (const std::string& r : DataFiles::developmentRoots())
            if (isDir(r)) dev.push_back(r);
        if (!dev.empty()) return dev;
    }
    return {};
}

int parseSlot(const std::string& s) {
    static const char* names[] = {"night", "dawn", "day", "dusk", "cloud", "goggles"};
    for (int i = 0; i < SLOT_COUNT; i++)
        if (iequals(s, names[i]) || iequals(s, timeSlotName(i))) return i;
    if (s.size() == 1 && s[0] >= '0' && s[0] <= '5') return s[0] - '0';
    throw Error("bad --slot: " + s);
}

// ---- the option table: one list drives the command line, the preferences file (app/prefs.h), its generated default and --write-config ----
using Spec = OptSpec<Options>;

std::string intText(long v) { return std::to_string(v); }

long parseIntIn(const std::string& cli, const std::string& v, long lo, long hi) {
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (v.empty() || *end || n < lo || n > hi) throw Error("bad " + cli + " (want " + std::to_string(lo) + ".." + std::to_string(hi) + "): " + v);
    return n;
}

bool parseYesNo(const std::string& cli, const std::string& v) {
    bool b = false;
    if (!parsePrefBool(v, b)) throw Error("bad " + cli + " (want yes or no): " + v);
    return b;
}

const OptTable<Options>& optionTable() {
    static const OptTable<Options> table = [] {
        OptTable<Options> t;
        auto add = [&](Spec s) { t.specs.push_back(std::move(s)); };
        // command line only: cli, kind, setter
        auto cliOnly = [&](const char* cli, OptKind kind, std::function<void(Options&, const std::string&)> set) {
            Spec s;
            s.cli = cli;
            s.kind = kind;
            s.set = std::move(set);
            add(std::move(s));
        };
        auto cliSwitch = [&](const char* cli, bool Options::*m) {
            const std::string name = cli;
            cliOnly(cli, OptKind::Switch, [m, name](Options& o, const std::string& v) { o.*m = parseYesNo(name, v); });
        };
        // a preference: the key is the option name without the dashes
        auto pref = [&](const char* cli, OptKind kind, const char* def, const char* help, std::function<void(Options&, const std::string&)> set,
                        std::function<std::string(const Options&)> get) {
            Spec s;
            s.cli = cli;
            s.key = std::string(cli).substr(2);
            s.kind = kind;
            s.def = def;
            s.help = help;
            s.set = std::move(set);
            s.get = std::move(get);
            add(std::move(s));
        };
        auto boolSwitch = [&](const char* cli, const char* help, bool Options::*m) {
            const std::string name = cli;
            pref(cli, OptKind::Switch, "no", help, [m, name](Options& o, const std::string& v) { o.*m = parseYesNo(name, v); },
                 [m](const Options& o) { return prefBoolText(o.*m); });
        };
        auto boolValue = [&](const char* cli, bool def, const char* help, bool Options::*m) {
            const std::string name = cli;
            pref(cli, OptKind::Value, def ? "yes" : "no", help, [m, name](Options& o, const std::string& v) { o.*m = parseYesNo(name, v); },
                 [m](const Options& o) { return prefBoolText(o.*m); });
        };
        auto percent = [&](const char* cli, const char* help, int Options::*m) {
            const std::string name = cli;
            pref(cli, OptKind::Value, "100", help, [m, name](Options& o, const std::string& v) { o.*m = static_cast<int>(parseIntIn(name, v, 0, 100)); },
                 [m](const Options& o) { return intText(o.*m); });
        };
        auto settingWord = [&](const char* cli, const char* def, const char* help, std::string Options::*m) {
            pref(cli, OptKind::Value, def, help,
                 [m](Options& o, const std::string& v) {
                     Options probe = o;     // refuses an unknown word before it is taken
                     probe.*m = v;
                     resolveSettings(SettingsText{probe.realism, probe.difficulty, probe.fixes});
                     o.*m = v;
                 },
                 [m](const Options& o) { return o.*m; });
        };

        cliOnly("--page", OptKind::Value, [](Options& o, const std::string& v) { o.page = v; });
        cliOnly("--slot", OptKind::Value, [](Options& o, const std::string& v) { o.slot = parseSlot(v); });
        cliOnly("--scroll", OptKind::Value, [](Options& o, const std::string& v) {
            if (std::sscanf(v.c_str(), "%d,%d", &o.scrollX, &o.scrollY) != 2) throw Error("bad --scroll: " + v);
            o.haveScroll = true;
        });
        cliSwitch("--hotspots", &Options::hotspots);
        cliOnly("--screenshot", OptKind::Value, [](Options& o, const std::string& v) { o.screenshot = v; });
        cliSwitch("--selftest-all-pages", &Options::selftest);
        cliOnly("--script", OptKind::Value, [](Options& o, const std::string& v) { o.script = v; o.haveScript = true; });
        cliSwitch("--log-events", &Options::logEvents);
        cliSwitch("--log-idle", &Options::logIdle);
        cliSwitch("--no-objects", &Options::noObjects);
        cliSwitch("--log-api", &Options::logApi);
        cliOnly("--save-dir", OptKind::Value, [](Options& o, const std::string& v) { o.saveDir = v; });
        cliSwitch("--intro", &Options::intro);
        cliSwitch("--menu-logic", &Options::menuLogic);
        cliSwitch("--dev", &Options::dev);
        cliOnly("--seed", OptKind::Value, [](Options& o, const std::string& v) { o.seed = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10)); });
        cliOnly("--movie", OptKind::Value, [](Options& o, const std::string& v) { o.movie = v; });
        cliOnly("--movie-palette", OptKind::Value, [](Options& o, const std::string& v) { o.moviePalette = v; });
        cliOnly("--test-fruit", OptKind::Value, [](Options& o, const std::string& v) {
            const size_t eq = v.find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 >= v.size()) throw Error("bad --test-fruit (want ORNAMENT=OBJECT): " + v);
            o.fruit.push_back({v.substr(0, eq), v.substr(eq + 1)});
        });
        cliOnly("--quit-after-ms", OptKind::Value, [](Options& o, const std::string& v) { o.quitAfterMs = static_cast<uint32_t>(parseIntIn("--quit-after-ms", v, 1, 3600000)); });
        cliOnly("--test-keys", OptKind::Value, [](Options& o, const std::string& v) { o.testKeys = v; });
        cliOnly("--window-size", OptKind::Value, [](Options& o, const std::string& v) {
            if (std::sscanf(v.c_str(), "%dx%d", &o.windowW, &o.windowH) != 2 || o.windowW < 64 || o.windowH < 64 || o.windowW > 16384 || o.windowH > 16384)
                throw Error("bad --window-size (want WxH): " + v);
        });
        cliOnly("--test-shot", OptKind::Value, [](Options& o, const std::string& v) { o.testShot = v; });
        cliOnly("--play", OptKind::Switch, [](Options& o, const std::string& v) { if (v != "no") o.game = true; });
        cliOnly("--data-home", OptKind::Value, [](Options& o, const std::string& v) { o.dataHome = v; });
        cliSwitch("--firstrun-only", &Options::firstrunOnly);
        cliSwitch("--firstrun-log", &Options::firstrunLog);
        cliOnly("--firstrun-shot", OptKind::Value, [](Options& o, const std::string& v) { o.firstrunShot = v; });
        cliOnly("--test-drop", OptKind::Value, [](Options& o, const std::string& v) { o.testDrops.push_back(v); });
        cliOnly("--config", OptKind::Value, [](Options& o, const std::string& v) { o.configFile = v; });
        cliSwitch("--no-config", &Options::noConfig);
        cliSwitch("--write-config", &Options::writeConfig);
        cliOnly("--load-autosave", OptKind::Optional, [](Options& o, const std::string& v) {
            o.loadAutosave = v.empty() ? -1 : static_cast<int>(parseIntIn("--load-autosave", v, 1, kAutosaveKeep));
        });

        // the preferences, in the order of the generated file
        boolSwitch("--game", "Attach the game logic (the port of BiosGame.dll): without it the window is only a page viewer. Set this to yes to play.", &Options::game);
        boolSwitch("--skip-intro", "Start a new game at once instead of the title, the intro and the main menu.", &Options::skipIntro);
        settingWord("--realism", "faithful", "Realism level: faithful, corrected or closedloop (see SETTINGS.md).", &Options::realism);
        settingWord("--difficulty", "original", "Difficulty: guided, original or survivor.", &Options::difficulty);
        settingWord("--fixes", "", "(Developer) replaces the bug fixes of the realism level: faithful, safe, cosmetic, restored or a list of flags.", &Options::fixes);
        boolSwitch("--fullscreen", "Start in desktop full screen (Alt/Option+Return toggles it while playing; the state at exit is kept).", &Options::fullscreen);
        pref("--display", OptKind::Value, "", "The screen the window opens on: a number (1 = the main screen) or a part of the screen's name (for example dell); empty = the main screen. Alt/Option+N moves the window to the next screen while playing; the screen at exit is kept.",
             [](Options& o, const std::string& v) {
                 if (v.size() > 100) throw Error("bad --display (want a number, a part of a screen's name, or list)");
                 o.display = v;
             },
             [](const Options& o) { return o.display; });
        pref("--panorama", OptKind::Value, "original", "Panoramas as the original draws them (original) or at the window's resolution, filtered and scrolling smoothly (smooth).",
             [](Options& o, const std::string& v) {
                 if (v != "original" && v != "smooth") throw Error("bad --panorama (want original or smooth): " + v);
                 o.smoothPanorama = v == "smooth";
             },
             [](const Options& o) { return std::string(o.smoothPanorama ? "smooth" : "original"); });
        pref("--panorama-filter", OptKind::Value, "linear", "Texture filter of the smooth panoramas: linear or nearest.",
             [](Options& o, const std::string& v) {
                 if (v != "linear" && v != "nearest") throw Error("bad --panorama-filter (want linear or nearest): " + v);
                 o.panoramaLinear = v == "linear";
             },
             [](const Options& o) { return std::string(o.panoramaLinear ? "linear" : "nearest"); });
        boolSwitch("--widescreen", "On panorama pages use the window's full width: the view shows more of the cylinder beside the 4:3 picture (implies panorama = smooth).", &Options::widescreen);
        pref("--interface", OptKind::Value, "original", "The 1998 bars above and below the picture (original) or the picture over the whole window with the status words, the message and the inventory shown over it only when there is something to show (overlay; implies panorama = smooth and widescreen).",
             [](Options& o, const std::string& v) {
                 if (v != "original" && v != "overlay") throw Error("bad --interface (want original or overlay): " + v);
                 o.interfaceOverlay = v == "overlay";
             },
             [](const Options& o) { return std::string(o.interfaceOverlay ? "overlay" : "original"); });
        pref("--text", OptKind::Value, "original", "The game's text enlarged with the frame (original) or drawn at the window's resolution (sharp).",
             [](Options& o, const std::string& v) {
                 if (v != "original" && v != "sharp") throw Error("bad --text (want original or sharp): " + v);
                 o.textSharp = v == "sharp";
             },
             [](const Options& o) { return std::string(o.textSharp ? "sharp" : "original"); });
        pref("--picture", OptKind::Value, "original", "The pictures as the original draws them (original) or cleaned of their dither and enlarged smoothly (clean; implies panorama = smooth and text = sharp).",
             [](Options& o, const std::string& v) {
                 if (v != "original" && v != "clean") throw Error("bad --picture (want original or clean): " + v);
                 o.pictureClean = v == "clean";
             },
             [](const Options& o) { return std::string(o.pictureClean ? "clean" : "original"); });
        cliOnly("--clean-texture", OptKind::Value, [](Options& o, const std::string& v) { o.cleanTexScale = static_cast<int>(parseIntIn("--clean-texture", v, 1, 2)); });
        cliOnly("--clean-slew", OptKind::Value, [](Options& o, const std::string& v) { o.cleanSlew = static_cast<int>(parseIntIn("--clean-slew", v, 0, 16)); });
        pref("--pass-ms", OptKind::Value, "33", "Interval of the engine's idle passes in ms (10..100): the pace of scrolling, zooms, fades and animations.",
             [](Options& o, const std::string& v) {
                 o.passMs = static_cast<uint32_t>(parseIntIn("--pass-ms", v, static_cast<long>(kMinPassMs), static_cast<long>(kMaxPassMs)));
             },
             [](const Options& o) { return intText(o.passMs); });
        boolSwitch("--mute", "Do not open an audio device at all.", &Options::mute);
        boolSwitch("--start-muted", "Start with the sound muted (the backslash key toggles it; the state at exit is kept).", &Options::startMuted);
        pref("--audio", OptKind::Value, "original", "Sound path: original (as the original mixes) or enhanced (float mixing, band-limited resampling, no clicks).",
             [](Options& o, const std::string& v) {
                 if (v != "original" && v != "enhanced") throw Error("bad --audio (want original or enhanced): " + v);
                 o.audioEnhanced = v == "enhanced";
             },
             [](const Options& o) { return std::string(o.audioEnhanced ? "enhanced" : "original"); });
        percent("--volume", "Master volume 0..100 (the - and = keys change it while playing; the level at exit is kept).", &Options::volMaster);
        percent("--volume-effects", "Volume of the sound effects, 0..100.", &Options::volEffects);
        percent("--volume-movies", "Volume of the movies' sound, 0..100.", &Options::volMovies);
        percent("--volume-terminal", "Volume of the terminal's sounds, 0..100.", &Options::volTerminal);
        boolValue("--autosave", true, "Autosave on or off (see autosave-minutes): every few minutes, on waking up and when you quit.", &Options::autosave);
        pref("--autosave-minutes", OptKind::Value, "10", "Minutes of play between two autosaves, 0 = no timed autosave (waking up and quitting still save).",
             [](Options& o, const std::string& v) { o.autosaveMinutes = static_cast<int>(parseIntIn("--autosave-minutes", v, 0, 24 * 60)); },
             [](const Options& o) { return intText(o.autosaveMinutes); });
        pref("--save-pages", OptKind::Value, "4", "Pages of save slots in the save and load menus (Page Up / Page Down, mouse wheel); 1 = the original's ten slots.",
             [](Options& o, const std::string& v) { o.savePages = static_cast<int>(parseIntIn("--save-pages", v, 1, 20)); },
             [](const Options& o) { return intText(o.savePages); });
        boolValue("--pause-on-focus-loss", true, "Pause the game while the window has lost the focus (a minimised window is always paused).", &Options::pauseOnFocusLoss);
        pref("--mouse-sensitivity", OptKind::Value, "1", "Factor on the mouse movement that scrolls a panorama, 0.25 .. 4.",
             [](Options& o, const std::string& v) {
                 if (!parseMouseSensitivity(v, o.mouseSensitivity)) throw Error("bad --mouse-sensitivity (want 0.25..4): " + v);
             },
             [](const Options& o) {
                 char b[32];
                 std::snprintf(b, sizeof b, "%g", o.mouseSensitivity);
                 return std::string(b);
             });
        return t;
    }();
    return table;
}

// Defaults, then (cfg) the preferences file, then the command line. Problems of the file are appended to `problems` (reported by the caller);
// a bad command line throws.
Options parseArgs(int argc, char** argv, const PrefFile* cfg = nullptr, std::vector<std::string>* problems = nullptr) {
    Options o;
    const OptTable<Options>& table = optionTable();
    if (cfg) {
        std::vector<std::string> local;
        table.apply(o, *cfg, problems ? *problems : local);
        for (const PrefEntry& pe : cfg->entries)
            if (pe.key == "autosave" || pe.key == "autosave-minutes") o.autosaveExplicit = true;
    }
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i], inlineValue;
        bool haveInline = false;
        if (a == "--help" || a == "-h") { o.help = true; continue; }
        const OptSpec<Options>* s = table.byCli(a);
        if (!s && a.compare(0, 2, "--") == 0) {       // --name=value
            const size_t eq = a.find('=');
            if (eq != std::string::npos && (s = table.byCli(a.substr(0, eq))) != nullptr) {
                inlineValue = a.substr(eq + 1);
                haveInline = true;
                a = a.substr(0, eq);
            }
        }
        if (!s) throw Error("unknown option: " + a);
        if (s->cli == "--autosave" || s->cli == "--autosave-minutes") o.autosaveExplicit = true;
        std::string value;
        if (haveInline) value = inlineValue;
        else if (s->kind == OptKind::Switch) value = "yes";
        else if (s->kind == OptKind::Value) {
            if (i + 1 >= argc) throw Error("missing value for " + a);
            value = argv[++i];
        } else if (i + 1 < argc && std::string(argv[i + 1]).compare(0, 2, "--") != 0) {
            value = argv[++i];
        }
        s->set(o, value);
    }
    if (o.pictureClean) { o.smoothPanorama = true; o.textSharp = true; }   // --picture clean needs the smooth panorama and the sharp text
    if (o.interfaceOverlay) { o.smoothPanorama = true; o.widescreen = true; }   // --interface overlay needs the smooth, wide panorama
    if (o.widescreen) o.smoothPanorama = true;     // --widescreen needs the smooth path
    const ResolvedSettings r = resolveSettings(SettingsText{o.realism, o.difficulty, o.fixes});
    o.settings = r.settings;
    o.bs4Fixes = r.bs4;
    return o;
}

int resolvePage(const PageView& v, const std::string& s) {
    char* end = nullptr;
    long n = std::strtol(s.c_str(), &end, 10);
    if (!s.empty() && !*end) return static_cast<int>(n);
    int p = v.findPage(s);
    if (!p) throw Error("unknown page: " + s);
    return p;
}

// Game logic used by the headless runs: optional ornament->fruit-object table (--test-fruit) and an
// optional event log (--log-events) in a stable text form: one `event <name> <args>` line per event.
class FrontLogic : public GameLogic {
public:
    bool log = false, logIdle = false;
    Engine* engine = nullptr;                   // for the page name of events that carry none
    std::map<std::string, std::string> fruit;   // lower-case ornament name -> object
    bool haveXfade = false;                     // answer to event 25 (script `xfade`)
    CrossFadeSpec xfade;
    bool filterTest = false;                    // OnBlitPage handler of the page-filter tests (script `filtertest`)
    GameLogic* inner = nullptr;                 // --game: the real game logic; every event is logged, then forwarded
    // Settings stored with saved games (savesettings.h): `cmdline` is what the options said, `current` what is in force now (a loaded
    // game with a settings file continues under its own); `apply` puts a SettingsText into the game and the terminal.
    SettingsText cmdline, current;
    std::function<void(const SettingsText&)> apply;

    static std::string lower(std::string s) {
        for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        return s;
    }
    void line(const std::string& s) const {
        if (log) { std::printf("event %s\n", s.c_str()); std::fflush(stdout); }
    }
    static std::string win(int w) { return w == -1 ? "" : " win=" + std::to_string(w); }
    static std::string keyName(int vk) {
        switch (vk) {
        case kVkS: return "S";
        case kVkD: return "D";
        case kVkSpace: return "space";
        case kVkShift: return "shift";
        case kVkCtrl: return "ctrl";
        case 0: return "other";
        default: return "vk" + std::to_string(vk);
        }
    }
    int onStartup() override { line("startup"); return inner ? inner->onStartup() : 0; }
    void onShutdown() override { line("shutdown"); if (inner) inner->onShutdown(); }
    void onEnterPage(const std::string& page) override { line("enterpage " + page); if (inner) inner->onEnterPage(page); }
    void onExitPage() override {
        line("exitpage " + (engine && engine->view().pageNumber() ? engine->view().page().name : std::string("-")));
        if (inner) inner->onExitPage();
    }
    void onTimer() override { line("timer"); if (inner) inner->onTimer(); }
    int onHotspot(const std::string& hotspot, int window, const std::string& page) override {
        line("hotspot " + page + " " + hotspot + win(window));
        return inner ? inner->onHotspot(hotspot, window, page) : 1;
    }
    int onPageTimer(const std::string& timer, int window, const std::string& page) override {
        line("pagetimer " + page + " " + (timer.empty() ? "-" : timer));
        return inner ? inner->onPageTimer(timer, window, page) : 1;
    }
    void onPageLoaded(const std::string& page, int window) override { line("pageloaded " + page + win(window)); if (inner) inner->onPageLoaded(page, window); }
    void onPageDrawn(const std::string& page, int window) override { line("pagedrawn " + page + win(window)); if (inner) inner->onPageDrawn(page, window); }
    void onPopupOpened(const std::string& page, int index) override { line("popupopened " + page + " " + std::to_string(index)); if (inner) inner->onPopupOpened(page, index); }
    void onPopupClosed(int index) override { line("popupclosed " + std::to_string(index)); if (inner) inner->onPopupClosed(index); }
    void onGameObject(const std::string& name, int code, const std::string& extra) override {
        line("gameobject " + name + " " + std::to_string(code) + (extra.empty() ? "" : " " + extra));
        if (inner) inner->onGameObject(name, code, extra);
    }
    void onActivateApp(bool active) override { line(std::string("activateapp ") + (active ? "1" : "0")); if (inner) inner->onActivateApp(active); }
    void onKeyDown(int vkey, int rep, int flags, int& doDefault) override {
        line("keydown " + keyName(vkey));
        if (inner) inner->onKeyDown(vkey, rep, flags, doDefault);
    }
    void onKeyUp(int vkey, int rep, int flags, int& doDefault) override {
        line("keyup " + keyName(vkey));
        if (inner) inner->onKeyUp(vkey, rep, flags, doDefault);
    }
    int onIdle(int count) override {
        if (logIdle) line("idle " + std::to_string(count));
        return inner ? inner->onIdle(count) : 0;
    }
    void onAppActiveChanged(bool active) override { line(std::string("appactive ") + (active ? "1" : "0")); if (inner) inner->onAppActiveChanged(active); }
    void onCompassHeading(int sector) override { line("compass " + std::to_string(sector)); if (inner) inner->onCompassHeading(sector); }
    void onPaletteChanged() override { line("palettechanged"); if (inner) inner->onPaletteChanged(); }
    bool onGetCrossFadeEx(const std::string& def, const std::string& dest, CrossFadeSpec& spec) override {
        line("getcrossfade " + def + " " + dest);
        if (!haveXfade) return inner ? inner->onGetCrossFadeEx(def, dest, spec) : false;
        spec = xfade;
        return true;
    }
    // test filter: view rows 0, 1, 2 are filled with the three bytes of API 0x32 (handled = true)
    bool onBlitPage(u8* dst, u8* dstEnd, void* buf, int bufLen, u8 a, u8 b, u8 c) override {
        if (!filterTest) return inner ? inner->onBlitPage(dst, dstEnd, buf, bufLen, a, b, c) : false;
        const u8 v[3] = {a, b, c};
        for (int r = 0; r < 3 && dst + (r + 1) * 640 <= dstEnd; r++) std::fill(dst + r * 640, dst + (r + 1) * 640, v[r]);
        if (buf && bufLen > 0) dst[3 * 640] = *static_cast<u8*>(buf);
        return true;
    }
    bool onGetNameOfFruitObjectFromPageImageName(const std::string& ornament, std::string& out) override {
        auto it = fruit.find(lower(ornament));
        line("fruit " + ornament + " " + (it == fruit.end() ? "-" : it->second));
        if (it == fruit.end()) return inner ? inner->onGetNameOfFruitObjectFromPageImageName(ornament, out) : false;
        out = it->second;
        return true;
    }
    void onHotspotHover(const std::string& hotspot, int window, bool entered) override {
        line(std::string("hover ") + hotspot + (entered ? " in" : " out") + win(window));
        if (inner) inner->onHotspotHover(hotspot, window, entered);
    }
    void onRightClick() override { line("rightclick"); if (inner) inner->onRightClick(); }
    void onMouseDelta(int& dx, int& dy) override {
        line("mousedelta " + std::to_string(dx) + " " + std::to_string(dy));
        if (inner) inner->onMouseDelta(dx, dy);
    }
    void onStatusBarObject(int index, const std::string& name, int arg) override {
        line("statusbar " + std::to_string(index) + " " + name);
        if (inner) inner->onStatusBarObject(index, name, arg);
    }
    // --game: the remaining events go to the game logic with its own return values
    void onEvent16(int a, int b) override { if (inner) inner->onEvent16(a, b); }
    void onBS4Screen(int leaving) override { if (inner) inner->onBS4Screen(leaving); }
    bool onBS4Query(int id, void* buf) override { return inner ? inner->onBS4Query(id, buf) : false; }
    void onSystemPaletteChanged() override { if (inner) inner->onSystemPaletteChanged(); }
    static std::string base(const std::string& path) { return path.substr(path.find_last_of("/\\") + 1); }
    // The port's own saves are the original's bytes followed by a block that records the settings in force (engine/savetrailer.h).
    // Saves of earlier versions had them in a "<name>.settings" file; none is written any more, and the stale one of a slot that is
    // written over is removed.
    bool onSaveGame(const std::string& f) override {
        line("savegame " + base(f));
        const bool ok = inner ? inner->onSaveGame(f) : true;
        if (ok && inner && apply) {
            appendTrailer(f);
            dropSidecar(f);
        }
        return ok;
    }
    bool onLoadGame(const std::string& f, long off) override {
        line("loadgame " + base(f) + " offset=" + std::to_string(off));
        if (!inner || !apply) return inner ? inner->onLoadGame(f, off) : true;
        // a save with a settings block (or, from earlier versions, a valid settings file) continues under them, any other (an original
        // save, NewGame.sav) under the options
        SettingsText want = cmdline;
        const bool stored = readStoredSettings(f, off, want);
        const SettingsText before = current;
        if (want != current) apply(want);       // before loading: the fixes act while the file is read
        const bool ok = inner->onLoadGame(f, off);
        if (!ok) {
            if (want != before) apply(before);
            return false;
        }
        current = want;
        if (stored && want != cmdline)
            std::fprintf(stderr, "biosys: this saved game continues under the settings it was saved with (%s), not the command line's (%s)\n",
                         want.describe().c_str(), cmdline.describe().c_str());
        return true;
    }
    // port: the stale settings file of a slot that was written over (the settings are in the save itself now)
    static void dropSidecar(const std::string& savPath) {
        const std::string sc = sidecarPathOf(savPath);
        if (!sc.empty()) std::remove(sc.c_str());
    }
    bool writeTrailer = true;           // script `menu trailer off`: a test aid that makes a save without the block
    void appendTrailer(const std::string& savPath) const {
        if (!writeTrailer || sidecarPathOf(savPath).empty()) return;        // NewGame.sav and files that are not saves of the player
        const std::vector<uint8_t> block = savetrailer::make(settingsText(current));
        std::FILE* fp = std::fopen(savPath.c_str(), "ab");
        if (!fp || std::fwrite(block.data(), 1, block.size(), fp) != block.size())
            std::fprintf(stderr, "biosys: cannot write the settings into %s\n", savPath.c_str());
        if (fp) std::fclose(fp);
    }
    // The settings a save was made under: its block, else (saves of earlier versions) its settings file. A block that is damaged is
    // reported and counts as absent. false: nothing stored (the caller keeps the options).
    bool readStoredSettings(const std::string& savPath, long off, SettingsText& out) const {
        if (sidecarPathOf(savPath).empty()) return false;
        std::vector<uint8_t> bytes;
        if (off >= 0 && readAll(savPath, bytes)) {
            std::string text, why;
            const savetrailer::Status st = savetrailer::inspect(bytes, static_cast<size_t>(off) + game::SaveGame::blobSize(), text, why);
            SettingsText t;
            if (st == savetrailer::Status::Valid) {
                if (parseSettingsText(text, t)) { out = t; return true; }
                why = "its settings text is not understood";
            }
            if (st != savetrailer::Status::Absent)
                std::fprintf(stderr, "biosys: %s: %s; the game mode stored in it is ignored\n", base(savPath).c_str(), why.c_str());
        }
        return readSidecar(savPath, out);
    }
    // true if the save has a settings file made for exactly these bytes (and whose words the program knows)
    bool readSidecar(const std::string& savPath, SettingsText& out) const {
        const std::string sc = sidecarPathOf(savPath);
        std::vector<uint8_t> text, sav;
        if (sc.empty() || !readAll(sc, text)) return false;
        SettingsText t;
        uint64_t h = 0;
        if (!parseSidecar(std::string(text.begin(), text.end()), t, h) || !readAll(savPath, sav) || h != saveHash(sav)) return false;
        try {
            resolveSettings(t);
        } catch (const Error&) {
            return false;
        }
        out = t;
        return true;
    }
    static bool readAll(const std::string& path, std::vector<uint8_t>& out) {
        std::FILE* fp = std::fopen(path.c_str(), "rb");
        if (!fp) return false;
        out.clear();
        uint8_t buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, fp)) > 0) out.insert(out.end(), buf, buf + n);
        std::fclose(fp);
        return true;
    }
    bool onSetFeatureParameter(const std::string& n, int id, int a, int b, int c) override {
        return inner ? inner->onSetFeatureParameter(n, id, a, b, c) : false;
    }
    bool onGetFeatureParameter(const std::string& n, int id, u32& oi, double& ov) override {
        return inner ? inner->onGetFeatureParameter(n, id, oi, ov) : false;
    }
    void onTextInputDone(bool cancelled, const std::string& text) override {
        line(std::string("textinput ") + (cancelled ? "cancelled" : "done") + " '" + text + "'");
        if (inner) inner->onTextInputDone(cancelled, text);
    }
    void onMenuQuerySavedGame(int slot, int& ex, std::string& d, int unk) override {
        if (inner) inner->onMenuQuerySavedGame(slot, ex, d, unk);
        line("menuquery " + std::to_string(slot) + (unk ? " save" : " load") + " exists=" + std::to_string(ex));
    }
    void onSimStatusHotspot(const std::string& h) override { line("simstatushotspot " + h); if (inner) inner->onSimStatusHotspot(h); }
    bool onEvent35(const std::string& p) override {
        const bool r = inner ? inner->onEvent35(p) : true;
        line("event35 " + p + " -> " + (r ? "1" : "0"));
        return r;
    }
    void onReturnToSavedPage(const std::string& p) override { line("returntosavedpage " + p); if (inner) inner->onReturnToSavedPage(p); }
    void onEvent37() override { line("event37"); if (inner) inner->onEvent37(); }
    void onEvent38() override { line("event38"); if (inner) inner->onEvent38(); }
    bool onEvent44() override {
        const bool r = inner ? inner->onEvent44() : true;
        line(std::string("event44 -> ") + (r ? "1" : "0"));
        return r;
    }
    void onSoundEvent(int handle, int code) override { line("sound " + std::to_string(handle) + " " + std::to_string(code)); if (inner) inner->onSoundEvent(handle, code); }
    void onSpriteEvent(int handle, int code, int arg) override {
        line("sprite " + std::to_string(handle) + " " + std::to_string(code) + (code == 5 ? " " + std::to_string(arg) : ""));
        if (inner) inner->onSpriteEvent(handle, code, arg);
    }
    void onMovieEvent(int handle, int code, int arg) override {
        line("movie " + std::to_string(handle) + " " + std::to_string(code) + (code == 5 ? " " + std::to_string(arg) : ""));
        if (inner) inner->onMovieEvent(handle, code, arg);
    }
    // events the viewer logic does not log: forwarded to the game logic only
    int onRestart() override { line("restart"); return inner ? inner->onRestart() : 0; }
    void onMenuCommand(int code, int arg) override {
        // New game in the menu: with the New game screen enabled the window asks which realism and difficulty first (main.cpp menuPoll)
        // and then calls startNewGame; nothing reaches the game logic before that
        if (code == 1 && newGameScreen && !confirmedNewGame && inner) {
            pendingNewGame = true;
            return;
        }
        line("menucommand " + std::to_string(code) + " " + std::to_string(arg));
        if (inner) inner->onMenuCommand(code, arg);
    }
    // The New game screen: `cmdline` (the options, which a game without stored settings is loaded under, NewGame.sav among them) becomes
    // the chosen realism and difficulty, and the command goes on as the menu would have sent it.
    bool newGameScreen = false, pendingNewGame = false, confirmedNewGame = false;
    void startNewGame(const std::string& realism, const std::string& difficulty) {
        cmdline.realism = realism;
        cmdline.difficulty = difficulty;
        confirmedNewGame = true;
        try {
            onMenuCommand(1, 0);
        } catch (...) {
            confirmedNewGame = false;
            throw;
        }
        confirmedNewGame = false;
    }
};

struct Setup {
    FrontLogic logic;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<game::LoggingApi> apiLog;     // --log-api
    std::unique_ptr<game::BiosGame> game;         // --game
    std::unique_ptr<MenuTestLogic> menuLogic;     // --menu-logic
    AutosavePlanner autosave;                     // when an autosave is due (app/autosave.h); only with --game
    // The settings menu (F10) and the New game screen (app/settingsmenu.h): the model and the input handling are SDL-free and shared by the
    // window and the script commands `menu ...`; `cur` is the options as they are now (what the menu changes, what the preferences file
    // holds), the two functions are set by the window for what only it can read or change.
    smenu::Controller menu;
    std::unique_ptr<MenuFonts> menuFonts;
    Options cur;
    std::map<std::string, std::string> pendingPrefs;      // changes not yet written into the preferences file
    bool menuScriptPaused = false;                        // script: the menu has paused the engine
    bool menuHint = false;                                // script: draw the "F10 settings" hint (the window: the engine's menu is up)
    std::function<void(Options&)> snapshotWindow;
    std::function<void(const smenu::Change&)> applyWindow;
    std::vector<std::string> screenNames;
};

// "" if an autosave (a normal save) can be made now, else why not. atQuit: the program is closing, the game menu that the Quit command was
// chosen in is still up and its own save path works (the save closes it).
std::string saveBlockerOf(Setup& s, bool atQuit) {
    if (!s.game) return "no game logic";
    Engine& e = *s.engine;
    if (e.blocking()) return "a blocking operation runs";
    if (e.textInputActive()) return "a text entry is up";
    if (e.titleScreenUp()) return "the title screen is up";
    if (e.simStatusUp()) return "the status window is up";
    if (e.bs4Active()) return "the terminal is up";
    if (e.gameMenuOpen() && !atQuit) return "a menu is open";
    return s.game->saveBlocker();
}

// One autosave: written as autosave0.sav through the normal save path (so its .settings file is made too), then rotated in. Never throws;
// a failure is reported on stderr and leaves the game and the older autosaves as they were.
bool runAutosave(Setup& s, const char* why) {
    Engine& e = *s.engine;
    const std::string dir = e.saveDirectory();
    bool ok = false;
    std::string err = "the save folder is not set";
    if (!dir.empty()) {
        try {
            autosave::discardTemp(dir);
            if (s.game->saveToFile(autosave::fileName(0))) {
                ok = autosave::rotate(dir, err);
                if (!ok) autosave::discardTemp(dir);
            } else {
                err = e.lastReport().empty() ? "the save was refused" : e.lastReport();
                autosave::discardTemp(dir);
            }
        } catch (const Error& x) {
            err = x.what();
            autosave::discardTemp(dir);
        }
    }
    s.autosave.attempted();
    if (!ok) std::fprintf(stderr, "biosys: autosave (%s) failed: %s; the game goes on\n", why, err.c_str());
    return ok;
}

// The play-time hook of the window loop and of the script's `advance`: `dtMs` of play have passed. Makes the autosave that is due as soon as a
// normal save is possible.
void autosaveTick(Setup& s, uint32_t dtMs) {
    if (!s.game || !s.game->gameRunning() || s.engine->mediaPaused()) return;     // not while the window is inactive: no play time passes
    s.autosave.advance(dtMs);
    s.autosave.observeAwake(s.game->awake());
    const AutosavePlanner::Why why = s.autosave.due();
    if (why == AutosavePlanner::Why::None || !saveBlockerOf(s, false).empty()) return;
    runAutosave(s, why == AutosavePlanner::Why::Wake ? "wake from sleep" : "timed");
}

// Loads autosave n (1..3, -1 = the newest) into the running game, as the load menu would. msg: why not.
bool loadAutosaveInto(Setup& s, int n, std::string& msg) {
    if (!s.game) { msg = "no game logic"; return false; }
    const std::string dir = s.engine->saveDirectory();
    if (n < 0) n = autosave::newest(dir);
    if (n == 0 || dir.empty() || !autosave::exists(autosave::path(dir, n))) { msg = "no autosave to load"; return false; }
    const std::string why = s.game->loadBlocker();
    if (!why.empty()) { msg = "cannot load now: " + why; return false; }
    if (!s.game->loadFromFile(autosave::fileName(n))) { msg = "autosave " + std::to_string(n) + " could not be loaded"; return false; }
    s.autosave.restart();
    msg = "autosave " + std::to_string(n) + " loaded";
    return true;
}

// The script command `autosave ARGS` (the injected clock stands in for the window's play time: see MANUAL.md "Saving")
std::string autosaveCommand(Setup& s, const std::string& args) {
    std::istringstream in(args);
    std::string cmd;
    in >> cmd;
    std::ostringstream out;
    AutosavePlanner& p = s.autosave;
    if (!s.game) throw Error("script: 'autosave' needs --game");
    if (cmd == "status" || cmd.empty()) {
        const AutosavePlanner::Why w = p.due();
        const std::string dir = s.engine->saveDirectory();
        out << "autosave enabled=" << (p.enabled() ? 1 : 0) << " minutes=" << p.minutes() << " played=" << p.playedMs() << " due="
            << (w == AutosavePlanner::Why::Wake ? "wake" : w == AutosavePlanner::Why::Periodic ? "timed" : "no") << " blocker='" << saveBlockerOf(s, false)
            << "' newest=" << (dir.empty() ? 0 : autosave::newest(dir)) << "\n";
    } else if (cmd == "minutes") {
        int m = 0;
        in >> m;
        p.setMinutes(m);
        p.setEnabled(true);
        out << "autosave minutes " << p.minutes() << "\n";
    } else if (cmd == "on" || cmd == "off") {
        p.setEnabled(cmd == "on");
        out << "autosave " << cmd << "\n";
    } else if (cmd == "now" || cmd == "quit") {      // as if due now / the autosave of the program's exit
        const std::string why = saveBlockerOf(s, cmd == "quit");
        if (!why.empty()) out << "autosave " << cmd << " refused: " << why << "\n";
        else out << "autosave " << cmd << (runAutosave(s, cmd == "quit" ? "quit" : "now") ? " saved" : " failed") << "\n";
    } else if (cmd == "load") {                       // the confirmed F9: load autosave N (default: the newest)
        int n = -1;
        in >> n;
        std::string msg;
        const bool ok = loadAutosaveInto(s, n, msg);
        out << "autosave load " << (ok ? "ok: " : "failed: ") << msg << "\n";
    } else {
        throw Error("script: autosave status | minutes N | on | off | now | quit | load [N]");
    }
    return out.str();
}

// ---- the settings menu and the New game screen: host side (the model and the drawing are in app/settingsmenu*.h / .cpp) ----

// The options as they are now: the options the run holds, with what the engine and the window have changed since (keys, the menu).
Options menuLiveOptions(Setup& s) {
    Options now = s.cur;
    Mixer& mx = s.engine->mixer();
    now.volMaster = mx.masterVolume();
    now.volEffects = mx.groupVolume(VoiceGroup::Effects);
    now.volMovies = mx.groupVolume(VoiceGroup::Movies);
    now.volTerminal = mx.groupVolume(VoiceGroup::Terminal);
    now.startMuted = mx.muted();
    now.audioEnhanced = mx.mode() == AudioMode::Enhanced;
    now.autosave = s.autosave.enabled();
    now.autosaveMinutes = s.autosave.minutes();
    now.savePages = s.engine->slotPages();
    if (s.snapshotWindow) s.snapshotWindow(now);
    return now;
}

// Loads the model from the live state; call when the menu opens
void menuRefresh(Setup& s) {
    smenu::Model& m = s.menu.model;
    m.setScreens(s.screenNames);
    m.setPersistent(!s.cur.configPath.empty() && !s.cur.noConfig);
    m.setRunning(s.logic.current.realism, s.logic.current.difficulty, s.logic.current.fixes);
    m.load(optionTable().currentValues(menuLiveOptions(s)));
}

// Puts the preferences changed by the menu into the preferences file (comments and unknown lines stay); nothing with --no-config
void menuFlush(Setup& s) {
    if (s.pendingPrefs.empty()) return;
    std::map<std::string, std::string> updates;
    updates.swap(s.pendingPrefs);
    if (s.cur.configPath.empty() || s.cur.noConfig) return;
    std::string text;
    if (!readTextFile(s.cur.configPath, text)) text = optionTable().defaultFile();
    if (!writeTextFile(s.cur.configPath, updatePrefsText(text, updates)))
        std::fprintf(stderr, "biosys: cannot save the preferences to %s\n", s.cur.configPath.c_str());
}

// One change of the menu: into the options, into the engine and the autosave, into the window (if there is one), and onto the list of
// what goes into the preferences file.
void menuApplyChange(Setup& s, const smenu::Change& c) {
    const OptSpec<Options>* sp = optionTable().byKey(c.key);
    if (!sp) return;
    try {
        sp->set(s.cur, c.text);
    } catch (const Error& err) {
        std::fprintf(stderr, "biosys: settings menu: %s\n", err.what());
        return;
    }
    Engine& e = *s.engine;
    Mixer& mx = e.mixer();
    const std::string& k = c.key;
    if (k == "volume") mx.setMasterVolume(s.cur.volMaster);
    else if (k == "volume-effects") mx.setGroupVolume(VoiceGroup::Effects, s.cur.volEffects);
    else if (k == "volume-movies") mx.setGroupVolume(VoiceGroup::Movies, s.cur.volMovies);
    else if (k == "volume-terminal") mx.setGroupVolume(VoiceGroup::Terminal, s.cur.volTerminal);
    else if (k == "start-muted") mx.setMuted(s.cur.startMuted);
    else if (k == "audio") mx.setMode(s.cur.audioEnhanced ? AudioMode::Enhanced : AudioMode::Original);
    else if (k == "pass-ms") e.setPassMs(s.cur.passMs);
    else if (k == "autosave") { s.cur.autosaveExplicit = true; s.autosave.setEnabled(s.cur.autosave); }
    else if (k == "autosave-minutes") { s.cur.autosaveExplicit = true; s.autosave.setMinutes(s.cur.autosaveMinutes); }
    else if (k == "save-pages") e.setSlotPages(s.cur.savePages);
    else if (k == "text" || k == "interface") e.setTextTracking(s.cur.textSharp || s.cur.interfaceOverlay);
    else if (k == "realism") s.logic.cmdline.realism = s.cur.realism;       // what a game without stored settings is loaded under
    else if (k == "difficulty") s.logic.cmdline.difficulty = s.cur.difficulty;
    s.pendingPrefs[k] = sp->get(s.cur);
    if (s.applyWindow) s.applyWindow(c);
}

// After every input to the menu and once per frame of the window: applies what changed, writes the preferences (not in the middle of a
// slider drag), and carries out what the New game screen decided. `syncPause` is called when a menu has just closed, before a new game
// starts (the game must not be paused when it is loaded).
void menuPoll(Setup& s, const std::function<void()>& syncPause) {
    smenu::Controller& c = s.menu;
    for (const smenu::Change& ch : c.model.takeChanges()) menuApplyChange(s, ch);
    if (!c.dragging()) menuFlush(s);
    if (c.mode() == smenu::Controller::Mode::NewGame && c.newGame.outcome() != smenu::NewGameModel::Outcome::None) {
        const bool start = c.newGame.outcome() == smenu::NewGameModel::Outcome::Start;
        const std::string r = c.newGame.realism(), d = c.newGame.difficulty();
        c.close();
        if (start) {      // the choice is the next preselection too (only written when it differs from what the options say)
            if (r != s.cur.realism) menuApplyChange(s, {"realism", r});
            if (d != s.cur.difficulty) menuApplyChange(s, {"difficulty", d});
            menuFlush(s);
        }
        syncPause();
        if (start) s.logic.startNewGame(r, d);
    }
    if (s.logic.pendingNewGame && !c.isOpen()) {
        s.logic.pendingNewGame = false;
        c.openNewGame(s.cur.realism, s.cur.difficulty);
        syncPause();
    }
}

bool menuMayOpen(Setup& s) { return !s.engine->textInputActive() && !s.menu.isOpen() && !s.engine->blocking(); }

MenuFonts& menuFontsOf(Setup& s) {
    if (!s.menuFonts) s.menuFonts.reset(new MenuFonts(s.engine->files()));
    return *s.menuFonts;
}

bool parseGridPoint(const std::string& t, int& x, int& y) { return std::sscanf(t.c_str(), "%d,%d", &x, &y) == 2; }

// The script command `menu ARGS` (MANUAL.md "The settings menu"; tests/test_settingsmenu.sh). The pause of the game while a menu is up is
// what the window does with PauseState: Engine::setMediaPaused.
std::string menuCommand(Setup& s, const std::string& args) {
    std::istringstream in(args);
    std::string cmd;
    in >> cmd;
    smenu::Controller& c = s.menu;
    Engine& e = *s.engine;
    std::ostringstream out;
    auto syncPause = [&]() {      // (as the window's PauseState: the edge only)
        if (c.isOpen() != s.menuScriptPaused) {
            s.menuScriptPaused = c.isOpen();
            e.setMediaPaused(s.menuScriptPaused);
        }
    };
    auto poll = [&]() { menuPoll(s, syncPause); syncPause(); };
    auto state = [&]() {
        const smenu::Item* it = c.model.selected();
        out << "menu mode=" << (c.mode() == smenu::Controller::Mode::Settings ? "settings" : c.mode() == smenu::Controller::Mode::NewGame ? "newgame" : "closed")
            << " tab=" << smenu::tabName(c.model.tab()) << " row=" << c.model.row();
        if (c.mode() == smenu::Controller::Mode::NewGame)
            out << " realism=" << c.newGame.realism() << " difficulty=" << c.newGame.difficulty() << " focus=" << c.newGame.focus();
        else if (it)
            out << " key=" << it->key << " value=" << smenu::Model::valueText(*it) << " pref=" << smenu::Model::prefText(*it) << " when=" << smenu::whenText(it->when);
        out << " paused=" << (e.mediaPaused() ? 1 : 0) << " pending=" << (s.logic.pendingNewGame ? 1 : 0) << " persistent=" << (c.model.persistent() ? 1 : 0) << "\n";
    };
    if (cmd == "open") {
        if (!menuMayOpen(s)) out << "menu open refused\n";
        else { menuRefresh(s); c.openSettings(); }
        poll();
        state();
    } else if (cmd == "close") {
        c.close();
        poll();
        state();
    } else if (cmd == "poll") {
        poll();
        state();
    } else if (cmd == "newgame") {       // newgame on|off: the New game screen answers the menu's New game (the window's rule for --game without --skip-intro)
        std::string v;
        in >> v;
        s.logic.newGameScreen = v != "off";
        out << "menu newgame " << (s.logic.newGameScreen ? "on" : "off") << "\n";
    } else if (cmd == "trailer") {       // trailer on|off: whether saves get the settings block (a test aid: the original part of a save is the same either way)
        std::string v;
        in >> v;
        s.logic.writeTrailer = v != "off";
        out << "menu trailer " << (s.logic.writeTrailer ? "on" : "off") << "\n";
    } else if (cmd == "hint") {
        std::string v;
        in >> v;
        s.menuHint = v != "off";
    } else if (cmd == "key") {
        std::string name, mod;
        in >> name >> mod;
        static const std::map<std::string, smenu::MKey> keys = {
            {"up", smenu::MKey::Up}, {"down", smenu::MKey::Down}, {"left", smenu::MKey::Left}, {"right", smenu::MKey::Right}, {"tab", smenu::MKey::Tab},
            {"return", smenu::MKey::Return}, {"escape", smenu::MKey::Escape}, {"f10", smenu::MKey::F10}, {"pageup", smenu::MKey::PageUp},
            {"pagedown", smenu::MKey::PageDown}, {"home", smenu::MKey::Home}, {"end", smenu::MKey::End}};
        auto k = keys.find(name);
        if (k == keys.end()) throw Error("script: menu key up|down|left|right|tab|return|escape|f10|pageup|pagedown|home|end [shift]");
        if (!c.isOpen() && k->second == smenu::MKey::F10) {      // the key that opens it (the window's rule: not in a text entry)
            if (menuMayOpen(s)) { menuRefresh(s); c.openSettings(); }
        } else {
            c.key(k->second, mod == "shift");
        }
        poll();
        state();
    } else if (cmd == "tab") {
        std::string name;
        in >> name;
        for (int t = 0; t < smenu::kTabs; t++)
            if (iequals(name, smenu::tabName(t))) c.model.setTab(t);
        state();
    } else if (cmd == "down" || cmd == "up" || cmd == "move" || cmd == "click" || cmd == "drag") {
        std::string a, b;
        in >> a >> b;
        int x = 0, y = 0, x2 = 0, y2 = 0;
        if (!parseGridPoint(a, x, y)) throw Error("script: menu click|down|up|move X,Y | drag X,Y X,Y");
        if (cmd == "drag" && !parseGridPoint(b, x2, y2)) throw Error("script: menu drag X,Y X,Y");
        if (cmd == "move") c.mouseMove(x, y);
        else if (cmd == "down") c.mouseDown(x, y);
        else if (cmd == "up") c.mouseUp(x, y);
        else if (cmd == "click") { c.mouseMove(x, y); c.mouseDown(x, y); c.mouseUp(x, y); }
        else {
            c.mouseMove(x, y);
            c.mouseDown(x, y);
            for (int i = 1; i <= 8; i++) c.mouseMove(x + (x2 - x) * i / 8, y + (y2 - y) * i / 8);
            c.mouseUp(x2, y2);
        }
        poll();
        state();
    } else if (cmd == "wheel") {
        std::string a, dir;
        in >> a >> dir;
        int x = 0, y = 0;
        if (!parseGridPoint(a, x, y) || (dir != "up" && dir != "down")) throw Error("script: menu wheel X,Y up|down");
        c.wheel(x, y, dir == "up" ? 1 : -1);
        poll();
        state();
    } else if (cmd == "state") {
        state();
    } else if (cmd == "items") {         // the rows of the current tab
        const std::vector<int> rows = c.model.rows(c.model.tab());
        for (int r : rows) {
            const smenu::Item& it = c.model.items()[static_cast<size_t>(r)];
            out << "menu item key=" << it.key << " label='" << it.label << "' value=" << smenu::Model::valueText(it) << " pref=" << smenu::Model::prefText(it)
                << " when=" << smenu::whenText(it.when) << "\n";
        }
    } else if (cmd == "engine") {        // what the engine and the autosave hold, read from them (not from the options)
        Mixer& mx = e.mixer();
        out << "menu engine master=" << mx.masterVolume() << " effects=" << mx.groupVolume(VoiceGroup::Effects) << " movies=" << mx.groupVolume(VoiceGroup::Movies)
            << " terminal=" << mx.groupVolume(VoiceGroup::Terminal) << " muted=" << (mx.muted() ? 1 : 0) << " audio=" << (mx.mode() == AudioMode::Enhanced ? "enhanced" : "original")
            << " passms=" << e.passMs() << " slotpages=" << e.slotPages() << " autosave=" << (s.autosave.enabled() ? 1 : 0) << " autosaveminutes=" << s.autosave.minutes()
            << " texttracking=" << (e.textTracking() ? 1 : 0) << " cmdline=" << s.logic.cmdline.realism << "/" << s.logic.cmdline.difficulty
            << " running=" << s.logic.current.realism << "/" << s.logic.current.difficulty << " paused=" << (e.mediaPaused() ? 1 : 0) << "\n";
    } else if (cmd == "options") {       // the options as the run holds them (what the preferences file gets)
        for (const auto& kv : optionTable().currentValues(s.cur)) out << "menu option " << kv.first << "=" << kv.second << "\n";
    } else if (cmd == "shot") {          // shot FILE [WIDTH HEIGHT]: the window's picture with the menu over it (CPU composition, 24-bit BMP)
        std::string file;
        int W = 1280, H = 960;
        in >> file >> W >> H;
        if (file.empty() || W < 64 || H < 64 || W > 16384 || H > 16384) throw Error("script: menu shot FILE [WIDTH HEIGHT]");
        const int k = std::max(1, std::min(W / Frame::W, H / Frame::H));
        std::vector<uint32_t> layer, rgb;
        const bool hint = !c.isOpen() && (s.menuHint || e.gameMenuOpen());
        renderMenuLayer(c, menuFontsOf(s), k, hint, layer);
        Frame f;
        e.render(f);
        composeMenuPicture(f, e.view().palette(), W, H, c.isOpen(), layer, k, rgb);
        writeBmp24(file, W, H, rgb);
        out << "menu shot " << file << " " << W << "x" << H << " scale=" << k << " layer=" << (layer.empty() ? 0 : 1) << "\n";
    } else if (cmd == "textcheck") {     // every help text and description must fit its place
        const MenuFonts& F = menuFontsOf(s);
        int worstHelp = 0, worstNg = 0, bad = 0;
        smenu::Model probe;
        for (const smenu::Item& it : probe.items()) {
            const int n = static_cast<int>(wrapMenuText(*F.small(), it.help, smenu::kPW - 40).size());
            worstHelp = std::max(worstHelp, n);
            if (n > 3) { bad++; out << "menu textcheck too long: " << it.key << " (" << n << " lines)\n"; }
        }
        for (const auto* opts : {&smenu::realismOptions(), &smenu::difficultyOptions()})
            for (const smenu::NewGameOption& o : *opts) {
                const int n = static_cast<int>(wrapMenuText(*F.small(), o.text, smenu::ngBlockRect(0, 0).w - 20).size());
                worstNg = std::max(worstNg, n);
                if (n > 4) { bad++; out << "menu textcheck too long: " << o.pref << " (" << n << " lines)\n"; }
            }
        const int note = static_cast<int>(wrapMenuText(*F.small(), smenu::newGameNote(), smenu::kNW - 40).size());
        if (note > 4) bad++;
        out << "menu textcheck help=" << worstHelp << " newgame=" << worstNg << " note=" << note << " fontface=" << (F.ok() ? 1 : 0) << " bad=" << bad << "\n";
    } else {
        throw Error("script: menu open|close|poll|key NAME [shift]|tab NAME|click X,Y|down|up|move X,Y|drag X,Y X,Y|wheel X,Y up|down|state|items|engine|options|shot FILE [W H]|newgame on|off|hint on|off|trailer on|off|textcheck");
    }
    return out.str();
}

std::vector<std::string> gDataRoots;      // where the game's files are (resolveDataRoots), set by main() before the engine is made

void makeEngine(Setup& s, const Options& o, const char* argv0) {
    s.logic.log = o.logEvents;
    s.logic.logIdle = o.logIdle;
    for (auto& f : o.fruit) s.logic.fruit[FrontLogic::lower(f.first)] = f.second;
    s.engine.reset(new Engine(DataFiles(gDataRoots), findDataDir(argv0), &s.logic, nullptr));
    {   // --audio / --volume*: the mixer's mode and volume groups (defaults leave the original path bit-identical)
        Mixer& mx = s.engine->mixer();
        mx.setMode(o.audioEnhanced ? AudioMode::Enhanced : AudioMode::Original);
        mx.setMasterVolume(o.volMaster);
        mx.setGroupVolume(VoiceGroup::Effects, o.volEffects);
        mx.setGroupVolume(VoiceGroup::Movies, o.volMovies);
        mx.setGroupVolume(VoiceGroup::Terminal, o.volTerminal);
        mx.setMuted(o.startMuted);
    }
    s.engine->setSlotPages(o.savePages);
    s.engine->setTextTracking(o.textSharp || o.interfaceOverlay);   // --text sharp: the canvas keeps the text runs (engine/textplane.h)
    {   // the timed autosave: a window game, or a headless run that asked for it (so that scripts never write into a folder by themselves)
        const bool headless = o.haveScript || !o.screenshot.empty() || o.selftest;
        s.autosave.setMinutes(o.autosaveMinutes);
        s.autosave.setEnabled(o.autosave && (!headless || o.autosaveExplicit));
    }
    Engine& e = *s.engine;
    e.setBs4Fixes(o.bs4Fixes);
    if (o.logEvents)
        e.animTrace = [](const ObjectAnim& rec, bool hideHeld, bool priority) {
            std::printf("event anim %d %s hideHeld=%d priority=%d\n", rec.event, rec.file.c_str(), hideHeld ? 1 : 0, priority ? 1 : 0);
            std::fflush(stdout);
        };
    s.logic.engine = &e;
    if (o.game) {
        // the game logic talks to the engine through EngineApi only; --log-api puts a printing decorator in between
        EngineApi* api = &e.api();
        if (o.logApi) {
            s.apiLog.reset(new game::LoggingApi(*api, [](const std::string& line) {
                std::printf("apicall %s\n", line.c_str() + 4);   // generated lines start with "api "
                std::fflush(stdout);
            }));
            api = s.apiLog.get();
        }
        game::BiosGame::Config gc;
        gc.randSeed = o.seed;
        gc.dataDir = findDataDir(argv0);
        gc.set(o.settings);
        if (!o.settings.isDefault() || o.bs4Fixes.any())
            std::fprintf(stderr, "biosys: settings %s; terminal fixes=%s\n", o.settings.toString().c_str(), o.bs4Fixes.toString().c_str());
        Engine* ep = &e;
        gc.tick = [ep]() { return ep->now(); };
        gc.sendKeyUp = [ep](int vk, unsigned) {     // the game window's WM_KEYUP (SendMessageA): the key release goes into the engine
            ep->keyUp(vk == kVkS ? Key::S : vk == kVkD ? Key::D : vk == kVkSpace ? Key::Space : Key::Other, vk);
        };
        gc.postMouseMove = [ep](int x, int y, bool yFromCursor) { ep->postMouseMove(x, yFromCursor ? ep->pointerY() : y); };   // PostMessageA(WM_MOUSEMOVE)
        // the window waits on the real clock where the original busy-waits (the journey); scripts and screenshots have an injected clock
        if (!o.haveScript && o.screenshot.empty() && !o.selftest) gc.busyWait = [ep](u32 ms) { ep->waitBlocking(ms); };
        s.game.reset(new game::BiosGame(*api, e.files(), gc));
        s.logic.cmdline = s.logic.current = SettingsText{o.realism, o.difficulty, o.fixes};
        game::BiosGame* gp = s.game.get();
        s.logic.apply = [gp, ep](const SettingsText& t) {
            const ResolvedSettings r = resolveSettings(t);
            gp->setSettings(r.settings);
            ep->setBs4Fixes(r.bs4);
        };
        s.logic.inner = s.game.get();
        e.setGameLogicAttached(true);
    }
    if (o.menuLogic && !o.game) {
        s.menuLogic.reset(new MenuTestLogic(e));
        s.logic.inner = s.menuLogic.get();
    }
    if (!o.saveDir.empty() && !o.haveScript && o.screenshot.empty() && !o.selftest) {   // a window run makes a missing --save-dir (SDL makes the default one)
        std::error_code ec;
        std::filesystem::create_directories(o.saveDir, ec);
    }
    e.setSaveDirectory(o.saveDir.empty() ? defaultSaveDir() : o.saveDir);
    e.reportSink = [](const std::string&, const std::string& log) { std::fprintf(stderr, "biosys: %s\n", log.c_str()); };   // the original's log file
    e.setTimeSlot(o.slot);
    e.setOutlines(o.hotspots);
    // With the game logic the original start-up applies in the window (title bitmap, the intro page, then the menu: new game
    // loads NewGame.sav through the menu's command); headless runs and --skip-intro go to the new game at once, --intro keeps
    // the original start-up in a script. Without the game logic the viewer starts on the new-game page.
    const bool original = o.game && (o.intro || (!o.skipIntro && o.loadAutosave == 0 && !o.haveScript && o.screenshot.empty()));
    if (original) e.showTitleScreen();
    if (e.startup() == 0) {   // OnStartup returning 0: the engine starts the game itself
        if (!o.game) e.startNewGame(!o.noObjects);
        else if (original) e.jumpToStartPage();
        else e.menuNewGame();
    }
    if (o.loadAutosave != 0) {     // --load-autosave: the new game that was just made is replaced by the autosave, as F9 would
        std::string msg;
        if (!loadAutosaveInto(s, o.loadAutosave, msg)) throw Error("--load-autosave: " + msg);
    }
    if (!o.page.empty()) e.goToPage(resolvePage(e.view(), o.page), false);
    if (o.haveScroll) e.setScroll(o.scrollX, o.scrollY);
}

int selftestAllPages(Setup& s, const Options& o) {
    Engine& e = *s.engine;
    Frame f;
    int ok = 0, failed = 0, fell = 0;
    for (int n = 1; n <= e.view().pageCount(); n++) {
        try {
            e.goToPage(n, false);
            e.render(f);
            ok++;
            if (e.view().paletteFellBack()) {
                fell++;
                std::printf("palette fallback: page %d %s -> %s\n", n, e.view().page().name.c_str(), e.view().paletteFile().c_str());
            }
        } catch (const Error& err) {
            failed++;
            std::printf("FAILED page %d: %s\n", n, err.what());
        }
    }
    std::printf("selftest: %d pages rendered, %d failed, %d used a fallback palette\n", ok, failed, fell);
    return failed ? 1 : 0;
}

std::string titleFor(const Engine& e, const std::string& last, const std::string& gEntry, bool gMode) {
    const PageView& v = e.view();
    std::string t = "Biosys \xe2\x80\x94 " + v.page().name + " (#" + std::to_string(v.pageNumber()) + ") \xe2\x80\x94 " +
                    (v.paletteFile().empty() ? "-" : v.paletteFile()) + " \xe2\x80\x94 " + timeSlotName(v.timeSlot());
    if (v.paletteSetOverride() >= 0) t += " [set " + std::to_string(v.paletteSetOverride()) + "]";
    t += " \xe2\x80\x94 " + (gMode ? "goto: " + gEntry + "_" : last);
    return t;
}

// ---- cursors (spec 5.1): BIODATA/CURSORS/*.BMP shown as SDL colour cursors, hot spot = bitmap centre ----

class CursorSet {
public:
    ~CursorSet() {
        for (auto& c : cache_) if (c.second) SDL_DestroyCursor(c.second);
        if (fallbackArrow_) SDL_DestroyCursor(fallbackArrow_);
        if (fallbackHand_) SDL_DestroyCursor(fallbackHand_);
    }
    SDL_Cursor* get(Engine& e, const std::string& stem, int scale) {
        auto it = cache_.find(stem);
        if (it != cache_.end() && it->second) return it->second;
        SDL_Cursor* c = nullptr;
        try {
            Bitmap8 bm = e.loadCursor(stem);
            SDL_Surface* s = SDL_CreateSurface(bm.image.w, bm.image.h, SDL_PIXELFORMAT_ARGB8888);
            if (s) {
                Uint32* px = static_cast<Uint32*>(s->pixels);
                for (int y = 0; y < bm.image.h; y++)
                    for (int x = 0; x < bm.image.w; x++) {
                        const Color& col = bm.palette.c[bm.image.at(x, y)];
                        bool cyan = col.r == 0 && col.g == 255 && col.b == 255;   // colour key
                        px[y * (s->pitch / 4) + x] = cyan ? 0u : (0xFF000000u | (Uint32(col.r) << 16) | (Uint32(col.g) << 8) | col.b);
                    }
                SDL_Surface* big = scale > 1 ? SDL_ScaleSurface(s, s->w * scale, s->h * scale, SDL_SCALEMODE_NEAREST) : nullptr;
                SDL_Surface* use = big ? big : s;
                c = SDL_CreateColorCursor(use, use->w / 2, use->h / 2);
                if (big) SDL_DestroySurface(big);
                SDL_DestroySurface(s);
            }
        } catch (const Error& err) {
            std::fprintf(stderr, "cursor %s: %s\n", stem.c_str(), err.what());
        }
        if (!c) {                                   // fall back to a system cursor
            if (!fallbackArrow_) fallbackArrow_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
            if (!fallbackHand_) fallbackHand_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
            c = stem == "CURSOR" || stem == "CURSE" || stem == "MNUARROW" ? fallbackArrow_ : fallbackHand_;
            cache_[stem] = nullptr;
            return c;
        }
        cache_[stem] = c;
        return c;
    }

private:
    std::map<std::string, SDL_Cursor*> cache_;
    SDL_Cursor* fallbackArrow_ = nullptr;
    SDL_Cursor* fallbackHand_ = nullptr;
};

// The screens in the order the player counts them: the main screen first, then the others as SDL lists them.
struct Screens {
    std::vector<SDL_DisplayID> ids;
    std::vector<std::string> names;
};
Screens listScreens() {
    Screens sc;
    int n = 0;
    SDL_DisplayID* d = SDL_GetDisplays(&n);
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    for (int i = 0; d && i < n; i++) if (d[i] == primary) sc.ids.push_back(d[i]);
    for (int i = 0; d && i < n; i++) if (d[i] != primary) sc.ids.push_back(d[i]);
    SDL_free(d);
    for (SDL_DisplayID id : sc.ids) {
        const char* nm = SDL_GetDisplayName(id);
        sc.names.push_back(nm ? nm : "");
    }
    return sc;
}
int screenOfWindow(const Screens& sc, SDL_Window* window) {     // 1-based; the main screen when unknown
    const SDL_DisplayID id = SDL_GetDisplayForWindow(window);
    for (size_t i = 0; i < sc.ids.size(); i++) if (sc.ids[i] == id) return static_cast<int>(i) + 1;
    return 1;
}

// What the player changed with keys while playing (volume, mute, full screen, the screen) goes back into the preferences file when the window closes:
// only those values, and only when they differ from what the run started with.
void saveRuntimePrefs(const Options& start, SDL_Window* window, Engine& e) {
    if (start.configPath.empty() || start.noConfig) return;
    std::map<std::string, std::string> changed;
    const OptTable<Options>& table = optionTable();
    Options now = start;
    now.volMaster = e.mixer().masterVolume();
    now.startMuted = e.mixer().muted();
    now.fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    {
        const Screens sc = listScreens();
        now.display = displayPrefFor(start.display, screenOfWindow(sc, window), sc.names);
    }
    for (const char* key : {"volume", "start-muted", "fullscreen", "display"}) {
        const OptSpec<Options>* sp = table.byKey(key);
        if (sp && sp->get(now) != sp->get(start)) changed[key] = sp->get(now);
    }
    if (changed.empty()) return;
    std::string text;
    if (std::FILE* f = std::fopen(start.configPath.c_str(), "rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
        std::fclose(f);
    } else {
        text = table.defaultFile();
    }
    const std::string out = updatePrefsText(text, changed);
    if (std::FILE* f = std::fopen(start.configPath.c_str(), "wb")) {
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
    } else {
        std::fprintf(stderr, "biosys: cannot save the preferences to %s\n", start.configPath.c_str());
    }
}

int runWindow(Setup& s, const Options& o) {
    Engine& e = *s.engine;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    // --display: the screen the window opens on (the main screen when the word names none)
    Screens screens = listScreens();
    s.screenNames = screens.names;
    if (o.display == "list") {
        for (size_t i = 0; i < screens.ids.size(); i++) {
            SDL_Rect b{0, 0, 0, 0};
            SDL_GetDisplayBounds(screens.ids[i], &b);
            std::printf("%zu  %s  %d x %d%s\n", i + 1, screens.names[i].c_str(), b.w, b.h, i == 0 ? "  (main screen)" : "");
        }
        SDL_Quit();
        return 0;
    }
    int startScreen = pickDisplay(o.display, screens.names);
    if (!o.display.empty() && !startScreen)
        std::fprintf(stderr, "biosys: no screen matches --display %s (see --display list); using the main screen\n", o.display.c_str());
    if (!startScreen) startScreen = 1;
    const SDL_DisplayID startId = screens.ids.empty() ? SDL_GetPrimaryDisplay() : screens.ids[static_cast<size_t>(startScreen) - 1];
    // start at 2x when the display has room for it, else at the largest whole-number scale that fits (at least 1)
    int scale = 2;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(startId, &usable))
        scale = std::max(1, std::min(2, static_cast<int>(fitPicture(usable.w, usable.h, Frame::W, Frame::H).scale)));
    // The window is created on its screen as a normal window and made full screen afterwards: the full screen is then on the screen the
    // window is on, the same way as with Option+Return.
    SDL_Window* window = nullptr;
    {
        const SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Biosys");
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, o.windowW ? o.windowW : Frame::W * scale);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, o.windowH ? o.windowH : Frame::H * scale);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, static_cast<Sint64>(SDL_WINDOWPOS_CENTERED_DISPLAY(startId)));
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, static_cast<Sint64>(SDL_WINDOWPOS_CENTERED_DISPLAY(startId)));
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
        window = SDL_CreateWindowWithProperties(props);
        SDL_DestroyProperties(props);
    }
    if (window && o.fullscreen) {
        SDL_SetWindowFullscreen(window, true);
        SDL_SyncWindow(window);
    }
    if (window && !o.display.empty()) {
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(window, &pw, &ph);
        const int on = screenOfWindow(screens, window);
        std::printf("biosys: window on screen %d (%s), %d x %d pixels%s\n", on, screens.names.empty() ? "" : screens.names[static_cast<size_t>(on) - 1].c_str(), pw, ph,
                    o.fullscreen ? ", full screen" : "");
    }
    SDL_Renderer* ren = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!ren) {
        std::fprintf(stderr, "window/renderer failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderLogicalPresentation(ren, Frame::W, Frame::H, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    SDL_SetRenderVSync(ren, 1);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, Frame::W, Frame::H);
    if (!tex) {
        std::fprintf(stderr, "texture failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    std::unique_ptr<SdlAudio> audio;
    if (!o.mute) {
        try {
            audio.reset(new SdlAudio(e.mixer()));
        } catch (const Error& err) {
            std::fprintf(stderr, "biosys: warning: no audio (%s); continuing silently\n", err.what());
        }
    }
    CursorSet cursors;
    e.setPointerCapture(true);

    Frame frame;
    std::vector<Uint32> argb(static_cast<size_t>(Frame::W) * Frame::H);
    bool running = true, gMode = false, cursorHidden = false, wasCaptured = false, clipShown = false;
    SDL_Rect shownClip{0, 0, 0, 0};
    std::string gEntry, last = "ready", shownTitle, shownCursor;
    Uint64 volumeShownUntil = 0;     // the master volume is shown in the title until this time
    PauseState pause(o.pauseOnFocusLoss);   // minimised / lost focus / mouse released (windowlogic.h)
    bool swallowButtonUp = false;           // the click that takes the mouse back is not a click in the game
    MotionScaler scaler;                    // --mouse-sensitivity
    scaler.setFactor(o.mouseSensitivity);
    DoublePress f9;                         // F9: load the newest autosave after a second press within 3 s
    int f9Newest = 0;
    Uint64 lastLoopTicks = SDL_GetTicks();
    int overrideSet = e.view().paletteSetOverride();

    auto guarded = [&](auto&& fn) {
        try {
            fn();
        } catch (const Error& err) {
            last = std::string("error: ") + err.what();
            std::fprintf(stderr, "%s\n", last.c_str());
        }
    };
    // The status-screen hold (see StatusHold): the Caps Lock state or the backquote key is the game's Caps Lock key being down.
    StatusHold hold;
    auto syncHold = [&]() {
        const bool lockOn = (SDL_GetModState() & SDL_KMOD_CAPS) != 0;
        switch (hold.update(lockOn, e.menuOpen() || e.textInputActive())) {
        case StatusHold::Action::Down: guarded([&] { e.keyDown(Key::Other, kVkCapsLock); }); break;
        case StatusHold::Action::Up: guarded([&] { e.keyUp(Key::Other, kVkCapsLock); }); break;
        default: break;
        }
    };
    auto goPage = [&](int n) {
        guarded([&] {
            e.goToPage(n);
            last = "page " + e.view().page().name;
        });
    };
    // --widescreen: on a wide frame the renderer's logical size is (640 + 2 * wideK) x 480 and the 4:3 frame sits wideK columns right of the
    // origin; every other frame has the logical size 640 x 480 as before. Pointer positions are converted at these three places only.
    int logicalW = Frame::W, wideK = 0;
    auto setLogical = [&](int w) {
        if (w == logicalW) return;
        SDL_SetRenderLogicalPresentation(ren, w, Frame::H, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
        logicalW = w;
        wideK = (w - Frame::W) / 2;
    };
    // --interface overlay: while the last frame drawn was an overlay frame (overlayShown) the window is the picture's render space and the pointer goes
    // through the maps of app/overlaylayout.h (view rows linearly, the bottom rows through the strip, the dock at the bottom edge)
    bool overlayOn = o.interfaceOverlay, overlayShown = false;
    ovl::Layout ovLayout;
    OverlayScene ovScene;
    ovl::MotionCarry carry;               // the captured pointer's motion in engine pixels, the rest carried
    double lastRX = 0, lastRY = 0;        // where the pointer was last seen / put, render pixels (overlay frames)
    bool haveLast = false;                // lastRX / lastRY are known (the pointer was warped or seen since it was captured)
    int menuOx = 0, menuOy = 0, menuMk = 1;   // where the settings menu's 640 x 480 grid is in an overlay frame
    auto fromWindow = [&](float wx, float wy, float& x, float& y) {     // window position -> frame position (the pointer is for the centre only)
        SDL_RenderCoordinatesFromWindow(ren, wx, wy, &x, &y);
        if (overlayShown) {
            const ovl::EnginePoint p = ovl::windowToEngine(ovLayout, x, y);
            x = static_cast<float>(p.x);
            y = static_cast<float>(p.y);
            return;
        }
        if (wideK > 0) x = static_cast<float>(widePointerFrameX(static_cast<int>(std::floor(x)), wideK));
    };
    auto toWindow = [&](float x, float y, float& wx, float& wy) {
        if (overlayShown) {
            double rx, ry;
            ovl::engineToWindow(ovLayout, x, y, rx, ry);
            SDL_RenderCoordinatesToWindow(ren, static_cast<float>(rx), static_cast<float>(ry), &wx, &wy);
            return;
        }
        SDL_RenderCoordinatesToWindow(ren, x + static_cast<float>(wideK), y, &wx, &wy);
    };
    auto toRender = [&](SDL_Event& ev, float& x, float& y) {
        if (ev.type == SDL_EVENT_MOUSE_MOTION) { x = ev.motion.x; y = ev.motion.y; }
        else { x = ev.button.x; y = ev.button.y; }
        fromWindow(x, y, x, y);
    };

    std::unique_ptr<CleanFront> cleanP;      // --picture clean: the workers (app/clean.h); outlive the layers that hold their results
    if (o.pictureClean) {
        CleanOptions co;
        co.texScale = o.cleanTexScale;
        co.slew = o.cleanSlew;
        cleanP.reset(new CleanFront(co));
    }
    SDL_Texture* texClean = nullptr;         // the cleaned 640 x 480 frame (640 * k x 480 * k)
    int texCleanW = 0, texCleanH = 0, cleanShownScale = 0;
    bool cleanShownAlpha = false;
    std::unique_ptr<PanoramaLayer> panoramaP(new PanoramaLayer(ren));   // destroyed before the renderer
    PanoramaLayer& panorama = *panoramaP;
    panorama.setLinear(o.panoramaLinear);
    if (cleanP) panorama.setClean(o.cleanTexScale);
    bool smoothPano = o.smoothPanorama, panoShown = false, wideOn = o.widescreen;
    WideController wideCtl;
    SDL_Texture* texWide = nullptr;      // the frame (and the side rain) on the wide canvas
    int texWideW = 0;
    std::vector<Uint32> argbWide;
    Frame noFilterFrame;
    Uint64 shotDue = 0;                  // --test-shot: when to take the picture
    std::unique_ptr<SharpTextLayer> sharpP(o.textSharp ? new SharpTextLayer(ren) : nullptr);   // --text sharp; destroyed before the renderer
    Engine::TextFrame textFrame;
    SDL_Texture* texMenu = nullptr;      // the settings menu / New game screen / hint layer (app/settingsmenu_draw.h), over the picture
    int menuTexK = 0;
    uint64_t menuSig = 0;
    std::vector<uint32_t> menuLayer;
    // The layer is the app's, drawn over everything the engine and the other layers made; it is never part of the engine's frame. A menu
    // darkens the whole picture; the layer sits on the 640 x 480 grid, centred (in a wide frame: in the 4:3 middle).
    auto drawMenuLayer = [&](int ow, int oh, bool hint) {
        const int k = std::max(1, std::min(8, std::min(ow / Frame::W, oh / Frame::H)));
        const uint64_t sig = s.menu.revision() * 64 + (hint ? 1 : 0) + (s.menu.isOpen() ? 2 : 0) + static_cast<uint64_t>(k) * 4;
        if (!texMenu || k != menuTexK) {
            if (texMenu) SDL_DestroyTexture(texMenu);
            texMenu = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, Frame::W * k, Frame::H * k);
            if (!texMenu) return;
            SDL_SetTextureBlendMode(texMenu, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(texMenu, SDL_SCALEMODE_NEAREST);
            menuTexK = k;
            menuSig = 0;
        }
        if (sig != menuSig) {
            if (renderMenuLayer(s.menu, menuFontsOf(s), k, hint, menuLayer)) SDL_UpdateTexture(texMenu, nullptr, menuLayer.data(), Frame::W * k * 4);
            menuSig = sig;
        }
        if (menuLayer.empty()) return;
        if (s.menu.isOpen()) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, static_cast<Uint8>(kMenuDim));
            SDL_RenderFillRect(ren, nullptr);
        }
        SDL_FRect dst{static_cast<float>(wideK), 0.0f, static_cast<float>(Frame::W), static_cast<float>(Frame::H)};
        if (overlayShown) {      // render pixels, no logical presentation: the grid at the whole-number scale k, centred
            menuMk = k;
            menuOx = (ow - Frame::W * k) / 2;
            menuOy = (oh - Frame::H * k) / 2;
            dst = SDL_FRect{static_cast<float>(menuOx), static_cast<float>(menuOy), static_cast<float>(Frame::W * k), static_cast<float>(Frame::H * k)};
        }
        SDL_RenderTexture(ren, texMenu, nullptr, &dst);
    };
    auto takeTestShot = [&](bool wideFrame) {      // --test-shot: the window's picture, once, shortly before it closes
        if (!(shotDue && SDL_GetTicks() >= shotDue)) return;
        shotDue = 0;
        if (SDL_Surface* px = SDL_RenderReadPixels(ren, nullptr)) {
            SDL_Surface* conv = SDL_ConvertSurface(px, SDL_PIXELFORMAT_ARGB8888);
            if (conv) {
                std::vector<uint32_t> rgb(static_cast<size_t>(conv->w) * conv->h);
                for (int y = 0; y < conv->h; y++)
                    for (int x = 0; x < conv->w; x++)
                        rgb[static_cast<size_t>(y) * conv->w + x] = static_cast<const Uint32*>(static_cast<const void*>(static_cast<const char*>(conv->pixels) + static_cast<size_t>(y) * conv->pitch))[x] & 0x00FFFFFFu;
                try { writeBmp24(o.testShot, conv->w, conv->h, rgb); } catch (const Error& err) { std::fprintf(stderr, "%s\n", err.what()); }
                std::printf("testshot %s %dx%d wide=%d logical=%d overlay=%d\n", o.testShot.c_str(), conv->w, conv->h, wideFrame ? 1 : 0, logicalW, overlayShown ? 1 : 0);
                std::fflush(stdout);
                SDL_DestroySurface(conv);
            }
            SDL_DestroySurface(px);
        }
    };
    // --interface overlay: a frame with the 368-row view (Engine::viewFrame). The view fills the window (scale k = min(H / 368, W / 640), not a whole
    // number), the panorama is smooth and as wide as the window allows, the bars of the 640 x 480 frame are not drawn: the status words, the message
    // line and the inventory strip are the overlays of app/overlaycompose.h (drawn from the text runs of the top bar and the bar rows of the frame).
    std::unique_ptr<OverlayRenderer> ovR(new OverlayRenderer(ren));   // destroyed before the renderer
    SDL_Texture* texOvView = nullptr;      // the view rows of the frame (and the rain beside them): 640 + 2 * extra columns x 368
    int texOvW = 0;
    std::vector<Uint32> argbOv;
    std::vector<uint32_t> ovTextArgb;      // the sharp text of the view
    int ovTextW = 0, ovTextH = 0;
    uint64_t ovTextSig = 0;
    auto drawOverlayFrame = [&]() {
        int ow = 0, oh = 0;
        SDL_GetRenderOutputSize(ren, &ow, &oh);
        if (!overlayShown) {
            SDL_SetRenderLogicalPresentation(ren, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
            overlayShown = true;
            logicalW = -1;
            wideK = 0;
        }
        ovLayout = ovl::layoutFor(ow, oh);
        const ovl::Layout& L = ovLayout;
        const bool statusOp = e.simStatusOpaque();       // the status screen (Tab) with a palette of its own: the picture keeps its width, the panorama is frozen
        const bool wantNoFilter = e.pageFilterActive() && !statusOp;
        e.setSplitLayers(true);                          // the held hand / the object animation come as layers (shifted to the picture's edge when it is wide)
        if (wantNoFilter) guarded([&] { e.renderNoFilter(noFilterFrame); });   // before the real render: panorama() must describe that one
        guarded([&] { e.renderText(textFrame); });
        e.setSplitLayers(false);
        const ViewLayers layers = e.viewLayers();
        const Palette& pal = textFrame.palette;
        const bool pano = e.panorama().active;
        bool smooth = false;
        int showL = 0, showR = 0;
        if (pano) {
            try {
                WideRequest wr;
                wr.ctl = &wideCtl;
                wr.cover = &textFrame.full;                          // (without the held image: a held object never causes the 4:3 fallback)
                wr.noFilter = wantNoFilter ? &noFilterFrame : nullptr;
                wr.extra = L.extraCols;
                wr.keep = statusOp;
                panorama.setStatusScreen(statusOp);
                panorama.plan(e.panorama(), textFrame.full, static_cast<u32>(SDL_GetTicks()), 1.0, &wr);
                if (panorama.decision().wide) { showL = panorama.decision().showLeft; showR = panorama.decision().showRight; }
                smooth = true;
            } catch (const Error& err) {
                last = std::string("error: ") + err.what();
                std::fprintf(stderr, "%s\n", last.c_str());
                smoothPano = false;
            }
        } else {
            panorama.setStatusScreen(false);
            if (panoShown) panorama.reset();
            wideCtl.reset();
        }
        // the held image goes back into the frame at its own place (the cover was measured without it)
        layers.blitInto(textFrame.full);
        layers.blitInto(textFrame.free);
        frame = sharpP ? textFrame.free : textFrame.full;
        for (size_t i = 0; i < argb.size(); i++) {
            const Color& c = pal.c[frame.px[i]];
            argb[i] = 0xFF000000u | (Uint32(c.r) << 16) | (Uint32(c.g) << 8) | c.b;
        }
        {
            int px = 0, py = 0;
            e.pointerPos(px, py);
            SceneInput si;
            si.frame = &textFrame.full;
            si.pal = &pal;
            si.runs = &textFrame.runs;
            si.barMode = e.barMode();
            si.flashing = e.bar().flashing(e.now());
            si.pointerInBar = py >= ovl::kBarY;
            si.barObjects = e.bar().objectCount();
            si.nowMs = e.now();
            ovScene.update(L, si);
        }
        if (cleanP) {       // the cleaned panorama texture still applies (the cleaned frame itself is not used by this layout)
            guarded([&] {
                // the status screen up: the panorama page is not given to the cleaner (it would be cleaned again with the status palette)
                cleanP->step(e, frame, pal, pano && !statusOp ? &e.panorama() : nullptr, std::max(1, static_cast<int>(std::lround(L.k))));
                std::vector<CleanBand> up;
                cleanP->popUploads(e.view().pageSerial(), CleanFront::kUploadBytes, up);
                panorama.applyCleanBands(up, e.view().pageSerial());
            });
        }
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        panoShown = smooth;
        const double pL = L.viewX - showL * L.k, pR = L.viewX + (Frame::W + showR) * L.k;
        if (!L.fourThree && (pL > 0.5 || pR < ow - 0.5)) guarded([&] { ovR->drawAmbient(ovScene, L, pL, pR); });
        if (smooth) guarded([&] { panorama.drawPlannedAffine(e.panorama(), frame, argb, L.k, L.viewX, L.viewY); });
        if (smooth && statusOp && showL + showR > 0) {     // the status screen is the focus: the panorama beside the centre is dimmed
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, static_cast<Uint8>(std::lround(255.0 * (1.0 - kWideStatusSideBrightness))));
            const SDL_FRect l{static_cast<float>(L.viewX - showL * L.k), static_cast<float>(L.viewY), static_cast<float>(showL * L.k), static_cast<float>(kViewH * L.k)};
            const SDL_FRect r{static_cast<float>(L.viewX + kViewW * L.k), static_cast<float>(L.viewY), static_cast<float>(showR * L.k), static_cast<float>(kViewH * L.k)};
            if (showL > 0) SDL_RenderFillRect(ren, &l);
            if (showR > 0) SDL_RenderFillRect(ren, &r);
        }
        // the view rows of the frame (transparent where the panorama shows through) and the rain beside the centre
        const int ex = smooth ? L.extraCols : 0, W2 = Frame::W + 2 * ex;
        argbOv.assign(static_cast<size_t>(W2) * kViewH, 0);
        for (int y = 0; y < kViewH; y++)
            std::copy(argb.begin() + static_cast<std::ptrdiff_t>(ovl::kTopH + y) * Frame::W, argb.begin() + static_cast<std::ptrdiff_t>(ovl::kTopH + y + 1) * Frame::W,
                      argbOv.begin() + static_cast<std::ptrdiff_t>(y) * W2 + ex);
        if (smooth && !panorama.rain().empty()) {
            const std::vector<int16_t>& rain = panorama.rain();
            for (int y = 0; y < kViewH; y++) {
                for (int i = 0; i < showL; i++) {
                    const int r = wideSideRain(rain, -1 - i, y);
                    if (r >= 0) argbOv[static_cast<size_t>(y) * W2 + (ex - 1 - i)] = 0xFF000000u | (Uint32(pal.c[r].r) << 16) | (Uint32(pal.c[r].g) << 8) | pal.c[r].b;
                }
                for (int i = 0; i < showR; i++) {
                    const int r = wideSideRain(rain, kViewW + i, y);
                    if (r >= 0) argbOv[static_cast<size_t>(y) * W2 + (ex + kViewW + i)] = 0xFF000000u | (Uint32(pal.c[r].r) << 16) | (Uint32(pal.c[r].g) << 8) | pal.c[r].b;
                }
            }
        }
        if (!texOvView || texOvW != W2) {
            if (texOvView) SDL_DestroyTexture(texOvView);
            texOvView = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W2, kViewH);
            if (texOvView) SDL_SetTextureBlendMode(texOvView, SDL_BLENDMODE_BLEND);
            texOvW = W2;
        }
        if (texOvView) {
            SDL_SetTextureScaleMode(texOvView, L.whole() ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
            SDL_UpdateTexture(texOvView, nullptr, argbOv.data(), W2 * 4);
            const SDL_FRect dst{static_cast<float>(L.viewX - ex * L.k), static_cast<float>(L.viewY), static_cast<float>(W2 * L.k), static_cast<float>(kViewH * L.k)};
            SDL_RenderTexture(ren, texOvView, nullptr, &dst);
        }
        if (sharpP) {       // the sharp text of the view, at the view's scale
            const int ix = static_cast<int>(std::floor(L.viewX)), iy = static_cast<int>(std::floor(L.viewY));
            const double fx = L.viewX - ix, fy = L.viewY - iy;
            uint64_t sig = textOverlaySignature(textFrame, static_cast<int>(L.k * 64.0));
            sig = (sig ^ static_cast<uint64_t>(fx * 1024.0) ^ (static_cast<uint64_t>(fy * 1024.0) << 11) ^ (static_cast<uint64_t>(ow) << 24) ^ (static_cast<uint64_t>(oh) << 40)) | 1;
            if (sig != ovTextSig) {
                renderTextOverlayScaled(textFrame, L.k, ovl::kTopH, kViewH, fx, fy, "topbar", ovTextArgb, ovTextW, ovTextH);
                ovTextSig = sig;
            }
            guarded([&] { ovR->drawBitmap(100, ovTextSig, ovTextArgb, ovTextW, ovTextH, ix, iy); });
        }
        guarded([&] { ovR->drawOverlays(ovScene, L); });
        {
            const bool hint = false;
            if (s.menu.isOpen()) guarded([&] { drawMenuLayer(ow, oh, hint); });
        }
        takeTestShot(smooth && showL + showR > 0);
        SDL_RenderPresent(ren);
    };
    auto drawFrame = [&]() {
        if (overlayOn && e.viewFrame()) { drawOverlayFrame(); return; }
        if (overlayShown) { overlayShown = false; logicalW = -1; wideK = 0; }      // back to the logical presentation of the 640 x 480 frame
        const bool statusOp = e.simStatusOpaque();       // the status screen (Tab) with a palette of its own: the picture keeps its width, the panorama is frozen
        const bool wantNoFilter = wideOn && smoothPano && e.pageFilterActive() && !statusOp;
        e.setSplitLayers(wideOn && smoothPano);          // the held hand / the object animation come as layers (shifted to the picture's edge when it is wide)
        if (wantNoFilter) guarded([&] { e.renderNoFilter(noFilterFrame); });   // before the real render: panorama() must describe that one
        if (sharpP) guarded([&] { e.renderText(textFrame); frame = textFrame.free; });   // the frame without its low-resolution text
        else guarded([&] { e.render(frame); });
        e.setSplitLayers(false);
        const ViewLayers layers = e.viewLayers();
        const Palette& pal = e.view().palette();
        int ow = 0, oh = 0;
        SDL_GetRenderOutputSize(ren, &ow, &oh);
        bool smooth = false, wideFrame = false;
        const bool pano = smoothPano && e.panorama().active;
        const double scale = std::max(1.0, std::floor(std::min(static_cast<double>(ow) / Frame::W, static_cast<double>(oh) / Frame::H)));
        bool cleanFrame = false;             // the cleaned frame is what this frame draws (else the plain enlarged one)
        WideWindow ww;
        bool planned = false;
        if (pano) {   // the camera and the width of the picture; the panorama is drawn below, after the frame is complete
            try {
                WideRequest wr;
                const WideRequest* pw = nullptr;
                if (wideOn) {
                    ww = wideWindow(ow, oh);
                    wr.ctl = &wideCtl;
                    wr.cover = sharpP ? &textFrame.full : &frame;    // (without the held image: a held object never causes the 4:3 fallback)
                    wr.noFilter = wantNoFilter ? &noFilterFrame : nullptr;
                    wr.extra = ww.extra;
                    wr.keep = statusOp;
                    pw = &wr;
                } else {
                    wideCtl.reset();
                }
                panorama.setStatusScreen(statusOp);
                panorama.plan(e.panorama(), frame, static_cast<u32>(SDL_GetTicks()), scale, pw);
                wideFrame = panorama.decision().wide;
                planned = true;
            } catch (const Error& err) {
                last = std::string("error: ") + err.what();
                std::fprintf(stderr, "%s\n", last.c_str());
                smoothPano = false;
                wideFrame = false;
            }
        } else {
            panorama.setStatusScreen(false);
            if (panoShown) panorama.reset();
            wideCtl.reset();
        }
        // the held image goes back into the frame at its own place (the cover was measured without it); the cleaner and the texture see the finished frame
        layers.blitInto(frame);
        if (sharpP) { layers.blitInto(textFrame.free); layers.blitInto(textFrame.full); }
        for (size_t i = 0; i < argb.size(); i++) {
            const Color& c = pal.c[frame.px[i]];
            argb[i] = 0xFF000000u | (Uint32(c.r) << 16) | (Uint32(c.g) << 8) | c.b;
        }
        if (cleanP) {
            guarded([&] {
                // while the status screen is up the panorama page is not given to the cleaner (its palette is the status palette: the page would be cleaned again
                // and the old colours come back band by band after the release); the frame is cleaned as an opaque flat picture
                const Engine::PanoramaFrame* cpf = pano && !statusOp ? &e.panorama() : nullptr;
                cleanP->step(e, frame, pal, cpf, static_cast<int>(scale));
                std::vector<CleanBand> up;
                cleanP->popUploads(e.view().pageSerial(), CleanFront::kUploadBytes, up);
                panorama.applyCleanBands(up, e.view().pageSerial());
                if (cleanP->gotFrame) {
                    const CleanFrameOut& r = cleanP->out;
                    if (!texClean || texCleanW != Frame::W * r.scale || texCleanH != Frame::H * r.scale) {
                        if (texClean) SDL_DestroyTexture(texClean);
                        texCleanW = Frame::W * r.scale;
                        texCleanH = Frame::H * r.scale;
                        texClean = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, texCleanW, texCleanH);
                        if (!texClean) throw Error(std::string("clean frame texture: ") + SDL_GetError());
                    }
                    SDL_UpdateTexture(texClean, nullptr, r.argb.data(), texCleanW * 4);
                    cleanShownScale = r.scale;
                    cleanShownAlpha = r.alpha;
                }
            });
            // the newest result stands in until the next one is ready; it is used only if it was made for what is drawn now (scale, with or
            // without the panorama under it); in between the plain frame is drawn for a frame or two
            cleanFrame = texClean && cleanShownScale == std::min(4, static_cast<int>(scale)) && cleanShownAlpha == (pano && !statusOp);
            if (cleanFrame) SDL_SetTextureScaleMode(texClean, cleanShownScale == static_cast<int>(scale) ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
        }
        if (pano) {   // the panorama under the frame; the view pixels that show the bare projection turn transparent
            try {
                if (!planned) throw Error("panorama not planned");
                setLogical(wideFrame ? ww.logicalW : Frame::W);
                SDL_RenderClear(ren);
                panorama.drawPlanned(e.panorama(), frame, argb, scale);
                smooth = true;
            } catch (const Error& err) {
                if (planned) {
                    last = std::string("error: ") + err.what();
                    std::fprintf(stderr, "%s\n", last.c_str());
                }
                smoothPano = false;
                wideFrame = false;
                setLogical(Frame::W);
                SDL_RenderClear(ren);
            }
        } else {
            setLogical(Frame::W);
            SDL_RenderClear(ren);
        }
        panoShown = smooth;
        if (wideFrame) {
            const int k = ww.extra, W = ww.logicalW;
            if (!texWide || texWideW != W) {
                if (texWide) SDL_DestroyTexture(texWide);
                texWide = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, Frame::H);
                if (texWide) {
                    SDL_SetTextureScaleMode(texWide, SDL_SCALEMODE_NEAREST);
                    SDL_SetTextureBlendMode(texWide, SDL_BLENDMODE_BLEND);
                }
                texWideW = W;
            }
            argbWide.assign(static_cast<size_t>(W) * Frame::H, 0);
            if (!cleanFrame)   // (the cleaned frame is its own texture, drawn at the centre; this one then holds the side rain only)
                for (int y = 0; y < Frame::H; y++) std::copy(argb.begin() + static_cast<std::ptrdiff_t>(y) * Frame::W, argb.begin() + static_cast<std::ptrdiff_t>(y + 1) * Frame::W,
                                                             argbWide.begin() + static_cast<std::ptrdiff_t>(y) * W + k);
            const WideDecision& d = panorama.decision();
            const std::vector<int16_t>& rain = panorama.rain();
            if (!rain.empty()) {   // the rain continued into the side areas (engine/panowide.h)
                const int view0 = e.panorama().view.y, sc = std::max(1, static_cast<int>(scale));
                const int showL = d.showLeft / sc, showR = d.showRight / sc;
                for (int y = 0; y < kViewH; y++)
                    for (int i = 0; i < showL; i++) {
                        const int r = wideSideRain(rain, -1 - i, y);
                        if (r >= 0) argbWide[static_cast<size_t>(view0 + y) * W + (k - 1 - i)] = 0xFF000000u | (Uint32(pal.c[r].r) << 16) | (Uint32(pal.c[r].g) << 8) | pal.c[r].b;
                    }
                for (int y = 0; y < kViewH; y++)
                    for (int i = 0; i < showR; i++) {
                        const int r = wideSideRain(rain, kViewW + i, y);
                        if (r >= 0) argbWide[static_cast<size_t>(view0 + y) * W + (k + kViewW + i)] = 0xFF000000u | (Uint32(pal.c[r].r) << 16) | (Uint32(pal.c[r].g) << 8) | pal.c[r].b;
                    }
            }
            if (statusOp) {   // the status screen is the focus: the panorama beside it is dimmed (a black layer over the side columns of the view)
                const int view0 = e.panorama().view.y, sc = std::max(1, static_cast<int>(scale));
                const int showL = d.showLeft / sc, showR = d.showRight / sc;
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 0, 0, 0, static_cast<Uint8>(std::lround(255.0 * (1.0 - kWideStatusSideBrightness))));
                const SDL_FRect l{static_cast<float>(k - showL), static_cast<float>(view0), static_cast<float>(showL), static_cast<float>(kViewH)};
                const SDL_FRect r{static_cast<float>(k + kViewW), static_cast<float>(view0), static_cast<float>(showR), static_cast<float>(kViewH)};
                if (showL > 0) SDL_RenderFillRect(ren, &l);
                if (showR > 0) SDL_RenderFillRect(ren, &r);
            }
            if (cleanFrame) {
                const SDL_FRect centre{static_cast<float>(k), 0.0f, static_cast<float>(Frame::W), static_cast<float>(Frame::H)};
                SDL_SetTextureBlendMode(texClean, SDL_BLENDMODE_BLEND);
                SDL_RenderTexture(ren, texClean, nullptr, &centre);
            }
            if (texWide) {
                SDL_UpdateTexture(texWide, nullptr, argbWide.data(), W * 4);
                SDL_RenderTexture(ren, texWide, nullptr, nullptr);
            }
        } else if (cleanFrame) {
            SDL_SetTextureBlendMode(texClean, cleanShownAlpha ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
            SDL_RenderTexture(ren, texClean, nullptr, nullptr);
        } else {
            SDL_SetTextureBlendMode(tex, smooth ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
            SDL_UpdateTexture(tex, nullptr, argb.data(), Frame::W * 4);
            SDL_RenderTexture(ren, tex, nullptr, nullptr);
        }
        if (sharpP) {   // the sharp text over the enlarged frame
            sharpP->draw(textFrame, std::max(1, std::min(ow / Frame::W, oh / Frame::H)), wideFrame ? wideK : 0);
        }
        {
            const bool hint = !s.menu.isOpen() && s.game && e.gameMenuOpen();      // "F10 settings" while the engine's main menu is shown
            if (s.menu.isOpen() || hint) guarded([&] { drawMenuLayer(ow, oh, hint); });
        }
        takeTestShot(wideFrame);
        SDL_RenderPresent(ren);
    };
    // The host of the pacing (app/pacing.h): the real clock, the window's events, the picture at the display's rate.
    struct SdlHost {
        std::function<bool()> ev;
        std::function<void()> dr;
        u32 now() const { return static_cast<u32>(SDL_GetTicks()); }
        void idle(u32 ms) { SDL_Delay(ms); }
        bool events() { return ev(); }
        void draw() { dr(); }
    } host;
    // Host side of the blocking animations (spec 2.8): input is discarded, only quit matters.
    host.ev = [&]() {
        SDL_Event pe;
        while (SDL_PollEvent(&pe)) {
            if (pe.type == SDL_EVENT_QUIT) running = false;
            else if (pe.type == SDL_EVENT_KEY_DOWN && pe.key.key == SDLK_ESCAPE) running = false;
        }
        return running;
    };
    host.dr = drawFrame;
    Pacer<SdlHost> pacer(host, o.passMs);
    e.setPassMs(o.passMs);
    e.setScrollAccumulate(true);
    panorama.setPassMs(o.passMs);
    // one turn of a blocking animation = one pass interval (a zoom frame, a cross-fade step, a jiggle step), or the clock wait the
    // original makes (a palette fade step, a journey); the frame stays on screen until then and the display keeps presenting
    e.setPump([&](u32& t) { return pacer.pumpTurn(t, e.pumpMaxMs()); });

    auto applyPause = [&](PauseState::Edge edge) {      // the original's inactive application: media and game clock stop (Engine::setMediaPaused)
        if (edge == PauseState::Edge::Pause) {
            e.setMediaPaused(true);
            if (audio) audio->pause(true);
        } else if (edge == PauseState::Edge::Resume) {
            if (audio) audio->pause(false);
            e.setMediaPaused(false);
        }
    };
    // The mouse let go (Option+M; also when the window gets the focus back while the pointer is beside the picture): nothing pulls the
    // pointer back, the system's arrow shows and the game is paused, so the window can be dragged to another screen. A click in the
    // picture, or Option+M again, takes it back.
    Uint64 noAutoReleaseUntil = 0;      // focus changes that our own window moves cause are not the player leaving
    bool sawFocusLoss = false;
    auto releaseMouse = [&](bool r) {
        if (r == pause.isReleased()) return;
        applyPause(pause.released(r));
        if (r) {
            SDL_SetWindowMouseRect(window, nullptr);
            SDL_SetCursor(SDL_GetDefaultCursor());
            SDL_ShowCursor();
        }
        clipShown = false;
        shownCursor.clear();
        cursorHidden = false;
        wasCaptured = false;
    };
    auto pointerOverPicture = [&]() {
        float gx = 0, gy = 0;
        int wx = 0, wy = 0, ww = 0, wh = 0;
        SDL_GetGlobalMouseState(&gx, &gy);
        SDL_GetWindowPosition(window, &wx, &wy);
        SDL_GetWindowSize(window, &ww, &wh);
        return gx >= static_cast<float>(wx) && gx < static_cast<float>(wx + ww) && gy >= static_cast<float>(wy) && gy < static_cast<float>(wy + wh);
    };
    // Option+N: the window goes to another screen and keeps its state (a full-screen window leaves full screen, moves, and enters it there).
    auto moveToScreen = [&](int n) {
        const Screens sc = listScreens();
        if (n < 1 || n > static_cast<int>(sc.ids.size())) return;
        const SDL_DisplayID id = sc.ids[static_cast<size_t>(n) - 1];
        const bool fs = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
        noAutoReleaseUntil = SDL_GetTicks() + 3000;
        if (fs) {
            SDL_SetWindowFullscreen(window, false);
            SDL_SyncWindow(window);
        }
        SDL_Rect ub;
        int ww = 0, wh = 0;
        SDL_GetWindowSize(window, &ww, &wh);
        if (SDL_GetDisplayUsableBounds(id, &ub) && (ww > ub.w || wh > ub.h)) {      // too big for that screen: the largest whole-number scale that fits
            const int k = std::max(1, static_cast<int>(fitPicture(ub.w, ub.h, Frame::W, Frame::H).scale));
            SDL_SetWindowSize(window, Frame::W * k, Frame::H * k);
        }
        SDL_SetWindowPosition(window, static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(id)), static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(id)));
        SDL_SyncWindow(window);
        if (fs) {
            SDL_SetWindowFullscreen(window, true);
            SDL_SyncWindow(window);
        }
        noAutoReleaseUntil = SDL_GetTicks() + 3000;
        wasCaptured = false;        // the pointer follows the window (it is put back at the game's pointer position)
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(window, &pw, &ph);
        const int on = screenOfWindow(sc, window);
        std::printf("biosys: window on screen %d (%s), %d x %d pixels%s\n", on, sc.names[static_cast<size_t>(on) - 1].c_str(), pw, ph, fs ? ", full screen" : "");
        std::fflush(stdout);
    };
    std::vector<std::string> introMovies;
    size_t nextMovie = 0;
    auto playMovie = [&](const std::string& file, const std::string& pal) {
        e.movies().closeAll();
        int h = e.debugPlayMovie(file, pal);
        last = h < 0 ? "movie failed: " + file : "movie " + file + (pal.empty() ? "" : " palette " + pal);
        if (h < 0) std::fprintf(stderr, "biosys: cannot play movie %s\n", file.c_str());
    };
    if (!o.movie.empty()) playMovie(o.movie, o.moviePalette);

    // ---- the settings menu (F10) and the New game screen: what only the window can read and change ----
    auto reopenAudio = [&]() {      // the audio mode changes the device's format: the device is opened again (the mixer is already in the new mode)
        if (o.mute) return;
        audio.reset();
        try {
            audio.reset(new SdlAudio(e.mixer()));
            if (pause.paused()) audio->pause(true);
        } catch (const Error& err) {
            std::fprintf(stderr, "biosys: warning: no audio (%s); continuing silently\n", err.what());
        }
    };
    auto printLive = [&](const char* why) {       // test aid: what the window holds now, read from the window's own state
        if (o.testKeys.empty()) return;
        std::printf("menulive %s smooth=%d wide=%d linear=%d sharp=%d clean=%d fullscreen=%d screen=%d pause=%d sens=%g pass=%u audio=%s paused=%d menu=%d overlay=%d\n", why, smoothPano ? 1 : 0,
                    wideOn ? 1 : 0, panorama.linear() ? 1 : 0, sharpP ? 1 : 0, cleanP ? 1 : 0, (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) ? 1 : 0,
                    screenOfWindow(listScreens(), window), pause.pausesOnFocusLoss() ? 1 : 0, scaler.factor(), pacer.clock().passMs(),
                    e.mixer().mode() == AudioMode::Enhanced ? "enhanced" : "original", pause.paused() ? 1 : 0, s.menu.isOpen() ? 1 : 0, overlayOn ? 1 : 0);
        std::fflush(stdout);
    };
    s.snapshotWindow = [&](Options& now) {
        now.smoothPanorama = smoothPano;
        now.widescreen = wideOn;
        now.interfaceOverlay = overlayOn;
        now.panoramaLinear = panorama.linear();
        now.textSharp = sharpP != nullptr;
        now.pictureClean = cleanP != nullptr;
        now.fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
        now.display = std::to_string(screenOfWindow(listScreens(), window));
        now.pauseOnFocusLoss = pause.pausesOnFocusLoss();
        now.mouseSensitivity = scaler.factor();
        now.passMs = pacer.clock().passMs();
    };
    s.applyWindow = [&](const smenu::Change& c) {
        const Options& cu = s.cur;
        const std::string& k = c.key;
        if (k == "panorama") smoothPano = cu.smoothPanorama;
        else if (k == "panorama-filter") panorama.setLinear(cu.panoramaLinear);
        else if (k == "widescreen") wideOn = cu.widescreen;
        else if (k == "interface") { overlayOn = cu.interfaceOverlay; if (overlayOn) { smoothPano = true; wideOn = true; } }
        else if (k == "picture") {
            if (cu.pictureClean && !cleanP) {
                CleanOptions co;
                co.texScale = cu.cleanTexScale;
                co.slew = cu.cleanSlew;
                cleanP.reset(new CleanFront(co));
                panorama.setClean(cu.cleanTexScale);
            } else if (!cu.pictureClean && cleanP) {
                panorama.setClean(0);
                cleanP.reset();
            }
        } else if (k == "text") {
            if (cu.textSharp && !sharpP) sharpP.reset(new SharpTextLayer(ren));
            else if (!cu.textSharp) sharpP.reset();
        } else if (k == "fullscreen") {
            if (((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0) != cu.fullscreen) {
                noAutoReleaseUntil = SDL_GetTicks() + 3000;
                SDL_SetWindowFullscreen(window, cu.fullscreen);
                SDL_SyncWindow(window);
                noAutoReleaseUntil = SDL_GetTicks() + 3000;
            }
        } else if (k == "display") {
            const int n = std::atoi(c.text.c_str());
            if (n >= 1 && n != screenOfWindow(listScreens(), window)) moveToScreen(n);
        } else if (k == "audio") reopenAudio();
        else if (k == "pass-ms") {
            pacer.clock() = PassClock(cu.passMs);
            panorama.setPassMs(cu.passMs);
        } else if (k == "mouse-sensitivity") scaler.setFactor(cu.mouseSensitivity);
        else if (k == "pause-on-focus-loss") applyPause(pause.setPauseOnFocusLoss(cu.pauseOnFocusLoss));
        printLive(("set " + k).c_str());
    };
    // A menu going up or down: the game waits (the third and fourth pause reason, see PauseState), the pointer is the desktop's
    auto menuSync = [&]() {
        const bool up = s.menu.isOpen();
        if (up == pause.isMenu()) return;
        applyPause(pause.menu(up));
        clipShown = false;
        shownCursor.clear();
        cursorHidden = false;
        wasCaptured = false;
        if (up) {
            SDL_SetWindowMouseRect(window, nullptr);
            SDL_SetCursor(SDL_GetDefaultCursor());
            SDL_ShowCursor();
        }
        printLive(up ? "open" : "close");
    };
    auto menuPollNow = [&]() {
        guarded([&] { menuPoll(s, menuSync); });
        menuSync();
    };
    auto menuGrid = [&](float wx, float wy, int& gx, int& gy) {      // window position -> the 640 x 480 grid (in a wide frame the grid is the middle)
        float lx = 0, ly = 0;
        SDL_RenderCoordinatesFromWindow(ren, wx, wy, &lx, &ly);
        if (overlayShown) {
            gx = static_cast<int>(std::floor((lx - static_cast<float>(menuOx)) / static_cast<float>(menuMk)));
            gy = static_cast<int>(std::floor((ly - static_cast<float>(menuOy)) / static_cast<float>(menuMk)));
            return;
        }
        gx = static_cast<int>(std::floor(lx)) - wideK;
        gy = static_cast<int>(std::floor(ly));
    };
    std::set<SDL_Keycode> swallowedKeys;     // keys that went down for the menu: their release does not reach the game either
    auto mkeyOf = [](SDL_Keycode k, smenu::MKey& out) {
        switch (k) {
        case SDLK_UP: out = smenu::MKey::Up; return true;
        case SDLK_DOWN: out = smenu::MKey::Down; return true;
        case SDLK_LEFT: out = smenu::MKey::Left; return true;
        case SDLK_RIGHT: out = smenu::MKey::Right; return true;
        case SDLK_TAB: out = smenu::MKey::Tab; return true;
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: out = smenu::MKey::Return; return true;
        case SDLK_ESCAPE: out = smenu::MKey::Escape; return true;
        case SDLK_F10: out = smenu::MKey::F10; return true;
        case SDLK_PAGEUP: out = smenu::MKey::PageUp; return true;
        case SDLK_PAGEDOWN: out = smenu::MKey::PageDown; return true;
        case SDLK_HOME: out = smenu::MKey::Home; return true;
        case SDLK_END: out = smenu::MKey::End; return true;
        default: return false;
        }
    };
    s.logic.newGameScreen = s.game != nullptr;       // set after the start-up: only the automatic first start (--skip-intro, --load-autosave) skips the screen; New game chosen in the menu later asks

    // test aids (--test-keys, --quit-after-ms): real SDL key events pushed into the queue, so the window's own key handling is what runs
    struct TestKey {
        SDL_Keycode key = SDLK_UNKNOWN;
        SDL_Keymod mod = SDL_KMOD_NONE;
        int kind = 0;                 // 0 key, 1 click at a grid point, 2 drag between two grid points
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    };
    std::vector<TestKey> testKeys;
    {
        std::istringstream tk(o.testKeys);
        std::string name;
        static const std::map<std::string, SDL_Keycode> names = {
            {"minus", SDLK_MINUS}, {"equals", SDLK_EQUALS}, {"backslash", SDLK_BACKSLASH}, {"f5", SDLK_F5}, {"f6", SDLK_F6}, {"f9", SDLK_F9}, {"f7", SDLK_F7},
            {"pagedown", SDLK_PAGEDOWN}, {"pageup", SDLK_PAGEUP}, {"escape", SDLK_ESCAPE}, {"return", SDLK_RETURN}, {"m", SDLK_M}, {"n", SDLK_N},
            {"f10", SDLK_F10}, {"up", SDLK_UP}, {"down", SDLK_DOWN}, {"left", SDLK_LEFT}, {"right", SDLK_RIGHT}, {"tab", SDLK_TAB}, {"home", SDLK_HOME},
            {"end", SDLK_END}, {"comma", SDLK_COMMA}, {"space", SDLK_SPACE}};
        while (std::getline(tk, name, ',')) {
            TestKey t;
            if (name.rfind("click@", 0) == 0 || name.rfind("drag@", 0) == 0) {      // click@X:Y, drag@X0:Y0:X1:Y1 (the 640 x 480 grid; mouse events of the window)
                const bool drag = name[0] == 'd';
                t.kind = drag ? 2 : 1;
                if (std::sscanf(name.c_str() + (drag ? 5 : 6), "%d:%d:%d:%d", &t.x0, &t.y0, &t.x1, &t.y1) < (drag ? 4 : 2)) throw Error("bad --test-keys name: " + name);
                if (!drag) { t.x1 = t.x0; t.y1 = t.y0; }
            } else {
                for (bool more = true; more;) {      // alt-, shift-, cmd- in front of a key name: the key with that modifier held
                    more = false;
                    if (name.rfind("alt-", 0) == 0) { t.mod = static_cast<SDL_Keymod>(t.mod | SDL_KMOD_LALT); name = name.substr(4); more = true; }
                    else if (name.rfind("shift-", 0) == 0) { t.mod = static_cast<SDL_Keymod>(t.mod | SDL_KMOD_LSHIFT); name = name.substr(6); more = true; }
                    else if (name.rfind("cmd-", 0) == 0) { t.mod = static_cast<SDL_Keymod>(t.mod | SDL_KMOD_LGUI); name = name.substr(4); more = true; }
                }
                auto it = names.find(name);
                if (it == names.end()) throw Error("bad --test-keys name: " + name);
                t.key = it->second;
            }
            testKeys.push_back(t);
        }
    }
    size_t nextTestKey = 0;
    const Uint64 startTicks = SDL_GetTicks();
    Uint64 nextTestKeyAt = startTicks + 1500;
    if (!o.testShot.empty() && o.quitAfterMs) shotDue = startTicks + (o.quitAfterMs > 300 ? o.quitAfterMs - 300 : 0);

    while (running) {
        SDL_Event ev;
        bool motion = false;
        if (nextTestKey < testKeys.size() && SDL_GetTicks() >= nextTestKeyAt) {
            const TestKey& t = testKeys[nextTestKey];
            if (t.kind == 0) {
                SDL_Event ke{};
                ke.type = SDL_EVENT_KEY_DOWN;
                ke.key.key = t.key;
                ke.key.mod = t.mod;
                ke.key.down = true;
                SDL_PushEvent(&ke);
                ke.type = SDL_EVENT_KEY_UP;
                ke.key.down = false;
                SDL_PushEvent(&ke);
            } else {
                auto at = [&](int gx, int gy, float& wx, float& wy) { toWindow(static_cast<float>(gx), static_cast<float>(gy), wx, wy); };
                auto motion = [&](int gx, int gy) {
                    SDL_Event me{};
                    me.type = SDL_EVENT_MOUSE_MOTION;
                    at(gx, gy, me.motion.x, me.motion.y);
                    SDL_PushEvent(&me);
                };
                auto button = [&](int gx, int gy, bool down) {
                    SDL_Event be{};
                    be.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
                    be.button.button = SDL_BUTTON_LEFT;
                    be.button.down = down;
                    at(gx, gy, be.button.x, be.button.y);
                    SDL_PushEvent(&be);
                };
                motion(t.x0, t.y0);
                button(t.x0, t.y0, true);
                if (t.kind == 2)
                    for (int i = 1; i <= 8; i++) motion(t.x0 + (t.x1 - t.x0) * i / 8, t.y0 + (t.y1 - t.y0) * i / 8);
                button(t.x1, t.y1, false);
            }
            nextTestKey++;
            nextTestKeyAt = SDL_GetTicks() + 300;
        }
        if (o.quitAfterMs && SDL_GetTicks() >= startTicks + o.quitAfterMs && nextTestKey >= testKeys.size()) running = false;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                sawFocusLoss = true;
                applyPause(pause.focus(false));
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                // Coming back with the pointer beside the picture (a click on the title bar, the pointer on another screen): the mouse stays
                // free, so the window can be dragged; a click in the picture goes on. Coming back over the picture goes on at once.
                // (Not in a scripted test run: its window is in nobody's hands.)
                if (sawFocusLoss && pause.pausesOnFocusLoss() && !o.quitAfterMs && o.testKeys.empty() && SDL_GetTicks() >= noAutoReleaseUntil && !pointerOverPicture()) releaseMouse(true);
                applyPause(pause.focus(true));
                syncHold();     // the lock may have been switched while another program had the keyboard
                break;
            case SDL_EVENT_WINDOW_MINIMIZED: applyPause(pause.minimised(true)); break;
            case SDL_EVENT_WINDOW_RESTORED: applyPause(pause.minimised(false)); break;
            case SDL_EVENT_MOUSE_WHEEL:     // the wheel turns the pages of save slots in the save and load menus
                if (s.menu.isOpen()) {      // (an open settings menu: the wheel changes the row under the pointer)
                    int gx, gy;
                    menuGrid(ev.wheel.mouse_x, ev.wheel.mouse_y, gx, gy);
                    const float wy = ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -ev.wheel.y : ev.wheel.y;
                    s.menu.wheel(gx, gy, wy > 0 ? 1 : wy < 0 ? -1 : 0);
                    menuPollNow();
                    break;
                }
                if (e.gameMenuOpen())
                    if (const int pk = pageKeyOfWheel(ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -ev.wheel.y : ev.wheel.y)) guarded([&] { e.keyDown(Key::Other, pk); e.keyUp(Key::Other, pk); });
                break;
            case SDL_EVENT_MOUSE_MOTION: {
                if (s.menu.isOpen()) {              // the menu has the mouse: nothing reaches the engine
                    int gx, gy;
                    menuGrid(ev.motion.x, ev.motion.y, gx, gy);
                    s.menu.mouseMove(gx, gy);
                    menuPollNow();
                    break;
                }
                if (pause.isReleased()) break;      // the mouse is the desktop's
                if (overlayShown && e.pointerCaptured()) {
                    // the overlay interface, captured pointer: motion relative to where the pointer last was, in engine pixels (window pixels / k,
                    // in bar mode / u), the remainder carried; in the vertical release the pointer row moves by these deltas too (no inverse map)
                    float rx = 0, ry = 0;
                    SDL_RenderCoordinatesFromWindow(ren, ev.motion.x, ev.motion.y, &rx, &ry);
                    if (!haveLast) {        // not warped yet: relative to where the engine's pointer is shown
                        int qx, qy;
                        e.pointerPos(qx, qy);
                        ovl::engineToWindow(ovLayout, qx, qy, lastRX, lastRY);
                        haveLast = true;
                    }
                    const double mx = rx - lastRX, my = ry - lastRY;
                    lastRX = rx;
                    lastRY = ry;
                    if (e.barMode()) scaler.reset();
                    guarded([&] {
                        if (overlayCapturedMove(e, ovLayout, carry, mx, my, [&](int& ddx, int& ddy) { scaler.apply(ddx, ddy, ddx, ddy); })) motion = true;   // --mouse-sensitivity: the panorama scroll only
                    });
                    break;
                }
                carry.reset();
                haveLast = false;
                float fx, fy;
                toRender(ev, fx, fy);
                int x = static_cast<int>(fx), y = static_cast<int>(fy), px, py;
                e.pointerPos(px, py);
                int dx = x - px, dy = y - py;
                if (e.pointerCaptured() && !dx && !dy) break;   // the echo of our own pointer warp
                if (e.pointerCaptured() && !e.barMode()) scaler.apply(dx, dy, dx, dy);   // --mouse-sensitivity: the panorama scroll only
                else scaler.reset();
                guarded([&] { e.mouseMove(x, y, dx, dy); });
                motion = true;
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                bool down = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                if (s.menu.isOpen()) {
                    int gx, gy;
                    menuGrid(ev.button.x, ev.button.y, gx, gy);
                    if (ev.button.button == SDL_BUTTON_LEFT) {
                        if (down) s.menu.mouseDown(gx, gy);
                        else s.menu.mouseUp(gx, gy);
                    }
                    swallowButtonUp = down;      // a click that closes the menu must not reach the game as a release
                    menuPollNow();
                    break;
                }
                if (pause.isReleased()) {           // a click in the picture takes the mouse back; it is not a click in the game
                    if (down) {
                        releaseMouse(false);
                        swallowButtonUp = true;
                    }
                    break;
                }
                if (!down && swallowButtonUp) { swallowButtonUp = false; break; }
                Button b = ev.button.button == SDL_BUTTON_LEFT ? Button::Left : Button::Right;
                if (!e.pointerCaptured()) {
                    float fx, fy;
                    toRender(ev, fx, fy);
                    int px, py;
                    e.pointerPos(px, py);
                    e.mouseMove(static_cast<int>(fx), static_cast<int>(fy), static_cast<int>(fx) - px, static_cast<int>(fy) - py);
                }
                guarded([&] { if (down) e.mouseDown(b); else e.mouseUp(b); });
                break;
            }
            case SDL_EVENT_KEY_UP: {
                if (swallowedKeys.erase(ev.key.key)) break;      // the release of a key the menu took
                if (gMode) break;
                SDL_Keycode k = ev.key.key;
                if (StatusHold::isHoldKey(k)) hold.graveKey(false);
                syncHold();
                guarded([&] {
                    if (k == SDLK_S) e.keyUp(Key::S);
                    else if (k == SDLK_D) e.keyUp(Key::D);
                    else if (k == SDLK_SPACE) e.keyUp(Key::Space);
                    else if (k == SDLK_LCTRL || k == SDLK_RCTRL) e.keyUp(Key::Ctrl);
                    else if (k == SDLK_LSHIFT || k == SDLK_RSHIFT) e.keyUp(Key::Shift);
                    else if (int vk = vkOfSdl(k)) e.keyUp(Key::Other, vk);
                });
                break;
            }
            case SDL_EVENT_KEY_DOWN: {
                SDL_Keycode k = ev.key.key;
                bool shift = ev.key.mod & SDL_KMOD_SHIFT;
                if (s.menu.isOpen()) {      // the settings menu has every key; none reaches the engine or the game
                    swallowedKeys.insert(k);
                    smenu::MKey mk;
                    const bool cmdComma = k == SDLK_COMMA && (ev.key.mod & SDL_KMOD_GUI);
                    if (cmdComma) mk = smenu::MKey::F10;
                    if (cmdComma || mkeyOf(k, mk)) {
                        const bool nav = mk == smenu::MKey::Up || mk == smenu::MKey::Down || mk == smenu::MKey::Left || mk == smenu::MKey::Right ||
                                         mk == smenu::MKey::PageUp || mk == smenu::MKey::PageDown;
                        if (!ev.key.repeat || nav) s.menu.key(mk, shift);
                    }
                    menuPollNow();
                    break;
                }
                if ((k == SDLK_F10 || (k == SDLK_COMMA && (ev.key.mod & SDL_KMOD_GUI))) && !gMode) {   // F10 / Cmd+,: the settings menu
                    swallowedKeys.insert(k);         // the key never reaches the game (it would be typed into its cheat buffer)
                    if (!ev.key.repeat && menuMayOpen(s)) {
                        menuRefresh(s);
                        s.menu.openSettings();
                        menuSync();
                    }
                    break;
                }
                if (gMode) {
                    if (k == SDLK_ESCAPE) gMode = false;
                    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                        gMode = false;
                        guarded([&] { goPage(resolvePage(e.view(), gEntry)); });
                    } else if (k == SDLK_BACKSPACE) {
                        if (!gEntry.empty()) gEntry.pop_back();
                    } else if (k >= 32 && k < 127 && !(ev.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) {
                        char c = static_cast<char>(k);
                        if (shift) {
                            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
                            else if (c == '-') c = '_';
                        }
                        gEntry.push_back(c);
                    }
                    break;
                }
                {   // master volume keys (windowlogic.h); not while a menu or a text entry is up
                    const VolumeKey vkey = volumeKeyOf(k, e.menuOpen() || e.textInputActive());
                    if (vkey != VolumeKey::None) {
                        Mixer& mx = e.mixer();
                        if (vkey == VolumeKey::Mute) { if (!ev.key.repeat) mx.setMuted(!mx.muted()); }
                        else { mx.setMuted(false); mx.setMasterVolume(stepVolume(mx.masterVolume(), vkey)); }
                        volumeShownUntil = SDL_GetTicks() + 1500;
                        break;
                    }
                }
                if (ev.key.repeat) break;
                if (StatusHold::isHoldKey(k)) hold.graveKey(true);
                syncHold();
                if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && (ev.key.mod & SDL_KMOD_ALT) && !e.textInputActive()) {
                    noAutoReleaseUntil = SDL_GetTicks() + 3000;
                    SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));   // desktop full screen, on the screen the window is on
                    SDL_SyncWindow(window);
                    noAutoReleaseUntil = SDL_GetTicks() + 3000;
                    break;
                }
                if ((ev.key.mod & SDL_KMOD_ALT) && !e.textInputActive()) {
                    if (k == SDLK_M || ev.key.scancode == SDL_SCANCODE_M) {       // Option+M: let go of the mouse / take it back
                        releaseMouse(!pause.isReleased());
                        break;
                    }
                    if (k == SDLK_N || ev.key.scancode == SDL_SCANCODE_N) {       // Option+N: the window goes to the next screen
                        const Screens sc = listScreens();
                        moveToScreen(nextDisplay(screenOfWindow(sc, window), static_cast<int>(sc.ids.size())));
                        break;
                    }
                }
                if (pause.isReleased()) break;      // no game keys while the mouse is the desktop's
                if (e.gameMenuOpen()) {     // Page Up / Page Down: the pages of save slots (only the menu reads them, the game never sees them)
                    if (const int pk = pageKeyOfSdl(k)) { guarded([&] { e.keyDown(Key::Other, pk); }); break; }
                }
                if (k == SDLK_F9 && s.game && !e.menuOpen() && !e.textInputActive()) {   // F9 twice within 3 s: the newest autosave
                    const Uint32 t = SDL_GetTicks();
                    if (!f9.armed(t) && (f9Newest = autosave::newest(e.saveDirectory())) == 0) {
                        last = "no autosave to load";
                    } else if (f9.press(t) == DoublePress::Result::Confirmed) {
                        guarded([&] { std::string msg; loadAutosaveInto(s, -1, msg); last = msg; });
                    }
                    break;
                }
                guarded([&] {      // OnKeyDown for every key (the shift / ctrl handling is in Engine::keyDown below)
                    if (k == SDLK_S) e.keyDown(Key::S);
                    else if (k == SDLK_D) e.keyDown(Key::D);
                    else if (k == SDLK_SPACE) e.keyDown(Key::Space);
                    else if (k != SDLK_LSHIFT && k != SDLK_RSHIFT && k != SDLK_LCTRL && k != SDLK_RCTRL)
                        if (int vk = vkOfSdl(k)) e.keyDown(Key::Other, vk);
                });
                if (e.menuOpen() || e.textInputActive()) break;   // the menu / the text entry took the key
                // the developer keys (page-viewer leftovers) work only with --dev; the keys above went to the game either way
                if (!o.dev && (k == SDLK_F2 || k == SDLK_H || k == SDLK_O || k == SDLK_G || k == SDLK_M || k == SDLK_N || k == SDLK_BACKSPACE ||
                               k == SDLK_F7 || k == SDLK_LEFTBRACKET || k == SDLK_RIGHTBRACKET || k == SDLK_P || k == SDLK_L || (k >= SDLK_1 && k <= SDLK_6)))
                    break;
                switch (k) {
                case SDLK_LSHIFT: case SDLK_RSHIFT: e.keyDown(Key::Shift); break;
                case SDLK_LCTRL: case SDLK_RCTRL: e.keyDown(Key::Ctrl); break;
                case SDLK_ESCAPE:
                    if (e.view().popupOpen()) { e.view().closePopup(); last = "popup closed"; }
                    else if (!o.game && !o.menuLogic) running = false;   // with a game logic Esc belongs to the game (the menu)
                    break;
                case SDLK_F2:
                    guarded([&] { e.startNewGame(); last = "new game"; });
                    break;
                case SDLK_H: e.setOutlines(!e.outlines()); break;
                case SDLK_O: e.setShowHidden(!e.view().showHidden()); last = e.view().showHidden() ? "hidden shown" : "hidden skipped"; break;
                case SDLK_G: gMode = true; gEntry.clear(); break;
                case SDLK_M: {
                    if (introMovies.empty()) {
                        std::string dir = e.files().findTime("movies\\intro");
                        if (DIR* d = dir.empty() ? nullptr : opendir(dir.c_str())) {
                            while (const dirent* de = readdir(d)) {
                                std::string n = de->d_name;
                                if (n.size() > 4 && iequals(n.substr(n.size() - 4), ".avi")) introMovies.push_back(n);
                            }
                            closedir(d);
                            std::sort(introMovies.begin(), introMovies.end());
                        }
                    }
                    if (introMovies.empty()) { last = "no movies in movies\\intro"; break; }
                    std::string n = introMovies[nextMovie++ % introMovies.size()];
                    std::string base = n.substr(0, n.size() - 4);
                    std::string pal = "palettes\\movies\\intro\\" + base + ".pal";
                    playMovie("movies\\intro\\" + n, e.files().findData(pal).empty() ? "" : pal);
                    break;
                }
                case SDLK_N: {
                    float fx, fy;
                    SDL_GetMouseState(&fx, &fy);
                    fromWindow(fx, fy, fx, fy);
                    int sx = static_cast<int>(fx), sy = static_cast<int>(fy);
                    if (e.pointerCaptured()) e.pointerPos(sx, sy);
                    int h = e.debugPlaySprite("anims\\sensor\\alarm.tan", sx, sy, !shift);
                    last = h < 0 ? "sprite failed (does it fit?)" : std::string("sprite alarm.tan ") + (shift ? "fixed" : "page-relative");
                    break;
                }
                case SDLK_F3:
                    if (!o.dev) break;
                    smoothPano = !smoothPano;
                    last = smoothPano ? "smooth panoramas" : "original panoramas";
                    break;
                case SDLK_F7:
                    if (!o.dev) break;
                    wideOn = !wideOn;
                    if (wideOn) smoothPano = true;
                    last = wideOn ? "widescreen panoramas" : "4:3 panoramas";
                    break;
                case SDLK_F4:
                    if (!o.dev) break;
                    panorama.setLinear(!panorama.linear());
                    last = panorama.linear() ? "panorama filter: linear" : "panorama filter: nearest";
                    break;
                case SDLK_BACKSPACE:
                    last = e.goBack() ? "back to " + e.view().page().name : "history empty";
                    break;
                case SDLK_LEFTBRACKET: goPage(e.view().pageNumber() > 1 ? e.view().pageNumber() - 1 : e.view().pageCount()); break;
                case SDLK_RIGHTBRACKET: goPage(e.view().pageNumber() < e.view().pageCount() ? e.view().pageNumber() + 1 : 1); break;
                case SDLK_P:
                case SDLK_L: {
                    int n = e.view().paletteSetCount() + 1;   // -1..count-1
                    overrideSet = ((overrideSet + 1 + (k == SDLK_L ? n - 1 : 1)) % n) - 1;
                    e.setPaletteSetOverride(overrideSet);
                    last = overrideSet < 0 ? "palette set: page default" : "palette set " + std::to_string(overrideSet);
                    break;
                }
                default:
                    if (k >= SDLK_1 && k <= SDLK_6) {
                        e.setTimeSlot(static_cast<int>(k - SDLK_1));
                        last = std::string("slot ") + timeSlotName(e.view().timeSlot());
                    }
                }
                break;
            }
            default: break;
            }
        }

        if (s.logic.pendingNewGame) menuPollNow();     // New game was chosen in the engine's menu: the New game screen opens
        syncHold();     // once per frame too: a menu or text entry that has just closed lets a Caps Lock that is still on take hold
        // The engine's idle pass runs at the pass interval on the real clock, not once per display frame (app/pacing.h); everything else
        // of the loop (input, the pointer, the picture) runs at the display's rate.
        auto pass = [&](u32 t) {
            guarded([&] {
                if (!gMode && !pause.isMenu()) {
                    const bool* ks = SDL_GetKeyboardState(nullptr);
                    int dx = (ks[SDL_SCANCODE_RIGHT] ? 8 : 0) - (ks[SDL_SCANCODE_LEFT] ? 8 : 0);
                    int dy = (ks[SDL_SCANCODE_DOWN] ? 8 : 0) - (ks[SDL_SCANCODE_UP] ? 8 : 0);
                    if (dx || dy) e.scrollBy(dx, dy);
                }
                e.update(t);
            });
        };
        auto beforeDraw = [&]() {
            if (pause.isReleased() || pause.isMenu()) return;     // the mouse is the desktop's: no warp, no confinement, the system's arrow
            // pointer capture (spec 5.2): keep the OS pointer at the engine's pointer position
            bool captured = e.pointerCaptured();
            if (captured && (motion || !wasCaptured)) {
                int px, py;
                e.pointerPos(px, py);
                float wx, wy;
                toWindow(static_cast<float>(px), static_cast<float>(py), wx, wy);
                SDL_WarpMouseInWindow(window, wx, wy);
                if (overlayShown) {
                    float rx = 0, ry = 0;
                    SDL_RenderCoordinatesFromWindow(ren, wx, wy, &rx, &ry);
                    lastRX = rx;
                    lastRY = ry;
                    haveLast = true;
                    carry.reset();
                }
            }
            wasCaptured = captured;

            std::string stem = e.cursorFile();
            if (stem != shownCursor) {
                SDL_SetCursor(cursors.get(e, stem, 2));
                shownCursor = stem;
            }
            bool wantVisible = e.cursorVisible();
            if (wantVisible == cursorHidden) {
                if (wantVisible) SDL_ShowCursor(); else SDL_HideCursor();
                cursorHidden = !wantVisible;
            }

            // API 0x68 / exe:00411860: the pointer is confined to the engine's clip rectangle (none while it is captured)
            {
                Rect cr;
                SDL_Rect wr{0, 0, 0, 0};
                bool clip = !captured && e.cursorClipRect(cr);
                if (clip && overlayShown) {         // the free pointer of the overlay interface may go anywhere in the window (the dock is its bottom edge)
                    int ww = 0, wh = 0;
                    SDL_GetWindowSize(window, &ww, &wh);
                    wr = SDL_Rect{0, 0, ww, wh};
                } else if (clip) {
                    float x0, y0, x1, y1;
                    toWindow(static_cast<float>(cr.x), static_cast<float>(cr.y), x0, y0);
                    toWindow(static_cast<float>(cr.x + cr.w), static_cast<float>(cr.y + cr.h), x1, y1);
                    wr = SDL_Rect{static_cast<int>(x0), static_cast<int>(y0), static_cast<int>(x1 - x0), static_cast<int>(y1 - y0)};
                }
                if (clip != clipShown || (clip && (wr.x != shownClip.x || wr.y != shownClip.y || wr.w != shownClip.w || wr.h != shownClip.h))) {
                    SDL_SetWindowMouseRect(window, clip ? &wr : nullptr);
                    clipShown = clip;
                    shownClip = wr;
                }
            }
        };
        if (pause.isMinimised()) SDL_Delay(40);       // nothing to show, nothing to run (the original's idle pass does nothing either)
        else pacer.step(pass, beforeDraw);
        {   // the play time of the autosave: real time, but not while paused, and a gap of more than 2 s (the computer slept) is not play
            const Uint64 tn = SDL_GetTicks();
            const uint32_t dt = static_cast<uint32_t>(std::min<Uint64>(tn - lastLoopTicks, 2000));
            lastLoopTicks = tn;
            if (!pause.paused()) guarded([&] { autosaveTick(s, dt); });
        }

        if (e.quitRequested()) running = false;       // API 0x02
        if (!e.lastMessage().empty()) last = e.lastMessage();
        std::string title = titleFor(e, last, gEntry, gMode);
        if (SDL_GetTicks() < volumeShownUntil)
            title += e.mixer().muted() ? "  |  volume: muted" : "  |  volume: " + std::to_string(e.mixer().masterVolume()) + "%";
        if (const int sp = e.menuSlotPage(); sp > 0 && e.slotPages() > 1)
            title += "  |  slots page " + std::to_string(sp) + "/" + std::to_string(e.slotPages()) + " (Page Up / Page Down, wheel)";
        if (f9.armed(SDL_GetTicks())) title += "  |  press F9 again within 3 s to load autosave " + std::to_string(f9Newest);
        if (pause.paused())
            title += pause.isMinimised() ? "  |  paused (minimised)" : pause.isReleased() ? "  |  paused, mouse released: click the picture (or Option+M) to go on" : "  |  paused (no focus)";
        if (title != shownTitle) { SDL_SetWindowTitle(window, title.c_str()); shownTitle = title; }
    }
    // quit with a game running: one more autosave, before anything is closed (the pump is off so that the menu's fade-out does not wait)
    e.setPump(nullptr);
    if (s.game && s.autosave.enabled() && s.game->gameRunning()) {
        const std::string why = saveBlockerOf(s, true);
        if (why.empty()) guarded([&] { runAutosave(s, "quit"); });
        else std::fprintf(stderr, "biosys: no autosave at exit: %s\n", why.c_str());
    }
    menuFlush(s);
    saveRuntimePrefs(s.cur, window, e);
    s.applyWindow = nullptr;
    s.snapshotWindow = nullptr;
    e.shutdown();
    audio.reset();
    sharpP.reset();
    ovR.reset();
    if (texOvView) SDL_DestroyTexture(texOvView);
    panoramaP.reset();
    if (texMenu) SDL_DestroyTexture(texMenu);
    if (texClean) SDL_DestroyTexture(texClean);
    cleanP.reset();
    if (texWide) SDL_DestroyTexture(texWide);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(window);
    SDL_Quit();
    (void)o;
    return 0;
}

} // namespace

bool readTextFile(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool writeTextFile(const std::string& path, const std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    return ok;
}

// The command line is read twice: the first time (nothing from a file) tells where the preferences file is and whether the run uses it;
// the second time starts from the file's values and lets the command line override them. A headless run (--script, --screenshot,
// --selftest-all-pages) ignores the file unless --config FILE names one.
Options loadOptions(int argc, char** argv) {
    const Options probe = parseArgs(argc, argv);
    if (probe.help) return probe;
    const bool headless = probe.haveScript || !probe.screenshot.empty() || probe.selftest;
    if (probe.noConfig && probe.writeConfig) throw Error("--write-config and --no-config contradict each other");
    const bool explicitFile = !probe.configFile.empty();
    const bool use = !probe.noConfig && (!headless || explicitFile);
    if (!use && !probe.writeConfig) return probe;
    std::string saveDir = probe.saveDir.empty() ? defaultSaveDir() : probe.saveDir;
    while (saveDir.size() > 1 && saveDir.back() == '/') saveDir.pop_back();
    if (!headless) {   // a window run makes a missing save folder, so that the preferences and the saves have a place
        std::error_code ec;
        std::filesystem::create_directories(saveDir, ec);
    }
    const std::string path = explicitFile ? probe.configFile : saveDir + "/biosys.conf";
    std::string text;
    const bool have = readTextFile(path, text);
    const OptTable<Options>& table = optionTable();
    if (!have && explicitFile && !probe.writeConfig) throw Error("cannot read the preferences file " + path);
    if (!have && !explicitFile && use && !probe.writeConfig) {      // the first start: a commented default file, so that there is something to edit
        text = table.defaultFile();
        if (!writeTextFile(path, text)) std::fprintf(stderr, "biosys: cannot write the default preferences file %s\n", path.c_str());
    }
    const PrefFile pf = parsePrefs(text);
    std::vector<std::string> problems;
    Options o = parseArgs(argc, argv, use ? &pf : nullptr, &problems);
    for (const std::string& pr : problems) std::fprintf(stderr, "biosys: %s: %s\n", path.c_str(), pr.c_str());
    o.configPath = use ? path : "";
    if (o.writeConfig) {        // the effective settings (file + command line) back into the file, comments and unknown lines kept
        const std::string out = updatePrefsText(have ? text : table.defaultFile(), table.currentValues(o));
        if (!writeTextFile(path, out)) throw Error("cannot write the preferences file " + path);
        std::printf("biosys: preferences written to %s\n", path.c_str());
    }
    return o;
}

namespace {

// Started from Finder (no arguments, or only macOS's -psn_ one) as a bundled app: play.
bool insideAppBundle(const char* argv0) { return argv0 && std::strstr(argv0, ".app/Contents/MacOS/") != nullptr; }

} // namespace

int main(int argc, char** argv) {
    bool windowMode = true;         // an error is shown in a message box too (a bundled app has no terminal)
    try {
        static char playArg[] = "--play";
        std::vector<char*> args;
        for (int i = 0; i < argc; i++)
            if (i == 0 || std::strncmp(argv[i], "-psn_", 5) != 0) args.push_back(argv[i]);
        if (args.size() == 1 && insideAppBundle(argc ? argv[0] : nullptr)) args.push_back(playArg);
        for (size_t i = 1; i < args.size(); i++) {
            const std::string a = args[i];
            if (a == "--version") {
                std::printf("Biosys Remaster %s\n", biosysVersion());
                return 0;
            }
            if (a == "--script" || a.rfind("--script=", 0) == 0 || a == "--screenshot" || a.rfind("--screenshot=", 0) == 0 || a == "--selftest-all-pages" || a == "--help" ||
                a == "-h" || a == "--write-config")
                windowMode = false;
        }
        argc = static_cast<int>(args.size());
        argv = args.data();
        Options o = loadOptions(argc, argv);
        if (o.help) {
            std::fputs(kHelp, stdout);
            return 0;
        }
        if (o.writeConfig) return 0;
        const bool headless = o.haveScript || !o.screenshot.empty() || o.selftest;
        windowMode = !headless;
        if (o.loadAutosave != 0 && !o.game) throw Error("--load-autosave needs --game");
        gDataRoots = resolveDataRoots(o);
        if (gDataRoots.empty()) {
            const std::string home = dataHomeOf(o);
            if (headless)
                throw Error("The game's data is not here yet. Start Biosys Remaster once in a window to hand over your two original CDs (it imports them into " + home +
                            "), or point --data-home at an earlier import, or set BIOSYS_DATA to a folder that holds Biodata and Biotime.");
            FirstRunOptions fo;
            fo.dataHome = home;
            fo.quitAfterMs = o.quitAfterMs;
            fo.testDrops = o.testDrops;
            fo.shotFile = o.firstrunShot;
            fo.log = o.firstrunLog;
            if (runFirstRun(fo) == FirstRunResult::Quit) return 0;
            gDataRoots = resolveDataRoots(o);
            if (gDataRoots.empty()) throw Error("The import finished, but the game's data is not where it should be (" + home + ").");
        }
        if (o.firstrunOnly) return 0;
        {   // the engine looks for NewGame.sav through the same variable
            std::string joined;
            for (const std::string& r : gDataRoots) joined += (joined.empty() ? "" : ":") + r;
            setenv("BIOSYS_DATA", joined.c_str(), 1);
        }
        Setup s;
        makeEngine(s, o, argc ? argv[0] : nullptr);
        s.cur = o;
        if (o.selftest) return selftestAllPages(s, o);
        if (o.haveScript) {
            ScriptHooks hooks;
            hooks.setCrossFade = [&s](const CrossFadeSpec& sp) { s.logic.xfade = sp; s.logic.haveXfade = true; };
            hooks.setFilterTest = [&s](bool on) { s.logic.filterTest = on; };
            hooks.smoothCaptureSdl = [](const Engine::PanoramaFrame& pf, const Frame& f, int scale, std::vector<uint32_t>& rgb, std::string& err) {
                std::vector<Uint32> out;
                const bool ok = captureSdlSoftware(pf, f, scale, out, err);
                rgb.assign(out.begin(), out.end());
                return ok;
            };
            if (s.game) hooks.realtimeWaits = [&s](bool on) {
                Engine* ep = s.engine.get();
                s.game->setBusyWait(on ? std::function<void(u32)>([ep](u32 ms) { ep->waitBlocking(ms); }) : std::function<void(u32)>());
            };
            if (s.game) hooks.game = [&s](const std::string& a) { return s.game->debugCommand(a); };
            hooks.menu = [&s](const std::string& a) { return menuCommand(s, a); };
            if (s.game) {
                hooks.tick = [&s](uint32_t dt) { autosaveTick(s, dt); };
                hooks.autosave = [&s](const std::string& a) { return autosaveCommand(s, a); };
            }
            runScript(*s.engine, o.script, &hooks);
            if (o.screenshot.empty()) {
                s.engine->shutdown();
                return 0;
            }
        }
        if (!o.screenshot.empty()) {
            Frame f;
            s.engine->render(f);
            writeBmp(o.screenshot, f.toImage(), s.engine->view().palette());
            return 0;
        }
        return runWindow(s, o);
    } catch (const Error& e) {
        std::fprintf(stderr, "biosys: %s\n", e.what());
        // The message box is for a player who started the app by double click and has no terminal: only inside an app bundle. A build
        // started from a terminal (build/biosys) has said it on stderr; a modal box there would block whatever started it (a test that
        // expects the error exit waited for hours on it).
        const char* vd = std::getenv("SDL_VIDEODRIVER");
        const char* base = SDL_GetBasePath();
        const bool bundled = base && std::strstr(base, ".app/Contents/") != nullptr;
        if (windowMode && bundled && !(vd && std::strcmp(vd, "dummy") == 0))
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Biosys Remaster", e.what(), nullptr);
        return 1;
    }
}
