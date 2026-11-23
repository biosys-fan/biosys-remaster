// biosys-wavdump: inspect WAV files and render them through the software mixer.
//   info <file>...
//   render <out.wav> <file> [gain pan rate loopSeconds]
#include "engine/audio.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>

using namespace biosys;

static int cmdInfo(int argc, char** argv) {
    int bad = 0;
    for (int i = 0; i < argc; i++) {
        try {
            SoundData s = SoundData::loadWav(argv[i]);
            int peak = 0;
            for (int16_t v : s.samples) peak = std::max(peak, std::abs(static_cast<int>(v)));
            std::printf("%s rate=%d channels=%d bits=%d frames=%zu duration=%.3f peak=%d\n", argv[i],
                        s.sampleRate, s.channels, s.sourceBits, s.frames,
                        static_cast<double>(s.frames) / s.sampleRate, peak);
        } catch (const Error& e) {
            std::printf("%s ERROR %s\n", argv[i], e.what());
            bad = 1;
        }
    }
    return bad;
}

static void put16(std::ofstream& f, unsigned v) { char b[2] = {char(v & 255), char(v >> 8)}; f.write(b, 2); }
static void put32(std::ofstream& f, unsigned v) { put16(f, v & 0xFFFF); put16(f, v >> 16); }

static int cmdRender(int argc, char** argv) {
    if (argc < 2) return -1;
    float gain = argc > 2 ? static_cast<float>(std::atof(argv[2])) : 1.f;
    float pan = argc > 3 ? static_cast<float>(std::atof(argv[3])) : 0.f;
    double rate = argc > 4 ? std::atof(argv[4]) : 0.0;
    double loopSec = argc > 5 ? std::atof(argv[5]) : 0.0;

    auto snd = std::make_shared<const SoundData>(SoundData::loadWav(argv[1]));
    Mixer mx(44100, 2);
    int h = mx.open(snd);
    mx.setGain(h, gain);
    mx.setPan(h, pan);
    mx.setRate(h, rate);
    mx.setLoop(h, loopSec > 0);
    mx.play(h, true);

    std::vector<int16_t> out;
    std::vector<int16_t> blk(1024 * 2);
    const size_t limit = static_cast<size_t>((loopSec > 0 ? loopSec : 600.0) * 44100);
    size_t total = 0;
    while (total < limit && (loopSec > 0 || mx.isPlaying(h))) {
        size_t n = std::min<size_t>(1024, limit - total);
        mx.mix(blk.data(), static_cast<int>(n));
        out.insert(out.end(), blk.begin(), blk.begin() + n * 2);
        total += n;
    }

    std::ofstream f(argv[0], std::ios::binary);
    if (!f) throw Error(std::string("cannot write ") + argv[0]);
    unsigned bytes = static_cast<unsigned>(out.size() * 2);
    f.write("RIFF", 4); put32(f, 36 + bytes); f.write("WAVEfmt ", 8);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, 44100); put32(f, 44100 * 4); put16(f, 4); put16(f, 16);
    f.write("data", 4); put32(f, bytes);
    for (int16_t v : out) put16(f, static_cast<uint16_t>(v));
    if (!f) throw Error("write failed");
    std::printf("wrote %s: %zu frames (%.3f s)\n", argv[0], total, total / 44100.0);
    return 0;
}

int main(int argc, char** argv) {
    try {
        if (argc >= 3 && std::string(argv[1]) == "info") return cmdInfo(argc - 2, argv + 2);
        if (argc >= 4 && std::string(argv[1]) == "render") {
            int r = cmdRender(argc - 2, argv + 2);
            if (r >= 0) return r;
        }
    } catch (const Error& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    std::fprintf(stderr, "usage: biosys-wavdump info <file>...\n"
                         "       biosys-wavdump render <out.wav> <file> [gain pan rate loopSeconds]\n");
    return 2;
}
