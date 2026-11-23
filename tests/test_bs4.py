#!/usr/bin/env python3
"""BioSys 4 terminal (src/engine/bs4*.cpp) tests: static pictures against the independent oracle, the built-in checks, a navigation
session with real clicks, the ported panel driven by the real game logic. Frames: build/shots/bs4_*.bmp"""
import math
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bs4_oracle  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'build', 'biosys-bs4test')
OUT = os.path.join(ROOT, 'build', 'test', 'bs4')
SHOTS = os.path.join(ROOT, 'build', 'shots')
os.makedirs(OUT, exist_ok=True)
os.makedirs(SHOTS, exist_ok=True)

fails = 0


def check(ok, what, detail=''):
    global fails
    if ok:
        print('PASS ' + what)
    else:
        fails += 1
        print('FAIL ' + what + (': ' + detail if detail else ''))


def run(script, *args):
    p = subprocess.run([BIN] + list(args), input=script, capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


oracle = bs4_oracle.Oracle()

# ---------------------------------------------------------------------------------------------------------------- 1. static pictures
rc, out, err = run('static %s\n' % OUT, '--static')
check(rc == 0, 'biosys-bs4test --static writes the pages', err)
bad_pages = []
n_main = n_panel = 0
for n in range(1, 112):
    if n <= 45:
        w, h, rows = bs4_oracle.read_bmp(os.path.join(OUT, 'main_%03d.bmp' % n))
        want = oracle.main_frame(n)
        n_main += 1
    else:
        w, h, rows = bs4_oracle.read_bmp(os.path.join(OUT, 'panel_%03d.bmp' % n))
        main, x, y, name = oracle.opener(n)
        meta = open(os.path.join(OUT, 'panel_%03d.txt' % n)).read().strip()
        if meta != 'main=%d x=%d y=%d name=%s' % (main, x, y, name):
            bad_pages.append('%s: opener %r vs %r' % (oracle.names[n - 1], meta, (main, x, y, name)))
        want = oracle.panel_frame(main, n, x, y)
        n_panel += 1
    bad, first = bs4_oracle.frame_diff(want, rows)
    if bad:
        bad_pages.append('%s: %d pixels differ, first %r' % (oracle.names[n - 1], bad, first))
check(not bad_pages and n_main == 45 and n_panel == 66,
      'static picture: %d main pages and %d panel pages (over their main page) match the oracle pixel for pixel' % (n_main, n_panel),
      '; '.join(bad_pages[:5]))

masks = {}
for line in open(os.path.join(OUT, 'masks.txt')):
    name, m = line.split()
    masks[name] = int(m, 16)
bad = [oracle.names[n - 1] for n in range(1, 46) if masks[oracle.names[n - 1]] != oracle.mask(n)]
check(not bad and sum(1 for v in masks.values() if v) > 10,
      'visible item mask of all 45 main pages after load matches the oracle (%d pages with a non-zero mask)' % sum(1 for v in masks.values() if v),
      ', '.join(bad))

# ---------------------------------------------------------------------------------------------------------------- 2. built-in checks
rc, out, err = run('selftest\n')
lines = out.splitlines()
npass = sum(1 for l in lines if l.startswith('PASS'))
nfail = [l for l in lines if l.startswith('FAIL')]
for l in nfail:
    print(l)
check(rc == 0 and npass >= 20 and not nfail, 'built-in checks (verb table, compose order, item masks, save round trip, slider geometry, panels): %d passed' % npass)
if lines:
    print('   ' + lines[-1])

# ---------------------------------------------------------------------------------------------------------------- 3. navigation
class Sim:
    """the initial state of a page's hotspots, to choose click points like a user would"""

    def __init__(self, n):
        self.p = oracle.page(n)
        self.en = [not (h['flags'] & 1) for h in self.p.hotspots]

    def top_at(self, x, y):
        for i in range(len(self.p.hotspots) - 1, -1, -1):
            l, t, r, b = self.p.hotspots[i]['rect']
            if self.en[i] and l <= x <= r and t <= y <= b:
                return i
        return -1

    def point(self, hi):
        l, t, r, b = self.p.hotspots[hi]['rect']
        cx, cy = (l + r) // 2, (t + b) // 2
        if self.top_at(cx, cy) == hi:
            return cx, cy
        for y in range(t, b + 1):
            for x in range(l, r + 1):
                if self.top_at(x, y) == hi:
                    return x, y
        return None

    def apply17(self, h):
        a = h['action']
        for n in range(31):
            for i, hh in enumerate(self.p.hotspots):
                if hh['name'].startswith('%d_' % n):
                    if (a[1] >> n) & 1:
                        self.en[i] = True
                    if (a[4] >> n) & 1:
                        self.en[i] = False


names = oracle.names
start = names.index('menuscreen') + 1


def edges(n):
    s = Sim(n)
    out = []
    for i, h in enumerate(s.p.hotspots):
        if s.en[i] and h['action'][0] == 0x12:
            pt = s.point(i)
            if pt:
                t = h['action'][1]
                if t == names.index('email1') + 1:
                    t = names.index('emailpw') + 1
                if t == names.index('synth1') + 1:
                    t = names.index('synthpw') + 1
                out.append((t, pt))
    return out


# breadth first search over the main pages with the hotspots that are enabled when a page is loaded
paths = {start: []}
queue = [start]
while queue:
    n = queue.pop(0)
    for t, pt in edges(n):
        if t not in paths and 1 <= t <= 45:
            paths[t] = paths[n] + [(pt, t)]
            queue.append(t)
targets = sorted(paths, key=lambda t: (len(paths[t]), t))[:16]
script = ['load menuscreen']
expect = []
for t in targets:
    script.append('load menuscreen')
    for (x, y), nt in paths[t]:
        script.append('click %d %d' % (x, y))
        script.append('state')
        expect.append(nt)
    script.append('shot %s/bs4_nav_%02d_%s.bmp' % (SHOTS, t, names[t - 1]))
rc, out, err = run('\n'.join(script) + '\n')
states = [l for l in out.splitlines() if l.startswith('state')]
got = [int(re.search(r' no=(\d+) ', l).group(1)) for l in states]
check(rc == 0 and got == expect and len(set(targets)) >= 10,
      'navigation with real clicks: %d main pages reached through %d clicks' % (len(set(targets)), len(expect)),
      'expected %r got %r' % (expect[:8], got[:8]))

# panels opened by real clicks: first the hotspot that enables the item group (verb 0x17), then the hotspot that opens the panel (0x1a)
panel_script = []
panel_expect = []
seen = set()
for n in sorted(paths):
    s = Sim(n)
    for hi, h in enumerate(s.p.hotspots):
        if h['action'][0] != 0x1a or s.en[hi] or h['action'][1] in seen:
            continue
        m = re.match(r'(\d+)_', h['name'])
        if not m:
            continue
        item = int(m.group(1))
        for gi, g in enumerate(s.p.hotspots):
            if g['action'][0] == 0x17 and s.en[gi] and (g['action'][1] >> item) & 1:
                s2 = Sim(n)
                gp = s2.point(gi)
                s2.apply17(g)
                hp = s2.point(hi) if s2.en[hi] else None
                if gp and hp:
                    seen.add(h['action'][1])
                    panel_script += ['load %s' % names[n - 1], 'click %d %d' % gp, 'click %d %d' % hp, 'state',
                                     'shot %s/bs4_panel_%s_%s.bmp' % (SHOTS, names[n - 1], names[h['action'][1] - 1])]
                    panel_expect.append((names[h['action'][1] - 1], h['name'], h['action'][2], h['action'][3]))
                break
rc, out, err = run('\n'.join(panel_script) + '\n')
pstates = [l for l in out.splitlines() if l.startswith('state')]
ok = len(pstates) == len(panel_expect)
wrong = []
for l, (pn, opener, x, y) in zip(pstates, panel_expect):
    m = re.search(r' panel=(\S+) opener=(\S*) ', l)
    if not m or m.group(1) != pn or m.group(2) != opener:
        wrong.append((pn, opener, l[:100]))
check(ok and not wrong and len(panel_expect) >= 10,
      'panel pages opened by real clicks (item group, then the 0x1a hotspot): %d panels' % len(panel_expect), repr(wrong[:3]))

spec = ['specah', 'speccfan', 'specrain', 'specsump', 'specpod', 'specwind', 'specterm']
spec_script = []
for s_ in spec:
    spec_script += ['load tech', 'panel %s 442 72' % s_, 'state', 'shot %s/bs4_%s.bmp' % (SHOTS, s_)]
rc, out, err = run('\n'.join(spec_script) + '\n')
sstates = [l for l in out.splitlines() if l.startswith('state')]
check(rc == 0 and len(sstates) == len(spec) and all((' panel=%s ' % s_) in l for s_, l in zip(spec, sstates)),
      '%d spec* panels (verb 0x1a at 442,72) open over the tech page' % len(spec))

# ---------------------------------------------------------------------------------------------------------------- 4. real game logic
script = 'load tech\nstate\nget - 0x3f3\nget - 0x3f4\nget - 0x3f2\nshot %s/bs4_game_tech.bmp\n' % SHOTS
rc, out, err = run(script, '--game', '--clock', 'Day 1 12:00')
st = [l for l in out.splitlines() if l.startswith('state')][0]
gets = [float(re.search(r'dbl=(\S+)', l).group(1)) for l in out.splitlines() if l.startswith('get')]
gauges = dict((int(a), (float(b), int(c))) for a, b, c in re.findall(r'(\d):([\d.]+)/(\d+)', st))
ok = len(gauges) == 3
msg = ''
for i, v in enumerate(gets):          # reserve 0x3f3, consumption 0x3f4, generation 0x3f2
    v = min(max(v, 0.0), 200.0)
    pct = int(((v - 0.0) * 100.0 / 200.0) * 151.0 * 0.01)
    if i not in gauges or abs(gauges[i][0] - v) > 0.051 or gauges[i][1] != pct:
        ok = False
        msg += ' gauge %d: %r vs value %r pct %d;' % (i, gauges.get(i), v, pct)
# the bars on the screen: the first `pct` columns are the bar image, the rest of the 151 columns is cleared
w, h, rows = bs4_oracle.read_bmp(os.path.join(SHOTS, 'bs4_game_tech.bmp'))
p = oracle.page(names.index('tech') + 1)
for gi, (nm, clear) in enumerate([('reservebar', oracle.white), ('powconbar', oracle.pal.index((206, 206, 206))),
                                  ('powgenbar', oracle.pal.index((206, 206, 206)))]):
    oi = [i for i, o in enumerate(p.ornaments) if o['name'] == nm][0]
    o = p.ornaments[oi]
    iw, ih, stride, raw = p.image_pixels(o['image'])
    pct = gauges[gi][1] if gi in gauges else 0
    for r in range(4):
        src = raw[(ih - 1 - r) * stride:(ih - 1 - r) * stride + iw]
        for c in range(151):
            want = src[c] if c < pct else clear
            if rows[o['y'] + r][o['x'] + c] != want:
                ok = False
                msg += ' %s bar pixel (%d,%d);' % (nm, c, r)
                break
check(ok, 'main page gauges show the values of the real game logic (WORLD_POWER_* 0x3f3 / 0x3f4 / 0x3f2) as %s' % ', '.join(
    '%.1f' % gauges[i][0] for i in sorted(gauges)), msg)

script = '\n'.join(['hook-ah', 'load pollut', 'clickh techah', 'clickh 5_ahs2', 'state', 'mask panel', 'get fs6htch 10', 'events',
                    'shot %s/bs4_game_panelah.bmp' % SHOTS])
rc, out, err = run(script + '\n', '--game')
m = re.search(r'mask panel (0x[0-9a-f]+)', out)
v = int(re.search(r'get fs6htch 10 = ok int=(-?\d+)', out).group(1))
pm = int(m.group(1), 16) if m else 0
check('panel=panelah' in out and ((pm >> 17) & 1) == (1 if v else 0) and ((pm >> 18) & 1) == (0 if v else 1) and 'highlight=hiahs1/5' in out,
      'panelah (ported, exe:00424ca0) with the real game logic: FEATURE_OPEN of fs6htch = %d is shown as item %s' % (v, '0x11 (open)' if v else '0x12 (closed)'),
      out[-300:])

script = '\n'.join(['load watpur', 'clickh techalg', 'clickh 7_algo1', 'get fbo3algae 1', 'clickh 2_turnon', 'get fbo3algae 1', 'events',
                    'clickh 1_turnoff', 'get fbo3algae 1', 'events', 'state'])
rc, out, err = run(script + '\n', '--game')
vals = [int(x) for x in re.findall(r'get fbo3algae 1 = ok int=(-?\d+)', out)]
ev = [l for l in out.splitlines() if l.startswith('set ')]
check(vals == [0, 1, 0] and ev == ['set fbo3algae 1 1 0 0', 'set fbo3algae 1 0 0 0'] and 'panel=panelalgs' in out,
      'a real click on the panel control sends set FEATURE_ON (event 26) and the real game logic applies it (0 -> 1 -> 0)',
      'values %r events %r' % (vals, ev))

# ---------------------------------------------------------------------------------------------------------------- 5. timer, keys
script = '\n'.join(['set - 0x3f3 0 150.0', 'set - 0x3f4 0 20.0', 'set - 0x3f2 0 60.0', 'set - 0x41a 0 0', 'load tech', 'state',
                    'set - 0x3f3 0 50.0', 'advance 400', 'state', 'advance 1200', 'state', 'logs'])
rc, out, err = run(script + '\n')
st = [l for l in out.splitlines() if l.startswith('state')]
g = [re.search(r'gauges=(\S+)', l).group(1) for l in st]
check(len(st) == 3 and g[0].startswith('0:150.0/113,1:20.0/15,2:60.0/45') and g[1] == g[0] and g[2].startswith('0:50.0/37,'),
      'the once-per-second tick polls the gauge parameters (150 -> 50 shows after 1 s, not after 400 ms): %r' % (g,))
script = '\n'.join(['load menuscreen', 'key 13', 'state', 'textentry', 'type ab1', 'key 8', 'key 8', 'text', 'key 40', 'type XYZ', 'text',
                    'type 12345678901234567890', 'text'])
rc, out, err = run(script + '\n')
texts = re.findall(r'text \[(.*)\] entry=1', out)
check('page=tech' in out and texts == ['', 'XYZ', 'XYZ1234567890123'],
      'keys: Enter on menuscreen opens tech; in text entry mode letters / digits / space are added, Backspace deletes, 16 characters at most', repr(texts))

print('ALL OK' if not fails else '%d FAILED' % fails)
sys.exit(1 if fails else 0)
