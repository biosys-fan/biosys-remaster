#include "app/clean.h"
#include "engine/panowide.h"
#include <algorithm>
#include <cstring>

namespace biosys {

namespace {
bool samePal(const Palette& a, const Palette& b) {
    for (int i = 0; i < 256; i++)
        if (!(a.c[i] == b.c[i])) return false;
    return true;
}
double msSince(std::chrono::steady_clock::time_point t) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); }
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// the page texture

void PageCleaner::run(const CleanPageJob& j, const std::function<void(CleanBand&&)>& sink) {
    using namespace picclean;
    const int w = kPanoW, h = kPanoH, s = s_;
    const bool newSerial = serial_ != j.serial || idx_.empty();
    if (newSerial) {
        if (j.y0 != 0 || j.y1 != h) throw Error("PageCleaner: a new page needs all its rows");
        serial_ = j.serial;
        idx_.assign(static_cast<size_t>(w) * h, 0);
        rgb_.assign(static_cast<size_t>(w) * h * 3, 0);
        partner_.reset();
        havePal_ = haveHist_ = false;
    }
    if (j.rows.size() != static_cast<size_t>(j.y1 - j.y0) * w) throw Error("PageCleaner: bad row data");
    if (!j.rows.empty()) std::memcpy(&idx_[static_cast<size_t>(j.y0) * w], j.rows.data(), j.rows.size());
    if (!haveHist_) {
        histogram(idx_.data(), w, h, nullptr, hist_);
        haveHist_ = true;
    }
    const bool palChanged = !havePal_ || !samePal(j.pal, pal_) || j.haveDay != haveDay_ || (j.haveDay && !samePal(j.day, day_));
    int d0, d1;
    if (newSerial || palChanged || j.full) {
        pal_ = j.pal;
        day_ = j.day;
        haveDay_ = j.haveDay;
        havePal_ = true;
        T_ = threshold(pal_.c, haveDay_ ? day_.c : nullptr, hist_);
        soft_ = false;
        table_ = partner_.updateSlew(pal_.c, T_, -1, slew_, soft_);
        d0 = 0;
        d1 = h;
    } else {
        d0 = std::max(0, j.y0 - 1);
        d1 = std::min(h, j.y1 + 1);
    }
    if (d1 <= d0) return;
    dedither(idx_.data(), w, h, pal_.c, soft_ ? nullptr : table_, nullptr, true, d0, d1, rgb_.data(), soft_ ? table_ : nullptr);
    // output rows of source row y depend on de-dithered rows y - 1 .. y + 2
    const int e0 = d0 == 0 ? 0 : std::max(0, d0 - 2), e1 = d1 == h ? h : std::min(h, d1 + 2);
    const int tw = cleanTexW(s), W = w * s;
    for (int b0 = e0; b0 < e1; b0 += kCleanBandRows) {
        const int b1 = std::min(e1, b0 + kCleanBandRows);
        CleanBand band;
        band.serial = serial_;
        const int top = b0 == 0 ? 1 : 0, bot = b1 == h ? 1 : 0;
        const int rows = (b1 - b0) * s;
        band.px.assign(static_cast<size_t>(rows + top + bot) * tw, 0);
        uint32_t* body = band.px.data() + static_cast<size_t>(top) * tw;
        enlargeRgb(rgb_.data(), w, h, s, Kernel::Catmull, true, b0, b1, body + 1, static_cast<size_t>(tw));
        for (int r = 0; r < rows; r++) {
            uint32_t* row = body + static_cast<size_t>(r) * tw;
            row[0] = row[W];
            row[W + 1] = row[1];
        }
        if (top) std::memcpy(band.px.data(), body, static_cast<size_t>(tw) * 4);
        if (bot) std::memcpy(body + static_cast<size_t>(rows) * tw, body + static_cast<size_t>(rows - 1) * tw, static_cast<size_t>(tw) * 4);
        band.ty0 = b0 * s + 1 - top;
        band.ty1 = b1 * s + 1 + bot;
        sink(std::move(band));
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the frame

bool CleanFrameJob::operator==(const CleanFrameJob& o) const {
    return scale == o.scale && dither == o.dither && useHist == o.useHist && haveDay == o.haveDay && serial == o.serial && ix == o.ix && trn == o.trn &&
           view.x == o.view.x && view.y == o.view.y && view.w == o.view.w && view.h == o.view.h && raw.size() == o.raw.size() && std::equal(raw.begin(), raw.end(), o.raw.begin(), [](const Rect& a, const Rect& b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }) &&
           samePal(pal, o.pal) && (!haveDay || samePal(day, o.day));
}

void FrameCleaner::run(const CleanFrameJob& j, CleanFrameOut& out) {
    using namespace picclean;
    const int w = Frame::W, h = Frame::H, s = std::max(1, std::min(4, j.scale));
    const size_t n = static_cast<size_t>(w) * h;
    if (j.ix.size() != n) throw Error("FrameCleaner: bad frame");
    std::vector<u8> rgb(n * 3);
    if (!j.dither) {
        paletteRows(j.ix.data(), w, j.pal.c, 0, h, rgb.data());
        out.threshold = 0;
    } else {
        int T = kT0;
        if (j.useHist) {
            if (j.surface) {
                histogram(j.surface->data(), static_cast<int>(j.surface->size()), 1, nullptr, hist_);
                histSerial_ = j.serial;
                haveHist_ = true;
            }
            if (haveHist_ && histSerial_ == j.serial) T = threshold(j.pal.c, j.haveDay ? j.day.c : nullptr, hist_);
        }
        out.threshold = T;
        std::vector<u8> skip;
        if (!j.trn.empty() || !j.raw.empty()) {
            skip = j.trn.empty() ? std::vector<u8>(n, 0) : j.trn;
            for (const Rect& r : j.raw)
                for (int y = std::max(0, r.y); y < std::min(h, r.y + r.h); y++)
                    for (int x = std::max(0, r.x); x < std::min(w, r.x + r.w); x++) skip[static_cast<size_t>(y) * w + x] = 1;
        }
        if (j.serial != tableSerial_ || !j.useHist) { partner_.reset(); tableSerial_ = j.serial; }   // another picture: the reference's decisions at once
        bool soft = false;
        const uint8_t* cl = partner_.updateSlew(j.pal.c, T, -1, slew_, soft);
        dedither(j.ix.data(), w, h, j.pal.c, soft ? nullptr : cl, skip.empty() ? nullptr : skip.data(), false, 0, h, rgb.data(), soft ? cl : nullptr);
    }
    if (!j.trn.empty()) keyFill(rgb.data(), j.trn.data(), w, h, keyPasses_);
    const size_t W = static_cast<size_t>(w) * s;
    out.scale = s;
    out.alpha = !j.trn.empty();
    out.argb.resize(W * h * s);
    enlargeRgb(rgb.data(), w, h, s, Kernel::Catmull, false, 0, h, out.argb.data(), W);
    if (out.alpha) {
        std::vector<u8> mask(n), alpha(W * static_cast<size_t>(h) * s);
        for (size_t i = 0; i < n; i++) mask[i] = j.trn[i] ? 0 : 255;
        enlargePlane(mask.data(), w, h, s, Kernel::Catmull, false, 0, h, alpha.data(), W);
        const int vx0 = j.view.x * s, vx1 = (j.view.x + j.view.w) * s, vy0 = j.view.y * s, vy1 = (j.view.y + j.view.h) * s;
        for (size_t i = 0; i < alpha.size(); i++) {
            const int X = static_cast<int>(i % W), Y = static_cast<int>(i / W);
            const uint32_t a = (X < vx0 || X >= vx1 || Y < vy0 || Y >= vy1) ? 255u : alpha[i];
            out.argb[i] = (out.argb[i] & 0x00FFFFFFu) | (a << 24);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the service

struct CleanService::MainTimer {
    CleanService& s;
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    ~MainTimer() {
        const double ms = msSince(t0);
        std::lock_guard<std::mutex> lk(s.m_);
        s.st_.mainMsMax = std::max(s.st_.mainMsMax, ms);
    }
};

CleanService::CleanService(const CleanOptions& o) : o_(o), pageCl_(o.texScale, o.slew), frameCl_(o.slew, o.keyPasses) {
    if (o_.texScale != 1 && o_.texScale != 2) throw Error("CleanService: texScale must be 1 or 2");
    if (o_.async) {
        pageThr_ = std::thread([this] { pageLoop(); });
        frameThr_ = std::thread([this] { frameLoop(); });
    }
}

CleanService::~CleanService() {
    {
        std::lock_guard<std::mutex> lk(m_);
        stop_ = true;
    }
    cvPage_.notify_all();
    cvFrame_.notify_all();
    if (pageThr_.joinable()) pageThr_.join();
    if (frameThr_.joinable()) frameThr_.join();
}

void CleanService::runPage(CleanPageJob& j) {
    const auto t0 = std::chrono::steady_clock::now();
    pageCl_.run(j, [this](CleanBand&& b) {
        std::lock_guard<std::mutex> lk(m_);
        bands_.push_back(std::move(b));
        st_.pageBands++;
    });
    const double ms = msSince(t0);
    std::lock_guard<std::mutex> lk(m_);
    st_.pageJobs++;
    st_.pageMsSum += ms;
    st_.pageMsMax = std::max(st_.pageMsMax, ms);
}

void CleanService::runFrame(CleanFrameJob& j) {
    std::unique_ptr<CleanFrameOut> o(new CleanFrameOut);
    {
        std::lock_guard<std::mutex> lk(m_);
        o->argb.swap(spare_);
        o->seq = ++frameSeq_;
    }
    const auto t0 = std::chrono::steady_clock::now();
    frameCl_.run(j, *o);
    const double ms = msSince(t0);
    std::lock_guard<std::mutex> lk(m_);
    if (readyFrame_) st_.framesDropped++;      // a result nobody took
    readyFrame_ = std::move(o);
    st_.frameJobs++;
    st_.frameMsSum += ms;
    st_.frameMsMax = std::max(st_.frameMsMax, ms);
}

void CleanService::pageLoop() {
    std::unique_lock<std::mutex> lk(m_);
    for (;;) {
        cvPage_.wait(lk, [&] { return stop_ || pendingPage_; });
        if (stop_) return;
        std::unique_ptr<CleanPageJob> j = std::move(pendingPage_);
        pageBusy_ = true;
        lk.unlock();
        try { runPage(*j); } catch (const Error&) {}
        lk.lock();
        pageBusy_ = false;
        cvIdle_.notify_all();
    }
}

void CleanService::frameLoop() {
    std::unique_lock<std::mutex> lk(m_);
    for (;;) {
        cvFrame_.wait(lk, [&] { return stop_ || pendingFrame_; });
        if (stop_) return;
        std::unique_ptr<CleanFrameJob> j = std::move(pendingFrame_);
        frameBusy_ = true;
        lk.unlock();
        try { runFrame(*j); } catch (const Error&) {}
        lk.lock();
        frameBusy_ = false;
        cvIdle_.notify_all();
    }
}

void CleanService::submitPage(const Surface& page, u32 serial, const Palette& pal, const Palette* day) {
    MainTimer timer{*this};
    if (page.w != kPanoW || page.h != kPanoH) throw Error("CleanService: not a 2560 x 1024 page");
    const bool newSerial = !haveMirror_ || serial != mirrorSerial_;
    int y0 = kPanoH, y1 = 0;
    if (newSerial) {
        y0 = 0;
        y1 = kPanoH;
        mirror_.assign(page.px.size(), 0);
    } else {
        for (int y = 0; y < kPanoH; y++)
            if (std::memcmp(&mirror_[static_cast<size_t>(y) * kPanoW], page.row(y), kPanoW) != 0) {
                y0 = std::min(y0, y);
                y1 = y + 1;
            }
    }
    const bool dayChanged = mirrorHaveDay_ != (day != nullptr) || (day && !samePal(*day, mirrorDay_));
    const bool palChanged = newSerial || !samePal(pal, mirrorPal_) || dayChanged;
    if (!newSerial && y1 <= y0 && !palChanged) return;
    CleanPageJob* job = nullptr;
    std::unique_ptr<CleanPageJob> fresh(new CleanPageJob);
    std::unique_lock<std::mutex> lk(m_);
    bool merged = false;
    if (pendingPage_ && !newSerial && pendingPage_->serial == serial) {   // not started yet: merge into it
        merged = true;
        if (pendingPage_->y1 > pendingPage_->y0) {
            if (y1 > y0) { y0 = std::min(y0, pendingPage_->y0); y1 = std::max(y1, pendingPage_->y1); }
            else { y0 = pendingPage_->y0; y1 = pendingPage_->y1; }
        }
        job = pendingPage_.get();
    } else {
        pendingPage_ = std::move(fresh);
        job = pendingPage_.get();
    }
    job->serial = serial;
    job->full = newSerial || (merged && job->full);
    if (y1 < y0) y1 = y0;
    job->y0 = y1 > y0 ? y0 : 0;
    job->y1 = y1 > y0 ? y1 : 0;
    job->rows.assign(page.px.begin() + static_cast<std::ptrdiff_t>(job->y0) * kPanoW, page.px.begin() + static_cast<std::ptrdiff_t>(job->y1) * kPanoW);
    job->pal = pal;
    job->haveDay = day != nullptr;
    if (day) job->day = *day;
    // mirrors (the main thread's own)
    if (job->y1 > job->y0) std::memcpy(&mirror_[static_cast<size_t>(job->y0) * kPanoW], job->rows.data(), job->rows.size());
    mirrorSerial_ = serial;
    haveMirror_ = true;
    mirrorPal_ = pal;
    mirrorHaveDay_ = day != nullptr;
    if (day) mirrorDay_ = *day;
    if (o_.async) {
        lk.unlock();
        cvPage_.notify_one();
    } else {
        std::unique_ptr<CleanPageJob> j = std::move(pendingPage_);
        lk.unlock();
        runPage(*j);
    }
}

void CleanService::takeBands(std::vector<CleanBand>& out) {
    MainTimer timer{*this};
    std::lock_guard<std::mutex> lk(m_);
    for (CleanBand& b : bands_) out.push_back(std::move(b));
    bands_.clear();
}

void CleanService::submitFrame(CleanFrameJob&& j) {
    MainTimer timer{*this};
    if (lastFrame_ && *lastFrame_ == j) return;
    lastFrame_.reset(new CleanFrameJob(j));
    lastFrame_->surface.reset();
    std::unique_lock<std::mutex> lk(m_);
    if (pendingFrame_) {
        st_.framesDropped++;
        if (!j.surface && pendingFrame_->surface && pendingFrame_->serial == j.serial) j.surface = pendingFrame_->surface;   // the histogram data must not be lost
    }
    pendingFrame_.reset(new CleanFrameJob(std::move(j)));
    if (o_.async) {
        lk.unlock();
        cvFrame_.notify_one();
    } else {
        std::unique_ptr<CleanFrameJob> p = std::move(pendingFrame_);
        lk.unlock();
        runFrame(*p);
    }
}

bool CleanService::takeFrame(CleanFrameOut& out) {
    MainTimer timer{*this};
    std::lock_guard<std::mutex> lk(m_);
    if (!readyFrame_) return false;
    std::swap(out, *readyFrame_);
    spare_.swap(readyFrame_->argb);   // the buffer the caller no longer needs is the next result's
    readyFrame_.reset();
    return true;
}

bool CleanService::idle() {
    std::lock_guard<std::mutex> lk(m_);
    return !pendingPage_ && !pendingFrame_ && !pageBusy_ && !frameBusy_;
}

void CleanService::waitIdle() {
    std::unique_lock<std::mutex> lk(m_);
    cvIdle_.wait(lk, [&] { return !pendingPage_ && !pendingFrame_ && !pageBusy_ && !frameBusy_; });
}

CleanService::Stats CleanService::stats() {
    std::lock_guard<std::mutex> lk(m_);
    return st_;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the main thread's side

void CleanFront::step(Engine& e, const Frame& frame, const Palette& pal, const Engine::PanoramaFrame* pano, int scale) {
    Engine::CleanInfo ci;
    e.cleanInfo(ci);
    if (pano && pano->active) svc.submitPage(*pano->page, pano->pageSerial, *pano->palette, ci.day);
    CleanFrameJob j;
    j.ix = frame.px;
    j.pal = pal;
    j.dither = ci.kind == Engine::CleanInfo::Page;
    j.raw = ci.movies;
    j.scale = std::max(1, std::min(4, scale));
    j.serial = ci.pageSerial;
    j.useHist = ci.surface != nullptr;
    j.haveDay = ci.day != nullptr;
    if (ci.day) j.day = *ci.day;
    if (pano && pano->active) {
        j.view = pano->view;
        j.trn.assign(frame.px.size(), 0);
        for (int y = 0; y < kViewH; y++) {
            const int fy = pano->view.y + y;
            if (fy < 0 || fy >= Frame::H) continue;
            for (int x = 0; x < kViewW; x++) {
                const size_t fi = static_cast<size_t>(fy) * Frame::W + pano->view.x + x;
                if (frame.px[fi] == pano->bare[static_cast<size_t>(y) * kViewW + x]) {
                    j.trn[fi] = 1;
                    j.ix[fi] = 0;
                }
            }
        }
    }
    if (ci.surface && (!haveSurface_ || surfaceSerial_ != ci.pageSerial)) {
        j.surface = std::make_shared<std::vector<u8>>(ci.surface->px);
        surfaceSerial_ = ci.pageSerial;
        haveSurface_ = true;
    }
    svc.submitFrame(std::move(j));
    svc.takeBands(bands);
    for (CleanBand& b : bands) queue_.push_back(std::move(b));
    bands.clear();
    gotFrame = svc.takeFrame(out);
}

void CleanFront::popUploads(u32 serial, size_t budget, std::vector<CleanBand>& out) {
    size_t used = 0, taken = 0;
    for (CleanBand& b : queue_) {
        if (b.serial != serial) { taken++; continue; }                    // a page that is no longer shown
        if (used > 0 && used + b.px.size() * 4 > budget) break;
        used += b.px.size() * 4;
        out.push_back(std::move(b));
        taken++;
    }
    queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(taken));
}

// ---------------------------------------------------------------------------------------------------------------------------------
// headless compositions

void cleanPageTexture(const Surface& page, const Palette& pal, const Palette* day, int texScale, int slew, std::vector<u32>& tex) {
    PageCleaner pc(texScale, slew);
    CleanPageJob j;
    j.serial = 1;
    j.full = true;
    j.y0 = 0;
    j.y1 = kPanoH;
    j.rows = page.px;
    j.pal = pal;
    j.haveDay = day != nullptr;
    if (day) j.day = *day;
    tex.assign(static_cast<size_t>(cleanTexW(texScale)) * cleanTexH(texScale), 0xFF000000u);
    const int tw = cleanTexW(texScale);
    pc.run(j, [&](CleanBand&& b) { std::memcpy(&tex[static_cast<size_t>(b.ty0) * tw], b.px.data(), b.px.size() * 4); });
}

namespace {
// straight-alpha blend of `src` (0xAARRGGBB) over `dst` (0x00RRGGBB)
inline u32 blendOver(u32 src, u32 dst) {
    const u32 a = src >> 24;
    if (a == 255) return src & 0x00FFFFFFu;
    if (a == 0) return dst;
    u32 o = 0;
    for (int sh = 0; sh <= 16; sh += 8) o |= ((((src >> sh) & 255) * a + ((dst >> sh) & 255) * (255 - a) + 127) / 255) << sh;
    return o;
}

// One cleaned frame in the caller's thread.
void cleanFrameSync(Engine& e, const Frame& frame, const Palette& pal, const Engine::PanoramaFrame* pano, int scale, const CleanOptions& o, CleanFrameOut& out) {
    CleanOptions so = o;
    so.async = false;
    CleanFront cf(so);
    cf.step(e, frame, pal, pano, scale);
    if (!cf.gotFrame) throw Error("cleaner: no frame");
    out = std::move(cf.out);
}
} // namespace

void composeCleanCpu(Engine& e, const Frame& frame, const Palette& pal, int scale, const CleanOptions& o, bool linear, std::vector<u32>& rgb, std::string* notes) {
    if (scale < 1 || scale > 4) throw Error("cleanshot: scale must be 1..4");
    const Engine::PanoramaFrame& pf = e.panorama();
    const bool pano = pf.active;
    CleanFrameOut fo;
    cleanFrameSync(e, frame, pal, pano ? &pf : nullptr, scale, o, fo);
    const int OW = Frame::W * scale, OH = Frame::H * scale;
    rgb.assign(static_cast<size_t>(OW) * OH, 0);
    if (pano) {
        std::vector<u32> tex;
        Engine::CleanInfo ci;
        e.cleanInfo(ci);
        cleanPageTexture(*pf.page, *pf.palette, ci.day, o.texScale, o.slew, tex);
        PanoMesh mesh;
        buildPanoMesh(pf.camera, panoCellFor(scale), mesh);
        std::vector<u32> sm(static_cast<size_t>(kViewW) * scale * kViewH * scale, 0);
        rasterizePano(mesh, tex, scale, linear, sm, o.texScale);
        for (int y = 0; y < kViewH * scale; y++)
            for (int x = 0; x < kViewW * scale; x++) {
                const int fy = pf.view.y * scale + y, fx = pf.view.x * scale + x;
                if (fy >= 0 && fy < OH && fx >= 0 && fx < OW) rgb[static_cast<size_t>(fy) * OW + fx] = sm[static_cast<size_t>(y) * kViewW * scale + x];
            }
    }
    for (size_t i = 0; i < rgb.size(); i++) rgb[i] = blendOver(fo.argb[i], rgb[i]);
    if (notes) *notes = "T=" + std::to_string(fo.threshold) + (pano ? " panorama" : " flat");
}

int composeCleanWideCpu(Engine& e, const Frame& frame, const Palette& pal, int scale, const CleanOptions& o, bool linear, const WideCanvas& cv,
                        const std::vector<int16_t>* rain, std::vector<u32>& rgb) {
    const Engine::PanoramaFrame& pf = e.panorama();
    if (!pf.active) throw Error("cleanwide: no panorama");
    CleanFrameOut fo;
    cleanFrameSync(e, frame, pal, e.simStatusOpaque() ? nullptr : &pf, scale, o, fo);     // the status screen is opaque over the whole view
    std::vector<u32> tex;
    Engine::CleanInfo ci;
    e.cleanInfo(ci);
    cleanPageTexture(*pf.page, *pf.palette, ci.day, o.texScale, o.slew, tex);
    const int W = cv.outW, centreW = Frame::W * scale, left = cv.left;
    rgb.assign(static_cast<size_t>(W) * Frame::H * scale, 0);
    u32 lut[256];
    for (int i = 0; i < 256; i++) lut[i] = (static_cast<u32>(pal.c[i].r) << 16) | (static_cast<u32>(pal.c[i].g) << 8) | pal.c[i].b;
    const int cell = panoCellFor(scale);
    auto cells = [&](int outCols) { return (outCols + scale * cell - 1) / (scale * cell) * cell; };
    PanoMesh mesh;
    buildPanoMeshRange(pf.camera, cell, -cells(cv.showLeft), kViewW + cells(cv.showRight), mesh);
    std::vector<u32> sm(static_cast<size_t>(W) * kViewH * scale, 0xFFFFFFFFu);
    rasterizePanoRegion(mesh, tex, scale, linear, W, kViewH * scale, left, left - cv.showLeft, left + centreW + cv.showRight, sm, o.texScale);
    int uncovered = 0;
    for (int y = 0; y < kViewH * scale; y++) {
        u32* orow = &rgb[static_cast<size_t>(pf.view.y * scale + y) * W];
        const u32* srow = &sm[static_cast<size_t>(y) * W];
        for (int X = left - cv.showLeft; X < left + centreW + cv.showRight; X++) {
            const u32 s = srow[X];
            if (s == 0xFFFFFFFFu) { uncovered++; continue; }
            orow[X] = s & 0x00FFFFFFu;
            const bool centre = X >= left && X < left + centreW;
            if (!centre && rain) {
                const int fx = X < left ? -1 - (left - 1 - X) / scale : kViewW + (X - left - centreW) / scale;
                const int r = wideSideRain(*rain, fx, y / scale);
                if (r >= 0) orow[X] = lut[r & 255];
            }
        }
    }
    for (int Y = 0; Y < Frame::H * scale; Y++)
        for (int X = 0; X < centreW; X++) {
            u32& d = rgb[static_cast<size_t>(Y) * W + left + X];
            d = blendOver(fo.argb[static_cast<size_t>(Y) * centreW + X], d);
        }
    return uncovered;
}

} // namespace biosys
