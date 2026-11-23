// build/biosys-selftext_check [dir]: lists every string id literal passed to ShowMessage / ShowMessageDeferred /
// ShowMessageLine in src/game/self*.cpp, obj_*.cpp, gameobj.cpp and bg_*.cpp and checks that dllString(id) holds it.
// Exit 1 when an id is missing. Ids that are not literals are listed as "not checked".
#include "game/self_text.gen.h"
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using biosys::game::dllString;

namespace {
bool wanted(const std::string& n) {
    auto starts = [&](const char* p) { return n.rfind(p, 0) == 0; };
    if (n.size() < 4 || n.compare(n.size() - 4, 4, ".cpp") != 0) return false;
    return starts("self") || starts("obj_") || starts("bg_") || n == "gameobj.cpp";
}
bool isLiteral(const std::string& s, long& v) {
    static const std::regex re(R"(^\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*$)");
    std::smatch m;
    if (!std::regex_match(s, m, re)) return false;
    v = std::strtol(m[1].str().c_str(), nullptr, 0);
    return true;
}
// the text without comments (newlines kept, so line numbers hold)
std::string stripComments(const std::string& t) {
    std::string o;
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == '/' && i + 1 < t.size() && t[i + 1] == '/') {
            while (i < t.size() && t[i] != '\n') i++;
            o += '\n';
        } else if (t[i] == '/' && i + 1 < t.size() && t[i + 1] == '*') {
            i += 2;
            while (i + 1 < t.size() && !(t[i] == '*' && t[i + 1] == '/')) { if (t[i] == '\n') o += '\n'; i++; }
            i++;
        } else if (t[i] == '"') {
            o += t[i++];
            while (i < t.size() && t[i] != '"') { if (t[i] == '\\') o += t[i++]; o += t[i++]; }
            o += '"';
        } else {
            o += t[i];
        }
    }
    return o;
}
// splits the argument list text at top-level commas
std::vector<std::string> splitArgs(const std::string& s) {
    std::vector<std::string> out;
    int depth = 0;
    std::string cur;
    for (char c : s) {
        if (c == '(' || c == '[' || c == '{') depth++;
        if (c == ')' || c == ']' || c == '}') depth--;
        if (c == ',' && depth == 0) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}
}

int main(int argc, char** argv) {
    fs::path dir = argc > 1 ? argv[1] : "src/game";
    if (!fs::is_directory(dir)) { std::fprintf(stderr, "selftext_check: no directory %s\n", dir.string().c_str()); return 2; }
    // which arguments carry string ids: ShowMessage(p1, id, second, p4), ShowMessageDeferred(id), ShowMessageLine(id, second)
    struct Fn { const char* name; std::vector<int> idArgs; };
    const Fn fns[] = {{"ShowMessage", {1, 2}}, {"ShowMessageDeferred", {0}}, {"ShowMessageLine", {0, 1}}};
    int checked = 0, missing = 0, unchecked = 0;
    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file() && wanted(e.path().filename().string())) files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (auto& f : files) {
        std::ifstream in(f);
        std::stringstream ss;
        ss << in.rdbuf();
        std::string text = stripComments(ss.str());
        for (const Fn& fn : fns) {
            std::regex re(std::string("(^|[^A-Za-z0-9_])") + fn.name + R"(\s*\()");
            for (auto it = std::sregex_iterator(text.begin(), text.end(), re); it != std::sregex_iterator(); ++it) {
                size_t open = it->position() + it->length();    // just after '('
                size_t p = open;
                int depth = 1;
                while (p < text.size() && depth > 0) {
                    if (text[p] == '(') depth++;
                    else if (text[p] == ')') depth--;
                    p++;
                }
                std::string args = text.substr(open, p - open - 1);
                // skip the declarations and definitions "void Self::ShowMessage(int p1, ..."
                if (args.find("int ") != std::string::npos) continue;
                size_t line = 1 + std::count(text.begin(), text.begin() + static_cast<long>(open), '\n');
                auto a = splitArgs(args);
                for (int idx : fn.idArgs) {
                    if (idx >= static_cast<int>(a.size())) continue;
                    long v;
                    if (!isLiteral(a[idx], v)) {
                        std::printf("not checked  %s:%zu %s arg %d: %s\n", f.filename().string().c_str(), line, fn.name, idx,
                                    a[idx].c_str());
                        unchecked++;
                        continue;
                    }
                    if (v == 0 || v == 0xffff) continue;      // none / clear
                    checked++;
                    if (!dllString(static_cast<int>(v))) {
                        std::printf("MISSING      %s:%zu %s id %ld (0x%lx)\n", f.filename().string().c_str(), line, fn.name, v, v);
                        missing++;
                    }
                }
            }
        }
    }
    std::printf("selftext_check: %d ids checked, %d missing, %d not literals\n", checked, missing, unchecked);
    return missing ? 1 : 0;
}
