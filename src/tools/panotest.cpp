// Unit tests and measurements of the smooth-panorama mathematics (src/engine/panomesh.*). No game data needed.
//   biosys-panotest mesh      the interpolation error of the mesh in output pixels, per cell size and output scale
//   biosys-panotest seam      the mesh covers the view without gaps and samples the right page column across the x seam
//   biosys-panotest bench     CPU cost per frame of the mesh and the texture conversion
//   biosys-panotest smoother  the camera smoother: monotonic, one step of latency at most, exact at the end, snaps
#include "engine/panomesh.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>

using namespace biosys;

namespace {

int failed = 0;
void check(const char* name, bool ok, const char* detail = "") {
    std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, *detail ? " -- " : "", detail);
    if (!ok) failed++;
}

PanoCamera camera(double yaw, double pitch, double fov) {
    const double f1 = 320.0 / std::tan(fov * 0.00872664625), f2 = 184.0 / std::tan(0.8984954979);
    return PanoCamera{yaw, pitch, fov, f1 > f2 ? f1 : f2};
}

// exact (u, v) against the linear interpolation over the mesh cell, as view pixels x output scale
double meshError(const PanoCamera& cam, int cell, double scale) {
    double worst = 0;
    const int steps = 4;
    for (int cy = 0; cy < kViewH; cy += cell)
        for (int cx = 0; cx < kViewW; cx += cell) {
            double cu[2][2], cv[2][2];
            for (int j = 0; j < 2; j++)
                for (int i = 0; i < 2; i++) {
                    panoMap(cam, cx + i * cell, cy + j * cell, cu[j][i], cv[j][i]);
                    if (cv[j][i] < 0) cv[j][i] = 0;
                    if (cv[j][i] > kPanoH) cv[j][i] = kPanoH;
                }
            for (int sj = 0; sj <= steps; sj++)
                for (int si = 0; si <= steps; si++) {
                    const double fx = static_cast<double>(si) / steps, fy = static_cast<double>(sj) / steps;
                    double iu, iv;
                    if (fx + fy <= 1.0) {   // triangle tl, tr, bl
                        iu = cu[0][0] + (cu[0][1] - cu[0][0]) * fx + (cu[1][0] - cu[0][0]) * fy;
                        iv = cv[0][0] + (cv[0][1] - cv[0][0]) * fx + (cv[1][0] - cv[0][0]) * fy;
                    } else {                // triangle tr, br, bl
                        iu = cu[0][1] + (cu[1][1] - cu[0][1]) * fy + (cu[1][0] - cu[1][1]) * (1 - fx);
                        iv = cv[0][1] + (cv[1][1] - cv[0][1]) * fy + (cv[1][0] - cv[1][1]) * (1 - fx);
                    }
                    const double x = cx + fx * cell, y = cy + fy * cell;
                    double u, v, u1, v1, u2, v2;
                    panoMap(cam, x, y, u, v);
                    const double h = 0.01;
                    panoMap(cam, x + h, y, u1, v1);
                    panoMap(cam, x, y + h, u2, v2);
                    const double a = (u1 - u) / h, c = (v1 - v) / h, b = (u2 - u) / h, d = (v2 - v) / h;   // J = [a b; c d]
                    const double det = a * d - b * c;
                    if (std::fabs(det) < 1e-9) continue;
                    const double du = iu - u, dv = iv - v;
                    const double ex = (d * du - b * dv) / det, ey = (-c * du + a * dv) / det;   // view pixels
                    worst = std::max(worst, std::sqrt(ex * ex + ey * ey) * scale);
                }
        }
    return worst;
}

void meshMode() {
    struct Cam { const char* name; PanoCamera c; };
    const Cam cams[] = {
        {"fov75 pitch0", camera(1000, 0, 75)},          {"fov75 pitch+27.5", camera(1000, 27.5, 75)},
        {"fov75 pitch-27.5", camera(1000, -27.5, 75)},  {"fov75 pitch+14", camera(2400, 14, 75)},
        {"fov39 (zoom end)", camera(1000, 5, 39)},      {"fov60", camera(300, -10, 60)},
        {"fov128 (zoom start)", camera(1000, 0, 128)},  {"fov128 pitch1.8", camera(-1200, 1.8, 128)},
    };
    const int cells[] = {16, 8, 4};
    const double scales[] = {1, 2, 3, 4, 6};
    std::printf("max error of the texture coordinates in output pixels (worst over %zu cameras)\n", sizeof cams / sizeof cams[0]);
    std::printf("cell \\ scale ");
    for (double s : scales) std::printf("%8.0fx", s);
    std::printf("\n");
    double chosen = 0;
    for (int cell : cells) {
        std::printf("%4d         ", cell);
        for (double s : scales) {
            double w = 0;
            for (const Cam& c : cams) w = std::max(w, meshError(c.c, cell, s));
            std::printf("%9.4f", w);
            if (cell == panoCellFor(s)) chosen = std::max(chosen, w);
        }
        std::printf("\n");
    }
    for (double s : scales) {
        double w = 0;
        for (const Cam& c : cams) w = std::max(w, meshError(c.c, panoCellFor(s), s));
        std::printf("chosen cell %d at %.0fx: max error %.4f output pixels\n", panoCellFor(s), s, w);
        char name[64];
        std::snprintf(name, sizeof name, "mesh error at %.0fx (cell %d) under 0.25 output pixel", s, panoCellFor(s));
        check(name, w < 0.25);
    }
    std::printf("worst chosen error %.4f output pixels\n", chosen);
}

void seamMode() {
    // texture that stores the page position: 0x00RRGGBB = (column << 10) | row
    std::vector<u32> tex(static_cast<size_t>(kPanoTexW) * kPanoTexH);
    for (int y = 0; y < kPanoTexH; y++)
        for (int x = 0; x < kPanoTexW; x++) {
            const int col = (x - 1 + kPanoW) % kPanoW, row = std::min(std::max(y - 1, 0), kPanoH - 1);
            tex[static_cast<size_t>(y) * kPanoTexW + x] = 0xFF000000u | (static_cast<u32>(col) << 10) | static_cast<u32>(row);
        }
    const double yaws[] = {1280, 0, 100, 2500, -1200, 1250, 2559, 1, 1279.5, -1279};
    const double pitches[] = {0, 27.5, -27.5};
    int cams = 0, uncovered = 0, wrongCol = 0, wrongRow = 0, samples = 0, vertsOut = 0;
    for (int scale : {1, 2, 3}) {
        for (double yaw : yaws)
            for (double pitch : pitches) {
                const PanoCamera cam = camera(yaw, pitch, 75);
                PanoMesh mesh;
                buildPanoMesh(cam, panoCellFor(scale), mesh);
                for (int k : mesh.idx) {
                    const MeshVertex& v = mesh.verts[static_cast<size_t>(k)];
                    if (v.u < 0 || v.u > kPanoW || v.v < 0 || v.v > kPanoH) vertsOut++;
                }
                std::vector<u32> out(static_cast<size_t>(kViewW) * scale * kViewH * scale, 0xFFFFFFFFu);
                rasterizePano(mesh, tex, scale, false, out);
                cams++;
                for (int Y = 0; Y < kViewH * scale; Y++)
                    for (int X = 0; X < kViewW * scale; X++) {
                        const u32 p = out[static_cast<size_t>(Y) * kViewW * scale + X];
                        if (p == 0xFFFFFFFFu) { uncovered++; continue; }
                        double u, v;
                        panoMap(cam, (X + 0.5) / scale, (Y + 0.5) / scale, u, v);
                        const int ec = static_cast<int>(std::floor(u)), er = static_cast<int>(std::floor(std::min(std::max(v, 0.0), kPanoH - 1e-6)));
                        const int gc = static_cast<int>(p >> 10), gr = static_cast<int>(p & 1023);
                        int dc = ((gc - ec) % kPanoW + kPanoW) % kPanoW;
                        if (dc > kPanoW / 2) dc -= kPanoW;
                        samples++;
                        if (std::abs(dc) > 1) wrongCol++;
                        if (std::abs(gr - er) > 1) wrongRow++;
                    }
            }
    }
    char d[160];
    std::snprintf(d, sizeof d, "%d cameras, %d samples", cams, samples);
    check("every vertex the triangles use after the seam split has u in [0, 2560] and v in [0, 1024]", vertsOut == 0);
    check("no view pixel is left uncovered by the triangles (cameras across the x seam, top and bottom limits, 1x 2x 3x)", uncovered == 0, d);
    std::snprintf(d, sizeof d, "wrong columns %d, wrong rows %d of %d", wrongCol, wrongRow, samples);
    check("every pixel samples the exact page column / row within one texel (nearest, wrapped at the seam)", wrongCol == 0 && wrongRow == 0, d);
}

void smootherMode() {
    PanoCameraSmoother s;
    PanoCamera c = camera(1000, 0, 75);
    // a scripted scroll: engine steps of +20 +10 +5 +2 +1 at 16 ms intervals, the display samples every 4 ms
    std::vector<std::pair<u32, double>> steps = {{16, 20}, {32, 10}, {48, 5}, {64, 2}, {80, 1}};
    double yaw = 1000, prev = -1;
    u32 stamp = 0;
    bool mono = true, bounded = true, lagOk = true;
    std::vector<double> engineAt(400, 1000);
    for (u32 t = 0; t <= 200; t += 4) {
        for (auto& st : steps)
            if (st.first == t) { yaw += st.second; stamp = t; }
        engineAt[t / 4] = yaw;
        c.yaw = yaw;
        const PanoCamera d = s.update(c, 1, stamp, t);
        if (prev >= 0 && d.yaw < prev - 1e-12) mono = false;
        prev = d.yaw;
        if (d.yaw > yaw + 1e-12) bounded = false;
        const double lagTarget = t >= 16 ? engineAt[(t - 16) / 4] : 1000;
        if (d.yaw < lagTarget - 1e-9) lagOk = false;
        if (t >= 96 && std::fabs(d.yaw - yaw) > 0) lagOk = false;
    }
    check("smoother: displayed yaw monotonic, never beyond the engine's camera", mono && bounded);
    check("smoother: never behind the engine's camera of 16 ms before; exactly on it from 16 ms after the last step", lagOk);

    PanoCameraSmoother s2;
    PanoCamera a = camera(1000, 0, 75);
    s2.update(a, 1, 100, 100);
    a.yaw = 1010;
    PanoCamera d = s2.update(a, 1, 108, 116);
    check("smoother: a step dated 8 ms ago is half way (1005)", std::fabs(d.yaw - 1005) < 1e-9 && !s2.snapped());
    d = s2.update(a, 1, 108, 124);
    check("smoother: 16 ms after the step it is exactly on the camera", d.yaw == 1010);
    d = s2.update(a, 2, 108, 130);
    check("smoother: a page change snaps", s2.snapped());
    a.yaw = 1500;
    d = s2.update(a, 2, 140, 140);
    check("smoother: a jump of more than 160 pixels snaps", s2.snapped() && d.yaw == 1500);
    PanoCamera z = camera(1500, 0, 72);
    d = s2.update(z, 2, 140, 150);
    check("smoother: a zoom frame (fov 72) snaps", s2.snapped());
    z = camera(1500, 0, 75);
    d = s2.update(z, 2, 140, 160);
    check("smoother: the first frame back at fov 75 still snaps (the fov changed)", s2.snapped());
    d = s2.update(z, 2, 140, 170);
    check("smoother: and the next frame does not", !s2.snapped());
    // seam: the engine's yaw wraps from 1279 to -1279 (+2): the glide goes forward
    PanoCameraSmoother s3;
    PanoCamera w = camera(1279, 0, 75);
    s3.update(w, 1, 100, 100);
    w.yaw = -1279;
    d = s3.update(w, 1, 108, 116);
    check("smoother: across the yaw wrap of the engine the glide goes the short way (1280)", std::fabs(d.yaw - 1280) < 1e-9);
    // never rewinds when a stale step time is passed
    PanoCameraSmoother s4;
    PanoCamera q = camera(500, 0, 75);
    s4.update(q, 1, 0, 100);
    q.yaw = 510;
    d = s4.update(q, 1, 40, 108);   // dated before the last display time: clamped to it
    check("smoother: a step time before the last frame is clamped (no rewind, half way at 8 ms)", std::fabs(d.yaw - 505) < 1e-9);
}

template <class F> double msPer(int n, F f) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; i++) f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
}

// CPU cost per frame of the parts of the smooth path that do not run on the GPU
void benchMode() {
    const PanoCamera cam = camera(1000, 5, 75);
    PanoMesh mesh;
    for (int cell : {8, 4, 2}) {
        const double ms = msPer(50, [&] { buildPanoMesh(cam, cell, mesh); });
        std::printf("mesh cell %d: %.3f ms per frame (%zu vertices, %zu triangles)\n", cell, ms, mesh.verts.size(), mesh.idx.size() / 3);
    }
    Surface page;
    page.init(kPanoW, kPanoH, 7);
    for (size_t i = 0; i < page.px.size(); i++) page.px[i] = static_cast<u8>(i * 31);
    Palette pal;
    for (int i = 0; i < 256; i++) pal.c[i] = Color{static_cast<u8>(i), static_cast<u8>(255 - i), static_cast<u8>(i / 2)};
    std::vector<u32> tex;
    std::printf("page texture, all 1024 rows (a palette change): %.3f ms\n", msPer(20, [&] { panoTextureAll(page, pal, tex); }));
    std::printf("page texture, 32 rows (a changed ornament or animation): %.3f ms\n", msPer(200, [&] { panoTextureRows(page, pal, 100, 132, tex); }));
    for (int scale : {2, 3}) {
        std::vector<u32> out(static_cast<size_t>(kViewW) * scale * kViewH * scale);
        buildPanoMesh(cam, panoCellFor(scale), mesh);
        std::printf("CPU rasteriser, bilinear, %dx (%d x %d): %.1f ms (only the headless capture uses it)\n", scale, kViewW * scale, kViewH * scale,
                    msPer(3, [&] { rasterizePano(mesh, tex, scale, true, out); }));
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::string m = argc > 1 ? argv[1] : "";
    if (m == "mesh") meshMode();
    else if (m == "seam") seamMode();
    else if (m == "smoother") smootherMode();
    else if (m == "bench") benchMode();
    else { std::fprintf(stderr, "usage: biosys-panotest mesh|seam|smoother\n"); return 2; }
    std::printf(failed ? "FAILED %d\n" : "ALL OK\n", failed);
    return failed ? 1 : 0;
}
