// Engine::renderText: the frame with and without its low-resolution GDI text, and the list of the text runs that show (engine.h).
#include "engine/engine.h"
#include "engine/textplane.h"
#include <algorithm>

namespace biosys {

void Engine::renderText(TextFrame& tf) {
    struct Ref {
        Surface* s = nullptr;
        std::vector<u8>* mask = nullptr;
        int dx = 0, dy = 0;
        bool mapped = true;
        std::string name;
    };
    std::vector<Ref> refs;
    tf.runs.clear();
    tf.orphans = 0;
    tf.mask.assign(static_cast<size_t>(Frame::W) * Frame::H, 0);
    if (TextPlane::tracking() && !titleUp_ && !frozen_) {
        std::vector<PageView::TextSurface> v;
        if (bs4_) {
            std::vector<Bs4::TextSurface> b;
            bs4_->textSurfaces(b);
            for (const Bs4::TextSurface& t : b) refs.push_back({t.surface, nullptr, t.dx, t.dy, true, t.name});
        } else if (menu_) {
            menu_->view->textSurfaces(v);
            for (const PageView::TextSurface& t : v) refs.push_back({t.surface, t.mask, t.dx, t.dy, t.mapped, t.window == -1 ? "menu" : t.name});
        } else {
            view_.textSurfaces(v);
            for (const PageView::TextSurface& t : v) refs.push_back({t.surface, t.mask, t.dx, t.dy, t.mapped, t.name});
            if (simStatusUp()) {
                std::vector<PageView::TextSurface> w;
                sim_->view->textSurfaces(w);
                for (const PageView::TextSurface& t : w)
                    if (t.window == -1) refs.push_back({t.surface, t.mask, t.dx, t.dy, t.mapped, "status"});
            }
        }
    }
    bool any = false;
    for (const Ref& r : refs) any = any || (r.s->text && !r.s->text->empty());
    tf.palette = view_.palette();
    if (!any) {
        render(tf.full);
        tf.free = tf.full;
        return;
    }
    // the frame without the text first, so that panorama() describes the real frame
    if (bs4_) {
        render(tf.full);
        tf.free = bs4_->lastFlippedTextFree();
        if (tf.free.px.size() != tf.full.px.size()) tf.free = tf.full;
    } else {
        for (Ref& r : refs)
            if (r.mapped && r.s->text) r.s->text->strip(*r.s, r.mask);
        textFreePass_ = true;
        render(tf.free);
        textFreePass_ = false;
        for (Ref& r : refs)
            if (r.mapped && r.s->text) r.s->text->restore(*r.s, r.mask);
        render(tf.full);
    }
    const size_t N = tf.mask.size();
    for (size_t i = 0; i < N; i++) tf.mask[i] = tf.full.px[i] != tf.free.px[i] ? 1 : 0;
    std::vector<u8> claimed(N, 0);
    for (Ref& r : refs) {
        if (!r.mapped || !r.s->text) continue;
        std::vector<TextPlane::LiveRun> lr;
        r.s->text->live(*r.s, r.mask, lr);
        for (const TextPlane::LiveRun& l : lr) {
            if (!l.run->font) continue;
            TextRunOut o;
            for (u32 at : l.owned) {
                const int x = static_cast<int>(at % static_cast<u32>(r.s->w)) + r.dx, y = static_cast<int>(at / static_cast<u32>(r.s->w)) + r.dy;
                if (x < 0 || y < 0 || x >= Frame::W || y >= Frame::H) continue;
                const size_t p = static_cast<size_t>(y) * Frame::W + static_cast<size_t>(x);
                if (!tf.mask[p] || tf.full.px[p] != l.run->ink) continue;     // hidden by something else (a sprite, a rain drop)
                o.px.push_back(static_cast<u32>(p));
                claimed[p] = 1;
            }
            if (o.px.empty()) continue;
            const TextRun& t = *l.run;
            o.id = t.id;
            o.layer = r.name;
            o.font = t.font;
            o.text = t.text;
            o.x = t.x + r.dx;
            o.y = t.y + r.dy;
            o.clipL = std::max(t.clipL + r.dx, 0);
            o.clipT = std::max(t.clipT + r.dy, 0);
            o.clipR = std::min(t.clipR + r.dx, Frame::W);
            o.clipB = std::min(t.clipB + r.dy, Frame::H);
            o.ink = t.ink;
            o.r = tf.palette.c[t.ink].r;
            o.g = tf.palette.c[t.ink].g;
            o.b = tf.palette.c[t.ink].b;
            o.colorRef = t.colorRef;
            o.align = t.align;
            o.opaque = t.opaque;
            o.bkIndex = t.bkIndex;
            tf.runs.push_back(std::move(o));
        }
    }
    std::sort(tf.runs.begin(), tf.runs.end(), [](const TextRunOut& a, const TextRunOut& b) { return a.id < b.id; });
    for (size_t i = 0; i < N; i++)
        if (tf.mask[i] && !claimed[i]) {      // a difference that is not a listed text pixel stays as the engine drew it
            tf.free.px[i] = tf.full.px[i];
            tf.mask[i] = 0;
            tf.orphans++;
        }
}

} // namespace biosys
