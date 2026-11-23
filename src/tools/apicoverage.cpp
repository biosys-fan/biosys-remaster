// biosys-apicoverage: which of the original's 110 API commands the engine implements.
//   biosys-apicoverage [path/to/dll_api_wrappers.tsv]
// One line per command: `0xNN Name implemented|stub dll=<wrapper address or ->` (stubs also `needs=<feature>`),
// then a summary and the stubs grouped by the engine feature they need (* = the DLL actually sends it).
#include "engine/devroot.h"
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include "engine/engineapi.h"

using namespace biosys;

namespace {

bool exists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

// api_code (column 3, "0x15") -> dll address; columns: dll_address ghidra_name api_code method mapping
std::map<int, std::string> readWrappers(const std::string& path) {
    std::map<int, std::string> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> col;
        std::string cur;
        for (char c : line) {
            if (c == '\t') { col.push_back(cur); cur.clear(); }
            else cur += c;
        }
        col.push_back(cur);
        if (col.size() < 4) continue;
        out[static_cast<int>(std::strtol(col[2].c_str(), nullptr, 0))] = col[0];
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    std::string tsv;
    if (argc > 1) tsv = argv[1];
    else {
        std::string a = argv[0];
        size_t slash = a.rfind('/');
        std::string cands[] = {(slash == std::string::npos ? std::string(".") : a.substr(0, slash)) + "/../data/dll_api_wrappers.tsv",
                               "data/dll_api_wrappers.tsv", BIOSYS_DEV_ROOT "/port/data/dll_api_wrappers.tsv"};
        for (const std::string& c : cands)
            if (exists(c)) { tsv = c; break; }
    }
    std::map<int, std::string> wrappers = readWrappers(tsv);
    const std::vector<ApiCommandInfo>& table = apiCommandTable();
    int impl = 0, stub = 0, stubSent = 0;
    std::map<std::string, std::vector<std::string>> byCat;
    for (const ApiCommandInfo& c : table) {
        bool has = wrappers.count(c.code) != 0;
        std::printf("0x%02x %s %s dll=%s", c.code, c.name, c.implemented ? "implemented" : "stub", has ? wrappers[c.code].c_str() : "-");
        if (!c.implemented) {
            std::printf(" needs=%s", c.category);
            stub++;
            if (has) stubSent++;
            char buf[64];
            std::snprintf(buf, sizeof buf, "0x%02x ", c.code);
            byCat[c.category].push_back(std::string(buf) + c.name + (has ? "*" : ""));
        } else {
            impl++;
        }
        std::printf("\n");
    }
    std::printf("summary: commands=%zu implemented=%d stub=%d dll-wrappers=%zu stubs-the-dll-sends=%d\n", table.size(), impl, stub,
                wrappers.size(), stubSent);
    for (auto& kv : byCat) {
        std::printf("stubs needing %s:", kv.first.c_str());
        for (const std::string& s : kv.second) std::printf(" [%s]", s.c_str());
        std::printf("\n");
    }
    return 0;
}
