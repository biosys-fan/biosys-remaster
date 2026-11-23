#!/usr/bin/env python3
"""Independent oracle for the static picture of the BioSys 4 pages (written from analysis/notes/bs4.md 1.5 / 1.6 / 1.7 and the
decompile, not from the C++): page record -> palette-index picture.

Main page: 640x480 surface filled with the transparent colour (palette entry 0,255,255); the type-1 ornaments whose flag bit 0 is
clear are painted in file order at their position (transparent ones, flag bit 4, skip the pixels equal to the palette index of the
ornament's key colour; the key bytes are B,G,R); the screen is white (255,255,255) with the main surface on top, skipping the
transparent colour. Panel: the same for a page of its own size, shown at (x,y) over its main page; opening a panel on a main page
whose visible mask has bit 30 (an item named 30_*) first shows the items 30 and 31 of the main page (spec 1.7, verb 0x1a).

usage as a module: oracle = Oracle(); oracle.main_frame(n); oracle.panel_frame(main, panel, x, y); oracle.mask(n)
"""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import struct
import sys

sys.path.insert(0, (_DEV + '/analysis/scripts'))
import biosys_pages  # noqa: E402  (the existing page-file parser; the picture logic below is independent)

DAT = (_DEV + '/setup-cd/installed/DATA1/Biodata/BIOSYS4.DAT')
PAL = (_DEV + '/game-cd/files/BIODATA/BIOSYS4.PAL')


def read_bmp(path):
    """8-bit BMP -> (w, h, [rows top-down as bytes])"""
    d = open(path, 'rb').read()
    off, = struct.unpack_from('<I', d, 10)
    w, h = struct.unpack_from('<ii', d, 18)
    bpp, = struct.unpack_from('<H', d, 28)
    assert d[:2] == b'BM' and bpp == 8 and h > 0
    stride = (w + 3) & ~3
    rows = [d[off + y * stride: off + y * stride + w] for y in range(h)]
    rows.reverse()
    return w, h, rows


class Oracle:
    def __init__(self):
        self.pf = biosys_pages.PageFile(DAT)
        d = open(PAL, 'rb').read()
        self.pal = [tuple(d[0x18 + 4 * i: 0x18 + 4 * i + 3]) for i in range(256)]
        self.transparent = self.pal.index((0, 255, 255))
        self.white = self.pal.index((255, 255, 255))
        self.pages = {}
        self.names = [e['name'] for e in self.pf.table]

    def page(self, n):
        if n not in self.pages:
            self.pages[n] = self.pf.page(self.pf.table[n - 1])
        return self.pages[n]

    # ---- item masks (spec 1.6): bit n = the first hotspot named "n_*" is enabled, else the first type-1 ornament "n_*" is shown
    @staticmethod
    def _prefixed(name, n):
        return name.startswith('%d_' % n)

    def mask(self, n, hidden=None):
        p = self.page(n)
        m = 0
        for item in range(32):
            hs = [h for h in p.hotspots if self._prefixed(h['name'], item)]
            if hs:
                if not (hs[0]['flags'] & 1):
                    m |= 1 << item
                continue
            os_ = [(i, o) for i, o in enumerate(p.ornaments) if o['type'] == 1 and self._prefixed(o['name'], item)]
            if os_:
                i, o = os_[0]
                fl = o['flags'] if hidden is None else hidden[i]
                if not (fl & 1):
                    m |= 1 << item
        return m

    # ---- painting
    def _surface(self, n, flags=None):
        p = self.page(n)
        w, h = p.width or 640, p.height or 480
        surf = [bytearray([self.transparent]) * w for _ in range(h)]
        for i, o in enumerate(p.ornaments):
            fl = o['flags'] if flags is None else flags[i]
            if o['type'] != 1 or (fl & 1):
                continue
            iw, ih, stride, raw = p.image_pixels(o['image'])      # bottom-up rows
            key = None
            if fl & 0x10:
                b0, b1, b2 = o['transp'][0], o['transp'][1], o['transp'][2]   # file bytes: B, G, R
                key = self.pal.index((b2, b1, b0))
            for y in range(ih):
                dy = o['y'] + y
                if not 0 <= dy < h:
                    continue
                src = raw[(ih - 1 - y) * stride:(ih - 1 - y) * stride + iw]
                x0 = o['x']
                lo, hi = max(0, -x0), min(iw, w - x0)
                if hi <= lo:
                    continue
                src = src[lo:hi]
                dst = surf[dy]
                if key is None:
                    dst[x0 + lo:x0 + hi] = src
                else:
                    seg = dst[x0 + lo:x0 + hi]
                    dst[x0 + lo:x0 + hi] = bytes(s if s != key else t for s, t in zip(src, seg))
        return surf

    def _blit(self, frame, surf, dx, dy):
        for y, row in enumerate(surf):
            if not 0 <= dy + y < 480:
                continue
            dst = frame[dy + y]
            seg = dst[dx:dx + len(row)]
            dst[dx:dx + len(row)] = bytes(s if s != self.transparent else t for s, t in zip(row, seg))

    def main_frame(self, n, flags=None):
        frame = [bytearray([self.white]) * 640 for _ in range(480)]
        self._blit(frame, self._surface(n, flags), 0, 0)
        return frame

    def panel_frame(self, main, panel, x, y):
        p = self.page(main)
        flags = [o['flags'] for o in p.ornaments]
        if self.mask(main) & (1 << 30):                       # verb 0x1a: the main page shows its items 30 and 31
            for i, o in enumerate(p.ornaments):
                if o['type'] == 1 and (self._prefixed(o['name'], 30) or self._prefixed(o['name'], 31)):
                    flags[i] &= ~1
        frame = [bytearray([self.white]) * 640 for _ in range(480)]
        self._blit(frame, self._surface(main, flags), 0, 0)
        self._blit(frame, self._surface(panel), x, y)
        return frame

    def opener(self, panel):
        """the page / position a panel is opened from: the first verb-0x1a hotspot (pages 1..45 in order) that names it, else tech at 442,72"""
        for n in range(1, 46):
            for h in self.page(n).hotspots:
                if h['action'][0] == 0x1a and h['action'][1] == panel:
                    return n, h['action'][2], h['action'][3], h['name']
        return self.names.index('tech') + 1, 442, 72, ''


def frame_diff(a_rows, b_rows):
    bad = 0
    first = None
    for y in range(480):
        if a_rows[y] != b_rows[y]:
            for x in range(640):
                if a_rows[y][x] != b_rows[y][x]:
                    bad += 1
                    if first is None:
                        first = (x, y, a_rows[y][x], b_rows[y][x])
    return bad, first
