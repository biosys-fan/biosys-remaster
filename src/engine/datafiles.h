// CONTRACT — locating original game files.
#pragma once
#include "engine/common.h"

namespace biosys {

// The original game addresses files relative to two directories, BIODATA (pages, objects,
// palettes, lookups, fonts, cursors) and BIOTIME (anims, sounds, movies), using backslashes and
// arbitrary case. Part of the data lives in the installed copy and part only on the CD, so every
// lookup searches a list of roots in order.
class DataFiles {
public:
    // roots: directories that contain "Biodata"/"Biotime" (any case), highest priority first.
    explicit DataFiles(std::vector<std::string> roots);

    // Default roots for tools and tests: environment variable BIOSYS_DATA (colon-separated) if set, otherwise the
    // development roots (see developmentRoots), otherwise none.
    static DataFiles fromEnvironment();
    // Same, with the roots the caller supplies (the app: the imported copy) instead of the development roots.
    static DataFiles fromEnvironment(const std::vector<std::string>& fallbackRoots);
    // BIOSYS_DATA split at ':' (empty entries dropped); empty if the variable is unset or empty.
    static std::vector<std::string> environmentRoots();
    // <BIOSYS_DEV_ROOT>/setup-cd/installed/DATA1 and <BIOSYS_DEV_ROOT>/game-cd/files: only in a development build
    // (the Makefile defines BIOSYS_DEV_ROOT); a release build has none.
    static std::vector<std::string> developmentRoots();

    // Resolve "palettes\rforest\rf_day.pal" (BIODATA) / "sounds\house\pan3.wav" (BIOTIME) to a real
    // path, matching every path component case-insensitively. Returns "" if not found in any root.
    std::string findData(const std::string& rel) const;
    std::string findTime(const std::string& rel) const;

    // The data directory as the original's API 0x1b hands it to the game: the BIODATA directory of the first root that has
    // one, without a trailing separator. The game appends "\\fonts\\x" or a lookup name to it; findData/findTime resolve a
    // path that starts with this directory (or the BIOTIME one) back to the relative name. "" if no root has Biodata.
    std::string dataDirectory() const;

    // Same, but throw Error naming the file if it is missing.
    std::string requireData(const std::string& rel) const;
    std::string requireTime(const std::string& rel) const;

private:
    std::string find(const char* top, const std::string& rel) const;
    std::vector<std::string> roots_;
};

} // namespace biosys
