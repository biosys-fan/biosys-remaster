#include "app/text_sharp.h"
#include "engine/panomesh.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace biosys {

namespace {

// The low-resolution pixels where this run's sharp glyphs may be drawn: the run's own visible text pixels and their 8 neighbours, inside the
// cell (one pixel wider on each side) and the clip rectangle.
struct Region {
    int x0 = 0, y0 = 0, w = 0, h = 0;
    std::vector<u8> in;
    bool has(int x, int y) const { return x >= x0 && y >= y0 && x < x0 + w && y < y0 + h && in[static_cast<size_t>(y - y0) * w + (x - x0)]; }
};

Region regionOf(const Engine::TextRunOut& r) {
    Region g;
    int minx = Frame::W, miny = Frame::H, maxx = -1, maxy = -1;
    for (u32 p : r.px) {
        const int x = static_cast<int>(p % Frame::W), y = static_cast<int>(p / Frame::W);
        minx = std::min(minx, x); maxx = std::max(maxx, x);
        miny = std::min(miny, y); maxy = std::max(maxy, y);
    }
    if (maxx < 0) return g;
    g.x0 = std::max(minx - 1, 0);
    g.y0 = std::max(miny - 1, 0);
    g.w = std::min(maxx + 2, Frame::W) - g.x0;
    g.h = std::min(maxy + 2, Frame::H) - g.y0;
    std::vector<u8> seed(static_cast<size_t>(g.w) * g.h, 0);
    for (u32 p : r.px) {
        const int x = static_cast<int>(p % Frame::W) - g.x0, y = static_cast<int>(p / Frame::W) - g.y0;
        seed[static_cast<size_t>(y) * g.w + x] = 1;
    }
    g.in.assign(seed.size(), 0);
    const int cl = std::max(r.clipL, r.x - 1), cr = std::min(r.clipR, r.x + r.font->textWidth(r.text.data(), static_cast<int>(r.text.size())) + 1);
    const int ct = std::max(r.clipT, r.y), cb = std::min(r.clipB, r.y + r.font->height());
    for (int y = 0; y < g.h; y++)
        for (int x = 0; x < g.w; x++) {
            const int ax = x + g.x0, ay = y + g.y0;
            if (ax < cl || ax >= cr || ay < ct || ay >= cb) continue;
            bool on = false;
            for (int dy = -1; dy <= 1 && !on; dy++)
                for (int dx = -1; dx <= 1 && !on; dx++) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < g.w && ny < g.h && seed[static_cast<size_t>(ny) * g.w + nx]) on = true;
                }
            g.in[static_cast<size_t>(y) * g.w + x] = on ? 1 : 0;
        }
    return g;
}

// straight-alpha "over" of (r, g, b, a) on the pixel
inline bool over(uint32_t& dst, int r, int g, int b, int a) {
    if (a <= 0) return false;
    const int da = static_cast<int>(dst >> 24);
    if (da == 0) {
        dst = static_cast<uint32_t>(a) << 24 | static_cast<uint32_t>(r) << 16 | static_cast<uint32_t>(g) << 8 | static_cast<uint32_t>(b);
        return true;
    }
    const int oa = a + da * (255 - a) / 255;
    if (oa <= 0) return false;
    auto mix = [&](int s, int d) { return (s * a * 255 + d * da * (255 - a) + (oa * 255) / 2) / (oa * 255); };
    const int dr = static_cast<int>((dst >> 16) & 255), dg = static_cast<int>((dst >> 8) & 255), db = static_cast<int>(dst & 255);
    dst = static_cast<uint32_t>(oa) << 24 | static_cast<uint32_t>(mix(r, dr)) << 16 | static_cast<uint32_t>(mix(g, dg)) << 8 | static_cast<uint32_t>(mix(b, db));
    return false;
}

} // namespace

size_t renderTextOverlay(const Engine::TextFrame& tf, int k, std::vector<uint32_t>& argb) {
    const int W = Frame::W * k, H = Frame::H * k;
    argb.assign(static_cast<size_t>(W) * H, 0);
    size_t touched = 0;
    for (const Engine::TextRunOut& r : tf.runs) {
        if (!r.font || !r.font->hasFace()) continue;
        const Region reg = regionOf(r);
        if (reg.w <= 0) continue;
        const int baseline = (r.y + r.font->ascent()) * k;
        int pen = r.x;
        for (unsigned char c : r.text) {
            const GlyphCoverage& g = r.font->glyphScaled(c, k);
            const int gx0 = pen * k + g.left, gy0 = baseline - g.top;
            for (int v = 0; v < g.h; v++) {
                const int Y = gy0 + v;
                if (Y < 0 || Y >= H) continue;
                for (int u = 0; u < g.w; u++) {
                    const int X = gx0 + u;
                    if (X < 0 || X >= W) continue;
                    const int a = g.a[static_cast<size_t>(v) * g.w + u];
                    if (a == 0 || !reg.has(X / k, Y / k)) continue;
                    if (over(argb[static_cast<size_t>(Y) * W + X], r.r, r.g, r.b, a)) touched++;
                }
            }
            pen += r.font->advance(c);
        }
    }
    return touched;
}

size_t renderTextOverlayScaled(const Engine::TextFrame& tf, double k, int row0, int rows, double fx, double fy, const char* skipLayer,
                               std::vector<uint32_t>& argb, int& outW, int& outH) {
    outW = static_cast<int>(std::ceil(Frame::W * k)) + 2;
    outH = static_cast<int>(std::ceil(rows * k)) + 2;
    argb.assign(static_cast<size_t>(outW) * outH, 0);
    size_t touched = 0;
    for (const Engine::TextRunOut& r : tf.runs) {
        if (!r.font || !r.font->hasFace()) continue;
        if (skipLayer && r.layer == skipLayer) continue;
        if (r.y + r.font->height() <= row0 || r.y >= row0 + rows) continue;
        const Region reg = regionOf(r);
        if (reg.w <= 0) continue;
        const int baseline = static_cast<int>(std::lround(fy + (r.y + r.font->ascent() - row0) * k));
        int pen = r.x;
        for (unsigned char c : r.text) {
            const GlyphCoverage& g = r.font->glyphScaledF(c, k);
            const int gx0 = static_cast<int>(std::lround(fx + pen * k)) + g.left, gy0 = baseline - g.top;
            for (int v = 0; v < g.h; v++) {
                const int Y = gy0 + v;
                if (Y < 0 || Y >= outH) continue;
                const int ly = static_cast<int>(std::floor((Y - fy) / k)) + row0;
                for (int u = 0; u < g.w; u++) {
                    const int X = gx0 + u;
                    if (X < 0 || X >= outW) continue;
                    const int a = g.a[static_cast<size_t>(v) * g.w + u];
                    if (a == 0 || !reg.has(static_cast<int>(std::floor((X - fx) / k)), ly)) continue;
                    if (over(argb[static_cast<size_t>(Y) * outW + X], r.r, r.g, r.b, a)) touched++;
                }
            }
            pen += r.font->advance(c);
        }
    }
    return touched;
}

void composeSharpText(const Engine::TextFrame& tf, int k, std::vector<uint32_t>& rgb, const std::vector<uint32_t>* base) {
    const int W = Frame::W * k, H = Frame::H * k;
    std::vector<uint32_t> ov;
    renderTextOverlay(tf, k, ov);
    if (base && base->size() != static_cast<size_t>(W) * H) throw Error("composeSharpText: bad base picture");
    rgb.assign(static_cast<size_t>(W) * H, 0);
    for (int Y = 0; Y < H; Y++)
        for (int X = 0; X < W; X++) {
            const size_t at = static_cast<size_t>(Y) * W + X;
            uint32_t px;
            if (base) {
                px = (*base)[at];
            } else {
                const Color& c = tf.palette.c[tf.free.px[static_cast<size_t>(Y / k) * Frame::W + static_cast<size_t>(X / k)]];
                px = static_cast<uint32_t>(c.r) << 16 | static_cast<uint32_t>(c.g) << 8 | c.b;
            }
            const uint32_t o = ov[at];
            const int a = static_cast<int>(o >> 24);
            if (a) {
                auto mix = [&](int s, int d) { return (s * a + d * (255 - a) + 127) / 255; };
                px = static_cast<uint32_t>(mix((o >> 16) & 255, (px >> 16) & 255)) << 16 | static_cast<uint32_t>(mix((o >> 8) & 255, (px >> 8) & 255)) << 8 |
                     static_cast<uint32_t>(mix(o & 255, px & 255));
            }
            rgb[at] = px;
        }
}

uint64_t textOverlaySignature(const Engine::TextFrame& tf, int k) {
    uint64_t h = 1469598103934665603ull;
    auto mixin = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    mixin(static_cast<uint64_t>(k));
    for (const Engine::TextRunOut& r : tf.runs) {
        mixin(r.id);
        mixin(static_cast<uint64_t>(static_cast<unsigned>(r.x)) << 32 | static_cast<unsigned>(r.y));
        mixin(static_cast<uint64_t>(r.r) << 16 | static_cast<uint64_t>(r.g) << 8 | r.b);
        mixin(static_cast<uint64_t>(static_cast<unsigned>(r.clipL)) << 48 | static_cast<uint64_t>(static_cast<unsigned>(r.clipT)) << 32 |
              static_cast<uint64_t>(static_cast<unsigned>(r.clipR)) << 16 | static_cast<unsigned>(r.clipB));
        mixin(r.px.size());
        for (u32 p : r.px) mixin(p);
        for (unsigned char c : r.text) mixin(c);
    }
    return h;
}

// ------------------------------------------------------------------------------------------------ script commands

namespace {

std::string hexOf(const std::string& s) {
    static const char* d = "0123456789abcdef";
    std::string o;
    for (unsigned char c : s) { o += d[c >> 4]; o += d[c & 15]; }
    return o.empty() ? "-" : o;
}

void writeRuns(const std::string& path, const Engine::TextFrame& tf) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) throw Error("cannot write " + path);
    for (const Engine::TextRunOut& r : tf.runs) {
        std::string adv;
        for (unsigned char c : r.text) adv += (adv.empty() ? "" : ",") + std::to_string(r.font->advance(c));
        std::fprintf(f, "run id=%u layer=%s x=%d y=%d clip=%d,%d,%d,%d ink=%d rgb=%d,%d,%d color=0x%08x align=%u opaque=%d bk=%d face=%s ppem=%d ascent=%d descent=%d "
                        "px=%zu adv=%s text=%s\n",
                     r.id, std::string(r.layer.empty() ? "-" : r.layer).c_str(), r.x, r.y, r.clipL, r.clipT, r.clipR, r.clipB, r.ink, r.r, r.g, r.b, r.colorRef,
                     r.align, r.opaque ? 1 : 0, r.bkIndex, std::string(r.font->family()).c_str(), r.font->ppem(), r.font->ascent(), r.font->descent(), r.px.size(),
                     adv.empty() ? "-" : adv.c_str(), hexOf(r.text).c_str());
    }
    std::fclose(f);
}

void writeFrames(const std::string& prefix, const Engine::TextFrame& tf) {
    writeBmp(prefix + "_full.bmp", tf.full.toImage(), tf.palette);
    writeBmp(prefix + "_free.bmp", tf.free.toImage(), tf.palette);
    Image8 m;
    m.w = Frame::W;
    m.h = Frame::H;
    m.px = tf.mask;
    Palette bw;
    bw.c[1] = Color{255, 255, 255};
    writeBmp(prefix + "_mask.bmp", m, bw);
    writeRuns(prefix + "_runs.txt", tf);
}

size_t maskCount(const Engine::TextFrame& tf) {
    return static_cast<size_t>(std::count(tf.mask.begin(), tf.mask.end(), 1));
}

} // namespace

bool scriptTextCommand(Engine& e, const std::string& op, const std::string& arg) {
    if (op == "sharptext") {
        if (arg != "on" && arg != "off") throw Error("script: sharptext on|off");
        e.setTextTracking(arg == "on");
    } else if (op == "textframes" || op == "textshot" || op == "textruns" || op == "textredraw") {
        std::istringstream a(arg);
        std::string file, prefix, tok;
        int scale = 2;
        bool smooth = false;
        a >> file;
        if (op == "textshot") {
            a >> scale;
            while (a >> tok) {
                if (tok == "smooth") smooth = true;
                else prefix = tok;
            }
        }
        if (file.empty() || scale < 1 || scale > 8) throw Error("script: " + op + " FILE [SCALE [PREFIX]]");
        Engine::TextFrame tf;
        e.renderText(tf);
        if (op == "textframes") {
            writeFrames(file, tf);
            std::printf("textframes %s runs=%zu maskpx=%zu orphans=%zu\n", file.c_str(), tf.runs.size(), maskCount(tf), tf.orphans);
        } else if (op == "textruns") {
            writeRuns(file, tf);
            std::printf("textruns %s runs=%zu\n", file.c_str(), tf.runs.size());
        } else if (op == "textredraw") {
            // the text-free frame with each run's low-resolution glyphs drawn again where the frame shows text: must be the frame itself
            Frame g = tf.free;
            for (const Engine::TextRunOut& r : tf.runs) {
                if (!r.font) continue;
                const int baseline = r.y + r.font->ascent();
                int pen = r.x;
                for (unsigned char c : r.text) {
                    const GlyphBitmap& gb = r.font->glyph(c);
                    for (int gy = 0; gy < gb.h; gy++)
                        for (int gx = 0; gx < gb.w; gx++) {
                            const int x = pen + gb.left + gx, y = baseline - gb.top + gy;
                            if (!gb.px[static_cast<size_t>(gy) * gb.w + gx] || x < r.clipL || x >= r.clipR || y < r.clipT || y >= r.clipB) continue;
                            if (x < 0 || y < 0 || x >= Frame::W || y >= Frame::H) continue;
                            const size_t p = static_cast<size_t>(y) * Frame::W + x;
                            if (tf.mask[p]) g.px[p] = r.ink;
                        }
                    pen += gb.advance;
                }
            }
            size_t diff = 0;
            for (size_t i = 0; i < g.px.size(); i++) diff += g.px[i] != tf.full.px[i];
            writeBmp(file, g.toImage(), tf.palette);
            std::printf("textredraw %s diff=%zu runs=%zu maskpx=%zu orphans=%zu\n", file.c_str(), diff, tf.runs.size(), maskCount(tf), tf.orphans);
        } else {
            std::vector<uint32_t> rgb, under;
            bool smoothed = false;
            if (smooth && e.panorama().active) {      // the smooth panorama under the text-free frame (panomesh.h), as the window does
                const Engine::PanoramaFrame& pf = e.panorama();
                std::vector<u32> tex;
                panoTextureAll(*pf.page, *pf.palette, tex);
                composeSmooth(tf.free, *pf.palette, pf.view, pf.bare, tex, pf.camera, scale, true, under);
                smoothed = true;
            }
            composeSharpText(tf, scale, rgb, smoothed ? &under : nullptr);
            writeBmp24(file, Frame::W * scale, Frame::H * scale, rgb);
            if (!prefix.empty()) writeFrames(prefix, tf);
            std::printf("textshot %s %d runs=%zu maskpx=%zu orphans=%zu\n", file.c_str(), scale, tf.runs.size(), maskCount(tf), tf.orphans);
        }
        std::fflush(stdout);
    } else {
        return false;
    }
    return true;
}

} // namespace biosys
