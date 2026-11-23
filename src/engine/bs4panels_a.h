// BioSys 4 terminal: what the device panels of packet P4 (pages 54..69: cfan, sfan, heat, rain, mist, vent, sump, osm, uv, filt, tide, wave
// and the dead panelsprk) share. Spec analysis/notes/bs4.md sections 1.13 / 2 / 4; the shape of an Init is described in bs4page.h.
//
// Every Init of these panels starts alike (reload name, highlight, feature, the FEATURE_MALFUNCTION check with its "broken" ornament) and
// continues with a few reads of feature parameters whose values are drawn by the widgets of bs4widgets.h; the helpers below are those
// repeated parts. Each carries the exe address of the first function that has it.
#pragma once
#include "engine/bs4.h"
#include "engine/bs4widgets.h"
#include <initializer_list>
#include <string>

namespace biosys::bs4a {

// One row of an Init's `switch (variant)`: feature, name of the main-page hotspot that re-opens the panel, highlight ornament.
struct Variant {
    int variant;
    const char* feature;
    const char* reload;
    const char* highlight;
};
const Variant* findVariant(const Variant* table, size_t n, int variant);

// The start of an Init (exe:00421de0): +0x188 reload name, +0x1bec / +0x1bf8 highlight ornament and its item bit, +0xa4 feature.
void begin(Bs4Page& p, const Variant& v, int weight);
void begin(Bs4Page& p, const std::string& feature, const std::string& reload, const std::string& highlight, int weight);

// GetFeatureParameter with the original's message text when the read fails ("BS4: Failed to get the value of parameter <name> ...").
bool get(Bs4Page& p, int id, const char* paramName, u32& i, double& d);
// A double parameter that has to lie in [0, hi]: logs "... is outside its valid range" and clamps (the 0..hi check of exe:004246c0 and
// others). false: the read failed (logged), the Init stops.
bool getClamped(Bs4Page& p, int id, const char* paramName, double hi, double& value);
void logTooLarge(Bs4Page& p, const char* paramName, u32 value);   // a time of day above 86400 (only logged by the original)

// The FEATURE_MALFUNCTION (7) block that opens every Init (exe:00421de0): not broken hides the ornament "broken"; broken shows it, drops
// the handler, plays click sound 7 and hides `hideItems`. true = the panel is broken, the Init ends here. A failed read counts as not broken.
bool broken(Bs4Page& p, std::initializer_list<int> hideItems);

// FEATURE_ON (1) -> items 1 / 2 / 0xf and page.devOn (exe:00421de0). false: the read failed, the Init ends.
bool onState(Bs4Page& p);
// FEATURE_TIME_ACTIVATE (3) + FEATURE_DAILY_TIME_LIMIT (6) -> the schedule slider's two times and the text rectangles (exe:00421de0).
bool schedule(Bs4Page& p);
// FEATURE_WATER_SOURCE (0x5f, 0x60 or 0x61) -> items 0x11 / 0x12 and the remembered source (exe:00423e60). false: Init ends.
bool waterSource(Bs4Page& p);
// FEATURE_ACTIVE (2) -> items 1 / 2 and page.devActive: `activeShowsFirst` = the order of osm / uv (inactive -> item 1, active -> item 2);
// the other order (active -> item 1) is that of sprk / tide / wave. false: the read failed (logged), the Init ends.
bool activeItems(Bs4Page& p, bool inactiveShowsItem1);

// The bar image of a gauge slot: ornament `name` -> slot.image, slot.bar = (x, y, x + 0x97, y + 4). Logs and sets image -1 when it is missing.
bool findBar(Bs4Page& p, Bs4GaugeSlot& g, const char* name);
// Fill a gauge slot (text rectangle, range, parameter id, current value, bar width).
void setupGauge(Bs4GaugeSlot& g, double value, double min, double max, int paramId, int tl, int tt, int tr, int tb);

// page.loopSound = loop; start or stop it
void setLoop(Bs4Page& p, int loop, bool start);

// Phase 2 of the handlers of osm / uv / wave / tide / sprk (exe:004331d0 ...): FEATURE_ACTIVE changed behind our back -> reload the panel.
void pollActiveReload(Bs4Page& p);
// Phase 2 of rain / mist: FEATURE_WATER_SOURCE changed -> reload (exe:00434370).
void pollWaterSourceReload(Bs4Page& p);

// The setters called by InitPanel after verb 0x17 (spec 1.13): `now` / `old` are the visible masks.
void setOnFromMask(Bs4Page& p, u32 now, u32 old);                 // exe:004323c0
void setActiveFromMask(Bs4Page& p, u32 now, u32 old);             // exe:00431f00 (item 2 = on)
void setActiveFromMaskB(Bs4Page& p, u32 now, u32 old);            // exe:00432180 (item 1 = on)
void setOnAndWaterSource(Bs4Page& p, u32 now, u32 old);           // exe:004324c0 / exe:004325d0

// ---- the two-value state of the range sliders (the original keeps copies of the +0x538.. fields at +0x5a8 / +0x5e0 / +0x618) --------------
struct RangeState {
    double a = 0, b = 0;
    bool dragA = false, dragB = false;
    Bs4Rect markerA, markerB;
};
// copy `s` into page.range, set the rest as given, run exe:00437e90 and copy the result back (exe:00434610 / exe:00434920)
void runRange(Bs4Page& p, RangeState& s, int phase, const Bs4Rect& textA, const Bs4Rect& textB, int y, int paramA, int paramB, double scale);
RangeState& heatDay();         // +0x5a8
RangeState& heatNight();       // +0x5e0
RangeState& mistRange();       // +0x618
u32& waterSourceState();       // +0x4d4

// One line of text for the tests (tools/bs4test.cpp `p4`): the widget state of the panel page.
std::string debugState(Bs4Page& p);

} // namespace biosys::bs4a
