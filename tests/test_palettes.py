#!/usr/bin/env python3
"""Checks data/page_palettes.tsv and data/palette_sets.tsv against the game data."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os, struct, sys

PORT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
ROOTS = [(_DEV + '/setup-cd/installed/DATA1/Biodata'), (_DEV + '/game-cd/files/BIODATA')]

import re

# Files that the original tables name but that do not exist anywhere in the game data. Every entry
# is a (regex on the lower-case name with backslashes, reason). The DLL tables are used as they
# are; the game simply fails to load these (a failed load returns 0, e.g. dll:100604d0).
ALLOWED_MISSING = [
    (r'\.(xft|pal)(palettes|lookups)\\',
     'initializer typo in the DLL: a missing comma glued two names into one slot (sets 1, 19-24) and '
     'shifted the rest of the group by one'),
    (r'^(palettes|lookups)\\(misc\\(death|gameover)|peyote\\peyote2a?)\.(pal|xft)$',
     'sets 3..5 (peyote2, death, gameover) are not used by any location; files were never shipped'),
    (r'^lookups\\(lifts|angel|peyote|airpipes|savannah\\baobab)\\',
     'no cross-fade lookups shipped for lifts, angel, peyote, airpipes, baobab (palette-only sets)'),
    (r'^lookups\\house\\ph(9|13)\\',
     'PH9/PH13 ship only palettes (and PH9\\DAY.XFT)'),
    (r'^palettes\\lifts\\\w+\\(emergncy|\w+(cloud|gogs))\.pal$',
     'lift sets: emergency/cloud/goggles palettes of the lift sets were not shipped'),
]


def find_ci(root, rel):
    cur = root
    for part in rel.replace('\\', '/').split('/'):
        if not part:
            continue
        try:
            names = os.listdir(cur)
        except OSError:
            return None
        hit = [n for n in names if n.lower() == part.lower()]
        if not hit:
            return None
        cur = os.path.join(cur, hit[0])
    return cur


def exists(rel):
    return any(find_ci(r, rel) for r in ROOTS)


def tsv(path):
    out = []
    for ln in open(path, encoding='utf-8').read().split('\n'):
        if ln and not ln.startswith('#'):
            out.append(ln.split('\t'))
    return out


def page_names():
    for r in ROOTS:
        p = find_ci(r, 'PAGES.DAT')
        if p:
            d = open(p, 'rb').read()
            n = struct.unpack_from('<I', d, 0)[0]
            return [d[4 + 28 * i + 16:4 + 28 * i + 28].split(b'\0')[0].decode('latin-1') for i in range(n)]
    raise SystemExit('PAGES.DAT not found')


def main():
    errors = []
    sets = tsv(os.path.join(PORT, 'data', 'palette_sets.tsv'))
    rows = tsv(os.path.join(PORT, 'data', 'page_palettes.tsv'))
    pages = page_names()

    keys = set()
    for f in sets:
        if len(f) != 5:
            errors.append('palette_sets: bad row %r' % f); continue
        keys.add((int(f[0]), f[1], int(f[2])))
    nsets = max(k[0] for k in keys) + 1
    want = {(s, g, i) for s in range(nsets) for g in 'AB' for i in range(6)}
    if keys != want or len(sets) != len(want):
        errors.append('palette_sets.tsv does not hold exactly %d sets x 2 groups x 6 slots' % nsets)

    names = [r[0] for r in rows]
    if len(rows) != len(pages) or sorted(n.lower() for n in names) != sorted(p.lower() for p in pages):
        errors.append('page_palettes.tsv: %d rows for %d pages; missing=%s extra=%s' % (
            len(rows), len(pages),
            sorted(set(p.lower() for p in pages) - set(n.lower() for n in names))[:5],
            sorted(set(n.lower() for n in names) - set(p.lower() for p in pages))[:5]))
    if len(set(n.lower() for n in names)) != len(names):
        errors.append('page_palettes.tsv: duplicate page rows')

    kinds = {'ctor': 0, 'inherit': 0, 'fixed': 0, 'guess': 0}
    files = set()
    for f in rows:
        if len(f) != 4:
            errors.append('page_palettes: bad row %r' % f); continue
        page, s, fixed, src = f
        s = int(s)
        if not -1 <= s < nsets:
            errors.append('%s: set %d out of range' % (page, s))
        kind = src.split(':')[0]
        if kind not in kinds:
            errors.append('%s: unknown source %r' % (page, src))
        else:
            kinds[kind] += 1
        if s == -1 and not fixed and kind not in ('guess',):
            errors.append('%s: no palette at all' % page)
        if fixed:
            files.add(fixed.lower())
        if kind == 'inherit':
            par = src.split(':', 1)[1]
            pr = [r for r in rows if r[0].lower() == par.lower()]
            if not pr or (int(pr[0][1]), pr[0][2]) != (s, fixed):
                errors.append('%s: inherits %s but values differ' % (page, par))
    for f in sets:
        files.add(f[3].lower()); files.add(f[4].lower())
    files.discard('')

    missing = sorted(x for x in files if not exists(x))
    allowed = [x for x in missing if any(re.search(rx, x) for rx, _ in ALLOWED_MISSING)]
    unexpected = [x for x in missing if x not in allowed]
    for x in unexpected:
        errors.append('missing game file: ' + x)

    print('pages: %d rows (PAGES.DAT has %d); sets: %d' % (len(rows), len(pages), nsets))
    print('source kinds: ctor=%d inherit=%d fixed=%d guess=%d' % (kinds['ctor'], kinds['inherit'], kinds['fixed'], kinds['guess']))
    print('files checked: %d, missing but allow-listed: %d%s' % (len(files), len(allowed), ''))
    for rx, why in ALLOWED_MISSING:
        print('  allow %-70s %d  (%s)' % (rx, sum(1 for x in allowed if re.search(rx, x)), why))
    for e in errors:
        print('ERROR: ' + e)
    print('palette tables: ' + ('FAILED' if errors else 'ok'))
    return 1 if errors else 0


sys.exit(main())
