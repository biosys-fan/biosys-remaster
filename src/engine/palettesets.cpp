// Palette set tables (see palettesets.h). Data comes from data/palette_sets.tsv and
// data/page_palettes.tsv, generated from dll:100a36b0 (set table) and the location
// constructors dll:1000c270 / dll:10024f60 by tools/gen/gen_palette_tables.py.
#include "engine/palettesets.h"

#include <cstdlib>

namespace biosys {

const char* timeSlotName(int slot) {
    static const char* const names[SLOT_COUNT] = {"night", "dawn", "day", "dusk", "cloud", "gogs"};
    if (slot < 0 || slot >= SLOT_COUNT) return "?";
    return names[slot];
}

namespace {

// Splits a TSV file into non-comment lines of tab-separated fields (empty fields are kept).
std::vector<std::vector<std::string>> readTsv(const std::string& path, size_t minFields) {
    std::vector<u8> bytes = readFile(path);
    std::vector<std::vector<std::string>> rows;
    size_t pos = 0;
    int lineNo = 0;
    while (pos < bytes.size()) {
        size_t end = pos;
        while (end < bytes.size() && bytes[end] != '\n') ++end;
        std::string line(reinterpret_cast<const char*>(bytes.data()) + pos, end - pos);
        pos = end + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        size_t s = 0;
        for (;;) {
            size_t t = line.find('\t', s);
            if (t == std::string::npos) { f.push_back(line.substr(s)); break; }
            f.push_back(line.substr(s, t - s));
            s = t + 1;
        }
        if (f.size() < minFields)
            throw Error(path + ":" + std::to_string(lineNo) + ": too few fields");
        rows.push_back(std::move(f));
    }
    return rows;
}

int parseInt(const std::string& s, const std::string& path, int lo, int hi) {
    char* e = nullptr;
    long v = std::strtol(s.c_str(), &e, 10);
    if (s.empty() || *e != '\0' || v < lo || v > hi)
        throw Error(path + ": bad number '" + s + "'");
    return static_cast<int>(v);
}

} // namespace

PaletteTable PaletteTable::load(const std::string& dataDir) {
    const std::string dir = dataDir.empty() || dataDir.back() == '/' ? dataDir : dataDir + "/";
    const std::string setsPath = dir + "palette_sets.tsv";
    const std::string pagesPath = dir + "page_palettes.tsv";
    PaletteTable t;

    // set, group (A|B), slot, pal, xft
    std::vector<std::vector<bool>> seen;
    for (const auto& f : readTsv(setsPath, 5)) {
        int set = parseInt(f[0], setsPath, 0, 255);
        int slot = parseInt(f[2], setsPath, 0, SLOT_COUNT - 1);
        if (f[1] != "A" && f[1] != "B") throw Error(setsPath + ": bad group '" + f[1] + "'");
        int g = f[1] == "B" ? 1 : 0;
        if (set >= static_cast<int>(t.sets_.size())) {
            t.sets_.resize(static_cast<size_t>(set) + 1);
            seen.resize(t.sets_.size(), std::vector<bool>(2 * SLOT_COUNT, false));
        }
        t.sets_[static_cast<size_t>(set)].pal[g][slot] = f[3];
        t.sets_[static_cast<size_t>(set)].xft[g][slot] = f[4];
        seen[static_cast<size_t>(set)][static_cast<size_t>(g * SLOT_COUNT + slot)] = true;
    }
    if (t.sets_.empty()) throw Error(setsPath + ": no palette sets");
    for (const auto& s : seen)
        for (bool b : s)
            if (!b) throw Error(setsPath + ": incomplete palette set");

    // page, set, fixed, source
    for (const auto& f : readTsv(pagesPath, 3)) {
        PageRow r;
        r.page = f[0];
        r.set = parseInt(f[1], pagesPath, -1, 255);
        if (r.set >= t.setCount()) throw Error(pagesPath + ": set out of range for page " + r.page);
        r.fixed = f[2];
        t.pages_.push_back(std::move(r));
    }
    return t;
}

const PaletteSet& PaletteTable::set(int index) const {
    if (index < 0 || index >= setCount()) throw Error("palette set index out of range");
    return sets_[static_cast<size_t>(index)];
}

int PaletteTable::setForPage(const std::string& pageName) const {
    for (const PageRow& r : pages_)
        if (iequals(r.page, pageName)) return r.set;
    return -1;
}

std::string PaletteTable::fixedPaletteForPage(const std::string& pageName) const {
    for (const PageRow& r : pages_)
        if (iequals(r.page, pageName)) return r.fixed;
    return std::string();
}

} // namespace biosys
