// BS4 main-page dynamics: the device-highlight overlays (exe:00439450, 004396a0, step (d) of exe:0041baf0) and the alarm-probe icons
// (exe:00439890, 00439a90, 004399b0). Spec analysis/notes/bs4.md section 2 ("Overlay / device-highlight table", "Alarm-probe icons").
#include "engine/bs4main_priv.h"
#include "engine/bs4routes.h"

namespace biosys {
namespace bs4main {

namespace {

// The runtime state dword (+0x20) of the overlay rows (exe:00486410) and of the alarm ornament rows (exe:00487780). They are process
// globals in the original; only the rows of the current page's range are ever read, and those are written first.
int g_overlayState[128];     // (103 used: kOverlayCount)
int g_alarmState[32];        // (22 used: kAlarmOrnamentCount)

bool oneOf(const char* name, const char* const* list, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (bs4Iequals(name, list[i])) return true;
    return false;
}
const char* const kLungs[] = {"fbs2lung", "fbo1lung", "fbd5lung", "fbr2lung"};         // exe:00489678, 004895b4, 00489518, 00489448
const char* const kPumps[] = {"fbs3watpmp", "fbo2watpmp", "fbd3watpmp", "fbr3watpmp"};   // exe:00489634, 00489588, 004894d4, 00489404

// The state one overlay shows after its parameter was read (the common part of exe:00439450 and exe:004396a0):
// AIR_FLOW (0x50) > 0, or the parameter != 0; for the lungs and the pumps also the route has to lead to the overlay's biome.
int overlayState(Bs4Page& p, const Overlay& o, const DeviceRow& row, u32 value, double dvalue) {
    if (row.paramId == 0x50) return dvalue > 0.0 ? 1 : 0;
    const bool lung = oneOf(o.feature, kLungs, 4), pump = !lung && oneOf(o.feature, kPumps, 4);
    if (lung || pump) {
        if (value == 0) return 0;
        int open = 0;
        const bool ok = lung ? bs4LungRouteIsOpen(p.owner().host(), o.feature, o.ornament, open)
                             : bs4PumpRouteIsOpen(p.owner().host(), o.feature, o.ornament, open);
        return ok && open == 1 ? 1 : 0;
    }
    return value != 0 ? 1 : 0;
}

} // namespace

// exe:00439450. The row is the first one of the table whose page is this page and whose item bit is set in `mask`.
void buildOverlayStates(Bs4Page& p, u32 mask) {
    p.deviceRow = -1;
    size_t r = 0;
    for (; r < kDeviceCount; r++)
        if (bs4Iequals(kDevices[r].page, p.name()) && (mask & (1u << (kDevices[r].bit & 31)))) break;
    if (r == kDeviceCount) return;
    p.deviceRow = static_cast<int>(r);
    const DeviceRow& row = kDevices[r];
    for (u32 i = row.first; i <= row.last && i < kOverlayCount; i++) {
        g_overlayState[i] = 0;
        u32 v = 0;
        double d = 0;
        if (!getFeature(p, kOverlays[i].feature, static_cast<int>(row.paramId), v, d)) {
            logf(p, "BS4: Failed to get the value of parameter %lu for BiosFear feature '%s'.", static_cast<unsigned long>(row.paramId),
                 kOverlays[i].feature);
            return;
        }
        g_overlayState[i] = overlayState(p, kOverlays[i], row, v, d);
    }
}

// exe:004396a0. A parameter that cannot be read leaves the state as it was (no message).
void updateOverlayStates(Bs4Page& p) {
    if (p.deviceRow < 0 || static_cast<size_t>(p.deviceRow) >= kDeviceCount) return;
    const DeviceRow& row = kDevices[p.deviceRow];
    bool changed = false;
    for (u32 i = row.first; i <= row.last && i < kOverlayCount; i++) {
        u32 v = 0;
        double d = 0;
        if (!getFeature(p, kOverlays[i].feature, static_cast<int>(row.paramId), v, d)) continue;
        const int s = overlayState(p, kOverlays[i], row, v, d);
        if (g_overlayState[i] != s) {
            changed = true;
            g_overlayState[i] = s;
        }
    }
    if (changed) repaint(p);
}

// exe:0041baf0 step (d): every lit overlay of the row, painted by name at its own position (hidden or not)
void paintOverlays(Bs4Page& p) {
    if (p.deviceRow < 0 || static_cast<size_t>(p.deviceRow) >= kDeviceCount) return;
    const DeviceRow& row = kDevices[p.deviceRow];
    for (u32 i = row.first; i <= row.last && i < kOverlayCount; i++)
        if (g_overlayState[i] != 0) p.paintOrnamentByName(kOverlays[i].ornament, nullptr, -1);
}

namespace {
// exe:00439890 / 00439a90: the red ornament of a probe shows when FEATURE_ALARMS (0x32) has one of the two bits of the page's row
int alarmValue(u32 bits, const AlarmPage& row) { return (bits & row.mask1) != 0 || (bits & row.mask2) != 0 ? 1 : 0; }
}

// exe:00439890
void buildAlarmStates(Bs4Page& p) {
    p.alarmRow = -1;
    size_t r = 0;
    for (; r < kAlarmPageCount; r++)
        if (bs4Iequals(kAlarmPages[r].page, p.name())) break;
    if (r == kAlarmPageCount) return;
    p.alarmRow = static_cast<int>(r);
    const AlarmPage& row = kAlarmPages[r];
    for (u32 i = row.first; i <= row.last && i < kAlarmOrnamentCount; i++) {
        g_alarmState[i] = 0;
        u32 bits = 0;
        double d = 0;
        if (!getFeature(p, kAlarmOrnaments[i].feature, 0x32, bits, d)) {
            logf(p, "BS4: Failed to get the value of parameter FEATURE_ALARMS for BiosFear sensor '%s'.", kAlarmOrnaments[i].feature);
            return;
        }
        g_alarmState[i] = alarmValue(bits, row);
    }
}

// exe:00439a90
void updateAlarmStates(Bs4Page& p) {
    if (p.alarmRow < 0 || static_cast<size_t>(p.alarmRow) >= kAlarmPageCount) return;
    const AlarmPage& row = kAlarmPages[p.alarmRow];
    bool changed = false;
    for (u32 i = row.first; i <= row.last && i < kAlarmOrnamentCount; i++) {
        u32 bits = 0;
        double d = 0;
        if (!getFeature(p, kAlarmOrnaments[i].feature, 0x32, bits, d)) continue;
        const int s = alarmValue(bits, row);
        if (g_alarmState[i] != s) {
            changed = true;
            g_alarmState[i] = s;
        }
    }
    if (changed) repaint(p);
}

// exe:004399b0: the ornament `warn` once if any probe of the row is in alarm; with the item bit 16 of the page shown (the probe
// overview) also the red ornament of every probe in alarm
void paintAlarmIcons(Bs4Page& p) {
    if (p.alarmRow < 0 || static_cast<size_t>(p.alarmRow) >= kAlarmPageCount) return;
    const AlarmPage& row = kAlarmPages[p.alarmRow];
    for (u32 i = row.first; i <= row.last && i < kAlarmOrnamentCount; i++)
        if (g_alarmState[i] == 1) {
            p.paintOrnamentByName("warn", nullptr, -1);                                  // exe:0048d4d4
            break;
        }
    if (p.visibleMask() & 0x10000u)
        for (u32 i = row.first; i <= row.last && i < kAlarmOrnamentCount; i++)
            if (g_alarmState[i] != 0) p.paintOrnamentByName(kAlarmOrnaments[i].ornament, nullptr, -1);
}

namespace dbg {
std::vector<int> overlays() { return std::vector<int>(g_overlayState, g_overlayState + kOverlayCount); }
std::vector<int> alarms() { return std::vector<int>(g_alarmState, g_alarmState + kAlarmOrnamentCount); }
}

} // namespace bs4main
} // namespace biosys
