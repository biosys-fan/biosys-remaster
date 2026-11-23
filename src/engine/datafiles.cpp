#include "engine/datafiles.h"
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>

namespace biosys {

namespace {

bool exists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

// Resolve one path component of dir case-insensitively; "" if absent.
std::string child(const std::string& dir, const std::string& name) {
    std::string direct = dir + "/" + name;
    if (exists(direct)) return direct;
    DIR* d = opendir(dir.c_str());
    if (!d) return "";
    std::string found;
    while (const dirent* e = readdir(d)) {
        if (iequals(e->d_name, name)) { found = dir + "/" + e->d_name; break; }
    }
    closedir(d);
    return found;
}

} // namespace

DataFiles::DataFiles(std::vector<std::string> roots) : roots_(std::move(roots)) {}

std::vector<std::string> DataFiles::environmentRoots() {
    std::vector<std::string> roots;
    if (const char* env = std::getenv("BIOSYS_DATA"); env && *env) {
        std::string cur;
        for (const char* c = env;; c++) {
            if (*c == ':' || !*c) {
                if (!cur.empty()) roots.push_back(cur);
                cur.clear();
                if (!*c) break;
            } else {
                cur += *c;
            }
        }
    }
    return roots;
}

std::vector<std::string> DataFiles::developmentRoots() {
#ifdef BIOSYS_DEV_ROOT
    return {BIOSYS_DEV_ROOT "/setup-cd/installed/DATA1", BIOSYS_DEV_ROOT "/game-cd/files"};
#else
    return {};
#endif
}

DataFiles DataFiles::fromEnvironment(const std::vector<std::string>& fallbackRoots) {
    std::vector<std::string> roots = environmentRoots();
    if (roots.empty()) roots = fallbackRoots;
    return DataFiles(std::move(roots));
}

DataFiles DataFiles::fromEnvironment() { return fromEnvironment(developmentRoots()); }

std::string DataFiles::dataDirectory() const {
    for (const std::string& root : roots_) {
        std::string p = child(root, "Biodata");
        if (!p.empty()) return p;
    }
    return "";
}

std::string DataFiles::find(const char* top, const std::string& rel0) const {
    // an absolute-looking name: the data directory of API 0x1b (or the BIOTIME sibling) followed by a relative name
    std::string rel = rel0;
    {
        for (const std::string& root : roots_) {
            std::string p = child(root, top);
            if (p.empty() || rel.size() < p.size() || rel.compare(0, p.size(), p) != 0) continue;
            if (rel.size() > p.size() && rel[p.size()] != '/' && rel[p.size()] != '\\') continue;
            rel = rel.substr(p.size());
            break;
        }
    }
    std::vector<std::string> parts;
    std::string cur;
    for (char c : rel) {
        if (c == '\\' || c == '/') { if (!cur.empty()) parts.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) parts.push_back(cur);
    for (const std::string& part : parts)
        if (part == "..") return "";
    for (const std::string& root : roots_) {
        std::string p = child(root, top);
        for (size_t i = 0; !p.empty() && i < parts.size(); i++) p = child(p, parts[i]);
        if (!p.empty() && !parts.empty()) return p;
    }
    return "";
}

std::string DataFiles::findData(const std::string& rel) const { return find("Biodata", rel); }
std::string DataFiles::findTime(const std::string& rel) const { return find("Biotime", rel); }

std::string DataFiles::requireData(const std::string& rel) const {
    std::string p = findData(rel);
    if (p.empty()) throw Error("data file not found: BIODATA\\" + rel);
    return p;
}

std::string DataFiles::requireTime(const std::string& rel) const {
    std::string p = findTime(rel);
    if (p.empty()) throw Error("data file not found: BIOTIME\\" + rel);
    return p;
}

} // namespace biosys
