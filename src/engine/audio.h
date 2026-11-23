// Audio core: WAV decoding and a software mixer. Platform-independent (no SDL here);
// the output device pulls blocks via Mixer::mix().
#pragma once
#include "engine/common.h"
#include <memory>
#include <mutex>

namespace biosys {

// Decoded PCM, always interleaved int16.
struct SoundData {
    std::vector<int16_t> samples;   // frames * channels
    int sampleRate = 0;
    int channels = 0;               // 1 or 2
    int sourceBits = 16;            // bit depth of the file (8 or 16)
    size_t frames = 0;

    // Throws Error for non-PCM, unsupported or malformed files.
    static SoundData loadWav(const std::string& path);
    static SoundData parseWav(const u8* data, size_t size);
};

// Software mixer with up to 40 voices (the original engine's limit).
// All methods may be called from any thread; mix() is intended for the audio thread.
//
// Pan law: constant power. For pan p in [-1,1], angle = (p+1)*pi/4, gainL = cos, gainR = sin.
// A centred mono sound is therefore attenuated by 1/sqrt(2) (-3.01 dB) on each channel.
// Stereo sounds use a balance law instead: pan > 0 scales left by (1-p), pan < 0 scales right
// by (1+p), centre leaves both untouched.
// Gain is clamped to [0,1], pan to [-1,1].
//
// Two output paths (AudioMode). Original (default): every voice is resampled by linear interpolation at its own
// fractional position, scaled by its gain / pan, all voices are summed in float WITHOUT any intermediate clipping, and the sum
// is rounded and hard-clipped to int16 at the output (no wrap-around). Enhanced: a band-limited (Kaiser-windowed sinc,
// 32 taps for upsampling) resampler, per-voice gain smoothing of a few ms (start, stop, pause, resume, volume, pan), a short fade
// over the last milliseconds of a one-shot sound, and a soft-knee saturation (identity below 0.8 full scale) instead of a hard
// clip. Voice positions, timing of ends and takeFinished() are the same in both modes.
//
// Volume groups: master, and one per VoiceGroup (linear amplitude, percent / 100), plus a mute. At the defaults (100, not muted)
// the original path is bit-identical to a mixer without them.
enum class AudioMode { Original = 0, Enhanced = 1 };
enum class VoiceGroup { Effects = 0, Movies = 1, Terminal = 2 };

class Mixer {
public:
    static constexpr int kMaxVoices = 40;
    static constexpr int kGroups = 3;

    explicit Mixer(int outRate = 44100, int outChannels = 2);

    int outRate() const { return outRate_; }
    int outChannels() const { return outChannels_; }
    void setOutRate(int hz);   // the device's rate (set before the device starts; voices follow at once)

    void setMode(AudioMode m);
    AudioMode mode() const;

    // Volumes in percent, clamped to 0..100. Changes ramp over at most one 256-frame block.
    void setMasterVolume(int percent);
    void setGroupVolume(VoiceGroup g, int percent);
    void setMuted(bool muted);
    int  masterVolume() const;
    int  groupVolume(VoiceGroup g) const;
    bool muted() const;

    int  open(std::shared_ptr<const SoundData> sound, VoiceGroup group = VoiceGroup::Effects);   // handle 1..40, or 0 if none free
    void close(int h);
    void play(int h, bool restart);   // restart=false resumes a stopped voice from where it stopped
    void stop(int h);
    bool isPlaying(int h) const;
    void setLoop(int h, bool loop);
    void setGain(int h, float linear0to1);
    void setPan(int h, float minus1to1);
    void setRate(int h, double playbackHz);   // 0 = the sound's own rate

    // Renders `frames` frames of interleaved int16 (outChannels per frame), overwriting out.
    void mix(int16_t* out, int frames);
    // The same as float, nominally -1..1 (int16 full scale = 1). Enhanced mode: no int16 rounding; original mode: mix() / 32768.
    void mixFloat(float* out, int frames);

    // Handles of non-looping voices that ran to their end since the last call (each once).
    std::vector<int> takeFinished();

private:
    struct Voice {
        std::shared_ptr<const SoundData> snd;
        bool used = false, playing = false, loop = false;
        float gain = 1.f, pan = 0.f;
        double rate = 0.0;     // 0 = own rate
        double pos = 0.0;      // in source frames
        VoiceGroup group = VoiceGroup::Effects;
        // enhanced mode only
        float curL = 0.f, curR = 0.f;      // smoothed gains actually applied
        bool tail = false;                 // stopped, still ramping down
    };
    // enhanced mode: what a restarted or closed voice was playing, fading out
    struct Ghost {
        std::shared_ptr<const SoundData> snd;
        double pos = 0.0, rate = 0.0;
        bool loop = false, used = false;
        float l = 0.f, r = 0.f;
    };
    static constexpr int kGhosts = 16;
    void spawnGhost(const Voice& v);   // mu_ held
    Voice* get(int h);
    const Voice* get(int h) const;

    void mixOriginal(int16_t* out, int frames);                // mu_ held
    void renderEnhanced(float* out, int n);                    // mu_ held; n <= kChunk; out = n * outChannels floats, saturated
    static constexpr int kChunk = 256;
    float groupTarget(VoiceGroup g) const { return groupVol_[static_cast<int>(g)] / 100.f; }
    float masterTarget() const { return muted_ ? 0.f : masterVol_ / 100.f; }

    int outRate_, outChannels_;
    AudioMode mode_ = AudioMode::Original;
    int masterVol_ = 100, groupVol_[kGroups] = {100, 100, 100};
    bool muted_ = false, mixed_ = false;
    float masterCur_ = 1.f, groupCur_[kGroups] = {1.f, 1.f, 1.f};
    mutable std::mutex mu_;
    Voice voices_[kMaxVoices];
    Ghost ghosts_[kGhosts];
    int finished_[kMaxVoices];
    int nFinished_ = 0;
};

} // namespace biosys
