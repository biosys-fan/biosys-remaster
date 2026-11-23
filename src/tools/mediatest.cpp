// biosys-mediatest: headless script driver for the media systems (no SDL).
//   biosys-mediatest [script-file]      (script on stdin when no file is given)
// One command per line, '#' starts a comment. Results are printed as "key=value" lines; the test helper
// (tests/test_media.py) parses them. Commands:
//   page W H <fill|pattern>      pal <gray|tan FILE|file PAL>      clock T | adv DT      update
//   frame clear V                paint                              draw VIEWX VIEWY      pagechange NAME
//   bmp page|frame PATH          crc page|frame X Y W H             pix page|frame X Y    events   echo ...
//   sprite.open k=v...  sprite.play H RESTART [PRIO]  sprite.stop H  sprite.close H  sprite.width H W
//   sprite.info H  sprite.sound H  sprite.freeall  sprite.pageSize W H
//   movie.open file=.. x= y= loop= audio=  movie.play H RESTART  movie.stop H  movie.close H  movie.key H R G B
//   movie.vol H V  movie.bind H PAGE  movie.double H  movie.savebg H  movie.synth H  movie.restore H STOP CLOSE
//   movie.info H  movie.pause  movie.resume
//   sound.open file= loop= freq= pan= vol= auto=  sound.play H RESTART  sound.stop H  sound.close H
//   sound.prop H NAME V  sound.playwave FILE FLAGS  sound.pan H ENABLE X WRAP  sound.view X W  sound.info H
//   sound.pause  sound.resume NOTIFY  sound.count
//   mix SECONDS [out.wav]        mixuntil movie|sound H MAXSECONDS
#include <zlib.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include "engine/media.h"

using namespace biosys;

namespace {

struct World {
    DataFiles files = DataFiles::fromEnvironment();
    Mixer mixer;
    SoundSystem sounds{mixer, files};
    SpriteSystem sprites{sounds, files};
    MovieSystem movies{mixer, files};
    Image8 page;
    Frame frame;
    Palette pal;
    bool hasPal = false;
    uint32_t now = 0;
};

std::map<std::string, std::string> kv(const std::vector<std::string>& t, size_t from) {
    std::map<std::string, std::string> m;
    for (size_t i = from; i < t.size(); i++) {
        auto e = t[i].find('=');
        if (e != std::string::npos) m[t[i].substr(0, e)] = t[i].substr(e + 1);
    }
    return m;
}
int geti(const std::map<std::string, std::string>& m, const char* k, int d) {
    auto it = m.find(k);
    return it == m.end() ? d : std::atoi(it->second.c_str());
}
std::string gets(const std::map<std::string, std::string>& m, const char* k) {
    auto it = m.find(k);
    return it == m.end() ? "" : it->second;
}

void writeWav(const std::string& path, const std::vector<int16_t>& s) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw Error("cannot write " + path);
    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    uint32_t bytes = static_cast<uint32_t>(s.size() * 2);
    f.write("RIFF", 4); w32(36 + bytes); f.write("WAVEfmt ", 8); w32(16); w16(1); w16(2); w32(44100); w32(44100 * 4);
    w16(4); w16(16); f.write("data", 4); w32(bytes);
    f.write(reinterpret_cast<const char*>(s.data()), bytes);
}

void printEvents(World& w) {
    const char* names[] = {"sound", "sprite", "movie"};
    std::vector<MediaEvent> all;
    for (auto& e : w.sounds.takeEvents()) all.push_back(e);
    for (auto& e : w.sprites.takeEvents()) all.push_back(e);
    for (auto& e : w.movies.takeEvents()) all.push_back(e);
    for (auto& e : all) std::printf("event %s h=%d code=%d arg=%d\n", names[e.kind], e.handle, e.code, e.arg);
    std::printf("events n=%zu\n", all.size());
}

void mixBlocks(World& w, int frames, std::vector<int16_t>* keep, int& pl, int& pr) {
    std::vector<int16_t> buf(1024 * 2);
    while (frames > 0) {
        int n = std::min(frames, 1024);
        w.mixer.mix(buf.data(), n);
        for (int i = 0; i < n; i++) {
            pl = std::max(pl, std::abs(static_cast<int>(buf[2 * i])));
            pr = std::max(pr, std::abs(static_cast<int>(buf[2 * i + 1])));
        }
        if (keep) keep->insert(keep->end(), buf.begin(), buf.begin() + n * 2);
        frames -= n;
    }
}

Image8& target(World& w, const std::string& which, Image8& tmp) {
    if (which == "page") return w.page;
    tmp = w.frame.toImage();
    return tmp;
}

void run(World& w, const std::vector<std::string>& t) {
    const std::string& c = t[0];
    auto I = [&](size_t i) { return i < t.size() ? std::atoi(t[i].c_str()) : 0; };
    if (c == "echo") { for (size_t i = 1; i < t.size(); i++) std::printf("%s%s", i > 1 ? " " : "", t[i].c_str()); std::printf("\n"); }
    else if (c == "page") {
        w.page.w = I(1); w.page.h = I(2);
        w.page.px.assign(static_cast<size_t>(w.page.w) * w.page.h, 0);
        for (int y = 0; y < w.page.h; y++)
            for (int x = 0; x < w.page.w; x++)
                w.page.px[static_cast<size_t>(y) * w.page.w + x] =
                    t[3] == "pattern" ? static_cast<u8>((x * 7 + y * 13) % 251 + 1) : static_cast<u8>(std::atoi(t[3].c_str()));
        w.sprites.setPageSize(w.page.w, w.page.h);
        w.movies.setPageSize(w.page.w, w.page.h);
    }
    else if (c == "pal") {
        if (t[1] == "gray") for (int i = 0; i < 256; i++) w.pal.c[i] = Color{static_cast<u8>(i), static_cast<u8>(i), static_cast<u8>(i)};
        else if (t[1] == "tan") w.pal = TanFile(w.files.requireTime(t[2])).palette();
        else w.pal = Palette::load(w.files.requireData(t[2]));
        w.hasPal = true;
    }
    else if (c == "clock") w.now = static_cast<uint32_t>(std::strtoul(t[1].c_str(), nullptr, 10));
    else if (c == "adv") w.now += static_cast<uint32_t>(std::strtoul(t[1].c_str(), nullptr, 10));
    else if (c == "update") { w.sprites.update(w.now); w.movies.update(w.now); w.sounds.update(); }
    else if (c == "frame") w.frame.clear(static_cast<u8>(I(2)));
    else if (c == "paint") { w.sprites.paintPageRelative(w.page, w.pal); w.movies.paintPageRelative(w.page, w.pal); }
    else if (c == "draw") { w.sprites.drawFixed(w.frame, I(1), I(2), w.pal); w.movies.drawSynth(w.frame, I(1), I(2), w.pal); }
    else if (c == "pagechange") {
        w.sprites.freeAllOnPageChange(); w.movies.onPageChange(t.size() > 1 ? t[1] : ""); w.sounds.onPageChange();
    }
    else if (c == "bmp") { Image8 tmp; writeBmp(t[2], target(w, t[1], tmp), w.pal); }
    else if (c == "crc") {
        Image8 tmp; Image8& im = target(w, t[1], tmp);
        int x = I(2), y = I(3), cw = I(4), ch = I(5);
        std::vector<u8> r;
        for (int j = 0; j < ch; j++)
            for (int i = 0; i < cw; i++) r.push_back(x + i < im.w && y + j < im.h ? im.at(x + i, y + j) : 0);
        std::printf("crc %08x\n", static_cast<u32>(crc32(0, r.data(), static_cast<uInt>(r.size()))));
    }
    else if (c == "pix") { Image8 tmp; Image8& im = target(w, t[1], tmp); std::printf("pix %d\n", im.at(I(2), I(3))); }
    else if (c == "events") printEvents(w);
    else if (c == "avi.us") std::printf("avi.us %u\n", AviFile(w.files.requireTime(t[1])).microsPerFrame());
    // sprites
    else if (c == "sprite.pageSize") { w.sprites.setPageSize(I(1), I(2)); }
    else if (c == "sprite.open") {
        auto m = kv(t, 1);
        SpriteOpenParams p;
        p.file = gets(m, "file"); p.wav = gets(m, "wav");
        p.x = geti(m, "x", 0); p.y = geti(m, "y", 0); p.mode = geti(m, "mode", 1); p.delayMs = geti(m, "delay", 0);
        p.window = geti(m, "window", -1); p.loop = geti(m, "loop", 0); p.keepPainted = geti(m, "keep", 0);
        p.maxPlayMs = geti(m, "max", 0); p.soundMode = geti(m, "soundmode", 0); p.visibleWidth = geti(m, "vis", -1);
        p.pageRelative = geti(m, "rel", 0); p.eraseOnStop = geti(m, "erase", 0); p.autoplay = geti(m, "auto", 0);
        p.autoFree = geti(m, "free", 0);
        int h; int rc = w.sprites.open(p, h);
        std::printf("sprite.open h=%d rc=%d\n", h, rc);
    }
    else if (c == "sprite.play") std::printf("rc=%d\n", w.sprites.play(I(1), I(2) != 0, w.now, I(3) != 0));
    else if (c == "sprite.stop") std::printf("rc=%d\n", w.sprites.stop(I(1)));
    else if (c == "sprite.close") std::printf("rc=%d\n", w.sprites.close(I(1)));
    else if (c == "sprite.width") std::printf("rc=%d\n", w.sprites.setVisibleWidth(I(1), I(2)));
    else if (c == "sprite.sound") std::printf("sprite.sound %d\n", w.sprites.soundHandle(I(1)));
    else if (c == "sprite.freeall") w.sprites.freeAllOnPageChange();
    else if (c == "sprite.info")
        std::printf("sprite.info frame=%d playing=%d any=%d count=%d dirty=%d\n", w.sprites.currentFrame(I(1)),
                    w.sprites.isPlaying(I(1)), w.sprites.anyPlaying(), w.sprites.count(), w.sprites.takeDirty());
    // movies
    else if (c == "movie.open") {
        auto m = kv(t, 1);
        MovieOpenParams p;
        p.file = gets(m, "file"); p.x = geti(m, "x", 0); p.y = geti(m, "y", 0); p.loop = geti(m, "loop", 0);
        p.audio = geti(m, "audio", 1); p.window = geti(m, "window", -1);
        if (w.hasPal) { p.palette = w.pal; p.hasPalette = true; }
        int h; int rc = w.movies.open(p, h);
        std::printf("movie.open h=%d rc=%d\n", h, rc);
    }
    else if (c == "movie.play") std::printf("rc=%d\n", w.movies.play(I(1), I(2) != 0, w.now));
    else if (c == "movie.stop") std::printf("rc=%d\n", w.movies.stop(I(1)));
    else if (c == "movie.close") std::printf("rc=%d\n", w.movies.close(I(1)));
    else if (c == "movie.key") std::printf("rc=%d\n", w.movies.setColorKey(I(1), static_cast<u8>(I(2)), static_cast<u8>(I(3)), static_cast<u8>(I(4))));
    else if (c == "movie.vol") std::printf("rc=%d\n", w.movies.setVolume(I(1), I(2)));
    else if (c == "movie.bind") std::printf("rc=%d\n", w.movies.bindPage(I(1), t[2]));
    else if (c == "movie.double") std::printf("rc=%d\n", w.movies.setLineDoubling(I(1)));
    else if (c == "movie.savebg") std::printf("rc=%d\n", w.movies.setSaveBackground(I(1)));
    else if (c == "movie.synth") std::printf("rc=%d\n", w.movies.setSynth(I(1)));
    else if (c == "movie.restore") std::printf("rc=%d\n", w.movies.setRestore(I(1), I(2) != 0, I(3) != 0));
    else if (c == "movie.pause") w.movies.pause(w.now);
    else if (c == "movie.resume") w.movies.resume(w.now);
    else if (c == "movie.info")
        std::printf("movie.info frame=%d playing=%d audio=%d count=%d\n", w.movies.currentFrame(I(1)), w.movies.isPlaying(I(1)),
                    w.movies.audioPlaying(I(1)), w.movies.count());
    // sounds
    else if (c == "sound.open") {
        auto m = kv(t, 1);
        SoundOpenParams p;
        p.file = gets(m, "file"); p.loop = geti(m, "loop", 0) == 1; p.frequency = geti(m, "freq", 0);
        p.pan = geti(m, "pan", 0); p.volume = geti(m, "vol", 0); p.autoplay = geti(m, "auto", 0);
        int h; int rc = w.sounds.open(p, h);
        std::printf("sound.open h=%d rc=%d\n", h, rc);
    }
    else if (c == "sound.play") std::printf("rc=%d\n", w.sounds.play(I(1), I(2) != 0));
    else if (c == "sound.stop") std::printf("rc=%d\n", w.sounds.stop(I(1)));
    else if (c == "sound.close") std::printf("rc=%d\n", w.sounds.close(I(1)));
    else if (c == "sound.prop") std::printf("rc=%d\n", w.sounds.setProperty(I(1), t[2], I(3)));
    else if (c == "sound.playwave") std::printf("sound.playwave h=%d\n", w.sounds.playWave(t[1], I(2)));
    else if (c == "sound.pan") std::printf("rc=%d\n", w.sounds.panToPage(I(1), I(2) != 0, I(3), I(4) != 0));
    else if (c == "sound.view") w.sounds.setView(I(1), I(2));
    else if (c == "sound.pause") w.sounds.pause();
    else if (c == "sound.resume") w.sounds.resume(I(1) != 0);
    else if (c == "sound.count") std::printf("sound.count %d\n", w.sounds.count());
    else if (c == "sound.info") std::printf("sound.info valid=%d playing=%d\n", w.sounds.valid(I(1)), w.sounds.isPlaying(I(1)));
    // mixer
    else if (c == "mix") {
        int frames = static_cast<int>(std::atof(t[1].c_str()) * 44100);
        std::vector<int16_t> keep;
        int pl = 0, pr = 0;
        mixBlocks(w, frames, t.size() > 2 ? &keep : nullptr, pl, pr);
        if (t.size() > 2) writeWav(t[2], keep);
        std::printf("mix frames=%d peakL=%d peakR=%d\n", frames, pl, pr);
    }
    else if (c == "mixuntil") {
        int h = I(2), total = 0, pl = 0, pr = 0, limit = static_cast<int>(std::atof(t[3].c_str()) * 44100);
        while (total < limit) {
            bool playing = t[1] == "movie" ? w.movies.audioPlaying(h) : w.sounds.isPlaying(h);
            if (!playing) break;
            mixBlocks(w, 512, nullptr, pl, pr);
            total += 512;
        }
        std::printf("mixuntil frames=%d peakL=%d peakR=%d\n", total, pl, pr);
    }
    else throw Error("unknown command: " + c);
}

} // namespace

int main(int argc, char** argv) {
    try {
        World w;
        std::ifstream fin;
        if (argc > 1) { fin.open(argv[1]); if (!fin) throw Error("cannot open script"); }
        std::istream& in = argc > 1 ? fin : std::cin;
        std::string line;
        int rc = 0;
        while (std::getline(in, line)) {
            auto hash = line.find('#');
            if (hash != std::string::npos) line.resize(hash);
            std::istringstream ss(line);
            std::vector<std::string> t;
            std::string tok;
            while (ss >> tok) t.push_back(tok);
            if (t.empty()) continue;
            try { run(w, t); }
            catch (const Error& e) { std::printf("error: %s (%s)\n", e.what(), line.c_str()); rc = 1; }
            std::fflush(stdout);
        }
        return rc;
    } catch (const Error& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
