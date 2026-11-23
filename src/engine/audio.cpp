#include "engine/audio.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace biosys {

SoundData SoundData::loadWav(const std::string& path) {
    std::vector<u8> buf = readFile(path);
    try {
        return parseWav(buf.data(), buf.size());
    } catch (const Error& e) {
        throw Error(path + ": " + e.what());
    }
}

SoundData SoundData::parseWav(const u8* data, size_t size) {
    ByteReader r(data, size);
    if (size < 12 || std::string(reinterpret_cast<const char*>(data), 4) != "RIFF" ||
        std::string(reinterpret_cast<const char*>(data) + 8, 4) != "WAVE")
        throw Error("not a RIFF/WAVE file");

    bool haveFmt = false, haveData = false;
    unsigned channels = 0, bits = 0, blockAlign = 0;
    u32 rate = 0;
    size_t dataOff = 0, dataLen = 0;

    size_t off = 12;
    while (off + 8 <= size && !haveData) {
        std::string id(reinterpret_cast<const char*>(data) + off, 4);
        size_t len = r.u32At(off + 4);
        size_t body = off + 8;
        size_t avail = size - body;
        if (id == "fmt ") {
            if (len < 16 || len > avail) throw Error("bad fmt chunk");
            if (len != 16 && len != 18 && len != 40) throw Error("unsupported fmt chunk size");
            unsigned tag = r.u16At(body);
            channels = r.u16At(body + 2);
            rate = r.u32At(body + 4);
            blockAlign = r.u16At(body + 12);
            bits = r.u16At(body + 14);
            if (tag == 0xFFFE) {
                if (len != 40) throw Error("bad WAVE_FORMAT_EXTENSIBLE chunk");
                tag = r.u16At(body + 24);   // first two bytes of the SubFormat GUID
            }
            if (tag != 1) throw Error("not PCM (format tag " + std::to_string(tag) + ")");
            haveFmt = true;
        } else if (id == "data") {
            dataOff = body;
            dataLen = std::min(len, avail);
            haveData = true;
        }
        size_t next = body + len + (len & 1);
        if (next < body || next > size) break;
        off = next;
    }
    if (!haveFmt) throw Error("missing fmt chunk");
    if (!haveData) throw Error("missing data chunk");
    if (channels < 1 || channels > 2) throw Error("unsupported channel count");
    if (bits != 8 && bits != 16) throw Error("unsupported bit depth");
    if (rate == 0 || rate > 1000000) throw Error("bad sample rate");
    size_t frameBytes = channels * (bits / 8);
    if (blockAlign != frameBytes) throw Error("inconsistent block alignment");

    SoundData s;
    s.sampleRate = static_cast<int>(rate);
    s.channels = static_cast<int>(channels);
    s.sourceBits = static_cast<int>(bits);
    s.frames = dataLen / frameBytes;
    size_t n = s.frames * channels;
    s.samples.resize(n);
    const u8* p = r.bytesAt(dataOff, n * (bits / 8));
    if (bits == 8) {
        for (size_t i = 0; i < n; i++) s.samples[i] = static_cast<int16_t>((static_cast<int>(p[i]) - 128) * 256);
    } else {
        for (size_t i = 0; i < n; i++)
            s.samples[i] = static_cast<int16_t>(static_cast<u16>(p[2 * i] | (p[2 * i + 1] << 8)));
    }
    return s;
}

Mixer::Mixer(int outRate, int outChannels)
    : outRate_(outRate > 0 ? outRate : 44100), outChannels_(outChannels == 1 ? 1 : 2) {}

void Mixer::setOutRate(int hz) {
    std::lock_guard<std::mutex> lk(mu_);
    if (hz >= 8000 && hz <= 384000) outRate_ = hz;
}

void Mixer::setMode(AudioMode m) {
    std::lock_guard<std::mutex> lk(mu_);
    mode_ = m;
}
AudioMode Mixer::mode() const {
    std::lock_guard<std::mutex> lk(mu_);
    return mode_;
}

// Before the first block was ever mixed the levels are set outright (start-up options); afterwards they ramp.
void Mixer::setMasterVolume(int p) {
    std::lock_guard<std::mutex> lk(mu_);
    masterVol_ = std::min(100, std::max(0, p));
    if (!mixed_) masterCur_ = masterTarget();
}
void Mixer::setGroupVolume(VoiceGroup g, int p) {
    std::lock_guard<std::mutex> lk(mu_);
    groupVol_[static_cast<int>(g)] = std::min(100, std::max(0, p));
    if (!mixed_) groupCur_[static_cast<int>(g)] = groupTarget(g);
}
void Mixer::setMuted(bool m) {
    std::lock_guard<std::mutex> lk(mu_);
    muted_ = m;
    if (!mixed_) masterCur_ = masterTarget();
}
int Mixer::masterVolume() const {
    std::lock_guard<std::mutex> lk(mu_);
    return masterVol_;
}
int Mixer::groupVolume(VoiceGroup g) const {
    std::lock_guard<std::mutex> lk(mu_);
    return groupVol_[static_cast<int>(g)];
}
bool Mixer::muted() const {
    std::lock_guard<std::mutex> lk(mu_);
    return muted_;
}

Mixer::Voice* Mixer::get(int h) {
    if (h < 1 || h > kMaxVoices || !voices_[h - 1].used) return nullptr;
    return &voices_[h - 1];
}
const Mixer::Voice* Mixer::get(int h) const {
    if (h < 1 || h > kMaxVoices || !voices_[h - 1].used) return nullptr;
    return &voices_[h - 1];
}

int Mixer::open(std::shared_ptr<const SoundData> sound, VoiceGroup group) {
    if (!sound || sound->channels < 1 || sound->channels > 2 || sound->sampleRate <= 0) return 0;
    std::lock_guard<std::mutex> lk(mu_);
    for (int i = 0; i < kMaxVoices; i++) {
        if (!voices_[i].used) {
            voices_[i] = Voice();
            voices_[i].snd = std::move(sound);
            voices_[i].used = true;
            voices_[i].group = group;
            return i + 1;
        }
    }
    return 0;
}

// Enhanced mode: an audible voice that is restarted or closed is handed to a ghost that fades out, so nothing jumps.
void Mixer::spawnGhost(const Voice& v) {
    if (mode_ != AudioMode::Enhanced || !v.snd || (v.curL <= 1e-4f && v.curR <= 1e-4f)) return;
    for (Ghost& g : ghosts_) {
        if (g.used) continue;
        g.snd = v.snd; g.pos = v.pos; g.rate = v.rate; g.loop = v.loop; g.l = v.curL; g.r = v.curR; g.used = true;
        return;
    }
}

void Mixer::close(int h) {
    std::shared_ptr<const SoundData> release;   // freed outside the lock
    {
        std::lock_guard<std::mutex> lk(mu_);
        Voice* v = get(h);
        if (!v) return;
        spawnGhost(*v);
        release = std::move(v->snd);
        *v = Voice();
        int j = 0;
        for (int i = 0; i < nFinished_; i++)
            if (finished_[i] != h) finished_[j++] = finished_[i];
        nFinished_ = j;
    }
}

void Mixer::play(int h, bool restart) {
    std::lock_guard<std::mutex> lk(mu_);
    Voice* v = get(h);
    if (!v) return;
    if (restart || v->pos >= static_cast<double>(v->snd->frames)) {
        if (v->pos != 0.0) spawnGhost(*v);
        if (mode_ == AudioMode::Enhanced) { v->curL = v->curR = 0.f; }
        v->pos = 0.0;
    }
    v->playing = true;
    v->tail = false;
}

void Mixer::stop(int h) {
    std::lock_guard<std::mutex> lk(mu_);
    if (Voice* v = get(h)) {
        v->tail = v->playing && mode_ == AudioMode::Enhanced && (v->curL > 0.f || v->curR > 0.f);
        v->playing = false;
    }
}

bool Mixer::isPlaying(int h) const {
    std::lock_guard<std::mutex> lk(mu_);
    const Voice* v = get(h);
    return v && v->playing;
}

void Mixer::setLoop(int h, bool loop) {
    std::lock_guard<std::mutex> lk(mu_);
    if (Voice* v = get(h)) v->loop = loop;
}

void Mixer::setGain(int h, float g) {
    std::lock_guard<std::mutex> lk(mu_);
    if (Voice* v = get(h)) v->gain = std::isnan(g) ? 0.f : std::min(1.f, std::max(0.f, g));
}

void Mixer::setPan(int h, float p) {
    std::lock_guard<std::mutex> lk(mu_);
    if (Voice* v = get(h)) v->pan = std::isnan(p) ? 0.f : std::min(1.f, std::max(-1.f, p));
}

void Mixer::setRate(int h, double hz) {
    std::lock_guard<std::mutex> lk(mu_);
    if (Voice* v = get(h)) v->rate = (hz > 0.0 && std::isfinite(hz)) ? hz : 0.0;
}

std::vector<int> Mixer::takeFinished() {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<int> r(finished_, finished_ + nFinished_);
    nFinished_ = 0;
    return r;
}

void Mixer::mix(int16_t* out, int frames) {
    std::lock_guard<std::mutex> lk(mu_);
    mixed_ = true;
    if (mode_ == AudioMode::Original) { mixOriginal(out, frames); return; }
    float tmp[kChunk * 2];
    for (int base = 0; base < frames; base += kChunk) {
        int n = std::min(kChunk, frames - base);
        renderEnhanced(tmp, n);
        for (int i = 0; i < n * outChannels_; i++) {
            long q = std::lrintf(tmp[i] * 32768.f);
            out[static_cast<size_t>(base) * outChannels_ + i] = static_cast<int16_t>(q > 32767 ? 32767 : q < -32768 ? -32768 : q);
        }
    }
}

void Mixer::mixFloat(float* out, int frames) {
    std::unique_lock<std::mutex> lk(mu_);
    mixed_ = true;
    if (mode_ == AudioMode::Enhanced) {
        for (int base = 0; base < frames; base += kChunk)
            renderEnhanced(out + static_cast<size_t>(base) * outChannels_, std::min(kChunk, frames - base));
        return;
    }
    std::vector<int16_t> tmp(static_cast<size_t>(frames) * outChannels_);
    mixOriginal(tmp.data(), frames);
    for (size_t i = 0; i < tmp.size(); i++) out[i] = tmp[i] / 32768.f;
}

// ---------------------------------------------------------------------------------------------
// Enhanced path

namespace {

// Kaiser-windowed sinc, half width kL source samples, tabulated at kOS points per sample.
struct Kernel {
    static constexpr int kL = 16, kOS = 256;
    static constexpr double kBeta = 9.0;
    std::vector<float> t;
    static double i0(double x) {
        double sum = 1, term = 1;
        for (int k = 1; k < 60; k++) { term *= (x / (2 * k)) * (x / (2 * k)); sum += term; if (term < 1e-12 * sum) break; }
        return sum;
    }
    Kernel() : t(static_cast<size_t>(kL) * kOS + 2, 0.f) {
        const double pi = 3.14159265358979323846, norm = i0(kBeta);
        for (int i = 0; i <= kL * kOS; i++) {
            double x = static_cast<double>(i) / kOS, r = x / kL;
            double sinc = x == 0 ? 1.0 : std::sin(pi * x) / (pi * x);
            double w = r >= 1.0 ? 0.0 : i0(kBeta * std::sqrt(1 - r * r)) / norm;
            t[static_cast<size_t>(i)] = static_cast<float>(sinc * w);
        }
    }
    static const Kernel& get() { static const Kernel k; return k; }
};

constexpr int kMaxTaps = 2 * Kernel::kL * 32 + 8;   // cutoff is never below 1/32 of the source Nyquist

// Band-limited value of the source at fractional position p. fc: cutoff as a fraction of the source Nyquist (1, or 1/step when
// the voice plays faster than the output rate, which keeps the aliasing out of the downsampled result).
struct Resampler {
    const SoundData& s;
    bool loop;
    float fc;
    const float* tab;
    float w[kMaxTaps];

    Resampler(const SoundData& snd, bool lp, double step) : s(snd), loop(lp), fc(step > 1.0 ? static_cast<float>(1.0 / std::max(step, 1.0)) : 1.f) {
        if (fc < 1.f / 32) fc = 1.f / 32;
        tab = Kernel::get().t.data();
    }
    void at(double p, float& l, float& r) {
        const float W = Kernel::kL / fc;
        const long n = static_cast<long>(s.frames);
        long jlo = static_cast<long>(std::floor(p - W)) + 1, jhi = static_cast<long>(std::floor(p + W));
        int cnt = static_cast<int>(jhi - jlo + 1);
        if (cnt > kMaxTaps) cnt = kMaxTaps;
        const float scale = fc * Kernel::kOS;
        for (int q = 0; q < cnt; q++) {
            float t = static_cast<float>(std::fabs(p - static_cast<double>(jlo + q))) * scale;
            int i0 = static_cast<int>(t);
            float fr = t - static_cast<float>(i0);
            w[q] = i0 >= Kernel::kL * Kernel::kOS ? 0.f : fc * (tab[i0] + (tab[i0 + 1] - tab[i0]) * fr);
        }
        const int ch = s.channels;
        float a = 0.f, b = 0.f;
        const int16_t* d = s.samples.data();
        if (jlo >= 0 && jlo + cnt <= n) {
            const int16_t* q0 = d + jlo * ch;
            if (ch == 1) { for (int q = 0; q < cnt; q++) a += w[q] * q0[q]; b = a; }
            else for (int q = 0; q < cnt; q++) { a += w[q] * q0[2 * q]; b += w[q] * q0[2 * q + 1]; }
        } else {
            for (int q = 0; q < cnt; q++) {
                long j = jlo + q;
                if (loop) { j %= n; if (j < 0) j += n; }
                else j = j < 0 ? 0 : j >= n ? n - 1 : j;     // before the start / after the end: the edge value is held
                a += w[q] * d[j * ch];
                b += w[q] * d[j * ch + (ch == 2 ? 1 : 0)];
            }
        }
        l = a; r = b;
    }
};

inline float slew(float cur, float target, float maxStep) {
    float d = target - cur;
    return d > maxStep ? cur + maxStep : d < -maxStep ? cur - maxStep : target;
}

// Identity below the knee, then a tanh shoulder that reaches 1 asymptotically (no hard corner).
inline float softSat(float x) {
    constexpr float knee = 0.8f;
    float a = std::fabs(x);
    if (a <= knee) return x;
    float y = knee + (1.f - knee) * std::tanh((a - knee) / (1.f - knee));
    return x < 0 ? -y : y;
}

} // namespace

void Mixer::renderEnhanced(float* out, int n) {
    float acc[kChunk * 2];
    std::fill(acc, acc + n * 2, 0.f);
    const double kPi = 3.14159265358979323846;
    const float ramp = 1.f / std::max(1.f, 0.003f * static_cast<float>(outRate_));   // full scale in 3 ms

    for (int vi = 0; vi < kMaxVoices; vi++) {
        Voice& v = voices_[vi];
        if (!v.used || (!v.playing && !v.tail)) continue;
        const SoundData& s = *v.snd;
        const double nframes = static_cast<double>(s.frames);
        const double step = (v.rate > 0.0 ? v.rate : s.sampleRate) / outRate_;
        const float gg = groupTarget(v.group);
        float gl, gr;
        if (s.channels == 1) {
            double a = (v.pan + 1.0) * kPi / 4.0;
            gl = v.gain * static_cast<float>(std::cos(a));
            gr = v.gain * static_cast<float>(std::sin(a));
        } else {
            gl = v.gain * (v.pan > 0.f ? 1.f - v.pan : 1.f);
            gr = v.gain * (v.pan < 0.f ? 1.f + v.pan : 1.f);
        }
        gl *= gg; gr *= gg;
        const float endLen = static_cast<float>(0.003 * s.sampleRate * (v.rate > 0.0 ? v.rate / s.sampleRate : 1.0));   // source frames in 3 ms of output time
        Resampler rs(s, v.loop, step);
        // false when the voice has finished (a playing voice reports it, a fading one just ends)
        auto inRange = [&]() {
            if (v.pos < nframes) return true;
            if (v.loop && s.frames > 0) { v.pos = std::fmod(v.pos, nframes); return true; }
            if (v.playing) {
                v.playing = false;
                bool dup = false;
                for (int k = 0; k < nFinished_; k++) dup |= finished_[k] == vi + 1;
                if (!dup) finished_[nFinished_++] = vi + 1;
            }
            v.tail = false;
            v.curL = v.curR = 0.f;
            return false;
        };
        for (int i = 0; i < n; i++) {
            if (!inRange()) break;
            const float tl = v.playing ? gl : 0.f, tr = v.playing ? gr : 0.f;
            v.curL = slew(v.curL, tl, ramp * std::max(std::max(gl, v.curL), 1e-3f));
            v.curR = slew(v.curR, tr, ramp * std::max(std::max(gr, v.curR), 1e-3f));
            float l, r;
            rs.at(v.pos, l, r);
            if (!v.loop) {
                float rem = static_cast<float>(nframes - v.pos);
                if (rem < endLen) { float f = rem / endLen; l *= f; r *= f; }
            }
            acc[2 * i] += l * v.curL;
            acc[2 * i + 1] += r * v.curR;
            v.pos += step;
            if (!v.playing && v.curL == 0.f && v.curR == 0.f) { v.tail = false; break; }
            if (!inRange()) break;
        }
    }

    for (Ghost& g : ghosts_) {
        if (!g.used) continue;
        const SoundData& s = *g.snd;
        const double nframes = static_cast<double>(s.frames);
        const double step = (g.rate > 0.0 ? g.rate : s.sampleRate) / outRate_;
        Resampler rs(s, g.loop, step);
        const float sl = ramp * std::max(g.l, 1e-3f), sr = ramp * std::max(g.r, 1e-3f);
        for (int i = 0; i < n && g.used; i++) {
            if (g.pos >= nframes) {
                if (g.loop && s.frames > 0) g.pos = std::fmod(g.pos, nframes);
                else { g.used = false; break; }
            }
            g.l = slew(g.l, 0.f, sl); g.r = slew(g.r, 0.f, sr);
            float l, r;
            rs.at(g.pos, l, r);
            acc[2 * i] += l * g.l;
            acc[2 * i + 1] += r * g.r;
            g.pos += step;
            if (g.l == 0.f && g.r == 0.f) g.used = false;
        }
        if (!g.used) g.snd.reset();
    }

    // master volume / mute: linear over the block; then saturation
    const float mt = masterTarget(), ma = masterCur_, minc = (mt - ma) / static_cast<float>(n);
    for (int i = 0; i < n; i++) {
        const float m = (ma != mt ? ma + minc * static_cast<float>(i + 1) : ma) * (1.f / 32768.f);
        float l = softSat(acc[2 * i] * m), r = softSat(acc[2 * i + 1] * m);
        if (outChannels_ == 2) { out[2 * i] = l; out[2 * i + 1] = r; }
        else out[i] = softSat((acc[2 * i] + acc[2 * i + 1]) * 0.5f * m);
    }
    masterCur_ = mt;
}

// ---------------------------------------------------------------------------------------------
// Original path (unchanged arithmetic; the group and master gains are exactly 1 at their defaults)

void Mixer::mixOriginal(int16_t* out, int frames) {
    float acc[kChunk * 2];
    const double kPi = 3.14159265358979323846;

    for (int base = 0; base < frames; base += kChunk) {
        int n = std::min(kChunk, frames - base);
        std::fill(acc, acc + n * 2, 0.f);
        float gA[kGroups], gInc[kGroups];
        for (int g = 0; g < kGroups; g++) {
            gA[g] = groupCur_[g];
            gInc[g] = (groupTarget(static_cast<VoiceGroup>(g)) - groupCur_[g]) / static_cast<float>(n);
        }

        for (int vi = 0; vi < kMaxVoices; vi++) {
            Voice& v = voices_[vi];
            if (!v.used || !v.playing) continue;
            const SoundData& s = *v.snd;
            const double nframes = static_cast<double>(s.frames);
            const double step = (v.rate > 0.0 ? v.rate : s.sampleRate) / outRate_;
            float gl, gr;
            if (s.channels == 1) {
                double a = (v.pan + 1.0) * kPi / 4.0;
                gl = v.gain * static_cast<float>(std::cos(a));
                gr = v.gain * static_cast<float>(std::sin(a));
            } else {
                gl = v.gain * (v.pan > 0.f ? 1.f - v.pan : 1.f);
                gr = v.gain * (v.pan < 0.f ? 1.f + v.pan : 1.f);
            }
            const int grp = static_cast<int>(v.group);
            const bool gRamp = gInc[grp] != 0.f;
            const float gl0 = gl, gr0 = gr;
            if (!gRamp) { gl = gl0 * gA[grp]; gr = gr0 * gA[grp]; }
            // Wraps a looping voice or ends a one-shot; false when the voice has finished.
            auto inRange = [&]() {
                if (v.pos < nframes) return true;
                if (v.loop && s.frames > 0) { v.pos = std::fmod(v.pos, nframes); return true; }
                v.playing = false;
                bool dup = false;
                for (int k = 0; k < nFinished_; k++) dup |= finished_[k] == vi + 1;
                if (!dup) finished_[nFinished_++] = vi + 1;
                return false;
            };
            for (int i = 0; i < n; i++) {
                if (!inRange()) break;
                size_t i0 = static_cast<size_t>(v.pos);
                float fr = static_cast<float>(v.pos - static_cast<double>(i0));
                size_t i1 = i0 + 1;
                if (i1 >= s.frames) i1 = v.loop ? 0 : s.frames - 1;
                float l, r;
                if (s.channels == 1) {
                    float a = s.samples[i0], b = s.samples[i1];
                    l = r = a + (b - a) * fr;
                } else {
                    float a = s.samples[2 * i0], b = s.samples[2 * i1];
                    float c = s.samples[2 * i0 + 1], d = s.samples[2 * i1 + 1];
                    l = a + (b - a) * fr;
                    r = c + (d - c) * fr;
                }
                if (gRamp) { float g = gA[grp] + gInc[grp] * static_cast<float>(i + 1); gl = gl0 * g; gr = gr0 * g; }
                acc[2 * i] += l * gl;
                acc[2 * i + 1] += r * gr;
                v.pos += step;
                if (!inRange()) break;
            }
        }

        for (int g = 0; g < kGroups; g++) groupCur_[g] = groupTarget(static_cast<VoiceGroup>(g));
        const float mt = masterTarget(), ma = masterCur_, minc = (mt - ma) / static_cast<float>(n);
        masterCur_ = mt;
        for (int i = 0; i < n; i++) {
            if (ma != 1.f || mt != 1.f) {
                const float m = ma != mt ? ma + minc * static_cast<float>(i + 1) : mt;
                acc[2 * i] *= m; acc[2 * i + 1] *= m;
            }
            auto sat = [](float x) {
                long q = std::lrintf(x);
                return static_cast<int16_t>(q > 32767 ? 32767 : q < -32768 ? -32768 : q);
            };
            if (outChannels_ == 2) {
                out[(base + i) * 2] = sat(acc[2 * i]);
                out[(base + i) * 2 + 1] = sat(acc[2 * i + 1]);
            } else {
                out[base + i] = sat((acc[2 * i] + acc[2 * i + 1]) * 0.5f);
            }
        }
    }
}

} // namespace biosys
