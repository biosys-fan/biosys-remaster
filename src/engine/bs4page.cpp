#include "engine/bs4page.h"
#include "engine/bs4.h"
#include "engine/bs4widgets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace biosys {

// ---------------------------------------------------------------------------------------------------- helpers

bool bs4Iequals(const std::string& a, const std::string& b) { return iequals(a, b); }

bool bs4IstartsWith(const std::string& s, const char* prefix) {
    const size_t n = std::strlen(prefix);
    return s.size() >= n && iequals(s.substr(0, n), std::string(prefix));
}

static std::string itemPrefix(int n) { return std::to_string(n) + "_"; }          // exe:0048d020 "%lu_"
static bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }   // strncmp: case-sensitive

// ---------------------------------------------------------------------------------------------------- registry

namespace {
struct RegistryData {
    std::map<std::string, Bs4PanelDef> panels;      // key: lower case
    std::vector<Bs4MainExt> mainExts;
    Bs4Hooks hooks;
    bool pageSpecific = true;
};
RegistryData& reg() {
    static RegistryData d;
    return d;
}
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
} // namespace

void Bs4Registry::addPanel(const std::string& page, Bs4PanelDef def) { reg().panels[lower(page)] = std::move(def); }
void Bs4Registry::removePanel(const std::string& page) { reg().panels.erase(lower(page)); }
const Bs4PanelDef* Bs4Registry::findPanel(const std::string& page) {
    auto it = reg().panels.find(lower(page));
    return it == reg().panels.end() ? nullptr : &it->second;
}
void Bs4Registry::addMainExt(Bs4MainExt ext) { reg().mainExts.push_back(std::move(ext)); }
const std::vector<Bs4MainExt>& Bs4Registry::mainExts() { return reg().mainExts; }
Bs4Hooks& Bs4Registry::hooks() { return reg().hooks; }
void Bs4Registry::setPageSpecific(bool on) { reg().pageSpecific = on; }
bool Bs4Registry::pageSpecific() { return reg().pageSpecific; }

// ---------------------------------------------------------------------------------------------------- the page

Bs4Page::Bs4Page(Bs4& owner, Kind kind) : owner_(owner), kind_(kind) {}      // exe:0041a4b0, 0041a560

// exe:0041a4e0 Release / exe:0041c200 Unload
void Bs4Page::releaseAll() {
    owner_.releasePointer();                                      // ClipCursor(NULL)
    if (loopSound != -1) {
        owner_.loopStop(loopSound);                               // exe:0040c660
        loopSound = -1;
    }
    phSurface = Surface();
    ornRect_.clear();
    images_.clear();
    imageLoaded_.clear();
    handler = nullptr;
    loaded_ = false;
    active_ = false;
}

// exe:0041c200 Unload (the same work as Release exe:0041a4e0; the record and surface are dropped as well)
void Bs4Page::unload() {
    releaseAll();
    rec = Page();
    surf = Surface();
    hover_ = -1;
}

// helper of exe:0041dcd0 / 0041cd00 / 00438f70: the first type-1 ornament of that name
int Bs4Page::findOrnament(const std::string& n) const {
    for (size_t i = 0; i < rec.ornaments.size(); i++)
        if (rec.ornaments[i].type == 1 && iequals(rec.ornaments[i].name, n)) return static_cast<int>(i);
    return -1;
}

// exe:0041e530 GetImageOffset: images are addressed by index here; they are decoded when first used
const Image8& Bs4Page::image(int idx) {
    if (idx < 0 || static_cast<size_t>(idx) >= images_.size()) throw Error("BS4: image index out of range on page " + name_);
    if (!imageLoaded_[static_cast<size_t>(idx)]) {
        images_[static_cast<size_t>(idx)] = owner_.pageFile().loadImage(rec, idx);
        imageLoaded_[static_cast<size_t>(idx)] = 1;
    }
    return images_[static_cast<size_t>(idx)];
}

// exe:0041e040 PaintImage -> exe:0040e0d0 PaintDIBImage. An image with the transparent flag is blitted with the colour key; the key is
// the first palette entry equal to the ornament's colour (exe:00412530). Rows are clipped to the surface.
bool Bs4Page::paintImage(int imgIdx, int x, int y, bool transparent, Color key, int widthLimit, Bs4Rect* outRect) {
    if (!loaded_) return false;
    if (imgIdx < 0 || static_cast<size_t>(imgIdx) >= rec.images.size()) return false;
    const Image8& im = image(imgIdx);
    int keyIdx = -1;
    if (transparent) {
        keyIdx = owner_.palette().find(key);
        if (keyIdx < 0) {
            owner_.log("Transparent colour " + std::to_string(key.r) + "," + std::to_string(key.g) + "," + std::to_string(key.b) +
                       " does not exist in the palette");
        }
    }
    if (widthLimit == 0) return true;                                // exe:0040e0d0: param_6 == 0 -> 1
    int w = im.w;
    if (widthLimit > 0 && widthLimit < w) w = widthLimit;
    const int x0 = std::max(x, 0), y0 = std::max(y, 0);
    const int x1 = std::min(x + w, surf.w), y1 = std::min(y + im.h, surf.h);
    for (int yy = y0; yy < y1; yy++) {
        const u8* s = &im.px[static_cast<size_t>(yy - y) * im.w + (x0 - x)];
        u8* d = surf.row(yy) + x0;
        const int n = x1 - x0;
        if (keyIdx < 0) std::copy(s, s + n, d);
        else for (int i = 0; i < n; i++) if (s[i] != keyIdx) d[i] = s[i];
    }
    if (outRect) *outRect = Bs4Rect{x, y, x + im.w, y + im.h};
    return true;
}

// exe:0041cd00
bool Bs4Page::paintOrnamentByName(const std::string& n, const int* pos, int widthLimit) {
    if (!loaded_) return false;
    const int i = findOrnament(n);
    if (i < 0) return false;
    const Ornament& o = rec.ornaments[static_cast<size_t>(i)];
    const int px = pos ? pos[0] : o.x, py = pos ? pos[1] : o.y;
    Bs4Rect out;
    if (!paintImage(static_cast<int>(o.image), px, py, o.transparentPaint(), o.transparent, widthLimit, &out)) {
        owner_.log("BS4: Failed to display image " + std::to_string(o.image) + " of page " + name_);
        return false;
    }
    return true;
}

// ---- items by index ---------------------------------------------------------------------------------------------------

// exe:0041cab0 ShowItemsByIndex
bool Bs4Page::showItems(int n, bool show, bool* changed) {
    if (!loaded_) return false;
    const std::string pre = itemPrefix(n);
    for (Hotspot& h : rec.hotspots)
        if (startsWith(h.name, pre)) {
            if (show) h.flags &= ~1u; else h.flags |= 1u;
        }
    for (Ornament& o : rec.ornaments)
        if (o.type == 1 && startsWith(o.name, pre)) {
            if (show) {
                if (o.flags & 1) { if (changed) *changed = true; o.flags &= ~1u; }
            } else if (!(o.flags & 1)) {
                if (changed) *changed = true;
                o.flags |= 1u;
            }
        }
    return true;
}

// exe:0041ce00 GetVisibleMask: item n is visible when its first hotspot is enabled, else (no hotspot) its first ornament is shown
bool Bs4Page::visibleMask(u32& mask) const {
    if (!loaded_) return false;
    mask = 0;
    for (int n = 0; n < 32; n++) {
        const std::string pre = itemPrefix(n);
        bool found = false;
        for (const Hotspot& h : rec.hotspots)
            if (startsWith(h.name, pre)) {
                if (!(h.flags & 1)) mask |= 1u << n;
                found = true;
                break;
            }
        if (found) continue;
        for (const Ornament& o : rec.ornaments)
            if (o.type == 1 && startsWith(o.name, pre)) {
                if (!(o.flags & 1)) mask |= 1u << n;
                break;
            }
    }
    return true;
}

// exe:0041cf40 ShowAllItems(mask)
bool Bs4Page::applyMask(u32 mask, bool* changed) {
    if (!loaded_) return false;
    for (int n = 0; n < 32; n++) showItems(n, (mask >> n) & 1u, changed);
    return true;
}

bool Bs4Page::setOrnamentVisible(const std::string& n, bool show) {      // exe:0041dcd0
    if (!loaded_) return false;
    const int i = findOrnament(n);
    if (i < 0) return false;
    Ornament& o = rec.ornaments[static_cast<size_t>(i)];
    if (show) o.flags &= ~1u; else o.flags |= 1u;
    return true;
}

bool Bs4Page::setHotspotEnabled(const std::string& n, bool enabled) {    // exe:0041dd60
    if (!loaded_) return false;
    for (Hotspot& h : rec.hotspots)
        if (iequals(h.name, n)) {
            if (enabled) h.flags &= ~1u; else h.flags |= 1u;
            return true;
        }
    return false;
}

bool Bs4Page::hotspotRect(const std::string& n, Bs4Rect& r, bool& enabled) const {     // exe:0041cf90
    if (!active_) return false;
    for (const Hotspot& h : rec.hotspots)
        if (iequals(h.name, n)) {
            r = Bs4Rect{h.left, h.top, h.right, h.bottom};
            enabled = !(h.flags & 1);
            return true;
        }
    return false;
}

bool Bs4Page::hoverName(std::string& out) const {                            // exe:0041cc90
    if (!active_ || rec.hotspots.empty()) return false;
    if (hover_ < 0 || static_cast<size_t>(hover_) >= rec.hotspots.size()) out.clear();
    else out = rec.hotspots[static_cast<size_t>(hover_)].name;
    return true;
}

bool Bs4Page::hitVisibleOrnament(int x, int y) const {                       // exe:0041cbe0
    if (!active_) return false;
    for (size_t i = 0; i < rec.ornaments.size(); i++) {
        const Ornament& o = rec.ornaments[i];
        if (o.type == 1 && !(o.flags & 1)) {
            const Bs4Rect& r = ornRect_[i];
            if (r.left != -1 && r.left <= x && x <= r.right && r.top <= y && y <= r.bottom) return true;
        }
    }
    return false;
}

// ---- input -------------------------------------------------------------------------------------------------------------

static void fillAction(const Hotspot& h, Bs4Action& a, bool withButton) {
    a.rect = Bs4Rect{h.left, h.top, h.right, h.bottom};
    a.button = withButton ? static_cast<int>(h.button) : 0;
    a.verb = h.action.verb;
    for (int i = 0; i < 8; i++) a.arg[i] = h.action.arg[i];
    a.name = h.name;
}

// exe:0041c290: hotspots are searched from the last to the first, disabled ones skipped; rects are inclusive
bool Bs4Page::mouseMove(u32 flags, int x, int y, Bs4Action& act, bool& hit) {
    hit = false;
    owner_.setCursorKind(0);
    if (!active_) return false;
    if (handler) {
        mouseFlags = flags; mouseEvent = 1; mouseX = x; mouseY = y;
        handler(3);
        if (mayContinue == 0) return false;
    }
    for (int i = static_cast<int>(rec.hotspots.size()) - 1; i >= 0; i--) {
        const Hotspot& h = rec.hotspots[static_cast<size_t>(i)];
        if (h.flags & 1) continue;
        if (h.left <= x && x <= h.right && h.top <= y && y <= h.bottom) {
            hover_ = i;
            hit = true;
            owner_.setCursorKind(2);
            if (h.action.verb != 0x19 || !(flags & 1) || kind_ == kEmbedded) return false;
            fillAction(h, act, false);
            return true;
        }
    }
    hover_ = -1;
    return false;
}

// exe:0041c470: fires the hotspot that the last move left in +0x2c, unless its verb is 0x19
bool Bs4Page::mouseUp(u32 flags, int x, int y, Bs4Action& act) {
    if (!active_) return false;
    if (handler) {
        mouseEvent = 2; mouseFlags = flags; mouseX = x; mouseY = y;
        handler(3);
        if (mayContinue == 0) return false;
    }
    if (rec.hotspots.empty()) return false;
    if (hover_ < 0 || static_cast<size_t>(hover_) >= rec.hotspots.size()) return false;
    const Hotspot& h = rec.hotspots[static_cast<size_t>(hover_)];
    if (h.action.verb == 0x19) return false;
    fillAction(h, act, true);
    return true;
}

// exe:0041c580: only an auto-repeat verb 0x19 reacts to a button press
bool Bs4Page::mouseDown(u32 flags, int x, int y, Bs4Action& act) {
    if (!active_) return false;
    if (handler) {
        mouseEvent = 3; mouseFlags = flags; mouseX = x; mouseY = y;
        handler(3);
        if (mayContinue == 0) return false;
    }
    if (rec.hotspots.empty()) return false;
    if (hover_ < 0 || static_cast<size_t>(hover_) >= rec.hotspots.size()) return false;
    const Hotspot& h = rec.hotspots[static_cast<size_t>(hover_)];
    if (h.action.verb != 0x19 || kind_ == kEmbedded) return false;
    fillAction(h, act, false);
    return true;
}

// exe:0041d070
void Bs4Page::mouseLeave() {
    if (!active_) return;
    hover_ = -1;
    if (handler) {
        mouseEvent = 4;
        handler(3);
    }
}

// exe:0041dc70
bool Bs4Page::pendingReload(std::string& n) const {
    if (kind_ != kPanel || !loaded_) return false;
    if (!reload) return false;
    n = reloadName;
    return true;
}

// ---- feature access ----------------------------------------------------------------------------------------------------

// event 27 (exe:004417b0) with the failure message of the callers
bool Bs4Page::getParam(const std::string& featureName, int id, u32& outInt, double& outValue) {
    if (owner_.host().onGetFeatureParameter(featureName, id, outInt, outValue)) return true;
    owner_.log("BS4: Failed to get the value of parameter " + std::to_string(id) + " for BiosFear feature '" + featureName + "'.");
    return false;
}

// event 26 (exe:00441720)
bool Bs4Page::setParam(const std::string& featureName, int id, int a, int b, int c) {
    if (owner_.host().onSetFeatureParameter(featureName, id, a, b, c)) return true;
    owner_.log("BS4: Failed to set the value of parameter " + std::to_string(id) + " of BiosFear feature '" + featureName + "'.");
    return false;
}

// event 26 with a double: (0, low dword, high dword) as in exe:00437e90
bool Bs4Page::setParamValue(const std::string& featureName, int id, double value) {
    uint64_t bits;
    std::memcpy(&bits, &value, 8);
    if (owner_.host().onSetFeatureParameter(featureName, id, 0, static_cast<int>(bits & 0xffffffffu), static_cast<int>(bits >> 32))) return true;
    owner_.log("BS4: Failed to set the value of parameter " + std::to_string(id) + " of BiosFear feature '" + featureName + "'.");
    return false;
}

void Bs4Page::repaintScreen() { owner_.needRepaint(); }
void Bs4Page::fill(const Bs4Rect& r, int idx) { owner_.fillRect(surf, r, idx); }
// exe:004124e0 + GDI DrawTextA with the terminal font (spec 1.10)
void Bs4Page::drawText(const Bs4Rect& r, const std::string& text, int red, int green, int blue, unsigned flags) {
    owner_.drawText(surf, r, text, red, green, blue, flags);
}

// ---- load / paint ------------------------------------------------------------------------------------------------------

// exe:0041a580. The original resets ~0x600 bytes of fields; the members are reset here one by one.
void Bs4Page::load(int pageNo) {
    releaseAll();
    rec = Page();
    surf = Surface();
    rec = owner_.pageFile().loadPage(pageNo);
    width_ = rec.width ? static_cast<int>(rec.width) : 640;          // exe:0041a580: 0 means 640 x 480 (not the book size of PAGES.DAT)
    height_ = rec.height ? static_cast<int>(rec.height) : 480;
    if (kind_ == kMain && (width_ != 640 || height_ != 480))
        throw Error("BS4: Size of main page '" + rec.name + "' is not 640x480");
    number_ = pageNo;
    name_ = rec.name;
    hover_ = -1;
    ornRect_.assign(rec.ornaments.size(), Bs4Rect());
    images_.assign(rec.images.size(), Image8());
    imageLoaded_.assign(rec.images.size(), 0);
    surf.init(width_, height_, static_cast<u8>(owner_.colours().transparent));

    // reset of the page fields (exe:0041a580 start)
    feature.clear(); feature2.clear();
    handler = nullptr;
    mouseFlags = 0; mouseX = mouseY = 0; mouseEvent = 0; mayContinue = 1;
    cached = false; cachedMask = 0; devOn = false; devActive = false; devOpen = false; alarmBits = 0; reload = false; reloadName.clear();
    loopSound = -1;
    markOn = markOff = dangerLev = litLev = broke = litBroke = levImage = -1;
    levPos[0] = levPos[1] = brokePos[0] = brokePos[1] = 0;
    ewaitImage = ewaitOffImage = numailImage = -1;
    ewaitPos[0] = ewaitPos[1] = numailPos[0] = numailPos[1] = 0;
    deviceRow = alarmRow = probeSensor = probeRow = -1;
    probeLast = 0; phPos[0] = phPos[1] = 0;
    counter = 0; lastTick = 0;
    gaugePresent[0] = gaugePresent[1] = gaugePresent[2] = false;
    gauge[0] = gauge[1] = gauge[2] = Bs4GaugeSlot();
    for (auto& g : extraGauge) g = Bs4GaugeSlot();
    tierName.clear(); tierImage = -1; tierPos[0] = tierPos[1] = 0;
    timeSlider = Bs4TimeSlider(); schedule = Bs4ScheduleSlider(); range = Bs4RangeSlider();
    dragOffset = dragStartX = sliderY = 0;

    loaded_ = true;                                                  // +0x08
    const bool specific = Bs4Registry::pageSpecific();
    if (specific && Bs4Registry::hooks().onPageLoaded) Bs4Registry::hooks().onPageLoaded(owner_, name_, kind_);   // exe:00409720
    if (kind_ == kMain && owner_.loadingGame_) applyMask(owner_.savedMask_);     // the kind-1 half of exe:00409720: restore the saved item mask

    if (kind_ == kPanel && specific) {
        initPanel();
        if (const Bs4PanelDef* d = Bs4Registry::findPanel(name_))
            if (d->onLoad && bs4IstartsWith(name_, "term")) d->onLoad(*this);
        if (bs4IstartsWith(name_, "spec")) {                          // spec pages: no highlight, repaint the main page
            owner_.setHighlight("", 0);
            owner_.main_.paint();
            owner_.needRepaint();
        }
    }
    if (kind_ == kMain) initMainGauges();
    if (kind_ == kMain && specific)
        for (const Bs4MainExt& e : Bs4Registry::mainExts())
            if (e.afterLoad && (e.page.empty() || iequals(e.page, name_))) e.afterLoad(*this);

    // paint (the same walk as Paint, plus the registration of the well-known ornaments by name)
    if (kind_ != kEmbedded) std::fill(surf.px.begin(), surf.px.end(), static_cast<u8>(owner_.colours().transparent));
    for (size_t i = 0; i < rec.ornaments.size(); i++) {
        const Ornament& o = rec.ornaments[i];
        if (o.type == 1) {
            const int img = static_cast<int>(o.image);
            if (iequals(o.name, "markon")) markOn = img;
            else if (iequals(o.name, "markof")) markOff = img;
            else if (iequals(o.name, "dangerlev")) dangerLev = img;
            else if (iequals(o.name, "lev")) { levImage = img; levPos[0] = o.x; levPos[1] = o.y; }
            else if (iequals(o.name, "litlev")) litLev = img;
            else if (iequals(o.name, "broke")) { broke = img; brokePos[0] = o.x; brokePos[1] = o.y; }
            else if (iequals(o.name, "litbro")) litBroke = img;
            else if (iequals(o.name, "ewait")) { ewaitImage = img; ewaitPos[0] = o.x; ewaitPos[1] = o.y; }
            else if (iequals(o.name, "ewaitoff")) ewaitOffImage = img;
            else if (iequals(o.name, "numail")) { numailImage = img; numailPos[0] = o.x; numailPos[1] = o.y; }
            if (!(o.flags & 1) &&
                !paintImage(img, o.x, o.y, o.transparentPaint(), o.transparent, -1, &ornRect_[i]))
                throw Error("BS4: Failed to paint image " + std::to_string(o.image) + " of page " + name_);
        } else if (o.type != 0 && o.type != 2) {
            owner_.log("BS4: Unknown ornament type " + std::to_string(o.type) + " (ornament " + o.name + ")");
        }
    }
    if (handler) handler(1);
    if (kind_ == kMain) {
        if (specific)
            for (const Bs4MainExt& e : Bs4Registry::mainExts())
                if (e.paint && (e.page.empty() || iequals(e.page, name_))) e.paint(*this);     // 004399b0, 00439d70
        for (int g = 0; g < 3; g++)
            if (gaugePresent[g]) bs4w::barGauge(*this, gauge[g], 1);
        if (specific)
            for (const Bs4MainExt& e : Bs4Registry::mainExts())
                if (e.loaded && (e.page.empty() || iequals(e.page, name_))) e.loaded(*this);   // 0043b990(0)
    }
    counter = 0;
    lastTick = owner_.host().tickCount();
    active_ = true;                                                  // +0x10
}

// exe:0041a580, kind 1: the three power gauges (names reservebar / powconbar / powgenbar, parameters 0x3f3 / 0x3f4 / 0x3f2) and the
// world alarm bits (0x41a)
void Bs4Page::initMainGauges() {
    struct Def { const char* ornament; int id; const char* what; };
    static const Def defs[3] = {{"reservebar", 0x3f3, "WORLD_POWER_RESERVE"}, {"powconbar", 0x3f4, "WORLD_POWER_CONSUMPTION"},
                                {"powgenbar", 0x3f2, "WORLD_POWER_GENERATION"}};
    for (int g = 0; g < 3; g++) {
        const int oi = findOrnament(defs[g].ornament);
        if (oi < 0) continue;
        const Ornament& o = rec.ornaments[static_cast<size_t>(oi)];
        u32 vi = 0;
        double v = 0;
        if (!owner_.host().onGetFeatureParameter("", defs[g].id, vi, v)) {
            owner_.log(std::string("BS4: Failed to get the value of ") + defs[g].what + ".");
            continue;
        }
        if (v < 0.0 || 200.0 < v) {                                  // the original clamps the value it just read (asm 0041ad99..0041ae2e)
            owner_.log(std::string("BS4: Value of parameter ") + defs[g].what + " is out of range.");
            if (v < 0.0) v = 0.0;
            if (200.0 < v) v = 200.0;
        }
        Bs4GaugeSlot& s = gauge[g];
        s.last = v;
        s.text = Bs4Rect();
        s.min = 0; s.max = 200.0;
        s.image = static_cast<int>(o.image);
        s.bar = Bs4Rect{o.x, o.y, o.x + 0x97, o.y + 4};
        s.paramId = defs[g].id;
        s.percent = bs4w::gaugePercent(v, s.min, s.max);
        gaugePresent[g] = true;
    }
    owner_.worldAlarm0 = owner_.worldAlarm1 = false;
    u32 bits = 0;
    double d = 0;
    if (!owner_.host().onGetFeatureParameter("", 0x41a, bits, d)) owner_.log("BS4: Failed to get the value of parameter WORLD_ALARMS.");
    else {
        owner_.worldAlarm0 = (bits & 1) != 0;
        owner_.worldAlarm1 = (bits & 2) != 0;
    }
}

// exe:0041baf0
bool Bs4Page::paint() {
    if (!active_) return false;
    const bool specific = Bs4Registry::pageSpecific();
    if (kind_ != kEmbedded) std::fill(surf.px.begin(), surf.px.end(), static_cast<u8>(owner_.colours().transparent));
    for (size_t i = 0; i < rec.ornaments.size(); i++) {
        const Ornament& o = rec.ornaments[i];
        if (o.type == 1) {
            if (!(o.flags & 1) &&
                !paintImage(static_cast<int>(o.image), o.x, o.y, o.transparentPaint(), o.transparent, -1, &ornRect_[i]))
                throw Error("BS4: Failed to paint image " + std::to_string(o.image) + " of page " + name_);
        } else if (o.type != 0 && o.type != 2) {
            owner_.log("BS4: Unknown ornament type " + std::to_string(o.type) + " (ornament " + o.name + ")");
        }
    }
    if (handler) handler(1);
    if (kind_ == kMain) {
        for (int g = 0; g < 3; g++)
            if (gaugePresent[g]) bs4w::barGauge(*this, gauge[g], 1);
        const std::string& hn = owner_.highlightName();
        if (!hn.empty() && (visibleMask() & (1u << (owner_.highlightBit() & 31)))) paintOrnamentByName(hn, nullptr, -1);
        if (specific)
            for (const Bs4MainExt& e : Bs4Registry::mainExts())
                if (e.paint && (e.page.empty() || iequals(e.page, name_))) e.paint(*this);     // overlays, 004399b0, 00439d70, (e), (f)
    }
    return true;
}

// exe:0041c690
void Bs4Page::tick() {
    if (!active_) return;
    const u32 now = owner_.host().tickCount();
    if (now - lastTick <= 999) return;
    counter++;
    lastTick = now;
    if (kind_ == kMain) {
        for (int g = 0; g < 3; g++)
            if (gaugePresent[g]) bs4w::barGauge(*this, gauge[g], 2);
        if (Bs4Registry::pageSpecific())
            for (const Bs4MainExt& e : Bs4Registry::mainExts())
                if (e.tick && (e.page.empty() || iequals(e.page, name_))) e.tick(*this);
        u32 bits = 0;
        double d = 0;
        if (!owner_.host().onGetFeatureParameter("", 0x41a, bits, d)) {
            owner_.log("BS4: Failed to get the value of parameter WORLD_ALARMS.");
        } else {
            owner_.worldAlarm0 = (bits & 1) != 0;
            owner_.worldAlarm1 = (bits & 2) != 0;      // exe:00439330 (the alarm sound hook) is an empty stub
        }
    }
    if (kind_ == kPanel && handler) handler(2);
}

// exe:0041d0a0 InitPanel
void Bs4Page::initPanel() {
    if (kind_ != kPanel || !loaded_) return;
    if (!Bs4Registry::pageSpecific()) return;
    if (bs4IstartsWith(name_, "term") || bs4IstartsWith(name_, "spec") || iequals(name_, "alarmlev") || iequals(name_, "alarmbrk")) return;
    const u32 mask = visibleMask();
    const Bs4PanelDef* def = Bs4Registry::findPanel(name_);
    // the variant of a visible mask: the lowest set bit among 3..14, else 24 (0 = none)
    const auto variantOf = [](u32 m) {
        for (int v = 3; v < 15; v++) if (m & (1u << v)) return v;
        return (m & (1u << 24)) ? 24 : 0;
    };
    bool again = false;
    if (cached) {
        if (mask == cachedMask) return;
        // The original keeps the device it was initialised for: a variant tab (verb 0x17 on the panel) switches the pictures of the
        // panel to another device, but only the state buttons are looked at, so controls and readouts stay on the first device.
        // Bs4Fixes::panelFollowsVariant: the panel is initialised again for the variant it shows now (the tab pictures, the feature,
        // the highlight on the main page, the loop sound and the readouts are all one device's).
        const int vNow = variantOf(mask);
        again = owner_.fixes().panelFollowsVariant && def && def->variants && def->init && vNow != 0 && vNow != variantOf(cachedMask);
        if (!again) {
            if (((mask ^ cachedMask) & 0xfeff8007u) != 0 && def && def->setter) def->setter(*this, mask, cachedMask);   // state buttons
            cachedMask = visibleMask();
            return;
        }
        handler = nullptr;
        reload = false;
    }
    owner_.setHighlight("", 0);                                       // +0x1bec = 0
    if (loopSound != -1) {
        owner_.loopStop(loopSound);
        loopSound = -1;
    }
    const int variant = variantOf(mask);
    if (variant) {
        if (def && def->variants && def->init) def->init(*this, variant);
    } else if (def && !def->variants && def->init) {
        def->init(*this, 0);
    } else if (!def || def->variants) {
        owner_.log("BS4: Failed to find a group in the page '" + name_ + "'.");
        cached = true;
        cachedMask = mask;
        return;
    }
    cachedMask = visibleMask();
    cached = true;
    owner_.main_.paint();                                              // repaint the main page (the highlight may have changed)
    owner_.needRepaint();
}

} // namespace biosys
