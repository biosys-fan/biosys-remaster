// SDL3 playback of a Mixer. This and audio_sdl.cpp are the only media files that include SDL.
#pragma once
#include "engine/audio.h"

struct SDL_AudioStream;

namespace biosys {

// Opens the default playback device as 44100 Hz stereo S16 and pulls from the Mixer in SDL's
// audio thread (SDL_OpenAudioDeviceStream get-callback). Latency is the device buffer SDL chooses
// (typically 512-1024 frames, 12-25 ms) plus the callback chunk (<= one SDL request, mixed in
// blocks of 1024 frames at most). The Mixer is thread-safe, so game-thread calls are fine.
// Enhanced mode (Mixer::mode()): the device is opened as 32-bit float at its own rate (the mixer is told, Mixer::setOutRate) and
// the mixer's float output goes straight to it. NOT tested on a real device.
class SdlAudio {
public:
    explicit SdlAudio(Mixer& mixer);   // throws Error if the device cannot be opened
    ~SdlAudio();
    SdlAudio(const SdlAudio&) = delete;
    SdlAudio& operator=(const SdlAudio&) = delete;

    void pause(bool paused);

private:
    static void callback(void* user, SDL_AudioStream* stream, int additional, int total);
    Mixer& mixer_;
    bool float32_ = false;
    SDL_AudioStream* stream_ = nullptr;
};

} // namespace biosys
