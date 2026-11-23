"""The sim-status window (API 0x4a..0x4e, 0x56, event 34) and the two player status screens drawn on it (CSelf::ShowStatus dll:10068f90,
CSelf::ShowEnvironmentStatus dll:1006aa80), through `build/biosys --game --script` with real key events: Tab shows the personal status and
Caps Lock the environment status while the key is held (Event_OnKeyDown dll:1001e6b0 shows, Event_OnKeyUp dll:10021790 closes).

Expectations come from independent code: the DLL's string table (analysis/out/dll_stringtable.txt), the player's stats (`game self`), and the
drawing arithmetic of the decompiled screens (positions, colours, scales) applied to the screenshot of the frame."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import math, os, re, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
sys.path.insert(0, os.path.join(ROOT, 'tests'))
from play_lib import Play

D = _DEV
STRINGS = D + '/analysis/out/dll_stringtable.txt'
BIODATA = D + '/game-cd/files/BIODATA'
T = 'build/test/status'
os.makedirs(T, exist_ok=True)
failed = 0


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:900] if detail else ''))


strings = {}
for l in open(STRINGS, errors='replace'):
    q = l.rstrip('\n').split('\t')
    if len(q) >= 3 and q[0].isdigit():
        strings[int(q[0])] = q[2].replace('\\n', '\n')


def S(id_):                      # the first column of the table is the resource id (decimal), the second the same in hex
    return strings[id_]


def game(script, *opts, seed=1):
    r = subprocess.run(['build/biosys', '--game', '--mute', '--seed', str(seed), '--save-dir', T + '/saves'] + list(opts) + ['--script', script],
                       capture_output=True, text=True, timeout=900)
    return r.returncode, r.stdout.splitlines(), r.stderr.splitlines()


def read_bmp(path):
    d = open(path, 'rb').read()
    off = struct.unpack('<I', d[10:14])[0]
    w, h = struct.unpack('<ii', d[18:26])
    pal = [tuple(d[54 + 4 * i:57 + 4 * i][::-1]) for i in range(256)]
    stride = (w + 3) // 4 * 4
    return [d[off + (h - 1 - y) * stride:][:w] for y in range(h)], pal


def nearest(pal, rgb):
    """the palette index GDI picks for PALETTERGB: squared distance, lowest index first"""
    best, bi = None, 0
    for i, c in enumerate(pal):
        d = sum((a - b) ** 2 for a, b in zip(c, rgb))
        if best is None or d < best:
            best, bi = d, i
    return bi


def colorref(c):                 # 0x02BBGGRR -> (r, g, b)
    return (c & 255, (c >> 8) & 255, (c >> 16) & 255)


def selfdump(lines):
    d = {}
    for l in lines:
        m = re.match(r'self\.(m_\w+) = (.*)$', l)
        if m:
            try:
                d[m.group(1)] = float(m.group(2))
            except ValueError:
                d[m.group(1)] = m.group(2)
    return d


def gamedump(lines):
    for l in lines:
        if l.startswith('game time='):
            return dict(t.split('=', 1) for t in l.split()[1:] if '=' in t)
    return {}


def texts(lines):
    return [(int(m.group(1)), int(m.group(2)), m.group(3)) for m in (re.match(r"ui text x=(-?\d+) y=(-?\d+) '(.*)'$", l) for l in lines) if m]


def trunc(v):
    return int(v)


SYMPTOM_IDS = {8: 0xcc, 1: 0xce, 2: 0xca, 4: 0xd7, 0x20: 0xd3, 0x10: 0xcb, 0x80: 0xd0, 0x40: 0xd4, 0x200: 0xd2, 0x100: 0xcd, 0x800: 0xd5, 0x400: 0xcf,
               0x2000: 0xd1, 0x1000: 0xd6, 0x8000: 0x158, 0x4000: 0x156, 0x10000: 0x161, 0x40000000: 0x14f}


def lt1(v):                       # "<1" below one, else the number: dll:1006a... FUN_1006c... (the text of the DLL: "<1")
    return '<1' if v < 1 else str(v)


def expected_status_texts(me, gd):
    """the texts CSelf::ShowStatus draws, (y or None, text), from the player's stats"""
    now = int(gd['time'])
    out = []
    out += [S(0x6a), S(0x6c), S(0x6b), S(0x6d)]
    phase = int(gd['phase'], 16)
    out += {5: [S(0x6b)], 0xf: [S(0x6b)], 0x14: [S(0x6c)], 0x1e: [S(0x6d)], 0x23: [S(0x6d)], 0x28: [S(0x6a)]}.get(phase, [])
    out += [S(0x68), S(0x75)]
    e = me['m_dEnergy']
    out.append(str(trunc(e)) if e <= 0 or e >= 1 else '1')
    if any(me.get(k, 0) for k in ('m_dwAdrenaline', 'm_dwCoca', 'm_dwEphedrineTimer', 'm_dwGuarana', 'm_dwCoffee')):
        out.append(S(0x160))
    if e < me['m_dWeightCarried']:
        out.append(S(0x69))
    h = me['m_dHealth']
    out.append(str(trunc(h)) if h >= 1 else '1')
    sy = int(me['m_dwSymptoms'])
    bit, y = 1, 0x85
    while sy and y <= 0xc4 and bit <= sy:
        if sy & bit:
            t = S(SYMPTOM_IDS[bit])
            if int(me['m_dwSymptomsCured']) & bit:
                t += S(0x15b)
            out.append(t)
            y += 0x10
        bit *= 2
    if int(me['m_dwLastDrank']):
        out.append(lt1((now - int(me['m_dwLastDrank'])) // 3600))
    if int(me['m_dwLastAte']) > 1:
        out.append(lt1((now - int(me['m_dwLastAte'])) // 3600))
    return out


def gauge_expect(me):
    """the gauges: (name, pen colour, kind, x0, y0, pixels): horizontal bars end at x0 + pixels, columns at y0 - pixels, dots at the end"""
    e, w, h, t = me['m_dEnergy'], me['m_dWeightCarried'], me['m_dHealth'], me['m_dTemperature']
    return [
        ('energy', colorref(0x023163ff), 'bar', 0x25, 0x51, trunc(e * 2)),
        ('weight', colorref(0x029c9c63), 'bar', 0x25, 0x68, trunc(w * 2)),
        ('health', colorref(0x0273efa5), 'bar', 0x192, 0x69, trunc(h * 2)),
        ('temperature', (255, 255, 255), 'dot', 0x11e, 0x109 - trunc(t * 2), 0),
        ('hydration', colorref(0x02ffce31), 'col', 0x196, 0x165, trunc(me['m_dHydration'])),
        ('nutrition', colorref(0x02319cce), 'col', 0x208, 0x165, trunc(me['m_dNutrition'])),
        ('protein', colorref(0x02ffcece), 'col', 0x27a, 0x165, trunc(me['m_dProtein'])),
    ]


def pen_pixels(g):
    """the pixels a gauge's pen paints: the line (the end point excluded) and the centre of the 5 x 5 dot"""
    name, col, kind, x0, y0, px = g
    pts = []
    if kind == 'bar':
        pts += [(x, y0) for x in range(x0, x0 + px)]
        pts.append((x0 + px, y0))
    elif kind == 'col':
        pts += [(x0, y) for y in range(y0, y0 - px, -1)]
        pts.append((x0, y0 - px))
    else:
        pts.append((x0, y0))
    return pts


# ---------------------------------------------------------------------------------------------------------------------------------
# the three states of the player: fresh, hungry and thirsty after 12 game hours (24 game seconds per second), poisoned (cyanide)
STATES = {
    'fresh': 'advance 3000',
    'hungry': 'advance 1000; advance 1800000',
    'poisoned': 'advance 1000; game consume 10; advance 400000',
}
results = {}
for name, prep in STATES.items():
    path = '%s/status_%s.bmp' % (T, name)
    rc, out, err = game('%s; game ui; echo @A; keydown TAB; advance 100; echo @B; game ui; dump simstatus; shot %s; pal; game self; game dump; '
                        'keyup TAB; advance 100; dump simstatus' % (prep, path), '--skip-intro', '--log-events')
    i = out.index('@A')
    j = out.index('@B')
    # (`game ui` prints and empties the record: the texts after @B are this screen's)
    results[name] = dict(rc=rc, out=out, err=err, texts=texts(out[j:]), me=selfdump(out[j:]), gd=gamedump(out[j:]), path=path,
                         sim=[l for l in out[j:] if l.startswith('simstatus ')])
    r = results[name]
    check('(%s) Tab (held) opens the window: it is up with its own palette, hotspots on, no transparent index; Tab released closes it' % name,
          rc == 0 and len(r['sim']) == 2 and 'up=1 hotspots=1 ownpalette=1 key=-1' in r['sim'][0] and 'up=0' in r['sim'][1], (rc, err[-2:], r['sim']))

for name, r in results.items():
    me, gd = r['me'], r['gd']
    exp = expected_status_texts(me, gd)
    got = [t for _, _, t in r['texts']]
    check('(%s) the drawn texts are the string-table strings and numbers of the player\'s state, in the order of the original (%d texts)' % (name, len(exp)),
          got == exp, (got, exp))
    ys = {t: y for _, y, t in r['texts']}
    check('(%s) their places: the day parts at (0x234, 0) (0x239, 0x3d) (0x25c, 0x1f) (0x20d, 0x1f), the buttons at (0x1bb, 3) (0x1be, 0x1d), the energy at y 0x47, the health at y 0x5f' % name,
          r['texts'][:4] == [(0x234, 0, S(0x6a)), (0x239, 0x3d, S(0x6c)), (0x25c, 0x1f, S(0x6b)), (0x20d, 0x1f, S(0x6d))] and
          [(x, y) for x, y, t in r['texts'] if t in (S(0x68), S(0x75))] == [(0x1bb, 3), (0x1be, 0x1d)], r['texts'][:8])
    rows, pal = read_bmp(r['path'])
    top = 56
    # bars: every pixel of the gauges' pens (line without its end, centre of the dot) carries the palette index of the pen colour
    bad = []
    for g in gauge_expect(me):
        want = nearest(pal, g[1])
        for (x, y) in pen_pixels(g):
            if 0 <= x < 640 and rows[top + y][x] != want:
                bad.append((g[0], x, y, rows[top + y][x], want))
    check('(%s) the seven bars (energy, weight, health, temperature dot, hydration, nutrition, protein columns) are drawn where the stats put them: '
          'line pixels and dot centres carry their pen colours' % name, not bad, bad[:5])
    # the bars of a different state end elsewhere: the pen-coloured pixels in the row beyond the dot are not the pen colour more than the background has
    # the time of day pie: a 60 degree sector around the angle of the time of day, brush 0x028cad9c
    tod = int(gd['tod'])
    a = tod * 7.272205324074074e-05
    brush = nearest(pal, colorref(0x028cad9c))

    def at(theta, rad):
        x = round(580 + rad * math.sin(theta))
        y = round(40 - rad * math.cos(theta))
        return rows[top + y][x]
    inside = [at(a + d, 9) for d in (-0.35, 0.0, 0.35)]
    outside = [at(a + d, 9) for d in (math.pi, math.pi + 0.5, math.pi - 0.5)]
    check('(%s) the pie: the sector (60 degrees) around the time of day (t = %d s) is the brush colour, the opposite side is not' % (name, tod),
          all(v == brush for v in inside) and all(v != brush for v in outside), (inside, outside, brush))

fresh, hungry, poisoned = results['fresh'], results['hungry'], results['poisoned']
check('(states) the three states differ the way they should: hydration and nutrition are 0 after 12 hours, the health is lower when poisoned and the poison shows as a symptom',
      hungry['me']['m_dHydration'] == 0 and hungry['me']['m_dNutrition'] == 0 and poisoned['me']['m_dwSymptoms'] != 0 and fresh['me']['m_dwSymptoms'] == 0 and
      poisoned['me']['m_dHealth'] < fresh['me']['m_dHealth'], (hungry['me']['m_dHydration'], poisoned['me']['m_dwSymptoms']))
check('(poisoned) the symptom (bit 5) is written on the screen at x 0x197 from y 0x85 as the string 0xd3 of the table',
      (0x197, 0x85, S(0xd3)) in poisoned['texts'], poisoned['texts'])

# differential check of the bars: the number of pen-coloured pixels in a gauge's row / column changes by exactly the change of the line's length
def count_pen(rows, pal, g, span):
    want = nearest(pal, g[1])
    name, col, kind, x0, y0, px = g
    if kind == 'bar':
        return sum(rows[56 + y0][x] == want for x in range(x0, min(640, x0 + span)))
    if kind == 'col':
        return sum(rows[56 + y][x0] == want for y in range(y0 - span, y0 + 1))
    return 0


rf, pf = read_bmp(fresh['path'])
rh, ph = read_bmp(hungry['path'])
rp, pp = read_bmp(poisoned['path'])
gf, gh, gp = gauge_expect(fresh['me']), gauge_expect(hungry['me']), gauge_expect(poisoned['me'])
bad = []
for gi, name in ((0, 'energy'), (2, 'health'), (4, 'hydration'), (5, 'nutrition')):
    for (ra, pa, ga), (rb, pb, gb) in (((rf, pf, gf[gi]), (rh, ph, gh[gi])), ((rf, pf, gf[gi]), (rp, pp, gp[gi]))):
        span = 100
        da = count_pen(ra, pa, ga, span) - count_pen(rb, pb, gb, span)
        dx = (ga[5] - gb[5]) if ga[2] == 'col' else (min(span, ga[5]) - min(span, gb[5]))
        if da != dx:
            bad.append((name, da, dx, ga[5], gb[5]))
check('(bars) the length of every bar follows the stat from state to state: the pen-coloured pixels of its row / column change by the change of the line', not bad, bad[:6])

# ---------------------------------------------------------------------------------------------------------------------------------
# the page is restored exactly when the key is released; the window covers the main view only
rc, out, err = game('advance 3000; echo @A; shot %s/before_tab.bmp; keydown TAB; advance 100; shot %s/tab.bmp; keyup TAB; advance 20; shot %s/after_tab.bmp' % (T, T, T), '--skip-intro')
a, _ = read_bmp(T + '/before_tab.bmp')
b, _ = read_bmp(T + '/tab.bmp')
c, _ = read_bmp(T + '/after_tab.bmp')
# the rain overlay of the raining start page (dll:10022530, colours 0x76 / 0x56 / 0x57) moves from frame to frame: pixels where either
# picture holds one of its colours are not counted
RAIN = (0x76, 0x56, 0x57)
dv = sum(a[y][x] != c[y][x] and a[y][x] not in RAIN and c[y][x] not in RAIN for y in range(480) for x in range(640))
check('(close) releasing Tab puts the page back (%d of 307200 pixels differ from before, rain drops apart; the page animates a little)' % dv, rc == 0 and dv < 2500, dv)
dview = sum(a[y][x] != b[y][x] for y in range(56, 424) for x in range(640))
check('(close) while the window is up it has replaced the main view (a different picture) and the view starts at y = 56', dview > 150000, dview)

# Escape (Event_OnKeyDown: the menu) closes it: OpenMenu hides the sim-status window first (exe:004468d0)
rc, out, err = game('advance 3000; keydown TAB; advance 100; dump simstatus; key ESC; advance 100; dump simstatus; dump menu', '--skip-intro')
ss = [l for l in out if l.startswith('simstatus ')]
check('(close) Escape opens the menu and the window is gone before it', rc == 0 and len(ss) == 2 and 'up=1' in ss[0] and 'up=0' in ss[1] and 'menu open' in out, (ss, out[-4:]))

# a page change closes it (every main page load hides it, exe:00401050)
rc, out, err = game('advance 3000; keydown TAB; advance 100; page R5; advance 100; dump simstatus', '--skip-intro')
ss = [l for l in out if l.startswith('simstatus ')]
check('(close) a page load puts the window away', rc == 0 and ss and 'up=0' in ss[-1], ss)

# ---------------------------------------------------------------------------------------------------------------------------------
# the hotspots: the sleep button starts the sleep (event 34: the game's OnSimStatusHotspot), the rest button rests
rc, out, err = game('advance 1000; advance 1800000; keydown TAB; advance 100; echo @pre; game self; echo @move; move 460,68; dump state; click 460,68; advance 3000; game self; dump simstatus',
                    '--skip-intro', '--log-events')
i = out.index('@pre')
j = out.index('@move')
pre = selfdump(out[i:j])
post = selfdump(out[j:])
ss = [l for l in out if l.startswith('simstatus ')]
curs = [l for l in out if l.startswith('cursor ')]
check('(sleep) over the sleep button the cursor is the window\'s hand (SIMHAND)', curs and curs[0].endswith('SIMHAND'), curs)
check('(sleep) clicking the sleep button fires event 34 with "sleep", the window closes and the player falls asleep (awake 1 -> 0)',
      rc == 0 and 'event simstatushotspot sleep' in out and pre['m_bAwake'] == 1 and post['m_bAwake'] == 0 and 'up=0' in ss[-1], (pre.get('m_bAwake'), post.get('m_bAwake'), ss))
# a press that starts outside the button and ends on it is not a click
rc, out, err = game('advance 1000; advance 1800000; keydown TAB; advance 100; move 200,200; echo @down; move 460,68; echo @end', '--skip-intro', '--log-events')
check('(sleep) no event without a click', rc == 0 and 'event simstatushotspot sleep' not in out)
rc, out, err = game('advance 3000; keydown TAB; advance 100; click 460,95; advance 500; dump simstatus; game self', '--skip-intro', '--log-events')
check('(rest) the rest button (y 29..55 of the window) fires event 34 with "rest"', rc == 0 and 'event simstatushotspot rest' in out, [l for l in out if 'simstatus' in l])

# the window takes the mouse: a click on the page under it does nothing, the right button is ignored
rc, out, err = game('advance 3000; keydown TAB; advance 100; echo @click; click 300,300; rclick 300,300; advance 100; dump state; dump simstatus', '--skip-intro', '--log-events')
evs = out[out.index('@click'):]
check('(modal) clicks and right clicks on the page under the status screen cause no hotspot / right-click events',
      rc == 0 and not any(l.startswith(('event hotspot', 'event rightclick', 'event event44')) for l in evs) and any('up=1' in l for l in evs), [l for l in evs if l.startswith('event')][:4])

# ---------------------------------------------------------------------------------------------------------------------------------
# what the engine suppresses while the window is up: pick-ups and the object keys (exe:00447f40 / 00448ac0 ...), the held object is not drawn
p = Play('R4')
p.advance(2000)
p.goto('L_R4R5', 'R5')
from play_lib import object_zones
page, hs = object_zones()['compass']
p.raw('echo @A', 'keydown TAB', 'advance 100', 'api DoHotspotAction -1 %s' % hs, 'advance 600', 'dump state', 'echo @B',
      'keyup TAB', 'advance 200', 'api DoHotspotAction -1 %s' % hs, 'advance 1500', 'dump state', 'echo @C')
rc, out, err = p.run(extra=['--skip-intro'])
sa = out[out.index('@A'):out.index('@B')]
sb = out[out.index('@B'):out.index('@C')]
check('(suppressed) the pick-up of an object (the drop-zone hotspot\'s verb, exe:0040d270 Dispatcher / exe:00447f40) is refused while the window is up (error 0xb), '
      'and works when it is not: the compass is in the hand', rc == 0 and 'api DoHotspotAction rc=11' in sa and 'held -' in sa and 'api DoHotspotAction rc=0' in sb and 'held compass' in sb,
      (sa[:3], sb[:3]))

# the held object is not drawn into the opaque window: the view is the window's picture alone
p2 = Play('R4')
p2.advance(2000)
p2.goto('L_R4R5', 'R5')
p2.raw('echo A', 'shot %s/_r5.bmp' % T)
p2.click_object('compass', wait=800)
p2.raw('keydown TAB', 'advance 100', 'shot %s/_held_tab.bmp' % T, 'keyup TAB', 'advance 100', 'dump state')
rc, out, err = p2.run(extra=['--skip-intro'])
held_ok = 'held compass' in out
p3 = Play('R4')
p3.advance(2000)
p3.goto('L_R4R5', 'R5')
p3.advance(800)
p3.raw('keydown TAB', 'advance 100', 'shot %s/_free_tab.bmp' % T, 'keyup TAB')
rc3, out3, err3 = p3.run(extra=['--skip-intro'])
x, _ = read_bmp(T + '/_held_tab.bmp')
y, _ = read_bmp(T + '/_free_tab.bmp')
dd = sum(x[r][c] != y[r][c] for r in range(56, 424) for c in range(640))
check('(suppressed) with the compass in the hand the status screen is the same picture as without (the held object is not drawn): %d of %d view pixels differ' % (dd, 368 * 640),
      rc == 0 and rc3 == 0 and held_ok and dd < 600, (held_ok, dd))

# ---------------------------------------------------------------------------------------------------------------------------------
# the environment screen: Caps Lock held, drawn over the page (palette index 54 transparent), no hotspots, no own palette
rc, out, err = game('advance 3000; game ui; echo @A; keydown CAPS; advance 100; echo @B; game ui; dump simstatus; shot %s/environment.bmp; pal; keyup CAPS; advance 100; dump simstatus; '
                    'shot %s/after_caps.bmp' % (T, T), '--skip-intro')
i, j = out.index('@A'), out.index('@B')
tx = texts(out[j:])[len(texts(out[:i])):]
ss = [l for l in out if l.startswith('simstatus ')]
check('(environment) Caps Lock shows the window laid over the page: up, no palette of its own, hotspots off, key index 54 (the transparent colour); the key release closes it',
      rc == 0 and len(ss) == 2 and 'up=1 hotspots=0 ownpalette=0 key=54' in ss[0] and 'up=0' in ss[1], ss)
known = {S(i_) for i_ in range(0x45, 0x5a)} | {S(i_) for i_ in (0x11b, 0x11d, 0x121, 0xc3, 0x59, 0x5b, 0x5c, 0x5d, 0x66, 0x67, 0x65, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xc9, 0xc8, 0x15e, 0x15f)}
unknown = [t for t in tx if t[2] not in known and not re.match(r'^-?\d+%$', t[2]) and not re.match(r'^\d+$', t[2])]
check('(environment) the texts are the table\'s strings (the biome, air, soil, wind, pollution, humidity) and a percentage, at the screen\'s columns (x 0x206 = 518, right edge 0x27a)',
      rc == 0 and tx and not unknown and any(x == 0x206 for x, y, t in tx) and tx[0][0] + 0 > 0, (unknown, tx))
ys = [y for x, y, t in tx if x == 0x206]
check('(environment) the biome name sits at y 0x31, then air at 0xe2, soil at 0xf4, wind at 0x106, pollution at 0x118, humidity at 0x12a',
      ys == [0x31, 0xe2, 0xf4, 0x106, 0x118, 0x12a], ys)
a, _ = read_bmp(T + '/before_tab.bmp')
b, pb = read_bmp(T + '/environment.bmp')
c, _ = read_bmp(T + '/after_caps.bmp')
vis = sum(a[y][x] != b[y][x] for y in range(56, 424) for x in range(640))
check('(environment) the page shows through the window (most of the view is still the page: %d of %d pixels changed), no pixel of index 54 is on the screen' % (vis, 368 * 640),
      0 < vis < 368 * 640 * 0.3 and sum(r_.count(54) for r_ in b) < 300, vis)

# the biome's health gauge (FUN_10009b10): the mean plant health of the biome's cels, 0..100 %, never the -100 % of a missing value
rc, out, err = game('advance 3000; keydown CAPS; advance 100; game ui; game dump', '--skip-intro')
pct = [t for x, y, t in texts(out) if re.match(r'^-?\d+%$', t)]
avg = [float(m.group(1)) for l in out for m in [re.search(r'plantavg=(\S+)', l)] if m]
check('(environment) the biome health shown (%s) is the plant health of the biome, 0..100 %% (the rainforest at the start: plantavg %s)' % (pct, avg),
      rc == 0 and pct and avg and 0 <= int(pct[0][:-1]) <= 100 and abs(int(pct[0][:-1]) - round(avg[0] * 100)) <= 12, (pct, avg))

print('FAILED %d' % failed if failed else 'ALL PASSED')
sys.exit(1 if failed else 0)
