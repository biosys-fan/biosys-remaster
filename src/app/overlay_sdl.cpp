#include "app/overlay_sdl.h"
#include <algorithm>
#include <cmath>

namespace biosys {

OverlayRenderer::~OverlayRenderer() {
    for (Slot& s : slots_)
        if (s.tex) SDL_DestroyTexture(s.tex);
    if (amb_) SDL_DestroyTexture(amb_);
}

OverlayRenderer::Slot& OverlayRenderer::slot(int i) {
    if (static_cast<size_t>(i) >= slots_.size()) slots_.resize(static_cast<size_t>(i) + 1);
    return slots_[static_cast<size_t>(i)];
}

void OverlayRenderer::upload(Slot& s, uint64_t rev, const uint32_t* px, int w, int h) {
    if (s.tex && (s.w != w || s.h != h)) {
        SDL_DestroyTexture(s.tex);
        s.tex = nullptr;
    }
    if (!s.tex) {
        s.tex = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
        if (!s.tex) throw Error(std::string("overlay texture: ") + SDL_GetError());
        SDL_SetTextureBlendMode(s.tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(s.tex, SDL_SCALEMODE_NEAREST);
        s.w = w;
        s.h = h;
        s.rev = 0;
    }
    if (s.rev != rev || rev == 0) {
        SDL_UpdateTexture(s.tex, nullptr, px, w * 4);
        s.rev = rev;
    }
}

void OverlayRenderer::drawSprite(int slotIndex, const Sprite& sp, double alpha) {
    if (sp.empty() || alpha <= 0) return;
    Slot& s = slot(slotIndex);
    upload(s, sp.rev, sp.argb.data(), sp.w, sp.h);
    SDL_SetTextureAlphaMod(s.tex, static_cast<Uint8>(std::lround(std::min(1.0, alpha) * 255.0)));
    const SDL_FRect dst{static_cast<float>(sp.x), static_cast<float>(sp.y), static_cast<float>(sp.w), static_cast<float>(sp.h)};
    SDL_RenderTexture(ren_, s.tex, nullptr, &dst);
}

void OverlayRenderer::drawBitmap(int slotIndex, uint64_t sig, const std::vector<uint32_t>& argb, int w, int h, int x, int y) {
    if (w <= 0 || h <= 0) return;
    Slot& s = slot(slotIndex);
    if (!s.tex || s.w != w || s.h != h || s.rev != sig) {
        if (argb.size() != static_cast<size_t>(w) * h) return;        // the caller did not rebuild it for this signature
        upload(s, sig, argb.data(), w, h);
    }
    SDL_SetTextureAlphaMod(s.tex, 255);
    const SDL_FRect dst{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)};
    SDL_RenderTexture(ren_, s.tex, nullptr, &dst);
}

void OverlayRenderer::drawAmbient(const OverlayScene& sc, const ovl::Layout& L, double pL, double pR) {
    if (sc.ambient().empty()) return;
    if (!amb_ || ambW_ != sc.ambientW() || ambH_ != sc.ambientH()) {
        if (amb_) SDL_DestroyTexture(amb_);
        amb_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, sc.ambientW(), sc.ambientH());
        if (!amb_) throw Error(std::string("ambient texture: ") + SDL_GetError());
        SDL_SetTextureScaleMode(amb_, SDL_SCALEMODE_LINEAR);
        ambW_ = sc.ambientW();
        ambH_ = sc.ambientH();
        ambBuild_ = -1;
    }
    if (ambBuild_ != sc.ambientBuilds()) {
        std::vector<uint32_t> px(sc.ambient());
        for (uint32_t& p : px) p |= 0xFF000000u;
        SDL_UpdateTexture(amb_, nullptr, px.data(), ambW_ * 4);
        ambBuild_ = sc.ambientBuilds();
    }
    const AmbientCrop cr = ambientCrop(L.W, L.H);
    const double g = 24.0 * L.u;
    // the part of the ambient picture behind the window columns [x0, x1)
    auto src = [&](double x0, double x1) {
        return SDL_FRect{static_cast<float>((cr.u0 + x0 / L.W * cr.uspan) * ambW_), static_cast<float>(cr.v0 * ambH_), static_cast<float>((x1 - x0) / L.W * cr.uspan * ambW_),
                         static_cast<float>(cr.vspan * ambH_)};
    };
    const Uint8 dark = static_cast<Uint8>(std::lround(ovl::kAmbientBrightness * 255.0));
    SDL_SetTextureBlendMode(amb_, SDL_BLENDMODE_NONE);
    SDL_SetTextureColorMod(amb_, dark, dark, dark);
    auto draw = [&](double x0, double x1) {
        if (x1 - x0 < 0.5) return;
        const SDL_FRect s = src(x0, x1), d{static_cast<float>(x0), 0.0f, static_cast<float>(x1 - x0), static_cast<float>(L.H)};
        SDL_RenderTexture(ren_, amb_, &s, &d);
    };
    const double lx1 = std::min<double>(L.W, std::max(0.0, pL)), rx0 = std::min<double>(L.W, std::max(0.0, pR));
    draw(0, lx1);
    draw(rx0, L.W);
    // the gradient: the bright ambient picture with an alpha that falls from 1 at the picture to 0 g pixels away
    SDL_SetTextureColorMod(amb_, 255, 255, 255);
    SDL_SetTextureBlendMode(amb_, SDL_BLENDMODE_BLEND);
    auto ramp = [&](double xa, double xb, float aa, float ab) {      // from xa (alpha aa) to xb (alpha ab), window columns
        if (std::fabs(xb - xa) < 0.5) return;
        const double x0 = std::min(xa, xb), x1 = std::max(xa, xb);
        const SDL_FRect s = src(x0, x1);
        const float u0 = s.x / ambW_, u1 = (s.x + s.w) / ambW_, v0 = s.y / ambH_, v1 = (s.y + s.h) / ambH_;
        SDL_Vertex v[4];
        const float al = xa < xb ? aa : ab, ar = xa < xb ? ab : aa;
        v[0] = SDL_Vertex{SDL_FPoint{static_cast<float>(x0), 0.0f}, SDL_FColor{1, 1, 1, al}, SDL_FPoint{u0, v0}};
        v[1] = SDL_Vertex{SDL_FPoint{static_cast<float>(x1), 0.0f}, SDL_FColor{1, 1, 1, ar}, SDL_FPoint{u1, v0}};
        v[2] = SDL_Vertex{SDL_FPoint{static_cast<float>(x1), static_cast<float>(L.H)}, SDL_FColor{1, 1, 1, ar}, SDL_FPoint{u1, v1}};
        v[3] = SDL_Vertex{SDL_FPoint{static_cast<float>(x0), static_cast<float>(L.H)}, SDL_FColor{1, 1, 1, al}, SDL_FPoint{u0, v1}};
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(ren_, amb_, v, 4, idx, 6);
    };
    if (lx1 > 0.5) ramp(std::max(0.0, lx1 - g), lx1, 0.0f, 1.0f);
    if (rx0 < L.W - 0.5) ramp(rx0, std::min<double>(L.W, rx0 + g), 1.0f, 0.0f);
}

void OverlayRenderer::drawOverlays(const OverlayScene& sc, const ovl::Layout& L) {
    if (sc.stripAlpha() > 0 && !sc.strip().empty()) {
        const double top = L.H - std::lround(ovl::kStripGradientHeights * L.stripH);
        const float a = static_cast<float>(ovl::kStripGradientAlpha * sc.stripAlpha());
        SDL_Vertex v[4];
        const float w = static_cast<float>(L.W);
        v[0] = SDL_Vertex{SDL_FPoint{0, static_cast<float>(top)}, SDL_FColor{0, 0, 0, 0}, SDL_FPoint{0, 0}};
        v[1] = SDL_Vertex{SDL_FPoint{w, static_cast<float>(top)}, SDL_FColor{0, 0, 0, 0}, SDL_FPoint{0, 0}};
        v[2] = SDL_Vertex{SDL_FPoint{w, static_cast<float>(L.H)}, SDL_FColor{0, 0, 0, a}, SDL_FPoint{0, 0}};
        v[3] = SDL_Vertex{SDL_FPoint{0, static_cast<float>(L.H)}, SDL_FColor{0, 0, 0, a}, SDL_FPoint{0, 0}};
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        SDL_RenderGeometry(ren_, nullptr, v, 4, idx, 6);
        drawSprite(0, sc.strip(), sc.stripAlpha());
    }
    drawSprite(1, sc.message(), sc.messageAlpha());
    int i = 2;
    for (const OverlayScene::Word& w : sc.words()) drawSprite(i++, w.sprite, 1.0);
}

} // namespace biosys
