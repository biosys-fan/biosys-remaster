// TrueType fonts for the GDI text of the original (FreeType, the one dependency of the engine core besides zlib).
//
// What the original does: BiosGame.dll registers BIODATA\FONTS\jon.ttf ("systemfont") and highgfb.ttf
// ("HighlandGothicFLF") with AddFontResourceA (API 0x38), creates logical fonts with CreateFontIndirectA from a LOGFONT
// (API 0x39) and draws with TextOutA / DrawTextA on the DC of a page surface (API 0x3a). The screen is 8-bit palettised
// and the LOGFONT quality is PROOF_QUALITY, so GDI draws monochrome (no anti-aliasing) glyphs; widths are the hinted
// integer advances (the fonts carry hdmx tables), without kerning. See analysis/notes/gdi_usage.md.
#pragma once
#include "engine/common.h"
#include <map>
#include <memory>

namespace biosys {

// The LOGFONTA fields that matter (the DLL fills height, weight 400, DEFAULT_CHARSET, PROOF_QUALITY, face name).
// weight / italic / underline / strikeOut / width / escapement are carried but not rendered.
struct LogFont {
    int height = 0;               // < 0: character (em) height in pixels; > 0: cell height (ascent + descent); 0: default
    int width = 0, escapement = 0, orientation = 0, weight = 400;
    bool italic = false, underline = false, strikeOut = false;
    u8 charSet = 1, outPrecision = 0, clipPrecision = 0, quality = 0, pitchAndFamily = 0;
    std::string faceName;
    // The 60-byte Win32 LOGFONTA (little-endian): height, width, escapement, orientation, weight (i32), italic,
    // underline, strikeOut, charSet, outPrecision, clipPrecision, quality, pitchAndFamily (u8), faceName[32].
    static LogFont fromWin32(const void* logFontA);
};

// One rendered glyph: 1 byte per pixel (1 = ink), placed at (left, -top) from the pen position on the baseline.
struct GlyphBitmap {
    int advance = 0;              // hinted integer advance in pixels
    int left = 0, top = 0;        // top: rows above the baseline
    int w = 0, h = 0;
    std::vector<u8> px;
};

// One glyph rendered at `scale` times the font's pixel size with FreeType's anti-aliasing (see Font::glyphScaled): 8-bit coverage
// (255 = full ink), placed at (left, -top) from the pen position on the baseline, all in output pixels. The pen position and the
// baseline are the low-resolution ones multiplied by the scale; the advance is not part of it (the layout stays the low-resolution one).
struct GlyphCoverage {
    int left = 0, top = 0;
    int w = 0, h = 0;
    std::vector<u8> a;
};

// A logical font: a face at one pixel size.
class Font {
public:
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
    bool hasFace() const;                       // false: no font file was registered; everything is empty / zero
    const std::string& family() const;          // the face that was matched ("" without a face)
    int ppem() const;                           // pixels per em
    // GDI text metrics of the cell: ascent / descent = OS/2 usWinAscent / usWinDescent scaled by ppem, rounded.
    int ascent() const;
    int descent() const;
    int height() const { return ascent() + descent(); }
    int advance(unsigned char c) const;
    const GlyphBitmap& glyph(unsigned char c) const;    // cached; an empty bitmap for blanks
    int textWidth(const char* s, int n) const;           // sum of the advances (no kerning)
    // The same glyph at ppem * scale (outline scaled, no hinting, 256-level anti-aliasing): for a presentation at the window's real
    // resolution. Cached per (glyph, scale); does not touch the cached low-resolution glyphs. scale 1 is allowed (a smoothed copy).
    const GlyphCoverage& glyphScaled(unsigned char c, int scale) const;
    // The same at a fractional scale (rounded to 1/64), for the overlay interface's non-integer view scale. Cached per (glyph, scale in 1/64).
    const GlyphCoverage& glyphScaledF(unsigned char c, double scale) const;
private:
    friend class FontLibrary;
    Font();
    struct Impl;
    std::unique_ptr<Impl> p_;
};

struct FaceInfo {
    std::string family, style, path;
    int unitsPerEm = 0, winAscent = 0, winDescent = 0;
};

// The registered font files (AddFontResource) and the logical fonts made from them (CreateFontIndirect).
class FontLibrary {
public:
    FontLibrary();
    ~FontLibrary();
    FontLibrary(const FontLibrary&) = delete;
    FontLibrary& operator=(const FontLibrary&) = delete;

    // AddFontResourceA: reads the file (a real path); false if it is not a TrueType font. Adding a file again counts up.
    bool addFile(const std::string& path);
    // RemoveFontResourceA: false if the file was never added. Fonts already made from it keep working.
    bool removeFile(const std::string& path);
    std::vector<FaceInfo> faces() const;

    // CreateFontIndirectA: always succeeds (the face is matched by family name, case-insensitively; an unknown name
    // falls back to the first registered face, and without any face to a font that draws nothing). Handle > 0.
    int createFont(const LogFont& lf);
    // DeleteObject on a font: false for an unknown handle.
    bool deleteFont(int handle);
    const Font* font(int handle) const;
    // The same font as a shared reference: text runs recorded for a high-resolution presentation keep it after DeleteObject.
    std::shared_ptr<const Font> fontShared(int handle) const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace biosys
