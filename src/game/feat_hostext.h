// What the story feature classes (feat_story.cpp, feats_story2.cpp) need of the shell beyond FeatureHost: the CEngineFX palette /
// window / sound calls and the player. FeatureHost (featenv.h) has no slot for these; BiosFeatureHost implements this interface
// as a second base (see feat_hostext.patch.txt). The classes reach it with `hostExt(host())`; without it (tests with no host, or a
// shell that does not implement it) the call is skipped, which is marked `PORT (host gap)` where it matters.
#pragma once
#include "game/featenv.h"

namespace biosys::game {

class Self;

class FeatureHostExt {
public:
    virtual ~FeatureHostExt() = default;
    virtual Self* player() { return nullptr; }                                 // DAT_100c1d38: the player (CSelf)
    virtual void clearWindow() {}                                              // CEngineFX::ClearWindow dll:10060df0
    virtual bool lockPalette(int token) { return false; }                      // FUN_10060ce0(&CEngineFX, token)
    virtual bool unlockPalette(int token) { return false; }                    // FUN_10060cb0(&CEngineFX, token)
    // FUN_10060170(&CEngineFX, file, xft, steps, immediate, token, unk); xft may be null
    virtual bool loadPalette(const char* file, const char* xft, int steps, int immediate, int token, int unk) { return false; }
    virtual int jumpToPage(const char* page, int p2, int p3, int p4, int p5) { return 0; }   // CEngineFX::JumpToPage dll:10060d10
    virtual bool stopSound(int handle) { return false; }                       // CEngineFX::StopSound dll:100609d0
    virtual void loopedAviClosed() {}                                          // FUN_10061120(&CEngineFX) dll:10061120
    // FUN_1001ae40 (the video message player): Eng_OpenAVIMovie(file, -1, x, y, OnMovieEvent, 0, 0, CEngineFX palette, &handle
    // = DAT_100c18f0); on success: volume when negative, option 0x4f(1, 0), colour key (0,255,255), option 0x42,
    // CEngineFX+0x580 = 0, Eng_PlayAVIMovie(handle, 0)
    virtual bool playVideoMessage(const char* movie, int x, int y, int volume) { return false; }
    virtual bool silent() const { return false; }                              // DAT_1009f83c: no sounds / images (time is being advanced)
};

inline FeatureHostExt* hostExt(FeatureHost* h) { return dynamic_cast<FeatureHostExt*>(h); }

} // namespace biosys::game
