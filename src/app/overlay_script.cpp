// Script commands of the overlay interface (headless; MANUAL.md 9.10, tests/test_overlay.sh):
//   overlay on [W H]            switch it on (text tracking too) for a window of W x H pixels (default 1920 x 1080)
//   overlay off                 switch it off, forget the state
//   overlay sharp on|off        draw the text of the view sharp (as --text sharp does)
//   overlay frame               one drawn frame: renders, decides the width of the panorama, updates the overlays (fades run on the engine's clock)
//                               prints `overlay frame t=<ms> view=<0|1> k=<scale> u=<u> pano=<0|1> show=L,R words=N msg=<alpha> strip=<alpha> want=<0|1> ...`
//   overlay shot FILE.bmp       the picture of the last frame (24-bit, W x H)
//   overlay move WX,WY          the free pointer at window position WX,WY (Engine::mouseMove with the inverse mapping); prints the engine position
//   overlay map EX,EY           the window position of an engine pointer position
//   overlay motion DX,DY        the captured pointer moved by DX,DY window pixels (the engine gets whole pixels: DX / k, in bar mode DX / u; the rest is carried)
//   overlay layout              prints the layout numbers
//   overlay diff                the presented picture of the last frame (the window's overlay composition, or the 640 x 480 frame when it is not a view frame / the overlay is off)
//                               against the previous two: `overlay diff n= size= t= mean= ext= state= d1= m1= d2= m2= grid=` (see tests/audit/visual.py)
//   overlay fault statuspal|blue N|none   faults for testing the checker
//   overlay pump on|off         every turn of the engine's blocking loops (transition film, cross-fade) presents a frame: `overlay frame` + `overlay diff`
//   overlay clean on|off|stats  the window's picture cleaner (synchronous) is fed by `overlay frame` as drawOverlayFrame feeds it; stats prints its job counters
#include "app/clean.h"
#include "app/overlaycompose.h"
#include "app/text_sharp.h"
#include "engine/panowide.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <memory>
#include <sstream>

namespace biosys {

namespace {

struct ScriptOverlay {
    bool on = false, sharp = false, pump = false, inPump = false;
    bool faultStatusPal = false;    // test of the test: the old bug (the status screen's palette is given to the page cleaner)
    int faultBlue = -1;             // test of the test: the presented picture of this diff (counted from now) has red and blue swapped
    int W = 1920, H = 1080;
    OverlayScene scene;
    WideController wide;
    Engine::TextFrame tf;
    Frame noFilter;
    Palette pal;
    std::vector<u32> tex;
    std::vector<u8> bare;
    Engine::PanoramaFrame pfCopy;
    OverlayView view;
    bool viewFrame = false;
    int showL = 0, showR = 0;
    std::vector<int16_t> rain;
    bool haveFrame = false;
    ovl::MotionCarry carry;
    std::unique_ptr<CleanFront> clean;
    // the window's PanoramaLayer, as far as the picture goes: the cleaned texture is filled band by band (8 MB per frame), used once every row is there
    std::vector<u32> cleanTex;
    std::vector<char> cleanCover;
    u32 cleanSerial = 0;
    bool haveCleanSerial = false, useClean = false;
    int cleanGen = 0;               // number of palette changes the cleaner has been given (of the page shown)
    uint32_t cleanPalHash = 0;
    std::vector<int> rowGen;        // per texture row: the palette generation of the band that filled it
    double mixFraction() const {    // fraction of the rows that do not belong to the latest palette (a texture that is being refreshed band by band)
        if (!useClean || rowGen.empty()) return 0;
        size_t n = 0;
        for (int g : rowGen) if (g != cleanGen) n++;
        return static_cast<double>(n) / rowGen.size();
    }
    int cleanCovered = 0;
    // overlay diff
    std::vector<u32> prev1, prev2;
    int prevW = 0, prevH = 0, diffN = 0;
    std::string heldInfo = "-";
    int heldX = 0, heldY = 0, heldW = 0, heldH = 0;
    std::vector<char> heldMask;     // opaque pixels of the held image (engine resolution)
    std::vector<u32> heldPrev;      // the window pixels of the held picture in the previous diff
    int heldPrevN = 0;
    int bandsThisFrame = 0, bandsTotal = 0;
    bool texIsClean = false;
    void applyBands(std::vector<CleanBand>& bands, u32 serial) {
        const int tw = cleanTexW(2), th = cleanTexH(2);
        for (CleanBand& b : bands) {
            if (b.serial != serial) continue;
            if (cleanTex.empty()) cleanTex.assign(static_cast<size_t>(tw) * th, 0);
            if (!haveCleanSerial || cleanSerial != serial) {
                cleanSerial = serial;
                haveCleanSerial = true;
                cleanCover.assign(static_cast<size_t>(th), 0);
                cleanCovered = 0;
                useClean = false;
                rowGen.assign(static_cast<size_t>(th), -1);
            }
            if (rowGen.size() != static_cast<size_t>(th)) rowGen.assign(static_cast<size_t>(th), -1);
            std::memcpy(&cleanTex[static_cast<size_t>(b.ty0) * tw], b.px.data(), b.px.size() * 4);
            for (int y = b.ty0; y < b.ty1; y++) rowGen[static_cast<size_t>(y)] = cleanGen;
            for (int y = b.ty0; y < b.ty1; y++)
                if (!cleanCover[static_cast<size_t>(y)]) { cleanCover[static_cast<size_t>(y)] = 1; cleanCovered++; }
        }
        bands.clear();
        useClean = haveCleanSerial && cleanSerial == serial && cleanCovered == th;
    }
};

ScriptOverlay& state() {
    static ScriptOverlay s;
    return s;
}

} // namespace

bool overlayPumpActive() { return state().pump; }

void overlayPumpFrame(Engine& e) {
    ScriptOverlay& S = state();
    if (S.inPump) return;
    S.inPump = true;
    try {
        if (S.on) scriptOverlayCommand(e, "overlay", "frame");
        scriptOverlayCommand(e, "overlay", "diff");
    } catch (...) {
        S.inPump = false;
        throw;
    }
    S.inPump = false;
}

bool scriptOverlayCommand(Engine& e, const std::string& op, const std::string& arg) {
    if (op != "overlay") return false;
    ScriptOverlay& S = state();
    std::istringstream a(arg);
    std::string sub;
    a >> sub;
    if (sub == "on") {
        int w = 0, h = 0;
        if (a >> w >> h) { S.W = w; S.H = h; }
        if (S.W < 64 || S.H < 64 || S.W > 8192 || S.H > 8192) throw Error("script: overlay on [W H] (64..8192)");
        S.on = true;
        S.scene.reset();
        S.wide.reset();
        S.haveFrame = false;
        e.setTextTracking(true);
        std::printf("overlay on %d %d\n", S.W, S.H);
    } else if (sub == "off") {
        S.on = false;
        S.haveFrame = false;
        S.scene.reset();
        std::printf("overlay off\n");
    } else if (sub == "pump") {
        std::string v;
        a >> v;
        if (v != "on" && v != "off") throw Error("script: overlay pump on|off");
        S.pump = v == "on";
    } else if (sub == "fault") {          // overlay fault statuspal | blue N | none: faults for testing the checker (tests/test_visual.py, `selftest`)
        std::string v;
        a >> v;
        if (v == "statuspal") S.faultStatusPal = true;
        else if (v == "blue") { int n = 0; a >> n; S.faultBlue = n; }
        else if (v == "none") { S.faultStatusPal = false; S.faultBlue = -1; }
        else throw Error("script: overlay fault statuspal|blue N|none");
    } else if (sub == "sharp") {
        std::string v;
        a >> v;
        if (v != "on" && v != "off") throw Error("script: overlay sharp on|off");
        S.sharp = v == "on";
    } else if (sub == "clean") {
        std::string v;
        a >> v;
        if (v == "on") {
            CleanOptions co;
            co.async = false;
            S.clean.reset(new CleanFront(co));
            S.cleanTex.clear();
            S.haveCleanSerial = S.useClean = false;
        } else if (v == "off") {
            S.clean.reset();
            S.cleanTex.clear();
            S.haveCleanSerial = S.useClean = false;
        } else if (v == "stats" && S.clean) {
            const CleanService::Stats st = S.clean->svc.stats();
            std::printf("overlay clean pagejobs=%ld framejobs=%ld bands=%ld\n", st.pageJobs, st.frameJobs, st.pageBands);
        } else {
            throw Error("script: overlay clean on|off|stats");
        }
    } else if (sub == "layout") {
        const ovl::Layout L = ovl::layoutFor(S.W, S.H);
        std::printf("overlay layout W=%d H=%d k=%.6f view=%.3f,%.3f,%.3f,%.3f u=%d strip=%d,%d,%d,%d fourthree=%d extra=%d\n", L.W, L.H, L.k, L.viewX, L.viewY, L.viewW, L.viewH, L.u,
                    L.stripX, L.stripY, L.stripW, L.stripH, L.fourThree ? 1 : 0, L.extraCols);
    } else if (sub == "map") {
        double x = 0, y = 0;
        char c = 0;
        a >> x >> c >> y;
        const ovl::Layout L = ovl::layoutFor(S.W, S.H);
        double wx, wy;
        ovl::engineToWindow(L, x, y, wx, wy);
        std::printf("overlay map %g,%g -> %.3f,%.3f\n", x, y, wx, wy);
    } else if (sub == "move") {
        double x = 0, y = 0;
        char c = 0;
        a >> x >> c >> y;
        const ovl::Layout L = ovl::layoutFor(S.W, S.H);
        const ovl::EnginePoint p = ovl::windowToEngine(L, x, y);
        e.mouseMove(static_cast<int>(p.x), static_cast<int>(p.y), 0, 0);
        int px = 0, py = 0;
        e.pointerPos(px, py);
        std::printf("overlay move %g,%g -> %d,%d bar=%d engine=%d,%d barmode=%d\n", x, y, static_cast<int>(p.x), static_cast<int>(p.y), p.bar ? 1 : 0, px, py, e.barMode() ? 1 : 0);
    } else if (sub == "motion") {        // overlay motion DX,DY: the captured pointer moved by DX,DY window pixels (needs `capture on`); prints the engine's pointer
        double x = 0, y = 0;
        char c = 0;
        a >> x >> c >> y;
        const ovl::Layout L = ovl::layoutFor(S.W, S.H);
        const bool moved = overlayCapturedMove(e, L, S.carry, x, y, nullptr);
        int px = 0, py = 0;
        e.pointerPos(px, py);
        std::printf("overlay motion %g,%g moved=%d engine=%d,%d\n", x, y, moved ? 1 : 0, px, py);
    } else if (sub == "frame") {
        if (!S.on) throw Error("script: overlay on first");
        const ovl::Layout L = ovl::layoutFor(S.W, S.H);
        S.viewFrame = e.viewFrame();
        const bool statusOp = S.viewFrame && e.simStatusOpaque();
        const bool wantNoFilter = S.viewFrame && e.pageFilterActive() && !statusOp;
        e.setSplitLayers(S.viewFrame);          // the held hand is a layer: the cover is measured without it, it is drawn back at its own place
        if (wantNoFilter) e.renderNoFilter(S.noFilter);
        e.renderText(S.tf);
        e.setSplitLayers(false);
        const ViewLayers layers = e.viewLayers();
        S.pal = S.tf.palette;
        S.heldInfo = "-";
        S.bandsThisFrame = 0;
        if (layers.held.shown && layers.held.image.w > 0) {       // the held image: place, size, hash of its indices, mean colour of its opaque pixels through the palette
            const ViewLayer& h = layers.held;
            uint64_t hh = 1469598103934665603ull;
            long cr = 0, cg = 0, cb = 0, cn = 0;
            for (u8 v : h.image.px) {
                hh = (hh ^ v) * 1099511628211ull;
                if (static_cast<int>(v) == h.transparent) continue;
                cr += S.pal.c[v].r; cg += S.pal.c[v].g; cb += S.pal.c[v].b; cn++;
            }
            char hb[128];
            std::snprintf(hb, sizeof hb, "%d,%d,%d,%d,%08x,%d,%d,%d", h.x, h.y, h.image.w, h.image.h, static_cast<unsigned>(hh & 0xffffffffu), cn ? static_cast<int>(cr / cn) : 0, cn ? static_cast<int>(cg / cn) : 0,
                          cn ? static_cast<int>(cb / cn) : 0);
            S.heldInfo = hb;
            S.heldX = h.x; S.heldY = h.y; S.heldW = h.image.w; S.heldH = h.image.h;
            S.heldMask.assign(h.image.px.size(), 0);
            for (size_t i = 0; i < h.image.px.size(); i++) S.heldMask[i] = static_cast<int>(h.image.px[i]) != h.transparent;
        } else {
            S.heldW = S.heldH = 0;
        }
        const Frame& shown = S.sharp ? S.tf.free : S.tf.full;
        const Engine::PanoramaFrame& pf = e.panorama();
        S.view = OverlayView();
        S.view.frame = &shown;
        S.view.pal = &S.pal;
        S.view.tf = &S.tf;
        S.view.sharp = S.sharp;
        S.showL = S.showR = 0;
        S.rain.clear();
        if (S.viewFrame && pf.active) {
            S.pfCopy = pf;
            S.bare.assign(pf.bare, pf.bare + static_cast<size_t>(kViewW) * kViewH);
            S.pfCopy.bare = S.bare.data();
            const bool cleanShownPrev = S.clean && S.useClean && S.haveCleanSerial && S.cleanSerial == pf.pageSerial;
            if (!(statusOp && !S.tex.empty()) && !cleanShownPrev) panoTextureAll(*pf.page, *pf.palette, S.tex);     // the status screen up: the textures keep the scene's colours (as PanoramaLayer::plan)
            WideInput in;
            in.frame = &S.tf.full;
            in.noFilter = wantNoFilter ? &S.noFilter : nullptr;
            in.view = pf.view;
            in.bare = S.bare.data();
            in.cam = pf.camera;
            in.limits.on = pf.limitsOn;
            in.limits.yawMin = pf.limitYawMin;
            in.limits.yawMax = pf.limitYawMax;
            in.scale = 1;
            in.availLeft = in.availRight = L.extraCols;
            in.quantum = 1;
            in.keep = statusOp;
            const WideDecision d = S.wide.decide(in);
            S.showL = d.showLeft;
            S.showR = d.showRight;
            if (d.wide && wantNoFilter && !statusOp) panoRainMap(S.tf.full, S.noFilter, pf.view, S.bare.data(), S.rain);
            S.view.pano = true;
            S.view.pf = &S.pfCopy;
            S.view.cam = pf.camera;
            S.view.tex = &S.tex;
            S.view.showL = S.showL;
            S.view.showR = S.showR;
            S.view.rain = S.rain.empty() ? nullptr : &S.rain;
            S.view.statusScreen = statusOp;
        } else {
            S.wide.reset();
        }
        layers.blitInto(S.tf.full);
        layers.blitInto(S.tf.free);
        if (S.clean && S.viewFrame && pf.active && (!statusOp || S.faultStatusPal)) {        // a palette change = a new job for the whole page (CleanService::submitPage)
            uint32_t ph = 2166136261u;
            for (const Color& c : pf.palette->c) ph = (((ph ^ c.r) * 16777619u) ^ c.g) * 16777619u ^ c.b;
            if (ph != S.cleanPalHash) { S.cleanPalHash = ph; S.cleanGen++; }
        }
        if (S.clean && S.viewFrame) {      // as drawOverlayFrame: the status screen up, the page is not given to the cleaner
            S.clean->step(e, shown, S.pal, pf.active && (!statusOp || S.faultStatusPal) ? &pf : nullptr, 1);     // (the window's layout does not use the cleaned 640 x 480 frame, only the page texture: scale 1 keeps the headless run cheap)
            std::vector<CleanBand> up;
            S.clean->popUploads(e.view().pageSerial(), CleanFront::kUploadBytes, up);
            S.bandsThisFrame = static_cast<int>(up.size());
            S.bandsTotal += S.bandsThisFrame;
            S.applyBands(up, e.view().pageSerial());
        }
        S.view.texScale = 1;
        S.texIsClean = false;
        if (S.view.pano && S.clean && S.useClean && S.haveCleanSerial && S.cleanSerial == pf.pageSerial) {
            S.view.tex = &S.cleanTex;
            S.view.texScale = 2;
            S.texIsClean = true;
        }
        int px = 0, py = 0;
        e.pointerPos(px, py);
        SceneInput si;
        si.frame = &S.tf.full;
        si.pal = &S.pal;
        si.runs = &S.tf.runs;
        si.barMode = e.barMode();
        si.flashing = e.bar().flashing(e.now());
        si.pointerInBar = py >= ovl::kBarY;
        si.barObjects = e.bar().objectCount();
        si.nowMs = e.now();
        if (S.viewFrame) S.scene.update(L, si);
        S.haveFrame = true;
        std::string boxes;
        for (size_t i = 0; i < S.scene.words().size(); i++) {
            const Sprite& w = S.scene.words()[i].sprite;
            boxes += " word" + std::to_string(i) + "=" + std::to_string(w.x) + "," + std::to_string(w.y) + "," + std::to_string(w.w) + "," + std::to_string(w.h);
        }
        const Sprite& ms = S.scene.message();
        if (!ms.empty()) boxes += " msgbox=" + std::to_string(ms.x) + "," + std::to_string(ms.y) + "," + std::to_string(ms.w) + "," + std::to_string(ms.h);
        const Sprite& st = S.scene.strip();
        if (!st.empty()) boxes += " stripbox=" + std::to_string(st.x) + "," + std::to_string(st.y) + "," + std::to_string(st.w) + "," + std::to_string(st.h);
        std::printf("overlay frame t=%u view=%d k=%.4f u=%d pano=%d show=%d,%d words=%zu msg=%.3f strip=%.3f want=%d ambient=%ld stripbuilds=%ld textbuilds=%ld runs=%zu%s\n", e.now(),
                    S.viewFrame ? 1 : 0, L.k, L.u, S.view.pano ? 1 : 0, S.showL, S.showR, S.scene.words().size(), S.scene.messageAlpha(), S.scene.stripAlpha(),
                    S.scene.stripWanted() ? 1 : 0, S.scene.ambientBuilds(), S.scene.stripBuilds(), S.scene.textBuilds(), S.tf.runs.size(), boxes.c_str());
    } else if (sub == "diff") {
        // The picture a player sees for the last `overlay frame` (or, with the overlay off, for the frame the engine renders now), compared with the two before.
        std::vector<u32> img;
        int W = 0, H = 0;
        if (S.on && S.haveFrame && S.viewFrame) {
            const ovl::Layout L = ovl::layoutFor(S.W, S.H);
            composeOverlay(L, S.scene, S.view, img);
            W = S.W;
            H = S.H;
        } else {
            Frame f;
            Palette pal;
            if (S.on && S.haveFrame) { f = S.tf.full; pal = S.pal; }
            else { e.render(f); pal = e.view().palette(); }
            W = Frame::W;
            H = Frame::H;
            img.resize(static_cast<size_t>(W) * H);
            for (size_t i = 0; i < img.size(); i++) img[i] = (u32(pal.c[f.px[i]].r) << 16) | (u32(pal.c[f.px[i]].g) << 8) | pal.c[f.px[i]].b;
        }
        if (S.faultBlue >= 0 && S.faultBlue-- == 0)
            for (u32& p : img) p = ((p & 255) << 16) | (p & 0xFF00) | ((p >> 16) & 255);
        const bool same = S.prevW == W && S.prevH == H && S.prev1.size() == img.size();
        const bool same2 = same && S.prev2.size() == img.size();
        constexpr int GX = 16, GY = 9;
        struct Cell { double r = 0, g = 0, b = 0, d1 = 0, d2 = 0; long n = 0; };
        std::vector<Cell> cells(GX * GY);
        double sr = 0, sg = 0, sb = 0, t1 = 0, t2 = 0;
        int m1 = 0, m2 = 0, x0 = W, x1 = -1, y0 = H, y1 = -1;
        for (int y = 0; y < H; y++) {
            const int cy = std::min(GY - 1, y * GY / H);
            for (int x = 0; x < W; x++) {
                const size_t i = static_cast<size_t>(y) * W + x;
                const u32 p = img[i];
                const int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
                Cell& c = cells[static_cast<size_t>(cy) * GX + std::min(GX - 1, x * GX / W)];
                c.r += r; c.g += g; c.b += b; c.n++;
                sr += r; sg += g; sb += b;
                if (p & 0xFFFFFF) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
                if (same) {
                    const u32 q = S.prev1[i];
                    const int d = (std::abs(r - static_cast<int>((q >> 16) & 255)) + std::abs(g - static_cast<int>((q >> 8) & 255)) + std::abs(b - static_cast<int>(q & 255)));
                    c.d1 += d; t1 += d; if (d > m1) m1 = d;
                }
                if (same2) {
                    const u32 q = S.prev2[i];
                    const int d = (std::abs(r - static_cast<int>((q >> 16) & 255)) + std::abs(g - static_cast<int>((q >> 8) & 255)) + std::abs(b - static_cast<int>(q & 255)));
                    c.d2 += d; t2 += d; if (d > m2) m2 = d;
                }
            }
        }
        const double n = static_cast<double>(W) * H;
        std::string grid;
        char buf[64];
        for (const Cell& c : cells) {
            const double k = c.n ? 1.0 / c.n : 0;
            std::snprintf(buf, sizeof buf, "%s%02x%02x%02x:%d:%d", grid.empty() ? "" : ",", static_cast<int>(c.r * k + 0.5), static_cast<int>(c.g * k + 0.5), static_cast<int>(c.b * k + 0.5),
                          static_cast<int>(c.d1 * k / 3.0 * 10 + 0.5), static_cast<int>(c.d2 * k / 3.0 * 10 + 0.5));
            grid += buf;
        }
        // the held picture as it is in the window: mean colour of its opaque pixels and their change since the previous diff
        double hr = 0, hg = 0, hb2 = 0, hdiff = 0;
        long hn = 0;
        if (S.on && S.haveFrame && S.viewFrame && S.heldW > 0) {
            const ovl::Layout L = ovl::layoutFor(S.W, S.H);
            const int X0 = std::max(0, static_cast<int>(std::floor(L.viewX + S.heldX * L.k))), X1 = std::min(W, static_cast<int>(std::ceil(L.viewX + (S.heldX + S.heldW) * L.k)));
            const int Y0 = std::max(0, static_cast<int>(std::floor(L.viewY + S.heldY * L.k))), Y1 = std::min(H, static_cast<int>(std::ceil(L.viewY + (S.heldY + S.heldH) * L.k)));
            std::vector<u32> cur;
            for (int y = Y0; y < Y1; y++)
                for (int x = X0; x < X1; x++) {
                    const int ix = static_cast<int>(std::floor((x + 0.5 - L.viewX) / L.k)) - S.heldX, iy = static_cast<int>(std::floor((y + 0.5 - L.viewY) / L.k)) - S.heldY;
                    if (ix < 1 || iy < 1 || ix >= S.heldW - 1 || iy >= S.heldH - 1) continue;
                    if (!S.heldMask[static_cast<size_t>(iy) * S.heldW + ix] || !S.heldMask[static_cast<size_t>(iy - 1) * S.heldW + ix] || !S.heldMask[static_cast<size_t>(iy + 1) * S.heldW + ix] ||
                        !S.heldMask[static_cast<size_t>(iy) * S.heldW + ix - 1] || !S.heldMask[static_cast<size_t>(iy) * S.heldW + ix + 1]) continue;     // interior of the opaque part only
                    const u32 p = img[static_cast<size_t>(y) * W + x];
                    cur.push_back(p);
                    hr += (p >> 16) & 255; hg += (p >> 8) & 255; hb2 += p & 255; hn++;
                }
            if (hn) { hr /= hn; hg /= hn; hb2 /= hn; }
            if (S.heldPrevN == static_cast<int>(cur.size()) && !cur.empty()) {
                long sum = 0;
                for (size_t i = 0; i < cur.size(); i++) {
                    const u32 a = cur[i], b = S.heldPrev[i];
                    sum += std::abs(static_cast<int>((a >> 16) & 255) - static_cast<int>((b >> 16) & 255)) + std::abs(static_cast<int>((a >> 8) & 255) - static_cast<int>((b >> 8) & 255)) +
                           std::abs(static_cast<int>(a & 255) - static_cast<int>(b & 255));
                }
                hdiff = sum / 3.0 / cur.size();
            }
            S.heldPrev.swap(cur);
            S.heldPrevN = static_cast<int>(S.heldPrev.size());
        } else {
            S.heldPrevN = 0;
        }
        const bool movies = e.movies().count() > 0;
        unsigned palHash = 2166136261u;
        {
            const Palette& pp = (S.on && S.haveFrame) ? S.pal : e.view().palette();
            for (const Color& c : pp.c) palHash = (((palHash ^ c.r) * 16777619u) ^ c.g) * 16777619u ^ c.b;
        }
        std::printf("overlay diff n=%d size=%dx%d t=%u view=%d pano=%d show=%d,%d mean=%.2f,%.2f,%.2f ext=%d,%d,%d,%d have1=%d have2=%d d1=%.4f m1=%d d2=%.4f m2=%d "
                    "pump=%d anim=%d movie=%d fade=%d cross=%d filter=%d status=%d msg=%.2f strip=%.2f pal=%08x clean=%d mix=%.3f bands=%d tb=%ld/%ld hrgb=%.1f,%.1f,%.1f hd=%.4f held=%s grid=%s\n",
                    S.diffN, W, H, e.now(), S.viewFrame ? 1 : 0, S.view.pano ? 1 : 0, S.showL, S.showR, sr / n, sg / n, sb / n, x0, y0, x1, y1, same ? 1 : 0, same2 ? 1 : 0,
                    same ? t1 / n / 3.0 : 0.0, m1 / 3, same2 ? t2 / n / 3.0 : 0.0, m2 / 3, S.inPump ? 1 : 0, e.sprites().anyPlaying() ? 1 : 0, movies ? 1 : 0, e.paletteFadeActive() ? 1 : 0,
                    e.crossFading() ? 1 : 0, e.pageFilterActive() ? 1 : 0, e.simStatusUp() ? 1 : 0, S.on ? S.scene.messageAlpha() : 0.0, S.on ? S.scene.stripAlpha() : 0.0, palHash, !S.clean ? -1 : (S.texIsClean ? 1 : 0), S.mixFraction(), S.bandsThisFrame, S.on ? S.scene.textBuilds() : 0L, S.on ? S.scene.ambientBuilds() : 0L, hr, hg, hb2, hdiff, S.heldInfo.c_str(), grid.c_str());
        S.diffN++;
        S.prev2.swap(S.prev1);
        S.prev1.swap(img);
        S.prevW = W;
        S.prevH = H;
        if (!same) S.prev2.clear();
    } else if (sub == "shot") {
        std::string file;
        a >> file;
        if (!S.on || !S.haveFrame || file.empty()) throw Error("script: overlay shot FILE (after overlay on and overlay frame)");
        if (!S.viewFrame) throw Error("script: this frame is not a view frame (the window draws it as always)");
        const ovl::Layout L = ovl::layoutFor(S.W, S.H);
        std::vector<u32> rgb;
        composeOverlay(L, S.scene, S.view, rgb);
        writeBmp24(file, S.W, S.H, rgb);
        std::printf("overlay shot %s %dx%d\n", file.c_str(), S.W, S.H);
    } else {
        throw Error("script: overlay on|off|sharp|pump|frame|shot|diff|move|map|layout");
    }
    std::fflush(stdout);
    return true;
}

} // namespace biosys
