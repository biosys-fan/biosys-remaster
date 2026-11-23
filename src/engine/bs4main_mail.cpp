// BS4 main-page dynamics: the mail marks (exe:0043b990 CheckEmailArrival, 0043bc70 UpdateEmailMarks, 0041deb0 OnEmailListPage and the
// steps (e) / (f) of exe:0041baf0). Spec analysis/notes/bs4.md 1.12. The state DAT_004979f8 is Bs4Session::mailState (0 no mail, 1 the story
// mail has arrived, 2 read), the arrival text DAT_00497970 is Bs4Session::mailStamp.
//
// The ornament choice that the spec left open: on a page with both the `ewait` and the `ewaitoff` ornament (all main pages but
// initial / emailpw / synthpw / menuscreen carry them, hidden) the mail mark BLINKS while the state is 1: every second the image of `ewait`
// (page counter even) or of `ewaitoff` (odd) is painted at the position of `ewait`; `numail` (the "new mail" banner of the mail pages)
// is painted at its own position as long as the state is 1. In the state 0 and 2 nothing is shown. The image index DAT_0049bc64 is a
// process global: the page paint repeats the image the last blink step chose.
#include "engine/bs4main_priv.h"
#include <cstdio>

namespace biosys {
namespace bs4main {

namespace {

// DAT_0049bc64: the image the page paint repeats. Written only by checkEmailArrival, which runs at the end of every page load before the first
// paint of an active page, so the initial value is never painted.
int g_mailImage = 0;

void paintAt(Bs4Page& p, int image, const int pos[2]) {      // exe:0041e040 with the opaque flag
    Bs4Rect r;
    p.paintImage(image, pos[0], pos[1], false, Color(), -1, &r);
}

// the second half every call of exe:0043b990 shares: the numail banner
void paintNumail(Bs4Page& p) {
    if (p.numailImage != -1) paintAt(p, p.numailImage, p.numailPos);
}

}

// exe:0043b990. mode 0 = at the end of the page load, 1 = the 1 Hz tick.
void checkEmailArrival(Bs4Page& p, int mode) {
    Bs4Session& s = p.owner().session();
    if (s.mailState == 0) {
        u32 flag = 0;
        double d = 0;
        if (!getFeature(p, "", 0x438, flag, d)) {                        // story flag DEVLIN_EMAIL_HAS_ARRIVED
            if (mode != 1) logf(p, "BS4: Failed to get the value of DEVLIN_EMAIL_HAS_ARRIVED.");
            return;
        }
        if (flag == 0) return;
        if (p.ewaitImage != -1 && p.ewaitOffImage != -1) {
            g_mailImage = p.ewaitImage;
            paintAt(p, g_mailImage, p.ewaitPos);
            p.repaintScreen();
        }
        paintNumail(p);
        s.mailState = 1;
        char buf[512] = {};
        if (p.owner().host().onBS4Query(0xb, buf)) {                     // the arrival time
            buf[511] = 0;
            s.mailStamp = buf;
        }
        return;
    }
    if (s.mailState != 1) return;
    if (p.ewaitImage != -1 && p.ewaitOffImage != -1) {
        g_mailImage = mode == 1 && (p.counter & 1) ? p.ewaitOffImage : p.ewaitImage;
        paintAt(p, g_mailImage, p.ewaitPos);
        p.repaintScreen();
    }
    paintNumail(p);
}

namespace {

// the `bolder` / `extra` ornament and hotspot of the page emailin
void showAndEnable(Bs4Page& p, const char* name) {
    p.setOrnamentVisible(name, true);
    p.setHotspotEnabled(name, true);
}

// the arrival time in white at (253,116) (TextOutA, transparent background)
void drawStamp(Bs4Page& p) {
    Canvas c = p.owner().canvasFor(p.surf);
    c.setBkMode(kTransparent);
    c.setTextAlign(0);
    c.setTextColor(paletteRgb(255, 255, 255));
    c.textOut(253, 116, p.owner().session().mailStamp);
    p.repaintScreen();
}

}

// exe:0043bc70: on `emailin` the `bolder` mark (mail not read yet) or the `extra` mark (read) is the one shown; none while no mail arrived
void updateEmailMarks(Bs4Page& p, int mode) {
    if (!pageIs(p, "emailin")) return;
    // (the original hides the ornaments first, then the hotspots; the order does not matter)
    p.setOrnamentVisible("bolder", false);
    p.setOrnamentVisible("extra", false);
    p.setHotspotEnabled("bolder", false);
    p.setHotspotEnabled("extra", false);
    const u32 state = p.owner().session().mailState;
    if (state == 1) showAndEnable(p, "bolder");
    else if (state == 2) showAndEnable(p, "extra");
    if (mode == 1) {
        repaint(p);
        p.owner().host().requestMouseMove();                             // exe:00451d50
    }
}

// exe:0041baf0 (e): the blink image and the banner while the mail has arrived; (f): the stamp on the open mail of `emailin`
void paintMail(Bs4Page& p) {
    if (p.owner().session().mailState == 1) {
        if (p.ewaitImage != -1 && p.ewaitOffImage != -1) {
            paintAt(p, g_mailImage, p.ewaitPos);
            p.repaintScreen();
        }
        paintNumail(p);
    }
    if (pageIs(p, "emailin") && (p.visibleMask() & 0x20000u)) drawStamp(p);
}

int mailImageNow() { return g_mailImage; }

} // namespace bs4main

// exe:0041deb0
void bs4OnEmailListPage(Bs4Page& main, const Bs4Action& a) {
    using namespace bs4main;
    if (!Bs4Registry::pageSpecific() || main.kind() != Bs4Page::kMain || !main.loaded()) return;
    if (pageIs(main, "emailin")) {
        if (bs4Iequals(a.name, "bolder")) {
            main.owner().session().mailState = 2;
            updateEmailMarks(main, 1);
        }
        if (bs4Iequals(a.name, "extra")) drawStamp(main);
    } else {
        selectProbe(main, a);
    }
}

} // namespace biosys
