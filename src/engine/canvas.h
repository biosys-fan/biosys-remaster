// Canvas: stands in for the Windows device context (HDC) that BiosGame.dll gets from API 0x3a GetWindowDC (and 0x4c) and
// draws on with GDI / MFC CDC: text in TrueType fonts, lines, filled rectangles, ellipses, pies. It draws into an 8-bit
// palette-index surface (a window's page surface); colours are Windows COLORREFs mapped to the nearest entry of the palette
// on display. Inventory of what the DLL calls: analysis/notes/gdi_usage.md. Reference: the semantics are those of GDI
// (TextOut at the top-left of the text cell, MM_TEXT, a 1-pixel solid pen, DrawText word wrap), reduced to what the DLL uses.
//
// A Canvas is created fresh for every GetWindowDC (DirectDraw hands out a DC with default state: no font, text colour
// black, background colour white, OPAQUE background mode, TA_TOP|TA_LEFT, 1-pixel black pen, white brush) and is
// destroyed by ReleaseWindowDC. The game code reaches it through `EngineApi::canvas(handle)`.
#pragma once
#include "engine/font.h"
#include "engine/framebuffer.h"
#include "engine/image.h"
#include "engine/textplane.h"
#include <functional>

namespace biosys {

// COLORREF = 0xTTbbggrr. TT = 0x02 PALETTERGB (what the DLL uses), 0x01 PALETTEINDEX, 0x00 plain RGB (nearest entry).
constexpr u32 paletteRgb(int r, int g, int b) { return 0x02000000u | static_cast<u32>(b) << 16 | static_cast<u32>(g) << 8 | static_cast<u32>(r); }
constexpr u32 paletteIndex(int i) { return 0x01000000u | static_cast<u32>(i & 0xffff); }
constexpr u32 rgb(int r, int g, int b) { return static_cast<u32>(b) << 16 | static_cast<u32>(g) << 8 | static_cast<u32>(r); }

struct GdiPoint { int x = 0, y = 0; };
struct GdiSize { int cx = 0, cy = 0; };
struct GdiRect { int left = 0, top = 0, right = 0, bottom = 0; };     // RECT: right / bottom exclusive

enum : int { kTransparent = 1, kOpaque = 2 };                          // SetBkMode
enum : unsigned {                                                      // SetTextAlign
    TA_LEFT = 0, TA_RIGHT = 2, TA_CENTER = 6, TA_TOP = 0, TA_BOTTOM = 8, TA_BASELINE = 24
};
enum : unsigned {                                                      // DrawText format flags
    DT_TOP = 0, DT_LEFT = 0, DT_CENTER = 1, DT_RIGHT = 2, DT_VCENTER = 4, DT_BOTTOM = 8, DT_WORDBREAK = 0x10,
    DT_SINGLELINE = 0x20, DT_NOCLIP = 0x100, DT_CALCRECT = 0x400, DT_NOPREFIX = 0x800
};

struct Pen {                  // CPen(PS_SOLID, width, colour); style 5 (PS_NULL) draws nothing
    int style = 0, width = 1;
    u32 color = rgb(0, 0, 0);
};
struct Brush {                // CBrush(colour); null = HOLLOW_BRUSH
    bool null = false;
    u32 color = rgb(255, 255, 255);
};

class Canvas {
public:
    using PaletteFn = std::function<const Palette&()>;
    // target: the page surface of the window. fonts: where font handles (API 0x39) are looked up. mask: optional, one byte
    // per target pixel, set to 1 for every pixel drawn (windows whose page is composed from ornaments each frame).
    Canvas(Surface& target, PaletteFn palette, const FontLibrary* fonts, std::vector<u8>* mask = nullptr);

    int width() const { return t_.w; }
    int height() const { return t_.h; }

    // ---- device context state (each returns the previous value, like the GDI call) ----
    int selectFont(int fontHandle);                 // SelectObject(hdc, hfont); 0 / unknown handle = the default font (none)
    u32 setTextColor(u32 colorRef);                 // SetTextColor
    u32 setBkColor(u32 colorRef);                   // SetBkColor
    int setBkMode(int mode);                        // SetBkMode: kTransparent / kOpaque
    unsigned setTextAlign(unsigned flags);          // SetTextAlign: only the horizontal and vertical alignment bits
    Pen selectPen(const Pen& p);                    // SelectObject(hdc, CPen); GetCurrentObject(hdc, OBJ_PEN) = pen()
    Brush selectBrush(const Brush& b);              // SelectObject(hdc, CBrush)
    const Pen& pen() const { return pen_; }
    const Brush& brush() const { return brush_; }
    u32 textColor() const { return textColor_; }
    u32 bkColor() const { return bkColor_; }
    int bkMode() const { return bkMode_; }
    unsigned textAlign() const { return align_; }
    int font() const { return font_; }

    // ---- text ----
    // TextOutA: (x, y) is the top-left of the text cell (TA_TOP | TA_LEFT); n < 0 = strlen. Opaque mode fills the cell
    // with the background colour first. Returns false only without a font.
    bool textOut(int x, int y, const char* s, int n = -1);
    bool textOut(int x, int y, const std::string& s) { return textOut(x, y, s.data(), static_cast<int>(s.size())); }
    // GetTextExtentPointA: cx = sum of the advances, cy = cell height (zero without a font).
    GdiSize textExtent(const char* s, int n = -1) const;
    GdiSize textExtent(const std::string& s) const { return textExtent(s.data(), static_cast<int>(s.size())); }
    // DrawTextA / CDC::DrawText(str, nCount, rect, format): format = DT_* flags. Supported: DT_WORDBREAK, DT_SINGLELINE,
    // DT_CENTER / DT_RIGHT, DT_VCENTER / DT_BOTTOM (single line), DT_NOCLIP, DT_CALCRECT, DT_NOPREFIX; '\n', "\r\n" and
    // '\r' end a line. Text is clipped to *rect unless DT_NOCLIP. Returns the height of the text in pixels (0 = failure).
    int drawText(const char* s, int n, GdiRect* rect, unsigned format);
    int drawText(const std::string& s, GdiRect* rect, unsigned format) { return drawText(s.data(), static_cast<int>(s.size()), rect, format); }

    // The lines DrawText prints for `width` pixels (see drawText); empty without a font.
    std::vector<std::string> layout(const char* s, int n, int width, unsigned format) const;

    // ---- lines and areas ----
    // CDC::MoveTo / LineTo. A 1-pixel pen draws Bresenham from the current position up to but not including the end point;
    // a wider pen stamps width x width squares (anchored at x - width/2, y - width/2) at every point including the end.
    GdiPoint moveTo(int x, int y);
    bool lineTo(int x, int y);
    GdiPoint currentPosition() const { return pos_; }
    // FillRect(hdc, &rect, CreateSolidBrush(colour)): [left, right) x [top, bottom).
    bool fillRect(const GdiRect& r, u32 colorRef);
    // CDC::FillSolidRect(x, y, cx, cy, colour) / (RECT*, colour): MFC sets the background colour to `colour` (and leaves it).
    bool fillSolidRect(int x, int y, int cx, int cy, u32 colorRef);
    bool fillSolidRect(const GdiRect& r, u32 colorRef);
    // Rectangle(hdc, l, t, r, b): outline in the pen, interior in the brush. (The DLL does not use it.)
    bool rectangle(int l, int t, int r, int b);
    // Ellipse(hdc, l, t, r, b): inscribed in [l, r) x [t, b), interior in the brush, 1-pixel outline in the pen.
    // Pie(hdc, l, t, r, b, xs, ys, xe, ye): the sector from the ray through (xs, ys) to the ray through (xe, ye), counter-clockwise.
    // Not pixel-exact against GDI's scan conversion (the DLL uses them for 5-pixel dots and one clock dial).
    bool ellipse(int l, int t, int r, int b);
    bool pie(int l, int t, int r, int b, int xs, int ys, int xe, int ye);
    // SetPixel / GetPixel (the DLL does not use them); GetPixel returns the palette index, -1 outside.
    bool setPixel(int x, int y, u32 colorRef);
    int pixel(int x, int y) const;

    // The palette index GDI would use for a COLORREF (squared RGB distance, first minimum), for the palette on display.
    u8 mapColor(u32 colorRef) const;

private:
    void plot(int x, int y, u8 index);
    void fillIdx(int l, int t, int r, int b, u8 index);
    void line(int x0, int y0, int x1, int y1, bool includeEnd);
    void stamp(int x, int y);
    void drawRun(int x, int y, const Font& f, const char* s, int n);
    bool shape(int l, int t, int r, int b, bool wedge, int xs, int ys, int xe, int ye);
    const Font* fontPtr() const;

    Surface& t_;
    bool track_ = false;                   // text tracking is on (TextPlane::tracking() when the Canvas was made)
    bool inText_ = false;
    u32 run_ = 0;
    PaletteFn pal_;
    const FontLibrary* fonts_;
    std::vector<u8>* mask_;
    int font_ = 0, bkMode_ = kOpaque;
    u32 textColor_ = rgb(0, 0, 0), bkColor_ = rgb(255, 255, 255);
    unsigned align_ = 0;
    Pen pen_;
    Brush brush_;
    GdiPoint pos_;
    GdiRect clip_{0, 0, 0, 0};    // limit of everything drawn (the DrawText rectangle; the whole surface otherwise)
    bool clipOn_ = false;
};

// Handle table behind API 0x3a: GetWindowDC hands out an opaque id (an HDC stand-in); the game code turns it into the
// object with `EngineApi::canvas(handle)`. Process-wide, ids are never reused.
class CanvasRegistry {
public:
    static int add(Canvas* c);            // does not own c; returns the new handle (> 0)
    static Canvas* get(int handle);       // nullptr if unknown
    static void remove(int handle);
};

} // namespace biosys
