#!/usr/bin/env python3
"""Tables of SETTINGS.md from build/settingsmeasure/<realism>_<scenario>_<seed>.tsv (written by biosys-settingsmeasure).
usage: python3 -I tests/settingsmeasure_tables.py [--days 10,30] [--scenarios idle,programme,frozen]"""
import glob, os, sys
args = sys.argv[1:]
days = [10, 30]; scen = ['idle', 'programme', 'frozen']
if '--days' in args: days = [int(x) for x in args[args.index('--days') + 1].split(',')]
if '--scenarios' in args: scen = args[args.index('--scenarios') + 1].split(',')
base = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build', 'settingsmeasure')

def load(path):
    rows = [l.rstrip('\n').split('\t') for l in open(path) if l.strip()]
    h = rows[0]
    return {int(r[2]): {h[i]: r[i] for i in range(len(h))} for r in rows[1:]}

def mean(vals):
    v = [float(x) for x in vals if x not in ('', None)]
    return sum(v) / len(v) if v else float('nan')

def g(x):
    return '%.4g' % x

cols = [('rainforest.humidity', 'RF hum'), ('rainforest.airTemp', 'RF T'), ('rainforest.o2', 'RF O2'), ('rainforest.plantsAlive', 'RF alive'),
        ('savannah.humidity', 'SAV hum'), ('savannah.o2', 'SAV O2'), ('deadbiome.o2', 'DB O2'), ('house.airTemp', 'house T'),
        ('ocean.health', 'ocean health'), ('plants.alive', 'plants alive'), ('plants.health', 'plant health')]
print('| realism | scenario | day | ' + ' | '.join(c[1] for c in cols) + ' |')
print('|---|---|---|' + '---|' * len(cols))
for sc in scen:
    for r in ['faithful', 'corrected', 'closedloop']:
        files = sorted(glob.glob(os.path.join(base, '%s_%s_*.tsv' % (r, sc))))
        if not files: continue
        data = [load(f) for f in files]
        for d in days:
            print('| %s | %s | %d | ' % (r, sc, d) + ' | '.join(g(mean([x[d][c[0]] for x in data])) for c in cols) + ' |')
