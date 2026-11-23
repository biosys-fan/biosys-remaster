// The first-run screen: its own SDL window, shown before the engine exists when the game's data is not there yet. It takes the two
// original CDs (dropped on the window, or chosen with a file dialog), imports them on a worker thread (install/importer.h) and returns
// when both are in the data home. The logic is SDL-free (firstrunlogic.h).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace biosys {

struct FirstRunOptions {
    std::string dataHome;                  // where install::importDisc writes
    uint32_t quitAfterMs = 0;              // --quit-after-ms (test aid): close the window as if the player quit
    std::vector<std::string> testDrops;    // --test-drop PATH: real SDL_EVENT_DROP_FILE events from 1 s on, 300 ms apart
    std::string shotFile;                  // --firstrun-shot FILE.bmp: the window's picture at 800 ms and when it closes
    bool log = false;                      // --firstrun-log: one line per state change on stdout
};

enum class FirstRunResult { Ready, Quit };

// Ready: both discs are imported (the window closed itself); Quit: the player closed the window / pressed Esc / Cmd+Q
// (an import in progress was cancelled and cleaned up). Throws Error if the window cannot be made.
FirstRunResult runFirstRun(const FirstRunOptions& o);

// BIOSYS_VERSION of the build, "dev" without one.
const char* biosysVersion();

} // namespace biosys
