// The game's random number generator: the MSVC 4.2 C runtime rand()/srand() the original calls (_rand dll:10079380,
// _srand dll:10079370) and the two helpers built on it. One generator per game (GameState), never global.
#pragma once
#include <cstdint>

namespace biosys::game {

class Rng {
public:
    // The runtime's per-thread seed (_holdrand) is 1 until srand is called (_initptd).
    explicit Rng(uint32_t seed = 1) : holdrand_(seed) {}

    // srand dll:10079370. The original seeds with GetTickCount() in Event_OnStartup (dll:1001d7b0); the port takes the
    // value from the host (SaveGame::Options::randSeed).
    void seed(uint32_t s) { holdrand_ = s; }
    uint32_t state() const { return holdrand_; }

    // _rand dll:10079380: seed = seed * 0x343fd + 0x269ec3; return (seed >> 16) & 0x7fff
    int rand() {
        holdrand_ = holdrand_ * 214013u + 2531011u;
        return static_cast<int>((holdrand_ & 0x7fff0000u) >> 16);
    }

    // RandRange dll:100075a0: uniform in [lo, hi] (the original computes in x87 extended precision)
    double randRange(double lo, double hi) { return (hi - lo) * rand() * kInv32767 + lo; }
    // RandPlusMinus dll:100075d0: uniform in [-x, x]
    double randPlusMinus(double x) { return x * 2.0 * rand() * kInv32767 - x; }
    // ChancePercent dll:10007570: the caller leaves its threshold on the FPU stack; true when rand() <= (int)threshold
    bool chance(double threshold) {
        int t = static_cast<int>(threshold);   // __ftol truncates; evaluated before the rand() call
        return rand() <= t;
    }

private:
    static constexpr double kInv32767 = 3.051850947599719e-05;   // 1 / 0x7fff
    uint32_t holdrand_;
};

} // namespace biosys::game
