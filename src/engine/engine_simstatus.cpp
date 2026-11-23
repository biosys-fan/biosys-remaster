// The sim-status window of the engine (API 0x4a CreateSimStatusWindow, 0x4b ShowSimStatusWindow, 0x4c GetReleaseSimStatusDC,
// 0x4d DisplaySimStatusImage, 0x4e DestroySimStatusWindow, 0x56 EnableSimStatusHotspot; event 34). Original: exe:00410130 create,
// exe:0040f1b0 the window's set-up, exe:0040f700 show, exe:0040f930 hide, exe:00410410 destroy, exe:0040fa50 its blit, exe:0040fc60 /
// exe:0040fd30 its mouse buttons, exe:0040fdd0 its own cursor sprites. See analysis/notes/gdi_usage.md for the DC.
//
// What the window is: a page ("status", window id -5, 640 x 368) with its own off-screen surface the game draws on (DC, images). While it
// is up it replaces what the main window's blit puts into the main view: with a palette of its own (ShowSimStatusWindow(1, palette, ..))
// the surface is blitted opaque and the palette goes on display; without one the page is blitted first and the surface goes over it with
// palette index `unk1c` transparent. Not done: the original's software cursor sprites (the front end shows SIMARROW / SIMHAND, which
// cursorFile() names), the black fill of the view before the palette change (the opaque blit covers it).
#include "engine/engine.h"

namespace biosys {

// exe:00410130 / exe:0040f1b0: the window object, its cursor bitmaps (CURSORS\SIMARROW.BMP, SIMHAND.BMP) and the page.
int Engine::simStatusCreate(const std::string& pageName) {
    if (sim_) return 1;
    auto s = std::make_unique<SimStatus>();
    s->view.reset(new PageView(files_, dataDir_));
    s->view->setGameLogicAttached(true, false);                // ornaments exactly as the page flags say
    const int n = s->view->findPage(pageName);
    if (!n) {
        report("", "Biosfear: sim status page " + pageName + " is unknown.");
        return 0;
    }
    try {
        loadCursor("SIMARROW");
        loadCursor("SIMHAND");
        s->view->goToPage(n, false);                           // TCPage::Load with window -5: no page events
    } catch (const Error& e) {
        report("", std::string("Failed to create the sim status window. ") + e.what());
        return 0;
    }
    sim_ = std::move(s);
    return 1;
}

// exe:0040f700. palette: 256 x {R, G, B, flags} or nullptr (the palette on display stays). key: transparent palette index of the surface
// when there is no palette of its own.
int Engine::simStatusShow(const u8* palette, bool hotspots, int key) {
    if (!sim_ || !sim_->view) return 0;
    if (view_.pageNumber() == 0) return 0;                     // FUN_00402800(main page)
    if (sim_->up) return 1;
    SimStatus& s = *sim_;
    s.pre = view_.palette();                                   // GetPalette(DAT_004957c8)
    s.preApi = view_.apiPaletteActive();
    Palette shown = s.pre;
    if (palette) {
        for (int i = 0; i < 256; i++) shown.c[i] = Color{palette[4 * i], palette[4 * i + 1], palette[4 * i + 2]};
    }
    const int cyan = shown.find(Color{0, 255, 255});
    if (cyan < 0) {                                            // "There is no transparent colour (0, 255, 255) ..."
        report("", "There is no transparent colour (0, 255, 255) in the sim status palette.  The Engine needs one.");
        return 0;
    }
    s.cyan = cyan;
    barMode_ = false;                                          // FUN_00444890(main, 0)
    pendDx_ = pendDy_ = 0;                                     // FUN_00445470(main, {0, 0})
    lastBarSlot_ = -2;                                         // DAT_00497a0c = -1: the bar's hover is forgotten
    if (palette) {
        s.ownPalette = true;
        s.key = -1;
        showPalette(shown);                                    // IDirectDrawPalette::SetEntries: no OnPaletteChanged
    } else {
        s.ownPalette = false;
        s.key = key;
    }
    s.hotspots = hotspots;
    s.pressValid = false;
    s.up = true;                                               // DAT_0049a39c = 1
    objs_.setSimStatusUp(true);
    updateHover();
    return 1;
}

// exe:0040f930: redraw = the main view is blitted again and the pointer put back; the palette of the window goes either way.
int Engine::simStatusHide(bool redraw) {
    if (!sim_ || !sim_->view) return 0;
    if (!sim_->up) return 1;
    SimStatus& s = *sim_;
    s.up = false;                                              // DAT_0049a39c = 0
    s.pressValid = false;
    objs_.setSimStatusUp(false);
    if (s.ownPalette) {
        if (s.preApi) showPalette(s.pre);
        else view_.clearApiPalette();
        s.ownPalette = false;
    }
    if (redraw && !mediaPaused_) dropFill();                   // the main view is blitted again: a ClearWindow fill is gone
    updateHover();
    return 1;
}

// exe:00410410
void Engine::simStatusDestroy(bool redraw) {
    if (!sim_) return;
    simStatusHide(redraw);
    for (size_t i = 0; i < dcs_.size(); i++)
        if (dcs_[i].window == kSimStatusWindow) {
            CanvasRegistry::remove(dcs_[i].handle);
            dcs_.erase(dcs_.begin() + static_cast<ptrdiff_t>(i));
            break;
        }
    sim_.reset();
}

// exe:0040fa50 (called from the main window's blit while the window is up). keyedPass == false: the status surface replaces the main
// view (own palette); keyedPass: it is laid over what the page blit drew, the key index transparent.
void Engine::simStatusRender(Frame& f, bool keyedPass) {
    SimStatus& s = *sim_;
    Frame g;
    s.view->render(g, false);
    const int top = view_.viewTop();
    for (int y = top; y < top + PageView::ViewH && y < Frame::H; y++) {
        const size_t row = static_cast<size_t>(y) * Frame::W;
        for (int x = 0; x < Frame::W; x++) {
            const u8 v = g.px[row + static_cast<size_t>(x)];
            if (keyedPass && v == s.key) continue;
            f.px[row + static_cast<size_t>(x)] = v;
        }
    }
}

} // namespace biosys
