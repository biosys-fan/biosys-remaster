// AVI reader with an 8-bit Microsoft Video 1 (CRAM/MSVC/WHAM) decoder and PCM audio access.
#pragma once
#include <cstdio>
#include <memory>
#include <string>
#include "engine/common.h"
#include "engine/image.h"

namespace biosys {

class AviFile {
public:
    explicit AviFile(const std::string& path);   // throws Error on unsupported/corrupt files
    AviFile(const AviFile&) = delete;
    AviFile& operator=(const AviFile&) = delete;
    AviFile(AviFile&&) noexcept;
    AviFile& operator=(AviFile&&) noexcept;
    ~AviFile();

    // --- video ---
    int frameCount() const;            // number of video chunks in 'movi'
    int width() const;
    int height() const;
    u32 microsPerFrame() const;
    double fps() const;
    // Decode the next video chunk into frame(); false when no frames remain.
    // palette() is updated by any palette-change chunks preceding that frame.
    bool nextFrame();
    int framesDecoded() const;         // frames decoded since open/rewind
    const Image8& frame() const;       // top-down, persistent between frames
    const Palette& palette() const;
    void rewind();                     // back to before frame 0; palette reset to the initial one
    const Palette& initialPalette() const;

    // --- audio (unsigned PCM, interleaved as stored) ---
    bool hasAudio() const;
    int audioSampleRate() const;
    int audioChannels() const;
    int audioBits() const;
    u32 audioSampleCount() const;      // total sample frames (bytes / (channels*bits/8))
    // PCM of all audio chunks stored since the previous call (or since open/rewind), up to and
    // including those interleaved before the video chunk of the current frame.
    // Before any frame is decoded this returns the audio preceding the first video chunk.
    std::vector<u8> takeAudio();
    // All audio of the file at once (independent of takeAudio's cursor).
    std::vector<u8> readAllAudio();

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace biosys
