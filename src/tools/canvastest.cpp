// biosys-canvastest: the GDI stand-in (font.h / canvas.h) without the rest of the engine, for tests/test_canvas.sh.
//   canvastest faces FONT.TTF...                    `face family=.. style=.. upm=.. winascent=.. windescent=..` per file
//   canvastest text FONT.TTF FACE HEIGHT STRING OUT.bmp
//        make the logical font (HEIGHT as in the LOGFONT), print its metrics and the advances, draw STRING black on white
//        at (10, 10) with TextOut and print the extent, an independent sum of FreeType advances and the ink bounding box
//   canvastest wrap FONT.TTF FACE HEIGHT WIDTH FLAGS STRING     the advances of STRING's characters and the lines DrawText makes
//   canvastest map PALETTE.PAL R,G,B...             the palette index Canvas::mapColor picks for each PALETTERGB colour
//   canvastest draw W H OUT.bmp OP...               line / rectangle primitives on an 8-bit surface (palette: index i = grey i)
//        ops: pen:W:IDX  move:X:Y  line:X:Y  fill:L:T:R:B:IDX  solid:X:Y:CX:CY:IDX  rect:L:T:R:B  brush:IDX  ellipse:L:T:R:B  pie:L:T:R:B:XS:YS:XE:YE
#include "engine/canvas.h"
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <sstream>

using namespace biosys;

namespace {

Palette greyPalette() {
    Palette p;
    for (int i = 0; i < 256; i++) p.c[i] = Color{static_cast<u8>(i), static_cast<u8>(i), static_cast<u8>(i)};
    return p;
}

void writeSurface(const std::string& path, const Surface& s, const Palette& p) {
    Image8 img;
    img.w = s.w;
    img.h = s.h;
    img.px = s.px;
    writeBmp(path, img, p);
}

int makeFont(FontLibrary& lib, const std::string& file, const std::string& face, int height) {
    if (!lib.addFile(file)) throw Error("cannot load font " + file);
    LogFont lf;
    lf.height = height;
    lf.charSet = 1;
    lf.quality = 2;
    lf.faceName = face;
    return lib.createFont(lf);
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> v;
    std::stringstream ss(s);
    std::string t;
    while (std::getline(ss, t, sep)) v.push_back(t);
    return v;
}

// Independent of font.cpp: FreeType called directly, monochrome hinted advances (the way GDI measures with hdmx / hinting).
int ftAdvanceSum(const std::string& file, int ppem, const std::string& text, std::vector<int>& each) {
    FT_Library lib;
    FT_Face face;
    if (FT_Init_FreeType(&lib)) throw Error("FT_Init_FreeType");
    if (FT_New_Face(lib, file.c_str(), 0, &face)) throw Error("FT_New_Face " + file);
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(ppem));
    int sum = 0;
    for (unsigned char c : text) {
        FT_UInt g = FT_Get_Char_Index(face, c);
        if (!g) g = FT_Get_Char_Index(face, 0xF000u + c);
        int adv = 0;
        if (!FT_Load_Glyph(face, g, FT_LOAD_TARGET_MONO | FT_LOAD_NO_BITMAP)) adv = static_cast<int>((face->glyph->advance.x + 32) >> 6);
        each.push_back(adv);
        sum += adv;
    }
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    return sum;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw Error("usage: canvastest faces|text|wrap|map|draw ...");
        const std::string cmd = argv[1];
        if (cmd == "faces") {
            FontLibrary lib;
            for (int i = 2; i < argc; i++) {
                if (!lib.addFile(argv[i])) { std::printf("face FAILED %s\n", argv[i]); continue; }
            }
            for (const FaceInfo& f : lib.faces())
                std::printf("face family=%s style=%s upm=%d winascent=%d windescent=%d path=%s\n", f.family.c_str(), f.style.c_str(), f.unitsPerEm, f.winAscent, f.winDescent, f.path.c_str());
        } else if (cmd == "text" && argc >= 7) {
            FontLibrary lib;
            const int h = makeFont(lib, argv[2], argv[3], std::atoi(argv[4]));
            const Font* f = lib.font(h);
            const std::string text = argv[5];
            std::printf("font family=%s ppem=%d ascent=%d descent=%d height=%d\n", f->family().c_str(), f->ppem(), f->ascent(), f->descent(), f->height());
            Surface s;
            s.init(400, 120, 0);
            Palette pal;
            for (int i = 0; i < 256; i++) pal.c[i] = i == 0 ? Color{255, 255, 255} : Color{0, 0, 0};
            Canvas c(s, [&pal]() -> const Palette& { return pal; }, &lib);
            c.selectFont(h);
            c.setBkMode(kTransparent);
            c.setTextColor(rgb(0, 0, 0));
            GdiSize ex = c.textExtent(text);
            const bool ok = c.textOut(10, 10, text);
            std::vector<int> each;
            const int ftsum = ftAdvanceSum(argv[2], f->ppem(), text, each);
            std::printf("advances");
            for (int a : each) std::printf(" %d", a);
            std::printf("\nextent cx=%d cy=%d ftsum=%d ok=%d\n", ex.cx, ex.cy, ftsum, ok ? 1 : 0);
            int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1, n = 0;
            for (int y = 0; y < s.h; y++)
                for (int x = 0; x < s.w; x++)
                    if (s.px[static_cast<size_t>(y) * static_cast<size_t>(s.w) + static_cast<size_t>(x)]) {
                        n++;
                        x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y);
                    }
            std::printf("ink pixels=%d bbox=%d,%d,%d,%d\n", n, x0, y0, x1, y1);
            writeSurface(argv[6], s, pal);
        } else if (cmd == "wrap" && argc >= 8) {
            FontLibrary lib;
            const int h = makeFont(lib, argv[2], argv[3], std::atoi(argv[4]));
            const Font* f = lib.font(h);
            Surface s;
            s.init(8, 8, 0);
            Palette pal = greyPalette();
            Canvas c(s, [&pal]() -> const Palette& { return pal; }, &lib);
            c.selectFont(h);
            const std::string text = argv[7];
            std::printf("advances");
            for (unsigned char ch : text) std::printf(" %d", f->advance(ch));
            std::printf("\n");
            for (const std::string& l : c.layout(text.data(), static_cast<int>(text.size()), std::atoi(argv[5]), static_cast<unsigned>(std::strtol(argv[6], nullptr, 0))))
                std::printf("line [%s] width=%d\n", l.c_str(), c.textExtent(l).cx);
        } else if (cmd == "map" && argc >= 3) {
            Palette pal = Palette::load(argv[2]);
            Surface s;
            s.init(1, 1, 0);
            Canvas c(s, [&pal]() -> const Palette& { return pal; }, nullptr);
            for (int i = 3; i < argc; i++) {
                int r = 0, g = 0, b = 0;
                if (std::sscanf(argv[i], "%d,%d,%d", &r, &g, &b) != 3) throw Error("bad colour");
                std::printf("map %d,%d,%d palettergb=%d rgb=%d\n", r, g, b, c.mapColor(paletteRgb(r, g, b)), c.mapColor(rgb(r, g, b)));
            }
        } else if (cmd == "draw" && argc >= 5) {
            Surface s;
            s.init(std::atoi(argv[2]), std::atoi(argv[3]), 0);
            Palette pal = greyPalette();
            Canvas c(s, [&pal]() -> const Palette& { return pal; }, nullptr);
            for (int i = 5; i < argc; i++) {
                const std::vector<std::string> t = split(argv[i], ':');
                auto n = [&](size_t k) { return std::atoi(t.at(k).c_str()); };
                const std::string& op = t.at(0);
                if (op == "pen") { Pen p; p.width = n(1); p.color = paletteIndex(n(2)); c.selectPen(p); }
                else if (op == "brush") { Brush b; b.color = paletteIndex(n(1)); c.selectBrush(b); }
                else if (op == "move") c.moveTo(n(1), n(2));
                else if (op == "line") c.lineTo(n(1), n(2));
                else if (op == "fill") c.fillRect(GdiRect{n(1), n(2), n(3), n(4)}, paletteIndex(n(5)));
                else if (op == "solid") c.fillSolidRect(n(1), n(2), n(3), n(4), paletteIndex(n(5)));
                else if (op == "rect") c.rectangle(n(1), n(2), n(3), n(4));
                else if (op == "ellipse") c.ellipse(n(1), n(2), n(3), n(4));
                else if (op == "pie") c.pie(n(1), n(2), n(3), n(4), n(5), n(6), n(7), n(8));
                else throw Error("bad op " + op);
            }
            writeSurface(argv[4], s, pal);
        } else {
            throw Error("bad arguments");
        }
    } catch (const Error& e) {
        std::fprintf(stderr, "canvastest: %s\n", e.what());
        return 1;
    }
    return 0;
}
