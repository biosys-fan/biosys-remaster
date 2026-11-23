#include "engine/projection.h"
#include <cmath>
#include <cstdint>

namespace biosys {

namespace {
constexpr double kRadius = 407.4366547808163;   // exe:0047b0a0 (render)
constexpr double kRadiusInv = 407.4;            // exe:0047b030 / 0047b5c8 (inverse, setup)
constexpr double kHalfRad = 0.008726646250000001, kRad = 0.017453292500000002;

// __ftol (exe:00461968): truncation toward zero, low 32 bits
int32_t ftol(double v) { return static_cast<int32_t>(static_cast<int64_t>(v)); }

// the focal length the render uses: max(320 / tan(fov / 2), 184 / tan(0.8984954979))
double focal(double fovDeg) {
    const double r1 = 320.0 / std::tan(fovDeg * kHalfRad);
    const double r2 = 184.0 / std::tan(0.8984954979);
    return r2 > r1 ? r2 : r1;
}
// the pitch limit in degrees
double pitchLimit(double f) {
    double lim = std::atan2(368.0 / (f * 2.0), 1.0) * -57.29577957855229 + 51.48;
    return lim < 0.0 ? 0.0 : lim;
}

struct Entry { int32_t u, v; };

// exe:0041a03c: one output line from 21 table entries
void renderLine(const u8* surf, const Entry* t, u8* dst) {
    auto at = [&](int64_t col, uint32_t q) -> u8 {
        const int64_t a = col + static_cast<int64_t>(q & 0x7fe00u) * 5;
        return a >= 0 && a < static_cast<int64_t>(kPanoW) * kPanoH ? surf[a] : 0;
    };
    for (int j = 19; j >= 0; j--) {
        const int32_t ua = t[j].u, va = t[j].v, ub = t[j + 1].u, vb = t[j + 1].v;
        bool slow = false;
        uint32_t e = static_cast<uint32_t>(ub) + 0xa000000u;
        uint32_t x1;
        if (static_cast<int32_t>(e) >= 0xa000000) {
            x1 = static_cast<uint32_t>(ub);
            if (ua < 0) slow = true;
        } else {
            x1 = e;
        }
        const int32_t du = static_cast<int32_t>(static_cast<uint32_t>(ua) - static_cast<uint32_t>(ub)) >> 5;
        const int32_t dv = static_cast<int32_t>(static_cast<uint32_t>(va) - static_cast<uint32_t>(vb)) >> 5;
        const int32_t istep = du >> 16;
        const uint32_t S = ((static_cast<uint32_t>(du) << 16) & 0xff000000u) + (static_cast<uint32_t>(dv >> 7) & 0x7ffffu);
        const int32_t col0 = static_cast<int32_t>(x1) >> 16;
        const uint32_t q0 = ((x1 << 16) & 0xff000000u) + (static_cast<uint32_t>(vb >> 7) & 0x7ffffu);
        uint32_t Q[34];
        int carry[34] = {};
        Q[0] = q0;
        Q[1] = q0 + (S & 0x7ffffu);
        for (int n = 1; n < 33; n++) {
            Q[n + 1] = Q[n] + S;
            carry[n] = Q[n + 1] < Q[n] ? 1 : 0;
        }
        u8* out = dst + 32 * j;
        int32_t c = col0;
        if (!slow) {
            out[31] = at(c, Q[0]);
            for (int n = 1; n < 32; n++) {
                c += istep + carry[n];
                out[31 - n] = at(c, Q[n]);
            }
        } else {
            for (int d = 0; d < 8; d++) {
                const int b = 4 * d;
                out[31 - b] = at(c, Q[b]);
                for (int k = 1; k <= 2; k++) {
                    c += istep + carry[b + k];
                    if (c < 0) c += kPanoW;
                    out[31 - (b + k)] = at(c, Q[b + k]);
                }
                c += istep + carry[b + 3];
                if (c < 0) c += kPanoW;
                c += istep + carry[b + 4];
                // ORIGINAL BUG: the fourth pixel of a group is read at the column of the next pixel (advanced once more, not wrapped yet)
                out[31 - (b + 3)] = at(c, Q[b + 3]);
                if (c < 0) c += kPanoW;
            }
        }
    }
}
} // namespace

void Projection::setFov(double fov) {
    fov_ = fov;
    offset_ = ftol(std::tan(fov * 0.5 * kRad) * kRadiusInv);
    fov1024_ = ftol(fov) << 10;
    const double f = focal(fov);   // exe:00445a70 uses 320/tan(fov*0.00872664625) and 184/tan(0.8984954979)
    const double lim = pitchLimit(f);
    gain_ = ftol(lim * 1024.0 * 0.003048780487804878);
}

void Projection::params(int x, int y, int& yaw, int& pitchIn) const {
    yaw = offset_ + x;
    pitchIn = gain_ * (y - 0x148);
}

void Projection::track(int x, int y) {
    int yaw, pin;
    params(x, y, yaw, pin);
    if (yaw > 0x500) yaw -= 0xa00;
    if (yaw <= -0x500) yaw += 0xa00;
    const double lim = pitchLimit(focal(fov1024_ * 0.0009765625));
    double p = pin * 0.0009765625;
    if (lim < p) p = lim;
    if (-lim > p) p = -lim;
    yaw_ = yaw;
    pitch_ = ftol(p * 1024.0);
}

// exe:0040cf00
void Projection::render(const u8* surface, int x, int y, u8* dst) {
    int yaw, pin;
    params(x, y, yaw, pin);
    if (yaw > 0x500) yaw -= 0xa00;
    if (yaw <= -0x500) yaw += 0xa00;
    const double fov = fov1024_ * 0.0009765625;
    double p = pin * 0.0009765625;
    const double f = focal(fov);
    const double lim = pitchLimit(f);
    if (lim < p) p = lim;
    if (-lim > p) p = -lim;
    yaw_ = yaw;
    pitch_ = ftol(p * 1024.0);
    cam_ = PanoCamera{static_cast<double>(yaw), p, fov, f};
    const double pr = p * kRad;
    const double cs = std::cos(pr), sn = std::sin(pr);
    const double A = cs * f, B = sn * f;
    Entry T[13][21];
    for (int k = 0; k < 13; k++) {
        const int r = -184 + 32 * k;
        const double D = A - sn * r;
        const double D2 = D * D;
        const double G = static_cast<double>(ftol(r * cs + B)) * kRadius;
        for (int j = 0; j < 21; j++) {
            const int c = -320 + 32 * j;
            T[k][j].u = ftol((std::atan2(static_cast<double>(c), D) * kRadius + yaw) * 65536.0);
            T[k][j].v = ftol((G / std::sqrt(static_cast<double>(c * c) + D2) + 512.0) * 65536.0);
        }
    }
    for (int k = 0; k < 12; k++) {
        const int base = 32 * k;
        renderLine(surface, T[k], dst + static_cast<size_t>(base) * kViewW);
        int cnt = 367 - base;
        if (cnt > 31) cnt = 31;
        Entry d[21], cur[21];
        for (int j = 0; j < 21; j++) {
            d[j].u = static_cast<int32_t>(static_cast<uint32_t>(T[k + 1][j].u) - static_cast<uint32_t>(T[k][j].u)) >> 5;
            d[j].v = static_cast<int32_t>(static_cast<uint32_t>(T[k + 1][j].v) - static_cast<uint32_t>(T[k][j].v)) >> 5;
            cur[j] = T[k][j];
        }
        for (int s = 0; s < cnt; s++) {
            for (int j = 0; j < 21; j++) {
                cur[j].u = static_cast<int32_t>(static_cast<uint32_t>(cur[j].u) + static_cast<uint32_t>(d[j].u));
                cur[j].v = static_cast<int32_t>(static_cast<uint32_t>(cur[j].v) + static_cast<uint32_t>(d[j].v));
            }
            renderLine(surface, cur, dst + static_cast<size_t>(base + 1 + s) * kViewW);
        }
    }
}

// exe:00403470 (panorama branch)
void Projection::inverse(int sx, int sy, int& px, int& py) const {
    const double rp = pitch_ * 1.7044230957031252e-05;
    const double Sc = 320.0 / std::tan(fov_ * kHalfRad);
    const double v = sy - 0xb8, hx = sx - 0x140;
    const double cs = std::cos(rp), sn = std::sin(rp);
    const double Fv = v * cs + sn * Sc;
    const double Dv = cs * Sc - sn * v;
    int x = ftol(std::atan(hx / Dv) * kRadiusInv + yaw_);
    if (x < 0) x += 0xa00;
    if (x > 0xa00) x -= 0xa00;
    px = x;
    py = ftol(Fv / std::sqrt(hx * hx + Dv * Dv) * kRadiusInv + 512.0);
}

bool Projection::forward(double u, double v, int& sx, int& sy) const {
    const double rp = pitch_ * 1.7044230957031252e-05;
    const double F = 320.0 / std::tan(fov_ * kHalfRad);
    double du = u - yaw_;
    while (du > kPanoW / 2) du -= kPanoW;
    while (du <= -kPanoW / 2) du += kPanoW;
    const double th = du / kRadiusInv, rho = (v - 512.0) / kRadiusInv;
    if (std::fabs(th) >= 1.5) return false;
    const double cs = std::cos(rp), sn = std::sin(rp), ct = std::cos(th);
    const double den = cs * ct + rho * sn;
    if (std::fabs(den) < 1e-9) return false;
    const double Y = F * (rho * cs - sn * ct) / den;
    const double D = F * cs - Y * sn;
    if (D <= 0) return false;
    sx = static_cast<int>(std::lround(320 + D * std::tan(th)));
    sy = static_cast<int>(std::lround(184 + Y));
    return true;
}

} // namespace biosys
