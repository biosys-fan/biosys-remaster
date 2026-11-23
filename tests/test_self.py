#!/usr/bin/env python3
"""Helper of tests/test_self.sh: the member lines of biosys-playertest dump must equal the oracle's CSelf dump."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import sys
sys.path.insert(0, (_DEV + '/analysis/scripts'))
import biosys_savblob as ref

def main(cpp_dump, sav):
    d, off = ref.read_sav(sav)
    want = []
    for nm, items, pad in ref.parse_blob(d[off:]):
        if nm != 'self':
            continue
        for it in items:
            for f in it.fields:
                want.append('%s/%s/%s = %s' % (nm, it.key, f.name if f.name != '_pad' else '_pad@%#x' % f.off, f.show()))
    got = [l.rstrip('\n') for l in open(cpp_dump)]
    bad = 0
    for i, (a, b) in enumerate(zip(got + [''] * 2, want + [''] * 2)):
        if a != b:
            print('FAIL line %d\n  C++:    %s\n  oracle: %s' % (i, a, b)); bad += 1
            if bad > 5: break
    if len(got) != len(want):
        print('FAIL: %d lines, oracle has %d' % (len(got), len(want))); bad += 1
    if not bad:
        print('PASS: %d CSelf member lines equal the oracle' % len(got))
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main(*sys.argv[1:3]))
