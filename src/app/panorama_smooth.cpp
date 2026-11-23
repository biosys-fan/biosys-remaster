#include "app/panorama_smooth.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace biosys {

PanoramaLayer::~PanoramaLayer() {
    if (page_) SDL_DestroyTexture(page_);
    if (cleanTex_) SDL_DestroyTexture(cleanTex_);
}

void PanoramaLayer::setLinear(bool on) {
    linear_ = on;
    if (page_) SDL_SetTextureScaleMode(page_, on ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    if (cleanTex_) SDL_SetTextureScaleMode(cleanTex_, on ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
}

static bool samePalette(const Palette& a, const Palette& b) {
    for (int i = 0; i < 256; i++)
        if (a.c[i].r != b.c[i].r || a.c[i].g != b.c[i].g || a.c[i].b != b.c[i].b) return false;
    return true;
}

// Re-uploads what changed since the last frame: all rows when the palette changed, else the rows whose indices differ.
void PanoramaLayer::upload(const Surface& page, const Palette& pal) {
    if (!page_) {
        page_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, kPanoTexW, kPanoTexH);
        if (!page_) throw Error(std::string("panorama texture: ") + SDL_GetError());
        SDL_SetTextureScaleMode(page_, linear_ ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        havePal_ = false;
    }
    int y0 = 0, y1 = kPanoH;
    if (havePal_ && samePalette(pal, pal_) && idx_.size() == page.px.size()) {
        y0 = kPanoH; y1 = 0;
        for (int y = 0; y < kPanoH; y++)
            if (std::memcmp(&idx_[static_cast<size_t>(y) * kPanoW], page.row(y), kPanoW) != 0) {
                y0 = std::min(y0, y);
                y1 = y + 1;
            }
        if (y0 >= y1) return;
    }
    panoTextureRows(page, pal, y0, y1, texCpu_);
    idx_ = page.px;
    pal_ = pal;
    havePal_ = true;
    // the texture rows of page rows [y0, y1), plus the border row above / below when they are the first / last page row
    const int t0 = y0 == 0 ? 0 : y0 + 1, t1 = y1 == kPanoH ? kPanoTexH : y1 + 1;
    SDL_Rect r{0, t0, kPanoTexW, t1 - t0};
    SDL_UpdateTexture(page_, &r, &texCpu_[static_cast<size_t>(t0) * kPanoTexW], kPanoTexW * 4);
    uploads_++;
}

void PanoramaLayer::draw(const Engine::PanoramaFrame& pf, const Frame& frame, std::vector<Uint32>& argb, u32 nowMs, double scale) {
    plan(pf, frame, nowMs, scale, nullptr);
    drawPlanned(pf, frame, argb, scale);
}

void PanoramaLayer::applyCleanBands(std::vector<CleanBand>& bands, u32 serial) {
    if (!cleanScale_) { bands.clear(); return; }
    const int tw = cleanTexW(cleanScale_), th = cleanTexH(cleanScale_);
    for (CleanBand& b : bands) {
        if (b.serial != serial) continue;                           // a page that is no longer shown
        if (!cleanTex_) {
            cleanTex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, tw, th);
            if (!cleanTex_) throw Error(std::string("clean panorama texture: ") + SDL_GetError());
            SDL_SetTextureScaleMode(cleanTex_, linear_ ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
            haveCleanSerial_ = false;
        }
        if (!haveCleanSerial_ || cleanSerial_ != serial) {
            cleanSerial_ = serial;
            haveCleanSerial_ = true;
            cleanCover_.assign(static_cast<size_t>(th), 0);
            cleanCovered_ = 0;
            useClean_ = false;
        }
        SDL_Rect r{0, b.ty0, tw, b.ty1 - b.ty0};
        SDL_UpdateTexture(cleanTex_, &r, b.px.data(), tw * 4);
        for (int y = b.ty0; y < b.ty1; y++)
            if (!cleanCover_[static_cast<size_t>(y)]) { cleanCover_[static_cast<size_t>(y)] = 1; cleanCovered_++; }
    }
    bands.clear();
    useClean_ = haveCleanSerial_ && cleanSerial_ == serial && cleanCovered_ == th;
}

void PanoramaLayer::plan(const Engine::PanoramaFrame& pf, const Frame& frame, u32 nowMs, double scale, const WideRequest* wide) {
    if (!(statusScreen_ && page_) && (!cleanScale_ || !useClean_ || cleanSerial_ != pf.pageSerial)) upload(*pf.page, *pf.palette);
    shown_ = follow_ ? smoother_.update(pf.camera, pf.pageSerial, pf.scrollStepTime, nowMs) : pf.camera;
    dec_ = WideDecision();
    rain_.clear();
    planExtra_ = 0;
    if (!wide || !wide->ctl || wide->extra <= 0) return;
    const int s = std::max(1, static_cast<int>(scale));
    WideInput in;
    in.frame = wide->cover ? wide->cover : &frame;
    in.noFilter = wide->noFilter;
    in.view = pf.view;
    in.bare = pf.bare;
    in.cam = shown_;
    in.limits.on = pf.limitsOn;
    in.limits.yawMin = pf.limitYawMin;
    in.limits.yawMax = pf.limitYawMax;
    in.scale = s;
    in.availLeft = in.availRight = wide->extra * s;
    in.quantum = s;
    in.keep = wide->keep;
    dec_ = wide->ctl->decide(in);
    if (!dec_.wide) return;
    planExtra_ = wide->extra;
    if (wide->noFilter && !wide->keep) panoRainMap(*in.frame, *wide->noFilter, pf.view, pf.bare, rain_);
}

void PanoramaLayer::drawPlanned(const Engine::PanoramaFrame& pf, const Frame& frame, std::vector<Uint32>& argb, double scale) {
    const PanoCamera& cam = shown_;
    const int s = std::max(1, static_cast<int>(scale));
    const int showL = dec_.wide ? dec_.showLeft / s : 0, showR = dec_.wide ? dec_.showRight / s : 0;
    const int cell = panoCellFor(scale);
    if (dec_.wide) buildPanoMeshRange(cam, cell, -((showL + cell - 1) / cell) * cell, kViewW + ((showR + cell - 1) / cell) * cell, mesh_);
    else buildPanoMesh(cam, cell, mesh_);
    sv_.resize(mesh_.verts.size());
    const SDL_FColor white{1.0f, 1.0f, 1.0f, 1.0f};
    const bool clean = cleanScale_ && useClean_ && cleanSerial_ == pf.pageSerial && cleanTex_;
    const int ks = clean ? cleanScale_ : 1;
    const float tw = static_cast<float>(kPanoW * ks + 2), th = static_cast<float>(kPanoH * ks + 2);
    for (size_t i = 0; i < sv_.size(); i++) {
        const MeshVertex& v = mesh_.verts[i];
        sv_[i].position = SDL_FPoint{v.x + pf.view.x + planExtra_, v.y + pf.view.y};
        sv_[i].color = white;
        sv_[i].tex_coord = SDL_FPoint{(v.u * ks + 1.0f) / tw, (v.v * ks + 1.0f) / th};
    }
    const SDL_Rect clip{pf.view.x + planExtra_ - showL, pf.view.y, pf.view.w + showL + showR, pf.view.h};
    SDL_SetRenderClipRect(ren_, &clip);
    SDL_RenderGeometry(ren_, clean ? cleanTex_ : page_, sv_.data(), static_cast<int>(sv_.size()), mesh_.idx.data(), static_cast<int>(mesh_.idx.size()));
    SDL_SetRenderClipRect(ren_, nullptr);
    clearBarePixels(pf, frame, argb);
}

void PanoramaLayer::clearBarePixels(const Engine::PanoramaFrame& pf, const Frame& frame, std::vector<Uint32>& argb) const {
    if (statusScreen_) return;      // the status picture is opaque over the whole view
    for (int y = 0; y < kViewH; y++) {
        const int fy = pf.view.y + y;
        if (fy < 0 || fy >= Frame::H) continue;
        for (int x = 0; x < kViewW; x++) {
            const size_t fi = static_cast<size_t>(fy) * Frame::W + pf.view.x + x;
            if (frame.px[fi] == pf.bare[static_cast<size_t>(y) * kViewW + x]) argb[fi] &= 0x00FFFFFFu;
        }
    }
}

void PanoramaLayer::drawPlannedAffine(const Engine::PanoramaFrame& pf, const Frame& frame, std::vector<Uint32>& argb, double k, double ox, double oy) {
    const PanoCamera& cam = shown_;
    const int showL = dec_.wide ? dec_.showLeft : 0, showR = dec_.wide ? dec_.showRight : 0;
    const int cell = panoCellFor(k);
    if (dec_.wide) buildPanoMeshRange(cam, cell, -((showL + cell - 1) / cell) * cell, kViewW + ((showR + cell - 1) / cell) * cell, mesh_);
    else buildPanoMesh(cam, cell, mesh_);
    sv_.resize(mesh_.verts.size());
    const SDL_FColor white{1.0f, 1.0f, 1.0f, 1.0f};
    const bool clean = cleanScale_ && useClean_ && cleanSerial_ == pf.pageSerial && cleanTex_;
    const int ks = clean ? cleanScale_ : 1;
    const float tw = static_cast<float>(kPanoW * ks + 2), th = static_cast<float>(kPanoH * ks + 2);
    for (size_t i = 0; i < sv_.size(); i++) {
        const MeshVertex& v = mesh_.verts[i];
        sv_[i].position = SDL_FPoint{static_cast<float>(ox + v.x * k), static_cast<float>(oy + v.y * k)};
        sv_[i].color = white;
        sv_[i].tex_coord = SDL_FPoint{(v.u * ks + 1.0f) / tw, (v.v * ks + 1.0f) / th};
    }
    const int x0 = static_cast<int>(std::floor(ox - showL * k)), x1 = static_cast<int>(std::ceil(ox + (kViewW + showR) * k));
    const int y0 = static_cast<int>(std::floor(oy)), y1 = static_cast<int>(std::ceil(oy + kViewH * k));
    const SDL_Rect clip{x0, y0, x1 - x0, y1 - y0};
    SDL_SetRenderClipRect(ren_, &clip);
    SDL_RenderGeometry(ren_, clean ? cleanTex_ : page_, sv_.data(), static_cast<int>(sv_.size()), mesh_.idx.data(), static_cast<int>(mesh_.idx.size()));
    SDL_SetRenderClipRect(ren_, nullptr);
    clearBarePixels(pf, frame, argb);
}

struct SdlSmoothSession::Impl {
    SDL_Surface* surf = nullptr;
    SDL_Renderer* ren = nullptr;
    std::unique_ptr<PanoramaLayer> layer;   // destroyed before the renderer
    int scale = 0;
    ~Impl() {
        layer.reset();
        if (ren) SDL_DestroyRenderer(ren);
        if (surf) SDL_DestroySurface(surf);
    }
};

SdlSmoothSession::SdlSmoothSession() : p_(new Impl) {}
SdlSmoothSession::~SdlSmoothSession() = default;
long SdlSmoothSession::uploads() const { return p_->layer ? p_->layer->uploads() : 0; }

bool SdlSmoothSession::capture(const Engine::PanoramaFrame& pf, const Frame& frame, int scale, std::vector<Uint32>& rgb, std::string& err) {
    Impl& m = *p_;
    if (m.scale != scale) {   // a new size: a new surface, renderer and layer (and with it a new page texture)
        m.layer.reset();
        if (m.ren) SDL_DestroyRenderer(m.ren);
        if (m.surf) SDL_DestroySurface(m.surf);
        m.ren = nullptr;
        m.surf = SDL_CreateSurface(Frame::W * scale, Frame::H * scale, SDL_PIXELFORMAT_ARGB8888);
        if (!m.surf) { err = SDL_GetError(); return false; }
        m.ren = SDL_CreateSoftwareRenderer(m.surf);
        if (!m.ren) { err = SDL_GetError(); return false; }
        m.layer.reset(new PanoramaLayer(m.ren));
        m.layer->setFollow(false);
        m.scale = scale;
    }
    SDL_Renderer* ren = m.ren;
    SDL_SetRenderScale(ren, static_cast<float>(scale), static_cast<float>(scale));
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    std::vector<Uint32> argb(static_cast<size_t>(Frame::W) * Frame::H);
    for (size_t i = 0; i < argb.size(); i++) {
        const Color& c = pf.palette->c[frame.px[i]];
        argb[i] = 0xFF000000u | (Uint32(c.r) << 16) | (Uint32(c.g) << 8) | c.b;
    }
    try {
        m.layer->draw(pf, frame, argb, pf.now, scale);
    } catch (const Error& e) {
        err = e.what();
        return false;
    }
    SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, Frame::W, Frame::H);
    if (!t) { err = SDL_GetError(); return false; }
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(t, nullptr, argb.data(), Frame::W * 4);
    SDL_RenderTexture(ren, t, nullptr, nullptr);
    SDL_DestroyTexture(t);
    SDL_Surface* px = SDL_RenderReadPixels(ren, nullptr);
    if (!px) { err = SDL_GetError(); return false; }
    SDL_Surface* conv = px->format == SDL_PIXELFORMAT_ARGB8888 ? px : SDL_ConvertSurface(px, SDL_PIXELFORMAT_ARGB8888);
    rgb.assign(static_cast<size_t>(m.surf->w) * m.surf->h, 0);
    for (int y = 0; y < m.surf->h; y++)
        for (int x = 0; x < m.surf->w; x++)
            rgb[static_cast<size_t>(y) * m.surf->w + x] =
                reinterpret_cast<const Uint32*>(static_cast<const char*>(conv->pixels) + static_cast<size_t>(y) * conv->pitch)[x] & 0x00FFFFFFu;
    if (conv != px) SDL_DestroySurface(conv);
    SDL_DestroySurface(px);
    return true;
}

bool captureSdlSoftware(const Engine::PanoramaFrame& pf, const Frame& frame, int scale, std::vector<Uint32>& rgb, std::string& err) {
    SdlSmoothSession once;
    return once.capture(pf, frame, scale, rgb, err);
}

} // namespace biosys
