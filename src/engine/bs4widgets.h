// BioSys 4 terminal: the helper widgets shared by the pages (spec analysis/notes/bs4.md section 2, packet P2). Each works on the
// fields of a Bs4Page (bs4page.h) and follows the page handler protocol: phase 1 = draw, 2 = poll the parameter and redraw if it
// changed, 3 = mouse event (page.mouseEvent / mouseX / mouseY; the widget writes page.mayContinue). A panel's handler calls the
// widgets it needs, e.g. `[&p](int phase) { bs4w::activeIndicator(p, phase); bs4w::scheduleSlider(p, phase); }`.
#pragma once
#include "engine/bs4page.h"

namespace biosys::bs4w {

// ftol(((v - min) * 100 / (max - min)) * 151.0 * 0.01): the pixel width of a bar of the 151-pixel bar images
int gaugePercent(double v, double min, double max);

// exe:00438910 (+ the slot wrappers exe:00438ca0 / 00438d90 / 00438e80, which only copy a slot of the page into the working slot: here a
// slot is passed directly). Phase 1 draws the value text (grey background, `%.1f`, "100.0" shown as "100") and the width-clipped bar
// image; phase 2 reads the parameter (page.feature, or the NULL feature for the world ids 0x3f2..0x3f4, 0x3fd..0x3ff, 0x834..0x837),
// clamps it to [min, max] and redraws when the `%.1f` text changed.
void barGauge(Bs4Page& p, Bs4GaugeSlot& slot, int phase);

// exe:00438f70: level tier icon (57x57 ornament named 0to9 / 10to20 / 21to40 / 41to60 / 61to80 / 81plus at its own position).
// `percent`: the integer the tier is chosen from. UNRESOLVED exe:00438f70: the original calls __ftol() on whatever the x87 stack
// holds at that moment (the caller, the handler exe:00433dc0, leaves the last gauge value there); the caller passes it explicitly.
void tierIcon(Bs4Page& p, int phase, int percent);

// exe:00439140: a 15x13 alarm icon (ornament `dangerlev`) at pos + (-0x13,-3), cleared with palette index `clearIndex`, shown while
// FEATURE_ALARMS (0x32) & (maskA | maskB) != 0. pos = the position the page code gives (x, y).
void alarmBitIcon(Bs4Page& p, int phase, const int pos[2], int clearIndex, u32 maskA, u32 maskB);

// exe:00439340: FEATURE_ACTIVE (2) -> page.devActive; shows / hides item 0x19 and starts / stops the page's loop sound
void activeIndicator(Bs4Page& p, int phase);

// exe:00436d60 / exe:00437470 / exe:00437e90
void timeLimitSlider(Bs4Page& p, int phase);      // FEATURE_TIME_LIMIT (5), 1 px = 10 min, 6 px = 1 h, seconds = x * 600 - 0x3840
void scheduleSlider(Bs4Page& p, int phase);       // TIME_ACTIVATE (3) + DAILY_TIME_LIMIT (6), two markers
void valueRangeSlider(Bs4Page& p, int phase);     // two values (page.range.paramA / paramB), x = value / scale + 0x18

// slider geometry (exposed for tests): x -> seconds of day, and (hours, minutes) -> marker x
u32 sliderSeconds(int markerLeft);
int sliderX(u32 hours, u32 minutes);

} // namespace biosys::bs4w
