#include "engine/textplane.h"
#include "engine/framebuffer.h"
#include <algorithm>

namespace biosys {

namespace {
bool gTracking = false;
u32 gNextRun = 1;
constexpr u16 kNone = 0xffff;
} // namespace

bool TextPlane::tracking() { return gTracking; }
void TextPlane::setTracking(bool on) { gTracking = on; }

TextPlane& planeOf(Surface& s) {
    if (!s.text) s.text = std::make_shared<TextPlane>();
    return *s.text;
}

void TextPlane::reset() {
    w_ = h_ = 0;
    ink_.clear();
    under_.clear();
    underMask_.clear();
    owner_.clear();
    touched_.clear();
    stripped_.clear();
    runs_.clear();
    cur_ = 0;
    records_ = 0;
}

void TextPlane::ensure(const Surface& s) {
    if (w_ == s.w && h_ == s.h && !ink_.empty()) return;
    reset();
    w_ = s.w;
    h_ = s.h;
    const size_t n = static_cast<size_t>(s.w) * static_cast<size_t>(s.h);
    ink_.assign(n, kNone);
    under_.assign(n, 0);
    underMask_.assign(n, 0);
    owner_.assign(n, 0);
}

bool TextPlane::isLive(const Surface& s, size_t at, const std::vector<u8>* mask) const {
    return ink_[at] != kNone && s.px[at] == ink_[at] && (!mask || (*mask)[at]);
}

void TextPlane::release(size_t at) {
    if (ink_[at] == kNone) return;
    ink_[at] = kNone;
    if (records_) records_--;
    auto it = runs_.find(owner_[at]);
    if (it != runs_.end() && --it->second.owned <= 0 && it->first != cur_) runs_.erase(it);
}

u32 TextPlane::beginRun(const Surface& s, TextRun run, std::vector<u8>* layerMask) {
    ensure(s);
    run.id = gNextRun++;
    if (run.font) {
        const int l = std::max({run.x, run.clipL, 0}), t = std::max({run.y, run.clipT, 0});
        const int r = std::min({run.x + run.font->textWidth(run.text.data(), static_cast<int>(run.text.size())), run.clipR, s.w});
        const int b = std::min({run.y + run.font->height(), run.clipB, s.h});
        for (int y = t; y < b; y++)
            for (int x = l; x < r; x++) {
                const size_t at = static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(x);
                if (ink_[at] != kNone && !isLive(s, at, layerMask)) release(at);
            }
    }
    cur_ = run.id;
    runs_[run.id].run = std::move(run);
    return cur_;
}

void TextPlane::putInk(const Surface& s, size_t at, u8 ink, u32 runId, const std::vector<u8>* layerMask) {
    if (at >= ink_.size()) return;
    const u8 before = s.px[at];
    const u8 maskBefore = layerMask ? (*layerMask)[at] : 1;
    if (ink_[at] != kNone) {
        const bool live = before == ink_[at] && maskBefore;
        if (!live) { under_[at] = before; underMask_[at] = maskBefore; }
        auto it = runs_.find(owner_[at]);
        if (it != runs_.end() && owner_[at] != runId) {
            if (--it->second.owned <= 0 && it->first != cur_) runs_.erase(it);
        } else if (owner_[at] == runId) {
            ink_[at] = ink;
            return;
        }
    } else {
        under_[at] = before;
        underMask_[at] = maskBefore;
        touched_.push_back(static_cast<u32>(at));
        records_++;
        if (touched_.size() > 4 * records_ + 4096) {
            std::vector<u32> keep;
            for (u32 t : touched_)
                if (ink_[t] != kNone) keep.push_back(t);
            std::sort(keep.begin(), keep.end());
            keep.erase(std::unique(keep.begin(), keep.end()), keep.end());
            touched_.swap(keep);
        }
    }
    ink_[at] = ink;
    owner_[at] = runId;
    runs_[runId].owned++;
}

void TextPlane::kill(size_t at) {
    if (at < ink_.size() && ink_[at] != kNone) release(at);
}

void TextPlane::endRun(u32 runId) {
    if (cur_ == runId) cur_ = 0;
    auto it = runs_.find(runId);
    if (it != runs_.end() && it->second.owned <= 0) runs_.erase(it);
}

void TextPlane::strip(Surface& s, std::vector<u8>* layerMask) {
    stripped_.clear();
    if (s.w != w_ || s.h != h_) return;
    for (u32 at : touched_)
        if (isLive(s, at, layerMask)) {
            stripped_.push_back(at);
            s.px[at] = under_[at];
            if (layerMask) (*layerMask)[at] = underMask_[at];
        }
}

void TextPlane::restore(Surface& s, std::vector<u8>* layerMask) {
    for (u32 at : stripped_) {
        s.px[at] = static_cast<u8>(ink_[at]);
        if (layerMask) (*layerMask)[at] = 1;
    }
    stripped_.clear();
}

void TextPlane::live(const Surface& s, const std::vector<u8>* layerMask, std::vector<LiveRun>& out) const {
    if (s.w != w_ || s.h != h_) return;
    std::map<u32, size_t> index;
    const size_t first = out.size();
    for (u32 at : touched_) {
        if (!isLive(s, at, layerMask)) continue;
        auto it = runs_.find(owner_[at]);
        if (it == runs_.end()) continue;
        auto ix = index.find(it->first);
        if (ix == index.end()) {
            ix = index.emplace(it->first, out.size()).first;
            out.emplace_back();
            out.back().run = &it->second.run;
        }
        out[ix->second].owned.push_back(at);
    }
    for (size_t i = first; i < out.size(); i++) {
        std::sort(out[i].owned.begin(), out[i].owned.end());
        out[i].owned.erase(std::unique(out[i].owned.begin(), out[i].owned.end()), out[i].owned.end());
    }
}

} // namespace biosys
