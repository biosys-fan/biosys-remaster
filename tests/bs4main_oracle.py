#!/usr/bin/env python3
"""Independent oracle for the BS4 main-page dynamics (packet P3), written from analysis/notes/bs4.md and the decompile of
exe:00439450 .. 0043bc70 and the tables dumped in analysis/out/bs4_tables.txt (not from the C++).

Everything here is plain functions on plain data: the rules of the original (which ornament shows for which parameter value, which
overlay is lit, how tall the probe bar is, ...) and a picture composer for the main pages on top of bs4_oracle.Oracle."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bs4_oracle  # noqa: E402

TABLES = (_DEV + '/analysis/out/bs4_tables.txt')
EXE = (_DEV + '/analysis/bin/BioSys.exe')


# ------------------------------------------------------------------------------------------------------------------ the tables
class Tables:
    def __init__(self):
        sec = None
        rows = {}
        for line in open(TABLES).read().split('\n'):
            if line.startswith('## '):
                sec = line[3:].split(' exe')[0]
                rows[sec] = []
                continue
            if sec and line.strip():
                rows[sec].append(line.split('\t'))
        self.health = [dict(feature=r[1], dead=r[2], unhealthy=r[3], healthy=r[4]) for r in rows['health table']]
        self.devices = [dict(page=r[1], first=int(r[2]), last=int(r[3]), bit=int(r[4]), param=int(r[5], 0)) for r in rows['feature-highlight table']]
        self.overlays = [dict(feature=r[1], ornament=r[2]) for r in rows['overlay table']]
        self.probes = []
        for r in rows['probe-page table']:
            rect = tuple(int(x) for x in re.findall(r'-?\d+', r[4]))
            self.probes.append(dict(page=r[1], bar=r[2], height=int(r[3].split('=')[1]), rect=rect, param=int(r[5].split('=')[1], 0),
                                    min=float(r[6].split('=')[1]), max=float(r[7].split('=')[1]), default=int(r[8].split('=')[1])))
        self.sensors = [dict(feature=r[1], hotspot=r[2]) for r in rows['probe/sensor list']]
        for s in self.sensors:        # the sensor's own mark: hiprob<s|o|e|r><n> for 16_sens<s|o|e|r><n>, hiuw for 16_sensor
            h = s['hotspot']
            s['mark'] = 'hiuw' if h == '16_sensor' else 'hiprob' + h[len('16_sens'):] if h != 'null' else ''
        self.alarm_pages = [dict(page=r[1], m1=int(r[2], 0), m2=int(r[3], 0), first=int(r[4]), last=int(r[5])) for r in rows['alarm-probe page table']]
        self.alarm_orns = [dict(ornament=r[1], feature=r[2]) for r in rows['alarm ornament list']]
        # sanity of the parse
        assert len(self.health) == 28 and len(self.devices) == 45 and len(self.overlays) == 103
        assert len(self.probes) == 20 and len(self.sensors) == 23 and len(self.alarm_pages) == 14 and len(self.alarm_orns) == 22


def exe_sensor_marks():
    """the third string of each sensor row (exe:00487c40 + 0x20), read straight from the exe, to check the naming rule above"""
    d = open(EXE, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
    secs = [struct.unpack_from('<8sIIII', d, pe + 24 + opt + i * 40) for i in range(nsec)]

    def off(va):
        for _, vs, a, rs, ro in secs:
            if a <= va - base < a + max(vs, rs):
                return ro + va - base - a
    out = []
    for i in range(23):
        o = off(0x487c40 + i * 0x2b + 0x20)
        out.append(d[o:o + 11].split(b'\0')[0].decode())
    return out


# ------------------------------------------------------------------------------------------------------------------ the rules
def health_flags(state):
    """(dead, unhealthy, healthy) hidden flags for SIM_HEALTH_STATE 2001 / 2002 / 2003, None = unknown value"""
    return {2001: (0, 1, 1), 2002: (1, 0, 1), 2003: (1, 1, 0)}.get(state)


def lung_dest(ornament):
    """destination of FEATURE_DESTINATION an overlay `hiair<from><to>on` / `hiutil<from><to>on` stands for"""
    m = re.fullmatch(r'hi(?:air|util)([soer])([soer])on', ornament)
    return {'s': 0x16, 'o': 0x18, 'e': 0x17, 'r': 0x15}[m.group(2)]


LUNGS = ('fbs2lung', 'fbo1lung', 'fbd5lung', 'fbr2lung')
PUMPS = ('fbs3watpmp', 'fbo2watpmp', 'fbd3watpmp', 'fbr3watpmp')


def overlay_state(feature, ornament, param, get):
    """the state the overlay of `feature` shows; get(feature, id) -> (int, double) or None when the read fails; None = the read of `param` failed"""
    r = get(feature, param)
    if r is None:
        return None
    i, d = r
    if param == 0x50:
        return 1 if d > 0.0 else 0
    if feature in LUNGS or feature in PUMPS:
        if i == 0:
            return 0
        if feature in LUNGS:
            a = get(feature, 0x11)
            if a is None or a[0] != 0:
                return 0
        b = get(feature, 0x14)
        if b is None:
            return 0
        dest = b[0]
        if not 0x15 <= dest <= 0x18:
            return 0
        return 1 if lung_dest(ornament) == dest else 0
    return 1 if i != 0 else 0


def probe_text(param, v):
    """the text exe:00439d70 draws"""
    if param == 0x23:
        return '%.3f' % v if v < 10.0 else '%.2f' % v
    return '%.1f' % v


def probe_read_text(param, v):
    """the text exe:0043a110 compares to decide on a repaint"""
    return ('%.3f' if param == 0x23 else '%.2f' if param == 0x22 else '%.1f') % v


def probe_bar(page, param, mn, mx, height, v):
    """exe:0043a4a0: the bar height in pixels"""
    if page == 'co2':
        mx = 0.1 if v < 0.1 else 5.0 if v < 5.0 else 100.0
    if v <= mn:
        return 0
    if v >= mx:
        return height
    return int(((v - mn) * 100.0 / (mx - mn)) * height * 0.01)


def menu_names(h):
    """h = [(int, double)] for 0x837 (evo), 0x834 (rf), 0x835 (sav), 0x836 (oc); returns the set of shown ornaments (without the power one)"""
    out = set()
    pre = ['evo', 'rf', 'sav', 'oc']
    for (i, v), p in zip(h, pre):
        if v >= 20.0:
            out.add(p + 'health' if v >= 50.0 else p + 'rej' if i == 0 else p + 'dy')
        else:
            out.add(p + 'ded')
    out.add('biobad' if any(v <= 50.0 for _, v in h) else 'biogood')
    return out


MENU_ALL = ['evoded', 'rfded', 'savded', 'ocded', 'evody', 'rfdy', 'savdy', 'ocdy', 'evohealth', 'rfhealth', 'savhealth', 'ochealth',
            'evorej', 'rfrej', 'savrej', 'ocrej', 'biogood', 'biobad', 'okpower', 'nopower']
POD_NAMES = {0x15: 'podrf', 0x16: 'podsav', 0x17: 'podevo', 0x18: 'podoc', 0x19: 'podhouse'}


# ------------------------------------------------------------------------------------------------------------------ pictures
class Composer:
    """paints main pages like the original: the visible ornaments (flags given), then ornaments painted by name"""

    def __init__(self, oracle):
        self.o = oracle

    def base(self, n, flags):
        return self.o._surface(n, flags)

    def paint(self, surf, n, ornament, opaque=False, at=None):
        """one ornament (dict) painted at its position (or `at`), with its own colour key (flag bit 4) unless `opaque`"""
        p = self.o.page(n)
        iw, ih, stride, raw = p.image_pixels(ornament['image'])
        key = None
        if (ornament['flags'] & 0x10) and not opaque:
            b0, b1, b2 = ornament['transp'][0], ornament['transp'][1], ornament['transp'][2]
            key = self.o.pal.index((b2, b1, b0))
        x0, y0 = at if at else (ornament['x'], ornament['y'])
        h, w = len(surf), len(surf[0])
        for y in range(ih):
            dy = y0 + y
            if not 0 <= dy < h:
                continue
            src = raw[(ih - 1 - y) * stride:(ih - 1 - y) * stride + iw]
            lo, hi = max(0, -x0), min(iw, w - x0)
            if hi <= lo:
                continue
            seg = surf[dy][x0 + lo:x0 + hi]
            src = src[lo:hi]
            surf[dy][x0 + lo:x0 + hi] = bytes(src) if key is None else bytes(s if s != key else t for s, t in zip(src, seg))

    def paint_named(self, surf, n, name):
        for o in self.o.page(n).ornaments:
            if o['type'] == 1 and o['name'].lower() == name.lower():
                self.paint(surf, n, o)
                return True
        return False

    def image_rows(self, n, ornament):
        """top-down rows of the ornament's image"""
        iw, ih, stride, raw = self.o.page(n).image_pixels(ornament['image'])
        return [bytes(raw[(ih - 1 - y) * stride:(ih - 1 - y) * stride + iw]) for y in range(ih)]

    def find(self, n, name):
        for i, o in enumerate(self.o.page(n).ornaments):
            if o['type'] == 1 and o['name'].lower() == name.lower():
                return o
        return None


def diff(surf_a, surf_b, ignore=()):
    """number of differing pixels between two row lists, ignoring [l, t, r, b) rectangles; also the first difference"""
    bad = 0
    first = None
    for y in range(len(surf_a)):
        a, b = surf_a[y], surf_b[y]
        if a == b:
            continue
        for x in range(len(a)):
            if a[x] != b[x] and not any(l <= x < r and t <= y < bt for l, t, r, bt in ignore):
                bad += 1
                if first is None:
                    first = (x, y, a[x], b[x])
    return bad, first


def read_page_bmp(path):
    w, h, rows = bs4_oracle.read_bmp(path)
    return [bytearray(r) for r in rows]
