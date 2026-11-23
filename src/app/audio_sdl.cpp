#include "app/audio_sdl.h"
#include <SDL3/SDL.h>
#include <vector>
#include "engine/common.h"

namespace biosys {

SdlAudio::SdlAudio(Mixer& mixer) : mixer_(mixer) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) throw Error(std::string("SDL audio init: ") + SDL_GetError());
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = 44100;
    if (mixer.mode() == AudioMode::Enhanced) {   // float samples at the device's own rate: SDL does no conversion of its own
        SDL_AudioSpec dev{};
        if (SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &dev, nullptr) && dev.freq >= 8000 && dev.freq <= 384000)
            spec.freq = dev.freq;
        spec.format = SDL_AUDIO_F32;
        mixer.setOutRate(spec.freq);
        float32_ = true;
    } else {
        mixer.setOutRate(spec.freq);   // the original path mixes at 44100 Hz (the audio mode can change while the program runs)
    }
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &SdlAudio::callback, this);
    if (!stream_) {
        std::string msg = SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        throw Error("SDL audio device: " + msg);
    }
    SDL_ResumeAudioStreamDevice(stream_);
}

SdlAudio::~SdlAudio() {
    if (stream_) SDL_DestroyAudioStream(stream_);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SdlAudio::pause(bool paused) {
    if (!stream_) return;
    if (paused) SDL_PauseAudioStreamDevice(stream_);
    else SDL_ResumeAudioStreamDevice(stream_);
}

void SdlAudio::callback(void* user, SDL_AudioStream* stream, int additional, int total) {
    auto* self = static_cast<SdlAudio*>(user);
    const int bytesPerFrame = self->float32_ ? 8 : 4;   // stereo F32 or S16
    std::vector<int16_t> buf(1024 * 2);
    std::vector<float> fbuf(self->float32_ ? 1024 * 2 : 0);
    int frames = additional / bytesPerFrame;
    while (frames > 0) {
        int n = frames < 1024 ? frames : 1024;
        if (self->float32_) {
            self->mixer_.mixFloat(fbuf.data(), n);
            SDL_PutAudioStreamData(stream, fbuf.data(), n * bytesPerFrame);
        } else {
            self->mixer_.mix(buf.data(), n);
            SDL_PutAudioStreamData(stream, buf.data(), n * bytesPerFrame);
        }
        frames -= n;
    }
}

} // namespace biosys
