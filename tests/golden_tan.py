#!/usr/bin/env python3
"""Canonical TAN dump from the reference Python parser (same format as `biosys-tandump info`)."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import importlib.util, sys, zlib

spec = importlib.util.spec_from_file_location('biosys_tan', (_DEV + '/analysis/scripts/biosys_tan.py'))
bt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bt)

def main():
    out = sys.stdout
    for path in sys.argv[1:]:
        t = bt.Tan(path)
        fs = t.stride * t.height
        out.write('%s frames=%d size=%dx%d transp=%d unk0c=0x%x table=%d physical=%d\n' % (
            path, t.n_frames, t.width, t.height, t.save_bg, t.unk0c, 100 if t.header_size == 0x5c8 else 200,
            len(set(t.offsets))))
        for i, o in enumerate(t.offsets):
            if o + fs > t.size:
                out.write('frame %d off=0x%x MISSING\n' % (i, o))
                continue
            raw = t.frame(i)
            rows = [raw[y * t.stride:y * t.stride + t.width] for y in range(t.height)]
            if t.bi_height > 0:
                rows.reverse()
            out.write('frame %d off=0x%x crc=%08x\n' % (i, o, zlib.crc32(b''.join(rows)) & 0xffffffff))

main()
