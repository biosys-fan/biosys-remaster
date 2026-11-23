#!/usr/bin/env python3
"""Independent oracle of the smooth (high-resolution) panorama presentation.

Written from the formulas of the original's projection (analysis/notes/projection.md section 3, tests/projection_oracle.py) and
from the conventions stated in src/engine/panomesh.h, not from the C++ mesh code:

 * Camera: taken from the scroll position the way the original does it (yaw = offset + scroll x, pitch = gain * (scroll y - 328)
   in 1/1024 degree clamped to the limit, focal length F = max(320 / tan(fov / 2), 184 / tan(0.8985))), computed here with
   projection_oracle's setup (exe:00445a70), not read from the C++.
 * Exact cylinder (no 32-pixel grid, no 16.16 fixed point, no truncation of the vertical plane coordinate, no seam quirk):
   view point (x, y) in 640 x 368 view pixels (pixel n covers [n, n+1)), c = x - 320.5, r = y - 184.5,
   D = F cos p - r sin p, u = atan2(c, D) * R + yaw, v = (r cos p + F sin p) * R / sqrt(c^2 + D^2) + 512, R = 2560 / (2 pi).
 * Page pixel i covers [i, i+1) in u (the original takes floor(u)); bilinear filtering between the pixel centres, x wraps modulo
   2560, y clamps to the page; the page through the palette in 8-bit RGB; the result is rounded half up.
Pure standard library; run with `python3 -I`.
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import projection_oracle as po

R = 2560.0 / (2.0 * math.pi)
PAGE_W, PAGE_H = 2560, 1024


def camera(x, y, fov=75.0):
    """(yaw, pitch_deg, focal) of the original's view at scroll (x, y)."""
    p = po.Projection(fov)
    yaw = p.yaw_offset + x
    if yaw > 0x500:
        yaw -= 0xA00
    if yaw <= -0x500:
        yaw += 0xA00
    pitch_in = p.pitch_step * (y - 0x148)
    scale, maxp = po._scale_and_maxpitch((p.fov_int >> 10) * 1.0)
    pitch = pitch_in / 1024.0
    pitch = min(max(pitch, -maxp), maxp)
    return float(yaw), pitch, scale


def page_point(cam, x, y):
    """exact page point (u, v) shown at the continuous view point (x, y), u unwrapped"""
    yaw, pitch, F = cam
    c = x - 320.5
    r = y - 184.5
    pr = pitch * 0.017453292519943295
    cs, sn = math.cos(pr), math.sin(pr)
    D = F * cs - r * sn
    return math.atan2(c, D) * R + yaw, (r * cs + F * sn) * R / math.sqrt(c * c + D * D) + 512.0


class Sampler:
    """bilinear sampler of the page (indices, 2560 x 1024) through a palette ([(r, g, b)] * 256)"""

    def __init__(self, surf, pal):
        self.surf = surf
        self.pal = pal

    def at(self, u, v):
        sx = u - 0.5
        sy = v - 0.5
        i0 = math.floor(sx)
        j0 = math.floor(sy)
        fx = sx - i0
        fy = sy - j0
        xa = i0 % PAGE_W
        xb = (i0 + 1) % PAGE_W
        ya = min(max(j0, 0), PAGE_H - 1)
        yb = min(max(j0 + 1, 0), PAGE_H - 1)
        s, pal = self.surf, self.pal
        p00 = pal[s[ya * PAGE_W + xa]]
        p10 = pal[s[ya * PAGE_W + xb]]
        p01 = pal[s[yb * PAGE_W + xa]]
        p11 = pal[s[yb * PAGE_W + xb]]
        out = []
        for k in range(3):
            top = p00[k] * (1 - fx) + p10[k] * fx
            bot = p01[k] * (1 - fx) + p11[k] * fx
            out.append(min(255, int(math.floor(top * (1 - fy) + bot * fy + 0.5))))
        return out

    def pixel(self, cam, X, Y, scale):
        """output pixel (X, Y) of the view rendered at `scale`"""
        u, v = page_point(cam, (X + 0.5) / scale, (Y + 0.5) / scale)
        return self.at(u, v)


def read_bmp24(path):
    """(w, h, rows) with rows[y] = bytes of w * 3 (RGB order)"""
    d = open(path, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    assert struct.unpack_from('<H', d, 28)[0] == 24
    st = (w * 3 + 3) & ~3
    rows = []
    for y in range(h):
        row = d[off + (h - 1 - y) * st: off + (h - 1 - y) * st + w * 3]
        b = bytearray(w * 3)
        b[0::3] = row[2::3]
        b[1::3] = row[1::3]
        b[2::3] = row[0::3]
        rows.append(bytes(b))
    return w, h, rows


def read_bmp8(path):
    """(w, h, palette [(r, g, b)], rows of indices)"""
    d = open(path, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    pal = [(d[54 + 4 * i + 2], d[54 + 4 * i + 1], d[54 + 4 * i]) for i in range(256)]
    st = (w + 3) & ~3
    rows = [d[off + (h - 1 - y) * st: off + (h - 1 - y) * st + w] for y in range(h)]
    return w, h, pal, rows


def write_bmp24(path, w, h, rows):
    """rows[y] = bytes of w * 3 in RGB order"""
    st = (w * 3 + 3) & ~3
    out = bytearray(b'BM' + struct.pack('<IHHI', 54 + st * h, 0, 0, 54) + struct.pack('<IiiHHIIiiII', 40, w, h, 1, 24, 0, st * h, 0, 0, 0, 0))
    for y in range(h - 1, -1, -1):
        r = rows[y]
        b = bytearray(st)
        b[0:w * 3:3] = r[2::3]
        b[1:w * 3:3] = r[1::3]
        b[2:w * 3:3] = r[0::3]
        out += b
    open(path, 'wb').write(out)


# ---------------------------------------------------------------------------------------------------------------------------------
# widescreen (src/engine/panowide.*): the same exact cylinder continued beside the 640-column view, written from the description in
# panowide.h (same focal length, so the horizontal field of view grows), not from the C++.

WIDE_CAP_DEG = 110.0     # the widest horizontal field of view drawn


def wide_pixel(S, cam, X, Y, scale, left, view_top=56):
    """canvas pixel (X, Y) of a wide capture whose 640 * scale picture starts at column `left`; the view starts at frame row `view_top`"""
    u, v = page_point(cam, (X - left + 0.5) / scale, (Y - view_top * scale + 0.5) / scale)
    return S.at(u, v)


def _u_extreme(cam, x, leftmost):
    """leftmost / rightmost page u over the rows of the view at view x (the extremes are at the two edge rows: u is monotonic in the row)"""
    us = [page_point(cam, x, y)[0] for y in (0.0, 368.0)]
    return min(us) if leftmost else max(us)


def wide_extent(cam, scale, avail_left, avail_right, limits=None):
    """the extra output columns each side the wide view shows: min(window, field-of-view cap, scroll limits), found by bisection on the exact
    mapping (not the closed form of the C++). limits: (yaw_min, yaw_max) of API 0x5a plus the yaw offset, or None."""
    yaw, pitch, F = cam
    cap_x = F * math.tan(math.radians(WIDE_CAP_DEG / 2))          # half width of the plane at the cap
    best = min(avail_left, avail_right, int(math.floor((cap_x - 320.5) * scale + 1e-9)), int(math.floor((cap_x - 319.5) * scale + 1e-9)))
    if limits is not None:
        ymin, ymax = limits
        allowed_l = ymin + (_u_extreme(cam, 0.0, True) - yaw)       # the original view with its centre at the limit
        allowed_r = ymax + (_u_extreme(cam, 640.0, False) - yaw)
        if yaw < ymin or yaw > ymax:
            return 0

        def ok(e):
            return (yaw + (_u_extreme(cam, -e / scale, True) - yaw) >= allowed_l - 1e-9 and
                    yaw + (_u_extreme(cam, 640.0 + e / scale, False) - yaw) <= allowed_r + 1e-9)
        lo, hi = 0, max(best, 0)
        if not ok(hi):
            while lo < hi:
                mid = (lo + hi + 1) // 2
                if ok(mid):
                    lo = mid
                else:
                    hi = mid - 1
            best = lo
    return max(best, 0)
