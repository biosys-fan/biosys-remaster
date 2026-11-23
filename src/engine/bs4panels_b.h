// BioSys 4 terminal, packet P5 (device panels B + hint boxes): the code the ten panels of bs4panel_algs / _cond / _oxy / _power /
// _hint.cpp have in common. Every init of the original repeats these steps inline (copy of the feature name, read of a parameter with the
// valid-range check, look-up of the bar ornament by name, the schedule read-out); here they are written once. The messages are the
// exe's strings (the original passes them to ReportMessage).
#pragma once
#include "engine/bs4.h"
#include "engine/bs4panels_a.h"
#include "engine/bs4widgets.h"
#include <string>

namespace biosys::bs4b {

// "BS4: Failed to get the value of parameter <what> for BiosFear feature '<feature>'."
std::string failGet(const std::string& what, const std::string& feature);
std::string failSet(const std::string& what, const std::string& feature);
// "BS4: Can't find image '<image>' on panel page that represents BiosFear feature '<feature>'."
std::string noImage(const std::string& image, const std::string& feature);

// event 27 with the original's own message on failure (the generic Bs4Page::getParam text is not the exe's).
bool read(Bs4Page& p, const std::string& feature, int id, u32& i, double& d, const std::string& failMessage);

// A gauge value read (the first half of the gauge set-up of every init): get `id`, log `rangePrefix + "%f" + rangeSuffix` and clamp when
// the value is outside [min, max] (the original clamps the value it has just read), store it in `slot.last`. False: the get failed.
bool readGauge(Bs4Page& p, Bs4GaugeSlot& slot, const std::string& feature, int id, double min, double max, const std::string& failMessage,
               const std::string& rangePrefix, const std::string& rangeSuffix);
// The second half: text rect, parameter id, [min, max], the bar ornament `ornament` (first type-1 ornament of that name: image and
// position), the 151 x 4 bar rect at the ornament's position and the pixel width from slot.last. False (and image = -1): no such ornament.
bool setupBar(Bs4Page& p, Bs4GaugeSlot& slot, const Bs4Rect& text, int id, double min, double max, const char* ornament,
              const std::string& missingMessage);

// FEATURE_MALFUNCTION (7) of the cond / sub / bubl / algs inits (exe:00429d50 and the same code in exe:00427630 ...): the ornament
// "broken" is shown while the device is broken; then the handler is cleared and click sound 7 played. Returns true when broken.
bool malfunction(Bs4Page& p, const std::string& feature);

// FEATURE_TIME_ACTIVATE (3) + FEATURE_DAILY_TIME_LIMIT (6) into p.schedule (the on / off times, exe:00429d50). False: a get failed.
bool readSchedule(Bs4Page& p, const std::string& feature);

// the setters exe:004323c0 (FEATURE_ON, panelalgs / panelbubl) and exe:00431f00 (FEATURE_ACTIVE, paneloxy) are the ones of bs4panels_a.cpp
using bs4a::setOnFromMask;
using bs4a::setActiveFromMask;

// the 3-gauge handler of panelcond / panelsub (exe:00433850 / 00433b00): gauge[0..2], not for the mouse phase
void threeGauges(Bs4Page& p, int phase);

} // namespace biosys::bs4b
