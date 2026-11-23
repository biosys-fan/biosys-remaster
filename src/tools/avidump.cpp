// avidump: inspect AVI movies. See usage().
#include "engine/avi.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <zlib.h>

using namespace biosys;

static void usage() {
    std::fprintf(stderr,
                 "usage: biosys-avidump info <file>\n"
                 "       biosys-avidump frames <file>\n"
                 "       biosys-avidump export <file> <frameIndex> <out.bmp>\n"
                 "       biosys-avidump audio <file> <out.raw>\n");
    std::exit(2);
}

static void toRgb(const AviFile& a, std::vector<u8>& rgb) {
    const Image8& im = a.frame();
    const Palette& pal = a.palette();
    rgb.resize(im.px.size() * 3);
    for (size_t i = 0; i < im.px.size(); i++) {
        const Color& c = pal.c[im.px[i]];
        rgb[i * 3] = c.r; rgb[i * 3 + 1] = c.g; rgb[i * 3 + 2] = c.b;
    }
}

static int run(int argc, char** argv) {
    if (argc < 3) usage();
    std::string cmd = argv[1];
    AviFile avi(argv[2]);
    if (cmd == "info" && argc == 3) {
        std::printf("%dx%d frames=%d fps=%.3f audio=", avi.width(), avi.height(), avi.frameCount(), avi.fps());
        if (avi.hasAudio())
            std::printf("pcm_u%d %dHz %dch samples=%u\n", avi.audioBits(), avi.audioSampleRate(), avi.audioChannels(),
                        avi.audioSampleCount());
        else
            std::printf("none samples=0\n");
    } else if (cmd == "frames" && argc == 3) {
        std::vector<u8> rgb;
        for (int i = 0; avi.nextFrame(); i++) {
            toRgb(avi, rgb);
            uLong crc = crc32(0, rgb.data(), static_cast<uInt>(rgb.size()));
            std::printf("frame %d crc=%08lx\n", i, crc);
        }
    } else if (cmd == "export" && argc == 5) {
        int idx = std::atoi(argv[3]);
        if (idx < 0 || idx >= avi.frameCount()) throw Error("frame index out of range");
        for (int i = 0; i <= idx; i++) avi.nextFrame();
        writeBmp(argv[4], avi.frame(), avi.palette());
    } else if (cmd == "audio" && argc == 4) {
        std::vector<u8> pcm = avi.readAllAudio();
        FILE* f = std::fopen(argv[3], "wb");
        if (!f || (!pcm.empty() && std::fwrite(pcm.data(), 1, pcm.size(), f) != pcm.size())) throw Error("cannot write output");
        std::fclose(f);
    } else {
        usage();
    }
    return 0;
}

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const Error& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
