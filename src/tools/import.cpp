// biosys-import: first-run import of the two original CDs.
//   identify PATH...
//   import [--cancel-after-bytes N] DEST PATH...
//   status DEST
#include "install/importer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace biosys;
using namespace biosys::install;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: biosys-import identify PATH... | import [--cancel-after-bytes N] DEST PATH... | status DEST\n");
        return 1;
    }
    const std::string cmd = argv[1];
    if (cmd == "identify") {
        for (int i = 2; i < argc; i++) {
            std::string why;
            DiscKind k = identify(argv[i], &why);
            if (k == DiscKind::Unknown) std::printf("%s: unknown - %s\n", argv[i], why.c_str());
            else std::printf("%s: %s\n", argv[i], discName(k));
        }
        return 0;
    }
    if (cmd == "status") {
        if (argc != 3) return std::fprintf(stderr, "usage: biosys-import status DEST\n"), 1;
        Status s = status(argv[2]);
        std::printf("setup: %s\ngame: %s\ncomplete: %s\n", s.setup ? "yes" : "no", s.game ? "yes" : "no",
                    s.complete() ? "yes" : "no");
        return 0;
    }
    if (cmd == "import") {
        int i = 2;
        uint64_t cancelAfter = 0;
        bool cancel = false;
        if (i + 1 < argc && std::strcmp(argv[i], "--cancel-after-bytes") == 0) {
            cancelAfter = std::strtoull(argv[i + 1], nullptr, 10);
            cancel = true;
            i += 2;
        }
        if (argc - i < 2) return std::fprintf(stderr, "usage: biosys-import import [--cancel-after-bytes N] DEST PATH...\n"), 1;
        const std::string dest = argv[i++];
        int rc = 0;
        for (; i < argc; i++) {
            std::string lastStep;
            int lastPct = -1;
            ProgressFn fn = [&](const Progress& p) {
                if (cancel && p.done >= cancelAfter && p.done > 0) return false;
                int pct = p.total ? int(p.done * 100 / p.total) / 5 * 5 : -1;
                if (lastStep != p.step || pct != lastPct) {
                    std::fprintf(stderr, "%s: %s", discName(p.disc), p.step);
                    if (p.total && std::strcmp(p.step, "finish") != 0) std::fprintf(stderr, " %d%%", pct);
                    std::fprintf(stderr, "\n");
                    lastStep = p.step;
                    lastPct = pct;
                }
                return true;
            };
            try {
                DiscKind k = importDisc(argv[i], dest, fn);
                std::printf("imported %s from %s\n", discName(k), argv[i]);
            } catch (const Error& e) {
                std::fprintf(stderr, "biosys-import: %s\n", e.what());
                rc = 1;
                break;
            }
        }
        return rc;
    }
    std::fprintf(stderr, "biosys-import: unknown command \"%s\"\n", cmd.c_str());
    return 1;
}
