// The light picture cleaner as a command-line tool (engine/picclean.*).
//   biosys-picclean run --idx F --w W --h H --pal F [--ref-pal F] [--key N] [--prot F] [--wrap] [--scale S] [--kernel K] [--t0 N]
//                       [--no-dedither] --out F [--out-dedither F]     same arguments and output files as scripts/light/reference.py; prints T=N
//   biosys-picclean soft --idx F --w W --h H --pal F --table F [--wrap] --out F      stage 1 with a soft partner table (65536 bytes, weights 0..32); RGB out
//   biosys-picclean weights                                            the integer weight tables (kernel scale r taps...), as reference.py's weight_table
//   biosys-picclean flicker --idx F --w W --h H --ref-pal F --trace F [--hyst N] [--soft RAMP100] [--slew N] [--step K] [--rows A:B]
//                                                                    the palette sequence of a dawn / dusk (a `palettetrace` output) through the cleaner:
//                                                                    per step the pixels whose partner decision flips and the cleaned luma jumps
//   biosys-picclean bench [--w 2560 --h 1024] [--reps N]               time per stage on a synthetic dithered picture
#include "engine/picclean.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <fstream>

using namespace biosys;
using namespace biosys::picclean;

namespace {

std::vector<uint8_t> slurp(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw Error("cannot read " + path);
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    return d;
}
void spit(const std::string& path, const std::vector<uint8_t>& d) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw Error("cannot write " + path);
    std::fwrite(d.data(), 1, d.size(), f);
    std::fclose(f);
}
std::vector<Color> readPal(const std::string& path) {
    const std::vector<uint8_t> d = slurp(path);
    if (d.size() != 768) throw Error("palette must be 768 bytes: " + path);
    std::vector<Color> p(256);
    for (int i = 0; i < 256; i++) { p[i].r = d[3 * i]; p[i].g = d[3 * i + 1]; p[i].b = d[3 * i + 2]; }
    return p;
}
double ms(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

int cmdRun(const std::map<std::string, std::string>& a, const std::map<std::string, bool>& flags) {
    auto need = [&](const char* k) -> const std::string& {
        auto it = a.find(k);
        if (it == a.end()) throw Error(std::string("missing ") + k);
        return it->second;
    };
    Params p;
    p.w = std::atoi(need("--w").c_str());
    p.h = std::atoi(need("--h").c_str());
    const std::vector<uint8_t> ix = slurp(need("--idx"));
    if (ix.size() != static_cast<size_t>(p.w) * p.h) throw Error("idx size does not match --w x --h");
    const std::vector<Color> pal = readPal(need("--pal"));
    std::vector<Color> ref;
    if (a.count("--ref-pal")) { ref = readPal(a.at("--ref-pal")); p.ref = ref.data(); }
    std::vector<uint8_t> prot;
    if (a.count("--prot")) { prot = slurp(a.at("--prot")); if (prot.size() != ix.size()) throw Error("prot size"); p.prot = prot.data(); }
    if (a.count("--key")) p.key = std::atoi(a.at("--key").c_str());
    p.ix = ix.data();
    p.pal = pal.data();
    p.wrap = flags.count("--wrap") != 0;
    p.dedither = flags.count("--no-dedither") == 0;
    if (a.count("--scale")) p.scale = std::atoi(a.at("--scale").c_str());
    if (a.count("--t0")) p.t0 = std::atoi(a.at("--t0").c_str());
    if (a.count("--kernel") && !parseKernel(a.at("--kernel"), p.kernel)) throw Error("bad --kernel");
    Result r;
    process(p, r);
    spit(need("--out"), r.out);
    if (a.count("--out-dedither")) spit(a.at("--out-dedither"), r.dd);
    std::printf("T=%d\n", r.T);
    return 0;
}

int cmdSoft(const std::map<std::string, std::string>& a, const std::map<std::string, bool>& flags) {
    auto need = [&](const char* k) -> const std::string& {
        auto it = a.find(k);
        if (it == a.end()) throw Error(std::string("missing ") + k);
        return it->second;
    };
    const int w = std::atoi(need("--w").c_str()), h = std::atoi(need("--h").c_str());
    const std::vector<uint8_t> ix = slurp(need("--idx")), table = slurp(need("--table"));
    if (ix.size() != static_cast<size_t>(w) * h || table.size() != 65536) throw Error("sizes");
    const std::vector<Color> pal = readPal(need("--pal"));
    std::vector<uint8_t> rgb(ix.size() * 3);
    dedither(ix.data(), w, h, pal.data(), nullptr, nullptr, flags.count("--wrap") != 0, 0, h, rgb.data(), table.data());
    spit(need("--out"), rgb);
    return 0;
}

int cmdWeights() {
    static const char* names[] = {"bilinear", "catmull", "mitchell", "lanczos3"};
    static const Kernel ks[] = {Kernel::Bilinear, Kernel::Catmull, Kernel::Mitchell, Kernel::Lanczos3};
    for (int k = 0; k < 4; k++)
        for (int s = 1; s <= 4; s++) {
            int tab[4][6];
            const int r = weightTable(ks[k], s, tab);
            std::printf("%s %d %d", names[k], s, r);
            for (int p = 0; p < s; p++) {
                std::printf(" [");
                for (int j = 0; j < 2 * r; j++) std::printf(j ? ",%d" : "%d", tab[p][j]);
                std::printf("]");
            }
            std::printf("\n");
        }
    return 0;
}

// a synthetic dithered sky / grass / lettering picture of the kind the game has (ordered dither between close palette entries)
void synth(int w, int h, std::vector<uint8_t>& ix, std::vector<Color>& pal) {
    pal.assign(256, Color());
    for (int i = 0; i < 256; i++) { pal[i].r = static_cast<uint8_t>(i); pal[i].g = static_cast<uint8_t>(255 - i / 2); pal[i].b = static_cast<uint8_t>((i * 7) & 255); }
    for (int i = 0; i < 64; i++) { pal[2 * i].r = static_cast<uint8_t>(80 + i); pal[2 * i].g = static_cast<uint8_t>(100 + i); pal[2 * i].b = static_cast<uint8_t>(140 + i);
                                   pal[2 * i + 1].r = static_cast<uint8_t>(86 + i); pal[2 * i + 1].g = static_cast<uint8_t>(106 + i); pal[2 * i + 1].b = static_cast<uint8_t>(152 + i); }
    ix.assign(static_cast<size_t>(w) * h, 0);
    uint32_t rng = 12345;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const int base = (y * 60 / h) * 2;               // gradient bands
            uint8_t v = static_cast<uint8_t>(base + ((x + y) & 1));
            if (y > h * 3 / 4) { rng = rng * 1664525u + 1013904223u; v = static_cast<uint8_t>(130 + ((rng >> 24) & 15)); }   // noisy "grass"
            if ((x / 40 + y / 24) % 7 == 0 && (x % 5 == 0 || y % 7 == 0)) v = 250;           // thin lines
            ix[static_cast<size_t>(y) * w + x] = v;
        }
}

int cmdBench(const std::map<std::string, std::string>& a) {
    const int reps = a.count("--reps") ? std::atoi(a.at("--reps").c_str()) : 5;
    const int sizes[2][2] = {{2560, 1024}, {640, 480}};
    for (const auto& sz : sizes) {
        const int w = sz[0], h = sz[1];
        std::vector<uint8_t> ix;
        std::vector<Color> pal;
        synth(w, h, ix, pal);
        std::vector<Color> ref = pal;
        for (int i = 0; i < 256; i++) { pal[i].r = static_cast<uint8_t>(ref[i].r * 3 / 4); pal[i].g = static_cast<uint8_t>(ref[i].g * 3 / 4); pal[i].b = static_cast<uint8_t>(ref[i].b * 3 / 4); }
        std::vector<uint8_t> cl(65536), rgb(static_cast<size_t>(w) * h * 3);
        uint32_t hist[256];
        double t[8] = {0};
        std::vector<uint32_t> out;
        for (int rep = 0; rep < reps; rep++) {
            auto t0 = std::chrono::steady_clock::now();
            histogram(ix.data(), w, h, nullptr, hist);
            auto t1 = std::chrono::steady_clock::now();
            const int T = threshold(pal.data(), ref.data(), hist);
            partnerTable(pal.data(), T, -1, cl.data());
            auto t2 = std::chrono::steady_clock::now();
            dedither(ix.data(), w, h, pal.data(), cl.data(), nullptr, true, 0, h, rgb.data());
            auto t3 = std::chrono::steady_clock::now();
            t[0] += ms(t0, t1); t[1] += ms(t1, t2); t[2] += ms(t2, t3);
            for (int s = 2; s <= 4; s++) {
                out.assign(static_cast<size_t>(w) * s * h * s, 0);
                auto e0 = std::chrono::steady_clock::now();
                enlargeRgb(rgb.data(), w, h, s, Kernel::Catmull, true, 0, h, out.data(), static_cast<size_t>(w) * s);
                auto e1 = std::chrono::steady_clock::now();
                t[s + 1] += ms(e0, e1);
            }
        }
        std::printf("%dx%d (mean of %d): histogram %.2f ms  threshold+partner table %.3f ms  de-dither %.2f ms  enlarge 2x %.2f ms  3x %.2f ms  4x %.2f ms\n", w, h, reps,
                    t[0] / reps, t[1] / reps, t[2] / reps, t[3] / reps, t[4] / reps, t[5] / reps);
    }
    return 0;
}


// the four boxes' qualification (bit 0 NW, 1 NE, 2 SW, 3 SE) of every pixel (wrapped horizontally, replicated vertically)
void boxMasks(const std::vector<uint8_t>& ix, int w, int h, const uint8_t* cl, std::vector<uint8_t>& out, int y0, int y1) {
    out.assign(static_cast<size_t>(w) * h, 0);
    std::vector<int> xl(static_cast<size_t>(w)), xr(static_cast<size_t>(w));
    for (int x = 0; x < w; x++) { xl[x] = (x + w - 1) % w; xr[x] = (x + 1) % w; }
    for (int y = y0; y < y1; y++) {
        const uint8_t* r0 = &ix[static_cast<size_t>(std::max(y - 1, 0)) * w];
        const uint8_t* r1 = &ix[static_cast<size_t>(y) * w];
        const uint8_t* r2 = &ix[static_cast<size_t>(std::min(y + 1, h - 1)) * w];
        for (int x = 0; x < w; x++) {
            const uint8_t* row = cl + r1[x] * 256;
            const int a = row[r0[xl[x]]], b = row[r0[x]], cq = row[r0[xr[x]]], d = row[r1[xl[x]]], f = row[r1[xr[x]]], g = row[r2[xl[x]]], hh = row[r2[x]], i = row[r2[xr[x]]];
            out[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>((a && b && d ? 1 : 0) | (b && cq && f ? 2 : 0) | (d && g && hh ? 4 : 0) | (f && hh && i ? 8 : 0));
        }
    }
}

int cmdFlicker(const std::map<std::string, std::string>& a) {
    auto need = [&](const char* k) -> const std::string& {
        auto it = a.find(k);
        if (it == a.end()) throw Error(std::string("missing ") + k);
        return it->second;
    };
    const int w = std::atoi(need("--w").c_str()), h = std::atoi(need("--h").c_str());
    const std::vector<uint8_t> ix = slurp(need("--idx"));
    if (ix.size() != static_cast<size_t>(w) * h) throw Error("idx size");
    const std::vector<Color> ref = readPal(need("--ref-pal"));
    const int hyst = a.count("--hyst") ? std::atoi(a.at("--hyst").c_str()) : 0;
    const bool soft = a.count("--soft") != 0;
    const int ramp = soft ? std::atoi(a.at("--soft").c_str()) : 0;
    const int slew = a.count("--slew") ? std::atoi(a.at("--slew").c_str()) : 0;
    const int stepK = a.count("--step") ? std::max(1, std::atoi(a.at("--step").c_str())) : 1;
    int r0 = 0, r1 = h;
    if (a.count("--rows") && std::sscanf(a.at("--rows").c_str(), "%d:%d", &r0, &r1) != 2) throw Error("--rows A:B");
    std::vector<std::vector<Color>> pals;
    {
        std::ifstream f(need("--trace"));
        std::string line;
        while (std::getline(f, line)) {
            if (line.compare(0, 8, "palette ") != 0) continue;
            const size_t sp = line.rfind(' ');
            const std::string hex = line.substr(sp + 1);
            if (hex.size() != 1536) continue;
            std::vector<Color> p(256);
            for (int i = 0; i < 256; i++) {
                auto hv = [&](int k) { return static_cast<uint8_t>(std::stoi(hex.substr(static_cast<size_t>(k) * 2, 2), nullptr, 16)); };
                p[i].r = hv(3 * i); p[i].g = hv(3 * i + 1); p[i].b = hv(3 * i + 2);
            }
            pals.push_back(std::move(p));
        }
    }
    if (pals.size() < 2) throw Error("trace holds fewer than two palettes");
    uint32_t hist[256];
    histogram(ix.data(), w, h, nullptr, hist);
    const size_t n = static_cast<size_t>(w) * h;
    PartnerState ps;
    std::vector<uint8_t> cl(65536), wt(65536), rgb(n * 3), prevRgb, mask, prevMask, lastFlip(n, 0);
    std::vector<int> prevRaw(n, 0), prevC(n, 0);
    std::vector<int> lastStep(n, -1000);
    long flipsTotal = 0, flipsMax = 0, stepsFlip = 0, toggles = 0, visTotal = 0, visMax = 0, visSteps = 0, visStepsAny = 0;
    double jcMax = 0, juMax = 0, meanAbsC = 0, meanAbsU = 0;
    int steps = 0, Tfirst = 0, Tlast = 0;
    for (size_t k = 0; k < pals.size(); k += static_cast<size_t>(stepK)) {
        const std::vector<Color>& pal = pals[k];
        int T = 0;
        if (slew > 0) {
            T = threshold(pal.data(), ref.data(), hist);
            bool sf = false;
            const uint8_t* t = ps.updateSlew(pal.data(), T, -1, slew, sf);
            std::memcpy(wt.data(), t, 65536);
            dedither(ix.data(), w, h, pal.data(), nullptr, nullptr, true, r0, r1, rgb.data(), wt.data());
        } else if (soft) {
            const int T100 = thresholdT100(pal.data(), ref.data(), hist);
            softTable(pal.data(), T100, ramp, -1, wt.data());
            dedither(ix.data(), w, h, pal.data(), nullptr, nullptr, true, r0, r1, rgb.data(), wt.data());
            T = T100 / 100;
        } else {
            T = threshold(pal.data(), ref.data(), hist);
            const uint8_t* t = ps.update(pal.data(), T, -1, hyst);
            std::memcpy(cl.data(), t, 65536);
            dedither(ix.data(), w, h, pal.data(), cl.data(), nullptr, true, r0, r1, rgb.data());
            boxMasks(ix, w, h, cl.data(), mask, r0, r1);
        }
        if (steps == 0) Tfirst = T;
        Tlast = T;
        long flips = 0, vis = 0;
        double jc = 0, ju = 0, sc = 0, su = 0;
        for (int y = r0; y < r1; y++)
            for (int x = 0; x < w; x++) {
                const size_t i = static_cast<size_t>(y) * w + x;
                const Color& pc = pal[ix[i]];
                const int lraw = 299 * pc.r + 587 * pc.g + 114 * pc.b;
                const int lc = 299 * rgb[i * 3] + 587 * rgb[i * 3 + 1] + 114 * rgb[i * 3 + 2];
                if (steps > 0) {
                    const double dU = std::abs(lraw - prevRaw[i]) / 1000.0, dC = std::abs(lc - prevC[i]) / 1000.0;
                    ju = std::max(ju, dU); jc = std::max(jc, dC);
                    su += dU; sc += dC;
                    if (dC - dU >= 3.0) vis++;
                    if (!soft && slew <= 0 && mask[i] != prevMask[i]) {
                        flips++;
                        if (static_cast<int>(steps) - lastStep[i] <= 8) toggles++;
                        lastStep[i] = steps;
                    }
                }
                prevRaw[i] = lraw;
                prevC[i] = lc;
            }
        if (steps > 0) {
            flipsTotal += flips;
            flipsMax = std::max(flipsMax, flips);
            stepsFlip += flips > 0;
            visTotal += vis;
            visMax = std::max(visMax, vis);
            visSteps += vis >= 50;
            visStepsAny += vis > 0;
            jcMax = std::max(jcMax, jc);
            juMax = std::max(juMax, ju);
            const double px = static_cast<double>(r1 - r0) * w;
            meanAbsC += sc / px;
            meanAbsU += su / px;
            if (a.count("--verbose")) std::printf("step %zu T=%d flips=%ld vis=%ld jc=%.2f ju=%.2f\n", k, T, flips, vis, jc, ju);
        }
        prevMask = mask;
        steps++;
    }
    const int ds = steps - 1;
    std::printf("flicker steps=%d T=%d..%d flipSteps=%ld flipsTotal=%ld flipsMax=%ld toggles=%ld | maxJumpCleaned=%.2f maxJumpRaw=%.2f meanJumpCleaned=%.4f meanJumpRaw=%.4f | "
                "excessPixelsTotal=%ld excessMax=%ld stepsWithExcess=%ld stepsWithExcess50=%ld\n",
                ds, Tfirst, Tlast, stepsFlip, flipsTotal, flipsMax, toggles, jcMax, juMax, meanAbsC / ds, meanAbsU / ds, visTotal, visMax, visStepsAny, visSteps);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw Error("usage: biosys-picclean run|weights|bench ...");
        const std::string cmd = argv[1];
        std::map<std::string, std::string> a;
        std::map<std::string, bool> flags;
        for (int i = 2; i < argc; i++) {
            const std::string k = argv[i];
            if (k == "--wrap" || k == "--no-dedither" || k == "--verbose") { flags[k] = true; a[k] = "1"; }
            else if (i + 1 < argc) a[k] = argv[++i];
            else throw Error("missing value for " + k);
        }
        if (cmd == "run") return cmdRun(a, flags);
        if (cmd == "weights") return cmdWeights();
        if (cmd == "soft") return cmdSoft(a, flags);
        if (cmd == "bench") return cmdBench(a);
        if (cmd == "flicker") return cmdFlicker(a);
        throw Error("unknown command " + cmd);
    } catch (const Error& e) {
        std::fprintf(stderr, "biosys-picclean: %s\n", e.what());
        return 1;
    }
}
