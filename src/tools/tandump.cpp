#include "engine/spriteanim.h"
#include <cstdio>
#include <cstdlib>
#include <zlib.h>

using namespace biosys;

static int cmdInfo(int argc, char** argv) {
    int rc = 0;
    for (int a = 2; a < argc; a++) {
        try {
            TanFile t(argv[a]);
            std::printf("%s frames=%d size=%dx%d transp=%u unk0c=0x%x table=%d physical=%d\n", argv[a],
                        t.frameCount(), t.width(), t.height(), t.transparency(), t.unk0c(), t.tableSize(),
                        t.physicalFrameCount());
            for (int i = 0; i < t.frameCount(); i++) {
                if (!t.hasFrame(i)) {
                    std::printf("frame %d off=0x%x MISSING\n", i, t.frameOffset(i));
                    continue;
                }
                Image8 img = t.frame(i);
                u32 crc = static_cast<u32>(crc32(0, img.px.data(), static_cast<uInt>(img.px.size())));
                std::printf("frame %d off=0x%x crc=%08x\n", i, t.frameOffset(i), crc);
            }
        } catch (const Error& e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            rc = 1;
        }
    }
    return rc;
}

int main(int argc, char** argv) {
    try {
        if (argc >= 3 && std::string(argv[1]) == "info") return cmdInfo(argc, argv);
        if (argc == 5 && std::string(argv[1]) == "export") {
            TanFile t(argv[2]);
            writeBmp(argv[4], t.frame(std::atoi(argv[3])), t.palette());
            return 0;
        }
    } catch (const Error& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    std::fprintf(stderr, "usage: biosys-tandump info <file>... | export <file> <frame> <out.bmp>\n");
    return 2;
}
