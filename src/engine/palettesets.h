// CONTRACT — which palette a page is shown with.
// The original control program (BiosGame.dll) holds 27 "palette sets"; each has two groups (A, B)
// of six time-of-day slots, with a palette file and a cross-fade lookup file per slot.
// The port ships them as generated tables in data/ (see data/README.md for the TSV columns).
#pragma once
#include "engine/common.h"

namespace biosys {

enum TimeSlot : int { SLOT_NIGHT = 0, SLOT_DAWN, SLOT_DAY, SLOT_DUSK, SLOT_CLOUD, SLOT_GOGGLES, SLOT_COUNT };
const char* timeSlotName(int slot);   // "night", "dawn", "day", "dusk", "cloud", "gogs"

struct PaletteSet {
    // File names relative to BIODATA, as the original stores them (backslashes), e.g.
    // "palettes\\rforest\\rf_day.pal", "lookups\\rforest\\r_day.xft".
    std::string pal[2][SLOT_COUNT];   // [group 0 = A, 1 = B][slot]
    std::string xft[2][SLOT_COUNT];
};

class PaletteTable {
public:
    // Reads <dataDir>/palette_sets.tsv and <dataDir>/page_palettes.tsv. Throws Error on failure.
    static PaletteTable load(const std::string& dataDir);

    int setCount() const { return static_cast<int>(sets_.size()); }
    const PaletteSet& set(int index) const;             // throws Error if out of range

    // Palette set index used by a page (name compared case-insensitively), or -1 if the page does
    // not use a set.
    int setForPage(const std::string& pageName) const;

    // For pages shown with one fixed palette (menus, BS4, movies...): its file name relative to
    // BIODATA (e.g. "menu.pal"); "" if none is known.
    std::string fixedPaletteForPage(const std::string& pageName) const;

private:
    struct PageRow { std::string page; int set; std::string fixed; };
    std::vector<PaletteSet> sets_;
    std::vector<PageRow> pages_;
};

} // namespace biosys
