// Media systems: sounds, sprite animations (.TAN) and AVI movies.
// Behaviour spec: analysis/notes/engine_runtime.md sections 2-4. Platform-independent (no SDL).
//
// Conventions shared by the three systems:
//  * Time is injected (uint32_t milliseconds); nothing here reads a wall clock.
//  * Instead of C callbacks, each system queues MediaEvents (takeEvents()).
//  * Methods return the original API result codes: 0 = ok, 0x10 bad sound handle, 0x11 failure,
//    0x13 bad sprite handle, 0x15 app inactive, 0x18 bad movie handle, 0x0f unknown sound property,
//    0x14 pan-to-page on a non-360 page, 6 unknown page.
//  * Drawing is done into targets handed to the draw methods; the systems never keep pointers to them.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "engine/audio.h"
#include "engine/avi.h"
#include "engine/common.h"
#include "engine/datafiles.h"
#include "engine/framebuffer.h"
#include "engine/image.h"
#include "engine/spriteanim.h"

namespace biosys {

// Callback events of the original API. Sound: 1 created, 2 played, 3 stopped/ended, 4 destroyed.
// Sprite: 1 created, 2 play, 3 stopped/finished, 4 freed, 5 per frame (window -4 only; arg = frame).
// Movie: 2 played/resumed, 3 stopped/finished, 4 destroyed, 5 per presented frame (arg = frame index).
struct MediaEvent {
    enum Kind { Sound = 0, Sprite = 1, Movie = 2 };
    Kind kind;
    int handle;
    int code;
    int arg = 0;
};

enum MediaError {
    kOk = 0, kUnknownPage = 6, kUnknownProperty = 0x0f, kBadSoundHandle = 0x10, kFailure = 0x11,
    kBadSpriteHandle = 0x13, kNotWrapPage = 0x14, kInactive = 0x15, kBadMovieHandle = 0x18,
};

// ---------------------------------------------------------------------------------------------
// Sounds (spec 4).
struct SoundOpenParams {
    std::string file;       // relative to BIOTIME ("sounds\\house\\pan3.wav")
    bool autoplay = false;
    bool loop = false;      // API 0x14: only the value 1 loops
    int frequency = 0;      // Hz, 0 = file rate, else 100..100000
    int pan = 0;            // 1/100 dB, -10000..10000, positive = right
    int volume = 0;         // 1/100 dB, -10000..0
    bool notify = true;     // queue events for this sound (the original passes a callback or not)
    bool pageScoped = false;
    int lock = 1;           // API opens lock the sound (1); verb 4 opens do not (0)
};

class SoundSystem {
public:
    static constexpr int kMaxSounds = 40;
    SoundSystem(Mixer& mixer, const DataFiles& files);

    int open(const SoundOpenParams& p, int& handle);        // exe:0043c000 family; 0x11 on failure
    int play(int handle, bool restart);                     // API 0x15; event 2 on every call
    int stop(int handle);                                   // API 0x16; event 3 if it was playing
    bool isPlaying(int handle);                             // API 0x17 (polls the mixer first)
    bool valid(int handle) const;
    // API 0x18: "loop" (nonzero), "frequency" (Hz), "pan", "volume" (1/100 dB). Case-insensitive.
    int setProperty(int handle, const std::string& name, int value);
    int close(int handle);                                  // API 0x19; 0x11 if still locked
    // API 0x27 / spec 4.5. enable=false only clears the flag. x = page x of the source. Returns 0x14 if
    // !wrap360. The pan is re-evaluated by setView().
    int panToPage(int handle, bool enable, int x, bool wrap360);
    void setView(int viewX, int pageWidth);                 // current scroll position; re-pans flagged sounds
    // Verb 4 PLAYWAVE (exe:0040d680). flags: 1 page-scoped, 2 loop, 4 force new instance. Returns the
    // handle, or -1 if the table is full / the file is unusable.
    int playWave(const std::string& file, int flags);
    // Test hook (script `soundtrace`): called for every start of a sound with its file and whether it restarts from the beginning.
    std::function<void(const std::string& file, bool restart)> playTrace;
    void update();                                          // exe:0043d3c0: detect ends, free finished lock-0 sounds
    void onPageChange();                                    // exe:0043d2c0: free page-scoped lock-0 sounds, clear pan flags
    void closeAll();
    // Menu open/close and app (de)activation: silently stop / resume logically playing sounds.
    // resume(true) queues event 2 per resumed sound (reactivation), resume(false) does not (menu close).
    void pause();
    void resume(bool notify);
    int count() const;
    std::vector<MediaEvent> takeEvents();

    // DirectSound units -> mixer units (exposed for tests).
    static float dbToGain(int hundredthsDb);
    static float panToMixerPan(int hundredthsDb, int channels);
    static int pagePan(int x, int viewX, int pageWidth);    // spec 4.5

private:
    struct Slot {
        bool used = false;
        int voice = 0;
        std::shared_ptr<const SoundData> data;
        std::string path;
        int lock = 0;
        bool pageScoped = false, finished = false, playing = false, paused = false, notify = true, loop = false;
        int freq = 0, pan = 0, vol = 0;
        bool panFlag = false;
        int panX = 0;
    };
    Slot* get(int h) { return h >= 0 && h < kMaxSounds && slots_[h].used ? &slots_[h] : nullptr; }
    const Slot* get(int h) const { return h >= 0 && h < kMaxSounds && slots_[h].used ? &slots_[h] : nullptr; }
    void emit(int h, int code);
    void destroy(int h);
    void applyPan(Slot& s);
    void applyGain(const Slot& s);
    int findFree();
    void poll(int h);

    Mixer& mixer_;
    const DataFiles& files_;
    Slot slots_[kMaxSounds];
    std::map<std::string, std::shared_ptr<const SoundData>> cache_;
    std::vector<MediaEvent> events_;
    int viewX_ = 0, pageWidth_ = 0;
};

// ---------------------------------------------------------------------------------------------
// Sprite animations (spec 2).
struct SpriteOpenParams {
    std::string file;       // .TAN relative to BIOTIME (+a8)
    std::string wav;        // optional attached wav (+c0)
    int x = 0, y = 0;       // +0c,+10: window-relative (fixed) or page coordinates (page-relative)
    int mode = 1;           // +18 play mode 1..4
    int delayMs = 0;        // +14
    int window = -1;        // +00: -1 main, 0..19 popup
    bool loop = false;      // +08
    bool keepPainted = false;   // +04
    int maxPlayMs = 0;      // +1c, 0 = unlimited
    int soundMode = 0;      // +ac: 1 = looping wav spans the animation, 0 = restart wav at each loop
    int visibleWidth = -1;  // +20: -1 = full
    bool pageRelative = false;  // +b8
    bool eraseOnStop = false;   // +bc (recorded; page-relative animations always restore unless keepPainted)
    bool autoplay = false;  // verb 11 only (starts at the next update)
    bool autoFree = false;  // verb 11: refcount 0, freed by update() once finished
    bool notify = true;     // queue events (the original passes a callback or not)
};

class SpriteSystem {
public:
    static constexpr int kMaxAnims = 10;
    SpriteSystem(SoundSystem& sounds, const DataFiles& files);

    void setActive(bool active) { active_ = active; }                // inactive: open/play -> 0x15, update no-op
    void setPageSize(int w, int h) { pageW_ = w; pageH_ = h; }       // 0,0 = no page loaded
    void setWindowRect(int window, const Rect& r);                   // main window (-1) defaults to 640x368
    void setViewScroll(int x, int y) { scrollX_ = x; scrollY_ = y; } // page position of the view (for burning fixed keep-painted sprites)
    // view point -> page point (exe:00403470 as exe:004594e0 uses it); unset: the flat scroll offset. Set by the engine for projected pages.
    void setViewMapper(std::function<void(int, int, int&, int&)> f) { mapper_ = std::move(f); }

    int open(const SpriteOpenParams& p, int& handle);                // API 0x1e/0x1f; event 1
    // API 0x20. priority=true (+08 == 1): make it the priority animation, play, run one update at once.
    int play(int handle, bool restart, uint32_t nowMs, bool priority = false);
    int stop(int handle);                                            // API 0x21: Stop(1); event 3 if it was playing
    bool isPlaying(int handle) const;                                // API 0x22
    bool valid(int handle) const { return get(handle) != nullptr; }
    int close(int handle);                                           // API 0x23
    int setVisibleWidth(int handle, int w);                          // API 0x36, effective from the next frame
    int soundHandle(int handle) const;                               // API 0x64 (-1 = none)
    void setPriority(int handle);                                    // -1 clears
    int currentFrame(int handle) const;                              // 0-based TAN frame last drawn, -1 none

    // kinds: bit0 = page-relative animations, bit1 = fixed ones (the idle pass runs them at different
    // points, spec 2.4); 3 = all.
    void update(uint32_t nowMs, int kinds = 3);
    void paintPageRelative(Image8& pageSurface, const Palette& pal);
    // The page surface was rebuilt without a page change (palette / option change): save-unders are
    // stale; playing page-relative animations paint their current frame again.
    void onSurfaceRebuilt();
    // Fixed animations onto the back buffer, priority animation first, then slots 0..9. viewX/viewY =
    // top-left of the main window's view rect on the back buffer.
    void drawFixed(Frame& backBuffer, int viewX, int viewY, const Palette& pal);
    bool anyPlaying() const;
    bool takeDirty();               // true if fixed-sprite appearance changed since the last call (recompose)
    void freeAllOnPageChange();     // exe:0045a150
    void freeWindow(int window);    // popup close
    int count() const;
    std::vector<MediaEvent> takeEvents();

    static int frameIndex(int mode, int step, int n);   // 0-based frame for a step (spec 2.2)
    static int stepCount(int mode, int n);

private:
    struct Anim {
        bool used = false;
        std::unique_ptr<TanFile> tan;
        SpriteOpenParams p;
        int lock = 0, n = 0, steps = 0;
        bool playing = false, finished = false, everStopped = false, wantStart = false;
        int step = 0;
        uint32_t nextDue = 0, startTick = 0;
        int cur = -1;                   // frame last selected
        int visibleW = 0, pendingVisibleW = -2;
        int soundH = -1;
        bool saveValid = false;
        Image8 save;
        bool pendingPaint = false, pendingRestore = false;
        int paintFrame = -1;
    };
    struct Erase { int x, y; Image8 img; };
    struct Burn { int x, y; Image8 img; bool keyed; Color key; };

    Anim* get(int h) { return h >= 0 && h < kMaxAnims && a_[h].used ? &a_[h] : nullptr; }
    const Anim* get(int h) const { return h >= 0 && h < kMaxAnims && a_[h].used ? &a_[h] : nullptr; }
    void emit(int h, int code, int arg = 0);
    void doStop(int h, bool stopSound);
    void freeAnim(int h, bool tornDown);
    void startPlay(int h, bool restart, uint32_t now);
    void stepAnim(int h, uint32_t now);
    bool hasSaveSurface(const Anim& a) const;
    Image8 frameImage(const Anim& a, int frame) const;      // cropped to the visible width
    int keyIndex(const Anim& a, const Palette& pal) const;
    Rect windowRect(int window) const;

    SoundSystem& sounds_;
    const DataFiles& files_;
    Anim a_[kMaxAnims];
    std::map<int, Rect> windows_;
    std::vector<Erase> erases_;
    std::vector<Burn> burns_;
    std::vector<MediaEvent> events_;
    int priority_ = -1;
    bool active_ = true, dirty_ = false;
    int pageW_ = 0, pageH_ = 0, scrollX_ = 0, scrollY_ = 0;
    std::function<void(int, int, int&, int&)> mapper_;
};

// ---------------------------------------------------------------------------------------------
// AVI movies (spec 3).
struct MovieOpenParams {
    std::string file;       // relative to BIOTIME
    int x = 0, y = 0;       // page coordinates
    int window = -1;
    bool loop = false;
    Palette palette;        // caller's 256-colour palette (+c0); used only to resolve the colour key
    bool hasPalette = false;    // false: the key is resolved against the palette passed to the draw call
    bool audio = true;      // caller decides: sound enabled and owning page number != 1
};

class MovieSystem {
public:
    static constexpr int kMaxMovies = 4;
    MovieSystem(Mixer& mixer, const DataFiles& files);

    void setActive(bool active) { active_ = active; }
    void setPageSize(int w, int h) { pageW_ = w; pageH_ = h; }       // 0,0 = no page loaded
    void setPageValidator(std::function<bool(const std::string&)> f) { pageExists_ = std::move(f); }

    int open(const MovieOpenParams& p, int& handle);                 // API 0x29
    int play(int handle, bool restart, uint32_t nowMs);              // API 0x2a
    int stop(int handle);                                            // API 0x2b
    bool isPlaying(int handle) const;                                // API 0x2c
    bool valid(int handle) const { return get(handle) != nullptr; }
    int close(int handle);                                           // API 0x2d
    int setColorKey(int handle, u8 r, u8 g, u8 b);                   // API 0x37
    int setVolume(int handle, int hundredthsDb);                     // API 0x3d
    int bindPage(int handle, const std::string& page);               // API 0x3f
    int setLineDoubling(int handle);                                 // API 0x41
    int setSaveBackground(int handle);                               // API 0x42
    int setSynth(int handle);                                        // API 0x43
    int setRestore(int handle, bool onStop, bool onClose);           // API 0x4f

    void update(uint32_t nowMs);
    void paintPageRelative(Image8& pageSurface, const Palette& pal);
    void onSurfaceRebuilt();                 // see SpriteSystem::onSurfaceRebuilt
    // Synth movies: onto the back buffer at (viewX + x, viewY + y), clipped to the 640x368 view rect.
    void drawSynth(Frame& backBuffer, int viewX, int viewY, const Palette& pal);

    // Page change: unbound and synth movies are destroyed; bound movies become visible only if bound to
    // `newPage`. All pending restores are dropped (the page surface was reloaded).
    void onPageChange(const std::string& newPage);
    void closeAll();                         // load game / restart / BS4 entry
    void freeWindow(int window);
    // exe:00457a60(0/1): the page of `window` is about to be replaced by an animation. A synth or unbound movie is destroyed (callbacks 3, 4,
    // erase on close); a bound playing one is stopped without callback (the clock is kept) and continues in resumeWindow (exe:00457af0).
    void suspendWindow(int window, uint32_t nowMs);
    void resumeWindow(int window, uint32_t nowMs);
    void pause(uint32_t nowMs);              // menu open: silent
    void resume(uint32_t nowMs);
    int currentFrame(int handle) const;      // index of the frame last presented, -1 none
    bool audioPlaying(int handle) const;
    // The rectangles (page coordinates for page-relative movies, view coordinates for synth ones) of the movies that have a frame on
    // show (the picture cleaner leaves film frames alone: engine/picclean.h).
    struct ShownRect { int x, y, w, h; bool synth; };
    void shownRects(std::vector<ShownRect>& out) const;
    int count() const;
    std::vector<MediaEvent> takeEvents();

private:
    struct Movie {
        bool used = false;
        std::unique_ptr<AviFile> avi;
        MovieOpenParams p;
        int lock = 0;
        bool playing = false, paused = false, everPlayed = false, finished = false, suspended = false;
        bool keyed = false; Color key;
        bool lineDouble = false, saveBg = false, synth = false, restoreStop = false, restoreClose = false;
        std::string bound;
        bool visible = true;
        int shown = -1;
        uint32_t base = 0, pausedAt = 0;
        Image8 cur;
        bool hasFrame = false, pendingPaint = false, pendingRestore = false, saveValid = false;
        Image8 save;
        int voice = 0, dsVol = 0;
        std::shared_ptr<const SoundData> audio;
    };
    struct Erase { int x, y; Image8 img; };

    Movie* get(int h) { return h >= 0 && h < kMaxMovies && m_[h].used ? &m_[h] : nullptr; }
    const Movie* get(int h) const { return h >= 0 && h < kMaxMovies && m_[h].used ? &m_[h] : nullptr; }
    void emit(int h, int code, int arg = 0);
    void doStop(int h, bool stopSound);
    void destroy(int h, bool tornDown);
    bool present(int h, uint32_t now);
    static uint32_t dueOffset(const Movie& m, int frame);
    bool fits(const Movie& m) const;
    Image8 source(const Movie& m) const;
    int keyIndex(const Movie& m, const Palette& pal) const;

    Mixer& mixer_;
    const DataFiles& files_;
    Movie m_[kMaxMovies];
    std::vector<Erase> erases_;
    std::vector<MediaEvent> events_;
    std::function<bool(const std::string&)> pageExists_;
    bool active_ = true;
    int pageW_ = 0, pageH_ = 0;
};

} // namespace biosys
