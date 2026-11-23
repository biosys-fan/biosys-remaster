// biosys-audioenh: the enhanced sound path and the volume groups, headless.
//   render OUT.f32 (--wav FILE | --avi FILE) [--mode original|enhanced] [--rate HZ] [--secs S] [--loop] [--gain G] [--pan P]
//          [--src-rate HZ] [--group effects|movies|terminal] [--vol master=N,effects=N,movies=N,terminal=N]
//          [--ev "T:cmd,T:cmd,..."]    commands: play restart stop resume gain=G pan=P master=N effects=N movies=N terminal=N mute unmute
//        OUT.f32: interleaved stereo float32, little endian, int16 full scale = 1 (what Mixer::mixFloat returns). --int16: the
//        same through Mixer::mix() as interleaved int16 (OUT.i16).
//        Prints "frames=N rate=R srcframes=N srcrate=R".
//   bench             CPU seconds per second of audio, both modes
//   selftest          volume groups, mute, key translation, original-mode identity of the defaults
#include "app/windowlogic.h"
#include "engine/audio.h"
#include "engine/avi.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

using namespace biosys;

static VoiceGroup parseGroup(const std::string& g) {
    if (g == "effects") return VoiceGroup::Effects;
    if (g == "movies") return VoiceGroup::Movies;
    if (g == "terminal") return VoiceGroup::Terminal;
    throw Error("bad group " + g);
}

struct Ev { double t; std::string cmd; };

static void applyCmd(Mixer& m, int h, const std::string& c) {
    auto eq = c.find('=');
    std::string k = c.substr(0, eq), v = eq == std::string::npos ? "" : c.substr(eq + 1);
    if (k == "play") m.play(h, false);
    else if (k == "restart") m.play(h, true);
    else if (k == "resume") m.play(h, false);
    else if (k == "stop") m.stop(h);
    else if (k == "gain") m.setGain(h, static_cast<float>(std::atof(v.c_str())));
    else if (k == "pan") m.setPan(h, static_cast<float>(std::atof(v.c_str())));
    else if (k == "rate") m.setRate(h, std::atof(v.c_str()));
    else if (k == "master") m.setMasterVolume(std::atoi(v.c_str()));
    else if (k == "effects") m.setGroupVolume(VoiceGroup::Effects, std::atoi(v.c_str()));
    else if (k == "movies") m.setGroupVolume(VoiceGroup::Movies, std::atoi(v.c_str()));
    else if (k == "terminal") m.setGroupVolume(VoiceGroup::Terminal, std::atoi(v.c_str()));
    else if (k == "mute") m.setMuted(true);
    else if (k == "unmute") m.setMuted(false);
    else throw Error("bad event command " + c);
}

static void setVolumes(Mixer& m, const std::string& spec) {
    std::stringstream ss(spec);
    std::string it;
    while (std::getline(ss, it, ',')) if (!it.empty()) applyCmd(m, 0, it);
}

static int cmdRender(int argc, char** argv) {
    if (argc < 3) return 2;
    std::string out = argv[0];
    std::string wav, avi, mode = "original", group = "effects", vols, evs;
    int rate = 44100;
    double secs = 0, gain = 1, pan = -1, srcRate = 0;
    bool loop = false, i16 = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { if (i + 1 >= argc) throw Error("missing value for " + a); return std::string(argv[++i]); };
        if (a == "--wav") wav = next();
        else if (a == "--avi") avi = next();
        else if (a == "--mode") mode = next();
        else if (a == "--rate") rate = std::atoi(next().c_str());
        else if (a == "--secs") secs = std::atof(next().c_str());
        else if (a == "--loop") loop = true;
        else if (a == "--gain") gain = std::atof(next().c_str());
        else if (a == "--pan") pan = std::atof(next().c_str());
        else if (a == "--src-rate") srcRate = std::atof(next().c_str());
        else if (a == "--group") group = next();
        else if (a == "--vol") vols = next();
        else if (a == "--ev") evs = next();
        else if (a == "--int16") i16 = true;
        else throw Error("bad option " + a);
    }
    std::shared_ptr<SoundData> snd = std::make_shared<SoundData>();
    if (!wav.empty()) *snd = SoundData::loadWav(wav);
    else if (!avi.empty()) {   // as MovieSystem::open builds it
        AviFile f(avi);
        if (!f.hasAudio()) throw Error("no audio in " + avi);
        std::vector<u8> pcm = f.readAllAudio();
        snd->sampleRate = f.audioSampleRate(); snd->channels = f.audioChannels(); snd->sourceBits = f.audioBits();
        size_t bps = static_cast<size_t>(f.audioBits() / 8), n = pcm.size() / bps;
        n -= n % snd->channels;
        snd->frames = n / snd->channels;
        snd->samples.resize(n);
        for (size_t i = 0; i < n; i++)
            snd->samples[i] = bps == 1 ? static_cast<int16_t>((static_cast<int>(pcm[i]) - 128) * 256)
                                       : static_cast<int16_t>(pcm[2 * i] | (pcm[2 * i + 1] << 8));
    } else throw Error("--wav or --avi needed");

    Mixer m(rate, 2);
    m.setMode(mode == "enhanced" ? AudioMode::Enhanced : AudioMode::Original);
    int h = m.open(snd, parseGroup(group));
    m.setGain(h, static_cast<float>(gain));
    m.setPan(h, static_cast<float>(pan));
    m.setLoop(h, loop);
    if (srcRate > 0) m.setRate(h, srcRate);
    if (!vols.empty()) setVolumes(m, vols);

    std::vector<Ev> events;
    {
        std::stringstream ss(evs);
        std::string it;
        while (std::getline(ss, it, ',')) {
            auto c = it.find(':');
            if (c == std::string::npos) continue;
            events.push_back({std::atof(it.substr(0, c).c_str()), it.substr(c + 1)});
        }
    }
    double dur = secs > 0 ? secs : static_cast<double>(snd->frames) / (srcRate > 0 ? srcRate : snd->sampleRate) + 0.05;
    size_t total = static_cast<size_t>(dur * rate);
    std::vector<float> fo(total * 2);
    std::vector<int16_t> io(i16 ? total * 2 : 0);
    size_t pos = 0, ei = 0;
    bool started = false;
    // events at exact frames; "play" at time 0 is implied when no event starts the voice
    for (const Ev& e : events) if (e.cmd == "play" || e.cmd == "restart") started = true;
    if (!started) m.play(h, true);
    while (pos < total) {
        size_t next = total;
        while (ei < events.size() && static_cast<size_t>(events[ei].t * rate) <= pos) applyCmd(m, h, events[ei++].cmd);
        if (ei < events.size()) next = std::min(next, static_cast<size_t>(events[ei].t * rate));
        size_t n = next - pos;
        if (i16) m.mix(io.data() + pos * 2, static_cast<int>(n));
        else m.mixFloat(fo.data() + pos * 2, static_cast<int>(n));
        pos = next;
    }
    std::ofstream f(out, std::ios::binary);
    if (!f) throw Error("cannot write " + out);
    if (i16) f.write(reinterpret_cast<const char*>(io.data()), static_cast<std::streamsize>(io.size() * 2));
    else f.write(reinterpret_cast<const char*>(fo.data()), static_cast<std::streamsize>(fo.size() * 4));
    std::printf("frames=%zu rate=%d srcframes=%zu srcrate=%d\n", total, rate, snd->frames, snd->sampleRate);
    return 0;
}

static std::shared_ptr<SoundData> constSound(int16_t v, size_t frames, int rate = 44100) {
    auto s = std::make_shared<SoundData>();
    s->sampleRate = rate; s->channels = 1; s->frames = frames; s->samples.assign(frames, v);
    return s;
}

static int failed = 0;
static void check(const std::string& name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
    if (!ok) failed++;
}

// the left channel of a hard-left voice: gain 1 exactly (cos 0)
static int16_t settled(Mixer& m, int blocks = 4) {
    int16_t o[2 * 256];
    for (int i = 0; i < blocks; i++) m.mix(o, 256);
    return o[2 * 255];
}

static int cmdSelftest() {
    const int16_t V = 10000;
    for (int mode = 0; mode < 2; mode++) {
        const std::string tag = mode ? "enhanced: " : "original: ";
        auto mk = [&](VoiceGroup g, int* hOut = nullptr) {
            auto m = std::make_unique<Mixer>(44100, 2);
            m->setMode(mode ? AudioMode::Enhanced : AudioMode::Original);
            int h = m->open(constSound(V, 200000), g);
            m->setPan(h, -1.f); m->setLoop(h, true); m->play(h, true);
            if (hOut) *hOut = h;
            return m;
        };
        {   // defaults leave every group at full level (the first block of an enhanced voice ramps in: look at the settled value)
            for (VoiceGroup g : {VoiceGroup::Effects, VoiceGroup::Movies, VoiceGroup::Terminal})
                check(tag + "defaults: group " + std::to_string(static_cast<int>(g)) + " settles at the source level", settled(*mk(g)) == V);
        }
        {   // each group knob scales exactly its own voices
            const VoiceGroup all[3] = {VoiceGroup::Effects, VoiceGroup::Movies, VoiceGroup::Terminal};
            for (int k = 0; k < 3; k++) {
                bool ok = true;
                for (int j = 0; j < 3; j++) {
                    auto m = mk(all[j]);
                    m->setGroupVolume(all[k], 50);
                    int16_t got = settled(*m, 6);
                    ok &= got == (j == k ? V / 2 : V);
                }
                check(tag + "group " + std::to_string(k) + " at 50 halves exactly its own voices and no others", ok);
            }
        }
        {
            auto m = mk(VoiceGroup::Movies);
            m->setMasterVolume(50); m->setGroupVolume(VoiceGroup::Movies, 50);
            check(tag + "master 50 x movies 50 = quarter", settled(*m, 6) == V / 4);
            m->setGroupVolume(VoiceGroup::Movies, 0);
            check(tag + "group 0 is silent", settled(*m, 6) == 0);
        }
        {
            auto m = mk(VoiceGroup::Effects);
            check(tag + "master volume reads back", m->masterVolume() == 100 && !m->muted());
            m->setMasterVolume(250); check(tag + "master clamps to 100", m->masterVolume() == 100);
            m->setMasterVolume(-3); check(tag + "master clamps to 0", m->masterVolume() == 0);
            m->setMasterVolume(100);
            m->setMuted(true);
            check(tag + "mute: silent after the ramp", settled(*m, 3) == 0);
            check(tag + "mute keeps the master level", m->masterVolume() == 100 && m->muted());
            m->setMuted(false);
            check(tag + "unmute restores", settled(*m, 3) == V);
        }
        {   // the change ramps: no sample-to-sample step bigger than the whole change spread over the block
            auto m = mk(VoiceGroup::Effects);
            settled(*m, 2);
            m->setMuted(true);
            int16_t o[2 * 256];
            m->mix(o, 256);
            int maxStep = 0;
            for (int i = 1; i < 256; i++) maxStep = std::max(maxStep, std::abs(o[2 * i] - o[2 * i - 2]));
            check(tag + "mute ramps over the block (max step " + std::to_string(maxStep) + " of " + std::to_string(V) + ")", maxStep <= V / 200);
        }
        {   // mixed voices of different groups are scaled independently
            Mixer m(44100, 2);
            m.setMode(mode ? AudioMode::Enhanced : AudioMode::Original);
            int a = m.open(constSound(1000, 200000), VoiceGroup::Effects), b = m.open(constSound(2000, 200000), VoiceGroup::Movies),
                c = m.open(constSound(4000, 200000), VoiceGroup::Terminal);
            for (int h : {a, b, c}) { m.setPan(h, -1.f); m.setLoop(h, true); m.play(h, true); }
            m.setGroupVolume(VoiceGroup::Effects, 0); m.setGroupVolume(VoiceGroup::Movies, 50);
            check(tag + "three groups at once: 0 x 1000 + 0.5 x 2000 + 1 x 4000", settled(m, 6) == 5000);
        }
    }
    // the original path with all-100 volumes is the arithmetic of a mixer without volume groups: a few exact values
    {
        Mixer m(44100, 2);
        auto s = std::make_shared<SoundData>();
        s->sampleRate = 22050; s->channels = 1; s->frames = 500; s->samples.resize(500);
        for (size_t i = 0; i < 500; i++) s->samples[i] = static_cast<int16_t>(i * 10);
        int h = m.open(s); m.setPan(h, -1.f); m.play(h, true);
        int16_t o[16]; m.mix(o, 8);
        check("original: linear interpolation values unchanged", o[0] == 0 && o[2] == 5 && o[4] == 10 && o[6] == 15);
    }
    // enhanced saturation: below 0.8 full scale the path is the identity, above it never wraps and stays below full scale
    {
        Mixer m(44100, 2);
        m.setMode(AudioMode::Enhanced);
        std::vector<int> hs;
        for (int i = 0; i < 5; i++) { int h = m.open(constSound(20000, 200000)); m.setPan(h, -1.f); m.setLoop(h, true); m.play(h, true); hs.push_back(h); }
        int16_t o[2 * 256];
        for (int i = 0; i < 6; i++) m.mix(o, 256);
        // 5 x 20000 = 100000 -> 3x full scale
        check("enhanced: a sum of 3x full scale saturates to the top, no wrap", o[2 * 255] >= 32700 && o[2 * 255] <= 32767);
        Mixer m2(44100, 2);
        m2.setMode(AudioMode::Enhanced);
        int h = m2.open(constSound(-25000, 200000)); m2.setPan(h, -1.f); m2.setLoop(h, true); m2.play(h, true);
        for (int i = 0; i < 6; i++) m2.mix(o, 256);
        check("enhanced: -25000 (0.76 full scale) passes unchanged", o[2 * 255] == -25000);
    }
    // key translation
    {
        check("keys: minus / keypad minus are Down", volumeKeyOf(SDLK_MINUS, false) == VolumeKey::Down && volumeKeyOf(SDLK_KP_MINUS, false) == VolumeKey::Down);
        check("keys: equals / plus / keypad plus are Up", volumeKeyOf(SDLK_EQUALS, false) == VolumeKey::Up && volumeKeyOf(SDLK_PLUS, false) == VolumeKey::Up &&
              volumeKeyOf(SDLK_KP_PLUS, false) == VolumeKey::Up);
        check("keys: backslash is Mute", volumeKeyOf(SDLK_BACKSLASH, false) == VolumeKey::Mute);
        check("keys: nothing fires while text entry / a menu is up", volumeKeyOf(SDLK_MINUS, true) == VolumeKey::None && volumeKeyOf(SDLK_EQUALS, true) == VolumeKey::None &&
              volumeKeyOf(SDLK_BACKSLASH, true) == VolumeKey::None);
        bool free = true;
        for (SDL_Keycode k : {SDLK_MINUS, SDLK_KP_MINUS, SDLK_EQUALS, SDLK_PLUS, SDLK_KP_PLUS, SDLK_BACKSLASH})
            free &= vkOfSdl(k) == 0 && !StatusHold::isHoldKey(k);
        check("keys: none of them is a key the game or the status hold uses (vkOfSdl = 0)", free);
        bool none = true;
        for (SDL_Keycode k = SDLK_A; k <= SDLK_Z; k++) none &= volumeKeyOf(k, false) == VolumeKey::None;
        for (SDL_Keycode k = SDLK_0; k <= SDLK_9; k++) none &= volumeKeyOf(k, false) == VolumeKey::None;
        for (SDL_Keycode k : {SDLK_SPACE, SDLK_RETURN, SDLK_ESCAPE, SDLK_BACKSPACE, SDLK_TAB, SDLK_LEFT, SDLK_RIGHT, SDLK_UP, SDLK_DOWN, SDLK_GRAVE, SDLK_CAPSLOCK,
                              SDLK_LEFTBRACKET, SDLK_RIGHTBRACKET, SDLK_F1, SDLK_F12})
            none &= volumeKeyOf(k, false) == VolumeKey::None;
        check("keys: letters, digits, arrows, Enter, Esc, F-keys, brackets, backquote are not volume keys", none);
        check("keys: step is 10, clamped to 0..100", stepVolume(100, VolumeKey::Up) == 100 && stepVolume(100, VolumeKey::Down) == 90 && stepVolume(5, VolumeKey::Down) == 0 &&
              stepVolume(0, VolumeKey::Down) == 0 && stepVolume(95, VolumeKey::Up) == 100 && stepVolume(50, VolumeKey::Mute) == 50);
    }
    std::printf(failed ? "AUDIOENH SELFTEST FAILED (%d)\n" : "AUDIOENH SELFTEST OK\n", failed);
    return failed ? 1 : 0;
}

static int cmdBench() {
    auto s = std::make_shared<SoundData>();
    s->sampleRate = 22050; s->channels = 1; s->frames = 44100; s->samples.resize(s->frames);
    for (size_t i = 0; i < s->frames; i++) s->samples[i] = static_cast<int16_t>(8000 * std::sin(i * 0.05) + 3000 * std::sin(i * 0.31));
    for (int mode = 0; mode < 2; mode++) {
        for (int voices : {1, 8, 40}) {
            Mixer m(48000, 2);
            m.setMode(mode ? AudioMode::Enhanced : AudioMode::Original);
            for (int i = 0; i < voices; i++) { int h = m.open(s); m.setLoop(h, true); m.setPan(h, (i % 5 - 2) * 0.4f); m.setGain(h, 0.3f); m.play(h, true); }
            std::vector<float> o(1024 * 2);
            const int blocks = 48000 * 10 / 1024;
            auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < blocks; i++) m.mixFloat(o.data(), 1024);
            double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            double audio = blocks * 1024.0 / 48000;
            std::printf("bench %s voices=%d cpu_per_audio_second=%.5f\n", mode ? "enhanced" : "original", voices, sec / audio);
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    try {
        if (argc >= 2 && std::string(argv[1]) == "selftest") return cmdSelftest();
        if (argc >= 2 && std::string(argv[1]) == "bench") return cmdBench();
        if (argc >= 3 && std::string(argv[1]) == "render") {
            int r = cmdRender(argc - 2, argv + 2);
            if (r != 2) return r;
        }
    } catch (const Error& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    std::fprintf(stderr, "usage: biosys-audioenh render|bench|selftest ...\n");
    return 2;
}
