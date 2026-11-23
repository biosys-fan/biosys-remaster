// biosys-audiotest: mixer unit checks. Prints PASS/FAIL per check; exit 1 if any failed.
#include "engine/audio.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>

using namespace biosys;

static int failures = 0;
static void check(const char* name, bool ok, const char* detail = "") {
    std::printf("%s: %s %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok) failures++;
}

static std::shared_ptr<SoundData> make(int rate, int ch, size_t frames, std::function<int16_t(size_t, int)> f) {
    auto s = std::make_shared<SoundData>();
    s->sampleRate = rate; s->channels = ch; s->frames = frames; s->sourceBits = 16;
    s->samples.resize(frames * ch);
    for (size_t i = 0; i < frames; i++)
        for (int c = 0; c < ch; c++) s->samples[i * ch + c] = f(i, c);
    return s;
}

static std::vector<int16_t> render(Mixer& m, int frames) {
    std::vector<int16_t> o(static_cast<size_t>(frames) * m.outChannels());
    m.mix(o.data(), frames);
    return o;
}

int main() {
    const int F = 1000;
    auto full = make(44100, 1, F, [](size_t, int) { return int16_t(32767); });

    {   // silence in -> silence out
        Mixer m;
        auto o = render(m, 512);
        bool z = true; for (auto v : o) z &= v == 0;
        check("silence_idle_mixer", z);
        auto sil = make(44100, 1, F, [](size_t, int) { return int16_t(0); });
        int h = m.open(sil); m.play(h, true);
        o = render(m, 512);
        z = true; for (auto v : o) z &= v == 0;
        check("silence_in_silence_out", z);
    }
    {   // centre pan: constant power, -3.01 dB => 0.70711 per channel
        Mixer m; int h = m.open(full); m.play(h, true);
        auto o = render(m, 100);
        bool same = true; for (int i = 0; i < 100; i++) same &= o[2*i] == o[2*i+1];
        int expect = static_cast<int>(std::lrint(32767 * std::sqrt(0.5)));
        check("mono_center_identical_channels", same);
        char d[64]; std::snprintf(d, sizeof d, "(got %d, expected %d)", o[0], expect);
        check("mono_center_attenuation_0.7071", std::abs(o[0] - expect) <= 1, d);
    }
    {   // gain 0.5 halves amplitude
        Mixer a, b;
        int ha = a.open(full), hb = b.open(full);
        a.play(ha, true); b.play(hb, true); b.setGain(hb, 0.5f);
        auto oa = render(a, 100), ob = render(b, 100);
        bool ok = true; for (size_t i = 0; i < oa.size(); i++) ok &= std::abs(ob[i] - oa[i] / 2.0) <= 1.0;
        check("gain_half", ok);
    }
    {   // pan -1: right silent, left full scale; pan +1 mirrored
        Mixer m; int h = m.open(full); m.setPan(h, -1.f); m.play(h, true);
        auto o = render(m, 100);
        bool ok = true; for (int i = 0; i < 100; i++) ok &= o[2*i] == 32767 && o[2*i+1] == 0;
        check("pan_left_right_silent", ok);
        Mixer m2; int h2 = m2.open(full); m2.setPan(h2, 1.f); m2.play(h2, true);
        o = render(m2, 100);
        ok = true; for (int i = 0; i < 100; i++) ok &= o[2*i] == 0 && o[2*i+1] == 32767;
        check("pan_right_left_silent", ok);
    }
    {   // stereo balance
        auto st = make(44100, 2, F, [](size_t, int c) { return int16_t(c ? -20000 : 10000); });
        Mixer m; int h = m.open(st); m.play(h, true);
        auto o = render(m, 10);
        check("stereo_centre_untouched", o[0] == 10000 && o[1] == -20000);
        m.setPan(h, 1.f); m.play(h, true);
        o = render(m, 10);
        check("stereo_pan_right_mutes_left", o[0] == 0 && o[1] == -20000);
    }
    {   // resampling: 22050 Hz sound through 44100 Hz mixer has twice the frames
        auto s = make(22050, 1, 500, [](size_t i, int) { return int16_t(i * 10); });
        Mixer m(44100, 2); int h = m.open(s); m.play(h, true);
        int n = 0; int16_t blk[2];
        while (m.isPlaying(h) && n < 100000) { m.mix(blk, 1); n++; }
        char d[64]; std::snprintf(d, sizeof d, "(%d frames)", n);
        check("resample_22050_to_44100_doubles_frames", n == 1000, d);
        // linear interpolation: odd output frames are midpoints
        Mixer m2; int h2 = m2.open(s); m2.setPan(h2, -1.f); m2.play(h2, true);
        auto o = render(m2, 8);
        check("resample_linear_interpolation", o[0] == 0 && o[2] == 5 && o[4] == 10 && o[6] == 15);
        // setRate: own rate at 0, doubled rate halves length
        Mixer m3; int h3 = m3.open(s); m3.setRate(h3, 44100); m3.play(h3, true);
        n = 0; while (m3.isPlaying(h3) && n < 100000) { m3.mix(blk, 1); n++; }
        check("setRate_double_halves_length", n == 500);
    }
    {   // loop wraps without a gap
        auto ramp = make(44100, 1, 100, [](size_t i, int) { return int16_t(i * 100); });
        Mixer m; int h = m.open(ramp); m.setPan(h, -1.f); m.setLoop(h, true); m.play(h, true);
        auto o = render(m, 1000);
        bool ok = true; for (int i = 0; i < 1000; i++) ok &= o[2*i] == (i % 100) * 100;
        check("loop_wraps_without_gap", ok && m.isPlaying(h));
        auto o2 = render(m, 130);   // block boundary mid-loop
        ok = true; for (int i = 0; i < 130; i++) ok &= o2[2*i] == (i % 100) * 100;
        check("loop_continues_across_blocks", ok);
        check("loop_never_finishes", m.takeFinished().empty());
    }
    {   // saturation
        Mixer m;
        int a = m.open(full), b = m.open(full), c = m.open(full);
        for (int h : {a, b, c}) { m.setPan(h, -1.f); m.play(h, true); }
        auto o = render(m, 100);
        bool ok = true; for (int i = 0; i < 100; i++) ok &= o[2*i] == 32767;
        auto neg = make(44100, 1, F, [](size_t, int) { return int16_t(-32768); });
        Mixer m2;
        int x = m2.open(neg), y = m2.open(neg);
        for (int h : {x, y}) { m2.setPan(h, -1.f); m2.play(h, true); }
        o = render(m2, 100);
        for (int i = 0; i < 100; i++) ok &= o[2*i] == -32768;
        check("saturates_not_wraps", ok);
    }
    {   // finished reporting
        auto s = make(44100, 1, 100, [](size_t, int) { return int16_t(1000); });
        Mixer m; int h = m.open(s); m.play(h, true);
        render(m, 50);
        check("playing_mid_sound", m.isPlaying(h) && m.takeFinished().empty());
        render(m, 100);
        check("stops_at_end", !m.isPlaying(h));
        auto f = m.takeFinished();
        check("takeFinished_reports_once", f.size() == 1 && f[0] == h && m.takeFinished().empty());
        m.play(h, false);   // finished voice replays from the start
        render(m, 200);
        f = m.takeFinished();
        check("takeFinished_after_replay", f.size() == 1 && f[0] == h);
        m.play(h, true); m.stop(h); render(m, 200);
        check("stopped_voice_not_reported", m.takeFinished().empty());
        auto o = render(m, 10);
        check("stopped_voice_silent", o[0] == 0);
        m.play(h, false); o = render(m, 10);   // resumes
        check("play_without_restart_resumes", o[0] != 0);
    }
    {   // handles
        Mixer m; auto s = make(44100, 1, 10, [](size_t, int) { return int16_t(1); });
        int hs[Mixer::kMaxVoices]; bool ok = true;
        for (int i = 0; i < Mixer::kMaxVoices; i++) { hs[i] = m.open(s); ok &= hs[i] == i + 1; }
        check("open_40_voices", ok);
        check("open_41st_fails", m.open(s) == 0);
        m.close(hs[7]);
        check("close_frees_slot", m.open(s) == hs[7]);
        m.play(0, true); m.play(99, true); m.setGain(-1, 1.f); m.close(0);
        check("invalid_handles_ignored", !m.isPlaying(0) && !m.isPlaying(99));
        check("open_null_fails", m.open(nullptr) == 0);
    }
    {   // concurrent control + mix (run under no sanitizer; checks no crash/deadlock)
        auto s = make(22050, 2, 5000, [](size_t i, int) { return int16_t(i); });
        Mixer m; bool done = false;
        std::thread t([&] { std::vector<int16_t> o(512 * 2); for (int i = 0; i < 2000; i++) m.mix(o.data(), 512); done = true; });
        for (int i = 0; i < 20000; i++) {
            int h = m.open(s); if (h) { m.setLoop(h, i & 1); m.play(h, true); m.setPan(h, 0.3f); m.takeFinished(); m.close(h); }
        }
        t.join();
        check("threaded_control_and_mix", done);
    }
    std::printf("%s\n", failures ? "AUDIOTEST FAILED" : "AUDIOTEST OK");
    return failures ? 1 : 0;
}
