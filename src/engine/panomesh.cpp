#include "engine/panomesh.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace biosys {

namespace {
constexpr double kRadius = 407.4366547808163;     // 2560 / 2 pi, exe:0047b0a0
constexpr double kRad = 0.017453292519943295;
}

// exe:0040cf00 without its truncations (ftol of the vertical plane coordinate, 16.16 fixed point, the 32-pixel grid)
void panoMap(const PanoCamera& cam, double x, double y, double& u, double& v) {
    const double c = x - 320.5, r = y - 184.5;
    const double pr = cam.pitchDeg * kRad;
    const double cs = std::cos(pr), sn = std::sin(pr);
    const double D = cam.focal * cs - r * sn;
    u = std::atan2(c, D) * kRadius + cam.yaw;
    v = (r * cs + cam.focal * sn) * kRadius / std::sqrt(c * c + D * D) + 512.0;
}

int panoCellFor(double outputScale) {
    if (outputScale <= 1.5) return 8;      // worst error 0.09 output pixel at 1x
    if (outputScale <= 8.0) return 4;      // 0.046 at 2x, 0.09 at 4x, 0.18 at 8x
    return 2;
}

namespace {

struct P4 { double x, y, u, v; };

// the point of the segment a-b where u = L; the end points are taken in a fixed order so that two triangles that share the
// edge compute bit-identical points
P4 cutAt(const P4& a, const P4& b, double L) {
    const P4* p = &a;
    const P4* q = &b;
    if (q->x < p->x || (q->x == p->x && q->y < p->y)) std::swap(p, q);
    const double t = (L - p->u) / (q->u - p->u);
    return P4{p->x + (q->x - p->x) * t, p->y + (q->y - p->y) * t, L, p->v + (q->v - p->v) * t};
}

// Sutherland-Hodgman against u >= L (keepAbove) or u <= L
void clipU(const std::vector<P4>& in, double L, bool keepAbove, std::vector<P4>& out) {
    out.clear();
    const size_t n = in.size();
    for (size_t i = 0; i < n; i++) {
        const P4& a = in[i];
        const P4& b = in[(i + 1) % n];
        const bool ina = keepAbove ? a.u >= L : a.u <= L;
        const bool inb = keepAbove ? b.u >= L : b.u <= L;
        if (ina) out.push_back(a);
        if (ina != inb) out.push_back(cutAt(a, b, L));
    }
}

} // namespace

void buildPanoMesh(const PanoCamera& cam, int cell, PanoMesh& out) { buildPanoMeshRange(cam, cell, 0, kViewW, out); }

void buildPanoMeshRange(const PanoCamera& cam, int cell, int xLo, int xHi, PanoMesh& out) {
    out.verts.clear();
    out.idx.clear();
    const int nx = (xHi - xLo) / cell + 1, ny = kViewH / cell + 1;
    std::vector<P4> g(static_cast<size_t>(nx) * ny);
    const double shift = -std::floor(cam.yaw / kPanoW) * kPanoW;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            P4& p = g[static_cast<size_t>(j) * nx + i];
            p.x = xLo + i * cell;
            p.y = j * cell;
            panoMap(cam, p.x, p.y, p.u, p.v);
            p.u += shift;
            p.v = std::min(std::max(p.v, 0.0), static_cast<double>(kPanoH));
        }
    out.verts.reserve(g.size());
    for (const P4& p : g)
        out.verts.push_back(MeshVertex{static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.u), static_cast<float>(p.v)});
    std::vector<P4> poly, a, b;
    auto triangle = [&](int i0, int i1, int i2) {
        const P4 &p0 = g[static_cast<size_t>(i0)], &p1 = g[static_cast<size_t>(i1)], &p2 = g[static_cast<size_t>(i2)];
        const double lo = std::min({p0.u, p1.u, p2.u}), hi = std::max({p0.u, p1.u, p2.u});
        if (lo >= 0.0 && hi <= kPanoW) {
            out.idx.push_back(i0);
            out.idx.push_back(i1);
            out.idx.push_back(i2);
            return;
        }
        poly.assign({p0, p1, p2});
        for (int w = static_cast<int>(std::floor(lo / kPanoW)); w <= static_cast<int>(std::floor(hi / kPanoW)); w++) {
            const double L = w * static_cast<double>(kPanoW), H = L + kPanoW;
            clipU(poly, L, true, a);
            clipU(a, H, false, b);
            if (b.size() < 3) continue;
            const int base = static_cast<int>(out.verts.size());
            for (const P4& p : b) {
                const double u = std::min(std::max(p.u - L, 0.0), static_cast<double>(kPanoW));
                out.verts.push_back(MeshVertex{static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(u), static_cast<float>(p.v)});
            }
            for (int k = 1; k + 1 < static_cast<int>(b.size()); k++) {
                out.idx.push_back(base);
                out.idx.push_back(base + k);
                out.idx.push_back(base + k + 1);
            }
        }
    };
    for (int j = 0; j + 1 < ny; j++)
        for (int i = 0; i + 1 < nx; i++) {
            const int tl = j * nx + i, tr = tl + 1, bl = tl + nx, br = bl + 1;
            triangle(tl, tr, bl);
            triangle(tr, br, bl);
        }
}

// ---------------------------------------------------------------------------------------------------------------------------------
// texture

static inline u32 rgbOf(const Palette& pal, u8 i) {
    const Color& c = pal.c[i];
    return 0xFF000000u | (static_cast<u32>(c.r) << 16) | (static_cast<u32>(c.g) << 8) | c.b;
}

void panoTextureRows(const Surface& page, const Palette& pal, int y0, int y1, std::vector<u32>& tex) {
    if (tex.size() != static_cast<size_t>(kPanoTexW) * kPanoTexH) tex.assign(static_cast<size_t>(kPanoTexW) * kPanoTexH, 0xFF000000u);
    if (page.w != kPanoW || page.h != kPanoH) throw Error("panoTexture: not a 2560 x 1024 page");
    u32 lut[256];
    for (int i = 0; i < 256; i++) lut[i] = rgbOf(pal, static_cast<u8>(i));
    y0 = std::max(y0, 0);
    y1 = std::min(y1, kPanoH);
    for (int y = y0; y < y1; y++) {
        const u8* s = page.row(y);
        u32* d = &tex[static_cast<size_t>(y + 1) * kPanoTexW];
        d[0] = lut[s[kPanoW - 1]];
        for (int x = 0; x < kPanoW; x++) d[x + 1] = lut[s[x]];
        d[kPanoW + 1] = lut[s[0]];
        if (y == 0) std::copy(d, d + kPanoTexW, tex.begin());
        if (y == kPanoH - 1) std::copy(d, d + kPanoTexW, tex.begin() + static_cast<std::ptrdiff_t>(kPanoH + 1) * kPanoTexW);
    }
}

void panoTextureAll(const Surface& page, const Palette& pal, std::vector<u32>& tex) { panoTextureRows(page, pal, 0, kPanoH, tex); }

// ---------------------------------------------------------------------------------------------------------------------------------
// CPU rasteriser: vertices on a 1/256 pixel grid, exact integer edge functions, top-left rule (no gaps or overlaps between triangles)

void rasterizePano(const PanoMesh& mesh, const std::vector<u32>& tex, int scale, bool linear, std::vector<u32>& out, int texScale) {
    rasterizePanoRegion(mesh, tex, scale, linear, kViewW * scale, kViewH * scale, 0, 0, kViewW * scale, out, texScale);
}

void rasterizePanoRegion(const PanoMesh& mesh, const std::vector<u32>& tex, int scale, bool linear, int W, int H, int offX, int clipX0, int clipX1,
                         std::vector<u32>& out, int texScale) {
    rasterizePanoAffine(mesh, tex, static_cast<double>(scale), linear, W, H, static_cast<double>(offX), 0.0, clipX0, clipX1, 0, H, out, texScale);
}

void rasterizePanoAffine(const PanoMesh& mesh, const std::vector<u32>& tex, double scale, bool linear, int W, int H, double offX, double offY, int clipX0,
                         int clipX1, int clipY0, int clipY1, std::vector<u32>& out, int texScale) {
    if (out.size() != static_cast<size_t>(W) * H) out.assign(static_cast<size_t>(W) * H, 0);
    const int texW = kPanoW * texScale + 2, texH = kPanoH * texScale + 2;
    clipX0 = std::max(clipX0, 0);
    clipX1 = std::min(clipX1, W);
    clipY0 = std::max(clipY0, 0);
    clipY1 = std::min(clipY1, H);
    struct V { int64_t x, y; double u, v; };
    auto fx = [&](double v) { return static_cast<int64_t>(std::llround(v * scale * 256.0)); };
    auto sample = [&](double u, double v) -> u32 {
        u *= texScale;
        v *= texScale;
        if (!linear) {
            const int ix = std::min(std::max(static_cast<int>(std::floor(u)) + 1, 0), texW - 1);
            const int iy = std::min(std::max(static_cast<int>(std::floor(v)) + 1, 0), texH - 1);
            return tex[static_cast<size_t>(iy) * texW + ix] & 0x00FFFFFFu;
        }
        const double sx = std::min(std::max(u + 0.5, 0.0), texW - 1.0 - 1e-9), sy = std::min(std::max(v + 0.5, 0.0), texH - 1.0 - 1e-9);
        const int ix = static_cast<int>(sx), iy = static_cast<int>(sy);
        const double ax = sx - ix, ay = sy - iy;
        const u32 *r0 = &tex[static_cast<size_t>(iy) * texW + ix], *r1 = r0 + texW;
        u32 res = 0;
        for (int sh = 0; sh <= 16; sh += 8) {
            const double c00 = (r0[0] >> sh) & 255, c10 = (r0[1] >> sh) & 255, c01 = (r1[0] >> sh) & 255, c11 = (r1[1] >> sh) & 255;
            const double c = (c00 * (1 - ax) + c10 * ax) * (1 - ay) + (c01 * (1 - ax) + c11 * ax) * ay;
            res |= static_cast<u32>(std::min(255.0, std::floor(c + 0.5))) << sh;
        }
        return res;
    };
    for (size_t t = 0; t + 2 < mesh.idx.size(); t += 3) {
        const MeshVertex& m0 = mesh.verts[static_cast<size_t>(mesh.idx[t])];
        const MeshVertex& m1 = mesh.verts[static_cast<size_t>(mesh.idx[t + 1])];
        const MeshVertex& m2 = mesh.verts[static_cast<size_t>(mesh.idx[t + 2])];
        const int64_t sx = static_cast<int64_t>(std::llround(offX * 256.0)), sy = static_cast<int64_t>(std::llround(offY * 256.0));
        V a{fx(m0.x) + sx, fx(m0.y) + sy, m0.u, m0.v}, b{fx(m1.x) + sx, fx(m1.y) + sy, m1.u, m1.v}, c{fx(m2.x) + sx, fx(m2.y) + sy, m2.u, m2.v};
        int64_t area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (area == 0) continue;
        if (area < 0) { std::swap(b, c); area = -area; }
        auto claims = [](const V& p, const V& q) { const int64_t dx = q.x - p.x, dy = q.y - p.y; return dy > 0 || (dy == 0 && dx < 0); };
        const bool cab = claims(a, b), cbc = claims(b, c), cca = claims(c, a);
        const int64_t minx = std::min({a.x, b.x, c.x}), maxx = std::max({a.x, b.x, c.x});
        const int64_t miny = std::min({a.y, b.y, c.y}), maxy = std::max({a.y, b.y, c.y});
        const int x0 = std::max<int>(clipX0, static_cast<int>((minx - 128) / 256) - 1), x1 = std::min<int>(clipX1 - 1, static_cast<int>((maxx + 128) / 256) + 1);
        const int y0 = std::max<int>(clipY0, static_cast<int>((miny - 128) / 256) - 1), y1 = std::min<int>(clipY1 - 1, static_cast<int>((maxy + 128) / 256) + 1);
        for (int py = y0; py <= y1; py++) {
            const int64_t Py = static_cast<int64_t>(py) * 256 + 128;
            for (int px = x0; px <= x1; px++) {
                const int64_t Px = static_cast<int64_t>(px) * 256 + 128;
                const int64_t eab = (b.x - a.x) * (Py - a.y) - (b.y - a.y) * (Px - a.x);
                const int64_t ebc = (c.x - b.x) * (Py - b.y) - (c.y - b.y) * (Px - b.x);
                const int64_t eca = (a.x - c.x) * (Py - c.y) - (a.y - c.y) * (Px - c.x);
                if (eab < 0 || ebc < 0 || eca < 0) continue;
                if ((eab == 0 && !cab) || (ebc == 0 && !cbc) || (eca == 0 && !cca)) continue;
                const double wa = static_cast<double>(ebc) / area, wb = static_cast<double>(eca) / area, wc = static_cast<double>(eab) / area;
                out[static_cast<size_t>(py) * W + px] = sample(wa * a.u + wb * b.u + wc * c.u, wa * a.v + wb * b.v + wc * c.v);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------

void panoOverlayMask(const Frame& frame, const Rect& view, const u8* bare, std::vector<u8>& mask) {
    mask.assign(static_cast<size_t>(kViewW) * kViewH, 1);
    for (int y = 0; y < kViewH; y++) {
        if (view.y + y < 0 || view.y + y >= Frame::H) continue;
        for (int x = 0; x < kViewW; x++)
            mask[static_cast<size_t>(y) * kViewW + x] = frame.px[static_cast<size_t>(view.y + y) * Frame::W + view.x + x] != bare[static_cast<size_t>(y) * kViewW + x];
    }
}

int composeSmooth(const Frame& frame, const Palette& pal, const Rect& view, const u8* bare, const std::vector<u32>& tex, const PanoCamera& cam,
                  int scale, bool linear, std::vector<u32>& out) {
    int uncovered = 0;
    const int OW = Frame::W * scale, OH = Frame::H * scale;
    out.assign(static_cast<size_t>(OW) * OH, 0);
    u32 lut[256];
    for (int i = 0; i < 256; i++) lut[i] = rgbOf(pal, static_cast<u8>(i)) & 0x00FFFFFFu;
    for (int Y = 0; Y < OH; Y++)
        for (int X = 0; X < OW; X++) out[static_cast<size_t>(Y) * OW + X] = lut[frame.px[static_cast<size_t>(Y / scale) * Frame::W + X / scale]];
    std::vector<u8> mask;
    panoOverlayMask(frame, view, bare, mask);
    PanoMesh mesh;
    buildPanoMesh(cam, panoCellFor(scale), mesh);
    std::vector<u32> sm(static_cast<size_t>(kViewW) * scale * kViewH * scale, 0xFFFFFFFFu);
    rasterizePano(mesh, tex, scale, linear, sm);
    for (int y = 0; y < kViewH * scale; y++)
        for (int x = 0; x < kViewW * scale; x++) {
            if (mask[static_cast<size_t>(y / scale) * kViewW + x / scale]) continue;
            const u32 s = sm[static_cast<size_t>(y) * kViewW * scale + x];
            if (s == 0xFFFFFFFFu) { uncovered++; continue; }   // not covered (cannot happen: the triangles tile the view); the scaled frame stays
            out[static_cast<size_t>(view.y * scale + y) * OW + view.x * scale + x] = s;
        }
    return uncovered;
}

// ---------------------------------------------------------------------------------------------------------------------------------

PanoCamera PanoCameraSmoother::at(u32 t) const {
    if (static_cast<int32_t>(t - t0_) >= static_cast<int32_t>(stepMs_)) return to_;
    const double f = static_cast<int32_t>(t - t0_) <= 0 ? 0.0 : static_cast<double>(t - t0_) / stepMs_;
    PanoCamera c = to_;
    c.yaw = from_.yaw + (to_.yaw - from_.yaw) * f;
    c.pitchDeg = from_.pitchDeg + (to_.pitchDeg - from_.pitchDeg) * f;
    c.fovDeg = from_.fovDeg + (to_.fovDeg - from_.fovDeg) * f;
    c.focal = from_.focal + (to_.focal - from_.focal) * f;
    return c;
}

PanoCamera PanoCameraSmoother::update(const PanoCamera& target, u32 pageSerial, u32 stepTime, u32 now) {
    const bool zooming = target.fovDeg != 75.0 || target.fovDeg != lastFov_;
    const bool zoom = zooming && !zoomGlide_;
    PanoCamera tgt = target;
    if (have_) {
        double d = target.yaw - engineYaw_;
        while (d > kPanoW / 2) d -= kPanoW;
        while (d <= -kPanoW / 2) d += kPanoW;
        tgt.yaw = to_.yaw + d;
    }
    bool snap = !have_ || pageSerial != serial_ || zoom;
    const bool moved = have_ && (tgt.yaw != to_.yaw || tgt.pitchDeg != to_.pitchDeg || tgt.fovDeg != to_.fovDeg || tgt.focal != to_.focal);
    if (!snap && moved && !zooming && (std::fabs(tgt.yaw - to_.yaw) > kSnapYaw || std::fabs(tgt.pitchDeg - to_.pitchDeg) > kSnapPitchDeg)) snap = true;
    if (snap) {
        if (!have_ || pageSerial != serial_ || zoom) tgt.yaw = target.yaw;
        from_ = to_ = tgt;
        t0_ = now;
    } else if (moved) {
        u32 s = stepTime != stepTime_ ? stepTime : now;
        if (static_cast<int32_t>(s - lastNow_) < 0) s = lastNow_;
        if (static_cast<int32_t>(s - now) > 0) s = now;
        from_ = at(s);
        to_ = tgt;
        t0_ = s;
    } else {
        to_ = tgt;   // the same place (fov and focal can only change with a snap)
    }
    snapped_ = snap;
    have_ = true;
    engineYaw_ = target.yaw;
    serial_ = pageSerial;
    stepTime_ = stepTime;
    lastNow_ = now;
    lastFov_ = target.fovDeg;
    return at(now);
}

// ---------------------------------------------------------------------------------------------------------------------------------

void writeBmp24(const std::string& path, int w, int h, const std::vector<u32>& rgb) {
    if (w <= 0 || h <= 0 || rgb.size() != static_cast<size_t>(w) * h) throw Error("writeBmp24: invalid image");
    std::FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) throw Error("cannot write " + path);
    const size_t stride = (static_cast<size_t>(w) * 3 + 3) & ~size_t(3);
    const uint32_t size = static_cast<uint32_t>(54 + stride * h);
    u8 hd[54] = {'B', 'M'};
    auto put32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; i++) hd[o + i] = static_cast<u8>(v >> (8 * i)); };
    put32(2, size); put32(10, 54); put32(14, 40); put32(18, static_cast<uint32_t>(w)); put32(22, static_cast<uint32_t>(h));
    hd[26] = 1; hd[28] = 24; put32(34, static_cast<uint32_t>(stride * h));
    std::fwrite(hd, 1, 54, fp);
    std::vector<u8> row(stride, 0);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            const u32 p = rgb[static_cast<size_t>(y) * w + x];
            row[static_cast<size_t>(x) * 3] = static_cast<u8>(p);
            row[static_cast<size_t>(x) * 3 + 1] = static_cast<u8>(p >> 8);
            row[static_cast<size_t>(x) * 3 + 2] = static_cast<u8>(p >> 16);
        }
        std::fwrite(row.data(), 1, stride, fp);
    }
    std::fclose(fp);
}

} // namespace biosys
