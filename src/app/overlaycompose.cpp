#include "app/overlaycompose.h"
#include "app/text_sharp.h"
#include "engine/panowide.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace biosys {

namespace {

// straight-alpha "over" of (r, g, b, a) on a 0xAARRGGBB pixel
inline void overPx(uint32_t& dst, int r, int g, int b, int a) {
    if (a <= 0) return;
    const int da = static_cast<int>(dst >> 24);
    if (da == 0) {
        dst = static_cast<uint32_t>(a) << 24 | static_cast<uint32_t>(r) << 16 | static_cast<uint32_t>(g) << 8 | static_cast<uint32_t>(b);
        return;
    }
    const int oa = a + da * (255 - a) / 255;
    if (oa <= 0) return;
    auto mix = [&](int s, int d) { return (s * a * 255 + d * da * (255 - a) + (oa * 255) / 2) / (oa * 255); };
    const int dr = static_cast<int>((dst >> 16) & 255), dg = static_cast<int>((dst >> 8) & 255), db = static_cast<int>(dst & 255);
    dst = static_cast<uint32_t>(oa) << 24 | static_cast<uint32_t>(mix(r, dr)) << 16 | static_cast<uint32_t>(mix(g, dg)) << 8 | static_cast<uint32_t>(mix(b, db));
}

uint64_t fnv(const void* p, size_t n, uint64_t h = 1469598103934665603ull) {
    const unsigned char* b = static_cast<const unsigned char*>(p);
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

void boxBlur(std::vector<float>& a, int w, int h, int r) {
    if (r < 1) return;
    std::vector<float> t(a.size());
    const float inv = 1.0f / static_cast<float>(2 * r + 1);
    for (int y = 0; y < h; y++) {          // horizontal
        const float* s = &a[static_cast<size_t>(y) * w];
        float* d = &t[static_cast<size_t>(y) * w];
        float acc = 0;
        for (int x = -r; x <= r; x++) acc += (x >= 0 && x < w) ? s[x] : 0.0f;
        for (int x = 0; x < w; x++) {
            d[x] = acc * inv;
            const int add = x + r + 1, sub = x - r;
            acc += (add < w ? s[add] : 0.0f) - (sub >= 0 ? s[sub] : 0.0f);
        }
    }
    for (int x = 0; x < w; x++) {          // vertical
        float acc = 0;
        for (int y = -r; y <= r; y++) acc += (y >= 0 && y < h) ? t[static_cast<size_t>(y) * w + x] : 0.0f;
        for (int y = 0; y < h; y++) {
            a[static_cast<size_t>(y) * w + x] = acc * inv;
            const int add = y + r + 1, sub = y - r;
            acc += (add < h ? t[static_cast<size_t>(add) * w + x] : 0.0f) - (sub >= 0 ? t[static_cast<size_t>(sub) * w + x] : 0.0f);
        }
    }
}

bool nearBlack(int r, int g, int b) { return r <= 6 && g <= 6 && b <= 6; }

uint32_t rgbOfIndex(const Palette& pal, uint8_t i) {
    const Color& c = pal.c[i];
    return static_cast<uint32_t>(c.r) << 16 | static_cast<uint32_t>(c.g) << 8 | c.b;
}

} // namespace

// ------------------------------------------------------------------------------------------------------------------ text with outline and halo

uint64_t nextSpriteRevision() {
    static uint64_t n = 0;
    return ++n;
}

Sprite buildTextSprite(const Engine::TextRunOut& r, int u, int cellX, int cellY) {
    Sprite s;
    if (!r.font || !r.font->hasFace() || r.text.empty() || u < 1) return s;
    const Font& f = *r.font;
    int width = 0;
    for (unsigned char c : r.text) width += f.advance(c);
    width *= u;
    const int ro = u, rh = 3 * u, pad = ro + rh + 2;
    const int w = width + 2 * pad, h = f.height() * u + 2 * pad;
    std::vector<uint8_t> cov(static_cast<size_t>(w) * h, 0);
    int pen = 0;
    for (unsigned char c : r.text) {
        const GlyphCoverage& g = f.glyphScaled(c, u);
        const int gx0 = pad + pen * u + g.left, gy0 = pad + f.ascent() * u - g.top;
        for (int v = 0; v < g.h; v++) {
            const int Y = gy0 + v;
            if (Y < 0 || Y >= h) continue;
            for (int x = 0; x < g.w; x++) {
                const int X = gx0 + x;
                if (X < 0 || X >= w) continue;
                uint8_t& d = cov[static_cast<size_t>(Y) * w + X];
                d = std::max(d, g.a[static_cast<size_t>(v) * g.w + x]);
            }
        }
        pen += f.advance(c);
    }
    // the outline: the coverage dilated by a disc of radius ro
    std::vector<uint8_t> dil(cov.size(), 0);
    std::vector<std::pair<int, int>> disc;
    for (int dy = -ro; dy <= ro; dy++)
        for (int dx = -ro; dx <= ro; dx++)
            if (dx * dx + dy * dy <= ro * ro + ro / 2) disc.emplace_back(dx, dy);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t m = 0;
            for (const auto& o : disc) {
                const int X = x + o.first, Y = y + o.second;
                if (X < 0 || Y < 0 || X >= w || Y >= h) continue;
                m = std::max(m, cov[static_cast<size_t>(Y) * w + X]);
            }
            dil[static_cast<size_t>(y) * w + x] = m;
        }
    // the halo: the dilated shape blurred (three box passes), a soft dark glow
    std::vector<float> halo(cov.size());
    for (size_t i = 0; i < halo.size(); i++) halo[i] = dil[i] / 255.0f;
    const int rb = std::max(1, rh / 2);
    boxBlur(halo, w, h, rb);
    boxBlur(halo, w, h, rb);
    boxBlur(halo, w, h, rb);
    s.rev = nextSpriteRevision();
    s.x = cellX - pad;
    s.y = cellY - pad;
    s.w = w;
    s.h = h;
    s.argb.assign(cov.size(), 0);
    for (size_t i = 0; i < s.argb.size(); i++) {
        const float hv = std::min(1.0f, halo[i] * 2.4f) * 0.8f;
        uint32_t px = 0;
        overPx(px, 0, 0, 0, static_cast<int>(hv * 255.0f + 0.5f));
        overPx(px, 8, 8, 10, dil[i]);
        overPx(px, r.r, r.g, r.b, cov[i]);
        s.argb[i] = px;
    }
    return s;
}

// ------------------------------------------------------------------------------------------------------------------ the scene

void OverlayScene::reset() { *this = OverlayScene(); }

void OverlayScene::buildAmbient(const SceneInput& in) {
    const int rows = ovl::kViewRows;
    uint64_t sig = fnv(&in.frame->px[static_cast<size_t>(ovl::kTopH) * Frame::W], static_cast<size_t>(rows) * Frame::W);
    sig = fnv(in.pal->c, sizeof(in.pal->c), sig);
    if (sig == ambientSig_ && !ambient_.empty()) return;
    ambientSig_ = sig;
    ambientBuilds_++;
    aw_ = ovl::kAmbientWidth;
    ah_ = 37;
    std::vector<float> ch[3];
    for (auto& c : ch) c.assign(static_cast<size_t>(aw_) * ah_, 0.0f);
    for (int j = 0; j < ah_; j++) {
        const int y0 = j * rows / ah_, y1 = std::max(y0 + 1, (j + 1) * rows / ah_);
        for (int i = 0; i < aw_; i++) {
            const int x0 = i * Frame::W / aw_, x1 = (i + 1) * Frame::W / aw_;
            double sr = 0, sg = 0, sb = 0;
            int n = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++) {
                    const Color& c = in.pal->c[in.frame->px[static_cast<size_t>(ovl::kTopH + y) * Frame::W + x]];
                    sr += c.r; sg += c.g; sb += c.b;
                    n++;
                }
            const size_t at = static_cast<size_t>(j) * aw_ + i;
            ch[0][at] = static_cast<float>(sr / n);
            ch[1][at] = static_cast<float>(sg / n);
            ch[2][at] = static_cast<float>(sb / n);
        }
    }
    // strongly blurred: two 3 x 3 box passes (edge pixels repeated)
    for (auto& c : ch)
        for (int pass = 0; pass < 2; pass++) {
            std::vector<float> t(c.size());
            for (int y = 0; y < ah_; y++)
                for (int x = 0; x < aw_; x++) {
                    float acc = 0;
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++)
                            acc += c[static_cast<size_t>(std::min(std::max(y + dy, 0), ah_ - 1)) * aw_ + std::min(std::max(x + dx, 0), aw_ - 1)];
                    t[static_cast<size_t>(y) * aw_ + x] = acc / 9.0f;
                }
            c.swap(t);
        }
    ambient_.assign(static_cast<size_t>(aw_) * ah_, 0);
    for (size_t i = 0; i < ambient_.size(); i++)
        ambient_[i] = static_cast<uint32_t>(std::lround(ch[0][i])) << 16 | static_cast<uint32_t>(std::lround(ch[1][i])) << 8 | static_cast<uint32_t>(std::lround(ch[2][i]));
}

void OverlayScene::buildStrip(const ovl::Layout& L, const SceneInput& in) {
    const int bw = ovl::kGridW, bh = ovl::kBarH, u = L.u;
    std::vector<uint32_t> rgb(static_cast<size_t>(bw) * bh);
    bool content = false;
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            const uint32_t c = rgbOfIndex(*in.pal, in.frame->px[static_cast<size_t>(ovl::kBarY + y) * Frame::W + x]);
            rgb[static_cast<size_t>(y) * bw + x] = c;
            if (!nearBlack(static_cast<int>(c >> 16 & 255), static_cast<int>(c >> 8 & 255), static_cast<int>(c & 255))) content = true;
        }
    if (!content) {        // the engine's rows are black: hidden (or no object); a sprite that is fading out stays
        if (in.frame && stripCount_ == 0) { strip_ = Sprite(); stripSig_ = 0; }
        return;
    }
    uint64_t sig = fnv(rgb.data(), rgb.size() * sizeof(uint32_t));
    sig = fnv(&u, sizeof u, sig);
    sig = fnv(&L.stripX, sizeof L.stripX, sig);
    sig = fnv(&L.stripY, sizeof L.stripY, sig);
    if (sig == stripSig_ && !strip_.empty()) return;
    stripSig_ = sig;
    stripBuilds_++;
    // the black around the icons (connected to the rim of the strip) is see-through; black inside an icon stays
    std::vector<uint8_t> clear(rgb.size(), 0);
    std::vector<int> stack;
    auto seed = [&](int x, int y) {
        const size_t i = static_cast<size_t>(y) * bw + x;
        const uint32_t c = rgb[i];
        if (!clear[i] && nearBlack(static_cast<int>(c >> 16 & 255), static_cast<int>(c >> 8 & 255), static_cast<int>(c & 255))) { clear[i] = 1; stack.push_back(static_cast<int>(i)); }
    };
    for (int x = 0; x < bw; x++) { seed(x, 0); seed(x, bh - 1); }
    for (int y = 0; y < bh; y++) { seed(0, y); seed(bw - 1, y); }
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        const int x = i % bw, y = i / bw;
        if (x > 0) seed(x - 1, y);
        if (x + 1 < bw) seed(x + 1, y);
        if (y > 0) seed(x, y - 1);
        if (y + 1 < bh) seed(x, y + 1);
    }
    strip_.rev = nextSpriteRevision();
    strip_.x = L.stripX;
    strip_.y = L.stripY;
    strip_.w = bw * u;
    strip_.h = bh * u;
    strip_.argb.assign(static_cast<size_t>(strip_.w) * strip_.h, 0);
    for (int y = 0; y < strip_.h; y++)
        for (int x = 0; x < strip_.w; x++) {
            const size_t i = static_cast<size_t>(y / u) * bw + static_cast<size_t>(x / u);
            if (!clear[i]) strip_.argb[static_cast<size_t>(y) * strip_.w + x] = 0xFF000000u | rgb[i];
        }
}

void OverlayScene::update(const ovl::Layout& L, const SceneInput& in) {
    const bool first = !haveNow_;
    double dt = 0;
    if (haveNow_) dt = std::min(1000.0, std::max(0.0, static_cast<double>(static_cast<int32_t>(in.nowMs - lastNow_))));
    lastNow_ = in.nowMs;
    haveNow_ = true;
    const bool layoutChanged = L.u != lastU_ || L.W != lastW_ || L.H != lastH_;
    lastU_ = L.u; lastW_ = L.W; lastH_ = L.H;

    // the top bar's text: the words (status words, y 0x12) and the message line (y 0x24); a word drawn in the black of the bar is "not shown"
    std::vector<const Engine::TextRunOut*> words;
    const Engine::TextRunOut* msg = nullptr;
    if (in.runs)
        for (const Engine::TextRunOut& r : *in.runs) {
            if (r.layer != "topbar" || r.text.empty() || !r.font || !r.font->hasFace()) continue;
            if (std::max({static_cast<int>(r.r), static_cast<int>(r.g), static_cast<int>(r.b)}) < 40) continue;
            if (r.y < ovl::kMessageY) words.push_back(&r);
            else msg = &r;
        }
    uint64_t wsig = fnv(&L.u, sizeof L.u), msig = wsig;
    wsig = fnv(&L.W, sizeof L.W, wsig);
    auto mixRun = [&](uint64_t h, const Engine::TextRunOut& r) {
        h = fnv(r.text.data(), r.text.size(), h);
        const int v[5] = {r.x, r.y, r.r, r.g, r.b};
        return fnv(v, sizeof v, h);
    };
    for (const Engine::TextRunOut* r : words) wsig = mixRun(wsig, *r);
    if (wsig != wordsSig_ || layoutChanged) {
        wordsSig_ = wsig;
        words_.clear();
        for (const Engine::TextRunOut* r : words) {
            Word w;
            w.sprite = buildTextSprite(*r, L.u, static_cast<int>(std::lround(L.centreX() + (r->x - ovl::kGridW / 2) * L.u)), 4 * L.u);
            words_.push_back(std::move(w));
            textBuilds_++;
        }
    }
    msig = fnv(&L.W, sizeof L.W, msig);
    msig = fnv(&L.H, sizeof L.H, msig);
    if (msg) msig = mixRun(msig, *msg);
    if (msg && (msig != msgSig_ || layoutChanged)) {
        msgSig_ = msig;
        message_ = buildTextSprite(*msg, L.u, static_cast<int>(std::lround(L.centreX() + (msg->x - ovl::kGridW / 2) * L.u)),
                                   L.stripY - 2 * L.u - msg->font->height() * L.u);
        textBuilds_++;
    }
    msgShown_ = msg != nullptr;
    msgFade_.step(msgShown_, dt, ovl::kMessageFadeInMs, ovl::kMessageFadeOutMs);
    if (first) msgFade_.v = msgShown_ ? 1.0 : 0.0;

    // the strip
    stripCount_ = in.barObjects;
    buildStrip(L, in);
    stripWant_ = ovl::stripWanted(in.barMode, in.flashing, in.pointerInBar) && !strip_.empty();
    stripFade_.step(stripWant_, dt, ovl::kStripFadeInMs, ovl::kStripFadeOutMs);
    if (first) stripFade_.v = stripWant_ ? 1.0 : 0.0;

    buildAmbient(in);
}

bool overlayCapturedMove(Engine& e, const ovl::Layout& L, ovl::MotionCarry& carry, double mx, double my, const std::function<void(int&, int&)>& adjust) {
    const double sc = e.barMode() ? L.u : L.k;
    int dx = carry.apply(mx, sc, 0), dy = carry.apply(my, sc, 1);
    if (!dx && !dy) return false;
    if (!e.barMode() && adjust) adjust(dx, dy);
    int px = 0, py = 0;
    e.pointerPos(px, py);
    e.mouseMove(px + dx, py + dy, dx, dy);
    return true;
}

AmbientCrop ambientCrop(int W, int H) {
    AmbientCrop c;
    const double sa = static_cast<double>(ovl::kGridW) / ovl::kViewRows, wa = static_cast<double>(W) / std::max(1, H);
    if (wa >= sa) {
        c.vspan = sa / wa;
        c.v0 = (1 - c.vspan) * 0.5;
    } else {
        c.uspan = wa / sa;
        c.u0 = (1 - c.uspan) * 0.5;
    }
    return c;
}

// ------------------------------------------------------------------------------------------------------------------ the CPU composition

namespace {

struct Rgb { double r, g, b; };

// the ambient picture at normalised position (u, v): bilinear, edge pixels repeated
Rgb sampleAmbient(const std::vector<uint32_t>& a, int aw, int ah, double u, double v) {
    const double sx = std::min(std::max(u * aw - 0.5, 0.0), aw - 1.0), sy = std::min(std::max(v * ah - 0.5, 0.0), ah - 1.0);
    const int x0 = static_cast<int>(sx), y0 = static_cast<int>(sy), x1 = std::min(x0 + 1, aw - 1), y1 = std::min(y0 + 1, ah - 1);
    const double fx = sx - x0, fy = sy - y0;
    auto px = [&](int x, int y, int sh) { return static_cast<double>((a[static_cast<size_t>(y) * aw + x] >> sh) & 255); };
    auto ch = [&](int sh) {
        return (px(x0, y0, sh) * (1 - fx) + px(x1, y0, sh) * fx) * (1 - fy) + (px(x0, y1, sh) * (1 - fx) + px(x1, y1, sh) * fx) * fy;
    };
    return Rgb{ch(16), ch(8), ch(0)};
}

inline uint32_t pack(double r, double g, double b) {
    auto q = [](double v) { return static_cast<uint32_t>(std::min(255.0, std::max(0.0, std::floor(v + 0.5)))); };
    return q(r) << 16 | q(g) << 8 | q(b);
}

void blendSprite(std::vector<uint32_t>& rgb, int W, int H, const Sprite& s, double alpha) {
    if (s.empty() || alpha <= 0) return;
    for (int y = 0; y < s.h; y++) {
        const int Y = s.y + y;
        if (Y < 0 || Y >= H) continue;
        for (int x = 0; x < s.w; x++) {
            const int X = s.x + x;
            if (X < 0 || X >= W) continue;
            const uint32_t p = s.argb[static_cast<size_t>(y) * s.w + x];
            const double a = (p >> 24) / 255.0 * alpha;
            if (a <= 0) continue;
            uint32_t& d = rgb[static_cast<size_t>(Y) * W + X];
            const double r = ((p >> 16) & 255) * a + ((d >> 16) & 255) * (1 - a), g = ((p >> 8) & 255) * a + ((d >> 8) & 255) * (1 - a),
                         b = (p & 255) * a + (d & 255) * (1 - a);
            d = pack(r, g, b);
        }
    }
}

} // namespace

void composeOverlay(const ovl::Layout& L, const OverlayScene& sc, const OverlayView& v, std::vector<uint32_t>& rgb) {
    const int W = L.W, H = L.H;
    rgb.assign(static_cast<size_t>(W) * H, 0);
    const double k = L.k;
    const double pL = L.viewX - v.showL * k, pR = L.viewX + (ovl::kGridW + v.showR) * k, pT = L.viewY, pB = L.viewY + ovl::kViewRows * k;
    const bool whole = L.whole();

    // the ambient fill beside the picture (not in the 4:3 layout: black stays above and below there)
    if (!L.fourThree && (pL > 0.5 || pR < W - 0.5) && !sc.ambient().empty()) {
        const AmbientCrop cr = ambientCrop(W, H);
        const double g = 24.0 * L.u;
        for (int Y = 0; Y < H; Y++)
            for (int X = 0; X < W; X++) {
                const double cx = X + 0.5;
                double d;
                if (cx < pL) d = pL - cx;
                else if (cx >= pR) d = cx - pR;
                else continue;
                const double b = ovl::ambientBrightness(d, g);
                const Rgb c = sampleAmbient(sc.ambient(), sc.ambientW(), sc.ambientH(), cr.u0 + (cx / W) * cr.uspan, cr.v0 + ((Y + 0.5) / H) * cr.vspan);
                rgb[static_cast<size_t>(Y) * W + X] = pack(c.r * b, c.g * b, c.b * b);
            }
    }

    const int X0 = std::max(0, static_cast<int>(std::ceil(pL - 0.5))), X1 = std::min(W, static_cast<int>(std::ceil(pR - 0.5)));
    const int Y0 = std::max(0, static_cast<int>(std::ceil(pT - 0.5))), Y1 = std::min(H, static_cast<int>(std::ceil(pB - 0.5)));

    // the view picture as 640 x 368 RGB (the frame through the palette)
    std::vector<uint32_t> vrgb(static_cast<size_t>(ovl::kGridW) * ovl::kViewRows);
    for (int y = 0; y < ovl::kViewRows; y++)
        for (int x = 0; x < ovl::kGridW; x++) vrgb[static_cast<size_t>(y) * ovl::kGridW + x] = rgbOfIndex(*v.pal, v.frame->px[static_cast<size_t>(ovl::kTopH + y) * Frame::W + x]);
    auto sampleRgb = [&](const std::vector<uint32_t>& img, int iw, int ih, double sx, double sy, bool linear, int sh) -> double {
        if (!linear) {
            const int x = std::min(std::max(static_cast<int>(std::floor(sx + 0.5)), 0), iw - 1), y = std::min(std::max(static_cast<int>(std::floor(sy + 0.5)), 0), ih - 1);
            return static_cast<double>((img[static_cast<size_t>(y) * iw + x] >> sh) & 255);
        }
        const double cx = std::min(std::max(sx, 0.0), iw - 1.0), cy = std::min(std::max(sy, 0.0), ih - 1.0);
        const int x0 = static_cast<int>(cx), y0 = static_cast<int>(cy), x1 = std::min(x0 + 1, iw - 1), y1 = std::min(y0 + 1, ih - 1);
        const double fx = cx - x0, fy = cy - y0;
        auto at = [&](int x, int y) { return static_cast<double>((img[static_cast<size_t>(y) * iw + x] >> sh) & 255); };
        return (at(x0, y0) * (1 - fx) + at(x1, y0) * fx) * (1 - fy) + (at(x0, y1) * (1 - fx) + at(x1, y1) * fx) * fy;
    };

    if (!v.pano) {      // a flat page: the 640 x 368 view scaled (nearest at a whole number, else bilinear)
        for (int Y = Y0; Y < Y1; Y++)
            for (int X = X0; X < X1; X++) {
                const double sx = (X + 0.5 - L.viewX) / k - 0.5, sy = (Y + 0.5 - L.viewY) / k - 0.5;
                rgb[static_cast<size_t>(Y) * W + X] = pack(sampleRgb(vrgb, ovl::kGridW, ovl::kViewRows, sx, sy, !whole, 16), sampleRgb(vrgb, ovl::kGridW, ovl::kViewRows, sx, sy, !whole, 8),
                                                         sampleRgb(vrgb, ovl::kGridW, ovl::kViewRows, sx, sy, !whole, 0));
            }
    } else {            // the smooth panorama, then what the engine drew over it
        PanoMesh mesh;
        const int cell = panoCellFor(k);
        buildPanoMeshRange(v.cam, cell, -((v.showL + cell - 1) / cell) * cell, ovl::kGridW + ((v.showR + cell - 1) / cell) * cell, mesh);
        std::vector<uint32_t> pb(static_cast<size_t>(W) * H, 0xFFFFFFFFu);
        rasterizePanoAffine(mesh, *v.tex, k, v.linear, W, H, L.viewX, L.viewY, X0, X1, Y0, Y1, pb, v.texScale);
        const int ex = std::max(v.showL, v.showR), ow = ovl::kGridW + 2 * ex;
        std::vector<uint32_t> ov(static_cast<size_t>(ow) * ovl::kViewRows, 0);      // 0xAARRGGBB: the frame where it was drawn over the panorama, the rain at the sides
        std::vector<uint8_t> mask;
        panoOverlayMask(*v.frame, v.pf->view, v.pf->bare, mask);
        for (int y = 0; y < ovl::kViewRows; y++)
            for (int x = 0; x < ovl::kGridW; x++) {
                const size_t i = static_cast<size_t>(y) * ovl::kGridW + x;
                ov[static_cast<size_t>(y) * ow + ex + x] = (mask[i] || v.statusScreen ? 0xFF000000u : 0u) | vrgb[i];
            }
        if (v.rain && !v.rain->empty())
            for (int y = 0; y < ovl::kViewRows; y++) {
                for (int i = 0; i < v.showL; i++) {
                    const int r = wideSideRain(*v.rain, -1 - i, y);
                    if (r >= 0) ov[static_cast<size_t>(y) * ow + (ex - 1 - i)] = 0xFF000000u | rgbOfIndex(*v.pal, static_cast<uint8_t>(r));
                }
                for (int i = 0; i < v.showR; i++) {
                    const int r = wideSideRain(*v.rain, ovl::kGridW + i, y);
                    if (r >= 0) ov[static_cast<size_t>(y) * ow + (ex + ovl::kGridW + i)] = 0xFF000000u | rgbOfIndex(*v.pal, static_cast<uint8_t>(r));
                }
            }
        for (int Y = Y0; Y < Y1; Y++)
            for (int X = X0; X < X1; X++) {
                uint32_t base = pb[static_cast<size_t>(Y) * W + X];
                if (base >> 24) base = rgb[static_cast<size_t>(Y) * W + X];     // not covered by the mesh (does not happen): what is there
                if (v.statusScreen && (X + 0.5 < L.viewX || X + 0.5 >= L.viewX + ovl::kGridW * k))      // beside the status screen: dimmed
                    base = pack(((base >> 16) & 255) * kWideStatusSideBrightness, ((base >> 8) & 255) * kWideStatusSideBrightness, (base & 255) * kWideStatusSideBrightness);
                const double sx = (X + 0.5 - L.viewX) / k + ex - 0.5, sy = (Y + 0.5 - L.viewY) / k - 0.5;
                const double a = sampleRgb(ov, ow, ovl::kViewRows, sx, sy, !whole, 24) / 255.0;
                if (a <= 0) { rgb[static_cast<size_t>(Y) * W + X] = base; continue; }
                const double r = sampleRgb(ov, ow, ovl::kViewRows, sx, sy, !whole, 16), g = sampleRgb(ov, ow, ovl::kViewRows, sx, sy, !whole, 8),
                             b = sampleRgb(ov, ow, ovl::kViewRows, sx, sy, !whole, 0);
                rgb[static_cast<size_t>(Y) * W + X] = pack(r * a + ((base >> 16) & 255) * (1 - a), g * a + ((base >> 8) & 255) * (1 - a), b * a + (base & 255) * (1 - a));
            }
    }

    // the text of the view, sharp
    if (v.sharp && v.tf) {
        std::vector<uint32_t> t;
        int tw = 0, th = 0;
        const int ix = static_cast<int>(std::floor(L.viewX)), iy = static_cast<int>(std::floor(L.viewY));
        renderTextOverlayScaled(*v.tf, k, ovl::kTopH, ovl::kViewRows, L.viewX - ix, L.viewY - iy, "topbar", t, tw, th);
        Sprite s;
        s.x = ix; s.y = iy; s.w = tw; s.h = th;
        s.argb.swap(t);
        // only inside the picture rows
        for (int y = 0; y < s.h; y++) {
            const int Y = s.y + y;
            if (Y < Y0 || Y >= Y1)
                for (int x = 0; x < s.w; x++) s.argb[static_cast<size_t>(y) * s.w + x] = 0;
        }
        blendSprite(rgb, W, H, s, 1.0);
    }

    // the overlays: the dark gradient behind the strip, the strip, the message line, the status words
    if (sc.stripAlpha() > 0 && !sc.strip().empty()) {
        const int top = H - static_cast<int>(std::lround(ovl::kStripGradientHeights * L.stripH));
        for (int Y = std::max(0, top); Y < H; Y++) {
            const double a = ovl::kStripGradientAlpha * (Y + 0.5 - top) / static_cast<double>(H - top) * sc.stripAlpha();
            for (int X = 0; X < W; X++) {
                uint32_t& d = rgb[static_cast<size_t>(Y) * W + X];
                d = pack(((d >> 16) & 255) * (1 - a), ((d >> 8) & 255) * (1 - a), (d & 255) * (1 - a));
            }
        }
        blendSprite(rgb, W, H, sc.strip(), sc.stripAlpha());
    }
    blendSprite(rgb, W, H, sc.message(), sc.messageAlpha());
    for (const OverlayScene::Word& w : sc.words()) blendSprite(rgb, W, H, w.sprite, 1.0);
}

} // namespace biosys
