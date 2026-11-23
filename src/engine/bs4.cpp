#include "engine/bs4.h"
#include "engine/textplane.h"
#include "engine/bmp.h"
#include <algorithm>
#include <cstring>

namespace biosys {

namespace {
constexpr u32 kPanelBit = 0x40000000;      // item 30 of the main page: "a panel may be open" marker (spec 1.7)

void putName(std::vector<u8>& b, size_t off, size_t cap, const std::string& s) {
    for (size_t i = 0; i < cap && i < s.size() + 1; i++) b[off + i] = i < s.size() ? static_cast<u8>(s[i]) : 0;
}
void putU32(std::vector<u8>& b, size_t off, u32 v) { for (int i = 0; i < 4; i++) b[off + static_cast<size_t>(i)] = static_cast<u8>(v >> (8 * i)); }
void putRect(std::vector<u8>& b, size_t off, const Bs4Rect& r) {
    putU32(b, off, static_cast<u32>(r.left)); putU32(b, off + 4, static_cast<u32>(r.top));
    putU32(b, off + 8, static_cast<u32>(r.right)); putU32(b, off + 12, static_cast<u32>(r.bottom));
}
} // namespace

// ------------------------------------------------------------------------------------------------ construct / initialise

// exe:00404a80 Construct + exe:00404d10 Initialise
Bs4::Bs4(Bs4Host& host, const DataFiles& files, Bs4Session& session, const u8* section, size_t sectionSize, const Bs4Options& options)
    : host_(host), session_(session), fixes_(options.fixes), main_(*this, Bs4Page::kMain), embedded_(*this, Bs4Page::kEmbedded), panel_(*this, Bs4Page::kPanel) {
    if (section && sectionSize != static_cast<size_t>(kSaveSize)) throw Error("BS4: Invalid saved game data specified.");
    loadPalette(files, options);                                   // exe:004084a0
    host_.setPalette(palette_);                                    // SetEntries(0, 0, 256): at once
    findColours();
    back_.init(640, 480, static_cast<u8>(col_.white));
    loadCursors(files);
    pf_.reset(new PageFile(files.requireData(options.pageFile)));
    const std::string ttf = files.findData("fonts\\jon.ttf");
    if (ttf.empty() || !fonts_.addFile(ttf)) host_.log("BS4: the font file jon.ttf could not be registered; text is not drawn");
    LogFont lf;                                                    // exe:00404d10: CreateFontIndirect, height 18, weight 400, charset 1, out precision 6, quality 2
    lf.height = 18; lf.weight = 400; lf.charSet = 1; lf.outPrecision = 6; lf.quality = 2; lf.faceName = "systemfont";
    fontHandle_ = fonts_.createFont(lf);
    pageEmail1_ = pf_->findPage("email1");
    pageEmailPw_ = pf_->findPage("emailpw");
    pageSynth1_ = pf_->findPage("synth1");
    pageSynthPw_ = pf_->findPage("synthpw");
    if (section) {
        if (!loadSectionImpl(section, sectionSize)) throw Error("BS4: the saved terminal state could not be restored");
    } else {
        int first = pf_->findPage(session_.initialPassword ? "menuscreen" : "initial");
        if (first == 0) first = 1;
        if (!loadMainPage(first)) throw Error("BS4: the first page could not be loaded");
    }
}

Bs4::~Bs4() {                                                      // exe:00404b90 Destroy
    main_.unload();
    embedded_.unload();
    panel_.unload();
}

// exe:004084a0 LoadPalette: BIOSYS4.PAL is exactly 0x418 bytes, RIFF, 256 x {R,G,B,flags} from file offset 0x18
void Bs4::loadPalette(const DataFiles& files, const Bs4Options& o) {
    const std::string path = files.requireData(o.paletteFile);
    std::vector<u8> d = readFile(path);
    if (d.size() != 0x418) throw Error("BS4: Expected palette file " + path + " to be 1048 bytes");
    palette_ = Palette::fromRiff(d);
    for (int i = 0; i < 256; i++) paletteFlags_[i] = d[0x18 + 4 * static_cast<size_t>(i) + 3];
}

// exe:00404d10 step 2: the seven colours the terminal needs, found in the palette (the first exact match)
void Bs4::findColours() {
    auto find = [&](int r, int g, int b, const char* what) {
        Color c;
        c.r = static_cast<u8>(r); c.g = static_cast<u8>(g); c.b = static_cast<u8>(b);
        const int i = palette_.find(c);
        if (i < 0) throw Error(std::string("BS4: There is no ") + what + " in the BioSys 4 palette.");
        return i;
    };
    col_.white = find(255, 255, 255, "white");
    col_.offWhite = find(247, 247, 247, "off-white");
    col_.transparent = find(0, 255, 255, "transparent colour");
    col_.grey = find(206, 206, 206, "grey");
    col_.red = find(165, 140, 140, "red");
    col_.green = find(162, 190, 165, "green");
    col_.lightGrey = find(231, 231, 231, "light grey");
}

// exe:00404d10 step 3: CURSORS\\BS4ARROW.BMP and BS4HAND.BMP, transparent colour = index +0x18a0
void Bs4::loadCursors(const DataFiles& files) {
    arrow_ = readBmp8(files.requireData("cursors\\bs4arrow.bmp")).image;
    hand_ = readBmp8(files.requireData("cursors\\bs4hand.bmp")).image;
}

// ------------------------------------------------------------------------------------------------ text and fills

// the DC of a surface: terminal font, as GetDC + SelectObject(+0x1ba8) (spec 1.10)
Canvas Bs4::canvasFor(Surface& s) {
    Canvas c(s, [this]() -> const Palette& { return palette_; }, &fonts_);
    c.selectFont(fontHandle_);
    return c;
}

// exe:004124e0: DDraw colour fill of [left,right) x [top,bottom), clipped; a rectangle of -1 does nothing
void Bs4::fillRect(Surface& s, const Bs4Rect& r, int index) {
    const int x0 = std::max(r.left, 0), y0 = std::max(r.top, 0), x1 = std::min(r.right, s.w), y1 = std::min(r.bottom, s.h);
    if (r.left == -1 && r.top == -1) return;
    for (int y = y0; y < y1; y++)
        if (x1 > x0) std::fill(s.row(y) + x0, s.row(y) + x1, static_cast<u8>(index));
}

void Bs4::drawText(Surface& s, const Bs4Rect& r, const std::string& text, int red, int green, int blue, unsigned dtFlags) {
    Canvas c = canvasFor(s);
    c.setBkMode(kTransparent);
    c.setTextColor(paletteRgb(red, green, blue));
    GdiRect gr{r.left, r.top, r.right, r.bottom};
    c.drawText(text.data(), static_cast<int>(text.size()), &gr, dtFlags);
}

// exe:00408680 UpdateClockText: the two clock fields on the back buffer, white background, (236,236,236), centred
void Bs4::updateClockText(bool force, bool flipAfter) {
    struct Field { int query; std::string* text; Bs4Rect rect; };
    Field fields[2] = {{7, &clockLeft_, Bs4Rect{0x14, 0xe, 0x45, 0x1c}}, {0xb, &clockRight_, Bs4Rect{0x53, 0xe, 0xcf, 0x1c}}};
    for (Field& f : fields) {
        char buf[512];
        std::memset(buf, 0, sizeof buf);
        if (!host_.onBS4Query(f.query, buf)) continue;
        buf[511] = 0;
        const std::string t = buf;
        if (!force && t == *f.text) continue;
        *f.text = t;
        fillRect(back_, f.rect, col_.white);
        drawText(back_, f.rect, t, 236, 236, 236, 0x25);
        if (flipAfter) flip();
    }
}

// ------------------------------------------------------------------------------------------------ compose / flip

// the page layers of exe:004067f0 without the clock text: white, the main page, its hover frame, the panel
void Bs4::composeLayers(Surface& dst) const {
    std::fill(dst.px.begin(), dst.px.end(), static_cast<u8>(col_.white));
    // (the embedded page layer is not ported: spec section 6)
    if (!main_.surf.px.empty()) {
        blitRegion(dst, Image8{main_.surf.w, main_.surf.h, main_.surf.px}, 0, 0, 0, 0, main_.surf.w, main_.surf.h, col_.transparent);
        if (hoverFrame_.left != -1) {                                 // 1-pixel box, black pen, MoveTo/LineTo (end point excluded)
            Canvas c(dst, [this]() -> const Palette& { return palette_; }, &fonts_);
            c.moveTo(hoverFrame_.left, hoverFrame_.top);
            c.lineTo(hoverFrame_.right, hoverFrame_.top);
            c.lineTo(hoverFrame_.right, hoverFrame_.bottom);
            c.lineTo(hoverFrame_.left, hoverFrame_.bottom);
            c.lineTo(hoverFrame_.left, hoverFrame_.top);
        }
    }
    if (panel_.loaded() && !panel_.surf.px.empty())
        blitRegion(dst, Image8{panel_.surf.w, panel_.surf.h, panel_.surf.px}, panelRect_.left, panelRect_.top, 0, 0, panel_.surf.w,
                   panel_.surf.h, col_.transparent);
}

// exe:004067f0
bool Bs4::compose() {
    if (back_.text) back_.text->reset();                              // the back buffer is painted afresh: no text on it yet
    composeLayers(back_);
    updateClockText(true, false);
    needsRepaint_ = false;
    return true;
}

// the back buffer with the software cursor of exe:00408b90 DrawCursor (kind 0 arrow, 2 hand, centred on the pointer, keyed with the
// transparent colour); the original saves and restores the pixels under the cursor, which this composition replaces
Frame Bs4::frame() const { return frameOf(back_); }

Frame Bs4::frameOf(const Surface& back) const {
    Frame f;
    f.px = back.px;
    if (cursorDrawn_ && (cursorKind_ == 0 || cursorKind_ == 2)) {       // exe:00408b90: centred on the pointer, keyed with the transparent colour
        const Image8& img = cursorKind_ == 0 ? arrow_ : hand_;
        const int x0 = cursorX_ - (img.w >> 1), y0 = cursorY_ - (img.h >> 1);
        for (int y = 0; y < img.h; y++)
            for (int x = 0; x < img.w; x++) {
                const int fx = x0 + x, fy = y0 + y;
                const u8 v = img.at(x, y);
                if (fx < 0 || fy < 0 || fx >= Frame::W || fy >= Frame::H || v == col_.transparent) continue;
                f.px[static_cast<size_t>(fy) * Frame::W + static_cast<size_t>(fx)] = v;
            }
    }
    return f;
}

void Bs4::flip() {
    host_.present(frame());
    if (TextPlane::tracking()) free_ = frameTextFree();
}

// The frame flip() presents, without the GDI text (page surfaces and the clock): the layers are composed with and without the text of
// the page surfaces, and where those differ the text-free value goes into the back buffer (stripped of its own clock text) before the
// cursor is drawn.
Frame Bs4::frameTextFree() {
    Surface plain = back_;
    plain.text.reset();
    if (back_.text) back_.text->strip(plain, nullptr);
    if ((main_.surf.text && !main_.surf.text->empty()) || (panel_.surf.text && !panel_.surf.text->empty())) {
        Surface a, b;
        a.init(back_.w, back_.h);
        b.init(back_.w, back_.h);
        composeLayers(a);
        if (main_.surf.text) main_.surf.text->strip(main_.surf, nullptr);
        if (panel_.surf.text) panel_.surf.text->strip(panel_.surf, nullptr);
        composeLayers(b);
        if (main_.surf.text) main_.surf.text->restore(main_.surf, nullptr);
        if (panel_.surf.text) panel_.surf.text->restore(panel_.surf, nullptr);
        for (size_t i = 0; i < plain.px.size(); i++)
            if (a.px[i] != b.px[i] && plain.px[i] == a.px[i]) plain.px[i] = b.px[i];
    }
    return frameOf(plain);
}

void Bs4::textSurfaces(std::vector<TextSurface>& out) {
    out.push_back({&main_.surf, 0, 0, "terminal"});
    if (panel_.loaded()) out.push_back({&panel_.surf, panelRect_.left, panelRect_.top, "terminal panel"});
    out.push_back({&back_, 0, 0, "terminal clock"});
}                          // exe:00411810

void Bs4::redraw() {                                                  // exe:004065d0
    cursorDrawn_ = false;
    compose();
    flip();
}

int Bs4::onIdle() {                                                   // exe:00406fe0
    if (needsRepaint_) {
        compose();
        flip();
    }
    return 0;
}

void Bs4::clipToPanelRect() { host_.clipPointer(&panelRect_); }
void Bs4::releasePointer() { host_.clipPointer(nullptr); }

// ------------------------------------------------------------------------------------------------ pages

int Bs4::pageNumber(const std::string& name) const { return pf_->findPage(name); }

// exe:004065f0
bool Bs4::loadMainPage(int pageNo) {
    if (pageNo < 1 || pageNo > pf_->pageCount()) {
        host_.log("BS4: Failed to get offset and length of page " + std::to_string(pageNo));
        return false;
    }
    textEntry = false;
    setHighlight("", 0);
    main_.unload();
    embedded_.unload();
    panel_.unload();
    panelRect_ = Bs4Rect();
    main_.load(pageNo);
    hoverName_.clear();
    hoverFrame_ = Bs4Rect();
    hoverFramesOff_ = false;
    currentName_ = main_.name();                                      // +0x18dc (the original writes it at the end of exe:004090a0)
    if (loadingGame_) return true;
    compose();
    flip();
    host_.requestMouseMove();
    if (Bs4Registry::pageSpecific() && Bs4Registry::hooks().onMainPageLoaded) Bs4Registry::hooks().onMainPageLoaded(*this, main_.name());
    return true;
}

// exe:0041ddf0 OnMainPageShown (the part that belongs to the P3 extensions)
static void mainShown(Bs4Page& main) {
    if (!Bs4Registry::pageSpecific() || !main.loaded()) return;
    for (const Bs4MainExt& e : Bs4Registry::mainExts())
        if (e.shown && (e.page.empty() || iequals(e.page, main.name()))) e.shown(main);
}

// exe:004080b0
bool Bs4::displayPanel(const Bs4Action& a) {
    const int pageNo = a.arg[0];
    if (pageNo < 1 || pageNo > pf_->pageCount()) {
        host_.log("BS4: Failed to get offset and length of page " + std::to_string(pageNo));
        return false;
    }
    const Page hdr = pf_->loadPage(pageNo);                           // the original reads the 0x5c-byte header only
    const int w = hdr.width ? static_cast<int>(hdr.width) : 640, h = hdr.height ? static_cast<int>(hdr.height) : 480;
    const int x = a.arg[1], y = a.arg[2];
    if (w + x > 640 || y + h > 480) {
        host_.log("BS4: Display Panel Page action: the page " + hdr.name + " at (" + std::to_string(x) + "," + std::to_string(y) +
                  ") does not fit the screen.");
        return false;
    }
    if (panel_.isActive()) {
        panel_.unload();
        panelRect_ = Bs4Rect();
        setHighlight("", 0);
    }
    panelOpener_ = a.name;                                            // +0x1b9c
    panel_.load(pageNo);
    panelRect_ = Bs4Rect{x, y, x + w, y + h};
    u32 mask = 0;
    if (main_.visibleMask(mask) && (mask & kPanelBit)) {              // exe:004080b0: the main page marks "panel up" with item 30
        bool changed = false;
        main_.showItems(30, true, &changed);
        main_.showItems(31, true, &changed);
        if (changed) main_.paint();
        hoverFramesOff_ = false;
    }
    if (loadingGame_) return true;
    compose();
    flip();
    host_.requestMouseMove();
    return true;
}

void Bs4::closePanelPage() {                                          // verb 0x1b
    if (!panel_.isActive()) return;
    panel_.unload();
    panelRect_ = Bs4Rect();
    setHighlight("", 0);
    main_.paint();
    compose();
    flip();
    host_.requestMouseMove();
}

// exe:00407c40: the six masks of a verb 0x17 (a1 show main, a2 show embedded, a3 show panel, a4 hide main, a5 hide embedded, a6 hide panel)
bool Bs4::showHideItems(const Bs4Action& a) {
    bool mainChanged = false, panelChanged = false;
    const u32 a1 = static_cast<u32>(a.arg[0]), a3 = static_cast<u32>(a.arg[2]), a4 = static_cast<u32>(a.arg[3]),
              a6 = static_cast<u32>(a.arg[5]);
    for (int n = 0; n < 32; n++) if ((a1 >> n) & 1u) main_.showItems(n, true, &mainChanged);
    if (a1 & kPanelBit) {
        if (panel_.isActive()) {
            panel_.unload();
            panelRect_ = Bs4Rect();
            setHighlight("", 0);
            mainChanged = true;
        }
        hoverFramesOff_ = true;
    }
    for (int n = 0; n < 32; n++) if ((a4 >> n) & 1u) main_.showItems(n, false, &mainChanged);
    if (a4 & kPanelBit) hoverFramesOff_ = false;
    // (the embedded page, a2 / a5, is not active: spec section 6)
    if (panel_.isActive()) {
        for (int n = 0; n < 32; n++) if ((a3 >> n) & 1u) panel_.showItems(n, true, &panelChanged);
        for (int n = 0; n < 32; n++) if ((a6 >> n) & 1u) panel_.showItems(n, false, &panelChanged);
    }
    if (mainChanged) {
        mainShown(main_);
        main_.paint();
    }
    if (panelChanged && panel_.isActive()) {
        panel_.initPanel();
        panel_.paint();
    }
    if (mainChanged || panelChanged) {
        compose();
        flip();
    }
    host_.requestMouseMove();
    return true;
}

// exe:00406bd0. Not ported on purpose: exe:00406510 OnRButtonUp (empty), exe:00408f60 CloseAnimations / exe:00408ff0 RetargetAnimations (the
// animation slots +0x18f4 are never filled), exe:00439330 the alarm sound stub (empty) and the embedded layer (exe:00407750 EmbedPageAction,
// exe:00407f70 ScrollEmbeddedAction, exe:004089d0, exe:00408ef0: dead in this BIOSYS4.DAT, spec section 6; verbs 0x14, 0x15, 0x19 throw).
// exe:004104e0 EnterBioSys4Mode, exe:004105c0 LeaveBioSys4Mode and exe:004106f0 / exe:00410730 (saved game palette) are engine glue.
bool Bs4::dispatchAction(Bs4Action& a) {
    if (!host_.appActive()) return false;
    lastHotspot_ = a.name;
    if (a.button >= 1 && a.button <= 14) host_.playClick(a.button);
    bool ok = true;
    switch (a.verb) {
    case 0x11:
    case 0x13:
        break;
    case 0x12: {
        if (!session_.emailPassword && pageEmail1_ == a.arg[0]) a.arg[0] = pageEmailPw_;
        if (!session_.synthPassword && pageSynth1_ == a.arg[0]) a.arg[0] = pageSynthPw_;
        bool loaded = false;
        try {
            loaded = loadMainPage(a.arg[0]);
        } catch (const Error& e) {
            host_.log(e.what());
        }
        if (!loaded) {
            host_.requestClose();
            ok = false;
        }
        break;
    }
    case 0x14:
        throw Error("BS4: verb 0x14 (embed a page) is not implemented: the embedded layer is dead code in this BIOSYS4.DAT (spec section 6)");
    case 0x15:
        throw Error("BS4: verb 0x15 (close the embedded page) is not implemented: the embedded layer is dead code (spec section 6)");
    case 0x16:
        ok = false;
        break;
    case 0x17:
        ok = showHideItems(a);
        break;
    case 0x18:
        return true;                                                  // the click handlers leave the terminal (Bs4Host::leave)
    case 0x19:
        throw Error("BS4: verb 0x19 (scroll the embedded page) is not implemented: the embedded layer is dead code (spec section 6)");
    case 0x1a:
        try {
            ok = displayPanel(a);
        } catch (const Error& e) {
            host_.log(e.what());
            ok = false;
        }
        break;
    case 0x1b:
        closePanelPage();
        break;
    default:
        host_.log("BS4: 'DispatchAction': unknown action verb " + std::to_string(static_cast<unsigned long>(a.verb)) + ".");
        return false;
    }
    if (!ok) return false;
    if (Bs4Registry::pageSpecific() && Bs4Registry::hooks().afterAction) Bs4Registry::hooks().afterAction(*this, a);    // exe:0040a760
    return true;
}

// ------------------------------------------------------------------------------------------------ input

void Bs4::onMouseMove(u32 flags, int x, int y) {                      // exe:00405ed0
    if (!main_.isActive()) return;
    lastMouseFlags_ = flags;
    cursorX_ = x; cursorY_ = y;
    const bool hooks = Bs4Registry::pageSpecific() && Bs4Registry::hooks().updateHover;
    Bs4Action act;
    bool hit = false;
    const bool fire = main_.mouseMove(flags, x, y, act, hit);
    if (hit) {
        embedded_.mouseLeave();
        panel_.mouseLeave();
    }
    if (fire) {
        if (act.verb == 0x18) { host_.leave(); return; }
        cursorDrawn_ = true;
        dispatchAction(act);
        if (hooks) Bs4Registry::hooks().updateHover(*this);
        return;
    }
    if (hooks) Bs4Registry::hooks().updateHover(*this);
    if (panel_.isActive()) {
        const Bs4Rect& r = panelRect_;
        if (x < r.left || r.right < x || y < r.top || r.bottom < y) {
            panel_.mouseLeave();
        } else {
            main_.mouseLeave();
            embedded_.mouseLeave();
            Bs4Action a2;
            bool h2 = false;
            panel_.mouseMove(flags, x - r.left, y - r.top, a2, h2);
        }
    }
    cursorDrawn_ = true;
    flip();
}

void Bs4::onLButtonUp(u32 flags, int x, int y) {                      // exe:00406210
    if (!main_.isActive()) return;
    Bs4Action a;
    if (main_.mouseUp(flags, x, y, a)) {
        if (a.verb == 0x18) host_.leave(); else dispatchAction(a);
        return;
    }
    if (panel_.isActive() && panel_.mouseUp(flags, x - panelRect_.left, y - panelRect_.top, a)) {
        if (a.verb == 0x18) host_.leave(); else dispatchAction(a);
    }
}

void Bs4::onLButtonDown(u32 flags, int x, int y) {                    // exe:00406390
    if (!main_.isActive()) return;
    Bs4Action a;
    if (main_.mouseDown(flags, x, y, a)) {
        if (a.verb == 0x18) host_.leave(); else dispatchAction(a);
        return;
    }
    if (panel_.isActive() && panel_.mouseDown(flags, x - panelRect_.left, y - panelRect_.top, a)) {
        if (a.verb == 0x18) host_.leave(); else dispatchAction(a);
    }
}

// exe:00405ce0. Text entry mode: letters (also 0xc0..0xdf), digits and space are appended (at most 16), Backspace deletes, Enter submits.
void Bs4::onKeyDown(int vkey, int repeat, int flags) {
    const Bs4Hooks& h = Bs4Registry::hooks();
    const bool specific = Bs4Registry::pageSpecific();
    if (textEntry) {
        const unsigned k = static_cast<unsigned>(vkey);
        const bool accepted = (k >= 0x41 && k <= 0x5a) || (k >= 0xc0 && k <= 0xdf) || k == 0x20 || (k >= 0x30 && k <= 0x39);
        if (accepted) {
            if (textBuffer.size() != 16) {
                textBuffer.push_back(static_cast<char>(vkey));
                if (specific && h.drawPasswordText) h.drawPasswordText(*this);
            }
        } else if (vkey == 8) {
            if (!textBuffer.empty()) {
                textBuffer.pop_back();
                if (specific && h.drawPasswordText) h.drawPasswordText(*this);
            }
        } else if (vkey == 0xd) {
            if (specific && h.submitPassword) h.submitPassword(*this);
        }
        return;
    }
    if (main_.isActive() && iequals(currentName_, "menuscreen") && vkey == 0xd) {
        int page = pf_->findPage("tech");
        loadMainPage(page);                                           // 0 (not found) fails like the original
    }
    host_.onKeyDown(vkey, repeat, flags);
}

void Bs4::onKeyUp(int vkey, int repeat, int flags) {                  // exe:00405e90 (F7 = screenshot: engine side)
    if (!textEntry) host_.onKeyUp(vkey, repeat, flags);
}

void Bs4::onTimer() {                                                 // exe:00406520, every 500 ms
    updateClockText(false, true);
    if (main_.isActive()) main_.tick();
    if (panel_.isActive()) {
        panel_.tick();
        std::string name;
        if (panel_.pendingReload(name)) {                             // a panel whose state changed is reloaded: a synthetic verb 0x1a
            Bs4Action a;
            a.rect = Bs4Rect{0, 0, 10, 10};
            a.verb = 0x1a;
            int n = 0;
            panel_.getPageNumber(n);
            a.arg[0] = n;
            a.arg[1] = panelRect_.left;
            a.arg[2] = panelRect_.top;
            a.name = name;
            dispatchAction(a);
        }
    }
}

// ------------------------------------------------------------------------------------------------ save / load

// exe:00407000
std::vector<u8> Bs4::saveSection() {
    std::vector<u8> b(static_cast<size_t>(kSaveSize), 0);
    putName(b, 0x000, 12, currentName_);
    // 0x00c: embedded page name (always empty), 0x024 its rect, 0x034/0x038 its scroll offset
    putRect(b, 0x024, Bs4Rect());
    std::string panelName;
    int n = 0;
    if (panel_.isActive() && panel_.getPageNumber(n)) panelName = pf_->entry(n).name;
    putName(b, 0x018, 12, panelName);
    if (panelName.empty()) {
        putRect(b, 0x03c, Bs4Rect());
    } else {
        putRect(b, 0x03c, panelRect_);
        putName(b, 0x04c, 11, panelOpener_);
    }
    for (int i = 0; i < 256; i++) {                                   // 0x057: the palette as 256 x {R,G,B,flags}
        const size_t o = 0x57 + 4 * static_cast<size_t>(i);
        b[o] = palette_.c[i].r; b[o + 1] = palette_.c[i].g; b[o + 2] = palette_.c[i].b; b[o + 3] = paletteFlags_[i];
    }
    const int idx[7] = {col_.white, col_.offWhite, col_.transparent, col_.grey, col_.red, col_.green, col_.lightGrey};
    for (int i = 0; i < 7; i++) putU32(b, 0x458 + 4 * static_cast<size_t>(i), static_cast<u32>(idx[i]));
    u32 mask = 0;
    main_.visibleMask(mask);
    putU32(b, 0x474, mask);
    putU32(b, 0x478, static_cast<u32>(main_.probeSensor));            // exe:0041e020
    putU32(b, 0x47c, static_cast<u32>(main_.probeRow));
    return b;
}

bool Bs4::loadSection(const u8* section, size_t size) { return loadSectionImpl(section, size); }

// exe:00407330
bool Bs4::loadSectionImpl(const u8* s, size_t size) {
    if (!s || size == 0) return true;
    if (size != static_cast<size_t>(kSaveSize)) {
        host_.log("BS4: Invalid saved game data specified.");
        return false;
    }
    loadingGame_ = true;
    ByteReader r(s, size);
    auto cstr = [&](size_t off, size_t cap) { return r.cstrAt(off, cap); };
    auto u32At = [&](size_t off) { return r.u32At(off); };
    for (int i = 0; i < 256; i++) {
        const size_t o = 0x57 + 4 * static_cast<size_t>(i);
        palette_.c[i].r = s[o]; palette_.c[i].g = s[o + 1]; palette_.c[i].b = s[o + 2]; paletteFlags_[i] = s[o + 3];
    }
    host_.setPalette(palette_);
    col_.white = static_cast<int>(u32At(0x458)); col_.offWhite = static_cast<int>(u32At(0x45c)); col_.transparent = static_cast<int>(u32At(0x460));
    col_.grey = static_cast<int>(u32At(0x464)); col_.red = static_cast<int>(u32At(0x468)); col_.green = static_cast<int>(u32At(0x46c));
    col_.lightGrey = static_cast<int>(u32At(0x470));
    savedMask_ = u32At(0x474); savedProbe_ = u32At(0x478); savedProbeRow_ = u32At(0x47c);
    const std::string mainName = cstr(0, 12), embeddedName = cstr(0xc, 12), panelName = cstr(0x18, 12);
    int first = pf_->findPage(mainName);                               // not found -> 0 -> the load fails
    bool ok = false;
    try {
        ok = loadMainPage(first);
        if (ok && !embeddedName.empty()) {
            host_.log("BS4: the saved game has an embedded page, which this port does not implement.");
            ok = false;
        }
        if (ok && !panelName.empty()) {
            Bs4Action a;
            a.rect = Bs4Rect{0, 0, 10, 10};
            a.verb = 0x1a;
            a.arg[0] = pf_->findPage(panelName);
            a.arg[1] = static_cast<i32>(u32At(0x3c));
            a.arg[2] = static_cast<i32>(u32At(0x40));
            a.name = cstr(0x4c, 11);
            ok = dispatchAction(a);
        }
    } catch (const Error& e) {
        host_.log(e.what());
        ok = false;
    }
    if (!ok) {
        loadingGame_ = false;
        return false;
    }
    clockLeft_.clear();
    clockRight_.clear();
    currentName_ = mainName;
    mainShown(main_);
    main_.paint();
    compose();
    flip();
    host_.requestMouseMove();
    loadingGame_ = false;
    return true;
}

bool Bs4::loadedProbeState(u32& sensor, u32& row) const {              // exe:00407720
    if (!loadingGame_) return false;
    sensor = savedProbe_;
    row = savedProbeRow_;
    return true;
}

} // namespace biosys
