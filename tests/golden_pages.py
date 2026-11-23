#!/usr/bin/env python3
"""Canonical page dump from the reference Python parser (same format as `biosys-dump dump`)."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import importlib.util, sys, zlib

spec = importlib.util.spec_from_file_location('biosys_pages', (_DEV + '/analysis/scripts/biosys_pages.py'))
bp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bp)

def norm_pixels(p, i):
    """Top-down, unpadded pixels. image_pixels() returns bottom-up rows."""
    w, h, stride, raw = p.image_pixels(i)
    rows = [raw[y * stride:y * stride + w] for y in range(h - 1, -1, -1)]
    return b''.join(rows)

def main():
    pf = bp.PageFile(sys.argv[1])
    out = sys.stdout
    for e in pf.table:
        p = pf.page(e)
        out.write('page %d %s flags=0x%x size=%dx%d startx=%d bg=%d\n' %
                  (e['num'], p.name, p.flags, p.width, p.height, p.start_x, p.background))
        for i, s in enumerate(p.files):
            out.write('file %d %s\n' % (i, s))
        for i, o in enumerate(p.ornaments):
            b, g, r = o['transp']  # the file stores B,G,R; the reference parser names them in file order
            out.write('orn %d %s type=%d flags=0x%x pos=%d,%d image=%d transp=%d,%d,%d\n' %
                      (i, o['name'], o['type'], o['flags'], o['x'], o['y'], o['image'], r, g, b))
        for i, h in enumerate(p.hotspots):
            a = h['action']
            out.write('hot %d %s rect=%d,%d,%d,%d flags=0x%x button=%d verb=%d args=%s\n' %
                      (i, h['name'], *h['rect'], h['flags'], h['button'], a[0], ','.join(str(x) for x in a[1:])))
        for i, t in enumerate(p.timers):
            import struct
            w = struct.unpack_from('<13i', t, 0)
            out.write('timer %d %s delay=%d verb=%d args=%s\n' %
                      (i, bp.cstr(t[0x34:]), w[1], w[4], ','.join(str(x) for x in w[5:13])))
        for i, x in enumerate(p.xfades):
            out.write('xfade %d %s steps=%d\n' % (i, x['name'], x['words'][1]))
        for im in p.images:
            px = norm_pixels(p, im['index'])
            out.write('img %d %dx%d comp=%d crc=%08x\n' %
                      (im['index'], im['width'], im['height'], im['compressed'], zlib.crc32(px) & 0xffffffff))

main()
