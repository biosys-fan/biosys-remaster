#!/usr/bin/env python3
"""Helpers of tests/test_sim.sh (standard library only).
  compare CPP.txt PY.txt        every member the oracle prints equals the C++ dump to 1e-9 relative (ints exactly); every
                                member of the simulated sections is covered by the oracle
  props DUMP.txt                property checks on a dump (no NaN / inf, values inside the original's clamps)
  conserve DUMP water|air, cycle DUMP, plants BASE POLLUTED DRAINED, differs A B   see tests/test_sim.sh
"""
import math, re, sys

def parse_blocks(path):
    blocks, cur, label = [], None, None
    for line in open(path):
        line = line.rstrip('\n')
        if line.startswith('== '):
            cur = {}
            blocks.append((line, cur))
        elif ' = ' in line and cur is not None:
            k, v = line.split(' = ', 1)
            cur[k] = v
    return blocks

def num(v):
    v = v.strip()
    if v.startswith('0x'):
        return int(v, 16)
    try:
        return int(v)
    except ValueError:
        return float(v)

def same(a, b, tol=1e-9):
    if isinstance(a, int) and isinstance(b, int):
        return a == b
    a, b = float(a), float(b)
    if a == b:
        return True
    if math.isnan(a) or math.isnan(b):
        return False
    return abs(a - b) <= tol * max(abs(a), abs(b))

SIM_SECTIONS = ('world', 'cels', 'tanks', 'airflows', 'waterflows', 'plants', 'timeline')
# members of the C++ dump the oracle has no model for: the clock record's tick length (a constant of the harness), the
# C++-only bookkeeping lines
NOT_MODELLED = {'timeline/timeline/m_dwTicksPerGameSec', 'sim/world/blueSkyLo', 'sim/world/blueSkyHi',
                'sim/world/lookUpReportEnabled'}

def compare(cpp, py):
    A, B = parse_blocks(cpp), parse_blocks(py)
    if len(A) != len(B) or not A:
        print('FAIL: %d blocks in the C++ dump, %d in the oracle' % (len(A), len(B)))
        return 1
    bad = 0
    worst = 0.0
    checked = 0
    for (la, a), (lb, b) in zip(A, B):
        if la != lb:
            print('FAIL: block headers differ: %r vs %r' % (la, lb))
            return 1
        for k, vb in b.items():
            if k not in a:
                print('FAIL %s: %s missing in the C++ dump' % (la, k)); bad += 1; continue
            checked += 1
            if k.startswith('sim/log/'):
                if a[k] != vb:
                    bad += 1
                    print('FAIL %s: %s  C++ %r  oracle %r' % (la, k, a[k], vb))
                continue
            x, y = num(a[k]), num(vb)
            if not same(x, y):
                bad += 1
                if bad <= 25:
                    print('FAIL %s: %s  C++ %s  oracle %s' % (la, k, a[k], vb))
            elif isinstance(x, float) and x != y:
                worst = max(worst, abs(x - y) / max(abs(x), abs(y)))
        for k in a:
            sec = k.split('/')[0]
            if (sec in SIM_SECTIONS or k.startswith('sim/')) and k not in b and k not in NOT_MODELLED \
                    and not k.startswith('sim/airflow/') and not k.startswith('sim/cel/') and not (k.startswith('sim/log/') and k in b):
                bad += 1
                if bad <= 25:
                    print('FAIL %s: %s is in the C++ dump but not modelled by the oracle' % (la, k))
    print('%s: %d members compared in %d blocks, worst relative difference %.3g, %d failures' %
          ('PASS' if bad == 0 else 'FAIL', checked, len(A), worst, bad))
    return 1 if bad else 0

# value ranges the original clamps to (cels: CBiomeCel::UpdateState dll:10018530, tanks: CWaterTank::Commit
# dll:1000f020, ocean/plants/flows: their update functions)
RANGES = [
    (r'cels/[^/]+/m_dAirTemp$', -20, 50), (r'cels/[^/]+/m_dSoilTemp$', -20, 50),
    (r'cels/[^/]+/m_dHumidity$', 0, 100), (r'cels/[^/]+/m_dOxygen$', 0, 100), (r'cels/[^/]+/m_dCO2$', 0, 100),
    (r'cels/[^/]+/m_dPollution$', 0, 100), (r'cels/[^/]+/m_dLightLetThru$', 0, 1), (r'cels/[^/]+/m_dLight$', 0, 1),
    (r'tanks/[^/]+/m_dOxygen$', 0, 100), (r'tanks/[^/]+/m_dPH$', 3, 13), (r'tanks/[^/]+/m_dSalinity$', 0, 100),
    (r'tanks/[^/]+/m_dTemp$', -20, 50), (r'tanks/[^/]+/m_dPurity$', 0, 100),
    (r'tanks/ocean/m_dHealth$', 0, 1), (r'tanks/ocean/m_dAlgae$', 0, 100),
    (r'waterflows/[^/]+/m_dFreeFlow$', 0, 1),
    (r'plants/[^/]+/m_dShortTermHealth$', 0.3, 1), (r'plants/[^/]+/m_dLongTermHealth$', 0, 1),
    (r'plants/[^/]+/m_dHealth$', 0, 1),
    (r'world/state/m_dPower$', 0, 200), (r'world/state/m_dPowerDemand$', 0, 100),
]

def props(path):
    blocks = parse_blocks(path)
    bad = 0
    n = 0
    for label, d in blocks:
        for k, v in d.items():
            try:
                x = num(v)
            except ValueError:
                continue
            n += 1
            # a cel without plants keeps the constructor's +infinity / -infinity (CBiomeCel ctor dll:10017b90)
            if k.endswith('/m_dPlantHealthMin') and x == math.inf or k.endswith('/m_dPlantHealthMax') and x == -math.inf:
                continue
            if isinstance(x, float) and (math.isnan(x) or math.isinf(x)):
                bad += 1
                if bad <= 20: print('FAIL %s: %s = %s' % (label, k, v))
                continue
            for pat, lo, hi in RANGES:
                if re.fullmatch(pat, k) and not (lo <= x <= hi):
                    bad += 1
                    if bad <= 20: print('FAIL %s: %s = %s outside [%g, %g]' % (label, k, v, lo, hi))
    print('%s: %d values in %d dumps, %d failures' % ('PASS' if bad == 0 else 'FAIL', n, len(blocks), bad))
    return 1 if bad else 0

def elapsed(label):
    return int(re.search(r'elapsed=(\d+)', label).group(1))

def total(block, pat):
    return sum(float(v) for k, v in block.items() if re.fullmatch(pat, k))

def conserve(path, what):
    """what = water: the water of all tanks (every flow moves exactly what it takes), air: the air volume of all cels
    (the two-way flows move volume between neighbours); the totals of all later dumps equal the first one's"""
    blocks = parse_blocks(path)
    pat = r'tanks/[^/]+/m_dWaterVol' if what == 'water' else r'cels/[^/]+/m_dAirVolume'
    t0 = total(blocks[0][1], pat)
    bad = 0
    for label, b in blocks[1:]:
        t = total(b, pat)
        ok = abs(t - t0) <= 1e-9 * abs(t0)
        print('%s %s total at %s: %.12g (start %.12g)' % ('ok  ' if ok else 'FAIL', what, label, t, t0))
        bad += 0 if ok else 1
    return 1 if bad else 0

def cycle(path):
    """The cel PH (house, no plants, no soil, heat gain 0) starts 20 degrees above its surroundings with every flow off:
    (1) one weather step is the relaxation with the 12000 s time constant, the soil exchange being zero while the air
    and the soil have the same temperature; (2) over the days that follow the cel follows the outside temperature,
    damped and some hours late"""
    blocks = parse_blocks(path)
    g = lambda b, k: float(b[k])
    # blocks 0, 1, 2 are after 0, 125, 250 game seconds: the first weather step (dt = 125) is in the second cycle
    b1, b2 = blocks[1][1], blocks[2][1]
    ta1, ta2 = g(b1, 'cels/PH/m_dAirTemp'), g(b2, 'cels/PH/m_dAirTemp')
    ext2 = g(b2, 'world/state/m_dExtTemp')
    want = ta1 + (ext2 - ta1) * 125.0 / 12000.0
    ok1 = abs(ta2 - want) <= 1e-9 * abs(want) and ta2 < ta1
    print('%s one step: %.12f -> %.12f, relaxation towards %.4f with tau 12000 s gives %.12f' %
          ('ok  ' if ok1 else 'FAIL', ta1, ta2, ext2, want))
    series = []
    for label, b in blocks[3:]:
        t = int(re.search(r't=(\d+)', label).group(1))
        series.append((t, g(b, 'world/state/m_dExtTemp'), g(b, 'cels/PH/m_dAirTemp')))
    bad = 0 if ok1 else 1
    start = blocks[0][1] and int(re.search(r't=(\d+)', blocks[0][0]).group(1))
    for day in (1, 2):       # whole days after the first one (the transient of the 20 degree start)
        d = [(t % 86400 / 3600.0, e, a) for t, e, a in series if 86400 * day <= t - start < 86400 * (day + 1)]
        if not d:
            print('FAIL no samples of day %d' % day); bad += 1; continue
        ext_swing = max(e for _, e, _ in d) - min(e for _, e, _ in d)
        air_swing = max(a for _, _, a in d) - min(a for _, _, a in d)
        peak_ext = max(d, key=lambda x: x[1])[0]
        peak_air = max(d, key=lambda x: x[2])[0]
        lag = peak_air - peak_ext
        ok = air_swing < 0.8 * ext_swing and 1.0 <= lag <= 8.0
        print('%s day %d after the start: outside swing %.2f (peak %.1f h), cel swing %.2f (peak %.1f h), lag %.1f h' %
              ('ok  ' if ok else 'FAIL', day, ext_swing, peak_ext, air_swing, peak_air, lag))
        bad += 0 if ok else 1
    return 1 if bad else 0

def plants(base_path, polluted_path, drained_path, static_path):
    """healthy start state keeps its plants healthy for the first hour; heavy pollution (which also darkens the cels) and
    a drained soil make them lose health"""
    bad = 0
    def healths(b):
        return {k.split('/')[1]: float(v) for k, v in b.items() if re.fullmatch(r'plants/[^/]+/m_dHealth', k)}
    def longs(b):
        return {k.split('/')[1]: float(v) for k, v in b.items() if re.fullmatch(r'plants/[^/]+/m_dLongTermHealth', k)}
    def shorts(b):
        return {k.split('/')[1]: float(v) for k, v in b.items() if re.fullmatch(r'plants/[^/]+/m_dShortTermHealth', k)}
    base = parse_blocks(base_path)
    h0, h1 = healths(base[0][1]), healths(base[1][1])
    # the plants whose cel has soil water within their range at the start and at the end of the hour, and no pollution
    info = {}
    for line in open(static_path):
        f = line.rstrip('\n').split('\t')
        if f[0] == 'plant':
            d = dict(kv.split('=', 1) for kv in f[3:])
            info[f[1]] = (d['cel'], float(d['swmin']), float(d['swmax']), int(d['flags']))
    def good(n):
        cel, lo, hi, flags = info[n]
        if flags & 8:
            return False
        for b in (base[0][1], base[1][1]):
            sw = float(b['cels/%s/m_dSoilWater' % cel])
            if not lo <= sw <= hi or float(b['cels/%s/m_dPollution' % cel]) > 10:
                return False
        return True
    ok_plants = [n for n in h0 if good(n)]
    lost = [n for n in ok_plants if h1[n] < h0[n] - 1e-12]
    ok = not lost and len(ok_plants) >= 20
    print('%s good conditions: %d of %d plants have soil water in their range, none of them loses health in the first hour%s' %
          ('ok  ' if ok else 'FAIL', len(ok_plants), len(h0), '' if ok else ': ' + ','.join(lost)))
    bad += 0 if ok else 1
    for name, path in (('pollution 90 % (dark cels)', polluted_path), ('soil drained to 5 %', drained_path)):
        blocks = parse_blocks(path)
        s0, s1 = shorts(blocks[0][1]), shorts(blocks[-1][1])
        l0, l1 = longs(blocks[0][1]), longs(blocks[-1][1])
        live = [n for n in s0 if not n.startswith('ADB')]
        worse = [n for n in live if s1[n] < s0[n] - 1e-12 or l1[n] < l0[n] - 1e-12]
        ok = len(worse) >= len(live) // 2
        print('%s %s: %d of %d plants lose health' % ('ok  ' if ok else 'FAIL', name, len(worse), len(live)))
        bad += 0 if ok else 1
    return 1 if bad else 0

def differs(a, b):
    """different seeds give a different weather: the random targets of the weather differ after a day"""
    A, B = parse_blocks(a)[-1][1], parse_blocks(b)[-1][1]
    keys = [k for k in A if k.startswith('world/state/') and k.endswith('Target')]
    diff = [k for k in keys if A[k] != B[k]]
    print('%s %d of %d weather targets differ between the two seeds' % ('ok  ' if diff else 'FAIL', len(diff), len(keys)))
    return 0 if diff else 1

if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == 'conserve':
        sys.exit(conserve(sys.argv[2], sys.argv[3]))
    if len(sys.argv) == 3 and sys.argv[1] == 'cycle':
        sys.exit(cycle(sys.argv[2]))
    if len(sys.argv) == 6 and sys.argv[1] == 'plants':
        sys.exit(plants(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]))
    if len(sys.argv) == 4 and sys.argv[1] == 'differs':
        sys.exit(differs(sys.argv[2], sys.argv[3]))
    if len(sys.argv) == 4 and sys.argv[1] == 'compare':
        sys.exit(compare(sys.argv[2], sys.argv[3]))
    if len(sys.argv) == 3 and sys.argv[1] == 'props':
        sys.exit(props(sys.argv[2]))
    raise SystemExit(__doc__)
