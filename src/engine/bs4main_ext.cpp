// BS4 main-page dynamics (packet P3): the Bs4MainExt that connects the functions of bs4main_*.cpp to the places where exe:0041a580 /
// 0041baf0 / 0041c690 / 0041ddf0 call them, and the helpers they share. Header: bs4main.h; internals: bs4main_priv.h.
#include "engine/bs4main_priv.h"
#include <cstdarg>
#include <cstdio>

namespace biosys {
namespace bs4main {

bool getFeature(Bs4Page& p, const std::string& feature, int id, u32& outInt, double& outValue) {
    return p.owner().host().onGetFeatureParameter(feature, id, outInt, outValue);
}

void logf(Bs4Page& p, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    p.owner().log(buf);
}

bool pageIs(const Bs4Page& p, const char* name) { return bs4Iequals(p.name(), name); }

bool isProbeForecastPage(const Bs4Page& p) { return pageIs(p, "xtemp") || pageIs(p, "xwind") || pageIs(p, "xsun"); }

void repaint(Bs4Page& p) {
    p.paint();                                   // exe:0041baf0
    p.repaintScreen();                           // +0x1b74 = 1
}

namespace {

// exe:0041a580, after the gauge slots and the world alarms, in this order
void afterLoad(Bs4Page& p) {
    buildAlarmStates(p);                         // 00439890
    initProbePage(p);                            // 00439b60
    updateHealthStates(p, 0);                    // 0043a5f0
    resetPodPosition();
    updatePodPosition(p, 0);                     // 0043a920
    updateEmailMarks(p, 0);                      // 0043bc70
    updateMenuScreen(p, 0);                      // 0043ad00
}

// exe:0041a580, the last step: the mail may have arrived
void loaded(Bs4Page& p) { checkEmailArrival(p, 0); }       // 0043b990

// exe:0041baf0 steps (d)..(f) and the same calls of exe:0041a580 after its walk (while the page is not yet active the original calls only
// 004399b0 and 00439d70 there)
void paint(Bs4Page& p) {
    if (p.isActive()) paintOverlays(p);
    paintAlarmIcons(p);                          // 004399b0
    drawProbeValue(p);                           // 00439d70
    if (p.isActive()) paintMail(p);
}

// exe:0041c690, once per second, after the gauges
void tick(Bs4Page& p) {
    updateOverlayStates(p);                      // 004396a0
    updateAlarmStates(p);                        // 00439a90
    checkEmailArrival(p, 1);                     // 0043b990
    readProbeValue(p, 1);                        // 0043a110
    updateHealthStates(p, 1);                    // 0043a5f0
    updatePodPosition(p, 1);                     // 0043a920
    updateEmailMarks(p, 1);                      // 0043bc70
    updateMenuScreen(p, 1);                      // 0043ad00
}

// exe:0040a760 -> exe:0041deb0 (bs4hooks.cpp calls hotspotAction after every successful verb)
void hotspotAction(Bs4Page& p, const Bs4Action& a) { bs4OnEmailListPage(p, a); }

Bs4MainExtRegistrar registrar(Bs4MainExt{"", afterLoad, loaded, paint, tick, onMainPageShown, hotspotAction});

} // namespace

} // namespace bs4main

namespace bs4main_debug {
std::vector<int> overlayStates() { return bs4main::dbg::overlays(); }
std::vector<int> alarmStates() { return bs4main::dbg::alarms(); }
std::string probeText() { return bs4main::probeTextNow(); }
int mailImage() { return bs4main::mailImageNow(); }
int podPosition() { return bs4main::podPositionNow(); }
}

} // namespace biosys
