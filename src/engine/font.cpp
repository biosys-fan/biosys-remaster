#include "engine/font.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include <algorithm>
#include <cstring>

namespace biosys {

namespace {

struct FtLib {
    FT_Library lib = nullptr;
    FtLib() {
        if (FT_Init_FreeType(&lib)) throw Error("font: cannot initialise FreeType");
    }
    ~FtLib() { FT_Done_FreeType(lib); }
};

// Windows-1252 bytes 0x80..0x9f as Unicode (undefined ones stay as they are).
const u16 kCp1252High[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                             0x2039, 0x0152, 0x8D,   0x017D, 0x8F,   0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                             0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};

u32 le32(const u8* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<u32>(p[3]) << 24; }

} // namespace

LogFont LogFont::fromWin32(const void* p) {
    const u8* b = static_cast<const u8*>(p);
    LogFont lf;
    lf.height = static_cast<i32>(le32(b));
    lf.width = static_cast<i32>(le32(b + 4));
    lf.escapement = static_cast<i32>(le32(b + 8));
    lf.orientation = static_cast<i32>(le32(b + 12));
    lf.weight = static_cast<i32>(le32(b + 16));
    lf.italic = b[20] != 0;
    lf.underline = b[21] != 0;
    lf.strikeOut = b[22] != 0;
    lf.charSet = b[23];
    lf.outPrecision = b[24];
    lf.clipPrecision = b[25];
    lf.quality = b[26];
    lf.pitchAndFamily = b[27];
    size_t n = 0;
    while (n < 32 && b[28 + n]) n++;
    lf.faceName.assign(reinterpret_cast<const char*>(b + 28), n);
    return lf;
}

// ---------------------------------------------------------------------------------------------------- Font

struct Font::Impl {
    std::shared_ptr<FtLib> ft;
    std::shared_ptr<std::vector<u8>> data;    // the font file, must outlive face
    FT_Face face = nullptr;
    std::string family;
    int ppem = 0, ascent = 0, descent = 0;
    mutable std::map<unsigned char, GlyphBitmap> cache;
    mutable std::map<std::pair<unsigned char, int>, GlyphCoverage> scaled;
    mutable std::map<std::pair<unsigned char, int>, GlyphCoverage> scaledF;   // by scale in 1/64
    ~Impl() {
        if (face) FT_Done_Face(face);
    }
    FT_UInt glyphIndex(unsigned char c) const {
        if (!face) return 0;
        // ANSI byte -> Unicode; fonts whose charmap is MS Symbol (JON.TTF: 0xF020..0xF0FF) carry the byte in the low byte.
        u32 u = (c >= 0x80 && c < 0xA0) ? kCp1252High[c - 0x80] : c;
        FT_UInt g = FT_Get_Char_Index(face, u);
        if (!g && c >= 0x20) g = FT_Get_Char_Index(face, 0xF000u + c);
        return g;
    }
    GlyphBitmap render(unsigned char c) const {
        GlyphBitmap g;
        if (!face) return g;
        // Monochrome hinted outline: the bytecode interpreter runs in its classic (full) mode for FT_RENDER_MODE_MONO.
        if (FT_Load_Glyph(face, glyphIndex(c), FT_LOAD_TARGET_MONO | FT_LOAD_RENDER | FT_LOAD_NO_BITMAP)) return g;
        const FT_GlyphSlot s = face->glyph;
        g.advance = static_cast<int>((s->advance.x + 32) >> 6);
        const FT_Bitmap& bm = s->bitmap;
        g.left = s->bitmap_left;
        g.top = s->bitmap_top;
        g.w = static_cast<int>(bm.width);
        g.h = static_cast<int>(bm.rows);
        g.px.assign(static_cast<size_t>(g.w) * static_cast<size_t>(g.h), 0);
        for (int y = 0; y < g.h; y++) {
            const unsigned char* row = bm.buffer + static_cast<ptrdiff_t>(y) * bm.pitch;
            for (int x = 0; x < g.w; x++) {
                bool ink = bm.pixel_mode == FT_PIXEL_MODE_MONO ? ((row[x >> 3] >> (7 - (x & 7))) & 1) != 0 : row[x] >= 128;
                g.px[static_cast<size_t>(y) * static_cast<size_t>(g.w) + static_cast<size_t>(x)] = ink ? 1 : 0;
            }
        }
        return g;
    }
    GlyphCoverage renderScaled(unsigned char c, int scale) const {
        GlyphCoverage g;
        if (!face || scale < 1) return g;
        // The outline at ppem * scale, unhinted so that the shape is the face's own at that size; the size is put back afterwards
        // because the low-resolution glyphs are rendered lazily from the same face.
        FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(std::min(ppem * scale, 4000)));
        if (!FT_Load_Glyph(face, glyphIndex(c), FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_NORMAL) &&
            !FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) {
            const FT_Bitmap& bm = face->glyph->bitmap;
            g.left = face->glyph->bitmap_left;
            g.top = face->glyph->bitmap_top;
            g.w = static_cast<int>(bm.width);
            g.h = static_cast<int>(bm.rows);
            g.a.assign(static_cast<size_t>(g.w) * static_cast<size_t>(g.h), 0);
            for (int y = 0; y < g.h; y++) {
                const unsigned char* row = bm.buffer + static_cast<ptrdiff_t>(y) * bm.pitch;
                for (int x = 0; x < g.w; x++)
                    g.a[static_cast<size_t>(y) * static_cast<size_t>(g.w) + static_cast<size_t>(x)] =
                        bm.pixel_mode == FT_PIXEL_MODE_MONO ? (((row[x >> 3] >> (7 - (x & 7))) & 1) ? 255 : 0) : row[x];
            }
        }
        FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(ppem));
        return g;
    }
    // The same at a fractional scale (the overlay interface scales the view by k = H / 368): the size in 26.6 pixels (72 dpi: points = pixels).
    GlyphCoverage renderScaledF(unsigned char c, int scale64) const {
        GlyphCoverage g;
        if (!face || scale64 < 16) return g;
        const long size64 = std::min<long>(static_cast<long>(ppem) * scale64, 4000L * 64);
        FT_Set_Char_Size(face, 0, size64, 72, 72);
        if (!FT_Load_Glyph(face, glyphIndex(c), FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_NORMAL) &&
            !FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) {
            const FT_Bitmap& bm = face->glyph->bitmap;
            g.left = face->glyph->bitmap_left;
            g.top = face->glyph->bitmap_top;
            g.w = static_cast<int>(bm.width);
            g.h = static_cast<int>(bm.rows);
            g.a.assign(static_cast<size_t>(g.w) * static_cast<size_t>(g.h), 0);
            for (int y = 0; y < g.h; y++) {
                const unsigned char* row = bm.buffer + static_cast<ptrdiff_t>(y) * bm.pitch;
                for (int x = 0; x < g.w; x++)
                    g.a[static_cast<size_t>(y) * static_cast<size_t>(g.w) + static_cast<size_t>(x)] =
                        bm.pixel_mode == FT_PIXEL_MODE_MONO ? (((row[x >> 3] >> (7 - (x & 7))) & 1) ? 255 : 0) : row[x];
            }
        }
        FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(ppem));
        return g;
    }
};

Font::Font() : p_(new Impl) {}
Font::~Font() = default;
bool Font::hasFace() const { return p_->face != nullptr; }
const std::string& Font::family() const { return p_->family; }
int Font::ppem() const { return p_->ppem; }
int Font::ascent() const { return p_->ascent; }
int Font::descent() const { return p_->descent; }

const GlyphBitmap& Font::glyph(unsigned char c) const {
    auto it = p_->cache.find(c);
    if (it == p_->cache.end()) it = p_->cache.emplace(c, p_->render(c)).first;
    return it->second;
}

const GlyphCoverage& Font::glyphScaled(unsigned char c, int scale) const {
    auto key = std::make_pair(c, scale);
    auto it = p_->scaled.find(key);
    if (it == p_->scaled.end()) it = p_->scaled.emplace(key, p_->renderScaled(c, scale)).first;
    return it->second;
}

const GlyphCoverage& Font::glyphScaledF(unsigned char c, double scale) const {
    const int s64 = static_cast<int>(scale * 64.0 + 0.5);
    auto key = std::make_pair(c, s64);
    auto it = p_->scaledF.find(key);
    if (it == p_->scaledF.end()) it = p_->scaledF.emplace(key, p_->renderScaledF(c, s64)).first;
    return it->second;
}

int Font::advance(unsigned char c) const { return glyph(c).advance; }

int Font::textWidth(const char* s, int n) const {
    int w = 0;
    for (int i = 0; i < n; i++) w += advance(static_cast<unsigned char>(s[i]));
    return w;
}

// ---------------------------------------------------------------------------------------------- FontLibrary

namespace {
struct FaceEntry {
    std::string path;
    std::shared_ptr<std::vector<u8>> data;
    FaceInfo info;
    int refs = 1;
};
} // namespace

struct FontLibrary::Impl {
    std::shared_ptr<FtLib> ft = std::make_shared<FtLib>();
    std::vector<FaceEntry> faces;
    std::map<int, std::shared_ptr<Font>> fonts;
    int nextHandle = 0x31;                     // handles look like GDI object handles; 0 is "no font"
};

FontLibrary::FontLibrary() : p_(new Impl) {}
FontLibrary::~FontLibrary() = default;

bool FontLibrary::addFile(const std::string& path) {
    for (FaceEntry& f : p_->faces)
        if (iequals(f.path, path)) {
            f.refs++;
            return true;
        }
    FaceEntry e;
    try {
        e.data = std::make_shared<std::vector<u8>>(readFile(path));
    } catch (const Error&) {
        return false;
    }
    FT_Face face = nullptr;
    if (FT_New_Memory_Face(p_->ft->lib, e.data->data(), static_cast<FT_Long>(e.data->size()), 0, &face)) return false;
    if (!FT_IS_SCALABLE(face) || !face->family_name) {
        FT_Done_Face(face);
        return false;
    }
    e.path = path;
    e.info.path = path;
    e.info.family = face->family_name;
    e.info.style = face->style_name ? face->style_name : "";
    e.info.unitsPerEm = face->units_per_EM;
    if (const TT_OS2* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2))) {
        e.info.winAscent = os2->usWinAscent;
        e.info.winDescent = os2->usWinDescent;
    }
    if (e.info.winAscent + e.info.winDescent <= 0) {
        e.info.winAscent = face->ascender;
        e.info.winDescent = -face->descender;
    }
    FT_Done_Face(face);
    p_->faces.push_back(std::move(e));
    return true;
}

bool FontLibrary::removeFile(const std::string& path) {
    for (size_t i = 0; i < p_->faces.size(); i++)
        if (iequals(p_->faces[i].path, path)) {
            if (--p_->faces[i].refs <= 0) p_->faces.erase(p_->faces.begin() + static_cast<ptrdiff_t>(i));
            return true;
        }
    return false;
}

std::vector<FaceInfo> FontLibrary::faces() const {
    std::vector<FaceInfo> out;
    for (const FaceEntry& f : p_->faces) out.push_back(f.info);
    return out;
}

int FontLibrary::createFont(const LogFont& lf) {
    const FaceEntry* match = nullptr;
    for (const FaceEntry& f : p_->faces)
        if (iequals(f.info.family, lf.faceName)) {
            match = &f;
            break;
        }
    if (!match && !p_->faces.empty()) match = &p_->faces.front();
    std::shared_ptr<Font> font(new Font);
    Font::Impl& im = *font->p_;
    im.ft = p_->ft;
    if (match) {
        im.data = match->data;
        if (FT_New_Memory_Face(p_->ft->lib, im.data->data(), static_cast<FT_Long>(im.data->size()), 0, &im.face)) im.face = nullptr;
    }
    if (im.face) {
        const FaceInfo& fi = match->info;
        const int upm = std::max(1, fi.unitsPerEm), cell = std::max(1, fi.winAscent + fi.winDescent);
        // GDI: a negative height is the em height in pixels; a positive one is the cell height (ascent + descent of the
        // OS/2 win metrics), so ppem = height * unitsPerEm / (winAscent + winDescent), rounded. 0 = default (16 pixel cell).
        const int h = lf.height == 0 ? 16 : lf.height;
        int ppem = h < 0 ? -h : static_cast<int>((static_cast<long long>(h) * upm + cell / 2) / cell);
        im.ppem = std::max(1, std::min(ppem, 1000));
        FT_Set_Pixel_Sizes(im.face, 0, static_cast<FT_UInt>(im.ppem));
        im.ascent = (fi.winAscent * im.ppem + upm / 2) / upm;
        im.descent = (fi.winDescent * im.ppem + upm / 2) / upm;
        im.family = fi.family;
        // Prefer a Unicode charmap, else the MS Symbol one (glyphIndex falls back to 0xF000 + byte for symbol fonts).
        FT_Encoding want[2] = {FT_ENCODING_UNICODE, FT_ENCODING_MS_SYMBOL};
        for (FT_Encoding enc : want) {
            bool done = false;
            for (int i = 0; i < im.face->num_charmaps && !done; i++)
                if (im.face->charmaps[i]->encoding == enc) done = FT_Set_Charmap(im.face, im.face->charmaps[i]) == 0;
            if (done) break;
        }
    }
    const int handle = p_->nextHandle++;
    p_->fonts[handle] = std::move(font);
    return handle;
}

std::shared_ptr<const Font> FontLibrary::fontShared(int handle) const {
    auto it = p_->fonts.find(handle);
    return it == p_->fonts.end() ? nullptr : it->second;
}

bool FontLibrary::deleteFont(int handle) { return p_->fonts.erase(handle) != 0; }

const Font* FontLibrary::font(int handle) const {
    auto it = p_->fonts.find(handle);
    return it == p_->fonts.end() ? nullptr : it->second.get();
}

} // namespace biosys
