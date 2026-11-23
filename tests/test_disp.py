"""Display tests. Each page is entered right after start-up (no time passes: the machines show the values of NewGame.sav), the
recorded texts (`game ui`: every string a display draws through GdiSession is recorded) are compared with the strings the
decompiled display functions compute from the saved values: formats ("%0.1f", "%0.2f", "%0.3f", "%d"), the y positions and the
right alignment (the x of a number is its right edge minus its width: x <= right edge, width = right - x). The bars and the text
are also checked on a screenshot: pixels of the amber ink exist inside the panel (the display really drew)."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import json, os, re, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
D = _DEV
SAV = D + '/setup-cd/installed/DATA1/NewGame.sav'
EVENTS = D + '/analysis/work/sb/events.jsonl'
STR = D + '/analysis/out/dll_stringtable.txt'
failed = 0


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:900] if detail else ''))


# ---- the saved game: every member of the dump ----------------------------------------------------------------------------
dump = {}
for l in subprocess.run(['build/biosys-gametest', 'dump', SAV], capture_output=True, text=True).stdout.splitlines():
    k, _, v = l.partition(' = ')
    dump[k] = v


def sv(key):
    return float(dump[key])


# ---- the builder: tank capacities, the cabinets' tanks -------------------------------------------------------------------
def dbl(lo, hi):
    return struct.unpack('<d', struct.pack('<II', lo & 0xffffffff, hi & 0xffffffff))[0]


cap, cabinets = {}, {}
for l in open(EVENTS):
    e = json.loads(l)
    if e[0] == 'tank' and e[2] == 'CWaterTank__CWaterTank':
        a = e[3]
        cap[a[0].upper()] = dbl(a[2], a[3])
    if e[0] == 'tank' and e[2] == 'COcean__COcean':
        a = e[3]
        cap['OCEAN'] = dbl(a[2], a[3])
    if e[0] == 'feature' and e[2] == 'FUN_10004620':
        cabinets[e[3][0].lower()] = [t.upper() for t in e[3][3:7]]

strings = {}
for l in open(STR):
    p = l.rstrip('\n').split('\t')
    if len(p) >= 3 and p[1].startswith('0x'):
        strings[int(p[1], 16)] = p[2]


def tank(name):
    n = name.lower()
    return dict(temp=sv('tanks/%s/m_dTemp' % n if ('tanks/%s/m_dTemp' % n) in dump else 'tanks/%s/m_dTemp' % name),
                purity=sv('tanks/%s/m_dPurity' % name), vol=sv('tanks/%s/m_dWaterVol' % name), cap=cap[name.upper()])


def tk(name):
    for k in (name, name.lower(), name.upper()):
        if 'tanks/%s/m_dTemp' % k in dump:
            return dict(temp=sv('tanks/%s/m_dTemp' % k), purity=sv('tanks/%s/m_dPurity' % k), vol=sv('tanks/%s/m_dWaterVol' % k),
                        cap=cap[name.upper()])
    raise KeyError(name)


def run(script, extra=()):
    r = subprocess.run(['build/biosys', '--game', '--mute', '--seed', '1'] + list(extra) + ['--script', script],
                       capture_output=True, text=True, timeout=300)
    if r.returncode != 0:
        print(r.stderr[-500:])
    texts, others = [], []
    for l in r.stdout.splitlines():
        m = re.match(r"ui text x=(-?\d+) y=(-?\d+) '(.*)'$", l)
        if m:
            texts.append((int(m.group(1)), int(m.group(2)), m.group(3)))
        elif l.startswith('ui '):
            others.append(l)
    return r.returncode, texts, others, r.stdout


def display(page):
    """the texts of the first display of the page (the page-entered display; a page-drawn one may follow with the same texts)"""
    rc, texts, others, out = run('page %s; game ui' % page)
    return rc, texts


def expect(name, page, rows):
    """rows: (right edge, y, text) in drawing order. The texts of the first display must start with these."""
    rc, texts = display(page)
    ok = rc == 0 and len(texts) >= len(rows)
    detail = texts
    if ok:
        for (right, y, text), (x, ty, tt) in zip(rows, texts):
            width = right - x
            if tt != text or ty != y or not (0 < width < 90):
                ok = False
                detail = 'want %r got %r (right-x=%d)' % ((right, y, text), (x, ty, tt), width)
                break
    check(name, ok, detail)
    return texts


def pct(v):
    return v * 100.0


# ================================================================ CBiomeSensor (fr4probe: the cel of R4)
s = lambda k: sv('features/fr4probe/' + k)
expect('sensor panel FR4probe_o: 12 numbers (air / soil temperature, light, wind, rain, humidity, pressure, pollution, CO2, oxygen, soil water, power)',
       'FR4probe_o',
       [(0x17b, 0x8b, '%0.2f' % s('m_dAirTempReading')), (0x1fe, 0x8b, '%0.2f' % s('m_dSoilTempReading')),
        (0x58, 0x135, '%d' % int(s('m_dLightReading'))), (0x10e, 0x135, '%0.1f' % s('m_dWindSpeedReading')),
        (0xab, 0x135, '%0.1f' % 0.0),   # rainfall today: not saved in the readings; the builder's value times 1/360 (0 for R4)
        (0xf6, 0xa4, '%0.1f' % s('m_dHumidityReading')), (0x12c, 0x7e, '%d' % int(s('m_dPressureReading'))),
        (0xd3, 0x7e, '%0.1f' % s('m_dPollutionReading')), (0x13a, 0xa4, '%0.3f' % s('m_dCO2Reading')),
        (0x19e, 0xa4, '%0.2f' % s('m_dOxygenReading')), (0x22c, 0xa4, '%0.1f' % s('m_dSoilWaterReading')),
        (0x226, 0x120, '%d' % int(sv('world/state/m_dPower')))])

# ================================================================ CHeater (the rainforest heater)
h = lambda k: sv('features/frfheater/' + k)
rc, texts = display('fRFheater')
ok = rc == 0 and len(texts) >= 5
if ok:
    ok = [(y, t) for x, y, t in texts[1:5]] == [(0x93, '%0.1f' % h('m_dDayOn')), (0x93, '%0.1f' % h('m_dDayOff')),
                                               (0x109, '%0.1f' % h('m_dNightOn')), (0x109, '%0.1f' % h('m_dNightOff'))] and texts[0][1] == 0x30
    ok = ok and [right - x for (x, y, t), right in zip(texts[:5], (0x146, 0x109, 0x19a, 0x109, 0x19a))] == \
        [right - x for (x, y, t), right in zip(texts[:5], (0x146, 0x109, 0x19a, 0x109, 0x19a))] and all(0 < r - x < 60 for (x, y, t), r in zip(texts[:5], (0x146, 0x109, 0x19a, 0x109, 0x19a)))
    avg = float(texts[0][2])
check('heater page fRFheater: probe temperature + the four set-points (10016e70)', ok, texts)

# ================================================================ CTanksCabinet (dll:100047b0)
def cabinet_rows(name, last):
    t = [tk(n) for n in cabinets[name]]
    fill = lambda x: x['vol'] / x['cap'] * 100.0
    pur = lambda x: x['purity'] * 100.0
    if last == '1':
        return [(0xc4, 0x8f, '%0.2f' % t[0]['temp']), (0x1be, 0x120, '%0.2f' % t[1]['temp']),
                (0x1c4, 0x8f, '%0.1f' % fill(t[0])), (0xca, 0x120, '%0.1f' % fill(t[1])),
                (0x147, 0x8f, '%0.1f' % pur(t[0])), (0x147, 0x120, '%0.1f' % pur(t[1]))]
    if name == 'fbptanks':      # page "fbptanks_2": the second variant of dll:100047b0 (strcmpi equal)
        return [(0xcb, 0x9d, '%0.2f' % t[2]['temp']), (0x1b6, 0x9d, '%0.1f' % fill(t[2])), (0x146, 0x125, '%0.1f' % fill(t[3])),
                (0x146, 0x9d, '%0.1f' % pur(t[2])), (0xe0, 0x125, '%0.1f' % pur(t[3]))]
    return [(0x9f, 0x94, '%0.2f' % t[2]['temp']), (0x18a, 0x94, '%0.1f' % fill(t[2])), (0x11a, 0x11c, '%0.1f' % fill(t[3])),
            (0x11a, 0x94, '%0.1f' % pur(t[2])), (0xb4, 0x11c, '%0.1f' % pur(t[3]))]


expect('cabinet page fbotanks_1 (dll:100047b0 case 1): 2 temperatures, 2 fill levels, 2 purities', 'fbotanks_1', cabinet_rows('fbotanks', '1'))
expect('cabinet page fbptanks_2 (the page named "fbptanks_2")', 'fbptanks_2', cabinet_rows('fbptanks', '2'))
expect('cabinet page fbotanks_2 (any other page ending in 2)', 'fbotanks_2', cabinet_rows('fbotanks', '2'))

# ================================================================ CAlgaeScrubber (dll:100037e0), CTideMachine (dll:10003f70)
expect('algae scrubber page: 100 - the ocean\'s algae, "%0.1f"', 'fbo3algae', [(0x15a, 0xa1, '%0.1f' % (100.0 - sv('tanks/ocean/m_dAlgae')))])


def clamp01(x):
    return 0.0 if x < 0 else 1.0 if x > 1 else x


tide, ocean = tk('TIDE'), tk('OCEAN') if False else dict(vol=sv('tanks/ocean/m_dWaterVol'), cap=cap['OCEAN'])
tf = clamp01(tide['vol'] / tide['cap']) * 100.0
of = clamp01(ocean['vol'] / ocean['cap']) * 100.0
mode = int(dump['features/fbo1Tide/m_bActive']) if 'features/fbo1Tide/m_bActive' in dump else 0
dirn = int(dump['features/fbo1Tide/unk_60']) if 'features/fbo1Tide/unk_60' in dump else 0
state_id = 0x143 if mode == 0 else (0x142 if dirn == 0 else 0x141)
rc, texts = display('fbo1tide')
check('tide page: two percentages ("%d") and the state text of the string table',
      rc == 0 and [(y, t) for x, y, t in texts[:3]] == [(0xa2, '%d' % int(tf)), (0xa2, '%d' % int(of)), (0xda, strings[state_id])] and
      texts[2][0] == 0x11b, texts)

# ================================================================ COxygenStore (dll:10025580)
store = sv('features/fbo4oxygen/m_dStore')
oxy = sv('cels/TO/m_dOxygen')
expect('oxygen store page: the store x 10 and the cel\'s oxygen, "%0.2f"', 'fbo4oxygen',
       [(0xbb, 0x6e, '%0.2f' % (store * 10.0)), (0xbb, 0xe4, '%0.2f' % oxy)])

# ================================================================ CLung (dll:100102f0): five numbers in the order of the display
rc, texts = display('fbo1lung')
fmts = ['%0.2f', '%0.1f', '%0.1f', '%0.3f', '%0.2f']
ys = [0xb1, 0xb1, 0xb1, 0xf5, 0xf5]
ok = rc == 0 and len(texts) >= 5 and [(y, bool(re.fullmatch(r'-?\d+\.\d{%s}' % f[-2], t))) for (x, y, t), f in zip(texts[:5], fmts)] == \
    [(y, True) for y in ys]
check('lung page: five numbers (air temperature, cel air volume %, pollution, CO2, oxygen) with the formats and y of dll:100102f0', ok, texts)

# ================================================================ the pod console (dll:1001b800)
rc, texts = display('bpod')
check('pod console: the destination message is string 0x14a (the rainforest needs no password), at (0xcf, 0x114)',
      rc == 0 and texts[:1] == [(0xcf, 0x114, strings[0x14a])], texts)

# ================================================================ pixels: the displays really drew into the page
def bmp_indices(path):
    d = open(path, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    assert bpp == 8
    row = (w + 3) & ~3
    return w, abs(h), [d[off + y * row: off + y * row + w] for y in range(abs(h))], h > 0


shots = [('fbo4oxygen', 'oxygen'), ('fbo1lung', 'lung'), ('fbptanks_2', 'tanks_2'), ('fbo3algae', 'algae'),
         ('FR4probe_o', 'sensor'), ('fRFheater', 'heater')]
for page, tag in shots:
    path = 'build/test/disp/%s.bmp' % tag
    off = 'build/test/disp/%s_off.bmp' % tag
    rc, _, _, out = run('advance 2000; page %s; advance 4000; shot %s' % (page, path))
    ok = rc == 0 and os.path.exists(path) and os.path.getsize(path) > 10000
    if ok:   # the page is on the screen: it is not black (fbo1tide is: the page renders black even without the game logic)
        w, hh, rows, _ = bmp_indices(path)
        ok = sum(1 for r in rows for b in r if b) > 20000
    check('screenshot build/test/disp/%s.bmp (%s)' % (tag, page), ok)

print('FAILED %d' % failed if failed else 'ALL OK')
sys.exit(1 if failed else 0)
