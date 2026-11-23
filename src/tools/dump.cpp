#include "engine/pagefile.h"
#include <cstdio>
#include <cstdlib>
#include <zlib.h>

using namespace biosys;

static void hexPad(const char* label, u32 v, int digits, const char* tail) {
    std::printf("%s0x%0*x%s", label, digits, v, tail);
}

static void cmdList(const PageFile& pf) {
    for (int n = 1; n <= pf.pageCount(); n++) {
        const PageTableEntry& e = pf.entry(n);
        std::printf("%3d %-12s ", n, e.name.c_str());
        hexPad("off=", e.offset, 8, " ");
        hexPad("static=", e.staticSize, 4, " ");
        hexPad("dyna=", e.dynaSize, 6, " ");
        std::printf("flags=%u\n", e.flags);
    }
}

static void printAction(const Action& a) {
    std::printf("verb=%d args=", a.verb);
    for (int i = 0; i < 8; i++) std::printf(i ? ",%d" : "%d", a.arg[i]);
}

static void cmdDump(const PageFile& pf) {
    for (int n = 1; n <= pf.pageCount(); n++) {
        Page p = pf.loadPage(n);
        std::printf("page %d %s flags=0x%x size=%ux%u startx=%u bg=%u\n", n, p.name.c_str(), p.flags, p.width,
                    p.height, p.initialScrollX, p.background);
        for (size_t i = 0; i < p.files.size(); i++) std::printf("file %zu %s\n", i, p.files[i].c_str());
        for (size_t i = 0; i < p.ornaments.size(); i++) {
            const Ornament& o = p.ornaments[i];
            std::printf("orn %zu %s type=%u flags=0x%x pos=%d,%d image=%u transp=%d,%d,%d\n", i, o.name.c_str(),
                        o.type, o.flags, o.x, o.y, o.image, o.transparent.r, o.transparent.g, o.transparent.b);
        }
        for (size_t i = 0; i < p.hotspots.size(); i++) {
            const Hotspot& h = p.hotspots[i];
            std::printf("hot %zu %s rect=%d,%d,%d,%d flags=0x%x button=%u ", i, h.name.c_str(), h.left, h.top,
                        h.right, h.bottom, h.flags, h.button);
            printAction(h.action);
            std::printf("\n");
        }
        for (size_t i = 0; i < p.timers.size(); i++) {
            const PageTimer& t = p.timers[i];
            std::printf("timer %zu %s delay=%u ", i, t.name.c_str(), t.delay);
            printAction(t.action);
            std::printf("\n");
        }
        for (size_t i = 0; i < p.crossFades.size(); i++)
            std::printf("xfade %zu %s steps=%u\n", i, p.crossFades[i].name.c_str(), p.crossFades[i].steps);
        for (size_t i = 0; i < p.images.size(); i++) {
            Image8 img = pf.loadImage(p, static_cast<int>(i));
            u32 crc = static_cast<u32>(crc32(0, img.px.data(), static_cast<uInt>(img.px.size())));
            std::printf("img %zu %ux%u comp=%u crc=%08x\n", i, p.images[i].width, p.images[i].height,
                        p.images[i].compressed, crc);
        }
    }
}

static int usage() {
    std::fprintf(stderr,
                 "usage: biosys-dump list <pagefile>\n"
                 "       biosys-dump dump <pagefile>\n"
                 "       biosys-dump export <pagefile> <pageNameOrNumber> <imageIndex> <out.bmp> <palette.pal>\n");
    return 2;
}

static long parseNum(const std::string& s, const char* what) {
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (s.empty() || *end) throw Error(std::string("bad ") + what + ": " + s);
    return v;
}

int main(int argc, char** argv) {
    try {
        if (argc < 3) return usage();
        std::string cmd = argv[1];
        PageFile pf(argv[2]);
        if (cmd == "list" && argc == 3) { cmdList(pf); return 0; }
        if (cmd == "dump" && argc == 3) { cmdDump(pf); return 0; }
        if (cmd == "export" && argc == 7) {
            std::string key = argv[3];
            int num = key.find_first_not_of("0123456789") == std::string::npos && !key.empty()
                          ? static_cast<int>(parseNum(key, "page number")) : pf.findPage(key);
            if (num < 1 || num > pf.pageCount()) throw Error("no such page: " + key);
            Page p = pf.loadPage(num);
            Image8 img = pf.loadImage(p, static_cast<int>(parseNum(argv[4], "image index")));
            writeBmp(argv[5], img, Palette::load(argv[6]));
            std::printf("wrote %s %dx%d\n", argv[5], img.w, img.h);
            return 0;
        }
        return usage();
    } catch (const Error& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
