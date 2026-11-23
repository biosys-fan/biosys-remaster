#include "app/settingsmenu_draw.h"
#include <algorithm>

namespace biosys {

using namespace smenu;

MenuFonts::MenuFonts(const DataFiles& files) {
    const std::string ttf = files.findData("fonts\\jon.ttf");
    if (!ttf.empty()) lib_.addFile(ttf);
    auto make = [&](int cell) {
        LogFont lf;
        lf.height = cell;
        lf.weight = 400;
        lf.charSet = 1;
        lf.quality = 2;
        lf.faceName = "systemfont";
        return lib_.font(lib_.createFont(lf));
    };
    title_ = make(26);
    body_ = make(18);
    small_ = make(16);
}

namespace {

struct Col {
    int r, g, b, a;
};
constexpr Col kPanel{12, 20, 16, 238}, kBorder{118, 160, 128, 255}, kText{226, 236, 222, 255}, kDimText{150, 168, 150, 255}, kAccent{242, 202, 92, 255};
constexpr Col kTabOn{50, 84, 62, 255}, kTabOff{22, 34, 28, 255}, kBtn{36, 58, 46, 255}, kHot{60, 94, 72, 255}, kRowSel{34, 58, 44, 255},
    kRowHover{24, 40, 32, 255}, kGroove{66, 86, 72, 255}, kFill{150, 190, 120, 255};

inline void blend(uint32_t& dst, const Col& c) {
    const int a = c.a;
    if (a <= 0) return;
    const int da = static_cast<int>(dst >> 24);
    if (a == 255 || da == 0) {
        dst = static_cast<uint32_t>(a) << 24 | static_cast<uint32_t>(c.r) << 16 | static_cast<uint32_t>(c.g) << 8 | static_cast<uint32_t>(c.b);
        return;
    }
    const int oa = a + da * (255 - a) / 255;
    auto mix = [&](int s, int d) { return (s * a * 255 + d * da * (255 - a) + (oa * 255) / 2) / (oa * 255); };
    const int dr = static_cast<int>((dst >> 16) & 255), dg = static_cast<int>((dst >> 8) & 255), db = static_cast<int>(dst & 255);
    dst = static_cast<uint32_t>(oa) << 24 | static_cast<uint32_t>(mix(c.r, dr)) << 16 | static_cast<uint32_t>(mix(c.g, dg)) << 8 | static_cast<uint32_t>(mix(c.b, db));
}

struct Canvas2 {
    std::vector<uint32_t>& px;
    int k, W, H;

    void rect(int gx, int gy, int gw, int gh, const Col& c) {
        const int x0 = std::max(0, gx * k), y0 = std::max(0, gy * k), x1 = std::min(W, (gx + gw) * k), y1 = std::min(H, (gy + gh) * k);
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) blend(px[static_cast<size_t>(y) * W + x], c);
    }
    void frame(int gx, int gy, int gw, int gh, int t, const Col& c) {
        rect(gx, gy, gw, t, c);
        rect(gx, gy + gh - t, gw, t, c);
        rect(gx, gy + t, t, gh - 2 * t, c);
        rect(gx + gw - t, gy + t, t, gh - 2 * t, c);
    }
    // a triangle pointing left or right, centred in the grid rectangle
    void arrow(const R& r, bool left, const Col& c) {
        const int wpx = 7 * k, hpx = 6 * k;
        const int x0 = (r.x + r.w / 2) * k - wpx / 2, cy = (r.y + r.h / 2) * k;
        for (int u = 0; u < wpx; u++) {
            const int t = left ? u : wpx - 1 - u;
            const int half = hpx * t / wpx;
            for (int dy = -half; dy <= half; dy++) {
                const int X = x0 + u, Y = cy + dy;
                if (X >= 0 && X < W && Y >= 0 && Y < H) blend(px[static_cast<size_t>(Y) * W + X], c);
            }
        }
    }
    void disc(int gcx, int gcy, int gr, const Col& c) {
        const int cx = gcx * k, cy = gcy * k, rr = gr * k;
        for (int y = -rr; y <= rr; y++)
            for (int x = -rr; x <= rr; x++)
                if (x * x + y * y <= rr * rr) {
                    const int X = cx + x, Y = cy + y;
                    if (X >= 0 && X < W && Y >= 0 && Y < H) blend(px[static_cast<size_t>(Y) * W + X], c);
                }
    }
    void ring(int gcx, int gcy, int gr, const Col& c) {
        const int cx = gcx * k, cy = gcy * k, ro = gr * k, ri = (gr - 1) * k;
        for (int y = -ro; y <= ro; y++)
            for (int x = -ro; x <= ro; x++) {
                const int d = x * x + y * y;
                if (d <= ro * ro && d >= ri * ri) {
                    const int X = cx + x, Y = cy + y;
                    if (X >= 0 && X < W && Y >= 0 && Y < H) blend(px[static_cast<size_t>(Y) * W + X], c);
                }
            }
    }
    int width(const Font& f, const std::string& s) const { return f.textWidth(s.data(), static_cast<int>(s.size())); }
    // `gy`: the top of the font's cell on the grid; align 0 left, 1 centre, 2 right at gx
    void text(const Font& f, int gx, int gy, const std::string& s, const Col& c, int align = 0) {
        if (!f.hasFace()) return;
        const int w = width(f, s);
        int pen = align == 1 ? gx - w / 2 : align == 2 ? gx - w : gx;
        const int baseline = (gy + f.ascent()) * k;
        for (unsigned char ch : s) {
            const GlyphCoverage& g = f.glyphScaled(ch, k);
            const int x0 = pen * k + g.left, y0 = baseline - g.top;
            for (int v = 0; v < g.h; v++) {
                const int Y = y0 + v;
                if (Y < 0 || Y >= H) continue;
                for (int u = 0; u < g.w; u++) {
                    const int X = x0 + u;
                    if (X < 0 || X >= W) continue;
                    const int a = g.a[static_cast<size_t>(v) * g.w + u];
                    if (a) blend(px[static_cast<size_t>(Y) * W + X], Col{c.r, c.g, c.b, a * c.a / 255});
                }
            }
            pen += f.advance(ch);
        }
    }
};

void drawSettings(Canvas2& cv, const Controller& c, const MenuFonts& F) {
    const Model& m = c.model;
    const Font &title = *F.title(), &body = *F.body(), &small = *F.small();
    cv.rect(kPX, kPY, kPW, kPH, kPanel);
    cv.frame(kPX, kPY, kPW, kPH, 2, kBorder);
    cv.text(title, kPX + 18, kPY + 8, "Settings", kText);
    {
        const R r = closeRect();
        cv.rect(r.x, r.y, r.w, r.h, c.hoverButton() == 1 ? kHot : kBtn);
        cv.frame(r.x, r.y, r.w, r.h, 1, kBorder);
        cv.text(body, r.x + r.w / 2, r.y + (r.h - body.height()) / 2, "Close", kText, 1);
    }
    for (int t = 0; t < kTabs; t++) {
        const R r = tabRect(t);
        const bool on = t == m.tab();
        cv.rect(r.x, r.y, r.w, r.h, on ? kTabOn : c.hoverTab() == t ? kBtn : kTabOff);
        cv.frame(r.x, r.y, r.w, r.h, 1, on ? kAccent : kBorder);
        cv.text(body, r.x + r.w / 2, r.y + (r.h - body.height()) / 2, tabName(t), on ? kText : kDimText, 1);
    }
    cv.rect(kPX + 8, kRowY0 - 6, kPW - 16, 1, kBorder);
    const std::vector<int> rows = m.rows(m.tab());
    for (int r = 0; r < static_cast<int>(rows.size()) && r < kMaxRows; r++) {
        const Item& it = m.items()[static_cast<size_t>(rows[static_cast<size_t>(r)])];
        const R rr = rowRect(r);
        const bool sel = r == m.row();
        if (sel) cv.rect(rr.x, rr.y, rr.w, rr.h, kRowSel);
        else if (c.hoverRow() == r) cv.rect(rr.x, rr.y, rr.w, rr.h, kRowHover);
        if (sel) cv.rect(rr.x, rr.y + 2, 3, rr.h - 4, kAccent);
        const int ty = rr.y + (rr.h - body.height()) / 2;
        cv.text(body, kLabelX, ty, it.label, it.kind == Kind::Info ? kDimText : kText);
        if (it.kind == Kind::Info) {
            cv.text(body, kValX1, ty, Model::valueText(it), kAccent, 2);
        } else if (it.kind == Kind::Choice) {
            const bool many = it.choices.size() > 1;
            cv.arrow(arrowLRect(r), true, many ? kText : kDimText);
            cv.arrow(arrowRRect(r), false, many ? kText : kDimText);
            cv.text(body, (kValX0 + kValX1) / 2, ty, Model::valueText(it), kAccent, 1);
        } else {
            const R t = trackRect(r);
            const int gy = t.y + t.h / 2;
            cv.rect(t.x, gy - 2, t.w, 4, kGroove);
            const double f = it.hi > it.lo ? static_cast<double>(it.value - it.lo) / (it.hi - it.lo) : 0.0;
            const int kx = t.x + static_cast<int>(f * (t.w - 1) + 0.5);
            cv.rect(t.x, gy - 2, std::max(0, kx - t.x), 4, kFill);
            cv.rect(kx - 3, gy - 8, 7, 16, kAccent);
            cv.text(body, kRangeTextX, ty, Model::valueText(it), kAccent);
        }
    }
    // the explanation of the selected row, and when the change applies
    cv.rect(kPX + 8, kFootY - 4, kPW - 16, 1, kBorder);
    if (const Item* s = m.selected()) {
        const std::vector<std::string> lines = wrapMenuText(small, s->help, kPW - 40);
        for (size_t i = 0; i < lines.size() && i < 3; i++) cv.text(small, kPX + 20, kFootY + 2 + static_cast<int>(i) * 16, lines[i], kText);
        cv.text(small, kPX + 20, kFootY + 54, std::string("This setting: ") + whenText(s->when), kAccent);
    }
    cv.text(small, kPX + 20, kFootY + 72, m.footNote(), kDimText);
}

void drawNewGame(Canvas2& cv, const Controller& c, const MenuFonts& F) {
    const NewGameModel& n = c.newGame;
    const Font &title = *F.title(), &body = *F.body(), &small = *F.small();
    cv.rect(kNX, kNY, kNW, kNH, kPanel);
    cv.frame(kNX, kNY, kNW, kNH, 2, kBorder);
    cv.text(title, kNX + 18, kNY + 8, "New game", kText);
    const char* heads[2] = {"Realism", "Difficulty"};
    for (int col = 0; col < 2; col++) {
        const std::vector<NewGameOption>& opts = col == 0 ? realismOptions() : difficultyOptions();
        const int chosen = col == 0 ? n.realismIndex() : n.difficultyIndex();
        const bool focused = n.focus() == (col == 0 ? NewGameModel::FocusRealism : NewGameModel::FocusDifficulty);
        cv.text(body, kNX + 16 + col * 280, kNY + 40, heads[col], focused ? kAccent : kDimText);
        for (int i = 0; i < 3; i++) {
            const R r = ngBlockRect(col, i);
            const bool on = i == chosen;
            cv.rect(r.x, r.y, r.w, r.h, on ? kRowSel : kTabOff);
            cv.frame(r.x, r.y, r.w, r.h, on && focused ? 2 : 1, on ? (focused ? kAccent : kBorder) : kGroove);
            cv.ring(r.x + 12, r.y + 13, 6, on ? kAccent : kDimText);
            if (on) cv.disc(r.x + 12, r.y + 13, 3, kAccent);
            cv.text(body, r.x + 26, r.y + 4, opts[static_cast<size_t>(i)].name, on ? kText : kDimText);
            const std::vector<std::string> lines = wrapMenuText(small, opts[static_cast<size_t>(i)].text, r.w - 20);
            for (size_t l = 0; l < lines.size() && l < 4; l++) cv.text(small, r.x + 10, r.y + 26 + static_cast<int>(l) * 14, lines[l], on ? kText : kDimText);
        }
    }
    const std::vector<std::string> note = wrapMenuText(small, newGameNote(), kNW - 40);
    for (size_t i = 0; i < note.size() && i < 4; i++) cv.text(small, kNX + 20, kNoteY + static_cast<int>(i) * 15, note[i], kDimText);
    struct B {
        R r;
        const char* label;
        int hoverId, focusId;
    } bs[2] = {{ngStartRect(), "Start", 1, NewGameModel::FocusStart}, {ngBackRect(), "Back", 2, NewGameModel::FocusBack}};
    for (const B& b : bs) {
        cv.rect(b.r.x, b.r.y, b.r.w, b.r.h, c.hoverButton() == b.hoverId ? kHot : kBtn);
        cv.frame(b.r.x, b.r.y, b.r.w, b.r.h, n.focus() == b.focusId ? 2 : 1, n.focus() == b.focusId ? kAccent : kBorder);
        cv.text(body, b.r.x + b.r.w / 2, b.r.y + (b.r.h - body.height()) / 2, b.label, kText, 1);
    }
}

} // namespace

std::vector<std::string> wrapMenuText(const Font& f, const std::string& text, int width) {
    std::vector<std::string> lines;
    std::string line;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t sp = text.find(' ', pos);
        if (sp == std::string::npos) sp = text.size();
        const std::string word = text.substr(pos, sp - pos);
        pos = sp + 1;
        if (word.empty()) continue;
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && f.textWidth(trial.data(), static_cast<int>(trial.size())) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = trial;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

bool renderMenuLayer(const Controller& c, const MenuFonts& fonts, int k, bool hint, std::vector<uint32_t>& argb) {
    k = std::max(1, std::min(k, 8));
    const bool open = c.isOpen();
    if ((!open && !hint) || !fonts.ok()) {
        argb.clear();
        return false;
    }
    const int W = kGridW * k, H = kGridH * k;
    argb.assign(static_cast<size_t>(W) * H, 0);
    Canvas2 cv{argb, k, W, H};
    if (c.mode() == Controller::Mode::Settings) drawSettings(cv, c, fonts);
    else if (c.mode() == Controller::Mode::NewGame) drawNewGame(cv, c, fonts);
    else {
        const Font& f = *fonts.small();
        const std::string t = "F10 settings";
        const int w = cv.width(f, t) + 16, h = f.height() + 8;
        const int x = kGridW - w - 10, y = kGridH - h - 8;
        cv.rect(x, y, w, h, Col{0, 0, 0, 150});
        cv.frame(x, y, w, h, 1, Col{118, 160, 128, 200});
        cv.text(f, x + w / 2, y + 4, t, Col{226, 236, 222, 255}, 1);
    }
    return true;
}

void composeMenuPicture(const Frame& frame, const Palette& pal, int winW, int winH, bool dim, const std::vector<uint32_t>& layer, int k,
                        std::vector<uint32_t>& rgb) {
    rgb.assign(static_cast<size_t>(winW) * winH, 0);
    const int s = std::max(1, std::min(winW / Frame::W, winH / Frame::H));
    const int x0 = (winW - Frame::W * s) / 2, y0 = (winH - Frame::H * s) / 2;
    for (int y = 0; y < Frame::H * s; y++) {
        const int wy = y0 + y;
        if (wy < 0 || wy >= winH) continue;
        for (int x = 0; x < Frame::W * s; x++) {
            const int wx = x0 + x;
            if (wx < 0 || wx >= winW) continue;
            const Color& c = pal.c[frame.px[static_cast<size_t>(y / s) * Frame::W + static_cast<size_t>(x / s)]];
            int r = c.r, g = c.g, b = c.b;
            if (dim) { r = r * (255 - kMenuDim) / 255; g = g * (255 - kMenuDim) / 255; b = b * (255 - kMenuDim) / 255; }
            if (!layer.empty() && s == k) {
                const uint32_t o = layer[static_cast<size_t>(y) * static_cast<size_t>(Frame::W * k) + static_cast<size_t>(x)];
                const int a = static_cast<int>(o >> 24);
                if (a) {
                    r = (static_cast<int>((o >> 16) & 255) * a + r * (255 - a) + 127) / 255;
                    g = (static_cast<int>((o >> 8) & 255) * a + g * (255 - a) + 127) / 255;
                    b = (static_cast<int>(o & 255) * a + b * (255 - a) + 127) / 255;
                }
            }
            rgb[static_cast<size_t>(wy) * winW + wx] = static_cast<uint32_t>(r) << 16 | static_cast<uint32_t>(g) << 8 | static_cast<uint32_t>(b);
        }
    }
}

} // namespace biosys
