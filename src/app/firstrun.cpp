// The first-run screen's window (see firstrun.h). The state machine is firstrunlogic.h; this file is the SDL side: events, the import
// worker thread, drawing. Everything is drawn at the window's pixel resolution from a fixed 960 x 600 design, scaled uniformly.
#include "app/firstrun.h"
#include "app/firstrunlogic.h"
#include "engine/font.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>

#ifndef BIOSYS_VERSION
#define BIOSYS_VERSION "dev"
#endif

namespace biosys {

const char* biosysVersion() { return BIOSYS_VERSION; }

namespace {

using namespace firstrun;

constexpr const char* kDownloadUrl = "https://www.myabandonware.com/game/biosys-f2f#downloads";

struct Col {
    uint8_t r, g, b, a;
};
constexpr Col kGreen{0x8f, 0xe0, 0x7a, 255}, kGreenHover{0xa8, 0xee, 0x96, 255}, kDark{0x0a, 0x2b, 0x27, 255}, kAmber{0xff, 0xcf, 0x70, 255};

// ---- text: the engine's FreeType-backed font code on a system font; SDL's debug font (2x) when none loads ----
const char* const kFontCandidates[] = {"/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/Supplemental/Arial.ttf", "/System/Library/Fonts/Geneva.ttf",
                                       "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};

// UTF-8 -> the font's ANSI (Windows-1252) bytes; what the font code can draw.
std::string toAnsi(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = c;
        size_t n = 1;
        if (c >= 0xF0) { cp = c & 7; n = 4; }
        else if (c >= 0xE0) { cp = c & 15; n = 3; }
        else if (c >= 0xC0) { cp = c & 31; n = 2; }
        else if (c >= 0x80) { cp = 0xFFFD; }
        for (size_t k = 1; k < n; k++) cp = (cp << 6) | (i + k < s.size() ? (static_cast<unsigned char>(s[i + k]) & 63) : 0);
        i += n;
        switch (cp) {
        case 0x2018: out += '\x91'; break;
        case 0x2019: out += '\x92'; break;
        case 0x201C: out += '\x93'; break;
        case 0x201D: out += '\x94'; break;
        case 0x2026: out += '\x85'; break;
        case 0x2013: out += '\x96'; break;
        case 0x2014: out += '\x97'; break;
        default:
            if (cp < 0x100 && (cp < 0x80 || cp >= 0xA0)) out += static_cast<char>(cp);
            else out += '?';
        }
    }
    return out;
}

std::string toAscii(const std::string& s) {
    std::string out, a = toAnsi(s);
    for (char ch : a) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x80) out += ch;
        else if (c == 0x85) out += "...";
        else if (c == 0x93 || c == 0x94) out += '"';
        else if (c == 0x91 || c == 0x92) out += '\'';
        else if (c == 0xA9) out += "(c)";
        else out += '?';
    }
    return out;
}

struct TextItem {
    std::string text;           // UTF-8
    double x = 0, y = 0;        // design pixels; y is the baseline
    double px = 16;             // em size in design pixels
    Col col{255, 255, 255, 255};
    int align = 0;              // 0 left, 1 centre, 2 right
};

class Typeface {
public:
    explicit Typeface(bool forceDebugFont) {
        for (const char* c : kFontCandidates)
            if (!forceDebugFont && lib_.addFile(c)) break;
        if (!lib_.faces().empty()) {
            LogFont lf;
            lf.height = -kMeasurePpem;
            measure_ = lib_.fontShared(lib_.createFont(lf));
            if (measure_ && !measure_->hasFace()) measure_.reset();
        }
        if (measure_) makeRaster();
    }
    bool ok() const { return measure_ != nullptr; }
    std::string family() const { return measure_ ? measure_->family() : "debug font"; }

    // width of a UTF-8 string at em size px, in the same unit as px
    double width(const std::string& utf8, double px) const {
        if (!ok()) return static_cast<double>(toAscii(utf8).size()) * 8 * debugScale(px);
        double w = 0;
        for (unsigned char c : toAnsi(utf8)) w += measure_->advance(c);
        return w * px / kMeasurePpem;
    }
    static int debugScale(double px) { return px >= 30 ? 3 : px >= 16 ? 2 : 1; }

    // Greedy word wrap to maxW; at most maxLines lines (the last one is cut with "..." when the text goes on).
    std::vector<std::string> wrap(const std::string& text, double px, double maxW, size_t maxLines) const {
        std::vector<std::string> lines;
        std::string cur, word;
        auto flushWord = [&]() {
            if (word.empty()) return;
            const std::string tryLine = cur.empty() ? word : cur + " " + word;
            if (cur.empty() || width(tryLine, px) <= maxW) cur = tryLine;
            else { lines.push_back(cur); cur = word; }
            word.clear();
        };
        for (char c : text) {
            if (c == ' ' || c == '\n') flushWord();
            else word += c;
        }
        flushWord();
        if (!cur.empty()) lines.push_back(cur);
        if (lines.size() > maxLines) {
            lines.resize(maxLines);
            std::string& l = lines.back();
            while (!l.empty() && width(l + "\xE2\x80\xA6", px) > maxW) {
                size_t k = l.size() - 1;
                while (k > 0 && (static_cast<unsigned char>(l[k]) & 0xC0) == 0x80) k--;
                l.erase(k);
            }
            l += "\xE2\x80\xA6";
        }
        return lines;
    }

    // Blends the glyphs of one item into a straight-alpha ARGB buffer of pw x ph pixels. scale: design -> pixels.
    void draw(const TextItem& t, double scale, double offX, double offY, std::vector<uint32_t>& buf, int pw, int ph) {
        if (!ok()) return;
        ensureRaster(scale);
        const std::string s = toAnsi(t.text);
        double w = width(t.text, t.px);
        double penX = offX + (t.x - (t.align == 1 ? w / 2 : t.align == 2 ? w : 0)) * scale;
        const int baseY = static_cast<int>(std::lround(offY + t.y * scale));
        const double glyphScale = t.px * scale / kRasterPpem;
        for (unsigned char c : s) {
            const GlyphCoverage& g = raster_->glyphScaledF(c, glyphScale);
            const int gx = static_cast<int>(std::lround(penX)) + g.left, gy = baseY - g.top;
            for (int yy = 0; yy < g.h; yy++) {
                const int y = gy + yy;
                if (y < 0 || y >= ph) continue;
                for (int xx = 0; xx < g.w; xx++) {
                    const int x = gx + xx;
                    if (x < 0 || x >= pw) continue;
                    const int a = g.a[static_cast<size_t>(yy) * g.w + xx] * t.col.a / 255;
                    if (!a) continue;
                    uint32_t& d = buf[static_cast<size_t>(y) * pw + x];
                    const int da = static_cast<int>(d >> 24);
                    const int oa = a + da * (255 - a) / 255;
                    auto mix = [&](int sc, int dc) { return oa ? (sc * a * 255 + dc * da * (255 - a) + oa * 127) / (oa * 255) : 0; };
                    const int r = mix(t.col.r, (d >> 16) & 255), gg = mix(t.col.g, (d >> 8) & 255), b = mix(t.col.b, d & 255);
                    d = static_cast<uint32_t>(oa) << 24 | static_cast<uint32_t>(std::min(r, 255)) << 16 | static_cast<uint32_t>(std::min(gg, 255)) << 8 |
                        static_cast<uint32_t>(std::min(b, 255));
                }
            }
            penX += measure_->advance(c) * t.px * scale / kMeasurePpem;
        }
    }

private:
    static constexpr int kMeasurePpem = 128;    // advances: a large size, so that the integer advances are fine-grained
    static constexpr int kRasterPpem = 12;      // glyph shapes: scaled from a small size, so that the 1/64 steps of the scale are fine
    void makeRaster() {
        LogFont lf;
        lf.height = -kRasterPpem;
        raster_ = lib_.fontShared(lib_.createFont(lf));
    }
    void ensureRaster(double scale) {      // a new window scale starts with an empty glyph cache
        if (scale != rasterScale_) {
            makeRaster();
            rasterScale_ = scale;
        }
    }
    FontLibrary lib_;
    std::shared_ptr<const Font> measure_, raster_;
    double rasterScale_ = 0;
};

// ---- the import worker: one disc at a time on its own thread ----
struct Worker {
    std::thread th;
    std::mutex mu;
    install::Progress prog;
    bool haveProg = false;
    std::atomic<bool> cancel{false}, finished{false};
    bool cancelled = false;
    std::string error;
    install::DiscKind result = install::DiscKind::Unknown;
    DiscKind expected = DiscKind::Unknown;

    void start(const Job& job, const std::string& home) {
        expected = job.kind;
        th = std::thread([this, job, home] {
            try {
                result = install::importDisc(job.path, home, [this](const install::Progress& p) {
                    std::lock_guard<std::mutex> lk(mu);
                    prog = p;
                    haveProg = true;
                    return !cancel.load();
                });
            } catch (const install::Cancelled&) {
                cancelled = true;
            } catch (const std::exception& e) {
                error = e.what();
            } catch (...) {
                error = "unknown error";
            }
            finished = true;
        });
    }
    void stop() {
        cancel = true;
        if (th.joinable()) th.join();
    }
};

// The file dialog's callback may come on any thread: the paths wait here for the window's loop.
struct DialogInbox {
    std::mutex mu;
    std::vector<std::string> paths;
    std::string error;
    std::atomic<bool> open{false};
};
DialogInbox& inbox() {
    static DialogInbox b;      // lives to the end of the process: a dialog may outlive the window
    return b;
}
void SDLCALL onFilesChosen(void*, const char* const* list, int) {
    DialogInbox& b = inbox();
    std::lock_guard<std::mutex> lk(b.mu);
    if (!list) b.error = SDL_GetError();
    else
        for (; *list; list++) b.paths.push_back(*list);
    b.open = false;
}

// ---- drawing ----
class Screen {
public:
    Screen(SDL_Renderer* ren, Typeface& tf) : ren_(ren), tf_(tf) { SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND); }
    ~Screen() {
        if (layer_) SDL_DestroyTexture(layer_);
    }

    // hover: the button under the pointer. capture: the picture is written to shotFile before it is presented.
    void draw(const Logic& L, Hit hover, const std::string& shotFile) {
        SDL_GetRenderOutputSize(ren_, &pw_, &ph_);
        fit_ = fitDesign(pw_, ph_);
        items_.clear();
        background();
        content(L, hover);
        flushText();
        if (L.dragging()) {
            const float m = static_cast<float>(8 * fit_.scale);
            roundOutline(m, m, pw_ - 2 * m, ph_ - 2 * m, 14 * fit_.scale, 3 * fit_.scale, Col{255, 255, 255, 190});
        }
        if (!shotFile.empty()) save(shotFile);
        SDL_RenderPresent(ren_);
    }

private:
    double X(double x) const { return fit_.offX + x * fit_.scale; }
    double Y(double y) const { return fit_.offY + y * fit_.scale; }
    void setCol(const Col& c) { SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, c.a); }

    void background() {
        auto fc = [](int r, int g, int b) { return SDL_FColor{r / 255.f, g / 255.f, b / 255.f, 1.f}; };
        const SDL_FColor top = fc(0x14, 0x58, 0x4a), mid = fc(0x0a, 0x2b, 0x27), bot = fc(0x05, 0x11, 0x0f);
        const float W = static_cast<float>(pw_), H = static_cast<float>(ph_), m = H * 0.6f;
        SDL_Vertex v[6] = {{{0, 0}, top, {0, 0}}, {{W, 0}, top, {0, 0}}, {{0, m}, mid, {0, 0}}, {{W, m}, mid, {0, 0}}, {{0, H}, bot, {0, 0}}, {{W, H}, bot, {0, 0}}};
        const int idx[12] = {0, 1, 2, 1, 3, 2, 2, 3, 4, 3, 5, 4};
        SDL_RenderGeometry(ren_, nullptr, v, 6, idx, 12);
    }
    void fillRect(double x, double y, double w, double h, const Col& c) {
        setCol(c);
        SDL_FRect r{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)};
        SDL_RenderFillRect(ren_, &r);
    }
    // rounded shapes by scanlines (pixel units)
    static double inset(double row, double h, double r) {      // how far the outline of a rounded rectangle is indented at this pixel row
        const double d = row + 0.5 < r ? r - (row + 0.5) : (row + 0.5 > h - r ? row + 0.5 - (h - r) : 0);
        return d <= 0 || r <= 0 ? 0 : r - std::sqrt(std::max(0.0, r * r - d * d));
    }
    void roundFill(double x, double y, double w, double h, double r, const Col& c) {
        r = std::min(r, std::min(w, h) / 2);
        setCol(c);
        const int rows = static_cast<int>(std::lround(h));
        for (int i = 0; i < rows; i++) {
            const double in = inset(i, rows, r);
            SDL_FRect fr{static_cast<float>(x + in), static_cast<float>(y + i), static_cast<float>(w - 2 * in), 1.f};
            SDL_RenderFillRect(ren_, &fr);
        }
    }
    void roundOutline(double x, double y, double w, double h, double r, double t, const Col& c) {
        r = std::min(r, std::min(w, h) / 2);
        setCol(c);
        const int rows = static_cast<int>(std::lround(h)), ti = std::max(1, static_cast<int>(std::lround(t)));
        for (int i = 0; i < rows; i++) {
            const double in = inset(i, rows, r);
            const float ox0 = static_cast<float>(x + in), ox1 = static_cast<float>(x + w - in);
            if (i < ti || i >= rows - ti) {
                SDL_FRect fr{ox0, static_cast<float>(y + i), ox1 - ox0, 1.f};
                SDL_RenderFillRect(ren_, &fr);
                continue;
            }
            const double in2 = ti + inset(i - ti, rows - 2 * ti, std::max(0.0, r - ti));
            const float ix0 = static_cast<float>(x + std::max(in2, in + 1)), ix1 = static_cast<float>(x + w - std::max(in2, in + 1));
            SDL_FRect a{ox0, static_cast<float>(y + i), std::max(1.f, ix0 - ox0), 1.f}, b{ix1, static_cast<float>(y + i), std::max(1.f, ox1 - ix1), 1.f};
            SDL_RenderFillRect(ren_, &a);
            SDL_RenderFillRect(ren_, &b);
        }
    }
    void thickLine(double x0, double y0, double x1, double y1, double t, const Col& c) {
        const double dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0) return;
        const double nx = -dy / len * t / 2, ny = dx / len * t / 2;
        const SDL_FColor col{c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f};
        auto P = [](double x, double y) { return SDL_FPoint{static_cast<float>(x), static_cast<float>(y)}; };
        SDL_Vertex v[4] = {{P(x0 + nx, y0 + ny), col, {0, 0}}, {P(x0 - nx, y0 - ny), col, {0, 0}}, {P(x1 + nx, y1 + ny), col, {0, 0}}, {P(x1 - nx, y1 - ny), col, {0, 0}}};
        const int idx[6] = {0, 1, 2, 1, 3, 2};
        SDL_RenderGeometry(ren_, nullptr, v, 4, idx, 6);
    }
    void text(const std::string& s, double x, double y, double px, const Col& c, int align = 0) { items_.push_back(TextItem{s, x, y, px, c, align}); }
    void lines(const std::vector<std::string>& ls, double x, double y0, double lineH, double px, const Col& c, int align = 0) {
        for (size_t i = 0; i < ls.size(); i++) text(ls[i], x, y0 + static_cast<double>(i) * lineH, px, c, align);
    }

    void card(const Logic& L, const Rect& r, DiscKind k, const char* name) {
        const DiscView& v = L.view(k);
        const bool done = v.slot == Slot::Done;
        const double S = fit_.scale;
        roundFill(X(r.x), Y(r.y), r.w * S, r.h * S, 14 * S, Col{255, 255, 255, 16});
        if (done) roundOutline(X(r.x), Y(r.y), r.w * S, r.h * S, 14 * S, 2 * S, kGreen);
        else roundOutline(X(r.x), Y(r.y), r.w * S, r.h * S, 14 * S, 1.5 * S, Col{255, 255, 255, 46});
        text(name, r.x + 24, r.y + 40, 22, Col{238, 246, 240, 255});
        text(Logic::stateText(v), r.x + 24, r.y + 78, 17, done ? kGreen : Col{190, 214, 202, 255});
        const double bx = r.x + 24, by = r.y + 102, bw = r.w - 48, bh = 10;
        roundFill(X(bx), Y(by), bw * S, bh * S, 5 * S, Col{0, 0, 0, 96});
        const double f = Logic::barFraction(v);
        if (f > 0) roundFill(X(bx), Y(by), std::max(bh, bw * f) * S, bh * S, 5 * S, kGreen);
        if (done) {      // the tick: two lines
            const double tx = r.x + r.w - 52, ty = r.y + 26;
            thickLine(X(tx), Y(ty + 10), X(tx + 8), Y(ty + 19), 3.2 * S, kGreen);
            thickLine(X(tx + 8), Y(ty + 19), X(tx + 26), Y(ty), 3.2 * S, kGreen);
        }
    }

    void content(const Logic& L, Hit hover) {
        const double S = fit_.scale;
        text("Biosys Remaster", 480, 84, 46, Col{244, 250, 245, 255}, 1);
        text("One step before you can play: the game needs the two original CDs.", 480, 124, 19, Col{196, 224, 212, 255}, 1);
        // the two steps: text on the left (wrapped), the button on the right
        auto step = [&](const char* num, const std::string& t, const Rect& btn) {
            const std::vector<std::string> ls = tf_.wrap(t, 18, 430, 3);
            const double cy = btn.y + btn.h / 2.0, lh = 23, y0 = cy - (static_cast<double>(ls.size()) - 1) * lh / 2 + 6;
            text(num, 100, y0, 18, kGreen);
            lines(ls, 126, y0, lh, 18, Col{228, 240, 232, 255});
        };
        step("1", "Download \xE2\x80\x9CGame CD\xE2\x80\x9D and \xE2\x80\x9CSetup CD\xE2\x80\x9D from My Abandonware (about 820 MB together).", kDownloadButton);
        step("2", "Drop both downloaded files anywhere on this window. No need to unpack them.", kChooseButton);
        {   // primary button
            const Rect& b = kDownloadButton;
            roundFill(X(b.x), Y(b.y), b.w * S, b.h * S, 10 * S, hover == Hit::Download ? kGreenHover : kGreen);
            text("Open the download page", b.x + b.w / 2.0, b.y + b.h / 2.0 + 6, 18, kDark, 1);
        }
        {   // secondary button
            const Rect& b = kChooseButton;
            if (hover == Hit::Choose) roundFill(X(b.x), Y(b.y), b.w * S, b.h * S, 10 * S, Col{0x8f, 0xe0, 0x7a, 40});
            roundOutline(X(b.x), Y(b.y), b.w * S, b.h * S, 10 * S, 2 * S, kGreen);
            text("Choose files\xE2\x80\xA6", b.x + b.w / 2.0, b.y + b.h / 2.0 + 6, 18, Col{232, 246, 234, 255}, 1);
        }
        card(L, kSetupCard, DiscKind::SetupCd, "Setup CD");
        card(L, kGameCard, DiscKind::GameCd, "Game CD");
        const std::string msg = L.message();
        if (!msg.empty()) lines(tf_.wrap(msg, 17, kMessageArea.w, 3), kMessageArea.x, kMessageArea.y + 18, 24, 17, L.allSet() ? kGreen : kAmber);
        const Col dim{255, 255, 255, 153};
        text("Unofficial fan project, not affiliated with the original creators or rights holders. Biosys \xC2\xA9 1999 JumpStart Solutions / Take-Two Interactive.", 40, 572, 11.5, dim);
        text("This app contains nothing from the original game. Your files stay on this computer.", 40, 588, 11.5, dim);
        text(biosysVersion(), 920, 588, 11.5, dim, 2);
    }

    // The text as one transparent layer of the window's size, rebuilt only when it changes; the debug font draws directly.
    void flushText() {
        if (!tf_.ok()) {
            for (const TextItem& t : items_) {
                const int k = Typeface::debugScale(t.px);
                const double w = tf_.width(t.text, t.px);
                const double x = t.x - (t.align == 1 ? w / 2 : t.align == 2 ? w : 0);
                const double sc = k * fit_.scale;
                SDL_SetRenderScale(ren_, static_cast<float>(sc), static_cast<float>(sc));
                SDL_SetRenderDrawColor(ren_, t.col.r, t.col.g, t.col.b, t.col.a);
                SDL_RenderDebugText(ren_, static_cast<float>(X(x) / sc), static_cast<float>(Y(t.y - 7.0 * k) / sc), toAscii(t.text).c_str());
            }
            SDL_SetRenderScale(ren_, 1, 1);
            return;
        }
        uint64_t sig = 1469598103934665603ull;
        auto mixIn = [&](uint64_t v) { sig = (sig ^ v) * 1099511628211ull; };
        mixIn(static_cast<uint64_t>(pw_) << 20 | static_cast<uint64_t>(ph_));
        for (const TextItem& t : items_) {
            for (unsigned char c : t.text) mixIn(c);
            mixIn(static_cast<uint64_t>(std::lround(t.x * 8)));
            mixIn(static_cast<uint64_t>(std::lround(t.y * 8)));
            mixIn(static_cast<uint64_t>(std::lround(t.px * 8)));
            mixIn(static_cast<uint64_t>(t.col.r) << 24 | static_cast<uint64_t>(t.col.g) << 16 | static_cast<uint64_t>(t.col.b) << 8 | t.col.a);
            mixIn(static_cast<uint64_t>(t.align));
        }
        if (!layer_ || layerW_ != pw_ || layerH_ != ph_) {
            if (layer_) SDL_DestroyTexture(layer_);
            layer_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, pw_, ph_);
            if (layer_) {
                SDL_SetTextureBlendMode(layer_, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(layer_, SDL_SCALEMODE_NEAREST);
            }
            layerW_ = pw_;
            layerH_ = ph_;
            layerSig_ = 0;
        }
        if (!layer_) return;
        if (sig != layerSig_) {
            buf_.assign(static_cast<size_t>(pw_) * ph_, 0u);
            for (const TextItem& t : items_) tf_.draw(t, fit_.scale, fit_.offX, fit_.offY, buf_, pw_, ph_);
            SDL_UpdateTexture(layer_, nullptr, buf_.data(), pw_ * 4);
            layerSig_ = sig;
        }
        SDL_FRect dst{0, 0, static_cast<float>(pw_), static_cast<float>(ph_)};
        SDL_RenderTexture(ren_, layer_, nullptr, &dst);
    }

    void save(const std::string& file) {
        SDL_Surface* px = SDL_RenderReadPixels(ren_, nullptr);
        if (!px) return;
        if (SDL_Surface* conv = SDL_ConvertSurface(px, SDL_PIXELFORMAT_XRGB8888)) {
            if (!SDL_SaveBMP(conv, file.c_str())) std::fprintf(stderr, "biosys: cannot write %s: %s\n", file.c_str(), SDL_GetError());
            SDL_DestroySurface(conv);
        }
        SDL_DestroySurface(px);
    }

    SDL_Renderer* ren_;
    Typeface& tf_;
    int pw_ = 0, ph_ = 0;
    Fit fit_;
    std::vector<TextItem> items_;
    SDL_Texture* layer_ = nullptr;
    int layerW_ = 0, layerH_ = 0;
    uint64_t layerSig_ = 0;
    std::vector<uint32_t> buf_;
};

} // namespace

FirstRunResult runFirstRun(const FirstRunOptions& o) {
    if (!SDL_Init(SDL_INIT_VIDEO)) throw Error(std::string("SDL_Init failed: ") + SDL_GetError());
    SDL_Window* window = SDL_CreateWindow("Biosys Remaster", kDesignW, kDesignH, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_Renderer* ren = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!ren) {
        const std::string why = SDL_GetError();
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        throw Error("cannot open the window: " + why);
    }
    SDL_SetWindowMinimumSize(window, 720, 480);
    SDL_SetRenderVSync(ren, 1);

    FirstRunResult result = FirstRunResult::Quit;
    {
        std::unique_ptr<Typeface> tf(new Typeface(std::getenv("BIOSYS_FIRSTRUN_DEBUGFONT") != nullptr));      // the variable makes the fallback font visible
        std::unique_ptr<Screen> screen(new Screen(ren, *tf));
        Logic logic([](const std::string& p, std::string* why) { return install::identify(p, why); }, install::status(o.dataHome));
        Worker worker;
        bool workerRunning = false;
        SDL_Cursor* handCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
        bool handShown = false;
        Hit hover = Hit::None;
        Hit pressed = Hit::None;
        std::string lastLog;
        const Uint64 t0 = SDL_GetTicks();
        Uint64 allSetAt = 0;
        bool shot800 = o.shotFile.empty();
        size_t nextDrop = 0;
        bool running = true;
        std::vector<std::string> dropStrings = o.testDrops;      // the events point into these: not resized any more

        auto emitLog = [&] {
            if (!o.log) return;
            const std::string line = logic.logLine();
            if (line != lastLog) {
                std::printf("%s\n", line.c_str());
                std::fflush(stdout);
                lastLog = line;
            }
        };
        auto pointerHit = [&](float wx, float wy) {
            float rx = 0, ry = 0;
            SDL_RenderCoordinatesFromWindow(ren, wx, wy, &rx, &ry);
            int pw = 0, ph = 0;
            SDL_GetRenderOutputSize(ren, &pw, &ph);
            const Fit f = fitDesign(pw, ph);
            return hitTest(f.toDesignX(rx), f.toDesignY(ry));
        };
        auto chooseFiles = [&] {
            if (inbox().open.exchange(true)) return;
            static const SDL_DialogFileFilter filters[] = {{"Biosys CD images", "zip;img;iso;bin"}, {"All files", "*"}};
            const char* dl = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
            SDL_ShowOpenFileDialog(onFilesChosen, nullptr, window, filters, 2, dl, true);
        };

        emitLog();
        while (running) {
            const Uint64 now = SDL_GetTicks() - t0;
            while (nextDrop < dropStrings.size() && now >= 1000 + 300 * nextDrop) {
                SDL_Event ev;
                SDL_zero(ev);
                ev.type = SDL_EVENT_DROP_FILE;
                ev.drop.windowID = SDL_GetWindowID(window);
                ev.drop.data = dropStrings[nextDrop].c_str();
                SDL_PushEvent(&ev);
                nextDrop++;
            }
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                switch (ev.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED: running = false; break;
                case SDL_EVENT_KEY_DOWN:
                    if (ev.key.key == SDLK_ESCAPE || (ev.key.key == SDLK_Q && (ev.key.mod & SDL_KMOD_GUI))) running = false;
                    break;
                case SDL_EVENT_DROP_BEGIN: logic.setDragging(true); break;
                case SDL_EVENT_DROP_COMPLETE: logic.setDragging(false); break;
                case SDL_EVENT_DROP_FILE:
                    if (ev.drop.data) logic.drop(ev.drop.data);
                    break;
                case SDL_EVENT_MOUSE_MOTION: hover = pointerHit(ev.motion.x, ev.motion.y); break;
                case SDL_EVENT_WINDOW_MOUSE_LEAVE: hover = Hit::None; break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (ev.button.button == SDL_BUTTON_LEFT) pressed = pointerHit(ev.button.x, ev.button.y);
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (ev.button.button == SDL_BUTTON_LEFT) {
                        const Hit h = pointerHit(ev.button.x, ev.button.y);
                        if (h != Hit::None && h == pressed) {
                            if (h == Hit::Download) {
                                if (!SDL_OpenURL(kDownloadUrl)) logic.note(std::string("The web browser could not be opened. Please open ") + kDownloadUrl + " yourself.");
                            } else {
                                chooseFiles();
                            }
                        }
                        pressed = Hit::None;
                    }
                    break;
                default: break;
                }
            }
            {   // files chosen in the dialog
                std::vector<std::string> got;
                std::string err;
                {
                    std::lock_guard<std::mutex> lk(inbox().mu);
                    got.swap(inbox().paths);
                    err.swap(inbox().error);
                }
                for (const std::string& p : got) logic.drop(p);
                if (!err.empty()) logic.note("The file dialog could not be opened: " + err);
            }
            if (!!handCursor && (hover != Hit::None) != handShown) {
                SDL_SetCursor(hover != Hit::None ? handCursor : SDL_GetDefaultCursor());
                handShown = hover != Hit::None;
            }
            // the import worker
            if (workerRunning) {
                {
                    std::lock_guard<std::mutex> lk(worker.mu);
                    if (worker.haveProg) logic.progress(worker.expected, worker.prog.step, worker.prog.done, worker.prog.total);
                }
                if (worker.finished) {
                    worker.stop();
                    workerRunning = false;
                    if (worker.cancelled) logic.cancelled(worker.expected);
                    else if (!worker.error.empty()) logic.failed(worker.expected, worker.error);
                    else if (worker.result != worker.expected) logic.failed(worker.expected, "this is not the " + std::string(install::discName(worker.expected)));
                    else logic.finished(worker.expected);
                }
            }
            if (!workerRunning) {
                Job job;
                if (logic.takeJob(job)) {
                    worker.cancel = false;
                    worker.finished = false;
                    worker.cancelled = false;
                    worker.haveProg = false;
                    worker.error.clear();
                    worker.start(job, o.dataHome);
                    workerRunning = true;
                }
            }
            if (!allSetAt && logic.allSet() && install::status(o.dataHome).complete()) allSetAt = now ? now : 1;
            emitLog();
            if (o.quitAfterMs && now >= o.quitAfterMs) running = false;
            std::string shotNow;
            if (!shot800 && now >= 800) {
                shotNow = o.shotFile;
                shot800 = true;
            }
            const bool closing = !running || (allSetAt && now >= allSetAt + 1000);
            if (closing && !o.shotFile.empty()) shotNow = o.shotFile;      // the picture of the last state
            screen->draw(logic, hover, shotNow);
            if (closing) {
                if (running) result = FirstRunResult::Ready;
                running = false;
                break;
            }
            SDL_Delay(12);
        }
        if (workerRunning) worker.stop();      // the player quit during an import: the importer cancels and removes what it wrote
        emitLog();
        screen.reset();
        if (handCursor) SDL_DestroyCursor(handCursor);
    }
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}

} // namespace biosys
