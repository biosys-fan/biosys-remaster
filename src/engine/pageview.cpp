#include "engine/pageview.h"
#include "engine/textplane.h"
#include <algorithm>

namespace biosys {

struct PageView::PageData {
    Page page;
    std::vector<Ornament> orig;        // the ornament table as the file has it (run-time flags / order are reset from it)
    std::vector<Image8> images;
    std::vector<bool> loaded;
    struct Paint {                     // one DisplayImage since the page was entered
        u32 image;
        int x, y;
        u32 widthLimit;
        bool transparent;
        Color key;
        u32 id;
    };
    std::vector<Paint> paints;
    // GDI text and lines drawn on a top bar / popup window (API 0x3a): the original draws them into the window's own off-screen
    // surface once; here the page is composed from its ornaments on every frame, so they live in a layer (palette indices plus a
    // 1-byte-per-pixel "drawn" mask) that is put over the ornaments. Page loads and ornament paints over a pixel clear it.
    Surface layer;
    std::vector<u8> layerMask;
};

static int wrapMod(int v, int m) { return ((v % m) + m) % m; }

static bool clickableVerb(int v) {
    return v == VERB_BACK || v == VERB_JUMP || v == VERB_POPUP || v == VERB_CLOSEPOPUP || v == VERB_JUMP_SCROLL ||
           v == VERB_ALLOWOBJECTDROP || v == 14;
}

PageView::PageView(const DataFiles& files, const std::string& dataDir)
    : files_(files), pf_(new PageFile(files.requireData("pages.dat"))), table_(PaletteTable::load(dataDir)) {
    int tb = pf_->findPage("topbar");
    if (tb) topbarPre_ = load(tb);
}

int PageView::pageCount() const { return pf_->pageCount(); }
int PageView::findPage(const std::string& name) const { return pf_->findPage(name); }
std::string PageView::pageNameOf(int number) const {
    return number >= 1 && number <= pf_->pageCount() ? pf_->entry(number).name : std::string();
}
int PageView::pageNumber() const { return cur_ ? cur_->page.number : 0; }
const Page& PageView::page() const {
    if (!cur_) throw Error("no current page");
    return cur_->page;
}
bool PageView::panorama() const { return cur_ && cur_->page.panorama(); }
bool PageView::fullScreen() const { return cur_ && cur_->page.height == 480; }
bool PageView::scrollable() const {
    return cur_ && !fullScreen() && (cur_->page.pixelWidth() > Frame::W || cur_->page.pixelHeight() > ViewH);
}
bool PageView::popupOpen() const { return popup_ != nullptr; }
void PageView::closePopup() { dropPopup(); }
bool PageView::openTopbar() {
    if (topbar_ || !topbarPre_) return false;
    topbar_ = topbarPre_;
    resetImageState(*topbar_);
    if (onTopbarOpened) onTopbarOpened();
    return true;
}
void PageView::openPopup(int number) {
    if (number < 1 || number > pageCount()) throw Error("popup page out of range: " + std::to_string(number));
    if (topbarPre_ && number == topbarPre_->page.number) { openTopbar(); return; }
    setPopup(load(number));
}
bool PageView::displayPopupPage(int number) {
    if (number < 1 || number > pageCount()) return false;                         // GetPageOffsetAndLength fails
    if ((topbar_ && topbar_->page.number == number) || (popup_ && popup_->page.number == number)) return true;
    const std::shared_ptr<PageData> pd = load(number);
    if (pd->page.width == 0 || pd->page.height == 0) return false;               // "Width and/or height ... is zero"
    if (!pd->page.popup()) return false;                                          // "Page %lu is not a popup page"
    openPopup(number);
    return true;
}
bool PageView::isPopupPage(int number) {
    return number >= 1 && number <= pageCount() && load(number)->page.popup();
}
const Page* PageView::topbarPage() const { return topbar_ ? &topbar_->page : nullptr; }
const Page* PageView::popupPage() const { return popup_ ? &popup_->page : nullptr; }

static void forgetHotspotState(std::map<std::pair<const Page*, int>, bool>& m, const Page* p) {
    for (auto it = m.begin(); it != m.end();) it = (it->first.first == p) ? m.erase(it) : std::next(it);
}

bool PageView::setHotspotEnabled(const Page& page, const std::string& name, bool enable, bool prefix, int* previous) {
    bool any = false;
    for (size_t i = 0; i < page.hotspots.size(); i++) {
        const std::string& hn = page.hotspots[i].name;
        bool match = prefix ? (!name.empty() && hn.size() >= name.size() && iequals(hn.substr(0, name.size()), name))
                            : iequals(hn, name);
        if (!match) continue;
        if (previous && !prefix) *previous = hotspotEnabled(page, static_cast<int>(i)) ? 1 : 0;
        hsDisabled_[{&page, static_cast<int>(i)}] = !enable;
        any = true;
        if (!prefix) break;
    }
    return any;
}

void PageView::setAllHotspotsEnabled(const Page& page, bool enable) {
    for (size_t i = 0; i < page.hotspots.size(); i++) hsDisabled_[{&page, static_cast<int>(i)}] = !enable;
}

bool PageView::hotspotEnabled(const Page& page, int index) const {
    const PageData* pd = nullptr;
    for (const PageData* c : {cur_.get(), topbar_.get(), popup_.get()})
        if (c && &c->page == &page) pd = c;
    if (!pd || index < 0 || static_cast<size_t>(index) >= page.hotspots.size()) return false;
    return !query(*pd, index).disabled;
}

void PageView::dropPopup() {
    if (!popup_) return;
    popup_.reset();
    if (onPopupClosed) onPopupClosed();
}
void PageView::setPopup(std::shared_ptr<PageData> p) {
    dropPopup();
    popup_ = std::move(p);
    forgetHotspotState(hsDisabled_, &popup_->page);
    resetImageState(*popup_);
    if (onPopupOpened) onPopupOpened(popup_->page.pixelWidth(), popup_->page.pixelHeight());
}
int PageView::scrollX() const { return sx_; }
int PageView::scrollY() const { return sy_; }
int PageView::timeSlot() const { return slot_; }
int PageView::paletteSetOverride() const { return override_; }
int PageView::paletteSetCount() const { return table_.setCount(); }
bool PageView::showHidden() const { return showHidden_; }
void PageView::setShowHidden(bool on) {
    if (on == showHidden_) return;
    showHidden_ = on;
    if (cur_) bake(false);
}
const Palette* PageView::dayPalette() const {
    if (!cur_) return nullptr;
    // (from the page's own table rows: resolvePalette leaves activeSet_ alone while the game logic's palette is in charge)
    if (!table_.fixedPaletteForPage(cur_->page.name).empty()) return nullptr;
    const int eff = override_ >= 0 ? override_ : table_.setForPage(cur_->page.name);
    if (eff < 0) return nullptr;
    const std::string& rel = table_.set(eff).pal[0][SLOT_DAY];
    if (rel.empty()) return nullptr;
    auto it = dayCache_.find(rel);
    if (it == dayCache_.end()) {
        const std::string path = files_.findData(rel);
        if (path.empty()) return nullptr;
        try { it = dayCache_.emplace(rel, Palette::load(path)).first; } catch (const Error&) { return nullptr; }
    }
    return &it->second;
}
const Palette& PageView::palette() const { return forced_ ? *forced_ : apiActive_ ? apiPal_ : pal_; }
void PageView::setApiPalette(const Palette& p) {
    apiPal_ = p;
    apiActive_ = true;
}
void PageView::setGameLogicAttached(bool on, bool rebake) {
    if (on == attached_) return;
    attached_ = on;
    if (cur_ && rebake) bake(false);
}
void PageView::forcePalette(const Palette* p) { forced_ = p ? std::make_shared<Palette>(*p) : nullptr; }
const std::string& PageView::paletteFile() const { return palFile_; }
PaletteSource PageView::paletteSource() const { return palSrc_; }
bool PageView::paletteFellBack() const {
    return palSrc_ == PaletteSource::DayFallback || palSrc_ == PaletteSource::KeptOnFailure ||
           palSrc_ == PaletteSource::Default;
}

std::shared_ptr<PageView::PageData> PageView::load(int number) {
    auto it = cache_.find(number);
    if (it != cache_.end()) return it->second;
    if (cache_.size() > 8)
        for (auto i = cache_.begin(); i != cache_.end();) i = (i->second.use_count() == 1) ? cache_.erase(i) : std::next(i);
    auto pd = std::make_shared<PageData>();
    pd->page = pf_->loadPage(number);
    pd->orig = pd->page.ornaments;
    pd->images.resize(pd->page.images.size());
    pd->loaded.assign(pd->page.images.size(), false);
    cache_[number] = pd;
    return pd;
}

const Image8& PageView::image(PageData& pd, size_t i) {
    if (!pd.loaded[i]) {
        pd.images[i] = pf_->loadImage(pd.page, static_cast<int>(i));
        pd.loaded[i] = true;
    }
    return pd.images[i];
}

// exe:004456b0: the page on display becomes the return page; the surface is kept without the objects (they are painted again when
// the page comes back).
void PageView::saveForReturn() {
    if (saved_) dropSavedPage();
    if (!cur_) return;
    if (eraseObjects) eraseObjects();
    auto s = std::make_unique<SavedPage>();
    s->pd = cur_;
    s->surface = surface_;
    s->sx = sx_;
    s->sy = sy_;
    if (repaintObjects) repaintObjects();
    cache_.erase(cur_->page.number);                // the page record now belongs to the saved copy
    saved_ = std::move(s);
    if (onReturnSaved) onReturnSaved();
}

// exe:004457a0
void PageView::dropSavedPage() {
    if (!saved_) return;
    saved_.reset();
    if (onReturnDropped) onReturnDropped();
}

std::string PageView::savedPageName() const { return saved_ ? saved_->pd->page.name : std::string(); }

bool PageView::restoreSavedPage() {
    if (!saved_) return false;
    pageSerial_++;
    destName_ = saved_->pd->page.name;
    if (cur_ && onLeave) onLeave();
    dropPopup();
    std::unique_ptr<SavedPage> s = std::move(saved_);
    cur_ = s->pd;
    surface_ = std::move(s->surface);
    sx_ = s->sx;
    sy_ = s->sy;
    for (Slot& sl : slots_) sl = Slot();
    forced_.reset();
    resolvePalette();
    return true;
}

bool PageView::storedBigPage(int number) {
    if (number < 1 || number > pageCount()) return false;
    std::shared_ptr<PageData> pd = load(number);
    return pd->page.width == 0xa00 || pd->page.height == 0x400;
}

void PageView::enter(int number, bool push, const int* scroll) {
    pageSerial_++;
    std::shared_ptr<PageData> next = load(number);   // may throw: leave state untouched
    // exe:00411350: a pending zoom-in jump saves the page being left; otherwise a destination that is not a feature page (F, A, N, U)
    // discards the saved one
    if (saveNext_) {
        saveNext_ = false;
        saveForReturn();
    } else {
        const char c = next->page.name.empty() ? '\0' : next->page.name[0];
        if (c != 'F' && c != 'f' && c != 'A' && c != 'a' && c != 'N' && c != 'n' && c != 'U' && c != 'u') dropSavedPage();
    }
    if (saved_ && next == saved_->pd) next = load(number);   // (cannot happen from a zoom jump; the saved record is never reused)
    if (cur_) destName_ = next->page.name;
    if (push && cur_) {
        history_.push_back({cur_->page.number, sx_, sy_});
        if (history_.size() > 64) history_.erase(history_.begin());
    }
    if (cur_ && onLeave) onLeave();
    struct Hold { bool& h; ~Hold() { if (h) h = false; } } hold{holdTrack_};   // (the Engine's onBaked ends the hold after the compass event)
    holdTrack_ = true;
    dropPopup();
    forgetHotspotState(hsDisabled_, &next->page);
    resetImageState(*next);
    for (Slot& sl : slots_) sl = Slot();
    cur_ = next;
    sx_ = 0; sy_ = 0;
    if (cur_->page.panorama()) {
        sx_ = static_cast<int>(cur_->page.initialScrollX);
        sy_ = (cur_->page.pixelHeight() - ViewH) / 2;
        setScroll(sx_, sy_);
    }
    if (scroll) setScroll(scroll[0], scroll[1]);
    forced_.reset();
    resolvePalette();
    bake(true);
    if (holdTrack_) releaseTrack();
}

void PageView::goToPage(int number, bool pushHistory, const int* scroll) {
    if (number < 1 || number > pageCount()) throw Error("page number out of range: " + std::to_string(number));
    enter(number, pushHistory, scroll);
}

bool PageView::goBack() {
    dropPopup();
    while (!history_.empty()) {
        HistoryEntry e = history_.back();
        history_.pop_back();
        try {
            enter(e.page, false);
        } catch (const Error&) {
            continue;
        }
        setScroll(e.sx, e.sy);
        return true;
    }
    return false;
}

void PageView::setScroll(int x, int y, bool track) {
    if (!scrollable()) { sx_ = sy_ = 0; return; }
    int maxY = std::max(0, cur_->page.pixelHeight() - ViewH);
    if (panorama()) sx_ = wrapMod(x, cur_->page.pixelWidth());
    else sx_ = std::clamp(x, 0, std::max(0, cur_->page.pixelWidth() - Frame::W));
    sy_ = std::clamp(y, 0, maxY);
    if (track && !holdTrack_ && projected()) proj_.track(sx_, sy_);   // only a projected page is rendered through exe:0040cf00
}

void PageView::releaseTrack() {
    holdTrack_ = false;
    if (projected()) proj_.track(sx_, sy_);
}

bool PageView::projected() const {
    return cur_ && cur_->page.pixelWidth() == kPanoW && cur_->page.pixelHeight() == kPanoH;
}

void PageView::scrollBy(int dx, int dy) { setScroll(sx_ + dx, sy_ + dy); }

void PageView::setTimeSlot(int slot) {
    if (slot < 0 || slot >= SLOT_COUNT) return;
    slot_ = slot;
    apiActive_ = false;
    if (cur_) { resolvePalette(); bake(false); }
}

void PageView::setPaletteSetOverride(int set) {
    override_ = (set >= 0 && set < table_.setCount()) ? set : -1;
    apiActive_ = false;
    if (cur_) { resolvePalette(); bake(false); }
}

bool PageView::tryLoadPalette(const std::string& rel) {
    std::string path = files_.findData(rel);
    if (path.empty()) return false;
    try {
        pal_ = Palette::load(path);
    } catch (const Error&) {
        return false;
    }
    palFile_ = rel;
    havePal_ = true;
    return true;
}

// Which palette a page uses: exe-side tables in data/ (fixed file, else set group A for the time slot).
void PageView::resolvePalette() {
    if (apiActive_) return;   // the game logic's palette is in charge
    const std::string& name = cur_->page.name;
    std::string fixed = table_.fixedPaletteForPage(name);
    int set = table_.setForPage(name);
    if (!fixed.empty()) {
        activeFixed_ = fixed;
    } else if (set >= 0) {
        activeSet_ = set;
        activeFixed_.clear();
    }
    int eff = override_ >= 0 ? override_ : activeSet_;
    std::string want;
    if (!activeFixed_.empty()) want = activeFixed_;
    else if (eff >= 0) want = table_.set(eff).pal[0][slot_];

    bool specified = !want.empty();
    if (specified && tryLoadPalette(want)) {
        palSrc_ = activeFixed_.empty() ? PaletteSource::Set : PaletteSource::Fixed;
        return;
    }
    if (specified && activeFixed_.empty() && eff >= 0 && tryLoadPalette(table_.set(eff).pal[0][SLOT_DAY])) {
        palSrc_ = PaletteSource::DayFallback;
        return;
    }
    if (havePal_) {
        palSrc_ = specified ? PaletteSource::KeptOnFailure : PaletteSource::Kept;
        return;
    }
    if (tryLoadPalette("pub.pal")) palSrc_ = PaletteSource::Default;
    else palSrc_ = PaletteSource::Default;   // nothing loadable: pal_ stays all-black
}


u8 PageView::nearestIndex(int r, int g, int b) const {
    int best = 0;
    long bestD = -1;
    const Palette& pal = paintPalette();
    for (int i = 0; i < 256; i++) {
        long dr = pal.c[i].r - r, dg = pal.c[i].g - g, db = pal.c[i].b - b;
        long d = dr * dr + dg * dg + db * db;
        if (bestD < 0 || d < bestD) { bestD = d; best = i; }
    }
    return static_cast<u8>(best);
}

// Ornaments painted at page display, in table order. Guess: pages whose ornaments are all initially
// hidden (the original picks one from game state) are shown with one base picture: the preferred state
// variant if present, else the first ornament. Location pages carry state variants ("dead", "HEALTHY",
// "dflood", "hflood"...): until the game logic is ported, show the state a new game starts in - healthy
// everywhere except the dead biome (pages DB*), which starts dead.
std::vector<size_t> PageView::visibleOrnaments(const PageData& pd) const {
    bool anyVisible = false;
    for (const Ornament& o : pd.page.ornaments) anyVisible |= !o.hidden();
    size_t base = 0;
    if (attached_) anyVisible = true;   // game logic attached: exactly the flags of the page (exe:00401050)
    if (!anyVisible) {
        const bool deadBiome = pd.page.name.size() >= 2 && iequals(pd.page.name.substr(0, 2), "DB");
        const char* want = deadBiome ? "dead" : "healthy";
        for (size_t oi = 0; oi < pd.page.ornaments.size(); oi++)
            if (iequals(pd.page.ornaments[oi].name, want)) { base = oi; break; }
    }
    std::vector<size_t> out;
    for (size_t oi = 0; oi < pd.page.ornaments.size(); oi++) {
        const Ornament& o = pd.page.ornaments[oi];
        if (o.hidden() && !showHidden_ && (anyVisible || oi != base)) continue;
        if (o.type != 1 || o.image >= pd.images.size()) continue;
        out.push_back(oi);
    }
    return out;
}

// Top bar and popups: painted straight into the frame (they overlay other pictures).
void PageView::drawOrnaments(Frame& f, PageData& pd, int originX, int originY, const Rect& clip) {
    for (size_t oi : visibleOrnaments(pd)) {
        const Ornament& o = pd.page.ornaments[oi];
        const Image8& img = image(pd, o.image);
        int tr = o.transparentPaint() ? keyPalette().find(o.transparent) : -1;
        blit(f, img, o.x + originX, o.y + originY, clip, tr);
    }
    for (const PageData::Paint& pt : pd.paints) {
        const Image8& img = image(pd, pt.image);
        Image8 cut = img;
        const int w = static_cast<int>(std::min<u32>(static_cast<u32>(img.w), pt.widthLimit));
        if (w <= 0) continue;
        if (w < img.w) {
            cut.w = w;
            cut.px.clear();
            for (int y = 0; y < img.h; y++)
                cut.px.insert(cut.px.end(), img.px.begin() + static_cast<size_t>(y) * img.w, img.px.begin() + static_cast<size_t>(y) * img.w + w);
        }
        blit(f, cut, pt.x + originX, pt.y + originY, clip, pt.transparent ? keyPalette().find(pt.key) : -1);
    }
}

void PageView::drawLayer(Frame& f, const PageData& pd, int originX, int originY, const Rect& clip) {
    if (pd.layerMask.empty()) return;
    const int W = pd.layer.w, H = pd.layer.h;
    for (int y = 0; y < H; y++) {
        const int sy = y + originY;
        if (sy < clip.y || sy >= clip.y + clip.h || sy < 0 || sy >= Frame::H) continue;
        for (int x = 0; x < W; x++) {
            const int sx = x + originX;
            if (sx < clip.x || sx >= clip.x + clip.w || sx < 0 || sx >= Frame::W) continue;
            const size_t at = static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x);
            if (pd.layerMask[at]) f.px[static_cast<size_t>(sy) * Frame::W + static_cast<size_t>(sx)] = pd.layer.px[at];
        }
    }
}

bool PageView::drawTarget(int window, Surface*& surface, std::vector<u8>*& mask, std::shared_ptr<void>& owner) {
    owner.reset();
    if (window == -1) {
        if (!cur_) return false;
        surface = &surface_;
        mask = nullptr;
        return true;
    }
    PageData* pd = pageDataOf(window);
    if (!pd) return false;
    owner = window == 0 ? topbar_ : popup_;
    if (pd->layerMask.empty()) {
        pd->layer.init(pd->page.pixelWidth(), pd->page.pixelHeight(), 0);
        pd->layerMask.assign(pd->layer.px.size(), 0);
    }
    surface = &pd->layer;
    mask = &pd->layerMask;
    return true;
}

void PageView::textSurfaces(std::vector<TextSurface>& out) {
    if (cur_) {
        TextSurface t;
        t.surface = &surface_;
        t.window = -1;
        t.mapped = !projected();
        t.dx = -sx_;
        t.dy = viewTop() - sy_;
        out.push_back(t);
    }
    auto layer = [&](PageData* pd, int window, int dy, const char* name) {
        if (!pd || pd->layerMask.empty()) return;
        TextSurface t;
        t.surface = &pd->layer;
        t.mask = &pd->layerMask;
        t.window = window;
        t.dy = dy;
        t.name = name;
        out.push_back(t);
    };
    if (!fullScreen()) layer(topbar_.get(), 0, 0, "topbar");
    layer(popup_.get(), 1, viewTop(), "popup");
}

void PageView::paintRec(Surface& sf, const Image8& img, int x, int y, u32 widthLimit, int key) {
    const int w = static_cast<int>(std::min<u32>(static_cast<u32>(img.w), widthLimit));
    if (w <= 0) return;
    const bool pano = cur_ && cur_->page.panorama();
    const int W = sf.w;
    for (int k = pano ? -1 : 0; k <= (pano ? 1 : 0); k++) blitRegion(sf, img, x + k * W, y, 0, 0, w, img.h, key);
}

// The visible ornaments in table order (spec 1.3 draw order, step 1; TCPage::ReRender exe:00401cc0).
void PageView::paintOrnaments(PageData& pd) {
    for (size_t oi : visibleOrnaments(pd)) {
        const Ornament& o = pd.page.ornaments[oi];
        const Image8& img = image(pd, o.image);
        int tr = o.transparentPaint() ? keyPalette().find(o.transparent) : -1;
        paintRec(surface_, img, o.x, o.y, 0xffffffffu, tr);
    }
}

// Page surface: cleared, then the ornaments baked in table order, then the DisplayImage paints again.
void PageView::bake(bool newPage) {
    const Page& p = cur_->page;
    surface_.init(p.pixelWidth(), p.pixelHeight(), nearestIndex(0, 0, 0));
    if (newPage && onLoading) onLoading();   // exe:00401050: OnPageLoaded comes before the ornaments are painted
    paintOrnaments(*cur_);
    for (const PageData::Paint& pt : cur_->paints)
        paintRec(surface_, image(*cur_, pt.image), pt.x, pt.y, pt.widthLimit, pt.transparent ? keyPalette().find(pt.key) : -1);
    if (onBaked) onBaked(newPage);
}

std::string PageView::keyedReport() const {
    std::string out;
    if (!cur_) return out;
    PageView* self = const_cast<PageView*>(this);   // image() only fills its decode cache
    auto line = [&](const std::string& what, const Image8& img, int x, int y, u32 widthLimit, Color key) {
        const int k = keyPalette().find(key);
        const int w = static_cast<int>(std::min<u32>(static_cast<u32>(img.w), widthLimit));
        int keyPx = 0, shown = 0;
        for (int yy = 0; yy < img.h; yy++)
            for (int xx = 0; xx < w; xx++) {
                if (k < 0 || img.at(xx, yy) != k) continue;
                keyPx++;
                int sx = x + xx;
                if (cur_->page.panorama()) sx = wrapMod(sx, surface_.w);
                if (sx < 0 || sx >= surface_.w || y + yy < 0 || y + yy >= surface_.h) continue;
                if (surface_.row(y + yy)[sx] == k) shown++;
            }
        out += "keyed " + what + " key=" + std::to_string(k) + " keypx=" + std::to_string(keyPx) + " shown=" + std::to_string(shown) + "\n";
    };
    for (size_t oi : visibleOrnaments(*cur_)) {
        const Ornament& o = cur_->page.ornaments[oi];
        if (o.transparentPaint()) line(o.name, self->image(*cur_, o.image), o.x, o.y, 0xffffffffu, o.transparent);
    }
    for (const PageData::Paint& pt : cur_->paints)
        if (pt.transparent) line("paint:" + cur_->page.ornaments[std::min<size_t>(pt.image, cur_->page.ornaments.size() - 1)].name,
                                 self->image(*cur_, pt.image), pt.x, pt.y, pt.widthLimit, pt.key);
    return out;
}

void PageView::resetImageState(PageData& pd) {
    pd.page.ornaments = pd.orig;
    pd.paints.clear();
    std::fill(pd.layerMask.begin(), pd.layerMask.end(), 0);
    if (pd.layer.text) pd.layer.text->reset();
}

PageView::PageData* PageView::pageDataOf(int window) const {
    if (window == -1) return cur_.get();
    if (window == 0) return topbar_.get();
    if (window == 1) return popup_.get();
    return nullptr;
}

static int findOrnamentRec(const Page& p, const std::string& name) {
    for (size_t i = 0; i < p.ornaments.size(); i++)
        if (p.ornaments[i].type == 1 && iequals(p.ornaments[i].name, name)) return static_cast<int>(i);
    return -1;
}

int PageView::imageSlotsInUse() const {
    int n = 0;
    for (const Slot& s : slots_) n += s.used ? 1 : 0;
    return n;
}

// TCPage::DisplayImage exe:00402e00 + TCPage::PaintImage exe:00404150 / PaintDIBImage exe:0040e0d0.
int PageView::displayImage(int window, const std::string& name, const int* pos, u32 widthLimit, bool wantSlot, int* slotOut,
                           bool keyedRestore) {
    PageData* pd = pageDataOf(window);
    if (!pd) return window == -1 ? 7 : 3;
    const int oi = findOrnamentRec(pd->page, name);
    if (oi < 0) return 7;
    const Ornament orn = pd->page.ornaments[static_cast<size_t>(oi)];
    const bool main = window == -1;
    if (main && eraseObjects) eraseObjects();
    int slot = -1;
    int rc = 0;
    if (orn.image >= pd->images.size()) {
        rc = 7;   // "Failed to display image": PaintImage rejects an image number outside the table
    } else {
        const Image8& img = image(*pd, orn.image);
        const int x = pos ? pos[0] : orn.x, y = pos ? pos[1] : orn.y;
        const int tr = orn.transparentPaint() ? keyPalette().find(orn.transparent) : -1;
        if (main && wantSlot && slotOut) {
            for (int i = 0; i < 16; i++)
                if (!slots_[i].used) { slot = i; break; }
            if (slot >= 0) {
                Slot& sl = slots_[slot];
                sl = Slot();
                sl.used = true;
                const bool pano = cur_->page.panorama();
                for (int k = pano ? -1 : 0; k <= (pano ? 1 : 0); k++) {
                    Rect r = clipRect({x + k * surface_.w, y, img.w, img.h}, surface_.w, surface_.h);
                    if (r.w > 0 && r.h > 0) sl.parts.push_back({r, copyOut(surface_, r)});
                }
            }
        }
        PageData::Paint rec{orn.image, x, y, widthLimit, orn.transparentPaint(), orn.transparent, nextPaintId_++};
        if (main) {
            paintRec(surface_, img, x, y, widthLimit, tr);
            if (slot >= 0) {
                Slot& sl = slots_[slot];
                sl.paintId = rec.id;
                if (keyedRestore && orn.transparentPaint() && tr >= 0) {
                    for (auto& part : sl.parts)
                        for (int yy = 0; yy < part.first.h; yy++)
                            for (int xx = 0; xx < part.first.w; xx++) {
                                u8& u = part.second[static_cast<size_t>(yy) * part.first.w + xx];
                                if (u == surface_.row(part.first.y + yy)[part.first.x + xx]) u = static_cast<u8>(tr);
                            }
                    sl.key = tr;
                }
            }
        }
        pd->paints.push_back(rec);
        if (!main && !pd->layerMask.empty()) {      // the image is painted over what was drawn there
            const int w = static_cast<int>(std::min<u32>(static_cast<u32>(img.w), widthLimit));
            for (int yy = 0; yy < img.h && y + yy < pd->layer.h; yy++)
                for (int xx = 0; xx < w && x + xx < pd->layer.w; xx++) {
                    if (x + xx < 0 || y + yy < 0 || (tr >= 0 && img.at(xx, yy) == tr)) continue;
                    pd->layerMask[static_cast<size_t>(y + yy) * static_cast<size_t>(pd->layer.w) + static_cast<size_t>(x + xx)] = 0;
                }
        }
    }
    if (main && repaintObjects) repaintObjects();
    if (slotOut) *slotOut = rc == 0 ? slot : -1;
    return rc;
}

int PageView::displayImageNumber(int window, u32 index, const int* pos) {
    PageData* pd = pageDataOf(window);
    if (!pd) return window == -1 ? 7 : 3;
    if (index >= pd->page.ornaments.size()) return 7;   // "specified image number ... is out of range"
    const std::string name = pd->page.ornaments[index].name;
    return displayImage(window, name, pos, 0xffffffffu, false, nullptr, false);
}

// TCPage::SetImageVisibility exe:00403390: only the flag; the picture changes when the page is painted again.
int PageView::setImageVisibility(int window, const std::string& name, bool visible) {
    PageData* pd = pageDataOf(window);
    if (!pd) return window == -1 ? 7 : 3;
    const int oi = findOrnamentRec(pd->page, name);
    if (oi < 0) return 7;
    Ornament& o = pd->page.ornaments[static_cast<size_t>(oi)];
    if (visible) o.flags &= ~1u; else o.flags |= 1u;
    return 0;
}

// exe:00402d20: the record moves to the end of the table (painted last by the next full paint).
int PageView::raiseImage(int window, const std::string& name) {
    PageData* pd = pageDataOf(window);
    if (!pd) return window == -1 ? 7 : 3;
    const int oi = findOrnamentRec(pd->page, name);
    if (oi < 0) return 7;
    auto& v = pd->page.ornaments;
    std::rotate(v.begin() + oi, v.begin() + oi + 1, v.end());
    return 0;
}

// exe:00403250: put the saved picture back (keyed: only the pixels the image had changed) and free the slot.
int PageView::releaseImageSlot(int window, int slot) {
    PageData* pd = pageDataOf(window);
    if (!pd) return window == -1 ? 0x27 : 3;
    if (window != -1 || slot < 0 || slot > 15 || !slots_[slot].used) return 0x27;
    Slot& sl = slots_[slot];
    // (deliberate difference: the original puts the saved picture back over the objects without lifting them, which
    // leaves an object lying in the rectangle erased and its save-under stale; here they are lifted and put back)
    if (eraseObjects) eraseObjects();
    for (auto& part : sl.parts) {
        if (sl.key < 0) {
            copyIn(surface_, part.first, part.second);
        } else {
            for (int yy = 0; yy < part.first.h; yy++)
                for (int xx = 0; xx < part.first.w; xx++) {
                    u8 v = part.second[static_cast<size_t>(yy) * part.first.w + xx];
                    if (v != sl.key) surface_.row(part.first.y + yy)[part.first.x + xx] = v;
                }
        }
    }
    auto& ps = pd->paints;
    ps.erase(std::remove_if(ps.begin(), ps.end(), [&](const PageData::Paint& p) { return p.id == sl.paintId; }), ps.end());
    sl = Slot();
    if (repaintObjects) repaintObjects();
    return 0;
}

// exe:00411760: the visible ornaments are painted over what is there (no clearing), objects drawn again.
void PageView::redrawPage() {
    if (!cur_) return;
    paintOrnaments(*cur_);
    if (redrawObjects) redrawObjects();
}

int PageView::findOrnament(const std::string& name) const {
    if (!cur_) return -1;
    for (size_t i = 0; i < cur_->page.ornaments.size(); i++) {
        const Ornament& o = cur_->page.ornaments[i];
        if (o.type == 1 && iequals(o.name, name) && o.image < cur_->images.size()) return static_cast<int>(i);
    }
    return -1;
}

const Image8& PageView::ornamentPixels(int ornament) {
    return image(*cur_, cur_->page.ornaments.at(static_cast<size_t>(ornament)).image);
}

int PageView::ornamentKey(int ornament) const {
    const Ornament& o = cur_->page.ornaments.at(static_cast<size_t>(ornament));
    return o.transparentPaint() ? keyPalette().find(o.transparent) : -1;
}

// View = a 640x368 window of the page surface; x wraps on panoramas. Full-screen pages are shown whole.
void PageView::cutView(Frame& f) const {
    const int top = viewTop(), vh = fullScreen() ? Frame::H : ViewH;
    const int W = surface_.w, H = surface_.h;
    if (projected() && surface_.w == kPanoW && surface_.h == kPanoH) {
        // exe:00443f30: 2560 x 1024 pages go through the projection (640 x 368 indices)
        std::vector<u8>& tmp = bare_;
        tmp.resize(static_cast<size_t>(kViewW) * kViewH);
        proj_.render(surface_.px.data(), sx_, sy_, tmp.data());
        for (int r = 0; r < kViewH && top + r < Frame::H; r++)
            std::copy_n(&tmp[static_cast<size_t>(r) * kViewW], kViewW, &f.px[static_cast<size_t>(top + r) * Frame::W]);
        projected_.valid = true;
        projected_.bare = tmp.data();
        projected_.camera = proj_.lastCamera();
        projected_.rect = Rect{0, top, kViewW, kViewH};
        projected_.pageSerial = pageSerial_;
        return;
    }
    for (int r = 0; r < vh; r++) {
        int py = sy_ + r;
        if (py >= H) break;
        u8* d = &f.px[static_cast<size_t>(top + r) * Frame::W];
        const u8* s = surface_.row(py);
        for (int c = 0; c < Frame::W; c++) {
            int px = panorama() ? wrapMod(sx_ + c, W) : sx_ + c;
            if (px >= W) break;
            d[c] = s[px];
        }
    }
}

// Hit rectangles are inclusive (the right/bottom values of the file are the last pixel, spec 1.3).
static Rect hotRect(int l, int t, int r, int b, int dx, int dy) {
    return {l - dx, t - dy, r - l + 1, b - t + 1};
}

HotspotOverride PageView::baseQuery(const PageData& pd, int index) const {
    const Hotspot& h = pd.page.hotspots[static_cast<size_t>(index)];
    if (hotspotQuery) return hotspotQuery(pd.page.number, index, h);
    HotspotOverride o;
    o.disabled = h.flags & 1;
    return o;
}

HotspotOverride PageView::query(const PageData& pd, int index) const {
    HotspotOverride o = baseQuery(pd, index);
    auto it = hsDisabled_.find({&pd.page, index});
    if (it != hsDisabled_.end()) {
        // zones keep their own enabled state; the API flag can only disable them further
        if (pd.page.hotspots[static_cast<size_t>(index)].action.verb == VERB_ALLOWOBJECTDROP) o.disabled = o.disabled || it->second;
        else o.disabled = it->second;
    }
    return o;
}

void PageView::outlinePage(Frame& f, const PageData& pd, int scrollX, int scrollY, int originY, const Rect& clip, bool wrap) {
    u8 yellow = nearestIndex(255, 255, 0), red = nearestIndex(255, 0, 0), green = nearestIndex(0, 255, 0);
    const int W = pd.page.pixelWidth();
    for (size_t i = 0; i < pd.page.hotspots.size(); i++) {
        const Hotspot& h = pd.page.hotspots[i];
        HotspotOverride o = query(pd, static_cast<int>(i));
        if (o.disabled) continue;
        bool zone = h.action.verb == VERB_ALLOWOBJECTDROP;
        u8 c = zone ? green : clickableVerb(h.action.verb) ? yellow : red;
        int l = o.hasRect ? o.l : h.left, t = o.hasRect ? o.t : h.top, r = o.hasRect ? o.r : h.right, b = o.hasRect ? o.b : h.bottom;
        if (&pd == cur_.get() && projected() && !popup_) {
            // the rectangle's outline is curved on the projected view: map its perimeter point by point
            auto put = [&](int px, int py) {
                int sx, sy;
                if (proj_.forward(px, py, sx, sy) && sx >= 0 && sx < kViewW && sy >= 0 && sy < kViewH &&
                    clip.y + sy < Frame::H)
                    f.px[static_cast<size_t>(clip.y + sy) * Frame::W + sx] = c;
            };
            for (int x = l; x <= r; x++) { put(x, t); put(x, b); }
            for (int y = t; y <= b; y++) { put(l, y); put(r, y); }
            continue;
        }
        for (int k = wrap ? -1 : 0; k <= (wrap ? 1 : 0); k++)
            outlineRect(f, hotRect(l, t, r, b, scrollX - k * W, scrollY - originY), clip, c);
    }
}

void PageView::render(Frame& f, bool outlines) {
    projected_.valid = false;
    f.clear(nearestIndex(0, 0, 0));
    if (!cur_) return;
    int top = viewTop(), vh = fullScreen() ? Frame::H : ViewH;
    Rect view{0, top, Frame::W, vh};
    if (!fullScreen() && topbar_) {
        Rect tb{0, 0, Frame::W, TopBarH};
        drawOrnaments(f, *topbar_, 0, 0, tb);
        drawLayer(f, *topbar_, 0, 0, tb);
        if (outlines) outlinePage(f, *topbar_, 0, 0, 0, tb, false);
    }
    cutView(f);
    if (popup_) {
        drawOrnaments(f, *popup_, 0, top, view);
        drawLayer(f, *popup_, 0, top, view);
    }
    if (outlines) {
        if (popup_) outlinePage(f, *popup_, 0, 0, top, view, false);
        else outlinePage(f, *cur_, sx_, sy_, top, view, panorama());
    }
}

HitInfo PageView::hitPage(const PageData& pd, int px, int py, bool wrap) const {
    const int W = pd.page.pixelWidth();
    for (int i = static_cast<int>(pd.page.hotspots.size()) - 1; i >= 0; i--) {
        const Hotspot& h = pd.page.hotspots[static_cast<size_t>(i)];
        HotspotOverride o = query(pd, i);
        if (o.disabled) continue;
        int l = o.hasRect ? o.l : h.left, t = o.hasRect ? o.t : h.top, r = o.hasRect ? o.r : h.right, b = o.hasRect ? o.b : h.bottom;
        for (int k = wrap ? -1 : 0; k <= (wrap ? 1 : 0); k++) {
            int x = px + k * W;
            if (x >= l && x <= r && py >= t && py <= b) return {&pd.page, i, &h};
        }
    }
    return {};
}

bool PageView::viewToPage(int sx, int sy, int& px, int& py) const {
    if (!cur_) return false;
    int top = viewTop(), vh = fullScreen() ? Frame::H : ViewH;
    if (sx < 0 || sx >= Frame::W || sy < top || sy >= top + vh) return false;
    if (projected()) { proj_.inverse(sx, sy - top, px, py); return true; }   // exe:00403470
    px = sx_ + sx;
    if (panorama()) px = wrapMod(px, cur_->page.pixelWidth());
    py = sy_ + sy - top;
    return true;
}

HitInfo PageView::hit(int sx, int sy) const {
    if (!cur_ || sx < 0 || sx >= Frame::W || sy < 0 || sy >= Frame::H) return {};
    int top = viewTop();
    if (!fullScreen() && sy < TopBarH) return topbar_ ? hitPage(*topbar_, sx, sy, false) : HitInfo{};
    if (sy >= top + (fullScreen() ? Frame::H : ViewH)) return {};
    if (popup_) return hitPage(*popup_, sx, sy - top, false);
    int px, py;
    if (!viewToPage(sx, sy, px, py)) return {};
    return hitPage(*cur_, px, py, panorama() && !projected());   // the original compares without wrap (exe:004020e0)
}

ClickResult PageView::click(int sx, int sy) {
    HitInfo h = hit(sx, sy);
    bool inTopBar = !fullScreen() && sy < TopBarH;
    if (!h) {
        ClickResult r;
        if (popup_ && !inTopBar && sy >= viewTop() && sy < viewTop() + ViewH) {
            dropPopup();
            r.hit = true;
            r.text = "popup closed";
        }
        return r;
    }
    return perform(*h.hotspot);
}

ClickResult PageView::perform(const Hotspot& h) {
    ClickResult r;
    Action a = h.action;
    std::string hn = h.name;
    r.hit = true;
    auto desc = [&]() {
        std::string s = "verb " + std::to_string(a.verb) + " '" + hn + "' args";
        for (int i = 0; i < 8; i++) s += (i ? "," : " ") + std::to_string(a.arg[i]);
        return s;
    };
    try {
        switch (a.verb) {
        case VERB_BACK:
            r.text = goBack() ? "back" : "back: history empty";
            return r;
        case VERB_JUMP:
        case VERB_JUMP_SCROLL: {
            int target = a.arg[0];
            if (target < 1 || target > pageCount()) throw Error("bad jump target " + std::to_string(target));
            enter(target, true);
            // exe:0040d4f0: a3,a4 (arg[2], arg[3]) are the start position when > 0; a6,a7 (the post-jump scroll
            // animation) are run by Engine::runAction through Engine::postJumpShake.
            if (a.verb == VERB_JUMP_SCROLL && scrollable() && (a.arg[2] > 0 || a.arg[3] > 0))
                setScroll(a.arg[2] > 0 ? a.arg[2] : scrollX(), a.arg[3] > 0 ? a.arg[3] : scrollY());
            r.text = "jump to " + cur_->page.name + " (#" + std::to_string(target) + ")";
            return r;
        }
        case VERB_POPUP: {
            int target = a.arg[0];
            if (!displayPopupPage(target)) throw Error("DisplayPopupPage failed for page " + std::to_string(target));
            r.text = "popup " + pageNameOf(target);
            return r;
        }
        case VERB_CLOSEPOPUP:
            dropPopup();
            r.text = "popup closed";
            return r;
        default:
            r.implemented = false;
            r.text = "not implemented: " + desc();
            return r;
        }
    } catch (const Error& e) {
        r.text = std::string("error: ") + e.what() + " (" + desc() + ")";
        return r;
    }
}

} // namespace biosys
