#!/usr/bin/env python3 -I
"""Runs build/biosys-fixmeasure for the faithful game, every fix alone and the presets (seeds 1..5, 30 game days), keeps the
TSVs in build/fixmeasure/ and prints the comparison tables of FIXES.md.  usage: python3 -I tests/fixmeasure_compare.py [--days N] [--rain]"""
import csv, os, subprocess, sys, re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'build', 'biosys-fixmeasure')
OUT = os.path.join(ROOT, 'build', 'fixmeasure')
SEEDS = [1, 2, 3, 4, 5]
DAYS = 30
DAYSHOW = [1, 3, 10, 30]
RAIN = '--rain' in sys.argv
if '--days' in sys.argv:
    DAYS = int(sys.argv[sys.argv.index('--days') + 1])

def flags():
    """(name, tier) from fixes.h"""
    src = open(os.path.join(ROOT, 'src', 'game', 'fixes.h')).read()
    return re.findall(r'^\s*X\((\w+), (\w+),', src, re.M)

def run(spec, seed):
    path = os.path.join(OUT, '%s%s_s%d.tsv' % (spec.replace(',', '+'), '_rain' if RAIN else '', seed))
    out = subprocess.run([BIN, '--seed', str(seed), '--days', str(DAYS), '--fixes', spec] + (['--rain'] if RAIN else []), capture_output=True, text=True, check=True).stdout
    open(path, 'w').write(out)
    rows = list(csv.reader(out.splitlines(), delimiter='\t'))
    h = rows[0]
    return h, {int(r[2]): dict(zip(h, r)) for r in rows[1:]}

def num(x):
    try: return float(x)
    except ValueError: return None

def compare(base, fixed, day):
    """per numeric column: mean over seeds of |fixed - faithful|; returns list of (column, mean faithful, mean fixed, scale-normalised delta)"""
    res = []
    cols = [c for c in base[SEEDS[0]][day] if c not in ('seed', 'fixes', 'day')]
    for c in cols:
        b = [num(base[s][day][c]) for s in SEEDS]
        f = [num(fixed[s][day][c]) for s in SEEDS]
        if any(v is None for v in b + f):
            continue
        mb, mf = sum(b) / len(b), sum(f) / len(f)
        d = sum(abs(x - y) for x, y in zip(b, f)) / len(b)
        scale = max(max(abs(num(base[s][dd][c]) or 0.0) for s in SEEDS for dd in base[s]), 1e-9)
        res.append((c, mb, mf, d, d / scale))
    return res

def deaths(data):
    return [data[s][DAYS]['player.deathTime'] for s in SEEDS]

def main():
    os.makedirs(OUT, exist_ok=True)
    base = {}
    for s in SEEDS:
        h, d = run('faithful', s)
        base[s] = d
    specs = [(n, t) for n, t in flags()] + [('safe', 'preset'), ('cosmetic', 'preset'), ('restored', 'preset')]
    print('scenario: ' + ('all rain machines on' if RAIN else 'idle'))
    print('faithful death time (s): ' + ', '.join(deaths(base)))
    print()
    print('| fix | tier | ' + ' | '.join('day %d' % d for d in DAYSHOW) + ' |')
    print('|---|---|' + '---|' * len(DAYSHOW))
    for name, tier in specs:
        data = {}
        for s in SEEDS:
            data[s] = run(name, s)[1]
        cells = []
        for day in DAYSHOW:
            res = compare(base, data, day)
            changed = [r for r in res if r[3] > 1e-9]
            if not changed:
                cells.append('identical')
                continue
            changed.sort(key=lambda r: -r[4])
            cells.append('%d cols; ' % len(changed) + '; '.join('%s %.4g -> %.4g' % (r[0], r[1], r[2]) for r in changed[:3]))

        dt = deaths(data)
        extra = '' if dt == deaths(base) else ' death: ' + ', '.join(dt)
        print('| %s | %s | %s |%s' % (name, tier, ' | '.join(cells), extra))

main()
