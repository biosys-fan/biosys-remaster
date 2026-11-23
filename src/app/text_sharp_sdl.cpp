#include "app/text_sharp_sdl.h"

namespace biosys {

SharpTextLayer::~SharpTextLayer() {
    if (tex_) SDL_DestroyTexture(tex_);
}

void SharpTextLayer::draw(const Engine::TextFrame& tf, int scale, int offsetCols) {
    scale = std::max(1, std::min(scale, 8));
    if (tf.runs.empty()) {
        any_ = false;
        sig_ = 0;
        return;
    }
    const uint64_t sig = textOverlaySignature(tf, scale);
    if (!tex_ || scale != scale_ || sig != sig_) {
        if (!tex_ || scale != scale_) {
            if (tex_) SDL_DestroyTexture(tex_);
            tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, Frame::W * scale, Frame::H * scale);
            if (!tex_) {
                any_ = false;
                return;
            }
            SDL_SetTextureBlendMode(tex_, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(tex_, SDL_SCALEMODE_NEAREST);
        }
        renderTextOverlay(tf, scale, argb_);
        SDL_UpdateTexture(tex_, nullptr, argb_.data(), Frame::W * scale * 4);
        scale_ = scale;
        sig_ = sig;
        rebuilds_++;
    }
    any_ = true;
    if (offsetCols > 0) {
        const SDL_FRect dst{static_cast<float>(offsetCols), 0.0f, static_cast<float>(Frame::W), static_cast<float>(Frame::H)};
        SDL_RenderTexture(ren_, tex_, nullptr, &dst);
    } else {
        SDL_RenderTexture(ren_, tex_, nullptr, nullptr);
    }
}

} // namespace biosys
