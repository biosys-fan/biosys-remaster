// --picture clean: the palette-aware de-dither and the Catmull-Rom enlargement (engine/picclean.h) applied to what the window shows. SDL-free
// (the window uploads the results as textures, app/panorama_smooth.cpp and main.cpp; the headless captures compose them on the CPU).
//
// Two pictures are cleaned, each by its own worker thread so that no frame of the window loop waits for them:
//  * the page texture of a projected panorama (2560 x 1024): de-dither with horizontal wrap, then Catmull-Rom by `texScale` (1: no enlargement,
//    the GPU's linear filter does the rest; 2: a 5120 x 2048 texture). Delivered as bands of texture rows; a palette change or a page change
//    recomputes everything, a change of some rows of the page surface (an animation painted into it) only those rows and their neighbours.
//  * the 640 x 480 frame (everything that is not the panorama: bars, close-ups, flat pages, menus, the terminal, and over a panorama the overlay:
//    held object, sprites, wearing overlays, rain; there the view pixels that show the bare projection are transparent), enlarged to the output
//    scale (1..4; the window's own filter does the rest above that). Delivered whole.
// A new job replaces a job that has not been started yet (the newest wins); while a result is being computed the previous one stays on
// screen. The main thread only copies what changed into the job (the page rows, the frame) and takes finished results.
#pragma once
#include "engine/engine.h"
#include "engine/panomesh.h"
#include "engine/picclean.h"
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace biosys {

struct CleanOptions {
    int texScale = 2;       // panorama page texture: 1 = de-dithered 1x, 2 = Catmull-Rom 2x
    int slew = 1;           // partner weights follow the reference's decision by at most slew / 32 per palette change (picclean.h PartnerState::updateSlew); 0 = the reference's decisions every time
    int keyPasses = 6;      // fill passes under the transparent pixels of an overlay frame (every tap of a pixel with alpha > 0 is within 6; the alpha hides the rest)
    bool async = true;      // false: every job runs inside the submit call (headless captures, tests)
};

constexpr int kCleanBandRows = 64;   // page rows per band
inline int cleanTexW(int texScale) { return kPanoW * texScale + 2; }
inline int cleanTexH(int texScale) { return kPanoH * texScale + 2; }

// texture rows [ty0, ty1) (border row 0 and the last one included where the band touches them) of the page texture of page `serial`
struct CleanBand {
    u32 serial = 0;
    int ty0 = 0, ty1 = 0;
    std::vector<u32> px;    // (ty1 - ty0) * cleanTexW
};

struct CleanPageJob {
    u32 serial = 0;
    bool full = false;
    int y0 = 0, y1 = 0;                 // the page rows in `rows` (full: all of them)
    std::vector<u8> rows;
    Palette pal;
    bool haveDay = false;
    Palette day;
};

class PageCleaner {
public:
    PageCleaner(int texScale, int slew) : s_(texScale), slew_(slew) {}
    // Applies the job to the cleaner's own copy of the page and hands the finished bands to `sink`, top to bottom.
    void run(const CleanPageJob& j, const std::function<void(CleanBand&&)>& sink);
    int currentT() const { return T_; }
    const std::vector<u8>& indices() const { return idx_; }
    const std::vector<u8>& dedithered() const { return rgb_; }
private:
    int s_, slew_;
    u32 serial_ = 0;
    std::vector<u8> idx_, rgb_;
    uint32_t hist_[256] = {};
    bool haveHist_ = false, havePal_ = false, haveDay_ = false;
    Palette pal_, day_;
    int T_ = picclean::kT0;
    picclean::PartnerState partner_;
    const uint8_t* table_ = nullptr;
    bool soft_ = false;
};

struct CleanFrameJob {
    std::vector<u8> ix;                 // 640 x 480, the transparent pixels set to 0
    std::vector<u8> trn;                // empty or 640 x 480: 1 = transparent (the panorama shows there)
    Rect view;                          // with trn: the panorama's view rectangle; the enlarged alpha is 255 outside it (the panorama is not drawn there, so the bars' edge
                                        // must not be blended with black)
    std::vector<Rect> raw;              // rectangles that are enlarged but not de-dithered (film frames)
    bool dither = true;                 // false: the terminal's frame (enlargement only)
    Palette pal;
    bool useHist = false;               // the threshold scale comes from the page's histogram (and day palette), else T = 14
    bool haveDay = false;
    Palette day;
    u32 serial = 0;
    std::shared_ptr<const std::vector<u8>> surface;   // the page's pixels when `serial` is new to the cleaner (histogram), else null
    int scale = 2;                      // 1..4
    bool operator==(const CleanFrameJob& o) const;
};

struct CleanFrameOut {
    int scale = 0;
    bool alpha = false;                 // the frame has transparent pixels (the panorama is under it)
    uint64_t seq = 0;
    int threshold = 0;
    std::vector<u32> argb;              // 640 * scale x 480 * scale, 0xAARRGGBB (straight alpha)
};

class FrameCleaner {
public:
    explicit FrameCleaner(int slew, int keyPasses) : slew_(slew), keyPasses_(keyPasses) {}
    void run(const CleanFrameJob& j, CleanFrameOut& out);
private:
    int slew_, keyPasses_;
    u32 histSerial_ = 0;
    bool haveHist_ = false;
    uint32_t hist_[256] = {};
    u32 tableSerial_ = 0;
    picclean::PartnerState partner_;
};

// The two workers and their mailboxes.
class CleanService {
public:
    explicit CleanService(const CleanOptions& o);
    ~CleanService();
    CleanService(const CleanService&) = delete;
    CleanService& operator=(const CleanService&) = delete;
    const CleanOptions& options() const { return o_; }

    // main thread: what changed in the projected page since the last call (rows, palette, page) becomes a job
    void submitPage(const Surface& page, u32 serial, const Palette& pal, const Palette* day);
    void takeBands(std::vector<CleanBand>& out);        // appends the bands finished since the last call
    // main thread: the frame job (dropped when it equals the last one submitted); takeFrame: the newest finished result (false: none new)
    void submitFrame(CleanFrameJob&& j);
    bool takeFrame(CleanFrameOut& out);
    bool idle();                                        // nothing pending or running (tests)
    void waitIdle();

    struct Stats {
        long pageJobs = 0, frameJobs = 0, pageBands = 0, framesDropped = 0;
        double pageMsMax = 0, pageMsSum = 0, frameMsMax = 0, frameMsSum = 0;
        double mainMsMax = 0;                           // the longest a call of the main thread's side took
    };
    Stats stats();

private:
    void pageLoop();
    void frameLoop();
    void runPage(CleanPageJob& j);
    void runFrame(CleanFrameJob& j);
    struct MainTimer;

    CleanOptions o_;
    std::mutex m_;
    std::condition_variable cvPage_, cvFrame_, cvIdle_;
    bool stop_ = false, pageBusy_ = false, frameBusy_ = false;
    std::unique_ptr<CleanPageJob> pendingPage_;
    std::unique_ptr<CleanFrameJob> pendingFrame_;
    std::vector<CleanBand> bands_;
    std::unique_ptr<CleanFrameOut> readyFrame_;
    std::vector<u32> spare_;
    uint64_t frameSeq_ = 0;
    Stats st_;
    // main-thread mirrors (no lock needed)
    std::vector<u8> mirror_;
    u32 mirrorSerial_ = 0;
    bool haveMirror_ = false;
    Palette mirrorPal_, mirrorDay_;
    bool mirrorHaveDay_ = false;
    std::unique_ptr<CleanFrameJob> lastFrame_;
    PageCleaner pageCl_;
    FrameCleaner frameCl_;
    std::thread pageThr_, frameThr_;
};

// The main thread's side of the window and of the headless window simulation: builds the jobs from the engine's state, takes the results.
// `tex` / `frame` are the CPU-side copies the SDL layer uploads from (or the compose functions read).
struct CleanFront {
    explicit CleanFront(const CleanOptions& o) : svc(o) {}
    CleanService svc;
    // One drawn frame. `frame`: the frame to show (without its low-resolution text when the sharp text is on). `pano`: the engine's panorama
    // description when the smooth panorama is drawn this frame (its page is cleaned, its bare projection pixels are transparent), else null.
    // scale: the output scale (1..; the cleaned frame is enlarged by min(scale, 4)). Returns after submitting and taking: bands() / frame() hold
    // what finished since the last call.
    void step(Engine& e, const Frame& frame, const Palette& pal, const Engine::PanoramaFrame* pano, int scale);
    // The page-texture bands the cleaner finished and nobody has uploaded yet, for the page `serial`, at most `budget` bytes (at least one band when
    // any is waiting): what a frame of the window may spend on uploading; bands of other pages are dropped. Appends to `out`.
    void popUploads(u32 serial, size_t budget, std::vector<CleanBand>& out);
    static constexpr size_t kUploadBytes = 8u << 20;
    std::vector<CleanBand> bands;       // finished since the last step (step() moves them into the queue behind popUploads)
    bool gotFrame = false;
    CleanFrameOut out;                  // valid when gotFrame
private:
    u32 surfaceSerial_ = 0;
    bool haveSurface_ = false;
    std::vector<CleanBand> queue_;
};

// ---- headless compositions (script.cpp: cleanshot, wideshot clean; tests) ----

// The whole cleaned page texture (with border), computed in the caller's thread.
void cleanPageTexture(const Surface& page, const Palette& pal, const Palette* day, int texScale, int slew, std::vector<u32>& tex);
// The picture the window shows with --picture clean at `scale` (1..4), without the sharp text: the frame (`tf.free` if tracking is on) cleaned;
// over a smooth panorama the page texture through the mesh (bilinear, as the GPU) under the cleaned overlay. 0x00RRGGBB, 640 * scale x 480 * scale.
// `notes`: a one-line description (threshold, kind).
void composeCleanCpu(Engine& e, const Frame& frame, const Palette& pal, int scale, const CleanOptions& o, bool linear, std::vector<u32>& rgb, std::string* notes);

// --widescreen: the same on a canvas of outW pixels (the 4:3 picture centred, `showLeft` / `showRight` output columns of the panorama beside it,
// `left` = the first column of the 4:3 picture), side rain drawn as the plain wide capture does (engine/panowide.h composeSmoothWide).
struct WideCanvas;
int composeCleanWideCpu(Engine& e, const Frame& frame, const Palette& pal, int scale, const CleanOptions& o, bool linear, const WideCanvas& cv,
                        const std::vector<int16_t>* rain, std::vector<u32>& rgb);

} // namespace biosys
