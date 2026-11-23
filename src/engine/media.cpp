// Media systems: sounds (exe:0043c000-0043d800), sprite animations (exe:00457fd0-0045a4d0) and
// AVI movies (exe:004574d0 family). See media.h and analysis/notes/engine_runtime.md sections 2-4.
#include "engine/media.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace biosys {

namespace {

bool reached(uint32_t now, uint32_t due) { return static_cast<int32_t>(now - due) >= 0; }

// Image8 helpers, all clipped to the destination.
Image8 copyOut(const Image8& page, int x, int y, int w, int h) {
    Image8 o;
    o.w = std::max(w, 0); o.h = std::max(h, 0);
    o.px.assign(static_cast<size_t>(o.w) * o.h, 0);
    for (int j = 0; j < o.h; j++) {
        int py = y + j;
        if (py < 0 || py >= page.h) continue;
        for (int i = 0; i < o.w; i++) {
            int px = x + i;
            if (px >= 0 && px < page.w) o.px[static_cast<size_t>(j) * o.w + i] = page.px[static_cast<size_t>(py) * page.w + px];
        }
    }
    return o;
}

void drawInto(Image8& page, const Image8& src, int x, int y, int key) {
    if (src.w <= 0 || src.h <= 0 || page.w <= 0) return;
    for (int j = 0; j < src.h; j++) {
        int py = y + j;
        if (py < 0 || py >= page.h) continue;
        for (int i = 0; i < src.w; i++) {
            int px = x + i;
            if (px < 0 || px >= page.w) continue;
            u8 v = src.px[static_cast<size_t>(j) * src.w + i];
            if (key >= 0 && v == key) continue;
            page.px[static_cast<size_t>(py) * page.w + px] = v;
        }
    }
}

Image8 cropWidth(const Image8& img, int w) {
    if (w >= img.w) return img;
    Image8 o;
    o.w = std::max(w, 0); o.h = o.w > 0 ? img.h : 0;
    o.px.resize(static_cast<size_t>(o.w) * o.h);
    for (int j = 0; j < o.h; j++)
        std::copy(img.px.begin() + static_cast<size_t>(j) * img.w, img.px.begin() + static_cast<size_t>(j) * img.w + o.w,
                  o.px.begin() + static_cast<size_t>(j) * o.w);
    return o;
}

template <class V> std::vector<MediaEvent> drain(V& v) {
    std::vector<MediaEvent> r;
    r.swap(v);
    return r;
}

} // namespace

// =============================================================================================
// SoundSystem

SoundSystem::SoundSystem(Mixer& mixer, const DataFiles& files) : mixer_(mixer), files_(files) {}

float SoundSystem::dbToGain(int v) {
    if (v >= 0) return 1.f;
    if (v <= -10000) return 0.f;
    return static_cast<float>(std::pow(10.0, v / 2000.0));
}

// DirectSound pan attenuates only the far channel, by |pan|/100 dB; the near channel stays at the
// sound's volume. The mixer's pan laws are (mono) constant power, (stereo) balance. We choose the
// mixer pan so that the far/near amplitude ratio is exactly the DirectSound ratio; the absolute level of
// a mono sound then differs from DirectSound by the mixer's -3 dB centre attenuation.
float SoundSystem::panToMixerPan(int pan, int channels) {
    if (pan == 0) return 0.f;
    int a = std::abs(pan);
    if (a >= 10000) return pan > 0 ? 1.f : -1.f;
    double att = std::pow(10.0, -a / 2000.0);
    double p;
    if (channels == 2) p = 1.0 - att;
    else p = 4.0 * std::atan(1.0 / att) / 3.14159265358979323846 - 1.0;
    return static_cast<float>(pan > 0 ? p : -p);
}

// exe:0043c7c0. Deviation from the literal spec text: the wrap-around is also tried in the other
// direction (d - W), otherwise sources at the far right of a 360 page would never take the short way.
int SoundSystem::pagePan(int x, int viewX, int pageWidth) {
    int q = pageWidth >> 2;
    if (q <= 0) return 0;
    int d = (x - 320) - viewX;
    if (std::abs(d + pageWidth) < std::abs(d)) d += pageWidth;
    else if (std::abs(d - pageWidth) < std::abs(d)) d -= pageWidth;
    int a = std::abs(d);
    if (a > q) a = q - (a % q);
    int pan = static_cast<int>(1400.0 / q * a);
    return d < 0 ? -pan : pan;
}

void SoundSystem::emit(int h, int code) {
    if (slots_[h].notify) events_.push_back({MediaEvent::Sound, h, code, 0});
}

void SoundSystem::applyGain(const Slot& s) { mixer_.setGain(s.voice, dbToGain(s.vol)); }

void SoundSystem::applyPan(Slot& s) { mixer_.setPan(s.voice, panToMixerPan(s.pan, s.data->channels)); }

void SoundSystem::destroy(int h) {
    Slot& s = slots_[h];
    bool notify = s.notify;
    mixer_.close(s.voice);
    s = Slot();
    if (notify) events_.push_back({MediaEvent::Sound, h, 4, 0});
}

int SoundSystem::findFree() {
    for (int i = 0; i < kMaxSounds; i++)
        if (!slots_[i].used) return i;
    // table full: reclaim a sound nobody holds and that is not playing
    for (int i = 0; i < kMaxSounds; i++)
        if (slots_[i].lock == 0 && !slots_[i].playing) { destroy(i); return i; }
    return -1;
}

int SoundSystem::open(const SoundOpenParams& p, int& handle) {
    handle = -1;
    if (p.volume > 0 || p.volume < -10000 || p.pan < -10000 || p.pan > 10000) return kFailure;
    if (p.frequency != 0 && (p.frequency < 100 || p.frequency > 100000)) return kFailure;
    std::string path = files_.findTime(p.file);
    if (path.empty()) return kFailure;
    std::shared_ptr<const SoundData> data;
    auto it = cache_.find(path);
    if (it != cache_.end()) data = it->second;
    else {
        try { data = std::make_shared<const SoundData>(SoundData::loadWav(path)); }
        catch (const Error&) { return kFailure; }
        cache_[path] = data;
    }
    int h = findFree();
    if (h < 0) return kFailure;
    // the terminal's own sounds (bs4sound\\clicks, loops, welcome) form their own volume group
    const bool terminal = p.file.size() > 9 && (p.file[0] | 32) == 'b' && (p.file[1] | 32) == 's' && p.file[2] == '4' &&
                          (p.file[3] | 32) == 's' && (p.file[4] | 32) == 'o' && (p.file[5] | 32) == 'u' && (p.file[6] | 32) == 'n' &&
                          (p.file[7] | 32) == 'd' && p.file[8] == '\\';
    int voice = mixer_.open(data, terminal ? VoiceGroup::Terminal : VoiceGroup::Effects);
    if (voice == 0) return kFailure;
    Slot& s = slots_[h];
    s = Slot();
    s.used = true; s.voice = voice; s.data = data; s.path = p.file; s.lock = p.lock;
    s.pageScoped = p.pageScoped; s.notify = p.notify; s.loop = p.loop;
    s.freq = p.frequency; s.pan = p.pan; s.vol = p.volume;
    mixer_.setLoop(voice, s.loop);
    mixer_.setRate(voice, s.freq);
    applyPan(s);
    applyGain(s);
    handle = h;
    emit(h, 1);
    if (p.autoplay) play(h, true);
    return kOk;
}

int SoundSystem::play(int h, bool restart) {
    Slot* s = get(h);
    if (!s) return kBadSoundHandle;
    s->finished = false; s->paused = false; s->playing = true;
    if (playTrace) playTrace(s->path, restart);
    mixer_.play(s->voice, restart);
    emit(h, 2);
    return kOk;
}

int SoundSystem::stop(int h) {
    Slot* s = get(h);
    if (!s) return kBadSoundHandle;
    poll(h);
    if (s->playing) {
        mixer_.stop(s->voice);
        s->playing = false; s->paused = false; s->finished = true;
        emit(h, 3);
    }
    return kOk;
}

void SoundSystem::poll(int h) {
    Slot& s = slots_[h];
    if (s.playing && !s.paused && !mixer_.isPlaying(s.voice)) {
        s.playing = false; s.finished = true;
        emit(h, 3);
    }
}

bool SoundSystem::isPlaying(int h) {
    if (!get(h)) return false;
    poll(h);
    return slots_[h].playing;
}

bool SoundSystem::valid(int h) const { return get(h) != nullptr; }

int SoundSystem::setProperty(int h, const std::string& name, int v) {
    Slot* s = get(h);
    if (!s) return kBadSoundHandle;
    std::string n;
    for (char c : name) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (n.size() > 12) return kUnknownProperty;
    if (n == "loop") {
        s->loop = v != 0;
        mixer_.setLoop(s->voice, s->loop);
        if (s->playing && !s->paused) mixer_.play(s->voice, false);   // original re-issues Play
        return kOk;
    }
    if (n == "frequency") {
        if (v != 0 && (v < 100 || v > 100000)) return kFailure;
        s->freq = v;
        mixer_.setRate(s->voice, v);
        return kOk;
    }
    if (n == "pan") {
        if (v < -10000 || v > 10000) return kFailure;
        s->pan = v;
        applyPan(*s);
        return kOk;
    }
    if (n == "volume") {
        if (v > 0 || v < -10000) return kFailure;
        s->vol = v;
        applyGain(*s);
        return kOk;
    }
    return kUnknownProperty;
}

int SoundSystem::close(int h) {
    Slot* s = get(h);
    if (!s) return kBadSoundHandle;
    if (s->lock > 0) s->lock--;
    if (s->lock > 0) return kFailure;
    destroy(h);
    return kOk;
}

int SoundSystem::panToPage(int h, bool enable, int x, bool wrap360) {
    Slot* s = get(h);
    if (!s) return kBadSoundHandle;
    if (!enable) { s->panFlag = false; return kOk; }
    if (!wrap360) return kNotWrapPage;
    s->panFlag = true; s->panX = x;
    s->pan = pagePan(x, viewX_, pageWidth_);
    applyPan(*s);
    return kOk;
}

void SoundSystem::setView(int viewX, int pageWidth) {
    viewX_ = viewX; pageWidth_ = pageWidth;
    for (int i = 0; i < kMaxSounds; i++)
        if (slots_[i].used && slots_[i].panFlag) {
            slots_[i].pan = pagePan(slots_[i].panX, viewX_, pageWidth_);
            applyPan(slots_[i]);
        }
}

int SoundSystem::playWave(const std::string& file, int flags) {
    if (!(flags & 4)) {
        for (int i = 0; i < kMaxSounds; i++)
            if (slots_[i].used && slots_[i].lock == 0 && iequals(slots_[i].path, file)) {
                play(i, true);
                return i;
            }
    }
    SoundOpenParams p;
    p.file = file; p.autoplay = true; p.loop = (flags & 2) != 0; p.pageScoped = (flags & 1) != 0;
    p.notify = false; p.lock = 0;
    int h;
    if (open(p, h) != kOk) return -1;
    return h;
}

void SoundSystem::update() {
    for (int i = 0; i < kMaxSounds; i++) {
        if (!slots_[i].used) continue;
        poll(i);
        if (slots_[i].lock == 0 && slots_[i].finished && !slots_[i].playing) destroy(i);
    }
}

void SoundSystem::onPageChange() {
    for (int i = 0; i < kMaxSounds; i++) {
        if (!slots_[i].used) continue;
        slots_[i].panFlag = false;
        if (slots_[i].pageScoped && slots_[i].lock == 0) destroy(i);
    }
}

void SoundSystem::closeAll() {
    for (int i = 0; i < kMaxSounds; i++)
        if (slots_[i].used) destroy(i);
}

void SoundSystem::pause() {
    for (auto& s : slots_)
        if (s.used && s.playing && !s.paused) { mixer_.stop(s.voice); s.paused = true; }
}

void SoundSystem::resume(bool notify) {
    for (int i = 0; i < kMaxSounds; i++) {
        Slot& s = slots_[i];
        if (!s.used || !s.paused) continue;
        s.paused = false;
        mixer_.play(s.voice, false);
        if (notify) emit(i, 2);
    }
}

int SoundSystem::count() const {
    int n = 0;
    for (auto& s : slots_) n += s.used;
    return n;
}

std::vector<MediaEvent> SoundSystem::takeEvents() { return drain(events_); }

// =============================================================================================
// SpriteSystem

SpriteSystem::SpriteSystem(SoundSystem& sounds, const DataFiles& files) : sounds_(sounds), files_(files) {
    windows_[-1] = Rect{0, 0, 640, 368};
}

void SpriteSystem::setWindowRect(int window, const Rect& r) { windows_[window] = r; }

Rect SpriteSystem::windowRect(int window) const {
    auto it = windows_.find(window);
    return it == windows_.end() ? Rect{0, 0, 0, 0} : it->second;
}

// Frame order, spec 2.2 (exe:00458840): 1: 1..n, 2: n..1, 3: 1..n..1, 4: n..1..n.
int SpriteSystem::stepCount(int mode, int n) { return (mode == 3 || mode == 4) ? 2 * n - 1 : n; }

int SpriteSystem::frameIndex(int mode, int step, int n) {
    switch (mode) {
    case 2: return n - 1 - step;
    case 3: return step < n ? step : 2 * n - 2 - step;
    case 4: return step < n ? n - 1 - step : step - (n - 1);
    default: return step;
    }
}

void SpriteSystem::emit(int h, int code, int arg) {
    if (a_[h].p.notify) events_.push_back({MediaEvent::Sprite, h, code, arg});
}

bool SpriteSystem::hasSaveSurface(const Anim& a) const {
    return a.tan->transparency() == 1 || (a.p.pageRelative && !a.p.keepPainted);
}

int SpriteSystem::keyIndex(const Anim& a, const Palette& pal) const {
    if (a.tan->transparency() != 1) return -1;
    u32 c = a.tan->unk0c();   // B = +0x0c, G = +0x0d, R = +0x0e
    return pal.find(Color{static_cast<u8>(c >> 16), static_cast<u8>(c >> 8), static_cast<u8>(c)});
}

Image8 SpriteSystem::frameImage(const Anim& a, int frame) const {
    try {
        return cropWidth(a.tan->frame(frame), a.visibleW);
    } catch (const Error&) {
        return Image8();
    }
}

int SpriteSystem::open(const SpriteOpenParams& p, int& handle) {
    handle = -1;
    if (!active_) return kInactive;
    std::string path = files_.findTime(p.file);
    if (path.empty()) return kFailure;
    std::unique_ptr<TanFile> tan;
    try { tan = std::make_unique<TanFile>(path); }
    catch (const Error&) { return kFailure; }
    int n = tan->frameCount();
    if (n <= 0) return kFailure;
    int w = tan->width(), h = tan->height();
    // exe:00457fd0 create checks
    if (p.pageRelative) {
        if (p.window != -1 || pageW_ <= 0) return kFailure;
        if (p.x < 0 || p.y < 0 || p.x + w > pageW_ || p.y + h > pageH_) return kFailure;
    } else {
        if (!windows_.count(p.window)) return kFailure;
        Rect wr = windowRect(p.window);
        if (p.x < 0 || p.y < 0 || p.x + w > wr.w || p.y + h > wr.h) return kFailure;
    }
    int slot = -1;
    for (int i = 0; i < kMaxAnims && slot < 0; i++)
        if (!a_[i].used) slot = i;
    if (slot < 0) {   // evict an animation with refcount 0 that is not playing
        for (int i = 0; i < kMaxAnims && slot < 0; i++)
            if (a_[i].lock == 0 && !a_[i].playing) { freeAnim(i, false); slot = i; }
    }
    if (slot < 0) return kFailure;   // "Too many animations ... maximum is 10"

    Anim& a = a_[slot];
    a = Anim();
    a.used = true;
    a.tan = std::move(tan);
    a.p = p;
    a.lock = p.autoFree ? 0 : 1;
    a.n = n;
    a.steps = stepCount(p.mode, n);
    a.visibleW = p.visibleWidth < 0 ? w : std::min(p.visibleWidth, w);
    a.wantStart = p.autoplay;
    if (!p.wav.empty()) {
        SoundOpenParams sp;
        sp.file = p.wav; sp.loop = p.loop && p.soundMode == 1; sp.notify = false;
        int sh;
        if (sounds_.open(sp, sh) == kOk) a.soundH = sh;
    }
    handle = slot;
    emit(slot, 1);
    return kOk;
}

void SpriteSystem::startPlay(int h, bool restart, uint32_t now) {
    Anim& a = a_[h];
    bool fresh = !a.everStopped || restart || a.finished;
    if (fresh) { a.step = 0; a.finished = false; }
    a.playing = true;
    a.startTick = now;
    a.nextDue = now;
    if (a.soundH >= 0) sounds_.play(a.soundH, fresh);
    emit(h, 2);
}

int SpriteSystem::play(int h, bool restart, uint32_t now, bool priority) {
    if (!active_) return kInactive;
    if (!get(h)) return kBadSpriteHandle;
    if (priority) priority_ = h;
    startPlay(h, restart, now);
    if (priority) stepAnim(h, now);
    return kOk;
}

// exe:00458970
void SpriteSystem::doStop(int h, bool stopSound) {
    Anim& a = a_[h];
    if (!a.playing) return;
    a.playing = false;
    a.everStopped = true;
    if (stopSound && a.soundH >= 0) sounds_.stop(a.soundH);
    emit(h, 3);
    dirty_ = true;
    if (a.p.pageRelative) {
        // spec 2.4: restore the save-under unless keep-painted (then the pending last frame is still painted)
        if (!a.p.keepPainted) { a.pendingPaint = false; a.pendingRestore = true; }
    } else if (a.p.keepPainted && a.cur >= 0) {
        // fixed + keep painted: burn the last frame into the page at the current view position (exe:004594e0)
        int bx = scrollX_ + a.p.x, by = scrollY_ + a.p.y;
        if (mapper_) mapper_(a.p.x, a.p.y, bx, by);   // projected page: the top-left corner through the inverse (exe:004594e0)
        Burn b{bx, by, frameImage(a, a.cur), a.tan->transparency() == 1,
               Color{static_cast<u8>(a.tan->unk0c() >> 16), static_cast<u8>(a.tan->unk0c() >> 8), static_cast<u8>(a.tan->unk0c())}};
        burns_.push_back(std::move(b));
    }
}

int SpriteSystem::stop(int h) {
    if (!get(h)) return kBadSpriteHandle;
    doStop(h, true);
    return kOk;
}

bool SpriteSystem::isPlaying(int h) const {
    const Anim* a = get(h);
    return a && a->playing;
}

void SpriteSystem::freeAnim(int h, bool tornDown) {
    Anim& a = a_[h];
    if (a.playing) doStop(h, true);
    if (!tornDown && a.pendingRestore && a.saveValid) erases_.push_back({a.p.x, a.p.y, std::move(a.save)});
    if (a.soundH >= 0) sounds_.close(a.soundH);
    emit(h, 4);
    a = Anim();
    if (priority_ == h) priority_ = -1;
    dirty_ = true;
}

int SpriteSystem::close(int h) {
    Anim* a = get(h);
    if (!a) return kBadSpriteHandle;
    if (a->lock > 0) a->lock--;
    if (a->lock > 0) return kFailure;
    freeAnim(h, false);
    return kOk;
}

int SpriteSystem::setVisibleWidth(int h, int w) {
    Anim* a = get(h);
    if (!a) return kBadSpriteHandle;
    a->pendingVisibleW = w < 0 ? a->tan->width() : std::min(w, a->tan->width());
    return kOk;
}

int SpriteSystem::soundHandle(int h) const {
    const Anim* a = get(h);
    return a ? a->soundH : -1;
}

void SpriteSystem::setPriority(int h) { priority_ = h; }

int SpriteSystem::currentFrame(int h) const {
    const Anim* a = get(h);
    return a ? a->cur : -1;
}

// exe:0045a190 / exe:00458ab0
void SpriteSystem::stepAnim(int h, uint32_t now) {
    Anim& a = a_[h];
    if (a.wantStart) { a.wantStart = false; startPlay(h, true, now); }
    if (!a.playing) return;
    if (a.p.loop && a.p.maxPlayMs > 0 && static_cast<int32_t>(now - a.startTick) >= a.p.maxPlayMs) {
        doStop(h, true);
        return;
    }
    if (!reached(now, a.nextDue)) return;
    if (a.step >= a.steps) {   // the last frame has been shown for its delay
        if (!a.p.loop) {
            a.finished = true;
            doStop(h, false);
            return;
        }
        a.step = 0;
        if (a.soundH >= 0 && a.p.soundMode == 0) sounds_.play(a.soundH, true);
    }
    a.nextDue = now + static_cast<uint32_t>(std::max(a.p.delayMs, 0));
    if (a.pendingVisibleW != -2) { a.visibleW = a.pendingVisibleW; a.pendingVisibleW = -2; }
    int f = frameIndex(a.p.mode, a.step, a.n);
    a.step++;
    a.cur = f;
    if (a.p.pageRelative) { a.pendingPaint = true; a.paintFrame = f; }
    else dirty_ = true;
    if (a.p.window == -4) emit(h, 5, f);
}

void SpriteSystem::update(uint32_t now, int kinds) {
    if (!active_) return;
    for (int h = 0; h < kMaxAnims; h++) {
        if (!a_[h].used) continue;
        if (!(kinds & (a_[h].p.pageRelative ? 1 : 2))) continue;
        stepAnim(h, now);
        if (a_[h].used && a_[h].lock == 0 && !a_[h].playing && a_[h].finished) freeAnim(h, false);
    }
}

void SpriteSystem::onSurfaceRebuilt() {
    erases_.clear();
    burns_.clear();
    for (auto& a : a_) {
        if (!a.used || !a.p.pageRelative) continue;
        a.saveValid = false; a.pendingRestore = false;
        a.pendingPaint = a.playing && a.cur >= 0;
        a.paintFrame = a.cur;
    }
}

void SpriteSystem::paintPageRelative(Image8& page, const Palette& pal) {
    for (auto& e : erases_) drawInto(page, e.img, e.x, e.y, -1);
    erases_.clear();
    for (auto& b : burns_) {
        drawInto(page, b.img, b.x, b.y, b.keyed ? pal.find(b.key) : -1);
    }
    burns_.clear();
    for (int h = 0; h < kMaxAnims; h++) {
        Anim& a = a_[h];
        if (!a.used || !a.p.pageRelative) continue;
        int w = a.tan->width(), ht = a.tan->height();
        if (a.pendingRestore) {
            if (a.saveValid) drawInto(page, a.save, a.p.x, a.p.y, -1);
            a.saveValid = false; a.pendingRestore = false; a.pendingPaint = false;
            continue;
        }
        if (!a.pendingPaint) continue;
        a.pendingPaint = false;
        if (hasSaveSurface(a)) {
            if (!a.saveValid) { a.save = copyOut(page, a.p.x, a.p.y, w, ht); a.saveValid = true; }
            else drawInto(page, a.save, a.p.x, a.p.y, -1);
        }
        drawInto(page, frameImage(a, a.paintFrame), a.p.x, a.p.y, keyIndex(a, pal));
    }
}

// exe:00443f30: fixed animations after the page blit; priority animation first, then slots 0..9.
void SpriteSystem::drawFixed(Frame& back, int viewX, int viewY, const Palette& pal) {
    auto drawOne = [&](int h) {
        Anim& a = a_[h];
        if (!a.used || a.p.pageRelative || !a.playing || a.cur < 0) return;
        Rect wr = windowRect(a.p.window);
        int ox = a.p.window == -1 ? viewX : wr.x, oy = a.p.window == -1 ? viewY : wr.y;
        Rect clip{ox, oy, wr.w, wr.h};
        blit(back, frameImage(a, a.cur), ox + a.p.x, oy + a.p.y, clip, keyIndex(a, pal));
    };
    if (priority_ >= 0 && priority_ < kMaxAnims) drawOne(priority_);
    for (int h = 0; h < kMaxAnims; h++)
        if (h != priority_) drawOne(h);
}

bool SpriteSystem::anyPlaying() const {
    for (auto& a : a_)
        if (a.used && a.playing) return true;
    return false;
}

bool SpriteSystem::takeDirty() {
    bool d = dirty_;
    dirty_ = false;
    return d;
}

// exe:0045a150: every primary page load frees all animations; the page surface is replaced, so no erase.
void SpriteSystem::freeAllOnPageChange() {
    for (int h = 0; h < kMaxAnims; h++)
        if (a_[h].used) freeAnim(h, true);
    erases_.clear();
    burns_.clear();
}

void SpriteSystem::freeWindow(int window) {
    for (int h = 0; h < kMaxAnims; h++)
        if (a_[h].used && a_[h].p.window == window) freeAnim(h, true);
}

int SpriteSystem::count() const {
    int n = 0;
    for (auto& a : a_) n += a.used;
    return n;
}

std::vector<MediaEvent> SpriteSystem::takeEvents() { return drain(events_); }

// =============================================================================================
// MovieSystem

MovieSystem::MovieSystem(Mixer& mixer, const DataFiles& files) : mixer_(mixer), files_(files) {}

void MovieSystem::emit(int h, int code, int arg) { events_.push_back({MediaEvent::Movie, h, code, arg}); }

// Presentation time of frame k relative to the time base (AVIStreamSampleToTime), rounded up to ms.
uint32_t MovieSystem::dueOffset(const Movie& m, int frame) {
    return static_cast<uint32_t>((static_cast<uint64_t>(frame) * m.avi->microsPerFrame() + 999) / 1000);
}

int MovieSystem::open(const MovieOpenParams& p, int& handle) {
    handle = -1;
    if (!active_) return kInactive;
    if (pageW_ <= 0) return kFailure;
    std::string path = files_.findTime(p.file);
    if (path.empty()) return kFailure;
    int slot = -1;
    for (int i = 0; i < kMaxMovies && slot < 0; i++)
        if (!m_[i].used) slot = i;
    if (slot < 0) return kFailure;
    std::unique_ptr<AviFile> avi;
    try { avi = std::make_unique<AviFile>(path); }
    catch (const Error&) { return kFailure; }
    Movie& m = m_[slot];
    m = Movie();
    m.used = true;
    m.p = p;
    m.lock = 1;
    if (p.audio && avi->hasAudio() && (avi->audioBits() == 8 || avi->audioBits() == 16) &&
        (avi->audioChannels() == 1 || avi->audioChannels() == 2)) {
        try {
            std::vector<u8> pcm = avi->readAllAudio();
            auto sd = std::make_shared<SoundData>();
            sd->sampleRate = avi->audioSampleRate();
            sd->channels = avi->audioChannels();
            sd->sourceBits = avi->audioBits();
            size_t bps = static_cast<size_t>(avi->audioBits() / 8);
            size_t n = pcm.size() / bps;
            n -= n % sd->channels;
            sd->frames = n / sd->channels;
            sd->samples.resize(n);
            for (size_t i = 0; i < n; i++)
                sd->samples[i] = bps == 1 ? static_cast<int16_t>((static_cast<int>(pcm[i]) - 128) * 256)
                                          : static_cast<int16_t>(pcm[2 * i] | (pcm[2 * i + 1] << 8));
            m.audio = sd;
            m.voice = mixer_.open(m.audio, VoiceGroup::Movies);   // 0 = mixer full: the movie then plays silently
        } catch (const Error&) {
            m.audio.reset(); m.voice = 0;
        }
    }
    m.avi = std::move(avi);
    handle = slot;
    return kOk;
}

bool MovieSystem::fits(const Movie& m) const {
    int w = m.avi->width(), h = m.avi->height() * (m.lineDouble ? 2 : 1);
    if (m.synth) return m.p.window == -1 && m.p.x >= 0 && m.p.y >= 0 && m.p.x + w <= 640 && m.p.y + h <= 368;
    return pageW_ > 0 && m.p.x >= 0 && m.p.y >= 0 && m.p.x + w <= pageW_ && m.p.y + h <= pageH_;
}

int MovieSystem::play(int h, bool restart, uint32_t now) {
    if (!active_) return kInactive;
    Movie* m = get(h);
    if (!m) return kBadMovieHandle;
    if (!fits(*m)) { destroy(h, false); return kFailure; }
    bool fresh = restart || !m->everPlayed || m->finished;
    if (fresh) {
        if (m->everPlayed || m->avi->framesDecoded() > 0) m->avi->rewind();
        m->shown = -1;
        m->base = now;
        m->hasFrame = false;
        if (m->voice) mixer_.play(m->voice, true);
    } else {
        m->base = now - dueOffset(*m, m->shown + 1);   // next frame due immediately
        if (m->voice) mixer_.play(m->voice, false);
    }
    m->playing = true; m->paused = false; m->everPlayed = true; m->finished = false;
    emit(h, 2);
    return kOk;
}

// exe:004578a0 stop: pendingRestore only when 0x4f asked for it.
void MovieSystem::doStop(int h, bool stopSound) {
    Movie& m = m_[h];
    if (!m.playing) return;
    m.playing = false; m.paused = false;
    if (stopSound && m.voice) mixer_.stop(m.voice);
    emit(h, 3);
    if (m.restoreStop && m.saveValid) { m.pendingRestore = true; m.pendingPaint = false; }
}

int MovieSystem::stop(int h) {
    if (!get(h)) return kBadMovieHandle;
    doStop(h, true);
    return kOk;
}

bool MovieSystem::isPlaying(int h) const {
    const Movie* m = get(h);
    return m && m->playing;
}

void MovieSystem::destroy(int h, bool tornDown) {
    Movie& m = m_[h];
    if (m.playing) doStop(h, true);
    if (!tornDown && !m.synth && m.saveValid && (m.pendingRestore || m.restoreClose))
        erases_.push_back({m.p.x, m.p.y, std::move(m.save)});
    if (m.voice) mixer_.close(m.voice);
    emit(h, 4);
    m = Movie();
}

int MovieSystem::close(int h) {
    Movie* m = get(h);
    if (!m) return kBadMovieHandle;
    if (m->lock > 0) m->lock--;
    if (m->lock > 0) return kFailure;
    destroy(h, false);
    return kOk;
}

int MovieSystem::setColorKey(int h, u8 r, u8 g, u8 b) {
    Movie* m = get(h);
    if (!m) return kBadMovieHandle;
    m->keyed = true; m->key = Color{r, g, b};
    return kOk;
}

int MovieSystem::setVolume(int h, int v) {
    Movie* m = get(h);
    if (!m) return kBadMovieHandle;
    if (v > 0 || v < -10000) return kFailure;
    m->dsVol = v;
    if (m->voice) mixer_.setGain(m->voice, SoundSystem::dbToGain(v));   // no audio: silent no-op
    return kOk;
}

int MovieSystem::bindPage(int h, const std::string& page) {
    Movie* m = get(h);
    if (!m) return kBadMovieHandle;
    if (m->playing || m->everPlayed || m->synth) return kFailure;
    if (pageExists_ && !pageExists_(page)) return kUnknownPage;
    m->bound = page;
    return kOk;
}

#define MOVIE_OPTION_PRECONDITION(m) \
    if (!(m)) return kBadMovieHandle; \
    if ((m)->playing || (m)->everPlayed || (m)->synth) return kFailure

int MovieSystem::setLineDoubling(int h) {
    Movie* m = get(h);
    MOVIE_OPTION_PRECONDITION(m);
    m->lineDouble = true;
    return kOk;
}

int MovieSystem::setSaveBackground(int h) {
    Movie* m = get(h);
    MOVIE_OPTION_PRECONDITION(m);
    m->saveBg = true;
    return kOk;
}

int MovieSystem::setSynth(int h) {
    Movie* m = get(h);
    MOVIE_OPTION_PRECONDITION(m);
    if (m->p.window != -1 || !m->bound.empty()) return kFailure;
    m->synth = true;
    if (!fits(*m)) { m->synth = false; return kFailure; }
    return kOk;
}

int MovieSystem::setRestore(int h, bool onStop, bool onClose) {
    Movie* m = get(h);
    MOVIE_OPTION_PRECONDITION(m);
    m->restoreStop = onStop; m->restoreClose = onClose;
    return kOk;
}

// exe:004563e0 / exe:004564b0: one presentation step per pass, no frame dropping.
bool MovieSystem::present(int h, uint32_t now) {
    Movie& m = m_[h];
    auto decode = [&]() {
        if (!m.avi->nextFrame()) return false;
        m.shown++;
        m.cur = m.avi->frame();
        m.hasFrame = true;
        if (!m.synth && m.visible) m.pendingPaint = true;
        emit(h, 5, m.shown);
        return true;
    };
    if (decode()) return true;
    if (m.p.loop) {   // back to frame 0, time base resets
        m.avi->rewind();
        m.shown = -1;
        m.base = now;
        if (m.voice) mixer_.play(m.voice, true);
        return decode();
    }
    m.finished = true;
    doStop(h, false);   // Stop(0): audio runs out on its own
    return false;
}

void MovieSystem::update(uint32_t now) {
    if (!active_) return;
    for (int h = 0; h < kMaxMovies; h++) {
        Movie& m = m_[h];
        if (!m.used || !m.playing || m.paused) continue;
        if (!reached(now, m.base + dueOffset(m, m.shown + 1))) continue;
        present(h, now);
    }
}

Image8 MovieSystem::source(const Movie& m) const {
    if (!m.lineDouble) return m.cur;
    Image8 o;
    o.w = m.cur.w; o.h = m.cur.h * 2;
    o.px.assign(static_cast<size_t>(o.w) * o.h, 0);   // every second row stays 0
    for (int y = 0; y < m.cur.h; y++)
        std::copy(m.cur.px.begin() + static_cast<size_t>(y) * o.w, m.cur.px.begin() + static_cast<size_t>(y + 1) * o.w,
                  o.px.begin() + static_cast<size_t>(2 * y) * o.w);
    return o;
}

int MovieSystem::keyIndex(const Movie& m, const Palette& pal) const {
    if (!m.keyed) return -1;
    return (m.p.hasPalette ? m.p.palette : pal).find(m.key);
}

void MovieSystem::paintPageRelative(Image8& page, const Palette& pal) {
    for (auto& e : erases_) drawInto(page, e.img, e.x, e.y, -1);
    erases_.clear();
    for (auto& m : m_) {
        if (!m.used || m.synth) continue;
        if (m.pendingRestore) {
            if (m.saveValid) drawInto(page, m.save, m.p.x, m.p.y, -1);
            m.saveValid = false; m.pendingRestore = false; m.pendingPaint = false;
            continue;
        }
        if (!m.pendingPaint || !m.visible || !m.hasFrame) continue;
        m.pendingPaint = false;
        Image8 src = source(m);
        int key = keyIndex(m, pal);
        if (!m.saveValid && (m.saveBg || m.restoreStop || m.restoreClose)) {
            m.save = copyOut(page, m.p.x, m.p.y, src.w, src.h);
            m.saveValid = true;
        } else if (m.saveValid && m.saveBg && key >= 0) {
            drawInto(page, m.save, m.p.x, m.p.y, -1);   // restore before each keyed frame
        }
        drawInto(page, src, m.p.x, m.p.y, key);
    }
}

void MovieSystem::drawSynth(Frame& back, int viewX, int viewY, const Palette& pal) {
    for (auto& m : m_) {
        if (!m.used || !m.synth || !m.hasFrame) continue;
        blit(back, source(m), viewX + m.p.x, viewY + m.p.y, Rect{viewX, viewY, 640, 368}, keyIndex(m, pal));
    }
}

void MovieSystem::onPageChange(const std::string& newPage) {
    for (int h = 0; h < kMaxMovies; h++) {
        Movie& m = m_[h];
        if (!m.used) continue;
        if (m.synth || m.bound.empty()) { destroy(h, true); continue; }
        m.visible = iequals(m.bound, newPage);
        m.saveValid = false; m.pendingRestore = false;
        m.pendingPaint = m.visible && m.hasFrame;   // the reloaded page needs the current frame again
    }
    erases_.clear();
}

void MovieSystem::onSurfaceRebuilt() {
    erases_.clear();
    for (auto& m : m_) {
        if (!m.used) continue;
        m.saveValid = false; m.pendingRestore = false;
        m.pendingPaint = !m.synth && m.visible && m.hasFrame;
    }
}

void MovieSystem::closeAll() {
    for (int h = 0; h < kMaxMovies; h++)
        if (m_[h].used) destroy(h, true);
    erases_.clear();
}

void MovieSystem::freeWindow(int window) {
    for (int h = 0; h < kMaxMovies; h++)
        if (m_[h].used && m_[h].p.window == window) destroy(h, true);
}

// exe:00457a60: movies of the window with no page binding (+0xb10 == 0) are freed (FUN_004542a0: stop, restore, callback 4); the others,
// when playing, are stopped with the callback switched off (+0x11c = 0) and flagged to go on (+0xb18 = 1).
void MovieSystem::suspendWindow(int window, uint32_t now) {
    for (int h = 0; h < kMaxMovies; h++) {
        Movie& m = m_[h];
        if (!m.used || m.p.window != window) continue;
        if (m.synth || m.bound.empty()) { destroy(h, false); continue; }
        if (!m.playing || m.suspended) continue;
        m.suspended = true;
        m.paused = true;
        m.pausedAt = now;
        if (m.voice) mixer_.stop(m.voice);
        if (m.restoreStop && m.saveValid) { m.pendingRestore = true; m.pendingPaint = false; }
    }
}

// exe:00457af0: AVIMovie::Play(0) for every flagged movie, again without the callback; the time spent suspended is not counted.
void MovieSystem::resumeWindow(int window, uint32_t now) {
    for (auto& m : m_) {
        if (!m.used || m.p.window != window || !m.suspended) continue;
        m.suspended = false;
        if (!m.paused) continue;
        m.paused = false;
        m.base += now - m.pausedAt;
        if (m.voice) mixer_.play(m.voice, false);
        if (m.visible && m.hasFrame && !m.synth) m.pendingPaint = true;
    }
}

void MovieSystem::pause(uint32_t now) {
    for (auto& m : m_)
        if (m.used && m.playing && !m.paused) {
            m.paused = true; m.pausedAt = now;
            if (m.voice) mixer_.stop(m.voice);
        }
}

void MovieSystem::resume(uint32_t now) {
    for (auto& m : m_)
        if (m.used && m.playing && m.paused) {
            m.paused = false;
            m.base += now - m.pausedAt;
            if (m.voice) mixer_.play(m.voice, false);
        }
}

int MovieSystem::currentFrame(int h) const {
    const Movie* m = get(h);
    return m ? m->shown : -1;
}

bool MovieSystem::audioPlaying(int h) const {
    const Movie* m = get(h);
    return m && m->voice && mixer_.isPlaying(m->voice);
}

void MovieSystem::shownRects(std::vector<ShownRect>& out) const {
    for (const Movie& m : m_)
        if (m.used && m.visible && m.hasFrame && m.cur.w > 0) out.push_back({m.p.x, m.p.y, m.cur.w, m.lineDouble ? m.cur.h * 2 : m.cur.h, m.synth});
}

int MovieSystem::count() const {
    int n = 0;
    for (auto& m : m_) n += m.used;
    return n;
}

std::vector<MediaEvent> MovieSystem::takeEvents() { return drain(events_); }

} // namespace biosys
