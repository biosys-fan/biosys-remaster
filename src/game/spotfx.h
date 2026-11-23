// The "spot FX" family: page/site effects that play one animation, movie or wav (saved table `fx`).
// Inheritance, from the constructors (vtables read from the DLL image):
//   CObject 10094764 -> CSpotFX 1008f1f8 (size 0x6c+; name from its dump strings "SpotFX/SpotAnim/SpotAvi/SpotWav")
//     -> CAnimFX      10093db0  (size 0xec)   dll:100706b0
//          -> CSynthAnimFX 10090548 (size 0xf8)   dll:10015ce0
//     -> CMovieFX     1008f240  (size 0xec)   dll:10002eb0
//     -> CSoundSiteFX 10090888  (size 0x80)   dll:10017870
// All five share one Save/Load pair (vtable slots +0x3c/+0x40 = dll:1000aa80 / 1000aaa0): the state dword +0x6c.
#pragma once
#include "game/world.h"

namespace biosys::game {

// CSpotFX +0x68 flag bits. Named from how the FX code tests them (CSpotFX::CanStart dll:1000a5b0, the Start/Stop
// methods dll:10003110, 100707e0, 100179c0, ...). The three type bits are or-ed in by the constructors.
namespace SpotFxFlag {
enum : uint32_t {
    Movie = 0x1,                    // CMovieFX (constructor ors it in)
    Anim = 0x2,                     // CAnimFX / CSynthAnimFX
    Sound = 0x4,                    // CSoundSiteFX
    LinkedObject = 0x10,            // only plays while the game object named like the file stem has byte +0x48 == 2;
                                    // stop/start also toggle hotspot "Y<stem>" (or "P<stem>"); never starts while
                                    // DAT_100c2a14 is set unless Looped
    RerollParam = 0x20,             // +0x7c is replaced by a computed value at every start (anim delay / sound param)
    Throttled = 0x40,               // movies: at most one start per 10800 game seconds (DAT_1009f548)
    Looped = 0x80,                  // loop the anim / movie / wav
    AviOption41 = 0x100,            // movie: Eng_Api41_AVIOption after opening
    SynthAvi = 0x200,               // movie: Eng_PrepareSynthAVIMovie after opening
    NotByDay = 0x1000,              // refuse in diurnal state 0xf morning, 0x14 day, 0x1e evening (CTimeline +0x2c)
    NotByNight = 0x2000,            // refuse in diurnal state 0x23 dusk, 0x28 night, 5 dawn
    LightsOn = 0x4000,              // only while the lights are on (DAT_100c3224 != 0)
    LightsOff = 0x8000,             // only while the lights are off
    SiteHealthy = 0x10000,          // only while the current site's state (site +0x14) is 2 (healthy)
    SiteDead = 0x20000,             // ... is 1 (dead)
    SiteUnhealthy = 0x40000,        // ... is 4 (unhealthy)
    OceanAlive = 0x80000,           // only while the ocean's health (COcean +0x88) is >= 0.1
    OceanDead = 0x100000,           // only while the ocean's health is < 0.1
    SiteState7Green = 0x200000,     // site state 7 and the cel's average plant health (+0xf0) > 0.2
    SiteState6Green = 0x400000,     // same for site state 6
    SiteState5Green = 0x800000,     // same for site state 5
    BlockInput = 0x1000000,         // Eng_Api2E_EnableUserInput(0, ..) while playing, re-enabled when it ends
    BlockInputArg = 0x2000000,      // second argument of that call
    AviOption4F = 0x4000000,        // movie: Eng_Api4F_AVIOption(handle, 1, 0) after opening
    KeepPainted = 0x8000000,        // anim: the last frame stays on the page when it stops
    ColourKey = 0x10000000,         // movie: colour key (0,255,255) via Eng_Api37 + Eng_Api42; also DAT_100c1904
    WavRestartsEachLoop = 0x80000000u,   // anim: wav restarts at every loop instead of spanning the animation
};
}

class CSpotFX : public Item {
public:
    bool typed() const override { return true; }
    void saveSlot(SlotWriter& w) const override;   // dll:1000aa80
    void loadSlot(SlotReader& r) override;         // dll:1000aaa0

    const std::string& file() const { return m_szFile; }     // upper-cased file name
    uint32_t flags() const { return m_dwFlags; }
    void setFlags(uint32_t f) { m_dwFlags = f; }              // the builder overwrites it for some FX
    bool on() const { return m_bOn != 0; }
    void setOn(bool v) { m_bOn = v ? 1 : 0; }
    bool playing() const { return m_bPlaying != 0; }
    void setPlaying(bool v) { m_bPlaying = v ? 1 : 0; }
    uint16_t id() const { return m_wId; }
    int32_t handle() const { return m_dwHandle; }
    void setHandle(int32_t h) { m_dwHandle = h; }
    int32_t param7c() const { return m_dwParam7c; }
    void setParam7c(int32_t v) { m_dwParam7c = v; }   // CAnimFX / CSoundSiteFX::Start with flag 0x20 re-roll it

protected:
    // The part of every constructor that is common: file name (upper-cased, CharUpperA), +0x7c, +0x74, +0x68
    // (flags | typeBit), +0x6c, +0x70 = 0, +0x78 = -1.
    CSpotFX(u32 ctorAddr, const char* className, const char* name, int32_t param7c, uint16_t id, uint32_t flags,
            uint32_t typeBit, int32_t on);
    static std::string copyString(const char* s, size_t cap, const char* what);   // char[cap] copy, throws if too long

    // +0x00 vtable; +0x04 char[100] file name; +0x68 flags
    std::string m_szFile;        // +0x04 char[100], upper-cased
    uint32_t m_dwFlags;          // +0x68 SpotFxFlag, constructor argument | type bit
    int32_t m_bOn;               // +0x6c BOOL "m_bOn", the only saved member
    int32_t m_bPlaying;          // +0x70 BOOL "m_bPlaying" (0)
    uint16_t m_wId;              // +0x74 FX id the world switches it by (SetFXState, "owner" in the dump)
    int32_t m_dwHandle;          // +0x78 engine handle of the open anim/movie/sound (-1)
    int32_t m_dwParam7c;         // +0x7c anim: frame delay; movie: x; sound: sound parameter (volume, guess)
};

// dll:100706b0, vtable 10093db0
class CAnimFX : public CSpotFX {
public:
    CAnimFX(const char* name, int32_t delay, int32_t x, int32_t y, uint16_t id, uint32_t flags, int32_t on,
            const char* wavFile);
    int32_t x() const { return m_dwX; }
    int32_t y() const { return m_dwY; }
    const std::string& wavFile() const { return m_szWavFile; }

protected:
    CAnimFX(u32 ctorAddr, const char* className, const char* name, int32_t delay, int32_t x, int32_t y, uint16_t id,
            uint32_t flags, int32_t on);   // CSynthAnimFX: the default CAnimFX constructor (dll:100705c0) + its own part
    int32_t m_dwX;               // +0x80
    int32_t m_dwY;               // +0x84
    std::string m_szWavFile;     // +0x88 char[100], "" = none (the CSynthAnimFX constructor leaves it uninitialised)
};

// dll:10015ce0, vtable 10090548. Plays the "synth" sprite animations; Start hands (x + 100, m_dSynthParam) to the
// synth stalker (DAT_100c27e4/e8/f0).
class CSynthAnimFX final : public CAnimFX {
public:
    CSynthAnimFX(const char* name, int32_t delay, int32_t x, int32_t y, uint16_t id, uint32_t flags, int32_t on,
                 double synthParam);
    double synthParam() const { return m_dSynthParam; }

private:
    double m_dSynthParam;        // +0xf0 (6.0 .. 10.0 in the builder; copied to DAT_100c27f0, meaning not determined)
};

// dll:10002eb0, vtable 1008f240
class CMovieFX final : public CSpotFX {
public:
    CMovieFX(const char* name, int32_t x, int32_t y, uint16_t id, uint32_t flags, int32_t on, const char* wavFile);
    int32_t x() const { return m_dwParam7c; }
    int32_t y() const { return m_dwY; }
    const std::string& wavFile() const { return m_szWavFile; }
    int32_t wavHandle() const { return m_dwWavHandle; }
    void setWavHandle(int32_t h) { m_dwWavHandle = h; }

private:
    int32_t m_dwY;               // +0x80 (+0x7c is x)
    std::string m_szWavFile;     // +0x84 char[100] soundtrack, "" = none
    int32_t m_dwWavHandle;       // +0xe8 engine sound handle (-1)
};

// dll:10017870, vtable 10090888
class CSoundSiteFX final : public CSpotFX {
public:
    CSoundSiteFX(const char* name, int32_t param7c, uint16_t id, uint32_t flags, int32_t on);
};

} // namespace biosys::game
