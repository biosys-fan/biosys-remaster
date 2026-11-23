// BS4 main-page dynamics (packet P3, spec analysis/notes/bs4.md 1.12, 1.13, 2, 3): the page-specific code of the main pages of the
// terminal (health, pod, menuscreen, the probe pages, the device-highlight and alarm overlays, the mail marks). It plugs into the
// terminal through a Bs4MainExt registered in bs4main_ext.cpp (afterLoad / loaded / paint / tick / shown); the one entry the
// hooks of packet P1b have to call themselves is bs4OnEmailListPage below.
#pragma once
#include "engine/bs4page.h"
#include <string>
#include <vector>

namespace biosys {

// exe:0041deb0 OnEmailListPage: the second half of exe:0040a760 AfterAction (after the `tech*` -> `spec*` panel opening did not apply);
// registered as Bs4MainExt::hotspotAction, which bs4hooks.cpp calls (nothing else has to call it):
// on `emailin` the hotspots `bolder` (mail read: Bs4Session::mailState = 2, marks updated) and `extra` (draws the arrival stamp); on
// every other main page the probe-sensor selection (hotspots `16_sens*`). `mainPage` = Bs4::mainPage(), `action` = the dispatched action
// (its name is the hotspot name). Does nothing when page-specific code is off.
void bs4OnEmailListPage(Bs4Page& mainPage, const Bs4Action& action);

// Read-only views of the runtime state of the dynamics, for the tests.
namespace bs4main_debug {
std::vector<int> overlayStates();          // the state dword of each of the 103 overlays (exe:00486410 +0x20)
std::vector<int> alarmStates();            // the state dword of each of the 22 alarm ornaments (exe:00487780 +0x20)
std::string probeText();                   // the text of the last DrawProbeValue (exe:00439d70; "" before the first)
int mailImage();                           // DAT_0049bc64: the image the blinking mail mark paints (exe:0041baf0)
int podPosition();                         // page +0x7c: the last pod position shown
}

} // namespace biosys
