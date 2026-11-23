#include "engine/bs4widgets.h"
#include "engine/bs4.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace biosys::bs4w {

namespace {

std::string fmt(const char* f, double v) {
    char b[64];
    std::snprintf(b, sizeof b, f, v);
    return b;
}
std::string lu(u32 v) { return std::to_string(static_cast<unsigned long>(v)); }
std::string hm(u32 h, u32 m) {               // "%lu:%02lu"
    char b[64];
    std::snprintf(b, sizeof b, "%lu:%02lu", static_cast<unsigned long>(h), static_cast<unsigned long>(m));
    return b;
}

// the three text styles of spec 1.10
constexpr int kGreenR = 0, kGreenG = 58, kGreenB = 8;           // 0x02083a00: on time / first value
constexpr int kRedR = 99, kRedG = 0, kRedB = 0;                 // 0x02000063: off time / second value
constexpr int kGreyR = 76, kGreyG = 74, kGreyB = 74;            // 0x024a4a4c: gauge value text

void offsetRect(Bs4Rect& r, int dx) { r.left += dx; r.right += dx; }

// x of a marker for a time of day: exe:00436d60 phase 1 (minutes rounded to the nearest 10)
int markerX(u32 hours, u32 minutes) {
    u32 m = minutes;
    const u32 r = m % 10;
    m = (r + 5 < 10) ? m - r : m - r + 10;
    return static_cast<int>(m / 10 + (hours * 3 + 0xc) * 2);
}

bool isWorldParam(int id) {
    return id == 0x3f3 || id == 0x3f2 || id == 0x3f4 || id == 0x3fe || id == 0x3ff || id == 0x3fd || id == 0x834 || id == 0x835 ||
           id == 0x836 || id == 0x837;
}

} // namespace

u32 sliderSeconds(int markerLeft) { return static_cast<u32>(markerLeft) * 600u - 0x3840u; }
int sliderX(u32 hours, u32 minutes) { return markerX(hours, minutes); }

int gaugePercent(double v, double mn, double mx) {
    const double frac = ((v - mn) * 100.0) / (mx - mn);
    return static_cast<int>(frac * 151.0 * 0.01);                // ftol: truncation
}

// ------------------------------------------------------------------------------------------------ exe:00438910
void barGauge(Bs4Page& p, Bs4GaugeSlot& g, int phase) {
    Bs4& o = p.owner();
    if (phase == 1) {
        if (g.paramId != 0x3f3) {
            p.fill(g.text, o.colours().grey);
            std::string t = fmt("%.1f", g.last);
            if (t == "100.0") t = "100";
            if (!g.text.none()) p.drawText(g.text, t, kGreyR, kGreyG, kGreyB, 0x25);
        }
        if (g.image != -1) {
            p.fill(g.bar, g.paramId == 0x3f3 ? o.colours().white : o.colours().grey);
            const int pos[2] = {g.bar.left, g.bar.top};
            // the original paints the bar at the rectangle's left/top, opaque, `percent` columns wide
            p.paintImage(g.image, pos[0], pos[1], false, Color(), g.percent, nullptr);
        }
    } else if (phase == 2) {
        const std::string name = isWorldParam(g.paramId) ? std::string() : p.feature;
        u32 vi = 0;
        double v = 0;
        if (!p.getParam(name, g.paramId, vi, v)) return;
        if (v < g.min) v = g.min;
        if (g.max < v) v = g.max;
        if (fmt("%.1f", v) != fmt("%.1f", g.last)) {
            g.last = v;
            g.percent = gaugePercent(v, g.min, g.max);
            barGauge(p, g, 1);
            p.repaintScreen();
        }
    }
}

// ------------------------------------------------------------------------------------------------ exe:00438f70
void tierIcon(Bs4Page& p, int phase, int percent) {
    if (phase == 1) {
        if (p.tierImage != -1) p.paintImage(p.tierImage, p.tierPos[0], p.tierPos[1], false, Color(), -1, nullptr);
        return;
    }
    if (phase != 2) return;
    const char* tier;
    if (percent < 10) tier = "0to9";
    else if (percent < 0x15) tier = "10to20";
    else if (percent < 0x29) tier = "21to40";
    else if (percent < 0x3d) tier = "41to60";
    else tier = percent > 0x50 ? "81plus" : "61to80";
    if (p.tierName == tier) return;
    p.tierName = tier;
    int found = -1;
    for (size_t i = 0; i < p.rec.ornaments.size(); i++)
        if (p.rec.ornaments[i].type == 1 && iequals(p.rec.ornaments[i].name, p.tierName)) { found = static_cast<int>(i); break; }
    if (found < 0) {
        p.tierImage = -1;
        return;
    }
    const Ornament& orn = p.rec.ornaments[static_cast<size_t>(found)];
    p.tierImage = static_cast<int>(orn.image);
    p.tierPos[0] = orn.x;
    p.tierPos[1] = orn.y;
    tierIcon(p, 1, percent);
    p.repaintScreen();
}

// ------------------------------------------------------------------------------------------------ exe:00439140
void alarmBitIcon(Bs4Page& p, int phase, const int pos[2], int clearIndex, u32 maskA, u32 maskB) {
    if (phase == 1) {
        if (p.dangerLev == -1) return;
        Bs4Rect r{pos[0] - 0x13, pos[1] - 3, pos[0] - 0x13 + 0xf, pos[1] - 3 + 0xd};
        p.fill(r, clearIndex);
        if ((maskA & p.alarmBits) != 0 || (maskB & p.alarmBits) != 0) {
            Color cyan;                                   // key bytes B=0xff, G=0xff, R=0: the transparent cyan
            cyan.r = 0; cyan.g = 0xff; cyan.b = 0xff;
            p.paintImage(p.dangerLev, r.left, r.top, true, cyan, -1, nullptr);
        }
    } else if (phase == 2) {
        u32 bits = 0;
        double d = 0;
        if (!p.getParam(p.feature, 0x32, bits, d)) return;
        bool changed = false;
        if ((p.alarmBits & maskA) != (bits & maskA)) {
            p.alarmBits = (bits & maskA) == 0 ? (p.alarmBits & ~maskA) : (p.alarmBits | maskA);
            changed = true;
        }
        if ((p.alarmBits & maskB) != (bits & maskB)) {
            p.alarmBits = (bits & maskB) == 0 ? (p.alarmBits & ~maskB) : (p.alarmBits | maskB);
            changed = true;
        }
        if (changed) {
            alarmBitIcon(p, 1, pos, clearIndex, maskA, maskB);
            p.repaintScreen();
        }
    }
}

// ------------------------------------------------------------------------------------------------ exe:00439340
void activeIndicator(Bs4Page& p, int phase) {
    u32 vi = 0;
    double d = 0;
    if (!p.getParam(p.feature, 2, vi, d)) return;
    const bool active = vi != 0;
    if (phase == 1) {
        p.devActive = active;
        p.showItem(0x19, active);
        return;
    }
    if (p.devActive != active) {
        p.devActive = active;
        p.showItem(0x19, active);
        p.paint();
        p.repaintScreen();
        if (p.loopSound != -1) {
            if (p.devActive) p.owner().loopStart(p.loopSound);
            else p.owner().loopStop(p.loopSound);
        }
    }
}

// ------------------------------------------------------------------------------------------------ exe:00436d60
void timeLimitSlider(Bs4Page& p, int phase) {
    Bs4& o = p.owner();
    Bs4TimeSlider& s = p.timeSlider;
    if (phase == 1) {
        p.fill(s.hoursText, o.colours().green);
        p.fill(s.minutesText, o.colours().green);
        p.drawText(s.hoursText, lu(s.hours), kGreenR, kGreenG, kGreenB, 0x25);
        p.drawText(s.minutesText, lu(s.minutes), kGreenR, kGreenG, kGreenB, 0x25);
        if (p.markOn != -1 && !s.dragging) {
            if (!s.marker.none()) p.fill(s.marker, o.colours().grey);
            p.paintImage(p.markOn, markerX(s.hours, s.minutes), p.sliderY, false, Color(), -1, &s.marker);
        }
        return;
    }
    if (phase == 3) {
        p.mayContinue = 1;
        if (s.dragging) p.mayContinue = 0;
        if (s.marker.none()) return;
        const int ev = p.mouseEvent;
        if (ev == 3) {
            if (p.devActive) return;
            if (p.mouseX < s.marker.left || s.marker.right < p.mouseX || p.mouseY < s.marker.top || s.marker.bottom < p.mouseY) return;
            s.dragging = true;
            o.clipToPanelRect();
            p.dragStartX = p.mouseX;
            p.mayContinue = 0;
            p.dragOffset = p.mouseX - s.marker.left;
        } else if (ev == 1) {
            if (!s.dragging) return;
            const int delta = (p.mouseX - p.dragOffset) - s.marker.left;
            if (delta == 0) return;
            Bs4Rect r = s.marker;
            offsetRect(r, delta);
            if (r.left < 0x18) offsetRect(r, 0x18 - r.left);
            if (static_cast<u32>(r.left) > 0xa8u) offsetRect(r, 0xa8 - r.left);
            if (r == s.marker) return;
            p.fill(s.marker, o.colours().grey);
            p.paintImage(p.markOn, r.left, p.sliderY, false, Color(), -1, &s.marker);
            const u32 h = s.hours, m = s.minutes;
            const u32 secs = sliderSeconds(s.marker.left);
            s.hours = secs / 0xe10;
            s.minutes = (secs % 0xe10) / 0x3c;
            timeLimitSlider(p, 1);
            s.hours = h;
            s.minutes = m;
            p.repaintScreen();
        } else if (ev == 2) {
            if (!s.dragging) return;
            s.dragging = false;
            p.mayContinue = 1;
            o.releasePointer();
            if (p.mouseX != p.dragStartX) {
                if (!p.setParam(p.feature, 5, static_cast<int>(sliderSeconds(s.marker.left)), 0, 0)) return;
            }
            o.host().requestMouseMove();
        } else if (ev == 4) {
            if (!s.dragging) return;
            s.dragging = false;
            p.mayContinue = 1;
            o.releasePointer();
            // ORIGINAL BUG (probably): exe:00436d60 sets both text fields to 30 when the pointer leaves during a drag; the next poll rewrites them
            s.hours = 0x1e;
            s.minutes = 0x1e;
        }
        return;
    }
    if (phase != 2) return;
    u32 vi = 0;
    double d = 0;
    if (p.getParam(p.feature, 2, vi, d)) {
        const bool active = vi != 0;
        if (p.devActive != active) {
            if (s.dragging) {
                s.dragging = false;
                p.mayContinue = 1;
                o.releasePointer();
            }
            p.reload = true;
            return;
        }
    }
    if (!p.getParam(p.feature, 5, vi, d)) return;
    if (vi > 0x15180u) vi = 0x15180u;
    const u32 h = vi / 0xe10, m = (vi % 0xe10) / 0x3c;
    if (s.hours == h && s.minutes == m) return;
    s.hours = h;
    s.minutes = m;
    bool changed = false;
    bool c = false;
    if (p.devActive) {
        if (!(p.cachedMask & 4)) {
            p.showItems(1, false, &c); p.showItems(2, true, &c); p.showItems(0x1f, false, &c);
            changed = true;
        }
    } else if (h == 0 && m == 0) {
        if (!(p.cachedMask & 0x80000000u)) {
            p.showItems(1, false, &c); p.showItems(2, false, &c); p.showItems(0x1f, true, &c);
            changed = true;
        }
    } else if (!(p.cachedMask & 2)) {
        p.showItems(1, true, &c); p.showItems(2, false, &c); p.showItems(0x1f, false, &c);
        changed = true;
    }
    if (changed) {
        p.cachedMask = p.visibleMask();
        p.paint();
    } else {
        timeLimitSlider(p, 1);
    }
    p.repaintScreen();
}

// ------------------------------------------------------------------------------------------------ exe:00437470
void scheduleSlider(Bs4Page& p, int phase) {
    Bs4& o = p.owner();
    Bs4ScheduleSlider& s = p.schedule;
    if (phase == 1 && p.devOn) {
        p.fill(s.onText, o.colours().green);
        p.fill(s.offText, o.colours().red);
        p.drawText(s.onText, hm(s.onHours, s.onMinutes), kGreenR, kGreenG, kGreenB, 0x25);
        p.drawText(s.offText, hm(s.offHours, s.offMinutes), kRedR, kRedG, kRedB, 0x25);
        if (p.markOn != -1 && !s.dragOn) {
            if (!s.markerOn.none()) p.fill(s.markerOn, o.colours().grey);
            p.paintImage(p.markOn, markerX(s.onHours, s.onMinutes), p.sliderY, false, Color(), -1, &s.markerOn);
        }
        if (p.markOff != -1 && !s.dragOff) {
            if (!s.markerOff.none()) p.fill(s.markerOff, o.colours().grey);
            p.paintImage(p.markOff, markerX(s.offHours, s.offMinutes), p.sliderY, false, Color(), -1, &s.markerOff);
        }
        return;
    }
    if (phase == 2) {
        u32 vi = 0;
        double d = 0;
        if (o.host().onGetFeatureParameter(p.feature, 1, vi, d) && p.devOn != (vi != 0)) {
            if (s.dragOn || s.dragOff) {
                s.dragOn = s.dragOff = false;
                p.mayContinue = 1;
                o.releasePointer();
            }
            p.reload = true;
        }
        return;
    }
    if (phase != 3 || !p.devOn) return;
    p.mayContinue = 1;
    if (s.dragOn || s.dragOff) p.mayContinue = 0;
    if (s.markerOn.none() || s.markerOff.left == -1) return;
    const int ev = p.mouseEvent;
    const int mx = p.mouseX, my = p.mouseY;
    auto hit = [&](const Bs4Rect& r) { return r.left <= mx && mx <= r.right && r.top <= my && my <= r.bottom; };
    if (ev == 3) {
        if (hit(s.markerOn)) {
            s.dragOn = true;
            o.clipToPanelRect();
            p.dragStartX = mx;
            p.mayContinue = 0;
            p.dragOffset = mx - s.markerOn.left;
            return;
        }
        if (hit(s.markerOff)) {
            s.dragOff = true;
            o.clipToPanelRect();
            p.dragStartX = mx;
            p.mayContinue = 0;
            p.dragOffset = mx - s.markerOff.left;
        }
    } else if (ev == 1) {
        if (!s.dragOn && !s.dragOff) return;
        Bs4Rect& cur = s.dragOn ? s.markerOn : s.markerOff;
        Bs4Rect& other = s.dragOn ? s.markerOff : s.markerOn;
        const int curImg = s.dragOn ? p.markOn : p.markOff, otherImg = s.dragOn ? p.markOff : p.markOn;
        const int delta = (mx - p.dragOffset) - cur.left;
        if (delta == 0) return;
        Bs4Rect r = cur;
        offsetRect(r, delta);
        if (r.left < 0x18) offsetRect(r, 0x18 - r.left);
        if (static_cast<u32>(r.left) > 0xa8u) offsetRect(r, 0xa8 - r.left);
        if (r == cur) return;
        p.fill(cur, o.colours().grey);
        p.paintImage(otherImg, other.left, p.sliderY, false, Color(), -1, &other);
        p.paintImage(curImg, r.left, p.sliderY, false, Color(), -1, &cur);
        const u32 a = s.onHours, b = s.onMinutes, c = s.offHours, dd = s.offMinutes;
        u32 secs = sliderSeconds(s.markerOn.left);
        s.onHours = secs / 0xe10; s.onMinutes = (secs % 0xe10) / 0x3c;
        secs = sliderSeconds(s.markerOff.left);
        s.offHours = secs / 0xe10; s.offMinutes = (secs % 0xe10) / 0x3c;
        scheduleSlider(p, 1);
        s.onHours = a; s.onMinutes = b; s.offHours = c; s.offMinutes = dd;
        p.repaintScreen();
    } else if (ev == 2) {
        if (!s.dragOn && !s.dragOff) return;
        s.dragOn = s.dragOff = false;
        p.mayContinue = 1;
        o.releasePointer();
        if (mx != p.dragStartX) {
            const u32 on = sliderSeconds(s.markerOn.left);
            s.onHours = on / 0xe10;
            s.onMinutes = (on % 0xe10) / 0x3c;
            if (!p.setParam(p.feature, 3, static_cast<int>(on), 0, 0)) return;
            const u32 off = sliderSeconds(s.markerOff.left);
            s.offHours = off / 0xe10;
            s.offMinutes = (off % 0xe10) / 0x3c;
            const u32 duration = off + (off < on ? 0x15180u - on : 0u - on);     // seconds from switch-on to switch-off, wrapping the day
            if (!p.setParam(p.feature, 6, static_cast<int>(duration), 0, 0)) return;
            scheduleSlider(p, 1);
            p.repaintScreen();
        }
        o.host().requestMouseMove();
    } else if (ev == 4) {
        if (!s.dragOn && !s.dragOff) return;
        s.dragOn = s.dragOff = false;
        p.mayContinue = 1;
        o.releasePointer();
        if (!s.markerOn.none()) { p.fill(s.markerOn, o.colours().grey); s.markerOn = Bs4Rect(); }
        if (!s.markerOff.none()) { p.fill(s.markerOff, o.colours().grey); s.markerOff = Bs4Rect(); }
        scheduleSlider(p, 1);
        p.repaintScreen();
    }
}

// ------------------------------------------------------------------------------------------------ exe:00437e90
void valueRangeSlider(Bs4Page& p, int phase) {
    Bs4& o = p.owner();
    Bs4RangeSlider& s = p.range;
    if (phase == 1 && p.devOn) {
        p.fill(s.textA, o.colours().green);
        p.fill(s.textB, o.colours().red);
        std::string a = fmt("%.1f", s.valueA), b = fmt("%.1f", s.valueB);
        if (a == "100.0") a = "100";
        if (b == "100.0") b = "100";
        p.drawText(s.textA, a, kGreenR, kGreenG, kGreenB, 0x25);
        p.drawText(s.textB, b, kRedR, kRedG, kRedB, 0x25);
        if (p.markOn != -1 && !s.dragA) {
            const int x = static_cast<int>(s.valueA / s.scale) + 0x18;
            if (!s.markerA.none()) p.fill(s.markerA, o.colours().grey);
            p.paintImage(p.markOn, x, s.y, false, Color(), -1, &s.markerA);
        }
        if (p.markOff == -1 || s.dragB) return;
        const int x = static_cast<int>(s.valueB / s.scale) + 0x18;
        if (!s.markerB.none()) p.fill(s.markerB, o.colours().grey);
        p.paintImage(p.markOff, x, s.y, false, Color(), -1, &s.markerB);
        return;
    }
    if (phase == 2) {
        u32 vi = 0;
        double d = 0;
        if (!o.host().onGetFeatureParameter(p.feature, 1, vi, d)) return;
        if (p.devOn == (vi != 0)) return;
        if (s.dragA || s.dragB) {
            s.dragA = s.dragB = false;
            p.mayContinue = 1;
            o.releasePointer();
        }
        p.reload = true;
        return;
    }
    if (phase != 3 || !p.devOn) return;
    p.mayContinue = 1;
    if (s.dragA || s.dragB) p.mayContinue = 0;
    if (s.markerA.none() || s.markerB.left == -1) return;
    const int ev = p.mouseEvent, mx = p.mouseX, my = p.mouseY;
    auto hit = [&](const Bs4Rect& r) { return r.left <= mx && mx <= r.right && r.top <= my && my <= r.bottom; };
    if (ev == 3) {
        if (hit(s.markerA)) {
            s.dragA = true;
            o.clipToPanelRect();
            p.dragStartX = mx;
            p.dragOffset = mx - s.markerA.left;
            p.mayContinue = 0;
            return;
        }
        if (hit(s.markerB)) {
            s.dragB = true;
            o.clipToPanelRect();
            p.dragOffset = mx - s.markerB.left;
            p.dragStartX = mx;
            p.mayContinue = 0;
        }
    } else if (ev == 4) {
        if (!s.dragA && !s.dragB) return;
        s.dragA = s.dragB = false;
        p.mayContinue = 1;
        o.releasePointer();
        if (!s.markerA.none()) { p.fill(s.markerA, o.colours().grey); s.markerA = Bs4Rect(); }
        if (!s.markerB.none()) { p.fill(s.markerB, o.colours().grey); s.markerB = Bs4Rect(); }
        valueRangeSlider(p, 1);
        p.repaintScreen();
    } else if (ev == 2) {
        if (!s.dragA && !s.dragB) return;
        p.mayContinue = 1;
        s.dragA = s.dragB = false;
        o.releasePointer();
        if (mx != p.dragStartX) {
            s.valueA = (s.markerA.left - 0x18) * s.scale;
            if (!p.setParamValue(p.feature, s.paramA, s.valueA)) {
                return;
            }
            s.valueB = (s.markerB.left - 0x18) * s.scale;
            if (!p.setParamValue(p.feature, s.paramB, s.valueB)) return;
            valueRangeSlider(p, 1);
            p.repaintScreen();
        }
        o.host().requestMouseMove();
    } else if (ev == 1) {
        if (!s.dragA && !s.dragB) return;
        Bs4Rect& cur = s.dragA ? s.markerA : s.markerB;
        Bs4Rect& other = s.dragA ? s.markerB : s.markerA;
        const int curImg = s.dragA ? p.markOn : p.markOff, otherImg = s.dragA ? p.markOff : p.markOn;
        const int delta = (mx - p.dragOffset) - cur.left;
        if (delta == 0) return;
        Bs4Rect r = cur;
        offsetRect(r, delta);
        if (r.left < 0x18) offsetRect(r, 0x18 - r.left);
        if (static_cast<u32>(r.left) > 0xa8u) offsetRect(r, 0xa8 - r.left);
        if (s.dragA) {                                    // the first marker may not pass the second
            if (!(s.markerB.left > r.right)) offsetRect(r, (s.markerB.left - r.right) - 1);
        } else if (!(s.markerA.right < r.left)) {         // nor the second the first
            offsetRect(r, (s.markerA.right - r.left) + 1);
        }
        if (r == cur) return;
        p.fill(cur, o.colours().grey);
        p.paintImage(otherImg, other.left, s.y, false, Color(), -1, &other);
        p.paintImage(curImg, r.left, s.y, false, Color(), -1, &cur);
        const double a = s.valueA, b = s.valueB;
        s.valueA = (s.markerA.left - 0x18) * s.scale;
        s.valueB = (s.markerB.left - 0x18) * s.scale;
        valueRangeSlider(p, 1);
        s.valueA = a;
        s.valueB = b;
        p.repaintScreen();
    }
}

} // namespace biosys::bs4w
